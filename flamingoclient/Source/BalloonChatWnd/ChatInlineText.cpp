/**
 *  ChatInlineText.cpp
 *
 *  「文字 + 内联表情」排版与绘制引擎的实现。接口说明见 ChatInlineText.h。
 *
 *  本文件的排版逻辑是从 ChatBubble.cpp 的匿名 namespace <u>原样平移</u>过来的
 *  （2026-08-05，见 docs/消息记录内容交互方案.md §4），除了给 LayoutInlineContent /
 *  PlaceOverlongToken / DrawInlineContent 增加"最多排几行"这个入参之外，没有改动
 *  任何一行既有逻辑 —— 气泡侧一律传 0（不限行），渲染结果与平移前完全一致。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程逐字共用，故自己把 balloonui / ATL 所需的前置依赖
 *  引进来。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  <u>绘制表情要求进程已初始化 GDI+</u>（表情 PNG 用 Gdiplus::Bitmap 解码，以保住
 *  alpha 通道，才能正确叠在彩色气泡底上）。排版与扁平文本那几个函数不碰 GDI+。
 *
 *  balloonwj@qq.com   2026-08-05
 */

// ---- ATL 前置依赖（CString / RECT 等），与 ChatWindowLayout.cpp 同一套 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include <gdiplus.h>   // 表情 PNG 保 alpha 解码与绘制
#include <map>

#include "ChatInlineText.h"

namespace balloonchatwnd {

namespace {

// 排版游标是否已经越过行数上限。maxLines <= 0 表示不限行，恒返回 false。
//   y：      当前排版游标的纵坐标（相对布局原点）。
//   lineH：  行高（像素）。
//   maxLines：最多排几行。
bool ReachedLineLimit(int y, int lineH, int maxLines)
{
    return (maxLines > 0 && lineH > 0 && y >= maxLines * lineH);
}

// 取本进程 exe 所在目录（带尾部反斜杠）。用于定位 Face\face<N>.png；用 Win32
// API 取，避免依赖 Hootina::CPath（本 .cpp 与 FlamingoUnit 共用，少拉链接依赖）。
CString FaceExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    CString path = buf;
    int slash = path.ReverseFind(L'\\');
    if (slash >= 0)
    {
        return path.Left(slash + 1);
    }
    return CString();
}

// 取某 faceId 的表情位图（按 faceId==face<N>.png 约定定位）。进程级缓存、保留 alpha
// （用 GDI+ Bitmap，绘制时 DrawImage 叠加，能正确盖在彩色气泡上）。失败返回 NULL。
// 缓存随进程退出释放（固定 ≤143 张小图，不显式清理）。
Gdiplus::Bitmap* GetFaceGdiBitmapCached(int faceId)
{
    static std::map<int, Gdiplus::Bitmap*> s_cache;
    std::map<int, Gdiplus::Bitmap*>::iterator it = s_cache.find(faceId);
    if (it != s_cache.end())
    {
        return it->second;   // 命中（含上次解码失败缓存的 NULL）
    }

    Gdiplus::Bitmap* result = NULL;
    CString path;
    path.Format(_T("%sFace\\face%d.png"), (LPCTSTR)FaceExeDir(), faceId);
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(path);
    if (bmp != NULL && bmp->GetLastStatus() == Gdiplus::Ok)
    {
        result = bmp;
    }
    else
    {
        delete bmp;   // 解码失败也要释放探针对象
    }
    s_cache[faceId] = result;   // 即使 NULL 也缓存，避免每帧重试解码
    return result;
}

// 把一段文本切成可折行的 token：连续空白合一段；显式换行单独成 token；连续 ASCII
// 非空白合成一个"词"（不在词中折行）；非 ASCII（中日韩等）每字一 token（可逐字
// 折行）。近似 DrawText 的 DT_WORDBREAK 行为。
// 一个折行 token。除了用于测量 / 绘制的文本，还带上它在<u>源串</u>里占的区间 ——
// 选中复制要把屏幕坐标映射回原文下标，而 token 文本与源串并非处处等长（\r\n 会被
// 归一成一个 "\n"），只靠 token 文本长度累加会错位。
struct WrapToken
{
    CString m_text;       // 测量 / 绘制用的文本
    int     m_srcBegin;   // 在源串里的起始下标
    int     m_srcCount;   // 在源串里占的字符数（\r\n 为 2，其余等于 m_text 长度）

