/**
 *  Markdown 文档的排版：把文档模型按给定的最大宽度排成文字片段、行与装饰（底色、线条、项目符号），
 *  供绘制（MdPaint）与命中测试（MdSelection）使用。
 *
 *  排版分两遍：第一遍不折行地测出各块的自然宽度，文档宽度取「自然宽度」与「最大宽度」中较小的一个（短消息收缩
 *  到内容宽度，与普通文字气泡一致）；第二遍按这个宽度正式排版，分隔线、代码块底色、标题下方细线都撑满这个宽度。
 *
 *  坐标以文档左上角为原点，单位为像素。
 *
 *  典型用法：
 *
 *      MdResolvedStyle style;
 *      ResolveMdStyle(apiStyle, style);
 *      MdLayoutResult result;
 *      LayoutMdModel(model, hdc, style, maxWidth, result);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "../FlamingoMarkdownApi.h"
#include "MdModel.h"

namespace flamingomd {

//文字颜色的用途，绘制时按它从样式里取颜色
enum MdColorRole
{
    md_color_text  = 0,   //正文颜色
    md_color_muted = 1,   //次要文字颜色：引用块里的文字、有序列表的编号
    md_color_link  = 2,   //链接颜色
    md_color_alert_first = 3,   //警告框标题：md_color_alert_first + 类型 - 1（3 ~ 7），颜色取 MdAlertColor(类型)
};

//装饰的种类
enum MdDecorationKind
{
    md_deco_code_block    = 0,   //代码块底色（圆角矩形，代码底色）
    md_deco_table_header  = 1,   //表格表头行底色
    md_deco_line          = 2,   //实心细线或竖条（分隔线、表格线、引用竖条、标题下方细线），线条色
    md_deco_bullet_disc   = 3,   //实心圆点：第一层无序列表
    md_deco_bullet_circle = 4,   //空心圆圈：第二层无序列表
    md_deco_bullet_square = 5,   //实心方块：第三层及更深的无序列表
    md_deco_task_open     = 6,   //未勾选的任务框
    md_deco_task_done     = 7,   //已勾选的任务框（强调色填充，带勾）
    md_deco_alert_back    = 8,   //警告框底色（整块，类型见 m_alertType，排在框内其它装饰之前）
    md_deco_alert_bar     = 9,   //警告框左侧竖条，类型的主色
    md_deco_alert_icon    = 10,  //警告框标题前的图标，各类型形状不同，类型的主色
};

/**
 *  一段已排好的文字：同一行、同一字体、同一行内元素里连续的若干字符。
 */
struct MdFragment
{
    int              m_x;            //片段左边（含左内边距）
    int              m_top;          //所在行的顶边
    int              m_width;        //片段宽度（含左右内边距）
    int              m_height;       //所在行的高度
    int              m_baseline;     //基线的纵坐标
    int              m_fontIndex;    //MdFontCache 中的字体序号
    uint32_t         m_style;        //样式位，取值见 enum MdInlineStyleBit
    int              m_linkIndex;    //链接序号；不是链接时为 -1
    int              m_colorRole;    //文字颜色的用途，取值见 enum MdColorRole
    int              m_flatBegin;    //第一个字符在渲染后文字中的下标；-1 表示不可选中（有序列表的编号）
    bool             m_inCodeBlock;  //是否属于代码块（代码块整体有底色，片段不再单独画行内代码底色）
    int              m_tokenKind;    //代码块语法着色的种类（取值见 codesnippet::CodeTokenKind，见 MdCodeColor.h）；
                                     //kMdCodeTokenPlain（0）表示不着色，按 m_colorRole 取色
    int              m_padLeft;      //左内边距（行内代码的底色比文字宽一点）
    int              m_padRight;     //右内边距
    std::wstring     m_text;         //片段文字
    std::vector<int> m_caret;        //m_caret[i]：第 i 个字符的右边缘相对 m_x 的偏移（已含左内边距）

    MdFragment()
        : m_x(0), m_top(0), m_width(0), m_height(0), m_baseline(0), m_fontIndex(-1), m_style(0),
          m_linkIndex(-1), m_colorRole(md_color_text), m_flatBegin(-1), m_inCodeBlock(false), m_tokenKind(0),
          m_padLeft(0), m_padRight(0)
    {
    }

    //片段在渲染后文字中的终点（不含）；不可选中时为 -1
    int FlatEnd() const
    {
        return (m_flatBegin < 0) ? -1 : m_flatBegin + static_cast<int>(m_text.size());
    }
};

