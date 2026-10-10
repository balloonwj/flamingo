/**
 *  从文档模型生成 .docx，说明见 MdDocx.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdDocx.h"

#include <stdio.h>
#include <map>

#include "MdAlert.h"       // 警告框的颜色与标题
#include "MdCodeColor.h"   // 代码块语法着色
#include "MdText.h"        // WideToUtf8

namespace flamingomd {

namespace {

//递归的最大深度：模型的嵌套层数本身已限制在 24 层以内，这里再兜底一次
const int kMaxDepth = 64;

//每一级缩进（twip，1/1440 英寸）：列表、引用每深一层缩进这么多
const int kIndentTwips = 420;

//列表编号与文字之间的悬挂缩进（twip）
const int kHangingTwips = 360;

//表格总宽（twip）：A4 版心约 9000 twip，各列平分
const int kTableWidthTwips = 9000;

//警告框段落左边框的线宽（八分之一磅，24 即 3 磅，与引用样式相同）与边框到文字的距离（磅）
const int kAlertBarEighths = 24;
const int kAlertBarSpacePt = 8;

//超链接关系编号的起点：rId1 为 styles、rId2 为 numbering
const int kFirstHyperlinkRid = 3;

//有序列表编号定义的起点：numId 1 给所有无序列表共用
const int kFirstOrderedNumId = 2;

//列表最多的级别数（Word 的编号定义支持 0 ~ 8 级）
const int kListLevels = 9;

//ZIP 里各结构的签名与字段取值
const uint32_t kZipLocalSig = 0x04034b50;
const uint32_t kZipCentralSig = 0x02014b50;
const uint32_t kZipEndSig = 0x06054b50;
const uint16_t kZipVersion = 20;             //需要的解压版本 2.0
const uint16_t kZipFlagUtf8 = 0x0800;        //文件名为 UTF-8
const uint16_t kZipMethodStored = 0;         //不压缩
const uint16_t kZipDosTime = 0;              //修改时间 00:00:00
const uint16_t kZipDosDate = (46 << 9) | (10 << 5) | 6;   //修改日期 2026-10-06（年份自 1980 起算）

//CRC-32 的多项式（反射形式）
const uint32_t kCrcPolynomial = 0xEDB88320u;
const int kCrcTableSize = 256;
const int kBitsPerByte = 8;

//XML 1.0 里允许出现的控制字符：制表符、换行、回车
bool IsAllowedXmlChar(wchar_t c)
{
    const wchar_t kFirstPrintable = 0x20;
    const wchar_t kNonChar1 = 0xFFFE;
    const wchar_t kNonChar2 = 0xFFFF;
    if (c == L'\t' || c == L'\n' || c == L'\r')
        return true;
    return c >= kFirstPrintable && c != kNonChar1 && c != kNonChar2;
}

//XML 文字转义：& < > " ' 转成实体，去掉 XML 不允许的字符
std::string Esc(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + text.size() / 8);
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        if (!IsAllowedXmlChar(c))
            continue;
        switch (c)
        {
        //& 必须最先转义
        case L'&':
            out += L"&amp;";
            break;
        //小于号
        case L'<':
            out += L"&lt;";
            break;
        //大于号
        case L'>':
            out += L"&gt;";
            break;
        //双引号（属性值里）
        case L'"':
            out += L"&quot;";
            break;
        //单引号
        case L'\'':
            out += L"&apos;";
            break;
        //其余字符原样保留
        default:
            out += c;
            break;
        }
    }
    return WideToUtf8(out);
}

//整数转十进制文字
std::string Num(long long value)
{
    char buf[32] = { 0 };
    _snprintf_s(buf, _TRUNCATE, "%lld", value);
    return buf;
}

//链接地址是否可以写进超链接：只放行 http、https、mailto
bool IsSafeTarget(const std::wstring& url)
{
    return url.compare(0, 7, L"http://") == 0 || url.compare(0, 8, L"https://") == 0
           || url.compare(0, 7, L"mailto:") == 0;
}

/**
 *  生成正文时的上下文。
 */
