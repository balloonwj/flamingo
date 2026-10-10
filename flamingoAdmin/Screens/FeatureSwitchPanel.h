/**
 *  flamingoAdmin —— 「功能开关」面板（docs/文件共享方案.md 第 10 节）：按企业打开或关闭一项功能。
 *
 *  一个面板管一个开关，管哪个由构造时传入的 FeatureSwitchSpec 决定（设置项名与界面文字）。目前有十三个：
 *  文件共享（FileShareSwitchSpec）、客户端点对点直连（P2PSwitchSpec，docs/P2P通信方案.md 9.5）、
 *  邀请入群需被邀请人同意（InviteConfirmSwitchSpec，docs/邀请入群方案.md §3.5）、允许用户自助注册（RegisterSwitchSpec）、
 *  群语音 / 群视频通话（GroupCallSwitchSpec，docs/群音视频通话方案.md 第 6 期）、语音字幕（CaptionSwitchSpec，
 *  docs/语音字幕方案.md 第 10 节）、单聊密聊（SecretChatSwitchSpec，docs/密聊方案.md 4.9）、帖子群（ThreadGroupSwitchSpec，
 *  docs/帖子群方案.md 4.7）、云端录制（CloudRecordSwitchSpec，docs/屏幕共享与录制方案.md 12.6.5，它的配额与保留天数由
 *  CloudRecordQuotaPanel 设置），以及机器人的四个开关（docs/机器人方案.md 4.1）：机器人总开关（BotSwitchSpec）、允许群主
 *  添加群机器人（BotGroupSwitchSpec）、允许员工配置个人机器人（BotPersonalSwitchSpec）、允许群主给群机器人配置外部服务
 *  （BotGroupExternalSwitchSpec）。它们在界面上的分组与排列见 FeatureSwitchesPage.h。
 *
 *  面板有两种样式：完整样式显示一行「名称 + 说明 + 开关」，下面一行说明当前取值的来历（默认设置，或已由
 *  管理员设置）；紧凑样式只有一行「短名称 + 取值来历 + 开关」，原先用在高度紧张的企业设置弹窗里。
 *  使用的地方（2026-10-08 起只有一处）：企业详情屏「功能开关」子 tab 的内容页（FeatureSwitchesPage），全部用完整样式，
 *  平台超管设置所看的企业，企业管理员设置本企业。此前分在两处：企业设置弹窗（EnterpriseSettingsModal）的第三块（紧凑样式）
 *  与平台设置页（SettingsScreen）的「功能开关」卡；紧凑样式目前没有界面使用，保留以备高度紧张的场合。
 *  拨动开关时先弹确认框说明影响，确认后才提交；失败或取消时开关恢复为服务端的状态。
 *
 *  本面板不直接接触网络与对话框（见 FileShareUi.h），单元测试见 Tests/FileShareScreensTests.cpp 的 FSP 系列。
 *
 *  典型用法：
 *      auto panel = std::make_unique<FeatureSwitchPanel>(FileShareSwitchSpec(), kApiTagFeatureGet, kApiTagFeatureSet,
 *                                                        kIdFeatSwitchFileShare);
 *      panel->SetRequestSink(&AdminFrame::SensitiveRequestThunk, frame);
 *      panel->SetUiRequestHandler(&AdminFrame::FileShareUiRequestThunk, frame);
 *      //切到企业详情的「功能开关」子 tab 时（平台超管传企业 ID，企业管理员传 0）
 *      panel->Load(enterpriseId);
 *      //AdminFrame 收到开关的 DUIN_VALUECHANGED 时
 *      panel->OnSwitchToggled(n->extra != 0);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Screens/FileShareUi.h"

namespace balloonwjui { class DuiLabel; class DuiSwitch; }

namespace flamingoadmin {

// 一个功能开关的描述：设置项名与界面上用到的全部文字。字符串都指向静态存储，复制本结构体不复制文字。
struct FeatureSwitchSpec
{
    LPCTSTR m_key;             // 设置项名，如 adminapi::kSettingFileShareEnabled
    LPCTSTR m_name;            // 功能名称，用于完整样式、确认框标题与提示，如「文件共享」
    LPCTSTR m_compactName;     // 紧凑样式里的短名称（名称列宽 96 像素，放不下全称时用它）
    LPCTSTR m_description;     // 完整样式里名称下面的一行说明
    LPCTSTR m_confirmOnText;   // 打开前确认框的正文
    LPCTSTR m_confirmOffText;  // 关闭前确认框的正文
    bool    m_offIsDanger;     // 关闭的确认框是否用危险样式（关闭会中断正在进行的业务时为 true）
};

// 文件共享开关（fileshare.enabled）的描述。
const FeatureSwitchSpec& FileShareSwitchSpec();

// 客户端点对点直连开关（p2p.enabled）的描述。
const FeatureSwitchSpec& P2PSwitchSpec();

// 「邀请入群需被邀请人同意」开关（group.inviteconfirm，docs/邀请入群方案.md §3.5）的描述。
const FeatureSwitchSpec& InviteConfirmSwitchSpec();

// 「允许用户自助注册」开关（register.enabled）的描述：关闭后该企业不能在 PC 客户端自助注册，只能由管理员新建账号。
const FeatureSwitchSpec& RegisterSwitchSpec();

// 「群语音 / 群视频通话」开关（groupcall.enabled）的描述：关闭后该企业成员不能发起或加入群通话，进行中的群通话不受影响。
const FeatureSwitchSpec& GroupCallSwitchSpec();

// 「语音字幕」开关（caption.enabled）的描述：关闭后该企业成员录屏与屏幕共享时不再提供字幕，已生成的字幕文件不受影响。
const FeatureSwitchSpec& CaptionSwitchSpec();

// 「单聊密聊」开关（secretchat.enabled，docs/密聊方案.md 4.9）的描述：未设置时开启。关闭后该企业成员的客户端不再显示
// 「密聊」按钮，正在进行的密聊会被服务端结束（双方只能查看已有内容），因此关闭的确认框用危险样式。
const FeatureSwitchSpec& SecretChatSwitchSpec();

// 「帖子群」开关（threadgroup.enabled，docs/帖子群方案.md 4.7）的描述：未设置时开启。关闭后该企业成员的客户端隐藏全部
// 帖子群、不能再新建帖子群；已有的帖子与留言保留，重新打开后恢复。
const FeatureSwitchSpec& ThreadGroupSwitchSpec();

// 「机器人」总开关（bot.enabled，docs/机器人方案.md 4.1）的描述：未设置时开启。关闭后本企业全部机器人停止响应，客户端隐藏
// 全部机器人入口；会中断正在使用的业务，故关闭的确认框用危险样式。
const FeatureSwitchSpec& BotSwitchSpec();

// 「允许群主添加群机器人」开关（bot.groupbot）的描述：未设置时开启。
const FeatureSwitchSpec& BotGroupSwitchSpec();

// 「允许员工配置个人机器人」开关（bot.personal）的描述：未设置时开启。
const FeatureSwitchSpec& BotPersonalSwitchSpec();

// 「允许群主给群机器人配置外部服务」开关（bot.groupexternal）的描述：未设置时关闭。
const FeatureSwitchSpec& BotGroupExternalSwitchSpec();

// 「云端录制」开关（cloudrecord.enabled）的描述：未设置时关闭。关闭后该企业成员不能再开始云端录制、不能把录像保存到云端，
// 正在进行的云端录制与已有的云端录像不受影响。
const FeatureSwitchSpec& CloudRecordSwitchSpec();

// 「功能开关」面板。完整样式高 kPanelHeight，紧凑样式高 kCompactHeight。
class FeatureSwitchPanel : public balloonwjui::DuiVBox, public fsui::ConfirmTarget
{
public:
    // 完整样式的高度（像素）：开关行 44 + 间距 4 + 状态行 20
    static const int kPanelHeight = 68;

    // 紧凑样式的高度（像素）：只有一行
    static const int kCompactHeight = 36;

    // 加载状态
    enum State
    {
        kStateIdle    = 0,   // 尚未加载（还没调用 Load）
        kStateLoading = 1,   // 读设置的请求已发出，等待应答
        kStateLoaded  = 2,   // 已读到设置，开关可用
        kStateFailed  = 3,   // 读设置失败
        kStateSaving  = 4,   // 改设置的请求已发出，等待应答
    };

    // 构造时建好控件树，此时不发请求。
    //   spec：管哪个开关（复制保存；其中的字符串须为静态存储，见 FileShareSwitchSpec / P2PSwitchSpec）。
    //   getTag / setTag：本面板读、改设置所用的结果标签（每个面板用不同的标签，宿主据此分发应答）。
    //   switchCtrlId：开关的控件 ID，宿主据此把开关的 DUIN_VALUECHANGED 转给 OnSwitchToggled。
    //   compact：true 为紧凑样式（一行，不显示说明），false 为完整样式。
    FeatureSwitchPanel(const FeatureSwitchSpec& spec, ApiTag getTag, ApiTag setTag, int switchCtrlId,
                       bool compact = false);

    // 本面板应占的高度（像素）：完整样式为 kPanelHeight，紧凑样式为 kCompactHeight。
    int PanelHeight() const { return m_compact ? kCompactHeight : kPanelHeight; }

    // 设置请求出口与界面请求处理函数，含义见 FileShareUi.h。user 所有权归调用方。
    void SetRequestSink(fsui::RequestSinkFn fn, void* user) { m_port.SetRequestSink(fn, user); }
    void SetUiRequestHandler(fsui::UiRequestFn fn, void* user) { m_port.SetUiRequestHandler(fn, user); }

    // 读某个企业的设置；应答回来之前开关不可用。
    //   enterpriseId：平台管理员要设置的企业（大于 0）；企业管理员传 0，由服务端按本企业处理。
    void Load(long long enterpriseId);

    // 接收转交来的异步接口结果；标签不是本面板的 getTag / setTag 时不做任何事。
    //   tag：结果标签。http：HTTP 往返结果（仅本次调用内有效）。
    void OnApiResult(ApiTag tag, const HttpResult& http);

    // 用户拨动了开关（开关的 DUIN_VALUECHANGED）：请宿主弹确认框，确认后提交。设置尚未读到或正在提交时
    // 把开关恢复原状、不做别的事。
    //   on：开关拨动后的状态。
    void OnSwitchToggled(bool on);

    // 确认框的结果（fsui::ConfirmTarget）：确认则提交，取消则把开关恢复为服务端的状态。
    void OnConfirmResult(bool confirmed) override;

    // ---- 只读访问（宿主与单元测试用）----

    // 本面板管的开关。
    const FeatureSwitchSpec& Spec() const { return m_spec; }

    // 读设置所用的结果标签与改设置所用的结果标签。
    ApiTag GetTag() const { return m_getTag; }
    ApiTag SetTag() const { return m_setTag; }

    // 当前状态，取值见 State。
    int GetState() const { return m_state; }

    // 当前作用的企业（Load 时传入的值）。
    long long EnterpriseId() const { return m_enterpriseId; }

    // 开关此刻是否打开、是否可用。
    bool IsSwitchChecked() const;
    bool IsSwitchEnabled() const;

    // 状态行的文字。
    CString StatusText() const;

    // 开关控件（单元测试据此模拟用户拨动）。
    balloonwjui::DuiSwitch* Switch() { return m_switch; }

    // 设置已读到时状态行的文字（静态函数，便于单元测试）。
    //   enabled：这项功能是否开启。isDefault：是否为内置默认值（企业没有设置记录）。
    static CString StatusFor(bool enabled, bool isDefault);

private:
    // 读设置的应答。
    void OnLoaded(const HttpResult& http);

    // 改设置的应答。
    void OnSaved(const HttpResult& http);

    // 按状态与取值刷新开关与状态行。
    void UpdateView();

    fsui::Port               m_port;          // 请求出口与界面请求的转发端口
    FeatureSwitchSpec        m_spec;          // 本面板管的开关（构造时确定，之后不变）
    bool                     m_compact;       // 是否为紧凑样式（构造时确定，之后不变）
    ApiTag                   m_getTag;        // 读设置的结果标签
    ApiTag                   m_setTag;        // 改设置的结果标签
    balloonwjui::DuiSwitch*  m_switch;        // 开关（借用，所有权在控件树中）
    balloonwjui::DuiLabel*   m_status;        // 状态行（借用，所有权在控件树中）
    int                      m_state;         // 当前状态，取值见 State
    long long                m_enterpriseId;  // 当前作用的企业；企业管理员为 0
    bool                     m_enabled;       // 服务端的取值：这项功能是否开启
    bool                     m_isDefault;     // 服务端的取值是否为内置默认值
    bool                     m_requested;     // 等待确认或提交中的目标取值
    bool                     m_confirming;    // 是否有一个确认框尚未回告结果
    CString                  m_error;         // 最近一次读设置失败的原因（中文）
};

} // namespace flamingoadmin
