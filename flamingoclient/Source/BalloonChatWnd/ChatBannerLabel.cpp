/**
 *  ChatBannerLabel.cpp
 *
 *  聊天窗顶部状态提示带的绘制实现。底色与分隔线用 GDI 直接填充（都是直角矩形，
 *  不需要抗锯齿），文字仍由基类 DuiLabel 用 DrawText 输出。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
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

#include "ChatBannerLabel.h"

namespace balloonchatwnd {

namespace {

// 下边缘分隔线的厚度（逻辑像素）。取 1px —— 它只是把提示带与下方内容切开，
// 再粗就喧宾夺主了。
const int kBorderThickness = 1;

// 用纯色填一块矩形。rc 为空（宽或高 <= 0）时什么都不做。
void FillRectSolid(HDC hdc, const RECT& rc, COLORREF color)
{
    if (rc.right <= rc.left || rc.bottom <= rc.top)
    {
        return;
    }
    HBRUSH hBrush = ::CreateSolidBrush(color);
    if (hBrush == NULL)
    {
        return;
    }
    RECT r = rc;
    ::FillRect(hdc, &r, hBrush);
    ::DeleteObject(hBrush);
}

} // anonymous namespace

ChatBannerLabel::ChatBannerLabel()
    : m_bgColor((COLORREF)CLR_INVALID)
    , m_borderColor((COLORREF)CLR_INVALID)
{
    // 提示带的文字默认居中单行，放不下时打省略号 —— 窗口被拖得很窄时宁可截断，
    // 也不让文字溢出到提示带外面去。
    SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void ChatBannerLabel::SetBannerColors(COLORREF bgColor, COLORREF borderColor)
{
    if (m_bgColor == bgColor && m_borderColor == borderColor)
    {
        return;
    }
    m_bgColor     = bgColor;
    m_borderColor = borderColor;
    Invalidate();
}

void ChatBannerLabel::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 不可见时直接退出：基类同样会跳过，这里提前返回省掉两次填充。
    if (!IsVisible())
    {
        return;
    }

    const RECT rc = m_rcItem;

    // 底色铺满整条。
    if (m_bgColor != (COLORREF)CLR_INVALID)
    {
        FillRectSolid(hdc, rc, m_bgColor);
    }

    // 下边缘分隔线。画在底色之上、文字之下，压住底色的最后 1px。
    if (m_borderColor != (COLORREF)CLR_INVALID)
    {
        RECT rcBorder = rc;
        rcBorder.top = rcBorder.bottom - kBorderThickness;
        FillRectSolid(hdc, rcBorder, m_borderColor);
    }

    // 文字仍交给基类 —— 字体、颜色、对齐、省略号都是它的事。基类以透明背景绘制，
    // 不会把上面铺好的底色盖掉。
    balloonwjui::DuiLabel::OnPaint(hdc, rcDirty);
}

} // namespace balloonchatwnd
