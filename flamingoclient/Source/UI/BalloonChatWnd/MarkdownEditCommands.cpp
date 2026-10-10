/**
 *  MarkdownEditCommands.cpp
 *
 *  输入区 Markdown 格式条编辑命令与编辑器回车续项的实现，说明见 MarkdownEditCommands.h。
 *
 *  做法：先按命令算出替换之后的全文与选区，再用新旧全文的公共前缀、公共后缀求出最小的替换区间。这样每个命令
 *  只需关心「结果应当是什么样」，替换区间的计算只有一份。
 *
 *  本 .cpp 不 include stdafx.h、不依赖 ATL，与 FlamingoUnit 共用（工程里 PrecompiledHeader = NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MarkdownEditCommands.h"

#include <algorithm>
#include <vector>

namespace balloonchatwnd {

namespace {

//行内标记
const wchar_t* const kBoldMarker = L"**";
const wchar_t* const kItalicMarker = L"*";
const wchar_t* const kStrikeMarker = L"~~";
const wchar_t* const kCodeMarker = L"`";

//下划线的开、闭标签（Markdown 没有下划线语法，按 Typora 等的做法写 HTML 标签，docs/Markdown消息方案.md 第 13 节）
const wchar_t* const kUnderlineOpenTag = L"<u>";
const wchar_t* const kUnderlineCloseTag = L"</u>";

//行首前缀
const wchar_t* const kHeadingPrefix = L"## ";
const wchar_t* const kQuotePrefix = L"> ";
const wchar_t* const kBulletPrefix = L"- ";
const wchar_t* const kTaskPrefix = L"- [ ] ";

//代码块围栏
const wchar_t* const kFence = L"```";

//链接地址的占位
const wchar_t* const kLinkUrlPlaceholder = L"https://";

//表格模板第二行（对齐行）与第三行（空数据行）
const wchar_t* const kTableAlignRow = L"| --- | --- |";
const wchar_t* const kTableEmptyRow = L"|  |  |";

//有序列表编号的进制
const int kDecimalBase = 10;

//已有前缀的长度：「> 」「>」「- 」、编号后的「. 」、「- [ ] 」
const int kQuoteWithSpaceLength = 2;
const int kQuoteBareLength = 1;
const int kBulletPrefixLength = 2;
const int kNumberSuffixLength = 2;
const int kTaskPrefixLength = 6;

//链接里说明文字之前的字符数：「[」，以及说明与地址之间的「](」
const int kLinkOpenLength = 1;
const int kLinkMiddleLength = 2;

//表格模板第一行里第一列列名之前的字符数：「| 」
const int kTableCellLead = 2;

//分割线（docs/Markdown消息方案.md 第 13 节）
const wchar_t* const kRuleLine = L"---";

//警告框（docs/Markdown消息方案.md 第 13 节）：各类型的类型名（下标为类型减一），类型行的开头与结尾，正文行的引用符
const wchar_t* const kAlertKeywords[kMarkdownAlertTypeCount] = { L"NOTE", L"TIP", L"IMPORTANT", L"WARNING", L"CAUTION" };
const wchar_t* const kAlertTagOpen = L"> [!";
const wchar_t* const kAlertTagMark = L"[!";
const wchar_t kAlertTagClose = L']';
const wchar_t kQuoteChar = L'>';
const wchar_t* const kAlertEmptyLine = L">";

//回车续项：任务框的三种写法与它们的长度；续出的新项一律用未完成的写法
const wchar_t* const kTaskBoxOpen = L"[ ]";
const wchar_t* const kTaskBoxDone = L"[x]";
const wchar_t* const kTaskBoxDoneUpper = L"[X]";
const size_t kTaskBoxLength = 3;

//回车续项：分隔线（「---」「* * *」）至少由几个标记字符组成
const int kThematicBreakMinMarks = 3;

//回车续项：有序列表编号最多几位数字（CommonMark 的规定，更长的不算列表项）
const size_t kMaxListNumberDigits = 9;

//回车续项：围栏（``` 或 ~~~）最短几个字符，围栏行行首最多几个空格
const size_t kFenceMinLength = 3;
const size_t kMaxFenceIndent = 3;

//标题级别：最高几级（CommonMark 规定 1 ~ 6 个 #）
const int kMaxHeadingLevel = 6;

//快捷键：「`」与「\」两个键的虚拟键码（VK_OEM_3、VK_OEM_5，美式键盘布局；本文件不引入 windows.h）
const unsigned kVkBacktick = 0xC0;
const unsigned kVkBackslash = 0xDC;

//判断字符是否为换行
bool IsBreak(wchar_t ch)
{
    return ch == L'\r' || ch == L'\n';
}

//判断 text 在 pos 处是否以 s 开头
bool StartsWithAt(const std::wstring& text, size_t pos, const std::wstring& s)
{
    return pos + s.size() <= text.size() && text.compare(pos, s.size(), s) == 0;
}

//pos 之前紧挨着的连续 ch 的个数
int CountRunBefore(const std::wstring& text, int pos, wchar_t ch)
{
    int n = 0;
    while (pos - n - 1 >= 0 && text[pos - n - 1] == ch)
        ++n;
    return n;
}

//pos 起紧挨着的连续 ch 的个数
int CountRunAfter(const std::wstring& text, int pos, wchar_t ch)
{
    int n = 0;
    while (pos + n < static_cast<int>(text.size()) && text[pos + n] == ch)
        ++n;
    return n;
}

//pos 所在行的行首
int LineStartOf(const std::wstring& text, int pos)
{
    while (pos > 0 && !IsBreak(text[pos - 1]))
        --pos;
    return pos;
}

//pos 所在行的行尾（不含换行）
int LineEndOf(const std::wstring& text, int pos)
{
    const int n = static_cast<int>(text.size());
    while (pos < n && !IsBreak(text[pos]))
        ++pos;
    return pos;
}

//pos 紧跟在一个换行（\r、\n 或 \r\n）之后时，退到这个换行的开头；否则原样返回
int BackOverLineBreak(const std::wstring& text, int pos)
{
    if (pos <= 0 || !IsBreak(text[pos - 1]))
        return pos;
    int p = pos - 1;
    if (text[p] == L'\n' && p > 0 && text[p - 1] == L'\r')
        --p;
    return p;
}

//在 pos 处插入一个块（表格、代码块）之前要补的换行，保证新块与上面的内容之间隔一个空行：
//pos 在行中时先换行再空一行；pos 在行首、且上一行不是空行时补一个空行；其余情况不补。
//不隔空行时 md4c 会把紧跟在表格行后面的内容并进表格（2026-10-06 实机核对发现代码块的围栏被当成表格的行）
std::wstring BlankLineBefore(const std::wstring& text, int pos, const std::wstring& brk)
{
    const int lineStart = LineStartOf(text, pos);
    if (pos != lineStart)
        return brk + brk;
    if (lineStart > 0)
    {
        const int prevLineStart = LineStartOf(text, BackOverLineBreak(text, lineStart));
        if (BackOverLineBreak(text, lineStart) > prevLineStart)
            return brk;
    }
    return std::wstring();
}

//由新旧全文求最小替换区间，填好结果
MarkdownEditResult MakeResult(const std::wstring& oldText, const std::wstring& newText, int selStart, int selEnd)
{
    MarkdownEditResult r;
    const size_t oldLen = oldText.size();
    const size_t newLen = newText.size();
    size_t prefix = 0;
    while (prefix < oldLen && prefix < newLen && oldText[prefix] == newText[prefix])
        ++prefix;
    size_t suffix = 0;
    while (suffix < oldLen - prefix && suffix < newLen - prefix
           && oldText[oldLen - 1 - suffix] == newText[newLen - 1 - suffix])
        ++suffix;
    r.m_replaceStart = static_cast<int>(prefix);
    r.m_replaceEnd = static_cast<int>(oldLen - suffix);
    r.m_replacement = newText.substr(prefix, newLen - suffix - prefix);
    r.m_selStart = selStart;
    r.m_selEnd = selEnd;
    r.m_newText = newText;
    return r;
}

//行内包裹：加上或去掉 marker
MarkdownEditResult WrapInline(const std::wstring& text, int s, int e, const std::wstring& marker,
                              const std::wstring& placeholder)
{
    const int m = static_cast<int>(marker.size());
    const wchar_t mc = marker[0];
    if (s == e)
    {
        const std::wstring inserted = marker + placeholder + marker;
        const std::wstring newText = text.substr(0, s) + inserted + text.substr(s);
        return MakeResult(text, newText, s + m, s + m + static_cast<int>(placeholder.size()));
    }

    //选区两侧已有标记：斜体按奇数个 * 判断（偶数个属于加粗），其余标记要求两侧都至少有 m 个
    const int before = CountRunBefore(text, s, mc);
    const int after = CountRunAfter(text, e, mc);
    const bool singleStar = (marker == kItalicMarker);
    const bool wrapped = singleStar ? ((before % 2) == 1 && (after % 2) == 1) : (before >= m && after >= m);
    if (wrapped)
    {
        const std::wstring newText = text.substr(0, s - m) + text.substr(s, e - s) + text.substr(e + m);
        return MakeResult(text, newText, s - m, e - m);
    }

    //选区本身带着两侧的标记：去掉
    const std::wstring sel = text.substr(s, e - s);
    if (static_cast<int>(sel.size()) >= 2 * m && sel.compare(0, m, marker) == 0
        && sel.compare(sel.size() - m, m, marker) == 0)
    {
        const std::wstring inner = sel.substr(m, sel.size() - 2 * m);
        const std::wstring newText = text.substr(0, s) + inner + text.substr(e);
        return MakeResult(text, newText, s, s + static_cast<int>(inner.size()));
    }

    const std::wstring newText = text.substr(0, s) + marker + sel + marker + text.substr(e);
    return MakeResult(text, newText, s + m, e + m);
}

//text 在 pos 处是否以 s 开头，ASCII 字母不区分大小写（标签 <u> 与 <U> 都认）
bool StartsWithAtNoCase(const std::wstring& text, size_t pos, const std::wstring& s)
{
    if (pos + s.size() > text.size())
        return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        wchar_t a = text[pos + i];
        wchar_t b = s[i];
        if (a >= L'A' && a <= L'Z')
            a = static_cast<wchar_t>(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z')
            b = static_cast<wchar_t>(b - L'A' + L'a');
        if (a != b)
            return false;
    }
    return true;
}

//行内标签包裹（下划线 <u>…</u>）：两侧标记不同，规则同 WrapInline——选区两侧已有标签或选区本身带着标签时去掉，
//否则加上；没有选区时插入「开标签 + 占位文字 + 闭标签」并选中占位文字
MarkdownEditResult WrapTag(const std::wstring& text, int s, int e, const std::wstring& open, const std::wstring& close,
                           const std::wstring& placeholder)
{
    const int o = static_cast<int>(open.size());
    const int c = static_cast<int>(close.size());
    if (s == e)
    {
        const std::wstring newText = text.substr(0, s) + open + placeholder + close + text.substr(s);
        return MakeResult(text, newText, s + o, s + o + static_cast<int>(placeholder.size()));
    }

    //选区两侧已有标签：去掉
    if (s >= o && StartsWithAtNoCase(text, static_cast<size_t>(s - o), open) && StartsWithAtNoCase(text, e, close))
    {
        const std::wstring newText = text.substr(0, s - o) + text.substr(s, e - s) + text.substr(e + c);
        return MakeResult(text, newText, s - o, e - o);
    }

    //选区本身带着两侧的标签：去掉
    const std::wstring sel = text.substr(s, e - s);
    if (static_cast<int>(sel.size()) >= o + c && StartsWithAtNoCase(sel, 0, open)
        && StartsWithAtNoCase(sel, sel.size() - c, close))
    {
        const std::wstring inner = sel.substr(o, sel.size() - o - c);
        const std::wstring newText = text.substr(0, s) + inner + text.substr(e);
        return MakeResult(text, newText, s, s + static_cast<int>(inner.size()));
    }

    const std::wstring newText = text.substr(0, s) + open + sel + close + text.substr(e);
    return MakeResult(text, newText, s + o, e + o);
}

//行首前缀的种类
enum PrefixKind
{
    kPrefixHeading = 0,   //标题
    kPrefixQuote   = 1,   //引用
    kPrefixBullet  = 2,   //无序列表
    kPrefixNumber  = 3,   //有序列表
    kPrefixTask    = 4,   //任务列表
};

//一行开头已有的该种前缀的长度；没有时返回 0
int ExistingPrefixLength(const std::wstring& line, int kind)
{
    switch (kind)
    {
    //一个或多个 # 加空格
    case kPrefixHeading:
    {
        size_t i = 0;
        while (i < line.size() && line[i] == L'#')
            ++i;
        return (i > 0 && i < line.size() && line[i] == L' ') ? static_cast<int>(i + 1) : 0;
    }

    //> 加可选的空格
    case kPrefixQuote:
        if (line.empty() || line[0] != L'>')
            return 0;
        return (line.size() > 1 && line[1] == L' ') ? kQuoteWithSpaceLength : kQuoteBareLength;

    //- 加空格，但不是任务项
    case kPrefixBullet:
        if (StartsWithAt(line, 0, L"- [ ] ") || StartsWithAt(line, 0, L"- [x] ") || StartsWithAt(line, 0, L"- [X] "))
            return 0;
        return StartsWithAt(line, 0, kBulletPrefix) ? kBulletPrefixLength : 0;

    //数字加「. 」
    case kPrefixNumber:
    {
        size_t i = 0;
        while (i < line.size() && line[i] >= L'0' && line[i] <= L'9')
            ++i;
        return (i > 0 && StartsWithAt(line, i, L". ")) ? static_cast<int>(i) + kNumberSuffixLength : 0;
    }

    //- [ ] 或 - [x]
    case kPrefixTask:
        if (StartsWithAt(line, 0, L"- [ ] ") || StartsWithAt(line, 0, L"- [x] ") || StartsWithAt(line, 0, L"- [X] "))
            return kTaskPrefixLength;
        return 0;

    //不会出现的种类
    default:
        return 0;
    }
}

//第 index 行（从 0 起）要加的前缀
std::wstring PrefixFor(int kind, int index)
{
    switch (kind)
    {
    //标题
    case kPrefixHeading:
        return kHeadingPrefix;

    //引用
    case kPrefixQuote:
        return kQuotePrefix;

    //无序列表
    case kPrefixBullet:
        return kBulletPrefix;

    //有序列表：逐行编号
    case kPrefixNumber:
    {
        std::wstring digits;
        int n = index + 1;
        while (n > 0)
        {
            digits.insert(digits.begin(), static_cast<wchar_t>(L'0' + n % kDecimalBase));
            n /= kDecimalBase;
        }
        return digits + L". ";
    }

    //任务列表
    case kPrefixTask:
        return kTaskPrefix;

    //不会出现的种类
    default:
        return std::wstring();
    }
}

//行首前缀：给选区覆盖的每一行加上或去掉前缀
MarkdownEditResult ApplyLinePrefix(const std::wstring& text, int s, int e, int kind)
{
    //选区终点正好落在某行行首（且不是空选区）时，那一行不算在内
    const int effectiveEnd = (e > s) ? BackOverLineBreak(text, e) : e;
    const int blockStart = LineStartOf(text, s);
    const int blockEnd = LineEndOf(text, (std::max)(effectiveEnd, s));

    //切出各行（不含换行）与行之间的换行原样保留
    std::vector<std::wstring> lines;
    std::vector<std::wstring> breaks;
    int pos = blockStart;
    for (;;)
    {
        const int lineEnd = LineEndOf(text, pos);
        lines.push_back(text.substr(pos, lineEnd - pos));
        if (lineEnd >= blockEnd)
            break;
        int next = lineEnd + 1;
        if (text[lineEnd] == L'\r' && next < static_cast<int>(text.size()) && text[next] == L'\n')
            ++next;
        breaks.push_back(text.substr(lineEnd, next - lineEnd));
        pos = next;
    }

    //非空行都已有前缀时去掉，否则加上（多行时空行不加）
    bool allHave = true;
    int nonEmpty = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].empty())
            continue;
        ++nonEmpty;
        if (ExistingPrefixLength(lines[i], kind) == 0)
            allHave = false;
    }
    const bool remove = (nonEmpty > 0 && allHave);
    const bool singleLine = (lines.size() == 1);

    std::wstring rebuilt;
    int firstDelta = 0;
    int number = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring line = lines[i];
        const int existing = ExistingPrefixLength(line, kind);
        int delta = 0;
        if (remove)
        {
            line = line.substr(existing);
            delta = -existing;
        }
        else if (!line.empty() || singleLine)
        {
            //有序列表重新编号；其余种类已有前缀的行保持不变
            if (kind == kPrefixNumber || existing == 0)
            {
                const std::wstring prefix = PrefixFor(kind, number);
                line = prefix + line.substr(existing);
                delta = static_cast<int>(prefix.size()) - existing;
            }
            ++number;
        }
        if (i == 0)
            firstDelta = delta;
        rebuilt += line;
        if (i < breaks.size())
            rebuilt += breaks[i];
    }

    const std::wstring newText = text.substr(0, blockStart) + rebuilt + text.substr(blockEnd);
    if (s == e)
    {
        //只有光标：随第一行的前缀变化平移，不越过行首
        const int cursor = (std::max)(blockStart, s + firstDelta);
        return MakeResult(text, newText, cursor, cursor);
    }
    return MakeResult(text, newText, blockStart, blockStart + static_cast<int>(rebuilt.size()));
}

//代码块：把选中的行包进围栏，已包着时去掉围栏；没有选区时插入带占位文字的空代码块
MarkdownEditResult ApplyCodeBlock(const std::wstring& text, int s, int e, wchar_t lb, const std::wstring& placeholder)
{
    const std::wstring brk(1, lb);
    if (s == e)
    {
        //与上面的内容隔一个空行，否则紧跟在表格行后面的围栏会被并进表格
        const bool atLineEnd = (s == LineEndOf(text, s));
        const std::wstring head = BlankLineBefore(text, s, brk) + kFence + brk;
        const std::wstring tail = brk + kFence + (atLineEnd ? std::wstring() : brk);
        const std::wstring newText = text.substr(0, s) + head + placeholder + tail + text.substr(s);
        const int selStart = s + static_cast<int>(head.size());
        return MakeResult(text, newText, selStart, selStart + static_cast<int>(placeholder.size()));
    }

    const int effectiveEnd = BackOverLineBreak(text, e);
    const int blockStart = LineStartOf(text, s);
    const int blockEnd = LineEndOf(text, (std::max)(effectiveEnd, s));
    const std::wstring block = text.substr(blockStart, blockEnd - blockStart);

    //已经是一个完整的围栏代码块：去掉首尾两行
    const int firstLineEnd = LineEndOf(block, 0);
    const int lastLineStart = LineStartOf(block, static_cast<int>(block.size()));
    if (firstLineEnd < lastLineStart && StartsWithAt(block, 0, kFence) && StartsWithAt(block, lastLineStart, kFence))
    {
        int innerStart = firstLineEnd + 1;
        if (block[firstLineEnd] == L'\r' && innerStart < static_cast<int>(block.size()) && block[innerStart] == L'\n')
            ++innerStart;
        int innerEnd = lastLineStart - 1;
        if (innerEnd > 0 && block[innerEnd] == L'\n' && block[innerEnd - 1] == L'\r')
            --innerEnd;
        const std::wstring inner = (innerEnd > innerStart) ? block.substr(innerStart, innerEnd - innerStart)
                                                           : std::wstring();
        const std::wstring newText = text.substr(0, blockStart) + inner + text.substr(blockEnd);
        return MakeResult(text, newText, blockStart, blockStart + static_cast<int>(inner.size()));
    }

    //包裹选中的行：上一行不是空行时先补一个空行（理由同上）
    const std::wstring lead = BlankLineBefore(text, blockStart, brk);
    const std::wstring wrapped = lead + kFence + brk + block + brk + kFence;
    const std::wstring newText = text.substr(0, blockStart) + wrapped + text.substr(blockEnd);
    const int innerStart = blockStart + static_cast<int>(lead.size()) + static_cast<int>(std::wstring(kFence).size()) + 1;
    return MakeResult(text, newText, innerStart, innerStart + static_cast<int>(block.size()));
}

//判断文字看起来是不是网址
bool LooksLikeUrl(const std::wstring& s)
{
    return StartsWithAt(s, 0, L"http://") || StartsWithAt(s, 0, L"https://") || StartsWithAt(s, 0, L"www.");
}

//链接：选中的是网址时把它放进括号、说明用占位文字；否则选中文字作说明、地址用占位
MarkdownEditResult ApplyLink(const std::wstring& text, int s, int e, const std::wstring& placeholder)
{
    const std::wstring sel = text.substr(s, e - s);
    std::wstring inserted;
    int selStart = 0;
    int selEnd = 0;
    if (s != e && LooksLikeUrl(sel))
    {
        inserted = L"[" + placeholder + L"](" + sel + L")";
        selStart = s + kLinkOpenLength;
        selEnd = selStart + static_cast<int>(placeholder.size());
    }
    else
    {
        const std::wstring label = (s == e) ? placeholder : sel;
        inserted = L"[" + label + L"](" + kLinkUrlPlaceholder + L")";
        if (s == e)
        {
            selStart = s + kLinkOpenLength;
            selEnd = selStart + static_cast<int>(label.size());
        }
        else
        {
            selStart = s + kLinkOpenLength + static_cast<int>(label.size()) + kLinkMiddleLength;
            selEnd = selStart + static_cast<int>(std::wstring(kLinkUrlPlaceholder).size());
        }
    }
    const std::wstring newText = text.substr(0, s) + inserted + text.substr(e);
    return MakeResult(text, newText, selStart, selEnd);
}

//表格：在选区终点之后另起一段插入模板，选中第一列的列名
MarkdownEditResult ApplyTable(const std::wstring& text, int e, wchar_t lb, const std::wstring& h1, const std::wstring& h2)
{
    const std::wstring brk(1, lb);
    //与上面的内容隔一个空行，表格才不会被并进上面的段落
    const std::wstring head = BlankLineBefore(text, e, brk);
    const std::wstring table = L"| " + h1 + L" | " + h2 + L" |" + brk + kTableAlignRow + brk + kTableEmptyRow;
    const bool atLineEnd = (e == LineEndOf(text, e));
    const std::wstring tail = atLineEnd ? std::wstring() : brk;
    const std::wstring newText = text.substr(0, e) + head + table + tail + text.substr(e);
    const int selStart = e + static_cast<int>(head.size()) + kTableCellLead;
    return MakeResult(text, newText, selStart, selStart + static_cast<int>(h1.size()));
}

//分割线：在选区终点之后另起一段插入「---」，光标停在其后。与上面的内容隔一个空行，否则「---」会把上一行变成二级标题
//（Markdown 的另一种标题写法）；后面还有文字时另起一行
MarkdownEditResult ApplyRule(const std::wstring& text, int e, wchar_t lb)
{
    const std::wstring brk(1, lb);
    const std::wstring head = BlankLineBefore(text, e, brk);
    const bool atLineEnd = (e == LineEndOf(text, e));
    const std::wstring tail = atLineEnd ? std::wstring() : brk;
    const std::wstring inserted = head + kRuleLine;
    const std::wstring newText = text.substr(0, e) + inserted + tail + text.substr(e);
    const int cursor = e + static_cast<int>(inserted.size());
    return MakeResult(text, newText, cursor, cursor);
}

//一行开头的引用符、列表符号与任务框的解析结果（回车续项用）
struct LineMarker
{
    bool         m_valid;          //这一行是不是列表项或引用
    int          m_contentStart;   //正文在行内的起点：缩进、引用符、列表符号、任务框与它们后面的空白都在它之前
    std::wstring m_next;           //回车后新行开头要写的标记
    std::wstring m_keepWhenEmpty;  //空项上回车时这一行留下的部分：引用里的空列表项留下引用符，其余情况为空

    LineMarker()
        : m_valid(false), m_contentStart(0)
    {
    }
};

//从 i 起跳过空格与制表符，返回第一个非空白字符的位置（没有时为行长）
size_t SkipBlanks(const std::wstring& line, size_t i)
{
    while (i < line.size() && (line[i] == L' ' || line[i] == L'\t'))
        ++i;
    return i;
}

//从 from 起这一行是不是分隔线：只由空白与至少三个同一种 -、*、_ 组成（「- - -」不是列表项）
bool IsThematicBreak(const std::wstring& line, size_t from)
{
    wchar_t mark = 0;
    int count = 0;
    for (size_t i = from; i < line.size(); ++i)
    {
        const wchar_t ch = line[i];
        if (ch == L' ' || ch == L'\t')
            continue;
        if ((ch != L'-' && ch != L'*' && ch != L'_') || (mark != 0 && ch != mark))
            return false;
        mark = ch;
        ++count;
    }
    return count >= kThematicBreakMinMarks;
}

//有序列表的编号加一，位数不少于原来：「9」之后是「10」，「09」之后是「10」，「007」之后是「008」
std::wstring NextListNumber(const std::wstring& digits)
{
    std::wstring next = digits;
    int i = static_cast<int>(next.size()) - 1;
    while (i >= 0 && next[i] == L'9')
    {
        next[i] = L'0';
        --i;
    }
    if (i >= 0)
        ++next[i];
    else
        next.insert(next.begin(), L'1');
    return next;
}

//解析一行开头的引用符、列表符号与任务框
LineMarker ParseLineMarker(const std::wstring& line)
{
    LineMarker m;

    //行首缩进之后的引用符：一个或多个「>」，每个后面可带一个空格
    size_t i = SkipBlanks(line, 0);
    const size_t quoteStart = i;
    while (i < line.size() && line[i] == L'>')
    {
        ++i;
        if (i < line.size() && line[i] == L' ')
            ++i;
    }
    const size_t quoteEnd = i;
    const bool quoted = (quoteEnd > quoteStart);

    //列表符号：引用里的列表写在引用符之后，前面还可以有缩进；分隔线不算
    const size_t markerStart = SkipBlanks(line, quoteEnd);
    size_t markerEnd = markerStart;   //列表符号之后的位置；没有列表符号时等于 markerStart
    std::wstring marker;              //新行用的列表符号（有序列表的编号已加一）
    if (markerStart < line.size() && !IsThematicBreak(line, markerStart))
    {
        const wchar_t ch = line[markerStart];
        if (ch == L'-' || ch == L'*' || ch == L'+')
        {
            markerEnd = markerStart + 1;
            marker.assign(1, ch);
        }
        else
        {
            size_t d = markerStart;
            while (d < line.size() && line[d] >= L'0' && line[d] <= L'9')
                ++d;
            const size_t digitCount = d - markerStart;
            if (digitCount > 0 && digitCount <= kMaxListNumberDigits && d < line.size()
                && (line[d] == L'.' || line[d] == L')'))
            {
                markerEnd = d + 1;
                marker = NextListNumber(line.substr(markerStart, digitCount)) + line[d];
            }
        }
    }

    //列表符号后面必须跟着空白，「-a」「1.a」不是列表项
    const size_t afterMarker = SkipBlanks(line, markerEnd);
    const bool listed = !marker.empty() && afterMarker > markerEnd;
    if (!listed && !quoted)
        return m;
    m.m_valid = true;

    //只有引用：新行写上同样的引用符，空引用上回车时整行清空
    if (!listed)
    {
        m.m_contentStart = static_cast<int>(quoteEnd);
        m.m_next = line.substr(0, quoteEnd);
        return m;
    }

    //列表项：缩进、引用符与列表符号后的空白都照原样
    m.m_next = line.substr(0, markerStart) + marker + line.substr(markerEnd, afterMarker - markerEnd);
    m.m_contentStart = static_cast<int>(afterMarker);
    m.m_keepWhenEmpty = quoted ? line.substr(0, quoteEnd) : std::wstring();

    //任务框：后面跟着空白或就是行尾时才算；新项一律未完成
    if (StartsWithAt(line, afterMarker, kTaskBoxOpen) || StartsWithAt(line, afterMarker, kTaskBoxDone)
        || StartsWithAt(line, afterMarker, kTaskBoxDoneUpper))
    {
        const size_t boxEnd = afterMarker + kTaskBoxLength;
        const size_t afterBox = SkipBlanks(line, boxEnd);
        if (afterBox > boxEnd || boxEnd == line.size())
        {
            const std::wstring boxSpacing = (afterBox > boxEnd) ? line.substr(boxEnd, afterBox - boxEnd)
                                                                : std::wstring(L" ");
            m.m_next += std::wstring(kTaskBoxOpen) + boxSpacing;
            m.m_contentStart = static_cast<int>(afterBox);
        }
    }
    return m;
}

//lineStart 所在行之前是否有尚未闭合的围栏代码块。行首（最多 3 个空格之后）至少三个 ` 或 ~ 开启围栏；
//同一种字符、长度不短于开启时、后面只有空白的一行闭合围栏
bool InsideFence(const std::wstring& text, int lineStart)
{
    const int len = static_cast<int>(text.size());
    wchar_t fenceChar = 0;
    size_t fenceLength = 0;
    int pos = 0;
    while (pos < lineStart)
    {
        const int lineEnd = LineEndOf(text, pos);
        const std::wstring line = text.substr(pos, lineEnd - pos);
        size_t i = 0;
        while (i < line.size() && i < kMaxFenceIndent && line[i] == L' ')
            ++i;
        const wchar_t ch = (i < line.size()) ? line[i] : 0;
        if (ch == L'`' || ch == L'~')
        {
            const size_t run = static_cast<size_t>(CountRunAfter(line, static_cast<int>(i), ch));
            if (run >= kFenceMinLength)
            {
                if (fenceChar == 0)
                {
                    fenceChar = ch;
                    fenceLength = run;
                }
                else if (ch == fenceChar && run >= fenceLength && SkipBlanks(line, i + run) == line.size())
                {
                    fenceChar = 0;
                    fenceLength = 0;
                }
            }
        }

        //越过行尾的一个换行（\r、\n 或 \r\n），到下一行行首
        if (lineEnd >= len)
            break;
        const bool crlf = (text[lineEnd] == L'\r' && lineEnd + 1 < len && text[lineEnd + 1] == L'\n');
        pos = lineEnd + (crlf ? 2 : 1);
    }
    return fenceChar != 0;
}

//一行开头的标题前缀：1 ~ 6 个「#」后跟空格或制表符（或就是行尾）。返回级别，不是标题时返回 0；
//prefixLength 写入前缀（含其后的一个空白）的长度，不是标题时为 0
int HeadingPrefixLevel(const std::wstring& line, int& prefixLength)
{
    prefixLength = 0;
    size_t i = 0;
    while (i < line.size() && line[i] == L'#')
        ++i;
    if (i == 0 || i > static_cast<size_t>(kMaxHeadingLevel))
        return 0;
    if (i == line.size())
    {
        prefixLength = static_cast<int>(i);
        return static_cast<int>(i);
    }
    if (line[i] != L' ' && line[i] != L'\t')
        return 0;
    prefixLength = static_cast<int>(i) + 1;
    return static_cast<int>(i);
}

//清除样式：字符是否属于单词（字母、数字，以及 ASCII 以外的字符），用于判断「_」是不是单词中间的下划线
bool IsWordChar(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch > 0x7F;
}

//清除样式：一行行首不动的部分（缩进、引用符、列表符号、任务框，或标题前缀）的长度
int ProtectedPrefixLength(const std::wstring& line)
{
    const LineMarker marker = ParseLineMarker(line);
    int heading = 0;
    HeadingPrefixLevel(line, heading);
    const int listed = marker.m_valid ? marker.m_contentStart : 0;
    return (std::max)(listed, heading);
}

//清除样式：去掉一段行内文字（不含换行与行首前缀）里的样式标记，规则见 ClearMarkdownInlineStyle
std::wstring StripInlineMarks(const std::wstring& s)
{
    std::wstring out;
    size_t i = 0;
    while (i < s.size())
    {
        const wchar_t ch = s[i];
        //下划线标签
        if (StartsWithAtNoCase(s, i, kUnderlineCloseTag))
        {
            i += std::wstring(kUnderlineCloseTag).size();
            continue;
        }
        if (StartsWithAtNoCase(s, i, kUnderlineOpenTag))
        {
            i += std::wstring(kUnderlineOpenTag).size();
            continue;
        }
        //双字符标记：加粗、删除线
        if (StartsWithAt(s, i, kBoldMarker) || StartsWithAt(s, i, L"__") || StartsWithAt(s, i, kStrikeMarker))
        {
            i += 2;
            continue;
        }
        //行内代码：去掉反引号，代码里的文字原样保留；没有配对的反引号直接去掉
        if (ch == L'`')
        {
            const size_t close = s.find(L'`', i + 1);
            if (close != std::wstring::npos)
            {
                out += s.substr(i + 1, close - i - 1);
                i = close + 1;
            }
            else
            {
                ++i;
            }
            continue;
        }
        //斜体的「*」
        if (ch == L'*')
        {
            ++i;
            continue;
        }
        //作强调用的「_」：两侧不都是单词字符时才去掉
        if (ch == L'_')
        {
            const bool wordBefore = (i > 0) && IsWordChar(s[i - 1]);
            const bool wordAfter = (i + 1 < s.size()) && IsWordChar(s[i + 1]);
            if (!(wordBefore && wordAfter))
            {
                ++i;
                continue;
            }
        }
        //链接 [文字](地址) 与图片 ![说明](地址)：只留下文字
        const bool image = (ch == L'!' && i + 1 < s.size() && s[i + 1] == L'[');
        if (ch == L'[' || image)
        {
            const size_t textStart = i + (image ? 2 : 1);
            const size_t middle = s.find(L"](", textStart);
            const size_t close = (middle != std::wstring::npos) ? s.find(L')', middle + 2) : std::wstring::npos;
            if (close != std::wstring::npos)
            {
                out += StripInlineMarks(s.substr(textStart, middle - textStart));
                i = close + 1;
                continue;
            }
        }
        out += ch;
        ++i;
    }
    return out;
}

//把 [blockStart, blockEnd) 按行切开：各行不含换行，行与行之间的换行原样保留（\r、\n 或 \r\n）
void SplitBlockLines(const std::wstring& text, int blockStart, int blockEnd, std::vector<std::wstring>& lines,
                     std::vector<std::wstring>& breaks)
{
    lines.clear();
    breaks.clear();
    int pos = blockStart;
    for (;;)
    {
        const int lineEnd = LineEndOf(text, pos);
        lines.push_back(text.substr(pos, lineEnd - pos));
        if (lineEnd >= blockEnd)
            break;
        int next = lineEnd + 1;
        if (text[lineEnd] == L'\r' && next < static_cast<int>(text.size()) && text[next] == L'\n')
            ++next;
        breaks.push_back(text.substr(lineEnd, next - lineEnd));
        pos = next;
    }
}

//警告框：字符是否为 ASCII 字母（类型名只由字母组成）
bool IsAsciiLetter(wchar_t ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

//警告框：一行行首（最多 3 个空格之后）是否为引用符「>」
bool StartsWithQuoteChar(const std::wstring& line)
{
    size_t i = 0;
    while (i < line.size() && i < kMaxFenceIndent && line[i] == L' ')
        ++i;
    return i < line.size() && line[i] == kQuoteChar;
}

/**
 *  警告框：一行是不是类型行「> [!类型]」（行首最多 3 个空格，「>」之后可有空白，「]」之后只能有空白；类型名不论
 *  是否认得都算，换类型时一并换掉）。是时写入类型名在行内的起点与长度。
 */
