/**
 *  ChatTextExtentCacheTests.cpp
 *
 *  文字测量结果缓存 ChatTextExtentCache 的测试（标签 [textextent]）。
 *
 *  覆盖：
 *    WC1 多种字体、多种文本（中英混排、长网址、换行、表情代码、生僻字与 emoji 等代理对、全角字符），
 *        缓存未命中与命中两次取到的结果都与直接调用 GDI 完全相同（两种测量都验证）
 *    WC2 同一段文字在不同字号下分开缓存：取到的宽度各不相同，且各自等于直接测量
 *    WC3 排版接入缓存后：关闭缓存、打开缓存首次、打开缓存再次三种情况下，折行放置结果、
 *        选区模型、气泡行高逐项相同
 *    WC4 大量不同文字写入同一字体时，条目数不超过单桶上限
 *    WC5 关闭缓存时两个测量函数不读也不写缓存
 *
 *  每个用例结束时恢复为"打开并清空"，不影响其它用例。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <gdiplus.h>
#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatTextExtentCache.h"
#include "../UI/BalloonChatWnd/ChatInlineText.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChat/BalloonChatMsgFormat.h"

using namespace balloonchatwnd;

namespace {

//GetTextExtentExPointW 的最大宽度，与 ChatInlineText.cpp 选区模型所用的值一致
const int kNoLimitExtent = 1000000;
//WC1 对每段文字测量的前缀长度上限（字符数）
const int kMaxPrefixLen = 24;
//WC3 排版的几种可用宽度（像素）：很窄（逐字折行）、中等、接近气泡最大宽度
const int kWrapWidths[] = { 80, 200, 400 };
//WC3 气泡测量时消息区的可用宽度（像素）
const int kBubbleAvailWidth = 620;
//WC3 表情格边长（像素），与气泡正文的内联表情一致
const int kFaceSizePx = 20;
//WC2 对比的两种字号（像素）
const int kSmallFontPx = 14;
const int kLargeFontPx = 20;

//测试用的一种字体设置
struct FontSpec
{
    //字号（像素）
    int  m_nPx;
    //是否加粗
    bool m_bBold;
    //是否斜体
    bool m_bItalic;
    //是否下划线
    bool m_bUnderline;
};

const FontSpec kFonts[] = {
    { 14, false, false, false },
    { 14, true,  false, false },
    { 18, false, true,  false },
    { 12, false, false, true  },
};

//测试语料：覆盖排版里各条分支会遇到的文字形态
const wchar_t* const kCorpus[] = {
    L"今天下午三点开会，记得带上电脑和充电器。",
    L"Hello world, this is a fairly long English sentence to exercise word wrapping in the bubble.",
    L"混合 mixed 文本 with 数字 12345 和标点！？",
    L"https://example.com/a/very/long/path/without/any/spaces/that/must/be/broken/0123456789",
    L"第一行\r\n第二行\n第三行   多个空格\t制表符",
    L"表情 /f[\"5\"] 夹在 /f[\"12\"] 中间",
    L"\xD83D\xDE00 emoji 与 \xD840\xDC0B 生僻字",
    L"\xFF21\xFF22\xFF23全角字母，\xFF71\xFF72\xFF73半角假名",
};

//与气泡正文相同的建字体写法（字体族固定微软雅黑）
HFONT CreateSpecFont(const FontSpec& spec)
{
    return ::CreateFontW(-spec.m_nPx, 0, 0, 0,
                         spec.m_bBold ? FW_SEMIBOLD : FW_NORMAL,
                         spec.m_bItalic ? TRUE : FALSE,
                         spec.m_bUnderline ? TRUE : FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

//屏幕兼容的内存 DC，选入指定字体；析构时还原并释放
struct FontDc
{
    //内存 DC
    HDC   m_hdc;
    //选入的字体
    HFONT m_hFont;
    //选入前的字体，析构时还原
    HFONT m_hOld;

    explicit FontDc(const FontSpec& spec)
    {
        m_hdc = ::CreateCompatibleDC(NULL);
        m_hFont = CreateSpecFont(spec);
        m_hOld = (HFONT)::SelectObject(m_hdc, m_hFont);
    }
    ~FontDc()
    {
        ::SelectObject(m_hdc, m_hOld);
        ::DeleteObject(m_hFont);
        ::DeleteDC(m_hdc);
    }
};

//用例结束时把缓存恢复为"打开并清空"
struct CacheReset
{
    CacheReset()
    {
        ChatTextExtentCache::Inst().SetEnabled(true);
        ChatTextExtentCache::Inst().Clear();
    }
    ~CacheReset()
    {
        ChatTextExtentCache::Inst().SetEnabled(true);
        ChatTextExtentCache::Inst().Clear();
    }
};

//GDI+ 初始化（气泡测量不用 GDI+，但 ChatBubble 的构造与表情解码依赖它已启动）
struct ScopedGdiplusForExtent
{
    //GdiplusStartup 返回的令牌
    ULONG_PTR m_token;
    ScopedGdiplusForExtent() : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }
    ~ScopedGdiplusForExtent()
    {
        Gdiplus::GdiplusShutdown(m_token);
    }
};

//直接调用 GDI 测宽高
SIZE DirectExtent(HDC hdc, LPCWSTR text, int len)
{
    SIZE sz;
    ::ZeroMemory(&sz, sizeof(sz));
    ::GetTextExtentPoint32W(hdc, text, len, &sz);
    return sz;
}

//直接调用 GDI 取逐字符累计宽度
std::vector<INT> DirectCharExtents(HDC hdc, LPCWSTR text, int len)
{
    std::vector<INT> dx((size_t)len, 0);
    INT fit = 0;
    SIZE sz;
    ::GetTextExtentExPointW(hdc, text, len, kNoLimitExtent, &fit, &dx[0], &sz);
    return dx;
}

//一次排版的完整结果：放置项与选区模型
struct LayoutResult
{
    //LayoutInlineContent 的返回值
    SIZE                      m_size;
    //放置项
    std::vector<InlinePlaced> m_placed;
    //选区模型
    SelLayout                 m_sel;
};

LayoutResult RunLayout(HDC hdc, const wchar_t* text, int maxW)
{
    LayoutResult r;
    const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(CString(text));
    r.m_size = LayoutInlineContent(hdc, segs, maxW, kFaceSizePx, &r.m_placed);
    const int totalChars = (int)BuildFlatText(segs).size();
    BuildSelLayoutFromPlaced(hdc, r.m_placed, totalChars, r.m_sel);
    return r;
}

//两次排版结果是否逐项相同；不同时返回第一处差异的说明
std::string CompareLayouts(const LayoutResult& a, const LayoutResult& b)
{
    if (a.m_size.cx != b.m_size.cx || a.m_size.cy != b.m_size.cy)
        return "size differs";
    if (a.m_placed.size() != b.m_placed.size())
        return "placed count differs";
    for (size_t i = 0; i < a.m_placed.size(); ++i)
    {
        const InlinePlaced& p = a.m_placed[i];
        const InlinePlaced& q = b.m_placed[i];
        if (p.isFace != q.isFace || p.faceId != q.faceId || p.text != q.text ||
            !::EqualRect(&p.rc, &q.rc) || p.charBegin != q.charBegin || p.charCount != q.charCount)
            return "placed item differs at " + std::to_string(i);
    }
    if (a.m_sel.m_totalChars != b.m_sel.m_totalChars || a.m_sel.m_lines.size() != b.m_sel.m_lines.size())
        return "sel layout shape differs";
    for (size_t li = 0; li < a.m_sel.m_lines.size(); ++li)
    {
        const SelLine& la = a.m_sel.m_lines[li];
        const SelLine& lb = b.m_sel.m_lines[li];
        if (la.m_top != lb.m_top || la.m_bottom != lb.m_bottom || la.m_runs.size() != lb.m_runs.size())
            return "sel line differs at " + std::to_string(li);
        for (size_t ri = 0; ri < la.m_runs.size(); ++ri)
        {
            const SelRun& ra = la.m_runs[ri];
            const SelRun& rb = lb.m_runs[ri];
            if (ra.m_charBegin != rb.m_charBegin || ra.m_charCount != rb.m_charCount ||
                ra.m_left != rb.m_left || ra.m_right != rb.m_right || ra.m_charRights != rb.m_charRights)
                return "sel run differs at line " + std::to_string(li) + " run " + std::to_string(ri);
        }
    }
    return std::string();
}

} // namespace

TEST_CASE("WC1 缓存未命中与命中的结果都与直接调用 GDI 完全相同", "[textextent]")
{
    CacheReset reset;
    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();

    int nCompared = 0;
    int nMismatch = 0;
    for (size_t f = 0; f < sizeof(kFonts) / sizeof(kFonts[0]); ++f)
    {
        FontDc dc(kFonts[f]);
        const TextExtentFontKey key = TextExtentFontKey::FromDc(dc.m_hdc);
        for (size_t t = 0; t < sizeof(kCorpus) / sizeof(kCorpus[0]); ++t)
        {
            const std::wstring text(kCorpus[t]);
            //各个前缀（单字、短词）以及全文，覆盖排版里逐词元测量的各种长度
            std::vector<int> lens;
            for (int n = 1; n <= kMaxPrefixLen && n <= (int)text.size(); ++n)
                lens.push_back(n);
            lens.push_back((int)text.size());

            for (size_t k = 0; k < lens.size(); ++k)
            {
                const int len = lens[k];
                const SIZE direct = DirectExtent(dc.m_hdc, text.c_str(), len);
                const SIZE miss = cache.TextExtent(dc.m_hdc, key, text.c_str(), len);
                const SIZE hit = cache.TextExtent(dc.m_hdc, key, text.c_str(), len);
                if (direct.cx != miss.cx || direct.cy != miss.cy || direct.cx != hit.cx || direct.cy != hit.cy)
                    ++nMismatch;

                const std::vector<INT> directDx = DirectCharExtents(dc.m_hdc, text.c_str(), len);
                std::vector<INT> missDx;
                std::vector<INT> hitDx;
                cache.CharExtents(dc.m_hdc, key, text.c_str(), len, kNoLimitExtent, missDx);
                cache.CharExtents(dc.m_hdc, key, text.c_str(), len, kNoLimitExtent, hitDx);
                if (directDx != missDx || directDx != hitDx)
                    ++nMismatch;
                ++nCompared;
            }
        }
    }

    INFO("compared " << nCompared << " (font, text) pairs");
    CHECK(nCompared > 0);
    CHECK(nMismatch == 0);
    CHECK(cache.EntryCount() > 0);
}

TEST_CASE("WC2 同一段文字在不同字号下分开缓存", "[textextent]")
{
    CacheReset reset;
    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();
    const wchar_t* const text = L"Hello 世界";
    const int len = (int)wcslen(text);

    //变量名不能叫 small：Windows 头文件 rpcndr.h 里有 #define small char
    const FontSpec smallSpec = { kSmallFontPx, false, false, false };
    const FontSpec largeSpec = { kLargeFontPx, false, false, false };
    FontDc dcSmall(smallSpec);
    FontDc dcLarge(largeSpec);

    //先让小字号的结果进缓存，再取大字号：若键里没有区分字体，大字号会取到小字号的结果
    const SIZE s1 = cache.TextExtent(dcSmall.m_hdc, TextExtentFontKey::FromDc(dcSmall.m_hdc), text, len);
    const SIZE l1 = cache.TextExtent(dcLarge.m_hdc, TextExtentFontKey::FromDc(dcLarge.m_hdc), text, len);
    const SIZE s2 = cache.TextExtent(dcSmall.m_hdc, TextExtentFontKey::FromDc(dcSmall.m_hdc), text, len);

    CHECK(s1.cx != l1.cx);
    CHECK(s1.cx == DirectExtent(dcSmall.m_hdc, text, len).cx);
    CHECK(l1.cx == DirectExtent(dcLarge.m_hdc, text, len).cx);
    CHECK(s2.cx == s1.cx);
}

TEST_CASE("WC3 排版接入缓存后，开关缓存前后的放置结果、选区模型与气泡行高逐项相同", "[textextent]")
{
    CacheReset reset;
    ScopedGdiplusForExtent gdip;
    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();

    //一、排版与选区模型
    for (size_t f = 0; f < sizeof(kFonts) / sizeof(kFonts[0]); ++f)
    {
        FontDc dc(kFonts[f]);
        for (size_t t = 0; t < sizeof(kCorpus) / sizeof(kCorpus[0]); ++t)
        {
            for (size_t w = 0; w < sizeof(kWrapWidths) / sizeof(kWrapWidths[0]); ++w)
            {
                cache.SetEnabled(false);
                const LayoutResult off = RunLayout(dc.m_hdc, kCorpus[t], kWrapWidths[w]);
                cache.SetEnabled(true);
                cache.Clear();
                const LayoutResult first = RunLayout(dc.m_hdc, kCorpus[t], kWrapWidths[w]);
                const LayoutResult again = RunLayout(dc.m_hdc, kCorpus[t], kWrapWidths[w]);

                INFO("font " << f << ", text " << t << ", width " << kWrapWidths[w]);
                CHECK(CompareLayouts(off, first) == "");
                CHECK(CompareLayouts(off, again) == "");
            }
        }
    }

    //二、气泡的首次测量：行高不因缓存而变化
    for (size_t t = 0; t < sizeof(kCorpus) / sizeof(kCorpus[0]); ++t)
    {
        const ChatMessage msg = MakeTextMessage(false, CString(kCorpus[t]));

        cache.SetEnabled(false);
        ChatBubble off;
        off.SetMessage(msg);
        off.Measure(kBubbleAvailWidth);

        cache.SetEnabled(true);
        cache.Clear();
        ChatBubble first;
        first.SetMessage(msg);
        first.Measure(kBubbleAvailWidth);
        ChatBubble again;
        again.SetMessage(msg);
        again.Measure(kBubbleAvailWidth);

        INFO("text " << t);
        CHECK(off.GetRowHeight() == first.GetRowHeight());
        CHECK(off.GetRowHeight() == again.GetRowHeight());
    }
}

TEST_CASE("WC4 同一字体写入大量不同文字，条目数不超过单桶上限", "[textextent]")
{
    CacheReset reset;
    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();
    const FontSpec spec = { kSmallFontPx, false, false, false };
    FontDc dc(spec);
    const TextExtentFontKey key = TextExtentFontKey::FromDc(dc.m_hdc);

    //比上限多写一些，确认触发了"满了就清空该桶"
    const size_t kExtra = 500;
    size_t maxSeen = 0;
    for (size_t i = 0; i < ChatTextExtentCache::kMaxEntriesPerFont + kExtra; ++i)
    {
        const std::wstring text = L"w" + std::to_wstring(i);
        cache.TextExtent(dc.m_hdc, key, text.c_str(), (int)text.size());
        if (cache.EntryCount() > maxSeen)
            maxSeen = cache.EntryCount();
    }

    CHECK(maxSeen <= ChatTextExtentCache::kMaxEntriesPerFont);
    CHECK(cache.EntryCount() < ChatTextExtentCache::kMaxEntriesPerFont);
}

TEST_CASE("WC5 关闭缓存时不读也不写缓存", "[textextent]")
{
    CacheReset reset;
    ChatTextExtentCache& cache = ChatTextExtentCache::Inst();
    const FontSpec spec = { kSmallFontPx, false, false, false };
    FontDc dc(spec);
    const TextExtentFontKey key = TextExtentFontKey::FromDc(dc.m_hdc);
    const wchar_t* const text = L"关闭缓存";
    const int len = (int)wcslen(text);

    cache.SetEnabled(false);
    const SIZE sz = cache.TextExtent(dc.m_hdc, key, text, len);
    std::vector<INT> dx;
    cache.CharExtents(dc.m_hdc, key, text, len, kNoLimitExtent, dx);

    CHECK(cache.EntryCount() == 0);
    CHECK(sz.cx == DirectExtent(dc.m_hdc, text, len).cx);
    CHECK(dx == DirectCharExtents(dc.m_hdc, text, len));
}
