/**
 *  图片 / 文件上传分片驱动单元测试（Suite UD），ImageUploadDriverTests.cpp
 *
 *  用一个模拟服务端行为的假传输（FakeServerTransport，逻辑对标
 *  fileserversrc/FileSession::onUploadChunk）离线复现各上传路径：秒传命中、
 *  正常分片、断点续传（.part 已有部分字节）、服务端回执错误码、传输层失败、
 *  服务端不推进续传位点导致的卡死保护、非法 nextoffset、入参兜底。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-03
 */
#include "catch_amalgamated.hpp"
#include "../net/ImageUploadDriver.h"
#include "../net/Protocol.h"   // error_code_*

#include <vector>

using namespace net;

//模拟服务端的假传输：内部维护「已收字节数」m_received，逻辑对标服务端
//FileSession::onUploadChunk —— offset 对齐则写入、出现空洞或重发则忽略，
//收齐后回 done。可选 instant 模拟秒传命中（首片即完成）。
class FakeServerTransport : public IUploadChunkTransport
{
public:
    FakeServerTransport(int64_t total, int64_t presetReceived, bool instant)
        : m_total(total)
        , m_received(presetReceived)
        , m_instant(instant)
        , m_calls(0)
    {
    }

    bool uploadChunk(int64_t offset, const char* data, int64_t len, UploadChunkAck& ack) override
    {
        (void)data;
        ++m_calls;
        ack.m_code = error_code_ok;

        //秒传命中：首片即回完成
        if (m_instant)
        {
            ack.m_done       = true;
            ack.m_nextoffset = m_total;
            return true;
        }

        //offset 对齐权威续传位点才写入；出现空洞或重发已有数据则忽略写入
        if (offset == m_received && len > 0)
            m_received += len;

        if (m_received >= m_total)
        {
            ack.m_done       = true;
            ack.m_nextoffset = m_total;
        }
        else
        {
            ack.m_done       = false;
            ack.m_nextoffset = m_received;
        }
        return true;
    }

    int calls() const { return m_calls; }

private:
    int64_t m_total;      //文件总字节数
    int64_t m_received;   //已收字节数（权威续传位点）
    bool    m_instant;    //是否模拟秒传命中
    int     m_calls;      //被调用次数（断言分片数用）
};

//记录进度回调的 sink，便于断言进度单调且最终到达 total。
class RecordingProgressSink : public IUploadProgressSink
{
public:
    void onProgress(int64_t sent, int64_t total) override
    {
        m_sent.push_back(sent);
        m_lastTotal = total;
    }
    std::vector<int64_t> m_sent;
    int64_t              m_lastTotal = 0;
};

TEST_CASE("UD1 秒传命中：首片即完成")
{
    std::vector<char> data(100, 'x');
    FakeServerTransport fake(100, 0, true /*instant*/);
    RecordingProgressSink sink;
    UploadDriveResult r = ImageUploadDriver::run(data.data(), 100, 512 * 1024, fake, &sink);
    CHECK(r.m_ok);
    //秒传只需一次交互
    CHECK(fake.calls() == 1);
    //完成时进度回调到达 total
    REQUIRE_FALSE(sink.m_sent.empty());
    CHECK(sink.m_sent.back() == 100);
}

TEST_CASE("UD2 正常分片：1MiB / 512KiB 两片完成")
{
    const int64_t total = 1024 * 1024;
    std::vector<char> data(static_cast<size_t>(total), 'a');
    FakeServerTransport fake(total, 0, false);
    RecordingProgressSink sink;
    UploadDriveResult r = ImageUploadDriver::run(data.data(), total, 512 * 1024, fake, &sink);
    CHECK(r.m_ok);
    CHECK(fake.calls() == 2);
    //进度单调不减、最终到达 total
    REQUIRE_FALSE(sink.m_sent.empty());
    for (size_t i = 1; i < sink.m_sent.size(); ++i)
        CHECK(sink.m_sent[i] >= sink.m_sent[i - 1]);
    CHECK(sink.m_sent.back() == total);
    CHECK(sink.m_lastTotal == total);
}