bool ParseAlertTagLine(const std::wstring& line, int& keywordStart, int& keywordLength)
{
    if (!StartsWithQuoteChar(line))
        return false;
    size_t i = SkipBlanks(line, line.find(kQuoteChar) + 1);
    if (!StartsWithAt(line, i, kAlertTagMark))
        return false;
    const size_t first = i + std::wstring(kAlertTagMark).size();
    size_t j = first;
    while (j < line.size() && IsAsciiLetter(line[j]))
        ++j;
    if (j == first || j >= line.size() || line[j] != kAlertTagClose || SkipBlanks(line, j + 1) != line.size())
        return false;
    keywordStart = static_cast<int>(first);
    keywordLength = static_cast<int>(j - first);
    return true;
}

//在行尾 pos 处插入一个块之后要补的换行：下一行是非空的文字时补一个换行，与它隔一个空行（否则那行文字会作为
//延续行并进引用块或警告框）；pos 是全文末尾或下一行是空行时不补
std::wstring BlankLineAfter(const std::wstring& text, int pos, const std::wstring& brk)
{
    const int n = static_cast<int>(text.size());
    if (pos >= n)
        return std::wstring();
    int next = pos + 1;
    if (text[pos] == L'\r' && next < n && text[next] == L'\n')
        ++next;
    return (LineEndOf(text, next) > next) ? brk : std::wstring();
}

