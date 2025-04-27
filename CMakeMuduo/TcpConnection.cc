#include "TcpConnection.h"
#include "Logger.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>

static EventLoop* CheckLoopNotNull(EventLoop* loop)
{
	if (loop == nullptr)
	{
		LOG_FATAL("%s:%s:%d TcpConnection Loop is null \n", __FILE__, __FUNCTION__, __LINE__);
	}
	return loop;
}

TcpConnection::TcpConnection(EventLoop* loop, const std::string& nameArg, int sockfd, const InetAddress& localAddr,
	const InetAddress& peerAddr):
	loop_(CheckLoopNotNull(loop)), 
	name_(nameArg), 
	state_(kConnecting),
	reading_(true), 
	socket_(new Socket(sockfd)), 
	channel_(new Channel(loop, sockfd)), 
	localAddr_(localAddr),
	peerAddr_(peerAddr), 
	highWaterMark_(64*1024*1024)	//64M
{
	//下面给channel设置相应的回调函数 poller监听到给channel通知感兴趣的事件发生了 ，channel会回调相应函数
	channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
	channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
	channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
	channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

	LOG_INFO("TcpConnection::ctor[%s] at %d\n", name_.c_str(), sockfd);
	socket_->setKeepAlive(true);
}


TcpConnection::~TcpConnection()
{
	LOG_INFO("TcpConnection::dtor[%s] at %d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}

//发送数据	数据=> json pb 发送
void TcpConnection::send(const std::string& buf)
{
	if (state_ = kConnected)
	{
		if (loop_->isInLoopThread())
		{
			sendInLoop(buf.c_str(), buf.size());
		}
		else
		{
			loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
		}
	}
}

//关闭当前连接
void TcpConnection::shutdown()
{
	if (state_ == kConnected)
	{
		setState(kDisconnecting);
		loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
	}
}

//连接建立
void TcpConnection::connectEstablished()
{
	setState(kConnected);
	channel_->tie(shared_from_this());
	channel_->enableReading();
	//新连接建立，执行回调
	connectionCallback_(shared_from_this());
}

//连接销毁
void TcpConnection::connectDestroyed()
{
	if (state_ == kConnected)
	{
		setState(kDisconnected);
		channel_->disableAll();

		connectionCallback_(shared_from_this());
	}
	//把channel从poller中删除
	channel_->remove();
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
	int savedErrno = 0;
	size_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
	if (n > 0)
	{
		messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
	}
	else if (n == 0)
	{
		handleClose();
	}
	else
	{
		errno = savedErrno;
		LOG_ERROR("TcpConnection handleRead error");
		handleError();
	}
}

void TcpConnection::handleWrite()
{
	if (channel_->isWriting())
	{
		int savedErrno = 0;
		ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
		if (n > 0)
		{
			outputBuffer_.retrieve(n);
			if (outputBuffer_.readableBytes() == 0)
			{
				channel_->disableWriting();
				if (writeCompleteCallback_)
				{
					//唤醒loop所在的线程，执行回调
					loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
				}
			}
			if (state_ == kDisconnecting)
			{
				shutdownInLoop();
			}
		}
		else
		{
			LOG_ERROR("TcpConnection::handleWrite error\n");
		}
	}
	else
	{
		LOG_ERROR("TcpConnection fd=%d is down, no more writing\n", channel_->fd());
	}
}

void TcpConnection::handleClose()
{
	LOG_INFO("fd=%d state=%d \n", channel_->fd(), (int)state_);
	setState(kDisconnected);
	channel_->disableAll();

	TcpConnectionPtr connPtr(shared_from_this());
	connectionCallback_(connPtr);	//执行连接关闭的回调
	closeCallback_(connPtr);	//关闭连接的回调
}

void TcpConnection::handleError()
{
	int optval;
	socklen_t optlen = sizeof optval;
	int err = 0;
	if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
	{
		err = errno;
	}
	else
	{
		err = optval;
	}
	LOG_ERROR("TcpConnection::handleError()");
}

//发送数据 应用写的快，内核发送数据慢 需要把待发送数据写入缓冲区，而且设置了水位回调,防止发送太快
void TcpConnection::sendInLoop(const void* data, size_t len)
{
	ssize_t nwrote = 0;
	size_t remaining = len;
	bool faultError = false;

	if (state_ == kDisconnected)
	{
		LOG_ERROR("disconnected, give up writing\n");
		return;
	}

	//channel第一次开始写数据，而且缓冲区没有待发送数据
	if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
	{
		nwrote = ::write(channel_->fd(), data, len);
		if (nwrote >= 0)
		{
			remaining = len - nwrote;
			//一次性数据发送完成，就不用再给channel设置epollout事件了
			if (remaining == 0 && writeCompleteCallback_)
			{
				loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
			}
		}
		else
		{
			nwrote = 0;
			if (errno != EWOULDBLOCK)
			{
				LOG_ERROR("TcpConnection::sendInLoop error!");
				if (errno == EPIPE || errno == ECONNRESET)
				{
					faultError = true;
				}
			}
		}
	}

	//这一次write并没有把数据完全发送出去，需要把数据保存在缓冲区中，给channel注册epollout事件
	//poller发现发送缓冲区有空间，通知channel调用handlewrite回调
	//也就是调用tcpconnection::handlewrite方法把发送缓冲区中数据全部发送完成
	if (!faultError && remaining > 0)
	{
		//发送缓冲区待发送数据长度
		size_t oldLen = outputBuffer_.readableBytes();
		if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_&&highWaterMarkCallback_)
		{
			loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
		}
		outputBuffer_.append((char*)data + nwrote, remaining);
		if (!channel_->isWriting())
		{
			//注册channel的写事件
			channel_->enableWriting();
		}
	}
}

void TcpConnection::shutdownInLoop()
{
	//output中的数据已经全部发送完成
	if (!channel_->isWriting())
	{
		//关闭写端
		socket_->shutdownWrite();
	}
}