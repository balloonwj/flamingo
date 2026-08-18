/**
 *  GroupMemberList.cpp
 *
 *  成员列表容器的实现。固定行高堆叠子控件；绘制时把子控件裁剪到本控件
 *  矩形内（DuiControl::OnPaint 不会按父矩形裁剪子控件）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
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

#include "GroupMemberList.h"

namespace balloonchatwnd {

GroupMemberList::GroupMemberList()
{
}

void GroupMemberList::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 子控件按固定行高自上而下堆叠。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    int y = rcAvail.top;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        RECT r = { rcAvail.left, y, rcAvail.right, y + kGroupMemberRowHeight };
        kids[i]->SetRect(r);
        y += kGroupMemberRowHeight;
    }
}

void GroupMemberList::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 绘制各成员行 —— 裁剪到本控件矩形内（DuiControl::OnPaint 不会按父
    // 矩形裁剪子控件；成员过多时多出的行须被裁掉，不画到边栏外）。
    const int savedDC = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, m_rcItem.left, m_rcItem.top,
                        m_rcItem.right, m_rcItem.bottom);
    DuiControl::OnPaint(hdc, rcDirty);
    ::RestoreDC(hdc, savedDC);
}

} // namespace balloonchatwnd
