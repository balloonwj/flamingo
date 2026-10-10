/**
 *  RelayComposeLayout.h
 *
 *  「发起接龙」窗口（RelayComposeDlg）客户区控件树的构造接口与窗口尺寸。控件树单独成文，是为了让
 *  布局单测在不建窗口的情况下按设计尺寸排版、检查底部按钮排得下（开发约定文档 UI 约定第 7 条）。
 *  窗口代码按下面的 RelayComposeCtrlId 取回需要操作的控件。
 *
 *  典型用法（窗口建好之后）：
 *      SetTitleBarHeight(ballooncard::kRelayComposeTitleBarH);
 *      SetClientContent(ballooncard::BuildRelayComposeBody());
 *      ResizeClient(ballooncard::kRelayComposeWindowWidth, ballooncard::kRelayComposeWindowHeight);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace ballooncard {

// 窗口整窗尺寸（逻辑像素，含标题栏；DuiFrameWindow 的 ResizeClient 设的就是整窗，见开发约定文档 UI 约定第 7 条）。
// 不可缩放。高度 2026-10-02 联调时由 520 收到 460：520 时按钮上方空出一大块；2026-10-03 截止时间由
// 一个下拉框改为两行微调框（年月日一行、时分秒一行），多出一行，高度相应加到 490
const int kRelayComposeWindowWidth  = 440;
const int kRelayComposeWindowHeight = 490;

// 标题栏高度（逻辑像素），在上面的整窗高度之内；内容区高度 = 整窗高度 - 本值
const int kRelayComposeTitleBarH = 32;

/**
 *  「发起接龙」窗口客户区控件的数字 id，窗口内唯一。
 */
//编号从 101 起：DuiFrameWindow 标题栏的最小化 / 最大化 / 关闭按钮固定使用 1 / 2 / 3（见 balloonui
//DuiFrameWindow.cpp 的 OnDuiChildNotify），它们的点击通知与客户区控件走同一条 WM_DUI_NOTIFY。
//客户区控件与之同号时，窗口自己的通知处理会先把标题栏按钮的点击当成该控件处理：2026-10-03
//之前详情窗「撤回我的」是 3，点标题栏的 X 关不掉窗口，已参与时还会弹出撤回确认。
//布局单测 RCL3 检查整棵控件树没有 id 落在 1~3。
enum RelayComposeCtrlId
{
    kRelayComposeIdTitle    = 101,   // 标题输入框（DuiEdit，单行，必填）
    kRelayComposeIdNote     = 102,   // 说明输入框（DuiEdit，多行，选填）
    //103 原为截止时间下拉框，2026-10-03 截止时间改为下面六个微调框后不再使用
    kRelayComposeIdEntry    = 104,   // 「我的接龙」输入框（DuiEdit，单行，选填）
    kRelayComposeIdCancel   = 105,   // 「取消」按钮
    kRelayComposeIdSubmit   = 106,   // 「发起」按钮
    kRelayComposeIdError    = 107,   // 按钮行上方的提示行（DuiLabel），检查不通过、发送失败时写原因
    kRelayComposeIdDeadlineYear   = 108,   // 截止时间：年（DuiSpinBox，范围由窗口按当前年份设定）
    kRelayComposeIdDeadlineMonth  = 109,   // 截止时间：月（DuiSpinBox，1~12）
    kRelayComposeIdDeadlineDay    = 110,   // 截止时间：日（DuiSpinBox，1~31，这一天是否存在在点「发起」时检查）
    kRelayComposeIdDeadlineHour   = 111,   // 截止时间：时（DuiSpinBox，0~23）
    kRelayComposeIdDeadlineMinute = 112,   // 截止时间：分（DuiSpinBox，0~59）
    kRelayComposeIdDeadlineSecond = 113,   // 截止时间：秒（DuiSpinBox，0~59）
};

/**
 *  构造「发起接龙」窗口的客户区控件树：自上而下是标题、说明、截止时间、我的接龙各一组（小标题 +
 *  输入控件）、提示行、弹性占位、底部「取消 / 发起」按钮行。截止时间是两行微调框：
 *  「[年]年 [月]月 [日]日」与「[时]时 [分]分 [秒]秒」，初始值与年份范围由窗口按当前时间设定。
 *  @return 客户区根控件，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildRelayComposeBody();

} // namespace ballooncard
