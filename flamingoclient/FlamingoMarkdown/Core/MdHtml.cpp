/**
 *  从文档模型生成 HTML，说明见 MdHtml.h。
 *
 *  整篇导出（BuildMdHtml）与按选区导出（BuildMdHtmlRange，复制到剪贴板用）共用同一套生成函数：各函数都带着
 *  导出范围 HtmlRange，整篇导出时范围为「全部」，所有判断都直接放行，输出与引入按选区导出之前逐字节相同。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdHtml.h"

#include <stdio.h>

#include <algorithm>
#include <vector>

#include "MdAlert.h"       // 警告框的颜色与标题
#include "MdCodeColor.h"   // 代码块语法着色
#include "MdText.h"        // WideToUtf8

namespace flamingomd {

namespace {

//递归的最大深度：模型的嵌套层数本身已限制在 24 层以内，这里再兜底一次
const int kMaxDepth = 64;

//内嵌的样式表：与预览接近的配色，正文宽度不超过 860 像素
const char* const kCss =
    "body{margin:0;padding:24px;background:#fff;color:#2a2a2a;"
    "font-family:\"Microsoft YaHei\",\"PingFang SC\",\"Segoe UI\",sans-serif;font-size:15px;line-height:1.6;}"
    ".md{max-width:860px;margin:0 auto;}"
    "h1,h2,h3,h4,h5,h6{margin:1.2em 0 0.5em;line-height:1.3;}"
    "h1{font-size:1.8em;}h2{font-size:1.5em;}h3{font-size:1.25em;}h4{font-size:1.1em;}h5,h6{font-size:1em;}"
    "p{margin:0.6em 0;}"
    "a{color:#1a6fd6;text-decoration:underline;}"
    "code{font-family:Consolas,\"Courier New\",monospace;background:#efede9;border-radius:3px;padding:0 4px;}"
    "pre{background:#f3f1ed;border-radius:6px;padding:10px 12px;overflow:auto;}"
    "pre code{background:none;padding:0;}"
    "blockquote{margin:0.6em 0;padding:0 12px;border-left:4px solid #d9d5ce;color:#666;}"
    "table{border-collapse:collapse;margin:0.6em 0;}"
    "th,td{border:1px solid #d3d0c9;padding:4px 10px;}"
    "th{background:#ebe9e4;}"
    "hr{border:none;border-top:1px solid #d3d0c9;margin:1em 0;}"
    "ul.task{list-style:none;padding-left:1.2em;}"
    "li>p{margin:0.2em 0;}";

/**
 *  导出范围。整篇导出时 m_full 为 true；按选区导出时为渲染后文字里的 [m_begin, m_end)，且 m_begin < m_end。
 */
struct HtmlRange
{
    bool m_full;    //是否导出全部
    int  m_begin;   //选区起点（渲染后文字的下标）
    int  m_end;     //选区终点（不含）
};

//块是否在导出范围内。整篇导出时都在；没有文字的块（分隔线、空段落）起点等于终点，落在选区之内或正好在选区
//终点上时算在内；其余块与选区有交集时算在内
bool InRange(const MdBlock& block, const HtmlRange& range)
{
    if (range.m_full)
        return true;
    if (block.m_flatBegin < 0)
        return false;
    if (block.m_flatBegin == block.m_flatEnd)
        return range.m_begin <= block.m_flatBegin && block.m_flatBegin <= range.m_end;
    return block.m_flatBegin < range.m_end && block.m_flatEnd > range.m_begin;
}

//列表项是否为任务项
bool IsTaskItem(const MdBlock& item)
{
    return item.m_taskState == md_task_unchecked || item.m_taskState == md_task_checked;
}

//链接地址是否可以写进 href：只放行 http、https、mailto（模型里的地址已规范化，这里再确认一次）
bool IsSafeHref(const std::wstring& url)
{
    return url.compare(0, 7, L"http://") == 0 || url.compare(0, 8, L"https://") == 0
           || url.compare(0, 7, L"mailto:") == 0;
}

/**
 *  代码文字 [from, to) 写成 HTML：kinds 不为空（语言名认得）时按着色种类分段，着色的段包进
 *  <span style="color:#RRGGBB">（写成行内样式，复制粘贴进 Word、邮件等程序时颜色不依赖样式表）；kinds 为空时原样转义。
 */
std::string CodeSpanHtml(const std::wstring& code, const std::vector<unsigned char>& kinds, size_t from, size_t to)
{
    if (kinds.empty())
        return EscapeMdHtml(code.substr(from, to - from));
    std::string out;
    size_t a = from;
    while (a < to)
    {
        const size_t b = MdCodeColorRunEnd(code.c_str(), to, &kinds[0], a);
        const std::string piece = EscapeMdHtml(code.substr(a, b - a));
        if (kinds[a] == kMdCodeTokenPlain)
        {
            out += piece;
        }
        else
        {
            out += "<span style=\"color:#";
            out += MdCodeTokenColorHex(kinds[a]);
            out += "\">";
            out += piece;
            out += "</span>";
        }
        a = b;
    }
    return out;
}

