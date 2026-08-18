/**
 *  ChatTextSelection.h
 *
 *  聊天气泡内"选中文字并复制"的<u>纯逻辑</u>部分 —— 坐标到字符下标的命中测试、
 *  选区归一化、选区高亮矩形的计算、选中文本的提取、双击选词。
 *
 *  不依赖 GDI / HWND / ATL / balloonui，只用 std::wstring 与 std::vector，便于被
 *  FlamingoUnit 测试工程直接调用断言。真实字体度量（每个字符多宽、折到第几行）
 *  由 ChatBubble 用现有的 LayoutInlineContent 算好后填进 SelLayout 喂进来 ——
 *  本模块只认"排版结果"，不关心它是怎么排出来的。
 *
 *  字符口径：全文按 UTF-16 字符（WCHAR）计数；<u>一个内联表情占一个字符位</u>，
 *  在扁平文本里用 U+FFFC（OBJECT REPLACEMENT CHARACTER）占位。这样选区就是一段
 *  连续的下标区间，跨文字与表情都不用特殊处理。
 *
 *  本文件不 include stdafx.h，工程里须把对应 .cpp 标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <string>
#include <vector>
#include <utility>   // std::pair —— HitTestCharRanges 的区间参数

namespace balloonchatwnd {

// 扁平文本里代表"一个内联表情"的占位字符（U+FFFC OBJECT REPLACEMENT CHARACTER）。
// 选中并复制时会被替换成可读文案（见 ExtractSelectedText 的 faceLabel）。
const wchar_t kSelFacePlaceholder = 0xFFFC;

/**
 *  排版后的一"段"：同一行内一段连续内容（一段文字，或一个内联表情格）。
 */
struct SelRun
{
    int  m_charBegin;   // 本段首字符在<u>全文</u>里的下标
    int  m_charCount;   // 本段字符数；表情段恒为 1
    int  m_left;        // 本段左边界 x（相对文本区原点）
    int  m_right;       // 本段右边界 x

    // 本段内每个字符的<u>右</u>边界 x，长度须等于 m_charCount。第 k 个字符的左边界
    // 是 (k==0 ? m_left : m_charRights[k-1])。表情段填它自己的右边界即可。
    std::vector<int> m_charRights;

    SelRun()
        : m_charBegin(0)
        , m_charCount(0)
        , m_left(0)
        , m_right(0)
    {
    }
};

/**
 *  排版后的一行。
 */
struct SelLine
{
    int                 m_top;      // 行上边界 y（相对文本区原点）
    int                 m_bottom;   // 行下边界 y
    std::vector<SelRun> m_runs;     // 行内各段，按从左到右排列

    SelLine()
        : m_top(0)
        , m_bottom(0)
    {
    }
};

/**
 *  一条消息的完整排版结果。
 */
struct SelLayout
{
    std::vector<SelLine> m_lines;        // 各行，按从上到下排列
    int                  m_totalChars;   // 全文字符总数（含表情占位）

    SelLayout()
        : m_totalChars(0)
    {
    }
};

/**
 *  选区高亮矩形（相对文本区原点）。
 */
struct SelRect
{
    int m_left;
    int m_top;
    int m_right;
    int m_bottom;

    SelRect()
        : m_left(0)
        , m_top(0)
        , m_right(0)
        , m_bottom(0)
    {
    }
};

/**
 *  把一个点映射成字符下标（可作选区端点，取值 [0, m_totalChars]）。
 *
 *  规则：落在某个字符左半边取该字符<u>之前</u>的位置、右半边取<u>之后</u>，与常规
 *  文本选择手感一致；点在行首之左归到该行首字符，行尾之右归到该行末尾（不含换行）；
 *  点在首行之上归 0，末行之下归全文末尾。
 *    layout：排版结果。
 *    x / y：点坐标（相对文本区原点）。
 *  @return 字符下标；layout 为空时恒返回 0。
 */
int CharIndexFromPoint(const SelLayout& layout, int x, int y);

/**
 *  把"锚点 + 当前点"归一化成前闭后开区间 [start, end)。反向拖拽（锚点在后）
 *  也能得到正确区间；两者相等时得到空区间。
 *    anchor / caret：两个端点下标。
 *    startOut / endOut：[出参] 归一化后的区间。
 */
void NormalizeSelRange(int anchor, int caret, int& startOut, int& endOut);

/**
 *  算出选区 [start, end) 的高亮矩形。跨行时每行一个矩形：首行从选起点到行尾、
 *  中间行是整行、末行从行首到选终点。空区间返回空。
 *    layout：排版结果。
 *    start / end：选区（会自行夹到合法范围）。
 *  @return 高亮矩形列表，按行序。
 */
std::vector<SelRect> BuildSelectionRects(const SelLayout& layout, int start, int end);

/**
 *  从扁平文本里取出选中的那一段，并把内联表情占位符替换成可读文案。
 *    flatText： 全文（表情用 kSelFacePlaceholder 占位）。
 *    start / end：选区（会自行夹到合法范围）。
 *    faceLabel：表情替换成的文案，一般传 L"[表情]"。
 *  @return 选中文本；空区间返回空串。
 */
std::wstring ExtractSelectedText(const std::wstring& flatText, int start, int end,
                                 const std::wstring& faceLabel);

/**
 *  双击选词：给出 index 处应当选中的区间。
 *
 *  规则：落在连续 ASCII 词字符（字母 / 数字 / 下划线）上选中整个词；落在连续空白
 *  上选中整段空白；其余（汉字、标点、表情占位）只选那<u>一个</u>字符 —— 中文没有
 *  词边界，逐字选是各家 IM 的通行做法。
 *    flatText：全文。
 *    index：   双击处的字符下标（越界会夹到合法范围）。
 *    startOut / endOut：[出参] 选中区间。
 */
void WordRangeAt(const std::wstring& flatText, int index, int& startOut, int& endOut);

/**
 *  在若干字符区间里找出「包含给定点」的那一个。
 *
 *  与 CharIndexFromPoint 的区别是本函数<u>要求真正落在字形矩形之内</u>：
 *  CharIndexFromPoint 是为拖选服务的，点在行尾之外也会归到最近的字符上；而这里
 *  服务于「点没点中某段特定文字」（如气泡里的 @某人），点空了就必须算没点中，
 *  否则用户在气泡空白处右键也会弹出 @ 的菜单。
 *    layout：字符级排版结果。
 *    ranges：各待测区间，前闭后开 [first, second)；区间之间不必有序、允许重叠，
 *            重叠时返回<u>下标最小</u>的那个。
 *    x / y： 点坐标（相对文本区原点，与 layout 同一坐标系）。
 *  @return 命中的区间在 ranges 里的下标；一个都没命中返回 -1。
 */
int HitTestCharRanges(const SelLayout& layout,
                      const std::vector<std::pair<int, int> >& ranges,
                      int x, int y);

} // namespace balloonchatwnd
