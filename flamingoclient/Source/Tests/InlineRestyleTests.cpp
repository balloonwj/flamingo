/**
 *  InlineRestyleTests.cpp
 *
 *  正文里若干段字符区间换样式重画（超链接、@提醒高亮）的单元测试（标签 [chatinline][restyle]）：
 *
 *    RS1 像素一致：带多个折行链接与一段 @ 片段的文字，用 DrawInlineContentRestyledRanges 画一张，
 *        再按 2026-10-01 之前的做法（逐个矩形裁剪、整条重画）画一张，两张逐像素完全相同；
 *        常规字形与斜体加粗字形各验证一遍。
 *    RS2 单区间函数 DrawInlineContentRestyledRange 与只传一个区间的多区间函数画出来逐像素相同。
 *    RS3 工作量不随链接数增长：重画 68 个链接时整条排版只做 1 次，放置项的绘制次数不超过放置项总数。
 *    RS4 聊天气泡层面：带 10 个链接与带 68 个链接的气泡，第一次绘制时整条排版的次数相同且是个小常数。
 *    RS5 消息记录窗的正文控件（展开态）同样满足 RS4 的条件。
 *
 *  背景：一条几十行、每行一个链接的消息，旧做法要把整条内容排版、绘制几十遍，首次绘制要好几秒。
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

#include "../UI/BalloonChatWnd/ChatInlineText.h"
#include "../UI/BalloonChatWnd/ChatLinkDetect.h"
#include "../UI/BalloonChat/BalloonChatMsgFormat.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"     // RS4：把消息追加进列表后绘制
#include "../UI/BalloonChatWnd/ChatMockData.h"        // RS4：MakeTextMessage
#include "../UI/BalloonHistory/HistoryContentView.h"  // RS5：消息记录窗的正文控件
#include "ChatLinkSampleText.h"

#include <gdiplus.h>

using namespace balloonchatwnd;

namespace {

// 画布尺寸（像素）：宽度够放文本区，高度够放下 RS1 那段文字折行后的全部内容
const int kCanvasW = 260;
const int kCanvasH = 900;

// RS1 / RS2 的文本区宽度（像素）：取得窄一些，让链接折成两行以上，验证跨行区间
const int kNarrowTextW = 200;

// RS3 的文本区宽度（像素），与单聊窗默认大小下文字气泡的最大正文宽度同一量级
const int kWideTextW = 400;

// RS3 的画布高度（像素）：68 行文字在 400 像素宽下折行后的高度以内
const int kTallCanvasH = 2400;

// 正文字号（像素），即气泡正文的默认字号；表情格边长与之相同（本组文字不含表情）
const int kFontPx = 14;
const int kFacePx = 14;

// RS1 / RS2 用的文字行数
const int kShortLines = 6;

// RS3 用的文字行数，与用户实际发送的那段文字相同
const int kLongLines = 68;

// RS1 里模拟 @ 片段的字符区间（扁平文本口径，右开）：落在第一行标题中间，取值无特殊含义
const int kMentionBegin = 4;
const int kMentionEnd = 9;

// 三种文字颜色：正文、链接、@ 高亮
const COLORREF kBodyColor = RGB(0x33, 0x33, 0x33);
const COLORREF kLinkColor = RGB(0x2F, 0x6F, 0xD0);
const COLORREF kMentionColor = RGB(0xE0, 0x6A, 0x2C);

// 画布底色
const COLORREF kCanvasBg = RGB(0xFF, 0xFF, 0xFF);

// 一块 32 位自底向上的 DIB 画布，可直接读像素，析构时释放。
class PixelCanvas
{
public:
    // 建一块 w×h 的画布并涂成白色；失败时 Dc() 返回 NULL。
    PixelCanvas(int w, int h)
        : m_dc(NULL)
        , m_bmp(NULL)
        , m_old(NULL)
        , m_bits(NULL)
        , m_w(w)
        , m_h(h)
    {
        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = ::GetDC(NULL);
        m_dc = ::CreateCompatibleDC(screen);
        m_bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &m_bits, NULL, 0);
        ::ReleaseDC(NULL, screen);
        if (m_dc == NULL || m_bmp == NULL)
        {
            return;
        }
        m_old = ::SelectObject(m_dc, m_bmp);
        RECT rc = { 0, 0, w, h };
        HBRUSH bg = ::CreateSolidBrush(kCanvasBg);
        ::FillRect(m_dc, &rc, bg);
        ::DeleteObject(bg);
    }

    // 换回原位图并释放资源。
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

    // 画布的设备上下文；创建失败时为 NULL。
    HDC Dc() const
    {
        return (m_bits != NULL) ? m_dc : NULL;
    }

    // 两块同尺寸画布的像素是否逐字节相同。先 GdiFlush，确保之前的 GDI 绘制都已写进位图。
    bool SamePixels(const PixelCanvas& other) const
    {
        ::GdiFlush();
        if (m_bits == NULL || other.m_bits == NULL || m_w != other.m_w || m_h != other.m_h)
        {
            return false;
        }
        return memcmp(m_bits, other.m_bits, (size_t)m_w * (size_t)m_h * sizeof(DWORD)) == 0;
    }

    // 与底色不同的像素个数（用来确认确实画上了东西，避免两张空白画布"相同"）。
    int InkPixelCount() const
    {
        ::GdiFlush();
        if (m_bits == NULL)
        {
            return 0;
        }
        const DWORD* p = (const DWORD*)m_bits;
        const DWORD bg = ((DWORD)GetRValue(kCanvasBg) << 16) | ((DWORD)GetGValue(kCanvasBg) << 8)
                       | (DWORD)GetBValue(kCanvasBg);
        int count = 0;
        for (int i = 0; i < m_w * m_h; ++i)
        {
            if ((p[i] & 0x00FFFFFF) != bg)
            {
                ++count;
            }
        }
        return count;
    }

private:
    HDC     m_dc;     // 画布内存 DC
    HBITMAP m_bmp;    // DIB 位图
    HGDIOBJ m_old;    // 选入 DIB 之前的原位图，析构时换回
    void*   m_bits;   // DIB 像素首地址（每像素 4 字节，自底向上）
    int     m_w;      // 画布宽（像素）
    int     m_h;      // 画布高（像素）
};

// 一段待测文字的排版结果：段序列、扁平文本、字符级排版模型、链接区间。
struct Prepared
{
    std::vector<balloonchat::ChatSegment> m_segs;    // 正文段序列
    std::wstring                          m_flat;    // 扁平文本
    SelLayout                             m_sel;     // 字符级排版模型
    std::vector<InlineCharRange>          m_links;   // 各链接的字符区间
    size_t                                m_placed;  // 整条排版的放置项个数
};

// 按样式建字体，写法与 ChatInlineText.cpp 的 CreateInlineFont 相同。
HFONT CreateFontFor(const InlineTextStyle& st)
{
    return ::CreateFontW(-st.sizePx, 0, 0, 0, st.bold ? FW_SEMIBOLD : FW_NORMAL,
                         st.italic ? TRUE : FALSE, st.underline ? TRUE : FALSE, st.strike ? TRUE : FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

// 解析文字、按 textW 宽排版，产出字符级排版模型与链接区间（与气泡 Measure 的做法相同）。
Prepared Prepare(HDC hdc, const CString& text, int textW, const InlineTextStyle& st)
{
    Prepared p;
    p.m_segs = balloonchat::ParseSendBody(text);
    p.m_flat = BuildFlatText(p.m_segs);
    HFONT font = CreateFontFor(st);
    HGDIOBJ old = ::SelectObject(hdc, font);
    std::vector<InlinePlaced> placed;
    LayoutInlineContent(hdc, p.m_segs, textW, kFacePx, &placed);
    BuildSelLayoutFromPlaced(hdc, placed, (int)p.m_flat.size(), p.m_sel);
    ::SelectObject(hdc, old);
    ::DeleteObject(font);
    p.m_placed = placed.size();
    const std::vector<LinkSpan> links = DetectLinks(p.m_flat);
    for (size_t i = 0; i < links.size(); ++i)
    {
        p.m_links.push_back(InlineCharRange(links[i].m_off, links[i].m_off + links[i].m_len));
    }
    return p;
}

// 2026-10-01 之前的换样式重画做法，作为像素比对的基准：每个区间的每个矩形都设裁剪区、整条重画。
void ReferenceRestyle(HDC hdc, const Prepared& p, const std::vector<InlineCharRange>& ranges,
                      const RECT& txRc, const InlineTextStyle& st)
{
    for (size_t k = 0; k < ranges.size(); ++k)
    {
        const std::vector<SelRect> rects = BuildSelectionRects(p.m_sel, ranges[k].m_begin, ranges[k].m_end);
        for (size_t r = 0; r < rects.size(); ++r)
        {
            HRGN saved = ::CreateRectRgn(0, 0, 0, 0);
            const int hadClip = ::GetClipRgn(hdc, saved);
            ::IntersectClipRect(hdc, txRc.left + rects[r].m_left, txRc.top + rects[r].m_top,
                                txRc.left + rects[r].m_right, txRc.top + rects[r].m_bottom);
            DrawInlineContent(hdc, p.m_segs, txRc, st, kFacePx);
            ::SelectClipRgn(hdc, (hadClip == 1) ? saved : NULL);
            ::DeleteObject(saved);
        }
    }
}

// 各区间一共占几个矩形。
size_t CountRects(const Prepared& p, const std::vector<InlineCharRange>& ranges)
{
    size_t n = 0;
    for (size_t k = 0; k < ranges.size(); ++k)
    {
        n += BuildSelectionRects(p.m_sel, ranges[k].m_begin, ranges[k].m_end).size();
    }
    return n;
}

// 由正文样式派生链接样式与 @ 高亮样式：只改颜色，链接另加下划线。
InlineTextStyle LinkStyleOf(const InlineTextStyle& body)
{
    InlineTextStyle s = body;
    s.color = kLinkColor;
    s.underline = true;
    return s;
}

InlineTextStyle MentionStyleOf(const InlineTextStyle& body)
{
    InlineTextStyle s = body;
    s.color = kMentionColor;
    return s;
}

// RS1 的一次比对：同一段文字，先画正文，再按新、旧两种做法重画链接与 @ 片段，比较两张画布。
void CheckPixelEquivalence(const InlineTextStyle& body)
{
    PixelCanvas fresh(kCanvasW, kCanvasH);
    PixelCanvas reference(kCanvasW, kCanvasH);
    REQUIRE(fresh.Dc() != NULL);
    REQUIRE(reference.Dc() != NULL);

    const CString text = chattest::BuildLinkSampleText(kShortLines);
    const Prepared p = Prepare(fresh.Dc(), text, kNarrowTextW, body);
    REQUIRE(p.m_links.size() == (size_t)kShortLines);
    //文本区窄，每个链接都折成了两行以上，跨行区间确实被覆盖到
    CHECK(CountRects(p, p.m_links) > p.m_links.size());

    std::vector<InlineCharRange> mention;
    mention.push_back(InlineCharRange(kMentionBegin, kMentionEnd));
    const RECT txRc = { 0, 0, kNarrowTextW, kCanvasH };
    const InlineTextStyle linkStyle = LinkStyleOf(body);
    const InlineTextStyle mentionStyle = MentionStyleOf(body);

    DrawInlineContent(fresh.Dc(), p.m_segs, txRc, body, kFacePx);
    DrawInlineContentRestyledRanges(fresh.Dc(), p.m_segs, p.m_sel, mention, txRc, mentionStyle, kFacePx);
    DrawInlineContentRestyledRanges(fresh.Dc(), p.m_segs, p.m_sel, p.m_links, txRc, linkStyle, kFacePx);

    DrawInlineContent(reference.Dc(), p.m_segs, txRc, body, kFacePx);
    ReferenceRestyle(reference.Dc(), p, mention, txRc, mentionStyle);
    ReferenceRestyle(reference.Dc(), p, p.m_links, txRc, linkStyle);

    CHECK(fresh.InkPixelCount() > 0);
    CHECK(fresh.SamePixels(reference));
}

} // anonymous namespace

TEST_CASE("RS1 换样式重画与旧做法逐像素相同：折行链接 + @ 片段，常规与斜体加粗字形", "[chatinline][restyle]")
{
    InlineTextStyle body;
    body.sizePx = kFontPx;
    body.color = kBodyColor;

    SECTION("常规字形")
    {
        CheckPixelEquivalence(body);
    }
    SECTION("斜体加粗字形：字形会伸出放置项矩形，重画范围须照样覆盖")
    {
        body.italic = true;
        body.bold = true;
        CheckPixelEquivalence(body);
    }
}

TEST_CASE("RS2 单区间函数与只传一个区间的多区间函数逐像素相同", "[chatinline][restyle]")
{
    PixelCanvas single(kCanvasW, kCanvasH);
    PixelCanvas multi(kCanvasW, kCanvasH);
    REQUIRE(single.Dc() != NULL);
    REQUIRE(multi.Dc() != NULL);

    InlineTextStyle body;
    body.sizePx = kFontPx;
    body.color = kBodyColor;
    const CString text = chattest::BuildLinkSampleText(kShortLines);
    const Prepared p = Prepare(single.Dc(), text, kNarrowTextW, body);
    REQUIRE_FALSE(p.m_links.empty());
    const RECT txRc = { 0, 0, kNarrowTextW, kCanvasH };
    const InlineTextStyle linkStyle = LinkStyleOf(body);

    //取跨行的第一个链接
    const InlineCharRange link = p.m_links[0];
    std::vector<InlineCharRange> one;
    one.push_back(link);

    DrawInlineContent(single.Dc(), p.m_segs, txRc, body, kFacePx);
    DrawInlineContentRestyledRange(single.Dc(), p.m_segs, p.m_sel, link.m_begin, link.m_end, txRc, linkStyle,
                                   kFacePx);
    DrawInlineContent(multi.Dc(), p.m_segs, txRc, body, kFacePx);
    DrawInlineContentRestyledRanges(multi.Dc(), p.m_segs, p.m_sel, one, txRc, linkStyle, kFacePx);

    CHECK(single.InkPixelCount() > 0);
    CHECK(single.SamePixels(multi));
}

TEST_CASE("RS3 重画 68 个链接时整条排版只做 1 次，放置项绘制次数不超过放置项总数", "[chatinline][restyle]")
{
    PixelCanvas canvas(kWideTextW, kTallCanvasH);
    REQUIRE(canvas.Dc() != NULL);

    InlineTextStyle body;
    body.sizePx = kFontPx;
    body.color = kBodyColor;
    const CString text = chattest::BuildLinkSampleText(kLongLines);
    const Prepared p = Prepare(canvas.Dc(), text, kWideTextW, body);
    REQUIRE(p.m_links.size() == (size_t)kLongLines);
    const RECT txRc = { 0, 0, kWideTextW, kTallCanvasH };

    ResetInlineProbeCounters();
    DrawInlineContentRestyledRanges(canvas.Dc(), p.m_segs, p.m_sel, p.m_links, txRc, LinkStyleOf(body), kFacePx);
    INFO("placed " << p.m_placed << ", link rects " << CountRects(p, p.m_links)
         << ", placed draws " << InlinePlacedDrawCount());
    CHECK(InlineLayoutCallCount() == 1);
    CHECK(InlinePlacedDrawCount() > 0);
    CHECK(InlinePlacedDrawCount() <= (unsigned long)p.m_placed);
}

// =====================================================================
// RS4 / RS5：调用方（聊天气泡、消息记录窗正文控件）一次画完全部链接
// =====================================================================

namespace {

// RS4 / RS5 对比的两种行数：链接少的一档与用户实际发送的那一档
const int kFewLinkLines = 10;

// 聊天消息区尺寸（像素），取单聊窗默认大小下消息区的实际尺寸量级
const int kChatViewW = 620;
const int kChatViewH = 420;

// 第一次绘制时整条排版次数的上限：正文一次、链接一次，再留一次余量
const unsigned long kMaxLayoutsPerPaint = 3;

// 在作用域内启动 GDI+，离开时关闭。气泡的形状都用 GDI+ 绘制。
class GdiplusScope
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    GdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~GdiplusScope()
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

// 把一条 lines 行的带链接消息追加进消息列表并排好版，返回第一次绘制期间整条排版的次数。
unsigned long ChatBubbleLayoutsOnFirstPaint(int lines)
{
    PixelCanvas canvas(kChatViewW, kChatViewH);
    REQUIRE(canvas.Dc() != NULL);
    const RECT view = { 0, 0, kChatViewW, kChatViewH };
    ChatMessageList list;
    list.ForceLayout(view);
    list.AppendMessage(MakeTextMessage(true, chattest::BuildLinkSampleText(lines)));
    list.ForceLayout(view);

    ResetInlineProbeCounters();
    list.OnPaint(canvas.Dc(), view);
    return InlineLayoutCallCount();
}

// 让消息记录窗的正文控件以展开态显示一条 lines 行的带链接消息，返回一次绘制期间整条排版的次数。
unsigned long HistoryContentLayoutsOnPaint(int lines)
{
    balloonhistory::HistoryContentView content;
    content.SetContent(chattest::BuildLinkSampleText(lines));
    content.SetExpanded(true);
    const int h = content.MeasureHeight(kWideTextW);
    REQUIRE(h > 0);
    PixelCanvas canvas(kWideTextW, h);
    REQUIRE(canvas.Dc() != NULL);
    const RECT rc = { 0, 0, kWideTextW, h };
    content.SetRect(rc);

    ResetInlineProbeCounters();
    content.OnPaint(canvas.Dc(), rc);
    return InlineLayoutCallCount();
}

} // anonymous namespace

TEST_CASE("RS4 聊天气泡第一次绘制时整条排版的次数不随链接数增长", "[chatinline][restyle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    const unsigned long few = ChatBubbleLayoutsOnFirstPaint(kFewLinkLines);
    const unsigned long many = ChatBubbleLayoutsOnFirstPaint(kLongLines);
    INFO("layouts with " << kFewLinkLines << " links: " << few << ", with " << kLongLines << " links: " << many);
    //2026-10-04 起气泡绘制时直接复用测量阶段的排版结果（Tests/ChatLayoutReuseTests.cpp 的 B1b），
    //第一次绘制的整条排版次数通常为 0，所以不再要求它大于 0；不随链接数增长这一点照旧检查
    CHECK(many == few);
    CHECK(many <= kMaxLayoutsPerPaint);
}

TEST_CASE("RS5 消息记录窗正文控件绘制时整条排版的次数不随链接数增长", "[chatinline][restyle]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    const unsigned long few = HistoryContentLayoutsOnPaint(kFewLinkLines);
    const unsigned long many = HistoryContentLayoutsOnPaint(kLongLines);
    INFO("layouts with " << kFewLinkLines << " links: " << few << ", with " << kLongLines << " links: " << many);
    CHECK(few > 0);
    CHECK(many == few);
    CHECK(many <= kMaxLayoutsPerPaint);
}