/**
 *  代码块的正文。整块都在范围内时用原文（制表符不展开，与整篇导出相同）；只选中一部分时取显示文字（制表符已展开，
 *  与渲染后文字的下标对应）里被选中的那一段。语言名认得时按语法着色，着色种类按整块代码算，保证从字符串、注释
 *  中间开始的选区颜色正确。
 */
std::string CodeBodyHtml(const MdBlock& block, const HtmlRange& range)
{
    const bool whole = range.m_full || (range.m_begin <= block.m_flatBegin && block.m_flatEnd <= range.m_end);
    const std::wstring& code = whole ? block.m_codeRaw : block.m_code;
    std::vector<unsigned char> kinds;
    ComputeMdCodeTokenKinds(block.m_codeLang, code, kinds);
    size_t from = 0;
    size_t to = code.size();
    if (!whole)
    {
        from = static_cast<size_t>((std::max)(0, range.m_begin - block.m_flatBegin));
        to = static_cast<size_t>((std::min)(static_cast<int>(code.size()), range.m_end - block.m_flatBegin));
        if (to < from)
            to = from;
    }
    return CodeSpanHtml(code, kinds, from, to);
}

//单元格的对齐方式写成 style
const char* AlignStyle(int align)
{
    switch (align)
    {
    //居中
    case md_align_center:
        return " style=\"text-align:center\"";
    //右对齐
    case md_align_right:
        return " style=\"text-align:right\"";
    //左对齐或未指定
    default:
        return "";
    }
}

/**
 *  把一个块的行内元素写成 HTML：相邻且属于同一链接的元素放进同一个 <a>。按选区导出时只写出与选区相交的部分，
 *  文字截到选区之内；强制换行只在严格落在选区内部时写出。
 */
void AppendInlines(const MdModel& model, const MdBlock& block, const HtmlRange& range, std::string& out)
{
    int openLink = -1;
    for (size_t i = 0; i < block.m_inlines.size(); ++i)
    {
        const MdInline& in = block.m_inlines[i];
        //按选区截取：[from, to) 是本元素文字里被选中的部分
        size_t from = 0;
        size_t to = in.m_text.size();
        if (!range.m_full)
        {
            const int b = in.m_flatBegin;
            const int e = b + static_cast<int>(in.m_text.size());
            const bool selected = in.m_isBreak ? (range.m_begin < b && b < range.m_end)
                                               : (b < range.m_end && e > range.m_begin);
            if (!selected)
                continue;
            from = static_cast<size_t>((std::max)(0, range.m_begin - b));
            to = static_cast<size_t>((std::min)(static_cast<int>(in.m_text.size()), range.m_end - b));
        }
        //离开上一个链接
        if (openLink >= 0 && in.m_linkIndex != openLink)
        {
            out += "</a>";
            openLink = -1;
        }
        if (in.m_isBreak)
        {
            out += "<br>\n";
            continue;
        }
        //进入新的链接
        if (in.m_linkIndex >= 0 && in.m_linkIndex != openLink
            && static_cast<size_t>(in.m_linkIndex) < model.m_links.size())
        {
            const std::wstring& url = model.m_links[in.m_linkIndex].m_url;
            if (IsSafeHref(url))
            {
                out += "<a href=\"";
                out += EscapeMdHtml(url);
                out += "\">";
                openLink = in.m_linkIndex;
            }
        }
        std::string piece = EscapeMdHtml(in.m_text.substr(from, to - from));
        if ((in.m_style & md_style_code) != 0)
            piece = "<code>" + piece + "</code>";
        if ((in.m_style & md_style_strike) != 0)
            piece = "<del>" + piece + "</del>";
        if ((in.m_style & md_style_italic) != 0)
            piece = "<em>" + piece + "</em>";
        //下划线（<u>…</u>）
        if ((in.m_style & md_style_underline) != 0)
            piece = "<u>" + piece + "</u>";
        //标题里的文字本身就是粗体，不再重复 <strong>
        if ((in.m_style & md_style_bold) != 0 && block.m_kind != md_block_heading)
            piece = "<strong>" + piece + "</strong>";
        out += piece;
    }
    if (openLink >= 0)
        out += "</a>";
}

void AppendBlock(const MdModel& model, int index, int depth, const HtmlRange& range, std::string& out);

