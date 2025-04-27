#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"
#include "Socket.h"
#include "Channel.h"

#include <memory>
#include <string>
#include <atomic>

class EventLoop;

//TcpServer => Acceptor =>有一个新用户连接，通过accept拿到connfd  =>TcpConnection 设置回调 =>Channel
// => poller 事件循环 =>调用Channel回调
class TcpConnection:noncopyable,public std::enable_shared_from_this<TcpConnection>
{
public:
	TcpConnection(EventLoop* loop, const std::string& nameArg, int sockfd, const InetAddress& localAddr,
		const InetAddress& peerAddr);
	~TcpConnection();

	EventLoop* getLoop() const { return loop_; }
	const std::string& name() const { return name_; }
	const InetAddress& localAddress() const { return localAddr_; }
	const InetAddress& peerAddress() const { return peerAddr_; }

	bool connected() const { return state_ == kConnected; }
	bool disconnected() const { return state_ == kDisconnected; }

	//发送数据
	void send(const std::string& buf);
	//关闭当前连接
	void shutdown();

	void setConnectionCallback(const ConnectionCallback& cb)
	{
		connectionCallback_ = cb;
	}
	void setMessageCallback(const MessageCallback& cb)
	{
		messageCallback_ = cb;
	}
	void setWriteCompleteCallback(const WriteCompleteCallback& cb)
	{
		writeCompleteCallback_ = cb;
	}
	void setCloseCallback(const CloseCallback& cb)
	{
		closeCallback_ = cb;
	}
	void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
	{
		highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark;
	}

	//连接建立
	void connectEstablished();
	//连接销毁
	void connectDestroyed();

private:
	enum StateE{kDisconnected, kConnecting, kConnected, kDisconnecting};

	void setState(StateE state) { state_ = state; }

	void handleRead(Timestamp receiveTime);
	void handleWrite();
	void handleClose();
	void handleError();

	void sendInLoop(const void* data, size_t len);
	void shutdownInLoop();


	EventLoop* loop_;	//这里绝对不是baseloop，因为TcpConnection都是在subloop里面管理的
	const std::string name_;
	std::atomic_int state_;
	bool reading_;

	//这里和acceptor类似 acceptor在mainloop里 tcpConnection在subloop里
	std::unique_ptr<Socket> socket_;
	std::unique_ptr<Channel> channel_;

	const InetAddress localAddr_;
	const InetAddress peerAddr_;

	//新连接的回调
	ConnectionCallback connectionCallback_;
	//读写消息的回调
	MessageCallback messageCallback_;
	//消息发送完的回调
	WriteCompleteCallback writeCompleteCallback_;
	CloseCallback closeCallback_;
	HighWaterMarkCallback highWaterMarkCallback_;
	size_t highWaterMark_;

	Buffer inputBuffer_;	//接收数据的缓冲区
	Buffer outputBuffer_;	//发送数据的缓冲区
};