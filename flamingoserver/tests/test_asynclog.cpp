/**
 *  异步日志的文件写入失败兜底单元测试（Suite ALG），test_asynclog.cpp
 *
 *  只覆盖一件事：以文件方式初始化时，日志文件建不出来不能把写日志线程整个结束掉。
 *  改造前 CAsyncLog::writeThreadProc 在 createNewFile 失败时直接 return，而它是唯一一个
 *  把日志队列写出去的线程，一旦退出，连控制台也不再有任何输出，进程却还在正常跑 ——
 *  现象是「服务活着但一行日志都没有」，非常难定位。现在改为退化成只写控制台并继续跑。
 *
 *  用例要观察控制台输出，做法是临时把 std::cout 的缓冲区换成字符串流，跑完恢复。
 *  写日志是异步的，故用有上限的轮询等结果，不用固定 sleep，避免慢机器上偶发失败。
 *
 *  注意：CAsyncLog 是全局状态，整个测试程序里只有本套用例会 init / uninit 它，
 *  每个用例结束前都必须 uninit 复位，否则会影响后面的用例。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "doctest.h"
#include "../base/AsyncLog.h"
#include "../utils/FileEnum.h"

//本套用例专用的临时目录与日志文件基名，与其它几套用的文件分开
static const char* const ALG_LOG_DIR      = "flamingo_asynclog_test";
static const char* const ALG_LOG_BASENAME = "flamingo_asynclog_test/alg";
//一个肯定不存在的目录，用来制造「日志文件建不出来」
static const char* const ALG_MISSING_DIR_BASENAME = "flamingo_asynclog_no_such_dir/alg";

//轮询等待的上限与间隔（毫秒）。写日志线程通常几毫秒内就有结果，
//上限给到 3 秒纯粹是给慢机器留余量，正常情况下用不到
static const int ALG_WAIT_LIMIT_MS = 3000;
static const int ALG_WAIT_STEP_MS  = 10;

//建目录；已存在不算失败
static void makeDir(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
}

//删目录；非空时删不掉，调用方须先清文件
static void removeDir(const char* path)
{
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

//读出 ALG_LOG_DIR 下所有文件的内容拼在一起。日志文件名由 CAsyncLog 拼出
//（基名 + 时间 + 进程号 + ".log"），这里不去猜它，直接扫目录
static std::string readAllLogContent()
{
    std::vector<std::string> names;
    fileenum::ListFileNames(ALG_LOG_DIR, names);

    std::string all;
    for (size_t i = 0; i < names.size(); ++i)
    {
        const std::string path = fileenum::WithTrailingSeparator(ALG_LOG_DIR) + names[i];
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in.is_open())
            continue;

        std::ostringstream os;
        os << in.rdbuf();
        all += os.str();
    }

    return all;
}

//清掉 ALG_LOG_DIR 下的全部文件以及目录本身
static void removeLogDir()
{
    std::vector<std::string> names;
    fileenum::ListFileNames(ALG_LOG_DIR, names);
    for (size_t i = 0; i < names.size(); ++i)
    {
        const std::string path = fileenum::WithTrailingSeparator(ALG_LOG_DIR) + names[i];
        remove(path.c_str());
    }

    removeDir(ALG_LOG_DIR);
}

//等到 isFileWriteDisabled() 等于 expectDisabled 为止，超时则返回当时的实际值
static bool waitFileWriteDisabled(bool expectDisabled)
{
    for (int waited = 0; waited < ALG_WAIT_LIMIT_MS; waited += ALG_WAIT_STEP_MS)
    {
        if (CAsyncLog::isFileWriteDisabled() == expectDisabled)
            return CAsyncLog::isFileWriteDisabled();

        std::this_thread::sleep_for(std::chrono::milliseconds(ALG_WAIT_STEP_MS));
    }

    return CAsyncLog::isFileWriteDisabled();
}

// ---------------------------------------------------------------------------
// ALG1  日志文件建不出来时退化为只写控制台
// ---------------------------------------------------------------------------

TEST_CASE("ALG1 日志文件建不出来时不结束写日志线程，改为只写控制台并继续输出")
{
    //把 std::cout 的缓冲区换成字符串流，以便核对控制台输出
    std::ostringstream captured;
    std::streambuf* oldBuf = std::cout.rdbuf(captured.rdbuf());

    //目录不存在，fopen 必然失败
    REQUIRE(CAsyncLog::init(ALG_MISSING_DIR_BASENAME));

    //文件写入应当被停用，而不是把写日志线程结束掉
    CHECK(waitFileWriteDisabled(true));

    LOGI("alg1 console fallback marker");

    //日志正文仍然要出现在控制台上
    bool seen = false;
    for (int waited = 0; waited < ALG_WAIT_LIMIT_MS; waited += ALG_WAIT_STEP_MS)
    {
        if (captured.str().find("alg1 console fallback marker") != std::string::npos)
        {
            seen = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(ALG_WAIT_STEP_MS));
    }
    CHECK(seen);

    CAsyncLog::uninit();
    std::cout.rdbuf(oldBuf);
}

// ---------------------------------------------------------------------------
// ALG2  目录可写时照常落盘
// ---------------------------------------------------------------------------

TEST_CASE("ALG2 目录可写时日志照常落盘，文件写入不被停用")
{
    removeLogDir();
    makeDir(ALG_LOG_DIR);

    std::ostringstream captured;
    std::streambuf* oldBuf = std::cout.rdbuf(captured.rdbuf());

    REQUIRE(CAsyncLog::init(ALG_LOG_BASENAME));

    LOGI("alg2 file output marker");

    std::string content;
    for (int waited = 0; waited < ALG_WAIT_LIMIT_MS; waited += ALG_WAIT_STEP_MS)
    {
        content = readAllLogContent();
        if (content.find("alg2 file output marker") != std::string::npos)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(ALG_WAIT_STEP_MS));
    }

    CHECK(content.find("alg2 file output marker") != std::string::npos);
    CHECK_FALSE(CAsyncLog::isFileWriteDisabled());

    CAsyncLog::uninit();
    std::cout.rdbuf(oldBuf);

    removeLogDir();
}
