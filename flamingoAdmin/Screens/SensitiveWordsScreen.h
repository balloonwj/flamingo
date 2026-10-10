/**
 *  flamingoAdmin —— 敏感词管理界面（docs/敏感词方案.md 第 7 节）。
 *
 *  页头之下是「词库」「拦截记录」两个页签：词库页签维护企业的敏感词（列表、搜索、分类筛选、分页、
 *  添加 / 编辑 / 删除、总开关），拦截记录页签查看服务端拦下的内容（只读）。平台超级管理员先在
 *  页面顶部选择企业，企业管理员只看本企业。
 *
 *  本界面不直接接触网络与对话框，便于单元测试（Tests/SensitiveWordsScreenTests.cpp）：
 *    - 要发的请求交给「请求出口」（SetRequestSink）。AdminFrame 把它接到 HttpWorker，结果按标签
 *      （kApiTagSw*）转交 OnApiResult；测试里换成记录下来。
 *    - 要弹的弹窗、确认框与提示交给「界面请求」处理函数（SetUiRequestHandler）。AdminFrame 负责弹出，
 *      用户在确认框里的选择经 OnConfirmResult 告诉本界面，弹窗里的提交经 SubmitWord 交给本界面。
 *
 *  典型用法（AdminFrame 内）：
 *      auto screen = std::make_unique<SensitiveWordsScreen>();
 *      screen->SetHostWindow(m_hWnd);
 *      screen->SetRequestSink(&AdminFrame::PostRequestThunk, this);
 *      screen->SetUiRequestHandler(&AdminFrame::SensitiveUiRequestThunk, this);
 *      //切到本界面时
 *      m_sensitiveWordsScreen->OnEnter();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Util/AdminApi.h"
#include "Util/SensitiveWordText.h"

#include <vector>

namespace balloonwjui { class DuiEdit; class DuiComboBox; class DuiSwitch; class DuiLabel; class DuiButton; }

namespace flamingoadmin {

namespace ui { class SegControl; class ActionTable; class PagerBar; class ColumnHeader; }

// 敏感词管理界面。整体是一个纵向布局：页头 + 页签 + 当前页签的内容区。
class SensitiveWordsScreen : public balloonwjui::DuiVBox
{
public:
    // 页签下标
    enum Tab
    {
        kTabWords = 0,   // 「词库」页签
        kTabHits  = 1,   // 「拦截记录」页签
    };

    // 本界面请宿主（AdminFrame）做的事
    enum UiRequestKind
    {
        kUiOpenAddModal   = 1,   // 打开「添加敏感词」弹窗
        kUiOpenEditModal  = 2,   // 打开「编辑敏感词」弹窗，UiRequest::m_word 为要编辑的词条
        kUiConfirmDelete  = 3,   // 弹确认框问是否删除 UiRequest::m_word；宿主随后调用 OnConfirmResult
        kUiConfirmDisable = 4,   // 弹确认框问是否关闭总开关；宿主随后调用 OnConfirmResult
        kUiCloseModal     = 5,   // 弹窗里的提交已成功，关闭弹窗
        kUiModalError     = 6,   // 弹窗里的提交失败：在弹窗里显示 UiRequest::m_text，弹窗不关
        kUiToast          = 7,   // 显示一条提示 UiRequest::m_text；m_isError 为 true 时按错误提示显示
        kUiOpenImportModal = 8,  // 打开「批量导入」弹窗
        kUiImportProgress = 9,   // 导入进行中：在导入弹窗里显示进度 UiRequest::m_text
        kUiImportDone     = 10,  // 导入结束：在导入弹窗里显示汇总 UiRequest::m_text；m_isError 为 true 表示中途失败
        kUiSaveExport     = 11,  // 导出的内容已取到：弹「另存为」对话框，预填文件名 m_text，写入 m_bytes
    };

    // 一次界面请求
    struct UiRequest
    {
        int                          m_kind;     // 请求种类，取值见 UiRequestKind
        adminapi::SensitiveWordItem  m_word;     // 相关的词条（编辑、删除时有效）
        CString                      m_text;     // 提示或错误文字（kUiModalError、kUiToast 时有效）
        bool                         m_isError;  // kUiToast、kUiImportDone 时是否按错误显示
        std::string                  m_bytes;    // kUiSaveExport 时要写进文件的字节（已带 UTF-8 BOM、换行为 CRLF）

        UiRequest()
            : m_kind(0), m_isError(false)
        {
        }
    };

    // 请求出口：把一个已构造好的请求发出去。请求按空的 ServerConfig 构造，实现方补上服务器地址、
    // 回传窗口与消息后交给 HttpWorker（本界面因此不依赖 AppConfig，单元测试里也不读配置文件）。
    //   user：SetRequestSink 时传入的指针。request：请求，m_tag 已填好。
    typedef void (*RequestSinkFn)(void* user, HttpRequest& request);

    // 界面请求的处理函数。
    //   user：SetUiRequestHandler 时传入的指针。request：请求内容，仅本次调用内有效。
    typedef void (*UiRequestFn)(void* user, const UiRequest& request);

    // 「拦截记录」页签的时间范围（时间范围下拉框的下标）
    enum HitRange
    {
        kHitRange7Days   = 0,   // 近 7 天（默认）
        kHitRange30Days  = 1,   // 近 30 天
        kHitRange180Days = 2,   // 近 180 天（拦截记录只保留 180 天）
    };

    // 「拦截记录」表格的列下标，与 HitRowCells 的输出顺序一致
    enum HitColumn
    {
        kHcTime     = 0,   // 拦截时间
        kHcSender   = 1,   // 发送者
        kHcSource   = 2,   // 来源
        kHcTarget   = 3,   // 会话
        kHcWord     = 4,   // 命中词
        kHcCategory = 5,   // 分类
        kHcSnippet  = 6,   // 内容摘要（占满剩余宽度）
        kHitColumnCount = 7,
    };

    // 「拦截记录」发送者搜索的防抖定时器 ID，同样挂在主窗口上（AdminFrame 转给 OnHitSearchTimer）。
    static const UINT_PTR kHitSearchTimerId = 5;

    // 搜索防抖用的定时器 ID，挂在主窗口上（AdminFrame 收到这个 ID 的 WM_TIMER 后转给 OnSearchTimer）。
    // 主窗口自己的定时器只用了 2 号；balloonui 的控件用线程定时器或自己的窗口，不占用主窗口的定时器号。
    static const UINT_PTR kSearchTimerId = 4;

    // 每页条数
    static const int kPageSize = 50;

    // 构造时一次性建好整棵控件树；此时还不发请求。
    SensitiveWordsScreen();

    // 注入主窗口句柄，用于挂搜索防抖定时器。
    //   hwnd：主窗口句柄，生命周期长于本界面；为空时搜索不防抖、立即查询。
    void SetHostWindow(HWND hwnd) { m_hostWnd = hwnd; }

    // 设置请求出口。fn 为空时请求被丢弃（记一条日志）。user 所有权归调用方。
    void SetRequestSink(RequestSinkFn fn, void* user);

    // 设置界面请求的处理函数。fn 为空时界面请求被丢弃。user 所有权归调用方。
    void SetUiRequestHandler(UiRequestFn fn, void* user);

    // 每次切换到本界面时由 AdminFrame 调用：按当前登录角色决定界面形态，并刷新数据。
    // 角色在这里而不是在构造时判断：界面只在第一次登录时建一次，同一进程里换角色重新登录时不会重建。
    void OnEnter();

    // 接收转交来的异步接口结果。
    //   tag：结果标签（kApiTagSw*）。http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 页签切换（kIdSwTabs 的 DUIN_VALUECHANGED）。index 取值见 Tab，越界时不做任何事。
    void OnTabChanged(int index);

    // 平台超管在企业选择框里选了一项（kIdSwEnterprise 的 DUIN_VALUECHANGED）。
    //   index：选择框里的下标；越界时不做任何事。
    void OnEnterpriseSelected(int index);

    // 搜索框文字变了（kIdSwSearch 的 DUIN_VALUECHANGED）：停止输入约 400 毫秒后才查询。
    void OnSearchChanged();

    // 搜索防抖定时器到点（kSearchTimerId 的 WM_TIMER）：按搜索框当前文字从第 1 页查询。
    void OnSearchTimer();

    // 分类筛选变了（kIdSwCategory 的 DUIN_VALUECHANGED）。
    //   index：下拉框下标，0 为「全部分类」，1～6 为各分类。
    void OnCategoryChanged(int index);

    // 用户拨动了总开关（kIdSwSwitch 的 DUIN_VALUECHANGED）。打开立即提交；关闭先请宿主弹确认框。
    //   on：开关拨动后的状态。
    void OnSwitchToggled(bool on);

    // 用户点了「添加」按钮（kIdSwAdd 的 DUIN_CLICK）：请宿主打开添加弹窗。
    void OnAddClicked();

    // 宿主把用户在确认框里的选择告诉本界面。
    //   kind：当初请求的种类（kUiConfirmDelete 或 kUiConfirmDisable）。confirmed：是否点了确认。
    void OnConfirmResult(int kind, bool confirmed);

    // 弹窗里点了「保存」：提交添加或修改。结果经 kUiCloseModal 或 kUiModalError 告诉宿主。
    //   id：词条 ID，0 表示添加。word / category / remark：弹窗里填的内容。
    void SubmitWord(long long id, const CString& word, int category, const CString& remark);

    // 用户点了「批量导入」（kIdSwImport 的 DUIN_CLICK）：请宿主打开导入弹窗。平台超管未选企业时不做任何事。
    void OnImportClicked();

    // 导入弹窗里点了「开始导入」：把 text 切成行，按每批 1000 行依次提交 import.do（一批返回后再发下一批）。
    // 进度经 kUiImportProgress、结果经 kUiImportDone 告诉宿主；结束后（含中途失败）从第 1 页重新拉列表。
    //   text：弹窗里的全文。defaultCategory：只有词条、或分类名写错的行归入的分类，1～6，越界按「其他」。
    //   error：[出参，可为 NULL] 没有开始时的原因（没有可导入的内容、上一次导入尚未结束、未选企业）。
    //   返回：已开始提交返回 true。
    bool StartImport(const CString& text, int defaultCategory, CString* error);

    // 是否正在导入（还有批次没有返回）。
    bool IsImporting() const { return m_importing; }

    // 用户点了「导出」（kIdSwExport 的 DUIN_CLICK）：拉取本企业全部词条，取到后经 kUiSaveExport 请宿主存盘；
    // 词库为空时只提示、不弹存盘框。平台超管未选企业、或上一次导出尚未返回时不做任何事。
    void OnExportClicked();

    // ---- 「拦截记录」页签 ----

    // 时间范围变了（kIdSwHitRange 的 DUIN_VALUECHANGED）。index 取值见 HitRange，越界按近 7 天。
    void OnHitRangeChanged(int index);

    // 发送者搜索框文字变了（kIdSwHitSearch 的 DUIN_VALUECHANGED）：停止输入约 400 毫秒后才查询。
    void OnHitSearchChanged();

    // 发送者搜索的防抖定时器到点（kHitSearchTimerId 的 WM_TIMER）：按搜索框当前文字从第 1 页查询。
    void OnHitSearchTimer();

    // 来源筛选变了（kIdSwHitSource 的 DUIN_VALUECHANGED）。index：0 为全部来源，1～12 为各来源。
    void OnHitSourceChanged(int index);

    // 分类筛选变了（kIdSwHitCategory 的 DUIN_VALUECHANGED）。index：0 为全部分类，1～6 为各分类。
    void OnHitCategoryChanged(int index);

    // 拦截记录翻到第 page 页（分页条回调）。越界或与当前页相同时不做任何事。
    void GoToHitPage(int page);

    // 一条拦截记录在表格里各列的文字（静态函数，便于单元测试），顺序见 HitColumn。
    // 发送者、会话为空时显示「-」；内容摘要里的换行换成空格。
    //   item：拦截记录。cells：[出参] 各列文字，调用前会被清空。
    static void HitRowCells(const adminapi::SensitiveHitItem& item, std::vector<CString>& cells);

    // 把 Unix 毫秒时间格式化为本地时间「YYYY-MM-DD HH:MM:SS」；不大于 0 时返回「-」。
    static CString FormatHitTime(long long ms);

    // 当前时间，Unix 毫秒（拦截记录的时间范围按它倒推）。
    static long long NowMs();

    // 鼠标在拦截记录表格 pt 处时的悬停提示：落在某一行的「内容摘要」列上时为该行摘要全文，其余为空串。
    CString HitTooltipAt(POINT pt) const;

    // 导入时被跳过的一行（汇总用）
    struct ImportSkipLine
    {
        int     m_line;    // 在导入原文里的行号，从 1 起
        CString m_word;    // 该行的词条
        CString m_reason;  // 跳过原因（中文）

        ImportSkipLine()
            : m_line(0)
        {
        }
    };

    // 导入结束后的汇总文字，显示在导入弹窗里（静态函数，便于单元测试）。逐行列出被跳过的行，
    // 最多列 kImportSummaryMaxLines 行，其余只给出行数。
    //   added：成功添加的条数。skips：被跳过的行，按原文行号排列。
    //   failure：中途失败的原因，空串表示全部批次都已成功提交。
    //   failedBatch：失败的是第几批（从 1 起），failure 为空时忽略。batchCount：共几批。
    static CString BuildImportSummary(int added, const std::vector<ImportSkipLine>& skips,
                                      const CString& failure, int failedBatch, int batchCount);

    // 导入汇总里最多逐行列出的被跳过行数
    static const int kImportSummaryMaxLines = 500;

    // 翻到第 page 页（分页条回调）。越界、与当前页相同或加载中时不做任何事（加载中时记下，结果回来后补做）。
    void GoToPage(int page);

    // ---- 只读访问（AdminFrame 与单元测试用）----

    // 当前页签下标，取值见 Tab。
    int ActiveTab() const { return m_activeTab; }

    // 企业选择框是否显示（只有平台超管显示）。
    bool IsEnterprisePickerVisible() const;

    // 企业选择框里的企业个数。
    int EnterpriseCount() const { return static_cast<int>(m_enterpriseIds.size()); }

    // 当前操作的企业 ID：平台超管为所选企业，未选时为 0；企业管理员恒为 0（由服务端改写为本企业）。
    long long CurrentEnterpriseId() const { return m_enterpriseId; }

    // 工具行（搜索、分类、添加、总开关）是否可用：平台超管未选企业时不可用。
    bool IsToolbarEnabled() const;

    // 词库表格当前的覆盖提示文字（空串表示正常显示数据行）。
    CString OverlayText() const;

    // 词库表格当前的行数。
    int RowCount() const { return static_cast<int>(m_words.size()); }

    // 「已用 N / 10000」的文字。
    CString UsageText() const;

    // 总开关当前是否打开。
    bool IsSwitchChecked() const;

    // 当前页码（从 1 起）与总页数。
    int Page() const { return m_page; }
    int PageCount() const;

    // 词库表格（单元测试据此模拟点击行内链接）。
    ui::ActionTable* WordTable() { return m_wordTable; }

    // 词库表头（单元测试据此核对表头与数据行对齐）。
    ui::ColumnHeader* WordHeader() { return m_wordHeader; }

    // 拦截记录的行数、覆盖提示、页码与总页数。
    int HitRowCount() const { return static_cast<int>(m_hits.size()); }
    CString HitOverlayText() const;
    int HitPage() const { return m_hitPage; }
    int HitPageCount() const;

    // 拦截记录表格与发送者搜索框（单元测试据此模拟输入与悬停）。
    ui::ActionTable* HitTable() { return m_hitTable; }
    balloonwjui::DuiEdit* HitSearchBox() { return m_hitSearch; }

    // 总开关（单元测试据此模拟用户拨动开关）。
    balloonwjui::DuiSwitch* EnabledSwitch() { return m_switch; }

    // ---- DuiControl 覆写 ----
    // 排版：先按基类排好子控件，再让表头让出表格滚动条的宽度（界面高度变化会改变滚动条的显隐）。
    void Layout(const RECT& rcAvail) override;

private:
    // 加载状态
    enum LoadState
    {
        kLoadIdle    = 0,   // 尚未加载（平台超管未选企业）
        kLoadLoading = 1,   // 请求已发出，等待应答
        kLoadLoaded  = 2,   // 已成功载入
        kLoadFailed  = 3,   // 加载失败
    };

    // 一次词库列表查询的条件。用来判断新到的列表结果与表格当前显示的数据是否出自同一查询：
    // 条件不同（换页、换企业、搜索、筛选）时表格回到顶部，相同（删除、修改之后重新加载）时保持滚动位置。
    struct WordQuery
    {
        long long m_enterpriseId;  // 企业 ID（企业管理员恒为 0）；-1 表示「没有数据」，与任何查询都不同
        CString   m_keyword;       // 搜索文字
        int       m_category;      // 分类筛选，0 为全部
        int       m_page;          // 页码，从 1 起

        WordQuery()
            : m_enterpriseId(-1), m_category(0), m_page(0)
        {
        }

        // 两次查询的条件是否完全相同。
        bool SameAs(const WordQuery& other) const
        {
            return m_enterpriseId == other.m_enterpriseId && m_keyword == other.m_keyword
                && m_category == other.m_category && m_page == other.m_page;
        }
    };

    // 按 m_activeTab 切换两个内容区的显隐，并让本容器重新排版（SetVisible 本身不触发排版）。
    void ApplyTabVisibility();

    // 把界面恢复到「刚进入、还没有数据」的状态（换了登录身份时调用）。
    void ResetState();

    // 发起企业列表请求（平台超管用来填企业选择框）。
    void StartLoadEnterprises();

    // 发起词库列表请求：当前企业、搜索文字、分类、页码。加载中时只记下「待会儿再查一次」。
    void StartLoadWords();

    // 处理各接口的应答。
    void OnEnterprisesLoaded(const HttpResult& http);
    void OnWordsLoaded(const HttpResult& http);
    void OnWordSaved(const HttpResult& http, bool isAdd);
    void OnWordsDeleted(const HttpResult& http);
    void OnEnabledSet(const HttpResult& http);
    void OnImportBatchDone(const HttpResult& http);
    void OnHitsLoaded(const HttpResult& http);

    // 发起拦截记录请求：当前企业、时间范围、发送者、来源、分类、页码。加载中时只记下「待会儿再查一次」。
    void StartLoadHits();

    // 按加载状态与数据更新拦截记录表格的行数、覆盖提示与分页条。
    void UpdateHitView();

    // 拦截记录表格的回调
    static void PaintHitRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    void PaintHitRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover);
    static void OnHitPageThunk(void* user, int newPage);
    static CString HitTooltipThunk(void* user, POINT pt);
    void OnExportDone(const HttpResult& http);

    // 提交 m_importBatches 里下标为 m_importIndex 的那一批，并报告进度。
    void SendImportBatch();

    // 导入结束（全部完成或中途失败）：报告汇总、清理导入状态、从第 1 页重新拉列表。
    //   failure：中途失败的原因，空串表示全部完成。
    void FinishImport(const CString& failure);

    // 当前企业的名称：平台超管取企业选择框里所选的那一项；企业管理员没有企业名，返回空串。
    CString CurrentEnterpriseName() const;

    // 按加载状态、企业选择与数据更新覆盖提示、工具行可用状态、用量文字与分页条。
    void UpdateView();

    // 让词库表头右侧让出与表格滚动条相同的宽度，使表头各列与数据行对齐。
    void SyncHeaderInset();

    // 发出一个请求（经请求出口）。
    void Send(HttpRequest& request, ApiTag tag);

    // 发出一个界面请求（经界面请求处理函数）。
    void Raise(const UiRequest& request);
    void RaiseToast(const CString& text, bool isError);

    // 表格回调
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover);
    static void OnRowActionThunk(void* user, int rowIndex, int actionIndex);
    static void OnPageThunk(void* user, int newPage);

    // ---- 外部接口 ----
    HWND          m_hostWnd;        // 主窗口句柄（搜索防抖定时器挂在它上面），由 AdminFrame 注入
    RequestSinkFn m_sink;           // 请求出口，可为空
    void*         m_sinkUser;       // 请求出口的用户数据，所有权归调用方
    UiRequestFn   m_uiHandler;      // 界面请求处理函数，可为空
    void*         m_uiHandlerUser;  // 界面请求处理函数的用户数据，所有权归调用方

    // ---- 子控件借用指针（所有权在控件树中）----
    ui::SegControl*           m_tabs;        // 页签切换
    balloonwjui::DuiControl*  m_wordsPanel;  // 「词库」页签内容区
    balloonwjui::DuiControl*  m_hitsPanel;   // 「拦截记录」页签内容区
    balloonwjui::DuiControl*  m_pickerBox;   // 企业选择框所在的那一段（平台超管才显示）
    balloonwjui::DuiComboBox* m_picker;      // 企业选择框
    balloonwjui::DuiLabel*    m_usage;       // 「已用 N / 10000」
    balloonwjui::DuiSwitch*   m_switch;      // 总开关
    balloonwjui::DuiEdit*     m_search;      // 搜索框
    balloonwjui::DuiComboBox* m_category;    // 分类筛选
    balloonwjui::DuiButton*   m_addButton;   // 「添加」按钮
    balloonwjui::DuiButton*   m_importButton; // 「批量导入」按钮
    balloonwjui::DuiButton*   m_exportButton; // 「导出」按钮
    ui::ColumnHeader*         m_wordHeader;  // 词库表头
    ui::ActionTable*          m_wordTable;   // 词库表格
    ui::PagerBar*             m_pager;       // 词库分页条
    ui::ActionTable*          m_hitTable;    // 拦截记录表格
    ui::ColumnHeader*         m_hitHeader;   // 拦截记录表头
    ui::PagerBar*             m_hitPager;    // 拦截记录分页条
    balloonwjui::DuiEdit*     m_hitSearch;   // 发送者搜索框
    balloonwjui::DuiComboBox* m_hitRangeBox; // 时间范围下拉框
    balloonwjui::DuiComboBox* m_hitSourceBox; // 来源筛选下拉框
    balloonwjui::DuiComboBox* m_hitCategoryBox; // 分类筛选下拉框

    // ---- 状态 ----
    int                                       m_activeTab;      // 当前页签下标，取值见 Tab
    bool                                      m_isPlatform;     // 进入本界面时的角色是否为平台超管
    CString                                   m_sessionToken;   // 上次进入时的登录令牌，用来发现换了身份
    std::vector<long long>                    m_enterpriseIds;  // 企业选择框各项对应的企业 ID
    long long                                 m_enterpriseId;   // 当前企业 ID，见 CurrentEnterpriseId
    int                                       m_loadState;      // 词库的加载状态，取值见 LoadState
    CString                                   m_loadError;      // 最近一次加载失败的原因（中文），失败时显示在表格中央
    bool                                      m_reloadPending;  // 加载中又有新的查询条件，结果回来后补查一次
    CString                                   m_keyword;        // 当前搜索文字
    int                                       m_categoryFilter; // 当前分类筛选，0 为全部
    int                                       m_page;           // 当前页码，从 1 起
    int                                       m_total;          // 满足条件的总条数
    int                                       m_wordCount;      // 该企业的词条总数
    bool                                      m_enabled;        // 该企业的总开关状态（以服务端为准）
    bool                                      m_requestedEnabled; // 最近一次 setenabled 请求要设成的状态
    std::vector<adminapi::SensitiveWordItem>  m_words;          // 本页词条
    long long                                 m_pendingDeleteId; // 等待确认删除的词条 ID，0 表示没有
    bool                                      m_saving;         // 弹窗提交进行中（防止连点）
    WordQuery                                 m_requestedQuery; // 最近一次发出的列表请求的条件
    WordQuery                                 m_shownQuery;     // 表格当前所显示数据的查询条件；没有数据时企业 ID 为 -1
    bool                                      m_scrollTopOnLoad; // 下一次列表结果到达时是否把表格滚回顶部（添加成功后置位）
    std::vector<CString>                      m_enterpriseNames; // 企业选择框各项对应的企业名，与 m_enterpriseIds 一一对应
    std::vector<adminapi::SensitiveHitItem>   m_hits;           // 本页拦截记录
    int                                       m_hitTotal;       // 满足筛选条件的拦截记录总数
    int                                       m_hitPage;        // 拦截记录当前页码，从 1 起
    int                                       m_hitLoadState;   // 拦截记录的加载状态，取值见 LoadState
    CString                                   m_hitLoadError;   // 拦截记录最近一次加载失败的原因（中文）
    bool                                      m_hitReloadPending; // 拦截记录加载中又有新的查询条件，结果回来后补查一次
    int                                       m_hitRange;       // 时间范围，取值见 HitRange
    CString                                   m_hitKeyword;     // 发送者搜索文字
    int                                       m_hitSource;      // 来源筛选，0 为全部
    int                                       m_hitCategory;    // 分类筛选，0 为全部
    std::vector<swtext::ImportBatch>          m_importBatches;  // 本次导入切好的各批；导入结束后清空
    size_t                                    m_importIndex;    // 正在等待应答的批次下标
    int                                       m_importCategory; // 本次导入的默认分类，1～6
    int                                       m_importAdded;    // 本次导入已成功添加的条数
    std::vector<ImportSkipLine>               m_importSkips;    // 本次导入已被跳过的行，按原文行号排列
    bool                                      m_importing;      // 是否正在导入
    bool                                      m_exporting;      // 导出请求已发出、尚未返回
};

} // namespace flamingoadmin