struct DocCtx
{
    const MdModel*               m_model;          //文档模型
    std::vector<std::wstring>*   m_hyperlinks;     //超链接地址，第 i 个为 rId(kFirstHyperlinkRid + i)
    std::map<int, int>           m_linkRid;        //模型里的链接序号 → 关系编号
    std::vector<unsigned>*       m_orderedStarts;  //各有序列表的起始编号，第 i 个为 numId(kFirstOrderedNumId + i)

    DocCtx()
        : m_model(NULL), m_hyperlinks(NULL), m_orderedStarts(NULL)
    {
    }
};

/**
 *  当前块所处的位置（随递归向下传递）。
 */
struct BlockPos
{
    int  m_quoteDepth;   //处在几层引用里
    int  m_listDepth;    //处在几层列表里
    int  m_numId;        //当前列表的编号定义（无序列表为 1，有序列表各自一个）；不在列表里时为 0
    int  m_taskState;    //当前列表项的任务状态，取值见 enum MdTaskState
    bool m_bold;         //强制加粗（表头单元格）
    int  m_align;        //段落对齐（表格单元格），取值见 enum MdAlign
    int  m_alertType;    //最内层的引用是警告框时为其类型（取值见 MdAlert.h），否则为 0

    BlockPos()
        : m_quoteDepth(0), m_listDepth(0), m_numId(0), m_taskState(md_task_none), m_bold(false),
          m_align(md_align_default), m_alertType(md_alert_none)
    {
    }
};

/**
 *  警告框段落的边框与底色（<w:pPr> 里 w:pBdr、w:shd 两项，须排在 w:numPr 之后、w:spacing 与 w:ind 之前）。
 *  @param type      警告框类型；为 0 时返回空串
 *  @param withShade 是否带底色：代码块段落有自己的底色，只加边框
 */
std::string AlertParaProps(int type, bool withShade)
{
    if (type == md_alert_none)
        return std::string();
    std::string out = "<w:pBdr><w:left w:val=\"single\" w:sz=\"" + Num(kAlertBarEighths) + "\" w:space=\""
                      + Num(kAlertBarSpacePt) + "\" w:color=\"" + MdAlertColorHex(MdAlertColor(type))
                      + "\"/></w:pBdr>";
    if (withShade)
        out += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"" + MdAlertColorHex(MdAlertBackColor(type)) + "\"/>";
    return out;
}

//一个文字片段的 <w:r>
std::string Run(const std::wstring& text, uint32_t style, bool forceBold, bool linkStyle)
{
    std::string rpr;
    if (linkStyle)
        rpr += "<w:rStyle w:val=\"Hyperlink\"/>";
    if ((style & md_style_code) != 0)
        rpr += "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/>";
    if (forceBold || (style & md_style_bold) != 0)
        rpr += "<w:b/>";
    if ((style & md_style_italic) != 0)
        rpr += "<w:i/>";
    if ((style & md_style_strike) != 0)
        rpr += "<w:strike/>";
    //下划线（<u>…</u>）：WordprocessingML 里 w:u 须排在 w:strike 之后、w:shd 之前
    if ((style & md_style_underline) != 0)
        rpr += "<w:u w:val=\"single\"/>";
    if ((style & md_style_code) != 0)
        rpr += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"EFEDE9\"/>";
    std::string out = "<w:r>";
    if (!rpr.empty())
        out += "<w:rPr>" + rpr + "</w:rPr>";
    out += "<w:t xml:space=\"preserve\">" + Esc(text) + "</w:t></w:r>";
    return out;
}

