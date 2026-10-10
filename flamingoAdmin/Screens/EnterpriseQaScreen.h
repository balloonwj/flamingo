/**
 *  flamingoAdmin —— 企业问答页 EnterpriseQaScreen（docs/群问答方案.md，接口见 docs/Admin控制台接口文档.md 模块 13）。
 *
 *  维护显示在群聊窗「问答」页签里的企业问答：每条可以应用到本企业全部群，或只应用到所选的群；群成员只读、不能隐藏，
 *  某个群自己维护了同一问题时那个群只显示群里那一条。页面从上到下：页头、企业选择框（仅平台超管）、
 *  搜索框与「刷新」「新增问答」、问答表格（问题与答案摘要 / 应用范围 / 修改人 / 修改时间 / 编辑·删除）。
 *
 *  与敏感词页同一种分工：本界面不直接接触网络与弹窗，请求经 SetRequestSink 交给 AdminFrame 发出，打开弹窗、
 *  确认框、提示等经 SetUiRequestHandler 请 AdminFrame 代办；应答由 AdminFrame 转回 OnApiResult。
 *  问答一个企业最多 1000 条，一次全部拉回，搜索只在本地筛选。单元测试见 Tests/EnterpriseQaTests.cpp。
 *
 *  典型用法（AdminFrame 内）：
 *      screen->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
 *      screen->SetUiRequestHandler(&AdminFrame::QaUiRequestThunk, this);
 *      //切到本界面时
 *      screen->OnEnter();
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <vector>

#include "Controls/Layout/DuiLayout.h"
#include "Util/AdminApi.h"
#include "Util/QaAdminApi.h"

namespace balloonwjui { class DuiEdit; class DuiComboBox; class DuiLabel; class DuiButton; }

namespace flamingoadmin {

namespace ui { class ActionTable; class ColumnHeader; }

// 企业问答页。整体是一个纵向布局：页头 + 企业选择行 + 工具行 + 表格卡。
class EnterpriseQaScreen : public balloonwjui::DuiVBox
{
public:
    // 本界面请宿主（AdminFrame）做的事
    enum UiRequestKind
    {
        kUiOpenAddModal  = 1,   // 打开「新增企业问答」弹窗
        kUiOpenEditModal = 2,   // 打开「编辑企业问答」弹窗，UiRequest::m_item 为要编辑的问答
        kUiConfirmDelete = 3,   // 弹确认框问是否删除 UiRequest::m_item；宿主随后调用 OnConfirmDelete
        kUiCloseModal    = 4,   // 弹窗里的提交已成功，关闭弹窗
        kUiModalError    = 5,   // 弹窗里的提交失败：在弹窗里显示 UiRequest::m_text，弹窗不关
        kUiToast         = 6,   // 显示一条提示 UiRequest::m_text；m_isError 为 true 时按错误提示显示
        kUiGroupsLoaded  = 7,   // 本企业的群拉到了：弹窗开着时用 Groups() 补上群列表
    };

    // 一次界面请求
    struct UiRequest
    {
        int           m_kind;     // 请求种类，取值见 UiRequestKind
        qaapi::QaItem m_item;     // 相关的问答（编辑、删除时有效）
        CString       m_text;     // 提示或错误文字（kUiModalError、kUiToast 时有效）
        bool          m_isError;  // kUiToast 时是否按错误显示

        UiRequest()
            : m_kind(0), m_isError(false)
        {
        }
    };

    // 请求出口：请求按空的 ServerConfig 构造，实现方补上服务器地址与回传目标后交给 HttpWorker。
    //   user：SetRequestSink 时传入的指针。request：请求，m_tag 已填好。
    typedef void (*RequestSinkFn)(void* user, HttpRequest& request);

    // 界面请求的处理函数。
    //   user：SetUiRequestHandler 时传入的指针。request：请求内容，仅本次调用内有效。
    typedef void (*UiRequestFn)(void* user, const UiRequest& request);

    // 加载状态
    enum LoadState
    {
        kLoadIdle    = 0,   // 还没加载（平台超管还没选企业）
        kLoadLoading = 1,   // 正在加载
        kLoadLoaded  = 2,   // 已加载
        kLoadFailed  = 3,   // 加载失败
    };

    // 构造时一次性建好整棵控件树；此时还不发请求。
    EnterpriseQaScreen();

    // 设置请求出口。fn 为空时请求被丢弃（记一条日志）。user 所有权归调用方。
    void SetRequestSink(RequestSinkFn fn, void* user);

    // 设置界面请求的处理函数。fn 为空时界面请求被丢弃。user 所有权归调用方。
    void SetUiRequestHandler(UiRequestFn fn, void* user);

    // 每次切换到本界面时由 AdminFrame 调用：按当前登录角色决定界面形态（平台超管显示企业选择框），并刷新数据。
    void OnEnter();

    // 接收转交来的异步接口结果。tag：kApiTagQa*；http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 平台超管在企业选择框里选了一项（kIdQaEnterprise 的 DUIN_VALUECHANGED）。index 越界时不做任何事。
    void OnEnterpriseSelected(int index);

    // 搜索框文字变了（kIdQaSearch 的 DUIN_VALUECHANGED）：立即在本地筛选。
    void OnSearchChanged();

    // 「刷新」（kIdQaRefresh 的 DUIN_CLICK）：重拉问答与群。
    void OnRefreshClicked();

    // 「新增问答」（kIdQaAdd 的 DUIN_CLICK）：请宿主打开新增弹窗。平台超管未选企业时只提示。
    void OnAddClicked();

    // 宿主把确认框里的选择告诉本界面：confirmed 为 true 时删除当初请求确认的那一条。
    void OnConfirmDelete(bool confirmed);

    // 弹窗里点了「保存」：提交新增或修改（item.m_id 为 0 时新增）。结果经 kUiCloseModal 或 kUiModalError 告诉宿主。
    void SubmitQa(const qaapi::QaItem& item);

    // 本企业的群（弹窗选择应用范围用；可能还没拉到，为空）
    const std::vector<qaapi::QaGroup>& Groups() const { return m_groups; }

    // 全部问答（单元测试用）
    const std::vector<qaapi::QaItem>& Items() const { return m_items; }

    // 表格里显示的行对应 Items() 的下标（单元测试用）
    const std::vector<size_t>& ShownRows() const { return m_shown; }

    // 当前加载状态（单元测试用），取值见 LoadState
    int LoadStateValue() const { return m_loadState; }

    /**
     *  按关键字筛选问答：问题或答案里包含关键字（不区分大小写）的入选，保持原顺序。纯函数。
     *    items：全部问答；keyword：搜索框文字，去掉首尾空白后为空表示全部入选。
     *  @return 入选条目的下标
     */
    static std::vector<size_t> FilterItems(const std::vector<qaapi::QaItem>& items, const CString& keyword);

    // 表格行内操作的回调（ActionTable）。rowIndex：表格行；actionIndex：0 编辑、1 删除。
    void OnRowAction(int rowIndex, int actionIndex);

