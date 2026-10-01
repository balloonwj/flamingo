/**
 *  客户端日志接口：LOG_INFO / LOG_WARNING / LOG_ERROR 三个宏，以及初始化、收尾与强制写出函数。
 *
 *  2026-10-01 起底层换成异步日志 CAsyncLog（拷贝自服务端，见 AsyncLog.h）：调用线程只做
 *  格式化并把日志行放进队列，由写日志线程写入文件，UI 线程打日志不再等待磁盘。
 *  此前的实现每写一行都同步调用 FlushFileBuffers，实测每行约 1.4 毫秒。
 *  本文件的三个宏与 CIULog 的接口保持不变，业务代码无需改动。
 *
 *  格式串可以是窄字符（"..."，按源文件的 UTF-8 原样写入），也可以是宽字符（_T("...") 或
 *  L"..."，格式化后转成 UTF-8 写入），日志文件因此统一为 UTF-8 编码。此前宽字符日志按系统
 *  代码页写入，同一个文件里混着两种编码。开发者自己写的日志文字仍按代码规范一律用英文。
 *
 *  每行格式：[级别][年-月-日 时:分:秒:毫秒][线程 id][函数签名:行号]正文
 *
 *  典型用法：
 *      CIULog::Init(true, false, L"D:\\Flamingo\\Logs\\flamingo");   // WinMain 开头
 *      LOG_INFO("login ok, userid=%d.", userid);
 *      LOG_ERROR(_T("open file failed: %s."), pszPath);
 *      CIULog::Uninit();                                             // WinMain 末尾
 *
 *  使用本头文件之前须已包含 <windows.h>（用到 PCSTR / PCWSTR）。
 *
 *  balloonwj@qq.com   2026-10-01（本次改写为异步日志；原文件的创建日期不详）
 */

#ifndef __LOG_H__
#define __LOG_H__

#include <atomic>
#include "AsyncLog.h"

#define LOG_INFO(...)       CIULog::Log(LOG_LEVEL_INFO, __FUNCSIG__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...)    CIULog::Log(LOG_LEVEL_WARNING, __FUNCSIG__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)      CIULog::Log(LOG_LEVEL_ERROR, __FUNCSIG__, __LINE__, __VA_ARGS__)

//进程即将异常结束（崩溃处理、ExitProcess）之前，等待队列里的日志写入文件的最长时间，单位毫秒。
//正常情况下队列里只有几行，微秒量级就能写完；这里只是防止磁盘异常时无限等待
const int kIULogFlushTimeoutMs = 2000;

//客户端日志的静态接口，底层由 CAsyncLog 实现。所有方法都是线程安全的
class CIULog
{
public:
    /**
     *  启动日志。必须在任何日志输出之前、进程启动的最早阶段调用。
     *
     *  @param bToFile               true 写入文件；false 只输出到控制台与调试器
     *  @param bTruncateLongLog      是否把过长的日志正文截断到 256 个字符
     *  @param pszLogFilePathPrefix  日志文件的完整路径前缀（不含扩展名），例如
     *                               "D:\\Flamingo\\Logs\\flamingo"；实际文件名为
     *                               "<前缀>.<年月日时分秒>.<进程 id>.log"，单个文件写满
     *                               10 MB 后另起一个新文件。所在目录不存在时会逐级创建。
     *                               bToFile 为 false 时忽略
     *  @return bToFile 为 true 而前缀为空时返回 false，其余情况返回 true。日志文件由写日志
     *          线程稍后创建，建不出来时退化为只输出到控制台，不会反映在返回值里
     *  副作用：创建日志目录与写日志线程。已经初始化过时先调用 Uninit 再重新初始化。
     */
    static bool Init(bool bToFile, bool bTruncateLongLog, PCWSTR pszLogFilePathPrefix);

    /**
     *  停止日志：先把队列里的日志全部写入文件，再结束写日志线程、关闭文件。
     *  之后的日志调用退化为只输出到调试器。没有初始化过时调用也安全。
     */
    static void Uninit();

    /**
     *  按配置文件 [app] loglevel 的取值设置日志级别，低于该级别的日志不再输出。
     *  @param nConfigLevel 0 或更小：输出全部日志（INFO 及以上）；1：只输出 WARNING 与 ERROR；
     *                      2 或更大：只输出 ERROR
     */
    static void SetLevel(int nConfigLevel);

    /**
     *  等待此前的日志全部写入文件，不关闭文件，之后仍可继续写日志。
     *  用在进程即将异常结束、来不及走 Uninit 的地方（崩溃处理、ExitProcess 之前）。
     *  @param nTimeoutMs 最长等待时间，单位毫秒，通常传 kIULogFlushTimeoutMs
     *  @return 全部写完返回 true；超时或日志未初始化时返回 false
     */
    static bool Flush(int nTimeoutMs);

    /**
     *  输出一行日志（宽字符格式串）。一般通过 LOG_INFO 等宏调用。
     *  @param nLevel          日志级别，LOG_LEVEL_INFO / LOG_LEVEL_WARNING / LOG_LEVEL_ERROR
     *  @param pszFunctionSig  所在函数的签名（__FUNCSIG__，窄字符）
     *  @param nLineNo         所在行号
     *  @param pszFmt          宽字符格式串，%s 对应宽字符串参数；格式化结果转成 UTF-8 写入
     *  @return 已放入写日志队列（或已输出到调试器）返回 true；低于当前日志级别被过滤时返回 false
     */
    static bool Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCWSTR pszFmt, ...);

    /**
     *  输出一行日志（窄字符格式串），参数含义同上；格式化结果按原字节写入（源文件为 UTF-8）。
     */
    static bool Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...);

private:
    CIULog() = delete;
    ~CIULog() = delete;

    CIULog(const CIULog& rhs) = delete;
    CIULog& operator=(const CIULog& rhs) = delete;

    //把已经格式化好的正文交给 CAsyncLog；未初始化时直接输出到调试器
    static bool Emit(long nLevel, PCSTR pszFunctionSig, int nLineNo, const std::string& strMsg);

private:
    //是否已经 Init 且尚未 Uninit。未初始化期间的日志不进入队列（没有写日志线程，
    //进了队列就不会被写出），直接输出到调试器，与改写前的行为一致
    static std::atomic<bool>    m_bInited;
    //当前日志级别，与 CAsyncLog 里的级别一致；格式化之前先据此过滤，省掉被丢弃日志的格式化开销
    static std::atomic<long>    m_nLevel;
};

#endif // !__LOG_H__
