/**
 *  文件流式收发「取消标志在分片边界生效」单元测试（Suite FST），
 *  FileStreamTransferTests.cpp
 *
 *  用假通道（IFileFrameChannel 的测试实现）驱动 net::FileStreamTransfer，锁死取消行为：
 *    FST1 上传前就已取消 → 一帧都不发，直接失败返回；
 *    FST2 上传途中置位取消 → 在下一个分片边界停下，之后不再发帧；
 *    FST3 cancel 传 NULL（不可取消路径）→ 照常传完，取消逻辑不误伤；
 *    FST4 服务端 nextoffset 不推进 → 卡死保护在固定轮数后终止；
 *    FST5 下载前就已取消 → 不产生任何落地文件，半成品临时文件也不残留；
 *    FST6 下载途中置位取消 → 停下且临时文件被删除、目标文件不存在。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-03
 */
#include "catch_amalgamated.hpp"
#include "../net/FileStreamTransfer.h"
#include "../net/FileProtocolClient.h"
#include "../net/FrameCodec.h"

#include <windows.h>
#include <fstream>
#include <string>
#include <vector>

using namespace net;

namespace {

//在系统临时目录下造一个唯一路径（不创建文件）。tag 用于区分不同用例，避免相互覆盖。
static std::wstring MakeTempPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    std::wstring p = dir;
    p += L"fst_";
    p += tag;
    p += L".bin";
    return p;
}

//写一个指定字节数的测试文件，内容为可重复的填充字节。
static bool WriteTestFile(const std::wstring& path, size_t bytes)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    std::vector<char> buf(bytes, 'A');
    out.write(buf.data(), static_cast<std::streamsize>(bytes));
    return out.good();
}

static bool FileExists(const std::wstring& path)
{
    DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

//构造一个上传分片回执的包体（普通 JSON，无附件）。流式实现从通道拿到的正是包体本身，
//故这里不需要封帧。
static std::string MakeUploadAck(bool done, int64_t nextoffset)
{
    std::string s = "{\"code\":0,\"msg\":\"ok\",\"done\":";
    s += (done ? "true" : "false");
    s += ",\"nextoffset\":";
    s += std::to_string((long long)nextoffset);
    s += "}";
    return s;
}

//构造一个下载分片应答的包体（附件格式：4字节desclen + 描述符 + 二进制段）。
static std::string MakeDownloadChunkBody(int64_t offset, int64_t totalsize, bool done,
                                         const std::string& payload)
{
    std::string desc = "{\"code\":0,\"msg\":\"ok\",\"offset\":";
    desc += std::to_string((long long)offset);
    desc += ",\"totalsize\":";
    desc += std::to_string((long long)totalsize);
    desc += ",\"done\":";
    desc += (done ? "true" : "false");
    desc += "}";

    std::string body;
    REQUIRE(net::encodeAttachmentBody(desc, payload.data(),
                                      static_cast<uint32_t>(payload.size()), body));
    return body;
}

/**
 *  假上传通道：记录发帧次数，按预设脚本回回执；可在指定轮次置位取消标志，
 *  用来模拟「用户在传输进行中点了取消」。
 */
class FakeUploadChannel : public IFileFrameChannel
{
public:
    FakeUploadChannel()
        : m_sendCount(0)
        , m_recvCount(0)
        , m_cancelAtRecv(-1)
        , m_cancelFlag(NULL)
        , m_chunk(0)
        , m_total(0)
        , m_stall(false)
    {
    }

    bool SendBytes(const char* /*data*/, int64_t /*size*/, int /*timeoutSec*/) override
    {
        ++m_sendCount;
        return true;
    }

    bool RecvFrame(std::string& body, int /*timeoutSec*/) override
    {
        ++m_recvCount;

        //模拟用户在这一轮回执到达后点了「取消」：下一轮循环开头就该停下
        if (m_cancelAtRecv > 0 && m_recvCount == m_cancelAtRecv && m_cancelFlag != NULL)
            m_cancelFlag->store(true);

        //卡死场景：服务端每次都回同一个 nextoffset，位点永不推进
        if (m_stall)
        {
            body = MakeUploadAck(false, 0);
            return true;
        }

        int64_t next = static_cast<int64_t>(m_recvCount) * m_chunk;
        if (next >= m_total)
        {
            body = MakeUploadAck(true, m_total);
            return true;
        }
        body = MakeUploadAck(false, next);
        return true;
    }

    int                m_sendCount;     //SendBytes 被调用的次数（= 实际发出的分片帧数）
    int                m_recvCount;     //RecvFrame 被调用的次数
    int                m_cancelAtRecv;  //在第几次收回执后置位取消标志（<=0 表示不置位）
    std::atomic<bool>* m_cancelFlag;    //要置位的取消标志（不持有）
    int64_t            m_chunk;         //分片大小，用于推算 nextoffset
    int64_t            m_total;         //文件总字节数
    bool               m_stall;         //true=每次都回同一位点，触发卡死保护
};

/**
 *  假下载通道：按分片吐出数据；可在指定轮次置位取消标志。
 */
class FakeDownloadChannel : public IFileFrameChannel
{
public:
    FakeDownloadChannel()
        : m_sendCount(0)
        , m_recvCount(0)
        , m_cancelAtRecv(-1)
        , m_cancelFlag(NULL)
        , m_chunk(0)
        , m_total(0)
    {
    }

    bool SendBytes(const char* /*data*/, int64_t /*size*/, int /*timeoutSec*/) override
    {
        ++m_sendCount;
        return true;
    }

    bool RecvFrame(std::string& body, int /*timeoutSec*/) override
    {
        ++m_recvCount;

        int64_t offset = static_cast<int64_t>(m_recvCount - 1) * m_chunk;
        int64_t remain = m_total - offset;
        int64_t len    = (remain < m_chunk) ? remain : m_chunk;
        if (len < 0)
            len = 0;

        std::string payload(static_cast<size_t>(len), 'A');
        bool done = (offset + len >= m_total);
        body = MakeDownloadChunkBody(offset, m_total, done, payload);

        //模拟用户在这一轮数据落盘后点了「取消」：下一轮循环开头就该停下
        if (m_cancelAtRecv > 0 && m_recvCount == m_cancelAtRecv && m_cancelFlag != NULL)
            m_cancelFlag->store(true);
        return true;
    }

    int                m_sendCount;
    int                m_recvCount;
    int                m_cancelAtRecv;
    std::atomic<bool>* m_cancelFlag;
    int64_t            m_chunk;
    int64_t            m_total;
};

} // namespace

