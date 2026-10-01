/**
 *  ChatPinClickTests.cpp
 *
 *  聊天窗气泡旁的 Pin 图钉「点击即取消 Pin」的测试（标签 [pinclick]）。
 *
 *  被 Pin 住的消息在气泡外侧画一枚图钉（见 ChatBubble::RenderContent）。点击这枚图钉
 *  与右键菜单选「取消 Pin」走同一条路径：气泡调用 ChatMessageList::RequestPinBubble，
 *  由列表向宿主窗口投递 WM_CHAT_PIN_REQUEST（wParam=0 表示取消，lParam=堆上的 msgid），
 *  本地标记一律等服务端推送 cmd_message_pinned_notify 回来再改。
 *
 *  定位图钉的办法：把列表画到离屏位图上，找出图钉专用颜色的像素，取其外接矩形的中心
 *  作为点击点。这样验证的是「点在图钉实际画出来的位置上」，绘制几何与命中几何一旦
 *  不一致，用例就会失败。
 *
 *  覆盖：
 *    C 组 —— 点击：各种消息类型、左右两侧、群聊带发送者名时都能取消；点在别处、
 *             未 Pin、多选模式、没有 msgid 时都不投递
 *    H 组 —— 悬停：手型光标与「取消 Pin」悬停提示只在图钉上出现，移开即撤销
 *
 *  需要一个真实窗口接收投递，故建一个不显示的 DuiHost。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <vector>
#include <gdiplus.h>   // ScopedGdiplus：绘制前启动 GDI+

#include "../../../third-party/balloonui/balloonui/DuiHost.h"                      // 接收投递的宿主窗口
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"  // H 组：查悬停提示登记
#include "../App/CustomMsgDef.h"                                                    // WM_CHAT_PIN_REQUEST
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区尺寸（逻辑像素）。宽度足够让下面的短文本排成一行。
const int kListWidth  = 480;
const int kListHeight = 400;

// 图钉的颜色（与 ChatBubble.cpp 的 kPinIconColor 同值）。聊天窗里只有图钉用这个颜色，
// 用例据它在离屏位图上找出图钉的位置。
const COLORREF kPinColor = RGB(0xE6, 0x8A, 0x2E);

// 被 Pin 消息的服务器 msgid（任意正数即可）。
const long long kPinnedMsgid = 880001;

// 从图钉中心向气泡一侧水平移动的距离（像素），得到一个落在气泡正文上的点。
// 图钉与气泡边之间只隔 6 像素（ChatBubble.cpp 的 kPinIconGap），而下面用到的
// 测试文本排出来的气泡宽度远大于该距离，故这个点必然落在气泡内部。
const int kProbeIntoBubbleDx = 30;

// 测试用图片 / 文件消息的尺寸与大小。
const int       kImageW        = 120;   // 图片显示宽（像素）
const int       kImageH        = 90;    // 图片显示高（像素）
const long long kFileSizeBytes = 2048;  // 文件大小（字节）

// 悬停提示文字（与 ChatBubble.cpp 里登记的文字一致）。
const wchar_t* const kUnpinTip = L"取消 Pin";

// 测试宿主窗口的尺寸（逻辑像素）。窗口不显示，只用来接收投递。
const int kHostWidth  = 480;
const int kHostHeight = 400;

// 在作用域内启动 GDI+，离开时关闭。气泡底色、头像与图钉都用 GDI+ 绘制，测试进程不会
// 自动启动 GDI+，不启动的话这些形状一律画不出来，只剩 GDI 画的文字。
// 每个用例都要在建列表<u>之前</u>声明本对象，保证它比列表里的 GDI+ 对象活得更久。
class ScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    ScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~ScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 不显示的宿主窗口，析构时自动销毁。
class ScopedHiddenHost
{
public:
    // 创建宿主窗口（WS_POPUP、不带 WS_VISIBLE，跑测试时屏幕上不会闪出窗口）。
    ScopedHiddenHost()
    {
        RECT rc = { 0, 0, kHostWidth, kHostHeight };
        m_host.Create(NULL, rc, _T("ChatPinClickTests"), WS_POPUP);
    }

    // 销毁宿主窗口。
    ~ScopedHiddenHost()
    {
        if (m_host.IsWindow())
        {
            m_host.DestroyWindow();
        }
    }

    // 返回宿主对象（由本类持有，调用方借用）。
    DuiHost& Host()
    {
        return m_host;
    }

private:
    DuiHost m_host;   // 宿主对象，生命周期与本类相同
};

// 一次 WM_CHAT_PIN_REQUEST 投递的内容。
struct PinRequest
{
    WPARAM    m_wParam;   // 目标状态：1 钉住、0 取消
    long long m_msgid;    // 目标消息的服务器 msgid
};

// 取走投给 hWnd 的全部 WM_CHAT_PIN_REQUEST，并释放各自的堆负载（与宿主聊天窗处理完
// 负责 delete 的约定一致）。
std::vector<PinRequest> TakePinRequests(HWND hWnd)
{
    std::vector<PinRequest> out;
    MSG msg;
    while (::PeekMessageW(&msg, hWnd, WM_CHAT_PIN_REQUEST, WM_CHAT_PIN_REQUEST, PM_REMOVE))
    {
        PinRequest r;
        r.m_wParam = msg.wParam;
        long long* pMsgid = (long long*)msg.lParam;
        r.m_msgid = (pMsgid != NULL) ? *pMsgid : 0;
        delete pMsgid;
        out.push_back(r);
    }
    return out;
}

// 造一条已 Pin 的文字消息。
//   fromMe：是否本人发出（决定图钉画在气泡左侧还是右侧）。
//   msgid： 服务器 msgid；0 表示尚未分配。
ChatMessage MakePinnedText(bool fromMe, long long msgid)
{
    ChatMessage m = MakeTextMessage(fromMe, _T("这是一条被钉住的消息"));
    m.msgid  = msgid;
    m.pinned = true;
    return m;
}

// 取列表里第 idx 行气泡。
ChatBubble* BubbleAt(ChatMessageList& list, size_t idx)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(idx < kids.size());
    ChatBubble* b = dynamic_cast<ChatBubble*>(kids[idx].get());
    REQUIRE(b != NULL);
    return b;
}

// 把列表画到白底离屏位图上，求出图钉颜色像素的外接矩形。
//   list：   已排好版的消息列表。
//   outBox： 输出的外接矩形（列表坐标）；没有图钉像素时为空矩形。
// @return 图钉颜色的像素个数；画布建不出来返回 -1。
int PaintAndFindPin(ChatMessageList& list, RECT& outBox)
{
    ::SetRectEmpty(&outBox);

    HDC screenDC = ::GetDC(NULL);
    if (screenDC == NULL)
    {
        return -1;
    }
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return -1;
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
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return -1;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);

    RECT all = { 0, 0, kListWidth, kListHeight };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);

    list.OnPaint(memDC, all);

    // DIB 像素按 BGRA 存放，比对时只看 RGB 三通道。
    const unsigned int target = ((unsigned int)GetRValue(kPinColor) << 16)
                              | ((unsigned int)GetGValue(kPinColor) << 8)
                              |  (unsigned int)GetBValue(kPinColor);
    const unsigned int* px = (const unsigned int*)bits;
    int count = 0;
    for (int y = 0; y < kListHeight; ++y)
    {
        for (int x = 0; x < kListWidth; ++x)
        {
            if ((px[y * kListWidth + x] & 0x00FFFFFFu) != target)
            {
                continue;
            }
            if (count == 0)
            {
                outBox.left   = x;
                outBox.top    = y;
                outBox.right  = x + 1;
                outBox.bottom = y + 1;
            }
            else
            {
                outBox.left   = (x < outBox.left) ? x : outBox.left;
                outBox.top    = (y < outBox.top) ? y : outBox.top;
                outBox.right  = (x + 1 > outBox.right) ? x + 1 : outBox.right;
                outBox.bottom = (y + 1 > outBox.bottom) ? y + 1 : outBox.bottom;
            }
            ++count;
        }
    }

    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return count;
}

// 取矩形中心点。
POINT CenterOf(const RECT& rc)
{
    POINT pt;
    pt.x = (rc.left + rc.right) / 2;
    pt.y = (rc.top + rc.bottom) / 2;
    return pt;
}

// 建一个只含一条消息的列表、挂到宿主上并排版、绘制，返回图钉中心。
//   host：宿主窗口（借用）。
//   list：待填充的空列表（借用）。
//   msg： 要放进去的消息。
// @return 图钉中心（列表坐标）。画不出图钉时用例直接失败。
POINT SetupSingleAndLocatePin(DuiHost& host, ChatMessageList& list, const ChatMessage& msg)
{
    list.AppendMessage(msg);
    list.AttachToHost(&host);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);

    RECT box;
    const int count = PaintAndFindPin(list, box);
    REQUIRE(count > 0);
    return CenterOf(box);
}

} // anonymous namespace

// =====================================================================
// C 组：点击图钉
// =====================================================================

TEST_CASE("C1 点对方消息旁的图钉：投递取消 Pin 请求，本地标记保持不变", "[pinclick]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(false, kPinnedMsgid));
    ChatBubble* b = BubbleAt(list, 0);

    CHECK(b->OnLButtonDown(pin, MK_LBUTTON));
    b->OnLButtonUp(pin, 0);

    std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].m_wParam == 0);
    CHECK(reqs[0].m_msgid == kPinnedMsgid);
    //界面等服务端推送回来再改，点击本身不动本地标记
    CHECK(b->IsPinned());
}

TEST_CASE("C2 点本人消息旁的图钉（在气泡左侧）同样投递取消请求", "[pinclick]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(true, kPinnedMsgid));
    ChatBubble* b = BubbleAt(list, 0);

    b->OnLButtonDown(pin, MK_LBUTTON);
    b->OnLButtonUp(pin, 0);

    std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].m_wParam == 0);
    CHECK(reqs[0].m_msgid == kPinnedMsgid);
}

TEST_CASE("C3 群聊里带发送者名的对方消息，图钉随之下移后仍能点中", "[pinclick]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessage m = MakePinnedText(false, kPinnedMsgid);
    m.senderName = _T("谢知行");
    ChatMessageList list;
    const POINT pin = SetupSingleAndLocatePin(host.Host(), list, m);
    ChatBubble* b = BubbleAt(list, 0);

    b->OnLButtonDown(pin, MK_LBUTTON);
    b->OnLButtonUp(pin, 0);

    std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].m_msgid == kPinnedMsgid);
}

TEST_CASE("C4 点在气泡正文上不投递 Pin 请求，照常开始选择文字", "[pinclick]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(false, kPinnedMsgid));
    ChatBubble* b = BubbleAt(list, 0);

    //对方消息的图钉在气泡右侧，往左移一段就落在气泡正文上
    POINT onText = pin;
    onText.x -= kProbeIntoBubbleDx;

    //文字气泡按下即开始拖选，这一下由文字选择消费
    CHECK(b->OnLButtonDown(onText, MK_LBUTTON));
    b->OnLButtonUp(onText, 0);

    std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
    CHECK(reqs.empty());
}

TEST_CASE("C5 取消 Pin 之后，点原来画图钉的位置不再投递", "[pinclick]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(false, kPinnedMsgid));
    ChatBubble* b = BubbleAt(list, 0);

    //服务端推送回来、标记清掉之后重画一次，此时图钉已经不在了
    b->SetPinned(false);
    RECT box;
    CHECK(PaintAndFindPin(list, box) == 0);

    b->OnLButtonDown(pin, MK_LBUTTON);
    b->OnLButtonUp(pin, 0);

    std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
    CHECK(reqs.empty());
}

TEST_CASE("C6 多选模式下点图钉只切换本行选中态，不投递 Pin 请求", "[pinclick]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    list.AppendMessage(MakePinnedText(false, kPinnedMsgid));
    list.AttachToHost(&host.Host());
    list.SetMultiSelectMode(true);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);

    //多选模式下内容整体右移，图钉位置要在该模式下重新找
    RECT box;
    REQUIRE(PaintAndFindPin(list, box) > 0);
    const POINT pin = CenterOf(box);
    ChatBubble* b = BubbleAt(list, 0);
    REQUIRE_FALSE(b->IsSelected());

    b->OnLButtonDown(pin, MK_LBUTTON);
    b->OnLButtonUp(pin, 0);

    std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
    CHECK(reqs.empty());
    CHECK(b->IsSelected());
}

TEST_CASE("C7 尚未分配 msgid 的已 Pin 行，点图钉不投递", "[pinclick]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(false, 0));
    ChatBubble* b = BubbleAt(list, 0);

    b->OnLButtonDown(pin, MK_LBUTTON);
    b->OnLButtonUp(pin, 0);

    std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
    CHECK(reqs.empty());
}

TEST_CASE("C8 图片、文件消息旁的图钉同样可以点击取消", "[pinclick]")
{
    SECTION("图片消息")
    {
        ScopedGdiplus gdip;
        REQUIRE(gdip.Ok());
        ScopedHiddenHost host;
        REQUIRE(host.Host().IsWindow());
        ChatMessage m = MakeDownloadingImageMessage(false, kImageW, kImageH, kPinnedMsgid);
        m.pinned = true;
        ChatMessageList list;
        const POINT pin = SetupSingleAndLocatePin(host.Host(), list, m);
        ChatBubble* b = BubbleAt(list, 0);

        b->OnLButtonDown(pin, MK_LBUTTON);
        b->OnLButtonUp(pin, 0);

        std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
        REQUIRE(reqs.size() == 1);
        CHECK(reqs[0].m_wParam == 0);
        CHECK(reqs[0].m_msgid == kPinnedMsgid);
    }

    SECTION("文件消息")
    {
        ScopedGdiplus gdip;
        REQUIRE(gdip.Ok());
        ScopedHiddenHost host;
        REQUIRE(host.Host().IsWindow());
        ChatMessage m = MakeIncomingFileMessage(false, _T("需求说明.docx"), kFileSizeBytes,
                                                kPinnedMsgid, "fileid-pin", "md5-pin", 0);
        //Pin 按消息本身的 msgid 定位（见 ChatMessageList::RequestPinBubble）
        m.msgid  = kPinnedMsgid;
        m.pinned = true;
        ChatMessageList list;
        const POINT pin = SetupSingleAndLocatePin(host.Host(), list, m);
        ChatBubble* b = BubbleAt(list, 0);

        b->OnLButtonDown(pin, MK_LBUTTON);
        b->OnLButtonUp(pin, 0);

        std::vector<PinRequest> reqs = TakePinRequests(host.Host().m_hWnd);
        REQUIRE(reqs.size() == 1);
        CHECK(reqs[0].m_wParam == 0);
        CHECK(reqs[0].m_msgid == kPinnedMsgid);
    }
}

// =====================================================================
// H 组：悬停反馈
// =====================================================================

TEST_CASE("H1 光标只在可点击的图钉上显示为手型", "[pinclick][hover]")
{
    SECTION("已 Pin、有 msgid：图钉上是手型，气泡正文上不是")
    {
        ScopedGdiplus gdip;
        REQUIRE(gdip.Ok());
        ScopedHiddenHost host;
        REQUIRE(host.Host().IsWindow());
        ChatMessageList list;
        const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(false, kPinnedMsgid));
        ChatBubble* b = BubbleAt(list, 0);

        CHECK(b->OnSetCursor(pin));
        POINT onText = pin;
        onText.x -= kProbeIntoBubbleDx;
        CHECK_FALSE(b->OnSetCursor(onText));
    }

    SECTION("取消 Pin 并重画之后：原位置不再是手型")
    {
        ScopedGdiplus gdip;
        REQUIRE(gdip.Ok());
        ScopedHiddenHost host;
        REQUIRE(host.Host().IsWindow());
        ChatMessageList list;
        const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(false, kPinnedMsgid));
        ChatBubble* b = BubbleAt(list, 0);
        b->SetPinned(false);
        RECT box;
        PaintAndFindPin(list, box);

        CHECK_FALSE(b->OnSetCursor(pin));
    }

    SECTION("多选模式：图钉上不是手型")
    {
        ScopedGdiplus gdip;
        REQUIRE(gdip.Ok());
        ScopedHiddenHost host;
        REQUIRE(host.Host().IsWindow());
        ChatMessageList list;
        list.AppendMessage(MakePinnedText(false, kPinnedMsgid));
        list.AttachToHost(&host.Host());
        list.SetMultiSelectMode(true);
        RECT rc = { 0, 0, kListWidth, kListHeight };
        list.ForceLayout(rc);
        RECT box;
        REQUIRE(PaintAndFindPin(list, box) > 0);

        CHECK_FALSE(BubbleAt(list, 0)->OnSetCursor(CenterOf(box)));
    }
}

TEST_CASE("H2 悬停提示只在图钉上登记，移开图钉或离开本行即撤销", "[pinclick][hover]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    ScopedHiddenHost host;
    REQUIRE(host.Host().IsWindow());
    ChatMessageList list;
    const POINT pin = SetupSingleAndLocatePin(host.Host(), list, MakePinnedText(false, kPinnedMsgid));
    ChatBubble* b = BubbleAt(list, 0);
    DuiToolTipMgr& tips = DuiToolTipMgr::Inst();
    POINT onText = pin;
    onText.x -= kProbeIntoBubbleDx;

    //刚进入本行、还没到图钉上：不能有登记，否则宿主在鼠标进入本行任意位置时都会弹提示
    b->OnMouseMove(onText, 0);
    CHECK(tips.GetText(b).IsEmpty());

    //移到图钉上：登记「取消 Pin」
    b->OnMouseMove(pin, 0);
    CHECK(tips.GetText(b) == CString(kUnpinTip));

    //从图钉移回正文：撤销登记
    b->OnMouseMove(onText, 0);
    CHECK(tips.GetText(b).IsEmpty());

    //再移到图钉上后直接离开本行：同样撤销登记
    b->OnMouseMove(pin, 0);
    REQUIRE(tips.GetText(b) == CString(kUnpinTip));
    b->OnMouseLeave();
    CHECK(tips.GetText(b).IsEmpty());
}
