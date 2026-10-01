/**
 *  异步日志类：调用线程只负责格式化并把日志行放进队列，由一个专门的写日志线程写入文件。
 *
 *  本文件拷贝自服务端 flamingoserver/base/AsyncLog.h（原作者 zhangyl，2019-04-13），
 *  用来替换客户端原先每写一行都同步 FlushFileBuffers 的日志实现（实测每行约 1.4 毫秒，
 *  且全部花在调用线程上，UI 线程打日志会直接拖慢界面）。与服务端原版的差异只有以下几处，
 *  同步服务端的修改时请逐条核对：
 *    1. 不定义 LOGI / LOGE 等宏。客户端统一通过 IULog.h 的 LOG_INFO / LOG_WARNING /
 *       LOG_ERROR 打日志，那一层负责宽字符格式串与函数签名，不直接调用本类。
 *    2. 新增 flush()：等待队列里已有的日志全部写入文件，供崩溃处理与 ExitProcess 之前调用，
 *       避免进程结束时丢掉最后几行日志。
 *    3. 文件名按 UTF-8 传入、用 _wfopen 打开，安装路径含有系统代码页之外的字符时也能建出文件。
 *    4. 以文件方式初始化时，不再把每一行同时写到标准输出与调试器输出窗口
 *       （客户端是 GUI 程序，没有控制台；原先的客户端日志写文件时也不输出到调试器）。
 *    5. uninit() 在持有互斥量的情况下设置退出标志，修正写日志线程可能错过唤醒、
 *       导致 uninit() 里的 join 一直等待的问题（服务端原版同样存在）。
 *
 *  典型用法：由 CIULog::Init / Uninit 间接调用，业务代码不直接使用本类。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#ifndef __ASYNC_LOG_H__
#define __ASYNC_LOG_H__

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <list>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

#define LOG_API

//日志级别，数值越大越严重；低于当前级别的日志不输出（LOG_LEVEL_CRITICAL 除外）
enum LOG_LEVEL
{
    //最详细的跟踪信息，客户端目前没有使用
    LOG_LEVEL_TRACE,
    //调试信息，客户端目前没有使用
    LOG_LEVEL_DEBUG,
    //一般信息，对应客户端的 LOG_INFO
    LOG_LEVEL_INFO,
    //警告，对应客户端的 LOG_WARNING
    LOG_LEVEL_WARNING,
    //业务错误，对应客户端的 LOG_ERROR
    LOG_LEVEL_ERROR,
    //技术框架本身的错误，客户端目前没有使用
    LOG_LEVEL_SYSERROR,
    //致命错误：同步写出后让程序主动崩溃，客户端目前没有使用
    LOG_LEVEL_FATAL,
    //关键信息：不受日志级别控制，总是输出，客户端目前没有使用
    LOG_LEVEL_CRITICAL
};

class LOG_API CAsyncLog
{
public:
    /**
     *  启动写日志线程。
     *  @param pszLogFileName   日志文件路径前缀（UTF-8），实际文件名为
     *                          "<前缀>.<年月日时分秒>.<进程 id>.log"；传 nullptr 或空串表示只写控制台
     *  @param bTruncateLongLine 是否把过长的日志正文截断到 256 个字符
     *  @param nRollSize        单个日志文件的最大字节数，写满后另起一个新文件
     *  @return 恒为 true
     *  副作用：创建写日志线程；日志文件由该线程在第一次写入前创建（所在目录须已存在）。
     */
    static bool init(const char* pszLogFileName = nullptr, bool bTruncateLongLine = false, int64_t nRollSize = 10 * 1024 * 1024);

    /**
     *  停止写日志线程。队列里已有的日志会先全部写完再退出，然后关闭日志文件。
     *  没有 init 过时调用也安全。之后可以再次 init。
     */
    static void uninit();

    //设置日志级别，低于该级别的日志不再输出；超出 TRACE 至 FATAL 范围的取值被忽略
    static void setLevel(LOG_LEVEL nLevel);

    //写日志线程是否已经启动
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

    /**
     *  等待调用本函数之前放进队列的日志全部写入文件。
     *
     *  用在进程即将异常结束、来不及走 uninit 的地方（崩溃处理、ExitProcess 之前），
     *  避免最后几行日志还在队列里就随进程一起丢掉。不关闭文件，之后仍可继续写日志。
     *
     *  @param nTimeoutMs 最长等待时间，单位毫秒
     *  @return 队列已写空返回 true；超时或写日志线程未启动时返回 false
     */
    static bool flush(int nTimeoutMs);

    //不输出所在函数签名与行号
    static bool output(long nLevel, const char* pszFmt, ...);
    //输出所在函数签名（或文件名）与行号
    static bool output(long nLevel, const char* pszFileName, int nLineNo, const char* pszFmt, ...);

    //以十六进制输出一段二进制数据，用于排查协议包
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
    //当前日志文件；由写日志线程创建与切换，uninit 时关闭
    static FILE*                            m_hLogFile;
    //日志文件路径前缀（UTF-8）；为空表示只写控制台
    static std::string                      m_strFileName;
    //文件名中的进程 id，便于区分同一目录下多个进程的日志
    static std::string                      m_strFileNamePID;
    //长日志是否截断
    static bool                             m_bTruncateLongLog;
    //当前日志级别
    static LOG_LEVEL                        m_nCurrentLevel;
    //单个日志文件的最大字节数
    static int64_t                          m_nFileRollSize;
    //当前日志文件已经写入的字节数
    static int64_t                          m_nCurrentWrittenSize;
    //待写入的日志行，受 m_mutexWrite 保护
    static std::list<std::string>           m_listLinesToWrite;
    //写日志线程；init 创建，uninit 等待其结束
    static std::unique_ptr<std::thread>     m_spWriteThread;
    //保护 m_listLinesToWrite、m_bExit、m_bWriting
    static std::mutex                       m_mutexWrite;
    //有新日志或需要退出时通知写日志线程
    static std::condition_variable          m_cvWrite;
    //队列写空时通知 flush() 的等待方
    static std::condition_variable          m_cvIdle;
    //退出标志，受 m_mutexWrite 保护
    static bool                             m_bExit;
    //运行标志
    static bool                             m_bRunning;
    //写日志线程是否正在写出一行（已从队列取出、尚未写完），受 m_mutexWrite 保护；
    //flush() 据此判断"队列为空"时最后一行是否真的已经写进文件
    static bool                             m_bWriting;
    //日志文件写入是否已停用。建文件或写文件失败时置 true，此后只写控制台。
    //只由写日志线程修改，其余线程只读，故不额外加锁
    static bool                             m_bFileWriteDisabled;
};

#endif // !__ASYNC_LOG_H__
