/**
 *  语音气泡的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节：
 *    - VM11 气泡宽度随时长变化（最短 120、最长 240 像素，第 6b 步由 88 改为 120），时长文字 8″、1′05″、2′00″（第 3d 步）；
 *    - VM46 短语音也有波形：1 秒的语音至少画出 6 根波形柱，时长越长根数不减（第 6b 步）；
 *    - VM12 红点只在对方发来、本机还没听过的语音上显示（第 3d-1 步的显示条件）；
 *    - VM12b 同上，在画出来的气泡上检查：对方发来且未听的画了红点，已听的、本人发的都没有（第 3d-2 步）；
 *    - VM21 ApplyVoiceMessage 填写的字段：种类、时长、波形（没有时为均匀波形）、已听（本人发的一律已听），
 *           语音行的高度与单行文字相同（第 3d-2 步）。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../UI/BalloonChatWnd/ChatVoicePaint.h"   // VoiceWaveBarCount
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../net/VoiceContent.h"

using namespace balloonchatwnd;

TEST_CASE("VM11 语音气泡：时长文字为 8″、1′05″、2′00″，宽度随时长增长、有上下限", "[voicemsg]")
{
    //时长文字：秒用「″」，分用「′」，满 1 分钟后秒补两位
    CHECK(FormatVoiceDuration(8) == L"8″");
    CHECK(FormatVoiceDuration(59) == L"59″");
    CHECK(FormatVoiceDuration(60) == L"1′00″");
    CHECK(FormatVoiceDuration(65) == L"1′05″");
    CHECK(FormatVoiceDuration(120) == L"2′00″");
    CHECK(FormatVoiceDuration(0) == L"1″");

    //宽度：1 秒及以下最窄，满 kVoiceBubbleFullSeconds 秒及以上最宽，中间单调增长。
    //最短 120、最长 240 像素是用户在第 6 期定的数值（原先最短 88，不到 6 秒的语音画不出波形柱）
    const int kExpectedMinWidth = 120;
    const int kExpectedMaxWidth = 240;
    const int kExpectedFullSeconds = 60;
    CHECK(VoiceBubbleWidth(1) == kExpectedMinWidth);
    CHECK(VoiceBubbleWidth(0) == kExpectedMinWidth);
    CHECK(VoiceBubbleWidth(kExpectedFullSeconds) == kExpectedMaxWidth);
    CHECK(VoiceBubbleWidth(120) == kExpectedMaxWidth);
    CHECK(kVoiceBubbleMinWidth == kExpectedMinWidth);
    CHECK(kVoiceBubbleMaxWidth == kExpectedMaxWidth);
    int previous = VoiceBubbleWidth(1);
    for (int s = 2; s <= kVoiceBubbleFullSeconds; ++s)
    {
        const int w = VoiceBubbleWidth(s);
        CHECK(w >= previous);
        previous = w;
    }
    CHECK(VoiceBubbleWidth(10) > VoiceBubbleWidth(2));
    CHECK(VoiceBubbleWidth(30) > VoiceBubbleWidth(10));
}

TEST_CASE("VM12 未听红点：只在对方发来、本机还没听过的语音上显示", "[voicemsg]")
{
    CHECK(ShowVoiceUnplayedDot(false, false));
    CHECK_FALSE(ShowVoiceUnplayedDot(false, true));
    CHECK_FALSE(ShowVoiceUnplayedDot(true, false));
    CHECK_FALSE(ShowVoiceUnplayedDot(true, true));
}

namespace {

//画图测试的画布尺寸（像素）
const int kVbListWidth = 600;
const int kVbListHeight = 120;
//判定「红点的红色」的阈值：红色通道高、绿蓝通道低（红点用 #D9534F）
const int kVbRedMinR = 180;
const int kVbRedMaxG = 120;
const int kVbRedMaxB = 120;

//本用例里启动 / 关闭 GDI+
struct VbGdiplusScope
{
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌

    VbGdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }

    ~VbGdiplusScope()
    {
        if (m_token != 0)
            Gdiplus::GdiplusShutdown(m_token);
    }
};

//一条语音消息
ChatMessage voiceMessage(bool fromMe, bool played)
{
    ChatMessage m;
    m.fromMe = fromMe;
    m.msgid = 9001;
    m.timestampMs = 1700000000000LL;
    ApplyVoiceMessage(m, "998877", "d41d8cd98f00b204e9800998ecf8427e", 8, 25600, "", played);
    return m;
}

//把只含一条消息的列表画到白底 32 位位图上，返回红色像素的个数
int countRedPixels(const ChatMessage& msg)
{
    ChatMessageList list;
    list.AppendMessage(msg);
    RECT rc = { 0, 0, kVbListWidth, kVbListHeight };
    list.ForceLayout(rc);

    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kVbListWidth;
    bi.bmiHeader.biHeight = -kVbListHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bitmap = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bitmap != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bitmap);
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &rc, white);
    ::DeleteObject(white);
    list.OnPaint(memDC, rc);
    ::GdiFlush();
    int red = 0;
    const unsigned int* px = static_cast<const unsigned int*>(bits);
    for (int i = 0; i < kVbListWidth * kVbListHeight; ++i)
    {
        const int r = static_cast<int>((px[i] >> 16) & 0xFF);
        const int g = static_cast<int>((px[i] >> 8) & 0xFF);
        const int b = static_cast<int>(px[i] & 0xFF);
        if (r >= kVbRedMinR && g <= kVbRedMaxG && b <= kVbRedMaxB)
            ++red;
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bitmap);
    ::DeleteDC(memDC);
    return red;
}

//只含一条消息的列表里那一行的高度
int rowHeightOf(const ChatMessage& msg)
{
    ChatMessageList list;
    list.AppendMessage(msg);
    RECT rc = { 0, 0, kVbListWidth, kVbListHeight };
    list.ForceLayout(rc);
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    REQUIRE_FALSE(kids.empty());
    ChatBubble* bubble = dynamic_cast<ChatBubble*>(kids.back().get());
    REQUIRE(bubble != NULL);
    return bubble->GetRowHeight();
}

} // namespace

TEST_CASE("VM21 ApplyVoiceMessage：种类、时长、波形、已听，语音行与单行文字同高", "[voicemsg]")
{
    ChatMessage peer;
    peer.fromMe = false;
    ApplyVoiceMessage(peer, "998877", "abc", 8, 25600, "AAECAw==", false);
    CHECK(peer.kind == kMsgVoice);
    CHECK(peer.voiceFileId == "998877");
    CHECK(peer.voiceMd5 == "abc");
    CHECK(peer.voiceDuration == 8);
    CHECK(peer.voiceSize == 25600);
    REQUIRE(peer.voiceWave.size() == 4);
    CHECK(peer.voiceWave[3] == 3);
    CHECK_FALSE(peer.voicePlayed);
    CHECK(peer.voiceState == kVoiceStateReady);
    CHECK_FALSE(peer.text.IsEmpty());

    //本人发的一律已听；没有波形时画均匀波形；时长不足 1 秒按 1 秒
    ChatMessage self;
    self.fromMe = true;
    ApplyVoiceMessage(self, "1", "m", 0, 100, "", false);
    CHECK(self.voicePlayed);
    CHECK(self.voiceDuration == 1);
    CHECK(self.voiceWave.size() == static_cast<size_t>(net::kVoiceWaveBars));

    //行高：与一条单行文字消息相同
    VbGdiplusScope gdiplus;
    ChatMessage text;
    text.kind = kMsgText;
    text.fromMe = false;
    text.text = L"hi";
    text.msgid = 9002;
    text.timestampMs = 1700000000000LL;
    CHECK(rowHeightOf(voiceMessage(false, false)) == rowHeightOf(text));
}

TEST_CASE("VM12b 画出来的语音气泡：对方发来且未听的有红点，已听的、本人发的没有", "[voicemsg]")
{
    VbGdiplusScope gdiplus;
    //按差值比较，排除头像等其它带红色成分的像素：红点直径 8 像素，抗锯齿后至少多出二三十个红色像素
    const int unplayed = countRedPixels(voiceMessage(false, false));
    const int played = countRedPixels(voiceMessage(false, true));
    CHECK(unplayed - played >= 20);
    //本人发的：标成未听与已听画出来一样（ApplyVoiceMessage 把本人发的一律记为已听，气泡也不画红点）
    ChatMessage mineUnplayed = voiceMessage(true, true);
    mineUnplayed.voicePlayed = false;
    CHECK(countRedPixels(mineUnplayed) == countRedPixels(voiceMessage(true, true)));
}

TEST_CASE("VM46 短语音也有波形：1 秒的语音至少画出 6 根波形柱，时长越长根数不减", "[voicemsg]")
{
    const int kMinBarsForOneSecond = 6;   //用户在第 6 期确认：最短宽度 120 像素时 1 秒的语音有 6 根
    CHECK(VoiceWaveBarCount(VoiceBubbleWidth(1)) >= kMinBarsForOneSecond);
    int previous = VoiceWaveBarCount(VoiceBubbleWidth(1));
    for (int s = 2; s <= kVoiceBubbleFullSeconds; ++s)
    {
        const int bars = VoiceWaveBarCount(VoiceBubbleWidth(s));
        CHECK(bars >= previous);
        previous = bars;
    }
    CHECK(VoiceWaveBarCount(VoiceBubbleWidth(kVoiceBubbleFullSeconds)) > VoiceWaveBarCount(VoiceBubbleWidth(1)));
    //窄到放不下一根：0，不出现负数
    CHECK(VoiceWaveBarCount(0) == 0);
    CHECK(VoiceWaveBarCount(kVoicePadH) == 0);
}