//一个块的行内元素写成若干 <w:r>：同一链接的相邻元素放进同一个 <w:hyperlink>
std::string Runs(DocCtx& ctx, const MdBlock& block, bool forceBold)
{
    std::string out;
    int openLink = -1;
    for (size_t i = 0; i < block.m_inlines.size(); ++i)
    {
        const MdInline& in = block.m_inlines[i];
        //离开上一个链接
        if (openLink >= 0 && in.m_linkIndex != openLink)
        {
            out += "</w:hyperlink>";
            openLink = -1;
        }
        if (in.m_isBreak)
        {
            out += "<w:r><w:br/></w:r>";
            continue;
        }
        //进入新的链接：每个链接一个外部关系
        if (in.m_linkIndex >= 0 && in.m_linkIndex != openLink
            && static_cast<size_t>(in.m_linkIndex) < ctx.m_model->m_links.size())
        {
            const std::wstring& url = ctx.m_model->m_links[in.m_linkIndex].m_url;
            if (IsSafeTarget(url))
            {
                std::map<int, int>::const_iterator it = ctx.m_linkRid.find(in.m_linkIndex);
                int rid = 0;
                if (it == ctx.m_linkRid.end())
                {
                    rid = kFirstHyperlinkRid + static_cast<int>(ctx.m_hyperlinks->size());
                    ctx.m_hyperlinks->push_back(url);
                    ctx.m_linkRid[in.m_linkIndex] = rid;
                }
                else
                {
                    rid = it->second;
                }
                out += "<w:hyperlink r:id=\"rId" + Num(rid) + "\" w:history=\"1\">";
                openLink = in.m_linkIndex;
            }
        }
        //标题里的文字本身就是粗体（样式里定义），不再重复 <w:b/>
        const uint32_t style = (block.m_kind == md_block_heading) ? (in.m_style & ~static_cast<uint32_t>(md_style_bold))
                                                                  : in.m_style;
        out += Run(in.m_text, style, forceBold, openLink >= 0);
    }
    if (openLink >= 0)
        out += "</w:hyperlink>";
    return out;
}

//段落属性里的左缩进（引用与列表的层数之和）
std::string Indent(int levels, bool hanging)
{
    if (levels <= 0)
        return std::string();
    std::string out = "<w:ind w:left=\"" + Num(levels * kIndentTwips) + "\"";
    if (hanging)
        out += " w:hanging=\"" + Num(kHangingTwips) + "\"";
    return out + "/>";
}

//段落对齐
std::string Justify(int align)
{
    switch (align)
    {
    //居中
    case md_align_center:
        return "<w:jc w:val=\"center\"/>";
    //右对齐
    case md_align_right:
        return "<w:jc w:val=\"right\"/>";
    //左对齐或未指定
    default:
        return std::string();
    }
}

void AppendBlock(DocCtx& ctx, int index, BlockPos pos, bool& pendingNumber, int depth, std::string& out);

//依次写出全部子块
void AppendChildren(DocCtx& ctx, const MdBlock& block, const BlockPos& pos, bool& pendingNumber, int depth,
                    std::string& out)
{
    for (size_t i = 0; i < block.m_children.size(); ++i)
        AppendBlock(ctx, block.m_children[i], pos, pendingNumber, depth + 1, out);
}

