/**
 *  停止录制时写入线程迟迟不退出的诊断（标签 [recordstall]），RecordStopStallTests.cpp
 *
 *  背景：bugs.md BUG-120。独立录屏勾了「生成字幕」时点「停止」，客户端偶发卡死，日志里没有任何线索。RecordSession 的写入线程
 *  现在每进入一步就记下步骤与时刻；Stop 等它退出太久时，把它停在哪一步写进日志。本文件覆盖：
 *    - RSTALL1 步骤的英文名：每个步骤各不相同、只含 ASCII；不认识的取值为 "unknown"；
 *    - RSTALL2 何时写诊断日志（纯函数）：等到 firstMs 写第一条，之后每隔 intervalMs 写一条；
 *    - RSTALL3 写入线程卡在语音字幕的分路旁路里时，WriterStage 为「取声音」且停留时长随之增长；放开后 Stop 正常返回，
 *              步骤为「即将退出」；
 *    - RSTALL4 Stop 之后旁路仍卡住一段时间：Stop 等到诊断阈值时记下「取声音」这一步（LastStopStallStage），放开后返回；
 *              没有卡住时 LastStopStallStage 为 -1。
 *  RSTALL3、RSTALL4 写临时文件（系统临时目录），用完删除。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <atomic>
#include <set>
#include <string>
#include <thread>

#include "../../FlamingoMedia/Record/RecordAudioMixer.h"
#include "../../FlamingoMedia/Record/RecordSession.h"

using namespace screenrecord;

namespace {

//等待某个条件成立时，两次检查之间的间隔（毫秒）
const DWORD kRstPollMs = 10;
//等待某个条件成立的最长时间（毫秒）
const DWORD kRstWaitMaxMs = 5000;
//RSTALL3：旁路卡住之后再观察多久（毫秒），确认停留时长在增长
const DWORD kRstObserveMs = 300;
//RSTALL4：诊断阈值（毫秒），用例里改小以免等太久
const int kRstStallWarnMs = 200;
//RSTALL4：Stop 之后旁路继续卡住的时长（毫秒），须大于诊断阈值
const DWORD kRstHoldAfterStopMs = 600;

//系统临时目录下的一个文件路径
std::wstring rstTempPath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

/**
 *  会卡住的分路旁路：放行之前，第一次被调用后就一直等在 OnRecordAudio 里（写入线程随之停在「取声音」这一步）。
 */
class BlockingTap : public IRecordAudioTap
{
public:
    BlockingTap()
        : m_entered(false)
        , m_release(false)
    {
    }

    void OnRecordAudio(int source, const int16_t* pcm, size_t samples, int64_t ptsMs) override
    {
        (void)source;
        (void)pcm;
        (void)samples;
        (void)ptsMs;
        m_entered.store(true);
        while (!m_release.load())
            ::Sleep(kRstPollMs);
    }

    std::atomic<bool> m_entered;   //写入线程已经进入旁路
    std::atomic<bool> m_release;   //放行：之后的调用立即返回
};

//等到 pred() 成立，最多等 kRstWaitMaxMs；成立返回 true
template <typename Pred>
bool rstWaitFor(Pred pred)
{
    for (DWORD waited = 0; waited < kRstWaitMaxMs; waited += kRstPollMs)
    {
        if (pred())
            return true;
        ::Sleep(kRstPollMs);
    }
    return pred();
}

//RSTALL3 / RSTALL4 等待用的条件：旁路已被调用
struct TapEntered
{
    explicit TapEntered(const BlockingTap& tap)
        : m_tap(tap)
    {
    }
    bool operator()() const
    {
        return m_tap.m_entered.load();
    }
    const BlockingTap& m_tap;   //被等待的旁路
};

//在另一个线程上等一会儿再放行旁路（RSTALL4：Stop 会一直等到放行）
void releaseLater(BlockingTap* tap, DWORD delayMs)
{
    ::Sleep(delayMs);
    tap->m_release.store(true);
}

} // namespace

