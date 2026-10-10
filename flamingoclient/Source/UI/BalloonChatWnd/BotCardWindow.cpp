/**
 *  BotCardWindow.cpp
 *
 *  机器人资料卡小窗，说明见 BotCardWindow.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotCardWindow.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace balloonchatwnd {

CBotCardWindow::CBotCardWindow()
{
}

CBotCardWindow::~CBotCardWindow()
{
    CloseCard();
}

bool CBotCardWindow::Open(HWND owner, const BotCardView& view)
{
    const LPCTSTR title = TR(L"机器人资料");
    bool created = false;
    if (!IsWindow())
    {
        //以宿主为 owner：本窗口恒在它之上。WS_OVERLAPPEDWINDOW 的顶层窗口即便带 owner，::GetParent 也返回 NULL，
        //控件通知回到本窗口（开发约定文档 UI 约定 11）
        if (Create(owner, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("bot card: Create failed.");
            return false;
        }
        SetTitleBarHeight(kBotCardTitleBarH);
        SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        SetButtons(false, false, true);
        SetResizable(false);
        SetMinSize(kBotCardWindowWidth, kBotCardMinWindowHeight);
        created = true;
    }
    SetTitle(title);
    SetWindowText(title);

    //换上新内容后按窗口的实际字体量各段高度，再定窗口高度；高度没变时 ResizeClient 不会触发排版，所以最后逐层重排
    SetClientContent(BuildBotCardBody(view));
    const int bodyHeight = FitBotCardBody(GetClientContent(), kBotCardWindowWidth);
    const int windowHeight = BotCardWindowHeight(bodyHeight);
    ResizeClient(kBotCardWindowWidth, windowHeight);
    RelayoutBotCardBody(GetClientContent());
    if (created)
    {
        CenterWindow(owner);
    }
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("bot card: opened, body height=%d, window height=%d.", bodyHeight, windowHeight);
    return true;
}

void CBotCardWindow::CloseCard()
{
    if (IsWindow())
    {
        DestroyWindow();
    }
}

LRESULT CBotCardWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    CloseCard();
    return 0;
}

LRESULT CBotCardWindow::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //Esc 关闭资料卡；其余按键交给基类
    if (wParam == VK_ESCAPE)
    {
        PostMessage(WM_CLOSE);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBotCardWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    //「关闭」：投一条 WM_CLOSE 给自己，等这条点击通知处理完再销毁窗口（开发约定文档 UI 约定第 13 条）
    if (pNotify != NULL && pNotify->code == DUIN_CLICK && pNotify->ctrlId == kBotCardIdClose)
    {
        PostMessage(WM_CLOSE);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace balloonchatwnd
