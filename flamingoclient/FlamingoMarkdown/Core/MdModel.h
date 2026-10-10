/**
 *  Markdown 文档模型：由 MdBuilder 根据 md4c 的解析回调生成，排版（MdLayout）、纯文本提取（MdPlainText）、
 *  命中与取文字（MdSelection）都只读它。
 *
 *  结构：
 *    - 块（MdBlock）组成一棵树，全部存放在 MdModel::m_blocks 里，以下标互相引用；下标 0 是文档根；
 *    - 段落、标题、表格单元格这三种「文字块」持有行内元素（MdInline），其余块只持有子块；
 *    - 代码块持有原文（m_codeRaw）与 Tab 展开后的显示文字（m_code）；
 *    - 解析结束时生成「渲染后文字」（m_flatText，定义见 FlamingoMarkdownApi.h），并给每个行内元素、代码块
 *      记下它在其中的起点，供选区与取文字使用。
 *
 *  本文件只有数据定义，不依赖 Windows 头文件。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace flamingomd {

//行内元素的样式位，可组合
enum MdInlineStyleBit
{
    md_style_bold   = 0x01,   //加粗：**文字**、__文字__，以及标题里的全部文字
    md_style_italic = 0x02,   //斜体：*文字*、_文字_
    md_style_strike = 0x04,   //删除线：~~文字~~
    md_style_code   = 0x08,   //行内代码：`文字`，等宽字体、浅灰底
    md_style_link   = 0x10,   //链接文字（含图片语法转成的链接），链接色、下划线
    md_style_underline = 0x20,   //下划线：<u>文字</u>（docs/Markdown消息方案.md 第 13 节；行内只放行这一个 HTML 标签）
};

//块的种类
enum MdBlockKind
{
    md_block_document   = 0,    //文档根，只出现在下标 0
    md_block_paragraph  = 1,    //段落；紧凑列表里没有显式段落的列表项文字也放进一个段落
    md_block_heading    = 2,    //标题，级别见 m_headingLevel
    md_block_quote      = 3,    //引用块 >
    md_block_list       = 4,    //列表，有序与否见 m_ordered
    md_block_list_item  = 5,    //列表项，任务状态见 m_taskState
    md_block_code       = 6,    //代码块（围栏或缩进）
    md_block_rule       = 7,    //分隔线 ---
    md_block_table      = 8,    //表格
    md_block_table_row  = 9,    //表格的一行，是否表头行见 m_headerRow
    md_block_table_cell = 10,   //表格单元格，对齐见 m_align
};

//列表项的任务状态
enum MdTaskState
{
    md_task_none      = 0,   //普通列表项
    md_task_unchecked = 1,   //任务列表项，未勾选：- [ ]
    md_task_checked   = 2,   //任务列表项，已勾选：- [x]
};

//表格单元格的水平对齐
enum MdAlign
{
    md_align_default = 0,   //未指定，按左对齐显示
    md_align_left    = 1,   //左对齐 :---
    md_align_center  = 2,   //居中 :---:
    md_align_right   = 3,   //右对齐 ---:
};

/**
 *  行内元素：一段样式一致的文字，或者一处强制换行。
 */
struct MdInline
{
    std::wstring m_text;        //文字（UTF-16）；强制换行时为 L"\n"
    uint32_t     m_style;       //样式位组合，取值见 enum MdInlineStyleBit
    int          m_linkIndex;   //所属链接在 MdModel::m_links 中的下标；不是链接时为 -1
    bool         m_isBreak;     //是否为强制换行（硬换行，或「单个换行即换行」生效的软换行）
    int          m_flatBegin;   //在渲染后文字中的起点，解析结束时填写

    MdInline()
        : m_style(0), m_linkIndex(-1), m_isBreak(false), m_flatBegin(0)
    {
    }
};

/**
 *  块。各字段只对相应种类有意义，其余取默认值。
 */
struct MdBlock
{
    int                   m_kind;          //种类，取值见 enum MdBlockKind
    int                   m_parent;        //父块下标；文档根为 -1
    std::vector<int>      m_children;      //子块下标，按文档顺序
    std::vector<MdInline> m_inlines;       //行内元素：段落、标题、表格单元格才有
    int                   m_headingLevel;  //标题级别 1 ~ 6
    bool                  m_ordered;       //列表：是否有序
    unsigned              m_startNumber;   //有序列表：第一项的编号
    bool                  m_tight;         //列表：是否紧凑（项与项之间不空行）
    int                   m_taskState;     //列表项：任务状态，取值见 enum MdTaskState
    std::wstring          m_codeRaw;       //代码块：原文（Tab 不展开），去掉了结尾的换行
    std::wstring          m_code;          //代码块：显示文字（Tab 按 4 列制表位展开为空格）
    std::wstring          m_codeLang;      //代码块：围栏后写的语言名，没有时为空
    int                   m_codeIndex;     //代码块：在文档中的序号（从 0 起）
    int                   m_codeFlatBegin; //代码块：显示文字在渲染后文字中的起点
    int                   m_align;         //表格单元格：水平对齐，取值见 enum MdAlign
    bool                  m_headerRow;     //表格行与其单元格：是否属于表头行
    int                   m_columnCount;   //表格：列数（已按上限截断）
    int                   m_columnIndex;   //表格单元格：所在列（从 0 起）
    int                   m_flatBegin;     //块在渲染后文字中的起点：叶子块为自己文字的起点（不含前面的分隔符），
                                           //容器块为后代叶子块的最小起点；没有叶子块后代时为 -1（按选区导出 HTML 用）
    int                   m_flatEnd;       //块在渲染后文字中的终点（不含）：叶子块为自己文字的终点，容器块为后代
                                           //叶子块的最大终点；没有文字的叶子块（分隔线、空段落）起点等于终点
    int                   m_alertType;     //引用块：警告框的类型（> [!NOTE] 等，取值见 MdAlert.h 的 enum MdAlertType），
                                           //普通引用块为 0

    MdBlock()
        : m_kind(md_block_document), m_parent(-1), m_headingLevel(0), m_ordered(false), m_startNumber(1),
          m_tight(true), m_taskState(md_task_none), m_codeIndex(-1), m_codeFlatBegin(0),
          m_align(md_align_default), m_headerRow(false), m_columnCount(0), m_columnIndex(0),
          m_flatBegin(-1), m_flatEnd(-1), m_alertType(0)
    {
    }
};

/**
 *  链接。
 */
struct MdLink
{
    std::wstring m_url;       //地址，已补全 http:// 或 mailto: 前缀
    bool         m_isImage;   //是否由图片语法 ![说明](地址) 转成
};

/**
 *  整个文档。
 */
struct MdModel
{
    std::vector<MdBlock> m_blocks;          //全部块；下标 0 是文档根
    std::vector<MdLink>  m_links;           //全部链接，按出现顺序
    std::vector<int>     m_codeBlocks;      //代码块的块下标，按序号排列
    std::wstring         m_flatText;        //渲染后文字
    bool                 m_truncated;       //是否因超过上限而截断（嵌套层数、块数、表格列数）

    MdModel()
        : m_truncated(false)
    {
    }

    //判断某种块是否持有行内元素（段落、标题、表格单元格）
    static bool IsTextBlock(int kind)
    {
        return kind == md_block_paragraph || kind == md_block_heading || kind == md_block_table_cell;
    }
};

} // namespace flamingomd
