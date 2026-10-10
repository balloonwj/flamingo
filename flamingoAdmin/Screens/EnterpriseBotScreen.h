/**
 *  flamingoAdmin —— 机器人页 EnterpriseBotScreen（docs/机器人方案.md 4.1 与第 7 节，接口见 docs/Admin控制台接口文档.md 模块 14）。
 *
 *  管理本企业的企业机器人：每个机器人有一组有序的规则（被 @、关键词、新成员入群、定时、外部系统推送），可应用到
 *  本企业全部群或所选的群。页面从上到下：页头、企业选择框（仅平台超管）、工具行（「刷新」「外部服务域名白名单」
 *  「新建机器人」）、机器人表格（名称 / 应用范围 / 群主可关闭 / 规则数 / 状态 / 最近触发 / 操作）。来源为 GitHub、
 *  GitLab 仓库通知的机器人只读：不给「编辑」「删除」，只能停用或启用。
 *
 *  与企业问答页同一种分工：本界面不直接接触网络与弹窗，请求经 SetRequestSink 交给 AdminFrame 发出；打开机器人
 *  编辑窗、域名白名单窗、确认框、提示等经 SetUiRequestHandler 请 AdminFrame 代办；应答由 AdminFrame 转回
 *  OnApiResult。编辑窗里的保存、「试一试」、「重新生成」也由 AdminFrame 转给本界面发请求（SubmitBot、RequestTry、
 *  RequestResetToken）。确认框（删除、重新生成令牌）的结果经 fsui::ConfirmTarget::OnConfirmResult 回到本界面。
 *  一个企业最多 50 个企业机器人，一次全部拉回。单元测试见 Tests/EnterpriseBotTests.cpp。
 *
 *  典型用法（AdminFrame 内）：
 *      screen->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
 *      screen->SetUiRequestHandler(&AdminFrame::BotUiRequestThunk, this);
 *      //切到本界面时
 *      screen->OnEnter();
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <vector>

#include "Controls/Layout/DuiLayout.h"
#include "Util/AdminApi.h"
#include "Util/BotAdminApi.h"
#include "Screens/FileShareUi.h"   // fsui::ConfirmTarget：确认框的结果回到本界面

namespace balloonwjui { class DuiComboBox; class DuiButton; }

namespace flamingoadmin {

namespace ui { class ActionTable; class ColumnHeader; }

// 机器人页。整体是一个纵向布局：页头 + 企业选择行 + 工具行 + 表格卡。
class EnterpriseBotScreen : public balloonwjui::DuiVBox, public fsui::ConfirmTarget
{
public:
    // 本界面请宿主（AdminFrame）做的事
    enum UiRequestKind
    {
        kUiOpenAddModal       = 1,    // 打开「新建机器人」编辑窗
        kUiOpenEditModal      = 2,    // 打开「编辑机器人」编辑窗，UiRequest::m_bot 为带规则与密钥的详情
        kUiCloseModal         = 3,    // 编辑窗里的保存已成功，关闭编辑窗
        kUiModalError         = 4,    // 编辑窗里的保存失败：在编辑窗里显示 UiRequest::m_text，编辑窗不关
        kUiToast              = 5,    // 显示一条提示 UiRequest::m_text；m_isError 为 true 时按错误提示显示
        kUiGroupsLoaded       = 6,    // 本企业的群拉到了：编辑窗开着时用 Groups() 补上群列表
        kUiConfirm            = 7,    // 弹确认框：m_title / m_text / m_okText / m_danger；结果经 OnConfirmResult 回来
        kUiTryResult          = 8,    // 「试一试」有结果了：在编辑窗里显示 m_text；m_isError 为 true 时是请求失败
        kUiSecretsReset       = 9,    // 令牌与密钥已重新生成：m_bot 的 m_botId、m_webhookToken、m_secret 有效
        kUiOpenDomainsModal   = 10,   // 打开「外部服务域名白名单」窗，m_domains 为当前的白名单
        kUiCloseDomainsModal  = 11,   // 白名单已保存，关闭白名单窗
        kUiDomainsModalError  = 12,   // 白名单保存失败：在白名单窗里显示 m_text，窗口不关
        kUiDomainsLoaded      = 13,   // 白名单拉到了（或保存后更新了）：编辑窗开着时用 Domains() 更新本地校验所用的白名单
    };

    // 一次界面请求
    struct UiRequest
    {
        int                  m_kind;     // 请求种类，取值见 UiRequestKind
        botapi::BotItem      m_bot;      // 相关的机器人（kUiOpenEditModal、kUiSecretsReset 时有效）
        CString              m_title;    // 确认框标题（kUiConfirm）
        CString              m_text;     // 提示、错误、确认框正文或「试一试」的结果
        CString              m_okText;   // 确认按钮的文字（kUiConfirm）
        bool                 m_danger;   // 确认按钮是否为危险样式（kUiConfirm）
        bool                 m_isError;  // 是否按错误显示（kUiToast、kUiTryResult）
        std::vector<CString> m_domains;  // 白名单（kUiOpenDomainsModal）

        UiRequest()
            : m_kind(0), m_danger(false), m_isError(false)
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

    // 行内操作的下标，与表格 AddAction 的顺序一致
    enum RowAction
    {
        kActionEdit    = 0,   // 编辑：先拉详情，拉到后打开编辑窗（仓库通知的机器人不显示）
        kActionDisable = 1,   // 停用（只在启用的机器人上显示）
        kActionEnable  = 2,   // 启用（只在停用的机器人上显示）
        kActionDelete  = 3,   // 删除：先弹确认框（仓库通知的机器人不显示）
    };

    // 构造时一次性建好整棵控件树；此时还不发请求。
    EnterpriseBotScreen();

    // 设置请求出口。fn 为空时请求被丢弃（记一条日志）。user 所有权归调用方。
    void SetRequestSink(RequestSinkFn fn, void* user);

    // 设置界面请求的处理函数。fn 为空时界面请求被丢弃。user 所有权归调用方。
    void SetUiRequestHandler(UiRequestFn fn, void* user);

    // 每次切换到本界面时由 AdminFrame 调用：按当前登录角色决定界面形态（平台超管显示企业选择框），并刷新数据
    // （机器人列表、本企业的群、域名白名单）。
    void OnEnter();

    // 接收转交来的异步接口结果。tag：kApiTagBot*；http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 平台超管在企业选择框里选了一项（kIdBotEnterprise 的 DUIN_VALUECHANGED）。index 越界时不做任何事。
    void OnEnterpriseSelected(int index);

    // 「刷新」（kIdBotRefresh 的 DUIN_CLICK）：重拉机器人、群与白名单。
    void OnRefreshClicked();

    // 「新建机器人」（kIdBotAdd 的 DUIN_CLICK）：请宿主打开新建编辑窗。平台超管未选企业、已达 50 个时只提示。
    void OnAddClicked();

    // 「外部服务域名白名单」（kIdBotDomains 的 DUIN_CLICK）：请宿主打开白名单窗。平台超管未选企业时只提示。
    void OnDomainsClicked();

    // 表格行内操作的回调（ActionTable）。rowIndex：表格行；actionIndex：取值见 RowAction。
    void OnRowAction(int rowIndex, int actionIndex);

    // 编辑窗里点了「保存」（已通过本地校验）：提交新建或修改（bot.m_botId 为 0 时新建）。结果经 kUiCloseModal 或
    // kUiModalError 告诉宿主。上一次保存还没有应答时不重复提交。
    void SubmitBot(const botapi::BotItem& bot);

    // 编辑窗里点了「试一试」：带上编辑窗里当前的机器人请服务端判定。结果经 kUiTryResult 告诉宿主。
    //   draft：编辑窗里当前的机器人（可未保存）；input：模拟的消息。
    void RequestTry(const botapi::BotItem& draft, const botapi::BotTryInput& input);

    // 编辑窗里点了「重新生成」：先请宿主弹确认框，确认后才发请求。结果经 kUiSecretsReset 告诉宿主。
    //   botId：已保存的机器人ID；不大于 0 时只提示「保存后才能生成」。
    void RequestResetToken(long long botId);

    // 白名单窗里点了「保存」（已通过本地校验）：整份提交。结果经 kUiCloseDomainsModal 或 kUiDomainsModalError 告诉宿主。
    void SubmitDomains(const std::vector<CString>& domains);

    // 确认框的结果（fsui::ConfirmTarget）：确认则发出待确认的删除或重新生成请求，取消则放弃。
    void OnConfirmResult(bool confirmed) override;

    // 本企业的群（编辑窗选择应用范围、定时目标群、「试一试」的群用；可能还没拉到，为空）
    const std::vector<botapi::BotGroup>& Groups() const { return m_groups; }

    // 外部服务的域名白名单（编辑窗本地校验外部服务地址用）
    const std::vector<CString>& Domains() const { return m_domains; }

    // 白名单是否已经拉到（没拉到时编辑窗不在本地检查白名单，交给服务端）
    bool DomainsLoaded() const { return m_domainsLoaded; }

    // 全部机器人（单元测试用）
    const std::vector<botapi::BotItem>& Bots() const { return m_bots; }

    // 当前加载状态（单元测试用），取值见 LoadState
    int LoadStateValue() const { return m_loadState; }

    // 是否有保存请求正在等应答（单元测试用）
    bool IsSaving() const { return m_saving; }

    // 表格当前显示的行数（单元测试用；显示覆盖提示时为 0）
    int TableRowCount() const;

    // 表格第 rowIndex 行的第 actionIndex 个操作链接是否显示（单元测试用，与表格的判断相同）
    bool IsRowActionVisible(int rowIndex, int actionIndex) const;

    /**
     *  某个机器人的某个行内操作是否显示（纯函数）：仓库通知的机器人不显示「编辑」「删除」；「停用」只在启用的
     *  机器人上显示，「启用」只在停用的机器人上显示。
     *    bot：机器人；actionIndex：取值见 RowAction。
     */
    static bool IsActionVisibleFor(const botapi::BotItem& bot, int actionIndex);

