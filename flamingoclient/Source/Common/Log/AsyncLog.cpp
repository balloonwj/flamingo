/**
 *  异步日志类的实现。拷贝自服务端 flamingoserver/base/AsyncLog.cpp（原作者 zhangyl，
 *  2019-04-13），与服务端原版的差异见 AsyncLog.h 文件头，改动处均以"客户端差异 N"标注。
 *
 *  <u>本文件被 FlamingoUnit 测试工程直接编译</u>，故不走主工程的预编译头。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include <windows.h>
#include "AsyncLog.h"
#include <ctime>
#include <time.h>
#include <sys/timeb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sstream>
#include <iostream>
#include <stdarg.h>
#include <chrono>

#define MAX_LINE_LENGTH   256
#define DEFAULT_ROLL_SIZE 10 * 1024 * 1024

bool CAsyncLog::m_bTruncateLongLog = false;
FILE* CAsyncLog::m_hLogFile = NULL;
std::string CAsyncLog::m_strFileName = "default";
std::string CAsyncLog::m_strFileNamePID = "";
LOG_LEVEL CAsyncLog::m_nCurrentLevel = LOG_LEVEL_INFO;
int64_t CAsyncLog::m_nFileRollSize = DEFAULT_ROLL_SIZE;
int64_t CAsyncLog::m_nCurrentWrittenSize = 0;
std::list<std::string> CAsyncLog::m_listLinesToWrite;
std::unique_ptr<std::thread> CAsyncLog::m_spWriteThread;
std::mutex CAsyncLog::m_mutexWrite;
std::condition_variable CAsyncLog::m_cvWrite;
std::condition_variable CAsyncLog::m_cvIdle;
bool CAsyncLog::m_bExit = false;
bool CAsyncLog::m_bRunning = false;
bool CAsyncLog::m_bWriting = false;
bool CAsyncLog::m_bFileWriteDisabled = false;

bool CAsyncLog::init(const char* pszLogFileName/* = nullptr*/, bool bTruncateLongLine/* = false*/, int64_t nRollSize/* = 10 * 1024 * 1024*/)
{
    m_bTruncateLongLog = bTruncateLongLine;
    m_nFileRollSize = nRollSize;

    if (pszLogFileName == nullptr || pszLogFileName[0] == 0)
    {
        m_strFileName.clear();
    }
    else
        m_strFileName = pszLogFileName;

    //获取进程id，这样快速看到同一个进程的不同日志文件
    char szPID[8];
#ifdef WIN32
    snprintf(szPID, sizeof(szPID), "%05d", (int)::GetCurrentProcessId());
#else
    snprintf(szPID, sizeof(szPID), "%05d", (int)::getpid());
#endif
    m_strFileNamePID = szPID;

    //日志目录由调用方（CIULog::Init）负责创建

    //退出标志与文件停用标志都要复位，否则 uninit 之后再次 init 起来的写日志线程
    //会因为看到上一轮遗留的 m_bExit 而立即退出，整个进程再也打不出日志
    m_bExit = false;
    m_bWriting = false;
    m_bFileWriteDisabled = false;

    m_spWriteThread.reset(new std::thread(writeThreadProc));

    return true;
}

void CAsyncLog::uninit()
{
    //客户端差异 5：退出标志必须在持有互斥量时修改。写日志线程是在持锁状态下检查
    //m_bExit、然后在同一把锁上 wait 的；若这里不加锁，本线程的赋值与 notify 可能恰好
    //落在对方"检查完、还没开始 wait"的间隙里，这次通知就会丢失，对方一直等下去，
    //下面的 join 也就一直返回不了（标准库 condition_variable 的文档对此有明确要求）
    {
        std::lock_guard<std::mutex> guard(m_mutexWrite);
        m_bExit = true;
    }

    m_cvWrite.notify_one();

    //没有 init 过就调用 uninit 时 m_spWriteThread 是空的，不能直接解引用。
    //进程退出路径上多处都会调 uninit，容不得它自己再崩一次
    if (m_spWriteThread && m_spWriteThread->joinable())
        m_spWriteThread->join();

    if(m_hLogFile != nullptr)
    {
        fclose(m_hLogFile);
        m_hLogFile = nullptr;
    }
}

void CAsyncLog::setLevel(LOG_LEVEL nLevel)
{
    if (nLevel < LOG_LEVEL_TRACE || nLevel > LOG_LEVEL_FATAL)
        return;

    m_nCurrentLevel = nLevel;
}

