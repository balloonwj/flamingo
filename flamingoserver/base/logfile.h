#pragma once

#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <memory>

using namespace std;

namespace FileUtil
{
	class AppendFile;
}

class LogFile
{
public:
	LogFile(const string& basename,
		size_t rollSize,
		bool threadSafe = true,
		int flushInterval = 3,
		int checkEveryN = 1024);
	~LogFile();

	void append(const char* logline, int len);
	void flush();
	bool rollFile();

private:
	void append_unlocked(const char* logline, int len);

	static string getLogFileName(const string& basename, time_t* now);

	const string basename_;
	const size_t rollSize_;
	const int flushInterval_;
	const int checkEveryN_;

	int count_;

	std::shared_ptr<std::mutex> mutex_;
	time_t startOfPeriod_;
	time_t lastRoll_;
	time_t lastFlush_;
	std::shared_ptr<FileUtil::AppendFile> file_;

	const static int kRollPerSeconds_ = 60 * 60 * 24;
};