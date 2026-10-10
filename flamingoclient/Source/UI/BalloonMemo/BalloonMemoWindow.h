/**
 *  BalloonMemoWindow.h
 *
 *  本人的备忘窗口：主面板系统菜单「备忘与提醒」打开（docs/备忘与提醒方案.md 1.1；2026-10-03 起入口从系统设置移到
 *  系统菜单）。左边是月历（翻月、回到今天，今天与选中日高亮，有备忘的日子带圆点），右边是选中那天的备忘卡片，
 *  「＋ 添加备忘」在选中的那天新建。个人备忘的卡片悬停时显示「修改」「删除」，双击修改，右键弹出「修改 / 删除」
 *  菜单。被提醒的群备忘也列在这里、带所属群名与创建人，只能查看，修改要到群聊窗的「备忘」页签里去。
 *
 *  由备忘中枢持有，同一时刻只有一个；数据全部取自中枢的备忘簿，窗口自己不缓存。登记为中枢的观察者，
 *  收到 FMG_MSG_MEMO_CHANGED 时重绘月历与卡片。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Session/UserSessionData.h"   // FMG_MSG_MEMO_CHANGED
#include "MemoViewModel.h"                    // kCalendarCells
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

namespace balloonwjui { class DuiLabel; class DuiButton; class DuiScrollView; class DuiComboBox; }
namespace balloonmemo { class CMemoDayButton; }

/**
 *  本人的备忘窗口。
 */
class CBalloonMemoWindow : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonMemoWindow();
    ~CBalloonMemoWindow();

    /**
     *  打开窗口：没开着时新建，显示今天所在的月份并选中今天；已开着时调到前台，保留当前的月份与选中日。
     *    owner：发起打开的窗口（主面板），新建时居中摆在它上面；可为 NULL。
     *  @return 窗口已显示返回 true
     */
    bool Open(HWND owner);

    /**
     *  关闭窗口（注销时中枢调用）。
     */
    void CloseWindow();

    //卡片上的操作（「修改」「删除」、双击、右键）先投给自己、等当前的控件通知处理完再做：打开对话框或弹菜单
    //期间备忘簿若有变化会重建卡片，发出通知的那个控件会被销毁。wParam 为 MemoCardAction，备忘 ID 记在
    //m_pendingMemoId。窗口私有消息，偏移见开发约定文档 UI 约定第 6 条
    static const UINT kMsgMemoCardAction = WM_APP + 0xA2;

    BEGIN_MSG_MAP(CBalloonMemoWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(FMG_MSG_MEMO_CHANGED, OnMemoChanged)
        MESSAGE_HANDLER(kMsgMemoCardAction, OnCardActionMsg)
        //卡片的悬停按鼠标位置自己判（理由见 MemoCardList.h）。这两个处理器只旁听、不消费：处理完置
        //bHandled=FALSE，让 DuiFrameWindow / DuiHost 照常处理鼠标
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMoveMsg)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeaveMsg)
        //销毁时取消在中枢的登记，只旁听、不消费
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //卡片上的操作，作为 kMsgMemoCardAction 的 wParam
    enum MemoCardAction
    {
        kCardActionOpen   = 1,   //「修改」或双击：个人备忘打开修改，群备忘打开查看
        kCardActionDelete = 2,   //「删除」：确认后删除
        kCardActionMenu   = 3,   //右键：在光标处弹出「修改 / 删除」菜单
    };

    bool BuildWindow(HWND owner);
    void CaptureControls();

    //按当前的年月、选中日与备忘簿重画月历、日期标题与当天的卡片
    void Refresh();

    //选中某一天；不在当前显示的月份时切过去
    void SelectDay(int64_t day);

    //切到某年某月（年、月下拉框与「‹」「›」共用）：选中日保持原来的「几号」，那个月没有这一天时取最后一天；
    //年份超出 kCalendarYearMin～kCalendarYearMax 时什么都不做
    void GoToMonth(int year, int month);

    //按今年与当前显示的年月刷新年、月两个下拉框：年份的范围需要扩大时重新填项（只扩不缩），选中项不同时
    //改选（不发通知）
    void RefreshMonthCombos();

    //「＋ 添加备忘」：在选中的那天新建
    void OnAdd();

    //记下第 cardIndex 张卡片的备忘 ID，投一条 kMsgMemoCardAction；序号无效时什么都不做
    void PostCardAction(int action, int cardIndex);

    //打开编辑对话框：个人备忘可修改，群备忘只能查看
    void OpenMemo(int64_t memoid);

    //卡片的右键菜单
    void ShowCardMenu(int64_t memoid);

    //卡片重建之后按当前光标位置补一次悬停判定（光标停着不动时不会有 WM_MOUSEMOVE）
    void RefreshHoverAtCursor();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMemoChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCardActionMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseLeaveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    int     m_year;          //当前显示的年
    int     m_month;         //当前显示的月，1～12
    int64_t m_selectedDay;   //选中的日子（从 1970-01-01 起的天数）

    int     m_comboYearFirst;   //年份下拉框第一项的年份；0 表示还没填过项
    int     m_comboYearLast;    //年份下拉框最后一项的年份

    std::vector<int64_t> m_cardMemoIds;   //各卡片对应的备忘 ID，下标即卡片序号；Refresh 时整体替换
    int64_t              m_pendingMemoId; //投出 kMsgMemoCardAction 时记下的备忘 ID，处理时取出并清零

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiComboBox*      m_pYearCombo;    //年份下拉框
    balloonwjui::DuiComboBox*      m_pMonthCombo;   //月份下拉框
    balloonwjui::DuiLabel*         m_pDayTitle;     //右侧日期标题
    balloonwjui::DuiLabel*         m_pDayHint;      //右侧条数提示，拉取超时时改为「没有加载出来」
    balloonwjui::DuiScrollView*    m_pList;         //当天的备忘卡片（MemoCardList）
    balloonwjui::DuiButton*        m_pAdd;          //「＋ 添加备忘」：拉取超时时置灰
    balloonmemo::CMemoDayButton*   m_pCells[balloonmemo::kCalendarCells];   //42 个日期格

    HBITMAP m_hTitleIcon;   //标题栏左侧图标位图；SetIcon 只借用，本类持有、析构时释放
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;
};