//依次写出在导出范围内的子块
void AppendChildren(const MdModel& model, const MdBlock& block, int depth, const HtmlRange& range, std::string& out)
{
    for (size_t i = 0; i < block.m_children.size(); ++i)
    {
        const int child = block.m_children[i];
        if (child >= 0 && static_cast<size_t>(child) < model.m_blocks.size() && InRange(model.m_blocks[child], range))
            AppendBlock(model, child, depth + 1, range, out);
    }
}

//ASCII 字母转小写（警告框类型名转 class 名用；类型名只含大写字母）
std::string LowerAscii(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        out.push_back(static_cast<char>((c >= L'A' && c <= L'Z') ? (c - L'A' + L'a') : c));
    }
    return out;
}

//写出一个警告框：带类型 class 的 <div>（与 GitHub 生成的结构一致），第一段是标题。颜色直接写在元素的 style 上，
//粘贴到 Word 等不读样式表的程序里也能保留
void AppendAlert(const MdModel& model, const MdBlock& block, int depth, const HtmlRange& range, std::string& out)
{
    const int type = block.m_alertType;
    const std::string color = MdAlertColorHex(MdAlertColor(type));
    out += "<div class=\"markdown-alert markdown-alert-";
    out += LowerAscii(MdAlertKeyword(type));
    out += "\" style=\"margin:0.6em 0;padding:6px 12px;border-left:4px solid #";
    out += color;
    out += ";background:#";
    out += MdAlertColorHex(MdAlertBackColor(type));
    out += ";\">\n<p class=\"markdown-alert-title\" style=\"margin:0.2em 0;color:#";
    out += color;
    out += ";font-weight:bold;\">";
    out += EscapeMdHtml(MdAlertTitle(type));
    out += "</p>\n";
    AppendChildren(model, block, depth, range, out);
    out += "</div>\n";
}

//写出一个表格：表头行进 <thead>，其余进 <tbody>。按选区导出时只写出与选区相交的行，这些行的单元格全部写出
//（不在选区内的单元格留空），列数不变
void AppendTable(const MdModel& model, const MdBlock& table, const HtmlRange& range, std::string& out)
{
    out += "<table>\n";
    bool bodyOpen = false;
    bool headOpen = false;
    for (size_t r = 0; r < table.m_children.size(); ++r)
    {
        const int rowIndex = table.m_children[r];
        if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= model.m_blocks.size())
            continue;
        const MdBlock& row = model.m_blocks[rowIndex];
        if (!InRange(row, range))
            continue;
        if (row.m_headerRow && !headOpen && !bodyOpen)
        {
            out += "<thead>\n";
            headOpen = true;
        }
        else if (!row.m_headerRow && !bodyOpen)
        {
            if (headOpen)
                out += "</thead>\n";
            headOpen = false;
            out += "<tbody>\n";
            bodyOpen = true;
        }
        out += "<tr>";
        for (size_t c = 0; c < row.m_children.size(); ++c)
        {
            const int cellIndex = row.m_children[c];
            if (cellIndex < 0 || static_cast<size_t>(cellIndex) >= model.m_blocks.size())
                continue;
            const MdBlock& cell = model.m_blocks[cellIndex];
            out += row.m_headerRow ? "<th" : "<td";
            out += AlignStyle(cell.m_align);
            out += ">";
            AppendInlines(model, cell, range, out);
            out += row.m_headerRow ? "</th>" : "</td>";
        }
        out += "</tr>\n";
    }
    if (headOpen)
        out += "</thead>\n";
    if (bodyOpen)
        out += "</tbody>\n";
    out += "</table>\n";
}