//警告框：选区起点所在行已是类型行时只换类型名，选区随之平移；返回 false 表示不是类型行
bool ReplaceAlertKeyword(const std::wstring& text, int s, int e, const std::wstring& keyword, MarkdownEditResult& out)
{
    const int lineStart = LineStartOf(text, s);
    const std::wstring line = text.substr(lineStart, LineEndOf(text, s) - lineStart);
    int keywordStart = 0;
    int keywordLength = 0;
    if (!ParseAlertTagLine(line, keywordStart, keywordLength))
        return false;
    const int at = lineStart + keywordStart;
    const int oldEnd = at + keywordLength;
    const int newEnd = at + static_cast<int>(keyword.size());
    const std::wstring newText = text.substr(0, at) + keyword + text.substr(oldEnd);
    //类型名之后的位置随长度变化平移，落在类型名之中的位置不超出新类型名
    const int ns = (s >= oldEnd) ? s - oldEnd + newEnd : (std::min)(s, newEnd);
    const int ne = (e >= oldEnd) ? e - oldEnd + newEnd : (std::min)(e, newEnd);
    out = MakeResult(text, newText, ns, ne);
    return true;
}

//插入警告框，规则见 InsertMarkdownAlert（s、e 已截断到全文之内，s <= e）
MarkdownEditResult ApplyAlert(const std::wstring& text, int s, int e, wchar_t lb, const std::wstring& placeholder,
                              const std::wstring& keyword)
{
    MarkdownEditResult replaced;
    if (ReplaceAlertKeyword(text, s, e, keyword, replaced))
        return replaced;

    const std::wstring brk(1, lb);
    const std::wstring tagLine = std::wstring(kAlertTagOpen) + keyword + kAlertTagClose;
    if (s == e)
    {
        //没有选区：光标所在行为空行时就地插入，否则插在这一行之后
        const int lineStart = LineStartOf(text, s);
        const int lineEnd = LineEndOf(text, s);
        const int at = (lineStart == lineEnd) ? lineStart : lineEnd;
        const std::wstring head = BlankLineBefore(text, at, brk);
        const std::wstring inserted = head + tagLine + brk + kQuotePrefix + placeholder;
        const std::wstring newText = text.substr(0, at) + inserted + BlankLineAfter(text, at, brk) + text.substr(at);
        const int selEnd = at + static_cast<int>(inserted.size());
        return MakeResult(text, newText, selEnd - static_cast<int>(placeholder.size()), selEnd);
    }

    //有选区：选区终点正好落在某行行首时，那一行不算在内
    const int effectiveEnd = BackOverLineBreak(text, e);
    const int blockStart = LineStartOf(text, s);
    const int blockEnd = LineEndOf(text, (std::max)(effectiveEnd, s));
    std::vector<std::wstring> lines;
    std::vector<std::wstring> breaks;
    SplitBlockLines(text, blockStart, blockEnd, lines, breaks);
    std::wstring rebuilt = tagLine + brk;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].empty())
            rebuilt += kAlertEmptyLine;
        else if (StartsWithQuoteChar(lines[i]))
            rebuilt += lines[i];
        else
            rebuilt += kQuotePrefix + lines[i];
        if (i < breaks.size())
            rebuilt += breaks[i];
    }
    const std::wstring head = BlankLineBefore(text, blockStart, brk);
    const std::wstring newText = text.substr(0, blockStart) + head + rebuilt + BlankLineAfter(text, blockEnd, brk)
                                 + text.substr(blockEnd);
    const int selStart = blockStart + static_cast<int>(head.size());
    return MakeResult(text, newText, selStart, selStart + static_cast<int>(rebuilt.size()));
}

} // namespace

