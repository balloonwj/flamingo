/**
 *  flamingoAdmin —— 「共享审计」页（docs/文件共享方案.md 第 10 节）：按人、按动作、按时间查询文件共享的审计记录（只读）。
 *
 *  2026-10-08 起本页是文件共享页（FileShareCenterScreen）的「共享审计」页签（原先是侧栏里单独的一项），页头与「刷新」
 *  属于外层页面。本页自上而下：筛选行（平台超管的企业筛选、时间范围、操作人搜索、动作筛选、共享 ID）、
 *  审计表格（时间、操作人、分享者、共享名、动作、路径、大小、结果、来源）、详情卡（选中记录的全部字段）。
 *  记录有两个来源：服务端亲眼所见的（打开、申请传输、发起、取消、调整成员）与分享者客户端上报的（逐个文件的
 *  传输、新建、重命名、删除、移动）。企业管理员只看本企业；平台超管默认看全部企业，可以按企业筛选。
 *  表格与详情上下排列：九列在左右并排的布局里排不开。
 *
 *  本界面不直接接触网络（见 FileShareUi.h），单元测试见 Tests/FileShareScreensTests.cpp 的 FAU 系列。
 *
 *  典型用法（由 FileShareCenterScreen 建出并转发宿主的设置）：
 *      auto screen = std::make_unique<FileShareAuditScreen>();
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

// 「共享审计」页。
class FileShareAuditScreen : public balloonwjui::DuiVBox
{
public:
    // 审计表格的列下标，与 RowCells 的输出顺序一致
    enum Column
    {
        kColTime   = 0,   // 时间（到分钟）
        kColActor  = 1,   // 操作人（管理员带「（管理员）」）
        kColOwner  = 2,   // 分享者
        kColShare  = 3,   // 共享名
        kColAction = 4,   // 动作
        kColPath   = 5,   // 路径（占满剩余宽度）
        kColSize   = 6,   // 大小
        kColResult = 7,   // 结果（成功 / 失败）
        kColSource = 8,   // 来源
        kColumnCount = 9,
    };

    // 详情网格里各条目的下标（单元测试用）
    enum DetailItem
    {
        kDetailTime     = 0,    // 时间（到秒）
        kDetailActor    = 1,    // 操作人
        kDetailAction   = 2,    // 动作
        kDetailOwner    = 3,    // 分享者
        kDetailShare    = 4,    // 共享名
        kDetailShareId  = 5,    // 共享 ID
        kDetailSize     = 6,    // 大小
        kDetailResult   = 7,    // 结果（含错误原因）
        kDetailSource   = 8,    // 来源
        kDetailAccessId = 9,    // 访问会话 ID
        kDetailPath     = 10,   // 路径（独占一行，折成两行）
        kDetailToPath   = 11,   // 目标：重命名后的新名字或移动的目标目录（独占一行）
        kDetailItemCount = 12,
    };

    // 操作人搜索与共享 ID 输入的防抖定时器 ID，挂在主窗口上（AdminFrame 收到后转给对应的处理函数）。
    // 主窗口已用的定时器号：2 顶栏滚动提示，4、5 敏感词页，6 文件共享页。
    static const UINT_PTR kSearchTimerId  = 7;
    static const UINT_PTR kShareIdTimerId = 8;

    // 每页条数
    static const int kPageSize = 50;

    // 构造时一次性建好整棵控件树；此时还不发请求。
    FileShareAuditScreen();

    // 注入主窗口句柄，用于挂防抖定时器。hwnd 为空时不防抖、立即查询。
    void SetHostWindow(HWND hwnd) { m_hostWnd = hwnd; }

    // 设置请求出口与界面请求处理函数，含义见 FileShareUi.h。user 所有权归调用方。
    void SetRequestSink(fsui::RequestSinkFn fn, void* user) { m_port.SetRequestSink(fn, user); }
    void SetUiRequestHandler(fsui::UiRequestFn fn, void* user) { m_port.SetUiRequestHandler(fn, user); }

    // 每次切换到本页签时由外层的文件共享页调用：按当前登录角色决定是否显示企业筛选，并刷新列表。
    void OnEnter();

    // 刷新（外层页面页头的「刷新」在本页签显示时转到这里）：按当前条件重新拉取。
    void Refresh();

    // 接收转交来的异步接口结果（kApiTagFsAudit / kApiTagFsAuditEnterprises）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 平台超管在企业筛选框里选了一项。index：0 为「全部企业」，其余为企业列表下标加 1；越界时不做任何事。
    void OnEnterpriseSelected(int index);

    // 时间范围变了（kIdFaRange 的 DUIN_VALUECHANGED）。index 取值见 fstext::AuditRange，越界时不做任何事。
    void OnRangeChanged(int index);

    // 操作人搜索框文字变了：停止输入约 400 毫秒后才查询。
    void OnSearchChanged();

    // 操作人搜索的防抖定时器到点：按搜索框当前文字从第 1 页查询。
    void OnSearchTimer();

    // 动作筛选变了（kIdFaAction 的 DUIN_VALUECHANGED）。index：0 为全部动作，1～12 为各动作。
    void OnActionChanged(int index);

    // 共享 ID 输入框文字变了：停止输入约 400 毫秒后才查询。
    void OnShareIdChanged();

    // 共享 ID 的防抖定时器到点：输入框为空或不是正整数时不按共享筛选。
    void OnShareIdTimer();

    // 表格选中行变了（kIdFaTable 的 DUIN_VALUECHANGED）：刷新详情卡。index：行下标，-1 表示没有选中行。
    void OnRowSelected(int index);

    // 翻到第 page 页（分页条回调）。越界或与当前页相同时不做任何事。
    void GoToPage(int page);

    // 一条审计记录在表格里各列的文字（静态函数，便于单元测试），顺序见 Column。
    static void RowCells(const adminapi::FileShareAuditItem& item, std::vector<CString>& cells);

    // 把共享 ID 输入框的文字换算成共享 ID：去掉首尾空白后是正整数时返回该值，否则返回 0（不筛选）。
    static long long ParseShareId(const CString& text);

    // ---- 只读访问（AdminFrame 与单元测试用）----

    bool IsEnterprisePickerVisible() const;
    // 企业筛选框当前显示的文字（单元测试核对「全部企业」或所选企业名）。
    CString EnterprisePickerText() const;
    long long CurrentEnterpriseId() const { return m_enterpriseId; }
    long long CurrentShareIdFilter() const { return m_shareIdFilter; }
    int RowCount() const { return static_cast<int>(m_items.size()); }
    CString OverlayText() const;
    int Page() const { return m_page; }
    int PageCount() const;
    long long SelectedId() const { return m_selectedId; }
    CString DetailTitle() const;

    // 表格、详情网格与两个输入框（单元测试据此模拟输入与核对内容）。
    ui::ActionTable* Table() { return m_table; }
    ui::KeyValueGrid* DetailGrid() { return m_detail; }
    balloonwjui::DuiEdit* SearchBox() { return m_search; }
    balloonwjui::DuiEdit* ShareIdBox() { return m_shareIdBox; }

    // ---- DuiControl 覆写 ----
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

    void ResetState();
    void StartLoadEnterprises();

    // 发起审计列表请求：当前企业、时间范围、操作人、动作、共享 ID、页码。加载中时只记下「待会儿再查一次」。
    void StartLoad();

    void OnEnterprisesLoaded(const HttpResult& http);
    void OnAuditLoaded(const HttpResult& http);

    // 按加载状态与数据刷新表格与分页条。
    void UpdateView();

    // 按选中的记录刷新详情卡。
    void UpdateDetail();

    const adminapi::FileShareAuditItem* SelectedItem() const;
    void SyncHeaderInset();

    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    static void OnPageThunk(void* user, int newPage);

    // ---- 外部接口 ----
    HWND                      m_hostWnd;      // 主窗口句柄（防抖定时器挂在它上面），由 AdminFrame 注入
    fsui::Port                m_port;         // 请求出口与界面请求的转发端口

    // ---- 子控件借用指针（所有权在控件树中）----
    balloonwjui::DuiControl*  m_pickerBox;    // 企业筛选框所在的那一段（平台超管才显示）
    balloonwjui::DuiComboBox* m_picker;       // 企业筛选框
    balloonwjui::DuiComboBox* m_rangeBox;     // 时间范围下拉框
    balloonwjui::DuiEdit*     m_search;       // 操作人搜索框
    balloonwjui::DuiComboBox* m_actionBox;    // 动作筛选下拉框
    balloonwjui::DuiEdit*     m_shareIdBox;   // 共享 ID 输入框
    ui::ColumnHeader*         m_header;       // 审计表头
    ui::ActionTable*          m_table;        // 审计表格（没有行内操作）
    ui::PagerBar*             m_pager;        // 分页条
    balloonwjui::DuiLabel*    m_detailTitle;  // 详情卡标题
    ui::KeyValueGrid*         m_detail;       // 详情卡的信息网格

    // ---- 状态 ----
    bool                                   m_isPlatform;      // 进入本界面时的角色是否为平台超管
    CString                                m_sessionToken;    // 上次进入时的登录令牌，用来发现换了身份
    std::vector<long long>                 m_enterpriseIds;   // 企业筛选框第 2 项起各项对应的企业 ID
    long long                              m_enterpriseId;    // 当前企业筛选，0 表示全部企业
    int                                    m_range;           // 时间范围，取值见 fstext::AuditRange
    CString                                m_keyword;         // 操作人搜索文字
    int                                    m_action;          // 动作筛选，0 为全部
    long long                              m_shareIdFilter;   // 共享 ID 筛选，0 为不限
    int                                    m_page;            // 当前页码，从 1 起
    int                                    m_total;           // 满足条件的总条数
    int                                    m_loadState;       // 加载状态，取值见 LoadState
    CString                                m_loadError;       // 最近一次加载失败的原因（中文）
    bool                                   m_reloadPending;   // 加载中又有新的查询条件，结果回来后补查一次
    std::vector<adminapi::FileShareAuditItem> m_items;        // 本页记录
    long long                              m_selectedId;      // 选中的记录 ID，0 表示没有选中
};

} // namespace flamingoadmin
