#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>


class EventLoop;

//channel理解为通道 封装了sockfd和其感兴趣的event如 epoll_in epoll_out事件
//还绑定了 poller返回的具体事件
//eventloop包含channelList 和poller
//对应 demultiplex
class Channel:noncopyable
{
public:
	using EventCallback = std::function<void()>;
	using ReadEventCallback = std::function<void(Timestamp)>;

	Channel(EventLoop* loop, int fd);
	~Channel();

	//fd得到poller通知后，处理事件的
	void handleEvent(Timestamp receiveTime);

	//设置回调
	void setReadCallback(ReadEventCallback cb) {readCallback_ = std::move(cb);}
	void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
	void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
	void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

	//防止当channel被手动remove掉，channel还在执行回调
	void tie(const std::shared_ptr<void>&);

	int fd() const { return fd_; }
	int events() const { return events_; }
	//给后边的Poller来设置事件
	void set_revents(int revt) { revents_ = revt; }

	//update相当于epoll_ctl	设置fd相应的事件状态
	void enableReading() { events_ |= KReadEvent; update(); }
	void disableReading() { events_ &= KReadEvent; update(); }
	void enableWriting() { events_ |= KReadEvent; update(); }
	void disableWriting() { events_ &= ~KReadEvent; update(); }
	void disableAll() { events_ &= KNoneEvent; update(); }

	//返回fd当前的事件状态
	bool isNoneEvent() const { return events_ == KNoneEvent; }
	bool isWriting() const { return events_ & KWriteEvent; }
	bool isReading() const { return events_ & KReadEvent; }

	int index() { return index_; }
	void set_index(int idx) { index_ = idx; }
	
	//one loop per thread
	EventLoop* ownerLoop() { return loop_; }
	//在channel所属的eventloop中，把当前channel删除
	void remove();
private:
	//改变fd的events_后，update负责在poller更改fd相应的事件(在eventloop中实现)
	void update();
	void handleEventWithGuard(Timestamp receiveTime);

	static const int KNoneEvent;
	static const int KReadEvent;
	static const int KWriteEvent;

	EventLoop* loop_;	//事件循环
	const int fd_;	//fd	Poller监听的对象
	int events_;	//注册fd感兴趣的事件
	int revents_;	//poller返回的具体发生的事件
	int index_;		//表示在Poller中的状态

	std::weak_ptr<void> tie_;	
	bool tied_;

	//因为channel能够获得fd发生的具体事件revents，所以他负责调用具体事件的具体回调
	ReadEventCallback readCallback_;
	EventCallback writeCallback_;
	EventCallback closeCallback_;
	EventCallback errorCallback_;
};