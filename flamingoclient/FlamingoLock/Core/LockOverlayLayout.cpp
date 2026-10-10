/**
 *  LockOverlayLayout.cpp —— 见同名头文件说明。各尺寸取自样稿 tmp/lockscreen/lock_preview.html 的 CSS（逻辑像素）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "LockOverlayLayout.h"

#include <math.h>

namespace flamingolock {

namespace {

// ---- 按钮栏（逻辑像素）----
const float kButtonMinWidth = 128.0f;     // 按钮最小宽度
const float kButtonHeight = 48.0f;        // 按钮高度
const float kButtonGap = 22.0f;           // 相邻按钮的间距
const float kButtonPadX = 22.0f;          // 按钮内容两侧的留白
const float kBarBottomRatio = 0.09f;      // 按钮栏底边距屏幕底边的距离，占屏幕高度的比例

// ---- 密码框（逻辑像素）----
const float kPasswordBoxWidth = 300.0f;   // 输入框宽度
const float kPasswordHeight = 46.0f;      // 输入框与箭头按钮的高度
const float kPasswordGoGap = 10.0f;       // 输入框与箭头按钮的间距
const float kMessageHeight = 18.0f;       // 提示文字行高
const float kMessageGap = 6.0f;           // 输入框与提示文字的间距
const float kRowGap = 14.0f;              // 提示文字与按钮栏的间距

// ---- 确认框（逻辑像素）----
const float kConfirmWidth = 380.0f;       // 确认框宽度
const float kConfirmPadTop = 26.0f;       // 顶部留白
const float kConfirmPadX = 26.0f;         // 左右留白
const float kConfirmPadBottom = 20.0f;    // 底部留白
const float kConfirmTitleHeight = 26.0f;  // 标题行高
const float kConfirmTextGap = 10.0f;      // 标题与说明的间距
const float kConfirmTextHeight = 44.0f;   // 说明文字区高度（两行）
const float kConfirmActionsGap = 22.0f;   // 说明与按钮行的间距
const float kConfirmButtonWidth = 88.0f;  // 确认框按钮宽度
const float kConfirmButtonHeight = 36.0f; // 确认框按钮高度
const float kConfirmButtonGap = 12.0f;    // 两个按钮的间距

// ---- 预览提示（逻辑像素，2026-10-07 设置窗口「预览」用）----
const float kHintHeight = 40.0f;          // 提示胶囊的高度
const float kHintPadX = 24.0f;            // 文字两侧的留白
const float kHintMinMargin = 16.0f;       // 胶囊与屏幕左右边缘的最小距离（文字过长时据此截短）
const float kHintTopRatio = 0.06f;        // 胶囊顶边距屏幕顶边的距离，占屏幕高度的比例

// 按左上角与宽高拼一个矩形（四舍五入到整数像素）
RECT MakeRect(float left, float top, float width, float height)
{
    RECT rc;
    rc.left = static_cast<LONG>(floorf(left + 0.5f));
    rc.top = static_cast<LONG>(floorf(top + 0.5f));
    rc.right = static_cast<LONG>(floorf(left + width + 0.5f));
    rc.bottom = static_cast<LONG>(floorf(top + height + 0.5f));
    return rc;
}

// 点是否在矩形内（左上含、右下不含）
bool Contains(const RECT& rc, POINT pt)
{
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

} // namespace

LockOverlayLayout ComputeOverlayLayout(int width, int height, float scale, const int* contentWidths)
{
    const float s = (scale > 0.0f) ? scale : 1.0f;
    LockOverlayLayout L;
    L.m_scale = s;

    // ---- 按钮栏：三个按钮按各自宽度排成一行，水平居中 ----
    float widths[kOverlayButtonCount];
    float total = 0.0f;
    for (int i = 0; i < kOverlayButtonCount; ++i)
    {
        const float content = (contentWidths != NULL) ? static_cast<float>(contentWidths[i]) : 0.0f;
        const float w = content + kButtonPadX * 2.0f * s;
        widths[i] = (w > kButtonMinWidth * s) ? w : kButtonMinWidth * s;
        total += widths[i];
    }
    total += kButtonGap * s * (kOverlayButtonCount - 1);
    const float barBottom = static_cast<float>(height) * (1.0f - kBarBottomRatio);
    const float barTop = barBottom - kButtonHeight * s;
    float x = (static_cast<float>(width) - total) / 2.0f;
    RECT* buttons[kOverlayButtonCount] = { &L.m_unlockButton, &L.m_restartButton, &L.m_shutdownButton };
    for (int i = 0; i < kOverlayButtonCount; ++i)
    {
        *buttons[i] = MakeRect(x, barTop, widths[i], kButtonHeight * s);
        x += widths[i] + kButtonGap * s;
    }

    // ---- 密码框：提示文字在按钮栏上方，输入框与箭头再往上 ----
    const float msgTop = barTop - kRowGap * s - kMessageHeight * s;
    const float rowTop = msgTop - kMessageGap * s - kPasswordHeight * s;
    const float rowWidth = (kPasswordBoxWidth + kPasswordGoGap + kPasswordHeight) * s;
    const float rowLeft = (static_cast<float>(width) - rowWidth) / 2.0f;
    L.m_passwordBox = MakeRect(rowLeft, rowTop, kPasswordBoxWidth * s, kPasswordHeight * s);
    L.m_passwordGo = MakeRect(rowLeft + (kPasswordBoxWidth + kPasswordGoGap) * s, rowTop,
                              kPasswordHeight * s, kPasswordHeight * s);
    L.m_passwordMessage = MakeRect(rowLeft, msgTop, rowWidth, kMessageHeight * s);

    // ---- 确认框：屏幕正中 ----
    const float panelH = (kConfirmPadTop + kConfirmTitleHeight + kConfirmTextGap + kConfirmTextHeight +
                          kConfirmActionsGap + kConfirmButtonHeight + kConfirmPadBottom) * s;
    const float panelW = kConfirmWidth * s;
    const float panelLeft = (static_cast<float>(width) - panelW) / 2.0f;
    const float panelTop = (static_cast<float>(height) - panelH) / 2.0f;
    L.m_confirmPanel = MakeRect(panelLeft, panelTop, panelW, panelH);
    const float innerLeft = panelLeft + kConfirmPadX * s;
    const float innerWidth = panelW - kConfirmPadX * 2.0f * s;
    const float titleTop = panelTop + kConfirmPadTop * s;
    L.m_confirmTitle = MakeRect(innerLeft, titleTop, innerWidth, kConfirmTitleHeight * s);
    const float textTop = titleTop + (kConfirmTitleHeight + kConfirmTextGap) * s;
    L.m_confirmText = MakeRect(innerLeft, textTop, innerWidth, kConfirmTextHeight * s);
    const float actionsTop = panelTop + panelH - (kConfirmPadBottom + kConfirmButtonHeight) * s;
    const float okLeft = panelLeft + panelW - (kConfirmPadX + kConfirmButtonWidth) * s;
    L.m_confirmOk = MakeRect(okLeft, actionsTop, kConfirmButtonWidth * s, kConfirmButtonHeight * s);
    L.m_confirmCancel = MakeRect(okLeft - (kConfirmButtonGap + kConfirmButtonWidth) * s, actionsTop,
                                 kConfirmButtonWidth * s, kConfirmButtonHeight * s);
    return L;
}

LockOverlayHit HitTestOverlay(const LockOverlayLayout& layout, LockUiPhase phase, POINT pt)
{
    switch (phase)
    {
    //确认框：只测确认框里的两个按钮，框内其它地方吸收点击
    case kUiConfirmShutdown:
    case kUiConfirmRestart:
        if (Contains(layout.m_confirmCancel, pt))
        {
            return kHitConfirmCancel;
        }
        if (Contains(layout.m_confirmOk, pt))
        {
            return kHitConfirmOk;
        }
        if (Contains(layout.m_confirmPanel, pt))
        {
            return kHitConfirmPanel;
        }
        return kHitNothing;

    //密码阶段：先测密码框与箭头，再测按钮栏
    case kUiPassword:
        if (Contains(layout.m_passwordGo, pt))
        {
            return kHitPasswordGo;
        }
        if (Contains(layout.m_passwordBox, pt))
        {
            return kHitPasswordBox;
        }
        break;

    //按钮阶段：只测按钮栏（在 switch 之后统一测）
    case kUiButtons:
        break;

    //只有动画：没有可点的部件
    default:
        return kHitNothing;
    }

    if (Contains(layout.m_unlockButton, pt))
    {
        return kHitUnlock;
    }
    if (Contains(layout.m_restartButton, pt))
    {
        return kHitRestart;
    }
    if (Contains(layout.m_shutdownButton, pt))
    {
        return kHitShutdown;
    }
    return kHitNothing;
}

RECT ComputePreviewHintRect(int width, int height, float scale, int textWidth)
{
    const float s = (scale > 0.0f) ? scale : 1.0f;
    const float maxW = static_cast<float>(width) - kHintMinMargin * 2.0f * s;
    float w = static_cast<float>(textWidth > 0 ? textWidth : 0) + kHintPadX * 2.0f * s;
    if (w > maxW)
    {
        w = (maxW > 0.0f) ? maxW : 0.0f;
    }
    const float h = kHintHeight * s;
    const float left = (static_cast<float>(width) - w) / 2.0f;
    const float top = static_cast<float>(height) * kHintTopRatio;
    return MakeRect(left, top, w, h);
}

} // namespace flamingolock
