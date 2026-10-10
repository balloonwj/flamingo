/**
 *  MemoEditLayout.h
 *
 *  备忘编辑对话框（CMemoEditDialog）客户区控件树的构造接口、窗口尺寸与控件 id。个人备忘与群备忘共用，
 *  群模式多一行「提醒对象」。控件树单独成文，是为了让布局单测在不建窗口的情况下按设计尺寸排版
 *  （开发约定文档 UI 约定第 7 条）。
 *
 *  自上而下：标题；开始时间（年月日时分五个微调框）；提醒（下拉）与重复（下拉）；每周重复的星期一到星期日；
 *  结束（永不结束 / 提醒 N 次）；提醒方式（右下角弹窗 / 全屏动画，右侧一个「预览效果」）；群模式的提醒对象（全体成员 / 指定成员 +
 *  「选择成员」+ 已选人数）；备注（多行）；提示行；底部「取消 / 保存」。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonmemo {

//窗口整窗宽（逻辑像素，含边框；DuiFrameWindow 的 ResizeClient 设的就是整窗）
const int kMemoEditWindowWidth = 480;

//整窗高：个人备忘与群备忘各一个，群备忘多「提醒对象」一行。不可缩放
const int kMemoEditWindowHeightPersonal = 500;
const int kMemoEditWindowHeightGroup    = 536;

//标题栏高度（逻辑像素），在上面的整窗高度之内
const int kMemoEditTitleBarH = 32;

//「提醒」下拉的各项依次对应的提前分钟数，下拉的选项序号即本数组下标
const int kMemoAdvanceChoiceCount = 8;
extern const int kMemoAdvanceChoices[kMemoAdvanceChoiceCount];

/**
 *  编辑对话框客户区控件的数字 id。从 101 起，避开标题栏按钮固定使用的 1～3。
 */
enum MemoEditCtrlId
{
    kMemoEditIdTitle        = 101,   // 标题（DuiEdit，单行）
    kMemoEditIdYear         = 102,   // 开始时间：年（DuiSpinBox）
    kMemoEditIdMonth        = 103,   // 开始时间：月（DuiSpinBox，1~12）
    kMemoEditIdDay          = 104,   // 开始时间：日（DuiSpinBox，1~31，日期是否存在在保存时检查）
    kMemoEditIdHour         = 105,   // 开始时间：时（DuiSpinBox，0~23）
    kMemoEditIdMinute       = 106,   // 开始时间：分（DuiSpinBox，0~59）
    kMemoEditIdAdvance      = 107,   // 提醒（DuiComboBox）：到点提醒、提前 5 / 10 / 15 / 30 分钟、1 / 2 小时、1 天
    kMemoEditIdRepeat       = 108,   // 重复（DuiComboBox）：不重复、每天、每个工作日、每周、每月、每年，序号即 memo_repeat_type
    kMemoEditIdWeekdayFirst = 110,   // 星期一的勾选框（DuiButton 复选框）；星期 i（0 周一 ... 6 周日）是 kMemoEditIdWeekdayFirst + i
    kMemoEditIdWeekdayRow   = 117,   // 「每周」那一行（DuiHBox）：只在重复方式为每周时显示其中的控件，行本身始终占位
    kMemoEditIdEndNever     = 120,   // 结束：永不结束（单选）
    kMemoEditIdEndCount     = 121,   // 结束：提醒 N 次（单选）
    kMemoEditIdCount        = 122,   // N（DuiSpinBox，1~999）
    kMemoEditIdStylePopup   = 123,   // 提醒方式：右下角弹窗（单选）
    kMemoEditIdStyleFull    = 124,   // 提醒方式：全屏动画（单选）
    kMemoEditIdTargetAll    = 125,   // 提醒对象：全体成员（单选，仅群模式）
    kMemoEditIdTargetPick   = 126,   // 提醒对象：指定成员（单选，仅群模式）
    kMemoEditIdPickButton   = 127,   // 「选择成员」按钮（仅群模式）
    kMemoEditIdTargetText   = 128,   // 已选几人（DuiLabel，仅群模式）
    kMemoEditIdNote         = 129,   // 备注（DuiEdit，多行）
    kMemoEditIdError        = 130,   // 按钮行上方的提示行（DuiLabel）
    kMemoEditIdCancel       = 131,   // 「取消」（只读模式下文字为「关闭」）
    kMemoEditIdSave         = 132,   // 「保存」（只读模式下隐藏）
    kMemoEditIdDelete       = 133,   // 左下角「删除」（红色；只在修改已有的一条时显示）
    kMemoEditIdButtonRow    = 134,   // 底部按钮行（DuiHBox）：「删除」显隐之后对它强制重排
    kMemoEditIdPreviewFull  = 135,   // 「方式」一行「全屏动画」右侧的「预览效果」（文字按钮，2026-10-04 起）：按当前填写的内容播放全屏提醒
};

/**
 *  构造编辑对话框的客户区控件树。
 *    groupMode：是否为群备忘（多一行提醒对象）。
 *  @return 客户区根控件，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildMemoEditBody(bool groupMode);

} // namespace balloonmemo