private:
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    static void OnRowActionThunk(void* user, int rowIndex, int actionIndex);

    // 画表格的一行
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover);

    // 发一个请求（补上标签后交给请求出口）
    void Send(HttpRequest& request, ApiTag tag);

    // 发一个界面请求
    void Raise(const UiRequest& request);

    // 发一条提示
    void RaiseToast(const CString& text, bool isError);

    // 拉企业列表（平台超管的企业选择框）
    void StartLoadEnterprises();

    // 拉问答与群；平台超管未选企业时什么都不做
    void StartLoad();

    // 按当前数据与搜索框重算显示的行，刷新表格与提示
    void UpdateView();

    // 本次请求要带的企业 ID：企业管理员为 0（服务端按本企业处理），平台超管为选中的企业
    long long RequestEnterpriseId() const;

    RequestSinkFn                m_sink;            // 请求出口
    void*                        m_sinkUser;        // 请求出口的透传指针
    UiRequestFn                  m_uiHandler;       // 界面请求的处理函数
    void*                        m_uiHandlerUser;   // 界面请求处理函数的透传指针
    balloonwjui::DuiHBox*        m_pickerRow;       // 企业选择行（仅平台超管可见）
    balloonwjui::DuiComboBox*    m_picker;          // 企业选择框
    balloonwjui::DuiEdit*        m_search;          // 搜索框
    balloonwjui::DuiButton*      m_addButton;       // 「新增问答」
    ui::ColumnHeader*            m_header;          // 表头
    ui::ActionTable*             m_table;           // 问答表格
    bool                         m_isPlatform;      // 当前登录的是不是平台超管
    long long                    m_enterpriseId;    // 平台超管选中的企业；0 表示还没选
    std::vector<long long>       m_enterpriseIds;   // 企业选择框各项对应的企业 ID
    int                          m_loadState;       // 问答的加载状态，取值见 LoadState
    CString                      m_loadError;       // 加载失败的原因
    std::vector<qaapi::QaItem>   m_items;           // 全部问答，按修改时间倒序
    std::vector<qaapi::QaGroup>  m_groups;          // 本企业的群
    std::vector<size_t>          m_shown;           // 表格各行对应 m_items 的下标
    qaapi::QaItem                m_pendingDelete;   // 请求确认删除的那一条
    bool                         m_saving;          // 弹窗的保存请求是否已发出、正在等应答
    CString                      m_sessionToken;    // 上次进入本界面时的登录令牌；换了身份登录时清掉旧数据
};

} // namespace flamingoadmin
