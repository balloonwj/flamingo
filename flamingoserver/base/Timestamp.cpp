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

	//日期与时间部分用 strftime 输出，不再用 snprintf 逐个拼 struct tm 的字段。
	//原因有两个：一是 GCC 把 tm_mon / tm_mday 等字段当作无界的 int，认为每个 %02d
	//最坏要写 11 个字节，于是对着 buf[32] 报 -Wformat-truncation 误报；二是 strftime
	//在 Windows 与 POSIX 上都可用，省掉了 _snprintf_s 与 snprintf 两套分支。
	//年份此处输出四位，与原先的 "%4d" 在公元 1000 年以后的取值上完全一致。
	//strftime 的返回值是写入的字符数（不含结尾的 '\0'），缓冲区放不下时返回 0。
	size_t len = strftime(buf, sizeof(buf), "%Y%m%d %H:%M:%S", &tm_time);

	if (showMicroseconds)
	{
		int microseconds = static_cast<int>(microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
		snprintf(buf + len, sizeof(buf) - len, ".%06d", microseconds);
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