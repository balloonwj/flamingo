/**
 *  HistoryDateJumpPopup.h
 *
 *  消息记录窗口「选日期」的弹层：「从 [年][月][日]」「到 [年][月][日]」两行，下面「跳到这一天」
 *  「只看这段时间」两个按钮。子类化 balloonui 的 DuiPopupHost（无边框 WS_POPUP 浮层，自带
 *  ESC / 失焦自动关闭）。
 *
 *  2026-10-01 起，原先的「跳到日期」弹层（只有一组年月日）与时间下拉框里的「自定义...」（起止
 *  日期那一行）合并为本弹层：日期只在一个地方选，要做什么写在按钮上。类名与文件名沿用原名。
 *
 *  年月日用 DuiSpinBox 而不用下拉框：下拉框展开时会再弹出一个浮层并拿走焦点，本弹层随之
 *  因失焦而关闭，用户根本选不了日期。微调框既能直接输入数字，也能点 ▲▼ 调整，不会弹出
 *  第二层浮层。
 *
 *  内容树的构建与"通知 → 动作"的判定在 HistoryWindowLayout（BuildHistoryDatePopupContent /
 *  HistoryDatePopupActionFromNotify），那里有单元测试；本类只负责打开、读取日期、回调宿主。
 *
 *  提交时机：
 *    · 点「跳到这一天」、或在任一输入框里按回车 → 以 kHistoryDatePopupJump 提交。
 *    · 点「只看这段时间」                       → 以 kHistoryDatePopupFilter 提交。
 *    提交后调用方接受（回调返回 true）才关闭；日期有误时弹层保持打开。
 *    · ESC、点弹层外面                          → 关闭，不提交。
 *
 *  本弹层只把年月日交回调用方，不判断日期是否存在（如 2 月 30 日）、也不判断先后 —— 那由调用方
 *  用 CheckHistoryDateRange（HistoryDateRange.h）判断后提示。
 *
 *  典型用法（消息记录窗口）：
 *      m_dateJumpPopup.SetSubmitCallback(&OnDatePopupThunk, this);
 *      m_dateJumpPopup.OpenBelow(buttonScreenRect, from, to, minYear, maxYear);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"
#include "HistoryDateRange.h"   // HistoryDate

namespace balloonwjui { class DuiSpinBox; }

namespace balloonhistory {

/**
 *  CHistoryDateJumpPopup —— 「选日期」弹层。
 */
class CHistoryDateJumpPopup : public balloonwjui::DuiPopupHost
{
public:
    // 提交回调：用户点了两个按钮之一、或在输入框里按回车时触发一次。
    //   userdata：注册时传入的不透明指针，本类只透传、不解释。
    //   action：  用户的动作，取值见 HistoryDatePopupAction（HistoryWindowLayout.h）：
    //             kHistoryDatePopupJump 只用 from；kHistoryDatePopupFilter 用 from 与 to。
    //   from / to：用户选定的开始、结束日期；已限制在各微调框的范围内（月 1~12、日 1~31），
    //              但不保证这一天存在，也不保证开始不晚于结束。
    //   返回：true 表示调用方已接受，弹层随即关闭；false 表示日期有误（调用方已自行提示），
    //         弹层保持打开，用户可以直接改了再提交。
    typedef bool (*SubmitCallback)(void* userdata, int action, const HistoryDate& from,
                                   const HistoryDate& to);

    CHistoryDateJumpPopup();
    ~CHistoryDateJumpPopup();

    /**
     *  注册提交回调。
     *    cb：回调函数指针；NULL 表示取消注册。
     *    userdata：回调时原样带回的不透明指针；本类不持有其生命周期，调用方须保证它活得
     *              比本弹层久（一般传宿主窗口 this）。
     */
    void SetSubmitCallback(SubmitCallback cb, void* userdata);

    /**
     *  在锚点矩形下方打开弹层（右缘与锚点右缘对齐；下方放不下时 DuiPopupHost 自动翻到上方），
     *  并把两组年月日预置为给定日期。
     *    anchorScreen：锚点矩形（屏幕坐标），一般是「选日期」按钮或日期区间标签。
     *    from / to：   预置的开始、结束日期，一般由 ComputeHistoryDatePopupDefaults 算出。
     *    minYear / maxYear：年份微调框的范围，一般从最早一条消息的年份到今年；minYear 大于
     *                       maxYear 时两者交换。
     */
    void OpenBelow(const RECT& anchorScreen, const HistoryDate& from, const HistoryDate& to,
                   int minYear, int maxYear);

    BEGIN_MSG_MAP(CHistoryDateJumpPopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    /**
     *  内容树里的控件发来的 DUI 通知：两个按钮的点击、六个输入框的回车。是否对应某个动作由
     *  HistoryDatePopupActionFromNotify 判定（同时比对通知码与控件编号，见开发约定文档 UI 约定第 4 条）。
     *    lParam：balloonwjui::DuiNotify 的地址，仅在本次同步调用期间有效。
     *    bHandled：出参，未匹配时置 FALSE 让消息继续走基类的处理链。
     *    返回：恒为 0。
     */
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次打开时构建一次内容树，并取回六个微调框的借用指针。重复调用无副作用。
    void BuildContentOnce();

    // 把输入框里手输的数字写回微调框（DuiSpinBox 不会自动同步手输入），并限制在范围内。
    static int CommitTypedValue(balloonwjui::DuiSpinBox* spin);

    // 读出两组年月日并回调；调用方接受才关闭弹层。一次打开只成功提交一次（m_committed 守卫）。
    //   action：用户的动作，取值见 HistoryDatePopupAction。
    void DoSubmit(int action);

private:
    balloonwjui::DuiSpinBox* m_fromYear;    // 「从」年微调框（借用指针，所有权在内容树里）
    balloonwjui::DuiSpinBox* m_fromMonth;   // 「从」月微调框（同上）
    balloonwjui::DuiSpinBox* m_fromDay;     // 「从」日微调框（同上）
    balloonwjui::DuiSpinBox* m_toYear;      // 「到」年微调框（同上）
    balloonwjui::DuiSpinBox* m_toMonth;     // 「到」月微调框（同上）
    balloonwjui::DuiSpinBox* m_toDay;       // 「到」日微调框（同上）
    bool           m_built;                 // 内容树是否已构建（只建一次）
    bool           m_committed;             // 本次打开是否已经成功提交过，防止回车与点击各提交一次
    SubmitCallback m_submitCb;              // 提交回调；NULL 表示未注册
    void*          m_submitUd;              // 提交回调的用户数据，本类只透传
};

} // namespace balloonhistory