MarkdownEditResult ApplyMarkdownEditCommand(const MarkdownEditInput& in, int command)
{
    const std::wstring& text = in.m_text;
    const int len = static_cast<int>(text.size());
    int s = (std::min)(in.m_selStart, in.m_selEnd);
    int e = (std::max)(in.m_selStart, in.m_selEnd);
    s = (std::max)(0, (std::min)(s, len));
    e = (std::max)(0, (std::min)(e, len));

    switch (command)
    {
    //标题
    case md_edit_heading:
        return ApplyLinePrefix(text, s, e, kPrefixHeading);

    //加粗
    case md_edit_bold:
        return WrapInline(text, s, e, kBoldMarker, in.m_placeholder);

    //斜体
    case md_edit_italic:
        return WrapInline(text, s, e, kItalicMarker, in.m_placeholder);

    //下划线：<u>文字</u>
    case md_edit_underline:
        return WrapTag(text, s, e, kUnderlineOpenTag, kUnderlineCloseTag, in.m_placeholder);

    //删除线
    case md_edit_strike:
        return WrapInline(text, s, e, kStrikeMarker, in.m_placeholder);

    //行内代码
    case md_edit_code:
        return WrapInline(text, s, e, kCodeMarker, in.m_placeholder);

    //代码块
    case md_edit_code_block:
        return ApplyCodeBlock(text, s, e, in.m_lineBreak, in.m_placeholder);

    //链接
    case md_edit_link:
        return ApplyLink(text, s, e, in.m_placeholder);

    //引用
    case md_edit_quote:
        return ApplyLinePrefix(text, s, e, kPrefixQuote);

    //无序列表
    case md_edit_bullet_list:
        return ApplyLinePrefix(text, s, e, kPrefixBullet);

    //有序列表
    case md_edit_number_list:
        return ApplyLinePrefix(text, s, e, kPrefixNumber);

    //任务列表
    case md_edit_task_list:
        return ApplyLinePrefix(text, s, e, kPrefixTask);

    //表格
    case md_edit_table:
        return ApplyTable(text, e, in.m_lineBreak, in.m_tableHeader1, in.m_tableHeader2);

    //分割线
    case md_edit_rule:
        return ApplyRule(text, e, in.m_lineBreak);

    //警告框：编辑器按钮弹出类型菜单，直接执行本命令时按「提示」插入
    case md_edit_alert:
        return ApplyAlert(text, s, e, in.m_lineBreak, in.m_placeholder, kAlertKeywords[md_edit_alert_note - 1]);

    //不认识的命令：不改动
    default:
        return MakeResult(text, text, s, e);
    }
}

