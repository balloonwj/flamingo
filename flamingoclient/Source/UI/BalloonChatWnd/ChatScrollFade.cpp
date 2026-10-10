/**
 *  ChatScrollFade.cpp
 *
 *  聊天窗一族自绘细滚动条的淡入淡出控制器的实现，见 ChatScrollFade.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "ChatScrollFade.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiAnimation.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // 淡入淡出的时长常量

namespace balloonchatwnd {

using balloonwjui::DuiScrollBar;

CChatScrollFade::CChatScrollFade()
    : m_owner(NULL)
    , m_alpha(0.0f)
    , m_hover(false)
    , m_dragging(false)
    , m_token(std::make_shared<Token>())
{
    m_token->m_owner = this;
    m_token->m_alive = true;
}

CChatScrollFade::~CChatScrollFade()
{
    // 动画管理器里可能还排着本对象的回调，它们持有令牌副本；作废令牌，回调触发时就不再访问已销毁的本对象
    if (m_token)
    {
        m_token->m_alive = false;
        m_token->m_owner = NULL;
    }
}

void CChatScrollFade::SetOwner(balloonwjui::DuiControl* owner)
{
    m_owner = owner;
}

void CChatScrollFade::OnScrolled()
{
    Show();
}

void CChatScrollFade::SetHover(bool hover)
{
    if (hover == m_hover)
    {
        return;
    }
    m_hover = hover;
    // 进入：淡入并保持；离开：重新开始空闲计时（与 DuiScrollBar 的 OnMouseEnter / OnMouseLeave 相同）
    Show();
}

void CChatScrollFade::SetDragging(bool dragging)
{
    m_dragging = dragging;
    Show();
}

void CChatScrollFade::OnOwnerMouseLeave()
{
    m_hover = false;
    StartFadeOut();
}

void CChatScrollFade::Show()
{
    CancelAnims();
    StartFade(1.0f, DuiScrollBar::kFadeInMs);
    StartIdle(DuiScrollBar::kIdleHideMs);
}

void CChatScrollFade::StartFadeOut()
{
    // 拖动中保持可见：拖出了宿主控件也不应让滑块消失，松开后 SetDragging(false) 会重新计时
    if (m_dragging)
    {
        return;
    }
    CancelAnims();
    StartFade(0.0f, DuiScrollBar::kFadeOutMs);
}

void CChatScrollFade::CancelAnims()
{
    if (m_token)
    {
        m_token->m_alive = false;
    }
    m_token = std::make_shared<Token>();
    m_token->m_owner = this;
    m_token->m_alive = true;
}

void CChatScrollFade::StartFade(float to, int durationMs)
{
    if (m_alpha == to)
    {
        return;
    }
    FadeSetter setter;
    setter.m_token = m_token;
    std::unique_ptr<balloonwjui::DuiDoubleAnim> anim(
        new balloonwjui::DuiDoubleAnim(durationMs, (double)m_alpha, (double)to, setter));
    anim->SetEasing(&balloonwjui::DuiEase::EaseOutCubic);
    FadeDone done;
    done.m_token = m_token;
    done.m_to = to;
    anim->SetOnComplete(done);
    balloonwjui::DuiAnimMgr::Inst().Add(std::move(anim));
}

void CChatScrollFade::StartIdle(int delayMs)
{
    // 用一段只占时间、不改任何值的动画当空闲计时，与淡入淡出共用令牌，CancelAnims 一次取消两者
    std::unique_ptr<balloonwjui::DuiDoubleAnim> anim(new balloonwjui::DuiDoubleAnim(delayMs, 0.0, 1.0, IdleSetter()));
    IdleDone done;
    done.m_token = m_token;
    done.m_delayMs = delayMs;
    anim->SetOnComplete(done);
    balloonwjui::DuiAnimMgr::Inst().Add(std::move(anim));
}

void CChatScrollFade::InvalidateOwner()
{
    if (m_owner != NULL)
    {
        m_owner->Invalidate();
    }
}

void CChatScrollFade::FadeSetter::operator()(double value) const
{
    if (m_token && m_token->m_alive && m_token->m_owner != NULL)
    {
        m_token->m_owner->m_alpha = (float)value;
        m_token->m_owner->InvalidateOwner();
    }
}

void CChatScrollFade::FadeDone::operator()() const
{
    if (m_token && m_token->m_alive && m_token->m_owner != NULL)
    {
        m_token->m_owner->m_alpha = m_to;
        m_token->m_owner->InvalidateOwner();
    }
}

void CChatScrollFade::IdleSetter::operator()(double /*value*/) const
{
}

void CChatScrollFade::IdleDone::operator()() const
{
    if (!m_token || !m_token->m_alive || m_token->m_owner == NULL)
    {
        return;
    }
    CChatScrollFade* owner = m_token->m_owner;
    // 鼠标还悬停在命中带上、或正在拖动：保持可见，再等一轮；否则开始淡出
    if (owner->m_hover || owner->m_dragging)
    {
        owner->StartIdle(m_delayMs);
    }
    else
    {
        owner->StartFadeOut();
    }
}

} // namespace balloonchatwnd
