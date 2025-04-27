#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>

//kNew表示Channel未添加到Poller中
const int kNew = -1;	//Channel的index_初始化为-1
//Channel已经添加到Poller中
const int kAdded = 1;
//Channel从Poller中删除
const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop* loop)
	:Poller(loop),
	epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
	events_(kInitEventListSize)
{
	if (epollfd_ < 0)
		LOG_FATAL("epoll_create error:%d \n",errno);
}

EPollPoller::~EPollPoller()
{
	::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
	//实际上应该用LOG_DEBUG更合适
	LOG_INFO("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());

	int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);

	int saveErrno = errno;
	Timestamp now(Timestamp::now());
	
	if (numEvents > 0)
	{
		LOG_INFO("%d events happened\n", numEvents);
		fillActiveChannels(numEvents, activeChannels);
		if (numEvents == events_.size())
			events_.resize(events_.size() * 2);
	}
	else if (numEvents == 0)
	{
		LOG_DEBUG("%s timeout\n", __FUNCTION__);
	}
	else
	{
		if (saveErrno != EINTR)
		{
			errno = saveErrno;
			LOG_ERROR("EPollPoller::poll() err!\n");
		}
	}
	return now;
}

//channel update remove触发 => 调用EventLoop updateChannel removeChannel 
// => 实际上是Poller updateChannel removeChannel
/*
					EventLoop   activeChannels
		ChannelList				Poller
		所有的channel					ChannelMap<fd,Channel*>	//记录注册到Poller的fd
*/
void EPollPoller::updateChannel(Channel* channel)
{
	const int index = channel->index();
	LOG_INFO("func=%s => fd=%d events=%d index=%d \n",__FUNCTION__,channel->fd(), channel->events(), index);
	if (index == kNew || index == kDeleted)
	{
		if (index == kNew)
		{
			int fd = channel->fd();
			channels_[fd] = channel;
		}
		channel->set_index(kAdded);
		update(EPOLL_CTL_ADD, channel);
	}
	else  //channel已经注册过
	{
		if (channel->isNoneEvent())	//删除的情况
		{
			update(EPOLL_CTL_DEL, channel);
			channel->set_index(kDeleted);
		}
		else
		{
			update(EPOLL_CTL_MOD, channel);
		}
	}
}

//从Poller中删除channel
void EPollPoller::removeChannel(Channel* channel)
{
	int fd = channel->fd();
	channels_.erase(fd);

	LOG_INFO("func=%s => fd=%d \n", __FUNCTION__, channel->fd());

	int index = channel->index();
	if (index == kAdded)
	{
		update(EPOLL_CTL_DEL, channel);
	}
	channel->set_index(kNew);
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
	for (int i = 0; i < numEvents; i++)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
		channel->set_revents(events_[i].events);
		activeChannels->push_back(channel);	// EventLoop拿到了Poller返回的发生的channel列表
	}
}

//epoll_ctl具体操作
void EPollPoller::update(int operation, Channel* channel)
{
	int fd = channel->fd();

	struct epoll_event event;
	memset(&event, 0, sizeof(event));

	event.events = channel->events();
	event.data.fd = fd;
	event.data.ptr = channel;

	if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
	{
		if (operation == EPOLL_CTL_DEL)
		{
			LOG_ERROR("epoll_ctl del error:%d \n", errno);
		}
		else
		{
			LOG_FATAL("epoll_ctl add/mod error:%d \n", errno);
		}
	}
}