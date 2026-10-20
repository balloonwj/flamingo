/**
 *  退出、登出、电脑关机或注销前的「云端录像」对话框，CloudExitDialog.h
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.4 节（2026-10-07 用户确认）。还有云端录像在合成、上传或已暂停时，退出程序、
 *  登出账号、电脑关机或注销之前先问用户：列出这些录像，按钮为「退出，下次继续」「退出并放弃这些录像」「不退出」
 *  （登出、注销时文字中的「退出」换成「登出」「注销」）。电脑关机时程序分不清是关机还是重启，按钮改为「关机」「重启」
 *  「取消关机」，另有勾选项「同时放弃这些录像（从云端删除）」。选择放弃时，由调用方（CMediaModule）请云端录像中心删除。
 *
 *  本文件分两部分：
 *    - 纯逻辑（单元测试 Source/Tests/CloudExitDialogTests.cpp，标签 [cloudexit]）：各场合的文字与按钮、按钮与结果的对应、
 *      录像清单的写法；
 *    - CCloudExitDialog：balloonui DuiFrameWindow 的模态对话框。不设所有者窗口（2026-10-14 之前这样做是因为 balloonui
 *      会把有所有者的 WS_POPUP 窗口的控件通知发给所有者；现在控件通知一律发回本窗口，见开发约定文档 UI 约定第 11 条），
 *      模态期间禁用调用方给的窗口；置顶并出现在任务栏，关机时用户回到桌面能找到它。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法（CMediaModule::ConfirmCloudRecordExit）：
 *      screenrecord::CCloudExitDialog dlg;
 *      const int choice = dlg.RunModal(mainWindow, media_cloud_exit_app, titles);
 *      if (screenrecord::CloudExitChoiceAbandons(choice)) cloud->AbandonUnfinished();
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>
#include <vector>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏与任务栏的程序图标

namespace balloonwjui
{
    class DuiButton;
}

namespace screenrecord {

//对话框的按钮个数（从左到右：继续退出、放弃并退出 / 重启、不退出）
const int kCloudExitButtonCount = 3;
//录像清单里最多列出的条数，更多的写「等 N 条」
const int kCloudExitMaxListed = 5;
//按钮的下标（CloudExitChoiceFor 的 button 参数）；关掉窗口时传 kCloudExitClosed
const int kCloudExitButtonFirst = 0;
const int kCloudExitButtonSecond = 1;
const int kCloudExitButtonThird = 2;
const int kCloudExitClosed = -1;

//对话框在某个场合下的文字（MakeCloudExitDialogSpec 的结果，已取译文）
struct CloudExitDialogSpec
{
    std::wstring m_title;                              //标题栏文字
    std::wstring m_intro;                              //清单上方的说明
    std::wstring m_note;                               //清单下方的说明：继续与放弃分别意味着什么
    std::wstring m_buttons[kCloudExitButtonCount];     //按钮文字，从左到右
    bool         m_hasAbandonCheck;                    //是否有「同时放弃这些录像」勾选项（关机场合）
    std::wstring m_checkText;                          //勾选项的文字；没有勾选项时为空

    CloudExitDialogSpec();
};

/**
 *  某个场合的对话框文字。
 *  @param kind 场合，取值见 flamingomedia::MediaCloudExitKind；不认识的按退出程序处理
 */
CloudExitDialogSpec MakeCloudExitDialogSpec(int kind);

/**
 *  点了哪个按钮（或关掉了窗口）对应的结果。
 *  @param kind           场合，取值见 flamingomedia::MediaCloudExitKind
 *  @param button         按钮下标 kCloudExitButtonFirst～kCloudExitButtonThird；关掉窗口为 kCloudExitClosed
 *  @param abandonChecked 关机场合勾选项是否勾上；其他场合不用
 *  @return 取值见 flamingomedia::MediaCloudExitChoice
 */
int CloudExitChoiceFor(int kind, int button, bool abandonChecked);

/**
 *  这个结果是否要放弃未完成的录像（调用方据此请云端录像中心删除）。
 *  @param choice 取值见 flamingomedia::MediaCloudExitChoice
 */
bool CloudExitChoiceAbandons(int choice);

/**
 *  录像清单的文字：每行一条「· 标题」，没有标题的写「（未命名的录像）」，超过 kCloudExitMaxListed 条时最后一行写「等 N 条」。
 *  @param titles 录像标题
 *  @return 多行文字（行与行之间为 \n）
 */
std::wstring MakeCloudExitListText(const std::vector<std::wstring>& titles);

//「云端录像」对话框
class CCloudExitDialog : public balloonwjui::DuiFrameWindow
{
public:
    CCloudExitDialog();
    ~CCloudExitDialog();

    /**
     *  模态运行：打开对话框，禁用 owner，直到用户点了按钮或关掉窗口。只在 UI 线程调用。
     *  @param owner  模态期间禁用的窗口（exe 主面板），不作为所有者；可为 NULL
     *  @param kind   场合，取值见 flamingomedia::MediaCloudExitKind
     *  @param titles 未完成的录像的标题
     *  @return 取值见 flamingomedia::MediaCloudExitChoice；窗口建不出来时返回 media_cloud_exit_cancel 以外的「继续」结果，
     *          不挡住退出（与以前没有对话框时相同：录像下次登录后接着做）
     */
    int RunModal(HWND owner, int kind, const std::vector<std::wstring>& titles);

    /**
     *  不进模态循环地打开（单元测试用；RunModal 也经它建窗）。居中在主显示器的工作区上。
     *  @return 窗口建好返回 true
     */
    bool Open(int kind, const std::vector<std::wstring>& titles);

    //关闭窗口（不改结果）；可重复调用
    void Close();

    //用户是否已作出选择（点了按钮或关掉了窗口）
    bool IsDecided() const;

    //用户的选择，取值见 flamingomedia::MediaCloudExitChoice；还没作出选择时为 media_cloud_exit_cancel
    int Result() const;

    //对外可见的控件 id（单元测试按它找控件）
    enum
    {
        kIdButtonFirst  = 10,   //第一个按钮（继续退出 / 关机）
        kIdButtonSecond = 11,   //第二个按钮（放弃并退出 / 重启）
        kIdButtonThird  = 12,   //第三个按钮（不退出 / 取消关机）
        kIdAbandonCheck = 13,   //「同时放弃这些录像」勾选项（关机场合）
        kIdList         = 14,   //录像清单
    };

    BEGIN_MSG_MAP(CCloudExitDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //标题栏的关闭按钮或 Alt+F4：等于不退出（关机场合为取消关机）
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //记下用户的选择，结束模态循环（窗口由 RunModal 在循环结束后销毁，不在按钮的点击通知里销毁）
    void decide(int button);

    int                                 m_kind;        //场合，取值见 flamingomedia::MediaCloudExitKind
    int                                 m_result;      //用户的选择
    bool                                m_decided;     //用户是否已作出选择
    balloonwjui::DuiButton*             m_check;       //「同时放弃这些录像」勾选项（控件树持有，本指针仅引用）；没有时为 NULL
    balloonmain::ProgramWindowIcons     m_programIcons;//标题栏与任务栏的程序图标
};

} // namespace screenrecord
