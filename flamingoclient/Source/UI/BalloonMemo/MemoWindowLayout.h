/**
 *  MemoWindowLayout.h
 *
 *  本人备忘窗口（CBalloonMemoWindow）客户区控件树的构造接口、窗口尺寸与控件 id。控件树单独成文，
 *  是为了让布局单测在不建窗口的情况下按设计尺寸排版、检查各部分排得下（开发约定文档 UI 约定第 7 条）。
 *
 *  左边是月历：「‹ 2026年10月 › 今天」一行、星期一到星期日一行、6 行 × 7 列日期格；
 *  右边是选中那天的备忘：日期标题、条数提示、备忘列表、底部「＋ 添加备忘」按钮。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonmemo {

//窗口整窗尺寸（逻辑像素，含标题栏；DuiFrameWindow 的 ResizeClient 设的就是整窗）。可以拉大，不能小于这个尺寸
const int kMemoWindowWidth  = 760;
const int kMemoWindowHeight = 520;

//标题栏高度（逻辑像素），在上面的整窗高度之内
const int kMemoWindowTitleBarH = 32;

/**
 *  备忘窗口客户区控件的数字 id。从 101 起，避开标题栏最小化 / 最大化 / 关闭按钮固定使用的 1～3。
 */
enum MemoWindowCtrlId
{
    kMemoWinIdPrev       = 101,   // 「‹」上个月
    kMemoWinIdNext       = 102,   // 「›」下个月
    kMemoWinIdToday      = 103,   // 「今天」：回到今天所在的月份并选中今天
    //104 原为月份标题「2026年10月」，2026-10-04 换成年、月两个下拉框后不再使用
    kMemoWinIdDayTitle   = 105,   // 右侧选中那天的标题，如「10月3日 星期六」
    kMemoWinIdDayHint    = 106,   // 右侧条数提示：「共 2 条」或「这一天还没有备忘」
    kMemoWinIdList       = 107,   // 右侧当天的备忘卡片（DuiScrollView，见 MemoCardList.h）：悬停显示「修改」「删除」，双击修改或查看，右键弹菜单
    kMemoWinIdAdd        = 108,   // 「＋ 添加备忘」
    kMemoWinIdYear       = 109,   // 年份下拉框（DuiComboBox）：项由窗口按 CalendarYearRange 填入，选了就切到那一年的同一月
    kMemoWinIdMonth      = 110,   // 月份下拉框（DuiComboBox）：1～12 月，选了就切到当年的那个月
    kMemoWinIdDayFirst   = 200,   // 第一个日期格；第 i 格（从 0 起，共 42 格）的 id 是 kMemoWinIdDayFirst + i
};

/**
 *  构造备忘窗口的客户区控件树。
 *  @return 客户区根控件，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildMemoWindowBody();

} // namespace balloonmemo
