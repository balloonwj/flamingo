/**
 *  ChatCodeBlock.h
 *
 *  聊天「代码片段」卡片的排版与绘制：顶部一行写语言与行数、右侧「复制」，下面是等宽字体、按语法着色的代码，
 *  行数超过折叠上限时底部有「展开全部 / 收起」。与 ChatFileCard 一样只做几何与绘制，不是控件 ——
 *  聊天气泡、消息记录窗、演示窗口各自持有排版结果，按需重排与绘制。
 *
 *  保持代码格式的做法：
 *    · Tab 按列展开（默认 4 列一个制表位），空格原样保留，行首缩进不会被丢弃；
 *    · 用等宽字体 Consolas 排版。Consolas 没有中文等字形，而本机的系统字体链接里没有它的条目，
 *      GDI 不会自动换字体，所以先用 GetGlyphIndicesW 查出缺字形的字符，改用界面字体测量与绘制；
 *    · 折行时续行保持原缩进再缩进两列，读者能看出它是上一行的延续；关闭折行时超出卡片的部分不显示、
 *      行尾画省略号；
 *    · 复制给出的是原文（Tab 不展开），只把换行统一成 \r\n。
 *
 *  带批注的代码片段（docs/代码片段方案.md 第 10.5 节）：卡片不显示批注的说明，只把被批注的行铺浅黄底色、
 *  在卡片左缘画一条橙色竖条，顶部的行数说明后加「· N 条批注」；卡片高度与不带批注时相同。追加批注的消息
 *  （第 11 节，批注里有标为「新」的）再加「，其中 M 条是新的」。
 *
 *  典型用法：
 *      CodeBlockContent content = PrepareCodeBlockContent(pastedText, codesnippet::DetectCodeLanguage(pastedText));
 *      CodeBlockLayout layout;
 *      LayoutCodeBlock(hdc, content, CodeBlockOptions(), maxWidth, layout);
 *      PaintCodeBlock(hdc, layout, left, top, kCodePartNone);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../../Common/Code/CodeLexer.h"
#include "../../Common/Code/CodeNotes.h"   // CodeNote：被批注的行

namespace balloonchatwnd {

/**
 *  卡片上可以点击的部位，也用作绘制时的悬停部位。
 */
enum CodeBlockPart
{
    kCodePartNone   = 0,   //不在任何可点击部位上
    kCodePartCopy   = 1,   //顶部右侧的「复制」
    kCodePartFooter = 2,   //底部的「展开全部 / 收起」或「查看全部」（只有行数超过折叠上限时才有）
    kCodePartBody   = 3,   //卡片内的其它位置（顶部空白处、代码正文）：聊天窗里点它打开代码查看窗
};

/**
 *  代码卡片的显示选项。
 */
struct CodeBlockOptions
{
    int  m_fontPx;           //代码字号（像素）
    int  m_tabColumns;       //Tab 宽度（列），取值 1 ~ 16，越界时按默认值
    int  m_collapsedLines;   //折叠时最多显示几行；<= 0 表示从不折叠
    bool m_expanded;         //是否已展开显示全部行；行数不超过 m_collapsedLines 时无意义
    bool m_lineNumbers;      //是否显示行号
    bool m_wrap;             //长行是否折行；false 时超出卡片的部分不显示，行尾画省略号
    bool m_copied;           //「复制」刚被点过：顶部改写「已复制」，由宿主在一段时间后恢复
    bool m_footerOpensViewer; //底栏用来打开代码查看窗（聊天窗里）：折叠时底栏写「查看全部（共 N 行）」，
                              //不在原地展开，m_expanded 不起作用

    CodeBlockOptions();
};

/**
 *  卡片要显示的代码，由 PrepareCodeBlockContent 从原文生成。
 */
struct CodeBlockContent
{
    std::wstring                        m_code;        //规范化后的代码：换行统一为 \n，末尾的空行已去掉
    int                                 m_lang;        //语言，取值见 codesnippet::CodeLanguage
    std::vector<codesnippet::CodeToken> m_tokens;      //着色区间，指向 m_code
    int                                 m_lineCount;   //总行数（逻辑行），空代码为 0
    std::vector<std::pair<int, int> >   m_noteLines;   //被批注的逻辑行，每项为 (起始行, 结束行)，从 0 起、含两端，按起始行升序；
                                                       //不同作者的批注可以相交（docs/代码片段方案.md 第 11.4 节）
    int                                 m_newNoteCount;   //其中标为「新」的批注条数（第 11 节，追加批注的消息才不为 0）

