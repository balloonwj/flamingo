/**
 *  输入一个名字的小窗口（新建文件夹、重命名），ShareNameDialog.h
 *
 *  浏览窗口的「新建文件夹」与右键菜单的「重命名」用它取得名字（docs/文件共享方案.md 7.4，第 2 期第 2 步）：
 *  一行提示、一个单行输入框、一行错误提示、「确定」「取消」两个按钮。点「确定」或在输入框里按回车时先按
 *  ShareBrowseModel::CheckNewName 在本地检查，不合法就在窗口里写出原因并保持打开；合法则关闭窗口并把名字交给监听者。
 *  按 Esc、点「取消」或标题栏的关闭按钮直接关闭，不回调。
 *
 *  非模态、带所有者：打开期间把所有者窗口置为不可操作，关闭时恢复（与客户端的关闭确认框相同，不用嵌套消息循环）。
 *  控件全部用 balloonui 现成的（DuiLabel / DuiEdit / DuiButton / DuiVBox / DuiHBox）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（浏览窗口点了「新建文件夹」）：
 *      m_nameDialog.Open(m_hWnd, kNamePurposeNewFolder, L"新建文件夹", L"文件夹名称", L"新建文件夹", this);
 *      ...
 *      void OnShareNameEntered(int purpose, const std::wstring& name) { ... }
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏与任务栏的程序图标

namespace balloonwjui {
class DuiLabel;
class DuiEdit;
}

namespace fileshare {

/**
 *  名字输入窗口的监听者，由打开它的窗口实现。回调在 UI 线程上。
 */
class IShareNameDialogListener
{
public:
    virtual ~IShareNameDialogListener()
    {
    }

    /**
     *  用户确认了一个合法的名字。回调时窗口已隐藏、所有者已恢复可操作。
     *  @param purpose 打开时传入的用途编号，原样带回
     *  @param name    输入的名字
     */
    virtual void OnShareNameEntered(int purpose, const std::wstring& name) = 0;
};

/**
 *  CShareNameDialog —— 输入一个名字的小窗口。只在 UI 线程使用。
 */
class CShareNameDialog : public balloonwjui::DuiFrameWindow
{
public:
    CShareNameDialog();
    ~CShareNameDialog();

    /**
     *  打开窗口；已打开时先关闭旧的再打开。
     *  @param owner    所有者窗口（打开期间被置为不可操作），可为 NULL；不转移所有权
     *  @param purpose  用途编号，确认时原样交给监听者
     *  @param title    窗口标题
     *  @param prompt   输入框上方的提示
     *  @param initial  输入框里预先填好的文字（重命名时为原名，全部选中）
     *  @param listener 监听者（不拥有，须在本窗口关闭之前一直有效），不可为 NULL
     *  @return 窗口已显示返回 true
     */
    bool Open(HWND owner, int purpose, const wchar_t* title, const wchar_t* prompt, const std::wstring& initial,
              IShareNameDialogListener* listener);

    /**
     *  关闭窗口并恢复所有者，不回调。窗口未创建时调用是安全的。
     */
    void Close();

    //窗口是否打开着
    bool IsOpen() const;

    //把输入框的内容换成 text（测试用）
    void SetInputText(const std::wstring& text);

    /**
     *  与点「确定」相同：检查名字，合法则关闭窗口并回调监听者，不合法则显示原因（测试也直接调用）。
     *  @return 名字合法、已回调返回 true
     */
    bool Confirm();

    //当前显示的错误提示；没有错误时为空串（测试用）
    std::wstring GetErrorText() const;

    BEGIN_MSG_MAP(CShareNameDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏的关闭按钮、按 Esc 或「取消」（经 PostMessage）
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：两个按钮的点击、输入框里的回车与 Esc
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //构建客户区控件树
    void BuildContent(const wchar_t* prompt);

    //显示或清除错误提示
    void SetError(const std::wstring& text);

    //恢复所有者窗口为可操作并把它带到前台
    void RestoreOwner();

private:
    HWND                      m_owner;     //所有者窗口；打开期间被置为不可操作，关闭时恢复；没有时为 NULL
    int                       m_purpose;   //用途编号
    IShareNameDialogListener* m_listener;  //监听者（不拥有）
    balloonwjui::DuiEdit*     m_edit;      //输入框（归控件树所有，窗口销毁后失效）
    balloonwjui::DuiLabel*    m_error;     //错误提示（同上）
    balloonmain::ProgramWindowIcons m_programIcons;  //标题栏与任务栏 / Alt-Tab 的程序图标，本对象持有，跨关闭再打开复用
};

} // namespace fileshare
