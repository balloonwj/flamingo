/**
 *  群通话的来电窗，GroupCallIncomingDlg.h
 *
 *  被邀请加入群通话时在屏幕右下角弹出（docs/群音视频通话方案.md 6.1）：左边群头像，右边第一行「某某 邀请你加入群语音通话」，
 *  第二行「群名 · N 人正在通话」，底部「拒绝」「接听」，标题栏后接振铃倒计时（与一对一来电相同的写法）。尺寸与版式
 *  与一对一来电窗相同（420×200，见 Common/IncomingCallLayout.h，2026-10-05 起）。置顶、不抢焦点、
 *  不进任务栏。点按钮不直接发信令，而是向通知窗口（群通话控制器）投 kMsgGroupCallIncomingCommand，由控制器决定。
 *  倒计时走完后等服务端的振铃超时推送收起；推送迟迟不来时，再过 kIncomingGraceMs 按拒绝处理。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "../../Source/UI/BalloonMain/CornerPopupCountdown.h"   // CornerCountdown（标题后的倒计时）
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"           // ProgramWindowIcons（标题栏程序图标）

#include <stdint.h>
#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../Common/MediaAvatar.h"   // CMediaAvatarSource：左侧的群头像
#include "../Call/CallButtonArt.h"   // CallButtonArtCache：「拒绝」「接听」的圆形图标按钮

namespace balloonwjui { class DuiLabel; class DuiAvatar; }

namespace groupcall {

//倒计时走完之后再等服务端振铃超时推送的时长，单位毫秒；过了仍没收到就按拒绝处理
const int64_t kIncomingGraceMs = 10 * 1000;

//群通话的来电窗
class CGroupCallIncomingDlg : public balloonwjui::DuiFrameWindow
{
public:
    CGroupCallIncomingDlg();
    ~CGroupCallIncomingDlg();

    /**
     *  登记宿主：左侧的群头像经它取得。群通话控制器在 Init 时调用。
     *  @param host 宿主（借用，不拥有；须比本对象的使用期长）；为 NULL 时头像画问号
     */
    void SetHost(flamingomedia::IMediaHost* host);

    /**
     *  弹出来电窗（已弹出时刷新文字并重新开始倒计时）。
     *  @param groupId   群 id（左侧显示这个群的头像）
     *  @param prompt    第一行：邀请人与通话类型（MakeGroupCallInvitePrompt 的结果）
     *  @param detail    第二行：群名与正在通话的人数（MakeGroupCallInviteDetail 的结果）
     *  @param title     标题栏文字（倒计时接在后面），如「群语音通话」
     *  @param ringMs    剩余振铃毫秒数（1741 的 ringms）；不大于 0 时按 45 秒处理
     *  @param notifyWnd 通知窗口（群通话控制器的消息窗口），不拥有
     */
    void Open(int64_t groupId, const std::wstring& prompt, const std::wstring& detail, const std::wstring& title,
              int64_t ringMs, HWND notifyWnd);

    /**
     *  刷新第二行（房间里有人加入或离开时）。来电窗没有打开时什么都不做。
     */
    void SetDetail(const std::wstring& detail);

    //关闭来电窗，不发任何命令；可重复调用
    void Hide();

    //有群的头像变了：重新取群头像并重画。窗口没开时什么也不做
    void RefreshAvatar();

    //来电窗是否开着
    bool IsShowing() const;

    BEGIN_MSG_MAP(CGroupCallIncomingDlg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    enum
    {
        kIdAccept  = 10,   //「接听」按钮
        kIdDecline = 11,   //「拒绝」按钮
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //关窗并把命令投给通知窗口
    void send(int command);
    //按剩余时间刷新标题
    void refreshTitle();

    HWND                            m_notifyWnd;     //通知窗口（不拥有）
    std::wstring                    m_title;         //标题栏文字（不含倒计时）
    balloonwjui::DuiLabel*          m_detailLabel;   //第二行文字控件（归控件树所有）；窗口没建时为 NULL
    balloonwjui::DuiAvatar*         m_avatarCtrl;    //左侧的群头像控件（归控件树所有）；窗口没建时为 NULL
    int64_t                         m_groupId;       //来电所在的群（取群头像用）；没有来电时为 0
    balloonmain::ProgramWindowIcons m_icons;         //标题栏的程序图标，本对象持有；窗口先于它销毁
    balloonmain::CornerCountdown    m_countdown;     //振铃倒计时，弹出时从头开始
    int64_t                         m_expireAtMs;    //倒计时走完后按拒绝处理的时刻（GetTickCount64 毫秒）
    //群头像位图（归它所有）。声明在控件指针之后，析构时窗口已在析构函数里销毁，借用位图的控件都已不存在
    flamingomedia::CMediaAvatarSource m_avatars;
    //「拒绝」「接听」两个圆形图标按钮的位图（归它所有，2026-10-05 起）。同上，析构时借用位图的按钮都已不存在
    call::CallButtonArtCache          m_buttonArt;
};

} // namespace groupcall