private:
    // 待确认的操作
    enum PendingConfirm
    {
        kPendingNone       = 0,   // 没有待确认的操作
        kPendingDelete     = 1,   // 删除机器人 m_pendingBotId
        kPendingResetToken = 2,   // 重新生成机器人 m_pendingBotId 的令牌与密钥
    };

    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    static void OnRowActionThunk(void* user, int rowIndex, int actionIndex);
    static bool ActionVisibleThunk(void* user, int rowIndex, int actionIndex);

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

    // 拉机器人、群与白名单；平台超管未选企业时什么都不做
    void StartLoad();

    // 按当前数据刷新表格与覆盖提示
    void UpdateView();

    // 本次请求要带的企业 ID：企业管理员为 0（服务端按本企业处理），平台超管为选中的企业
    long long RequestEnterpriseId() const;

    // 当前是否可以对本企业操作（平台超管须先选企业）；不能时提示「请先选择企业」
    bool CheckEnterpriseChosen();

    // 按机器人ID找表格里的机器人；找不到返回 nullptr
    const botapi::BotItem* FindBot(long long botId) const;

    RequestSinkFn                  m_sink;            // 请求出口
    void*                          m_sinkUser;        // 请求出口的透传指针
    UiRequestFn                    m_uiHandler;       // 界面请求的处理函数
    void*                          m_uiHandlerUser;   // 界面请求处理函数的透传指针
    balloonwjui::DuiHBox*          m_pickerRow;       // 企业选择行（仅平台超管可见）；所有权在控件树
    balloonwjui::DuiComboBox*      m_picker;          // 企业选择框；所有权在控件树
    ui::ColumnHeader*              m_header;          // 表头；所有权在控件树
    ui::ActionTable*               m_table;           // 机器人表格；所有权在控件树
    bool                           m_isPlatform;      // 当前登录的是不是平台超管
    long long                      m_enterpriseId;    // 平台超管选中的企业；0 表示还没选
    std::vector<long long>         m_enterpriseIds;   // 企业选择框各项对应的企业 ID
    int                            m_loadState;       // 机器人列表的加载状态，取值见 LoadState
    CString                        m_loadError;       // 加载失败的原因
    std::vector<botapi::BotItem>   m_bots;            // 全部企业机器人，按机器人ID升序（不带规则与密钥）
    std::vector<botapi::BotGroup>  m_groups;          // 本企业的群
    std::vector<CString>           m_domains;         // 外部服务的域名白名单
    bool                           m_domainsLoaded;   // 白名单是否已经拉到
    std::vector<CString>           m_savingDomains;   // 已提交、等待应答的白名单；成功后写入 m_domains
    int                            m_pendingConfirm;  // 待确认的操作，取值见 PendingConfirm
    long long                      m_pendingBotId;    // 待确认操作的机器人ID
    long long                      m_detailBotId;     // 正在拉详情（准备打开编辑窗）的机器人ID；0 表示没有
    bool                           m_saving;          // 编辑窗的保存请求是否已发出、正在等应答
    bool                           m_trying;          // 「试一试」请求是否已发出、正在等应答
    bool                           m_savingDomainsBusy; // 白名单的保存请求是否已发出、正在等应答
    CString                        m_sessionToken;    // 上次进入本界面时的登录令牌；换了身份登录时清掉旧数据
};

} // namespace flamingoadmin
