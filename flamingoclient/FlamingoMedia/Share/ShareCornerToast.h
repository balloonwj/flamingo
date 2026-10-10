/**
 *  屏幕共享的右下角提示，ShareCornerToast.h
 *
 *  单聊里对方开始共享屏幕时，在屏幕右下角弹出一个可点击的提示（docs/屏幕共享与录制方案.md 第 2 节第 12 条、
 *  5.2 第 6 步）：「XX 正在共享屏幕」+「忽略」「加入」。不振铃。点按钮不直接操作房间，而是向通知窗口（屏幕共享控制器）
 *  投 kMsgShareToastCommand，由控制器决定加入或忽略；共享结束、或一分钟内没有操作，提示自动关闭。
 *
 *  形态与远程桌面的邀请对话框贴右下角的做法一致：基于 balloonui DuiFrameWindow 的小窗，置顶、不抢焦点、不进任务栏，
 *  落点用 exe 与 DLL 共用的 balloonmain::MoveWindowToWorkAreaCorner。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "../../Source/UI/BalloonMain/CornerPopupCountdown.h"   // CornerCountdown（标题后的倒计时）
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"           // ProgramWindowIcons（标题栏程序图标）

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace screenshare {

//右下角提示的标题、两个按钮的文字与按钮命令投递的消息号（第 18 期起控制申请提示也用这个窗口）
struct ShareToastTexts
{
    std::wstring m_title;     //标题栏文字（倒计时接在它后面）；已翻译好的文字
    std::wstring m_accept;    //主按钮文字（命令 kCmdJoin）
    std::wstring m_ignore;    //次按钮文字（命令 kCmdIgnore；关窗与超时也按它处理）
    UINT         m_message;   //按钮命令投给通知窗口的消息号

    //默认取共享开始提示的文字：「屏幕共享」「加入」「忽略」，消息为 kMsgShareToastCommand
    ShareToastTexts();
};

/**
 *  右下角提示上按钮的宽度：按钮文字在 balloonui 默认字体下的实际宽度加左右留白，不小于设计宽度。
 *  默认字体按系统 DPI 换算字高，所以缩放比例大、译文长时按钮随之加宽，文字不会被截掉（2026-10-05 联调发现
 *  125% 缩放下「发送到本会话」被截去第一个字）。
 *  @param text 按钮文字（已取译文）
 *  @return 按钮宽度，单位像素
 */
int ShareToastButtonWidth(const std::wstring& text);

/**
 *  右下角提示的整窗宽度：两个按钮加内边距与间距放得下时取设计宽度，放不下时加宽到刚好放下。
 *  @param ignoreWidth 次按钮宽度（像素，见 ShareToastButtonWidth）
 *  @param acceptWidth 主按钮宽度（像素）
 *  @return 整窗宽度，单位像素
 */
int ShareToastWindowWidth(int ignoreWidth, int acceptWidth);

//屏幕共享的右下角提示
class CShareCornerToast : public balloonwjui::DuiFrameWindow
{
public:
    //按钮命令（kMsgShareToastCommand 的 wParam）
    enum Command
    {
        kCmdJoin   = 1,   //加入
        kCmdIgnore = 2,   //忽略（含超时自动关闭）
    };

    CShareCornerToast();
    ~CShareCornerToast();

    /**
     *  弹出提示（已弹出时刷新文字并重新贴角）。
     *  @param text      提示文字
     *  @param token     随命令回传的标识（控制器据此找到对应的房间）
     *  @param notifyWnd 通知窗口；不拥有
     *  @param textLines 文字要占的行数（第 14 期：强制模式的说明较长，按三行加高窗口）；1 表示单行
     */
    void Show(const std::wstring& text, LPARAM token, HWND notifyWnd, int textLines = 1);

    /**
     *  同上，另指定标题、按钮文字与命令消息（第 18 期控制申请：「控制申请」「同意」「拒绝」）。
     *  @param texts 标题、按钮文字与消息号
     */
    void Show(const std::wstring& text, LPARAM token, HWND notifyWnd, int textLines, const ShareToastTexts& texts);

    //关闭提示；可重复调用
    void Hide();

    //随命令回传的标识；提示没有显示时为 0
    LPARAM Token() const;

    //两个按钮的控件 id（单元测试按它找按钮核对宽度）
    enum
    {
        kIdJoin   = 10,   //主按钮（「加入」「同意」「发送到本会话」等）
        kIdIgnore = 11,   //次按钮（「忽略」「拒绝」「关闭」等）
    };

    BEGIN_MSG_MAP(CShareCornerToast)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void send(int command);
    //按剩余时间刷新标题（「屏幕共享 · 58 秒后关闭」，第 15 期）
    void refreshTitle();

    HWND   m_notifyWnd;   //通知窗口（不拥有）
    LPARAM m_token;       //随命令回传的标识
    ShareToastTexts m_texts;   //当前弹出的标题、按钮文字与命令消息号（每次 Show 时设置）
    balloonmain::ProgramWindowIcons m_icons;      //标题栏的程序图标（第 15 期），本对象持有；窗口先于它销毁
    balloonmain::CornerCountdown    m_countdown;  //自动关闭的倒计时（第 15 期），弹出时从头开始
};

} // namespace screenshare
