#include "Buffer.h"

#include "../base/Platform.h"
#include "Sockets.h"
#include "Callbacks.h"

using namespace net;

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

int32_t Buffer::readFd(int fd, int* savedErrno)
{
	// saved an ioctl()/FIONREAD call to tell how much to read
	char extrabuf[65536];
    const size_t writable = writableBytes();
#ifndef WIN32
	struct iovec vec[2];
	
	vec[0].iov_base = begin() + writerIndex_;
	vec[0].iov_len = writable;
	vec[1].iov_base = extrabuf;
	vec[1].iov_len = sizeof extrabuf;
	// when there is enough space in this buffer, don't read into extrabuf.
	// when extrabuf is used, we read 128k-1 bytes at most.
	const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
	const ssize_t n = sockets::readv(fd, vec, iovcnt);
#else
    const int32_t n = sockets::read(fd, extrabuf, sizeof(extrabuf));
#endif
	if (n <= 0)
	{
#ifdef WIN32
        *savedErrno = ::WSAGetLastError();
#else
		*savedErrno = errno;
#endif
	}
	else if (implicit_cast<size_t>(n) <= writable)
	{
#ifdef WIN32
        //Windows平台需要手动把接收到的数据加入buffer中，Linux平台已经在 struct iovec 中指定了缓冲区写入位置
        append(extrabuf, n);
#else
        writerIndex_ += n;
#endif
	}
	else
	{
#ifdef WIN32
		//Windows平台直接将所有的字节放入缓冲区去
        append(extrabuf, n);
#else
        //Linux平台把剩下的字节补上去
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
#endif
	}
	// if (n == writable + sizeof extrabuf)
	// {
	//   goto line_30;
	// }
	return n;
}
