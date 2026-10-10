/**
 *  MarkdownEditCommands.h
 *
 *  Markdown 编辑器格式按钮的编辑命令（docs/Markdown消息方案.md 第 11.3 节；最初为聊天窗输入区的格式条而写，
 *  输入区的 Markdown 模式已删除，见第 11.2 节）：给定输入框的全文与选区，算出点击
 *  某个格式按钮之后应当把哪一段替换成什么、替换后选中哪里。纯函数，不碰控件，FlamingoUnit 直接断言
 *  （Tests/MarkdownEditCommandsTests.cpp，标签 [mdedit]）。
 *
 *  结果只描述「最小的一段替换」（替换区间之外的字符原样不动），宿主据此对输入框 SetSel + ReplaceSel，
 *  输入框里替换区间以外的内嵌对象（如截图）不受影响。
 *
 *  三类命令：
 *    - 行内包裹（加粗、斜体、删除线、行内代码）：选区两侧加上标记；选区两侧已有同样标记时去掉；没有选区时插入
 *      「标记 + 占位文字 + 标记」并选中占位文字；
 *    - 行首前缀（标题、引用、无序列表、有序列表、任务列表）：给选区覆盖的每一行加前缀；这些行都已有该前缀时去掉；
 *    - 整块插入（代码块、链接、表格）：代码块把选中的行包进 ``` 围栏；链接写成 [文字](https://)；表格在下一行插入模板。
 *
 *  换行：输入框（RichEdit）里段落之间是单个 \r，本模块把 \r 与 \n 都当换行，新插入的换行用调用方给出的字符。
 *
 *  另有 Markdown 编辑器的回车续项（ContinueMarkdownListOnEnter，docs/Markdown消息方案.md 第 12 节）：光标所在行
 *  是列表项或引用时，回车后在新行写上同样的标记。它与格式按钮共用本文件的行处理函数与最小替换区间的算法。
 *
 *  典型用法：
 *      MarkdownEditInput in;
 *      in.m_text = text;  in.m_selStart = a;  in.m_selEnd = b;  in.m_lineBreak = L'\r';
 *      in.m_placeholder = TR(L"加粗文字");
 *      MarkdownEditResult r = ApplyMarkdownEditCommand(in, md_edit_bold);
 *      edit->SetSel(r.m_replaceStart, r.m_replaceEnd);
 *      edit->ReplaceSel(r.m_replacement.c_str());
 *      edit->SetSel(r.m_selStart, r.m_selEnd);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

namespace balloonchatwnd {

//格式条的编辑命令。取值也是格式条按钮的排列顺序（编辑器按钮的控件编号为 kMdEdIdFormatFirst + 取值），只在本进程内使用，
//不写进文件或协议；2026-10-07 在斜体之后插入下划线，其后的取值依次加一
enum MarkdownEditCommand
{
    md_edit_heading     = 0,    //标题：行首加「## 」（编辑器的「H」按钮改为弹出标题级别菜单，见 SetMarkdownHeadingLevel）
    md_edit_bold        = 1,    //加粗：**文字**
    md_edit_italic      = 2,    //斜体：*文字*
    md_edit_underline   = 3,    //下划线：<u>文字</u>（docs/Markdown消息方案.md 第 13 节）
    md_edit_strike      = 4,    //删除线：~~文字~~
    md_edit_code        = 5,    //行内代码：`文字`
    md_edit_code_block  = 6,    //代码块：``` 围栏
    md_edit_link        = 7,    //链接：[文字](https://)
    md_edit_quote       = 8,    //引用：行首加「> 」
    md_edit_bullet_list = 9,    //无序列表：行首加「- 」
    md_edit_number_list = 10,   //有序列表：行首加「1. 」「2. 」……
    md_edit_task_list   = 11,   //任务列表：行首加「- [ ] 」
    md_edit_table       = 12,   //表格：插入两列表格模板
    md_edit_rule        = 13,   //分割线：另起一段插入「---」，与上面的文字隔一个空行（否则上一行会成为标题）
    md_edit_alert       = 14,   //警告框：编辑器的按钮弹出类型菜单（见 InsertMarkdownAlert）；直接执行本命令时按「提示」插入
};

//编辑命令的个数（格式条按钮的个数）
const int kMarkdownEditCommandCount = 15;

//警告框的类型（GitHub 写法 > [!NOTE] 等，顺序与 FlamingoMarkdown.dll 的 MdAlert.h 相同），InsertMarkdownAlert 的参数
enum MarkdownAlertType
{
    md_edit_alert_note      = 1,   //提示：> [!NOTE]
    md_edit_alert_tip       = 2,   //技巧：> [!TIP]
    md_edit_alert_important = 3,   //重要：> [!IMPORTANT]
    md_edit_alert_warning   = 4,   //警告：> [!WARNING]
    md_edit_alert_caution   = 5,   //注意：> [!CAUTION]
};

//警告框类型的个数
const int kMarkdownAlertTypeCount = 5;

/**
 *  命令的输入。
 */
