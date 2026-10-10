/**
 *  Markdown 文档模型生成器的实现，说明见 MdBuilder.h。
 *
 *  md4c 以回调的方式报告解析结果：进入 / 离开块、进入 / 离开行内元素、一段文字。本文件用一个栈记录当前打开的块，
 *  用另一个栈记录行内样式与链接，把文字追加到当前的文字块里。
 *
 *  需要特别处理的几种情况：
 *    - 紧凑列表的列表项里，md4c 不报告段落，文字直接出现在列表项里。此时补一个「隐式段落」接住文字；之后一旦
 *      有任何块进入或离开，隐式段落即结束，下一段文字另起一个。
 *    - 超过嵌套层数或块数上限的块不建立，记为「跳过」；离开时按记录配对。超出列数的表格单元格同样跳过，
 *      并丢弃其中的文字。
 *    - 回调在 C 代码中被调用，C++ 异常不能穿过 md4c 的栈帧：每个回调都捕获异常，记下错误并返回非 0 让解析中止。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdBuilder.h"

#include <new>

#include "md4c.h"

#include "../FlamingoMarkdownApi.h"
#include "MdAlert.h"     // MdAlertTypeFromKeyword：警告框的类型
#include "MdHostLog.h"
#include "MdText.h"

namespace flamingomd {

namespace {

//回调返回给 md4c 的值：0 继续解析，非 0 中止
const int kContinueParse = 0;
const int kAbortParse = 1;

//替换 0 号字符与非法字符用的 U+FFFD（CommonMark 的规定）
const wchar_t kReplacementChar = static_cast<wchar_t>(0xFFFD);

//行内原始 HTML 里认作下划线的标签（docs/Markdown消息方案.md 第 13 节）：开标签加 1、闭标签减 1、其余标签为 0
const int kUnderlineOpen = 1;
const int kUnderlineClose = -1;
const int kNotUnderlineTag = 0;

/**
 *  一段行内原始 HTML 是不是下划线标签：<u> 或 </u>，大小写不限，「>」之前可以有空白。
 *  @return kUnderlineOpen、kUnderlineClose 或 kNotUnderlineTag
 */
int UnderlineTagKind(const MD_CHAR* text, MD_SIZE size)
{
    std::string tag;
    for (MD_SIZE i = 0; i < size; ++i)
    {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        tag += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    }
    if (tag == "<u>")
        return kUnderlineOpen;
    if (tag == "</u>")
        return kUnderlineClose;
    return kNotUnderlineTag;
}

//进入块时的处理结果，离开块时据此配对
enum EnterAction
{
    kActionNone    = 0,   //不对应模型里的块（文档根、表头 / 表体分组）
    kActionPushed  = 1,   //建立了块并压栈，离开时出栈
    kActionSkipped = 2,   //超过上限没有建立，离开时什么也不做
    kActionDropped = 3,   //超出列数的表格单元格：没有建立，并且丢弃其中的文字
};

//生成器内部的错误状态
enum BuildError
{
    kBuildOk        = 0,   //没有错误
    kBuildNoMemory  = 1,   //回调里分配内存失败
    kBuildException = 2,   //回调里发生了其它异常
};

