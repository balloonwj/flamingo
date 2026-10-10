/**
 *  MemberPickerTreeView.h
 *
 *  选人对话框（CCreateGroupDlg）左侧候选树用的 DuiTreeView 子类。
 *
 *  继承 MainPanelTreeView 而不是直接继承 DuiTreeView —— 为的是白拿它的"滚轮上抛"：
 *  单列 DuiTreeView 自己不滚，滚轮落在铺满视口的树身上就没人处理了；MainPanelTreeView
 *  已经解决过这件事（沿父链找最近的 DuiScrollView 转发过去），本类与主面板那三棵树
 *  是同一个处境，直接复用，不另写一份。
 *
 *  本类自己只加一件事 —— 判定点击是否落在行内最左侧的复选框条带上：
 *    · 落在条带内  → 发 DUIN_PICKER_CHECK_TOGGLE 通知（可用 GetToggledNodeId 取
 *                    是哪个节点），并<u>消费</u>该次点击，不让基类再去选中行 /
 *                    展开收起；
 *    · 落在条带外  → 交回基类，走它默认的选中行 / 点箭头展开收起。
 *  之所以由树来判定而不是让行控件自己处理：挂在树节点上的自绘控件不进父链，通知
 *  发不出去（见 DuiTreeView::SetItemCustomControl 的实现注释与 MemberPickerRow.h
 *  的说明）。本类在父链里，NotifyParent 能正常抵达对话框。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include "MainPanelTreeView.h"

namespace balloonmain {

/**
 *  MemberPickerTreeView —— 候选树：滚轮上抛（继承） + 复选框点击判定。
 */
class MemberPickerTreeView : public MainPanelTreeView
{
public:
    /**
     *  通知码：行内复选框被点击。宿主在 WM_DUI_NOTIFY 里认这个码，再用
     *  GetToggledNodeId() 取节点 id 决定切换谁的勾选态。
     *  取 DUIN_CUSTOM + 40 段，避开 DuiTreeView 自己用掉的 DUIN_CUSTOM + 0..7。
     */
    enum { DUIN_PICKER_CHECK_TOGGLE = balloonwjui::DUIN_CUSTOM + 40 };

    /**
     *  通知码：树的内容总高变了（用户点箭头展开 / 收起了某个部门）。宿主收到后
     *  应按 GetContentHeight() 重设树在滚动区里的高度，否则收起之后底下会留一截
     *  空白、展开之后又滚不到最后几行。基类没有展开 / 收起的通知，故由本类在
     *  点击前后比对内容总高补一个。
     */
    enum { DUIN_PICKER_HEIGHT_CHANGED = balloonwjui::DUIN_CUSTOM + 41 };

    MemberPickerTreeView();

    /**
     *  取最近一次"复选框被点击"对应的节点 id（AddRoot / AddChild 的返回值）。
     *  仅在收到 DUIN_PICKER_CHECK_TOGGLE 后读取才有意义；没发生过返回 -1。
     */
    int GetToggledNodeId() const { return m_toggledNodeId; }

    /**
     *  设置点在行内容区（复选框条带以外）时是否也当作切换勾选。缺省为 false：只有点复选框条带才切换，点行身上交回基类
     *  选中这一行（选人对话框的行为）。为 true 时点在行内容区任何位置都发 DUIN_PICKER_CHECK_TOGGLE；展开箭头在内容区
     *  左侧，仍由基类展开 / 收起。
     *  之所以要在树里判定：挂在节点上的 MemberPickerRow 会按 DuiControl 的默认处理「消耗」鼠标松开，DuiTreeView 因此
     *  不再发行级 DUIN_CLICK，宿主收不到「点了行身上」（2026-10-08 选择部门小窗实机核对时发现）。
     *    on：true 点整行切换勾选，false 只有复选框条带切换。
     */
    void SetToggleOnRowClick(bool on) { m_toggleOnRowClick = on; }

    /**
     *  DuiControl 覆写：左键按下。落在复选框条带内则发通知并消费；否则交回基类。
     *    pt       命中点（客户区坐标，由 DuiHost 路由时已转换）。
     *    mkFlags  鼠标按键 / 修饰键状态位。
     *  返回：true 表示本次点击已被消化（基类不再处理），false 表示未处理。
     */
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

private:
    // 最近一次被点中复选框的节点 id；-1 表示还没发生过。仅供宿主在收到通知后取用。
    int m_toggledNodeId;
    // 点在行内容区（复选框条带以外）时是否也切换勾选；缺省 false，见 SetToggleOnRowClick
    bool m_toggleOnRowClick;
};

} // namespace balloonmain
