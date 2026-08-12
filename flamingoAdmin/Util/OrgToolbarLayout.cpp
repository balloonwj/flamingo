/**
 *  flamingoAdmin —— 组织树工具条几何计算的实现。
 *
 *  见 OrgToolbarLayout.h 的接口说明。全部为纯整数运算，不触碰任何 UI 状态。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "stdafx.h"
#include "Util/OrgToolbarLayout.h"

namespace flamingoadmin {

OrgToolbarMetrics LayoutOrgToolbar(const RECT& rcToolbar,
                                   const OrgToolbarSpec& spec)
{
    OrgToolbarMetrics metrics;

    // 按钮个数夹到 [0, kMaxOrgToolbarButtons]，防止调用方传入越界值时越写数组。
    int count = spec.m_buttonCount;
    if (count < 0)
    {
        count = 0;
    }
    if (count > kMaxOrgToolbarButtons)
    {
        count = kMaxOrgToolbarButtons;
    }
    metrics.m_buttonCount = count;

    // 工具条竖向中线：按钮与搜索框都以它为准居中。
    const int cy = (rcToolbar.top + rcToolbar.bottom) / 2;

    // 图标按钮整组右对齐：先算出最右侧按钮的右边界，再从右往左依次退。
    // 数组下标 0 存最左边那个，故用 (count - 1 - i) 换算成"从右数第几个"。
    int leftmostBtnL = rcToolbar.right - spec.m_sidePad;
    for (int i = 0; i < count; ++i)
    {
        const int fromRight = count - 1 - i;
        const int right = rcToolbar.right - spec.m_sidePad
                          - fromRight * (spec.m_buttonSize + spec.m_buttonGap);
        RECT rc;
        rc.left   = right - spec.m_buttonSize;
        rc.top    = cy - spec.m_buttonSize / 2;
        rc.right  = right;
        rc.bottom = rc.top + spec.m_buttonSize;
        metrics.m_rcButtons[i] = rc;

        if (i == 0)
        {
            leftmostBtnL = rc.left;
        }
    }

    // 搜索框：左侧留内边距，右到最左侧按钮之前。没有按钮时右到工具条内边距处。
    RECT rcSearch;
    rcSearch.left   = rcToolbar.left + spec.m_sidePad;
    rcSearch.top    = cy - spec.m_searchH / 2;
    rcSearch.right  = (count > 0) ? (leftmostBtnL - spec.m_searchGap)
                                  : (rcToolbar.right - spec.m_sidePad);
    rcSearch.bottom = rcSearch.top + spec.m_searchH;

    // 工具条过窄时上面算出的右边界可能跑到左边界左侧。宽度夹成 0，
    // 避免把负宽度矩形传给控件的 ForceLayout。
    if (rcSearch.right < rcSearch.left)
    {
        rcSearch.right = rcSearch.left;
    }
    metrics.m_rcSearch = rcSearch;

    return metrics;
}

} // namespace flamingoadmin
