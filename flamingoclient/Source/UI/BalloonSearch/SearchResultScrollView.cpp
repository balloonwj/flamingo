/**
 *  SearchResultScrollView.cpp
 *
 *  主面板搜索下拉结果滚动区的实现：直接使用基类 DuiScrollView 的滚动条（2026-10-04 起与聊天窗同一外观）。
 *  整体说明见 SearchResultScrollView.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 MainPanelScrollView.cpp 相同）----
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

#include "SearchResultScrollView.h"

namespace balloonsearch {

SearchResultScrollView::SearchResultScrollView()
{
    // 滚动条用基类的默认设置：悬浮式细滑块、默认命中带宽、自动隐藏
}

RECT SearchResultScrollView::ThumbRect() const
{
    RECT none = { 0, 0, 0, 0 };
    balloonwjui::DuiScrollBar* bar = const_cast<SearchResultScrollView*>(this)->GetScrollBar();
    if (bar == NULL || !bar->IsVisible())
    {
        return none;
    }
    return bar->ComputePaintThumbRect();
}

bool SearchResultScrollView::IsDraggingThumb() const
{
    balloonwjui::DuiScrollBar* bar = const_cast<SearchResultScrollView*>(this)->GetScrollBar();
    return bar != NULL && bar->IsDragging();
}

} // namespace balloonsearch
