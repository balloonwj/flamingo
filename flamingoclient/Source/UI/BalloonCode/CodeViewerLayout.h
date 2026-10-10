/**
 *  CodeViewerLayout.h
 *
 *  代码查看窗（docs/代码片段方案.md §3.3、§5.4、§10.5）的客户区控件树与几个专用控件：
 *    · CodeLineGutter：正文左侧的行号栏。绘制时向正文编辑框的排版引擎查询每个逻辑行起点的纵坐标，
 *      所以开了自动换行、或某些行因含中文而行高不同时，行号仍与正文对齐；被批注的行在左边画橙色竖条；
 *    · CodeMarkedEdit：被批注的行铺浅黄底色的 DuiRichEdit，代码查看窗与发送代码窗口的编辑框共用；
 *    · CodeViewerEdit：正文编辑框（只读，CodeMarkedEdit 的子类），滚动信息变化时请行号栏重画。
 *  正文右侧是批注栏（CodeNotesPane.h），与正文之间是可拖动的分隔条；没有批注时批注栏收起、工具条不显示「批注」。
 *  能追加批注时（docs/代码片段方案.md 第 12 节）批注栏可以编辑，底部多一行状态文字与「发送」，正文右键菜单多「添加批注…」。
 *
 *  只用 balloonui 现成控件与上面两个子类，不改 balloonui。控件树单独成文件，FlamingoUnit 可以按设计尺寸
 *  检查排得下（Tests/CodeViewerTests.cpp 的 CV5）。
 *
 *  典型用法（CodeViewerWindow.cpp）：
 *      SetClientContent(BuildCodeViewerBody(CodeLineGutter::WidthForLines(lines, fontPx)));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

namespace ballooncode {

// ---- 控件 id（避开标题栏按钮固定使用的 1~3）----
const UINT kCodeViewerIdLanguageCaption = 101;   //「语言」小标题
const UINT kCodeViewerIdLanguage        = 102;   //语言下拉框（只改本窗口的着色）
const UINT kCodeViewerIdWrap            = 103;   //「自动换行」勾选框
const UINT kCodeViewerIdCopy            = 104;   //「复制全部」
const UINT kCodeViewerIdSave            = 105;   //「另存为…」
const UINT kCodeViewerIdGutter          = 106;   //行号栏
const UINT kCodeViewerIdEdit            = 107;   //正文编辑框
const UINT kCodeViewerIdNotes           = 108;   //工具条的「批注 (N)」：展开或收起批注栏；没有批注时隐藏
const UINT kCodeViewerIdSplit           = 109;   //正文与批注栏之间的左右分栏（CodeNotesSplitter）
const UINT kCodeViewerIdNotesPane       = 110;   //右侧只读的批注栏（CodeNotesPane）
const UINT kCodeViewerIdAnnotate        = 111;   //工具条的「添加批注」：为选中的行添加批注（docs/代码片段方案.md 第 12 节）；
                                                 //只在单聊窗、群聊窗打开、且消息已有 msgid 时显示
const UINT kCodeViewerIdAnnotateBar     = 112;   //底部一行（状态文字与「发送」）的容器；能追加批注时才显示
const UINT kCodeViewerIdAnnotateStatus  = 113;   //底部一行的状态文字：某某的代码、语言、行数，以及能否发送
const UINT kCodeViewerIdAnnotateSend    = 114;   //底部一行的「发送」：把带批注的新代码片段交给聊天窗发出

// ---- 正文右键菜单的自定义项（DuiRichEdit 要求不小于 kRichEditMenuCustomBase，即 1000）----
const UINT kCodeViewerMenuAddNote       = 1201;  //「添加批注…」：为选中的行（只有光标时为光标所在的行）添加批注

// 被批注的行在代码里的浅黄底色（代码查看窗与发送代码窗口的编辑框共用；聊天里的代码卡片用同一个颜色）
const COLORREF kCodeNoteLineBg = RGB(0xFF, 0xF4, 0xD0);

// ---- 窗口尺寸（整窗尺寸，含标题栏，见开发约定文档 UI 约定第 7 条）----
const int kCodeViewerTitleBarH   = 32;    //标题栏高
const int kCodeViewerWindowWidth = 900;   //默认宽
const int kCodeViewerWindowHeight = 640;  //默认高
const int kCodeViewerMinWidth    = 740;   //最小宽（2026-10-07 由 560 提高：「批注 (N)」与「添加批注」都显示时工具条要放得下）
const int kCodeViewerMinHeight   = 360;   //最小高

/**
 *  正文左侧的行号栏。行号画在每个逻辑行第一个视觉行的位置，纵坐标每次绘制时向编辑框的排版引擎查询。
 */
