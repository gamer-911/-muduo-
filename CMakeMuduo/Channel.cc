#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>

const int Channel::KNoneEvent = 0;
const int Channel::KReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::KWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
	:loop_(loop),
	fd_(fd),
	events_(0),
	revents_(0),
	index_(-1),
	tied_(false)
{}

Channel::~Channel()
{
}

//一个Tcpconnection新连接创建的时候调用，Tcpconnection底层绑定管理了一个channel
void Channel::tie(const std::shared_ptr<void>& obj)
{
	tie_ = obj;
	tied_ = true;
}

void Channel::update()
{
	loop_->updateChannel(this);
}

void Channel::remove()
{
	loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
	if (tied_)
	{
		std::shared_ptr<void> guard = tie_.lock();
		if (guard)
		{
			handleEventWithGuard(receiveTime);
		}
	}
	else
	{
		handleEventWithGuard(receiveTime);
	}
}

//根据poller通知的channel发生的具体事件，由channel调用回调函数
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
	LOG_INFO("channel handleEvent revents:%d\n", revents_);

	if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
	{
		if(closeCallback_)
			closeCallback_();
	}

	if (revents_ & EPOLLERR)
	{
		if (errorCallback_)
			errorCallback_();
	}

	if (revents_ & EPOLLIN)
	{
		if (readCallback_)
			readCallback_(receiveTime);
	}

	if (revents_ & EPOLLOUT)
	{
		if (writeCallback_)
			writeCallback_();
	}
}