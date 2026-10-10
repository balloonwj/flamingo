/**
 *  GroupQaEditLayout.h
 *
 *  「新建问答」「修改问答」对话框（docs/群问答方案.md）的客户区控件树与尺寸常量。控件树单独成文件，
 *  对话框（GroupQaEditDialog）与布局单测（Source/Tests/GroupQaTests.cpp 的 QA-C3）共用。
 *
 *  从上到下：「问题」小标题、问题输入框（单行）、「答案」小标题、答案输入框（多行、占满余下高度）、
 *  提示行（校验不通过时显示原因）、按钮行（右侧「取消」「保存」）。
 *
 *  典型用法：
 *      SetClientContent(balloonchatwnd::BuildQaEditBody());
 *      ResizeClient(kQaEditWindowWidth, kQaEditWindowHeight);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <memory>

namespace balloonwjui { class DuiControl; }

namespace balloonchatwnd {

// 标题栏高（逻辑像素）
const int kQaEditTitleBarH = 32;
// 整窗宽（逻辑像素，含标题栏所在的整窗，DuiFrameWindow::ResizeClient 设的就是整窗）
const int kQaEditWindowWidth = 440;
// 整窗高（逻辑像素，含标题栏）
const int kQaEditWindowHeight = 440;

// 对话框里各控件的 ctrlId
enum QaEditCtrlId
{
    // 问题输入框（DuiEdit，单行）
    kQaEditIdQuestion = 1,
    // 答案输入框（DuiEdit，多行）
    kQaEditIdAnswer = 2,
    // 提示行（DuiLabel，红字）：校验不通过时显示原因，平时为空
    kQaEditIdError = 3,
    // 「取消」按钮
    kQaEditIdCancel = 4,
    // 「保存」按钮
    kQaEditIdSave = 5,
    // 按钮行（DuiHBox），布局单测用它检查按钮没有被挤出窗口
    kQaEditIdButtonRow = 6
};

/**
 *  建对话框的客户区控件树（标题栏以下）。
 *  @return 控件树根节点，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildQaEditBody();

} // namespace balloonchatwnd
