/**
 *  CodeComposeLayout.h
 *
 *  「发送代码」窗口（docs/代码片段方案.md §3.1、§5.5、§10）的客户区控件树与状态文字：第一行是语言下拉框、「行号」
 *  勾选框、「格式化」「批注」按钮与状态文字，中间左边是行号栏与等宽字体的编辑框（纯文本、不折行、可横向滚动），
 *  右边是可以收起的批注栏（CodeNotesPane，两者之间是可拖动的分隔条），底部是提示与「取消」「发送」。
 *  编辑框是本文件的 CodeComposeEdit（CodeViewerLayout.h 的 CodeMarkedEdit 的子类）：滚动或内容变化时请行号栏重画，
 *  被批注的行铺浅黄底色，选区变化时通知窗口。
 *  控件树单独成文件，FlamingoUnit 可以按设计尺寸检查排得下（Tests/CodeComposeTests.cpp）。
 *
 *  典型用法（CodeComposeWindow.cpp）：
 *      SetClientContent(BuildCodeComposeBody());
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "CodeViewerLayout.h"   // CodeLineGutter：行号栏与代码查看窗共用

namespace ballooncode {

// ---- 控件 id（避开标题栏按钮固定使用的 1~3）----
const UINT kCodeComposeIdLanguageCaption = 101;   //「语言」小标题
const UINT kCodeComposeIdLanguage        = 102;   //语言下拉框：第 0 项「自动识别」，第 i 项为语言 i - 1
const UINT kCodeComposeIdStatus          = 103;   //状态文字：识别结果、行数，超限时为提示
const UINT kCodeComposeIdEditor          = 104;   //代码编辑框
const UINT kCodeComposeIdHint            = 105;   //底部的快捷键提示
const UINT kCodeComposeIdCancel          = 106;   //「取消」
const UINT kCodeComposeIdSend            = 107;   //「发送」
const UINT kCodeComposeIdLineNumbers     = 108;   //「行号」勾选框（docs/代码片段方案.md 第 10.3 节）
const UINT kCodeComposeIdGutter          = 109;   //编辑框左侧的行号栏（CodeLineGutter，按视觉行编号）
const UINT kCodeComposeIdCodeFrame       = 110;   //行号栏与编辑框外面带边框的框
const UINT kCodeComposeIdFormat          = 111;   //「格式化」按钮（docs/代码片段方案.md 第 10.4 节）
const UINT kCodeComposeIdNotes           = 112;   //「批注」按钮：批注栏收起且有批注时写「批注 (N)」，点了展开；否则为选中的行添加批注
const UINT kCodeComposeIdSplit           = 113;   //代码区与批注栏之间的左右分栏（CodeNotesSplitter）
const UINT kCodeComposeIdNotesPane       = 114;   //右侧的批注栏（CodeNotesPane）
const UINT kCodeComposeIdUndoNote        = 115;   //状态文字右侧的「撤销」链接：删除批注之后出现，点了恢复刚删掉的那条

// ---- 编辑框右键菜单的自定义项（DuiRichEdit 要求不小于 kRichEditMenuCustomBase，即 1000）----
// 选中后编辑框向窗口发 DuiRichEdit::DUIN_RICHTEXT_MENUCOMMAND 通知，编号在 extra 里
const UINT kCodeComposeMenuFormat        = 1101;  //「格式化」
const UINT kCodeComposeMenuAddNote       = 1102;  //「添加批注…」：为选中的行（只有光标时为光标所在的行）添加批注

// ---- 窗口尺寸（整窗尺寸，含标题栏，见开发约定文档 UI 约定第 7 条）----
const int kCodeComposeTitleBarH    = 32;    //标题栏高
const int kCodeComposeWindowWidth  = 760;   //默认宽
const int kCodeComposeWindowHeight = 580;   //默认高
const int kCodeComposeMinWidth     = 560;   //最小宽（2026-10-06 由 480 提高：工具行加了「行号」与「格式化」）
const int kCodeComposeMinHeight    = 360;   //最小高

// 编辑框与行号栏的字号（像素，按 96 DPI 计，窗口按实际 DPI 换算），与聊天里的代码卡片相同
const int kCodeComposeFontPx = 13;

/**
 *  发送代码窗口的编辑框：纯文本模式、不折行的 CodeMarkedEdit（被批注的行铺浅黄底色，按排版引擎的行定位）。
 *  滚动或内容变化时请行号栏重画。
 */
