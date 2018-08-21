#pragma once

#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <string.h>
#include "CountDownLatch.h"

using namespace std;

const int kSmallBuffer = 4000;
const int kLargeBuffer = 4000*1000;

template<int SIZE>
class FixedBuffer
{
public:
	FixedBuffer()
		: cur_(data_)
	{
		setCookie(cookieStart);
	}

	~FixedBuffer()
	{
		setCookie(cookieEnd);
	}

	void append(const char* /*restrict*/ buf, size_t len)
	{
		// FIXME: append partially
		if (static_cast<size_t>(avail()) > len)
		{
			memcpy(cur_, buf, len);
			cur_ += len;
		}
	}

	const char* data() const { return data_; }
	int length() const { return static_cast<int>(cur_ - data_); }

	// write to data_ directly
	char* current() { return cur_; }
	int avail() const { return static_cast<int>(end() - cur_); }
	void add(size_t len) { cur_ += len; }

	void reset() { cur_ = data_; }
	void bzero() { memset(data_, 0, sizeof data_); }

	// for used by GDB
	const char* debugString();
	void setCookie(void (*cookie)()) { cookie_ = cookie; }
	// for used by unit test
	string asString() const { return string(data_, length()); }

private:
	const char* end() const { return data_ + sizeof data_; }
	// Must be outline function for cookies.
	static void cookieStart();
	static void cookieEnd();

	void (*cookie_)();
	char data_[SIZE];
	char* cur_;
};

class AsyncLogging
{
public:

	AsyncLogging(const char* basename = "logs/",
		size_t rollSize = 1024 * 1024 * 1024,
		int flushInterval = 3);

	~AsyncLogging()
	{
		if (running_)
		{
			stop();
		}
	}

    void setRollSize(size_t rollSize) 
    {
        rollSize_ = rollSize;
    }

    void setBaseName(const char* basename)
    {
        basename_ = basename;
    }

	void append(const char* logline, int len);

	void start()
	{
		running_ = true;
		thread_ .reset(new thread(std::bind(&AsyncLogging::threadFunc, this)));
		latch_.wait();
	}

	void stop()
	{
		running_ = false;
		cond_.notify_all();
		thread_->join();
	}

private:

	// declare but not define, prevent compiler-synthesized functions
	AsyncLogging(const AsyncLogging&);  // ptr_container
	void operator=(const AsyncLogging&);  // ptr_container

	void threadFunc();

	typedef FixedBuffer<kLargeBuffer> Buffer;
	typedef shared_ptr<Buffer> BufferPtr;
	typedef vector<BufferPtr > BufferVector;


	const int          flushInterval_;
	bool               running_;
	string             basename_;
	size_t             rollSize_;
	shared_ptr<thread> thread_;
	CountDownLatch     latch_;
	mutex              mutex_;
	condition_variable cond_;
	BufferPtr          currentBuffer_;
	BufferPtr          nextBuffer_;
	BufferVector       buffers_;
};