bool ContinueMarkdownListOnEnter(const MarkdownEditInput& in, MarkdownEditResult& out)
{
    const std::wstring& text = in.m_text;
    const int len = static_cast<int>(text.size());

    //有选区，或光标落在全文之外（宿主取到的全文去掉了末尾的空行，光标在那些空行上）：不处理。这里不截断下标，
    //截断后光标会被当成在最后一行行尾，续出一个多余的列表项
    if (in.m_selStart != in.m_selEnd || in.m_selStart < 0 || in.m_selStart > len)
        return false;
    const int caret = in.m_selStart;
    const int lineStart = LineStartOf(text, caret);
    const int lineEnd = LineEndOf(text, caret);
    const std::wstring line = text.substr(lineStart, lineEnd - lineStart);

    //不是列表项或引用、光标在行首标记之前、在围栏代码块里：不处理
    const LineMarker marker = ParseLineMarker(line);
    if (!marker.m_valid || caret - lineStart < marker.m_contentStart || InsideFence(text, lineStart))
        return false;

    //空项：去掉列表标记、结束列表，不换行；引用里的空列表项保留引用符
    if (SkipBlanks(line, static_cast<size_t>(marker.m_contentStart)) == line.size())
    {
        const std::wstring newText = text.substr(0, lineStart) + marker.m_keepWhenEmpty + text.substr(lineEnd);
        const int cursor = lineStart + static_cast<int>(marker.m_keepWhenEmpty.size());
        out = MakeResult(text, newText, cursor, cursor);
        return true;
    }

    //在光标处换行并写上标记，光标后面的文字原样带到新行
    const std::wstring inserted = std::wstring(1, in.m_lineBreak) + marker.m_next;
    const std::wstring newText = text.substr(0, caret) + inserted + text.substr(caret);
    const int cursor = caret + static_cast<int>(inserted.size());
    out = MakeResult(text, newText, cursor, cursor);
    return true;
}