bool CAsyncLog::isRunning()
{
    return m_bRunning;
}

bool CAsyncLog::isFileWriteDisabled()
{
    return m_bFileWriteDisabled;
}

//客户端差异 2
bool CAsyncLog::flush(int nTimeoutMs)
{
    std::unique_lock<std::mutex> guard(m_mutexWrite);

    //写日志线程没有启动或已经在退出：不会再有人把队列写空，等待没有意义
    if (!m_spWriteThread || m_bExit)
        return m_listLinesToWrite.empty() && !m_bWriting;

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(nTimeoutMs);
    while (!m_listLinesToWrite.empty() || m_bWriting)
    {
        if (m_cvIdle.wait_until(guard, deadline) == std::cv_status::timeout)
            return m_listLinesToWrite.empty() && !m_bWriting;
    }

    return true;
}

bool CAsyncLog::output(long nLevel, const char* pszFmt, ...)
{
    if (nLevel != LOG_LEVEL_CRITICAL)
    {
        if (nLevel < m_nCurrentLevel)
            return false;
    }

    std::string strLine;
    makeLinePrefix(nLevel, strLine);

    //log正文
    std::string strLogMsg;

    //先计算一下不定参数的长度，以便于分配空间
    va_list ap;
    va_start(ap, pszFmt);
    int nLogMsgLength = vsnprintf(NULL, 0, pszFmt, ap);
    va_end(ap);

    //容量必须算上最后一个\0
    if ((int)strLogMsg.capacity() < nLogMsgLength + 1)
    {
        strLogMsg.resize(nLogMsgLength + 1);
    }
    va_list aq;
    va_start(aq, pszFmt);
    vsnprintf((char*)strLogMsg.data(), strLogMsg.capacity(), pszFmt, aq);
    va_end(aq);

    //string内容正确但length不对，恢复一下其length
    std::string strMsgFormal;
    strMsgFormal.append(strLogMsg.c_str(), nLogMsgLength);

    //如果日志开启截断，长日志只取前MAX_LINE_LENGTH个字符
    if (m_bTruncateLongLog)
        strMsgFormal = strMsgFormal.substr(0, MAX_LINE_LENGTH);

    strLine += strMsgFormal;

    //不是输出到控制台才会在每一行末尾加一个换行符
    if (!m_strFileName.empty())
    {
        strLine += "\n";
    }

    if (nLevel != LOG_LEVEL_FATAL)
    {
        std::lock_guard<std::mutex> lock_guard(m_mutexWrite);
        m_listLinesToWrite.push_back(strLine);
        m_cvWrite.notify_one();
    }
    else
    {
        //为了让FATAL级别的日志能立即crash程序，采取同步写日志的方法
        writeToConsole(strLine);

        if (!m_strFileName.empty())
        {
            if (m_hLogFile == nullptr)
            {
                //新建文件
                char szNow[64];
                time_t now = time(NULL);
                tm time;
#ifdef _WIN32
                localtime_s(&time, &now);
#else
                localtime_r(&now, &time);
#endif
                strftime(szNow, sizeof(szNow), "%Y%m%d%H%M%S", &time);

                std::string strNewFileName(m_strFileName);
                strNewFileName += ".";
                strNewFileName += szNow;
                strNewFileName += ".";
                strNewFileName += m_strFileNamePID;
                strNewFileName += ".log";
                if (!createNewFile(strNewFileName.c_str()))
                    return false;
            }// end inner if

           writeToFile(strLine);

        }// end outer-if

        //让程序主动crash掉
        crash();
    }


    return true;
}