TEST_CASE("FST1 上传前已取消：一帧都不发")
{
    std::wstring src = MakeTempPath(L"up1");
    REQUIRE(WriteTestFile(src, 4096));

    std::atomic<bool> cancel(true);   //还没开始就已被取消
    FakeUploadChannel ch;
    ch.m_chunk = 1024;
    ch.m_total = 4096;

    int         errcode = 0;
    std::string errmsg;
    bool ok = FileStreamTransfer::uploadFile(ch, src, 4096, 1024, "tok", NULL, &cancel,
                                             errcode, errmsg);

    REQUIRE(ok == false);
    //关键：取消检查在循环最前面，故一片都不该发出去
    REQUIRE(ch.m_sendCount == 0);
    REQUIRE(ch.m_recvCount == 0);
    REQUIRE(errmsg == "upload cancelled by user");

    ::DeleteFileW(src.c_str());
}

TEST_CASE("FST2 上传途中取消：在分片边界停下")
{
    std::wstring src = MakeTempPath(L"up2");
    //10 片的量，若不取消会发满 10 帧
    REQUIRE(WriteTestFile(src, 10240));

    std::atomic<bool> cancel(false);
    FakeUploadChannel ch;
    ch.m_chunk        = 1024;
    ch.m_total        = 10240;
    ch.m_cancelFlag   = &cancel;
    ch.m_cancelAtRecv = 2;   //收到第 2 片回执后用户点取消

    int         errcode = 0;
    std::string errmsg;
    bool ok = FileStreamTransfer::uploadFile(ch, src, 10240, 1024, "tok", NULL, &cancel,
                                             errcode, errmsg);

    REQUIRE(ok == false);
    REQUIRE(errmsg == "upload cancelled by user");
    //已发 2 片后被取消：第 3 轮循环开头即返回，故总共只发了 2 片
    REQUIRE(ch.m_sendCount == 2);
    REQUIRE(ch.m_recvCount == 2);

    ::DeleteFileW(src.c_str());
}

