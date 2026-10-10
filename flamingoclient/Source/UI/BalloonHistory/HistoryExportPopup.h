/**
 *  HistoryExportPopup.h
 *
 *  聊天记录管理器右侧底部「↓ 导出」弹出的导出选项浮层（docs/聊天记录管理器方案.md §12.7）：范围（当前会话 /
 *  选择会话，选择时列出全部会话供勾选）、时间、格式、是否带图片与文件、是否打包为 zip，底部「取消」「导出」。
 *  子类化 balloonui 的 DuiPopupHost（无边框 WS_POPUP 浮层，自带 ESC / 失焦自动关闭），与「选日期」「搜索范围」
 *  浮层同一做法。内容骨架由 HistoryExportLayout 的 BuildHistoryExportPopupContent 构造（有单元测试），会话勾选
 *  列表各行沿用搜索范围浮层的 BuildHistoryScopePopupRow；本类负责填行、记录选项、回调宿主。
 *
 *  提交时机：点「导出」时读出各控件的状态，以选项回调宿主后关闭；选择会话却一个都没勾时不提交、提示勾选。
 *  ESC、点浮层外面、点「取消」只关闭。
 *
 *  典型用法（消息记录窗口）：
 *      m_exportPopup.SetSubmitCallback(&OnExportPopupThunk, this);
 *      m_exportPopup.OpenAbove(exportButtonScreenRect, entries, m_lastExportOptions);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <map>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"
#include "HistoryScopePopup.h"    // CHistoryScopePopup::Entry：会话勾选列表的一行
#include "HistoryExportJob.h"     // HistoryExportOptions

namespace balloonwjui { class DuiScrollView; class DuiLabel; }

namespace balloonhistory {

/**
 *  CHistoryExportPopup —— 聊天记录管理器的导出选项浮层。
 */
class CHistoryExportPopup : public balloonwjui::DuiPopupHost
{
public:
    // 提交回调：用户点了「导出」且选项有效时触发一次，随后浮层关闭。
    //   userdata：注册时传入的不透明指针，本类只透传。
    //   opts：    用户选定的导出选项（勾选的会话已去重排序）。
    typedef void (*SubmitCallback)(void* userdata, const HistoryExportOptions& opts);

    CHistoryExportPopup();
    ~CHistoryExportPopup();

    /**
     *  注册提交回调。
     *    cb：回调函数指针；NULL 表示取消注册。
     *    userdata：回调时原样带回；本类不持有，调用方须保证它活得比本浮层久（一般传宿主窗口 this）。
     */
    void SetSubmitCallback(SubmitCallback cb, void* userdata);

    /**
     *  在锚点矩形上方打开浮层（上方放不下时 DuiPopupHost 自动翻到下方）。
     *    anchorScreen：锚点矩形（屏幕坐标），一般是「↓ 导出」按钮。
     *    entries：     「选择会话」时列出的会话（一般是左栏的全部会话），复制一份保存。
     *    opts：        初始选项（一般是上一次导出时的选项）。
     */
    void OpenAbove(const RECT& anchorScreen, const std::vector<CHistoryScopePopup::Entry>& entries,
                   const HistoryExportOptions& opts);

    BEGIN_MSG_MAP(CHistoryExportPopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    /**
     *  内容树里的控件发来的 DUI 通知：范围与格式胶囊、会话复选框、两个按钮。判定时通知码与控件编号一起比对
     *  （开发约定文档 UI 约定第 4 条）。
     *    lParam：balloonwjui::DuiNotify 的地址，仅在本次同步调用期间有效。
     *    bHandled：出参，未匹配时置 FALSE 让消息继续走基类的处理链。
     */
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次打开时构建一次内容树。重复调用无副作用。
    void BuildContentOnce();

    // 重建会话勾选列表（勾选状态取自 m_opts.m_conversations）。
    void RebuildRows();

    // 把「已选 N 个会话」改成当前勾选的个数；warn 为 true 时改成「请至少勾选一个会话」。
    void UpdateInfo(bool warn);

    // 从格式以外的控件读出时间、附件与打包的选择，写进 m_opts。
    void ReadControls();

private:
    std::vector<CHistoryScopePopup::Entry> m_entries;      // 列出的会话（OpenAbove 时复制）
    HistoryExportOptions                   m_opts;         // 当前选项（胶囊与勾选随点随改，其余在提交时读）
    std::map<UINT, size_t>                 m_rowByCheckId; // 复选框编号 → m_entries 下标；RebuildRows 重建
    balloonwjui::DuiScrollView*            m_scroll;       // 会话勾选列表的滚动视口（借用，所有权在内容树里）
    balloonwjui::DuiLabel*                 m_info;         // 「已选 N 个会话」（借用，同上）
    bool                                   m_built;        // 内容树是否已构建（只建一次）
    bool                                   m_committed;    // 本次打开是否已经提交过
    SubmitCallback                         m_submitCb;     // 提交回调；NULL 表示未注册
    void*                                  m_submitUd;     // 提交回调的用户数据，本类只透传
};

} // namespace balloonhistory
