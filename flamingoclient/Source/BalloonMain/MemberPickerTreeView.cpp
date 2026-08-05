/**
 *  MemberPickerTreeView.cpp
 *
 *  候选树的复选框点击判定实现。
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

#include "MemberPickerTreeView.h"
#include "MemberPickerRow.h"

namespace balloonmain {

MemberPickerTreeView::MemberPickerTreeView()
    : m_toggledNodeId(-1)
{
}

bool MemberPickerTreeView::OnLButtonDown(POINT pt, UINT mkFlags)
{
    // 先问基类点在哪一行上。命不中任何行（点在空白区）就没有复选框可切。
    const int nodeId = HitTestId(pt);
    if (nodeId < 0)
    {
        return MainPanelTreeView::OnLButtonDown(pt, mkFlags);
    }

    // 复选框画在行"内容区"的最左侧，而内容区矩形正是树 layout 给自绘行控件的那个
    // 矩形 —— 直接取行控件的矩形，就不必在这里重算 glyph 宽度与缩进（那是基类的
    // 私有细节，重算一份迟早对不上）。行控件的矩形在它被画过一次之后即为最新；
    // 用户点得到这一行，说明它已经画出来了。
    balloonwjui::DuiControl* rowCtrl = GetItemCustomControl(nodeId);
    MemberPickerRow* row = dynamic_cast<MemberPickerRow*>(rowCtrl);
    if (row == NULL)
    {
        // 该行没挂行控件（理论上不该出现），退回基类默认行为。
        return MainPanelTreeView::OnLButtonDown(pt, mkFlags);
    }

    const RECT rcRow = row->GetRect();
    const int  stripRight = rcRow.left + MemberPickerRow::CheckboxStripWidth();
    if (pt.x >= rcRow.left && pt.x < stripRight)
    {
        m_toggledNodeId = nodeId;
        NotifyParent(DUIN_PICKER_CHECK_TOGGLE);
        return true;   // 消费掉：不让基类顺手把这一行选中 / 展开收起
    }

    // 落在复选框之外：交回基类走它的选中行 / 点箭头展开收起。基类没有展开 / 收起
    // 的通知，故在这里前后比对内容总高：变了就说明用户折叠或展开了某个部门，
    // 通知宿主重设树在滚动区里的高度。
    const int heightBefore = GetContentHeight();
    const bool handled = MainPanelTreeView::OnLButtonDown(pt, mkFlags);
    if (GetContentHeight() != heightBefore)
    {
        NotifyParent(DUIN_PICKER_HEIGHT_CHANGED);
    }
    return handled;
}

} // namespace balloonmain
