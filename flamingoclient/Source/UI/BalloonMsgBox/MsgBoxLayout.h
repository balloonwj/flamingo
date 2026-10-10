/**
 *  MsgBoxLayout.h
 *
 *  「消息盒子」窗口的客户区控件树构造与状态注入（docs/消息提醒模式与消息盒子方案.md §4.5）。
 *  消息盒子收的是提醒模式为「收进消息盒子且不提醒」的会话：窗口顶部一行左边「全部标为已读」、
 *  右边「批量管理」，下面是这些会话的列表，外观与主面板会话列表相同（同一个 MainPanelTreeView，
 *  行内容由主面板用填会话行的同一段代码填写），一个都没有时列表区居中显示空状态文字。
 *
 *  把"窗口长什么样"从窗口类里抽出来，可独立编译、可被单元测试直接调用：CBalloonMsgBoxWindow
 *  与 FlamingoUnit 测试工程共用本文件。本 .cpp 不 include stdafx.h，在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing；包含本头文件之前调用方须已引入 ATL/WTL。
 *
 *  典型用法（CBalloonMsgBoxWindow::InitUi）：
 *      SetClientContent(balloonmsgbox::BuildMsgBoxBody());
 *      ...主面板往 kIdMsgBoxTree 里填好行之后...
 *      balloonmsgbox::ApplyMsgBoxState(GetClientContent(), convCount, unreadTotal);
 *      balloonmsgbox::SyncMsgBoxContentHeight(GetClientContent());
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonmsgbox {

/**
 *  MsgBoxCtrlId —— 消息盒子窗口里需要被代码引用的控件 id。取 5600 段，与其它窗口互不重叠。
 *  DuiFrameWindow 保留 1/2/3 给标题栏按钮。
 */
enum MsgBoxCtrlId : UINT
{
    kIdMsgBoxMarkAllRead = 5600,   // 顶部左侧「全部标为已读」按钮；没有未读时禁用
    kIdMsgBoxManage      = 5601,   // 顶部右侧「批量管理」按钮：打开批量管理消息提醒窗口
    kIdMsgBoxScroll      = 5602,   // 会话列表的滚动视口（MainPanelScrollView，悬浮式自动隐藏滚动条）
    kIdMsgBoxTree        = 5603,   // 会话列表（MainPanelTreeView）；行由主面板填写
    kIdMsgBoxEmpty       = 5604,   // 空状态文字「没有收进消息盒子的会话」；有会话时隐藏
};

// 窗口尺寸（逻辑像素）。均为<u>整窗</u>尺寸、含标题栏：DuiFrameWindow 的客户区就是整个窗口
//（开发约定文档 UI 约定第 7 条）。宽度与主面板的默认宽度相近，列表行的观感与主面板一致。
const int kMsgBoxWindowWidth  = 380;   // 初始宽
const int kMsgBoxWindowHeight = 560;   // 初始高
const int kMsgBoxMinWidth     = 300;   // 最小宽：再窄会话名与时间就挤在一起了
const int kMsgBoxMinHeight    = 300;   // 最小高：至少放得下顶部按钮行与三四行会话
const int kMsgBoxTitleBarH    = 38;    // 标题栏高（与公告中心窗口一致）

// 列表的行高与头像边长（逻辑像素），与主面板会话列表相同
const int kMsgBoxRowHeight    = 48;

// 空状态文字
extern const wchar_t* const kMsgBoxEmptyText;

/**
 *  构造消息盒子窗口的客户区控件树（不含标题栏，标题栏由 DuiFrameWindow 负责）。
 *  初始为空状态：列表区隐藏、空状态文字可见、「全部标为已读」禁用。
 *  返回：客户区根控件，调用方拿走所有权。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildMsgBoxBody();

/**
 *  按当前数据切换界面状态：有会话时显示列表、隐藏空状态文字，没有时反过来；没有未读时
 *  「全部标为已读」禁用。显隐变化后对父容器 ForceLayout（开发约定文档 UI 约定第 5 条）。
 *    root         BuildMsgBoxBody 的返回值；NULL 时什么也不做。
 *    convCount    被收进消息盒子的会话数（>= 0）。
 *    unreadTotal  这些会话的未读之和（>= 0）。
 */
void ApplyMsgBoxState(balloonwjui::DuiControl* root, int convCount, int unreadTotal);

/**
 *  按列表当前的行数把滚动视口的内容高度设好（树控件自己问不出总高度，见 MainPanelScrollView）。
 *  主面板往列表里填完行、或增删行之后调用。
 *    root  BuildMsgBoxBody 的返回值；NULL 时什么也不做。
 */
void SyncMsgBoxContentHeight(balloonwjui::DuiControl* root);

} // namespace balloonmsgbox
