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
#define LOGT(...)    CAsyncLog::output(LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOGD(...)    CAsyncLog::output(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOGI(...)    CAsyncLog::output(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...)    CAsyncLog::output(LOG_LEVEL_WARNING, __FILE__, __LINE__,__VA_ARGS__)
#define LOGE(...)    CAsyncLog::output(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGSYSE(...) CAsyncLog::output(LOG_LEVEL_SYSERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGF(...)    CAsyncLog::output(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)        //为了让FATAL级别的日志能立即crash程序，采取同步写日志的方法
#define LOGC(...)    CAsyncLog::output(LOG_LEVEL_CRITICAL, __FILE__, __LINE__, __VA_ARGS__)     //关键信息，无视日志级别，总是输出

//用于输出数据包的二进制格式
#define LOG_DEBUG_BIN(buf, buflength) CAsyncLog::outputBinary(buf, buflength)

class LOG_API CAsyncLog
{
public:
    static bool init(const char* pszLogFileName = nullptr, bool bTruncateLongLine = false, int64_t nRollSize = 10 * 1024 * 1024);
	static void uninit();

    static void setLevel(LOG_LEVEL nLevel);
    static bool isRunning();

    /**
     *  日志文件写入是否已被停用。
     *
     *  以文件方式初始化（init 传了文件名）之后，如果日志文件建不出来或写不进去，
     *  写日志线程不会就此结束，而是把这个标志置上、退化为只写控制台继续跑，
     *  并向标准错误说明一次原因。调用方通常不需要关心它，本方法主要供单元测试核对。
     *
     *  @return true 表示文件写入已停用，此后日志只出现在控制台；
     *          以控制台方式初始化（init 未传文件名）时恒为 false。
     */
    static bool isFileWriteDisabled();
	
	//不输出线程ID号和所在函数签名、行号
	static bool output(long nLevel, const char* pszFmt, ...);
	//输出线程ID号和所在函数签名、行号	
    static bool output(long nLevel, const char* pszFileName, int nLineNo, const char* pszFmt, ...);

    static bool outputBinary(unsigned char* buffer, size_t size);

private:
    CAsyncLog() = delete;
    ~CAsyncLog() = delete;

    CAsyncLog(const CAsyncLog& rhs) = delete;
    CAsyncLog& operator=(const CAsyncLog& rhs) = delete;

    static void makeLinePrefix(long nLevel, std::string& strPrefix);
    static void getTime(char* pszTime, int nTimeStrLength);
    static bool createNewFile(const char* pszLogFileName);
    static bool writeToFile(const std::string& data);
    //停用日志文件写入，退化为只写控制台，并向标准错误说明原因。
    //只在写日志线程里调用，pszLogFileName 为出问题的文件路径，仅用于提示信息
    static void disableFileWrite(const char* pszLogFileName);
    //让程序主动崩溃
    static void crash();

    static const char* ullto4Str(int n);
    static char* formLog(int& index, char* szbuf, size_t size_buf, unsigned char* buffer, size_t size);

    static void writeThreadProc();

    /**
     *  把一行日志送到终端与调试器输出窗口。
     *
     *  收成一个函数是因为「该不该补换行」这件事有个容易踩的坑：启用了日志文件时，
     *  strLine 末尾自带一个 '\n'（那是写文件用的），此时再补 std::endl，终端里
     *  每条日志之间就会多空一行。三个输出点各写一遍必然漏改，故统一在此处理。
     *
     *  @param strLine 完整的一行日志，末尾可能已带 '\n'
     *  副作用：向标准输出写入；Windows 下同时写调试器输出窗口。
     */
    static void writeToConsole(const std::string& strLine);

private:
	static FILE*                            m_hLogFile;
    static std::string                      m_strFileName;          //日志文件名
    static std::string                      m_strFileNamePID;    //文件名中的进程id
    static bool                             m_bTruncateLongLog;     //长日志是否截断
    static LOG_LEVEL                        m_nCurrentLevel;        //当前日志级别
    static int64_t                          m_nFileRollSize;        //单个日志文件的最大字节数
    static int64_t                          m_nCurrentWrittenSize;  //已经写入的字节数目
    static std::list<std::string>           m_listLinesToWrite;     //待写入的日志
    static std::unique_ptr<std::thread>     m_spWriteThread;
    static std::mutex                       m_mutexWrite;
    static std::condition_variable          m_cvWrite;
    static bool                             m_bExit;                //退出标志
    static bool                             m_bRunning;             //运行标志
    //日志文件写入是否已停用。建文件或写文件失败时置 true，此后只写控制台。
    //只由写日志线程修改，其余线程只读，故不额外加锁
    static bool                             m_bFileWriteDisabled;
};

#endif // !__ASYNC_LOG_H__