struct MarkdownEditInput
{
    std::wstring m_text;          //输入框全文（段落之间可以是 \r、\n 或 \r\n）
    int          m_selStart;      //选区起点（字符下标），与 m_selEnd 顺序不限，超出范围的部分被截掉
    int          m_selEnd;        //选区终点（字符下标）
    wchar_t      m_lineBreak;     //新插入的换行用哪个字符：RichEdit 输入框为 \r
    std::wstring m_placeholder;   //没有选区时插入的占位文字（行内包裹与链接用），如「加粗文字」「链接文字」
    std::wstring m_tableHeader1;  //表格模板第一列的列名
    std::wstring m_tableHeader2;  //表格模板第二列的列名

    MarkdownEditInput()
        : m_selStart(0), m_selEnd(0), m_lineBreak(L'\n')
    {
    }
};

/**
 *  命令的结果：把 [m_replaceStart, m_replaceEnd) 换成 m_replacement，然后选中 [m_selStart, m_selEnd)
 *  （下标按替换之后的全文计）。
 */
struct MarkdownEditResult
{
    int          m_replaceStart;   //替换区间起点（替换前的下标）
    int          m_replaceEnd;     //替换区间终点（替换前的下标，不含）
    std::wstring m_replacement;    //替换成的文字
    int          m_selStart;       //替换后的选区起点
    int          m_selEnd;         //替换后的选区终点
    std::wstring m_newText;        //替换后的全文（单元测试核对用）

    MarkdownEditResult()
        : m_replaceStart(0), m_replaceEnd(0), m_selStart(0), m_selEnd(0)
    {
    }
};

/**
 *  执行一个编辑命令。
 *  @param in      输入（全文、选区、换行字符、占位文字）
 *  @param command 命令，取值见 enum MarkdownEditCommand；不认识的值不做任何替换
 *  @return 结果；不需要改动时替换区间为空、替换文字为空，选区保持原样
 */
MarkdownEditResult ApplyMarkdownEditCommand(const MarkdownEditInput& in, int command);

/**
 *  编辑器里按下回车时的列表续项。光标所在行是列表项（「- 」「* 」「+ 」「1. 」「1) 」，可带任务框「[ ] 」）或引用
 *  （「> 」，可嵌套、可在引用里写列表）时，在光标处换行并在新行写上同样的标记：行首缩进与标记后的空白照原样，
 *  有序列表编号加一，任务框一律为未完成；光标后面的文字原样带到新行。在只有标记、没有内容的空项上回车时，去掉
 *  这一行的列表标记、结束列表，不换行（引用里的空列表项只去掉列表标记、保留引用符）。
 *
 *  以下情况不处理，由宿主按普通回车处理：有选区；光标在行首的标记之前；光标落在全文之外（宿主取到的全文可能
 *  去掉了末尾的空行，此时光标位于末尾的空行上）；所在行不是列表项也不是引用（分隔线「* * *」「- - -」不算列表项）；
 *  所在行位于围栏代码块之内。
 *
 *  @param in  输入：全文、选区与新插入的换行用的字符；m_placeholder 与表格列名不使用。选区下标不做截断
 *  @param out 输出：处理时为要做的替换，以及替换后光标的位置（m_selStart == m_selEnd）；不处理时不改动
 *  @return true 表示已处理这次回车，宿主按 out 做一次可撤销的替换并丢掉这次回车；false 表示宿主照常处理回车
 */
bool ContinueMarkdownListOnEnter(const MarkdownEditInput& in, MarkdownEditResult& out);

/**
 *  把选区覆盖的每一行设为指定级别的标题（docs/Markdown消息方案.md 第 13 节，编辑器「H」按钮的菜单与 Ctrl+1 ~ Ctrl+6）：
 *  行首写成 level 个「#」加一个空格，原来是别的级别的标题换成这一级；level 为 0 时去掉标题前缀、恢复为正文。
 *  多行时空行不加；选区终点正好落在某行行首时那一行不算（与其它行首前缀命令相同）。与格式条原来的标题命令
 *  （md_edit_heading，再点一次去掉）不同，设为同一级别时不改动。
 *  @param in    输入：全文、选区与换行字符（m_placeholder 与表格列名不使用）
 *  @param level 0 表示正文，1 ~ 6 为标题级别；其它值不改动
 *  @return 结果；不需要改动时替换区间为空、替换文字为空，选区保持原样
 */
MarkdownEditResult SetMarkdownHeadingLevel(const MarkdownEditInput& in, int level);