void AppendBlock(const MdModel& model, int index, int depth, const HtmlRange& range, std::string& out)
{
    if (index < 0 || static_cast<size_t>(index) >= model.m_blocks.size() || depth > kMaxDepth)
        return;
    const MdBlock& block = model.m_blocks[index];
    char buf[64] = { 0 };
    switch (block.m_kind)
    {
    //文档根：只写子块
    case md_block_document:
        AppendChildren(model, block, depth, range, out);
        break;
    //段落
    case md_block_paragraph:
        out += "<p>";
        AppendInlines(model, block, range, out);
        out += "</p>\n";
        break;
    //标题 1 ~ 6 级
    case md_block_heading:
    {
        const int level = (block.m_headingLevel >= 1 && block.m_headingLevel <= 6) ? block.m_headingLevel : 1;
        sprintf_s(buf, "<h%d>", level);
        out += buf;
        AppendInlines(model, block, range, out);
        sprintf_s(buf, "</h%d>\n", level);
        out += buf;
        break;
    }
    //引用：警告框另行写出
    case md_block_quote:
        if (block.m_alertType != md_alert_none)
        {
            AppendAlert(model, block, depth, range, out);
            break;
        }
        out += "<blockquote>\n";
        AppendChildren(model, block, depth, range, out);
        out += "</blockquote>\n";
        break;
    //列表：有任务项时去掉圆点，用复选框代替。按选区导出时有序列表的起始编号取第一个被选中的项的编号
    case md_block_list:
    {
        bool task = false;
        unsigned start = block.m_startNumber;
        bool firstFound = range.m_full;
        for (size_t i = 0; i < block.m_children.size(); ++i)
        {
            const int child = block.m_children[i];
            if (child < 0 || static_cast<size_t>(child) >= model.m_blocks.size())
                continue;
            if (IsTaskItem(model.m_blocks[child]))
                task = true;
            if (!firstFound && InRange(model.m_blocks[child], range))
            {
                start = block.m_startNumber + static_cast<unsigned>(i);
                firstFound = true;
            }
        }
        if (block.m_ordered)
        {
            if (start != 1)
            {
                sprintf_s(buf, "<ol start=\"%u\">\n", start);
                out += buf;
            }
            else
            {
                out += "<ol>\n";
            }
        }
        else
        {
            out += task ? "<ul class=\"task\">\n" : "<ul>\n";
        }
        AppendChildren(model, block, depth, range, out);
        out += block.m_ordered ? "</ol>\n" : "</ul>\n";
        break;
    }
    //列表项：任务项前面加禁用的复选框
    case md_block_list_item:
        out += "<li>";
        if (block.m_taskState == md_task_checked)
            out += "<input type=\"checkbox\" disabled checked> ";
        else if (block.m_taskState == md_task_unchecked)
            out += "<input type=\"checkbox\" disabled> ";
        AppendChildren(model, block, depth, range, out);
        out += "</li>\n";
        break;
    //代码块：原文转义后放进 <pre><code>，有语言名时写 class；语言名认得时按语法着色
    case md_block_code:
        out += "<pre><code";
        if (!block.m_codeLang.empty())
        {
            out += " class=\"language-";
            out += EscapeMdHtml(block.m_codeLang);
            out += "\"";
        }
        out += ">";
        out += CodeBodyHtml(block, range);
        out += "</code></pre>\n";
        break;
    //分隔线
    case md_block_rule:
        out += "<hr>\n";
        break;
    //表格
    case md_block_table:
        AppendTable(model, block, range, out);
        break;
    //表格的行与单元格由 AppendTable 处理，不会单独走到这里
    default:
        break;
    }
}

//HTML 文件的开头：文档类型、编码、标题、内嵌样式表，到 <body> 为止（含其后的换行）
void AppendHead(const std::wstring& title, std::string& out)
{
    out += "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
    out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out += "<title>";
    out += title.empty() ? std::string("Markdown") : EscapeMdHtml(title);
    out += "</title>\n<style>";
    out += kCss;
    out += "</style>\n</head>\n<body>\n";
}

} // namespace

std::string EscapeMdHtml(const std::wstring& text)
{
    std::wstring escaped;
    escaped.reserve(text.size() + text.size() / 8);
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        switch (c)
        {
        //& 必须最先转义
        case L'&':
            escaped += L"&amp;";
            break;
        //小于号
        case L'<':
            escaped += L"&lt;";
            break;
        //大于号
        case L'>':
            escaped += L"&gt;";
            break;
        //双引号（属性值里）
        case L'"':
            escaped += L"&quot;";
            break;
        //单引号
        case L'\'':
            escaped += L"&#39;";
            break;
        //其余字符原样保留
        default:
            escaped += c;
            break;
        }
    }
    return WideToUtf8(escaped);
}

std::string BuildMdHtml(const MdModel& model, const std::wstring& title)
{
    std::string out;
    out.reserve(model.m_flatText.size() * 2 + 2048);
    AppendHead(title, out);
    out += "<div class=\"md\">\n";
    HtmlRange all = { true, 0, 0 };
    if (!model.m_blocks.empty())
        AppendBlock(model, 0, 0, all, out);
    out += "</div>\n</body>\n</html>\n";
    return out;
}

std::string BuildMdHtmlRange(const MdModel& model, const std::wstring& title, int begin, int end)
{
    //顺序不限、越界截断
    const int length = static_cast<int>(model.m_flatText.size());
    if (begin > end)
        std::swap(begin, end);
    begin = (std::max)(0, (std::min)(begin, length));
    end = (std::max)(0, (std::min)(end, length));
    if (begin == end || model.m_blocks.empty())
        return std::string();

    std::string out;
    out.reserve(static_cast<size_t>(end - begin) * 2 + 2048);
    AppendHead(title, out);
    out += kMdHtmlFragmentStart;
    out += "<div class=\"md\">\n";
    HtmlRange range = { false, begin, end };
    AppendBlock(model, 0, 0, range, out);
    out += "</div>";
    out += kMdHtmlFragmentEnd;
    out += "\n</body>\n</html>\n";
    return out;
}

} // namespace flamingomd