bool CAsyncLog::output(long nLevel, const char* pszFileName, int nLineNo, const char* pszFmt, ...)
{
    if (nLevel != LOG_LEVEL_CRITICAL)
    {
        if (nLevel < m_nCurrentLevel)
            return false;
    }

    std::string strLine;
    makeLinePrefix(nLevel, strLine);

    //函数签名
    char szFileName[512] = { 0 };
    snprintf(szFileName, sizeof(szFileName), "[%s:%d]", pszFileName, nLineNo);
    strLine += szFileName;

    //日志正文
    std::string strLogMsg;

    //先计算一下不定参数的长度，以便于分配空间
    va_list ap;
    va_start(ap, pszFmt);
    int nLogMsgLength = vsnprintf(NULL, 0, pszFmt, ap);
    va_end(ap);

    //容量必须算上最后一个\0
    if ((int)strLogMsg.capacity() < nLogMsgLength + 1)
    {
        strLogMsg.resize(nLogMsgLength + 1);
    }
    va_list aq;
    va_start(aq, pszFmt);
    vsnprintf((char*)strLogMsg.data(), strLogMsg.capacity(), pszFmt, aq);
    va_end(aq);

    //string内容正确但length不对，恢复一下其length
    std::string strMsgFormal;
    strMsgFormal.append(strLogMsg.c_str(), nLogMsgLength);

    //如果日志开启截断，长日志只取前MAX_LINE_LENGTH个字符
    if (m_bTruncateLongLog)
        strMsgFormal = strMsgFormal.substr(0, MAX_LINE_LENGTH);

    strLine += strMsgFormal;

    //不是输出到控制台才会在每一行末尾加一个换行符
    if (!m_strFileName.empty())
    {
        strLine += "\n";
    }

    if (nLevel != LOG_LEVEL_FATAL)
    {
        std::lock_guard<std::mutex> lock_guard(m_mutexWrite);
        m_listLinesToWrite.push_back(strLine);
        m_cvWrite.notify_one();
    }
    else
    {
        //为了让FATAL级别的日志能立即crash程序，采取同步写日志的方法
        writeToConsole(strLine);

        if (!m_strFileName.empty())
        {
            if (m_hLogFile == nullptr)
            {
                //新建文件
                char szNow[64];
                time_t now = time(NULL);
                tm time;
#ifdef _WIN32
                localtime_s(&time, &now);
#else
                localtime_r(&now, &time);
#endif
                strftime(szNow, sizeof(szNow), "%Y%m%d%H%M%S", &time);

                std::string strNewFileName(m_strFileName);
                strNewFileName += ".";
                strNewFileName += szNow;
                strNewFileName += ".";
                strNewFileName += m_strFileNamePID;
                strNewFileName += ".log";
                if (!createNewFile(strNewFileName.c_str()))
                    return false;
            }// end inner if

            writeToFile(strLine);
        }// end outer-if

        //让程序主动crash掉
        crash();
    }

    return true;
}

bool CAsyncLog::outputBinary(unsigned char* buffer, size_t size)
{
    //std::string strBinary;
    std::ostringstream os;

    static const size_t PRINTSIZE = 512;
    char szbuf[PRINTSIZE * 3 + 8];

    size_t lsize = 0;
    size_t lprintbufsize = 0;
    int index = 0;
    os << "address[" << static_cast<const void*>(buffer) << "] size[" << size << "] \n";
    while (true)
    {
        memset(szbuf, 0, sizeof(szbuf));
        if (size > lsize)
        {
            lprintbufsize = (size - lsize);
            lprintbufsize = lprintbufsize > PRINTSIZE ? PRINTSIZE : lprintbufsize;
            formLog(index, szbuf, sizeof(szbuf), buffer + lsize, lprintbufsize);
            size_t len = strlen(szbuf);

            //if (stream().buffer().avail() < static_cast<int>(len))
            //{
            //    stream() << szbuf + (len - stream().buffer().avail());
            //    break;
            //}
            //else
            //{
            os << szbuf;
            //}
            lsize += lprintbufsize;
        }
        else
        {
            break;
        }
    }

    std::lock_guard<std::mutex> lock_guard(m_mutexWrite);
    m_listLinesToWrite.push_back(os.str());
    m_cvWrite.notify_one();

    return true;
}

const char* CAsyncLog::ullto4Str(int n)
{
    static char buf[64 + 1];
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%06u", n);
    return buf;
}

char* CAsyncLog::formLog(int& index, char* szbuf, size_t size_buf, unsigned char* buffer, size_t size)
{
    size_t len = 0;
    size_t lsize = 0;
    int headlen = 0;
    char szhead[64 + 1] = { 0 };
    char szchar[17] = "0123456789abcdef";
    //memset(szhead, 0, sizeof(szhead));
    while (size > lsize && len + 10 < size_buf)
    {
        if (lsize % 32 == 0)
        {
            if (0 != headlen)
            {
                szbuf[len++] = '\n';
            }

            memset(szhead, 0, sizeof(szhead));
            strncpy(szhead, ullto4Str(index++), sizeof(szhead) - 1);
            headlen = static_cast<int>(strlen(szhead));
            szhead[headlen++] = ' ';

            strcat(szbuf, szhead);
            len += headlen;

        }
        if (lsize % 16 == 0 && 0 != headlen)
            szbuf[len++] = ' ';
        szbuf[len++] = szchar[(buffer[lsize] >> 4) & 0xf];
        szbuf[len++] = szchar[(buffer[lsize]) & 0xf];
        lsize++;
    }
    szbuf[len++] = '\n';
    szbuf[len++] = '\0';
    return szbuf;
}

