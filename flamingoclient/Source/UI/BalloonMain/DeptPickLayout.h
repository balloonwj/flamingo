/**
 *  DeptPickLayout.h
 *
 *  「选择部门」小窗（个人机器人规则「发送者 ▸ 指定的部门」，docs/机器人方案.md §14.8）的客户区控件树与状态注入。
 *  自上而下：搜索框、部门树（复选框 + 部门名，层级默认全部展开）、树为空时的提示、底部「已选 N 个部门（含下级部门）」
 *  与「取消」「确定」。
 *
 *  全部用现成控件组合：部门树复用选人对话框的 MemberPickerTreeView + MemberPickerRow（部门行不画头像），
 *  其余为 balloonui 的 DuiEdit / DuiScrollView / DuiLabel / DuiButton（开发约定文档 UI 约定第 2 条）。本 .cpp 不 include
 *  stdafx.h，在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing，与 FlamingoUnit.vcxproj 共用；包含本头文件之前
 *  调用方须已引入 ATL/WTL。
 *
 *  典型用法（CDeptPickDialog）：
 *      SetClientContent(balloonmain::BuildDeptPickBody());
 *      ResizeClient(kDeptPickWindowWidth, kDeptPickWindowHeight);
 *      balloonmain::ApplyDeptPickState(GetClientContent(), rowCount, checkedCount);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <memory>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonmain {

/**
 *  DeptPickCtrlId —— 窗口里需要被代码引用的控件 id。取 5800 段，与其它窗口互不重叠。
 */
enum DeptPickCtrlId : UINT
{
    kIdDeptPickSearch  = 5800,   // 搜索框（DuiEdit）：文字变化时按部门名称过滤
    kIdDeptPickScroll  = 5801,   // 部门树的滚动视口（DuiScrollView）；树为空时隐藏
    kIdDeptPickTree    = 5802,   // 部门树（MemberPickerTreeView）；行由窗口填写
    kIdDeptPickEmpty   = 5803,   // 树为空时的提示「没有找到部门」（DuiLabel）；有行时隐藏
    kIdDeptPickCount   = 5804,   // 已选计数「已选 N 个部门（含下级部门）」（DuiLabel）
    kIdDeptPickCancel  = 5805,   // 「取消」
    kIdDeptPickOk      = 5806,   // 「确定」；一个部门都没选时禁用
    kIdDeptPickBottom  = 5807,   // 底部操作行（DuiHBox），布局单测用它检查没有被挤出窗口
};

// 窗口尺寸（像素）。均为整窗尺寸、含标题栏（开发约定文档 UI 约定第 7 条）
const int kDeptPickWindowWidth  = 400;   // 整窗宽
const int kDeptPickWindowHeight = 560;   // 整窗高
const int kDeptPickTitleBarH    = 32;    // 标题栏高
// 部门树的行高（像素）：部门行只有复选框与名称，比选人对话框的成员行（44）矮
const int kDeptPickRowHeight = 36;

/**
 *  建窗口的客户区控件树（标题栏以下）。初始状态：树为空、提示可见、计数为 0、「确定」禁用。
 *  @return 控件树根节点，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildDeptPickBody();

/**
 *  按当前列表与勾选情况刷新界面：树为空时隐藏滚动视口、显示提示，否则反过来；更新已选计数；没有勾选时禁用「确定」。
 *  显隐有变化时对父容器重排（开发约定文档 UI 约定第 5 条）。
 *    root：        BuildDeptPickBody 建出的根节点，不持有；为 NULL 时什么都不做。
 *    rowCount：    树里当前列出的部门数（搜索过滤之后）。
 *    checkedCount：已勾选的部门数（含当前因搜索没有列出的）。
 */
void ApplyDeptPickState(balloonwjui::DuiControl* root, int rowCount, int checkedCount);

/**
 *  按部门树当前的内容总高（展开、收起之后会变）设置滚动视口的滚动范围，并对滚动视口重排（开发约定文档 UI 约定第 12 条）。
 *  重建树之后、收到 MemberPickerTreeView::DUIN_PICKER_HEIGHT_CHANGED 之后调用。
 *    root：BuildDeptPickBody 建出的根节点，不持有；为 NULL 时什么都不做。
 */
void SyncDeptPickTreeHeight(balloonwjui::DuiControl* root);

} // namespace balloonmain
