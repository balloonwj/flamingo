/**
 *  消息记录窗布局小工具的公开入口，HistoryLayoutKit.h
 *
 *  HistoryWindowLayout.cpp 里有一批造控件的小函数（标签、按钮、类型胶囊、分隔线、「通配符」复选框、时间下拉、
 *  按默认字体量文字宽度），原先都在它的匿名命名空间里。聊天记录管理器的文件页与图片页（HistoryLibraryLayout.cpp，
 *  docs/聊天记录管理器方案.md §11.5、§11.6）要用同一套外观，本文件把这几样按原样转调公开，不另写一份。
 *  实现在 HistoryWindowLayout.cpp 的 namespace layoutkit 里。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonhistory { namespace layoutkit {

//消息记录窗的几种文字颜色：标题、副标题与底部信息、行内名字、行内时间、空状态主文案
COLORREF TitleColor();
COLORREF SubtitleColor();
COLORREF NameColor();
COLORREF TimeColor();
COLORREF EmptyTitleColor();

/**
 *  造一个文本标签。
 *    text：文字；color：文字颜色；dtAlign：DrawText 对齐标志；ctrlId：控件编号，0 表示不设。
 *  @return 标签控件，调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> Label(LPCTSTR text, COLORREF color, DWORD dtAlign, UINT ctrlId = 0);

/**
 *  造一个普通按钮。
 *    text：按钮文字；ctrlId：控件编号；variant：外观。
 *  @return 按钮控件，调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> Button(LPCTSTR text, UINT ctrlId, balloonwjui::DuiButton::Variant variant);

/**
 *  造一个筛选胶囊：选中态为 Primary，其余为 Default（与类型胶囊同一外观）。
 *    text：文字；ctrlId：控件编号；active：是否选中。
 *  @return 按钮控件，调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> Chip(LPCTSTR text, UINT ctrlId, bool active);

//造一条横向分隔线（与消息记录窗标题区下方那条同色）。调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> Separator();

/**
 *  造一个「通配符」复选框（与右侧搜索行、左栏范围行那两个同一外观）。
 *    ctrlId：控件编号；checked：建出来时是否勾选（不发通知）；outWidth：[出参] 按文字宽度算出的控件宽度。
 *  @return 复选框控件，调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> WildcardCheckbox(UINT ctrlId, bool checked, int& outWidth);

/**
 *  造时间范围下拉（全部时间 / 近 7 天 / 近 30 天 / 近 3 个月 / 近半年 / 近一年，顺序与 HistoryTimeRange 一致）。
 *    ctrlId：控件编号；selIndex：选中项下标（不发通知）；outWidth：[出参] 按最宽一项实测出的宽度。
 *  @return 下拉框控件，调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> TimeCombo(UINT ctrlId, int selIndex, int& outWidth);

//按 balloonui 默认字体量一段文字的宽度（像素）；空串返回 0
int TextWidth(LPCTSTR text);

//按文字宽度「只增不减」地取控件宽度：文字放得下时为 designWidth，否则为文字宽度加 padding
int GrowWidth(LPCTSTR text, int designWidth, int padding);

//胶囊在文字宽度之外的左右留白合计（像素），配合 GrowWidth 使用
int ChipPadWidth();

//下拉框在文字宽度之外要占的宽度（内边距、箭头与余量合计，像素）
int ComboChromeWidth();

}} // namespace balloonhistory::layoutkit
