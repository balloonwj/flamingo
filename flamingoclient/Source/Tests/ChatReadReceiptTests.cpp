/**
 *  ChatReadReceiptTests.cpp
 *
 *  聊天气泡「已读回执」的显示规则测试。
 *
 *  背景（2026-09-30）：给离线的人发消息，气泡下方也显示「✓ 已读」。原因是气泡对
 *  所有自己发的消息无条件画「已读」，消息里的 read 字段从没被用上；而且群聊也共用
 *  这套气泡，群聊按协议根本不做已读回执。修正后的规则由 DecideReadReceipt 统一判定：
 *
 *    RR1-RR6  纯函数 DecideReadReceipt：已读 / 未读 / 尚未拿到服务器 msgid /
 *             对方的消息 / 未开启回执（群聊）/ 时间分隔行与文件夹行。
 *    RR7      回执行只在开启回执的列表里预留高度（群聊的自己消息不再多出一行空白）。
 *    RR8-RR10 绘制：未读画出主题橙的「未读」，已读画出灰色的「✓ 已读」，
 *             未开启回执或尚未拿到 msgid 时回执行什么都不画。
 *    RR11-RR13 按对方的已读位点批量标记：只标自己发出且 msgid 不大于位点的、位点只增
 *             不减、位点已知之后才回填 msgid 或才加载进来的消息也按位点判定。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存 + 离屏位图：不创建窗口、不要 HWND。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区宽度 / 高度（逻辑像素）。一条短消息加回执行远小于这个高度。
const int kListWidth  = 400;
const int kListHeight = 300;

// 服务端确认后回填的 msgid。取值本身无特殊含义，只要大于 0 即可。
const long long kServerMsgid = 9007199254741001LL;

// 判定「主题橙像素」的阈值：「未读」用主题橙 #FF7A45 绘制。抗锯齿会把边缘混成
// 浅色，只统计颜色足够纯的那部分：红通道高、绿通道居中、蓝通道低。
const int kOrangeMinR = 200;
const int kOrangeMinG = 90;
const int kOrangeMaxG = 170;
const int kOrangeMaxB = 130;

// 造一条自己发出的文字消息。
//   msgid：服务器 msgid，0 表示尚未拿到（发送中 / 发送失败）。
//   read： 对方是否已读。
ChatMessage MakeOwnText(long long msgid, bool read)
{
    ChatMessage m = MakeTextMessage(true, _T("回执测试"));
    m.msgid = msgid;
    m.read  = read;
    return m;
}

// 造一个只含一条消息的列表并按测试宽度排版。
//   receiptEnabled：列表是否开启已读回执（单聊开、群聊不开）。
void BuildList(ChatMessageList& list, const ChatMessage& msg, bool receiptEnabled)
{
    list.SetReadReceiptEnabled(receiptEnabled);
    list.AppendMessage(msg);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
}

// 取列表里第 idx 条真实消息的气泡（断言存在）。时间分隔行等 kMsgTime 行不计数：本人回显前面也会插时间行
// （docs/聊天消息时间显示方案.md 第 7 节，2026-10-14），按子控件下标取会取到时间行。
ChatBubble* BubbleAt(ChatMessageList& list, size_t idx)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    size_t seen = 0;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        REQUIRE(b != NULL);
        if (b->GetMessage().kind == kMsgTime)
            continue;
        if (seen == idx)
            return b;
        ++seen;
    }
    FAIL("no real message at index " << idx);
    return NULL;
}

// 把列表画到一张白底 32 位离屏位图上，按行优先取出全部像素（只保留 RGB）。
//   out：出参，kListWidth × kListHeight 个像素。
// @return 画布建不出来时返回 false。
bool PaintList(ChatMessageList& list, std::vector<unsigned int>& out)
{
    out.clear();
    HDC screenDC = ::GetDC(NULL);
    if (screenDC == NULL)
    {
        return false;
    }
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return false;
    }

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kListWidth;
    bi.bmiHeader.biHeight      = -kListHeight;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bitmap = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bitmap == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bitmap);

    RECT all = { 0, 0, kListWidth, kListHeight };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);

    list.OnPaint(memDC, all);
    ::GdiFlush();

    const unsigned int* px = (const unsigned int*)bits;
    out.assign(px, px + kListWidth * kListHeight);
    for (size_t i = 0; i < out.size(); ++i)
    {
        // 32 位 DIB 的高字节是未用的保留字节，比对时只看 RGB 三个通道。
        out[i] &= 0x00FFFFFFu;
    }

    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bitmap);
    ::DeleteDC(memDC);
    return true;
}

// 两次绘制结果之间，有多少像素不同；其中又有多少个在 b 里是主题橙。
//   outOrangeInB：出参，不同的像素里，b 一侧颜色是主题橙的个数。
int DiffPixels(const std::vector<unsigned int>& a, const std::vector<unsigned int>& b,
               int& outOrangeInB)
{
    outOrangeInB = 0;
    REQUIRE(a.size() == b.size());
    int diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i] == b[i])
        {
            continue;
        }
        ++diff;
        const int r = (int)((b[i] >> 16) & 0xFF);
        const int g = (int)((b[i] >> 8) & 0xFF);
        const int bl = (int)(b[i] & 0xFF);
        if (r >= kOrangeMinR && g >= kOrangeMinG && g <= kOrangeMaxG && bl <= kOrangeMaxB)
        {
            ++outOrangeInB;
        }
    }
    return diff;
}

// 按给定消息与回执开关造列表并绘制。
//   out：出参，绘制结果。
void RenderOne(const ChatMessage& msg, bool receiptEnabled, std::vector<unsigned int>& out)
{
    ChatMessageList list;
    BuildList(list, msg, receiptEnabled);
    REQUIRE(PaintList(list, out));
}

} // anonymous namespace

// =====================================================================
// RR1-RR6 纯函数 DecideReadReceipt
// =====================================================================

TEST_CASE("RR1 单聊里自己发出且对方已读的消息显示已读", "[readreceipt]")
{
    CHECK(DecideReadReceipt(MakeOwnText(kServerMsgid, true), true) == kReceiptRead);
}

TEST_CASE("RR2 单聊里自己发出且对方未读的消息显示未读", "[readreceipt]")
{
    CHECK(DecideReadReceipt(MakeOwnText(kServerMsgid, false), true) == kReceiptUnread);
}

TEST_CASE("RR3 尚未拿到服务器 msgid 的消息不显示回执", "[readreceipt]")
{
    // 发送中、发送失败的消息都还没有 msgid —— 服务端都还没收到，谈不上对方读没读。
    CHECK(DecideReadReceipt(MakeOwnText(0, false), true) == kReceiptNone);
    // read 字段即便被误置为真，也不能在拿到 msgid 之前显示已读。
    CHECK(DecideReadReceipt(MakeOwnText(0, true), true) == kReceiptNone);
}

TEST_CASE("RR4 对方发来的消息不显示回执", "[readreceipt]")
{
    ChatMessage m = MakeTextMessage(false, _T("对方的消息"));
    m.msgid = kServerMsgid;
    m.read  = true;
    CHECK(DecideReadReceipt(m, true) == kReceiptNone);
}

TEST_CASE("RR5 未开启回执的列表（群聊）不显示回执", "[readreceipt]")
{
    // 群聊按协议不做已读回执（服务端对群聊的 cmd_mark_read 不推送 cmd_message_read_notify）。
    CHECK(DecideReadReceipt(MakeOwnText(kServerMsgid, true), false) == kReceiptNone);
    CHECK(DecideReadReceipt(MakeOwnText(kServerMsgid, false), false) == kReceiptNone);
}

TEST_CASE("RR6 时间分隔行与文件夹行不显示回执", "[readreceipt]")
{
    ChatMessage timeRow = MakeOwnText(kServerMsgid, true);
    timeRow.kind = kMsgTime;
    CHECK(DecideReadReceipt(timeRow, true) == kReceiptNone);

    // 文件夹行是本地气泡，不进服务端消息表，不存在「对方读没读」。
    ChatMessage folderRow = MakeOwnText(kServerMsgid, true);
    folderRow.kind = kMsgFolder;
    CHECK(DecideReadReceipt(folderRow, true) == kReceiptNone);
}

// =====================================================================
// RR7 回执行只在开启回执的列表里预留高度
// =====================================================================

TEST_CASE("RR7 回执行只在开启回执时占高度", "[readreceipt][layout]")
{
    // 自己的消息：开启回执比不开启多出正好一行回执的高度（大于 0）。
    ChatMessageList withReceipt;
    BuildList(withReceipt, MakeOwnText(kServerMsgid, false), true);
    ChatMessageList withoutReceipt;
    BuildList(withoutReceipt, MakeOwnText(kServerMsgid, false), false);
    CHECK(BubbleAt(withReceipt, 0)->GetRowHeight() > BubbleAt(withoutReceipt, 0)->GetRowHeight());

    // 尚未拿到 msgid 时同样预留：msgid 回填后回执从无到有，行高不能跟着跳。
    ChatMessageList pending;
    BuildList(pending, MakeOwnText(0, false), true);
    CHECK(BubbleAt(pending, 0)->GetRowHeight() == BubbleAt(withReceipt, 0)->GetRowHeight());

    // 对方的消息：开不开回执行高都一样。
    ChatMessage peer = MakeTextMessage(false, _T("回执测试"));
    peer.msgid = kServerMsgid;
    ChatMessageList peerWith;
    BuildList(peerWith, peer, true);
    ChatMessageList peerWithout;
    BuildList(peerWithout, peer, false);
    CHECK(BubbleAt(peerWith, 0)->GetRowHeight() == BubbleAt(peerWithout, 0)->GetRowHeight());
}

// =====================================================================
// RR8-RR10 绘制
// =====================================================================
//
// 以「开启回执、但尚未拿到 msgid」的绘制结果为基准：此时回执行预留了高度、但什么都
// 不画。其余情形与它逐像素比较，多出来的像素就是回执行画上去的内容。

TEST_CASE("RR8 未读画出主题橙的「未读」", "[readreceipt][paint]")
{
    std::vector<unsigned int> baseline;
    RenderOne(MakeOwnText(0, false), true, baseline);
    std::vector<unsigned int> unread;
    RenderOne(MakeOwnText(kServerMsgid, false), true, unread);

    int orange = 0;
    const int diff = DiffPixels(baseline, unread, orange);
    INFO("diff=" << diff << " orange=" << orange);
    CHECK(diff > 0);
    CHECK(orange > 0);
}

TEST_CASE("RR9 已读画出灰色的「✓ 已读」，与未读画得不同", "[readreceipt][paint]")
{
    std::vector<unsigned int> baseline;
    RenderOne(MakeOwnText(0, false), true, baseline);
    std::vector<unsigned int> read;
    RenderOne(MakeOwnText(kServerMsgid, true), true, read);
    std::vector<unsigned int> unread;
    RenderOne(MakeOwnText(kServerMsgid, false), true, unread);

    int orange = 0;
    const int diff = DiffPixels(baseline, read, orange);
    INFO("diff=" << diff << " orange=" << orange);
    CHECK(diff > 0);
    // 「已读」是灰色，不应出现主题橙。
    CHECK(orange == 0);

    int orangeUnused = 0;
    CHECK(DiffPixels(read, unread, orangeUnused) > 0);
}

// =====================================================================
// RR11-RR13 按对方的已读位点批量标记（ChatMessageList::ApplyPeerReadPosition）
// =====================================================================
//
// 已读不是逐条确认，而是一个位点：对方报告「读到了第 X 条」，msgid ≤ X 的都算已读。
// 位点来自 cmd_message_read_notify 推送（实时）与同步消息应答（重开窗口 / 离线期间）。

namespace {

// 按消息 msgid 在列表里找到那一行，返回它当前的 read 字段。
bool IsRowRead(ChatMessageList& list, long long msgid, bool fromMe)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().msgid == msgid && b->GetMessage().fromMe == fromMe)
        {
            return b->GetMessage().read;
        }
    }
    FAIL("row not found, msgid=" << msgid);
    return false;
}

// 位点测试用的几条消息 msgid。取值只需保持大小顺序。
const long long kMsgidOwnA  = 100;
const long long kMsgidPeer  = 150;
const long long kMsgidOwnB  = 200;
const long long kMsgidOwnC  = 300;
const long long kMsgidLater = 250;   // 位点已知之后才拿到 msgid 的那一条

// 发送回执带回来的时间戳（毫秒）。取值本身无意义，非零即可。
const long long kAckTimestampMs = 1700000000000LL;

// 造一个开启回执的列表：自己的 A / B / C、对方的一条，以及一条尚未拿到 msgid 的自己的消息。
void BuildPositionList(ChatMessageList& list)
{
    list.SetReadReceiptEnabled(true);
    list.AppendMessage(MakeOwnText(kMsgidOwnA, false));
    ChatMessage peer = MakeTextMessage(false, _T("对方的消息"));
    peer.msgid = kMsgidPeer;
    list.AppendMessage(peer);
    list.AppendMessage(MakeOwnText(kMsgidOwnB, false));
    list.AppendMessage(MakeOwnText(kMsgidOwnC, false));
    ChatMessage pending = MakeOwnText(0, false);
    pending.clientMsgId = "cmid-pending";
    list.AppendMessage(pending);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
}

} // anonymous namespace

TEST_CASE("RR11 只把自己发出、msgid 不大于位点的消息标为已读", "[readreceipt][position]")
{
    ChatMessageList list;
    BuildPositionList(list);

    // 对方读到了 B：A、B 标为已读，一共两行变化。
    CHECK(list.ApplyPeerReadPosition(kMsgidOwnB) == 2);
    CHECK(IsRowRead(list, kMsgidOwnA, true));
    CHECK(IsRowRead(list, kMsgidOwnB, true));
    // C 在位点之后，仍是未读。
    CHECK_FALSE(IsRowRead(list, kMsgidOwnC, true));
    // 对方发来的消息不受影响（它没有「对方读没读」）。
    CHECK_FALSE(IsRowRead(list, kMsgidPeer, false));
    // 尚未拿到 msgid 的消息不标 —— 无从与位点比较。
    CHECK_FALSE(IsRowRead(list, 0, true));
    CHECK(list.PeerReadMsgid() == kMsgidOwnB);
}

TEST_CASE("RR12 位点只增不减，重复或更小的位点不改变任何行", "[readreceipt][position]")
{
    ChatMessageList list;
    BuildPositionList(list);

    CHECK(list.ApplyPeerReadPosition(kMsgidOwnC) == 3);
    // 更小的位点（乱序到达的旧推送）：不回退位点、不把已读改回未读。
    CHECK(list.ApplyPeerReadPosition(kMsgidOwnA) == 0);
    CHECK(list.PeerReadMsgid() == kMsgidOwnC);
    CHECK(IsRowRead(list, kMsgidOwnC, true));
    // 相同的位点再来一次：没有新的行被标记。
    CHECK(list.ApplyPeerReadPosition(kMsgidOwnC) == 0);
    // 非正的位点直接忽略。
    CHECK(list.ApplyPeerReadPosition(0) == 0);
    CHECK(list.PeerReadMsgid() == kMsgidOwnC);
}

TEST_CASE("RR13 位点已知之后才出现的消息也按位点判定", "[readreceipt][position]")
{
    ChatMessageList list;
    BuildPositionList(list);
    REQUIRE(list.ApplyPeerReadPosition(kMsgidOwnC) == 3);

    // 发送回执晚于已读推送到达：回填的 msgid 不大于位点，回填时就标为已读。
    REQUIRE(list.BackfillSentMessage("cmid-pending", kMsgidLater, kAckTimestampMs));
    CHECK(IsRowRead(list, kMsgidLater, true));

    // 位点已知之后才加载进来的历史消息（先收到位点、后拉到这一页）：追加时就按位点判定。
    const long long olderOwn = 50;
    list.AppendMessage(MakeOwnText(olderOwn, false));
    CHECK(IsRowRead(list, olderOwn, true));
    // 位点之后的新消息照旧是未读。
    const long long newerOwn = 400;
    list.AppendMessage(MakeOwnText(newerOwn, false));
    CHECK_FALSE(IsRowRead(list, newerOwn, true));
}

// =====================================================================
// RR14 真实消息的构造函数不能预置「已读」
// =====================================================================
//
// 2026-09-30 实机回归：周思源给离线的韩晓燕发了两条表情，服务端返回的对方已读位点是 0，
// 气泡却显示「✓ 已读」。原因是 MakeTextMessage / MakeImageMessage / MakeFileMessage
// 都复用了设计稿 mock 的构造函数，而那几个函数写着 m.read = fromMe（「自己的消息 mock
// 一律已读」）。RR1-RR13 构造消息后都手动给 read 赋了值，正好把它掩盖掉。本条直接用真实
// 构造函数、不碰 read。

TEST_CASE("RR14 真实消息构造出来时一律是未读", "[readreceipt]")
{
    // 表情是内嵌在文字消息里发的，与文字走同一个构造函数。
    ChatMessage text = MakeTextMessage(true, _T("[表情]"));
    CHECK_FALSE(text.read);

    ChatMessage image = MakeImageMessage(true, _T("C:\\nonexistent.png"), 100, 80, "cmid-img", 0);
    CHECK_FALSE(image.read);

    ChatMessage file = MakeFileMessage(true, _T("a.txt"), 1024, _T(""), "cmid-file", 0);
    CHECK_FALSE(file.read);

    // 拿到服务器 msgid 之后、对方的位点还没越过它时，应当显示「未读」。
    text.msgid = kServerMsgid;
    CHECK(DecideReadReceipt(text, true) == kReceiptUnread);
    image.msgid = kServerMsgid;
    CHECK(DecideReadReceipt(image, true) == kReceiptUnread);
    file.msgid = kServerMsgid;
    CHECK(DecideReadReceipt(file, true) == kReceiptUnread);
}

TEST_CASE("RR10 未开启回执时已读消息也不画回执", "[readreceipt][paint]")
{
    // 未开启回执时回执行不占高度，以同样未开启回执、尚未拿到 msgid 的绘制为基准。
    std::vector<unsigned int> baseline;
    RenderOne(MakeOwnText(0, false), false, baseline);
    std::vector<unsigned int> read;
    RenderOne(MakeOwnText(kServerMsgid, true), false, read);

    int orange = 0;
    CHECK(DiffPixels(baseline, read, orange) == 0);
}
