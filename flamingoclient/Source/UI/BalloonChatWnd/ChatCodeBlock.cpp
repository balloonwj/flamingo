/**
 *  ChatCodeBlock.cpp
 *
 *  代码片段卡片的排版与绘制，见 ChatCodeBlock.h。
 *
 *  排版按逻辑行逐行进行：先把这一行的着色区间落到每个字符上，再展开 Tab、查出等宽字体缺字形的字符、
 *  分段测量出每个字符的右边位置，最后按可用宽度折行、切成同字体同颜色的文字段。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖，与同目录 ChatBubble.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>

#include <string>
#include <vector>

#include "ChatCodeBlock.h"
#include "ChatPaintUtil.h"            // AddRoundRect / DrawGdiText / MeasureTextWidth
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找；UiFontName：回退字体

namespace balloonchatwnd {

namespace {

using chatpaint::AddRoundRect;
using chatpaint::DrawGdiText;
using chatpaint::MeasureTextWidth;

// ---- 显示选项的默认值 ----
const int kDefaultFontPx         = 13;   // 代码字号（像素），比聊天正文的 14 小一号，等宽字体显得不那么挤
const int kDefaultTabColumns     = 4;    // Tab 宽度（列）
const int kMaxTabColumns         = 16;   // Tab 宽度上限（列），超过按默认值
const int kDefaultCollapsedLines = 15;   // 折叠时最多显示的行数

// 等宽字体。Windows 7 起系统自带；万一没有，CreateFontW 按 FIXED_PITCH | FF_MODERN 换成别的等宽字体
const wchar_t* const kCodeFontName = L"Consolas";

// ---- 几何常量（逻辑像素）----
const Gdiplus::REAL kCardRadius = 8.0f;   // 卡片圆角半径
const int kHeaderH          = 30;   // 顶部一行（语言名、行数、复制）的高度
const int kPadH             = 12;   // 卡片左右内边距
const int kHeaderTextPx     = 12;   // 顶部与底栏文字的字号
const int kHeaderGap        = 6;    // 语言名与行数说明之间的间距
const int kHeaderMinGap     = 16;   // 行数说明与「复制」之间至少留出的间距
const int kCopyHitPad       = 4;    // 「复制」点击区在文字左右各放宽的像素
const int kBodyPadTop       = 8;    // 代码区上内边距
const int kBodyPadBottom    = 10;   // 代码区下内边距
const int kGutterGap        = 12;   // 行号与代码之间的间距
const int kRowExtra         = 5;    // 视觉行高在字体高度之外多出的行距
const int kFooterH          = 30;   // 底栏「展开全部 / 收起」的高度
const int kMinCardWidth     = 240;  // 卡片最小宽度
const int kMinTextColumns   = 8;    // 代码区至少能放下几列字符；卡片再窄也按这个宽度排版
const int kWrapIndentColumns = 2;   // 续行在原缩进之外再缩进的列数，表明它是上一行的延续
const int kWrapPreferPercent = 60;  // 折行时，最后一个空格之后的位置不少于可用宽度的这个百分比，就在空格之后折行
const int kDecimalBase      = 10;   // 计算行号位数用的进制

// ---- 配色 ----
const Gdiplus::Color kCardBorderColor(255, 0xE3, 0xE1, 0xDC);   // 卡片描边
const Gdiplus::Color kCardBodyColor(255, 0xFF, 0xFF, 0xFF);     // 代码区底色
const Gdiplus::Color kCardHeaderColor(255, 0xF3, 0xF2, 0xEF);   // 顶部与底栏底色（与对方气泡同色）
const COLORREF kSeparatorColor   = RGB(0xE3, 0xE1, 0xDC);   // 顶部、底栏与代码区之间的分隔线
const COLORREF kTitleColor       = RGB(0x55, 0x55, 0x55);   // 语言名
const COLORREF kInfoColor        = RGB(0x8A, 0x8A, 0x8A);   // 行数说明、「已复制」
const COLORREF kLinkColor        = RGB(0xFF, 0x7A, 0x45);   // 「复制」与底栏文字（主题橙，与其它卡片的按钮一致）
const COLORREF kLinkHoverColor   = RGB(0xCC, 0x62, 0x37);   // 鼠标悬停时的链接文字
const COLORREF kLineNumberColor  = RGB(0xA8, 0xA6, 0xA2);   // 行号
const COLORREF kNoteLineColor    = RGB(0xFF, 0xF4, 0xD0);   // 被批注的行的底色（与 CodeNotesPane.h 的 kCodeNoteLineBg 相同）
const COLORREF kNoteMarkColor    = RGB(0xF0, 0x9A, 0x3E);   // 被批注的行左侧的竖条（与代码查看窗行号栏的竖条相同）
const int kNoteBarLeft = 4;   // 竖条离卡片左边的距离（像素）
const int kNoteBarW    = 3;   // 竖条宽度（像素）
const COLORREF kEllipsisColor    = RGB(0x8A, 0x8A, 0x8A);   // 关闭折行时行尾的省略号
const COLORREF kEllipsisBgColor  = RGB(0xFF, 0xFF, 0xFF);   // 省略号底色，与代码区底色相同，盖住被截断的文字

// 各着色种类的文字颜色，下标为 codesnippet::CodeTokenKind（参照 Visual Studio 浅色主题）
const COLORREF kTokenColors[codesnippet::code_token_kind_count] =
{
    RGB(0x1F, 0x1F, 0x1F),   // code_token_plain：普通文字
    RGB(0x00, 0x00, 0xFF),   // code_token_keyword：关键字
    RGB(0x26, 0x7F, 0x99),   // code_token_type：类型名
    RGB(0xA3, 0x15, 0x15),   // code_token_string：字符串
    RGB(0x09, 0x86, 0x58),   // code_token_number：数字
    RGB(0x00, 0x80, 0x00),   // code_token_comment：注释
    RGB(0x80, 0x80, 0x80),   // code_token_preprocessor：预处理指令、注解、宏
    RGB(0x79, 0x5E, 0x26),   // code_token_function：函数名
    RGB(0x80, 0x00, 0x00),   // code_token_tag：XML / HTML 标签
    RGB(0x04, 0x51, 0xA5),   // code_token_attr：XML / HTML 属性名、JSON 键名
    RGB(0x00, 0x10, 0x80),   // code_token_variable：$ 变量
};

// 关闭折行时行尾的省略号
const wchar_t* const kEllipsis = L"\x2026";

//按字号建代码字体
HFONT CreateCodeFont(int fontPx)
{
    return ::CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, kCodeFontName);
}

//按字号建回退字体（界面字体），画等宽字体缺字形的字符，如中文注释
HFONT CreateFallbackFont(int fontPx)
{
    return ::CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
}

//显示选项里的 Tab 宽度，越界时取默认值
int EffectiveTabColumns(const CodeBlockOptions& options)
{
    if (options.m_tabColumns < 1 || options.m_tabColumns > kMaxTabColumns)
        return kDefaultTabColumns;
    return options.m_tabColumns;
}

//一个正整数的十进制位数
int DecimalDigits(int value)
{
    int digits = 1;
    while (value >= kDecimalBase)
    {
        value /= kDecimalBase;
        ++digits;
    }
    return digits;
}

//空白（空格、Tab、换行）
bool IsBlankChar(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\n';
}

/**
 *  排版一行代码时用到的临时数据。各数组按行复用，避免每行重新分配。
 */
