#include "TcpServer.h"
#include "Logger.h"

static EventLoop* CheckLoopNotNull(EventLoop* loop)
{
	if (loop == nullptr)
	{
		LOG_FATAL("%s:%s:%d mainLoop is null \n", __FILE__, __FUNCTION__, __LINE__);
	}
	return loop;
}

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& nameArg, Option option)
	:loop_(CheckLoopNotNull(loop)), ipPort_(listenAddr.toIpPort()), name_(nameArg), 
	acceptor_(new Acceptor(loop, listenAddr, option = kReusePort)), 
	threadPool_(new EventLoopThreadPool(loop, name_)), connectionCallback_(),
	messageCallback_(),	nextConnId_(1), started_(0)
{
	//当有新用户连接时，会执行TcpServer::newConnection回调
	acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1,
		std::placeholders::_2));
}

TcpServer::~TcpServer()
{
	for (auto& item : connections_)
	{
		//当conn出了右括号 即可释放TcpConnection
		TcpConnectionPtr conn(item.second);
		item.second.reset();    // 把原始的智能指针复位

		// 销毁连接
		conn->getLoop()->runInLoop(
			std::bind(&TcpConnection::connectDestroyed, conn));
	}
}

//设置底层subloop个数
void TcpServer::setThreadNum(int numThreads)
{
	threadPool_->setThreadNum(numThreads);
}

//开启服务器监听  loop.loop()
void TcpServer::start()
{
	//防止一个tcpserver对象被start多次
	if (started_++ == 0)
	{
		//启动底层线程池
		threadPool_->start();
		//
		loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
	}
}

//有一个新客户端连接，acceptor就会执行这个回调
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
	//轮询算法，选择一个subloop来管理channel
	EventLoop* ioLoop = threadPool_->getNextLoop();
	char buf[64] = { 0 };
	snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
	++nextConnId_;
	std::string connName = name_ + buf;
	LOG_INFO("TcpServer::newConnection[%s]	- new connection [%s] from %s\n", 
		name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

	//通过sockfd获取其绑定的本机ip地址和端口信息
	sockaddr_in local;
	::memset(&local, 0, sizeof local);
	socklen_t addrlen = sizeof local;
	if (::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
	{
		LOG_ERROR("sockets::getLocalAddr\n");
	}
	InetAddress localAddr(local);

	//根据连接成功的fd创建TcpConnection
	TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
	connections_[connName] = conn;
	//用户设置给TcpServer=》TcpConnection=》Channel
	conn->setConnectionCallback(connectionCallback_);
	conn->setWriteCompleteCallback(writeCompleteCallback_);
	conn->setMessageCallback(messageCallback_);

	//设置如何关闭连接的回调
	conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
	//直接调用cpConnection::connectEstablished
	ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
	loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
	LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n", name_.c_str(), conn->name().c_str());
	size_t n = connections_.erase(conn->name());

	EventLoop* ioLoop = conn->getLoop();
	ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}