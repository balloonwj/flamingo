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
#include "ChatTextExtentCache.h"   // 逐词元测宽的结果缓存（排版的主要开销在逐字的 GDI 测量）

namespace balloonchatwnd {

namespace {

// 性能探针计数器，见 ChatInlineText.h 的 InlineLayoutCallCount / InlinePlacedDrawCount
unsigned long g_inlineLayoutCalls = 0;   // LayoutInlineContent 的调用次数
unsigned long g_inlinePlacedDraws = 0;   // 绘制放置项的次数

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

// 某 faceId 的表情图片路径：<exe 目录>\Face\face<N>.png。气泡绘制与输入框插入表情都按它定位。
CString FacePathForId(int faceId)
{
    CString path;
    path.Format(_T("%sFace\\face%d.png"), (LPCTSTR)FaceExeDir(), faceId);
    return path;
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
    const CString path = FacePathForId(faceId);
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

CString FaceImagePathFromId(int faceId)
{
    return FacePathForId(faceId);
}

// 把消息段序列按 maxW 折行排版。调用前须已把目标字体 select 进 hdc（测量按该字体）。
//   outPlaced：非 NULL 时填入各放置项（绘制用）；NULL 时只测尺寸。
//   返回：内容整体尺寸（不含内边距）。
SIZE LayoutInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                         int maxW, int faceSize, std::vector<InlinePlaced>* outPlaced,
                         int maxLines, bool* outTruncated)
{
    ++g_inlineLayoutCalls;

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
    //逐词元测宽走缓存：中文每个字单独成一个词元，逐个调 GDI 是排版的主要开销
    //（50 条消息的首次测量里约占一半以上）。缓存键取自当前选入 hdc 的字体，每次排版取一次
    const TextExtentFontKey fontKey = TextExtentFontKey::FromDc(hdc);
    ChatTextExtentCache& extentCache = ChatTextExtentCache::Inst();

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

    //逐字符累计宽度走缓存，理由同 LayoutInlineContent；缓存键每次调用取一次
    const TextExtentFontKey fontKey = TextExtentFontKey::FromDc(hdc);
    ChatTextExtentCache& extentCache = ChatTextExtentCache::Inst();

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
            // 逐字符右边界 = 段左边界 + 前 k+1 个字符的累计宽度，由缓存直接追加进来，
            // 不经过中间数组（调试版运行库下每段一次的小块分配开销与 GDI 调用相当）。
            const int textLen = p.text.GetLength();
            run.m_charRights.reserve((size_t)(textLen > run.m_charCount ? textLen : run.m_charCount));
            extentCache.AppendCharRights(hdc, fontKey, p.text, textLen,
                                         kCharExtentNoLimit, p.rc.left, run.m_charRights);
            // 兜底：万一量失败（返回的项数不足），用等分宽度补齐，宁可略有偏差
            // 也不要让选区少几个字符点不中。
            while ((int)run.m_charRights.size() < run.m_charCount)
            {
                const int k = (int)run.m_charRights.size() + 1;
                run.m_charRights.push_back(
                    p.rc.left + (p.rc.right - p.rc.left) * k / run.m_charCount);
            }
        }