class CodeComposeEdit : public CodeMarkedEdit
{
public:
    CodeComposeEdit();

    /**
     *  设置要跟着滚动与内容变化重画的行号栏。
     *  @param gutter 行号栏（借用，同一控件树里的控件）；为 NULL 时不通知
     */
    void SetGutter(CodeLineGutter* gutter);

    /**
     *  设置右键菜单「添加批注…」是否可用（批注达到上限时不可用）。
     */
    void SetAddNoteEnabled(bool enabled);

    /**
     *  设置右键菜单「格式化」是否可用（编辑批注期间与批注模式下不可用，与「格式化」按钮一致）。
     */
    void SetFormatEnabled(bool enabled);

    //DuiRichEdit 覆写：引擎报告滚动信息变化时，除基类的处理外再请行号栏重画。
    //这是在引擎的回调栈里被调用的，只做失效标记，不反过来查询引擎
    void TxSiteScrollInfoChanged(bool bVertical) override;

    //CodeMarkedEdit 覆写：内容变化（EN_CHANGE）时请行号栏重画（行数可能变了），其余交给基类
    HRESULT TxSiteNotify(DWORD iNotify, void* pv) override;

protected:
    //DuiRichEdit 覆写：按 SetAddNoteEnabled、SetFormatEnabled 设置「添加批注…」「格式化」是否可用
    void OnBuildContextMenu(std::vector<balloonwjui::DuiRichEditMenuItem>& items) override;

private:
    CodeLineGutter* m_gutter;                          //行号栏（借用）
    bool   m_addNoteEnabled;                           //右键菜单「添加批注…」是否可用
    bool   m_formatEnabled;                            //右键菜单「格式化」是否可用
};

/**
 *  发送代码窗口行号栏的宽度：至少按两位数留宽，行数的位数更多时加宽。
 *  @param lines  编辑框当前的行数
 *  @param fontPx 行号字号（像素）
 *  @return 宽度（像素）
 */
int CodeComposeGutterWidth(int lines, int fontPx);

/**
 *  把选区扩展到它覆盖的整行（编辑框里的换行是 \r）。选区的结束位置正好在某一行的行首时，那一行不算在内。
 *  @param text      编辑框全文
 *  @param selBegin  选区起点（字符位置）
 *  @param selEnd    选区终点（字符位置，不含）；不大于 selBegin 时按光标所在的一行处理
 *  @param lineBegin [出参] 第一行的行首位置
 *  @param lineEnd   [出参] 最后一行的行尾位置（不含该行的换行符）
 */
void ExpandSelectionToLines(const std::wstring& text, long selBegin, long selEnd, long& lineBegin, long& lineEnd);

/**
 *  搭「发送代码」窗口的客户区控件树。编辑框的字体由窗口另行设置（窗口持有字体句柄）。
 *  @return 控件树的根
 */
std::unique_ptr<balloonwjui::DuiControl> BuildCodeComposeBody();

/**
 *  状态文字。
 *  @param autoDetect 语言下拉框选的是「自动识别」
 *  @param lang       实际采用的语言（自动识别时是识别结果），取值见 codesnippet::CodeLanguage
 *  @param lines      行数；为 0 表示编辑框是空的
 *  @param tooLarge   发出去会超过单条消息的体积上限
 *  @return 状态文字：超限时为超限说明；空时为空串；否则为识别结果与行数
 */
std::wstring CodeComposeStatusText(bool autoDetect, int lang, int lines, bool tooLarge);

/**
 *  批注模式（docs/代码片段方案.md 第 11.3 节）的状态文字。
 *  @param senderName 原消息发送者的显示名；为空时不写「某某的代码」
 *  @param lang       原消息的语言，取值见 codesnippet::CodeLanguage
 *  @param lines      行数
 *  @param newCount   本次新增或修改的批注条数；为 0 时「发送」不可用
 *  @param tooLarge   发出去会超过单条消息的体积上限
 *  @return 超限时为超限说明；否则为「张三的代码 · C/C++ · 42 行」，后面接「添加或修改批注后才能发送」或「已新增或修改 N 条批注」
 */
std::wstring CodeAnnotateStatusText(const std::wstring& senderName, int lang, int lines, int newCount, bool tooLarge);

} // namespace ballooncode
