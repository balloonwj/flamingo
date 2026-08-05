/**
 *  图片 / 文件下载分片驱动单元测试（Suite DD），ImageDownloadDriverTests.cpp
 *
 *  用一个模拟服务端行为的假传输（FakeDownloadTransport，逻辑对标
 *  fileserversrc/FileSession::onDownloadChunk）离线复现各下载路径：单片完成、
 *  多片拼接、服务端错误码、传输层失败、累计大小不符（早 done）、服务端不推进
 *  导致的卡死保护、越界。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-03
 */
#include "catch_amalgamated.hpp"
#include "../net/ImageDownloadDriver.h"
#include "../net/Protocol.h"   // error_code_*

#include <vector>
#include <string>

using namespace net;

//模拟服务端的假下载传输：内部持有完整文件字节，按 offset 切出不超过 chunkSize
//的一片返回，逻辑对标服务端 FileSession::onDownloadChunk（末片置 done）。
class FakeDownloadTransport : public IDownloadChunkTransport
{
public:
    FakeDownloadTransport(const std::string& content, int64_t chunkSize)
        : m_content(content)
        , m_chunk(chunkSize)
        , m_calls(0)
    {
    }

    bool downloadChunk(int64_t offset, DownloadChunkInfo& info, std::string& chunk) override
    {
        ++m_calls;
        int64_t total = (int64_t)m_content.size();
        info.m_code      = error_code_ok;
        info.m_offset    = offset;
        info.m_totalsize = total;

        int64_t remain   = total - offset;
        if (remain < 0)
            remain = 0;
        int64_t len = (remain < m_chunk) ? remain : m_chunk;
        chunk.assign(m_content.data() + offset, (size_t)len);

        info.m_done = (offset + len >= total);
        return true;
    }

    int calls() const { return m_calls; }

private:
    std::string m_content;   //完整文件内容
    int64_t     m_chunk;     //单片上限
    int         m_calls;     //被调用次数
};

//记录进度回调的 sink，便于断言进度单调且最终到达 total。
class RecordingDownloadSink : public IDownloadProgressSink
{
public:
    void onProgress(int64_t received, int64_t total) override
    {
        m_recv.push_back(received);
        m_lastTotal = total;
    }
    std::vector<int64_t> m_recv;
    int64_t              m_lastTotal = 0;
};

TEST_CASE("DD1 单片完成：一次请求即收齐")
{
    std::string content(100, 'x');
    FakeDownloadTransport fake(content, 512 * 1024);
    std::string out;
    RecordingDownloadSink sink;
    DownloadDriveResult r = ImageDownloadDriver::run(fake, out, &sink);
    CHECK(r.m_ok);
    CHECK(fake.calls() == 1);
    CHECK(out == content);
    REQUIRE_FALSE(sink.m_recv.empty());
    CHECK(sink.m_recv.back() == 100);
    CHECK(sink.m_lastTotal == 100);
}

TEST_CASE("DD2 多片拼接：1MiB / 512KiB 两片")
{
    const int64_t total = 1024 * 1024;
    std::string content((size_t)total, 'a');
    //内容里掺入可区分字节，验证拼接顺序正确
    content[0] = 'H';
    content[(size_t)total - 1] = 'T';
    FakeDownloadTransport fake(content, 512 * 1024);
    std::string out;
    RecordingDownloadSink sink;
    DownloadDriveResult r = ImageDownloadDriver::run(fake, out, &sink);
    CHECK(r.m_ok);
    CHECK(fake.calls() == 2);
    REQUIRE(out.size() == (size_t)total);
    CHECK(out == content);
    //进度单调不减、最终到达 total
    for (size_t i = 1; i < sink.m_recv.size(); ++i)
        CHECK(sink.m_recv[i] >= sink.m_recv[i - 1]);
    CHECK(sink.m_recv.back() == total);
}

TEST_CASE("DD3 服务端错误码：文件不存在透传 601")
{
    class NotExistTransport : public IDownloadChunkTransport
    {
    public:
        bool downloadChunk(int64_t, DownloadChunkInfo& info, std::string& chunk) override
        {
            info.m_code = error_code_file_not_exist;
            info.m_msg  = "file not exist";
            chunk.clear();
            return true;
        }
    };
    NotExistTransport t;
    std::string out;
    DownloadDriveResult r = ImageDownloadDriver::run(t, out, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_file_not_exist);
}

TEST_CASE("DD4 传输层失败：downloadChunk 返回 false")
{
    class FailTransport : public IDownloadChunkTransport
    {
    public:
        bool downloadChunk(int64_t, DownloadChunkInfo&, std::string&) override
        {
            return false;   //模拟发送/收取/拆包失败（连接断开等）
        }
    };
    FailTransport t;
    std::string out;
    DownloadDriveResult r = ImageDownloadDriver::run(t, out, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_server_internal);
}

TEST_CASE("DD5 累计大小不符：服务端过早 done 判失败")
{
    //假传输：首片只给 10 字节就 done，但 totalsize 标 100 —— 大小不符
    class EarlyDoneTransport : public IDownloadChunkTransport
    {
    public:
        bool downloadChunk(int64_t, DownloadChunkInfo& info, std::string& chunk) override
        {
            info.m_code      = error_code_ok;
            info.m_offset    = 0;
            info.m_totalsize = 100;
            info.m_done      = true;
            chunk.assign(10, 'x');   //只给 10 字节
            return true;
        }
    };
    EarlyDoneTransport t;
    std::string out;
    DownloadDriveResult r = ImageDownloadDriver::run(t, out, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_server_internal);
}

TEST_CASE("DD6 卡死保护：服务端不推进不会死循环")
{
    //假传输：永远 done=false 且返回空片（从不推进）
    class StallTransport : public IDownloadChunkTransport
    {
    public:
        bool downloadChunk(int64_t, DownloadChunkInfo& info, std::string& chunk) override
        {
            info.m_code      = error_code_ok;
            info.m_offset    = 0;
            info.m_totalsize = 1024 * 1024;
            info.m_done      = false;
            chunk.clear();   //空片，不推进
            ++m_n;
            return true;
        }
        int m_n = 0;
    };
    StallTransport t;
    std::string out;
    DownloadDriveResult r = ImageDownloadDriver::run(t, out, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_server_internal);
    CHECK(t.m_n <= 16);   //有限次后终止
}

TEST_CASE("DD7 小图整片：内容与字节序一致")
{
    //构造 300 字节、每字节为其下标低位，验证拼接后逐字节一致（多片切分）
    std::string content;
    content.resize(300);
    for (size_t i = 0; i < content.size(); ++i)
        content[i] = (char)(i & 0xFF);
    FakeDownloadTransport fake(content, 128);   //128 字节一片 → 3 片
    std::string out;
    DownloadDriveResult r = ImageDownloadDriver::run(fake, out, NULL);
    CHECK(r.m_ok);
    CHECK(fake.calls() == 3);
    REQUIRE(out.size() == content.size());
    CHECK(out == content);
}