    CodeBlockContent()
        : m_lang(codesnippet::code_lang_plain)
        , m_lineCount(0)
        , m_newNoteCount(0)
    {
    }
};

/**
 *  一段同字体、同颜色的文字（排版结果）。坐标相对卡片左上角。
 */
struct CodeBlockRun
{
    int          m_row;        //所在视觉行，从 0 起
    int          m_x;          //文字左边
    int          m_width;      //文字宽度（像素）
    std::wstring m_text;       //文字，Tab 已展开成空格
    int          m_kind;       //着色种类，取值见 codesnippet::CodeTokenKind
    bool         m_fallback;   //是否用回退字体（等宽字体缺这些字符的字形，如中文）

    CodeBlockRun()
        : m_row(0)
        , m_x(0)
        , m_width(0)
        , m_kind(codesnippet::code_token_plain)
        , m_fallback(false)
    {
    }
};

/**
 *  代码卡片的排版结果。坐标都相对卡片左上角。
 */
struct CodeBlockLayout
{
    SIZE                      m_size;           //卡片整体尺寸
    int                       m_fontPx;         //排版所用的代码字号，绘制时按同一字号建字体
    int                       m_rowHeight;      //视觉行高
    int                       m_baseline;       //基线距视觉行顶的距离
    int                       m_bodyTop;        //第一视觉行的顶边
    int                       m_rowCount;       //视觉行数（含折行产生的续行）
    int                       m_shownLines;     //显示了几行逻辑行
    int                       m_totalLines;     //逻辑总行数
    bool                      m_collapsible;    //行数超过折叠上限，有底栏
    bool                      m_collapsed;      //当前是折叠状态，有没显示的行
    bool                      m_copied;         //「复制」刚被点过，顶部右侧显示灰色的「已复制」
    int                       m_gutterRight;    //行号栏右边（行号右对齐到这里）；不显示行号时为 0
    int                       m_textLeft;       //代码区左边
    int                       m_textRight;      //代码区右边，超出部分不画
    std::vector<CodeBlockRun> m_runs;           //要画的文字段，按视觉行升序
    std::vector<int>          m_lineFirstRow;   //每个显示的逻辑行的第一个视觉行，下标为逻辑行号（从 0 起）
    std::vector<int>          m_overflowRows;   //关闭折行时内容超出代码区的视觉行，行尾画省略号
    std::vector<std::pair<int, int> > m_markedRows;   //被批注的行所占的视觉行，每项为 (首行, 末行)，含两端；只含显示出来的行
    std::wstring              m_title;          //顶部左侧的语言名
    std::wstring              m_info;           //顶部语言名后的行数说明
    std::wstring              m_copyText;       //顶部右侧的「复制」或「已复制」
    std::wstring              m_footerText;     //底栏文字；没有底栏时为空
    RECT                      m_copyRect;       //「复制」的点击区
    RECT                      m_footerRect;     //底栏的点击区；没有底栏时为空矩形

    CodeBlockLayout();
};

/**
 *  由原文准备卡片内容：规范化换行、着色、数行数、记下被批注的行。
 *  @param raw   代码原文（可以带 \r\n、\r 换行，末尾可以有空行）
 *  @param lang  语言，取值见 codesnippet::CodeLanguage
 *  @param notes 批注，行号指规范化之后的代码（与消息里的代码相同）；越界、相交的条目按整理规则丢弃
 *  @return 卡片内容
 */
CodeBlockContent PrepareCodeBlockContent(const std::wstring& raw, int lang,
                                         const std::vector<codesnippet::CodeNote>& notes = std::vector<codesnippet::CodeNote>());

/**
 *  排版代码卡片。
 *  @param hdc      只用于测量；函数内部自建字体，用完还原，不改变 hdc 的状态
 *  @param content  卡片内容
 *  @param options  显示选项
 *  @param maxWidth 卡片最大宽度（像素）；卡片宽度按最长的行收窄，但不小于一个最小宽度（maxWidth 更小时取 maxWidth）
 *  @param out      [出参] 排版结果
 */