MarkdownEditResult SetMarkdownHeadingLevel(const MarkdownEditInput& in, int level)
{
    const std::wstring& text = in.m_text;
    const int len = static_cast<int>(text.size());
    int s = (std::min)(in.m_selStart, in.m_selEnd);
    int e = (std::max)(in.m_selStart, in.m_selEnd);
    s = (std::max)(0, (std::min)(s, len));
    e = (std::max)(0, (std::min)(e, len));
    if (level < 0 || level > kMaxHeadingLevel)
        return MakeResult(text, text, s, e);

    //选区终点正好落在某行行首（且不是空选区）时，那一行不算在内
    const int effectiveEnd = (e > s) ? BackOverLineBreak(text, e) : e;
    const int blockStart = LineStartOf(text, s);
    const int blockEnd = LineEndOf(text, (std::max)(effectiveEnd, s));
    std::vector<std::wstring> lines;
    std::vector<std::wstring> breaks;
    SplitBlockLines(text, blockStart, blockEnd, lines, breaks);

    //每行去掉原有的标题前缀，写上新级别的前缀（正文为空）；多行时空行不加
    const std::wstring prefix = (level > 0) ? std::wstring(static_cast<size_t>(level), L'#') + L" " : std::wstring();
    const bool singleLine = (lines.size() == 1);
    std::wstring rebuilt;
    int firstDelta = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring line = lines[i];
        int delta = 0;
        if (!line.empty() || singleLine)
        {
            int existing = 0;
            HeadingPrefixLevel(line, existing);
            line = prefix + line.substr(static_cast<size_t>(existing));
            delta = static_cast<int>(prefix.size()) - existing;
        }
        if (i == 0)
            firstDelta = delta;
        rebuilt += line;
        if (i < breaks.size())
            rebuilt += breaks[i];
    }

    const std::wstring newText = text.substr(0, blockStart) + rebuilt + text.substr(blockEnd);
    //已经是这一级（或本来就是正文）：不改动，选区保持原样
    if (newText == text)
        return MakeResult(text, text, s, e);
    if (s == e)
    {
        //只有光标：随第一行的前缀变化平移，不越过行首
        const int cursor = (std::max)(blockStart, s + firstDelta);
        return MakeResult(text, newText, cursor, cursor);
    }
    return MakeResult(text, newText, blockStart, blockStart + static_cast<int>(rebuilt.size()));
}

