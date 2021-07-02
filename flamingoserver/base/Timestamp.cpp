#include "Timestamp.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdio.h>

static_assert(sizeof(Timestamp) == sizeof(int64_t), "sizeof(Timestamp) error");

Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
: microSecondsSinceEpoch_(microSecondsSinceEpoch)
{
}

string Timestamp::toString() const
{
    char buf[64] = { 0 };
    int64_t seconds = microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
    int64_t microseconds = microSecondsSinceEpoch_ % kMicroSecondsPerSecond;
    snprintf(buf, sizeof(buf)-1, "%lld.%06lld", (long long int)seconds, (long long int)microseconds);
    return buf;
}

string Timestamp::toFormattedString(bool showMicroseconds) const
{
	time_t seconds = static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
	struct tm tm_time;

#ifdef WIN32
	localtime_s(&tm_time, &seconds);
#else
	struct tm *ptm;
	ptm = localtime(&seconds);
	tm_time = *ptm;
#endif
	//ss << std::put_time(&ptm, "%F %T");
		//<<		((double)(microSecondsSinceEpoch_ % kMicroSecondsPerSecond)) / kMicroSecondsPerSecond;

	char buf[32] = { 0 };

	if (showMicroseconds)
	{
		int microseconds = static_cast<int>(microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
#ifdef WIN32
		_snprintf_s(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d",
			tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
			tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
			microseconds);
#else
		snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d",
			tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
			tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
			microseconds);
#endif
	}
	else
	{
#ifdef WIN32
		_snprintf_s(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d",
			tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
			tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
#else
		snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d",
			tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
			tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
#endif
	}
	
	
	return buf;
}

Timestamp Timestamp::now()
{
	chrono::time_point<chrono::system_clock, chrono::microseconds> now = chrono::time_point_cast<chrono::microseconds>(
		chrono::system_clock::now());

	int64_t microSeconds = now.time_since_epoch().count();
	Timestamp time(microSeconds);
	return time;
}

Timestamp Timestamp::invalid()
{
	return Timestamp();
}