/**
 *  ChatBubbleSelectionRepaintTests.cpp
 *
 *  文字气泡选区变化时的局部重画（标签 [chatbubble][selrepaint]）：
 *
 *    SL1 像素一致：一条带链接的长消息，选区依次「向下扩、往回缩、全选、取消」，每次只在渲染缓存里
 *        重画变化的那几行，画出来与整条重画的结果逐像素相同。
 *    SL2 工作量：选区变化一次，重画时不重新排版；画的文字段数有上限，且两三万字与九千字的消息
 *        相差无几（不随总字数增长）。
 *    SL3 宽度变化、多选状态变化、内容变化时仍整条重画（超长气泡分段缓存时为整段重画）；宽度与多选状态
 *        变化时会重新排版。2026-10-04 起正文的排版在测量阶段完成、绘制时直接复用，所以「重新排版」
 *        从重排（ForceLayout）开始计数，「整条重画」看 ChatBubble::RenderPassCount。
 *
 *  SL1、SL2 在两种画布上各跑一遍：DIB（直接读写像素的位图）与 DDB（与屏幕兼容的位图，真实客户端
 *  的消息区画在这种位图上）。GDI+ 在 DDB 上绘制时会改动设备上下文的裁剪区，只在 DIB 上测会漏掉
 *  "局部重画时裁剪区失效"这类问题（2026-10-01 实测）。
 *
 *  背景：原先选区每变一次整条气泡都要重新排版、从头画进缓存，两三万字的消息拖选每步约 250 毫秒
 *  （CPF12，Debug 版），拖起来明显不跟手。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string.h>
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatInlineText.h"   // InlineLayoutCallCount / InlinePlacedDrawCount

using namespace balloonchatwnd;

namespace {

// 消息区宽度（像素），取单聊窗默认大小下消息区的量级
const int kViewW = 620;

// SL1 的画布高度（像素）：足以放下整条测试消息，比较的是整条气泡而不只是可视的一截
const int kTallViewH = 2400;

// SL2 / SL3 的可视高度（像素），与单聊窗消息区同一量级
const int kViewH = 420;

// SL1 测试消息的行数，以及每行的内容；中间夹一行链接，验证链接那一层也是局部重画
const int kSl1Lines = 40;
const wchar_t* const kSl1Line = L"Flamingo.exe!net::FileTransfer::WorkerProc() Line 476    C++    Symbols loaded.";
const wchar_t* const kSl1LinkLine = L"详情见 https://example.com/s/Ucc3wkw5zAHT1oEiyoijNQ 这一页";

// SL2 两档消息的字数（约数）
const int kSl2SmallChars = 9000;
const int kSl2LargeChars = 27000;

// SL2 选区变化一次时允许画的文字段数上限：拖动一步只涉及两三行，每行一二十段，留足余量
const unsigned long kSl2MaxPlacedDraws = 300;

// 在作用域内启动 GDI+，离开时关闭。气泡的形状都用 GDI+ 绘制。
class GdiplusScope
{
public:
    GdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~GdiplusScope()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 一块画布，析构时释放。dib 为 true 时是 32 位 DIB，为 false 时是与屏幕兼容的位图（DDB，
// 真实客户端的消息区画在这种位图上）。两种都用 GetDIBits 把像素读出来比较。
class PixelCanvas
{
public:
    PixelCanvas(int w, int h, bool dib)
        : m_dc(NULL)
        , m_bmp(NULL)
        , m_old(NULL)
        , m_w(w)
        , m_h(h)
    {
        HDC screen = ::GetDC(NULL);
        m_dc = ::CreateCompatibleDC(screen);
        if (dib)
        {
            BITMAPINFO bi;
            ::ZeroMemory(&bi, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = NULL;
            m_bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        }
        else
        {
            m_bmp = ::CreateCompatibleBitmap(screen, w, h);
        }
        ::ReleaseDC(NULL, screen);
        if (m_dc != NULL && m_bmp != NULL)
        {
            m_old = ::SelectObject(m_dc, m_bmp);
        }
    }

    ~PixelCanvas()
    {
        if (m_dc != NULL && m_old != NULL)
        {
            ::SelectObject(m_dc, m_old);
        }
        if (m_bmp != NULL)
        {
            ::DeleteObject(m_bmp);
        }
        if (m_dc != NULL)
        {
            ::DeleteDC(m_dc);
        }
    }

    HDC Dc() const
    {
        return (m_bmp != NULL && m_old != NULL) ? m_dc : NULL;
    }

    // 把像素读成 32 位自底向上的数组（每像素一个 DWORD）。
    std::vector<DWORD> ReadPixels() const
    {
        std::vector<DWORD> px((size_t)m_w * (size_t)m_h, 0);
        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = m_w;
        bi.bmiHeader.biHeight = m_h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        ::GdiFlush();
        //GetDIBits 要求位图未选入任何 DC，读之前先换出，读完换回
        ::SelectObject(m_dc, m_old);
        ::GetDIBits(m_dc, m_bmp, 0, (UINT)m_h, &px[0], &bi, DIB_RGB_COLORS);
        ::SelectObject(m_dc, m_bmp);
        return px;
    }

    // 两块同尺寸画布的像素是否逐个相同。
    bool SamePixels(const PixelCanvas& other) const
    {
        if (m_w != other.m_w || m_h != other.m_h)
        {
            return false;
        }
        return ReadPixels() == other.ReadPixels();
    }

private:
    HDC     m_dc;     // 画布内存 DC
    HBITMAP m_bmp;    // 位图（DIB 或 DDB）
    HGDIOBJ m_old;    // 选入位图之前的原位图
    int     m_w;      // 画布宽（像素）
    int     m_h;      // 画布高（像素）
};

// 画布种类的名字，INFO 里用。
const char* CanvasKindName(bool dib)
{
    return dib ? "DIB" : "DDB";
}

// SL1 的测试消息：kSl1Lines 行，正中一行带链接。
CString BuildSl1Text()
{
    std::wstring text;
    for (int i = 0; i < kSl1Lines; ++i)
    {
        if (i > 0)
        {
            text += L"\r\n";
        }
        text += (i == kSl1Lines / 2) ? kSl1LinkLine : kSl1Line;
    }
    return CString(text.c_str());
}

// 约 chars 字的调用栈文字（与 CPF12 同一格式）。
CString BuildStackText(int chars)
{
    std::wstring text;
    while ((int)text.size() < chars)
    {
        text += kSl1Line;
        text += L"\r\n";
    }
    return CString(text.c_str());
}

// 消息列表最后一行的气泡（前面可能有时间分隔行）。
ChatBubble* LastBubble(ChatMessageList& list)
{
    return dynamic_cast<ChatBubble*>(list.Children().back().get());
}

// 气泡矩形内、距左上角 (dx, dy) 的点（宿主坐标）。
POINT PointInBubble(ChatBubble* b, int dx, int dy)
{
    const RECT rc = b->GetRect();
    POINT pt = { rc.left + dx, rc.top + dy };
    return pt;
}

// 一个装着一条长消息、已排好版并完整画过一遍的消息列表。
struct ListWithMessage
{
    ChatMessageList m_list;   // 消息列表
    RECT            m_view;   // 列表的可视矩形

    ListWithMessage(const CString& text, int viewH)
    {
        m_view.left = 0;
        m_view.top = 0;
        m_view.right = kViewW;
        m_view.bottom = viewH;
        m_list.ForceLayout(m_view);
        m_list.AppendMessage(MakeTextMessage(false, text));
        m_list.ForceLayout(m_view);
    }
};

} // anonymous namespace

namespace {

// SL1 的测试体：在 dib 指定的那种画布上做一遍。
void RunSl1(bool dib)
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    const CString text = BuildSl1Text();

    //A：走局部重画；B：每次选区变化后强制整条重画，作为基准
    ListWithMessage a(text, kTallViewH);
    ListWithMessage b(text, kTallViewH);
    ChatBubble* ba = LastBubble(a.m_list);
    ChatBubble* bb = LastBubble(b.m_list);
    REQUIRE(ba != NULL);
    REQUIRE(bb != NULL);
    //前提：整条气泡都在画布里
    REQUIRE(ba->GetRect().bottom <= kTallViewH);

    INFO("canvas " << CanvasKindName(dib));
    PixelCanvas ca(kViewW, kTallViewH, dib);
    PixelCanvas cb(kViewW, kTallViewH, dib);
    REQUIRE(ca.Dc() != NULL);
    REQUIRE(cb.Dc() != NULL);
    a.m_list.OnPaint(ca.Dc(), a.m_view);
    b.m_list.OnPaint(cb.Dc(), b.m_view);
    REQUIRE(ca.SamePixels(cb));

    //对两个气泡做同样的操作，A 正常重画，B 强制整条重画，再比较
    struct Step
    {
        const char* m_name;   // 这一步做了什么
        int         m_kind;   // 0 按下，1 拖动到，2 松开，3 全选，4 取消选择
        int         m_dy;     // 指针在气泡内的纵向位置（像素），按下 / 拖动 / 松开时有效
    };
    //气泡内的横向落点（像素），落在正文上即可，取值无特殊含义
    const int kDx = 120;
    const Step steps[] =
    {
        { "press",          0, 60 },
        { "drag down",      1, 400 },
        { "drag back up",   1, 220 },
        { "drag past link", 1, 700 },
        { "release",        2, 700 },
        { "select all",     3, 0 },
        { "clear",          4, 0 },
    };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
    {
        INFO("step " << i << ": " << steps[i].m_name);
        ChatBubble* pair[] = { ba, bb };
        for (int k = 0; k < 2; ++k)
        {
            ChatBubble* bub = pair[k];
            const POINT pt = PointInBubble(bub, kDx, steps[i].m_dy);
            switch (steps[i].m_kind)
            {
            //按下：开始拖选
            case 0:
                bub->OnLButtonDown(pt, MK_LBUTTON);
                break;
            //拖动：移动选区的另一端
            case 1:
                bub->OnMouseMove(pt, MK_LBUTTON);
                break;
            //松开：结束拖选
            case 2:
                bub->OnLButtonUp(pt, 0);
                break;
            //全选
            case 3:
                bub->SelectAllText();
                break;
            //取消选择
            default:
                bub->ClearTextSelection();
                break;
            }
        }
        bb->Invalidate();   //基准：整张渲染缓存置脏，下次绘制整条重画
        a.m_list.OnPaint(ca.Dc(), a.m_view);
        b.m_list.OnPaint(cb.Dc(), b.m_view);
        CHECK(ca.SamePixels(cb));
    }
}

} // anonymous namespace

TEST_CASE("SL1 选区变化时只重画变化的几行，结果与整条重画逐像素相同", "[chatbubble][selrepaint]")
{
    RunSl1(true);
    RunSl1(false);
}

namespace {

// SL2 的测试体：在 dib 指定的那种画布上做一遍。
void RunSl2(bool dib)
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    INFO("canvas " << CanvasKindName(dib));
    const int lengths[] = { kSl2SmallChars, kSl2LargeChars };
    unsigned long draws[2] = { 0, 0 };
    for (int li = 0; li < 2; ++li)
    {
        INFO("chars " << lengths[li]);
        ListWithMessage m(BuildStackText(lengths[li]), kViewH);
        PixelCanvas canvas(kViewW, kViewH, dib);
        REQUIRE(canvas.Dc() != NULL);
        m.m_list.OnPaint(canvas.Dc(), m.m_view);   //首次完整绘制，建好渲染缓存
        ChatBubble* bub = LastBubble(m.m_list);
        REQUIRE(bub != NULL);

        //在可视区里按下、往下拖一点：只涉及两三行（气泡滚在底部，矩形上沿在可视区之上）
        const RECT rc = bub->GetRect();
        const int kDx = 120;            //气泡内的横向落点（像素）
        const int kPressY = 100;        //按下处距可视区上沿（像素）
        const int kDragY = 140;         //拖到处距可视区上沿（像素）
        POINT press = { rc.left + kDx, kPressY };
        POINT drag = { rc.left + kDx, kDragY };
        bub->OnLButtonDown(press, MK_LBUTTON);
        m.m_list.OnPaint(canvas.Dc(), m.m_view);

        ResetInlineProbeCounters();
        bub->OnMouseMove(drag, MK_LBUTTON);
        m.m_list.OnPaint(canvas.Dc(), m.m_view);
        CHECK(InlineLayoutCallCount() == 0);
        CHECK(InlinePlacedDrawCount() > 0);
        CHECK(InlinePlacedDrawCount() <= kSl2MaxPlacedDraws);
        draws[li] = InlinePlacedDrawCount();
        bub->OnLButtonUp(drag, 0);
    }
    INFO("draws " << draws[0] << " vs " << draws[1]);
    //两档的绘制量相近：大的一档不超过小的一档的两倍（整条重画时两者相差三倍）
    CHECK(draws[1] <= draws[0] * 2);
}

} // anonymous namespace

TEST_CASE("SL2 选区变化一次不重新排版，画的文字段数不随消息总字数增长", "[chatbubble][selrepaint]")
{
    RunSl2(true);
    RunSl2(false);
}

TEST_CASE("SL3 宽度、多选状态、内容变化时仍整条重画", "[chatbubble][selrepaint]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    ListWithMessage m(BuildStackText(kSl2SmallChars), kViewH);
    PixelCanvas canvas(kViewW, kViewH, false);
    REQUIRE(canvas.Dc() != NULL);
    m.m_list.OnPaint(canvas.Dc(), m.m_view);
    ChatBubble* bub = LastBubble(m.m_list);
    REQUIRE(bub != NULL);

    //先做一次局部重画（选区变化），确认此时确实不排版，也不整条重画
    bub->SelectAllText();
    ResetInlineProbeCounters();
    ChatBubble::ResetRenderPassCount();
    m.m_list.OnPaint(canvas.Dc(), m.m_view);
    CHECK(InlineLayoutCallCount() == 0);
    CHECK(ChatBubble::RenderPassCount() == 0);

    SECTION("宽度变化")
    {
        //窄一些的消息区（像素），取值无特殊含义
        const int kNarrowW = 500;
        const RECT narrow = { 0, 0, kNarrowW, kViewH };
        //排版发生在重排时的测量阶段，所以从重排之前开始计数
        ResetInlineProbeCounters();
        ChatBubble::ResetRenderPassCount();
        m.m_list.ForceLayout(narrow);
        m.m_list.OnPaint(canvas.Dc(), narrow);
        CHECK(InlineLayoutCallCount() > 0);
        CHECK(ChatBubble::RenderPassCount() > 0);
    }
    SECTION("多选状态变化")
    {
        ResetInlineProbeCounters();
        ChatBubble::ResetRenderPassCount();
        m.m_list.SetMultiSelectMode(true);
        m.m_list.ForceLayout(m.m_view);
        m.m_list.OnPaint(canvas.Dc(), m.m_view);
        CHECK(InlineLayoutCallCount() > 0);
        CHECK(ChatBubble::RenderPassCount() > 0);
    }
    SECTION("内容变化")
    {
        //Invalidate 只把渲染缓存置脏，正文本身没变，可以复用测量阶段的排版，只要求整条重画
        bub->Invalidate();
        ChatBubble::ResetRenderPassCount();
        m.m_list.OnPaint(canvas.Dc(), m.m_view);
        CHECK(ChatBubble::RenderPassCount() > 0);
    }
}