struct LineScratch
{
    std::vector<int>  m_kinds;       //原行每个字符的着色种类
    std::vector<int>  m_srcIndex;    //展开 Tab 后每个字符对应原行里的下标
    std::vector<WORD> m_glyphs;      //等宽字体里每个字符的字形号；缺字形时为 0xFFFF
    std::vector<char> m_fallback;    //每个字符是否用回退字体（1 是、0 否）
    std::vector<int>  m_charRight;   //每个字符右边距行首的像素距离
    std::vector<int>  m_extents;     //测量一段文字时 GetTextExtentExPointW 填回的累计宽度
    std::vector<int>  m_starts;      //各视觉行的起始字符下标
};

//把与 [lineBegin, lineEnd) 相交的着色区间落到这一行的每个字符上；cursor 是第一个可能相交的区间下标，随行推进
void ApplyTokensToLine(const std::vector<codesnippet::CodeToken>& tokens, size_t lineBegin, size_t lineEnd,
                       size_t& cursor, std::vector<int>& kinds)
{
    kinds.assign(lineEnd - lineBegin, codesnippet::code_token_plain);
    //跳过已经在本行之前结束的区间；跨多行的区间（块注释）在它结束之前一直保留
    while (cursor < tokens.size() && static_cast<size_t>(tokens[cursor].m_begin + tokens[cursor].m_length) <= lineBegin)
        ++cursor;
    for (size_t t = cursor; t < tokens.size(); ++t)
    {
        const size_t tokenBegin = static_cast<size_t>(tokens[t].m_begin);
        const size_t tokenEnd = tokenBegin + static_cast<size_t>(tokens[t].m_length);
        if (tokenBegin >= lineEnd)
            break;
        const size_t from = (tokenBegin > lineBegin) ? tokenBegin : lineBegin;
        const size_t to = (tokenEnd < lineEnd) ? tokenEnd : lineEnd;
        for (size_t k = from; k < to; ++k)
            kinds[k - lineBegin] = tokens[t].m_kind;
    }
}