class CodeLineGutter : public balloonwjui::DuiControl
{
public:
    CodeLineGutter();

    /**
     *  设置正文编辑框。
     *  @param edit 编辑框（借用，同一控件树里的兄弟控件，随控件树销毁）；为 NULL 时不画行号
     */
    void SetEdit(balloonwjui::DuiRichEdit* edit);

    /**
     *  设置各逻辑行在编辑框里的起始字符位置（BuildLineStartPositions 的结果），并重画。
     */
    void SetLineStarts(const std::vector<long>& starts);

    /**
     *  切换编号方式。编辑框不折行时每个视觉行就是一个逻辑行，可以直接按排版引擎的视觉行编号，不必提供各行起点；
     *  发送代码窗口用这种方式，打字时行号始终与内容一致（docs/代码片段方案.md 第 10.3 节）。
     *  @param follow true 按视觉行编号（不使用 SetLineStarts 的数据）；false 按 SetLineStarts 给出的逻辑行编号（默认）
     */
    void SetFollowEngineLines(bool follow);

    /**
     *  指定行号字号并重画。
     *  @param fontPx 字号（像素）；不大于 0 时按 FontPxForDpi 取代码查看窗的字号（默认）
     */
    void SetFontPx(int fontPx);

    /**
     *  设置要在行号左侧画竖条标记的行（批注所在的行，docs/代码片段方案.md 第 10.5 节）并重画。
     *  @param ranges 每项是一段行范围的起止逻辑行（从 0 起、含两端）；为空时不画
     */
    void SetMarkedRanges(const std::vector<std::pair<int, int> >& ranges);

    /**
     *  某一逻辑行是否在标记范围内。
     *  @param line 逻辑行（从 0 起）
     */
    bool IsLineMarked(int line) const;

    /**
     *  行号栏所需的宽度：按总行数的位数加左右留白。
     *  @param lines  总行数
     *  @param fontPx 行号字号（像素）
     *  @return 宽度（像素）
     */
    static int WidthForLines(int lines, int fontPx);

    /**
     *  行号与正文共用的字号（像素）：查看窗正文为 kCodeViewerFontHalfPoints 半磅，按 DPI 换算。
     *  @param dpi 窗口的 DPI；不大于 0 时按 96
     */
    static int FontPxForDpi(int dpi);

    //DuiControl 覆写：画底色、右侧分隔线与可见范围内的行号
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    //第 line 行（从 0 起）起点的字符位置；按视觉行编号时向引擎查询，否则取 m_lineStarts
    long LineStartCp(int line) const;

    balloonwjui::DuiRichEdit* m_edit;         //正文编辑框（借用）
    std::vector<long>         m_lineStarts;   //各逻辑行的起始字符位置，升序；按视觉行编号时不使用
    bool                      m_followEngine; //是否按排版引擎的视觉行编号（编辑框不折行时使用）
    int                       m_fontPx;       //行号字号（像素）；不大于 0 时按 FontPxForDpi 计算
    std::vector<std::pair<int, int> > m_marked;   //要画竖条标记的行范围（起止逻辑行，从 0 起、含两端）
};

/**
 *  标出被批注的行的编辑框：被批注的行铺浅黄底色（docs/代码片段方案.md 第 10.5 节）。
 *  排版引擎画文字时背景是透明的、底色由控件自己铺，所以先按常规画一遍，再裁剪到被批注的行的范围内、以浅黄底色重画一遍，
 *  整行铺满，自动换行时同样正确。纯文本模式与富文本模式都适用，不需要改动文字的格式。
 */
class CodeMarkedEdit : public balloonwjui::DuiRichEdit
{
public:
    CodeMarkedEdit();

    /**
     *  设置被批注的行并重画。
     *  @param ranges 每项为一段逻辑行的 (起始行, 结束行)，从 0 起、含两端；为空时不标出
     */
    void SetMarkedRanges(const std::vector<std::pair<int, int> >& ranges);

    /**
     *  设置各逻辑行在编辑框里的起始字符位置（BuildLineStartPositions 的结果）。编辑框会自动换行时须设置；
     *  为空时按排版引擎的行（EM_LINEINDEX）定位，只适用于不折行的编辑框。
     */
    void SetMarkLineStarts(const std::vector<long>& starts);

