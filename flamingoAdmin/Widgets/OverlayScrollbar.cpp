/**
 *  flamingoAdmin —— 悬浮式 auto-hide 滚动条控件 OverlayScrollbar 实现。
 *
 *  几何复用 Util/ScrollbarMetrics（纯函数）；淡入淡出经子类化 DuiAnim 实现
 *  （不使用 lambda，遵开发约定文档代码规范 #10）。详见 OverlayScrollbar.h。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Widgets/OverlayScrollbar.h"
#include "Util/ScrollbarMetrics.h"
#include "Widgets/UiHelpers.h"   // ui::FillTranslucent
#include "Theme/AdminTheme.h"
#include "DuiAnimation.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 外观 ----
const int kSbMinThumb = 24;    // 滑块最小高度（像素）
const int kSbMaxAlpha = 200;   // 滑块完全显示时的不透明度（0-255）

// ---- 淡入 / 驻留 / 淡出时间线（毫秒）----
const int kFadeInMs  = 200;
const int kHoldMs    = 800;
const int kFadeOutMs = 300;
const int kAnimMs    = kFadeInMs + kHoldMs + kFadeOutMs;
const double kFadeInFrac  = static_cast<double>(kFadeInMs) / kAnimMs;
const double kHoldEndFrac =
    static_cast<double>(kFadeInMs + kHoldMs) / kAnimMs;

// 滚动条「淡入→驻留→淡出」一次性动画。每次 Flash() 新建一个并令宿主代次
// +1，旧动画在 OnTick 里因代次过期变为空操作（DuiAnimMgr 无按句柄取消接口，
// 故以代次作废）。OverlayScrollbar 生命周期长于动画，持其裸指针安全。
// 不使用 lambda：子类化 DuiAnim、重写 OnTick 承载回调。
class FadeAnim : public DuiAnim
{
public:
    // owner：所属滚动条；gen：本动画代次；fromAlpha：起始不透明度。
    FadeAnim(OverlayScrollbar* owner, int gen, float fromAlpha)
        : DuiAnim(kAnimMs)
        , m_owner(owner)
        , m_gen(gen)
        , m_fromAlpha(fromAlpha)
    {
    }

protected:
    // t 为 [0,1] 线性进度（未设 easing），交 FadeAlphaAt 分段映射到 alpha。
    void OnTick(double t) override
    {
        float a = FadeAlphaAt(t, m_fromAlpha, kFadeInFrac, kHoldEndFrac);
        m_owner->OnFadeTick(m_gen, a);
    }

private:
    OverlayScrollbar* m_owner;      // 所属滚动条
    int               m_gen;        // 本动画代次
    float             m_fromAlpha;  // 起始不透明度
};

} // anonymous namespace

OverlayScrollbar::OverlayScrollbar()
{
}

void OverlayScrollbar::SetMetrics(int contentH, int scroll)
{
    m_contentH = contentH;
    m_scroll = scroll;
}

void OverlayScrollbar::SetOnScroll(OverlayScrollbarScrollFn fn, void* ctx)
{
    m_onScroll = fn;
    m_onScrollCtx = ctx;
}

void OverlayScrollbar::Flash()
{
    if (!ScrollbarNeeded(m_contentH, ViewH()))
    {
        return;   // 内容放得下，无需显示滚动条
    }
    // 代次 +1 令此前动画作废，再挂一个新的「淡入→驻留→淡出」动画。
    ++m_animGen;
    DuiAnimMgr::Inst().Add(std::unique_ptr<DuiAnim>(
        new FadeAnim(this, m_animGen, m_alpha)));
}

void OverlayScrollbar::OnFadeTick(int gen, float alpha)
{
    if (gen != m_animGen)
    {
        return;   // 已被新的 Flash 取代，本动画作废
    }
    if (alpha < 0.0f)
    {
        alpha = 0.0f;
    }
    if (alpha > 1.0f)
    {
        alpha = 1.0f;
    }
    m_alpha = alpha;
    Invalidate();   // 只刷新本控件那条细矩形
}

int OverlayScrollbar::ViewH() const
{
    return m_rcItem.bottom - m_rcItem.top;
}

RECT OverlayScrollbar::ThumbRectNow() const
{
    return ThumbRect(m_rcItem, m_contentH, ViewH(), m_scroll, kSbMinThumb);
}

balloonwjui::DuiControl* OverlayScrollbar::HitTest(POINT pt)
{
    // 仅当滚动条可见、且点落在滑块上时命中本控件；其余位置「穿透」给宿主，
    // 使滚轮 / 点击列表内容不被这条细控件拦截。
    if (!m_bVisible || m_alpha <= 0.0f)
    {
        return nullptr;
    }
    if (!ScrollbarNeeded(m_contentH, ViewH()))
    {
        return nullptr;
    }
    RECT thumb = ThumbRectNow();
    if (thumb.bottom <= thumb.top || !::PtInRect(&thumb, pt))
    {
        return nullptr;
    }
    return this;
}

void OverlayScrollbar::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!m_bVisible || m_alpha <= 0.0f)
    {
        return;
    }
    if (!ScrollbarNeeded(m_contentH, ViewH()))
    {
        return;
    }
    RECT thumb = ThumbRectNow();
    if (thumb.bottom <= thumb.top)
    {
        return;
    }
    int a = static_cast<int>(m_alpha * kSbMaxAlpha);
    ui::FillTranslucent(hdc, thumb, theme::kInk400, a);
}

bool OverlayScrollbar::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // HitTest 已确保点落在滑块上，这里直接起拖。
    RECT thumb = ThumbRectNow();
    m_dragging = true;
    m_dragOffsetY = pt.y - thumb.top;
    Capture();
    Flash();
    return true;
}

bool OverlayScrollbar::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    if (m_dragging)
    {
        // 拖动中：由滑块期望顶部反算滚动偏移，回调宿主更新并保持可见。
        RECT thumb = ThumbRectNow();
        int thumbH = thumb.bottom - thumb.top;
        int wantTop = pt.y - m_dragOffsetY;
        int newScroll = ScrollOffsetFromThumbTop(m_rcItem, thumbH, wantTop,
                                                 m_contentH, ViewH());
        m_scroll = newScroll;
        if (m_onScroll != nullptr)
        {
            m_onScroll(m_onScrollCtx, newScroll);
        }
        Flash();
        Invalidate();
        return true;
    }
    // 悬停在滑块上 → 保持可见。
    Flash();
    return true;
}

bool OverlayScrollbar::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    if (m_dragging)
    {
        m_dragging = false;
        ReleaseCapture();
        Flash();
        return true;
    }
    return true;
}

} // namespace flamingoadmin
