#pragma once

#include "noncopyable.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Acceptor.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"


#include <functional>
#include <string>
#include <string.h>
#include <memory>
#include <atomic>
#include <unordered_map>


//对外的服务器编程使用的类
class TcpServer:noncopyable
{
public:
	using ThreadInitCallback = std::function<void(EventLoop*)>;

	enum Option
	{
		kNoReusePort,
		kReusePort,
	};

	TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& nameArg, Option option = kNoReusePort);
	~TcpServer();

	//设置底层subloop个数
	void setThreadNum(int numThreads);
	void setThreadInitCallback(const ThreadInitCallback& cb) { threadInitCallback_ = cb; }
	void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
	void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
	void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }


	//开启服务器监听
	void start();

private:
	void newConnection(int sockfd, const InetAddress& peerAddr);
	void removeConnection(const TcpConnectionPtr& conn);
	void removeConnectionInLoop(const TcpConnectionPtr& conn);

	using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

	//baseloop
	EventLoop* loop_;

	const std::string ipPort_;
	const std::string name_;
	
	//mainloop
	std::unique_ptr<Acceptor> acceptor_;

	//one loop per thread
	std::shared_ptr<EventLoopThreadPool> threadPool_;

	//新连接的回调
	ConnectionCallback connectionCallback_;
	//读写消息的回调
	MessageCallback messageCallback_;
	//消息发送完的回调
	WriteCompleteCallback writeCompleteCallback_;

	//loop线程初始化的回调
	ThreadInitCallback threadInitCallback_;

	std::atomic_int started_;
	int nextConnId_;
	ConnectionMap connections_;
};