/**
 *  CodeSnippetDemoWindow.h
 *
 *  「代码片段」演示窗口（调试入口 --codesnippet，只在 Debug 构建里有）。用来在接入聊天窗之前看效果：
 *  左边是粘贴区，可以从 Visual Studio、浏览器等外部程序 Ctrl+V 粘贴代码；右边按聊天窗的样式同时画出
 *  「作为普通文字消息发送」的现状与「作为代码片段发送」的新方案，粘贴或修改后自动刷新。
 *
 *  顶部可以选语言（默认自动识别）、开关行号与自动换行；代码卡片上的「复制」真的写剪贴板，可以粘到记事本里
 *  核对格式是否保持；「展开全部 / 收起」在本窗口里就地展开（接入聊天窗后改为打开代码查看窗）。
 *  「载入示例」轮流填入几种语言的示例代码，含 Tab 缩进与中文注释。
 *  左边粘贴区里一个 Tab 显示为半英寸宽：纯文本模式下 RichEdit 用的是 balloonui 宿主给出的默认段落格式
 *  （DuiTextHost 的 m_pfDefault），DuiRichEdit 没有公开修改它的接口；右边代码卡片按 4 列展开，以右边为准。
 *
 *  只用 balloonui 现成控件（DuiLabel / DuiComboBox / DuiButton / DuiRichEdit / DuiScrollView / 布局容器），
 *  右侧预览区是一个自绘控件，代码卡片的排版与绘制全部来自 BalloonChatWnd/ChatCodeBlock，不改 balloonui。
 *
 *  窗口私有的定时器：kTimerRefresh（粘贴区文字变化后延时刷新预览）、kTimerCopied（「已复制」恢复为「复制」）、
 *  kTimerSelfTest（--compose-selftest：发送代码窗口显示出来之后再开始自测），编号取得较大，避开 balloonui 宿主窗口自己用的定时器。
 *
 *  典型用法（DebugEntryPoints.cpp）：
 *      m_codeSnippetDemo.CreateAndShow(ctx.m_hHost);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "CodeComposeWindow.h"   // --compose：同时打开发送代码窗口（成员需完整类型）

namespace balloonwjui { class DuiLabel; class DuiComboBox; class DuiButton; class DuiRichEdit; class DuiScrollView; }

namespace ballooncode {

class CodeSnippetPreview;

/**
 *  「代码片段」演示窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与标题栏。
 */
class CCodeSnippetDemoWindow : public balloonwjui::DuiFrameWindow
{
public:
    CCodeSnippetDemoWindow();
    ~CCodeSnippetDemoWindow();

    /**
     *  创建并显示窗口（已创建时只调到前台）。
     *  @param hHost 调试入口的宿主窗口：本窗口关闭时向它投递 WM_CLOSE_MAINDLG 请求退出进程；
     *               为 NULL 时关闭只销毁本窗口
     *  @return 窗口已显示时返回 true
     */
    bool CreateAndShow(HWND hHost);

    BEGIN_MSG_MAP(CCodeSnippetDemoWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        //--compose 打开的发送代码窗口点了「发送」：代码回填到粘贴区
        MESSAGE_HANDLER(kMsgCodeComposeSend, OnComposeSend)
        //销毁时释放编辑框字体、清空借用的控件指针，只旁听、不消费，DuiFrameWindow 照常处理
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //搭客户区控件树，捕获各控件指针
    void BuildClient();

    //按粘贴区当前的文字与顶部选项刷新预览和状态行
    void RefreshPreview();

    //下拉框当前选中的语言；选中「自动识别」时返回 -1
    int SelectedLanguage() const;

    //把剪贴板里的文字放进粘贴区（替换原有内容）
    void PasteFromClipboard();

    /**
     *  --compose-selftest：在发送代码窗口里按用户的操作顺序自动添加批注（放光标、点「批注」、填说明、点「确定」，
     *  再换一行加第二条，最后回到第一条所在的行点「批注」），鼠标点击以 SendMessage 发给窗口，每一步的状态写进日志。
     *  只给开发者排查批注交互用。
     */
    void RunComposeSelfTest();

    //轮流填入下一段示例代码
    void LoadNextSample();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //发送代码窗口交回的代码（lParam 为堆上的 CodeComposeResult，处理后 delete）：填进粘贴区并刷新预览
    LRESULT OnComposeSend(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    HWND   m_hHost;          //调试入口的宿主窗口（借用），关闭本窗口时请它退出进程；可为 NULL
    HFONT  m_editorFont;     //粘贴区的等宽字体，本类持有，窗口销毁时释放（DuiRichEdit 只借用不释放）
    int    m_sampleIndex;    //下一次「载入示例」填入第几段示例，按示例总数循环
    CCodeComposeWindow m_compose;   //命令行另带 --compose 时打开的发送代码窗口（核对外观与交互用），随本窗口关闭
    long long m_lastAnnotateBaseMsgId;      //发送代码窗口最近一次交回的结果里原消息的 msgid（不是批注模式时为 0），自测据此检查
    int       m_lastAnnotateNewNotes;       //同一次结果里标「新」的批注条数，自测据此检查
    std::vector<codesnippet::CodeNote> m_previewNotes;   //发送代码窗口交回的批注，预览卡片据此标出被批注的行；载入示例时清空

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiComboBox*   m_pLanguage;      //语言下拉框，第一项为「自动识别」
    balloonwjui::DuiButton*     m_pLineNumbers;   //「行号」勾选框
    balloonwjui::DuiButton*     m_pWrap;          //「自动换行」勾选框
    balloonwjui::DuiLabel*      m_pStatus;        //状态行：识别结果、行数、像不像代码
    balloonwjui::DuiRichEdit*   m_pEditor;        //粘贴区（纯文本模式、等宽字体、不自动换行）
    balloonwjui::DuiScrollView* m_pScroll;        //右侧预览的滚动视图
    CodeSnippetPreview*         m_pPreview;       //右侧预览（自绘控件）
};

} // namespace ballooncode