//测量展开后的一行：查出缺字形的字符改用回退字体，分段量出每个字符的右边位置
void MeasureLine(HDC hdc, HFONT codeFont, HFONT fallbackFont, const std::wstring& disp, LineScratch& scratch)
{
    const int n = static_cast<int>(disp.size());
    scratch.m_fallback.assign(n, 0);
    scratch.m_charRight.assign(n, 0);
    if (n == 0)
        return;

    ::SelectObject(hdc, codeFont);
    scratch.m_glyphs.assign(n, 0);
    if (::GetGlyphIndicesW(hdc, disp.c_str(), n, &scratch.m_glyphs[0], GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR)
    {
        for (int k = 0; k < n; ++k)
        {
            if (scratch.m_glyphs[k] == 0xFFFF)
                scratch.m_fallback[k] = 1;
        }
    }

    //同一种字体的连续字符一次量完：GetTextExtentExPointW 填回前 1、2、3……个字符的累计宽度
    int x = 0;
    int k = 0;
    while (k < n)
    {
        const int segBegin = k;
        const char fallback = scratch.m_fallback[k];
        while (k < n && scratch.m_fallback[k] == fallback)
            ++k;
        const int segLen = k - segBegin;
        ::SelectObject(hdc, fallback != 0 ? fallbackFont : codeFont);
        scratch.m_extents.assign(segLen, 0);
        SIZE size = { 0, 0 };
        ::GetTextExtentExPointW(hdc, disp.c_str() + segBegin, segLen, 0, NULL, &scratch.m_extents[0], &size);
        for (int j = 0; j < segLen; ++j)
            scratch.m_charRight[segBegin + j] = x + scratch.m_extents[j];
        x += scratch.m_extents[segLen - 1];
    }
}

//一段文字是否全是空格（Tab 已展开）；这样的段不必绘制
bool IsAllSpaces(const std::wstring& disp, int begin, int end)
{
    for (int k = begin; k < end; ++k)
    {
        if (disp[k] != L' ')
            return false;
    }
    return true;
}

} // anonymous namespace

CodeBlockOptions::CodeBlockOptions()
    : m_fontPx(kDefaultFontPx)
    , m_tabColumns(kDefaultTabColumns)
    , m_collapsedLines(kDefaultCollapsedLines)
    , m_expanded(false)
    , m_lineNumbers(true)
    , m_wrap(true)
    , m_copied(false)
    , m_footerOpensViewer(false)
{
}

CodeBlockLayout::CodeBlockLayout()
    : m_fontPx(kDefaultFontPx)
    , m_rowHeight(0)
    , m_baseline(0)
    , m_bodyTop(0)
    , m_rowCount(0)
    , m_shownLines(0)
    , m_totalLines(0)
    , m_collapsible(false)
    , m_collapsed(false)
    , m_copied(false)
    , m_gutterRight(0)
    , m_textLeft(0)
    , m_textRight(0)
{
    m_size.cx = 0;
    m_size.cy = 0;
    ::SetRectEmpty(&m_copyRect);
    ::SetRectEmpty(&m_footerRect);
}

std::wstring NormalizeCodeNewlines(const std::wstring& raw)
{
    std::wstring s;
    s.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const wchar_t c = raw[i];
        if (c == L'\r')
        {
            s += L'\n';
            if (i + 1 < raw.size() && raw[i + 1] == L'\n')
                ++i;   //\r\n 只算一个换行
            continue;
        }
        s += c;
    }

    //去掉开头只含空白的行：从第一个非空白字符退回到它所在行的行首，保留这一行自身的缩进
    size_t first = 0;
    while (first < s.size() && IsBlankChar(s[first]))
        ++first;
    if (first == s.size())
        return std::wstring();
    size_t lineStart = s.rfind(L'\n', first);
    lineStart = (lineStart == std::wstring::npos) ? 0 : lineStart + 1;

    //去掉末尾只含空白的行：截到最后一个非空白字符所在行的行尾
    size_t last = s.size();
    while (last > 0 && IsBlankChar(s[last - 1]))
        --last;
    size_t lineEnd = s.find(L'\n', last);
    if (lineEnd == std::wstring::npos)
        lineEnd = s.size();
    return s.substr(lineStart, lineEnd - lineStart);
}

int CodeCharColumns(wchar_t c)
{
    //谚文字母、中日韩部首到彝文、谚文音节、兼容汉字、竖排与兼容形式、全角字符：在等宽排版里占两列
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3)
        || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60)
        || (c >= 0xFFE0 && c <= 0xFFE6))
    {
        return 2;
    }
    return 1;
}

