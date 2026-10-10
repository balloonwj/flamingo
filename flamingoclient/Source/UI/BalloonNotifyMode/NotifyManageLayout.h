/**
 *  NotifyManageLayout.h
 *
 *  批量管理窗口「消息提醒管理」的客户区控件树构造与状态注入（docs/消息提醒模式与消息盒子方案.md §4.6）。
 *  自上而下：页签条（群聊 / 单聊）、搜索框、「全选」勾选框与已选计数、会话列表（复选框 + 头像 +
 *  名字 + 右端的当前模式）、底部「将选中的设为 [下拉框]」+「应用」。
 *
 *  全部用 balloonui 现成控件组合（DuiTab / DuiEdit / DuiButton / DuiLabel / DuiComboBox），列表复用
 *  选人对话框的 MemberPickerTreeView + MemberPickerRow 与主面板的 MainPanelScrollView
 *  （开发约定文档 UI 约定第 2 条）。本 .cpp 不 include stdafx.h，在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing，与 FlamingoUnit.vcxproj 共用；包含本头文件之前调用方须已引入 ATL/WTL。
 *
 *  典型用法（CBalloonNotifyManageWindow）：
 *      SetClientContent(balloonnotifymode::BuildNotifyManageBody());
 *      balloonnotifymode::SetManageTargetChoices(root, isGroup);   //切页签时换下拉框选项
 *      balloonnotifymode::ApplyManageListState(root, rowCount, truncated, checkedCount, checkState);
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonnotifymode {

/**
 *  NotifyManageCtrlId —— 窗口里需要被代码引用的控件 id。取 5700 段，与其它窗口互不重叠。
 */
enum NotifyManageCtrlId : UINT
{
    kIdManageTabs       = 5700,   // 页签条（DuiTab）：第 0 页群聊、第 1 页单聊
    kIdManageSearch     = 5701,   // 搜索框（DuiEdit）
    kIdManageSelectAll  = 5702,   // 「全选」勾选框（DuiButton，复选框样式）：勾 / 取消当前列出的全部行
    kIdManageCount      = 5703,   // 已选计数「已选 N 个」（DuiLabel）
    kIdManageHint       = 5704,   // 列表被截断时的提示（DuiLabel）；没有截断时隐藏
    kIdManageScroll     = 5705,   // 列表的滚动视口（MainPanelScrollView）
    kIdManageTree       = 5706,   // 会话列表（MemberPickerTreeView）；行由窗口填写
    kIdManageEmpty      = 5707,   // 列表为空时的提示文字（DuiLabel）；有行时隐藏
    kIdManageTarget     = 5708,   // 「将选中的设为」后面的下拉框（DuiComboBox），选项随页签变
    kIdManageApply      = 5709,   // 「应用」按钮；没有勾选任何会话时禁用
};

// 页签序号
const int kManageTabGroup  = 0;   // 群聊页
const int kManageTabSingle = 1;   // 单聊页

// 窗口尺寸（逻辑像素）。均为<u>整窗</u>尺寸、含标题栏（开发约定文档 UI 约定第 7 条）。
const int kManageWindowWidth  = 460;   // 初始宽：名字与右端的模式文字「收进消息盒子且不提醒」能同时放下
const int kManageWindowHeight = 600;   // 初始高
const int kManageMinWidth     = 420;   // 最小宽：底部「将选中的设为 [下拉框] 应用」一行放得下
const int kManageMinHeight    = 420;   // 最小高
const int kManageTitleBarH    = 38;    // 标题栏高（与消息盒子窗口一致）

// 列表行高（逻辑像素），与选人对话框的候选行相同
const int kManageRowHeight = 44;

// 列表为空时的提示文字
extern const wchar_t* const kManageEmptyText;

/**
 *  构造窗口的客户区控件树（不含标题栏）。初始：群聊页、下拉框为群聊的五个选项并选中「允许消息提醒」、
 *  列表为空、「应用」禁用、截断提示隐藏。
 *  返回：客户区根控件，调用方拿走所有权。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildNotifyManageBody();

/**
 *  按页签换下拉框的选项：群聊五项、单聊四项（没有屏蔽），顺序与右键二级菜单相同（NotifyModeMenu）。
 *  换完后选中「允许消息提醒」，不发通知。
 *    root     BuildNotifyManageBody 的返回值；NULL 时什么也不做。
 *    isGroup  是否群聊页。
 */
void SetManageTargetChoices(balloonwjui::DuiControl* root, bool isGroup);

/**
 *  下拉框当前选中的目标模式（enum conversation_notify_mode）。没有选中时按允许消息提醒。
 *    root     BuildNotifyManageBody 的返回值；NULL 时返回允许消息提醒。
 *    isGroup  是否群聊页（决定下标与模式的对应）。
 */
int GetManageTargetMode(balloonwjui::DuiControl* root, bool isGroup);

/**
 *  按列表当前状态切换界面：有行时显示列表、没有时显示空提示；截断时显示提示；更新已选计数；
 *  「全选」框的勾选态；没有勾选任何会话时「应用」禁用。显隐变化后对父容器 ForceLayout，并按行数
 *  设好滚动视口的内容高度。
 *    root          BuildNotifyManageBody 的返回值；NULL 时什么也不做。
 *    rowCount      列表里的行数（>= 0）。
 *    truncated     列表是否被截断（只列出了前 kNotifyManageMaxRows 行）。
 *    checkedCount  当前页签里勾选的会话数（含没有列出的）。
 *    allChecked    列出的行是否全部勾选（为 true 时「全选」框显示为勾选）。
 */
void ApplyManageListState(balloonwjui::DuiControl* root, int rowCount, bool truncated,
                          int checkedCount, bool allChecked);

} // namespace balloonnotifymode