    /**
     *  设置行高，用来确定最后一行底色的下边（其余各行的下边取下一行的起点）。
     *  @param px 行高（像素，已按窗口 DPI 换算）；不大于 0 时按 13 像素字号的等宽字体估算
     */
    void SetLineHeight(int px);

    /**
     *  设置选区变化时投给宿主窗口的消息。引擎在自己的回调里报告选区变化，那时不能反过来查询引擎，
     *  所以改为投递消息，由窗口在消息里读取选区（用来高亮光标所在行的批注）。
     *  @param msg    消息号；为 0 时不投递
     *  @param wParam 随消息带上的 wParam
     */
    void SetSelChangeMessage(UINT msg, WPARAM wParam);

    //DuiRichEdit 覆写：按常规画完之后，在被批注的行的范围内以浅黄底色重画一遍
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    //DuiRichEdit 覆写：选区变化（EN_SELCHANGE）时投递 SetSelChangeMessage 设置的消息，其余交给基类
    HRESULT TxSiteNotify(DWORD iNotify, void* pv) override;

protected:
    //DuiRichEdit 覆写：重画被批注的行时底色为浅黄，其余时候交给基类
    COLORREF FillColor() const override;

private:
    //被批注的行在宿主客户区里占的纵向范围（只取与控件区域相交的部分）
    std::vector<RECT> MarkedBands();

    //第 line 个逻辑行（从 0 起）起点的字符位置
    long MarkLineStart(int line) const;

    std::vector<std::pair<int, int> > m_marked;   //被批注的逻辑行，(起始行, 结束行)，从 0 起、含两端
    std::vector<long> m_lineStarts;               //各逻辑行的起始字符位置；为空时按排版引擎的行定位
    int  m_lineHeight;                            //行高（像素）
    bool m_markPass;                              //正在以浅黄底色重画被批注的行（FillColor 据此换色）
    UINT   m_selChangeMsg;                        //选区变化时投给宿主窗口的消息；0 表示不投递
    WPARAM m_selChangeParam;                      //随该消息带上的 wParam
};

/**
 *  等宽字体 Consolas 在给定字号下的行高（像素），用于确定被批注的最后一行底色的下边。
 *  @param fontPx 字号（像素）
 *  @return 行高；取不到时返回 0
 */
int CodeFontLineHeight(int fontPx);

/**
 *  正文编辑框：只读的 CodeMarkedEdit，滚动信息变化时请行号栏重画。能追加批注时，窗口给它追加右键菜单项
 *  「添加批注…」（kCodeViewerMenuAddNote），其可用状态由 SetAddNoteEnabled 控制。
 */
class CodeViewerEdit : public CodeMarkedEdit
{
public:
    CodeViewerEdit();

    /**
     *  设置要跟着滚动重画的行号栏。
     *  @param gutter 行号栏（借用，同一控件树里的兄弟控件）；为 NULL 时不通知
     */
    void SetGutter(CodeLineGutter* gutter);

    /**
     *  设置右键菜单「添加批注…」是否可用（正在编辑批注、批注达到上限时不可用）。菜单项本身由窗口用
     *  AppendContextMenuItem 追加，没有追加时本设置不起作用。
     */
    void SetAddNoteEnabled(bool enabled);

    //DuiRichEdit 覆写：引擎报告滚动信息变化（滚动、内容或宽度变化）时，除基类的处理外再请行号栏重画。
    //这是在引擎的回调栈里被调用的，只做失效标记，不反过来查询引擎
    void TxSiteScrollInfoChanged(bool bVertical) override;

protected:
    //DuiRichEdit 覆写：按 SetAddNoteEnabled 设置「添加批注…」是否可用
    void OnBuildContextMenu(std::vector<balloonwjui::DuiRichEditMenuItem>& items) override;

private:
    CodeLineGutter* m_gutter;           //行号栏（借用）
    bool            m_addNoteEnabled;   //右键菜单「添加批注…」是否可用
};

/**
 *  搭代码查看窗的客户区控件树：第一行是语言、自动换行、「批注 (N)」「添加批注」（默认隐藏）与两个按钮，下面是行号栏与
 *  正文编辑框，右侧是收起状态的只读批注栏，最下面是状态文字与「发送」一行（默认隐藏，能追加批注时窗口显示它）。
 *  行号栏与编辑框已经互相关联好（SetEdit / SetGutter）。
 *  @param gutterWidth 行号栏宽度（像素，CodeLineGutter::WidthForLines 的结果）
 *  @return 控件树的根
 */
std::unique_ptr<balloonwjui::DuiControl> BuildCodeViewerBody(int gutterWidth);

} // namespace ballooncode
