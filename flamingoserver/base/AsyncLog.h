/** 
 * @desc:   异步日志类，AsyncLog.h
 * @author: zhangyl
 * @date:   2019.04.13
 */

#ifndef __ASYNC_LOG_H__
#define __ASYNC_LOG_H__

#include <stdio.h>
#include <string>
#include <list>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

//#ifdef LOG_EXPORTS
//#define LOG_API __declspec(dllexport)
//#else
//#define LOG_API __declspec(dllimport)
//#endif

#define LOG_API

enum LOG_LEVEL
{
    LOG_LEVEL_TRACE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,    //用于业务错误
    LOG_LEVEL_SYSERROR, //用于技术框架本身的错误
    LOG_LEVEL_FATAL,    //FATAL 级别的日志会让在程序输出日志后退出
    LOG_LEVEL_CRITICAL  //CRITICAL 日志不受日志级别控制，总是输出
};

//TODO: 多增加几个策略
//注意：如果打印的日志信息中有中文，则格式化字符串要用_T()宏包裹起来，
//e.g. LOGI(_T("GroupID=%u, GroupName=%s, GroupName=%s."), lpGroupInfo->m_nGroupCode, lpGroupInfo->m_strAccount.c_str(), lpGroupInfo->m_strName.c_str());
#define LOGT(...)    CAsyncLog::Output(LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOGD(...)    CAsyncLog::Output(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOGI(...)    CAsyncLog::Output(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...)    CAsyncLog::Output(LOG_LEVEL_WARNING, __FILE__, __LINE__,__VA_ARGS__)
#define LOGE(...)    CAsyncLog::Output(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGSYSE(...) CAsyncLog::Output(LOG_LEVEL_SYSERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGF(...)    CAsyncLog::Output(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)        //为了让FATAL级别的日志能立即crash程序，采取同步写日志的方法
#define LOGC(...)    CAsyncLog::Output(LOG_LEVEL_CRITICAL, __FILE__, __LINE__, __VA_ARGS__)     //关键信息，无视日志级别，总是输出

//用于输出数据包的二进制格式
#define LOG_DEBUG_BIN(buf, buflength) CAsyncLog::OutputBinary(buf, buflength)

class LOG_API CAsyncLog
{
public:
    static bool Init(const char* pszLogFileName = nullptr, bool bTruncateLongLine = false, int64_t nRollSize = 10 * 1024 * 1024);
	static void Uninit();

    static void SetLevel(LOG_LEVEL nLevel);
    static bool IsRunning();
	
	//不输出线程ID号和所在函数签名、行号
	static bool Output(long nLevel, const char* pszFmt, ...);
	//输出线程ID号和所在函数签名、行号	
    static bool Output(long nLevel, const char* pszFileName, int nLineNo, const char* pszFmt, ...);

    static bool OutputBinary(unsigned char* buffer, size_t size);

private:
    CAsyncLog() = delete;
    ~CAsyncLog() = delete;

    CAsyncLog(const CAsyncLog& rhs) = delete;
    CAsyncLog& operator=(const CAsyncLog& rhs) = delete;

    static void MakeLinePrefix(long nLevel, std::string& strPrefix);
    static void GetTime(char* pszTime, int nTimeStrLength);
    static bool CreateNewFile(const char* pszLogFileName);
    static bool WriteToFile(const std::string& data);
    //让程序主动崩溃
    static void Crash();

    static const char* ullto4Str(int n);
    static char* FormLog(int& index, char* szbuf, size_t size_buf, unsigned char* buffer, size_t size);

    static void WriteThreadProc();
	
private:
	static bool		                        m_bToFile;			    //日志写入文件还是写到控制台  
	static FILE*                            m_hLogFile;
    static std::string                      m_strFileName;          //日志文件名
    static std::string                      m_strFileNamePID;    //文件名中的进程id
    static bool                             m_bTruncateLongLog;     //长日志是否截断
    static LOG_LEVEL                        m_nCurrentLevel;        //当前日志级别
    static int64_t                          m_nFileRollSize;        //单个日志文件的最大字节数
    static int64_t                          m_nCurrentWrittenSize;  //已经写入的字节数目
    static std::list<std::string>           m_listLinesToWrite;     //待写入的日志
    static std::shared_ptr<std::thread>     m_spWriteThread;
    static std::mutex                       m_mutexWrite;
    static std::condition_variable          m_cvWrite;
    static bool                             m_bExit;                //退出标志
    static bool                             m_bRunning;             //运行标志
};

#endif // !__ASYNC_LOG_H__