//写出一个段落或标题：列表项的第一个段落带编号（任务项改为 ☐ / ☑ 前缀），其余段落只缩进
void AppendTextParagraph(DocCtx& ctx, const MdBlock& block, const BlockPos& pos, bool& pendingNumber, std::string& out)
{
    std::string ppr;
    if (block.m_kind == md_block_heading)
    {
        const int level = (block.m_headingLevel >= 1 && block.m_headingLevel <= 6) ? block.m_headingLevel : 1;
        ppr += "<w:pStyle w:val=\"Heading" + Num(level) + "\"/>";
    }
    else if (pos.m_quoteDepth > 0 && pos.m_alertType == md_alert_none)
    {
        //普通引用用引用样式（灰色左边框、灰色文字）；警告框里的段落用正文颜色，边框与底色直接写在段落上
        ppr += "<w:pStyle w:val=\"Quote\"/>";
    }
    std::string prefix;
    const bool numbered = pendingNumber && pos.m_listDepth > 0 && pos.m_taskState == md_task_none;
    if (numbered)
    {
        const int level = (pos.m_listDepth - 1 < kListLevels) ? pos.m_listDepth - 1 : kListLevels - 1;
        ppr += "<w:numPr><w:ilvl w:val=\"" + Num(level) + "\"/><w:numId w:val=\"" + Num(pos.m_numId) + "\"/></w:numPr>";
        ppr += AlertParaProps(pos.m_alertType, true);
    }
    else
    {
        ppr += AlertParaProps(pos.m_alertType, true);
        ppr += Indent(pos.m_listDepth + pos.m_quoteDepth, false);
    }
    //任务项：第一个段落前面加勾选框符号
    if (pendingNumber && pos.m_taskState == md_task_checked)
        prefix = "\xE2\x98\x91 ";
    else if (pendingNumber && pos.m_taskState == md_task_unchecked)
        prefix = "\xE2\x98\x90 ";
    pendingNumber = false;
    ppr += Justify(pos.m_align);
    out += "<w:p>";
    if (!ppr.empty())
        out += "<w:pPr>" + ppr + "</w:pPr>";
    if (!prefix.empty())
        out += "<w:r><w:t xml:space=\"preserve\">" + prefix + "</w:t></w:r>";
    out += Runs(ctx, block, pos.m_bold);
    out += "</w:p>";
}

/**
 *  代码里 [begin, end) 这一段（不含换行与制表符）写成 run。kinds 不为空（语言名认得）时按着色种类分成几个 run，
 *  着色的 run 带 w:color；kinds 为空时整段一个 run。
 */
void AppendCodeRuns(const std::wstring& code, size_t begin, size_t end, const std::vector<unsigned char>& kinds,
                    std::string& out)
{
    size_t a = begin;
    while (a < end)
    {
        const size_t b = kinds.empty() ? end : MdCodeColorRunEnd(code.c_str(), end, &kinds[0], a);
        const int kind = kinds.empty() ? kMdCodeTokenPlain : kinds[a];
        out += "<w:r>";
        if (kind != kMdCodeTokenPlain)
            out += "<w:rPr><w:color w:val=\"" + MdCodeTokenColorHex(kind) + "\"/></w:rPr>";
        out += "<w:t xml:space=\"preserve\">" + Esc(code.substr(a, b - a)) + "</w:t></w:r>";
        a = b;
    }
}

//写出代码块：每行一个 CodeBlock 段落，制表符写成 <w:tab/>；语言名认得时按语法着色
void AppendCode(const MdBlock& block, const BlockPos& pos, std::string& out)
{
    const std::wstring& code = block.m_codeRaw;
    //语言名认得时 kinds 与 code 逐字符对齐，否则为空
    std::vector<unsigned char> kinds;
    ComputeMdCodeTokenKinds(block.m_codeLang, code, kinds);
    size_t begin = 0;
    while (begin <= code.size())
    {
        size_t end = code.find(L'\n', begin);
        if (end == std::wstring::npos)
            end = code.size();
        //行尾（不含 \r\n 里的 \r）
        size_t lineEnd = end;
        if (lineEnd > begin && code[lineEnd - 1] == L'\r')
            --lineEnd;
        out += "<w:p><w:pPr><w:pStyle w:val=\"CodeBlock\"/>" + AlertParaProps(pos.m_alertType, false)
               + Indent(pos.m_listDepth + pos.m_quoteDepth, false) + "</w:pPr>";
        size_t seg = begin;
        while (seg <= lineEnd)
        {
            size_t tab = code.find(L'\t', seg);
            if (tab == std::wstring::npos || tab > lineEnd)
                tab = lineEnd;
            if (tab > seg)
                AppendCodeRuns(code, seg, tab, kinds, out);
            if (tab < lineEnd)
                out += "<w:r><w:tab/></w:r>";
            seg = tab + 1;
        }
        out += "</w:p>";
        if (end >= code.size())
            break;
        begin = end + 1;
    }
}

