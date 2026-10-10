/**
 *  MainPanelScrollView.cpp
 *
 *  主面板滚动视口实现。2026-10-04 起直接使用基类 DuiScrollView 的滚动条（balloonui 统一的悬浮式细滑块，
 *  淡入淡出由 DuiAnimMgr 推进），本文件只负责给窗口缩放边框让位与「立即隐藏」。整体说明见 MainPanelScrollView.h。
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

#include "MainPanelScrollView.h"

namespace balloonmain {

MainPanelScrollView::MainPanelScrollView()
    : m_edgeInsetPx(0)
{
    // 不走自动测高（方案 1a）。原因：内容是一个 vbox，里面装 label + 单列
    // DuiTreeView（weight=1）。DuiVBox::GetDesiredSize 只把固定高（fixedMain）
    // 累加进 mainSum；treeview 作为 weight 子贡献 0，而 DuiTreeView 自己又
    // 没覆写 GetDesiredSize（默认返回 {0,0}）。所以 auto 测出来的高度只
    // 等于 label 高 + padding，远小于实际内容总高，scrollview 会误以为
    // "内容未溢出"不滚。改由业务侧（FillContactsTree / FillGroupsTree /
    // FillMessagesTree）在数据填完后调 SetContentHeight 显式同步。
    SetAutoContentHeight(false);
    // 滚动条用基类的默认设置：悬浮式细滑块、默认命中带宽、自动隐藏（2026-10-04 起不再置 0 自绘）
}

void MainPanelScrollView::HideScrollBarNow()
{
    balloonwjui::DuiScrollBar* bar = GetScrollBar();
    if (bar == NULL)
    {
        return;
    }
    // DuiScrollBar 没有单独的「立即隐藏」接口：关掉再打开自动隐藏，会取消进行中的淡入淡出与空闲计时，
    // 并把不透明度直接置 0
    bar->SetAutoHide(false);
    bar->SetAutoHide(true);
}

void MainPanelScrollView::SetEdgeInsetPx(int px)
{
    int v = px;
    if (v < 0)
    {
        v = 0;
    }
    if (v == m_edgeInsetPx)
    {
        return;
    }
    m_edgeInsetPx = v;
    // 矩形没变、变的是滚动条的位置：必须用 ForceLayout 才会重新排版（SetRect 在矩形不变时直接返回）
    ForceLayout(m_rcItem);
    Invalidate();
}

void MainPanelScrollView::Layout(const RECT& rcAvail)
{
    DuiScrollView::Layout(rcAvail);
    balloonwjui::DuiScrollBar* bar = GetScrollBar();
    if (m_edgeInsetPx <= 0 || bar == NULL || !bar->IsVisible())
    {
        return;
    }
    // 基类把命中带贴着视口右缘放；整体左移让出窗口缩放边框，滑块随之左移（命中测试、绘制都按它的矩形）
    RECT rc = bar->GetRect();
    ::OffsetRect(&rc, -m_edgeInsetPx, 0);
    if (rc.left < m_rcItem.left)
    {
        rc.left = m_rcItem.left;
    }
    bar->SetRect(rc);
}

} // namespace balloonmain
