/**
 *  屏幕共享结束的右下角提示，ShareEndedToast.h
 *
 *  2026-10-05 用户要求共享结束时不再弹 MessageBox，改为右下角弹窗，显示时长、分享人与参与者。内容自上而下：
 *  结束说明（「屏幕共享已结束」，对方掉线时为「对方网络中断，屏幕共享已结束」，粗体）、共享标题、时长、分享人、参与者，
 *  底部一个「知道了」。分享人与参与者较长时自动换行，窗口按行数加高。不自动关闭：只在点「知道了」或标题栏关闭时关闭。
 *  已弹出时再次 Show 替换内容。
 *
 *  形态与 CShareCornerToast 一致：balloonui DuiFrameWindow 的小窗，置顶、不抢焦点、不进任务栏，贴在工作区右下角。
 *  文字由 FormatShareEndedTexts（ShareEndedSummary.h）给出。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons（标题栏程序图标）

#include <string>
#include "ShareEndedSummary.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace screenshare {

//屏幕共享结束的右下角提示
class CShareEndedToast : public balloonwjui::DuiFrameWindow
{
public:
    //各行文字与按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3；单元测试按它找控件）
    enum
    {
        kIdHeadline     = 10,   //结束说明
        kIdShareTitle   = 11,   //共享标题
        kIdDuration     = 12,   //时长
        kIdSharers      = 13,   //分享人
        kIdParticipants = 14,   //参与者
        kIdOk           = 15,   //「知道了」
    };

    CShareEndedToast();
    ~CShareEndedToast();

    /**
     *  弹出提示（已弹出时替换内容并重新贴角）。
     *  @param headline   结束说明（已翻译）
     *  @param shareTitle 共享标题；为空时不显示这一行
     *  @param texts      时长、分享人、参与者三行（FormatShareEndedTexts 给出）
     */
    void Show(const std::wstring& headline, const std::wstring& shareTitle, const ShareEndedTexts& texts);

    //关闭；可重复调用
    void Hide();

    BEGIN_MSG_MAP(CShareEndedToast)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    balloonmain::ProgramWindowIcons m_icons;   //标题栏的程序图标，本对象持有；窗口先于它销毁
};

} // namespace screenshare
