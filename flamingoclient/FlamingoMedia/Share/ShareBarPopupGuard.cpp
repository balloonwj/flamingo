/**
 *  浮动工具条弹出菜单期间的关闭与推迟销毁，说明见 ShareBarPopupGuard.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareBarPopupGuard.h"

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"

namespace screenshare {

bool ShareBarShouldDeferDestroy(bool popupActive)
{
    return popupActive;
}

bool ShareBarShouldDeliverChoice(UINT chosen, bool barHidden, bool notifyWndValid)
{
    return chosen != 0 && !barHidden && notifyWndValid;
}

CShareBarPopupGuard::CShareBarPopupGuard()
    : m_root(NULL)
    , m_dialogActive(false)
    , m_dialog(NULL)
    , m_pendingDestroy(false)
    , m_interrupted(false)
{
}

void CShareBarPopupGuard::BeginMenu(balloonwjui::DuiMenu* root,
                                    const std::vector<std::unique_ptr<balloonwjui::DuiMenu> >& subMenus)
{
    m_interrupted = false;
    m_root = root;
    m_subMenus.clear();
    for (size_t i = 0; i < subMenus.size(); ++i)
        m_subMenus.push_back(subMenus[i].get());
}

void CShareBarPopupGuard::BeginDialog()
{
    m_interrupted = false;
    m_dialogActive = true;
    m_dialog = NULL;
}

void CShareBarPopupGuard::SetDialogWindow(HWND dialog)
{
    if (m_dialogActive)
        m_dialog = dialog;
}

void CShareBarPopupGuard::End()
{
    m_root = NULL;
    m_subMenus.clear();
    m_dialogActive = false;
    m_dialog = NULL;
}

bool CShareBarPopupGuard::IsActive() const
{
    return m_root != NULL || m_dialogActive;
}

void CShareBarPopupGuard::Close()
{
    //先关子菜单再关根菜单：DuiMenu::HideNow 只关它自己的弹出窗口。根菜单先关的话 TrackPopup 立即返回，子菜单对象随
    //调用方的局部变量释放，之后才处理的子菜单 WM_CLOSE 会访问已释放的 DuiMenu（DuiMenuPopup::OnFinalMessage）。
    //投递顺序即处理顺序，子菜单的 WM_CLOSE 排在根菜单之前
    for (size_t i = m_subMenus.size(); i > 0; --i)
    {
        if (m_subMenus[i - 1] != NULL)
            m_subMenus[i - 1]->HideNow();
    }
    if (m_root != NULL)
        m_root->HideNow();
    //取色框：相当于点了「取消」，ChooseColor 随之返回 FALSE
    if (m_dialogActive && m_dialog != NULL && ::IsWindow(m_dialog))
        ::PostMessage(m_dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
}

void CShareBarPopupGuard::SetPendingDestroy()
{
    m_pendingDestroy = true;
    m_interrupted = true;
}

bool CShareBarPopupGuard::WasInterrupted() const
{
    return m_interrupted;
}

bool CShareBarPopupGuard::IsPendingDestroy() const
{
    return m_pendingDestroy;
}

bool CShareBarPopupGuard::TakePendingDestroy()
{
    const bool pending = m_pendingDestroy;
    m_pendingDestroy = false;
    return pending;
}

} // namespace screenshare