TEST_CASE("UD3 断点续传：服务端已有部分字节，nextoffset 跨过已有部分")
{
    const int64_t total = 1024 * 1024;
    //服务端 .part 已有 600KiB，客户端从 0 开始发，应被引导跳到 600KiB 续传
    const int64_t preset = 600 * 1024;
    std::vector<char> data(static_cast<size_t>(total), 'b');
    FakeServerTransport fake(total, preset, false);
    RecordingProgressSink sink;
    UploadDriveResult r = ImageUploadDriver::run(data.data(), total, 512 * 1024, fake, &sink);
    CHECK(r.m_ok);
    //首片(0)被忽略只回当前位点 600K，次片(600K)补齐到 1M：共两次
    CHECK(fake.calls() == 2);
    CHECK(sink.m_sent.back() == total);
}

TEST_CASE("UD4 服务端回执错误码：MD5 不一致透传 605")
{
    //假传输：第一次即回 code=605（md5 不一致）
    class Md5MismatchTransport : public IUploadChunkTransport
    {
    public:
        bool uploadChunk(int64_t, const char*, int64_t, UploadChunkAck& ack) override
        {
            ack.m_code = error_code_md5_mismatch;
            ack.m_msg  = "md5 mismatch";
            ack.m_done = false;
            return true;
        }
    };
    std::vector<char> data(100, 'x');
    Md5MismatchTransport t;
    UploadDriveResult r = ImageUploadDriver::run(data.data(), 100, 512 * 1024, t, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_md5_mismatch);
}

TEST_CASE("UD5 传输层失败：uploadChunk 返回 false")
{
    class FailTransport : public IUploadChunkTransport
    {
    public:
        bool uploadChunk(int64_t, const char*, int64_t, UploadChunkAck&) override
        {
            return false;   //模拟发送/收取/解析失败（连接断开等）
        }
    };
    std::vector<char> data(100, 'x');
    FailTransport t;
    UploadDriveResult r = ImageUploadDriver::run(data.data(), 100, 512 * 1024, t, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_upload_fail);
}

TEST_CASE("UD6 卡死保护：服务端不推进续传位点不会死循环")
{
    //假传输：永远 done=false 且 nextoffset=0（从不推进）
    class StallTransport : public IUploadChunkTransport
    {
    public:
        bool uploadChunk(int64_t, const char*, int64_t, UploadChunkAck& ack) override
        {
            ack.m_code       = error_code_ok;
            ack.m_done       = false;
            ack.m_nextoffset = 0;
            ++m_n;
            return true;
        }
        int m_n = 0;
    };
    std::vector<char> data(1024 * 1024, 'a');
    StallTransport t;
    UploadDriveResult r = ImageUploadDriver::run(data.data(), 1024 * 1024, 512 * 1024, t, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_upload_fail);
    //有限次后终止（不死循环）：调用次数受卡死阈值约束
    CHECK(t.m_n <= 16);
}

TEST_CASE("UD7 非法 nextoffset：越过文件尾判失败")
{
    class BadOffsetTransport : public IUploadChunkTransport
    {
    public:
        bool uploadChunk(int64_t, const char*, int64_t, UploadChunkAck& ack) override
        {
            ack.m_code       = error_code_ok;
            ack.m_done       = false;
            ack.m_nextoffset = 999999999;   //远超 size
            return true;
        }
    };
    std::vector<char> data(100, 'x');
    BadOffsetTransport t;
    UploadDriveResult r = ImageUploadDriver::run(data.data(), 100, 512 * 1024, t, NULL);
    CHECK_FALSE(r.m_ok);
    CHECK(r.m_errcode == error_code_upload_fail);
}

TEST_CASE("UD8 入参兜底：data 为空 / size 非正")
{
    FakeServerTransport fake(100, 0, false);
    UploadDriveResult r1 = ImageUploadDriver::run(NULL, 100, 512 * 1024, fake, NULL);
    CHECK_FALSE(r1.m_ok);
    CHECK(r1.m_errcode == error_code_upload_fail);

    char buf[4] = { 0 };
    UploadDriveResult r2 = ImageUploadDriver::run(buf, 0, 512 * 1024, fake, NULL);
    CHECK_FALSE(r2.m_ok);
    CHECK(r2.m_errcode == error_code_upload_fail);
}

TEST_CASE("UD9 chunksize<=0 回退到默认分片大小仍能完成")
{
    const int64_t total = 700 * 1024;   //稍大于一个默认分片(512K)
    std::vector<char> data(static_cast<size_t>(total), 'c');
    FakeServerTransport fake(total, 0, false);
    UploadDriveResult r = ImageUploadDriver::run(data.data(), total, 0 /*回退默认*/, fake, NULL);
    CHECK(r.m_ok);
    //默认 512K 分片：700K 需两片
    CHECK(fake.calls() == 2);
}