TEST_CASE("RSTALL1 writer stage names", "[recordstall]")
{
    std::set<std::string> names;
    for (int stage = record_stage_not_started; stage <= record_stage_done; ++stage)
    {
        const char* name = RecordWriterStageName(stage);
        REQUIRE(name != NULL);
        const std::string text(name);
        CHECK_FALSE(text.empty());
        CHECK(text != "unknown");
        for (size_t i = 0; i < text.size(); ++i)
            CHECK(static_cast<unsigned char>(text[i]) < 0x80);
        names.insert(text);
    }
    CHECK(names.size() == static_cast<size_t>(record_stage_done - record_stage_not_started + 1));
    CHECK(std::string(RecordWriterStageName(record_stage_done + 1)) == "unknown");
    CHECK(std::string(RecordWriterStageName(-1)) == "unknown");
}

TEST_CASE("RSTALL2 when to report a stalled stop", "[recordstall]")
{
    const int first = kRecordStopStallFirstMs;
    const int interval = kRecordStopStallIntervalMs;
    //还没写过：等到 first 才写
    CHECK_FALSE(RecordStopStallReportDue(0, -1, first, interval));
    CHECK_FALSE(RecordStopStallReportDue(first - 1, -1, first, interval));
    CHECK(RecordStopStallReportDue(first, -1, first, interval));
    CHECK(RecordStopStallReportDue(first + 1234, -1, first, interval));
    //写过之后：距上一条满 interval 才写
    CHECK_FALSE(RecordStopStallReportDue(first + interval - 1, first, first, interval));
    CHECK(RecordStopStallReportDue(first + interval, first, first, interval));
    CHECK_FALSE(RecordStopStallReportDue(first + interval + 10, first + interval, first, interval));
}

TEST_CASE("RSTALL3 writer stage shows a tap that blocks", "[recordstall]")
{
    const std::wstring path = rstTempPath(L"flamingo_rstall3_test.m4a");
    ::DeleteFileW(path.c_str());
    BlockingTap tap;
    RecordSession session;
    CHECK(session.WriterStage() == record_stage_not_started);
    RecordOptions opt;
    opt.m_path = path;
    opt.m_audioOnly = true;
    session.Audio().SetEnabled(record_audio_mic, true);
    session.Audio().SetTap(&tap);
    REQUIRE(session.Start(opt, NULL, NULL, NULL));
    REQUIRE(rstWaitFor(TapEntered(tap)));
    CHECK(session.WriterStage() == record_stage_pull_audio);
    const int64_t before = session.WriterStageElapsedMs();
    ::Sleep(kRstObserveMs);
    CHECK(session.WriterStage() == record_stage_pull_audio);
    CHECK(session.WriterStageElapsedMs() >= before + static_cast<int64_t>(kRstObserveMs) / 2);
    tap.m_release.store(true);
    session.Stop();
    CHECK(session.WriterStage() == record_stage_done);
    //Stop 时旁路已放行，写入线程很快退出，用不着写诊断日志
    CHECK(session.LastStopStallStage() == -1);
    session.Audio().SetTap(NULL);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("RSTALL4 stop reports the stage the writer is stuck in", "[recordstall]")
{
    const std::wstring path = rstTempPath(L"flamingo_rstall4_test.m4a");
    ::DeleteFileW(path.c_str());
    BlockingTap tap;
    RecordSession session;
    session.SetStopStallWarnMs(kRstStallWarnMs);
    RecordOptions opt;
    opt.m_path = path;
    opt.m_audioOnly = true;
    session.Audio().SetEnabled(record_audio_mic, true);
    session.Audio().SetTap(&tap);
    REQUIRE(session.Start(opt, NULL, NULL, NULL));
    REQUIRE(rstWaitFor(TapEntered(tap)));
    std::thread releaser(releaseLater, &tap, kRstHoldAfterStopMs);
    const ULONGLONG begin = ::GetTickCount64();
    session.Stop();
    const ULONGLONG spent = ::GetTickCount64() - begin;
    releaser.join();
    //Stop 一直等到旁路放行才返回，期间写过诊断日志，记下的是「取声音」
    CHECK(spent >= static_cast<ULONGLONG>(kRstStallWarnMs));
    CHECK(session.LastStopStallStage() == record_stage_pull_audio);
    CHECK(session.WriterStage() == record_stage_done);
    session.Audio().SetTap(NULL);
    ::DeleteFileW(path.c_str());
}