//判断 text 是否以 prefix 开头（ASCII 不区分大小写）
bool StartsWithNoCase(const std::wstring& text, const wchar_t* prefix)
{
    size_t i = 0;
    for (; prefix[i] != 0; ++i)
    {
        if (i >= text.size())
            return false;
        wchar_t a = text[i];
        wchar_t b = prefix[i];
        if (a >= L'A' && a <= L'Z')
            a = static_cast<wchar_t>(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z')
            b = static_cast<wchar_t>(b - L'A' + L'a');
        if (a != b)
            return false;
    }
    return true;
}

//去掉首尾的空白
std::wstring TrimWide(const std::wstring& text)
{
    const wchar_t* const kSpaces = L" \t\r\n";
    size_t begin = text.find_first_not_of(kSpaces);
    if (begin == std::wstring::npos)
        return std::wstring();
    size_t end = text.find_last_not_of(kSpaces);
    return text.substr(begin, end - begin + 1);
}

//把 md4c 的属性串（可能夹着实体）解码成 UTF-16
std::wstring DecodeAttribute(const MD_ATTRIBUTE& attr)
{
    std::wstring out;
    if (attr.text == NULL || attr.size == 0)
        return out;
    if (attr.substr_offsets == NULL || attr.substr_types == NULL)
        return Utf8ToWide(attr.text, attr.size);

    for (size_t i = 0; attr.substr_offsets[i] < attr.size; ++i)
    {
        const MD_OFFSET begin = attr.substr_offsets[i];
        const MD_OFFSET end = attr.substr_offsets[i + 1];
        const std::wstring piece = Utf8ToWide(attr.text + begin, end - begin);
        if (attr.substr_types[i] == MD_TEXT_ENTITY)
        {
            std::wstring decoded;
            out += DecodeEntity(piece, decoded) ? decoded : piece;
        }
        else if (attr.substr_types[i] == MD_TEXT_NULLCHAR)
        {
            out.push_back(kReplacementChar);
        }
        else
        {
            out += piece;
        }
    }
    return out;
}

/**
 *  md4c 回调的接收者：持有解析过程中的全部状态。
 */
class ModelBuilder
{
public:
    ModelBuilder(const MdBuildOptions& options, MdModel& model)
        : m_options(options), m_model(model), m_underlineDepth(0), m_dropTextDepth(0), m_inTableHead(false),
          m_error(kBuildOk)
    {
        MdBlock root;
        root.m_kind = md_block_document;
        root.m_parent = -1;
        m_model.m_blocks.push_back(root);
        m_stack.push_back(0);
        m_implicit.push_back(false);
    }

    //最后一次回调里发生的错误，取值见 enum BuildError
    int Error() const
    {
        return m_error;
    }

    static int OnEnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
    {
        return static_cast<ModelBuilder*>(userdata)->Guard(kCallEnterBlock, static_cast<int>(type), detail, NULL, 0);
    }

    static int OnLeaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
    {
        return static_cast<ModelBuilder*>(userdata)->Guard(kCallLeaveBlock, static_cast<int>(type), detail, NULL, 0);
    }

    static int OnEnterSpan(MD_SPANTYPE type, void* detail, void* userdata)
    {
        return static_cast<ModelBuilder*>(userdata)->Guard(kCallEnterSpan, static_cast<int>(type), detail, NULL, 0);
    }

    static int OnLeaveSpan(MD_SPANTYPE type, void* detail, void* userdata)
    {
        return static_cast<ModelBuilder*>(userdata)->Guard(kCallLeaveSpan, static_cast<int>(type), detail, NULL, 0);
    }

    static int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
    {
        return static_cast<ModelBuilder*>(userdata)->Guard(kCallText, static_cast<int>(type), NULL, text, size);
    }

private:
    //回调种类，交给 Guard 分派
    enum CallKind
    {
        kCallEnterBlock = 0,   //进入块
        kCallLeaveBlock = 1,   //离开块
        kCallEnterSpan  = 2,   //进入行内元素
        kCallLeaveSpan  = 3,   //离开行内元素
        kCallText       = 4,   //一段文字
    };

    //行内样式栈的一项
    struct SpanState
    {
        uint32_t m_style;       //进入该行内元素后的样式位
        int      m_linkIndex;   //进入该行内元素后的当前链接；-1 表示不在链接里
        bool     m_isImage;     //是否为图片语法
        size_t   m_textChars;   //图片：进入后追加的说明文字字符数（不含前缀），离开时为 0 则显示地址
        std::wstring m_url;     //图片：规范化前的地址，说明为空时显示
    };

    //在边界上捕获异常并分派到具体的处理函数
    int Guard(int call, int type, void* detail, const MD_CHAR* text, MD_SIZE size)
    {
        try
        {
            switch (call)
            {
            //进入块
            case kCallEnterBlock:
                return EnterBlock(type, detail);

            //离开块
            case kCallLeaveBlock:
                return LeaveBlock();

            //进入行内元素
            case kCallEnterSpan:
                return EnterSpan(type, detail);

            //离开行内元素
            case kCallLeaveSpan:
                return LeaveSpan();

            //一段文字
            case kCallText:
                return Text(type, text, size);

            //不会出现的回调种类
            default:
                return kContinueParse;
            }
        }
        catch (const std::bad_alloc&)
        {
            m_error = kBuildNoMemory;
        }
        catch (...)
        {
            m_error = kBuildException;
        }
        return kAbortParse;
    }

    //当前栈顶（实际打开的最内层块）
    int Top() const
    {
        return m_stack.back();
    }

    //结束栈顶的隐式段落（如果有）
    void CloseImplicitParagraph()
    {
        if (m_stack.size() > 1 && m_implicit.back())
        {
            m_stack.pop_back();
            m_implicit.pop_back();
        }
    }

    //判断能否再建一个块：层数与总数都在上限之内
    bool CanCreateBlock()
    {
        if (static_cast<int>(m_stack.size()) > kMdMaxBlockDepth || m_model.m_blocks.size() >= kMdMaxBlockCount)
        {
            if (!m_model.m_truncated)
                MdLogWarning("parse: block depth or count over limit (depth %d, count %u), extra blocks flattened.",
                             kMdMaxBlockDepth, static_cast<unsigned>(kMdMaxBlockCount));
            m_model.m_truncated = true;
            return false;
        }
        return true;
    }

    //在栈顶下建一个子块并压栈，返回新块下标
    int PushBlock(int kind, bool implicit)
    {
        const int parent = Top();
        MdBlock block;
        block.m_kind = kind;
        block.m_parent = parent;
        m_model.m_blocks.push_back(block);
        const int index = static_cast<int>(m_model.m_blocks.size()) - 1;
        m_model.m_blocks[parent].m_children.push_back(index);
        m_stack.push_back(index);
        m_implicit.push_back(implicit);
        return index;
    }

    //最内层的表格块下标；不在表格里时返回 -1
    int CurrentTable() const
    {
        for (size_t i = m_stack.size(); i > 0; --i)
        {
            const int idx = m_stack[i - 1];
            if (m_model.m_blocks[idx].m_kind == md_block_table)
                return idx;
        }
        return -1;
    }

    int EnterBlock(int type, void* detail)
    {
        CloseImplicitParagraph();
        //没闭合的 <u> 只到所在段落结束为止
        m_underlineDepth = 0;

        switch (type)
        {
        //文档根：模型里已经有了
        case MD_BLOCK_DOC:
            m_actions.push_back(kActionNone);
            return kContinueParse;

        //表头分组：其中的行是表头行
        case MD_BLOCK_THEAD:
            m_inTableHead = true;
            m_actions.push_back(kActionNone);
            return kContinueParse;

        //表体分组
        case MD_BLOCK_TBODY:
            m_inTableHead = false;
            m_actions.push_back(kActionNone);
            return kContinueParse;

        //其余块按种类处理，见下方
        default:
            break;
        }

        //超出列数的单元格：不建立，丢弃其中的文字
        if (type == MD_BLOCK_TH || type == MD_BLOCK_TD)
        {
            const int table = CurrentTable();
            const int row = Top();
            const bool inRow = (m_model.m_blocks[row].m_kind == md_block_table_row);
            const int column = inRow ? static_cast<int>(m_model.m_blocks[row].m_children.size()) : 0;
            if (table < 0 || !inRow || column >= m_model.m_blocks[table].m_columnCount)
            {
                if (!m_model.m_truncated)
                    MdLogWarning("parse: table wider than %d columns, extra cells dropped.", kMdMaxTableColumns);
                m_model.m_truncated = true;
                m_actions.push_back(kActionDropped);
                ++m_dropTextDepth;
                return kContinueParse;
            }
        }

        if (!CanCreateBlock())
        {
            m_actions.push_back(kActionSkipped);
            return kContinueParse;
        }

        switch (type)
        {
        //引用块
        case MD_BLOCK_QUOTE:
            PushBlock(md_block_quote, false);
            break;

        //警告框（> [!NOTE] 等，md4c 已去掉「[!类型]」那一行）：带类型的引用块
        case MD_BLOCK_ADMONITION:
        {
            const int idx = PushBlock(md_block_quote, false);
            const MD_BLOCK_ADMONITION_DETAIL* d = static_cast<const MD_BLOCK_ADMONITION_DETAIL*>(detail);
            if (d != NULL)
                m_model.m_blocks[idx].m_alertType = MdAlertTypeFromKeyword(DecodeAttribute(d->type));
            break;
        }

        //无序列表
        case MD_BLOCK_UL:
        {
            const int idx = PushBlock(md_block_list, false);
            const MD_BLOCK_UL_DETAIL* d = static_cast<const MD_BLOCK_UL_DETAIL*>(detail);
            m_model.m_blocks[idx].m_ordered = false;
            m_model.m_blocks[idx].m_tight = (d == NULL) || (d->is_tight != 0);
            break;
        }

        //有序列表
        case MD_BLOCK_OL:
        {
            const int idx = PushBlock(md_block_list, false);
            const MD_BLOCK_OL_DETAIL* d = static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
            m_model.m_blocks[idx].m_ordered = true;
            m_model.m_blocks[idx].m_startNumber = (d != NULL) ? d->start : 1;
            m_model.m_blocks[idx].m_tight = (d == NULL) || (d->is_tight != 0);
            break;
        }

        //列表项（可能是任务项）
        case MD_BLOCK_LI:
        {
            const int idx = PushBlock(md_block_list_item, false);
            const MD_BLOCK_LI_DETAIL* d = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
            if (d != NULL && d->is_task != 0)
            {
                const bool checked = (d->task_mark == 'x' || d->task_mark == 'X');
                m_model.m_blocks[idx].m_taskState = checked ? md_task_checked : md_task_unchecked;
            }
            break;
        }

        //分隔线
        case MD_BLOCK_HR:
            PushBlock(md_block_rule, false);
            break;

        //标题
        case MD_BLOCK_H:
        {
            const int idx = PushBlock(md_block_heading, false);
            const MD_BLOCK_H_DETAIL* d = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
            m_model.m_blocks[idx].m_headingLevel = (d != NULL) ? static_cast<int>(d->level) : 1;
            break;
        }

        //代码块：记下语言名与序号
        case MD_BLOCK_CODE:
        {
            const int idx = PushBlock(md_block_code, false);
            const MD_BLOCK_CODE_DETAIL* d = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
            if (d != NULL)
                m_model.m_blocks[idx].m_codeLang = TrimWide(DecodeAttribute(d->lang));
            m_model.m_blocks[idx].m_codeIndex = static_cast<int>(m_model.m_codeBlocks.size());
            m_model.m_codeBlocks.push_back(idx);
            break;
        }

        //段落；原始 HTML 块已用 MD_FLAG_NOHTMLBLOCKS 关闭，万一出现也按段落显示其文字
        case MD_BLOCK_P:
        case MD_BLOCK_HTML:
            PushBlock(md_block_paragraph, false);
            break;

        //表格：记下列数（按上限截断）
        case MD_BLOCK_TABLE:
        {
            const int idx = PushBlock(md_block_table, false);
            const MD_BLOCK_TABLE_DETAIL* d = static_cast<const MD_BLOCK_TABLE_DETAIL*>(detail);
            int columns = (d != NULL) ? static_cast<int>(d->col_count) : 0;
            if (columns > kMdMaxTableColumns)
            {
                columns = kMdMaxTableColumns;
                m_model.m_truncated = true;
            }
            m_model.m_blocks[idx].m_columnCount = columns;
            break;
        }

        //表格的一行
        case MD_BLOCK_TR:
        {
            const int idx = PushBlock(md_block_table_row, false);
            m_model.m_blocks[idx].m_headerRow = m_inTableHead;
            break;
        }

        //表格单元格（表头或表体）
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
        {
            const int row = Top();
            const int column = static_cast<int>(m_model.m_blocks[row].m_children.size());
            const int idx = PushBlock(md_block_table_cell, false);
            const MD_BLOCK_TD_DETAIL* d = static_cast<const MD_BLOCK_TD_DETAIL*>(detail);
            MdBlock& cell = m_model.m_blocks[idx];
            cell.m_headerRow = (type == MD_BLOCK_TH) || m_model.m_blocks[row].m_headerRow;
            cell.m_columnIndex = column;
            cell.m_align = (d != NULL) ? static_cast<int>(d->align) : md_align_default;
            break;
        }

        //未启用的扩展块（脚注、空行块等）：不建立，其中的文字并入外层
        default:
            m_actions.push_back(kActionSkipped);
            return kContinueParse;
        }

        m_actions.push_back(kActionPushed);
        return kContinueParse;
    }

    int LeaveBlock()
    {
        CloseImplicitParagraph();
        m_underlineDepth = 0;
        if (m_actions.empty())
            return kContinueParse;
        const int action = m_actions.back();
        m_actions.pop_back();

        switch (action)
        {
        //建立过块：出栈，代码块顺便整理文字
        case kActionPushed:
        {
            const int idx = Top();
            MdBlock& block = m_model.m_blocks[idx];
            if (block.m_kind == md_block_code)
            {
                //md4c 给每行代码都带上 \n，去掉最后一个，免得代码块底部多出一个空行
                if (!block.m_codeRaw.empty() && block.m_codeRaw[block.m_codeRaw.size() - 1] == L'\n')
                    block.m_codeRaw.erase(block.m_codeRaw.size() - 1);
                block.m_code = ExpandTabs(block.m_codeRaw, kMdTabWidth);
            }
            if (m_stack.size() > 1)
            {
                m_stack.pop_back();
                m_implicit.pop_back();
            }
            break;
        }

        //丢弃文字的单元格结束
        case kActionDropped:
            if (m_dropTextDepth > 0)
                --m_dropTextDepth;
            break;

        //没有建立块，无需出栈
        default:
            break;
        }
        return kContinueParse;
    }

    //当前样式位。下划线不随行内元素的嵌套记录，只看 <u> 的打开层数：在加粗等元素里面写的 </u> 也能结束下划线
    uint32_t CurrentStyle() const
    {
        const uint32_t base = m_spans.empty() ? 0 : m_spans.back().m_style;
        const uint32_t underline = (m_underlineDepth > 0) ? static_cast<uint32_t>(md_style_underline) : 0u;
        return (base & ~static_cast<uint32_t>(md_style_underline)) | underline;
    }

    //当前链接下标；-1 表示不在链接里
    int CurrentLink() const
    {
        return m_spans.empty() ? -1 : m_spans.back().m_linkIndex;
    }

    int EnterSpan(int type, void* detail)
    {
        SpanState state;
        state.m_style = CurrentStyle();
        state.m_linkIndex = CurrentLink();
        state.m_isImage = false;
        state.m_textChars = 0;

        switch (type)
        {
        //斜体
        case MD_SPAN_EM:
            state.m_style |= md_style_italic;
            break;

        //加粗
        case MD_SPAN_STRONG:
            state.m_style |= md_style_bold;
            break;

        //删除线
        case MD_SPAN_DEL:
            state.m_style |= md_style_strike;
            break;

        //行内代码
        case MD_SPAN_CODE:
            state.m_style |= md_style_code;
            break;

        //链接：地址不被接受时其文字按普通文字显示
        case MD_SPAN_A:
        {
            const MD_SPAN_A_DETAIL* d = static_cast<const MD_SPAN_A_DETAIL*>(detail);
            const std::wstring url = (d != NULL) ? NormalizeLinkUrl(DecodeAttribute(d->href)) : std::wstring();
            if (!url.empty())
            {
                MdLink link;
                link.m_url = url;
                link.m_isImage = false;
                m_model.m_links.push_back(link);
                state.m_linkIndex = static_cast<int>(m_model.m_links.size()) - 1;
                state.m_style |= md_style_link;
            }
            break;
        }

        //图片：不下载，转成链接；先追加前缀文字
        case MD_SPAN_IMG:
        {
            const MD_SPAN_IMG_DETAIL* d = static_cast<const MD_SPAN_IMG_DETAIL*>(detail);
            const std::wstring src = (d != NULL) ? TrimWide(DecodeAttribute(d->src)) : std::wstring();
            const std::wstring url = NormalizeLinkUrl(src);
            state.m_isImage = true;
            state.m_url = src;
            if (!url.empty())
            {
                MdLink link;
                link.m_url = url;
                link.m_isImage = true;
                m_model.m_links.push_back(link);
                state.m_linkIndex = static_cast<int>(m_model.m_links.size()) - 1;
                state.m_style |= md_style_link;
            }
            m_spans.push_back(state);
            if (!m_options.m_imageLinkPrefix.empty())
                AppendInline(m_options.m_imageLinkPrefix, state.m_style, state.m_linkIndex, false);
            m_spans.back().m_textChars = 0;
            return kContinueParse;
        }

        //未启用的扩展（下划线、公式、上下标等）：样式不变
        default:
            break;
        }

        m_spans.push_back(state);
        return kContinueParse;
    }

    int LeaveSpan()
    {
        if (m_spans.empty())
            return kContinueParse;
        const SpanState state = m_spans.back();
        m_spans.pop_back();

        //说明为空的图片显示地址，否则界面上什么也看不到
        if (state.m_isImage && state.m_textChars == 0 && !state.m_url.empty())
            AppendInline(state.m_url, state.m_style, state.m_linkIndex, false);
        return kContinueParse;
    }

    //取接收文字的块：栈顶是文字块就用它，否则在栈顶下补一个隐式段落
    int TextBlockForAppend()
    {
        const int top = Top();
        if (MdModel::IsTextBlock(m_model.m_blocks[top].m_kind))
            return top;
        if (m_model.m_blocks.size() >= kMdMaxBlockCount)
        {
            m_model.m_truncated = true;
            return -1;
        }
        return PushBlock(md_block_paragraph, true);
    }

    //追加一段文字或一处强制换行；与前一段样式、链接都相同时合并
    void AppendInline(const std::wstring& text, uint32_t style, int linkIndex, bool isBreak)
    {
        if (m_dropTextDepth > 0)
            return;
        const int idx = TextBlockForAppend();
        if (idx < 0)
            return;
        std::vector<MdInline>& inlines = m_model.m_blocks[idx].m_inlines;

        //图片说明计数：只统计说明文字，不含前缀
        if (!isBreak)
        {
            for (size_t i = m_spans.size(); i > 0; --i)
            {
                if (m_spans[i - 1].m_isImage)
                {
                    m_spans[i - 1].m_textChars += text.size();
                    break;
                }
            }
        }

        if (!isBreak && !inlines.empty())
        {
            MdInline& last = inlines.back();
            if (!last.m_isBreak && last.m_style == style && last.m_linkIndex == linkIndex)
            {
                last.m_text += text;
                return;
            }
        }
        MdInline item;
        item.m_text = isBreak ? std::wstring(L"\n") : text;
        item.m_style = style;
        item.m_linkIndex = linkIndex;
        item.m_isBreak = isBreak;
        inlines.push_back(item);
    }

    int Text(int type, const MD_CHAR* text, MD_SIZE size)
    {
        if (m_dropTextDepth > 0)
            return kContinueParse;

        const int top = Top();
        const bool inCodeBlock = (m_model.m_blocks[top].m_kind == md_block_code);

        switch (type)
        {
        //代码块里的文字（含行尾 \n）放进代码原文；行内代码按代码样式追加
        case MD_TEXT_CODE:
            if (inCodeBlock)
                m_model.m_blocks[top].m_codeRaw += Utf8ToWide(text, size);
            else
                AppendInline(Utf8ToWide(text, size), CurrentStyle() | md_style_code, CurrentLink(), false);
            break;

        //0 号字符：按 CommonMark 的规定显示为 U+FFFD
        case MD_TEXT_NULLCHAR:
            AppendInline(std::wstring(1, kReplacementChar), CurrentStyle(), CurrentLink(), false);
            break;

        //硬换行与软换行：聊天里单个换行就换行，两者都按强制换行处理
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            if (inCodeBlock)
                m_model.m_blocks[top].m_codeRaw += L"\n";
            else
                AppendInline(std::wstring(), CurrentStyle(), CurrentLink(), true);
            break;

        //行内原始 HTML：只认 <u> 与 </u>（切换下划线，标签本身不显示；没有对应 <u> 的 </u> 直接丢掉）；
        //其余标签与以前一样按普通文字显示
        case MD_TEXT_HTML:
        {
            const int tag = UnderlineTagKind(text, size);
            if (tag == kUnderlineOpen)
                ++m_underlineDepth;
            else if (tag == kUnderlineClose && m_underlineDepth > 0)
                --m_underlineDepth;
            else if (tag == kNotUnderlineTag)
                AppendInline(Utf8ToWide(text, size), CurrentStyle(), CurrentLink(), false);
            break;
        }

        //实体：认识的解码，不认识的按原样显示
        case MD_TEXT_ENTITY:
        {
            const std::wstring raw = Utf8ToWide(text, size);
            std::wstring decoded;
            AppendInline(DecodeEntity(raw, decoded) ? decoded : raw, CurrentStyle(), CurrentLink(), false);
            break;
        }

        //普通文字；原始 HTML 与公式已关闭，万一出现也按普通文字显示
        default:
            if (inCodeBlock)
                m_model.m_blocks[top].m_codeRaw += Utf8ToWide(text, size);
            else
                AppendInline(Utf8ToWide(text, size), CurrentStyle(), CurrentLink(), false);
            break;
        }
        return kContinueParse;
    }

    const MdBuildOptions&  m_options;        //解析选项，解析期间有效
    MdModel&               m_model;          //正在生成的模型
    std::vector<int>       m_stack;          //当前打开的块（下标），栈底是文档根
    std::vector<bool>      m_implicit;       //与 m_stack 一一对应：该块是否为隐式段落
    std::vector<int>       m_actions;        //每次进入块的处理结果（取值见 enum EnterAction），离开时出栈配对
    std::vector<SpanState> m_spans;          //行内样式栈
    int                    m_underlineDepth; //<u> 的打开层数：大于 0 时文字带下划线；块开始与结束时清零（没闭合的 <u>
                                             //到所在段落结束为止）
    int                    m_dropTextDepth;  //大于 0 时丢弃文字（处在超出列数的单元格里）
    bool                   m_inTableHead;    //是否处在表头分组里
    int                    m_error;          //回调里发生的错误，取值见 enum BuildError
};

/**
 *  生成渲染后文字的遍历状态。
 */
struct FlatWriter
{
    MdModel* m_model;        //正在处理的模型
    bool     m_first;        //是否还没有写过任何叶子块
    int      m_prevKind;     //上一个叶子块的种类
    int      m_prevParent;   //上一个叶子块的父块下标

    //按文档顺序遍历块，叶子块之间写入分隔符，文字块与代码块写入文字并记下起点；每个块记下自己的起止
    void Walk(int index)
    {
        MdBlock& block = m_model->m_blocks[index];
        const bool isLeaf = MdModel::IsTextBlock(block.m_kind) || block.m_kind == md_block_code
                            || block.m_kind == md_block_rule;
        if (!isLeaf)
        {
            for (size_t i = 0; i < block.m_children.size(); ++i)
                Walk(block.m_children[i]);
            //容器块的起止：子块按文档顺序排列，第一个有范围的子块给出起点，最后一个给出终点
            block.m_flatBegin = -1;
            block.m_flatEnd = -1;
            for (size_t i = 0; i < block.m_children.size(); ++i)
            {
                const MdBlock& child = m_model->m_blocks[block.m_children[i]];
                if (child.m_flatBegin < 0)
                    continue;
                if (block.m_flatBegin < 0)
                    block.m_flatBegin = child.m_flatBegin;
                block.m_flatEnd = child.m_flatEnd;
            }
            return;
        }

        std::wstring& flat = m_model->m_flatText;
        if (!m_first)
        {
            //同一行相邻的单元格之间用 Tab，其余叶子块之间用换行
            const bool sameRow = (block.m_kind == md_block_table_cell && m_prevKind == md_block_table_cell
                                  && m_prevParent == block.m_parent);
            flat.push_back(sameRow ? L'\t' : L'\n');
        }
        m_first = false;
        m_prevKind = block.m_kind;
        m_prevParent = block.m_parent;

        block.m_flatBegin = static_cast<int>(flat.size());
        if (block.m_kind == md_block_code)
        {
            block.m_codeFlatBegin = static_cast<int>(flat.size());
            flat += block.m_code;
            block.m_flatEnd = static_cast<int>(flat.size());
            return;
        }
        for (size_t i = 0; i < block.m_inlines.size(); ++i)
        {
            block.m_inlines[i].m_flatBegin = static_cast<int>(flat.size());
            flat += block.m_inlines[i].m_text;
        }
        block.m_flatEnd = static_cast<int>(flat.size());
    }
};

} // namespace

