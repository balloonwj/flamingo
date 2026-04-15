/**
 * @desc:   异步日志类，AsyncLog.cpp
 * @author: zhangyl
 * @date:   2019.04.13
 */
#include "AsyncLog.h"
#include <ctime>
#include <time.h>
#include <sys/timeb.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <iostream>
#include <stdarg.h>
#include "../base/Platform.h"

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
bool CAsyncLog::CAsyncLog::m_bExit = false;
bool CAsyncLog::m_bRunning = false;

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

    //TODO：创建文件夹

    m_spWriteThread.reset(new std::thread(writeThreadProc));

    return true;
}

void CAsyncLog::uninit()
{
    m_bExit = true;

    m_cvWrite.notify_one();

    if (m_spWriteThread->joinable())
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
        std::cout << strLine << std::endl;
#ifdef _WIN32
        OutputDebugStringA(strLine.c_str());
        OutputDebugStringA("\n");
#endif
        
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
        std::cout << strLine << std::endl;
#ifdef _WIN32
        OutputDebugStringA(strLine.c_str());
        OutputDebugStringA("\n");
#endif

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
    os << "address[" << (long)buffer << "] size[" << size << "] \n";
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
            headlen = strlen(szhead);
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
    
    snprintf(pszTime, nTimeStrLength, "[%04d-%02d-%02d %02d:%02d:%02d:%03d]", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, tp.millitm);
    //strftime(pszTime, nTimeStrLength, "[%Y-%m-%d %H:%M:%S:]", &time);
}

bool CAsyncLog::createNewFile(const char* pszLogFileName)
{
    if (m_hLogFile != nullptr)
    {
        fclose(m_hLogFile);
    }

    //始终新建文件
    m_hLogFile = fopen(pszLogFileName, "w+");
    return m_hLogFile != nullptr;
}

bool CAsyncLog::writeToFile(const std::string& data)
{
    //为了防止长文件一次性写不完，放在一个循环里面分批写
    std::string strLocal(data);
    int ret = 0;
    while (true)
    {
        ret = fwrite(strLocal.c_str(), 1, strLocal.length(), m_hLogFile);
        if (ret <= 0)
            return false;
        else if (ret <= (int)strLocal.length())
        {
            strLocal.erase(0, ret);
        }

        if (strLocal.empty())
            break;
    }
    

    //::OutputDebugStringA(strDebugInfo.c_str());

    fflush(m_hLogFile);

    return true;
}

void CAsyncLog::crash()
{
    char* p = nullptr;
    *p = 0;
}

void CAsyncLog::writeThreadProc()
{
    m_bRunning = true;

    while (true)
    {        
        if (!m_strFileName.empty())
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
                if (!createNewFile(strNewFileName.c_str()))
                    return;
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
        }

        
        std::cout << strLine << std::endl;

#ifdef _WIN32
        OutputDebugStringA(strLine.c_str());
        OutputDebugStringA("\n");
#endif

        if (!m_strFileName.empty())
        {
            if (!writeToFile(strLine))
                return;

            m_nCurrentWrittenSize += strLine.length();
        }     
    }// end outer-while-loop

    m_bRunning = false;
}