/**
 *  flamingoAdmin —— 机器人页实现，说明见 EnterpriseBotScreen.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Screens/EnterpriseBotScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/ActionTable.h"
#include "Widgets/ColumnHeader.h"
#include "Widgets/UiHelpers.h"
#include "Util/BotRuleText.h"
#include "Util/Logger.h"
#include "Util/TimeText.h"   // 时间显示格式

#include "Controls/Basic/DuiButton.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiComboBox.h"

using namespace balloonwjui;

namespace flamingoadmin {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素），与企业问答页一致 ----
const int kHeaderH       = 100;  // 页头高度
const int kSideMargin    = 24;   // 内容区左 / 右 / 下统一外边距
const int kRowGap        = 12;   // 各行之间的间距
const int kBarH          = 36;   // 企业选择行与工具行的高度
const int kPickerLabelW  = 40;   // 「企业」标签宽度
const int kPickerW       = 300;  // 企业选择框宽度
const int kButtonW       = 96;   // 「刷新」按钮宽度
// 「新建机器人」按钮宽度：五个字加两侧留白。2026-10-08 实机核对时原来的 96 放不下，文字顶到了按钮边缘
const int kAddBtnW       = 120;
// 「外部服务域名白名单」按钮宽度：九个字加两侧留白（原来的 150 同样放不下）
const int kDomainsBtnW   = 190;
const int kButtonGap     = 8;    // 按钮之间的间距
const int kTableHeaderH  = 40;   // 表头高度
const int kRowHeight     = 60;   // 表格行高（名称与简介两行）
const int kCellPad       = 14;   // 单元格左右内边距
const int kLineH         = 22;   // 行内名称、简介各占的高度
const int kAvatarSize    = 32;   // 名称一栏左侧头像的边长
const int kAvatarGap     = 10;   // 头像与名称之间的间距
const int kChipH         = 20;   // 状态徽章的高度
const int kColScope      = 220;  // 应用范围
const int kColAllowOff   = 96;   // 群主可关闭
const int kColRules      = 70;   // 规则数
const int kColStatus     = 80;   // 状态
const int kColLastFire   = 150;  // 最近触发
const int kColActions    = 150;  // 操作（编辑 / 停用或启用 / 删除）

// 表格的列下标，与 BuildColumns 的顺序一致
enum BotColumn
{
    kBcName     = 0,   // 头像、名称与简介（占满剩余宽度）
    kBcScope    = 1,   // 应用范围
    kBcAllowOff = 2,   // 群主可关闭
    kBcRules    = 3,   // 规则数
    kBcStatus   = 4,   // 状态徽章
    kBcLastFire = 5,   // 最近触发时间
    kBcActions  = 6,   // 操作
};

// 企业选择框：拉企业列表时的状态筛选（-1 表示全部状态）与条数
const int kEntStatusAll     = -1;
const int kEntPickerPageMax = 1000;

// 头像底色缺省时用的颜色（协议文档 25.1 的示例色）
const COLORREF kDefaultAvatarColor = RGB(0x1E, 0x88, 0xE5);

// Unix 毫秒换算成 FILETIME（自 1601-01-01 起的 100 纳秒数）所需的常量
const ULONGLONG k100nsPerMs     = 10000ULL;
const ULONGLONG kEpochDiff100ns = 116444736000000000ULL;

// 把 Unix 毫秒格式化成本地时间「YYYY-MM-DD HH:MM」；不大于 0 时显示 zeroText
CString FormatTimeMs(long long ms, LPCTSTR zeroText)
{
    if (ms <= 0)
        return CString(zeroText);
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(ms) * k100nsPerMs + kEpochDiff100ns;
    FILETIME utc;
    utc.dwLowDateTime = u.LowPart;
    utc.dwHighDateTime = u.HighPart;
    FILETIME local;
    SYSTEMTIME st;
    if (!::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &st))
        return CString(zeroText);
    return timetext::FormatSystemTime(st, timetext::TimeStyle::Minute);
}

std::vector<ui::ColumnSpec> BuildColumns()
{
    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("名称"), 0, true));
    cols.push_back(ui::ColumnSpec(_T("应用范围"), kColScope));
    cols.push_back(ui::ColumnSpec(_T("群主可关闭"), kColAllowOff));
    cols.push_back(ui::ColumnSpec(_T("规则数"), kColRules));
    cols.push_back(ui::ColumnSpec(_T("状态"), kColStatus));
    cols.push_back(ui::ColumnSpec(_T("最近触发"), kColLastFire));
    cols.push_back(ui::ColumnSpec(_T("操作"), kColActions));
    return cols;
}

std::unique_ptr<DuiVBox> MakeCard()
{
    std::unique_ptr<DuiVBox> card(new DuiVBox());
    card->SetBgColor(theme::kInk0);
    card->SetCornerRadius(theme::kRadiusLg);
    card->SetBorderColor(theme::kInk100);
    card->SetBorderWidth(1.0f);
    return card;
}

std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, COLORREF color, DWORD align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE);
    return label;
}

std::unique_ptr<DuiButton> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetVariant(variant);
    button->SetTextPointSize(theme::kFontSizeNormal);
    return button;
}

// 名称下面那一行：仓库通知的机器人写明只读，其余显示简介（换行换成空格）
CString SecondLine(const botapi::BotItem& bot)
{
    if (botrule::IsReadOnlySource(bot.m_source))
    {
        const CString repo = (bot.m_source == botapi::kBotSourceGithub) ? CString(_T("GitHub")) : CString(_T("GitLab"));
        return repo + _T(" 仓库通知（在「仓库通知」页修改，本页只读）");
    }
    CString desc = bot.m_desc;
    desc.Replace(_T("\r\n"), _T(" "));
    desc.Replace(_T('\n'), _T(' '));
    desc.Replace(_T('\r'), _T(' '));
    return desc.IsEmpty() ? CString(_T("（没有简介）")) : desc;
}

} // namespace

EnterpriseBotScreen::EnterpriseBotScreen()
    : m_sink(nullptr)
    , m_sinkUser(nullptr)
    , m_uiHandler(nullptr)
    , m_uiHandlerUser(nullptr)
    , m_pickerRow(nullptr)
    , m_picker(nullptr)
    , m_header(nullptr)
    , m_table(nullptr)
    , m_isPlatform(false)
    , m_enterpriseId(0)
    , m_loadState(kLoadIdle)
    , m_domainsLoaded(false)
    , m_pendingConfirm(kPendingNone)
    , m_pendingBotId(0)
    , m_detailBotId(0)
    , m_saving(false)
    , m_trying(false)
    , m_savingDomainsBusy(false)
{
    // ---- 页头 ----
    std::unique_ptr<ui::PageHeader> header(new ui::PageHeader());
    header->SetTexts(_T("机器人"), _T("企业机器人"),
                     _T("配置在本企业群里自动应答、定时提醒、接收外部系统推送的机器人；群主与员工的机器人在客户端配置"));
    AddChild(std::move(header), Hint().Fixed(kHeaderH));

    // ---- 企业选择行（仅平台超管）----
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->AddChild(MakeLabel(_T("企业"), theme::kInk500, DT_LEFT), Hint().Fixed(kPickerLabelW));
        std::unique_ptr<DuiComboBox> picker(new DuiComboBox());
        picker->SetCtrlId(kIdBotEnterprise);
        picker->SetEditable(true);
        picker->SetIncrementalSearch(true);
        picker->SetIncrementalSearchSubstring(true);
        m_picker = picker.get();
        row->AddChild(std::move(picker), Hint().Fixed(kPickerW));
        row->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
        m_pickerRow = row.get();
        AddChild(std::move(row), Hint().Fixed(kBarH).Margin(kSideMargin, 0, kSideMargin, kRowGap));
    }

    // ---- 工具行：…… 「刷新」「外部服务域名白名单」「新建机器人」----
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(kButtonGap);
        row->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
        row->AddChild(MakeButton(kIdBotRefresh, _T("刷新"), DuiButton::Variant::Default), Hint().Fixed(kButtonW));
        row->AddChild(MakeButton(kIdBotDomains, _T("外部服务域名白名单"), DuiButton::Variant::Default),
                      Hint().Fixed(kDomainsBtnW));
        row->AddChild(MakeButton(kIdBotAdd, _T("新建机器人"), DuiButton::Variant::Primary), Hint().Fixed(kAddBtnW));
        AddChild(std::move(row), Hint().Fixed(kBarH).Margin(kSideMargin, 0, kSideMargin, kRowGap));
    }

    // ---- 表格卡：表头 + 表格 ----
    std::unique_ptr<DuiVBox> card = MakeCard();
    {
        std::unique_ptr<ui::ColumnHeader> head(new ui::ColumnHeader());
        head->SetColumns(BuildColumns());
        head->SetCellPadding(kCellPad);
        m_header = head.get();
        card->AddChild(std::move(head), Hint().Fixed(kTableHeaderH));

        std::unique_ptr<ui::ActionTable> table(new ui::ActionTable());
        table->SetCtrlId(kIdBotTable);
        table->SetRowHeight(kRowHeight);
        table->SetRowPainter(&EnterpriseBotScreen::PaintRowThunk, this);
        table->SetActionAreaWidth(kColActions);
        //顺序与 RowAction 一致；「停用」「启用」按行二选一，不显示的链接让出位置
        table->AddAction(_T("编辑"), theme::kColorInfo);
        table->AddAction(_T("停用"), theme::kColorWarn);
        table->AddAction(_T("启用"), theme::kColorOk);
        table->AddAction(_T("删除"), theme::kColorDanger);
        table->SetActionVisibility(&EnterpriseBotScreen::ActionVisibleThunk, this);
        table->SetCollapseHiddenActions(true);
        table->SetActionCallback(&EnterpriseBotScreen::OnRowActionThunk, this);
        m_table = table.get();
        card->AddChild(std::move(table), Hint().Weight(1));
    }
    AddChild(std::move(card), Hint().Weight(1).Margin(kSideMargin, 0, kSideMargin, kSideMargin));
}

void EnterpriseBotScreen::SetRequestSink(RequestSinkFn fn, void* user)
{
    m_sink = fn;
    m_sinkUser = user;
}

void EnterpriseBotScreen::SetUiRequestHandler(UiRequestFn fn, void* user)
{
    m_uiHandler = fn;
    m_uiHandlerUser = user;
}

void EnterpriseBotScreen::OnEnter()
{
    const AdminSession& session = AdminSession::Instance();
    //换了登录身份：清掉上一个身份留下的企业列表、机器人、群与白名单
    if (session.Token() != m_sessionToken)
    {
        m_enterpriseIds.clear();
        m_enterpriseId = 0;
        m_bots.clear();
        m_groups.clear();
        m_domains.clear();
        m_domainsLoaded = false;
        m_loadState = kLoadIdle;
        m_pendingConfirm = kPendingNone;
        m_pendingBotId = 0;
        m_detailBotId = 0;
        m_saving = false;
        m_trying = false;
        m_savingDomainsBusy = false;
        if (m_picker != nullptr)
            m_picker->ResetContent();
        m_sessionToken = session.Token();
    }
    m_isPlatform = session.IsPlatformAdmin();
    if (m_pickerRow != nullptr)
    {
        m_pickerRow->SetVisible(m_isPlatform);
        //SetVisible 不会重新排版：对本界面按当前矩形重新排一次（开发约定文档 UI 约定第 5 条）
        ForceLayout(GetRect());
    }
    FA_LOGI(_T("enter enterprise bot screen, platform admin: %d, enterprise: %lld"), m_isPlatform ? 1 : 0, m_enterpriseId);
    if (m_isPlatform && m_enterpriseIds.empty())
        StartLoadEnterprises();
    //每次进入都刷新：别的管理员可能刚改过；平台超管未选企业时 StartLoad 不发请求
    StartLoad();
    UpdateView();
}

long long EnterpriseBotScreen::RequestEnterpriseId() const
{
    return m_isPlatform ? m_enterpriseId : 0;
}

bool EnterpriseBotScreen::CheckEnterpriseChosen()
{
    if (m_isPlatform && m_enterpriseId <= 0)
    {
        RaiseToast(_T("请先选择企业"), true);
        return false;
    }
    return true;
}

void EnterpriseBotScreen::Send(HttpRequest& request, ApiTag tag)
{
    request.m_tag = static_cast<UINT_PTR>(tag);
    if (m_sink == nullptr)
    {
        FA_LOGW(_T("enterprise bot screen: no request sink, request dropped, tag: %d"), static_cast<int>(tag));
        return;
    }
    m_sink(m_sinkUser, request);
}

void EnterpriseBotScreen::Raise(const UiRequest& request)
{
    if (m_uiHandler == nullptr)
    {
        FA_LOGW(_T("enterprise bot screen: no ui handler, ui request dropped, kind: %d"), request.m_kind);
        return;
    }
    m_uiHandler(m_uiHandlerUser, request);
}

void EnterpriseBotScreen::RaiseToast(const CString& text, bool isError)
{
    UiRequest req;
    req.m_kind = kUiToast;
    req.m_text = text;
    req.m_isError = isError;
    Raise(req);
}

void EnterpriseBotScreen::StartLoadEnterprises()
{
    HttpRequest request = adminapi::MakeEnterpriseListRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                              CString(), kEntStatusAll, 1, kEntPickerPageMax);
    Send(request, kApiTagBotEnterprises);
}

void EnterpriseBotScreen::StartLoad()
{
    if (m_isPlatform && m_enterpriseId <= 0)
        return;
    m_loadState = kLoadLoading;
    const CString token = AdminSession::Instance().Token();
    HttpRequest list = botapi::MakeBotListRequest(ServerConfig(), token, RequestEnterpriseId());
    Send(list, kApiTagBotList);
    HttpRequest groups = botapi::MakeBotGroupsRequest(ServerConfig(), token, RequestEnterpriseId());
    Send(groups, kApiTagBotGroups);
    HttpRequest domains = botapi::MakeBotDomainsRequest(ServerConfig(), token, RequestEnterpriseId());
    Send(domains, kApiTagBotDomains);
}

void EnterpriseBotScreen::OnApiResult(ApiTag tag, const HttpResult& http)
{
    switch (tag)
    {
    //企业选择框的企业列表
    case kApiTagBotEnterprises:
    {
        const EnterpriseListResult r = adminapi::ParseEnterpriseListResponse(http);
        if (!r.Succeeded())
        {
            FA_LOGW(_T("enterprise bot screen: load enterprises failed, code: %d"), r.m_code);
            RaiseToast(_T("企业列表加载失败：") + r.m_message, true);
            break;
        }
        m_enterpriseIds.clear();
        if (m_picker != nullptr)
            m_picker->ResetContent();
        int selected = -1;
        for (size_t i = 0; i < r.m_list.size(); ++i)
        {
            const long long id = _ttoi64(r.m_list[i].m_id);
            m_enterpriseIds.push_back(id);
            if (m_picker != nullptr)
                m_picker->AddString(r.m_list[i].m_name);
            if (id == m_enterpriseId)
                selected = static_cast<int>(i);
        }
        if (m_picker != nullptr && selected >= 0)
            m_picker->SetCurSel(selected, false);
        UpdateView();
        break;
    }

    //机器人列表
    case kApiTagBotList:
    {
        const botapi::BotListResult r = botapi::ParseBotListResponse(http);
        if (!r.Succeeded())
        {
            m_loadState = kLoadFailed;
            m_loadError = r.m_message;
            FA_LOGW(_T("enterprise bot screen: load list failed, code: %d"), r.m_code);
        }
        else
        {
            m_loadState = kLoadLoaded;
            m_loadError.Empty();
            m_bots = r.m_bots;
            FA_LOGI(_T("enterprise bot screen: list loaded, count: %u"), static_cast<unsigned>(m_bots.size()));
        }
        UpdateView();
        break;
    }

    //本企业的群：应用范围一栏显示群名、编辑窗选择群
    case kApiTagBotGroups:
    {
        const botapi::BotGroupsResult r = botapi::ParseBotGroupsResponse(http);
        if (!r.Succeeded())
        {
            FA_LOGW(_T("enterprise bot screen: load groups failed, code: %d"), r.m_code);
            break;
        }
        m_groups = r.m_groups;
        UiRequest req;
        req.m_kind = kUiGroupsLoaded;
        Raise(req);
        UpdateView();
        break;
    }

    //外部服务的域名白名单：编辑窗本地校验外部服务地址用
    case kApiTagBotDomains:
    {
        const botapi::BotDomainsResult r = botapi::ParseBotDomainsResponse(http);
        if (!r.Succeeded())
        {
            FA_LOGW(_T("enterprise bot screen: load domains failed, code: %d"), r.m_code);
            break;
        }
        m_domains = r.m_domains;
        m_domainsLoaded = true;
        UiRequest req;
        req.m_kind = kUiDomainsLoaded;
        Raise(req);
        break;
    }

    //机器人详情：拉到后打开编辑窗
    case kApiTagBotDetail:
    {
        const botapi::BotDetailResult r = botapi::ParseBotDetailResponse(http);
        const long long expected = m_detailBotId;
        m_detailBotId = 0;
        if (!r.Succeeded())
        {
            RaiseToast(_T("机器人详情加载失败：") + r.m_message, true);
            FA_LOGW(_T("enterprise bot screen: load detail failed, code: %d"), r.m_code);
            break;
        }
        //点了别的机器人的「编辑」之后，前一个详情晚到：丢弃
        if (expected <= 0 || r.m_bot.m_botId != expected)
        {
            FA_LOGW(_T("enterprise bot screen: stale detail dropped, bot %lld != %lld"), r.m_bot.m_botId, expected);
            break;
        }
        UiRequest req;
        req.m_kind = kUiOpenEditModal;
        req.m_bot = r.m_bot;
        Raise(req);
        break;
    }

    //新建或修改：成功关编辑窗并重拉，失败在编辑窗里提示
    case kApiTagBotSave:
    {
        m_saving = false;
        const botapi::BotSaveResult r = botapi::ParseBotSaveResponse(http);
        UiRequest req;
        if (!r.Succeeded())
        {
            req.m_kind = kUiModalError;
            req.m_text = r.m_message;
            Raise(req);
            FA_LOGW(_T("enterprise bot screen: save failed, code: %d"), r.m_code);
            break;
        }
        req.m_kind = kUiCloseModal;
        Raise(req);
        RaiseToast(_T("已保存"), false);
        FA_LOGI(_T("enterprise bot screen: saved, botid: %lld"), r.m_botId);
        StartLoad();
        break;
    }

    //删除：成功后重拉
    case kApiTagBotDelete:
    {
        const botapi::BotResultBase r = botapi::ParseBotWriteResponse(http);
        if (!r.Succeeded())
        {
            RaiseToast(_T("删除失败：") + r.m_message, true);
            FA_LOGW(_T("enterprise bot screen: delete failed, code: %d"), r.m_code);
            break;
        }
        RaiseToast(_T("已删除"), false);
        StartLoad();
        break;
    }

    //停用 / 启用：成功后重拉
    case kApiTagBotSetStatus:
    {
        const botapi::BotResultBase r = botapi::ParseBotWriteResponse(http);
        if (!r.Succeeded())
        {
            RaiseToast(_T("修改状态失败：") + r.m_message, true);
            FA_LOGW(_T("enterprise bot screen: set status failed, code: %d"), r.m_code);
            break;
        }
        RaiseToast(_T("状态已更新"), false);
        StartLoad();
        break;
    }

    //「试一试」：结果交给编辑窗显示
    case kApiTagBotTry:
    {
        m_trying = false;
        const botapi::BotTryResult r = botapi::ParseBotTryResponse(http);
        UiRequest req;
        req.m_kind = kUiTryResult;
        if (!r.Succeeded())
        {
            req.m_isError = true;
            req.m_text = _T("试一试失败：") + r.m_message;
            FA_LOGW(_T("enterprise bot screen: try failed, code: %d"), r.m_code);
        }
        else
        {
            req.m_text = botrule::TryResultText(r);
            FA_LOGI(_T("enterprise bot screen: try result, kind: %d, reason: %d"), r.m_kind, r.m_reason);
        }
        Raise(req);
        break;
    }

    //重新生成令牌与密钥：结果交给编辑窗显示
    case kApiTagBotResetToken:
    {
        const botapi::BotResetTokenResult r = botapi::ParseBotResetTokenResponse(http);
        const long long botId = m_pendingBotId;
        m_pendingBotId = 0;
        if (!r.Succeeded())
        {
            RaiseToast(_T("重新生成失败：") + r.m_message, true);
            FA_LOGW(_T("enterprise bot screen: reset token failed, code: %d"), r.m_code);
            break;
        }
        UiRequest req;
        req.m_kind = kUiSecretsReset;
        req.m_bot.m_botId = botId;
        req.m_bot.m_webhookToken = r.m_webhookToken;
        req.m_bot.m_secret = r.m_secret;
        Raise(req);
        RaiseToast(_T("已重新生成令牌与签名密钥，旧令牌已失效"), false);
        FA_LOGI(_T("enterprise bot screen: token reset, botid: %lld"), botId);
        break;
    }

    //保存白名单：成功关窗口并更新本地白名单，失败在窗口里提示
    case kApiTagBotSetDomains:
    {
        m_savingDomainsBusy = false;
        const botapi::BotResultBase r = botapi::ParseBotWriteResponse(http);
        UiRequest req;
        if (!r.Succeeded())
        {
            req.m_kind = kUiDomainsModalError;
            req.m_text = r.m_message;
            Raise(req);
            FA_LOGW(_T("enterprise bot screen: set domains failed, code: %d"), r.m_code);
            break;
        }
        m_domains = m_savingDomains;
        m_domainsLoaded = true;
        m_savingDomains.clear();
        req.m_kind = kUiCloseDomainsModal;
        Raise(req);
        UiRequest loaded;
        loaded.m_kind = kUiDomainsLoaded;
        Raise(loaded);
        RaiseToast(_T("域名白名单已保存"), false);
        FA_LOGI(_T("enterprise bot screen: domains saved, count: %u"), static_cast<unsigned>(m_domains.size()));
        break;
    }

    //其它标签不归本界面
    default:
        break;
    }
}

void EnterpriseBotScreen::OnEnterpriseSelected(int index)
{
    if (index < 0 || index >= static_cast<int>(m_enterpriseIds.size()))
        return;
    const long long id = m_enterpriseIds[static_cast<size_t>(index)];
    if (id == m_enterpriseId)
        return;
    m_enterpriseId = id;
    m_bots.clear();
    m_groups.clear();
    m_domains.clear();
    m_domainsLoaded = false;
    StartLoad();
    UpdateView();
}

void EnterpriseBotScreen::OnRefreshClicked()
{
    StartLoad();
    UpdateView();
}

void EnterpriseBotScreen::OnAddClicked()
{
    if (!CheckEnterpriseChosen())
        return;
    if (static_cast<int>(m_bots.size()) >= botapi::kEnterpriseBotMax)
    {
        CString text;
        text.Format(_T("本企业的企业机器人已达 %d 个，请先删除一些"), botapi::kEnterpriseBotMax);
        RaiseToast(text, true);
        return;
    }
    UiRequest req;
    req.m_kind = kUiOpenAddModal;
    Raise(req);
}

void EnterpriseBotScreen::OnDomainsClicked()
{
    if (!CheckEnterpriseChosen())
        return;
    UiRequest req;
    req.m_kind = kUiOpenDomainsModal;
    req.m_domains = m_domains;
    Raise(req);
}

const botapi::BotItem* EnterpriseBotScreen::FindBot(long long botId) const
{
    for (size_t i = 0; i < m_bots.size(); ++i)
    {
        if (m_bots[i].m_botId == botId)
            return &m_bots[i];
    }
    return nullptr;
}

void EnterpriseBotScreen::OnRowAction(int rowIndex, int actionIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_bots.size()))
        return;
    const botapi::BotItem& bot = m_bots[static_cast<size_t>(rowIndex)];
    if (!IsActionVisibleFor(bot, actionIndex))
        return;
    const CString token = AdminSession::Instance().Token();
    switch (actionIndex)
    {
    //编辑：先拉详情（带规则与密钥），拉到后打开编辑窗
    case kActionEdit:
    {
        m_detailBotId = bot.m_botId;
        HttpRequest request = botapi::MakeBotDetailRequest(ServerConfig(), token, RequestEnterpriseId(), bot.m_botId);
        FA_LOGI(_T("enterprise bot screen: load detail, botid: %lld"), bot.m_botId);
        Send(request, kApiTagBotDetail);
        break;
    }

    //停用、启用：直接提交，成功后重拉
    case kActionDisable:
    case kActionEnable:
    {
        const int status = (actionIndex == kActionDisable) ? botapi::kBotStatusDisabled : botapi::kBotStatusActive;
        HttpRequest request = botapi::MakeBotSetStatusRequest(ServerConfig(), token, RequestEnterpriseId(),
                                                              bot.m_botId, status);
        FA_LOGI(_T("enterprise bot screen: set status, botid: %lld, status: %d"), bot.m_botId, status);
        Send(request, kApiTagBotSetStatus);
        break;
    }

    //删除：先请宿主弹确认框
    case kActionDelete:
    {
        m_pendingConfirm = kPendingDelete;
        m_pendingBotId = bot.m_botId;
        UiRequest req;
        req.m_kind = kUiConfirm;
        req.m_title = _T("删除机器人");
        req.m_text.Format(_T("确定删除机器人「%s」吗？删除后它不再响应任何消息，群里的历史消息仍显示它的名字。"),
                          bot.m_name.GetString());
        req.m_okText = _T("删除");
        req.m_danger = true;
        Raise(req);
        break;
    }

    //其它操作：不处理
    default:
        break;
    }
}

void EnterpriseBotScreen::SubmitBot(const botapi::BotItem& bot)
{
    if (m_saving)
        return;
    m_saving = true;
    HttpRequest request = botapi::MakeBotSaveRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                     RequestEnterpriseId(), bot);
    FA_LOGI(_T("enterprise bot screen: save, botid: %lld, scope: %d, groups: %u, rules: %u"), bot.m_botId, bot.m_scope,
            static_cast<unsigned>(bot.m_groupIds.size()), static_cast<unsigned>(bot.m_rules.size()));
    Send(request, kApiTagBotSave);
}

void EnterpriseBotScreen::RequestTry(const botapi::BotItem& draft, const botapi::BotTryInput& input)
{
    if (m_trying)
        return;
    m_trying = true;
    HttpRequest request = botapi::MakeBotTryRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                    RequestEnterpriseId(), draft, input);
    FA_LOGI(_T("enterprise bot screen: try, botid: %lld, rules: %u, at: %d, group: %lld"), draft.m_botId,
            static_cast<unsigned>(draft.m_rules.size()), input.m_at ? 1 : 0, input.m_groupId);
    Send(request, kApiTagBotTry);
}

void EnterpriseBotScreen::RequestResetToken(long long botId)
{
    if (botId <= 0)
    {
        RaiseToast(_T("机器人保存后才会生成 Webhook 令牌"), true);
        return;
    }
    m_pendingConfirm = kPendingResetToken;
    m_pendingBotId = botId;
    UiRequest req;
    req.m_kind = kUiConfirm;
    req.m_title = _T("重新生成令牌");
    req.m_text = _T("重新生成后，旧的 Webhook 令牌与签名密钥立即失效，外部系统须改用新的令牌才能继续推送。确定重新生成吗？");
    req.m_okText = _T("重新生成");
    req.m_danger = true;
    Raise(req);
}

void EnterpriseBotScreen::SubmitDomains(const std::vector<CString>& domains)
{
    if (m_savingDomainsBusy)
        return;
    m_savingDomainsBusy = true;
    m_savingDomains = domains;
    HttpRequest request = botapi::MakeBotSetDomainsRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                           RequestEnterpriseId(), domains);
    FA_LOGI(_T("enterprise bot screen: set domains, count: %u"), static_cast<unsigned>(domains.size()));
    Send(request, kApiTagBotSetDomains);
}

void EnterpriseBotScreen::OnConfirmResult(bool confirmed)
{
    const int pending = m_pendingConfirm;
    m_pendingConfirm = kPendingNone;
    if (!confirmed || m_pendingBotId <= 0)
    {
        m_pendingBotId = 0;
        return;
    }
    const CString token = AdminSession::Instance().Token();
    switch (pending)
    {
    //删除机器人
    case kPendingDelete:
    {
        HttpRequest request = botapi::MakeBotDeleteRequest(ServerConfig(), token, RequestEnterpriseId(), m_pendingBotId);
        FA_LOGI(_T("enterprise bot screen: delete, botid: %lld"), m_pendingBotId);
        m_pendingBotId = 0;
        Send(request, kApiTagBotDelete);
        break;
    }

    //重新生成令牌与密钥：m_pendingBotId 留到应答回来时用
    case kPendingResetToken:
    {
        HttpRequest request = botapi::MakeBotResetTokenRequest(ServerConfig(), token, RequestEnterpriseId(),
                                                               m_pendingBotId);
        FA_LOGI(_T("enterprise bot screen: reset token, botid: %lld"), m_pendingBotId);
        Send(request, kApiTagBotResetToken);
        break;
    }

    //没有待确认的操作
    default:
        m_pendingBotId = 0;
        break;
    }
}

bool EnterpriseBotScreen::IsActionVisibleFor(const botapi::BotItem& bot, int actionIndex)
{
    switch (actionIndex)
    {
    //编辑、删除：仓库通知的机器人只读
    case kActionEdit:
    case kActionDelete:
        return !botrule::IsReadOnlySource(bot.m_source);
    //停用：只在启用的机器人上显示
    case kActionDisable:
        return bot.m_status != botapi::kBotStatusDisabled;
    //启用：只在停用的机器人上显示
    case kActionEnable:
        return bot.m_status == botapi::kBotStatusDisabled;
    //不认识的操作
    default:
        return false;
    }
}

int EnterpriseBotScreen::TableRowCount() const
{
    return (m_table != nullptr) ? m_table->GetRowCount() : 0;
}

bool EnterpriseBotScreen::IsRowActionVisible(int rowIndex, int actionIndex) const
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_bots.size()))
        return false;
    return IsActionVisibleFor(m_bots[static_cast<size_t>(rowIndex)], actionIndex);
}

void EnterpriseBotScreen::UpdateView()
{
    if (m_table == nullptr)
        return;
    CString overlay;
    if (m_isPlatform && m_enterpriseId <= 0)
        overlay = _T("请先在上方选择企业");
    else if (m_loadState == kLoadLoading && m_bots.empty())
        overlay = _T("正在加载…");
    else if (m_loadState == kLoadFailed)
        overlay = _T("加载失败：") + m_loadError;
    else if (m_bots.empty())
        overlay = _T("还没有企业机器人，点右上角「新建机器人」添加");
    m_table->SetRowCount(overlay.IsEmpty() ? static_cast<int>(m_bots.size()) : 0);
    m_table->SetOverlayText(overlay);
    m_table->Invalidate();
}

void EnterpriseBotScreen::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    (void)selected;
    static_cast<EnterpriseBotScreen*>(user)->PaintRow(hdc, rowIndex, rowRect, hover);
}

void EnterpriseBotScreen::OnRowActionThunk(void* user, int rowIndex, int actionIndex)
{
    static_cast<EnterpriseBotScreen*>(user)->OnRowAction(rowIndex, actionIndex);
}

bool EnterpriseBotScreen::ActionVisibleThunk(void* user, int rowIndex, int actionIndex)
{
    return static_cast<EnterpriseBotScreen*>(user)->IsRowActionVisible(rowIndex, actionIndex);
}

void EnterpriseBotScreen::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_bots.size()) || m_header == nullptr)
        return;
    const botapi::BotItem& bot = m_bots[static_cast<size_t>(rowIndex)];
    if (hover)
        ui::FillRoundRect(hdc, rowRect, theme::kInk25, 0);

    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(m_header->Columns(), rowRect.left, rowRect.right, b);
    if (b.size() <= static_cast<size_t>(kBcLastFire))
        return;

    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    const int midY = (rowRect.top + rowRect.bottom) / 2;

    //名称一栏：头像（底色 + 头像文字），右边上面名称（加粗）、下面简介或「仓库通知」说明（灰字）
    {
        const int left = b[kBcName].m_left + kCellPad;
        const RECT avatar = { left, midY - kAvatarSize / 2, left + kAvatarSize, midY + kAvatarSize / 2 };
        ui::DrawAvatar(hdc, avatar, botrule::ColorFromHex(bot.m_color, kDefaultAvatarColor), botrule::AvatarGlyph(bot));
        const int textLeft = avatar.right + kAvatarGap;
        const RECT nameRc = { textLeft, midY - kLineH, b[kBcName].m_right - kCellPad, midY };
        const RECT descRc = { textLeft, midY, b[kBcName].m_right - kCellPad, midY + kLineH };
        ui::DrawTextLine(hdc, bot.m_name, nameRc, theme::kInk700, ui::UiFont(theme::kFontSizeNormal, true), flags);
        ui::DrawTextLine(hdc, SecondLine(bot), descRc, theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false), flags);
    }

    HFONT font = ui::UiFont(theme::kFontSizeNormal, false);
    CString ruleCount;
    ruleCount.Format(_T("%d"), bot.m_ruleCount);
    const CString cells[] =
    {
        botrule::ScopeText(bot, m_groups),
        CString(bot.m_allowGroupOff ? _T("允许") : _T("不允许")),
        ruleCount,
    };
    for (int i = kBcScope; i <= kBcRules; ++i)
    {
        const RECT rc = { b[i].m_left + kCellPad, rowRect.top, b[i].m_right - kCellPad, rowRect.bottom };
        if (rc.right > rc.left)
            ui::DrawTextLine(hdc, cells[i - kBcScope], rc, theme::kInk500, font, flags);
    }

    //状态徽章：启用为绿点，停用为灰点
    {
        const bool disabled = (bot.m_status == botapi::kBotStatusDisabled);
        const CString status = botrule::StatusName(bot.m_status);
        const int cw = ui::ChipWidth(hdc, status, true);
        const int left = b[kBcStatus].m_left + kCellPad;
        const RECT chip = { left, midY - kChipH / 2, left + cw, midY + kChipH / 2 };
        ui::DrawChip(hdc, chip, status, theme::kInk600, theme::kInk50, true, disabled ? theme::kInk300 : theme::kColorOk);
    }

    //最近触发
    {
        const RECT rc = { b[kBcLastFire].m_left + kCellPad, rowRect.top, b[kBcLastFire].m_right - kCellPad, rowRect.bottom };
        if (rc.right > rc.left)
            ui::DrawTextLine(hdc, FormatTimeMs(bot.m_lastFire, _T("还没有触发过")), rc, theme::kInk500, font, flags);
    }

    //行底分隔线
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

} // namespace flamingoadmin