void CAsyncLog::makeLinePrefix(long nLevel, std::string& strPrefix)
{
    //级别
    strPrefix = "[INFO]";
    if (nLevel == LOG_LEVEL_TRACE)
        strPrefix = "[TRACE]";
    else if (nLevel == LOG_LEVEL_DEBUG)
        strPrefix = "[DEBUG]";
    else if (nLevel == LOG_LEVEL_WARNING)
        strPrefix = "[WARN]";
    else if (nLevel == LOG_LEVEL_ERROR)
        strPrefix = "[ERROR]";
    else if (nLevel == LOG_LEVEL_SYSERROR)
        strPrefix = "[SYSE]";
    else if (nLevel == LOG_LEVEL_FATAL)
        strPrefix = "[FATAL]";
    else if (nLevel == LOG_LEVEL_CRITICAL)
        strPrefix = "[CRITICAL]";

    //时间
    char szTime[64] = { 0 };
    getTime(szTime, sizeof(szTime));

    strPrefix += "[";
    strPrefix += szTime;
    strPrefix += "]";

    //当前线程信息
    char szThreadID[32] = { 0 };
    std::ostringstream osThreadID;
    osThreadID << std::this_thread::get_id();
    snprintf(szThreadID, sizeof(szThreadID), "[%s]", osThreadID.str().c_str());
    strPrefix += szThreadID;
}

void CAsyncLog::getTime(char* pszTime, int nTimeStrLength)
{
    struct timeb tp;
    ftime(&tp);

    time_t now = tp.time;
    tm time;
#ifdef _WIN32
    localtime_s(&time, &now);
#else
    localtime_r(&now, &time);
#endif

    //这里只输出时间本身，外层的方括号由调用方 makeLinePrefix() 统一添加，
    //否则会出现 [[2026-08-03 18:30:15:725]] 这样被两层方括号包裹的日志
    snprintf(pszTime, nTimeStrLength, "%04d-%02d-%02d %02d:%02d:%02d:%03d", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, tp.millitm);
    //strftime(pszTime, nTimeStrLength, "%Y-%m-%d %H:%M:%S:", &time);
}

bool CAsyncLog::createNewFile(const char* pszLogFileName)
{
    if (m_hLogFile != nullptr)
    {
        fclose(m_hLogFile);
        m_hLogFile = nullptr;
    }

    //客户端差异 3：文件名是 UTF-8，转成宽字符后用 _wfopen 打开。fopen 按系统代码页
    //解释窄字符串，安装目录含有代码页之外的字符时会打不开文件
    const int nWideLen = ::MultiByteToWideChar(CP_UTF8, 0, pszLogFileName, -1, NULL, 0);
    if (nWideLen <= 0)
        return false;

    std::wstring strWideFileName((size_t)nWideLen, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, pszLogFileName, -1, &strWideFileName[0], nWideLen);

    //始终新建文件
    m_hLogFile = _wfopen(strWideFileName.c_str(), L"w+");
    return m_hLogFile != nullptr;
}

void CAsyncLog::disableFileWrite(const char* pszLogFileName)
{
    m_bFileWriteDisabled = true;

    if (m_hLogFile != nullptr)
    {
        fclose(m_hLogFile);
        m_hLogFile = nullptr;
    }

    //这条提示不能用 LOGE 之类打出去：出问题的正是写日志本身，
    //绕回自己只会让提示跟着丢掉，所以直接写标准错误
    fprintf(stderr, "[AsyncLog] cannot write log file \"%s\" (%s), "
                    "fall back to console output only\n",
            pszLogFileName != nullptr ? pszLogFileName : "", strerror(errno));
    fflush(stderr);
}