std::wstring ExpandCodeTabs(const std::wstring& line, int tabColumns, std::vector<int>* srcIndex)
{
    if (tabColumns < 1)
        tabColumns = 1;
    std::wstring out;
    out.reserve(line.size());
    if (srcIndex != NULL)
    {
        srcIndex->clear();
        srcIndex->reserve(line.size());
    }
    int column = 0;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const wchar_t c = line[i];
        if (c == L'\t')
        {
            const int spaces = tabColumns - (column % tabColumns);
            for (int k = 0; k < spaces; ++k)
            {
                out += L' ';
                if (srcIndex != NULL)
                    srcIndex->push_back(static_cast<int>(i));
            }
            column += spaces;
            continue;
        }
        out += c;
        if (srcIndex != NULL)
            srcIndex->push_back(static_cast<int>(i));
        column += CodeCharColumns(c);
    }
    return out;
}

void ComputeCodeWrapStarts(const std::vector<int>& charRight, const std::wstring& text, int firstWidth,
                           int contWidth, std::vector<int>& outStarts)
{
    outStarts.clear();
    outStarts.push_back(0);
    const int n = static_cast<int>(charRight.size());
    if (n == 0)
        return;
    if (firstWidth < 1)
        firstWidth = 1;
    if (contWidth < 1)
        contWidth = 1;
    const bool canPreferSpace = (static_cast<int>(text.size()) >= n);

    int start = 0;
    int avail = firstWidth;
    for (;;)
    {
        const int base = (start > 0) ? charRight[start - 1] : 0;
        //每个视觉行至少放一个字符，之后能放多少放多少
        int end = start + 1;
        while (end < n && charRight[end] - base <= avail)
            ++end;
        if (end >= n)
            break;
        //优先在最后一个空格之后折行，只要那个位置不至于让这一行太短
        if (canPreferSpace)
        {
            for (int k = end - 1; k > start; --k)
            {
                if (text[k - 1] == L' ' && text[k] != L' ')
                {
                    if ((charRight[k - 1] - base) * 100 >= avail * kWrapPreferPercent)
                        end = k;
                    break;
                }
            }
        }
        outStarts.push_back(end);
        start = end;
        avail = contWidth;
    }
}

CodeBlockContent PrepareCodeBlockContent(const std::wstring& raw, int lang,
                                         const std::vector<codesnippet::CodeNote>& notes)
{
    CodeBlockContent content;
    content.m_code = NormalizeCodeNewlines(raw);
    content.m_lang = (lang >= 0 && lang < codesnippet::code_lang_count) ? lang : codesnippet::code_lang_plain;
    codesnippet::TokenizeCode(content.m_code, content.m_lang, content.m_tokens);
    if (!content.m_code.empty())
    {
        content.m_lineCount = 1;
        for (size_t i = 0; i < content.m_code.size(); ++i)
        {
            if (content.m_code[i] == L'\n')
                ++content.m_lineCount;
        }
    }
    //被批注的行：按代码行数整理之后换成从 0 起的行号
    if (!notes.empty() && content.m_lineCount > 0)
    {
        const std::vector<codesnippet::CodeNote> valid = codesnippet::SanitizeCodeNotes(notes, content.m_lineCount);
        for (size_t i = 0; i < valid.size(); ++i)
            content.m_noteLines.push_back(std::make_pair(valid[i].m_from - 1, valid[i].m_to - 1));
        content.m_newNoteCount = codesnippet::CountNewCodeNotes(valid);
    }
    return content;
}

