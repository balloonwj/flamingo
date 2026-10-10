/**
 *  屏幕共享模块的确认小窗（可带一个勾选项），ShareConfirmDialog.h
 *
 *  一段提问文字（最多两行，自动换行）、一个可选的勾选项，加「取消」与确认按钮。确认时向通知窗口（屏幕共享控制器）
 *  PostMessage 调用方指定的结果消息，wParam 为 1、lParam 为勾选项的状态（1 勾选 / 0 未勾选）；「取消」或关窗时
 *  wParam 为 0。
 *  现有一处用法（第 14 期）：发起人在观看者名单里点某人的「移出」，弹出「确定将 李四 移出屏幕共享吗？」，勾选项为
 *  「允许他再次加入」（默认勾选），结果消息为 kMsgShareRemoveConfirmResult。
 *
 *  形态：balloonui DuiFrameWindow 的 WS_POPUP 小窗，居中在 anchor 所在显示器的工作区上，置顶、不进任务栏。不设所有者窗口：
 *  balloonui 把按钮通知发给 GetParent 返回的窗口，弹出窗口设了所有者，按钮通知就会发到所有者那里（见 ShareAnnotationBar.h）。
 *  发起人一侧可排除在屏幕采集之外，观看者看不到这个确认框。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（UI 线程）：
 *      ShareConfirmSpec spec;
 *      spec.m_title = L"移出观看者";
 *      spec.m_prompt = MakeShareRemovePrompt(name);
 *      spec.m_checkText = L"允许他再次加入";
 *      spec.m_okText = L"移出";
 *      spec.m_resultMsg = kMsgShareRemoveConfirmResult;
 *      m_removeDlg.OpenCentered(mainWindow(), m_hWnd, spec, true);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace balloonwjui
{
    class DuiButton;
}

namespace screenshare {

//确认小窗的用法参数
struct ShareConfirmSpec
{
    std::wstring m_title;       //标题栏文字
    std::wstring m_prompt;      //提问文字，最多显示两行
    std::wstring m_checkText;   //勾选项的文字；为空时不显示勾选项，结果的 lParam 恒为 0
    bool         m_checked;     //勾选项的初始状态
    std::wstring m_okText;      //确认按钮的文字，如「移出」
    UINT         m_resultMsg;   //结果消息号，投给通知窗口
    int          m_promptLines; //提问区的行数（2026-10-07 起可设，默认 2）：凭分享链接加入的确认要写标题、描述与提示，需要更多行
    int          m_width;       //整窗宽（像素，2026-10-07 起可设）；0 表示默认宽 360

    ShareConfirmSpec();
};

//屏幕共享模块的确认小窗
class CShareConfirmDialog : public balloonwjui::DuiFrameWindow
{
public:
    CShareConfirmDialog();
    ~CShareConfirmDialog();

    /**
     *  按给定参数打开确认框，居中在 anchor 所在显示器的工作区上；已打开时先按取消关闭旧的（旧的结果照常发出）。
     *  @param anchor             定位用的窗口（exe 主窗口），只用来确定显示器、不作为所有者；为 NULL 时居中在主显示器上
     *  @param notifyWnd          通知窗口，不拥有
     *  @param spec               用法参数
     *  @param excludeFromCapture 是否排除在屏幕采集之外（发起人共享屏幕时传 true）
     */
    void OpenCentered(HWND anchor, HWND notifyWnd, const ShareConfirmSpec& spec, bool excludeFromCapture);

    //不发结果直接关闭；可重复调用
    void Close();

    //确认框是否开着
    bool IsOpen() const;

    BEGIN_MSG_MAP(CShareConfirmDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

    //控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3；2026-10-07 起公开，单元测试按它找控件）
    enum
    {
        kIdCheck  = 10,   //勾选项
        kIdOk     = 11,   //确认按钮
        kIdCancel = 12,   //取消
        kIdPrompt = 13,   //提问文字
    };

private:

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按当前参数算出的整窗高度（没有勾选项时更矮）
    int dialogHeight() const;
    //发出结果并关窗：ok 为 false 时 wParam 为 0
    void finish(bool ok);

    HWND                    m_notifyWnd;   //通知窗口（不拥有）
    ShareConfirmSpec        m_spec;        //本次打开的用法参数
    balloonwjui::DuiButton* m_check;       //勾选项（控件树持有，本指针仅引用）；不显示勾选项时为 NULL
    bool                    m_finished;    //本次是否已发过结果
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
