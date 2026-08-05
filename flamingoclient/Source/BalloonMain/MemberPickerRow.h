/**
 *  MemberPickerRow.h
 *
 *  选人对话框（CCreateGroupDlg）左侧候选树里"一行"的绘制控件：复选框 + 头像 +
 *  姓名。挂在 DuiTreeView 节点上（SetItemCustomControl），由树负责行底色 / 悬停
 *  高亮 / 展开箭头 / 缩进，本控件只画内容区。
 *
 *  为什么是"只画不管事件"：balloonui 挂给树节点的自绘控件<u>不进父链</u>
 *  （见 DuiTreeView::SetItemCustomControl 的实现注释），控件内部即便放一个真的
 *  DuiButton 复选框，它的通知也发不到对话框；库的设计意图是自绘控件只负责绘制、
 *  交互由树自己判定。故本控件不覆写任何鼠标处理，"点到复选框了没有"由
 *  MemberPickerTreeView 在行内按横坐标判定，判定用的条带宽度即本类的
 *  CheckboxStripWidth()。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonmain {

/**
 *  MemberPickerRow —— 候选树的行内容（复选框 + 头像 + 姓名），纯绘制。
 */
class MemberPickerRow : public balloonwjui::DuiControl
{
public:
    MemberPickerRow();

    /**
     *  复选框条带的宽度（逻辑像素）—— 行内容区最左侧这么宽的一竖条属于复选框，
     *  点在这里视为"切换勾选"，点在右边视为"点这一行"（选中 / 展开收起）。
     *  MemberPickerTreeView 判定点击落点时取本值，两处口径由此始终一致。
     */
    static int CheckboxStripWidth();

    /**
     *  设置本行要显示的内容。
     *    name         显示名（部门名或成员显示名）；NULL 视为空串。
     *    avatarText   头像里的字（一般取显示名首字）；空串表示本行不画头像
     *                 （部门节点用这种形态：只有复选框 + 名字）。
     *    avatarColor  头像底色。
     *  副作用：触发重绘。
     */
    void SetRowData(LPCTSTR name, LPCTSTR avatarText, COLORREF avatarColor);

    /**
     *  设置勾选态。
     *    state：取值见 MemberPickerTreeModel.h 的 MemberPickerCheckState
     *           （未选 / 已选 / 半选）。半选画成实心小方块，用于"部门下只勾了
     *           一部分成员"。
     *  副作用：触发重绘。
     */
    void SetCheckState(int state);
    int  GetCheckState() const { return m_checkState; }

    /**
     *  DuiControl 覆写：画复选框 + 头像 + 姓名。形状走 GDI+ 抗锯齿，
     *  文字走 GDI DrawText（清晰）。
     *    hdc      目标设备上下文。
     *    rcDirty  脏矩形（客户区坐标），本控件整块重画、不做局部裁剪。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    CString  m_name;          // 显示名
    CString  m_avatarText;    // 头像内的字；空 = 不画头像（部门行）
    COLORREF m_avatarColor;   // 头像底色
    int      m_checkState;    // 勾选态（MemberPickerCheckState）
};

} // namespace balloonmain