void LayoutCodeBlock(HDC hdc, const CodeBlockContent& content, const CodeBlockOptions& options, int maxWidth,
                     CodeBlockLayout& out)
{
    out = CodeBlockLayout();
    const int fontPx = (options.m_fontPx > 0) ? options.m_fontPx : kDefaultFontPx;
    const int tabColumns = EffectiveTabColumns(options);
    out.m_fontPx = fontPx;
    out.m_totalLines = content.m_lineCount;

    //顶部与底栏的文字
    const wchar_t* displayName = codesnippet::CodeLanguageDisplayName(content.m_lang);
    out.m_title = (content.m_lang == codesnippet::code_lang_plain) ? TR(displayName) : displayName;
    //带批注时行数说明后加「· N 条批注」（docs/代码片段方案.md 第 10.5 节）；追加批注的消息再写出新批注的条数（第 11.3 节）
    if (content.m_noteLines.empty())
        out.m_info = TRF(L"{0} 行", static_cast<unsigned int>(content.m_lineCount));
    else if (content.m_newNoteCount > 0)
        out.m_info = TRF(L"{0} 行 · {1} 条批注，其中 {2} 条是新的", static_cast<unsigned int>(content.m_lineCount),
                         static_cast<unsigned int>(content.m_noteLines.size()),
                         static_cast<unsigned int>(content.m_newNoteCount));
    else
        out.m_info = TRF(L"{0} 行 · {1} 条批注", static_cast<unsigned int>(content.m_lineCount),
                         static_cast<unsigned int>(content.m_noteLines.size()));
    out.m_copied = options.m_copied;
    out.m_copyText = options.m_copied ? TR(L"已复制") : TR(L"复制");
    out.m_collapsible = (options.m_collapsedLines > 0 && content.m_lineCount > options.m_collapsedLines);
    //聊天窗里底栏用来打开查看窗，卡片始终保持折叠
    out.m_collapsed = (out.m_collapsible && (options.m_footerOpensViewer || !options.m_expanded));
    out.m_shownLines = out.m_collapsed ? options.m_collapsedLines : content.m_lineCount;
    if (out.m_collapsible && options.m_footerOpensViewer)
    {
        out.m_footerText = TRF(L"查看全部（共 {0} 行）", static_cast<unsigned int>(content.m_lineCount));
    }
    else if (out.m_collapsible)
    {
        out.m_footerText = out.m_collapsed
            ? TRF(L"展开全部（共 {0} 行）", static_cast<unsigned int>(content.m_lineCount))
            : std::wstring(TR(L"收起"));
    }

    HFONT codeFont = CreateCodeFont(fontPx);
    HFONT fallbackFont = CreateFallbackFont(fontPx);
    HGDIOBJ oldFont = ::SelectObject(hdc, codeFont);
    TEXTMETRICW tm;
    ::ZeroMemory(&tm, sizeof(tm));
    ::GetTextMetricsW(hdc, &tm);
    const int charWidth = (tm.tmAveCharWidth > 0) ? tm.tmAveCharWidth : fontPx / 2;
    out.m_rowHeight = tm.tmHeight + kRowExtra;
    out.m_baseline = kRowExtra / 2 + tm.tmAscent;
    out.m_bodyTop = kHeaderH + kBodyPadTop;

    //行号栏按总行数的位数定宽，展开与折叠时宽度一致
    int gutterWidth = 0;
    if (options.m_lineNumbers && content.m_lineCount > 0)
        gutterWidth = DecimalDigits(content.m_lineCount) * charWidth;
    out.m_gutterRight = (gutterWidth > 0) ? kPadH + gutterWidth : 0;
    out.m_textLeft = kPadH + ((gutterWidth > 0) ? gutterWidth + kGutterGap : 0);
    int availText = maxWidth - out.m_textLeft - kPadH;
    if (availText < charWidth * kMinTextColumns)
        availText = charWidth * kMinTextColumns;

    LineScratch scratch;
    size_t tokenCursor = 0;
    size_t lineBegin = 0;
    int row = 0;
    int widest = 0;
    for (int li = 0; li < out.m_shownLines; ++li)
    {
        size_t lineEnd = content.m_code.find(L'\n', lineBegin);
        if (lineEnd == std::wstring::npos)
            lineEnd = content.m_code.size();
        const std::wstring line = content.m_code.substr(lineBegin, lineEnd - lineBegin);
        ApplyTokensToLine(content.m_tokens, lineBegin, lineEnd, tokenCursor, scratch.m_kinds);
        lineBegin = lineEnd + 1;

        const std::wstring disp = ExpandCodeTabs(line, tabColumns, &scratch.m_srcIndex);
        const int n = static_cast<int>(disp.size());
        out.m_lineFirstRow.push_back(row);
        if (n == 0)
        {
            ++row;   //空行也占一个视觉行
            continue;
        }
        MeasureLine(hdc, codeFont, fallbackFont, disp, scratch);

        //折行：续行缩进到这一行的原缩进再加两列，但不超过可用宽度的一半
        int contIndent = 0;
        if (options.m_wrap)
        {
            int firstNonSpace = 0;
            while (firstNonSpace < n && disp[firstNonSpace] == L' ')
                ++firstNonSpace;
            contIndent = (firstNonSpace > 0 && firstNonSpace < n) ? scratch.m_charRight[firstNonSpace - 1] : 0;
            contIndent += kWrapIndentColumns * charWidth;
            if (contIndent > availText / 2)
                contIndent = availText / 2;
            ComputeCodeWrapStarts(scratch.m_charRight, disp, availText, availText - contIndent, scratch.m_starts);
        }
        else
        {
            scratch.m_starts.assign(1, 0);
            if (scratch.m_charRight[n - 1] > availText)
                out.m_overflowRows.push_back(row);
        }

        //按视觉行切成同字体、同颜色的文字段
        for (size_t r = 0; r < scratch.m_starts.size(); ++r)
        {
            const int rowBegin = scratch.m_starts[r];
            const int rowEnd = (r + 1 < scratch.m_starts.size()) ? scratch.m_starts[r + 1] : n;
            const int base = (rowBegin > 0) ? scratch.m_charRight[rowBegin - 1] : 0;
            const int offset = (r > 0) ? contIndent : 0;
            int j = rowBegin;
            while (j < rowEnd)
            {
                const int runBegin = j;
                const int kind = scratch.m_kinds[scratch.m_srcIndex[j]];
                const char fallback = scratch.m_fallback[j];
                while (j < rowEnd && scratch.m_kinds[scratch.m_srcIndex[j]] == kind && scratch.m_fallback[j] == fallback)
                    ++j;
                const int x0 = ((runBegin > 0) ? scratch.m_charRight[runBegin - 1] : 0) - base + offset;
                const int x1 = scratch.m_charRight[j - 1] - base + offset;
                if (x1 > widest)
                    widest = x1;
                if (IsAllSpaces(disp, runBegin, j))
                    continue;
                //关闭折行时完全落在代码区右边之外的段不必保存
                if (!options.m_wrap && x0 >= availText)
                    continue;
                CodeBlockRun run;
                run.m_row = row;
                run.m_x = out.m_textLeft + x0;
                run.m_width = x1 - x0;
                run.m_text = disp.substr(runBegin, j - runBegin);
                run.m_kind = kind;
                run.m_fallback = (fallback != 0);
                out.m_runs.push_back(run);
            }
            ++row;
        }
    }
    out.m_rowCount = (row > 0) ? row : 1;

    //被批注的行换算成视觉行：从起始行的第一个视觉行，到结束行下一行的第一个视觉行之前；只取显示出来的行
    for (size_t k = 0; k < content.m_noteLines.size(); ++k)
    {
        const int from = content.m_noteLines[k].first;
        const int to = content.m_noteLines[k].second;
        if (from < 0 || from >= (int)out.m_lineFirstRow.size())
            continue;
        const int firstRow = out.m_lineFirstRow[(size_t)from];
        const int lastRow = (to + 1 < (int)out.m_lineFirstRow.size()) ? out.m_lineFirstRow[(size_t)(to + 1)] - 1
                                                                      : out.m_rowCount - 1;
        if (lastRow >= firstRow)
            out.m_markedRows.push_back(std::make_pair(firstRow, lastRow));
    }

    //卡片宽度：按最宽的视觉行收窄，但要放得下顶部一行与底栏，且不超过 maxWidth
    const int textNeeded = (widest < availText) ? widest : availText;
    int width = out.m_textLeft + textNeeded + kPadH;
    const int titleW = MeasureTextWidth(hdc, out.m_title.c_str(), kHeaderTextPx, true);
    const int infoW = MeasureTextWidth(hdc, out.m_info.c_str(), kHeaderTextPx, false);
    const int copyW = MeasureTextWidth(hdc, out.m_copyText.c_str(), kHeaderTextPx, false);
    const int headerNeeded = kPadH + titleW + kHeaderGap + infoW + kHeaderMinGap + copyW + kPadH;
    const int footerNeeded = out.m_collapsible
        ? kPadH + MeasureTextWidth(hdc, out.m_footerText.c_str(), kHeaderTextPx, false) + kPadH
        : 0;
    int minWidth = kMinCardWidth;
    if (headerNeeded > minWidth)
        minWidth = headerNeeded;
    if (footerNeeded > minWidth)
        minWidth = footerNeeded;
    if (width < minWidth)
        width = minWidth;
    if (width > maxWidth)
        width = maxWidth;
    out.m_textRight = width - kPadH;

    const int height = out.m_bodyTop + out.m_rowCount * out.m_rowHeight + kBodyPadBottom
                       + (out.m_collapsible ? kFooterH : 0);
    out.m_size.cx = width;
    out.m_size.cy = height;
    ::SetRect(&out.m_copyRect, width - kPadH - copyW - kCopyHitPad, 0, width - kPadH + kCopyHitPad, kHeaderH);
    if (out.m_collapsible)
        ::SetRect(&out.m_footerRect, 0, height - kFooterH, width, height);

    ::SelectObject(hdc, oldFont);
    ::DeleteObject(codeFont);
    ::DeleteObject(fallbackFont);
}

