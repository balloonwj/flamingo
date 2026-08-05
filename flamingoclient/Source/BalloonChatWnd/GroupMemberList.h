/**
 *  GroupMemberList.h
 *
 *  群聊窗右侧边栏的成员列表容器 —— 把若干 GroupMemberItem 按固定行高
 *  自上而下堆叠。子控件超出本控件矩形的部分由 OnPaint 裁剪。
 *
 *  注：本阶段 mock 成员数（9 名）未超过边栏可视高度，故暂不做滚动；
 *  真实群（成员更多）的滚动留作后续。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

// 成员行固定行高（逻辑像素）。
const int kGroupMemberRowHeight = 38;

/**
 *  GroupMemberList —— 成员列表容器（固定行高堆叠）。
 */
class GroupMemberList : public balloonwjui::DuiControl
{
public:
    GroupMemberList();

    // DuiControl 覆写：把子控件按固定行高自上而下堆叠。
    void Layout(const RECT& rcAvail) override;

    // DuiControl 覆写：绘制各成员行（裁剪到本控件矩形内）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
};

} // namespace balloonchatwnd