//写出表格：表头行重复显示、加底色与粗体；缺少的单元格补空白格，保证每行列数一致
void AppendTable(DocCtx& ctx, const MdBlock& table, const BlockPos& pos, int depth, std::string& out)
{
    const int columns = (table.m_columnCount > 0) ? table.m_columnCount : 1;
    const int colWidth = kTableWidthTwips / columns;
    out += "<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/><w:tblW w:w=\"0\" w:type=\"auto\"/>"
           "<w:tblLook w:val=\"04A0\" w:firstRow=\"1\" w:lastRow=\"0\" w:firstColumn=\"0\" w:lastColumn=\"0\" "
           "w:noHBand=\"0\" w:noVBand=\"1\"/></w:tblPr><w:tblGrid>";
    for (int c = 0; c < columns; ++c)
        out += "<w:gridCol w:w=\"" + Num(colWidth) + "\"/>";
    out += "</w:tblGrid>";
    for (size_t r = 0; r < table.m_children.size(); ++r)
    {
        const int rowIndex = table.m_children[r];
        if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= ctx.m_model->m_blocks.size())
            continue;
        const MdBlock& row = ctx.m_model->m_blocks[rowIndex];
        out += "<w:tr>";
        if (row.m_headerRow)
            out += "<w:trPr><w:tblHeader/></w:trPr>";
        int written = 0;
        for (size_t c = 0; c < row.m_children.size() && written < columns; ++c, ++written)
        {
            const int cellIndex = row.m_children[c];
            if (cellIndex < 0 || static_cast<size_t>(cellIndex) >= ctx.m_model->m_blocks.size())
                continue;
            const MdBlock& cell = ctx.m_model->m_blocks[cellIndex];
            out += "<w:tc><w:tcPr><w:tcW w:w=\"" + Num(colWidth) + "\" w:type=\"dxa\"/>";
            if (row.m_headerRow)
                out += "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"EBE9E4\"/>";
            out += "</w:tcPr>";
            BlockPos cellPos;
            cellPos.m_bold = row.m_headerRow;
            cellPos.m_align = cell.m_align;
            bool noNumber = false;
            AppendTextParagraph(ctx, cell, cellPos, noNumber, out);
            out += "</w:tc>";
        }
        //补空白格：每个单元格至少要有一个段落
        for (; written < columns; ++written)
            out += "<w:tc><w:tcPr><w:tcW w:w=\"" + Num(colWidth) + "\" w:type=\"dxa\"/></w:tcPr><w:p/></w:tc>";
        out += "</w:tr>";
    }
    out += "</w:tbl>";
    //表格后面紧跟一个空段落：Word 要求表格之后还有段落，相邻两个表格也不会粘在一起
    out += "<w:p/>";
    (void)pos;
    (void)depth;
}