TEST_CASE("FST3 cancel 传 NULL 时照常传完")
{
    std::wstring src = MakeTempPath(L"up3");
    REQUIRE(WriteTestFile(src, 3072));

    FakeUploadChannel ch;
    ch.m_chunk = 1024;
    ch.m_total = 3072;

    int         errcode = 0;
    std::string errmsg;
    //不可取消路径：cancel 传 NULL，不该被取消逻辑误伤
    bool ok = FileStreamTransfer::uploadFile(ch, src, 3072, 1024, "tok", NULL, NULL,
                                             errcode, errmsg);

    REQUIRE(ok);
    REQUIRE(ch.m_sendCount == 3);

    ::DeleteFileW(src.c_str());
}

TEST_CASE("FST4 服务端位点不推进时卡死保护生效")
{
    std::wstring src = MakeTempPath(L"up4");
    REQUIRE(WriteTestFile(src, 10240));

    FakeUploadChannel ch;
    ch.m_chunk = 1024;
    ch.m_total = 10240;
    ch.m_stall = true;   //每次都回同一个 nextoffset

    int         errcode = 0;
    std::string errmsg;
    bool ok = FileStreamTransfer::uploadFile(ch, src, 10240, 1024, "tok", NULL, NULL,
                                             errcode, errmsg);

    REQUIRE(ok == false);
    REQUIRE(errmsg == "upload stalled, no progress from server");
    //连续 kMaxStreamStallRounds 轮没推进即终止，不会无限重试下去
    REQUIRE(ch.m_sendCount == kMaxStreamStallRounds);

    ::DeleteFileW(src.c_str());
}

TEST_CASE("FST5 下载前已取消：不留任何文件")
{
    std::wstring dest = MakeTempPath(L"dn5");
    std::wstring tmp  = dest + L".downloading";
    ::DeleteFileW(dest.c_str());
    ::DeleteFileW(tmp.c_str());

    std::atomic<bool> cancel(true);
    FakeDownloadChannel ch;
    ch.m_chunk = 1024;
    ch.m_total = 4096;

    std::wstring outPath;
    int          errcode = 0;
    std::string  errmsg;
    bool ok = FileStreamTransfer::downloadFile(ch, "tok", "", 4096, dest, NULL, &cancel,
                                               outPath, errcode, errmsg);

    REQUIRE(ok == false);
    REQUIRE(errmsg == "download cancelled by user");
    REQUIRE(ch.m_sendCount == 0);
    //目标文件不该出现，半成品临时文件也要被清掉
    REQUIRE(FileExists(dest) == false);
    REQUIRE(FileExists(tmp) == false);
}

TEST_CASE("FST6 下载途中取消：临时文件被清掉、目标文件不存在")
{
    std::wstring dest = MakeTempPath(L"dn6");
    std::wstring tmp  = dest + L".downloading";
    ::DeleteFileW(dest.c_str());
    ::DeleteFileW(tmp.c_str());

    std::atomic<bool> cancel(false);
    FakeDownloadChannel ch;
    ch.m_chunk        = 1024;
    ch.m_total        = 10240;
    ch.m_cancelFlag   = &cancel;
    ch.m_cancelAtRecv = 2;   //收到第 2 片后用户点取消

    std::wstring outPath;
    int          errcode = 0;
    std::string  errmsg;
    bool ok = FileStreamTransfer::downloadFile(ch, "tok", "", 10240, dest, NULL, &cancel,
                                               outPath, errcode, errmsg);

    REQUIRE(ok == false);
    REQUIRE(errmsg == "download cancelled by user");
    //已收 2 片后被取消：第 3 轮开头即返回
    REQUIRE(ch.m_sendCount == 2);
    REQUIRE(ch.m_recvCount == 2);
    //半途而废的内容不能留在磁盘上冒充完整文件
    REQUIRE(FileExists(dest) == false);
    REQUIRE(FileExists(tmp) == false);
    REQUIRE(outPath.empty());
}