    WrapToken()
        : m_srcBegin(0)
        , m_srcCount(0)
    {
    }
};

void TokenizeForWrap(const CString& s, std::vector<WrapToken>& out)
{
    const int n = s.GetLength();
    int i = 0;
    while (i < n)
    {
        const wchar_t c = s[i];
        WrapToken tok;
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
                ++j;   // \r\n 视作一个换行
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

// 把一个"本身就比整行可用宽度还宽"的文字词元按字符切开、逐行填进排版结果。
//
// 折行本来只发生在词与词之间（空格处、汉字之间），但像 C++ 符号名、长网址、长
// 文件路径这类中间没有空格的连续 ASCII 串，会被 TokenizeForWrap 当成一个不可分割
// 的词元：一行放不下、又没有任何可断之处，原样放置就会画到气泡外面去。此时只能
// 退一步在字符之间断开 —— 效果与记事本勾选"自动换行"后长行被折断一致。
//
// 每切出一片都作为独立的 InlinePlaced 产出，并按片内字符数推进 charBegin，这样
// 选区模型仍然精确到字符，选中复制不会错位。
//   hdc：          已选好目标字体的设备上下文（度量按该字体）。
//   tok：          待切分的词元文本；调用方须保证它不是换行符。
//   tokCharBegin： 本词元首字符在扁平文本里的下标。
//   maxW：         整行可用宽度（像素）。
//   lineH：        行高（像素）。
//   outPlaced：    非 NULL 时填入各放置项；为 NULL 时只推进排版游标（只测尺寸）。
//   x / y：        [出入参] 排版游标，返回时指向本词元末尾。
//   maxLineW：     [出入参] 迄今为止的最宽行宽。
//   maxLines：     最多排几行；<=0 表示不限行。
//   truncated：    [出入参] 因行数上限提前收工时置 true。
void PlaceOverlongToken(HDC hdc, const CString& tok, int tokCharBegin,
                        int maxW, int lineH, std::vector<InlinePlaced>* outPlaced,
                        int& x, int& y, int& maxLineW, int maxLines, bool& truncated)
{
    const int tokLen = tok.GetLength();
    int consumed = 0;   // 本词元已放置的字符数

    while (consumed < tokLen)
    {
        // 问 GDI：当前行剩下这点宽度，从 consumed 处起还放得下几个字符。
        // 这里不切子串、直接从原串偏移处量，避免超长串上反复复制。
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
                // 当前行剩余宽度连一个字符都放不下：换行之后再试。
                if (x > maxLineW)
                {
                    maxLineW = x;
                }
                x = 0;
                y += lineH;
                if (ReachedLineLimit(y, lineH, maxLines))
                {
                    truncated = true;
                    return;
                }
                continue;
            }
            // 已经在行首仍放不下（可用宽度窄于单个字符）：强行放一个。宁可这
            // 一个字符略微出界，也不能让循环在这里空转。
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

        // 还有剩余字符，说明这一行已经填满，换行继续填。
        if (consumed < tokLen)
        {
            if (x > maxLineW)
            {
                maxLineW = x;
            }
            x = 0;
            y += lineH;
            if (ReachedLineLimit(y, lineH, maxLines))
            {
                truncated = true;
                return;
            }
        }
    }
}

} // 匿名 namespace（本文件私有的排版细节）

// 把消息段序列按 maxW 折行排版。调用前须已把目标字体 select 进 hdc（测量按该字体）。
//   outPlaced：非 NULL 时填入各放置项（绘制用）；NULL 时只测尺寸。
//   返回：内容整体尺寸（不含内边距）。
SIZE LayoutInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                         int maxW, int faceSize, std::vector<InlinePlaced>* outPlaced,
                         int maxLines, bool* outTruncated)
{
    // 因行数上限而没排完时置 true，收尾处回填给调用方。
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

    int x = 0;
    int y = 0;
    int maxLineW = 0;
    // 扁平文本里的游标：文字按源串长度推进，表情占 1（U+FFFC 占位）。
    // 被丢弃的 token（换行、折行后行首空白）不放置、但同样要推进，否则后面的
    // 字符下标会整体前移，选中复制取到的就是错位的子串。
    int charPos = 0;

    for (size_t si = 0; si < segs.size() && !truncated; ++si)
    {
        // 已排满允许的行数：后面的内容一概不再排，并告诉调用方"还有没画完的"。
        if (ReachedLineLimit(y, lineH, maxLines))
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
                if (ReachedLineLimit(y, lineH, maxLines))
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
                p.charCount = 1;          // 表情在扁平文本里占一个占位字符
                outPlaced->push_back(p);
            }
            x += faceSize;
            ++charPos;
        }
        else
        {
            std::vector<WrapToken> tokens;
            TokenizeForWrap(seg.m_strText, tokens);
            for (size_t ti = 0; ti < tokens.size() && !truncated; ++ti)
            {
                // 每个词元排之前都过一次上限：显式换行走的是 continue，
                // 由这里统一兜住，不必在那个分支里再判一遍。
                if (ReachedLineLimit(y, lineH, maxLines))
                {
                    truncated = true;
                    break;
                }

                const WrapToken& wt = tokens[ti];
                const CString&   tok = wt.m_text;
                const int tokCharBegin = charPos;
                charPos += wt.m_srcCount;   // 无论放不放置，游标都要推进

                if (tok == _T("\n"))   // 显式换行
                {
                    if (x > maxLineW)
                    {
                        maxLineW = x;
                    }
                    x = 0;
                    y += lineH;
                    continue;
                }
                SIZE ts;
                ::GetTextExtentPoint32W(hdc, tok, tok.GetLength(), &ts);
                const bool isSpace = (tok[0] == L' ' || tok[0] == L'\t');
                if (x > 0 && x + ts.cx > maxW)
                {
                    if (x > maxLineW)
                    {
                        maxLineW = x;
                    }
                    x = 0;
                    y += lineH;
                    if (ReachedLineLimit(y, lineH, maxLines))
                    {
                        truncated = true;
                        break;
                    }
                    if (isSpace)
                    {
                        continue;   // 折行后丢弃行首空白
                    }
                }
                // 词元本身就比整行还宽（长符号名 / 长网址 / 长路径这类连续 ASCII
                // 串）：换到新行也照样放不下，词间折行救不了，只能在字符之间断开，
                // 否则会一路画出气泡边界之外。
                if (ts.cx > maxW)
                {
                    PlaceOverlongToken(hdc, tok, tokCharBegin, maxW, lineH,
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

    // 被行数上限截断时，游标可能已跨到"第 maxLines+1 行"的位置（判定发生在换行
    // <u>之后</u>）。高度必须夹回恰好 maxLines 行，否则折叠态会凭空多出一行空白。
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

// 把消息段序列拼成"扁平文本"：文字原样拼接，每个表情占一个 U+FFFC 占位字符。
// 选中区间就是这个串上的下标区间，复制时再把占位符换成 "[表情]"。
std::wstring BuildFlatText(const std::vector<balloonchat::ChatSegment>& segs)
{
    std::wstring flat;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].m_bFace)
        {
            flat += kSelFacePlaceholder;
        }
        else
        {
            flat += (LPCWSTR)segs[i].m_strText;
        }
    }
    return flat;
}

// 传给 GetTextExtentExPointW 的 nMaxExtent（最大允许宽度，单位：逻辑单位/像素）。
//
// 该 API 只回填"在 nMaxExtent 宽度内放得下"的那几个字符的累计宽度，其余留原值。
// 因此此处若传 0，等于声明"一个字符都放不下"，lpnDx 会原封不动地全是 0 —— 实测
// （14px 微软雅黑、"hello"）：nMaxExtent=0 时 fit=0、数组为 [0,0,0,0,0]；改传
// 100000 时 fit=5、数组为 [9,17,21,25,34]。数组全 0 会让段内每个字符的边界都退化
// 成段左边界，选中文字就只能整段整段地选、选不中段内部的字。
//
// 这里取一个远大于任何气泡文本宽度的值，让整段文字都算"放得下"，从而拿到完整的
// 逐字符累计宽度。气泡文本区宽度受 kBubbleMaxWidthRatio 约束，远到不了这个量级。
static const int kCharExtentNoLimit = 1000000;

// 把排版放置结果转成选区模型：按 top 分行，每项转成一段，并用 GetTextExtentExPointW
// 量出段内每个字符的右边界（选区左右边界、命中测试都要精确到字符）。
// 调用前须已把目标字体 select 进 hdc —— 与排版时必须是同一套字体，否则量出来的
// 字符宽度和实际画出来的对不上。
void BuildSelLayoutFromPlaced(HDC hdc, const std::vector<InlinePlaced>& placed,
                              int totalChars, SelLayout& out)
{
    out.m_lines.clear();
    out.m_totalChars = totalChars;

    for (size_t i = 0; i < placed.size(); ++i)
    {
        const InlinePlaced& p = placed[i];

        // 放置项按文档顺序产出，top 变了就是换了一行。
        if (out.m_lines.empty() || out.m_lines.back().m_top != p.rc.top)
        {
            SelLine line;
            line.m_top    = p.rc.top;
            line.m_bottom = p.rc.bottom;
            out.m_lines.push_back(line);
        }
        SelLine& line = out.m_lines.back();
        // 同一行里表情格比文字矮 / 高时，行高取最大，保证选区高亮盖住整行。
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
            // 逐字符累计宽度。alpDx[k] 是前 k+1 个字符的总宽（相对段起点）。
            std::vector<INT> alpDx((size_t)p.text.GetLength(), 0);
            INT  fit = 0;
            SIZE sz;
            ::GetTextExtentExPointW(hdc, p.text, p.text.GetLength(),
                                    kCharExtentNoLimit, &fit,
                                    alpDx.empty() ? NULL : &alpDx[0], &sz);
            for (size_t k = 0; k < alpDx.size(); ++k)
            {
                run.m_charRights.push_back(p.rc.left + alpDx[k]);
            }
            // 兜底：万一量失败（返回的项数不足），用等分宽度补齐，宁可略有偏差
            // 也不要让选区少几个字符点不中。
            while ((int)run.m_charRights.size() < run.m_charCount)
            {
                const int k = (int)run.m_charRights.size() + 1;
                run.m_charRights.push_back(
                    p.rc.left + (p.rc.right - p.rc.left) * k / run.m_charCount);
            }
        }

        line.m_runs.push_back(run);
    }
}