/**
 *  一行。命中测试先按行定位，再在行内找片段。
 */
struct MdLine
{
    int m_top;             //顶边
    int m_bottom;          //底边（不含）
    int m_left;            //所在区域的左边（段落、单元格或代码块的内容区）
    int m_right;           //所在区域的右边（不含）
    int m_firstFragment;   //第一个片段在 MdLayoutResult::m_fragments 中的下标
    int m_fragmentCount;   //片段个数；空行为 0
    int m_flatBegin;       //行首在渲染后文字中的下标
    int m_flatEnd;         //行尾在渲染后文字中的下标（不含行尾的换行）
    int m_codeIndex;       //属于代码块时为代码块序号，否则为 -1

    MdLine()
        : m_top(0), m_bottom(0), m_left(0), m_right(0), m_firstFragment(0), m_fragmentCount(0),
          m_flatBegin(0), m_flatEnd(0), m_codeIndex(-1)
    {
    }
};

/**
 *  一个装饰：底色、线条、项目符号或任务框。
 */
struct MdDecoration
{
    int  m_kind;        //种类，取值见 enum MdDecorationKind
    RECT m_rect;        //外框
    int  m_alertType;   //警告框的三种装饰：警告框类型（取值见 MdAlert.h 的 enum MdAlertType）；其它装饰为 0
};

/**
 *  排版结果。
 */
struct MdLayoutResult
{
    int                       m_width;          //文档宽度
    int                       m_height;         //文档高度
    std::vector<MdFragment>   m_fragments;      //全部文字片段，按阅读顺序
    std::vector<MdLine>       m_lines;          //全部行，按阅读顺序
    std::vector<MdDecoration> m_decorations;    //全部装饰，按绘制顺序（底色在前）
    std::vector<RECT>         m_codeRects;      //各代码块的底色外框，按代码块序号
    std::vector<RECT>         m_copyButtons;    //各代码块「复制」按钮的外框，按代码块序号
    int                       m_cornerRadius;   //圆角半径（代码块与行内代码底色）
    int                       m_lineWidth;      //细线宽度

    MdLayoutResult()
        : m_width(0), m_height(0), m_cornerRadius(0), m_lineWidth(1)
    {
    }

    //清空全部内容
    void Clear()
    {
        m_width = 0;
        m_height = 0;
        m_fragments.clear();
        m_lines.clear();
        m_decorations.clear();
        m_codeRects.clear();
        m_copyButtons.clear();
    }
};

/**
 *  DLL 内部使用的排版样式：复制了宿主传入的 MdStyle（字体名改为自行持有），并换算出各项间距。
 */
struct MdResolvedStyle
{
    std::wstring m_fontName;          //正文字体名
    std::wstring m_codeFontName;      //代码字体名
    int          m_fontPx;            //正文字号（像素）
    int          m_codeFontPx;        //代码字号（像素）
    int          m_scalePercent;      //界面缩放百分比
    uint32_t     m_textColor;         //正文颜色
    uint32_t     m_mutedColor;        //次要文字颜色
    uint32_t     m_linkColor;         //链接颜色
    uint32_t     m_codeBackColor;     //代码底色
    uint32_t     m_ruleColor;         //线条颜色
    uint32_t     m_tableHeaderBackColor; //表头底色
    uint32_t     m_selectionBackColor;   //选区底色
    uint32_t     m_accentColor;       //强调色
    bool         m_fillWidth;         //文档宽度撑满 maxWidth（宿主设了 md_layout_fill_width）；否则收缩到内容宽度

    MdResolvedStyle();

    //判断两份样式是否完全相同（相同则可以复用上一次的排版结果）
    bool operator==(const MdResolvedStyle& other) const;
};

/**
 *  把宿主传入的样式转成内部样式。
 *  @param in  宿主样式，调用方已检查 m_cbSize 与 m_fontName
 *  @param out [出参] 内部样式
 */
void ResolveMdStyle(const MdStyle& in, MdResolvedStyle& out);

/**
 *  按最大宽度排版。
 *  @param model    文档模型
 *  @param hdc      用于测量的设备上下文；调用前后其中选中的对象不变
 *  @param style    排版样式
 *  @param maxWidth 最大宽度（像素），大于 0
 *  @param out      [出参] 排版结果（先被清空）
 */
void LayoutMdModel(const MdModel& model, HDC hdc, const MdResolvedStyle& style, int maxWidth, MdLayoutResult& out);

} // namespace flamingomd
