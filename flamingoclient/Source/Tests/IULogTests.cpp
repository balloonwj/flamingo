/**
 *  IULogTests.cpp
 *
 *  客户端日志（CIULog，底层为异步日志 CAsyncLog）的测试（标签 [iulog]）。
 *
 *  覆盖：
 *    LOG1  连续打 2000 行日志，调用线程上每次调用的平均耗时低于 0.1 毫秒
 *          （改写前每行同步 FlushFileBuffers，实测约 1.4 毫秒）
 *    LOG2  Uninit 之前打的日志全部写入文件、顺序不变；日志目录不存在时逐级创建
 *    LOG3  四个线程并发打日志，每一行都恰好出现一次、内容完整
 *    LOG4  宽字符与窄字符格式串的日志都以 UTF-8 写入，整个文件是合法的 UTF-8
 *    LOG5  Flush 返回之后（不 Uninit），此前打的日志已经全部在文件里
 *    LOG6  日志级别按配置取值过滤：0 全部输出，1 只输出 WARNING 与 ERROR，2 只输出 ERROR
 *    LOG7  初始化之前打的日志不进入队列，之后初始化出来的文件里没有它；未初始化时 Flush 返回 false
 *    LOG8  Init / Uninit 可以反复进行；连续两次 Init 不会崩溃
 *    LOG9  每行格式为 [INFO][年-月-日 时:分:秒:毫秒][线程 id][函数签名:行号]正文
 *    LOG10 日志目录路径含中文时照样能建出文件并写入
 *
 *  每个用例在 %TEMP% 下用各自的目录（目录名带进程 id，两个测试进程同时跑也不冲突），
 *  用例结束时 Uninit 并删掉该目录。日志模块是进程级的全局状态，用例之间按顺序执行，
 *  每个用例都必须以 Uninit 结束，否则后面的用例会写进别人的文件。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../Common/Log/IULog.h"
#include "../Common/Util/PerfStopWatch.h"

namespace {

//LOG1 的日志行数
const int kLog1Lines = 2000;
//LOG1 每次调用平均耗时的上限，单位毫秒。实测为微秒量级；改写前同步刷盘约 1.4 毫秒
const double kLog1MaxAvgMs = 0.1;
//LOG2 的日志行数
const int kLog2Lines = 500;
//LOG3 的线程数与每个线程的日志行数
const int kLog3Threads = 4;
const int kLog3LinesPerThread = 300;
//LOG5 的日志行数
const int kLog5Lines = 100;
//配置文件 loglevel 的三档取值，含义见 CIULog::SetLevel
const int kConfigLevelAll = 0;
const int kConfigLevelWarning = 1;
const int kConfigLevelError = 2;

//本用例专用的临时目录：%TEMP%\flamingo_iulog_<进程 id>_<用例名>，末尾不带分隔符
std::wstring MakeCaseDir(const wchar_t* pszCase)
{
    wchar_t szTemp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, szTemp);
    wchar_t szDir[MAX_PATH] = { 0 };
    swprintf_s(szDir, L"%sflamingo_iulog_%u_%s", szTemp, (unsigned)::GetCurrentProcessId(), pszCase);
    return szDir;
}

//列出目录下所有 "<前缀名>.*.log" 文件的完整路径
std::vector<std::wstring> FindLogFiles(const std::wstring& strDir, const wchar_t* pszPrefixName)
{
    std::vector<std::wstring> files;
    const std::wstring strPattern = strDir + L"\\" + pszPrefixName + L".*.log";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW(strPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return files;
    do
    {
        files.push_back(strDir + L"\\" + fd.cFileName);
    } while (::FindNextFileW(hFind, &fd));
    ::FindClose(hFind);
    return files;
}

//读出文件的全部字节。以共享读写方式打开，写日志线程还开着文件时也能读
std::string ReadAllBytes(const std::wstring& strPath)
{
    std::string data;
    HANDLE hFile = ::CreateFileW(strPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return data;
    char buf[4096];
    DWORD dwRead = 0;
    while (::ReadFile(hFile, buf, sizeof(buf), &dwRead, NULL) && dwRead > 0)
        data.append(buf, dwRead);
    ::CloseHandle(hFile);
    return data;
}

//按换行拆行，去掉行尾的 '\r'，丢弃末尾的空行
std::vector<std::string> SplitLines(const std::string& data)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < data.size())
    {
        size_t end = data.find('\n', start);
        if (end == std::string::npos)
            end = data.size();
        std::string line = data.substr(start, end - start);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (!line.empty())
            lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

//读出目录下该前缀全部日志文件的所有行（通常只有一个文件）
std::vector<std::string> ReadLogLines(const std::wstring& strDir, const wchar_t* pszPrefixName)
{
    std::vector<std::string> lines;
    const std::vector<std::wstring> files = FindLogFiles(strDir, pszPrefixName);
    for (size_t i = 0; i < files.size(); ++i)
    {
        const std::vector<std::string> part = SplitLines(ReadAllBytes(files[i]));
        lines.insert(lines.end(), part.begin(), part.end());
    }
    return lines;
}

//统计包含指定子串的行数
int CountLinesContaining(const std::vector<std::string>& lines, const std::string& strNeedle)
{
    int n = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].find(strNeedle) != std::string::npos)
            ++n;
    }
    return n;
}

//删除目录及其下的文件（只有一层子目录的情况）
void RemoveDirTree(const std::wstring& strDir)
{
    WIN32_FIND_DATAW fd;
    HANDLE hFind = ::FindFirstFileW((strDir + L"\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::wstring strName(fd.cFileName);
            if (strName == L"." || strName == L"..")
                continue;
            const std::wstring strPath = strDir + L"\\" + strName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveDirTree(strPath);
            else
                ::DeleteFileW(strPath.c_str());
        } while (::FindNextFileW(hFind, &fd));
        ::FindClose(hFind);
    }
    ::RemoveDirectoryW(strDir.c_str());
}

//用例开始时清掉上次残留的目录，结束时 Uninit 并删除目录。
//日志级别也在结束时恢复为默认，避免影响后面的用例
struct LogCaseScope
{
    //本用例的临时目录
    std::wstring m_strDir;

    explicit LogCaseScope(const wchar_t* pszCase) : m_strDir(MakeCaseDir(pszCase))
    {
        CIULog::Uninit();
        RemoveDirTree(m_strDir);
    }

    ~LogCaseScope()
    {
        CIULog::Uninit();
        CIULog::SetLevel(kConfigLevelAll);
        RemoveDirTree(m_strDir);
    }

    //本用例日志文件的路径前缀：<目录>\<前缀名>
    std::wstring Prefix(const wchar_t* pszPrefixName) const
    {
        return m_strDir + L"\\" + pszPrefixName;
    }
};

//LOG3 的线程入口：打 kLog3LinesPerThread 行带线程编号与序号的日志
void Log3ThreadProc(int nThreadIndex)
{
    for (int i = 0; i < kLog3LinesPerThread; ++i)
        LOG_INFO("log3 thread=%d seq=%d end", nThreadIndex, i);
}

//判断一段字节是否为合法的 UTF-8
bool IsValidUtf8(const std::string& data)
{
    if (data.empty())
        return true;
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data(), (int)data.size(), NULL, 0) > 0;
}

} // namespace

TEST_CASE("LOG1 连续打 2000 行日志，调用线程上每次调用平均耗时低于 0.1 毫秒", "[iulog]")
{
    LogCaseScope scope(L"log1");
    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"t").c_str()));

    PerfStopWatch sw;
    for (int i = 0; i < kLog1Lines; ++i)
        LOG_INFO("log1 benchmark line seq=%d, peer=%u, count=%u.", i, 5308u, 50u);
    const double avgMs = sw.ElapsedMs() / kLog1Lines;

    CIULog::Uninit();
    const std::vector<std::string> lines = ReadLogLines(scope.m_strDir, L"t");

    //把实测数字打到标准输出，通过时也能看到
    printf("[LOG1] %d log calls on the calling thread: average %.4f ms per call\n", kLog1Lines, avgMs);
    CHECK(CountLinesContaining(lines, "log1 benchmark line") == kLog1Lines);
    CHECK(avgMs < kLog1MaxAvgMs);
}

TEST_CASE("LOG2 Uninit 之前的日志全部写入且顺序不变，日志目录逐级创建", "[iulog]")
{
    LogCaseScope scope(L"log2");
    //两级都不存在的子目录
    const std::wstring strNestedDir = scope.m_strDir + L"\\a\\b";
    REQUIRE(CIULog::Init(true, false, (strNestedDir + L"\\t").c_str()));

    for (int i = 0; i < kLog2Lines; ++i)
        LOG_INFO("log2 seq=%d end", i);
    CIULog::Uninit();

    const std::vector<std::string> lines = ReadLogLines(strNestedDir, L"t");
    std::vector<int> seqs;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const size_t pos = lines[i].find("log2 seq=");
        if (pos == std::string::npos)
            continue;
        int seq = -1;
        sscanf_s(lines[i].c_str() + pos, "log2 seq=%d", &seq);
        seqs.push_back(seq);
    }

    REQUIRE(seqs.size() == (size_t)kLog2Lines);
    bool bInOrder = true;
    for (int i = 0; i < kLog2Lines; ++i)
    {
        if (seqs[(size_t)i] != i)
            bInOrder = false;
    }
    CHECK(bInOrder);
}

TEST_CASE("LOG3 四个线程并发打日志，每一行恰好出现一次且内容完整", "[iulog]")
{
    LogCaseScope scope(L"log3");
    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"t").c_str()));

    std::vector<std::thread> threads;
    for (int t = 0; t < kLog3Threads; ++t)
        threads.push_back(std::thread(Log3ThreadProc, t));
    for (size_t t = 0; t < threads.size(); ++t)
        threads[t].join();
    CIULog::Uninit();

    const std::vector<std::string> lines = ReadLogLines(scope.m_strDir, L"t");
    std::set<std::pair<int, int> > seen;
    int nMalformed = 0;
    int nDuplicated = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const size_t pos = lines[i].find("log3 thread=");
        if (pos == std::string::npos)
            continue;
        int nThread = -1;
        int nSeq = -1;
        //内容完整：行以 [INFO] 开头，以 " end" 结尾，中间两个编号都解析得出来
        const bool bParsed = sscanf_s(lines[i].c_str() + pos, "log3 thread=%d seq=%d", &nThread, &nSeq) == 2;
        const std::string& line = lines[i];
        const bool bFramed = line.compare(0, 6, "[INFO]") == 0 &&
                             line.size() >= 4 && line.compare(line.size() - 4, 4, " end") == 0;
        if (!bParsed || !bFramed)
        {
            ++nMalformed;
            continue;
        }
        if (!seen.insert(std::make_pair(nThread, nSeq)).second)
            ++nDuplicated;
    }

    CHECK(nMalformed == 0);
    CHECK(nDuplicated == 0);
    CHECK(seen.size() == (size_t)(kLog3Threads * kLog3LinesPerThread));
}

TEST_CASE("LOG4 宽字符与窄字符格式串都以 UTF-8 写入，文件是合法的 UTF-8", "[iulog]")
{
    LogCaseScope scope(L"log4");
    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"t").c_str()));

    //本文件按 UTF-8 编译（/utf-8），窄字符串字面量里的中文就是 UTF-8 字节
    LOG_INFO(L"log4 wide name=%s count=%d", L"群聊天", 7);
    LOG_INFO("log4 narrow name=%s", "群聊天");
    CIULog::Uninit();

    const std::vector<std::wstring> files = FindLogFiles(scope.m_strDir, L"t");
    REQUIRE(files.size() == 1);
    const std::string data = ReadAllBytes(files[0]);

    CHECK(IsValidUtf8(data));
    CHECK(data.find("log4 wide name=群聊天 count=7") != std::string::npos);
    CHECK(data.find("log4 narrow name=群聊天") != std::string::npos);
}

TEST_CASE("LOG5 Flush 返回之后不 Uninit，此前的日志已经全部在文件里", "[iulog]")
{
    LogCaseScope scope(L"log5");
    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"t").c_str()));

    for (int i = 0; i < kLog5Lines; ++i)
        LOG_INFO("log5 seq=%d", i);
    const bool bFlushed = CIULog::Flush(kIULogFlushTimeoutMs);

    //此时写日志线程仍开着文件，以共享方式读取
    const std::vector<std::string> lines = ReadLogLines(scope.m_strDir, L"t");

    CHECK(bFlushed);
    CHECK(CountLinesContaining(lines, "log5 seq=") == kLog5Lines);
}

TEST_CASE("LOG6 日志级别按配置取值过滤", "[iulog]")
{
    LogCaseScope scope(L"log6");
    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"t").c_str()));

    CIULog::SetLevel(kConfigLevelAll);
    CHECK(LOG_INFO("log6 level0 info"));
    CHECK(LOG_WARNING("log6 level0 warning"));
    CHECK(LOG_ERROR("log6 level0 error"));

    CIULog::SetLevel(kConfigLevelWarning);
    CHECK_FALSE(LOG_INFO("log6 level1 info"));
    CHECK(LOG_WARNING("log6 level1 warning"));
    CHECK(LOG_ERROR("log6 level1 error"));

    CIULog::SetLevel(kConfigLevelError);
    CHECK_FALSE(LOG_INFO("log6 level2 info"));
    CHECK_FALSE(LOG_WARNING("log6 level2 warning"));
    CHECK(LOG_ERROR("log6 level2 error"));
    CIULog::Uninit();

    const std::vector<std::string> lines = ReadLogLines(scope.m_strDir, L"t");
    CHECK(CountLinesContaining(lines, "log6 level0") == 3);
    CHECK(CountLinesContaining(lines, "log6 level1") == 2);
    CHECK(CountLinesContaining(lines, "log6 level2") == 1);
    CHECK(CountLinesContaining(lines, "[WARN]") == 2);
    CHECK(CountLinesContaining(lines, "[ERROR]") == 3);
}

TEST_CASE("LOG7 初始化之前的日志不进入队列；未初始化时 Flush 返回 false", "[iulog]")
{
    LogCaseScope scope(L"log7");

    CHECK(LOG_INFO("log7 before init"));
    CHECK_FALSE(CIULog::Flush(kIULogFlushTimeoutMs));

    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"t").c_str()));
    LOG_INFO("log7 after init");
    CIULog::Uninit();

    const std::vector<std::string> lines = ReadLogLines(scope.m_strDir, L"t");
    CHECK(CountLinesContaining(lines, "log7 before init") == 0);
    CHECK(CountLinesContaining(lines, "log7 after init") == 1);
}

TEST_CASE("LOG8 Init / Uninit 可以反复进行，连续两次 Init 不会崩溃", "[iulog]")
{
    LogCaseScope scope(L"log8");

    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"first").c_str()));
    LOG_INFO("log8 first round");
    CIULog::Uninit();

    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"second").c_str()));
    LOG_INFO("log8 second round");
    //不 Uninit 直接再次 Init：应当先停掉上一轮，而不是覆盖一个仍在运行的线程
    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"third").c_str()));
    LOG_INFO("log8 third round");
    CIULog::Uninit();

    CHECK(CountLinesContaining(ReadLogLines(scope.m_strDir, L"first"), "log8 first round") == 1);
    CHECK(CountLinesContaining(ReadLogLines(scope.m_strDir, L"second"), "log8 second round") == 1);
    CHECK(CountLinesContaining(ReadLogLines(scope.m_strDir, L"third"), "log8 third round") == 1);
}

TEST_CASE("LOG9 每行格式为 [级别][时间][线程 id][函数签名:行号]正文", "[iulog]")
{
    LogCaseScope scope(L"log9");
    REQUIRE(CIULog::Init(true, false, scope.Prefix(L"t").c_str()));
    const int nLine = __LINE__ + 1;
    LOG_INFO("log9 format probe");
    CIULog::Uninit();

    const std::vector<std::string> lines = ReadLogLines(scope.m_strDir, L"t");
    REQUIRE(lines.size() == 1);
    const std::string& line = lines[0];

    //[INFO][2026-10-01 00:12:34:567][1234][<函数签名>:<行号>]log9 format probe
    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int s = 0;
    int ms = 0;
    unsigned int tid = 0;
    const int nFields = sscanf_s(line.c_str(), "[INFO][%4d-%2d-%2d %2d:%2d:%2d:%3d][%u]",
                                 &y, &mo, &d, &h, &mi, &s, &ms, &tid);
    CHECK(nFields == 8);
    CHECK(tid == (unsigned int)::GetCurrentThreadId());

    char szLineTag[32] = { 0 };
    sprintf_s(szLineTag, ":%d]log9 format probe", nLine);
    CHECK(line.find(szLineTag) != std::string::npos);
    //函数签名来自 __FUNCSIG__，Catch2 生成的用例函数名里带有 CATCH2_INTERNAL_TEST
    CHECK(line.find("CATCH2_INTERNAL_TEST") != std::string::npos);
}

TEST_CASE("LOG10 日志目录路径含中文时照样能建出文件并写入", "[iulog]")
{
    LogCaseScope scope(L"log10");
    const std::wstring strDir = scope.m_strDir + L"\\日志目录";
    REQUIRE(CIULog::Init(true, false, (strDir + L"\\t").c_str()));
    LOG_INFO("log10 non-ascii dir");
    CIULog::Uninit();

    CHECK(FindLogFiles(strDir, L"t").size() == 1);
    CHECK(CountLinesContaining(ReadLogLines(strDir, L"t"), "log10 non-ascii dir") == 1);
}