// 消息文本里是否含内联表情（决定走内联排版还是原 DrawText 快路径）。
bool MessageTextHasFace(const CString& text)
{
    std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(text);
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].m_bFace)
        {
            return true;
        }
    }
    return false;
}

// 在 rc 内绘制"文字 + 内联表情"。st 决定字体 / 颜色（与 DrawGdiText 同口径）。
void DrawInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                       const RECT& rc, const InlineTextStyle& st, int faceSize,
                       int maxLines)
{
    HFONT hFont = ::CreateFontW(
        -st.sizePx, 0, 0, 0,
        st.bold ? FW_SEMIBOLD : FW_NORMAL,
        st.italic ? TRUE : FALSE,
        st.underline ? TRUE : FALSE,
        st.strike ? TRUE : FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);

    const int maxW = rc.right - rc.left;
    std::vector<InlinePlaced> placed;
    LayoutInlineContent(hdc, segs, maxW, faceSize, &placed, maxLines);

    const int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, st.color);
    const int ox = rc.left;
    const int oy = rc.top;

    for (size_t i = 0; i < placed.size(); ++i)
    {
        const InlinePlaced& p = placed[i];
        RECT r = { p.rc.left + ox, p.rc.top + oy, p.rc.right + ox, p.rc.bottom + oy };
        if (p.isFace)
        {
            Gdiplus::Bitmap* pBmp = GetFaceGdiBitmapCached(p.faceId);
            if (pBmp != NULL)
            {
                Gdiplus::Graphics g(hdc);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(pBmp, Gdiplus::Rect(r.left, r.top,
                                                r.right - r.left, r.bottom - r.top));
            }
        }
        else
        {
            ::DrawTextW(hdc, p.text, -1, &r,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        }
    }

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

} // namespace balloonchatwnd
