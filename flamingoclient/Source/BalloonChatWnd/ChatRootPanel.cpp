/**
 *  ChatRootPanel.cpp
 *
 *  单聊窗根面板的绘制实现 —— 用 GDI FillRect 铺纯白底（边缘锐利），
 *  再调基类绘制子控件。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
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

#include "ChatRootPanel.h"

namespace balloonchatwnd {

void ChatRootPanel::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 纯白底（GDI FillRect —— 轴对齐矩形边缘锐利）。
    HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(hdc, &m_rcItem, white);
    ::DeleteObject(white);

    // 基类（DuiVBox → DuiControl）默认实现：绘制各子控件。
    DuiVBox::OnPaint(hdc, rcDirty);
}

} // namespace balloonchatwnd
