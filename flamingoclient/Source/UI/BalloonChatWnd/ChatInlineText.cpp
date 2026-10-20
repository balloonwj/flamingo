/**
 *  ChatInlineText.cpp
 *
 *  「文字 + 内联表情」排版与绘制引擎的实现。接口说明见 ChatInlineText.h。
 *
 *  本文件的排版逻辑是从 ChatBubble.cpp 的匿名 namespace <u>原样平移</u>过来的
 *  （2026-08-05，见 docs/消息记录内容交互方案.md §4），除了给 LayoutInlineContent /
 *  PlaceOverlongToken / DrawInlineContent 增加"最多排几行"这个入参之外，没有改动
 *  任何一行既有逻辑 —— 气泡侧一律传 0（不限行），渲染结果与平移前完全一致。
 *  2026-10-14 起增加带选项的 LayoutInlineContentEx（逐行行高、避头尾，docs/聊天气泡美化方案.md 第 1 节第 4 条），只有聊天窗的
 *  气泡使用；LayoutInlineContent 以缺省选项转调它，结果与之前完全相同。
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
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体

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
//
// 词元只记它在源串里的位置，不再各自复制出一份字符串（2026-10-04）：中文每个字单独成一个词元，
// 一万字的消息原先要为切词元分配上万个小字符串。测量与绘制用的文字就是源串里的
// [m_srcBegin, m_srcBegin + m_srcCount) 这一段；显式换行例外，由 m_newline 标出。
struct WrapToken
{
    int  m_srcBegin;   // 在源串里的起始下标
    int  m_srcCount;   // 在源串里占的字符数（\r\n 为 2，其余就是测量 / 绘制用的文字长度）
    bool m_newline;    // 是否显式换行（\r、\n 或 \r\n）；为 true 时不测量、不放置

    WrapToken()
        : m_srcBegin(0)
        , m_srcCount(0)
        , m_newline(false)
    {
    }
};

void TokenizeForWrap(const CString& s, std::vector<WrapToken>& out)
{
    const int n = s.GetLength();
    // 词元数不会超过字符数，一次留足容量，免得逐个追加时反复扩容搬移
    out.reserve(out.size() + (size_t)n);
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
            i = j;
        }
        else if (c == L'\r' || c == L'\n')
        {
            int j = i + 1;
            if (c == L'\r' && j < n && s[j] == L'\n')
            {
                ++j;   // \r\n 视作一个换行
            }
            tok.m_newline = true;
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
            i = j;
        }
        else
        {
            ++i;
        }
        tok.m_srcCount = i - tok.m_srcBegin;
        out.push_back(tok);
    }
}

// 一次排版内单字词元的测量结果与文字（2026-10-04）。中文每个字单独成一个词元，一条长消息里同一个
// 字反复出现；进程级的测宽缓存每查一次都要构造键、加锁、查两层表，这里先在本次排版内记下每个字
// 第一次查到的宽高，之后直接取。结果与每次都查缓存完全相同（缓存对同一个字总是返回同一个值）。
// 同一个字的文字也只建一份 CString，各放置项按引用计数共用，不再各自分配。
// 只在一次 LayoutInlineContent 调用内使用，不跨调用、不跨线程。

// SingleCharMemo 记录表的初始容量（项），必须是 2 的幂；短消息用不满，长消息按需翻倍
const size_t kCharMemoInitialCapacity = 64;
// SingleCharMemo 记录表的容量上限（项），必须是 2 的幂；一条消息里不同的字超过它的一半后不再记录
const size_t kCharMemoMaxCapacity = 8192;

class SingleCharMemo
{
public:
    SingleCharMemo()
        : m_used(0)
    {
    }

    // 取字符 ch 的宽高：本次排版内第一次遇到时查进程级缓存并记下，之后直接返回记下的值。
    // 记录表满了（本次排版里不同的字超过上限）就直接查缓存，不再记录。
    SIZE Extent(HDC hdc, const TextExtentFontKey& key, ChatTextExtentCache& cache, wchar_t ch)
    {
        Entry* e = Find(ch);
        if (e != NULL)
        {
            return e->m_size;
        }
        const SIZE size = cache.TextExtent(hdc, key, &ch, 1);
        Insert(ch, size);
        return size;
    }

    // 字符 ch 的文字。记录过的字返回共用的那一份；没记录（表满）时新建一份
    CString Text(wchar_t ch)
    {
        Entry* e = Find(ch);
        if (e != NULL)
        {
            return e->m_text;
        }
        return CString(ch);
    }

private:
    // 记录表的一项
    struct Entry
    {
        wchar_t m_ch;     // 字符；m_used 为 false 时无意义
        bool    m_used;   // 本项是否已经记录了一个字
        SIZE    m_size;   // 该字的宽高（逻辑单位）
        CString m_text;   // 该字的文字，各放置项共用

        Entry()
            : m_ch(0)
            , m_used(false)
        {
            m_size.cx = 0;
            m_size.cy = 0;
        }
    };

    // 开放定址、线性探测：查找字符 ch 所在的项，没有记录时返回 NULL
    Entry* Find(wchar_t ch)
    {
        if (m_table.empty())
        {
            return NULL;
        }
        const size_t mask = m_table.size() - 1;
        for (size_t i = (size_t)ch & mask; ; i = (i + 1) & mask)
        {
            Entry& e = m_table[i];
            if (!e.m_used)
            {
                return NULL;
            }
            if (e.m_ch == ch)
            {
                return &e;
            }
        }
    }

    // 记录一个新字。装填超过一半时容量翻倍并重新放置；已到上限则不记录
    void Insert(wchar_t ch, const SIZE& size)
    {
        if (m_table.empty())
        {
            m_table.resize(kCharMemoInitialCapacity);
        }
        if ((m_used + 1) * 2 > m_table.size())
        {
            if (m_table.size() >= kCharMemoMaxCapacity)
            {
                return;
            }
            std::vector<Entry> old;
            old.swap(m_table);
            m_table.resize(old.size() * 2);
            m_used = 0;
            for (size_t k = 0; k < old.size(); ++k)
            {
                if (old[k].m_used)
                {
                    Place(old[k].m_ch, old[k].m_size, old[k].m_text);
                }
            }
        }
        Place(ch, size, CString(ch));
    }

    // 把一项放进当前的表（调用方已保证表里还有空位）
    void Place(wchar_t ch, const SIZE& size, const CString& textOfCh)
    {
        const size_t mask = m_table.size() - 1;
        size_t i = (size_t)ch & mask;
        while (m_table[i].m_used)
        {
            i = (i + 1) & mask;
        }
        m_table[i].m_ch = ch;
        m_table[i].m_used = true;
        m_table[i].m_size = size;
        m_table[i].m_text = textOfCh;
        ++m_used;
    }

    std::vector<Entry> m_table;   // 记录表，容量为 0 或 2 的幂；第一次记录时才分配
    size_t             m_used;    // 已记录的字数
};

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

// 避头标点：折行时不放在行首（docs/聊天气泡美化方案.md 第 1 节第 4 条）。中文句读、右引号、右括号、省略号，以及对应的 ASCII 与全角标点
const wchar_t kNoLineStartPunct[] = L"。，、；：？！）》」』】〕〉〗”’…．］｝％,.;:?!)]}%";
// 避尾标点：折行时不留在行尾。左引号、左括号，以及对应的 ASCII 与全角标点
const wchar_t kNoLineEndPunct[] = L"（《「『【〔〈〖“‘［｛([{";

// 字符 c 是否为避头标点
bool IsNoLineStartPunct(wchar_t c)
{
    return c != 0 && ::wcschr(kNoLineStartPunct, c) != NULL;
}

// 字符 c 是否为避尾标点
bool IsNoLineEndPunct(wchar_t c)
{
    return c != 0 && ::wcschr(kNoLineEndPunct, c) != NULL;
}

// 量一个词元的宽高：单字走本次排版内的记录表，其余走进程级测宽缓存（与 LayoutInlineContentEx 放置时的口径相同）
SIZE TokenExtent(HDC hdc, const TextExtentFontKey& fontKey, ChatTextExtentCache& extentCache, SingleCharMemo& charMemo,
                 LPCWSTR text, int len)
{
    return (len == 1) ? charMemo.Extent(hdc, fontKey, extentCache, text[0])
                      : extentCache.TextExtent(hdc, fontKey, text, len);
}

// 避头尾：当前词元 tokens[ti] 在本行放得下（放完后游标到 xAfter），是否仍应当把它换到下一行。
//   一、避头：紧随其后的避头标点（可能连着几个）在本行放不下，就连同当前词元一起换行，标点不会落在下一行行首；
//   二、避尾：当前词元以避尾标点结尾，而下一个词元在本行放不下，就提前换行，左括号不会留在本行行尾。
// 只看同一段文字里的后续词元（表情另成一段，不是标点）；显式换行与空白不参与判断。
bool KinsokuWantsWrap(HDC hdc, const TextExtentFontKey& fontKey, ChatTextExtentCache& extentCache,
                      SingleCharMemo& charMemo, const std::vector<WrapToken>& tokens, size_t ti, LPCWSTR segText,
                      int xAfter, int maxW)
{
    const WrapToken& cur = tokens[ti];
    const LPCWSTR curText = segText + cur.m_srcBegin;
    if (!IsNoLineStartPunct(curText[0]))
    {
        int punctW = 0;
        size_t k = ti + 1;
        while (k < tokens.size() && !tokens[k].m_newline)
        {
            const LPCWSTR nextText = segText + tokens[k].m_srcBegin;
            if (!IsNoLineStartPunct(nextText[0]))
            {
                break;
            }
            punctW += TokenExtent(hdc, fontKey, extentCache, charMemo, nextText, tokens[k].m_srcCount).cx;
            ++k;
        }
        if (k > ti + 1 && xAfter + punctW > maxW)
        {
            return true;
        }
    }
    if (IsNoLineEndPunct(curText[cur.m_srcCount - 1]) && ti + 1 < tokens.size() && !tokens[ti + 1].m_newline)
    {
        const LPCWSTR nextText = segText + tokens[ti + 1].m_srcBegin;
        if (nextText[0] != L' ' && nextText[0] != L'\t')
        {
            const int nextW = TokenExtent(hdc, fontKey, extentCache, charMemo, nextText, tokens[ti + 1].m_srcCount).cx;
            if (xAfter + nextW > maxW)
            {
                return true;
            }
        }
    }
    return false;
}

} // 匿名 namespace（本文件私有的排版细节）

CString FaceImagePathFromId(int faceId)
{
    return FacePathForId(faceId);
}

SIZE LayoutInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                         int maxW, int faceSize, std::vector<InlinePlaced>* outPlaced,
                         int maxLines, bool* outTruncated)
{
    //缺省选项就是原有规则：每行同高、不处理标点
    const InlineLayoutOptions defaults;
    return LayoutInlineContentEx(hdc, segs, maxW, faceSize, defaults, outPlaced, maxLines, outTruncated);
}

// 把消息段序列按 maxW 折行排版。调用前须已把目标字体 select 进 hdc（测量按该字体）。
//   opts：     排版选项；缺省值时与原 LayoutInlineContent 完全相同。
//   outPlaced：非 NULL 时填入各放置项（绘制用）；NULL 时只测尺寸。
//   返回：内容整体尺寸（不含内边距）。
//
// 逐行行高（opts.m_textLineH > 0）的做法：折行照旧按一个固定的名义行高推进（行号 = 纵坐标 / 名义行高），同时记下
// 哪些行放了表情；排完之后按每行的实际行高（纯文字行取 m_textLineH，含表情的行再与表情格比较）算出各行的上沿，
// 把放置项的纵坐标改写过去。只测尺寸（outPlaced 为 NULL）时同样按记下的表情行算高度。
SIZE LayoutInlineContentEx(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                           int maxW, int faceSize, const InlineLayoutOptions& opts,
                           std::vector<InlinePlaced>* outPlaced, int maxLines, bool* outTruncated)
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
    // 名义行高：原有规则下就是每一行的行高；逐行计算时只用来推进折行游标，取各种行高里最大的那个
    int lineH = (textH > faceSize) ? textH : faceSize;
    const bool perLineHeight = (opts.m_textLineH > 0);
    if (perLineHeight && opts.m_textLineH > lineH)
    {
        lineH = opts.m_textLineH;
    }
    // 逐行计算行高时记下哪些行放了表情（下标为行号，非 0 表示有）；放置项从 outPlaced 的这个位置开始是本次排出的
    std::vector<char> faceLines;
    const size_t placedBegin = (outPlaced != NULL) ? outPlaced->size() : 0;
    //逐词元测宽走缓存：中文每个字单独成一个词元，逐个调 GDI 是排版的主要开销
    //（50 条消息的首次测量里约占一半以上）。缓存键取自当前选入 hdc 的字体，每次排版取一次
    const TextExtentFontKey fontKey = TextExtentFontKey::FromDc(hdc);
    ChatTextExtentCache& extentCache = ChatTextExtentCache::Inst();
    SingleCharMemo charMemo;   // 本次排版内单字词元的宽高与文字，见 SingleCharMemo

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
            if (perLineHeight)
            {
                const size_t line = (size_t)(y / lineH);
                if (faceLines.size() <= line)
                {
                    faceLines.resize(line + 1, 0);
                }
                faceLines[line] = 1;
            }
            x += faceSize;
            ++charPos;
        }
        else
        {
            std::vector<WrapToken> tokens;
            TokenizeForWrap(seg.m_strText, tokens);
            const LPCWSTR segText = seg.m_strText;   // 词元文字就是这段源串里的一截
            if (outPlaced != NULL)
            {
                // 放置项一般一个词元一项（超长词元被切开时才多出几项），按词元数留足容量
                outPlaced->reserve(outPlaced->size() + tokens.size());
            }
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
                const int tokCharBegin = charPos;
                charPos += wt.m_srcCount;   // 无论放不放置，游标都要推进

                if (wt.m_newline)   // 显式换行
                {
                    if (x > maxLineW)
                    {
                        maxLineW = x;
                    }
                    x = 0;
                    y += lineH;
                    continue;
                }
                const LPCWSTR tokText = segText + wt.m_srcBegin;
                const int     tokLen = wt.m_srcCount;
                const SIZE ts = (tokLen == 1)
                                ? charMemo.Extent(hdc, fontKey, extentCache, tokText[0])
                                : extentCache.TextExtent(hdc, fontKey, tokText, tokLen);
                const bool isSpace = (tokText[0] == L' ' || tokText[0] == L'\t');
                // 避头尾（opts.m_kinsoku）：本词元放得下，但放下之后会让紧随的标点落到下一行行首、或让左括号留在行尾时，
                // 把本词元提前换到下一行
                const bool kinsokuWrap = opts.m_kinsoku && x > 0 && !isSpace && x + ts.cx <= maxW
                    && KinsokuWantsWrap(hdc, fontKey, extentCache, charMemo, tokens, ti, segText, x + ts.cx, maxW);
                if (x > 0 && (x + ts.cx > maxW || kinsokuWrap))
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
                    PlaceOverlongToken(hdc, CString(tokText, tokLen), tokCharBegin, maxW, lineH,
                                       outPlaced, x, y, maxLineW, maxLines, truncated);
                    continue;
                }
                if (outPlaced != NULL)
                {
                    InlinePlaced p;
                    p.isFace = false;
                    p.faceId = -1;
                    p.text = (tokLen == 1) ? charMemo.Text(tokText[0]) : CString(tokText, tokLen);
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
    if (!perLineHeight)
    {
        return size;
    }

    // 逐行行高：纯文字行取 m_textLineH（字体本身更高时取字体行高），含表情的行再与表情格边长比较取较大的一个
    const int plainH = (opts.m_textLineH > textH) ? opts.m_textLineH : textH;
    const int faceLineH = (plainH > faceSize) ? plainH : faceSize;
    const size_t lineCount = (size_t)(y / lineH) + 1;
    std::vector<int> lineTop(lineCount + 1, 0);
    for (size_t i = 0; i < lineCount; ++i)
    {
        const bool hasFace = (i < faceLines.size() && faceLines[i] != 0);
        lineTop[i + 1] = lineTop[i] + (hasFace ? faceLineH : plainH);
    }
    if (outPlaced != NULL)
    {
        for (size_t i = placedBegin; i < outPlaced->size(); ++i)
        {
            InlinePlaced& p = (*outPlaced)[i];
            // 名义行高下表情格的上沿落在本行之内（行高不小于表情格），整除即得行号
            size_t line = (size_t)(p.rc.top / lineH);
            if (line >= lineCount)
            {
                line = lineCount - 1;
            }
            const int rowTop = lineTop[line];
            const int rowH = lineTop[line + 1] - rowTop;
            if (p.isFace)
            {
                p.rc.top = rowTop + (rowH - faceSize) / 2;
                p.rc.bottom = p.rc.top + faceSize;
            }
            else
            {
                p.rc.top = rowTop;
                p.rc.bottom = rowTop + rowH;
            }
        }
    }
    size.cy = lineTop[lineCount];
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

    // 先数出行数与每行的段数，一次留足容量（2026-10-04）。每一段都带一个逐字符右边界数组，数组随行内
    // 段数增长而扩容时，已有的段要整体搬移一遍；调试版运行库下每个 std::vector 的构造与搬移都伴随堆分配，
    // 一万字的消息有四千来段，这部分开销比测宽本身还大。分行的判定与下面建模时完全相同：top 变了就是换了一行。
    std::vector<size_t> runsPerLine;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        if (i == 0 || placed[i].rc.top != placed[i - 1].rc.top)
        {
            runsPerLine.push_back(0);
        }
        ++runsPerLine.back();
    }
    out.m_lines.reserve(runsPerLine.size());

    for (size_t i = 0; i < placed.size(); ++i)
    {
        const InlinePlaced& p = placed[i];

        // 放置项按文档顺序产出，top 变了就是换了一行。
        if (out.m_lines.empty() || out.m_lines.back().m_top != p.rc.top)
        {
            out.m_lines.emplace_back();
            SelLine& newLine = out.m_lines.back();
            newLine.m_top    = p.rc.top;
            newLine.m_bottom = p.rc.bottom;
            newLine.m_runs.reserve(runsPerLine[out.m_lines.size() - 1]);
        }
        SelLine& line = out.m_lines.back();
        // 同一行里表情格比文字矮 / 高时，行高取最大，保证选区高亮盖住整行。
        if (p.rc.bottom > line.m_bottom)
        {
            line.m_bottom = p.rc.bottom;
        }

        // 段直接在行内数组里原地构造（容量已经留足，不会搬移），不再先建一个临时对象再移进去
        line.m_runs.emplace_back();
        SelRun& run = line.m_runs.back();
        run.m_charBegin = p.charBegin;
        run.m_charCount = p.charCount;
        run.m_left      = p.rc.left;
        run.m_right     = p.rc.right;

        if (p.isFace || p.charCount <= 0)
        {
            run.m_charRights.push_back(p.rc.right);
            run.m_charCount = 1;
        }
        else if (p.charCount == 1 && p.text.GetLength() == 1)
        {
            // 只有一个字符的文字段（中文每个字单独成段，长消息里这类段占大多数）：它的右边界就是
            // 放置矩形的右边界 —— 排版时正是按这个字符的宽度算出来的，不必再查一次逐字符宽度
            run.m_charRights.push_back(p.rc.right);
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
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
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

SIZE LayoutInlineContentForStyle(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                                 int maxW, int faceSize, const InlineTextStyle& st,
                                 const InlineLayoutOptions& opts,
                                 std::vector<InlinePlaced>* outPlaced, int maxLines)
{
    SIZE none = { 0, 0 };
    HFONT hFont = CreateInlineFont(st);
    if (hFont == NULL)
    {
        return none;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    const SIZE sz = LayoutInlineContentEx(hdc, segs, maxW, faceSize, opts, outPlaced, maxLines);
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
