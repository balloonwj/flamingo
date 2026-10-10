/**
 *  flamingoAdmin —— 「功能开关」面板实现，说明见 FeatureSwitchPanel.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Screens/FeatureSwitchPanel.h"
#include "Theme/AdminTheme.h"
#include "Util/Logger.h"

#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiSwitch.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kSwitchRowH  = 44;   // 开关行高度：名称 22 + 说明 22
const int kNameH       = 22;   // 「文件共享」名称行高度
const int kDescH       = 22;   // 说明行高度
const int kStatusGap   = 4;    // 开关行与状态行之间的间距
const int kStatusH     = 20;   // 状态行高度
const int kSwitchW     = 46;   // 开关宽度（DuiSwitch 默认尺寸 46×24）
const int kSwitchH     = 24;   // 开关高度
const int kSwitchGap   = 16;   // 说明文字与开关之间的间距
const int kCompactNameW = 96;  // 紧凑样式里短名称的宽度：放得下「点对点直连」五个字，两个面板的名称列对齐

static_assert(kSwitchRowH + kStatusGap + kStatusH == FeatureSwitchPanel::kPanelHeight,
              "kPanelHeight must equal the sum of the row heights");
static_assert(kNameH + kDescH == kSwitchRowH, "switch row height mismatch");

// 设置项的两个取值
const TCHAR kValueOn[]  = _T("1");   // 开启
const TCHAR kValueOff[] = _T("0");   // 关闭

// 一个单行文字标签
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, COLORREF color, int pointSize, bool bold)
{
    auto label = std::make_unique<DuiLabel>();
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextPointSize(pointSize, bold);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    return label;
}

} // namespace

const FeatureSwitchSpec& FileShareSwitchSpec()
{
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingFileShareEnabled,
        _T("文件共享"),
        _T("文件共享"),
        _T("允许成员在聊天里共享本机的磁盘、文件夹或文件"),
        _T("打开后，该企业成员的客户端恢复显示文件共享入口，已有的共享随分享者上线逐步恢复可用。确定打开吗？"),
        _T("关闭后，该企业成员的客户端不再显示文件共享入口，正在进行的访问与传输会立即结束。")
        _T("已有的共享不会失效，对方看到「暂不可用」，重新打开后自动恢复。确定关闭吗？"),
        true,
    };
    return spec;
}

const FeatureSwitchSpec& P2PSwitchSpec()
{
    //服务端关闭 P2P 时只影响新建的会话，已经建立的直连不强制断开（docs/P2P通信方案.md 9.4），故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingP2PEnabled,
        _T("客户端点对点直连"),
        _T("点对点直连"),
        _T("成员之间的一对一传输优先在两台电脑之间直接连接，连不通时自动经服务器中转"),
        _T("打开后，该企业成员之间新建的一对一传输会优先尝试直接连接，连不通时仍经服务器中转。确定打开吗？"),
        _T("关闭后，该企业成员之间新建的一对一传输一律经服务器中转，正在进行的直接连接不会中断。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& InviteConfirmSwitchSpec()
{
    //关闭后已经发出、仍在有效期内的卡片照样可以点了入群（docs/邀请入群方案.md §3.5），不中断任何业务，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingInviteConfirm,
        _T("邀请入群需被邀请人同意"),
        _T("入群需同意"),   // 紧凑样式的名称列只放得下五个字（kCompactNameW），2026-10-05 由「邀请入群需同意」改短
        _T("开启后，被邀请人会在与邀请人的单聊里收到入群邀请卡片，点「加入群聊」后才入群；关闭时被邀请人直接入群"),
        _T("打开后，该企业成员被邀请入群时，会在与邀请人的单聊里收到入群邀请卡片，点「加入群聊」后才入群。确定打开吗？"),
        _T("关闭后，该企业成员被邀请时直接入群；已经发出、仍在有效期内的入群邀请卡片照样可以点击入群。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& RegisterSwitchSpec()
{
    //关闭只拦新的自助注册，已有账号的登录与使用不受影响，也不中断任何业务，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingRegisterEnabled,
        _T("允许用户自助注册"),
        _T("自助注册"),
        _T("关闭后，员工无法在客户端注册加入本企业，只能由管理员新建账号"),
        _T("打开后，员工可以在客户端填写本企业全称自助注册加入。确定打开吗？"),
        _T("关闭后，新员工将无法在客户端自助注册加入本企业，只能由管理员在组织架构中新建账号。已有账号不受影响。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& GroupCallSwitchSpec()
{
    //关闭只拦新的发起与加入，进行中的群通话持续到所有人离开（docs/群音视频通话方案.md 第 6 期），不中断业务，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingGroupCallEnabled,
        _T("群语音 / 群视频通话"),
        _T("群通话"),
        _T("开启后，成员可以在群聊里发起最多 16 人的语音或视频通话"),
        _T("打开后，该企业成员可以在群聊里发起或加入群语音、群视频通话。确定打开吗？"),
        _T("关闭后，该企业成员将不能再发起或加入群通话，客户端群聊窗里的「语音通话」「视频通话」按钮随之隐藏；正在进行的群通话不受影响。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& CaptionSwitchSpec()
{
    //识别全在成员自己的电脑上完成，关闭只是客户端不再提供字幕入口，不中断任何业务，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingCaptionEnabled,
        _T("语音字幕"),
        _T("语音字幕"),
        _T("开启后，成员录屏和共享屏幕时可以把说话内容识别成字幕，识别在本机完成"),
        _T("打开后，该企业成员录屏和共享屏幕时可以生成语音字幕。识别在成员自己的电脑上进行，声音不会上传。确定打开吗？"),
        _T("关闭后，该企业成员的客户端将不再提供语音字幕：录屏不再生成字幕，屏幕共享中也不再显示字幕；已经生成的字幕文件不受影响。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& SecretChatSwitchSpec()
{
    //关闭时服务端结束该企业成员参与的全部密聊（docs/密聊方案.md 4.9），会中断正在进行的业务，故关闭与文件共享一样用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingSecretChatEnabled,
        _T("单聊密聊"),
        _T("单聊密聊"),
        _T("关闭后员工客户端不再显示「密聊」按钮，正在进行的密聊会被结束（双方只能查看已有内容）"),
        _T("打开后，该企业成员的客户端在单聊窗里恢复显示「密聊」按钮，成员之间可以发起密聊。确定打开吗？"),
        _T("关闭后，该企业成员的客户端不再显示「密聊」按钮，也不能再发起密聊；该企业成员正在进行的密聊会被立即结束，")
        _T("双方只能查看已有内容。确定关闭吗？"),
        true,
    };
    return spec;
}

const FeatureSwitchSpec& ThreadGroupSwitchSpec()
{
    //关闭只隐藏帖子群、服务端拒绝新的帖子群请求，帖子与留言都保留，重新打开后恢复（docs/帖子群方案.md 4.7），
    //不结束任何会话，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingThreadGroupEnabled,
        _T("帖子群"),
        _T("帖子群"),
        _T("关闭后员工客户端隐藏全部帖子群，不能再新建帖子群；已有的帖子与留言保留，重新打开后恢复"),
        _T("打开后，该企业成员的客户端恢复显示帖子群，建群时可以选择建帖子群，已有的帖子与留言照常可见。确定打开吗？"),
        _T("关闭后，该企业成员的客户端将隐藏全部帖子群，也不能再新建帖子群；已有的帖子与留言保留在服务器上，")
        _T("重新打开后恢复。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& BotSwitchSpec()
{
    //关闭时本企业全部机器人（企业、群、个人机器人）立即停止响应，会中断正在使用的业务，故关闭用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingBotEnabled,
        _T("机器人"),
        _T("机器人"),
        _T("关闭后本企业全部机器人停止响应，员工客户端隐藏全部机器人入口；机器人的配置保留，重新打开后恢复"),
        _T("打开后，本企业的企业机器人、群机器人与个人机器人恢复响应，员工客户端恢复显示机器人入口。确定打开吗？"),
        _T("关闭后，本企业全部机器人停止响应（企业机器人、群机器人与个人机器人都不再回复、不再发定时消息、")
        _T("不再接收外部系统推送），员工客户端隐藏全部机器人入口；机器人的配置保留，重新打开后恢复。确定关闭吗？"),
        true,
    };
    return spec;
}

const FeatureSwitchSpec& BotGroupSwitchSpec()
{
    //关闭只是群主不能再添加、修改群机器人，不中断别的业务，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingBotGroup,
        _T("允许群主添加群机器人"),
        _T("群机器人"),
        _T("开启后，群主可以在群聊窗里给本群添加机器人"),
        _T("打开后，本企业的群主可以在群聊窗里给本群添加、修改机器人。确定打开吗？"),
        _T("关闭后，本企业的群主不能再给群添加或修改群机器人，客户端隐藏相应入口。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& BotPersonalSwitchSpec()
{
    //关闭只是员工不能再配置个人机器人，不中断别的业务，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingBotPersonal,
        _T("允许员工配置个人机器人"),
        _T("个人机器人"),
        _T("开启后，员工可以配置在自己离开或忙碌时代为回复单聊的个人机器人"),
        _T("打开后，本企业员工可以在客户端配置自己的个人机器人。确定打开吗？"),
        _T("关闭后，本企业员工不能再配置个人机器人，客户端隐藏「我的机器人」入口。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& BotGroupExternalSwitchSpec()
{
    //关闭只是群主不能再给群机器人配置外部服务，不中断别的业务，故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingBotGroupExternal,
        _T("允许群主给群机器人配置外部服务"),
        _T("群外部服务"),   // 紧凑样式的名称列只放得下五个字（kCompactNameW）
        _T("开启后，群主可以给群机器人添加「外部系统推送」与「呼叫外部服务」两类规则"),
        _T("打开后，本企业的群主可以给群机器人配置外部系统推送与呼叫外部服务（地址须在外部服务域名白名单内），")
        _T("群消息可能会发送到外部服务。确定打开吗？"),
        _T("关闭后，本企业的群主不能再给群机器人添加外部系统推送与呼叫外部服务的规则。确定关闭吗？"),
        false,
    };
    return spec;
}

const FeatureSwitchSpec& CloudRecordSwitchSpec()
{
    //关闭只拦新的云端录制与上传，正在录的继续录完、已有的录像照常可看（docs/屏幕共享与录制方案.md 12.6.5），故关闭不用危险样式
    static const FeatureSwitchSpec spec =
    {
        adminapi::kSettingCloudRecordEnabled,
        _T("云端录制"),
        _T("云端录制"),
        _T("开启后，主持人可以在屏幕共享中开始云端录制，成员可以把录像保存到云端"),
        _T("打开后，该企业的屏幕共享主持人可以开始云端录制，成员录屏时可以选择保存到云端。录像占用企业的云端录像空间。确定打开吗？"),
        _T("关闭后，该企业成员将不能再开始云端录制、不能把录像保存到云端；正在进行的云端录制会录完，已有的云端录像照常可以观看。确定关闭吗？"),
        false,
    };
    return spec;
}

FeatureSwitchPanel::FeatureSwitchPanel(const FeatureSwitchSpec& spec, ApiTag getTag, ApiTag setTag, int switchCtrlId,
                                       bool compact)
    : m_spec(spec)
    , m_compact(compact)
    , m_getTag(getTag)
    , m_setTag(setTag)
    , m_switch(nullptr)
    , m_status(nullptr)
    , m_state(kStateIdle)
    , m_enterpriseId(0)
    , m_enabled(false)
    , m_isDefault(false)
    , m_requested(false)
    , m_confirming(false)
{
    if (compact)
    {
        //紧凑样式：[短名称] [取值来历] …… [开关]，一行
        auto row = std::make_unique<DuiHBox>();
        row->SetGap(kSwitchGap);
        row->AddChild(MakeLabel(m_spec.m_compactName, theme::kInk700, theme::kFontSizeNormal, true),
                      Hint().Fixed(kCompactNameW));
        auto status = MakeLabel(_T(""), theme::kInk500, theme::kFontSizeSmall, false);
        m_status = status.get();
        row->AddChild(std::move(status), Hint().Weight(1));
        auto sw = std::make_unique<DuiSwitch>();
        sw->SetCtrlId(switchCtrlId);
        m_switch = sw.get();
        row->AddChild(std::move(sw), Hint().Fixed(kSwitchW).Margin(0, (kCompactHeight - kSwitchH) / 2, 0,
                                                                   (kCompactHeight - kSwitchH) / 2));
        AddChild(std::move(row), Hint().Fixed(kCompactHeight));
        UpdateView();
        return;
    }

    //开关行：[名称 / 说明] …… [开关]
    auto row = std::make_unique<DuiHBox>();
    row->SetGap(kSwitchGap);
    {
        auto texts = std::make_unique<DuiVBox>();
        texts->AddChild(MakeLabel(m_spec.m_name, theme::kInk700, theme::kFontSizeNormal, true), Hint().Fixed(kNameH));
        texts->AddChild(MakeLabel(m_spec.m_description, theme::kInk400, theme::kFontSizeSmall, false),
                        Hint().Fixed(kDescH));
        row->AddChild(std::move(texts), Hint().Weight(1));

        auto sw = std::make_unique<DuiSwitch>();
        sw->SetCtrlId(switchCtrlId);
        m_switch = sw.get();
        //开关比这一行矮，上下各留一半差值使其垂直居中
        row->AddChild(std::move(sw), Hint().Fixed(kSwitchW).Margin(0, (kSwitchRowH - kSwitchH) / 2, 0,
                                                                   (kSwitchRowH - kSwitchH) / 2));
    }
    AddChild(std::move(row), Hint().Fixed(kSwitchRowH).Margin(0, 0, 0, kStatusGap));

    auto status = MakeLabel(_T(""), theme::kInk500, theme::kFontSizeSmall, false);
    m_status = status.get();
    AddChild(std::move(status), Hint().Fixed(kStatusH));

    UpdateView();
}

void FeatureSwitchPanel::Load(long long enterpriseId)
{
    m_enterpriseId = (enterpriseId > 0) ? enterpriseId : 0;
    m_state = kStateLoading;
    m_error.Empty();
    m_confirming = false;
    FA_LOGI(_T("feature switch panel: load settings, key: %s, enterprise: %lld, tag: %d"), m_spec.m_key, m_enterpriseId,
            static_cast<int>(m_getTag));
    HttpRequest request = adminapi::MakeFeatureSettingsRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                               m_enterpriseId);
    m_port.Send(request, m_getTag);
    UpdateView();
}

void FeatureSwitchPanel::OnApiResult(ApiTag tag, const HttpResult& http)
{
    if (tag == m_getTag)
        OnLoaded(http);
    else if (tag == m_setTag)
        OnSaved(http);
}

void FeatureSwitchPanel::OnLoaded(const HttpResult& http)
{
    const adminapi::FeatureSettingsResult r = adminapi::ParseFeatureSettingsResponse(http);
    //弹窗关掉后又为另一个企业打开时，前一个企业的应答可能晚到：企业对不上的应答丢弃
    if (r.Succeeded() && m_enterpriseId > 0 && r.m_enterpriseId != m_enterpriseId)
    {
        FA_LOGW(_T("feature switch panel: stale settings dropped, enterprise %lld != %lld"), r.m_enterpriseId,
                m_enterpriseId);
        return;
    }
    if (m_state != kStateLoading)
        return;
    if (!r.Succeeded())
    {
        m_state = kStateFailed;
        m_error = r.m_message;
        FA_LOGW(_T("feature switch panel: load settings failed, code: %d"), r.m_code);
        UpdateView();
        return;
    }
    adminapi::FeatureSettingItem item;
    if (!r.Find(m_spec.m_key, item))
    {
        //服务端没有给出这一项（版本不一致）：按读取失败处理，不让管理员在不知道当前取值时拨动开关
        m_state = kStateFailed;
        m_error = CString(_T("服务端没有返回")) + m_spec.m_name + _T("的设置");
        FA_LOGW(_T("feature switch panel: %s missing in response"), m_spec.m_key);
        UpdateView();
        return;
    }
    m_enabled = (item.m_value == kValueOn);
    m_isDefault = item.m_isDefault;
    m_state = kStateLoaded;
    FA_LOGI(_T("feature switch panel: settings loaded, enterprise: %lld, %s: %d, default: %d"),
            r.m_enterpriseId, m_spec.m_key, m_enabled ? 1 : 0, m_isDefault ? 1 : 0);
    UpdateView();
}

void FeatureSwitchPanel::OnSaved(const HttpResult& http)
{
    const adminapi::FeatureSetResult r = adminapi::ParseFeatureSetResponse(http);
    if (r.Succeeded() && m_enterpriseId > 0 && r.m_enterpriseId != m_enterpriseId)
    {
        FA_LOGW(_T("feature switch panel: stale set result dropped, enterprise %lld != %lld"), r.m_enterpriseId,
                m_enterpriseId);
        return;
    }
    if (m_state != kStateSaving)
        return;
    m_state = kStateLoaded;
    if (!r.Succeeded())
    {
        FA_LOGW(_T("feature switch panel: set failed, code: %d"), r.m_code);
        m_port.Toast(_T("切换失败：") + r.m_message, true);
        UpdateView();
        return;
    }
    m_enabled = r.m_enabled;
    //写过一次之后企业就有了自己的设置记录，不再是默认值
    m_isDefault = false;
    FA_LOGI(_T("feature switch panel: %s set to %d, enterprise: %lld"), m_spec.m_key, m_enabled ? 1 : 0,
            r.m_enterpriseId);
    m_port.Toast(CString(m_enabled ? _T("已打开") : _T("已关闭")) + m_spec.m_name, false);
    UpdateView();
}

void FeatureSwitchPanel::OnSwitchToggled(bool on)
{
    //设置还没读到、正在提交，或上一个确认框还没有结果：拨回原状
    if (m_state != kStateLoaded || m_confirming)
    {
        UpdateView();
        return;
    }
    if (on == m_enabled)
        return;
    m_requested = on;
    m_confirming = true;
    if (on)
        m_port.Confirm(CString(_T("打开")) + m_spec.m_name, m_spec.m_confirmOnText, _T("打开"), false, this);
    else
        m_port.Confirm(CString(_T("关闭")) + m_spec.m_name, m_spec.m_confirmOffText, _T("关闭"), m_spec.m_offIsDanger, this);
}

void FeatureSwitchPanel::OnConfirmResult(bool confirmed)
{
    if (!m_confirming)
        return;
    m_confirming = false;
    if (!confirmed || m_state != kStateLoaded)
    {
        //取消：开关恢复为服务端的状态
        UpdateView();
        return;
    }
    m_state = kStateSaving;
    FA_LOGI(_T("feature switch panel: set %s to %d, enterprise: %lld"), m_spec.m_key, m_requested ? 1 : 0,
            m_enterpriseId);
    HttpRequest request = adminapi::MakeFeatureSetRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                          m_enterpriseId, m_spec.m_key,
                                                          m_requested ? kValueOn : kValueOff);
    m_port.Send(request, m_setTag);
    UpdateView();
}

void FeatureSwitchPanel::UpdateView()
{
    if (m_switch != nullptr)
    {
        //提交中显示要设成的状态，其余显示服务端的状态
        const bool checked = (m_state == kStateSaving || m_confirming) ? m_requested : m_enabled;
        m_switch->SetChecked(checked, false, false);
        m_switch->SetEnabled(m_state == kStateLoaded);
    }
    if (m_status != nullptr)
        m_status->SetText(StatusText());
}

bool FeatureSwitchPanel::IsSwitchChecked() const
{
    return m_switch != nullptr && m_switch->IsChecked();
}

bool FeatureSwitchPanel::IsSwitchEnabled() const
{
    return m_switch != nullptr && m_switch->IsEnabled();
}

CString FeatureSwitchPanel::StatusText() const
{
    switch (m_state)
    {
    //还没调用 Load
    case kStateIdle:
        return CString();
    //读设置的请求已发出
    case kStateLoading:
        return CString(_T("正在读取…"));
    //读设置失败
    case kStateFailed:
        return _T("读取失败：") + m_error;
    //提交中
    case kStateSaving:
        return CString(_T("正在保存…"));
    //已读到设置
    case kStateLoaded:
    default:
        return StatusFor(m_enabled, m_isDefault);
    }
}

CString FeatureSwitchPanel::StatusFor(bool enabled, bool isDefault)
{
    if (isDefault)
        return CString(enabled ? _T("当前为默认设置：开启") : _T("当前为默认设置：关闭"));
    return CString(enabled ? _T("已由管理员设置为开启") : _T("已由管理员设置为关闭"));
}

} // namespace flamingoadmin
