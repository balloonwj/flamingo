/**
 *  MarkdownPreviewWindow.h
 *
 *  Markdown 预览窗口（docs/Markdown消息方案.md 第 11.3 节）：点聊天窗里的 Markdown 卡片（或右键「在窗口中查看」）打开，
 *  显示整篇文档的渲染效果。顶部一行是「发送者 · 时间」与三个按钮：
 *    - 「复制 ▾」：复制 Markdown 源文本、复制为纯文字、复制为图片；
 *    - 「导出 ▾」：与编辑器同一套导出（.md / .html / .png / .pdf / .docx，按可用情况列出）；
 *    - 「在编辑器中打开」：把源文本放进一个新的 Markdown 编辑器，可以修改后再发送。
 *  正文支持拖选、Ctrl+C、右键「复制 / 全选」、点链接在浏览器打开（ChatMarkdownPreview 提供）。
 *
 *  窗口由 CMarkdownEditorManager 创建与持有，同一条消息只开一个窗口；登出时全部关闭。
 *
 *  窗口私有消息 kMsgMarkdownPreviewDeferred（WM_APP + 0x8C）：工具栏按钮先投给自己，等点击通知处理完再弹菜单或打开
 *  编辑器，已登记在开发约定文档 UI 约定第 6 条。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "MarkdownEditorManager.h"                                // MarkdownPreviewDoc
#include "../BalloonMain/TitleBarIcon.h"                          // ProgramWindowIcons

namespace balloonwjui { class DuiLabel; class DuiScrollView; class DuiMenu; }
namespace balloonchatwnd { class ChatMarkdownPreview; }

namespace balloonmd {

//预览窗口投给自己的延后操作：wParam 为 enum MarkdownPreviewAction 的取值，lParam 未用
const UINT kMsgMarkdownPreviewDeferred = WM_APP + 0x8C;

//延后执行的操作
enum MarkdownPreviewAction
{
    md_preview_action_copy_menu   = 1,   //弹出复制方式菜单
    md_preview_action_export_menu = 2,   //弹出导出格式菜单
    md_preview_action_open_editor = 3,   //在编辑器中打开
};

/**
 *  Markdown 预览窗口。继承 balloonwjui::DuiFrameWindow（无边框自绘窗体与标题栏）。只在界面线程上使用。
 */
class CMarkdownPreviewWindow : public balloonwjui::DuiFrameWindow
{
public:
    CMarkdownPreviewWindow();

    /**
     *  创建并显示窗口。
     *  @param doc          要显示的文档
     *  @param cascadeIndex 第几个打开的窗口（从 0 起），据此相对屏幕中心错开
     *  @return 窗口已显示时返回 true
     */
    bool Open(const MarkdownPreviewDoc& doc, int cascadeIndex);

    //消息标识（打开时传入的 m_key），管理器据此复用窗口
    const std::string& Key() const;

    //把窗口调到前台（最小化时先还原）
    void Activate();

    BEGIN_MSG_MAP(CMarkdownPreviewWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //Esc 关闭窗口，要赶在 DuiHost 之前处理
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(kMsgMarkdownPreviewDeferred, OnDeferred)
        //销毁时清空借用的控件指针，只旁听、不消费，DuiFrameWindow 照常处理
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

protected:
    //窗口彻底销毁之后：通知管理器释放本对象（之后不得再访问任何成员）
    void OnFinalMessage(HWND hWnd) override;

private:
    //在 id 对应的按钮正下方弹出 DuiMenu，返回选中的命令号；没有选择时返回 0
    UINT TrackMenuBelow(UINT buttonId, balloonwjui::DuiMenu& menu);

    //弹出复制方式菜单并执行
    void DoCopyMenu();

    //弹出导出格式菜单并导出
    void DoExportMenu();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    MarkdownPreviewDoc m_doc;   //显示的文档；打开时复制一份，窗口存续期间不变
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏与任务栏的程序图标，本对象持有，随本对象析构释放

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonchatwnd::ChatMarkdownPreview* m_pPreview;   //正文
    balloonwjui::DuiScrollView*          m_pScroll;    //正文的滚动视图
};

} // namespace balloonmd
