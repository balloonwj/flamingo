/**
 *  flamingoAdmin —— 「文件共享」页（docs/文件共享方案.md 第 10 节）：列出企业成员发起的文件共享，查看成员与权限，
 *  必要时在后台强制取消。
 *
 *  2026-10-08 起本页是文件共享页（FileShareCenterScreen）的「文件共享」页签，页头与「刷新」属于外层页面。
 *  本页自上而下：筛选行（平台超管的企业筛选、分享者搜索、状态筛选）、共享表格（分享者、
 *  显示路径、所在会话、成员数、状态、发起时间，有效的共享行尾有「强制取消」）、详情卡（左边是选中共享的信息，
 *  右边是成员与权限）。企业管理员只看本企业的共享；平台超管默认看全部企业，可以按企业筛选。
 *  表格与详情上下排列而不是左右并排：表格六列加操作列在最小窗口宽度下才排得开。
 *
 *  本界面不直接接触网络与对话框（见 FileShareUi.h），单元测试见 Tests/FileShareScreensTests.cpp 的 FS 系列。
 *
 *  典型用法（由 FileShareCenterScreen 建出并转发宿主的设置）：
 *      auto screen = std::make_unique<FileSharesScreen>();
 *      screen->SetHostWindow(m_hWnd);
 *      screen->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
 *      screen->SetUiRequestHandler(&AdminFrame::FileShareUiRequestThunk, this);
 *      //切到本页签时
 *      screen->OnEnter();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Screens/FileShareUi.h"

#include <vector>

namespace balloonwjui { class DuiEdit; class DuiComboBox; class DuiLabel; }

namespace flamingoadmin {

namespace ui { class ActionTable; class PagerBar; class ColumnHeader; class KeyValueGrid; }

// 「文件共享」页。
class FileSharesScreen : public balloonwjui::DuiVBox, public fsui::ConfirmTarget
{
public:
    // 共享表格的列下标，与 RowCells 的输出顺序一致（操作列由表格自己画，不在其中）
    enum Column
    {
        kColOwner        = 0,   // 分享者：昵称（账号）
        kColPath         = 1,   // 显示路径（占满剩余宽度）
        kColConversation = 2,   // 所在会话
        kColMembers      = 3,   // 成员数
        kColState        = 4,   // 状态
        kColCreateTime   = 5,   // 发起时间
        kColumnCount     = 6,
    };

    // 行内操作的下标
    enum RowAction
    {
        kActionCancel = 0,   // 强制取消（只对有效的共享显示）
    };

    // 状态筛选下拉框的下标，数值与 fileshare/list.do 的 state 取值相同
    enum StateFilter
    {
        kFilterAll      = 0,   // 全部状态
        kFilterActive   = 1,   // 有效（默认）
        kFilterCanceled = 2,   // 已取消
    };

    // 分享者搜索的防抖定时器 ID，挂在主窗口上（AdminFrame 收到这个 ID 的 WM_TIMER 后转给 OnSearchTimer）。
    // 主窗口已用的定时器号：2 顶栏滚动提示，4、5 敏感词页的两个搜索框。
    static const UINT_PTR kSearchTimerId = 6;

    // 每页条数
    static const int kPageSize = 50;

    // 构造时一次性建好整棵控件树；此时还不发请求。
    FileSharesScreen();

    // 注入主窗口句柄，用于挂搜索防抖定时器。hwnd 为空时搜索不防抖、立即查询。
    void SetHostWindow(HWND hwnd) { m_hostWnd = hwnd; }

    // 设置请求出口与界面请求处理函数，含义见 FileShareUi.h。user 所有权归调用方。
    void SetRequestSink(fsui::RequestSinkFn fn, void* user) { m_port.SetRequestSink(fn, user); }
    void SetUiRequestHandler(fsui::UiRequestFn fn, void* user) { m_port.SetUiRequestHandler(fn, user); }

    // 每次切换到本页签时由外层的文件共享页调用：按当前登录角色决定是否显示企业筛选，并刷新列表。
    void OnEnter();

    // 刷新（外层页面页头的「刷新」在本页签显示时转到这里）：按当前条件重新拉取列表与选中共享的成员。
    void Refresh();

    // 接收转交来的异步接口结果（kApiTagFsList / kApiTagFsMembers / kApiTagFsCancel / kApiTagFsEnterprises）。
    //   tag：结果标签。http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 平台超管在企业筛选框里选了一项（kIdFsEnterprise 的 DUIN_VALUECHANGED）。
    //   index：0 为「全部企业」，其余为企业列表里的下标加 1；越界时不做任何事。
    void OnEnterpriseSelected(int index);

    // 分享者搜索框文字变了（kIdFsSearch 的 DUIN_VALUECHANGED）：停止输入约 400 毫秒后才查询。
    void OnSearchChanged();

    // 搜索防抖定时器到点（kSearchTimerId 的 WM_TIMER）：按搜索框当前文字从第 1 页查询。
    void OnSearchTimer();

    // 状态筛选变了（kIdFsState 的 DUIN_VALUECHANGED）。index 取值见 StateFilter，越界时不做任何事。
    void OnStateChanged(int index);

    // 表格选中行变了（kIdFsTable 的 DUIN_VALUECHANGED）：刷新详情卡并拉取该共享的成员。
    //   index：行下标；-1 表示没有选中行。
    void OnRowSelected(int index);

    // 翻到第 page 页（分页条回调）。越界或与当前页相同时不做任何事。
    void GoToPage(int page);

    // 点了某行的「强制取消」：请宿主弹确认框，确认后提交。行越界或共享不是有效状态时不做任何事。
    void OnCancelClicked(int rowIndex);

    // 确认框的结果（fsui::ConfirmTarget）：确认则提交强制取消。
    void OnConfirmResult(bool confirmed) override;

    // 一个共享在表格里各列的文字（静态函数，便于单元测试），顺序见 Column。
    //   item：共享。cells：[出参] 各列文字，调用前会被清空。
    static void RowCells(const adminapi::FileShareItem& item, std::vector<CString>& cells);

    // 强制取消确认框的正文（静态函数，便于单元测试）。
    static CString CancelConfirmText(const adminapi::FileShareItem& item);

    // ---- 只读访问（AdminFrame 与单元测试用）----

    // 企业筛选框是否显示（只有平台超管显示）。
    bool IsEnterprisePickerVisible() const;

    // 企业筛选框当前显示的文字（单元测试核对「全部企业」或所选企业名）。
    CString EnterprisePickerText() const;

    // 当前企业筛选：平台超管为所选企业，0 表示全部企业；企业管理员恒为 0（服务端按本企业处理）。
    long long CurrentEnterpriseId() const { return m_enterpriseId; }

    // 表格的行数、覆盖提示、页码与总页数。
    int RowCount() const { return static_cast<int>(m_shares.size()); }
    CString OverlayText() const;
    int Page() const { return m_page; }
    int PageCount() const;

    // 当前选中的共享 ID；没有选中时为 0。
    long long SelectedShareId() const { return m_selectedShareId; }

    // 详情卡标题与成员表格的行数、覆盖提示。
    CString DetailTitle() const;
    int MemberRowCount() const { return static_cast<int>(m_members.size()); }
    CString MemberOverlayText() const;

    // 共享表格、详情卡的信息网格与成员表格（单元测试据此模拟点击与核对内容）。
    ui::ActionTable* Table() { return m_table; }
    ui::KeyValueGrid* DetailGrid() { return m_detail; }
    ui::ActionTable* MemberTable() { return m_memberTable; }
    balloonwjui::DuiEdit* SearchBox() { return m_search; }

    // 详情网格里各条目的下标（单元测试用）。
    enum DetailItem
    {
        kDetailOwner        = 0,   // 分享者
        kDetailConversation = 1,   // 所在会话
        kDetailKind         = 2,   // 共享类型
        kDetailState        = 3,   // 状态
        kDetailCreateTime   = 4,   // 发起时间
        kDetailCancelTime   = 5,   // 取消时间
        kDetailCancelReason = 6,   // 取消原因
        kDetailEnterprise   = 7,   // 所属企业
        kDetailPath         = 8,   // 显示路径（独占一行）
        kDetailItemCount    = 9,
    };

    // ---- DuiControl 覆写 ----
    // 排版：先按基类排好子控件，再让表头让出表格滚动条的宽度。
    void Layout(const RECT& rcAvail) override;

private:
    // 加载状态
    enum LoadState
    {
        kLoadIdle    = 0,   // 尚未加载
        kLoadLoading = 1,   // 请求已发出，等待应答
        kLoadLoaded  = 2,   // 已成功载入
        kLoadFailed  = 3,   // 加载失败
    };

    // 把界面恢复到「刚进入、还没有数据」的状态（换了登录身份时调用）。
    void ResetState();

    // 发起企业列表请求（平台超管用来填企业筛选框）。
    void StartLoadEnterprises();

    // 发起共享列表请求。加载中时只记下「待会儿再查一次」。
    void StartLoadShares();

    // 发起选中共享的成员请求。
    void StartLoadMembers();

    // 各接口的应答。
    void OnEnterprisesLoaded(const HttpResult& http);
    void OnSharesLoaded(const HttpResult& http);
    void OnMembersLoaded(const HttpResult& http);
    void OnCancelDone(const HttpResult& http);

    // 按加载状态与数据刷新表格、分页条与筛选行。
    void UpdateView();

    // 按选中的共享刷新详情卡。
    void UpdateDetail();

    // 当前选中的共享；没有时返回 nullptr。
    const adminapi::FileShareItem* SelectedShare() const;

    // 企业 ID 对应的企业名；企业筛选框里没有时返回企业 ID 的文字。
    CString EnterpriseName(long long enterpriseId) const;

    // 让表头右侧让出与表格滚动条相同的宽度。
    void SyncHeaderInset();

    // 表格回调
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    static void OnRowActionThunk(void* user, int rowIndex, int actionIndex);
    static bool IsActionVisibleThunk(void* user, int rowIndex, int actionIndex);
    static void OnPageThunk(void* user, int newPage);
    static void PaintMemberRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                    bool hover);
    void PaintMemberRow(HDC hdc, int rowIndex, const RECT& rowRect);

    // ---- 外部接口 ----
    HWND                      m_hostWnd;        // 主窗口句柄（搜索防抖定时器挂在它上面），由 AdminFrame 注入
    fsui::Port                m_port;           // 请求出口与界面请求的转发端口

    // ---- 子控件借用指针（所有权在控件树中）----
    balloonwjui::DuiControl*  m_pickerBox;      // 企业筛选框所在的那一段（平台超管才显示）
    balloonwjui::DuiComboBox* m_picker;         // 企业筛选框
    balloonwjui::DuiEdit*     m_search;         // 分享者搜索框
    balloonwjui::DuiComboBox* m_stateBox;       // 状态筛选下拉框
    ui::ColumnHeader*         m_header;         // 共享表头
    ui::ActionTable*          m_table;          // 共享表格
    ui::PagerBar*             m_pager;          // 分页条
    balloonwjui::DuiLabel*    m_detailTitle;    // 详情卡标题（共享名）
    ui::KeyValueGrid*         m_detail;         // 详情卡的信息网格
    balloonwjui::DuiLabel*    m_memberTitle;    // 「成员与权限（N）」
    ui::ActionTable*          m_memberTable;    // 成员表格

    // ---- 状态 ----
    bool                                     m_isPlatform;      // 进入本界面时的角色是否为平台超管
    CString                                  m_sessionToken;    // 上次进入时的登录令牌，用来发现换了身份
    std::vector<long long>                   m_enterpriseIds;   // 企业筛选框第 2 项起各项对应的企业 ID
    std::vector<CString>                     m_enterpriseNames; // 与 m_enterpriseIds 一一对应的企业名
    long long                                m_enterpriseId;    // 当前企业筛选，0 表示全部企业
    CString                                  m_keyword;         // 当前分享者搜索文字
    int                                      m_stateFilter;     // 当前状态筛选，取值见 StateFilter
    int                                      m_page;            // 当前页码，从 1 起
    int                                      m_total;           // 满足条件的总条数
    int                                      m_loadState;       // 列表的加载状态，取值见 LoadState
    CString                                  m_loadError;       // 最近一次加载失败的原因（中文）
    bool                                     m_reloadPending;   // 加载中又有新的查询条件，结果回来后补查一次
    std::vector<adminapi::FileShareItem>     m_shares;          // 本页共享
    long long                                m_selectedShareId; // 选中的共享 ID，0 表示没有选中
    std::vector<adminapi::FileShareMemberItem> m_members;       // 选中共享的成员
    int                                      m_membersInFlight; // 已发出、尚未返回的成员请求个数（只认最后一个的应答）
    int                                      m_memberLoadState; // 成员的加载状态，取值见 LoadState
    CString                                  m_memberError;     // 成员加载失败的原因（中文）
    adminapi::FileShareItem                  m_pendingCancel;   // 等待确认强制取消的共享；m_shareId 为 0 表示没有
    bool                                     m_cancelling;      // 强制取消的请求已发出、尚未返回
};

} // namespace flamingoadmin
