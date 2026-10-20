/**
 *  单聊密聊 exe 一侧的单元测试（标签 [secretchat]），SecretChatHostTests.cpp
 *  覆盖（对应 docs/密聊方案.md 第 8 节 exe 部分，第 3a 期）：
 *    - SCE1 入口显隐：模块就绪 × 企业开关 × 服务端确认 × 对方是否本人；窗口处于密聊状态时「密聊」按钮隐藏；
 *    - SCE2 代发放行：只放行 2600~2699 段、包体非空、主连接已连上；
 *    - SCE3 会话登记簿：发起、收到邀请、应答、结束各条路径下会话号与对方的对应、待应答邀请的补出与到期；
 *    - SCE4 邀请卡片内容：两侧的标题、说明、剩余秒数与按钮，对方无法防截图时的说明，剩余秒数向上取整；
 *    - SCE5 卡片在消息列表里：按会话号查找、改写、移除、列出；有按钮的卡片比没有按钮的高；
 *    - SCE6 卡片按钮：被邀请方左「同意」右「忽略」，发起方只有「取消」；多选模式下不命中、不能选中；没有右键菜单；
 *    - SCE7 三种窗口状态下各按钮是否可用；
 *    - SCE8 提示文字：没能建立时的本地提示、发起失败的原因；
 *    - SCE9 列表的密聊限制：打开后不能多选（正在多选的立即退出）、不能复制、普通消息同样没有右键菜单；
 *    - SCE10 清除密聊内容：只移除密聊消息与邀请卡片，普通内容保留；气泡正文清零；
 *    - SCE11 内存图片：伪路径与前缀规则、登记、按会话前缀撤销；
 *    - SCE12 本地消息号互不冲突、发送失败原因、接收进度文字、图片大小上限；
 *    - SCE13 大图浮层：只看不存的工具条没有复制、另存与识别文字，画布从内存加载，浮层打开、Esc 关闭、解码失败不打开。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <climits>   // INT_MAX：命中扫描的初值
#include <set>
#include <string>
#include <gdiplus.h>

#include "../Feature/SecretChat/SecretChatGate.h"
#include "../UI/BalloonChatWnd/ChatSecretMode.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"   // SCE5、SCE6：把卡片排进一个列表里测量、绘制与命中
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatImageCache.h"    // SCE11：内存图片的登记与撤销
#include "../UI/BalloonChatWnd/SecretImageOverlay.h" // SCE13：大图浮层
#include "../UI/BalloonImageViewer/ImageCanvas.h"     // SCE13：画布从内存加载
#include "../UI/BalloonImageViewer/ImageViewerToolbar.h" // SCE13：只看不存的工具条
#include "../net/Protocol.h"

using namespace balloonchatwnd;
using namespace balloonsecret;

namespace {

//两个会话号与两位对方
const uint64_t kSessionA = 7001;
const uint64_t kSessionB = 7002;
const int64_t  kPeerA    = 20001;
const int64_t  kPeerB    = 20002;

//邀请的有效期（毫秒）与测试用的「当前时刻」（GetTickCount64 口径）
const uint64_t kInviteMs = 60000;
const uint64_t kNowTick  = 1000000;

//防截图级别：0 做不到，2 截图里不出现（与 secretchat::CaptureProtectLevel 一致）
const int kProtectNone    = 0;
const int kProtectExclude = 2;

//单聊窗控件 id，与 ChatWindowLayout.h 的 CtrlId 一致（这里不包含那个头文件，它要求先引入 WTL 的全套布局依赖）
const unsigned int kCtrlInputEdit  = 401;
const unsigned int kCtrlSendButton = 402;
const unsigned int kCtrlHdrVoice   = 410;
const unsigned int kCtrlHdrHistory = 414;
const unsigned int kCtrlTbEmoji    = 420;
const unsigned int kCtrlTbImage    = 421;
const unsigned int kCtrlTbFile     = 422;
const unsigned int kCtrlHdrSecret  = 466;
const unsigned int kCtrlNotManaged = 405;   // 顶部离线提示条：不受密聊状态管理

//消息区尺寸（逻辑像素）：聊天窗常见宽度，高度放得下几张卡片
const int kListW = 900;
const int kListH = 1200;

//命中扫描的步长（像素）
const int kScanStep = 2;

//一张套好的邀请卡片
ChatMessage InviteCard(bool iAmInviter, uint64_t sessionId, uint32_t remainSeconds, int peerProtect)
{
    ChatMessage msg;
    ApplySecretInviteCard(msg, iAmInviter, CString(L"张三"), sessionId, remainSeconds, peerProtect);
    return msg;
}

//把一条消息排进列表并画一遍（绘制时卡片才记下按钮行的矩形），返回那一行
ChatBubble* LayoutAndPaint(ChatMessageList& list, const ChatMessage& msg)
{
    list.AppendMessage(msg);
    RECT all = { 0, 0, kListW, kListH };
    list.ForceLayout(all);
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kListW, kListH);
    ::ReleaseDC(NULL, screenDC);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    list.OnPaint(memDC, all);
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return dynamic_cast<ChatBubble*>(list.Children().back().get());
}

//扫描一行里所有点，收集命中到的按钮下标；同时记下下标 0 的最左与下标 1 的最右横坐标
//（2026-10-14 起被邀请方的「忽略」（下标 1）在左、「同意」（下标 0）在右，docs/聊天卡片美化方案.md 5.4）
std::set<int> ScanButtons(const ChatBubble* bubble, int& minX0, int& maxX1)
{
    std::set<int> hits;
    minX0 = INT_MAX;
    maxX1 = -1;
    const RECT rc = bubble->GetRect();
    for (int y = rc.top; y < rc.bottom; y += kScanStep)
    {
        for (int x = rc.left; x < rc.right; x += kScanStep)
        {
            POINT pt = { x, y };
            const int idx = bubble->SecretInviteButtonAt(pt);
            if (idx < 0)
                continue;
            hits.insert(idx);
            if (idx == 0 && x < minX0)
                minX0 = x;
            if (idx == 1 && x > maxX1)
                maxX1 = x;
        }
    }
    return hits;
}

//SCE13：测试用的小图尺寸（像素）
const int kTinyImageW = 4;
const int kTinyImageH = 3;

//把一张 kTinyImageW × kTinyImageH 的纯色位图编码成 PNG 文件字节；失败返回空
std::vector<unsigned char> MakeTinyPng()
{
    std::vector<unsigned char> out;
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return out;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, info);
    CLSID png;
    bool found = false;
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(info[i].MimeType, L"image/png") == 0)
        {
            png = info[i].Clsid;
            found = true;
            break;
        }
    }
    if (!found)
        return out;
    Gdiplus::Bitmap bmp(kTinyImageW, kTinyImageH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0x33, 0x99, 0xFF));
        g.FillRectangle(&brush, 0, 0, kTinyImageW, kTinyImageH);
    }
    IStream* stream = NULL;
    if (FAILED(::CreateStreamOnHGlobal(NULL, TRUE, &stream)) || stream == NULL)
        return out;
    if (bmp.Save(stream, &png, NULL) == Gdiplus::Ok)
    {
        STATSTG stat;
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && SUCCEEDED(stream->Seek(zero, STREAM_SEEK_SET, NULL)))
        {
            out.resize(static_cast<size_t>(stat.cbSize.QuadPart));
            ULONG read = 0;
            stream->Read(&out[0], static_cast<ULONG>(out.size()), &read);
            out.resize(read);
        }
    }
    stream->Release();
    return out;
}

//卡片那一行的行高
int RowHeight(const ChatMessage& msg)
{
    ChatMessageList list;
    list.AppendMessage(msg);
    RECT all = { 0, 0, kListW, kListH };
    list.ForceLayout(all);
    ChatBubble* bubble = dynamic_cast<ChatBubble*>(list.Children().back().get());
    return (bubble != NULL) ? bubble->GetRowHeight() : -1;
}

} // namespace

TEST_CASE("SCE1 入口显隐：四个条件同时满足才显示，密聊状态下隐藏", "[secretchat]")
{
    CHECK(ShowSecretEntry(true, true, true, false));
    //任一条件不满足都不显示
    CHECK_FALSE(ShowSecretEntry(false, true, true, false));
    CHECK_FALSE(ShowSecretEntry(true, false, true, false));
    CHECK_FALSE(ShowSecretEntry(true, true, false, false));
    CHECK_FALSE(ShowSecretEntry(true, true, true, true));

    //「密聊」按钮本身受密聊状态管理：普通状态可用，密聊中与只能查看时隐藏
    CHECK(IsSecretCtrlAvailable(kSecretStateNormal, kCtrlHdrSecret));
    CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateActive, kCtrlHdrSecret));
    CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateReadOnly, kCtrlHdrSecret));
}

TEST_CASE("SCE2 代发放行：只放行密聊号段、包体非空且已连上", "[secretchat]")
{
    CHECK(CanSendSecretCommand(cmd_secret_invite, 10, true));
    CHECK(CanSendSecretCommand(2600, 1, true));
    CHECK(CanSendSecretCommand(2699, 1, true));
    //号段之外
    CHECK_FALSE(CanSendSecretCommand(2599, 10, true));
    CHECK_FALSE(CanSendSecretCommand(2700, 10, true));
    CHECK_FALSE(CanSendSecretCommand(cmd_send_message, 10, true));
    //包体为空、没连上
    CHECK_FALSE(CanSendSecretCommand(cmd_secret_invite, 0, true));
    CHECK_FALSE(CanSendSecretCommand(cmd_secret_invite, 10, false));
}

TEST_CASE("SCE3 会话登记簿：发起、收到、应答、结束与到期", "[secretchat]")
{
    SECTION("发起成功后登记对方，结束后删除")
    {
        SecretSessionBook book;
        book.OnInviteSent(kSessionA, kPeerA);
        CHECK(book.PeerOf(kSessionA) == kPeerA);
        //本人发起的不是待应答邀请
        PendingSecretInvite invite;
        CHECK_FALSE(book.GetIncoming(kPeerA, kNowTick, invite));
        book.Forget(kSessionA);
        CHECK(book.PeerOf(kSessionA) == 0);
        CHECK(book.SessionCount() == 0);
        //非法参数不登记
        book.OnInviteSent(0, kPeerA);
        book.OnInviteSent(kSessionA, 0);
        CHECK(book.SessionCount() == 0);
    }

    SECTION("收到邀请：倒计时内能补出，到期后不再补出")
    {
        SecretSessionBook book;
        book.OnInviteArrived(kSessionA, kPeerA, kNowTick + kInviteMs, kProtectNone);
        CHECK(book.PeerOf(kSessionA) == kPeerA);
        PendingSecretInvite invite;
        REQUIRE(book.GetIncoming(kPeerA, kNowTick, invite));
        CHECK(invite.m_sessionId == kSessionA);
        CHECK(invite.m_deadlineTick == kNowTick + kInviteMs);
        CHECK(invite.m_peerProtect == kProtectNone);
        //别人没有发来邀请
        CHECK_FALSE(book.GetIncoming(kPeerB, kNowTick, invite));
        //到期：不再补出，且顺带删除
        CHECK_FALSE(book.GetIncoming(kPeerA, kNowTick + kInviteMs, invite));
        CHECK_FALSE(book.GetIncoming(kPeerA, kNowTick, invite));
        //会话号与对方的对应仍在，等服务端的超时推送来了再删
        CHECK(book.PeerOf(kSessionA) == kPeerA);
    }

    SECTION("同意后不再待应答，会话登记保留；忽略后全部删除")
    {
        SecretSessionBook book;
        book.OnInviteArrived(kSessionA, kPeerA, kNowTick + kInviteMs, kProtectExclude);
        book.OnResponded(kSessionA);
        PendingSecretInvite invite;
        CHECK_FALSE(book.GetIncoming(kPeerA, kNowTick, invite));
        CHECK(book.PeerOf(kSessionA) == kPeerA);

        book.OnInviteArrived(kSessionB, kPeerB, kNowTick + kInviteMs, kProtectExclude);
        book.Forget(kSessionB);
        CHECK_FALSE(book.GetIncoming(kPeerB, kNowTick, invite));
        CHECK(book.PeerOf(kSessionB) == 0);
    }

    SECTION("同一个人发来新邀请：覆盖旧的待应答记录，旧会话结束时不删新的")
    {
        SecretSessionBook book;
        book.OnInviteArrived(kSessionA, kPeerA, kNowTick + kInviteMs, kProtectExclude);
        book.OnInviteArrived(kSessionB, kPeerA, kNowTick + kInviteMs, kProtectNone);
        PendingSecretInvite invite;
        REQUIRE(book.GetIncoming(kPeerA, kNowTick, invite));
        CHECK(invite.m_sessionId == kSessionB);
        book.Forget(kSessionA);
        REQUIRE(book.GetIncoming(kPeerA, kNowTick, invite));
        CHECK(invite.m_sessionId == kSessionB);
    }

    SECTION("退出登录：全部清空")
    {
        SecretSessionBook book;
        book.OnInviteSent(kSessionA, kPeerA);
        book.OnInviteArrived(kSessionB, kPeerB, kNowTick + kInviteMs, kProtectExclude);
        book.Clear();
        CHECK(book.SessionCount() == 0);
        PendingSecretInvite invite;
        CHECK_FALSE(book.GetIncoming(kPeerB, kNowTick, invite));
    }

    SECTION("剩余时间的换算：不足一秒向上取整，到期为 0")
    {
        CHECK(SecretInviteRemainMs(kNowTick + kInviteMs, kNowTick) == kInviteMs);
        CHECK(SecretInviteRemainMs(kNowTick, kNowTick + 1) == 0);
        CHECK(SecretInviteRemainSeconds(kInviteMs) == 60);
        CHECK(SecretInviteRemainSeconds(59001) == 60);
        CHECK(SecretInviteRemainSeconds(1) == 1);
        CHECK(SecretInviteRemainSeconds(0) == 0);
    }
}

TEST_CASE("SCE4 邀请卡片内容：两侧的标题、说明、按钮与剩余秒数", "[secretchat]")
{
    //被邀请方：画在左侧，「张三 邀请你进入密聊」，「同意」「忽略」两个按钮
    const ChatMessage invitee = InviteCard(false, kSessionA, 58, kProtectExclude);
    CHECK(invitee.kind == kMsgSecretInvite);
    CHECK_FALSE(invitee.fromMe);
    CHECK(invitee.msgid == 0);
    CHECK(invitee.secretSessionId == kSessionA);
    CHECK(invitee.secretInviteTitle == CString(L"张三 邀请你进入密聊"));
    CHECK(invitee.secretInviteSubtitle == CString(L"密聊消息不会被记录，退出后全部清除"));
    CHECK(invitee.secretInviteStatus == CString(L"剩余 58 秒"));
    CHECK(invitee.secretInviteButtons == 2);
    CHECK(SecretInviteButtonText(2, 0) == CString(L"同意"));
    CHECK(SecretInviteButtonText(2, 1) == CString(L"忽略"));
    CHECK(SecretInviteButtonAction(2, 0) == kSecretInviteAccept);
    CHECK(SecretInviteButtonAction(2, 1) == kSecretInviteIgnore);

    //发起方：画在右侧，「已邀请对方进入密聊」「等待对方同意」，只有「取消」
    const ChatMessage inviter = InviteCard(true, kSessionB, 60, kProtectExclude);
    CHECK(inviter.fromMe);
    CHECK(inviter.secretInviteTitle == CString(L"已邀请对方进入密聊"));
    CHECK(inviter.secretInviteSubtitle == CString(L"等待对方同意"));
    CHECK(inviter.secretInviteButtons == 1);
    CHECK(SecretInviteButtonText(1, 0) == CString(L"取消"));
    CHECK(SecretInviteButtonAction(1, 0) == kSecretInviteCancel);

    //越界的按钮
    CHECK(SecretInviteButtonAction(1, 1) == 0);
    CHECK(SecretInviteButtonAction(2, 2) == 0);
    CHECK(SecretInviteButtonAction(0, 0) == 0);
    CHECK(SecretInviteButtonText(2, 2).IsEmpty());

    //对方无法防截图：说明行如实写明（docs/密聊方案.md 4.6 第 1 条）
    const ChatMessage unprotected = InviteCard(false, kSessionA, 58, kProtectNone);
    CHECK(unprotected.secretInviteSubtitle == CString(L"对方的系统无法防止截图"));

    //倒计时只改剩余秒数
    ChatMessage ticking = invitee;
    UpdateSecretInviteRemain(ticking, 3);
    CHECK(ticking.secretInviteStatus == CString(L"剩余 3 秒"));
    CHECK(ticking.secretInviteTitle == invitee.secretInviteTitle);
}

TEST_CASE("SCE5 卡片在消息列表里：按会话号查找、改写、移除，有按钮的卡片更高", "[secretchat]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        ChatMessage text;
        text.kind = kMsgText;
        text.text = L"普通消息";
        list.AppendMessage(text);
        list.AppendMessage(InviteCard(false, kSessionA, 58, kProtectExclude));
        list.AppendMessage(InviteCard(true, kSessionB, 60, kProtectExclude));

        const std::vector<unsigned long long> ids = list.GetSecretInviteSessionIds();
        REQUIRE(ids.size() == 2);
        CHECK(ids[0] == kSessionA);
        CHECK(ids[1] == kSessionB);

        ChatMessage found;
        REQUIRE(list.GetSecretInviteMessage(kSessionA, found));
        CHECK(found.secretInviteButtons == 2);
        CHECK_FALSE(list.GetSecretInviteMessage(0, found));
        CHECK_FALSE(list.GetSecretInviteMessage(kSessionA + kSessionB, found));

        //改写剩余秒数
        UpdateSecretInviteRemain(found, 10);
        REQUIRE(list.SetSecretInviteMessage(kSessionA, found, false));
        ChatMessage again;
        REQUIRE(list.GetSecretInviteMessage(kSessionA, again));
        CHECK(again.secretInviteStatus == CString(L"剩余 10 秒"));
        CHECK_FALSE(list.SetSecretInviteMessage(kSessionA + kSessionB, found, true));

        //移除：只移除那一张，普通消息与另一张卡片不动
        REQUIRE(list.RemoveSecretInviteMessage(kSessionA));
        CHECK_FALSE(list.GetSecretInviteMessage(kSessionA, found));
        CHECK(list.GetSecretInviteMessage(kSessionB, found));
        CHECK(list.Children().size() == 2);
        CHECK_FALSE(list.RemoveSecretInviteMessage(kSessionA));

        //卡片底部一行常驻（左边是类型标签「密聊」，docs/聊天卡片美化方案.md 3.6、第 9 节），有没有按钮行高都一样
        ChatMessage noButton = InviteCard(false, kSessionA, 58, kProtectExclude);
        noButton.secretInviteButtons = 0;
        CHECK(RowHeight(InviteCard(false, kSessionA, 58, kProtectExclude)) == RowHeight(noButton));
        CHECK(RowHeight(InviteCard(true, kSessionB, 60, kProtectExclude))
              == RowHeight(InviteCard(false, kSessionA, 58, kProtectExclude)));
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("SCE6 卡片按钮：左忽略右同意、发起方只有取消，多选不命中，没有右键菜单", "[secretchat]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        SECTION("被邀请方：两个按钮各有自己的矩形")
        {
            ChatMessageList list;
            ChatBubble* bubble = LayoutAndPaint(list, InviteCard(false, kSessionA, 58, kProtectExclude));
            REQUIRE(bubble != NULL);
            CHECK(bubble->IsSecretRow());
            int minX0 = 0;
            int maxX1 = 0;
            const std::set<int> hits = ScanButtons(bubble, minX0, maxX1);
            CHECK(hits.size() == 2);
            CHECK(hits.count(0) == 1);
            CHECK(hits.count(1) == 1);
            //「忽略」（编号 1）全在「同意」（编号 0）左边：底部一行的按钮从右往左排，主要按钮「同意」在最右
            //（docs/聊天卡片美化方案.md 5.4、第 9 节；按钮编号的含义不变）
            CHECK(maxX1 < minX0);
        }

        SECTION("发起方：只有一个按钮")
        {
            ChatMessageList list;
            ChatBubble* bubble = LayoutAndPaint(list, InviteCard(true, kSessionB, 60, kProtectExclude));
            REQUIRE(bubble != NULL);
            int minX0 = 0;
            int maxX1 = 0;
            const std::set<int> hits = ScanButtons(bubble, minX0, maxX1);
            CHECK(hits.size() == 1);
            CHECK(hits.count(0) == 1);
        }

        SECTION("普通消息与会话号为 0 的卡片不命中")
        {
            ChatMessageList list;
            ChatMessage text;
            text.kind = kMsgText;
            text.text = L"普通消息";
            ChatBubble* bubble = LayoutAndPaint(list, text);
            REQUIRE(bubble != NULL);
            CHECK_FALSE(bubble->IsSecretRow());
            int minX0 = 0;
            int maxX1 = 0;
            CHECK(ScanButtons(bubble, minX0, maxX1).empty());

            ChatMessageList list2;
            ChatBubble* dead = LayoutAndPaint(list2, InviteCard(false, 0, 58, kProtectExclude));
            REQUIRE(dead != NULL);
            CHECK(ScanButtons(dead, minX0, maxX1).empty());
        }

        SECTION("多选模式下：按钮不命中，点击也不能选中")
        {
            ChatMessageList list;
            ChatBubble* bubble = LayoutAndPaint(list, InviteCard(false, kSessionA, 58, kProtectExclude));
            REQUIRE(bubble != NULL);
            list.SetMultiSelectMode(true);
            int minX0 = 0;
            int maxX1 = 0;
            CHECK(ScanButtons(bubble, minX0, maxX1).empty());
            const RECT rc = bubble->GetRect();
            POINT center = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
            CHECK_FALSE(bubble->OnLButtonDown(center, 0));
            CHECK_FALSE(bubble->IsSelected());
        }

        SECTION("右键：密聊卡片已处理（不弹任何菜单）")
        {
            ChatMessageList list;
            ChatBubble* bubble = LayoutAndPaint(list, InviteCard(false, kSessionA, 58, kProtectExclude));
            REQUIRE(bubble != NULL);
            const RECT rc = bubble->GetRect();
            POINT center = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
            CHECK(bubble->OnRButtonDown(center, 0));
        }
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("SCE7 三种窗口状态下各按钮是否可用", "[secretchat]")
{
    size_t count = 0;
    const unsigned int* managed = SecretManagedCtrlIds(count);
    REQUIRE(count > 0);
    //普通状态：全部可用
    for (size_t i = 0; i < count; ++i)
    {
        CHECK(IsSecretCtrlAvailable(kSecretStateNormal, managed[i]));
    }
    CHECK(IsSecretCtrlAvailable(kSecretStateNormal, kCtrlInputEdit));
    CHECK(IsSecretCtrlAvailable(kSecretStateNormal, kCtrlSendButton));

    //密聊中：工具栏只剩表情与图片，输入框与发送按钮可用，头部按钮全部隐藏
    CHECK(IsSecretCtrlAvailable(kSecretStateActive, kCtrlTbEmoji));
    CHECK(IsSecretCtrlAvailable(kSecretStateActive, kCtrlTbImage));
    CHECK(IsSecretCtrlAvailable(kSecretStateActive, kCtrlInputEdit));
    CHECK(IsSecretCtrlAvailable(kSecretStateActive, kCtrlSendButton));
    CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateActive, kCtrlTbFile));
    CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateActive, kCtrlHdrVoice));
    CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateActive, kCtrlHdrHistory));
    size_t availableInActive = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (IsSecretCtrlAvailable(kSecretStateActive, managed[i]))
            ++availableInActive;
    }
    CHECK(availableInActive == 2);

    //只能查看：受管的全部不可用，输入框与发送按钮也不可用
    for (size_t i = 0; i < count; ++i)
    {
        CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateReadOnly, managed[i]));
    }
    CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateReadOnly, kCtrlInputEdit));
    CHECK_FALSE(IsSecretCtrlAvailable(kSecretStateReadOnly, kCtrlSendButton));

    //不受管的控件在任何状态下都可用
    CHECK(IsSecretCtrlAvailable(kSecretStateActive, kCtrlNotManaged));
    CHECK(IsSecretCtrlAvailable(kSecretStateReadOnly, kCtrlNotManaged));
    CHECK(IsSecretState(kSecretStateActive));
    CHECK(IsSecretState(kSecretStateReadOnly));
    CHECK_FALSE(IsSecretState(kSecretStateNormal));
}

TEST_CASE("SCE9 列表的密聊限制：不能多选、不能复制、整张列表没有右键菜单", "[secretchat]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        ChatMessage text;
        text.kind = kMsgText;
        text.text = L"普通消息";
        ChatBubble* bubble = LayoutAndPaint(list, text);
        REQUIRE(bubble != NULL);
        const RECT rc = bubble->GetRect();
        POINT center = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };

        //没有限制时：普通消息不是密聊内容，可以进入多选
        CHECK_FALSE(bubble->IsSecretLocked());
        list.SetMultiSelectMode(true);
        CHECK(list.IsMultiSelectMode());

        //打开限制：正在多选的立即退出，之后再也进不去；普通消息同样被锁住（没有右键菜单、不能拖出）
        list.SetSecretRestricted(true);
        CHECK(list.IsSecretRestricted());
        CHECK_FALSE(list.IsMultiSelectMode());
        list.SetMultiSelectMode(true);
        CHECK_FALSE(list.IsMultiSelectMode());
        list.EnterMultiSelectWith(bubble);
        CHECK_FALSE(list.IsMultiSelectMode());
        CHECK(bubble->IsSecretLocked());
        CHECK(bubble->OnRButtonDown(center, 0));
        CHECK_FALSE(list.CopySelectedTextToClipboard());

        //关闭限制：恢复正常
        list.SetSecretRestricted(false);
        CHECK_FALSE(bubble->IsSecretLocked());
        list.SetMultiSelectMode(true);
        CHECK(list.IsMultiSelectMode());
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("SCE10 清除密聊内容：只移除密聊消息与邀请卡片，普通内容保留", "[secretchat]")
{
    ChatMessageList list;
    ChatMessage normal;
    normal.kind = kMsgText;
    normal.text = L"普通消息";
    list.AppendMessage(normal);

    ChatMessage secretText;
    secretText.kind = kMsgText;
    secretText.text = L"只在内存里的密聊文字";
    secretText.isSecret = true;
    secretText.clientMsgId = SecretClientMsgId(kSessionA, true, 1);
    list.AppendMessage(secretText);

    ChatMessage secretImage;
    secretImage.kind = kMsgImage;
    secretImage.isSecret = true;
    secretImage.imagePath = SecretImageMemoryPath(kPeerA, kSessionA, false, 1).c_str();
    list.AppendMessage(secretImage);

    list.AppendMessage(InviteCard(false, kSessionB, 58, kProtectExclude));

    CHECK(list.WipeSecretMessages() == 3);
    REQUIRE(list.Children().size() == 1);
    const ChatBubble* left = dynamic_cast<const ChatBubble*>(list.Children().front().get());
    REQUIRE(left != NULL);
    CHECK(left->GetMessage().text == CString(L"普通消息"));
    CHECK(list.GetSecretInviteSessionIds().empty());
    //没有密聊内容时什么都不做
    CHECK(list.WipeSecretMessages() == 0);

    //气泡自身的清零：正文清空
    ChatBubble wiped;
    wiped.SetMessage(secretText);
    wiped.WipeSecretText();
    CHECK(wiped.GetMessage().text.IsEmpty());
}

TEST_CASE("SCE11 内存图片：伪路径、登记、按会话前缀撤销", "[secretchat]")
{
    const std::wstring mineA = SecretImageMemoryPath(kPeerA, kSessionA, true, 1);
    const std::wstring peerA = SecretImageMemoryPath(kPeerA, kSessionA, false, 1);
    const std::wstring otherB = SecretImageMemoryPath(kPeerB, kSessionB, false, 1);
    CHECK(ChatImageCache::IsMemoryPath(mineA));
    CHECK_FALSE(ChatImageCache::IsMemoryPath(L"C:\\images\\a.png"));
    //本人发出与对方发来的同号图片不冲突
    CHECK(mineA != peerA);
    //同一个对方的全部密聊图片共享前缀，换了对方前缀不同
    const std::wstring prefixA = SecretImageMemoryPrefix(kPeerA);
    CHECK(mineA.compare(0, prefixA.size(), prefixA) == 0);
    CHECK(peerA.compare(0, prefixA.size(), prefixA) == 0);
    CHECK(otherB.compare(0, prefixA.size(), prefixA) != 0);

    //内容不需要是能解码的图片：这里只测登记与撤销
    const unsigned char bytes[] = { 1, 2, 3, 4, 5 };
    ChatImageCache& cache = ChatImageCache::Inst();
    REQUIRE(cache.PutMemoryImage(mineA, bytes, sizeof(bytes)));
    REQUIRE(cache.PutMemoryImage(peerA, bytes, sizeof(bytes)));
    REQUIRE(cache.PutMemoryImage(otherB, bytes, sizeof(bytes)));
    //不是伪路径、或没有字节的不登记
    CHECK_FALSE(cache.PutMemoryImage(L"C:\\images\\a.png", bytes, sizeof(bytes)));
    CHECK_FALSE(cache.PutMemoryImage(mineA, NULL, 0));

    std::vector<unsigned char> copy;
    REQUIRE(cache.CopyMemoryImage(peerA, copy));
    CHECK(copy.size() == sizeof(bytes));

    CHECK(cache.RemoveMemoryImages(prefixA) == 2);
    CHECK_FALSE(cache.CopyMemoryImage(mineA, copy));
    CHECK_FALSE(cache.CopyMemoryImage(peerA, copy));
    CHECK(cache.CopyMemoryImage(otherB, copy));
    CHECK(cache.RemoveMemoryImages(SecretImageMemoryPrefix(kPeerB)) == 1);
}

TEST_CASE("SCE12 本地消息号与发送失败原因", "[secretchat]")
{
    //文字与图片的编号各自从 1 起，本地消息号不能撞
    CHECK(SecretClientMsgId(kSessionA, true, 1) != SecretImageClientMsgId(kSessionA, true, 1));
    CHECK(SecretClientMsgId(kSessionA, true, 1) != SecretClientMsgId(kSessionA, false, 1));
    CHECK(SecretImageClientMsgId(kSessionA, true, 1) != SecretImageClientMsgId(kSessionA, false, 1));
    CHECK(SecretClientMsgId(kSessionA, true, 1) != SecretClientMsgId(kSessionB, true, 1));

    CHECK(SecretSendFailureText(error_code_secret_too_many) == L"发送过快，请稍后再发");
    CHECK(SecretSendFailureText(error_code_secret_too_large) == L"内容过长，没有发出");
    CHECK(SecretSendFailureText(error_code_secret_not_exist) == L"密聊已结束，没有发出");
    CHECK(SecretSendFailureText(-1) == L"发送失败");
    CHECK(SecretImageReceivingText(42) == CString(L"接收中 42%"));
    //图片大小上限：10 MB 含边界，0 字节不行
    CHECK(IsSecretImageSizeAcceptable(kSecretMaxImageBytes));
    CHECK_FALSE(IsSecretImageSizeAcceptable(kSecretMaxImageBytes + 1));
    CHECK_FALSE(IsSecretImageSizeAcceptable(0));
}

TEST_CASE("SCE13 大图浮层：只看不存的工具条、从内存加载、Esc 关闭", "[secretchat]")
{
    using balloonimageviewer::ImageViewerToolbar;
    using balloonimageviewer::ToolbarItemKind;

    //只看不存：8 个元素，没有复制、另存与识别文字；带不带识别文字都一样
    CHECK(ImageViewerToolbar::ItemCount(false, true) == 8);
    CHECK(ImageViewerToolbar::ItemCount(true, true) == 8);
    CHECK(ImageViewerToolbar::BarWidth(false, true) < ImageViewerToolbar::BarWidth(false, false));
    for (int i = 0; i < ImageViewerToolbar::ItemCount(false, true); ++i)
    {
        const ToolbarItemKind kind = ImageViewerToolbar::ItemKindAt(i, false, true);
        CHECK(kind != balloonimageviewer::kTbSaveAs);
        CHECK(kind != balloonimageviewer::kTbCopyImage);
        CHECK(kind != balloonimageviewer::kTbOcr);
    }
    //普通看图窗不受影响
    CHECK(ImageViewerToolbar::ItemCount(false, false) > 8);

    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        const std::vector<unsigned char> png = MakeTinyPng();
        REQUIRE_FALSE(png.empty());

        //画布从内存加载；空字节与无法解码的字节都不加载
        balloonimageviewer::ImageCanvas canvas(true);
        CHECK(canvas.LoadImageFromMemory(png));
        CHECK(canvas.HasImage());
        CHECK_FALSE(canvas.LoadImageFromMemory(std::vector<unsigned char>()));
        CHECK_FALSE(canvas.HasImage());
        const std::vector<unsigned char> garbage(png.size(), 0x5A);
        CHECK_FALSE(canvas.LoadImageFromMemory(garbage));

        //浮层：平时关着；打开后按 Esc 关闭；解码失败时不打开
        SecretImageOverlay overlay;
        CHECK_FALSE(overlay.IsOpen());
        REQUIRE(overlay.Open(png));
        CHECK(overlay.IsOpen());
        CHECK(overlay.OnKeyDown(VK_ESCAPE, 0));
        CHECK_FALSE(overlay.IsOpen());
        CHECK_FALSE(overlay.Open(garbage));
        CHECK_FALSE(overlay.IsOpen());
        //没有打开时关闭不出错
        overlay.Close();
        CHECK_FALSE(overlay.IsOpen());
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("SCE8 提示文字：没能建立时的本地提示与发起失败的原因", "[secretchat]")
{
    //被忽略与超时对发起方不区分（docs/密聊方案.md 4.2）
    CHECK(SecretInviteEndTip(secret_state_not_accepted, true) == L"对方暂未接受密聊邀请");
    CHECK(SecretInviteEndTip(secret_state_not_accepted, false) == L"密聊邀请已过期");
    //本人撤回不留提示，对方撤回时告诉被邀请方
    CHECK(SecretInviteEndTip(secret_state_canceled, true).empty());
    CHECK(SecretInviteEndTip(secret_state_canceled, false) == L"对方已撤回密聊邀请");
    CHECK(SecretInviteEndTip(secret_state_ended, false) == L"密聊邀请已失效");
    //已在其他设备处理：卡片直接移除，不留提示
    CHECK(SecretInviteEndTip(secret_state_handled_elsewhere, false).empty());
    //本机出错
    const int kLocalError = 100;
    CHECK(SecretInviteEndTip(kLocalError, true) == L"密聊建立失败，请重新发起");

    CHECK(SecretInviteFailureText(error_code_secret_busy) == L"你们之间已有一个密聊邀请，请先处理");
    CHECK(SecretInviteFailureText(error_code_secret_peer_unavailable) == L"对方当前不在线，或使用的客户端不支持密聊");
    CHECK(SecretInviteFailureText(error_code_secret_disabled) == L"本企业或对方所在企业没有开启密聊");
    CHECK(SecretInviteFailureText(-1) == L"暂时无法发起密聊，请稍后再试");
    //不认识的错误码带上数值，便于排查
    const int kUnknownCode = 4321;
    CHECK(SecretInviteFailureText(kUnknownCode).find(L"4321") != std::wstring::npos);
}
