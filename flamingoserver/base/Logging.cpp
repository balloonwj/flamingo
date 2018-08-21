#include "Logging.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <thread>

__thread char t_errnobuf[512];
__thread char t_time[32];
__thread time_t t_lastSecond;

const char* strerror_tl(int savedErrno)
{
	return strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
}

Logger::LogLevel initLogLevel()
{
	//if (::getenv("MUDUO_LOG_TRACE"))
	//	return Logger::TRACE;
	//else if (::getenv("MUDUO_LOG_DEBUG"))
	//	return Logger::DEBUG;
	//else
		return Logger::INFO;
}

Logger::LogLevel g_logLevel = initLogLevel();

const char* LogLevelName[Logger::NUM_LOG_LEVELS] =
{
	"TRACE ",
	"DEBUG ",
	"INFO  ",
	"WARN  ",
	"ERROR ",
	"FATAL ",
};

// helper class for known string length at compile time
class T
{
public:
	T(const char* str, unsigned len)
		:str_(str),
		len_(len)
	{
		assert(strlen(str) == len_);
	}

	const char* str_;
	const unsigned len_;
};

inline LogStream& operator<<(LogStream& s, T v)
{
	s.append(v.str_, v.len_);
	return s;
}

inline LogStream& operator<<(LogStream& s, const Logger::SourceFile& v)
{
	s.append(v.data_, v.size_);
	return s;
}

void defaultOutput(const char* msg, int len)
{
	size_t n = fwrite(msg, 1, len, stdout);
	//FIXME check n
	(void)n;
}

void defaultFlush()
{
	fflush(stdout);
}

Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;

Logger::Impl::Impl(LogLevel level, int savedErrno, const SourceFile& file, int line)
: time_(Timestamp::now()),
stream_(),
level_(level),
line_(line),
basename_(file)
{
	formatTime();
	stream_ << T(LogLevelName[level], 6);
	if (savedErrno != 0)
	{
		stream_ << strerror_tl(savedErrno) << " (errno=" << savedErrno << ") ";
	}
}

void Logger::Impl::formatTime()
{
	int64_t microSecondsSinceEpoch = time_.microSecondsSinceEpoch();
	time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / Timestamp::kMicroSecondsPerSecond);
	int microseconds = static_cast<int>(microSecondsSinceEpoch % Timestamp::kMicroSecondsPerSecond);
	if (seconds != t_lastSecond)
	{
		t_lastSecond = seconds;
		struct tm tm_time;
		
		::localtime_r(&seconds, &tm_time); // FIXME TimeZone::fromUtcTime

		int len = snprintf(t_time, sizeof(t_time), "%4d%02d%02d %02d:%02d:%02d",
			tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
			tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
		assert(len == 17); (void)len;
	}

	stream_ << T(t_time, 17) << " ";
}

void Logger::Impl::finish()
{
	stream_ << " - " << basename_ << ':' << line_ << '\n';
}

Logger::Logger(SourceFile file, int line)
: impl_(INFO, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, LogLevel level, const char* func)
: impl_(level, 0, file, line)
{
	impl_.stream_ << func << ' ';
}

Logger::Logger(SourceFile file, int line, LogLevel level)
: impl_(level, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, bool toAbort)
: impl_(toAbort ? FATAL : ERROR, errno, file, line)
{
}

Logger::~Logger()
{
	impl_.finish();
	const LogStream::Buffer& buf(stream().buffer());
	g_output(buf.data(), buf.length());
	if (impl_.level_ == FATAL)
	{
		g_flush();
		abort();
	}
}

void Logger::setLogLevel(Logger::LogLevel level)
{
	g_logLevel = level;
}

void Logger::setOutput(OutputFunc out)
{
	g_output = out;
}

void Logger::setFlush(FlushFunc flush)
{
	g_flush = flush;
}

namespace{
const char * ullto4Str(int n)
{
	static char buf[64 + 1];
	memset(buf, 0, sizeof(buf));
	sprintf(buf, "%06u", n);
	return buf;
}

char g_szchar[17] = "0123456789abcdef";

char* FormLog(int &index,char *szbuf, size_t size_buf, unsigned char* buffer, size_t size)
{
	size_t len = 0; 
	size_t lsize = 0;
	int headlen = 0;
	char szhead[64 + 1];
	memset(szhead, 0, sizeof(szhead));
	while (size > lsize && len + 10 < size_buf)
	{
		if (lsize % 32 == 0)
		{
			if (0 != headlen)
			{
				szbuf[len++] = '\n';
			}

			memset(szhead, 0, sizeof(szhead));
			strncpy(szhead, ullto4Str(index++), sizeof(szhead)-1);
			headlen = strlen(szhead);
			szhead[headlen++] = ' ';

			strcat(szbuf, szhead);
			len += headlen;

		}
		if (lsize % 16 == 0 && 0 != headlen)
			szbuf[len++] = ' ';
		szbuf[len++] = g_szchar[(buffer[lsize] >> 4) & 0xf];
		szbuf[len++] = g_szchar[(buffer[lsize]) & 0xf];
		lsize++;
	}
	szbuf[len++] = '\n';
	szbuf[len++] = '\0';
	return szbuf;
}
}
void Logger::WriteLog(unsigned char* buffer, size_t size)
{
	static const size_t PRINTSIZE = 512;
	char szbuf[PRINTSIZE * 3 + 8];

	size_t lsize = 0;
	size_t lprintbufsize = 0;
	int index = 0;
	stream() << "adress[" << (long)buffer << "] size[" << size << "] \n";
	while (true)
	{
		memset(szbuf, 0, sizeof(szbuf));
		if (size > lsize)
		{
			lprintbufsize = (size - lsize);
			lprintbufsize = lprintbufsize > PRINTSIZE ? PRINTSIZE : lprintbufsize;
			FormLog(index, szbuf, sizeof(szbuf), buffer + lsize, lprintbufsize);
			size_t len = strlen(szbuf);
	
			if (stream().buffer().avail() < static_cast<int>(len))
			{
				stream() << szbuf + (len - stream().buffer().avail());
				break;
			}
			else
			{
				stream() << szbuf;
			}
			lsize += lprintbufsize;
		}
		else
		{
			break;
		}
	}
}