void PaintCodeBlock(HDC hdc, const CodeBlockLayout& layout, int left, int top, int hoverPart)
{
    const int width = layout.m_size.cx;
    const int height = layout.m_size.cy;
    if (width <= 0 || height <= 0)
        return;

    //底色、顶部与底栏的底色、描边：GDI+ 抗锯齿画圆角
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)left + 0.5f, (Gdiplus::REAL)top + 0.5f,
                     (Gdiplus::REAL)width - 1.0f, (Gdiplus::REAL)height - 1.0f, kCardRadius);
        Gdiplus::SolidBrush bodyBrush(kCardBodyColor);
        g.FillPath(&bodyBrush, &path);
        g.SetClip(&path);
        Gdiplus::SolidBrush headerBrush(kCardHeaderColor);
        g.FillRectangle(&headerBrush, left, top, width, kHeaderH);
        if (layout.m_collapsible)
            g.FillRectangle(&headerBrush, left, top + height - kFooterH, width, kFooterH);
        g.ResetClip();
        Gdiplus::Pen borderPen(kCardBorderColor, 1.0f);
        g.DrawPath(&borderPen, &path);
    }

    //分隔线：一像素宽，用 GDI 画以保证清晰
    HBRUSH separator = ::CreateSolidBrush(kSeparatorColor);
    RECT line = { left + 1, top + kHeaderH - 1, left + width - 1, top + kHeaderH };
    ::FillRect(hdc, &line, separator);
    if (layout.m_collapsible)
    {
        ::SetRect(&line, left + 1, top + height - kFooterH, left + width - 1, top + height - kFooterH + 1);
        ::FillRect(hdc, &line, separator);
    }
    ::DeleteObject(separator);

    //顶部：语言名、行数、复制
    const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    const int titleW = MeasureTextWidth(hdc, layout.m_title.c_str(), kHeaderTextPx, true);
    RECT rc = { left + kPadH, top, left + kPadH + titleW + 1, top + kHeaderH };
    DrawGdiText(hdc, layout.m_title.c_str(), rc, kHeaderTextPx, true, kTitleColor, kOneLine);
    ::SetRect(&rc, rc.right + kHeaderGap, top, left + layout.m_copyRect.left, top + kHeaderH);
    DrawGdiText(hdc, layout.m_info.c_str(), rc, kHeaderTextPx, false, kInfoColor, kOneLine | DT_END_ELLIPSIS);
    //按实际文字宽度右对齐：「已复制」比排版时按「复制」算出的点击区宽，矩形不够宽时 DrawText 会把多出的部分裁掉
    const int copyTextW = MeasureTextWidth(hdc, layout.m_copyText.c_str(), kHeaderTextPx, false);
    int copyLeft = left + width - kPadH - copyTextW;
    if (copyLeft > left + layout.m_copyRect.left)
        copyLeft = left + layout.m_copyRect.left;
    ::SetRect(&rc, copyLeft, top, left + width - kPadH, top + kHeaderH);
    const bool copied = layout.m_copied;
    const COLORREF copyColor = copied ? kInfoColor : (hoverPart == kCodePartCopy ? kLinkHoverColor : kLinkColor);
    DrawGdiText(hdc, layout.m_copyText.c_str(), rc, kHeaderTextPx, false, copyColor,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, false, !copied && hoverPart == kCodePartCopy);

    //代码区：只画与裁剪区相交的视觉行
    const int saved = ::SaveDC(hdc);
    const int bodyTop = top + layout.m_bodyTop;
    const int bodyBottom = bodyTop + layout.m_rowCount * layout.m_rowHeight;
    ::IntersectClipRect(hdc, left + 1, bodyTop - kBodyPadTop, left + width - 1, bodyBottom + kBodyPadBottom);
    RECT clip = { 0, 0, 0, 0 };
    if (::GetClipBox(hdc, &clip) == NULLREGION)
    {
        ::RestoreDC(hdc, saved);
        return;
    }
    HFONT codeFont = CreateCodeFont(layout.m_fontPx);
    HFONT fallbackFont = CreateFallbackFont(layout.m_fontPx);
    HGDIOBJ oldFont = ::SelectObject(hdc, codeFont);
    ::SetBkMode(hdc, TRANSPARENT);

    //被批注的行：整行铺浅黄底色，卡片左缘画橙色竖条（docs/代码片段方案.md 第 10.5 节）；画在行号与文字之前
    if (!layout.m_markedRows.empty())
    {
        HBRUSH noteBg = ::CreateSolidBrush(kNoteLineColor);
        HBRUSH noteBar = ::CreateSolidBrush(kNoteMarkColor);
        for (size_t i = 0; i < layout.m_markedRows.size(); ++i)
        {
            const int y0 = bodyTop + layout.m_markedRows[i].first * layout.m_rowHeight;
            const int y1 = bodyTop + (layout.m_markedRows[i].second + 1) * layout.m_rowHeight;
            RECT band = { left + 1, y0, left + width - 1, y1 };
            ::FillRect(hdc, &band, noteBg);
            RECT bar = { left + kNoteBarLeft, y0, left + kNoteBarLeft + kNoteBarW, y1 };
            ::FillRect(hdc, &bar, noteBar);
        }
        ::DeleteObject(noteBg);
        ::DeleteObject(noteBar);
    }

    //行号：右对齐到行号栏右边，画在每个逻辑行的第一个视觉行
    if (layout.m_gutterRight > 0)
    {
        ::SetTextAlign(hdc, TA_RIGHT | TA_BASELINE);
        ::SetTextColor(hdc, kLineNumberColor);
        for (size_t li = 0; li < layout.m_lineFirstRow.size(); ++li)
        {
            const int rowTop = bodyTop + layout.m_lineFirstRow[li] * layout.m_rowHeight;
            if (rowTop > clip.bottom)
                break;
            if (rowTop + layout.m_rowHeight < clip.top)
                continue;
            const std::wstring number = std::to_wstring(static_cast<unsigned long long>(li + 1));
            ::ExtTextOutW(hdc, left + layout.m_gutterRight, rowTop + layout.m_baseline, 0, NULL, number.c_str(),
                          static_cast<UINT>(number.size()), NULL);
        }
    }

    //代码文字：裁到代码区左右边界之内
    ::IntersectClipRect(hdc, left + layout.m_textLeft, bodyTop - kBodyPadTop, left + layout.m_textRight,
                        bodyBottom + kBodyPadBottom);
    ::SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
    bool fallbackSelected = false;
    for (size_t i = 0; i < layout.m_runs.size(); ++i)
    {
        const CodeBlockRun& run = layout.m_runs[i];
        const int rowTop = bodyTop + run.m_row * layout.m_rowHeight;
        if (rowTop > clip.bottom)
            break;
        if (rowTop + layout.m_rowHeight < clip.top)
            continue;
        if (run.m_fallback != fallbackSelected)
        {
            ::SelectObject(hdc, run.m_fallback ? fallbackFont : codeFont);
            fallbackSelected = run.m_fallback;
        }
        ::SetTextColor(hdc, CodeTokenColor(run.m_kind));
        ::ExtTextOutW(hdc, left + run.m_x, rowTop + layout.m_baseline, 0, NULL, run.m_text.c_str(),
                      static_cast<UINT>(run.m_text.size()), NULL);
    }

    //关闭折行时被截断的行：行尾盖一块底色再画省略号
    if (!layout.m_overflowRows.empty())
    {
        ::SelectObject(hdc, codeFont);
        SIZE ellipsis = { 0, 0 };
        ::GetTextExtentPoint32W(hdc, kEllipsis, 1, &ellipsis);
        HBRUSH bg = ::CreateSolidBrush(kEllipsisBgColor);
        ::SetTextColor(hdc, kEllipsisColor);
        for (size_t i = 0; i < layout.m_overflowRows.size(); ++i)
        {
            const int rowTop = bodyTop + layout.m_overflowRows[i] * layout.m_rowHeight;
            //遮盖区比省略号多出一个字号的宽度：被截断的中文字比西文字宽，只盖省略号那么宽会留下半个字
            RECT cover = { left + layout.m_textRight - ellipsis.cx - layout.m_fontPx, rowTop,
                           left + layout.m_textRight, rowTop + layout.m_rowHeight };
            ::FillRect(hdc, &cover, bg);
            ::ExtTextOutW(hdc, left + layout.m_textRight - ellipsis.cx, rowTop + layout.m_baseline, 0, NULL,
                          kEllipsis, 1, NULL);
        }
        ::DeleteObject(bg);
    }

    ::SelectObject(hdc, oldFont);
    ::DeleteObject(codeFont);
    ::DeleteObject(fallbackFont);
    ::RestoreDC(hdc, saved);

    //底栏：展开全部 / 收起
    if (layout.m_collapsible)
    {
        RECT footer = { left, top + height - kFooterH, left + width, top + height };
        const bool hover = (hoverPart == kCodePartFooter);
        DrawGdiText(hdc, layout.m_footerText.c_str(), footer, kHeaderTextPx, false,
                    hover ? kLinkHoverColor : kLinkColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                    false, hover);
    }
}

