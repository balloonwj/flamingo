/**
 *  flamingoAdmin —— 内容区屏宿主 ScreenHost 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Shell/ScreenHost.h"

#include "DuiHost.h"

namespace flamingoadmin {

void ScreenHost::AddScreen(std::unique_ptr<balloonwjui::DuiControl> screen)
{
    balloonwjui::DuiControl* raw = screen.get();
    raw->SetVisible(false);          // 加入即隐藏，由 ShowScreen 决定显示哪个
    m_screens.push_back(raw);
    AddChild(std::move(screen));

    // 若本宿主已挂到 host，立即让新屏也挂上 host（否则其 Invalidate 无效）。
    if (balloonwjui::DuiHost* host = GetHost())
    {
        raw->AttachToHost(host);
    }
    // 若本宿主已有尺寸，立即为新屏排布，确保它一显示就已就位。
    if (m_rcItem.right > m_rcItem.left)
    {
        raw->ForceLayout(m_rcItem);
    }
}

void ScreenHost::ShowScreen(balloonwjui::DuiControl* screen)
{
    for (balloonwjui::DuiControl* s : m_screens)
    {
        s->SetVisible(s == screen);  // 目标屏可见，其余隐藏（不销毁）
    }
    m_current = screen;

    // 隐藏期间本屏不参与 Layout（见 Layout 注释），其内部矩形可能停留在
    // 上一次可见时的尺寸上。显示之前按当前内容区尺寸补排一次，保证切过来
    // 就是对的。屏切换由用户点击触发、频率极低，这一次布局的开销无关紧要。
    if (m_current != nullptr && m_rcItem.right > m_rcItem.left)
    {
        m_current->ForceLayout(m_rcItem);
    }
    Invalidate();
}

void ScreenHost::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 只排布当前显示的那个屏。
    //
    // 早先的实现是把全部 6 个屏（含隐藏的 5 个）都排一遍，理由是「确保切换
    // 显示时已就位」。但拖动窗口改变尺寸时 Layout 每帧都会跑，等于每帧多做
    // 5 份看不见的布局 —— 实测这是 WM_SIZE 耗时的主要来源。改为只排当前屏，
    // 隐藏屏的补排移到 ShowScreen（切换那一刻才排，见上）。
    if (m_current != nullptr)
    {
        m_current->ForceLayout(rcAvail);
    }
}

void ScreenHost::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 只绘制当前屏（其余屏隐藏，OnPaint 自身也会因 !m_bVisible 提前返回）。
    if (m_current != nullptr)
    {
        m_current->OnPaint(hdc, rcDirty);
    }
}

balloonwjui::DuiControl* ScreenHost::HitTest(POINT ptHostClient)
{
    // 命中测试只下发给当前屏：隐藏的屏不应响应鼠标。
    if (m_current != nullptr)
    {
        return m_current->HitTest(ptHostClient);
    }
    return nullptr;
}

} // namespace flamingoadmin
