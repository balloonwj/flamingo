/**
 *  ChatLayoutReuseTests.cpp
 *
 *  超长文字消息的测量优化（标签 [chatbubble][layoutreuse]，2026-10-04）：
 *
 *    B1a 测量时的排版（ChatBubble::Measure 的做法：按消息字体、以可用宽度折行）与绘制时的排版
 *        （RenderContent 的做法：按绘制样式、以文字区宽度折行）结果完全相同 —— 文字区宽度只要不小于
 *        测量出的最大行宽、不大于测量时的可用宽度即可。这是「绘制时直接复用测量的排版结果」的前提。
 *    B1b 超长文字气泡第一次绘制时不再调用排版（复用测量阶段的结果）。
 *    B1c 建选区模型（BuildSelLayoutFromPlaced）的结果与优化前的实现逐项相同。优化前的实现原样
 *        复制在本文件里作为参照（RefBuildSelLayout）。
 *    B1d 排版（LayoutInlineContent）的结果与优化前的实现逐项相同：放置项、尺寸、是否截断，含限行与
 *        只测尺寸两种调用。优化前的实现及其三个辅助函数原样复制在本文件里作为参照（RefLayoutInlineContent）。
 *    B2  （隐藏标签 [.chatperf]）同一次运行、同样负载下新旧两种实现的耗时对比，只输出数字，并断言新实现
 *        不比旧实现慢。运行：FlamingoUnit.exe "B2*"。
 *
 *  背景：一万字的文字消息，测量阶段建选区模型约 150~230 毫秒、排版约 60~110 毫秒，第一次绘制时
 *  又按绘制样式把整段重新排版一次（CPF13，Debug 版）。
 *
 *  样本覆盖：纯中文段落、中英混排且密集带链接、超长的无空格英文串（要在单词内部断行）、
 *  内联表情、显式换行、粗体斜体与较大字号。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChat/BalloonChatMsgFormat.h"   // ParseSendBody
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatFaceTag.h"          // kFaceDisplayPx
#include "../UI/BalloonChatWnd/ChatInlineText.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatTextExtentCache.h"
#include "../UI/BalloonChatWnd/ChatTextSelection.h"
#include "../Common/I18n/I18n.h"                       // i18n::UiFontName
#include "../Common/Util/PerfStopWatch.h"              // B2 计时

using namespace balloonchatwnd;

namespace {

// 消息区宽度与可视区高度（像素），取单聊窗默认大小下消息区的量级
const int kViewW = 620;
const int kViewH = 420;

// B1a 依次试的可用宽度（像素）：从很窄的气泡到单聊窗里文字区的最大宽度
const int kB1aWidths[] = { 120, 240, 384, 500 };

// B1b 超长消息的字数（约数）
const int kB1bChars = 10800;

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

// 一个样本：名字、正文，以及测量 / 绘制用的文字样式
struct Sample
{
    const char*     m_name;    // 样本名，失败时 INFO 里用
    CString         m_text;    // 消息正文（发送格式，表情写成 /f["id"]）
    InlineTextStyle m_style;   // 字号、粗体、斜体
};

// 拼一段约 chars 字、中英文混排、每一小节带一个链接、整段没有换行的文字（与 CPF13 同一形状）
CString BuildLinkParagraph(int chars)
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

// 本套用例的样本表
std::vector<Sample> BuildSamples()
{
    std::vector<Sample> samples;
    Sample s;

    s.m_name = "pure chinese paragraph";
    s.m_text = L"今天下午三点在三楼会议室开项目周会，请大家提前准备好本周的进展、遇到的问题以及下周的计划。"
               L"会议预计一个小时，结束后各小组再分别对齐细节，有冲突的同学请提前在群里说一声。";
    samples.push_back(s);

    s.m_name = "mixed text with dense links";
    s.m_text = BuildLinkParagraph(1200);
    samples.push_back(s);

    s.m_name = "long unbroken ascii token";
    s.m_text = L"see https://example.com/a/very/long/path/that/never/contains/any/space/characters/at/all/"
               L"0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdef"
               L"ghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz?query=1&other=2 done";
    samples.push_back(s);

    s.m_name = "inline faces";
    s.m_text = L"哈哈/f[\"5\"]这个太好笑了/f[\"12\"]/f[\"13\"]，明天 see you again/f[\"5\"]，记得带上 report.docx";
    samples.push_back(s);

    s.m_name = "explicit newlines";
    s.m_text = L"第一行\r\n第二行 with some English words\r\n\r\n    缩进的第四行，后面还有很长很长很长很长很长很长的一段文字需要折行";
    samples.push_back(s);

    s.m_name = "bold italic large";
    s.m_text = L"加粗斜体的大字号消息 Bold italic message, mixed with 中文 and numbers 1234567890 to wrap.";
    s.m_style.sizePx = 17;
    s.m_style.bold = true;
    s.m_style.italic = true;
    samples.push_back(s);

    return samples;
}

// 按 ChatBubble::Measure 的方式建测量用字体（与它逐参数相同）
HFONT CreateMeasureFont(const InlineTextStyle& st)
{
    return ::CreateFontW(
        -st.sizePx, 0, 0, 0,
        st.bold ? FW_SEMIBOLD : FW_NORMAL,
        st.italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
}

// 两组放置项是否逐项相同
bool SamePlaced(const std::vector<InlinePlaced>& a, const std::vector<InlinePlaced>& b, std::string& diff)
{
    if (a.size() != b.size())
    {
        char buf[96];
        sprintf_s(buf, "count %u vs %u", (unsigned)a.size(), (unsigned)b.size());
        diff = buf;
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        const InlinePlaced& x = a[i];
        const InlinePlaced& y = b[i];
        if (x.isFace != y.isFace || x.faceId != y.faceId || x.text != y.text
            || !::EqualRect(&x.rc, &y.rc) || x.charBegin != y.charBegin || x.charCount != y.charCount)
        {
            char buf[160];
            sprintf_s(buf, "item %u: rc (%ld,%ld,%ld,%ld) vs (%ld,%ld,%ld,%ld), chars %d+%d vs %d+%d",
                      (unsigned)i, x.rc.left, x.rc.top, x.rc.right, x.rc.bottom,
                      y.rc.left, y.rc.top, y.rc.right, y.rc.bottom,
                      x.charBegin, x.charCount, y.charBegin, y.charCount);
            diff = buf;
            return false;
        }
    }
    return true;
}

// 优化前的 BuildSelLayoutFromPlaced，原样复制（2026-10-04），作为 B1c 的参照
void RefBuildSelLayout(HDC hdc, const std::vector<InlinePlaced>& placed, int totalChars, SelLayout& out)
{
    //与产品代码同一取值：远大于任何气泡文本宽度，让整段文字都算「放得下」
    const int kCharExtentNoLimit = 1000000;
    out.m_lines.clear();
    out.m_totalChars = totalChars;
    const TextExtentFontKey fontKey = TextExtentFontKey::FromDc(hdc);
    ChatTextExtentCache& extentCache = ChatTextExtentCache::Inst();
    for (size_t i = 0; i < placed.size(); ++i)
    {
        const InlinePlaced& p = placed[i];
        if (out.m_lines.empty() || out.m_lines.back().m_top != p.rc.top)
        {
            SelLine line;
            line.m_top    = p.rc.top;
            line.m_bottom = p.rc.bottom;
            out.m_lines.push_back(line);
        }
        SelLine& line = out.m_lines.back();
        if (p.rc.bottom > line.m_bottom)
        {
            line.m_bottom = p.rc.bottom;
        }
        SelRun run;
        run.m_charBegin = p.charBegin;
        run.m_charCount = p.charCount;
        run.m_left      = p.rc.left;
        run.m_right     = p.rc.right;
        if (p.isFace || p.charCount <= 0)
        {
            run.m_charRights.push_back(p.rc.right);
            run.m_charCount = 1;
        }
        else
        {
            const int textLen = p.text.GetLength();
            run.m_charRights.reserve((size_t)(textLen > run.m_charCount ? textLen : run.m_charCount));
            extentCache.AppendCharRights(hdc, fontKey, p.text, textLen,
                                         kCharExtentNoLimit, p.rc.left, run.m_charRights);
            while ((int)run.m_charRights.size() < run.m_charCount)
            {
                const int k = (int)run.m_charRights.size() + 1;
                run.m_charRights.push_back(
                    p.rc.left + (p.rc.right - p.rc.left) * k / run.m_charCount);
            }
        }
        line.m_runs.push_back(std::move(run));
    }
}

// 两份选区模型是否逐项相同
bool SameSelLayout(const SelLayout& a, const SelLayout& b, std::string& diff)
{
    if (a.m_totalChars != b.m_totalChars || a.m_lines.size() != b.m_lines.size())
    {
        diff = "total chars or line count differ";
        return false;
    }
    for (size_t li = 0; li < a.m_lines.size(); ++li)
    {
        const SelLine& x = a.m_lines[li];
        const SelLine& y = b.m_lines[li];
        if (x.m_top != y.m_top || x.m_bottom != y.m_bottom || x.m_runs.size() != y.m_runs.size())
        {
            char buf[96];
            sprintf_s(buf, "line %u header differs", (unsigned)li);
            diff = buf;
            return false;
        }
        for (size_t ri = 0; ri < x.m_runs.size(); ++ri)
        {
            const SelRun& r = x.m_runs[ri];
            const SelRun& s = y.m_runs[ri];
            if (r.m_charBegin != s.m_charBegin || r.m_charCount != s.m_charCount || r.m_left != s.m_left
                || r.m_right != s.m_right || r.m_charRights != s.m_charRights)
            {
                char buf[96];
                sprintf_s(buf, "line %u run %u differs", (unsigned)li, (unsigned)ri);
                diff = buf;
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 优化前的 LayoutInlineContent 及其三个辅助函数，原样复制（2026-10-04），作为 B1d 的参照。
// 只改了名字（加 Ref 前缀）、去掉了性能探针计数，逻辑一行未动。
// ---------------------------------------------------------------------------

bool RefReachedLineLimit(int y, int lineH, int maxLines)
{
    return (maxLines > 0 && lineH > 0 && y >= maxLines * lineH);
}

struct RefWrapToken
{
    CString m_text;       // 测量 / 绘制用的文本
    int     m_srcBegin;   // 在源串里的起始下标
    int     m_srcCount;   // 在源串里占的字符数（\r\n 为 2，其余等于 m_text 长度）

    RefWrapToken()
        : m_srcBegin(0)
        , m_srcCount(0)
    {
    }
};

void RefTokenizeForWrap(const CString& s, std::vector<RefWrapToken>& out)
{
    const int n = s.GetLength();
    int i = 0;
    while (i < n)
    {
        const wchar_t c = s[i];
        RefWrapToken tok;
        tok.m_srcBegin = i;
        if (c == L' ' || c == L'\t')
        {
            int j = i;
            while (j < n && (s[j] == L' ' || s[j] == L'\t'))
            {
                ++j;
            }
            tok.m_text = s.Mid(i, j - i);
            i = j;
        }
        else if (c == L'\r' || c == L'\n')
        {
            int j = i + 1;
            if (c == L'\r' && j < n && s[j] == L'\n')
            {
                ++j;
            }
            tok.m_text = CString(_T("\n"));
            i = j;
        }
        else if (c < 0x80)
        {
            int j = i;
            while (j < n && s[j] < 0x80 && s[j] != L' ' && s[j] != L'\t'
                   && s[j] != L'\r' && s[j] != L'\n')
            {
                ++j;
            }
            tok.m_text = s.Mid(i, j - i);
            i = j;
        }
        else
        {
            tok.m_text = s.Mid(i, 1);
            ++i;
        }
        tok.m_srcCount = i - tok.m_srcBegin;
        out.push_back(tok);
    }
}

void RefPlaceOverlongToken(HDC hdc, const CString& tok, int tokCharBegin,
                           int maxW, int lineH, std::vector<InlinePlaced>* outPlaced,
                           int& x, int& y, int& maxLineW, int maxLines, bool& truncated)
{
    const int tokLen = tok.GetLength();
    int consumed = 0;
    while (consumed < tokLen)
    {
        int fitCount = 0;
        const int avail = maxW - x;
        if (avail > 0)
        {
            INT  fit = 0;
            SIZE restSize;
            ::ZeroMemory(&restSize, sizeof(restSize));
            ::GetTextExtentExPointW(hdc, (LPCWSTR)tok + consumed, tokLen - consumed,
                                    avail, &fit, NULL, &restSize);
            fitCount = (int)fit;
        }
        if (fitCount <= 0)
        {
            if (x > 0)
            {
                if (x > maxLineW)
                {
                    maxLineW = x;
                }
                x = 0;
                y += lineH;
                if (RefReachedLineLimit(y, lineH, maxLines))
                {
                    truncated = true;
                    return;
                }
                continue;
            }
            fitCount = 1;
        }
        if (fitCount > tokLen - consumed)
        {
            fitCount = tokLen - consumed;
        }
        const CString piece = tok.Mid(consumed, fitCount);
        SIZE ps;
        ::ZeroMemory(&ps, sizeof(ps));
        ::GetTextExtentPoint32W(hdc, piece, piece.GetLength(), &ps);
        if (outPlaced != NULL)
        {
            InlinePlaced p;
            p.isFace    = false;
            p.faceId    = -1;
            p.text      = piece;
            p.rc.left   = x;
            p.rc.top    = y;
            p.rc.right  = x + ps.cx;
            p.rc.bottom = y + lineH;
            p.charBegin = tokCharBegin + consumed;
            p.charCount = fitCount;
            outPlaced->push_back(p);
        }
        x += ps.cx;
        consumed += fitCount;
        if (consumed < tokLen)
        {
            if (x > maxLineW)
            {
                maxLineW = x;
            }
            x = 0;
            y += lineH;
            if (RefReachedLineLimit(y, lineH, maxLines))
            {
                truncated = true;
                return;
            }
        }
    }
}

SIZE RefLayoutInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                            int maxW, int faceSize, std::vector<InlinePlaced>* outPlaced,
                            int maxLines, bool* outTruncated)
{
    bool truncated = false;
    if (outTruncated != NULL)
    {
        *outTruncated = false;
    }
    if (maxW < 1)
    {
        maxW = 1;
    }
    TEXTMETRICW tm;
    ::ZeroMemory(&tm, sizeof(tm));
    ::GetTextMetricsW(hdc, &tm);
    const int textH = tm.tmHeight;
    const int lineH = (textH > faceSize) ? textH : faceSize;
    const TextExtentFontKey fontKey = TextExtentFontKey::FromDc(hdc);
    ChatTextExtentCache& extentCache = ChatTextExtentCache::Inst();
    int x = 0;
    int y = 0;
    int maxLineW = 0;
    int charPos = 0;
    for (size_t si = 0; si < segs.size() && !truncated; ++si)
    {
        if (RefReachedLineLimit(y, lineH, maxLines))
        {
            truncated = true;
            break;
        }
        const balloonchat::ChatSegment& seg = segs[si];
        if (seg.m_bFace)
        {
            if (x > 0 && x + faceSize > maxW)
            {
                if (x > maxLineW)
                {
                    maxLineW = x;
                }
                x = 0;
                y += lineH;
                if (RefReachedLineLimit(y, lineH, maxLines))
                {
                    truncated = true;
                    break;
                }
            }
            if (outPlaced != NULL)
            {
                InlinePlaced p;
                p.isFace = true;
                p.faceId = seg.m_nFaceId;
                const int top = y + (lineH - faceSize) / 2;
                p.rc.left = x;
                p.rc.top = top;
                p.rc.right = x + faceSize;
                p.rc.bottom = top + faceSize;
                p.charBegin = charPos;
                p.charCount = 1;
                outPlaced->push_back(p);
            }
            x += faceSize;
            ++charPos;
        }
        else
        {
            std::vector<RefWrapToken> tokens;
            RefTokenizeForWrap(seg.m_strText, tokens);
            for (size_t ti = 0; ti < tokens.size() && !truncated; ++ti)
            {
                if (RefReachedLineLimit(y, lineH, maxLines))
                {
                    truncated = true;
                    break;
                }
                const RefWrapToken& wt = tokens[ti];
                const CString&      tok = wt.m_text;
                const int tokCharBegin = charPos;
                charPos += wt.m_srcCount;
                if (tok == _T("\n"))
                {
                    if (x > maxLineW)
                    {
                        maxLineW = x;
                    }
                    x = 0;
                    y += lineH;
                    continue;
                }
                const SIZE ts = extentCache.TextExtent(hdc, fontKey, tok, tok.GetLength());
                const bool isSpace = (tok[0] == L' ' || tok[0] == L'\t');
                if (x > 0 && x + ts.cx > maxW)
                {
                    if (x > maxLineW)
                    {
                        maxLineW = x;
                    }
                    x = 0;
                    y += lineH;
                    if (RefReachedLineLimit(y, lineH, maxLines))
                    {
                        truncated = true;
                        break;
                    }
                    if (isSpace)
                    {
                        continue;
                    }
                }
                if (ts.cx > maxW)
                {
                    RefPlaceOverlongToken(hdc, tok, tokCharBegin, maxW, lineH,
                                          outPlaced, x, y, maxLineW, maxLines, truncated);
                    continue;
                }
                if (outPlaced != NULL)
                {
                    InlinePlaced p;
                    p.isFace = false;
                    p.faceId = -1;
                    p.text = tok;
                    p.rc.left = x;
                    p.rc.top = y;
                    p.rc.right = x + ts.cx;
                    p.rc.bottom = y + lineH;
                    p.charBegin = tokCharBegin;
                    p.charCount = wt.m_srcCount;
                    outPlaced->push_back(p);
                }
                x += ts.cx;
            }
        }
    }
    if (x > maxLineW)
    {
        maxLineW = x;
    }
    if (truncated && maxLines > 0 && y > (maxLines - 1) * lineH)
    {
        y = (maxLines - 1) * lineH;
    }
    if (outTruncated != NULL)
    {
        *outTruncated = truncated;
    }
    SIZE size;
    size.cx = maxLineW;
    size.cy = y + lineH;
    return size;
}

// B1d 依次试的行数上限：0 表示不限行；3 用来走到「排满即截断」的那几条路径
const int kB1dMaxLines[] = { 0, 3 };

} // anonymous namespace

TEST_CASE("B1a 测量时与绘制时的排版结果相同", "[chatbubble][layoutreuse]")
{
    const std::vector<Sample> samples = BuildSamples();
    HDC screen = ::GetDC(NULL);
    REQUIRE(screen != NULL);
    //绘制时的排版在渲染缓存的内存 DC 上进行，这里同样用一块与屏幕兼容的内存 DC
    HDC mem = ::CreateCompatibleDC(screen);
    REQUIRE(mem != NULL);

    for (size_t si = 0; si < samples.size(); ++si)
    {
        const Sample& sample = samples[si];
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(sample.m_text);
        for (size_t wi = 0; wi < sizeof(kB1aWidths) / sizeof(kB1aWidths[0]); ++wi)
        {
            const int maxW = kB1aWidths[wi];
            INFO("sample " << sample.m_name << ", width " << maxW);

            //测量：与 ChatBubble::Measure 相同，按消息字体在屏幕 DC 上以可用宽度折行
            HFONT hFont = CreateMeasureFont(sample.m_style);
            REQUIRE(hFont != NULL);
            HFONT hOld = (HFONT)::SelectObject(screen, hFont);
            std::vector<InlinePlaced> measured;
            const SIZE sz = LayoutInlineContent(screen, segs, maxW, kFaceDisplayPx, &measured);
            ::SelectObject(screen, hOld);
            ::DeleteObject(hFont);

            //绘制：与 RenderContent 相同，按绘制样式以文字区宽度折行。文字区宽度通常就是最大行宽，
            //带引用块的短消息会被放宽到引用块的最小宽度，所以再试一个介于两者之间的宽度
            const int widths[] = { (int)sz.cx, ((int)sz.cx + maxW) / 2, maxW };
            for (size_t k = 0; k < sizeof(widths) / sizeof(widths[0]); ++k)
            {
                INFO("render width " << widths[k] << " (max line width " << sz.cx << ")");
                std::vector<InlinePlaced> rendered;
                LayoutInlineContentForStyle(mem, segs, widths[k], kFaceDisplayPx, sample.m_style, &rendered);
                std::string diff;
                const bool same = SamePlaced(measured, rendered, diff);
                INFO(diff);
                CHECK(same);
            }
        }
    }
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
}

TEST_CASE("B1b 超长文字气泡第一次绘制时不再重新排版", "[chatbubble][layoutreuse]")
{
    GdiplusScope gdip;
    REQUIRE(gdip.Ok());
    const RECT view = { 0, 0, kViewW, kViewH };
    ChatMessageList list;
    list.ForceLayout(view);
    list.AppendMessage(MakeTextMessage(false, BuildLinkParagraph(kB1bChars)));
    list.ForceLayout(view);

    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, kViewW, kViewH);
    ::ReleaseDC(NULL, screen);
    REQUIRE(mem != NULL);
    REQUIRE(bmp != NULL);
    HGDIOBJ old = ::SelectObject(mem, bmp);

    ResetInlineProbeCounters();
    list.OnPaint(mem, view);
    const unsigned long layouts = InlineLayoutCallCount();
    INFO("layout calls during first paint: " << layouts);
    CHECK(layouts == 0);

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
}

TEST_CASE("B1c 建选区模型的结果与优化前的实现逐项相同", "[chatbubble][layoutreuse]")
{
    const std::vector<Sample> samples = BuildSamples();
    HDC screen = ::GetDC(NULL);
    REQUIRE(screen != NULL);
    for (size_t si = 0; si < samples.size(); ++si)
    {
        const Sample& sample = samples[si];
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(sample.m_text);
        const std::wstring flat = BuildFlatText(segs);
        for (size_t wi = 0; wi < sizeof(kB1aWidths) / sizeof(kB1aWidths[0]); ++wi)
        {
            INFO("sample " << sample.m_name << ", width " << kB1aWidths[wi]);
            HFONT hFont = CreateMeasureFont(sample.m_style);
            REQUIRE(hFont != NULL);
            HFONT hOld = (HFONT)::SelectObject(screen, hFont);
            std::vector<InlinePlaced> placed;
            LayoutInlineContent(screen, segs, kB1aWidths[wi], kFaceDisplayPx, &placed);

            SelLayout expected;
            RefBuildSelLayout(screen, placed, (int)flat.size(), expected);
            SelLayout actual;
            BuildSelLayoutFromPlaced(screen, placed, (int)flat.size(), actual);
            ::SelectObject(screen, hOld);
            ::DeleteObject(hFont);

            std::string diff;
            const bool same = SameSelLayout(expected, actual, diff);
            INFO(diff);
            CHECK(same);
        }
    }
    ::ReleaseDC(NULL, screen);
}

TEST_CASE("B1d 排版结果与优化前的实现逐项相同", "[chatbubble][layoutreuse]")
{
    const std::vector<Sample> samples = BuildSamples();
    HDC screen = ::GetDC(NULL);
    REQUIRE(screen != NULL);
    for (size_t si = 0; si < samples.size(); ++si)
    {
        const Sample& sample = samples[si];
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(sample.m_text);
        HFONT hFont = CreateMeasureFont(sample.m_style);
        REQUIRE(hFont != NULL);
        HFONT hOld = (HFONT)::SelectObject(screen, hFont);
        for (size_t wi = 0; wi < sizeof(kB1aWidths) / sizeof(kB1aWidths[0]); ++wi)
        {
            for (size_t li = 0; li < sizeof(kB1dMaxLines) / sizeof(kB1dMaxLines[0]); ++li)
            {
                const int maxW = kB1aWidths[wi];
                const int maxLines = kB1dMaxLines[li];
                INFO("sample " << sample.m_name << ", width " << maxW << ", max lines " << maxLines);

                //产出放置项的调用
                std::vector<InlinePlaced> expected;
                bool expectedTrunc = false;
                const SIZE expectedSize = RefLayoutInlineContent(screen, segs, maxW, kFaceDisplayPx, &expected,
                                                                 maxLines, &expectedTrunc);
                std::vector<InlinePlaced> actual;
                bool actualTrunc = false;
                const SIZE actualSize = LayoutInlineContent(screen, segs, maxW, kFaceDisplayPx, &actual,
                                                            maxLines, &actualTrunc);
                std::string diff;
                const bool same = SamePlaced(expected, actual, diff);
                INFO(diff);
                CHECK(same);
                CHECK(expectedSize.cx == actualSize.cx);
                CHECK(expectedSize.cy == actualSize.cy);
                CHECK(expectedTrunc == actualTrunc);

                //只测尺寸的调用
                bool sizeOnlyTrunc = false;
                const SIZE sizeOnly = LayoutInlineContent(screen, segs, maxW, kFaceDisplayPx, NULL,
                                                          maxLines, &sizeOnlyTrunc);
                CHECK(sizeOnly.cx == expectedSize.cx);
                CHECK(sizeOnly.cy == expectedSize.cy);
                CHECK(sizeOnlyTrunc == expectedTrunc);
            }
        }
        ::SelectObject(screen, hOld);
        ::DeleteObject(hFont);
    }
    ::ReleaseDC(NULL, screen);
}

// B2 每种实现重复排版的次数，取平均值，减小单次计时的抖动
const int kB2Repeats = 5;

TEST_CASE("B2 同一负载下新旧实现的耗时对比（排版与建选区模型）", "[.chatperf][layoutreuse]")
{
    HDC screen = ::GetDC(NULL);
    REQUIRE(screen != NULL);
    InlineTextStyle style;
    HFONT hFont = CreateMeasureFont(style);
    REQUIRE(hFont != NULL);
    HFONT hOld = (HFONT)::SelectObject(screen, hFont);
    //与 CPF13 相同的两档字数，以及单聊窗文字区的最大宽度
    const int lengths[] = { 5400, 10800 };
    const int kB2WrapWidth = 384;
    for (size_t li = 0; li < sizeof(lengths) / sizeof(lengths[0]); ++li)
    {
        const CString text = BuildLinkParagraph(lengths[li]);
        const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(text);
        const std::wstring flat = BuildFlatText(segs);
        //先各跑一遍，把文字测量缓存填满，后面量的都是缓存已命中时的耗时
        std::vector<InlinePlaced> placed;
        LayoutInlineContent(screen, segs, kB2WrapWidth, kFaceDisplayPx, &placed);

        double refLayoutMs = 0;
        double newLayoutMs = 0;
        double refSelMs = 0;
        double newSelMs = 0;
        for (int r = 0; r < kB2Repeats; ++r)
        {
            std::vector<InlinePlaced> a;
            PerfStopWatch sw;
            RefLayoutInlineContent(screen, segs, kB2WrapWidth, kFaceDisplayPx, &a, 0, NULL);
            refLayoutMs += sw.ElapsedMs();
            std::vector<InlinePlaced> b;
            sw.Restart();
            LayoutInlineContent(screen, segs, kB2WrapWidth, kFaceDisplayPx, &b);
            newLayoutMs += sw.ElapsedMs();

            SelLayout sa;
            sw.Restart();
            RefBuildSelLayout(screen, placed, (int)flat.size(), sa);
            refSelMs += sw.ElapsedMs();
            SelLayout sb;
            sw.Restart();
            BuildSelLayoutFromPlaced(screen, placed, (int)flat.size(), sb);
            newSelMs += sw.ElapsedMs();
        }
        refLayoutMs /= kB2Repeats;
        newLayoutMs /= kB2Repeats;
        refSelMs /= kB2Repeats;
        newSelMs /= kB2Repeats;
        printf("[chatperf] B2 %5d chars: layout old %.2f ms -> new %.2f ms, sel layout old %.2f ms -> new %.2f ms\n",
               text.GetLength(), refLayoutMs, newLayoutMs, refSelMs, newSelMs);
        fflush(stdout);
        CHECK(newLayoutMs <= refLayoutMs);
        CHECK(newSelMs <= refSelMs);
    }
    ::SelectObject(screen, hOld);
    ::DeleteObject(hFont);
    ::ReleaseDC(NULL, screen);
}
