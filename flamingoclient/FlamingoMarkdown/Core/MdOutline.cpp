/**
 *  卡片标题与摘要的生成，说明见 MdOutline.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdOutline.h"

#include <vector>

namespace flamingomd {

namespace {

//一个有文字的块
struct TextBlock
{
    int          m_kind;   //块的种类，取值见 enum MdBlockKind
    std::wstring m_text;   //块的文字：行内元素依次拼接，强制换行为 \n；代码块为显示文字
};

//是否为空白字符（空格、制表符、换行、全角空格）
bool IsBlank(wchar_t c)
{
    const wchar_t kIdeographicSpace = 0x3000;
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == kIdeographicSpace;
}

//把一段文字里的空白压成单个空格并去掉首尾空白
std::wstring Collapse(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        if (IsBlank(c))
        {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace)
            out += L' ';
        pendingSpace = false;
        out += c;
    }
    return out;
}

//按文档顺序收集有文字的块（段落、标题、单元格、代码块）
void CollectTextBlocks(const MdModel& model, std::vector<TextBlock>& out)
{
    for (size_t i = 0; i < model.m_blocks.size(); ++i)
    {
        const MdBlock& block = model.m_blocks[i];
        TextBlock tb;
        tb.m_kind = block.m_kind;
        if (block.m_kind == md_block_code)
        {
            tb.m_text = block.m_code;
        }
        else if (MdModel::IsTextBlock(block.m_kind))
        {
            for (size_t k = 0; k < block.m_inlines.size(); ++k)
                tb.m_text += block.m_inlines[k].m_isBreak ? std::wstring(L"\n") : block.m_inlines[k].m_text;
        }
        else
        {
            continue;
        }
        if (!Collapse(tb.m_text).empty())
            out.push_back(tb);
    }
}

//截到最大长度，超出时末尾加省略号
std::wstring Clip(const std::wstring& text, size_t maxChars)
{
    const wchar_t kEllipsis = 0x2026;
    if (text.size() <= maxChars)
        return text;
    return text.substr(0, maxChars) + kEllipsis;
}

} // namespace

void BuildMdCardText(const MdModel& model, std::wstring& title, std::wstring& summary)
{
    title.clear();
    summary.clear();
    std::vector<TextBlock> blocks;
    CollectTextBlocks(model, blocks);
    if (blocks.empty())
        return;

    //标题：第一个标题块；没有时取第一个块的第一行
    size_t titleIndex = blocks.size();
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (blocks[i].m_kind == md_block_heading)
        {
            titleIndex = i;
            break;
        }
    }
    std::wstring rest;   //标题取自第一行时，该块剩下的文字
    if (titleIndex < blocks.size())
    {
        title = Collapse(blocks[titleIndex].m_text);
    }
    else
    {
        titleIndex = 0;
        const std::wstring& first = blocks[0].m_text;
        //跳过开头的空行，取第一行有文字的内容
        size_t begin = 0;
        size_t end = first.find(L'\n');
        while (end != std::wstring::npos && Collapse(first.substr(begin, end - begin)).empty())
        {
            begin = end + 1;
            end = first.find(L'\n', begin);
        }
        if (end == std::wstring::npos)
        {
            title = Collapse(first.substr(begin));
        }
        else
        {
            title = Collapse(first.substr(begin, end - begin));
            rest = first.substr(end + 1);
        }
    }
    title = Clip(title, kMdCardTitleMaxChars);

    //摘要：其余各块的文字依次拼接
    std::wstring joined = rest;
    for (size_t i = 0; i < blocks.size() && joined.size() <= kMdCardSummaryMaxChars * 2; ++i)
    {
        if (i == titleIndex)
            continue;
        joined += L' ';
        joined += blocks[i].m_text;
    }
    summary = Clip(Collapse(joined), kMdCardSummaryMaxChars);
}

} // namespace flamingomd
