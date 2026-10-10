/**
 *  FlamingoLink.dll —— 「在线用户」页面窗口。
 *
 *  是控制台主窗口的子窗口（WS_CHILD），里面再挂一个 balloonui 宿主（DuiHost）画整页内容：
 *  顶部一行是名单汇总、连接状态与搜索框，下面一张白色卡片是表头与表格（每台设备一行）。
 *  之所以在 DuiHost 外再包一层本窗口：DuiHost 把控件通知（WM_DUI_NOTIFY）发给它的父窗口，
 *  若直接把 DuiHost 挂在控制台主窗口下，这些通知会跑到控制台那里去；有了本窗口，通知与
 *  定时器都留在 DLL 里处理。
 *
 *  页面只负责显示，数据由 IOnlineViewSource（LinkModule）提供；数据变化后 LinkModule 调 Refresh。
 *  只在界面线程上使用。
 *
 *  典型用法：
 *      COnlinePage page;
 *      page.CreatePage(hwndParent, &module);
 *      page.Refresh();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <vector>

#include "DuiHost.h"
#include "DuiNotify.h"
#include "FlamingoLink/Core/OnlineTable.h"

namespace balloonwjui { class DuiLabel; class DuiEdit; }
namespace flamingoadmin { namespace ui { class ColumnHeader; class ActionTable; } }

namespace flamingolink {

/**
 *  页面显示需要的全部数据，由 IOnlineViewSource 填写。
 */
struct OnlineViewState
{
    OnlineViewState()
        : m_table(NULL), m_hasEnterprise(false), m_state(0), m_attempt(0), m_delayMs(0)
        , m_waitStartTick(0), m_lastError(0), m_received(false)
    {
    }

    const OnlineTable* m_table;          //在线名单，归 LinkModule 所有；不得为 NULL
    bool               m_hasEnterprise;  //是否设置了企业
    CString            m_enterpriseName; //企业名称，可为空
    int                m_state;          //连接状态，取值见 enum LinkState
    int                m_attempt;        //等待重连时是第几次重连
    int                m_delayMs;        //等待重连时这次要等的毫秒数
    ULONGLONG          m_waitStartTick;  //进入等待时的 GetTickCount64
    int                m_lastError;      //最近一次订阅失败的错误码，0 表示没有
    bool               m_received;       //当前企业是否已收到过完整名单
};

/**
 *  页面数据的提供方（LinkModule 实现）。
 */
class IOnlineViewSource
{
public:
    virtual ~IOnlineViewSource() {}

    /**
     *  取页面显示需要的数据。
     *  @param out [出参]
     */
    virtual void GetViewState(OnlineViewState& out) = 0;
};

/**
 *  「在线用户」页面窗口。
 */
class COnlinePage : public CWindowImpl<COnlinePage>
{
public:
    DECLARE_WND_CLASS(_T("FlamingoLinkOnlinePage"))

    COnlinePage();
    virtual ~COnlinePage();

    COnlinePage(const COnlinePage&) = delete;
    COnlinePage& operator=(const COnlinePage&) = delete;

    /**
     *  创建页面窗口（初始隐藏）。
     *  @param parent  父窗口（控制台主窗口）
     *  @param source  数据提供方，生命期须覆盖本窗口；所有权归调用方
     *  @return 成功返回 true
     */
    bool CreatePage(HWND parent, IOnlineViewSource* source);

    /**
     *  按数据提供方的当前数据刷新整页：汇总、连接状态、表格行与提示文字。
     */
    void Refresh();

    BEGIN_MSG_MAP(COnlinePage)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
    END_MSG_MAP()

private:
    //建出 DuiHost 与整棵控件树
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL& bHandled);
    //DuiHost 铺满本窗口
    LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL& bHandled);
    //背景全由 DuiHost 画，这里不擦除，避免闪烁
    LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL& bHandled);
    //等待重连期间每秒刷新一次倒计时
    LRESULT OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled);
    //停掉倒计时定时器
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled);
    //控件通知：搜索框文字变化时重新筛选
    LRESULT OnDuiNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled);

    //建控件树
    std::unique_ptr<balloonwjui::DuiControl> BuildUi();

    //只刷新连接状态文字（倒计时用）
    void RefreshStatus(const OnlineViewState& vs);

    //按搜索框重新筛选并刷新表格与提示文字
    void RefreshRows(const OnlineViewState& vs);

    //表格绘行回调的转接
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);

    //画一行
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);

    //表格悬停提示的转接：鼠标停在部门一列时显示完整部门
    static CString TooltipThunk(void* user, POINT pt);

    balloonwjui::DuiHost              m_host;        //画整页内容的 balloonui 宿主（本窗口的子窗口）
    IOnlineViewSource*                m_source;      //数据提供方，归调用方所有
    balloonwjui::DuiLabel*            m_summary;     //名单汇总文字（控件树持有）
    balloonwjui::DuiLabel*            m_status;      //连接状态文字（控件树持有）
    balloonwjui::DuiEdit*             m_search;      //搜索框（控件树持有）
    flamingoadmin::ui::ColumnHeader*  m_header;      //表头（控件树持有）
    flamingoadmin::ui::ActionTable*   m_table;       //表格（控件树持有）
    std::vector<int>                  m_view;        //当前显示的行在 OnlineTable::Rows() 里的下标
    const OnlineTable*                m_model;       //上一次刷新时的名单，绘行回调据此取数据；归 LinkModule 所有
    bool                              m_dimmed;      //连接不正常时用灰色画表格行（保留最后一份数据）
    bool                              m_countdownOn; //倒计时定时器是否在运行
};

} // namespace flamingolink
