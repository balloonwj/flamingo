#include "CountDownLatch.h"

CountDownLatch::CountDownLatch(int count)
: count_(count)
{
}

void CountDownLatch::wait()
{
	std::unique_lock<std::mutex> lock(mutex_);
	while (count_ > 0)
	{
		condition_.wait(lock);
	}
}

void CountDownLatch::countDown()
{
	std::unique_lock<std::mutex> lock(mutex_);
	--count_;
	if (count_ == 0)
	{
		condition_.notify_all();
	}
}

int CountDownLatch::getCount() const
{
	std::unique_lock<std::mutex> lock(mutex_);
	return count_;
}