void AppendBlock(DocCtx& ctx, int index, BlockPos pos, bool& pendingNumber, int depth, std::string& out)
{
    if (index < 0 || static_cast<size_t>(index) >= ctx.m_model->m_blocks.size() || depth > kMaxDepth)
        return;
    const MdBlock& block = ctx.m_model->m_blocks[index];
    switch (block.m_kind)
    {
    //文档根：只写子块
    case md_block_document:
        AppendChildren(ctx, block, pos, pendingNumber, depth, out);
        break;
    //段落与标题
    case md_block_paragraph:
    case md_block_heading:
        AppendTextParagraph(ctx, block, pos, pendingNumber, out);
        break;
    //引用：子块多一层引用。警告框先写一段标题（加粗、类型的主色），框内段落带彩色左边框与底色
    case md_block_quote:
        pos.m_quoteDepth += 1;
        pos.m_alertType = block.m_alertType;
        if (block.m_alertType != md_alert_none)
        {
            pendingNumber = false;
            out += "<w:p><w:pPr>" + AlertParaProps(block.m_alertType, true)
                   + Indent(pos.m_listDepth + pos.m_quoteDepth, false) + "</w:pPr><w:r><w:rPr><w:b/><w:color w:val=\""
                   + MdAlertColorHex(MdAlertColor(block.m_alertType)) + "\"/></w:rPr><w:t xml:space=\"preserve\">"
                   + Esc(MdAlertTitle(block.m_alertType)) + "</w:t></w:r></w:p>";
        }
        AppendChildren(ctx, block, pos, pendingNumber, depth, out);
        break;
    //列表：无序列表共用编号定义 1，有序列表各自一个编号定义并记下起始编号
    case md_block_list:
        pos.m_listDepth += 1;
        if (block.m_ordered)
        {
            pos.m_numId = kFirstOrderedNumId + static_cast<int>(ctx.m_orderedStarts->size());
            ctx.m_orderedStarts->push_back(block.m_startNumber);
        }
        else
        {
            pos.m_numId = 1;
        }
        AppendChildren(ctx, block, pos, pendingNumber, depth, out);
        break;
    //列表项：第一个段落带编号（或勾选框）
    case md_block_list_item:
    {
        pos.m_taskState = block.m_taskState;
        bool itemPending = true;
        AppendChildren(ctx, block, pos, itemPending, depth, out);
        break;
    }
    //代码块
    case md_block_code:
        pendingNumber = false;
        AppendCode(block, pos, out);
        break;
    //分隔线：段落下边框
    case md_block_rule:
        out += "<w:p><w:pPr><w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"D3D0C9\"/>"
               "</w:pBdr></w:pPr></w:p>";
        break;
    //表格
    case md_block_table:
        pendingNumber = false;
        AppendTable(ctx, block, pos, depth, out);
        break;
    //表格的行与单元格由 AppendTable 处理，不会单独走到这里
    default:
        break;
    }
}

//word/styles.xml：默认字体与段落间距、标题 1 ~ 6、引用、代码块、超链接、表格网格
std::string StylesXml()
{
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults><w:rPrDefault><w:rPr>"
        "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Microsoft YaHei\" w:cs=\"Calibri\"/>"
        "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/><w:lang w:val=\"en-US\" w:eastAsia=\"zh-CN\"/>"
        "</w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"120\" w:line=\"300\" w:lineRule=\"auto\"/>"
        "</w:pPr></w:pPrDefault></w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>";
    //标题 1 ~ 6：字号（半点）依次减小，加粗，段前段后留白，大纲级别供导航窗格与目录使用
    const int sizes[6] = { 36, 32, 28, 26, 24, 24 };
    for (int level = 1; level <= 6; ++level)
    {
        out += "<w:style w:type=\"paragraph\" w:styleId=\"Heading" + Num(level) + "\"><w:name w:val=\"heading "
               + Num(level) + "\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
               "<w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"120\"/><w:outlineLvl w:val=\""
               + Num(level - 1) + "\"/></w:pPr><w:rPr><w:b/><w:bCs/><w:sz w:val=\"" + Num(sizes[level - 1])
               + "\"/><w:szCs w:val=\"" + Num(sizes[level - 1]) + "\"/></w:rPr></w:style>";
    }
    out +=
        "<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/>"
        "<w:qFormat/><w:pPr><w:pBdr><w:left w:val=\"single\" w:sz=\"24\" w:space=\"8\" w:color=\"D9D5CE\"/></w:pBdr>"
        "<w:ind w:left=\"420\"/></w:pPr><w:rPr><w:color w:val=\"666666\"/></w:rPr></w:style>"
        "<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"CodeBlock\"><w:name w:val=\"Code Block\"/>"
        "<w:basedOn w:val=\"Normal\"/><w:pPr><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F3F1ED\"/>"
        "<w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr>"
        "<w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:cs=\"Consolas\"/><w:sz w:val=\"20\"/>"
        "<w:szCs w:val=\"20\"/></w:rPr></w:style>"
        "<w:style w:type=\"character\" w:styleId=\"Hyperlink\"><w:name w:val=\"Hyperlink\"/>"
        "<w:rPr><w:color w:val=\"1A6FD6\"/><w:u w:val=\"single\"/></w:rPr></w:style>"
        "<w:style w:type=\"table\" w:styleId=\"TableGrid\"><w:name w:val=\"Table Grid\"/><w:tblPr><w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D3D0C9\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D3D0C9\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D3D0C9\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D3D0C9\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D3D0C9\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"D3D0C9\"/>"
        "</w:tblBorders><w:tblCellMar><w:left w:w=\"108\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/>"
        "</w:tblCellMar></w:tblPr></w:style>"
        "</w:styles>";
    return out;
}

