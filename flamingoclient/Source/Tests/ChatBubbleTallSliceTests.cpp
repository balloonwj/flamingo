/**
 *  ChatBubbleTallSliceTests.cpp
 *
 *  超长文字气泡只缓存可见部分（标签 [chatbubble][tallslice]，2026-10-04）：
 *
 *    A1 超长气泡第一次绘制后，渲染缓存只覆盖可见区附近的一段，高度不超过分段阈值，远小于整条气泡。
 *    A2 在顶部、中部、底部三个位置，分段绘制出来的像素与整条缓存绘制的结果逐像素相同；
 *       依次从底部滚到中部、顶部（缓存换段）也一样。DIB 与 DDB 两种画布各跑一遍。
 *    A3 在缓存段内移动可见区不重画缓存；移到段外只重画一次。
 *    A4 不超过阈值的普通气泡仍整条缓存，行为不变。
 *    A5 分段模式下选中文字（全选、拖选、取消）时的局部重画，结果与整条重画逐像素相同。
 *    A6 第一次绘制画的文字段数只与可见区有关：两万字与五千字的消息相差不多。
 *
 *  背景：会话里有五千到一万字的文字消息时，打开聊天窗后的第一次绘制要把整条气泡（上万像素高）
 *  画进缓存位图，用户环境实测约 300 毫秒，屏幕上却只露出其中几百像素。
 *
 *  测试直接调用 ChatBubble::OnPaint(hdc, 区域)，模拟消息列表在滚动到某个位置时只请气泡画可见的那一段；
 *  用 SetViewportOrgEx 把那一段映射到一块与可视区同高的小画布上，不必为整条气泡分配上万像素高的画布。
 *  基准一侧把分段阈值临时调到极大（等于关闭分段），整条缓存后画同一段。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <limits.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatInlineText.h"   // InlinePlacedDrawCount

using namespace balloonchatwnd;

namespace {

// 消息区宽度（像素），取单聊窗默认大小下消息区的量级
const int kViewW = 620;

// 可视区高度（像素），与单聊窗消息区同一量级
const int kViewH = 420;

// A1~A3、A5 用的超长消息字数（约数），与用户会话里最长的那条同一量级
const int kLongChars = 10800;

// A6 两档消息的字数（约数）
const int kA6SmallChars = 5400;
const int kA6LargeChars = 21600;

// A4 普通消息的字数（约数），排出来的气泡远低于分段阈值
const int kShortChars = 120;

// A3 在缓存段内移动可见区的距离（像素），远小于缓存段在可见区上下各留的余量
const int kA3SmallShift = 100;

// 关闭分段时使用的阈值：任何气泡都不会超过它
const int kNoSlice = INT_MAX;

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

// 在作用域内把分段阈值设为指定值，离开时恢复默认值，保证不影响其他用例
class SliceThresholdScope
{
public:
    explicit SliceThresholdScope(int px)
        : m_saved(ChatBubble::TallSliceThreshold())
    {
        ChatBubble::SetTallSliceThreshold(px);
    }

    ~SliceThresholdScope()
    {
        ChatBubble::SetTallSliceThreshold(m_saved);
    }

private:
    int m_saved;   // 进入作用域之前的阈值
};

// 一块与可视区同样大小的画布，析构时释放。dib 为 true 时是 32 位 DIB，为 false 时是与屏幕兼容的
// 位图（DDB，真实客户端的消息区画在这种位图上）。两种都用 GetDIBits 把像素读出来比较。
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

// 拼一段约 chars 字、中英文混排、每一小节带一个链接、整段没有换行的文字（与 CPF13 同一形状）。
CString BuildLongText(int chars)
{
    std::wstring text;
    wchar_t piece[160];
    for (int i = 1; (int)text.size() < chars; ++i)
    {
        swprintf_s(piece, L"[第 %d 课　远程控制系统开发要点之%d：连接、传输与界面] (https://mp.weixin.qq.com/s/Ucc3wkw5zAHT%06dQ)",
                   i, i, i);
        text += piece;
    }
    return CString(text.c_str());
}

// 一个装着一条消息、已按可视区排好版的消息列表。气泡的矩形以宿主坐标给出，可能大部分在可视区之外，
// 绘制时只取其中一段。
struct ListWithMessage
{
    ChatMessageList m_list;   // 消息列表

    explicit ListWithMessage(const CString& text)
    {
        const RECT view = { 0, 0, kViewW, kViewH };
        m_list.ForceLayout(view);
        m_list.AppendMessage(MakeTextMessage(false, text));
        m_list.ForceLayout(view);
    }

    // 最后一行的气泡（前面可能有时间分隔行）
    ChatBubble* Bubble()
    {
        return dynamic_cast<ChatBubble*>(m_list.Children().back().get());
    }
};

// 请气泡画出它自身从顶部往下 offset 像素起、kViewH 像素高的那一段，落到画布的 (·, 0) 处。
// 与消息列表滚动到这个位置时的做法相同：只把与可视区相交的部分交给气泡绘制。
void PaintBand(ChatBubble* b, const PixelCanvas& canvas, int offset)
{
    const RECT rc = b->GetRect();
    RECT band = { rc.left, rc.top + offset, rc.right, rc.top + offset + kViewH };
    if (band.bottom > rc.bottom)
    {
        band.bottom = rc.bottom;
    }
    POINT oldOrg = { 0, 0 };
    ::SetViewportOrgEx(canvas.Dc(), 0, -band.top, &oldOrg);
    b->OnPaint(canvas.Dc(), band);
    ::SetViewportOrgEx(canvas.Dc(), oldOrg.x, oldOrg.y, NULL);
}

// 气泡高度减去一屏：可见区落在气泡最底部时的 offset
int BottomOffset(ChatBubble* b)
{
    return b->GetRowHeight() - kViewH;
}

// 气泡内、距气泡顶部 (dx, dy) 的点（宿主坐标）
POINT PointInBubble(ChatBubble* b, int dx, int dy)
{
    const RECT rc = b->GetRect();
    POINT pt = { rc.left + dx, rc.top + dy };
    return pt;
}

// A2 的测试体：在 dib 指定的那种画布上做一遍
void RunA2(bool dib)
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    const CString text = BuildLongText(kLongChars);
    ListWithMessage a(text);
    ListWithMessage b(text);
    ChatBubble* ba = a.Bubble();
    ChatBubble* bb = b.Bubble();
    REQUIRE(ba != NULL);
    REQUIRE(bb != NULL);
    const int h = ba->GetRowHeight();
    REQUIRE(h > 3 * ChatBubble::TallSliceThreshold());

    INFO("canvas " << CanvasKindName(dib) << ", bubble height " << h);
    //依次从底部滚到中部、顶部，A 侧每次都要换段
    const int offsets[] = { BottomOffset(ba), h / 2 - kViewH / 2, 0 };
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
    {
        INFO("band offset " << offsets[i]);
        PixelCanvas ca(kViewW, kViewH, dib);
        PixelCanvas cb(kViewW, kViewH, dib);
        REQUIRE(ca.Dc() != NULL);
        REQUIRE(cb.Dc() != NULL);
        PaintBand(ba, ca, offsets[i]);
        {
            SliceThresholdScope noSlice(kNoSlice);
            PaintBand(bb, cb, offsets[i]);
        }
        CHECK(ca.SamePixels(cb));
    }
}

// A5 的测试体：在 dib 指定的那种画布上做一遍
void RunA5(bool dib)
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    const CString text = BuildLongText(kLongChars);
    ListWithMessage a(text);
    ListWithMessage b(text);
    ChatBubble* ba = a.Bubble();
    ChatBubble* bb = b.Bubble();
    REQUIRE(ba != NULL);
    REQUIRE(bb != NULL);
    const int bottom = BottomOffset(ba);

    INFO("canvas " << CanvasKindName(dib));
    PixelCanvas ca(kViewW, kViewH, dib);
    PixelCanvas cb(kViewW, kViewH, dib);
    REQUIRE(ca.Dc() != NULL);
    REQUIRE(cb.Dc() != NULL);
    PaintBand(ba, ca, bottom);
    {
        SliceThresholdScope noSlice(kNoSlice);
        PaintBand(bb, cb, bottom);
    }
    REQUIRE(ca.SamePixels(cb));

    //对两个气泡做同样的选区操作，A 走分段缓存里的局部重画，B 强制整条重画，再比较可见的那一段
    struct Step
    {
        const char* m_name;   // 这一步做了什么
        int         m_kind;   // 0 按下，1 拖动到，2 松开，3 全选，4 取消选择
        int         m_dy;     // 指针距可见区上沿的纵向位置（像素），按下 / 拖动 / 松开时有效
    };
    //气泡内的横向落点（像素），落在正文上即可，取值无特殊含义
    const int kDx = 120;
    const Step steps[] =
    {
        { "press",        0, 60 },
        { "drag down",    1, 300 },
        { "drag back up", 1, 180 },
        { "release",      2, 180 },
        { "select all",   3, 0 },
        { "clear",        4, 0 },
    };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
    {
        INFO("step " << i << ": " << steps[i].m_name);
        ChatBubble* pair[] = { ba, bb };
        for (int k = 0; k < 2; ++k)
        {
            ChatBubble* bub = pair[k];
            const POINT pt = PointInBubble(bub, kDx, bottom + steps[i].m_dy);
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
        PaintBand(ba, ca, bottom);
        {
            SliceThresholdScope noSlice(kNoSlice);
            PaintBand(bb, cb, bottom);
        }
        CHECK(ca.SamePixels(cb));
    }
}

} // anonymous namespace

TEST_CASE("A1 超长气泡第一次绘制后只缓存可见区附近的一段", "[chatbubble][tallslice]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    ListWithMessage m(BuildLongText(kLongChars));
    ChatBubble* b = m.Bubble();
    REQUIRE(b != NULL);
    const int h = b->GetRowHeight();
    REQUIRE(h > 3 * ChatBubble::TallSliceThreshold());

    PixelCanvas canvas(kViewW, kViewH, false);
    REQUIRE(canvas.Dc() != NULL);
    PaintBand(b, canvas, BottomOffset(b));

    INFO("bubble height " << h << ", cache height " << b->GetRenderCacheHeight());
    CHECK(b->GetRenderCacheHeight() >= kViewH);
    CHECK(b->GetRenderCacheHeight() <= ChatBubble::TallSliceThreshold());
}

TEST_CASE("A2 分段绘制的像素与整条缓存绘制逐像素相同", "[chatbubble][tallslice]")
{
    RunA2(true);
    RunA2(false);
}

TEST_CASE("A3 在缓存段内移动可见区不重画，移到段外只重画一次", "[chatbubble][tallslice]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    ListWithMessage m(BuildLongText(kLongChars));
    ChatBubble* b = m.Bubble();
    REQUIRE(b != NULL);
    PixelCanvas canvas(kViewW, kViewH, false);
    REQUIRE(canvas.Dc() != NULL);

    ChatBubble::ResetRenderPassCount();
    PaintBand(b, canvas, BottomOffset(b));
    CHECK(ChatBubble::RenderPassCount() == 1);

    //同一位置再画一次：直接用缓存
    PaintBand(b, canvas, BottomOffset(b));
    CHECK(ChatBubble::RenderPassCount() == 1);

    //往上移一小段，仍在缓存段内
    PaintBand(b, canvas, BottomOffset(b) - kA3SmallShift);
    CHECK(ChatBubble::RenderPassCount() == 1);

    //移到顶部，超出缓存段：重画一次
    PaintBand(b, canvas, 0);
    CHECK(ChatBubble::RenderPassCount() == 2);
}

TEST_CASE("A4 不超过阈值的普通气泡仍整条缓存", "[chatbubble][tallslice]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    ListWithMessage m(BuildLongText(kShortChars));
    ChatBubble* b = m.Bubble();
    REQUIRE(b != NULL);
    const int h = b->GetRowHeight();
    REQUIRE(h < ChatBubble::TallSliceThreshold());

    PixelCanvas canvas(kViewW, kViewH, false);
    REQUIRE(canvas.Dc() != NULL);
    PaintBand(b, canvas, 0);
    CHECK(b->GetRenderCacheHeight() == h);
}

TEST_CASE("A5 分段模式下选中文字的局部重画与整条重画逐像素相同", "[chatbubble][tallslice]")
{
    RunA5(true);
    RunA5(false);
}

TEST_CASE("A6 第一次绘制画的文字段数只与可见区有关", "[chatbubble][tallslice]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    const int lengths[] = { kA6SmallChars, kA6LargeChars };
    unsigned long draws[2] = { 0, 0 };
    for (int i = 0; i < 2; ++i)
    {
        ListWithMessage m(BuildLongText(lengths[i]));
        ChatBubble* b = m.Bubble();
        REQUIRE(b != NULL);
        PixelCanvas canvas(kViewW, kViewH, false);
        REQUIRE(canvas.Dc() != NULL);
        ResetInlineProbeCounters();
        PaintBand(b, canvas, BottomOffset(b));
        draws[i] = InlinePlacedDrawCount();
        printf("[tallslice] A6 %5d chars, row %5d px: first paint drew %lu placed items\n",
               lengths[i], b->GetRowHeight(), draws[i]);
    }
    //字数是四倍，画的文字段数应当差不多（只多出缓存段边界上的零头），这里放宽到 1.5 倍
    INFO("small " << draws[0] << ", large " << draws[1]);
    CHECK(draws[0] > 0);
    CHECK(draws[1] * 2 <= draws[0] * 3);
}