/**
 *  选区第一行（没有选区时为光标所在行）的标题级别，编辑器的标题菜单据此给当前级别打勾。
 *  行首 1 ~ 6 个「#」后跟空格（或就是行尾）才算标题；7 个以上、缺空格（「#标题」）不算。
 *  @param text     全文
 *  @param selStart 选区起点，与 selEnd 顺序不限，越界截断
 *  @param selEnd   选区终点
 *  @return 0 表示正文，1 ~ 6 为标题级别
 */
int MarkdownHeadingLevelAt(const std::wstring& text, int selStart, int selEnd);

/**
 *  清除样式（编辑器 Ctrl+\，docs/Markdown消息方案.md 第 13 节）：去掉选区里的行内样式标记——「**」「__」「~~」、
 *  行内代码的反引号（代码里的文字原样保留）、「*」、作强调用的「_」（单词中间的「_」不算）、<u> 与 </u>；
 *  链接 [文字](地址) 与图片 ![说明](地址) 只留下文字。每行行首的缩进、引用符、列表符号、任务框与标题前缀不动。
 *  选区正好被成对的标记包着时（如「**文字**」里选中「文字」），外侧的标记一起去掉。
 *  @param in 输入：全文、选区（m_lineBreak、m_placeholder 与表格列名不使用）
 *  @return 结果：替换后选中清除样式后的文字；没有选区时不改动
 */
MarkdownEditResult ClearMarkdownInlineStyle(const MarkdownEditInput& in);

/**
 *  插入警告框（编辑器「警告框」按钮的菜单，docs/Markdown消息方案.md 第 13 节）：
 *    - 选区起点所在的行已是警告框的类型行「> [!类型]」时，只把类型换成 type；
 *    - 有选区时，把选区覆盖的各行放进警告框：前面加一行「> [!类型]」，各行行首加「> 」（已以「>」开头的行不再加，
 *      空行写成「>」），然后选中整个警告框；
 *    - 没有选区时，在光标所在行之后另起一段（光标所在行为空行时就在这一行）插入「> [!类型]」与「> 占位文字」，
 *      选中占位文字。
 *  新的警告框与上面的文字隔一个空行（否则会并进上面的引用块），下面紧跟着文字时也补一个空行（否则那行文字会并进警告框）。
 *  @param in   输入：全文、选区、换行字符、占位文字（m_placeholder，如「警告框内容」；表格列名不使用）
 *  @param type 类型，取值见 enum MarkdownAlertType；越界时不改动
 *  @return 结果
 */
MarkdownEditResult InsertMarkdownAlert(const MarkdownEditInput& in, int type);

/**
 *  警告框类型在源文本里的写法（NOTE、TIP、IMPORTANT、WARNING、CAUTION）。
 *  @param type 类型，取值见 enum MarkdownAlertType
 *  @return 类型名；越界时返回空串（指向静态存储，不需要释放）
 */
const wchar_t* MarkdownAlertKeyword(int type);

//编辑器的格式快捷键（docs/Markdown消息方案.md 第 13 节，按用户给出的截图设定）
enum MarkdownShortcut
{
    md_shortcut_none        = 0,    //不是格式快捷键
    md_shortcut_heading1    = 1,    //Ctrl+1：一级标题（2 ~ 6 级依次为 md_shortcut_heading1 + 1 ~ 5，对应 Ctrl+2 ~ Ctrl+6）
    md_shortcut_heading6    = 6,    //Ctrl+6：六级标题
    md_shortcut_bold        = 7,    //Ctrl+B：加粗
    md_shortcut_italic      = 8,    //Ctrl+I：斜体
    md_shortcut_underline   = 9,    //Ctrl+U：下划线（<u>…</u>）
    md_shortcut_code        = 10,   //Ctrl+Shift+`：行内代码
    md_shortcut_strike      = 11,   //Alt+Shift+5：删除线
    md_shortcut_link        = 12,   //Ctrl+K：超链接
    md_shortcut_clear_style = 13,   //Ctrl+\：清除样式
};

/**
 *  按键与修饰键对应的格式快捷键。只认截图里列出的组合，多按或少按修饰键都不算。
 *  @param vk    虚拟键码（WM_KEYDOWN / WM_SYSKEYDOWN 的 wParam）；「`」为 VK_OEM_3（0xC0），「\」为 VK_OEM_5（0xDC）
 *  @param ctrl  是否按着 Ctrl
 *  @param shift 是否按着 Shift
 *  @param alt   是否按着 Alt
 *  @return 取值见 enum MarkdownShortcut
 */
int MarkdownShortcutFor(unsigned vk, bool ctrl, bool shift, bool alt);

} // namespace balloonchatwnd