//一个编号定义里第 level 级的 <w:lvl>
std::string Level(int level, bool ordered)
{
    //无序列表三种符号循环使用：实心圆、空心圆、实心方块
    const char* const bullets[3] = { "\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA" };
    std::string out = "<w:lvl w:ilvl=\"" + Num(level) + "\"><w:start w:val=\"1\"/>";
    if (ordered)
        out += "<w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%" + Num(level + 1) + ".\"/>";
    else
        out += std::string("<w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"") + bullets[level % 3] + "\"/>";
    out += "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"" + Num((level + 1) * kIndentTwips) + "\" w:hanging=\""
           + Num(kHangingTwips) + "\"/></w:pPr></w:lvl>";
    return out;
}

//word/numbering.xml：编号定义 0 为无序、1 为有序；numId 1 给无序列表共用，每个有序列表一个 numId 并覆写起始编号
std::string NumberingXml(const std::vector<unsigned>& orderedStarts)
{
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:abstractNum w:abstractNumId=\"0\"><w:multiLevelType w:val=\"hybridMultilevel\"/>";
    for (int level = 0; level < kListLevels; ++level)
        out += Level(level, false);
    out += "</w:abstractNum><w:abstractNum w:abstractNumId=\"1\"><w:multiLevelType w:val=\"hybridMultilevel\"/>";
    for (int level = 0; level < kListLevels; ++level)
        out += Level(level, true);
    out += "</w:abstractNum><w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>";
    for (size_t i = 0; i < orderedStarts.size(); ++i)
    {
        out += "<w:num w:numId=\"" + Num(kFirstOrderedNumId + static_cast<int>(i))
               + "\"><w:abstractNumId w:val=\"1\"/>";
        //每一级都从这个列表的起始编号重新开始，嵌套的有序列表因此各自独立编号
        for (int level = 0; level < kListLevels; ++level)
        {
            out += "<w:lvlOverride w:ilvl=\"" + Num(level) + "\"><w:startOverride w:val=\""
                   + Num(orderedStarts[i]) + "\"/></w:lvlOverride>";
        }
        out += "</w:num>";
    }
    out += "</w:numbering>";
    return out;
}

//word/_rels/document.xml.rels：样式、编号与各超链接
std::string DocumentRelsXml(const std::vector<std::wstring>& hyperlinks)
{
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
        "Target=\"styles.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" "
        "Target=\"numbering.xml\"/>";
    for (size_t i = 0; i < hyperlinks.size(); ++i)
    {
        out += "<Relationship Id=\"rId" + Num(kFirstHyperlinkRid + static_cast<int>(i))
               + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\""
               + Esc(hyperlinks[i]) + "\" TargetMode=\"External\"/>";
    }
    out += "</Relationships>";
    return out;
}

//小端写入 16 位与 32 位整数
void Put16(std::string& out, uint16_t v)
{
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> kBitsPerByte) & 0xFF);
}

void Put32(std::string& out, uint32_t v)
{
    Put16(out, static_cast<uint16_t>(v & 0xFFFF));
    Put16(out, static_cast<uint16_t>((v >> (kBitsPerByte * 2)) & 0xFFFF));
}

} // namespace