int MarkdownHeadingLevelAt(const std::wstring& text, int selStart, int selEnd)
{
    const int len = static_cast<int>(text.size());
    int s = (std::min)(selStart, selEnd);
    s = (std::max)(0, (std::min)(s, len));
    const int lineStart = LineStartOf(text, s);
    const int lineEnd = LineEndOf(text, s);
    int prefixLength = 0;
    return HeadingPrefixLevel(text.substr(lineStart, lineEnd - lineStart), prefixLength);
}

MarkdownEditResult ClearMarkdownInlineStyle(const MarkdownEditInput& in)
{
    const std::wstring& text = in.m_text;
    const int len = static_cast<int>(text.size());
    int s = (std::min)(in.m_selStart, in.m_selEnd);
    int e = (std::max)(in.m_selStart, in.m_selEnd);
    s = (std::max)(0, (std::min)(s, len));
    e = (std::max)(0, (std::min)(e, len));
    if (s == e)
        return MakeResult(text, text, s, e);

    //选区里的标记：逐行去掉，行首前缀与换行原样保留
    std::wstring cleaned;
    int pos = s;
    while (pos < e)
    {
        const int lineStart = LineStartOf(text, pos);
        const int lineEnd = LineEndOf(text, pos);
        const int protectedEnd = lineStart + ProtectedPrefixLength(text.substr(lineStart, lineEnd - lineStart));
        const int segEnd = (std::min)(lineEnd, e);
        if (pos < protectedEnd)
        {
            const int keepEnd = (std::min)(protectedEnd, segEnd);
            cleaned += text.substr(pos, keepEnd - pos);
            pos = keepEnd;
        }
        if (pos < segEnd)
        {
            cleaned += StripInlineMarks(text.substr(pos, segEnd - pos));
            pos = segEnd;
        }
        if (pos < e && pos == lineEnd)
        {
            int next = pos + 1;
            if (text[pos] == L'\r' && next < len && text[next] == L'\n')
                ++next;
            next = (std::min)(next, e);
            cleaned += text.substr(pos, next - pos);
            pos = next;
        }
    }

    //选区正好被成对的标记包着：外侧标记一起去掉，反复直到外侧没有成对的标记
    const wchar_t* const kPairs[][2] =
    {
        { kUnderlineOpenTag, kUnderlineCloseTag },
        { kBoldMarker, kBoldMarker },
        { L"__", L"__" },
        { kStrikeMarker, kStrikeMarker },
        { kCodeMarker, kCodeMarker },
        { kItalicMarker, kItalicMarker },
        { L"_", L"_" },
    };
    int outerS = s;
    int outerE = e;
    for (;;)
    {
        bool stripped = false;
        for (size_t p = 0; p < sizeof(kPairs) / sizeof(kPairs[0]) && !stripped; ++p)
        {
            const std::wstring open(kPairs[p][0]);
            const std::wstring close(kPairs[p][1]);
            const int o = static_cast<int>(open.size());
            if (outerS >= o && StartsWithAtNoCase(text, static_cast<size_t>(outerS - o), open)
                && StartsWithAtNoCase(text, static_cast<size_t>(outerE), close))
            {
                outerS -= o;
                outerE += static_cast<int>(close.size());
                stripped = true;
            }
        }
        //链接或图片的文字：外侧是「[」（或「![」）与「](地址)」
        if (!stripped && outerS >= 1 && text[outerS - 1] == L'[' && StartsWithAt(text, outerE, L"]("))
        {
            const int urlLineEnd = LineEndOf(text, outerE);
            const size_t paren = text.find(L')', static_cast<size_t>(outerE) + 2);
            if (paren != std::wstring::npos && static_cast<int>(paren) < urlLineEnd)
            {
                outerS -= (outerS >= 2 && text[outerS - 2] == L'!') ? 2 : 1;
                outerE = static_cast<int>(paren) + 1;
                stripped = true;
            }
        }
        if (!stripped)
            break;
    }

    const std::wstring newText = text.substr(0, outerS) + cleaned + text.substr(outerE);
    return MakeResult(text, newText, outerS, outerS + static_cast<int>(cleaned.size()));
}

