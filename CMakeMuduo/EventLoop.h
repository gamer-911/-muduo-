#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <unistd.h>
#include <memory>
#include <mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;

//事件循环类 主要包含了两个大模块 Channel和Poller（epoll）
class EventLoop : noncopyable
{
public:
	using Functor = std::function<void()>;

	EventLoop();
	~EventLoop();
	
	//开启事件循环
	void loop();
	//退出事件循环
	void quit();

	Timestamp pollReturnTime() const { return pollReturnTime_; }

	//在当前loop中执行cb
	void runInLoop(Functor cb);
	//把cb放入队列中，唤醒loop所在的线程，执行cb
	void queueInLoop(Functor cb);

	//唤醒loop所在的线程
	void wakeup();

	//其实是调用的Poller的方法
	void updateChannel(Channel* channel);
	void removeChannel(Channel* channel);
	bool hasChannel(Channel* channel);

	//判断eventloop对象是否在自己的线程里面
	bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
	//处理wakeup
	void handleRead();
	//执行mainloop注册的回调 处理mainloop分配的新的channel
	void doPendingFunctors();

	using ChannelList = std::vector<Channel*>;

	std::atomic_bool looping_;	//原子操作，通过CAS实现
	std::atomic_bool quit_;		//标识退出loop循环

	const pid_t threadId_;	//记录当前loop的所在线程id
	Timestamp pollReturnTime_;	//poller返回发生事件的channels的时间点
	std::unique_ptr<Poller> poller_;
	
	int wakeupFd_;	//当mainloop获取一个新用户的channel，通过轮询算法选择一个subloop，通过该成员唤醒subloop处理channel
	std::unique_ptr<Channel> wakeupChannel_;

	ChannelList activeChannels_;

	std::atomic_bool callingPendingFunctors_;	//标识当前loop是否有需要执行的回调操作
	std::vector<Functor> pendingFunctors_;	//存储loop需要执行的所有回调操作
	std::mutex mutex_;	//互斥锁用来保护上面vector的线程安全操作
};