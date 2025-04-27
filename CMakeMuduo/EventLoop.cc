#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

//防止一个线程创建多个Eventloop
__thread EventLoop* t_loopInThisThread = 0;

//定义默认的poller超时时间
const int kPollTimeMs = 10000;

//创建wakeupfd，用来唤醒subreactor来处理新来的channel
int createEventfd()
{
	int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (evtfd < 0)
	{
		LOG_FATAL("eventfd err:%d\n", errno); 
	}
	return evtfd;
}

EventLoop::EventLoop():looping_(false), quit_(false), callingPendingFunctors_(false), threadId_(CurrentThread::tid()),
						poller_(Poller::newDefaultPoller(this)), wakeupFd_(createEventfd()), wakeupChannel_(new Channel(this, wakeupFd_))
{
	LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
	if (t_loopInThisThread)
	{
		LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_loopInThisThread, threadId_);
	}
	else
	{
		t_loopInThisThread = this;
	}

	//设置wakeupfd的事件类型以及发生事件后的回调操作
	wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
	//每一个Eventloop都将监听wakeupchannel的EPOLLIN读事件
	wakeupChannel_->enableReading();
}


EventLoop::~EventLoop()
{
	wakeupChannel_->disableAll();
	wakeupChannel_->remove();
	::close(wakeupFd_);
	t_loopInThisThread = nullptr;
}


void EventLoop::handleRead()
{
	uint64_t one = 1;
	ssize_t n = read(wakeupFd_, &one, sizeof one);
	if (n != sizeof one)
	{
		LOG_ERROR("EventLoop::handleRead() reads %ld bytes instead of 8\n", n);
	}
}


//开启事件循环
void EventLoop::loop()
{
	looping_ = true;
	quit_ = false;

	LOG_INFO("EventLoop %p start loopling\n", this);

	while (!quit_)
	{
		activeChannels_.clear();
		//监听两类fd， 一种是clientfd 一种是wakeupfd
		pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
		
		for (Channel* channel : activeChannels_)
		{
			//poller监听哪些channel发生事件了，然后上报给eventloop
			//通知channel处理相应的事件
			channel->handleEvent(pollReturnTime_);
		}
		
		doPendingFunctors();
	}
	LOG_INFO("EventLoop %p stop looping\n", this);
	looping_ = false;
}


//退出事件循环		1.loop在自己的线程中调用quit		2.在非loop的线程中调用loop的quit 
void EventLoop::quit()
{
	quit_ = true;

	//如果是在其他线程中调用的quit,唤醒阻塞
	if (!isInLoopThread())	
		wakeup();
}

//在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
	if (isInLoopThread())	//在当前的loop线程中执行回调
	{
		cb();
	}
	else //在非当前loop线程中,就需要唤醒loop所在线程执行回调
	{
		queueInLoop(std::move(cb));
	}
}

//把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
	{
		std::unique_lock<std::mutex> lock(mutex_);
		pendingFunctors_.emplace_back(cb);
	}

	//唤醒相应的需要执行上面回调操作的loop线程
	//这里的callingPendingFunctors_表示正在执行回调，没阻塞在poll上,但是loop又有了新的回调，防止这一轮阻塞在poll上
	if (!isInLoopThread() || callingPendingFunctors_)	
	{
		wakeup();
	}
}


//唤醒loop所在的线程 向wakeupfd写一个数据,wakeup就发生读事件,当前线程就会被poll处唤醒
void EventLoop::wakeup()
{
	uint64_t one = 1;
	ssize_t n = ::write(wakeupFd_, &one, sizeof one);
	if (n != sizeof one)
	{
		LOG_ERROR("EventLoop::wakeup() reads %ld bytes instead of 8\n", n);
	}
}

//其实是调用的Poller的方法
void EventLoop::updateChannel(Channel* channel)
{
	poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
	poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
	return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors()
{
	std::vector<Functor> functors;
	callingPendingFunctors_ = true;

	{
		std::unique_lock<std::mutex> lock(mutex_);
		functors.swap(pendingFunctors_);
	}

	for (const Functor& functor : functors)
	{
		functor();
	}

	callingPendingFunctors_ = false;
}