int MarkdownShortcutFor(unsigned vk, bool ctrl, bool shift, bool alt)
{
    //Ctrl 加一个键：标题、加粗、斜体、下划线、链接、清除样式
    if (ctrl && !shift && !alt)
    {
        if (vk >= static_cast<unsigned>('1') && vk < static_cast<unsigned>('1') + kMaxHeadingLevel)
            return md_shortcut_heading1 + static_cast<int>(vk - static_cast<unsigned>('1'));
        switch (vk)
        {
        //Ctrl+B：加粗
        case 'B':
            return md_shortcut_bold;
        //Ctrl+I：斜体
        case 'I':
            return md_shortcut_italic;
        //Ctrl+U：下划线
        case 'U':
            return md_shortcut_underline;
        //Ctrl+K：超链接
        case 'K':
            return md_shortcut_link;
        //Ctrl+\：清除样式
        case kVkBackslash:
            return md_shortcut_clear_style;
        //其余 Ctrl 组合不是格式快捷键
        default:
            return md_shortcut_none;
        }
    }
    //Ctrl+Shift+`：行内代码
    if (ctrl && shift && !alt && vk == kVkBacktick)
        return md_shortcut_code;
    //Alt+Shift+5：删除线
    if (!ctrl && shift && alt && vk == static_cast<unsigned>('5'))
        return md_shortcut_strike;
    return md_shortcut_none;
}

MarkdownEditResult InsertMarkdownAlert(const MarkdownEditInput& in, int type)
{
    const std::wstring& text = in.m_text;
    const int len = static_cast<int>(text.size());
    int s = (std::min)(in.m_selStart, in.m_selEnd);
    int e = (std::max)(in.m_selStart, in.m_selEnd);
    s = (std::max)(0, (std::min)(s, len));
    e = (std::max)(0, (std::min)(e, len));
    if (type < md_edit_alert_note || type > md_edit_alert_caution)
        return MakeResult(text, text, s, e);
    return ApplyAlert(text, s, e, in.m_lineBreak, in.m_placeholder, kAlertKeywords[type - 1]);
}

const wchar_t* MarkdownAlertKeyword(int type)
{
    if (type < md_edit_alert_note || type > md_edit_alert_caution)
        return L"";
    return kAlertKeywords[type - 1];
}

} // namespace balloonchatwnd