bool CAsyncLog::writeToFile(const std::string& data)
{
    //为了防止长文件一次性写不完，放在一个循环里面分批写
    std::string strLocal(data);
    //fwrite 的返回值是 size_t，别用 int 接：x64 下会被窄化，
    //一次写入的字节数虽然不可能超过 int，但类型该对上
    size_t ret = 0;
    while (true)
    {
        ret = fwrite(strLocal.c_str(), 1, strLocal.length(), m_hLogFile);
        if (ret == 0)
            return false;
        else if (ret <= strLocal.length())
        {
            strLocal.erase(0, ret);
        }

        if (strLocal.empty())
            break;
    }


    //::OutputDebugStringA(strDebugInfo.c_str());

    //只把 C 运行库的缓冲交给操作系统，不强制落盘：进程崩溃时操作系统缓存里的数据
    //照样会写进文件，只有整机断电才可能丢失，这与原客户端逐行 FlushFileBuffers 的
    //代价（每行约 1.4 毫秒）相比完全值得
    fflush(m_hLogFile);

    return true;
}

void CAsyncLog::crash()
{
    char* p = nullptr;
    *p = 0;
}

void CAsyncLog::writeToConsole(const std::string& strLine)
{
    //启用日志文件时 strLine 末尾自带一个 '\n'（写文件用的），此时不能再补换行，
    //否则终端里每条日志之间会多空一行。未启用文件时末尾没有换行，需要自己补。
    const bool bEndsWithNewline = !strLine.empty() && strLine[strLine.size() - 1] == '\n';

    if (bEndsWithNewline)
        std::cout << strLine << std::flush;
    else
        std::cout << strLine << std::endl;

#ifdef _WIN32
    OutputDebugStringA(strLine.c_str());
    if (!bEndsWithNewline)
        OutputDebugStringA("\n");
#endif
}

void CAsyncLog::writeThreadProc()
{
    m_bRunning = true;

    while (true)
    {
        if (!m_strFileName.empty() && !m_bFileWriteDisabled)
        {
            if (m_hLogFile == nullptr || m_nCurrentWrittenSize >= m_nFileRollSize)
            {
                //重置m_nCurrentWrittenSize大小
                m_nCurrentWrittenSize = 0;

                //第一次或者文件大小超过rollsize，均新建文件
                char szNow[64];
                time_t now = time(NULL);
                tm time;
#ifdef _WIN32
                localtime_s(&time, &now);
#else
                localtime_r(&now, &time);
#endif
                strftime(szNow, sizeof(szNow), "%Y%m%d%H%M%S", &time);

                std::string strNewFileName(m_strFileName);
                strNewFileName += ".";
                strNewFileName += szNow;
                strNewFileName += ".";
                strNewFileName += m_strFileNamePID;
                strNewFileName += ".log";
                //建不出日志文件不能让本线程就此结束：它是唯一一个把日志队列写出去的线程，
                //一旦退出，连控制台也不再有任何输出，进程却还在正常跑，
                //现象就是「服务活着但一行日志都没有」，极难定位。
                //这里退化为只写控制台并说明一次原因，让服务继续可观测
                if (!createNewFile(strNewFileName.c_str()))
                {
                    disableFileWrite(strNewFileName.c_str());
                }
            }// end inner if
        }// end outer-if


        std::string strLine;
        {
            std::unique_lock<std::mutex> guard(m_mutexWrite);
            while (m_listLinesToWrite.empty())
            {
                if (m_bExit)
                    return;

                m_cvWrite.wait(guard);
            }

            strLine = m_listLinesToWrite.front();
            m_listLinesToWrite.pop_front();
            m_bWriting = true;
        }

        //客户端差异 4：写文件时不再同时输出到控制台与调试器，
        //只有未启用文件、或文件写入已停用时才输出到这两处
        const bool bToFile = !m_strFileName.empty() && !m_bFileWriteDisabled;
        if (!bToFile)
            writeToConsole(strLine);

        if (bToFile)
        {
            //写失败同样只停用文件写入，理由与上面建文件失败时一致
            if (!writeToFile(strLine))
            {
                disableFileWrite(m_strFileName.c_str());
            }
            else
            {
                m_nCurrentWrittenSize += strLine.length();
            }
        }

        //这一行已经写完；队列同时为空时通知 flush() 的等待方（客户端差异 2）
        {
            std::lock_guard<std::mutex> guard(m_mutexWrite);
            m_bWriting = false;
            if (m_listLinesToWrite.empty())
                m_cvIdle.notify_all();
        }
    }// end outer-while-loop

    m_bRunning = false;
}
