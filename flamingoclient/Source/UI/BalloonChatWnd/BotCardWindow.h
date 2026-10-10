/**
 *  BotCardWindow.h
 *
 *  群聊窗成员页签里点了某个机器人后弹出的资料卡小窗（docs/机器人方案.md §4.2）。2026-10-08 起由系统消息框改成
 *  应用风格的小窗（用户确认）：标题栏带程序图标，内容为头像、名字、「机器人」徽标与标签、简介、用法、来源，
 *  右下角「关闭」。控件树与尺寸见 BotCardLayout.h。
 *
 *  非模态，不禁用宿主窗口：资料卡只用来查看，开着它照样可以在群聊窗里打字。宿主群聊窗持有唯一一个实例，
 *  已打开时再点另一个机器人会换成新的内容并前置。
 *
 *  典型用法（群聊窗）：
 *      m_botCard.Open(m_hWnd, balloonchatwnd::MakeBotCardView(info, fallbackColor));
 *      //群聊窗销毁前
 *      m_botCard.CloseCard();
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include "BotCardLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏左侧的程序图标

namespace balloonchatwnd {

/**
 *  CBotCardWindow —— 机器人资料卡小窗。宽度固定，高度按内容定（上下限见 BotCardLayout.h），内容超过上限时详情区滚动。
 */
class CBotCardWindow : public balloonwjui::DuiFrameWindow
{
public:
    CBotCardWindow();
    ~CBotCardWindow();

    /**
     *  打开资料卡；已打开时换成新的内容、按新内容调整高度并前置。
     *    owner：宿主窗口（群聊窗），资料卡第一次打开时居中到它，并始终显示在它上面；
     *    view： 要显示的内容，只读。
     *  @return 窗口建出或前置成功返回 true
     */
    bool Open(HWND owner, const BotCardView& view);

    /**
     *  关闭资料卡；没有打开时什么都不做。宿主窗口销毁前调用。
     */
    void CloseCard();

    BEGIN_MSG_MAP(CBotCardWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    balloonmain::ProgramWindowIcons m_programIcons;   // 标题栏左侧的程序图标（本类持有）
};

} // namespace balloonchatwnd
