/**
 *  屏幕共享实时字幕（docs/语音字幕方案.md 第 9 节）的单元测试，标签 [sharecaption]，ShareCaptionTests.cpp
 *    SCAP1 —— 字幕控制帧：编码后经重组器还原，内容一致；中间结果与最终结果的标志；时长超过 65535 按 65535；
 *    SCAP2 —— 字幕控制帧：文字超过 600 字节时在 UTF-8 字符边界截断；不是字幕的控制帧、标志非法、长度不足都不认；
 *    SCAP3 —— 发送限速：中间结果每秒最多 5 次，最终结果不限；Reset 后重新计时；
 *    SCAP4 —— 字幕板：同一句的中间结果替换、最终结果保留 4 秒后消失、迟到的旧中间结果忽略、空文字清掉这一句；
 *    SCAP5 —— 字幕板：最多显示两个人，取最近更新的，较早的在前；成员离开时清掉；长时间不更新的中间结果消失；
 *    SCAP6 —— 录制共享时别人的字幕在录像时间轴上的起止时刻。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../../FlamingoMedia/Caption/ShareCaptionBoard.h"
#include "../../FlamingoMedia/Caption/ShareCaptionFrame.h"
#include "../net/Protocol.h"

using namespace screenshare;

namespace {

//把包体经重组器还原成媒体帧
bool ScapReassemble(const std::string& body, ShareMediaFrame& out)
{
    ShareFrameReassembler r;
    return r.push(body.data(), body.size(), out);
}

} // namespace

TEST_CASE("SCAP1 字幕控制帧：编码后还原一致；中间与最终结果；时长上限", "[sharecaption]")
{
    ShareCaptionText c;
    c.m_final = true;
    c.m_sentenceId = 0x01020304;
    c.m_durationMs = 1800;
    c.m_utf8 = "\xE4\xBD\xA0\xE5\xA5\xBD SQLite";
    ShareMediaFrame frame;
    REQUIRE(ScapReassemble(BuildShareCaptionBody(3, c), frame));
    CHECK(frame.m_track == screenshare_track_control);
    CHECK(frame.m_senderSeat == 3);
    ShareCaptionText back;
    REQUIRE(ParseShareCaptionFrame(frame, back));
    CHECK(back.m_final);
    CHECK(back.m_sentenceId == 0x01020304u);
    CHECK(back.m_durationMs == 1800u);
    CHECK(back.m_utf8 == c.m_utf8);
    //中间结果、超长时长、空文字
    c.m_final = false;
    c.m_durationMs = 100000;
    c.m_utf8.clear();
    REQUIRE(ScapReassemble(BuildShareCaptionBody(0, c), frame));
    REQUIRE(ParseShareCaptionFrame(frame, back));
    CHECK_FALSE(back.m_final);
    CHECK(back.m_durationMs == kShareCaptionMaxDurationMs);
    CHECK(back.m_utf8.empty());
}

TEST_CASE("SCAP2 字幕控制帧：超长文字按字符边界截断；不是字幕帧的不认", "[sharecaption]")
{
    //301 个「你」（每个 3 字节）= 903 字节，截到 600 字节 = 200 个字
    std::string longText;
    for (int i = 0; i < 301; ++i)
        longText += "\xE4\xBD\xA0";
    CHECK(TruncateUtf8(longText, kShareCaptionMaxTextBytes).size() == 600);
    CHECK(TruncateUtf8(longText, 601).size() == 600);
    CHECK(TruncateUtf8("abc", 10) == "abc");
    ShareCaptionText c;
    c.m_utf8 = longText;
    ShareMediaFrame frame;
    REQUIRE(ScapReassemble(BuildShareCaptionBody(1, c), frame));
    ShareCaptionText back;
    REQUIRE(ParseShareCaptionFrame(frame, back));
    CHECK(back.m_utf8.size() == 600);

    //共享内容状态帧不是字幕帧
    ShareMediaFrame state;
    REQUIRE(ScapReassemble(ShareFraming::buildSourceStateBody(0, screenshare_source_paused), state));
    CHECK_FALSE(ParseShareCaptionFrame(state, back));
    //标志非法、长度不足、轨道不是控制帧
    ShareMediaFrame bad = frame;
    bad.m_data[1] = 7;
    CHECK_FALSE(ParseShareCaptionFrame(bad, back));
    bad = frame;
    bad.m_data.resize(5);
    CHECK_FALSE(ParseShareCaptionFrame(bad, back));
    bad = frame;
    bad.m_track = screenshare_track_annotation;
    CHECK_FALSE(ParseShareCaptionFrame(bad, back));
}

TEST_CASE("SCAP3 发送限速：中间结果每秒最多 5 次，最终结果不限", "[sharecaption]")
{
    ShareCaptionThrottle t;
    CHECK(t.Allow(false, 1000));
    CHECK_FALSE(t.Allow(false, 1100));
    CHECK(t.Allow(true, 1100));
    CHECK(t.Allow(false, 1200));
    CHECK_FALSE(t.Allow(false, 1399));
    CHECK(t.Allow(false, 1400));
    t.Reset();
    CHECK(t.Allow(false, 1401));
}

TEST_CASE("SCAP4 字幕板：中间结果替换、最终结果保留 4 秒、迟到的旧中间结果忽略、空文字清掉", "[sharecaption]")
{
    ShareCaptionBoard b;
    CHECK(b.Empty());
    CHECK(b.OnCaption(2, L"A", L"你", false, 1, 0));
    CHECK(b.OnCaption(2, L"A", L"你好", false, 1, 100));
    REQUIRE(b.Visible().size() == 1);
    CHECK(b.Visible()[0].m_text == L"你好");
    CHECK(b.Visible()[0].m_partial);
    CHECK(b.OnCaption(2, L"A", L"你好。", true, 1, 200));
    CHECK_FALSE(b.Visible()[0].m_partial);
    //同一句的最终结果到了以后，迟到的中间结果忽略
    CHECK_FALSE(b.OnCaption(2, L"A", L"你", false, 1, 250));
    //新一句开始后，旧句的中间结果忽略
    CHECK(b.OnCaption(2, L"A", L"下一句", false, 2, 300));
    CHECK_FALSE(b.OnCaption(2, L"A", L"old", false, 1, 310));
    CHECK(b.Visible()[0].m_text == L"下一句");
    //空文字的中间结果：清掉这一句
    CHECK(b.OnCaption(2, L"A", L"", false, 2, 320));
    CHECK(b.Empty());
    //最终结果保留 4 秒
    CHECK(b.OnCaption(3, L"B", L"done", true, 1, 1000));
    CHECK_FALSE(b.Expire(1000 + kShareCaptionFinalHoldMs - 1));
    CHECK(b.Expire(1000 + kShareCaptionFinalHoldMs));
    CHECK(b.Empty());
}

TEST_CASE("SCAP5 字幕板：最多两个人、按更新先后；成员离开清掉；中间结果过时消失", "[sharecaption]")
{
    ShareCaptionBoard b;
    b.OnCaption(1, L"A", L"a", false, 1, 100);
    b.OnCaption(2, L"B", L"b", false, 1, 200);
    b.OnCaption(3, L"C", L"c", false, 1, 300);
    std::vector<ShareCaptionLine> v = b.Visible();
    REQUIRE(v.size() == 2);
    CHECK(v[0].m_seat == 2);
    CHECK(v[1].m_seat == 3);
    //A 又说话了：显示 C 与 A
    b.OnCaption(1, L"A", L"a2", false, 2, 400);
    v = b.Visible();
    REQUIRE(v.size() == 2);
    CHECK(v[0].m_seat == 3);
    CHECK(v[1].m_seat == 1);
    b.RemoveSeat(1);
    v = b.Visible();
    REQUIRE(v.size() == 2);
    CHECK(v[0].m_seat == 2);
    //中间结果 8 秒没更新：消失
    CHECK(b.Expire(200 + kShareCaptionPartialStaleMs));
    v = b.Visible();
    REQUIRE(v.size() == 1);
    CHECK(v[0].m_seat == 3);
    b.Clear();
    CHECK(b.Empty());
}

TEST_CASE("SCAP6 录制共享时别人的字幕在录像时间轴上的起止时刻", "[sharecaption]")
{
    int64_t s = 0;
    int64_t e = 0;
    ShareCaptionRemoteTimes(10000, 2500, s, e);
    CHECK(s == 7500);
    CHECK(e == 10000);
    //比录像开头还早：从 0 开始，至少显示 1 秒
    ShareCaptionRemoteTimes(300, 2500, s, e);
    CHECK(s == 0);
    CHECK(e == kShareCaptionMinRemoteMs);
    //没带时长：按 1 秒
    ShareCaptionRemoteTimes(5000, 0, s, e);
    CHECK(s == 4000);
    CHECK(e == 5000);
}
