/**
 *  聊天记录管理器「导出」选项浮层的内容，HistoryExportLayout.h
 *
 *  点右侧底部「↓ 导出」弹出的浮层（docs/聊天记录管理器方案.md §12.7），自上而下：
 *      导出聊天记录
 *      范围   [当前会话] [选择会话]
 *             （选择会话时）会话勾选列表 + 「已选 N 个会话」；当前会话时一行说明
 *      时间   [全部时间 ▾]（含「按当前筛选条件」）
 *      格式   [网页] [纯文本]
 *      □ 包含图片   □ 包含本机已有的文件   □ 打包为 zip
 *                                             [取消] [导出]
 *  本文件只建控件骨架、做局部更新；勾选列表各行由浮层（CHistoryExportPopup）用 BuildHistoryScopePopupRow 填。
 *  控件编号只在浮层自己的窗口里使用，与消息记录窗口的编号不冲突。布局单测见 Tests/HistoryExportJobTests.cpp。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "HistoryExportJob.h"   // HistoryExportOptions

namespace balloonhistory {

//浮层尺寸（像素）
const int kHistoryExportPopupW = 400;
const int kHistoryExportPopupH = 560;
//会话勾选列表一行的高（与搜索范围浮层相同）
const int kHistoryExportConvRowH = 40;
//会话勾选列表最多列出多少个会话（复选框编号段的容量）
const int kIdExpConvCapacity = 2000;

//浮层里的控件编号
enum HistoryExportCtrlId
{
    kIdExpRoot          = 8900,   //根容器
    kIdExpScopeCurrent  = 8901,   //范围胶囊：当前会话（编号连续，与 HistoryExportScope 一一对应）
    kIdExpScopeSelected = 8902,   //范围胶囊：选择会话
    kIdExpConvBox       = 8903,   //会话勾选区（列表 + 已选个数）；范围为当前会话时隐藏
    kIdExpConvScroll    = 8904,   //会话勾选列表的滚动视口
    kIdExpConvInfo      = 8905,   //「已选 N 个会话」
    kIdExpCurrentHint   = 8906,   //范围为当前会话时显示的一行说明
    kIdExpRangeCombo    = 8907,   //时间下拉（下标见 HistoryExportOptions::m_range）
    kIdExpFmtHtml       = 8908,   //格式胶囊：网页
    kIdExpFmtText       = 8909,   //格式胶囊：纯文本
    kIdExpImages        = 8910,   //复选框：包含图片
    kIdExpFiles         = 8911,   //复选框：包含本机已有的文件
    kIdExpZip           = 8912,   //复选框：打包为 zip
    kIdExpOk            = 8913,   //「导出」
    kIdExpCancel        = 8914,   //「取消」
    kIdExpScopeRow      = 8915,   //范围那一行（重排用）
    kIdExpConvCheckBase = 9000,   //会话勾选列表第 i 行复选框的编号 = 基址 + i（i < kIdExpConvCapacity）
};

/**
 *  建浮层的内容：各行控件按 opts 设好初始状态，会话勾选列表为空（由浮层填）。
 *  @param opts  初始选项
 *  @return 根容器（id = kIdExpRoot），调用方拿到所有权
 */
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryExportPopupContent(const HistoryExportOptions& opts);

/**
 *  按选项更新范围与格式胶囊的选中态、会话勾选区与说明的显隐，并重排发生显隐变化的容器（开发约定文档 UI 约定第 5 条）。
 *  @param root  浮层根容器（不持有），可为 NULL
 *  @param opts  当前选项
 */
void ApplyHistoryExportPopupState(balloonwjui::DuiControl* root, const HistoryExportOptions& opts);

} // namespace balloonhistory
