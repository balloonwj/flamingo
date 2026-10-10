/**
 *  聊天记录管理器时间轴模式的界面部分，HistoryTimelineLayout.h
 *
 *  时间轴模式（docs/聊天记录管理器方案.md §11.7，布局图见 tmp/histenhance/layout.html 图 4）在聊天记录页上
 *  多出三样东西，控件都由 BuildHistoryWindowBody 建好（平时隐藏），本文件负责填内容与切换显隐：
 *      搜索行最右边   [列表|时间轴]                    ApplyHistoryTimelineMode 切换选中态与导航条显隐
 *      筛选行下面     共 31 处命中…   第 3 / 31 处 [▲ 上一处][▼ 下一处]      ApplyHistoryHitBar
 *      列表右侧       导航条：▾ 2026  1,016 条 / 10月 ▬▬▬ 5 / 9月 ▬▬▬▬ 9 …  BuildTimelineNavContent
 *  列表本身的时间轴样式（时间列、竖线、日期大节点）由 BuildListContent 按 HistoryRowView::m_timeline 画。
 *
 *  导航条的月份行、年标题行按导航数据（BuildTimelineYears 的结果）依次编号：第 i 年标题行的编号是
 *  kIdHistoryNavYearBase + i；月份行把各年的月份接着往下数，第 j 个是 kIdHistoryNavMonthBase + j（收起的年份
 *  的月份行不建，编号照样占着）。窗口按落点找到行之后，用 TimelineNavYearOfId / TimelineNavMonthOfId 换回年月。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。单测见 Tests/HistoryTimelineLayoutTests.cpp
 *  （标签 [timeline]）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <memory>
#include <set>
#include <vector>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "HistoryTimeline.h"   // HistoryTimelineYear

namespace balloonhistory {

//导航条宽度（像素），含左侧 1 像素的分隔线
const int kHistoryNavW = 148;

/**
 *  建导航条的内容（导航条滚动视口的内容）：各年的标题行，展开的年份下面是各月的行，最后是图例。
 *  @param years            导航数据（BuildTimelineYears 的结果）
 *  @param currentMonthKey  当前列表所在的月份（年*100+月），这一行高亮；0 表示不高亮任何一行
 *  @param expandedYears    展开的年份；不在其中的年份只显示标题行
 *  @return 内容容器（id = kIdHistoryNavList），调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> BuildTimelineNavContent(const std::vector<HistoryTimelineYear>& years,
                                                                 int64_t currentMonthKey,
                                                                 const std::set<int>& expandedYears);

/**
 *  导航条月份行的编号换回年月。
 *  @param years  建导航条时用的导航数据
 *  @param id     月份行的编号
 *  @param year / month  [出参] 年、月；返回 false 时不改
 *  @return 编号属于月份行段且在导航数据范围内时返回 true
 */
bool TimelineNavMonthOfId(const std::vector<HistoryTimelineYear>& years, UINT id, int& year, int& month);

/**
 *  导航条年标题行的编号换回年份。
 *  @param years  建导航条时用的导航数据
 *  @param id     年标题行的编号
 *  @param year   [出参] 年份；返回 false 时不改
 *  @return 编号属于年标题行段且在导航数据范围内时返回 true
 */
bool TimelineNavYearOfId(const std::vector<HistoryTimelineYear>& years, UINT id, int& year);

/**
 *  切换「列表 | 时间轴」：两枚胶囊的选中态、列表右侧导航条的显隐；随后按开发约定文档 UI 约定第 5 条重排
 *  列表与导航条所在的那一行。时间轴模式关掉时命中条一并隐藏。
 *  @param root      聊天记录页的根容器或它的祖先（不持有），可为 NULL
 *  @param timeline  true 时间轴，false 列表
 */
void ApplyHistoryTimelineMode(balloonwjui::DuiControl* root, bool timeline);

/**
 *  更新命中条：显隐、左侧说明、右侧「第 x / N 处」与两个跳转按钮的可用状态；显隐变化后重排聊天记录页。
 *  @param root          聊天记录页的根容器或它的祖先（不持有），可为 NULL
 *  @param show          是否显示（时间轴模式且有关键字时为 true）
 *  @param info          左侧说明，如「共 31 处命中，时间轴模式下保留上下文、只高亮命中」
 *  @param position      右侧位置，如「第 3 / 31 处」；还没有当前处时可传空串
 *  @param canNavigate   上一处 / 下一处是否可点（有命中时为 true）
 */
void ApplyHistoryHitBar(balloonwjui::DuiControl* root, bool show, const CString& info, const CString& position,
                        bool canNavigate);

/**
 *  命中条当前是否显示。
 *  @param root  聊天记录页的根容器或它的祖先（不持有），可为 NULL
 */
bool IsHistoryHitBarVisible(balloonwjui::DuiControl* root);

} // namespace balloonhistory