unsigned GetMdParserFlags()
{
    //块级 HTML 仍关闭（按文字显示）；行内 HTML 开启，只为认出 <u>…</u>，其余行内标签在 Text 里按文字显示；
    //警告框（> [!NOTE] 等，第 13 节）由 md4c 识别
    return MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS
           | MD_FLAG_NOHTMLBLOCKS | MD_FLAG_HARD_SOFT_BREAKS | MD_FLAG_ADMONITIONS;
}

std::wstring NormalizeLinkUrl(const std::wstring& href)
{
    const std::wstring url = TrimWide(href);
    if (url.empty())
        return std::wstring();
    if (StartsWithNoCase(url, L"http://") || StartsWithNoCase(url, L"https://") || StartsWithNoCase(url, L"mailto:"))
        return url;
    if (StartsWithNoCase(url, L"www."))
        return L"http://" + url;

    //不带协议的电子邮件：有 @、没有冒号与斜杠
    if (url.find(L'@') != std::wstring::npos && url.find(L':') == std::wstring::npos
        && url.find(L'/') == std::wstring::npos)
        return L"mailto:" + url;
    return std::wstring();
}

int BuildMdModel(const char* utf8, size_t bytes, const MdBuildOptions& options, MdModel& model)
{
    try
    {
        model = MdModel();
        ModelBuilder builder(options, model);

        MD_PARSER parser;
        parser.abi_version = 0;
        parser.flags = GetMdParserFlags();
        parser.enter_block = &ModelBuilder::OnEnterBlock;
        parser.leave_block = &ModelBuilder::OnLeaveBlock;
        parser.enter_span = &ModelBuilder::OnEnterSpan;
        parser.leave_span = &ModelBuilder::OnLeaveSpan;
        parser.text = &ModelBuilder::OnText;
        parser.debug_log = NULL;
        parser.syntax = NULL;

        static const char kEmpty[] = "";
        const char* text = (utf8 != NULL) ? utf8 : kEmpty;
        const int rc = md_parse(text, static_cast<MD_SIZE>(bytes), &parser, &builder);
        if (builder.Error() == kBuildNoMemory)
            return md_out_of_memory;
        if (builder.Error() != kBuildOk)
        {
            MdLogError("parse: unexpected exception in parser callback.");
            return md_internal_error;
        }
        if (rc != 0)
        {
            MdLogError("parse: md4c returned %d.", rc);
            return md_internal_error;
        }

        FlatWriter writer;
        writer.m_model = &model;
        writer.m_first = true;
        writer.m_prevKind = md_block_document;
        writer.m_prevParent = -1;
        writer.Walk(0);
        return md_ok;
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
}

} // namespace flamingomd
