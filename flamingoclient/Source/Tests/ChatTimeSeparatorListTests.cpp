/**
 *  消息列表一侧的「时间分隔行」测试（标签 [chattime]），ChatTimeSeparatorListTests.cpp
 *
 *  背景（docs/聊天消息时间显示方案.md 第 7 节，2026-10-14）：本人发出的消息在本地先回显，回显时还没有服务端时间
 *  （时间戳为 0），要等回执回填。原来的消息列表对时间戳为 0 的消息不插分隔行、也不更新基准，于是隔夜之后本人发的
 *  第一条消息前面没有时间行；对方随后的回复与昨晚比较，时间行插在本人消息之后。另外分隔行的文字在插入时算好后不再
 *  更新，聊天窗开着过了零点，昨晚的「21:30」不会变成「昨天 21:30」。
 *
 *  覆盖：
 *    CTL1 昨晚对方一条 → 今早本人回显（时间戳 0）：本人那条之前有且只有一行今天的时间；
 *    CTL2 回执回填服务端时间后，3 分钟后对方回复不再插行（基准已换成服务端时间）；
 *    CTL3 本人连发三条回显、相隔不到 5 分钟：只在第一条前插一行；
 *    CTL4 跨午夜刷新：昨晚插入的「21:30」在「现在」换到今天之后绘制一次，变为「昨天 21:30」；日期没变时绘制不改文字；
 *    CTL5 本人发送失败的消息在本窗口删除（右键删除、多选删除）后，它前面的时间行一并移除；随后重发的消息前面重新插一行；
 *    CTL6 本人发出的密聊邀请卡片没有客户端消息 ID，不按回显处理，前面照旧不插时间行。
 *
 *  「现在」经 ChatMessageList::SetNowProviderForTest 固定；时间戳用 mktime 按本地时间构造，结果不随机器时区变化。
 *  列表挂在一个不显示的 DuiHost 上排版、画进内存位图（照 ChatPinClickTests.cpp 的做法）。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>
#include <time.h>
#include <gdiplus.h>   // ScopedCtlGdiplus：绘制前启动 GDI+

#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // 列表挂载的宿主窗口
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatSecretMode.h"   // CTL6：ApplySecretInviteCard 造密聊邀请卡片

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区尺寸（逻辑像素）
const int kCtlListWidth = 600;
const int kCtlListHeight = 600;

// 对方消息的服务器 msgid 起点（任意正数）
const long long kCtlPeerMsgidBase = 990000;
// 本人消息回执回填的服务器 msgid（任意正数）
const long long kCtlSelfMsgid = 990100;

// 32 位 DIB 的位深
const WORD kCtlBitsPerPixel = 32;

// CTL6 密聊邀请卡片的会话号（任意非零值）
const uint64_t kCtlSecretSession = 8001;
// CTL6 密聊邀请卡片显示的剩余秒数（任意正数）
const uint32_t kCtlSecretRemainSeconds = 60;
// CTL6 对方的防截屏能力取值（与 SecretChatHostTests.cpp 的 kProtectExclude 相同：完全排除于截屏之外）
const int kCtlSecretProtect = 2;

// 用例里固定的「现在」（Unix 毫秒），由 CtlFakeNow 返回
long long g_ctlNowMs = 0;

// 替换消息列表取时函数用：返回 g_ctlNowMs
long long CtlFakeNow()
{
    return g_ctlNowMs;
}

// 按本地时间构造一个 Unix 毫秒时间戳
long long CtlLocalMs(int year, int mon, int day, int hour, int minute)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_isdst = -1;
    const time_t secs = mktime(&t);
    return (long long)secs * 1000LL;
}

// 在作用域内把消息列表的「现在」固定为 g_ctlNowMs，离开时恢复读系统时钟
class ScopedCtlClock
{
public:
    // nowMs：初始的「现在」（Unix 毫秒）
    explicit ScopedCtlClock(long long nowMs)
    {
        g_ctlNowMs = nowMs;
        ChatMessageList::SetNowProviderForTest(&CtlFakeNow);
    }

    ~ScopedCtlClock()
    {
        ChatMessageList::SetNowProviderForTest(NULL);
    }

    // 改变「现在」
    void Set(long long nowMs)
    {
        g_ctlNowMs = nowMs;
    }
};

// 在作用域内启动 GDI+（气泡底色、头像用 GDI+ 绘制），须在建列表之前声明
class ScopedCtlGdiplus
{
public:
    ScopedCtlGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~ScopedCtlGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    // GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌
    bool      m_ok;      // 是否启动成功
};

// 不显示的宿主窗口，析构时销毁
class ScopedCtlHost
{
public:
    ScopedCtlHost()
    {
        RECT rc = { 0, 0, kCtlListWidth, kCtlListHeight };
        m_host.Create(NULL, rc, _T("ChatTimeSeparatorListTests"), WS_POPUP);
    }

    ~ScopedCtlHost()
    {
        if (m_host.IsWindow())
            m_host.DestroyWindow();
    }

    // 宿主对象（由本类持有，调用方借用）
    DuiHost& Host()
    {
        return m_host;
    }

private:
    DuiHost m_host;   // 宿主对象
};

// 对方发来的一条文字消息（已有服务器 msgid 与时间）
ChatMessage CtlPeerText(const CString& text, long long msgid, long long timestampMs)
{
    ChatMessage m = MakeTextMessage(false, text);
    m.msgid = msgid;
    m.timestampMs = timestampMs;
    return m;
}

// 本人刚发出、还没有回执的回显（msgid 与时间戳为 0）
ChatMessage CtlSelfEcho(const CString& text, const std::string& clientMsgId)
{
    ChatMessage m = MakeTextMessage(true, text);
    m.clientMsgId = clientMsgId;
    return m;
}

// 按客户端消息 ID 找到本人回显的那一行；找不到时用例失败
ChatBubble* CtlFindEcho(ChatMessageList& list, const std::string& clientMsgId)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().clientMsgId == clientMsgId)
            return b;
    }
    FAIL("echo not found: " << clientMsgId);
    return NULL;
}

// 列表里一行的摘要：是否为时间分隔行、文字
struct CtlRow
{
    bool    m_isSeparator;   // 是否为时间分隔行（kMsgTime 且没有 msgid）
    CString m_text;          // 该行的文字
};

// 依次取出列表里每一行的摘要
std::vector<CtlRow> CtlRows(ChatMessageList& list)
{
    std::vector<CtlRow> rows;
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b == NULL)
            continue;
        CtlRow row;
        row.m_isSeparator = (b->GetMessage().kind == kMsgTime && b->Msgid() <= 0);
        row.m_text = b->GetMessage().text;
        rows.push_back(row);
    }
    return rows;
}

// 时间分隔行的个数
size_t CtlSeparatorCount(const std::vector<CtlRow>& rows)
{
    size_t n = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_isSeparator)
            ++n;
    }
    return n;
}

// 挂到宿主并排版
void CtlAttach(DuiHost& host, ChatMessageList& list)
{
    list.AttachToHost(&host);
    RECT rc = { 0, 0, kCtlListWidth, kCtlListHeight };
    list.ForceLayout(rc);
}

// 把列表画进一张内存位图一次（触发绘制时的处理）
void CtlPaintOnce(ChatMessageList& list)
{
    HDC screenDC = ::GetDC(NULL);
    REQUIRE(screenDC != NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    REQUIRE(memDC != NULL);
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kCtlListWidth;
    bi.bmiHeader.biHeight = -kCtlListHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kCtlBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bmp != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    RECT all = { 0, 0, kCtlListWidth, kCtlListHeight };
    list.OnPaint(memDC, all);
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
}

} // namespace

TEST_CASE("CTL1 隔夜后本人回显的消息前面有一行今天的时间", "[chattime]")
{
    ScopedCtlGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedCtlHost host;
    REQUIRE(host.Host().IsWindow());
    ScopedCtlClock clock(CtlLocalMs(2026, 10, 14, 9, 5));

    ChatMessageList list;
    list.AppendMessage(CtlPeerText(_T("今天先到这里"), kCtlPeerMsgidBase, CtlLocalMs(2026, 10, 13, 21, 30)));
    list.AppendMessage(CtlSelfEcho(_T("早，昨天说的方案我看完了"), "ctl1-echo"));
    CtlAttach(host.Host(), list);

    const std::vector<CtlRow> rows = CtlRows(list);
    //昨晚那条前面一行、本人回显前面一行，共两行
    REQUIRE(CtlSeparatorCount(rows) == 2);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].m_isSeparator);
    CHECK(rows[0].m_text == CString(_T("昨天 21:30")));
    CHECK_FALSE(rows[1].m_isSeparator);
    CHECK(rows[2].m_isSeparator);
    CHECK(rows[2].m_text == CString(_T("09:05")));
    CHECK_FALSE(rows[3].m_isSeparator);
}

TEST_CASE("CTL2 回执回填后基准换成服务端时间，紧接着的回复不再插行", "[chattime]")
{
    ScopedCtlGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedCtlHost host;
    REQUIRE(host.Host().IsWindow());
    //本机时钟比服务端慢 5 分钟：回显时本机是 09:00，回执回填的服务端时间是 09:05。只有回填时把基准换成服务端时间，
    //09:08 的回复才不满 5 分钟；基准停在本机的 09:00 会多插一行，停在昨晚 21:30 也会多插一行
    ScopedCtlClock clock(CtlLocalMs(2026, 10, 14, 9, 0));

    ChatMessageList list;
    list.AppendMessage(CtlPeerText(_T("今天先到这里"), kCtlPeerMsgidBase, CtlLocalMs(2026, 10, 13, 21, 30)));
    list.AppendMessage(CtlSelfEcho(_T("早，昨天说的方案我看完了"), "ctl2-echo"));
    CtlAttach(host.Host(), list);
    REQUIRE(list.BackfillSentMessage("ctl2-echo", kCtlSelfMsgid, CtlLocalMs(2026, 10, 14, 9, 5)));

    //对方 09:08 回复：与本人那条的服务端时间 09:05 比较，不满 5 分钟、同一天，不插
    clock.Set(CtlLocalMs(2026, 10, 14, 9, 8));
    list.AppendMessage(CtlPeerText(_T("好，十点开会对一下"), kCtlPeerMsgidBase + 1, CtlLocalMs(2026, 10, 14, 9, 8)));

    const std::vector<CtlRow> rows = CtlRows(list);
    CHECK(CtlSeparatorCount(rows) == 2);
    REQUIRE(rows.size() == 5);
    //依次是：昨晚的时间行、昨晚的消息、今早的时间行、本人那条、对方回复（回复前没有时间行）
    CHECK(rows[2].m_isSeparator);
    CHECK_FALSE(rows[3].m_isSeparator);
    CHECK_FALSE(rows[4].m_isSeparator);
}

TEST_CASE("CTL3 本人连发三条回显只在第一条前插一行", "[chattime]")
{
    ScopedCtlGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedCtlHost host;
    REQUIRE(host.Host().IsWindow());
    ScopedCtlClock clock(CtlLocalMs(2026, 10, 14, 9, 5));

    ChatMessageList list;
    list.AppendMessage(CtlPeerText(_T("今天先到这里"), kCtlPeerMsgidBase, CtlLocalMs(2026, 10, 13, 21, 30)));
    list.AppendMessage(CtlSelfEcho(_T("早"), "ctl3-echo-1"));
    clock.Set(CtlLocalMs(2026, 10, 14, 9, 6));
    list.AppendMessage(CtlSelfEcho(_T("昨天说的方案我看完了"), "ctl3-echo-2"));
    clock.Set(CtlLocalMs(2026, 10, 14, 9, 7));
    list.AppendMessage(CtlSelfEcho(_T("有两处想再商量一下"), "ctl3-echo-3"));
    CtlAttach(host.Host(), list);

    const std::vector<CtlRow> rows = CtlRows(list);
    CHECK(CtlSeparatorCount(rows) == 2);
    REQUIRE(rows.size() == 6);
    CHECK(rows[2].m_isSeparator);
    CHECK(rows[2].m_text == CString(_T("09:05")));
}

TEST_CASE("CTL4 跨过零点后绘制时刷新分隔行的文字", "[chattime]")
{
    ScopedCtlGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedCtlHost host;
    REQUIRE(host.Host().IsWindow());
    ScopedCtlClock clock(CtlLocalMs(2026, 10, 13, 21, 35));

    ChatMessageList list;
    list.AppendMessage(CtlPeerText(_T("今天先到这里"), kCtlPeerMsgidBase, CtlLocalMs(2026, 10, 13, 21, 30)));
    CtlAttach(host.Host(), list);

    //当天绘制：文字不变
    CtlPaintOnce(list);
    std::vector<CtlRow> rows = CtlRows(list);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_text == CString(_T("21:30")));

    //过了零点再绘制：变为「昨天 21:30」
    clock.Set(CtlLocalMs(2026, 10, 14, 9, 0));
    CtlPaintOnce(list);
    rows = CtlRows(list);
    CHECK(rows[0].m_text == CString(_T("昨天 21:30")));

    //同一天再绘制一次：保持不变
    clock.Set(CtlLocalMs(2026, 10, 14, 10, 0));
    CtlPaintOnce(list);
    rows = CtlRows(list);
    CHECK(rows[0].m_text == CString(_T("昨天 21:30")));
}

TEST_CASE("CTL5 删除发送失败的消息后时间行一并移除，重发时重新插行", "[chattime]")
{
    ScopedCtlGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedCtlHost host;
    REQUIRE(host.Host().IsWindow());
    ScopedCtlClock clock(CtlLocalMs(2026, 10, 14, 9, 5));

    ChatMessageList list;
    list.AppendMessage(CtlPeerText(_T("今天先到这里"), kCtlPeerMsgidBase, CtlLocalMs(2026, 10, 13, 21, 30)));
    list.AppendMessage(CtlSelfEcho(_T("早，昨天说的方案我看完了"), "ctl5-echo"));
    CtlAttach(host.Host(), list);
    REQUIRE(list.MarkSendFailed("ctl5-echo", _T("网络异常")));
    ChatBubble* failed = CtlFindEcho(list, "ctl5-echo");

    SECTION("右键删除")
    {
        //没有服务器 msgid 的行就地移除，返回 true
        REQUIRE(list.RequestDeleteBubble(failed));
    }
    SECTION("多选删除")
    {
        list.EnterMultiSelectWith(failed);
        //没有服务器 msgid 的行就地移除，不需要服务端删除
        CHECK(list.DeleteSelected().empty());
    }

    //只剩昨晚的时间行与昨晚那条，「09:05」那一行随之移除
    std::vector<CtlRow> rows = CtlRows(list);
    REQUIRE(rows.size() == 2);
    CHECK(CtlSeparatorCount(rows) == 1);
    CHECK(rows[0].m_text == CString(_T("昨天 21:30")));

    //09:06 重发：与昨晚那条不在同一天，前面重新插一行「09:06」
    clock.Set(CtlLocalMs(2026, 10, 14, 9, 6));
    list.AppendMessage(CtlSelfEcho(_T("早，昨天说的方案我看完了"), "ctl5-resend"));
    rows = CtlRows(list);
    REQUIRE(rows.size() == 4);
    CHECK(rows[2].m_isSeparator);
    CHECK(rows[2].m_text == CString(_T("09:06")));
    CHECK_FALSE(rows[3].m_isSeparator);
}

TEST_CASE("CTL6 本人发出的密聊邀请卡片前面不插时间行", "[chattime]")
{
    ScopedCtlGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedCtlHost host;
    REQUIRE(host.Host().IsWindow());
    ScopedCtlClock clock(CtlLocalMs(2026, 10, 14, 9, 5));

    ChatMessageList list;
    list.AppendMessage(CtlPeerText(_T("今天先到这里"), kCtlPeerMsgidBase, CtlLocalMs(2026, 10, 13, 21, 30)));
    ChatMessage card;
    ApplySecretInviteCard(card, true, CString(_T("张三")), kCtlSecretSession, kCtlSecretRemainSeconds, kCtlSecretProtect);
    REQUIRE(card.fromMe);
    REQUIRE(card.clientMsgId.empty());
    REQUIRE(card.timestampMs == 0);
    list.AppendMessage(card);
    CtlAttach(host.Host(), list);

    //卡片会在邀请结束时单独移除，前面若插了时间行就会留在原处，所以照旧不插：只有昨晚那一行
    const std::vector<CtlRow> rows = CtlRows(list);
    REQUIRE(rows.size() == 3);
    CHECK(CtlSeparatorCount(rows) == 1);
    CHECK_FALSE(rows[2].m_isSeparator);
}