uint32_t MdCrc32(const void* data, size_t bytes)
{
    static uint32_t table[kCrcTableSize];
    static bool ready = false;
    if (!ready)
    {
        for (int n = 0; n < kCrcTableSize; ++n)
        {
            uint32_t c = static_cast<uint32_t>(n);
            for (int k = 0; k < kBitsPerByte; ++k)
                c = (c & 1) ? (kCrcPolynomial ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> kBitsPerByte);
    return crc ^ 0xFFFFFFFFu;
}

std::string BuildMdStoredZip(const std::vector<MdZipEntry>& entries)
{
    std::string out;
    std::string central;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const MdZipEntry& e = entries[i];
        const uint32_t crc = MdCrc32(e.m_data.data(), e.m_data.size());
        const uint32_t size = static_cast<uint32_t>(e.m_data.size());
        const uint32_t offset = static_cast<uint32_t>(out.size());
        //本地文件头
        Put32(out, kZipLocalSig);
        Put16(out, kZipVersion);
        Put16(out, kZipFlagUtf8);
        Put16(out, kZipMethodStored);
        Put16(out, kZipDosTime);
        Put16(out, kZipDosDate);
        Put32(out, crc);
        Put32(out, size);
        Put32(out, size);
        Put16(out, static_cast<uint16_t>(e.m_name.size()));
        Put16(out, 0);
        out += e.m_name;
        out += e.m_data;
        //中央目录项
        Put32(central, kZipCentralSig);
        Put16(central, kZipVersion);
        Put16(central, kZipVersion);
        Put16(central, kZipFlagUtf8);
        Put16(central, kZipMethodStored);
        Put16(central, kZipDosTime);
        Put16(central, kZipDosDate);
        Put32(central, crc);
        Put32(central, size);
        Put32(central, size);
        Put16(central, static_cast<uint16_t>(e.m_name.size()));
        Put16(central, 0);   //扩展字段长度
        Put16(central, 0);   //注释长度
        Put16(central, 0);   //起始磁盘号
        Put16(central, 0);   //内部属性
        Put32(central, 0);   //外部属性
        Put32(central, offset);
        central += e.m_name;
    }
    const uint32_t centralOffset = static_cast<uint32_t>(out.size());
    out += central;
    //中央目录结束记录
    Put32(out, kZipEndSig);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, static_cast<uint16_t>(entries.size()));
    Put16(out, static_cast<uint16_t>(entries.size()));
    Put32(out, static_cast<uint32_t>(central.size()));
    Put32(out, centralOffset);
    Put16(out, 0);
    return out;
}

std::string BuildMdDocxDocumentXml(const MdModel& model, std::vector<std::wstring>& hyperlinks,
                                   std::vector<unsigned>& orderedStarts)
{
    hyperlinks.clear();
    orderedStarts.clear();
    DocCtx ctx;
    ctx.m_model = &model;
    ctx.m_hyperlinks = &hyperlinks;
    ctx.m_orderedStarts = &orderedStarts;
    std::string body;
    if (!model.m_blocks.empty())
    {
        bool pending = false;
        AppendBlock(ctx, 0, BlockPos(), pending, 0, body);
    }
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><w:body>";
    out += body;
    //页面：A4（11906 × 16838 twip），四边各 2 厘米（1134 twip）
    out += "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" "
           "w:left=\"1134\" w:header=\"567\" w:footer=\"567\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>";
    return out;
}

std::string BuildMdDocx(const MdModel& model)
{
    std::vector<std::wstring> hyperlinks;
    std::vector<unsigned> orderedStarts;
    const std::string documentXml = BuildMdDocxDocumentXml(model, hyperlinks, orderedStarts);
    std::vector<MdZipEntry> entries;
    MdZipEntry e;
    e.m_name = "[Content_Types].xml";
    e.m_data =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/numbering.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>"
        "</Types>";
    entries.push_back(e);
    e.m_name = "_rels/.rels";
    e.m_data =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/></Relationships>";
    entries.push_back(e);
    e.m_name = "word/document.xml";
    e.m_data = documentXml;
    entries.push_back(e);
    e.m_name = "word/styles.xml";
    e.m_data = StylesXml();
    entries.push_back(e);
    e.m_name = "word/numbering.xml";
    e.m_data = NumberingXml(orderedStarts);
    entries.push_back(e);
    e.m_name = "word/_rels/document.xml.rels";
    e.m_data = DocumentRelsXml(hyperlinks);
    entries.push_back(e);
    return BuildMdStoredZip(entries);
}

} // namespace flamingomd