int HitTestCodeBlock(const CodeBlockLayout& layout, POINT pt)
{
    if (::PtInRect(&layout.m_copyRect, pt))
        return kCodePartCopy;
    if (layout.m_collapsible && ::PtInRect(&layout.m_footerRect, pt))
        return kCodePartFooter;
    const RECT card = { 0, 0, layout.m_size.cx, layout.m_size.cy };
    if (::PtInRect(&card, pt))
        return kCodePartBody;
    return kCodePartNone;
}

bool SetCodeBlockCopied(CodeBlockLayout& layout, bool copied)
{
    if (layout.m_copied == copied)
        return false;
    layout.m_copied = copied;
    layout.m_copyText = copied ? TR(L"已复制") : TR(L"复制");
    return true;
}

COLORREF CodeTokenColor(int kind)
{
    if (kind < 0 || kind >= codesnippet::code_token_kind_count)
        return kTokenColors[codesnippet::code_token_plain];
    return kTokenColors[kind];
}

std::wstring CodeTextForClipboard(const std::wstring& code)
{
    std::wstring out;
    out.reserve(code.size() + code.size() / 16);
    for (size_t i = 0; i < code.size(); ++i)
    {
        if (code[i] == L'\n')
            out += L"\r\n";
        else
            out += code[i];
    }
    return out;
}

} // namespace balloonchatwnd
