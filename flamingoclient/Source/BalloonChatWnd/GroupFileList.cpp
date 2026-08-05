/**
 *  GroupFileList.cpp
 *
 *  文件列表堆叠容器实现。固定行高自上而下堆叠子控件；不裁剪、不滚动 ——
 *  作为 DuiScrollView 的内容控件，裁剪与滚动由 DuiScrollView 视口负责。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-08
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

#include "GroupFileList.h"
#include "GroupFileItem.h"   // kGroupFileRowHeight

namespace balloonchatwnd {

GroupFileList::GroupFileList()
{
}

int GroupFileList::ContentHeight() const
{
    return (int)Children().size() * kGroupFileRowHeight;
}

void GroupFileList::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 子控件（文件行）按固定行高自上而下堆叠，吃满本容器宽度。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    int y = rcAvail.top;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        RECT r = { rcAvail.left, y, rcAvail.right, y + kGroupFileRowHeight };
        kids[i]->SetRect(r);
        y += kGroupFileRowHeight;
    }
}

} // namespace balloonchatwnd