        // run 此后不再使用，移入而不是复制（复制会把 m_charRights 再分配一遍）
        line.m_runs.push_back(std::move(run));
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

namespace {

// 按放置项把文字与表情画出来。调用前须已把字体、文字颜色与透明背景设进 hdc。
//   hdc：      目标设备上下文。
//   placed：   LayoutInlineContent 产出的放置项（坐标相对布局原点）。
//   ox / oy：  布局原点的绝对坐标。
//   filter：   非 NULL 时只画绝对坐标矩形与它相交的项（换样式重画时跳过区间矩形之外的项）；
//              NULL 时不按它筛。
//   inflatePx：按 hdc 当前裁剪区筛选时，把裁剪区左右各放宽多少像素再判断相交（取字号）：
//              斜体等字形会向左右伸出放置项矩形，放宽后伸进裁剪区的那部分照样画出来。
// 另外始终按 hdc 当前裁剪区筛选：完全落在裁剪区之外的项画了也不出像素，直接跳过。只重画一小
// 块区域（如长消息选区变化时只重画变化的几行）时，耗时因此只与这块区域里的项数有关。
void DrawPlacedItems(HDC hdc, const std::vector<InlinePlaced>& placed, int ox, int oy,
                     const RECT* filter, int inflatePx)
{
    RECT clip = { 0, 0, 0, 0 };
    const int clipKind = ::GetClipBox(hdc, &clip);
    if (clipKind == NULLREGION)
    {
        return;   // 裁剪区为空，什么都画不出来
    }
    const bool useClip = (clipKind != ERROR);
    if (useClip)
    {
        ::InflateRect(&clip, inflatePx, 0);
    }

    for (size_t i = 0; i < placed.size(); ++i)
    {
        const InlinePlaced& p = placed[i];
        RECT r = { p.rc.left + ox, p.rc.top + oy, p.rc.right + ox, p.rc.bottom + oy };
        RECT overlap;
        if (filter != NULL && !::IntersectRect(&overlap, &r, filter))
        {
            continue;
        }
        if (useClip && !::IntersectRect(&overlap, &r, &clip))
        {
            continue;
        }
        ++g_inlinePlacedDraws;
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
}

// 按样式建正文字体（微软雅黑），排版与绘制共用，保证两处的字形一致。
HFONT CreateInlineFont(const InlineTextStyle& st)
{
    return ::CreateFontW(
        -st.sizePx, 0, 0, 0,
        st.bold ? FW_SEMIBOLD : FW_NORMAL,
        st.italic ? TRUE : FALSE,
        st.underline ? TRUE : FALSE,
        st.strike ? TRUE : FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

} // anonymous namespace

SIZE LayoutInlineContentForStyle(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                                 int maxW, int faceSize, const InlineTextStyle& st,
                                 std::vector<InlinePlaced>* outPlaced, int maxLines)
{
    SIZE none = { 0, 0 };
    HFONT hFont = CreateInlineFont(st);
    if (hFont == NULL)
    {
        return none;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    const SIZE sz = LayoutInlineContent(hdc, segs, maxW, faceSize, outPlaced, maxLines);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz;
}

void DrawInlinePlaced(HDC hdc, const std::vector<InlinePlaced>& placed, const RECT& rc,
                      const InlineTextStyle& st)
{
    HFONT hFont = CreateInlineFont(st);
    if (hFont == NULL)
    {
        return;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    const int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, st.color);
    DrawPlacedItems(hdc, placed, rc.left, rc.top, NULL, st.sizePx);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 在 rc 内绘制"文字 + 内联表情"。st 决定字体 / 颜色（与 DrawGdiText 同口径）。
void DrawInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                       const RECT& rc, const InlineTextStyle& st, int faceSize,
                       int maxLines)
{
    std::vector<InlinePlaced> placed;
    LayoutInlineContentForStyle(hdc, segs, rc.right - rc.left, faceSize, st, &placed, maxLines);
    DrawInlinePlaced(hdc, placed, rc, st);
}

void DrawInlinePlacedRestyledRanges(HDC hdc,
                                    const std::vector<InlinePlaced>& placed,
                                    const SelLayout& layout,
                                    const std::vector<InlineCharRange>& ranges,
                                    const RECT& rcText,
                                    const InlineTextStyle& style)
{
    if (ranges.empty() || layout.m_lines.empty())
    {
        return;
    }
    HFONT hFont = CreateInlineFont(style);
    if (hFont == NULL)
    {
        return;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    const int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, style.color);

    for (std::size_t k = 0; k < ranges.size(); ++k)
    {
        if (ranges[k].m_end <= ranges[k].m_begin)
        {
            continue;
        }
        //区间可能跨行，故先取它占的若干矩形，逐个矩形裁剪重画。
        const std::vector<SelRect> rects = BuildSelectionRects(layout, ranges[k].m_begin, ranges[k].m_end);
        for (std::size_t r = 0; r < rects.size(); ++r)
        {
            const RECT clip = { rcText.left + rects[r].m_left, rcText.top + rects[r].m_top,
                                rcText.left + rects[r].m_right, rcText.top + rects[r].m_bottom };

            //保存原裁剪区。GetClipRgn 返回 1 表示原本有裁剪区、0 表示没有，
            //恢复时据此决定是还原那个区域还是清成"不裁剪"。
            HRGN saved = ::CreateRectRgn(0, 0, 0, 0);
            const int hadClip = ::GetClipRgn(hdc, saved);
            ::IntersectClipRect(hdc, clip.left, clip.top, clip.right, clip.bottom);

            //只画与裁剪区相交的放置项：其余项落在裁剪区之外，画了也不出像素。矩形左右各扩一个
            //字号再判断，斜体等向左右伸出放置项矩形的字形部分也照样重画，与整条重画的结果一致。
            //上下不扩：放置项高度即行高，字形不会伸出上下边，扩了反而会把相邻两行的项都算进来
            RECT filter = clip;
            ::InflateRect(&filter, style.sizePx, 0);
            DrawPlacedItems(hdc, placed, rcText.left, rcText.top, &filter, style.sizePx);

            ::SelectClipRgn(hdc, (hadClip == 1) ? saved : NULL);
            ::DeleteObject(saved);
        }
    }

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

void DrawInlineContentRestyledRanges(HDC hdc,
                                     const std::vector<balloonchat::ChatSegment>& segs,
                                     const SelLayout& layout,
                                     const std::vector<InlineCharRange>& ranges,
                                     const RECT& rcText,
                                     const InlineTextStyle& style,
                                     int faceSize)
{
    if (ranges.empty() || layout.m_lines.empty())
    {
        return;
    }
    //整条内容只排版一次，各区间共用。排版用的字体与 DrawInlineContent 按同一样式建的完全相同，
    //放置项的位置因此与底层正文逐像素对齐
    std::vector<InlinePlaced> placed;
    LayoutInlineContentForStyle(hdc, segs, rcText.right - rcText.left, faceSize, style, &placed);
    DrawInlinePlacedRestyledRanges(hdc, placed, layout, ranges, rcText, style);
}

void DrawInlineContentRestyledRange(HDC hdc,
                                    const std::vector<balloonchat::ChatSegment>& segs,
                                    const SelLayout& layout,
                                    int charBegin, int charEnd,
                                    const RECT& rcText,
                                    const InlineTextStyle& style,
                                    int faceSize)
{
    std::vector<InlineCharRange> ranges;
    ranges.push_back(InlineCharRange(charBegin, charEnd));
    DrawInlineContentRestyledRanges(hdc, segs, layout, ranges, rcText, style, faceSize);
}

unsigned long InlineLayoutCallCount()
{
    return g_inlineLayoutCalls;
}

unsigned long InlinePlacedDrawCount()
{
    return g_inlinePlacedDraws;
}

void ResetInlineProbeCounters()
{
    g_inlineLayoutCalls = 0;
    g_inlinePlacedDraws = 0;
}

} // namespace balloonchatwnd
