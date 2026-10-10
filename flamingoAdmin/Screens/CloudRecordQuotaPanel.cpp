/**
 *  flamingoAdmin —— 「云端录像空间」面板实现，说明见 CloudRecordQuotaPanel.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "Screens/CloudRecordQuotaPanel.h"
#include "Theme/AdminTheme.h"
#include "Util/FileShareText.h"   // FormatSize：字节数的显示
#include "Util/Logger.h"

#include "Controls/Basic/DuiButton.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiEdit.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kHeadRowH   = 24;   // 第一行（名称与已用空间）高度
const int kHeadGap    = 6;    // 第一行与输入行之间的间距
const int kInputRowH  = 34;   // 输入行高度
const int kNoteGap    = 4;    // 输入行与说明行之间的间距
const int kNoteH      = 20;   // 说明行高度
const int kNameW      = 110;  // 名称「云端录像空间」的宽度
const int kGap        = 8;    // 同一行里控件之间的间距
const int kLabelW     = 28;   // 「配额」「保留」两个字的宽度
const int kQuotaEditW = 64;   // 配额输入框宽度：放得下 7 位数字
const int kUnitGbW    = 22;   // 「GB」宽度
const int kDaysEditW  = 48;   // 天数输入框宽度：放得下 4 位数字
const int kUnitDayW   = 16;   // 「天」宽度
const int kSaveW      = 64;   // 「保存」按钮宽度
const int kQuotaMaxChars = 7; // 配额输入框最多几位（kQuotaGbMax 为 7 位）
const int kDaysMaxChars  = 4; // 天数输入框最多几位（kRetainDaysMax 为 4 位）

static_assert(kHeadRowH + kHeadGap + kInputRowH + kNoteGap + kNoteH == CloudRecordQuotaPanel::kPanelHeight,
              "kPanelHeight must equal the sum of the row heights");

// 十进制换算的基数
const long long kDecimalBase = 10;

// 一个单行文字标签
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, COLORREF color, int pointSize, bool bold, UINT align = DT_LEFT)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextPointSize(pointSize, bold);
    label->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    return label;
}

// 一个数字输入框
std::unique_ptr<DuiEdit> MakeNumberEdit(int ctrlId, int maxChars)
{
    std::unique_ptr<DuiEdit> e(new DuiEdit());
    e->SetCtrlId(ctrlId);
    e->SetShowBorder(true);
    e->SetBgColor(theme::kInk0);
    e->SetVerticalCenter(true);
    e->SetMaxLength(maxChars);
    return e;
}

// 把去掉首尾空白的文字解析成不带符号的十进制整数；空串、含非数字字符或超过 max 时返回 false
bool ParseUnsigned(const CString& text, long long max, long long& out)
{
    CString t = text;
    t.Trim();
    if (t.IsEmpty())
        return false;
    long long value = 0;
    for (int i = 0; i < t.GetLength(); ++i)
    {
        const TCHAR c = t[i];
        if (c < _T('0') || c > _T('9'))
            return false;
        value = value * kDecimalBase + (c - _T('0'));
        //逐位判断，长串数字不会溢出
        if (value > max)
            return false;
    }
    out = value;
    return true;
}

// 整数的文字
CString NumberText(long long value)
{
    CString text;
    text.Format(_T("%lld"), value);
    return text;
}

} // namespace

CloudRecordQuotaPanel::CloudRecordQuotaPanel(ApiTag getTag, ApiTag setTag, int saveCtrlId, int quotaCtrlId,
                                             int daysCtrlId)
    : m_getTag(getTag)
    , m_setTag(setTag)
    , m_quotaEdit(nullptr)
    , m_daysEdit(nullptr)
    , m_saveBtn(nullptr)
    , m_status(nullptr)
    , m_state(kStateIdle)
    , m_enterpriseId(0)
    , m_quotaGb(0)
    , m_retainDays(0)
    , m_usedBytes(-1)
    , m_savingQuota(-1)
    , m_savingDays(-1)
    , m_pendingSets(0)
    , m_saveFailed(false)
{
    //第一行：[名称] [已用空间与状态]
    std::unique_ptr<DuiHBox> head(new DuiHBox());
    head->SetGap(kGap);
    head->AddChild(MakeLabel(_T("云端录像空间"), theme::kInk700, theme::kFontSizeNormal, true), Hint().Fixed(kNameW));
    std::unique_ptr<DuiLabel> status = MakeLabel(_T(""), theme::kInk500, theme::kFontSizeSmall, false, DT_RIGHT);
    m_status = status.get();
    head->AddChild(std::move(status), Hint().Weight(1));
    AddChild(std::move(head), Hint().Fixed(kHeadRowH).Margin(0, 0, 0, kHeadGap));

    //输入行：配额 [  ] GB  保留 [  ] 天  ……  [保存]
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(kGap);
    row->AddChild(MakeLabel(_T("配额"), theme::kInk500, theme::kFontSizeNormal, false), Hint().Fixed(kLabelW));
    std::unique_ptr<DuiEdit> quota = MakeNumberEdit(quotaCtrlId, kQuotaMaxChars);
    m_quotaEdit = quota.get();
    row->AddChild(std::move(quota), Hint().Fixed(kQuotaEditW));
    row->AddChild(MakeLabel(_T("GB"), theme::kInk500, theme::kFontSizeNormal, false), Hint().Fixed(kUnitGbW));
    row->AddChild(MakeLabel(_T("保留"), theme::kInk500, theme::kFontSizeNormal, false), Hint().Fixed(kLabelW));
    std::unique_ptr<DuiEdit> days = MakeNumberEdit(daysCtrlId, kDaysMaxChars);
    m_daysEdit = days.get();
    row->AddChild(std::move(days), Hint().Fixed(kDaysEditW));
    row->AddChild(MakeLabel(_T("天"), theme::kInk500, theme::kFontSizeNormal, false), Hint().Fixed(kUnitDayW));
    row->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    std::unique_ptr<DuiButton> save(new DuiButton());
    save->SetCtrlId(saveCtrlId);
    save->SetText(_T("保存"));
    save->SetVariant(DuiButton::Variant::Default);
    save->SetTextPointSize(theme::kFontSizeNormal);
    m_saveBtn = save.get();
    row->AddChild(std::move(save), Hint().Fixed(kSaveW));
    AddChild(std::move(row), Hint().Fixed(kInputRowH).Margin(0, 0, 0, kNoteGap));

    //说明行：两项设置的含义
    AddChild(MakeLabel(_T("原始帧与合成后的录像都计入配额；保留 0 天表示永久保留，改天数只影响之后结束的录像"),
                       theme::kInk400, theme::kFontSizeSmall, false),
             Hint().Fixed(kNoteH));

    UpdateView(false);
}

void CloudRecordQuotaPanel::Load(long long enterpriseId)
{
    m_enterpriseId = (enterpriseId > 0) ? enterpriseId : 0;
    m_state = kStateLoading;
    m_error.Empty();
    m_pendingSets = 0;
    FA_LOGI(_T("cloud record quota panel: load settings, enterprise: %lld, tag: %d"), m_enterpriseId,
            static_cast<int>(m_getTag));
    HttpRequest request = adminapi::MakeFeatureSettingsRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                               m_enterpriseId);
    m_port.Send(request, m_getTag);
    UpdateView(false);
}

void CloudRecordQuotaPanel::OnApiResult(ApiTag tag, const HttpResult& http)
{
    if (tag == m_getTag)
        OnLoaded(http);
    else if (tag == m_setTag)
        OnSaved(http);
}

void CloudRecordQuotaPanel::OnLoaded(const HttpResult& http)
{
    const adminapi::FeatureSettingsResult r = adminapi::ParseFeatureSettingsResponse(http);
    //弹窗关掉后又为另一个企业打开时，前一个企业的应答可能晚到：企业对不上的应答丢弃
    if (r.Succeeded() && m_enterpriseId > 0 && r.m_enterpriseId != m_enterpriseId)
    {
        FA_LOGW(_T("cloud record quota panel: stale settings dropped, enterprise %lld != %lld"), r.m_enterpriseId,
                m_enterpriseId);
        return;
    }
    if (m_state != kStateLoading)
        return;
    if (!r.Succeeded())
    {
        m_state = kStateFailed;
        m_error = r.m_message;
        FA_LOGW(_T("cloud record quota panel: load settings failed, code: %d"), r.m_code);
        UpdateView(false);
        return;
    }
    adminapi::FeatureSettingItem quota;
    adminapi::FeatureSettingItem days;
    long long quotaGb = 0;
    long long retainDays = 0;
    CString ignored;
    //服务端没有给出这两项（版本不一致）或取值不是数字：按读取失败处理，不让管理员在不知道当前取值时修改
    if (!r.Find(adminapi::kSettingCloudRecordQuotaGb, quota) || !r.Find(adminapi::kSettingCloudRecordRetainDays, days)
        || !ParseInputs(quota.m_value, days.m_value, quotaGb, retainDays, ignored))
    {
        m_state = kStateFailed;
        m_error = _T("服务端没有返回云端录像的配额与保留天数");
        FA_LOGW(_T("cloud record quota panel: quota or retain days missing in response"));
        UpdateView(false);
        return;
    }
    m_quotaGb = quotaGb;
    m_retainDays = retainDays;
    m_usedBytes = r.m_cloudRecordUsedBytes;
    m_state = kStateLoaded;
    FA_LOGI(_T("cloud record quota panel: settings loaded, enterprise: %lld, quota: %lld GB, retain: %lld days, used: %lld"),
            r.m_enterpriseId, m_quotaGb, m_retainDays, m_usedBytes);
    UpdateView(true);
}

void CloudRecordQuotaPanel::OnSaveClicked()
{
    if (m_state != kStateLoaded || m_quotaEdit == nullptr || m_daysEdit == nullptr)
        return;
    long long quotaGb = 0;
    long long days = 0;
    CString error;
    if (!ParseInputs(m_quotaEdit->GetText(), m_daysEdit->GetText(), quotaGb, days, error))
    {
        m_port.Toast(error, true);
        return;
    }
    m_savingQuota = (quotaGb != m_quotaGb) ? quotaGb : -1;
    m_savingDays = (days != m_retainDays) ? days : -1;
    if (m_savingQuota < 0 && m_savingDays < 0)
    {
        m_port.Toast(_T("云端录像空间没有修改"), false);
        return;
    }
    m_state = kStateSaving;
    m_pendingSets = 0;
    m_saveFailed = false;
    m_saveError.Empty();
    if (m_savingQuota >= 0)
        SendSet(adminapi::kSettingCloudRecordQuotaGb, m_savingQuota);
    if (m_savingDays >= 0)
        SendSet(adminapi::kSettingCloudRecordRetainDays, m_savingDays);
    UpdateView(false);
}

void CloudRecordQuotaPanel::SendSet(LPCTSTR key, long long value)
{
    ++m_pendingSets;
    FA_LOGI(_T("cloud record quota panel: set %s to %lld, enterprise: %lld"), key, value, m_enterpriseId);
    HttpRequest request = adminapi::MakeFeatureSetRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                          m_enterpriseId, key, NumberText(value));
    m_port.Send(request, m_setTag);
}

void CloudRecordQuotaPanel::OnSaved(const HttpResult& http)
{
    const adminapi::FeatureSetResult r = adminapi::ParseFeatureSetResponse(http);
    if (r.Succeeded() && m_enterpriseId > 0 && r.m_enterpriseId != m_enterpriseId)
    {
        FA_LOGW(_T("cloud record quota panel: stale set result dropped, enterprise %lld != %lld"), r.m_enterpriseId,
                m_enterpriseId);
        return;
    }
    if (m_state != kStateSaving || m_pendingSets <= 0)
        return;
    --m_pendingSets;
    if (!r.Succeeded() && !m_saveFailed)
    {
        m_saveFailed = true;
        m_saveError = r.m_message;
        FA_LOGW(_T("cloud record quota panel: set failed, code: %d"), r.m_code);
    }
    if (m_pendingSets > 0)
        return;

    if (m_saveFailed)
    {
        //两项里可能有一项已经改成功：重新读一次，界面显示服务端的真实取值
        m_port.Toast(_T("保存失败：") + m_saveError, true);
        Load(m_enterpriseId);
        return;
    }
    if (m_savingQuota >= 0)
        m_quotaGb = m_savingQuota;
    if (m_savingDays >= 0)
        m_retainDays = m_savingDays;
    m_state = kStateLoaded;
    FA_LOGI(_T("cloud record quota panel: saved, enterprise: %lld, quota: %lld GB, retain: %lld days"), m_enterpriseId,
            m_quotaGb, m_retainDays);
    m_port.Toast(_T("已保存云端录像空间"), false);
    UpdateView(true);
}

void CloudRecordQuotaPanel::UpdateView(bool refillEdits)
{
    const bool editable = (m_state == kStateLoaded);
    if (m_quotaEdit != nullptr)
    {
        if (refillEdits)
            m_quotaEdit->SetTextNoNotify(NumberText(m_quotaGb));
        m_quotaEdit->SetEnabled(editable);
    }
    if (m_daysEdit != nullptr)
    {
        if (refillEdits)
            m_daysEdit->SetTextNoNotify(NumberText(m_retainDays));
        m_daysEdit->SetEnabled(editable);
    }
    if (m_saveBtn != nullptr)
        m_saveBtn->SetEnabled(editable);
    if (m_status != nullptr)
        m_status->SetText(StatusText());
}

CString CloudRecordQuotaPanel::StatusText() const
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
        return UsageText(m_usedBytes, m_quotaGb);
    }
}

bool CloudRecordQuotaPanel::ParseInputs(const CString& quotaText, const CString& daysText, long long& quotaGb,
                                        long long& days, CString& error)
{
    if (!ParseUnsigned(quotaText, kQuotaGbMax, quotaGb))
    {
        error.Format(_T("配额请填写 0～%lld 之间的整数（单位 GB）"), kQuotaGbMax);
        return false;
    }
    if (!ParseUnsigned(daysText, kRetainDaysMax, days))
    {
        error.Format(_T("保留天数请填写 0～%lld 之间的整数，0 表示永久保留"), kRetainDaysMax);
        return false;
    }
    return true;
}

CString CloudRecordQuotaPanel::UsageText(long long usedBytes, long long quotaGb)
{
    CString text;
    //已用空间未知（老服务端或云端录制没有初始化）：只写配额
    if (usedBytes < 0)
    {
        text.Format(_T("共 %lld GB"), quotaGb);
        return text;
    }
    //FormatSize 对 0 写「-」，这里写成 0 B
    const CString used = (usedBytes == 0) ? CString(_T("0 B")) : fstext::FormatSize(usedBytes);
    if (quotaGb == 0)
    {
        text.Format(_T("已用 %s，配额为 0，不能再新增"), static_cast<LPCTSTR>(used));
        return text;
    }
    text.Format(_T("已用 %s / 共 %lld GB"), static_cast<LPCTSTR>(used), quotaGb);
    return text;
}

} // namespace flamingoadmin