void LayoutCodeBlock(HDC hdc, const CodeBlockContent& content, const CodeBlockOptions& options, int maxWidth,
                     CodeBlockLayout& out);

/**
 *  绘制代码卡片。只画与 hdc 当前裁剪区相交的视觉行。
 *  @param hdc        目标设备上下文；绘制前后状态不变
 *  @param layout     LayoutCodeBlock 的结果
 *  @param left / top 卡片左上角在 hdc 里的坐标
 *  @param hoverPart  鼠标悬停的部位，取值见 CodeBlockPart；悬停的链接加下划线
 */
void PaintCodeBlock(HDC hdc, const CodeBlockLayout& layout, int left, int top, int hoverPart);

/**
 *  点击判断。
 *  @param layout 排版结果
 *  @param pt     相对卡片左上角的坐标
 *  @return 命中的部位，取值见 CodeBlockPart：「复制」、底栏、卡片内的其它位置，卡片外为 kCodePartNone
 */
int HitTestCodeBlock(const CodeBlockLayout& layout, POINT pt);

/**
 *  切换顶部右侧的「复制」与「已复制」字样，不重新排版（点击区仍按「复制」的宽度，绘制时按实际文字宽度右对齐）。
 *  @param layout [出入参] 排版结果
 *  @param copied true 显示灰色的「已复制」，false 恢复为「复制」
 *  @return 字样有变化时返回 true（调用方据此重画）
 */
bool SetCodeBlockCopied(CodeBlockLayout& layout, bool copied);

/**
 *  着色种类对应的文字颜色（浅色背景下，参照 Visual Studio 浅色主题）。
 *  @param kind 着色种类，取值见 codesnippet::CodeTokenKind；越界时返回普通文字色
 */
COLORREF CodeTokenColor(int kind);

/**
 *  复制到剪贴板用的文字：原文不变，只把换行统一成 \r\n（记事本等程序只认 \r\n）。
 *  @param code 规范化后的代码（CodeBlockContent::m_code）
 */
std::wstring CodeTextForClipboard(const std::wstring& code);

// ---- 以下为纯函数，不依赖 GDI，供单测直接断言 ----

/**
 *  规范化换行：\r\n 与单独的 \r 都改成 \n；去掉开头与末尾只含空白的行（从别处复制代码时常带一个结尾换行）。
 *  第一行代码自身的缩进、各行行首行尾的空格与 Tab 一律保留。全是空白时返回空串。
 */
std::wstring NormalizeCodeNewlines(const std::wstring& raw);

/**
 *  一个字符在等宽排版里占几列：中日韩文字与全角符号占 2 列，其余占 1 列。只用于计算 Tab 展开到哪一列。
 */
int CodeCharColumns(wchar_t c);

/**
 *  把一行里的 Tab 展开成空格，每个 Tab 补到下一个 tabColumns 的整数倍列。
 *  @param line       一行文字（不含换行）
 *  @param tabColumns Tab 宽度（列），小于 1 时按 1 处理
 *  @param srcIndex   [出参，可为 NULL] 展开后每个字符对应原行里的下标；Tab 展开出的几个空格都对应那个 Tab
 *  @return 展开后的文字
 */
std::wstring ExpandCodeTabs(const std::wstring& line, int tabColumns, std::vector<int>* srcIndex);

/**
 *  计算一行的折行位置。
 *  @param charRight  每个字符右边距行首的像素距离（单调不减），即前 k+1 个字符的总宽
 *  @param text       这一行的文字（与 charRight 等长），用来优先在空格之后折行
 *  @param firstWidth 第一视觉行的可用宽度（像素）
 *  @param contWidth  续行的可用宽度（像素，已扣掉续行缩进）
 *  @param outStarts  [出参] 各视觉行的起始字符下标，第一个恒为 0；空行也有一个视觉行
 *  每个视觉行至少放一个字符，即使这个字符比可用宽度还宽。
 */
void ComputeCodeWrapStarts(const std::vector<int>& charRight, const std::wstring& text, int firstWidth,
                           int contWidth, std::vector<int>& outStarts);

} // namespace balloonchatwnd
