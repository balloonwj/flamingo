/**
 *  flamingoAdmin —— RootContainer 根容器实现。
 *
 *  历史的 ui::Toast 类已删除, admin 改用 balloonwjui::DuiToast。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Widgets/Toast.h"
#include "Widgets/UiHelpers.h"
#include "Theme/AdminTheme.h"

namespace flamingoadmin { namespace ui {

// ===========================================================================
// RootContainer
// ===========================================================================
void RootContainer::SetToast(std::unique_ptr<balloonwjui::DuiToast> toast)
{
    m_toast = toast.get();
    AddChild(std::move(toast));
}

void RootContainer::AddPage(std::unique_ptr<balloonwjui::DuiControl> page)
{
    balloonwjui::DuiControl* raw = page.get();
    raw->SetVisible(false);          // 加入即隐藏，由 ShowPage 决定显示哪个
    m_pages.push_back(raw);
    AddChild(std::move(page));

    // 若本容器已挂到 host，立即让新页面也挂上 host（否则其 Invalidate 无效）。
    if (balloonwjui::DuiHost* host = GetHost())
    {
        raw->AttachToHost(host);
    }
    // 若本容器已有尺寸，立即为新页面排布，确保它一显示就已就位。
    if (m_rcItem.right > m_rcItem.left)
    {
        raw->ForceLayout(m_rcItem);
    }
}

void RootContainer::ShowPage(balloonwjui::DuiControl* page)
{
    for (balloonwjui::DuiControl* p : m_pages)
    {
        p->SetVisible(p == page);    // 目标页可见，其余隐藏
    }
    m_current = page;
    Invalidate();
}

void RootContainer::AddModal(std::unique_ptr<balloonwjui::DuiControl> modal)
{
    balloonwjui::DuiControl* raw = modal.get();
    raw->SetVisible(false);          // 加入即隐藏，由 ShowModal 显示
    m_modals.push_back(raw);
    AddChild(std::move(modal));

    // 若本容器已挂到 host，立即让弹窗也挂上 host。
    if (balloonwjui::DuiHost* host = GetHost())
    {
        raw->AttachToHost(host);
    }
    // 若本容器已有尺寸，立即为弹窗排布。
    if (m_rcItem.right > m_rcItem.left)
    {
        raw->ForceLayout(m_rcItem);
    }
}

void RootContainer::ShowModal(balloonwjui::DuiControl* modal)
{
    for (balloonwjui::DuiControl* m : m_modals)
    {
        m->SetVisible(m == modal);   // 目标弹窗可见，其余隐藏
    }
    m_activeModal = modal;
    Invalidate();
}

void RootContainer::HideModal()
{
    for (balloonwjui::DuiControl* m : m_modals)
    {
        m->SetVisible(false);
    }
    m_activeModal = nullptr;
    Invalidate();
}

bool RootContainer::IsModalVisible() const
{
    return m_activeModal != nullptr && m_activeModal->IsVisible();
}

void RootContainer::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;
    // 所有页面（含隐藏的）都铺满客户区排布，确保切换显示时已就位。
    for (balloonwjui::DuiControl* p : m_pages)
    {
        p->ForceLayout(rcAvail);
    }
    // 弹窗层同样铺满客户区（其内部再把表单卡片居中）。
    for (balloonwjui::DuiControl* m : m_modals)
    {
        m->ForceLayout(rcAvail);
    }
    // Toast 自行定位:balloonwjui::DuiToast::Layout 忽略 rcAvail, 用
    // GetParent()->GetRect() 算"顶居中 + topOffset", 此处无需主动触发。
}

void RootContainer::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 自底向上：当前页面 → 弹窗 → Toast，保证 Toast 永远浮在最上层。
    if (m_current != nullptr)
    {
        m_current->OnPaint(hdc, rcDirty);
    }
    if (m_activeModal != nullptr && m_activeModal->IsVisible())
    {
        m_activeModal->OnPaint(hdc, rcDirty);
    }
    if (m_toast != nullptr)
    {
        m_toast->OnPaint(hdc, rcDirty);
    }
}

balloonwjui::DuiControl* RootContainer::HitTest(POINT ptHostClient)
{
    // 弹窗可见时，命中测试只下发给弹窗（弹窗本身的遮罩会兜住所有点击）。
    if (m_activeModal != nullptr && m_activeModal->IsVisible())
    {
        return m_activeModal->HitTest(ptHostClient);
    }
    // 否则只下发给当前页面：Toast 非交互，隐藏的页面也不应响应。
    if (m_current != nullptr)
    {
        return m_current->HitTest(ptHostClient);
    }
    return nullptr;
}

}} // namespace flamingoadmin::ui
