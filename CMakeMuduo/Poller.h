#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <vector>
#include <unordered_map>

class Channel;
class EventLoop;

//muduo库中多路事件分发器的核心IO复用模块
class Poller : noncopyable
{
public:
	using ChannelList = std::vector<Channel*>;

	Poller(EventLoop* loop);
	virtual ~Poller() = default;

	//保留统一的给IO复用的接口，需要重写
	virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;
	virtual void updateChannel(Channel* channel) = 0;
	virtual void removeChannel(Channel* channel) = 0;

	//判断参数channel是否在当前poller当中
	virtual bool hasChannel(Channel* channel) const;

	//EventLoop可以通过该接口获得默认的IO复用的具体实现
	static Poller* newDefaultPoller(EventLoop* loop);
protected:
	//map的key表示sockfd
	using ChannelMap = std::unordered_map<int, Channel*>;
	ChannelMap channels_;
private:
	EventLoop* ownerLoop_;	//定义poller所属的eventloop事件循环
};