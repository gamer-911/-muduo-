#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

//poller工作在LT模式
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
	//栈上分配64k内存
	char extrabuf[65536] = { 0 };
	struct iovec vec[2];
	const size_t writable = writableBytes();
	vec[0].iov_base = begin()+writerIndex_;
	vec[0].iov_len = writable;

	vec[1].iov_base = extrabuf;
	vec[1].iov_len = sizeof extrabuf;

	//如果当前buffer大小已经超过64k，不再使用extrabuf
	const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
	const ssize_t n = ::readv(fd, vec, iovcnt);

	if (n < 0)
	{
		*savedErrno = errno;
	}
	else if(n<=writable)	//没用到extrabuf
	{
		writerIndex_ += n;
	}
	else
	{
		writerIndex_ = buffer_.size();
		append(extrabuf, n-writable);
	}
	return n;
}

ssize_t Buffer::writeFd(int fd, int* savedErrno)
{
	ssize_t n = ::write(fd, peek(), readableBytes());
	if (n < 0)
	{
		*savedErrno = errno;
	}
	return n;
}