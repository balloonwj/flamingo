/**
 *  消息记录窗与合并记录查看窗里的语音（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节
 *  （2026-10-04 用户确认）：
 *    - VM35 消息记录窗的语音行：从本地库的 content 读出时长、波形、md5、fileid；做成语音消息时本机有大小相符的
 *           文件为就绪、没有时标为需要下载（借用下载失败态，点击即下载）；未听带红点；整页排版后语音条控件落在
 *           所在行之内、高度为 kVoiceStripH（第 4b 步）；
 *    - VM36 共用的语音条控件：宽度随时长、与聊天气泡一致；未听才画红点（像素检查），不画红点的设置下不画；
 *           单击投出的请求带 msgid 与 clientMsgId，只有点在语音条上才算（第 4b 步）；
 *    - VM36b 语音条只在自己的矩形之内作画，红点消失后不留残迹（第 5 期联调发现的问题）；
 *    - VM44b 红点同步（消息记录窗，第 6a 步）：按 msgid 找到当前页的语音条；别处听过一条语音后，只在会话相符时
 *           把同一条标为已听（消息列表一侧见 VoicePlayTests.cpp 的 VM44）；
 *    - VM37 合并记录查看窗的语音行：语音子消息显示成语音条、不画红点；本机有文件判为就绪，没有时标为需要下载
 *           （第 4c 步）。
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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../net/Protocol.h"
#include "../net/MessageProtocolClient.h"
#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatReceivedVoice.h"
#include "../UI/BalloonChatWnd/ChatVoiceStrip.h"
#include "../UI/BalloonChatWnd/ChatVoicePaint.h"
#include "../UI/BalloonHistory/HistoryView.h"
#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../UI/BalloonHistory/MergedRecordRows.h"
#include "../UI/BalloonHistory/HistoryRowFormat.h"   // LocalDayKey

using namespace balloonchatwnd;
using namespace balloonhistory;

namespace {

//测试用的语音字段
const int kVhDurationSec = 8;
const long long kVhVoiceBytes = 5;
const char* const kVhMd5 = "0123456789abcdef0123456789abcdef";
//画图测试的画布尺寸（像素）
const int kVhCanvasW = 320;
const int kVhCanvasH = 60;
//判定「红点的红色」的阈值：红色通道高、绿蓝通道低（红点用 #D9534F）
const int kVhRedMinR = 180;
const int kVhRedMaxG = 120;
const int kVhRedMaxB = 120;
//整页排版用的列表宽度（像素）
const int kVhListWidth = 600;

//本用例里启动 / 关闭 GDI+
struct VhGdiplusScope
{
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌

    VhGdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }

    ~VhGdiplusScope()
    {
        if (m_token != 0)
            Gdiplus::GdiplusShutdown(m_token);
    }
};

//每个进程独立的临时目录（以反斜杠结尾），不存在时创建
std::wstring tempVoiceDir(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    const std::wstring path = std::wstring(dir) + prefix + name + L"\\";
    ::CreateDirectoryW(path.c_str(), NULL);
    return path;
}

//写一个指定字节数的文件
void writeBytes(const std::wstring& path, size_t bytes)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    const std::string data(bytes, 'x');
    DWORD written = 0;
    ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, NULL);
    ::CloseHandle(h);
    REQUIRE(written == static_cast<DWORD>(bytes));
}

//一条对方发来的语音（就绪，带本地路径）
ChatMessage readyVoice(bool played)
{
    ChatMessage m;
    m.fromMe = false;
    m.msgid = 5101;
    ApplyVoiceMessage(m, "998877", kVhMd5, kVhDurationSec, kVhVoiceBytes, "AAECAw==", played);
    m.voiceLocalPath = L"C:\\voice\\x.ogg";
    return m;
}

//把一个语音条控件画到白底 32 位位图上，返回红色像素的个数
int countStripRedPixels(const ChatMessage& m, bool showDot)
{
    ChatVoiceStrip strip;
    strip.SetVoiceMessage(m, showDot);
    RECT rc = { 0, 0, VoiceStripWidth(m), kVoiceStripH };
    strip.SetRect(rc);

    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kVhCanvasW;
    bi.bmiHeader.biHeight = -kVhCanvasH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bitmap = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bitmap != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bitmap);
    RECT canvas = { 0, 0, kVhCanvasW, kVhCanvasH };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &canvas, white);
    ::DeleteObject(white);
    strip.OnPaint(memDC, rc);
    ::GdiFlush();
    int red = 0;
    const unsigned int* px = static_cast<const unsigned int*>(bits);
    for (int i = 0; i < kVhCanvasW * kVhCanvasH; ++i)
    {
        const int r = static_cast<int>((px[i] >> 16) & 0xFF);
        const int g = static_cast<int>((px[i] >> 8) & 0xFF);
        const int b = static_cast<int>(px[i] & 0xFF);
        if (r >= kVhRedMinR && g <= kVhRedMaxG && b <= kVhRedMaxB)
            ++red;
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bitmap);
    ::DeleteDC(memDC);
    return red;
}

//把一个语音条控件放在白底 32 位位图的 item 处作画，返回 item 之外不再是白色的像素个数。
//item 应离画布四边都留有余量，越出控件矩形的像素才落在画布之内、能被数到
int countStripPixelsOutside(const ChatMessage& m, bool showDot, const RECT& item)
{
    ChatVoiceStrip strip;
    strip.SetVoiceMessage(m, showDot);
    strip.SetRect(item);

    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kVhCanvasW;
    bi.bmiHeader.biHeight = -kVhCanvasH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bitmap = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bitmap != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bitmap);
    RECT canvas = { 0, 0, kVhCanvasW, kVhCanvasH };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &canvas, white);
    ::DeleteObject(white);
    strip.OnPaint(memDC, item);
    ::GdiFlush();
    int outside = 0;
    const unsigned int* px = static_cast<const unsigned int*>(bits);
    for (int y = 0; y < kVhCanvasH; ++y)
    {
        for (int x = 0; x < kVhCanvasW; ++x)
        {
            POINT pt = { x, y };
            if (::PtInRect(&item, pt))
                continue;
            if ((px[y * kVhCanvasW + x] & 0x00FFFFFF) != 0x00FFFFFF)
                ++outside;
        }
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bitmap);
    ::DeleteDC(memDC);
    return outside;
}

} // namespace

TEST_CASE("VM35 消息记录窗的语音行：读出语音字段、本机文件决定就绪或需要下载、红点、行控件", "[voicemsg]")
{
    //从本地库的 content 读出语音字段
    const HistoryContentBrief brief = ParseContentBrief(message_type_voice,
        "{\"voice\":{\"fileid\":\"998877\",\"md5\":\"0123456789abcdef0123456789abcdef\",\"duration\":8,"
        "\"size\":5,\"wave\":\"AAECAw==\"}}");
    CHECK(brief.m_voiceFileId == "998877");
    CHECK(brief.m_voiceMd5 == kVhMd5);
    CHECK(brief.m_voiceDuration == kVhDurationSec);
    CHECK(brief.m_voiceSize == kVhVoiceBytes);
    CHECK(brief.m_voiceWave == "AAECAw==");
    //字段写成字符串（转发来的内容可能如此）也能读
    const HistoryContentBrief strBrief = ParseContentBrief(message_type_voice,
        "{\"voice\":{\"fileid\":998877,\"md5\":\"m\",\"duration\":\"12\",\"size\":\"40000\"}}");
    CHECK(strBrief.m_voiceFileId == "998877");
    CHECK(strBrief.m_voiceDuration == 12);
    CHECK(strBrief.m_voiceSize == 40000);

    //做成语音消息：本机有大小相符的文件为就绪，没有时标为需要下载
    const std::wstring dir = tempVoiceDir(L"vm35_voice");
    const std::wstring file = dir + L"0123456789abcdef0123456789abcdef.ogg";
    ::DeleteFileW(file.c_str());
    ChatMessage absent = BuildStoredVoiceMessage(5201, false, brief.m_voiceFileId, brief.m_voiceMd5,
                                                 brief.m_voiceDuration, brief.m_voiceSize, brief.m_voiceWave,
                                                 false, dir);
    CHECK(absent.kind == kMsgVoice);
    CHECK(absent.msgid == 5201);
    CHECK_FALSE(absent.voicePlayed);
    CHECK(absent.voiceState == kVoiceStateDownloadFailed);
    CHECK(absent.voiceLocalPath.IsEmpty());
    CHECK(DecideVoiceClick(absent) == kVoiceClickRetryDownload);
    writeBytes(file, static_cast<size_t>(kVhVoiceBytes));
    ChatMessage ready = BuildStoredVoiceMessage(5201, false, brief.m_voiceFileId, brief.m_voiceMd5,
                                                brief.m_voiceDuration, brief.m_voiceSize, brief.m_voiceWave,
                                                false, dir);
    CHECK(ready.voiceState == kVoiceStateReady);
    CHECK(ready.voiceLocalPath == CString(file.c_str()));
    CHECK(DecideVoiceClick(ready) == kVoiceClickPlay);
    //本人发的一律已听
    CHECK(BuildStoredVoiceMessage(5202, true, "1", kVhMd5, 3, kVhVoiceBytes, "", false, dir).voicePlayed);
    ::DeleteFileW(file.c_str());
    ::RemoveDirectoryW(dir.c_str());

    //行控件：语音行建出一个编号为「基址 + 行下标」的语音条控件，整页排版后落在所在行之内
    VhGdiplusScope gdiplus;
    std::vector<HistoryRowView> rows(2);
    rows[0].m_senderName = L"A";
    rows[0].m_fullText = L"hi";
    rows[0].m_msgid = 5301;
    rows[1].m_senderName = L"B";
    rows[1].m_msgid = 5302;
    rows[1].m_isVoice = true;
    rows[1].m_voice = ready;
    rows[1].m_voiceShowDot = true;
    rows[1].m_fullText = L"[语音] 8″";
    std::unique_ptr<balloonwjui::DuiControl> list = BuildListContent(rows, L"", L"", kVhListWidth);
    REQUIRE(list);
    ChatVoiceStrip* strip = dynamic_cast<ChatVoiceStrip*>(list->FindCtrlById((UINT)(kIdHistoryVoiceBase + 1)));
    REQUIRE(strip != NULL);
    CHECK(strip->VoiceMessage().msgid == 5201);
    CHECK(list->FindCtrlById((UINT)(kIdHistoryVoiceBase + 0)) == NULL);
    RECT area = { 0, 0, kVhListWidth, 2000 };
    list->ForceLayout(area);
    balloonwjui::DuiControl* row1 = list->FindCtrlById((UINT)(kIdHistoryRowBase + 1));
    REQUIRE(row1 != NULL);
    const RECT stripRc = strip->GetRect();
    const RECT rowRc = row1->GetRect();
    CHECK(stripRc.bottom - stripRc.top == kVoiceStripH);
    CHECK(stripRc.right - stripRc.left == VoiceStripWidth(ready));
    CHECK(stripRc.top >= rowRc.top);
    CHECK(stripRc.bottom <= rowRc.bottom);
}

TEST_CASE("VM36 语音条控件：宽度随时长、未听才画红点、单击请求带消息 id", "[voicemsg]")
{
    //宽度：语音条与聊天气泡同宽，另加红点留白
    ChatMessage shortVoice = readyVoice(true);
    ChatMessage longVoice = readyVoice(true);
    longVoice.voiceDuration = 60;
    CHECK(VoiceStripWidth(shortVoice) == VoiceBubbleWidth(kVhDurationSec) + kVoiceDotGap + kVoiceDotSize);
    CHECK(VoiceStripWidth(longVoice) > VoiceStripWidth(shortVoice));
    RECT item = { 10, 20, 10 + VoiceStripWidth(shortVoice), 20 + kVoiceStripH };
    const RECT bar = VoiceStripBarRect(item, shortVoice);
    CHECK(bar.left == 10);
    CHECK(bar.right - bar.left == VoiceBubbleWidth(kVhDurationSec));
    CHECK(bar.bottom - bar.top == kVoiceStripH);

    //红点：对方发来且未听、并且允许画红点时才有
    VhGdiplusScope gdiplus;
    const int unplayed = countStripRedPixels(readyVoice(false), true);
    const int played = countStripRedPixels(readyVoice(true), true);
    const int noDot = countStripRedPixels(readyVoice(false), false);
    CHECK(unplayed - played >= 20);
    CHECK(noDot == played);

    //单击投出的请求：带 msgid 与 clientMsgId
    ChatMessage mine = readyVoice(true);
    mine.clientMsgId = "CM-36";
    const VoiceActionRequest req = MakeVoiceToggleRequest(mine);
    CHECK(req.m_msgid == 5101);
    CHECK(req.m_clientMsgId == "CM-36");

    //只有点在语音条上才处理：右侧红点留白不算
    ChatVoiceStrip strip;
    strip.SetVoiceMessage(shortVoice, true);
    strip.SetRect(item);
    POINT inBar = { bar.left + 5, bar.top + 5 };
    POINT inGap = { bar.right + 2, bar.top + 5 };
    CHECK(strip.OnLButtonDown(inBar, 0));
    CHECK_FALSE(strip.OnLButtonDown(inGap, 0));
}

TEST_CASE("VM36b 语音条只在自己的矩形之内作画，红点消失后不留残迹", "[voicemsg]")
{
    //第 5 期联调发现：消息记录窗里点开未听语音后，红点原位置残留一个灰点。语音条红点消失时只重绘自己的矩形，
    //所以凡是画到矩形之外的像素都不会被擦掉。这里把语音条放在画布中间，检查矩形之外一个像素都没有画
    VhGdiplusScope gdiplus;
    const int kMargin = 10;   //语音条离画布左边与上边的距离（像素），越出矩形的像素落在这段余量里
    const ChatMessage unplayed = readyVoice(false);
    const RECT item = { kMargin, kMargin, kMargin + VoiceStripWidth(unplayed), kMargin + kVoiceStripH };
    REQUIRE(item.right < kVhCanvasW);
    REQUIRE(item.bottom < kVhCanvasH);
    CHECK(countStripPixelsOutside(unplayed, true, item) == 0);
    CHECK(countStripPixelsOutside(readyVoice(true), true, item) == 0);
    //画红点时红点仍然完整可见（与 VM36 的阈值相同）
    CHECK(countStripRedPixels(unplayed, true) - countStripRedPixels(readyVoice(true), true) >= 20);
}

TEST_CASE("VM44b 红点同步（消息记录窗）：只在会话相符时把当前页里同一条语音标为已听", "[voicemsg]")
{
    VhGdiplusScope gdiplus;
    const int kViewCtype = 1;            //记录窗显示的是单聊
    const int64_t kViewCid = 5010;       //对端 userid
    const int kOtherCtype = 2;           //群聊
    const int64_t kOtherCid = 5011;      //另一个会话

    //当前页两条对方发来、未听的语音
    std::vector<HistoryRowView> rows(2);
    rows[0].m_senderName = L"A";
    rows[0].m_msgid = 4501;
    rows[0].m_isVoice = true;
    rows[0].m_voice = readyVoice(false);
    rows[0].m_voice.msgid = 4501;
    rows[0].m_voiceShowDot = true;
    rows[1] = rows[0];
    rows[1].m_msgid = 4502;
    rows[1].m_voice.msgid = 4502;
    std::unique_ptr<balloonwjui::DuiControl> list = BuildListContent(rows, L"", L"", kVhListWidth);
    REQUIRE(list);

    //按 msgid 找语音条
    ChatVoiceStrip* first = FindHistoryVoiceStrip(list.get(), 4501);
    ChatVoiceStrip* second = FindHistoryVoiceStrip(list.get(), 4502);
    REQUIRE(first != NULL);
    REQUIRE(second != NULL);
    CHECK(first->VoiceMessage().msgid == 4501);
    CHECK(second->VoiceMessage().msgid == 4502);
    CHECK(FindHistoryVoiceStrip(list.get(), 0) == NULL);
    CHECK(FindHistoryVoiceStrip(list.get(), 7777) == NULL);
    CHECK(FindHistoryVoiceStrip(NULL, 4501) == NULL);

    //会话不符（类型不同或 ID 不同）：不改
    CHECK_FALSE(MarkHistoryVoicePlayed(list.get(), kViewCtype, kViewCid, kViewCtype, kOtherCid, 4501));
    CHECK_FALSE(MarkHistoryVoicePlayed(list.get(), kViewCtype, kViewCid, kOtherCtype, kViewCid, 4501));
    CHECK_FALSE(first->VoiceMessage().voicePlayed);

    //会话相符：这一条改为已听，另一条不变；再标一次、页里没有的 msgid 都返回 false
    CHECK(MarkHistoryVoicePlayed(list.get(), kViewCtype, kViewCid, kViewCtype, kViewCid, 4501));
    CHECK(first->VoiceMessage().voicePlayed);
    CHECK_FALSE(second->VoiceMessage().voicePlayed);
    CHECK_FALSE(MarkHistoryVoicePlayed(list.get(), kViewCtype, kViewCid, kViewCtype, kViewCid, 4501));
    CHECK_FALSE(MarkHistoryVoicePlayed(list.get(), kViewCtype, kViewCid, kViewCtype, kViewCid, 7777));
    CHECK_FALSE(MarkHistoryVoicePlayed(NULL, kViewCtype, kViewCid, kViewCtype, kViewCid, 4502));
}

TEST_CASE("VM37 合并记录查看窗的语音行：显示成语音条、不画红点、本机文件决定就绪或需要下载", "[voicemsg]")
{
    const char* const content =
        "{\"title\":\"t\",\"messages\":["
        "{\"msgid\":\"61\",\"from\":\"100001\",\"msgtype\":1,\"timestamp\":1700000000000,"
        "\"content\":{\"text\":\"hi\"},\"fromname\":\"A\"},"
        "{\"msgid\":\"62\",\"from\":\"100002\",\"msgtype\":4,\"timestamp\":1700000060000,"
        "\"content\":{\"fileid\":\"998877\",\"md5\":\"0123456789abcdef0123456789abcdef\",\"duration\":65,"
        "\"size\":5,\"wave\":\"AAECAw==\"},\"fromname\":\"B\"}]}";
    net::MergedRecord record;
    REQUIRE(net::MessageProtocolClient::parseMergedContent(content, record));
    std::vector<HistoryRowView> rows;
    BuildMergedRecordRows(record, std::map<int64_t, CString>(), LocalDayKey(1700000000000LL), rows);
    REQUIRE(rows.size() == 2);
    CHECK_FALSE(rows[0].m_isVoice);
    REQUIRE(rows[1].m_isVoice);
    CHECK_FALSE(rows[1].m_voiceShowDot);
    CHECK(rows[1].m_voice.kind == kMsgVoice);
    CHECK(rows[1].m_voice.msgid == 62);
    CHECK(rows[1].m_voice.voiceDuration == 65);
    CHECK(rows[1].m_voice.voiceMd5 == kVhMd5);
    CHECK(rows[1].m_voice.voiceFileId == "998877");
    CHECK(rows[1].m_fullText == CString(L"[语音] 1′05″"));

    //本机文件：有大小相符的文件为就绪，没有时标为需要下载
    const std::wstring dir = tempVoiceDir(L"vm37_voice");
    const std::wstring file = dir + L"0123456789abcdef0123456789abcdef.ogg";
    ::DeleteFileW(file.c_str());
    ResolveVoiceRowLocalState(rows[1], dir);
    CHECK(rows[1].m_voice.voiceState == kVoiceStateDownloadFailed);
    CHECK(rows[1].m_voice.voiceLocalPath.IsEmpty());
    writeBytes(file, static_cast<size_t>(kVhVoiceBytes));
    ResolveVoiceRowLocalState(rows[1], dir);
    CHECK(rows[1].m_voice.voiceState == kVoiceStateReady);
    CHECK(rows[1].m_voice.voiceLocalPath == CString(file.c_str()));
    //非语音行不受影响
    ResolveVoiceRowLocalState(rows[0], dir);
    CHECK_FALSE(rows[0].m_isVoice);
    ::DeleteFileW(file.c_str());
    ::RemoveDirectoryW(dir.c_str());
}
