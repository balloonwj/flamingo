/**
 *  flamingoAdmin —— 新建公告弹窗 ComposeModal 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/ComposeModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"
#include "Mock/DataStore.h"
#include "DuiNotify.h"

#include "Controls/Input/DuiEdit.h"
#include "DuiHost.h"   // InvalidateDuiRect：滚动 / 勾选时只刷新投放目标面板
#include "Widgets/OverlayScrollbar.h"  // 投放目标选择器的悬浮滚动条子控件
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 弹窗布局常量（96 dpi 逻辑像素）----
const int kPanelW   = 680;   // 弹窗宽度
const int kPanelH   = 856;   // 弹窗高度（按「目标选择器 + 定时」最满情形定）
const int kHeaderH  = 52;    // 标题栏高度
const int kFooterH  = 60;    // 按钮栏高度
const int kBodyPad  = 20;    // 表单区左 / 右内距
const int kLabelH   = 18;    // 字段标签高度
const int kFieldGap = 6;     // 标签与其控件的间距
const int kRowGap   = 10;    // 区块之间的间距

const int kEditH      = 38;  // 单行输入框高度
const int kBodyEditH  = 84;  // 正文输入框高度
const int kSchedEditH = 34;  // 定时时间输入框高度
const int kSevChipH   = 44;  // 严重等级卡片高度
const int kAudChipH   = 36;  // 受众分段高度
const int kSendCardH  = 58;  // 发送方式卡片高度
const int kPinH       = 30;  // 置顶行高度
const int kPickerH    = 140; // 目标选择器面板高度
const int kPickerRowH = 28;  // 目标选择器列表行高
const int kBtnH       = 34;  // 底部按钮高度

// 目标选择器列表内容上 / 下内边距（可视高度 = kPickerH - 2×该值）。
const int kPickerInnerPadV = 4;
// 悬浮滚动条距投放目标面板右内缘的间距（像素）。0：命中带贴区域右缘，滑块离右缘 2 像素由 DuiScrollBar 决定，与 PC 客户端一致。
const int kPickerSbInset = 0;

const int kBackdropAlpha = 112;  // 遮罩不透明度
const int kModalTitlePt  = 14;   // 弹窗标题字号（磅）

// 严重等级选项（下标即服务端 severity 编码：0 普通 / 1 提醒 / 2 警告）。
// 级别选项与公告列表、详情、导出共用一套名称，下标即服务端级别编号。
const TCHAR* const* const kSevLabels = adminapi::kAnnSeverityLabels;
const int kSevCount = adminapi::kAnnSeverityLabelCount;

// 受众类型选项（下标即服务端 audienceType 编码）。
const TCHAR* const kAudLabels[] =
{
    _T("全部企业"), _T("指定企业"), _T("指定部门"), _T("指定员工"), _T("平台内部")
};
const int kAudCount = 5;

// 目标类型编码（公告 targets[].type，也与 audienceType 1/2/3 对应）。
const int kTargetEnterprise = 1;  // 企业
const int kTargetDept       = 2;  // 部门
const int kTargetUser       = 3;  // 员工

// 严重等级编码 → 客户端 Severity 枚举（仅用于画级别圆点）。
Severity SeverityForCode(int code)
{
    switch (code)
    {
    case 1:
        return Severity::Warn;
    case 2:
        return Severity::Danger;
    default:
        return Severity::Info;
    }
}

// 构造一个统一风格的输入框。
std::unique_ptr<DuiEdit> MakeEdit(int id, LPCTSTR placeholder, bool multiline)
{
    auto e = std::make_unique<DuiEdit>();
    e->SetCtrlId(id);
    if (multiline)
    {
        e->SetMultiLine(true);
        // 多行正文按框宽自动换行,内容超过框高时显示原生竖直滚动条
        // (否则普通 EDIT 默认 ES_AUTOHSCROLL,会横向滚动、不换行)。
        e->SetWordWrap(true);
    }
    e->SetPlaceholder(placeholder);
    e->SetShowBorder(true);
    e->SetBgColor(theme::kInk0);
    return e;
}

// 画一个小方框复选框（已选 = 实心 + 白勾）。
void DrawCheckBox(HDC hdc, const RECT& box, bool checked)
{
    ui::FillRoundRect(hdc, box, checked ? theme::kFlamingo500 : theme::kInk0,
                      theme::kRadiusSm);
    if (!checked)
    {
        ui::StrokeRoundRect(hdc, box, theme::kInk200, theme::kRadiusSm, 1.5f);
        return;
    }
    // 白色对勾：三段短线。
    const int cx = box.left;
    const int cy = box.top;
    ui::FillLine(hdc, cx + 4, cy + 9,  4, 2, theme::kInk0);
    ui::FillLine(hdc, cx + 7, cy + 11, 2, 2, theme::kInk0);
    ui::FillLine(hdc, cx + 8, cy + 5,  2, 6, theme::kInk0);
}

} // anonymous namespace

ComposeModal::ComposeModal()
{
    SetVisible(false);
    // 目标选择器要拉组织树时经本控件的 ctrlId 上报 AdminFrame。
    SetCtrlId(kIdComposeModal);

    auto title = MakeEdit(kIdComposeTitle,
                          _T("例如：FlamingoIM v4.2 客户端发布说明"), false);
    m_editTitle = title.get();
    AddChild(std::move(title));

    auto body = MakeEdit(kIdComposeBody, _T("公告正文，换行将被保留。"), true);
    m_editBody = body.get();
    AddChild(std::move(body));

    auto sched = MakeEdit(kIdComposeDate, _T("YYYY-MM-DD HH:MM"), false);
    m_editSchedule = sched.get();
    AddChild(std::move(sched));

    // 投放目标选择器的悬浮滚动条。须在 closeBtn / cancel / submit 之前
    // AddChild，以保持 Layout 里「末三个子控件 = 关闭 / 取消 / 发布」的约定。
    auto pickerSb = std::make_unique<OverlayScrollbar>();
    m_pickerSb = pickerSb.get();
    m_pickerSb->SetOnScroll(&ComposeModal::OnPickerSbScroll, this);
    AddChild(std::move(pickerSb));

    auto closeBtn = std::make_unique<ui::IconButton>();
    closeBtn->SetCtrlId(kIdComposeClose);
    closeBtn->SetIcon(ui::Icon::Close);
    closeBtn->SetIconSize(16);
    closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    AddChild(std::move(closeBtn));

    auto cancel = std::make_unique<DuiButton>();
    cancel->SetCtrlId(kIdComposeCancel);
    cancel->SetText(_T("取消"));
    cancel->SetVariant(DuiButton::Variant::Default);
    cancel->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(cancel));

    auto submit = std::make_unique<DuiButton>();
    submit->SetCtrlId(kIdComposeSubmit);
    submit->SetText(_T("立即发布"));
    submit->SetVariant(DuiButton::Variant::Primary);
    submit->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(submit));
}

void ComposeModal::Reset()
{
    m_editTitle->SetText(_T(""));
    m_editBody->SetText(_T(""));
    m_editAnnId = 0;
    m_severity = 0;
    m_audience = 0;
    m_pinned   = false;
    m_sendMode = 0;
    m_targets.clear();
    m_scopeEntId.Empty();
    m_scopeTree = OrgNode();
    m_scopeTreeLoaded = false;
    m_pendingTreeEntId.Empty();
    m_pickerScroll = 0;
    m_editSchedule->SetVisible(false);

    // 定时时间默认填「明天 09:00」。
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    FILETIME ft = {};
    ::SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += 24ULL * 60ULL * 60ULL * 10000000ULL;   // + 1 天（100ns 单位）
    ft.dwLowDateTime  = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME tm = {};
    ::FileTimeToSystemTime(&ft, &tm);
    CString def;
    def.Format(_T("%04d-%02d-%02d 09:00"), tm.wYear, tm.wMonth, tm.wDay);
    m_editSchedule->SetText(def);

    // 提交按钮文字复位。
    DuiButton* submit =
        static_cast<DuiButton*>(m_children.back().get());
    submit->SetText(_T("立即发布"));

    RecomputeLayout();
}

void ComposeModal::LoadForEdit(long long announcementId, const CString& title,
                               const CString& body, int severity, int audienceType,
                               bool pinned, const std::vector<AnnTarget>& targets,
                               bool scheduled, const CString& scheduledTime)
{
    Reset();   // 先复位，确保不残留上一次状态
    m_editAnnId = announcementId;
    m_editTitle->SetText(title);
    m_editBody->SetText(body);
    m_severity = severity;
    m_audience = audienceType;
    m_pinned   = pinned;
    m_targets  = targets;
    if (scheduled)
    {
        // 回填「定时」：与用户点选「定时发送」时的处理一致（显示时间框、改提交按钮文字）。
        m_sendMode = 1;
        m_editSchedule->SetVisible(true);
        const CString normalized = NormalizeScheduleText(scheduledTime);
        if (!normalized.IsEmpty())
        {
            m_editSchedule->SetText(normalized);
        }
        DuiButton* submit = static_cast<DuiButton*>(m_children.back().get());
        submit->SetText(_T("计划发送"));
    }
    RecomputeLayout();
    Invalidate();
}

void ComposeModal::LoadForCopy(const CString& title, const CString& body, int severity,
                               int audienceType, bool pinned,
                               const std::vector<AnnTarget>& targets)
{
    // 复制即「用原内容新建一条」：编辑 ID 传 0，定时状态不沿用（发送方式为立即）。
    LoadForEdit(0, title, body, severity, audienceType, pinned, targets, false, CString());
}

void ComposeModal::PresetEnterpriseAudience(long long enterpriseId)
{
    Reset();
    if (enterpriseId > 0)
    {
        // 受众「指定企业」的编码与目标类型「企业」的编码相同，都是 kTargetEnterprise。
        m_audience = kTargetEnterprise;
        m_targets.push_back(AnnTarget(kTargetEnterprise, enterpriseId));
    }
    RecomputeLayout();
    Invalidate();
}

CString ComposeModal::NormalizeScheduleText(const CString& serverTime)
{
    // 「YYYY-MM-DD HH:MM」共 16 个字符；各位置上的分隔符依次在下列下标处。
    const int kMinuteTextLen = 16;
    const int kDash1 = 4;
    const int kDash2 = 7;
    const int kSpace = 10;
    const int kColon = 13;

    CString text = serverTime;
    text.Trim();
    if (text.GetLength() < kMinuteTextLen)
    {
        return CString();
    }
    text = text.Left(kMinuteTextLen);
    for (int i = 0; i < kMinuteTextLen; ++i)
    {
        const TCHAR ch = text[i];
        bool ok = false;
        if (i == kDash1 || i == kDash2)
        {
            ok = (ch == _T('-'));
        }
        else if (i == kSpace)
        {
            ok = (ch == _T(' '));
        }
        else if (i == kColon)
        {
            ok = (ch == _T(':'));
        }
        else
        {
            ok = (ch >= _T('0') && ch <= _T('9'));
        }
        if (!ok)
        {
            return CString();
        }
    }
    return text;
}

bool ComposeModal::Validate() const
{
    CString t = m_editTitle->GetText();
    CString b = m_editBody->GetText();
    t.Trim();
    b.Trim();
    if (t.IsEmpty() || b.IsEmpty())
    {
        return false;
    }
    // 受众为「指定企业 / 部门 / 员工」时必须至少选一个目标。
    if (TargetPickerVisible() && m_targets.empty())
    {
        return false;
    }
    return true;
}

CString ComposeModal::GetTitle() const
{
    return m_editTitle->GetText();
}

CString ComposeModal::GetBody() const
{
    return m_editBody->GetText();
}

CString ComposeModal::GetScheduledTime() const
{
    return m_editSchedule->GetText();
}

void ComposeModal::SetOrgTree(const CString& enterpriseId, const OrgNode& root)
{
    // 仅当回填的正是当前作用域企业时才采用。
    if (enterpriseId != m_scopeEntId)
    {
        return;
    }
    m_scopeTree       = root;
    m_scopeTreeLoaded = true;
    m_pickerScroll    = 0;
    if (m_pendingTreeEntId == enterpriseId)
    {
        m_pendingTreeEntId.Empty();
    }
    InvalidatePicker();
}

bool ComposeModal::TargetPickerVisible() const
{
    return m_audience == kTargetEnterprise
        || m_audience == kTargetDept
        || m_audience == kTargetUser;
}

void ComposeModal::RecomputeLayout()
{
    const bool pickerVisible = TargetPickerVisible();

    // 各区块行高(标签 + 间距 + 控件)。
    const int titleRow = kLabelH + kFieldGap + kEditH;
    const int sevRow   = kLabelH + kFieldGap + kSevChipH;
    const int audRow   = kLabelH + kFieldGap + kAudChipH;
    const int pickerRow= kLabelH + kFieldGap + kPickerH;
    const int sendRow  = kLabelH + kFieldGap + kSendCardH;
    const int schedRow = kLabelH + kFieldGap + kSchedEditH;  // 底部最高情形(定时)
    const int bodyLabelGap = kLabelH + kFieldGap;            // 正文标签 + 间距

    // 正文之上(标题行 + 间距)与正文之下(其余所有行 + 间距)的固定总高。
    const int aboveBody = titleRow + kRowGap;
    int belowBody = sevRow + kRowGap + audRow + kRowGap;
    if (pickerVisible)
    {
        belowBody += pickerRow + kRowGap;
    }
    belowBody += kPinH + kRowGap + sendRow + kRowGap + schedRow;

    // 内容可用高 = 对话框高 - 标题栏 - 上下内距 - 底部按钮栏。让正文吸收
    // "可用高 - 其余固定内容"的差额,使表单铺满对话框(空隙全给正文,正文框
    // 尽量大);最满情形下差额≈0,正文维持默认高度 kBodyEditH。
    const int availForContent = kPanelH - kHeaderH - kBodyPad - kBodyPad - kFooterH;
    const int natTotal = aboveBody + bodyLabelGap + kBodyEditH + kRowGap + belowBody;
    m_bodyEditH = kBodyEditH;
    if (availForContent > natTotal)
    {
        m_bodyEditH += (availForContent - natTotal);
    }

    int y = 0;
    m_yTitle = y;
    y += titleRow + kRowGap;
    m_yBody = y;
    y += (bodyLabelGap + m_bodyEditH) + kRowGap;
    m_ySev = y;
    y += sevRow + kRowGap;
    m_yAud = y;
    y += audRow + kRowGap;
    m_yPicker = y;
    if (pickerVisible)
    {
        y += pickerRow + kRowGap;
    }
    m_yPin = y;
    y += kPinH + kRowGap;
    m_ySend = y;
    y += sendRow + kRowGap;
    m_ySched = y;

    // 标题 / 正文 / 定时时间是三个 DuiEdit 子控件，而本弹窗是手写自绘面板、
    // 自行摆放它们的矩形，故区块纵向位置一变就必须同步重排，否则它们会停在
    // 旧位置：例如 LoadForEdit / Reset 切换受众后只调本函数、不调 Layout，
    // 定时时间输入框会错位到「置顶」行上方。
    // m_panelRect 在首次 Layout 之后才有效，未就绪时跳过、由 Layout 兜底。
    if (m_panelRect.right > m_panelRect.left)
    {
        const int fl = m_panelRect.left + kBodyPad;
        const int fw = kPanelW - kBodyPad * 2;
        const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;
        m_editTitle->ForceLayout(RECT{ fl,
            bodyTop + m_yTitle + kLabelH + kFieldGap,
            fl + fw, bodyTop + m_yTitle + kLabelH + kFieldGap + kEditH });
        m_editBody->ForceLayout(RECT{ fl,
            bodyTop + m_yBody + kLabelH + kFieldGap,
            fl + fw, bodyTop + m_yBody + kLabelH + kFieldGap + m_bodyEditH });
        m_editSchedule->ForceLayout(RECT{ fl,
            bodyTop + m_ySched + kLabelH + kFieldGap,
            fl + 280, bodyTop + m_ySched + kLabelH + kFieldGap + kSchedEditH });

        // 投放目标悬浮滚动条：摆在面板右内缘的细竖条上，高 = 列表可视区
        // 高度；受众非「指定…」时面板不可见、滚动条一并隐藏。
        m_pickerSb->SetVisible(pickerVisible);
        if (pickerVisible)
        {
            RECT pk = PickerRect();
            m_pickerSb->ForceLayout(RECT{
                pk.right - kPickerSbInset - OverlayScrollbar::kSuggestedWidth,
                pk.top + kPickerInnerPadV,
                pk.right - kPickerSbInset,
                pk.bottom - kPickerInnerPadV });
        }
    }
}

void ComposeModal::CollectScopeNodes(const OrgNode& node,
                                     std::vector<const OrgNode*>& outList) const
{
    const OrgNodeKind want = (m_audience == kTargetDept)
                                 ? OrgNodeKind::Dept : OrgNodeKind::User;
    for (size_t i = 0; i < node.m_children.size(); ++i)
    {
        const OrgNode& child = node.m_children[i];
        if (child.m_kind == want)
        {
            outList.push_back(&child);
        }
        CollectScopeNodes(child, outList);
    }
}

bool ComposeModal::IsTargetChecked(int type, const CString& id) const
{
    const long long idv = _ttoi64(id);
    for (size_t i = 0; i < m_targets.size(); ++i)
    {
        if (m_targets[i].m_type == type && m_targets[i].m_id == idv)
        {
            return true;
        }
    }
    return false;
}

void ComposeModal::ToggleTarget(int type, const CString& id)
{
    const long long idv = _ttoi64(id);
    for (size_t i = 0; i < m_targets.size(); ++i)
    {
        if (m_targets[i].m_type == type && m_targets[i].m_id == idv)
        {
            m_targets.erase(m_targets.begin() + i);
            return;
        }
    }
    m_targets.push_back(AnnTarget(type, idv));
}

void ComposeModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelW / 2;
    m_panelRect.right  = cx + kPanelW / 2;
    m_panelRect.top    = cy - kPanelH / 2;
    m_panelRect.bottom = cy + kPanelH / 2;

    RecomputeLayout();

    // 关闭按钮（右上）。
    const size_t n = m_children.size();
    m_children[n - 3]->ForceLayout(RECT{ m_panelRect.right - 12 - 30,
        m_panelRect.top + (kHeaderH - 30) / 2,
        m_panelRect.right - 12, m_panelRect.top + (kHeaderH + 30) / 2 });

    // 取消 / 发布（右下）。
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - kBtnH) / 2;
    RECT rcSubmit = { m_panelRect.right - 20 - 112, by,
                      m_panelRect.right - 20,       by + kBtnH };
    RECT rcCancel = { rcSubmit.left - 10 - 76, by, rcSubmit.left - 10, by + kBtnH };
    m_children[n - 2]->ForceLayout(rcCancel);
    m_children[n - 1]->ForceLayout(rcSubmit);

    // 标题 / 正文 / 定时时间三个输入框的矩形已在 RecomputeLayout() 里随各区块
    // 一并重排（上面第 349 行已调用），此处无需再单独处理。
}

void ComposeModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    m_hits.clear();
    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);

    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    // 标题栏 + 分隔线。
    RECT rcTitle = { m_panelRect.left + 20, m_panelRect.top,
                     m_panelRect.right - 50, m_panelRect.top + kHeaderH };
    ui::DrawTextLine(hdc, m_editAnnId != 0 ? _T("编辑公告") : _T("新建公告"),
                     rcTitle, theme::kInk700, ui::UiFont(kModalTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelW, 1, theme::kInk100);

    const int fl = m_panelRect.left + kBodyPad;
    const int fw = kPanelW - kBodyPad * 2;
    const int fr = fl + fw;
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;

    HFONT labelFont = ui::UiFont(theme::kFontSizeSmall, false);

    // ---- 标题 / 正文 标签（输入框由子控件绘制）----
    ui::DrawTextLine(hdc, _T("标题 *"),
                     RECT{ fl, bodyTop + m_yTitle, fr, bodyTop + m_yTitle + kLabelH },
                     theme::kInk500, labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("正文 *"),
                     RECT{ fl, bodyTop + m_yBody, fr, bodyTop + m_yBody + kLabelH },
                     theme::kInk500, labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- 严重等级 ----
    ui::DrawTextLine(hdc, _T("严重等级"),
                     RECT{ fl, bodyTop + m_ySev, fr, bodyTop + m_ySev + kLabelH },
                     theme::kInk500, labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    {
        const int cardY = bodyTop + m_ySev + kLabelH + kFieldGap;
        const int gap = 8;
        const int cw = (fw - gap * (kSevCount - 1)) / kSevCount;
        for (int i = 0; i < kSevCount; ++i)
        {
            const int cx = fl + i * (cw + gap);
            RECT card = { cx, cardY, cx + cw, cardY + kSevChipH };
            const bool sel = (i == m_severity);
            ui::FillRoundRect(hdc, card, sel ? theme::kFlamingo50 : theme::kInk0,
                              theme::kRadiusMd);
            ui::StrokeRoundRect(hdc, card, sel ? theme::kFlamingo300 : theme::kInk150,
                                theme::kRadiusMd, 1.0f);
            ui::DrawSeverityDot(hdc, cx + 14, cardY + kSevChipH / 2,
                                SeverityForCode(i));
            RECT cl = { cx + 28, cardY, cx + cw - 8, cardY + kSevChipH };
            ui::DrawTextLine(hdc, kSevLabels[i], cl,
                             sel ? theme::kFlamingo700 : theme::kInk700,
                             ui::UiFont(theme::kFontSizeSmall, sel),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            Hit h;
            h.m_rc = card;
            h.m_kind = 0;
            h.m_arg = i;
            h.m_tgtType = 0;
            m_hits.push_back(h);
        }
    }

    // ---- 受众 ----
    ui::DrawTextLine(hdc, _T("受众"),
                     RECT{ fl, bodyTop + m_yAud, fr, bodyTop + m_yAud + kLabelH },
                     theme::kInk500, labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    {
        const int rowY = bodyTop + m_yAud + kLabelH + kFieldGap;
        const int gap = 6;
        const int cw = (fw - gap * (kAudCount - 1)) / kAudCount;
        for (int i = 0; i < kAudCount; ++i)
        {
            const int cx = fl + i * (cw + gap);
            RECT chip = { cx, rowY, cx + cw, rowY + kAudChipH };
            const bool sel = (i == m_audience);
            ui::FillRoundRect(hdc, chip, sel ? theme::kFlamingo500 : theme::kInk50,
                              theme::kRadiusMd);
            if (!sel)
            {
                ui::StrokeRoundRect(hdc, chip, theme::kInk150, theme::kRadiusMd, 1.0f);
            }
            ui::DrawTextLine(hdc, kAudLabels[i], chip,
                             sel ? theme::kInk0 : theme::kInk600,
                             ui::UiFont(theme::kFontSizeSmall, sel),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            Hit h;
            h.m_rc = chip;
            h.m_kind = 1;
            h.m_arg = i;
            h.m_tgtType = 0;
            m_hits.push_back(h);
        }
    }

    // ---- 目标选择器 ----
    if (TargetPickerVisible())
    {
        ui::DrawTextLine(hdc, _T("投放目标 *"),
                         RECT{ fl, bodyTop + m_yPicker,
                               fr - 120, bodyTop + m_yPicker + kLabelH },
                         theme::kInk500, labelFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        CString cnt;
        cnt.Format(_T("已选 %d 项"), static_cast<int>(m_targets.size()));
        ui::DrawTextLine(hdc, cnt,
                         RECT{ fr - 120, bodyTop + m_yPicker,
                               fr, bodyTop + m_yPicker + kLabelH },
                         theme::kInk400, labelFont,
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT picker = { fl, bodyTop + m_yPicker + kLabelH + kFieldGap,
                        fr, bodyTop + m_yPicker + kLabelH + kFieldGap + kPickerH };
        PaintTargetPicker(hdc, picker);
    }

    // ---- 置顶 ----
    {
        const int py = bodyTop + m_yPin;
        RECT box = { fl, py + 5, fl + 18, py + 23 };
        DrawCheckBox(hdc, box, m_pinned);
        RECT tl = { box.right + 10, py, fr, py + 28 };
        ui::DrawTextLine(hdc, _T("置顶（客户端首次打开时优先展示）"), tl,
                         theme::kInk600, ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit h;
        h.m_rc = RECT{ fl, py, fr, py + 28 };
        h.m_kind = 2;
        h.m_arg = 0;
        h.m_tgtType = 0;
        m_hits.push_back(h);
    }

    // ---- 发送方式 ----
    ui::DrawTextLine(hdc, _T("发送时间"),
                     RECT{ fl, bodyTop + m_ySend, fr, bodyTop + m_ySend + kLabelH },
                     theme::kInk500, labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    {
        const int cardY = bodyTop + m_ySend + kLabelH + kFieldGap;
        const int gap = 8;
        const int cw = (fw - gap) / 2;
        const TCHAR* const slabels[2] = { _T("立即发送"), _T("定时发送") };
        const TCHAR* const sdescs[2]  =
        {
            _T("点击发布后立即推送"), _T("选择未来某个时间推送")
        };
        for (int i = 0; i < 2; ++i)
        {
            const int cx = fl + i * (cw + gap);
            RECT card = { cx, cardY, cx + cw, cardY + kSendCardH };
            const bool sel = (i == m_sendMode);
            ui::FillRoundRect(hdc, card, sel ? theme::kFlamingo50 : theme::kInk0,
                              theme::kRadiusMd);
            ui::StrokeRoundRect(hdc, card, sel ? theme::kFlamingo300 : theme::kInk150,
                                theme::kRadiusMd, 1.0f);
            RECT cl = { cx + 14, cardY + 10, cx + cw - 8, cardY + 30 };
            ui::DrawTextLine(hdc, slabels[i], cl,
                             sel ? theme::kFlamingo700 : theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT cd = { cx + 14, cardY + 30, cx + cw - 8, cardY + kSendCardH - 8 };
            ui::DrawTextLine(hdc, sdescs[i], cd, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            Hit h;
            h.m_rc = card;
            h.m_kind = 3;
            h.m_arg = i;
            h.m_tgtType = 0;
            m_hits.push_back(h);
        }
    }

    // ---- 定时时间 ----
    if (m_sendMode == 1)
    {
        ui::DrawTextLine(hdc, _T("发送时间（YYYY-MM-DD HH:MM）"),
                         RECT{ fl, bodyTop + m_ySched,
                               fr, bodyTop + m_ySched + kLabelH },
                         theme::kInk500, labelFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // 输入框由子控件绘制。
    }
    else
    {
        ui::DrawTextLine(hdc, _T("公告将在点击「立即发布」后立即推送。"),
                         RECT{ fl, bodyTop + m_ySched, fr, bodyTop + m_ySched + 24 },
                         theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

void ComposeModal::PaintTargetPicker(HDC hdc, const RECT& rc)
{
    ui::FillRoundRect(hdc, rc, theme::kInk25, theme::kRadiusMd);
    ui::StrokeRoundRect(hdc, rc, theme::kInk150, theme::kRadiusMd, 1.0f);

    // 先把逻辑行平铺出来：0=可勾选项 1=企业头 2=提示。
    struct Row
    {
        int     m_kind;
        int     m_tgtType;
        CString m_id;
        CString m_name;
        bool    m_isScopeEnt;
    };
    std::vector<Row> rows;
    const std::vector<Enterprise>& ents = DataStore::Instance().Enterprises();

    if (m_audience == kTargetEnterprise)
    {
        for (size_t i = 0; i < ents.size(); ++i)
        {
            Row r;
            r.m_kind = 0;
            r.m_tgtType = kTargetEnterprise;
            r.m_id = ents[i].m_id;
            r.m_name = ents[i].m_name;
            r.m_isScopeEnt = false;
            rows.push_back(r);
        }
    }
    else
    {
        for (size_t i = 0; i < ents.size(); ++i)
        {
            Row head;
            head.m_kind = 1;
            head.m_tgtType = 0;
            head.m_id = ents[i].m_id;
            head.m_name = ents[i].m_name;
            head.m_isScopeEnt = (ents[i].m_id == m_scopeEntId);
            rows.push_back(head);
            if (ents[i].m_id != m_scopeEntId)
            {
                continue;
            }
            if (!m_scopeTreeLoaded)
            {
                Row tip;
                tip.m_kind = 2;
                tip.m_tgtType = 0;
                tip.m_name = _T("加载中…");
                tip.m_isScopeEnt = false;
                rows.push_back(tip);
                continue;
            }
            std::vector<const OrgNode*> nodes;
            CollectScopeNodes(m_scopeTree, nodes);
            if (nodes.empty())
            {
                Row tip;
                tip.m_kind = 2;
                tip.m_tgtType = 0;
                tip.m_name = _T("（该企业暂无可选项）");
                tip.m_isScopeEnt = false;
                rows.push_back(tip);
            }
            for (size_t k = 0; k < nodes.size(); ++k)
            {
                Row r;
                r.m_kind = 0;
                r.m_tgtType = m_audience;
                r.m_id = nodes[k]->m_id;
                r.m_name = nodes[k]->m_name;
                r.m_isScopeEnt = false;
                rows.push_back(r);
            }
        }
    }

    m_pickerContentH = static_cast<int>(rows.size()) * kPickerRowH;

    int saved = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
    HFONT rowFont = ui::UiFont(theme::kFontSizeSmall, false);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int top = rc.top + kPickerInnerPadV
                        + static_cast<int>(i) * kPickerRowH - m_pickerScroll;
        if (top + kPickerRowH <= rc.top || top >= rc.bottom)
        {
            continue;
        }
        const Row& r = rows[i];
        const int cy = top + kPickerRowH / 2;
        if (r.m_kind == 1)            // 企业头：可点切换作用域
        {
            if (r.m_isScopeEnt)
            {
                ui::FillSolidRect(hdc, RECT{ rc.left + 1, top,
                                  rc.right - 1, top + kPickerRowH },
                                  theme::kInk50);
            }
            ui::DrawTextLine(hdc, r.m_name,
                             RECT{ rc.left + 12, top,
                                   rc.right - 12, top + kPickerRowH },
                             theme::kInk700, ui::UiFont(theme::kFontSizeSmall, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                             | DT_END_ELLIPSIS);
            Hit h;
            h.m_rc = RECT{ rc.left, top, rc.right, top + kPickerRowH };
            h.m_kind = 4;
            h.m_arg = 0;
            h.m_tgtType = 0;
            h.m_tgtId = r.m_id;
            m_hits.push_back(h);
        }
        else if (r.m_kind == 2)       // 提示行
        {
            ui::DrawTextLine(hdc, r.m_name,
                             RECT{ rc.left + 32, top,
                                   rc.right - 12, top + kPickerRowH },
                             theme::kInk400, rowFont,
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else                          // 可勾选项
        {
            const int indent = (m_audience == kTargetEnterprise) ? 12 : 32;
            const bool checked = IsTargetChecked(r.m_tgtType, r.m_id);
            RECT box = { rc.left + indent, cy - 9, rc.left + indent + 18, cy + 9 };
            DrawCheckBox(hdc, box, checked);
            ui::DrawTextLine(hdc, r.m_name,
                             RECT{ box.right + 10, top,
                                   rc.right - 12, top + kPickerRowH },
                             theme::kInk700, rowFont,
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                             | DT_END_ELLIPSIS);
            Hit h;
            h.m_rc = RECT{ rc.left, top, rc.right, top + kPickerRowH };
            h.m_kind = 5;
            h.m_arg = 0;
            h.m_tgtType = r.m_tgtType;
            h.m_tgtId = r.m_id;
            m_hits.push_back(h);
        }
    }
    if (rows.empty())
    {
        ui::DrawTextLine(hdc, _T("暂无企业数据"), rc, theme::kInk400, rowFont,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 把最新内容总高 / 滚动量同步给悬浮滚动条子控件；它作为子控件会在本
    // 函数返回后由 base OnPaint 绘制自身（悬浮于面板右缘、auto-hide）。
    m_pickerSb->SetMetrics(m_pickerContentH, m_pickerScroll);
    ::RestoreDC(hdc, saved);
}

void ComposeModal::InvalidatePicker()
{
    if (m_pHost == nullptr)
    {
        return;
    }
    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;
    // 从「投放目标」标签行顶部一直到面板列表底部。这块区域内没有输入框，单独
    // 刷新它即可让滚动 / 勾选只重绘面板自身，不牵连标题 / 正文 / 定时时间三个
    // DuiEdit。早先这三处内嵌真 Win32 输入框子窗口，被整窗刷新牵连时会闪烁；
    // 改为无窗口控件之后已不会闪烁，缩小刷新区域现在只用于减少重绘量。
    RECT rc = { fl, bodyTop + m_yPicker,
                fr, bodyTop + m_yPicker + kLabelH + kFieldGap + kPickerH };
    m_pHost->InvalidateDuiRect(rc);
}

RECT ComposeModal::PickerRect() const
{
    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;
    const int top = bodyTop + m_yPicker + kLabelH + kFieldGap;
    RECT rc;
    rc.left = fl;
    rc.top = top;
    rc.right = fr;
    rc.bottom = top + kPickerH;
    return rc;
}

void ComposeModal::OnPickerSbScroll(void* ctx, int newScroll)
{
    // OverlayScrollbar 拖动滑块的回调：把新滚动量写回本弹窗并刷新投放目标面板。
    ComposeModal* self = static_cast<ComposeModal*>(ctx);
    self->m_pickerScroll = newScroll;
    self->InvalidatePicker();
}

balloonwjui::DuiControl* ComposeModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
    if (::PtInRect(&m_panelRect, ptHostClient))
    {
        for (const auto& child : m_children)
        {
            if (DuiControl* hit = child->HitTest(ptHostClient))
            {
                return hit;
            }
        }
        return this;
    }
    return this;
}

bool ComposeModal::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (const Hit& h : m_hits)
    {
        if (!::PtInRect(&h.m_rc, pt))
        {
            continue;
        }
        if (h.m_kind == 0)            // 严重等级
        {
            m_severity = h.m_arg;
            Invalidate();
        }
        else if (h.m_kind == 1)       // 受众
        {
            if (h.m_arg != m_audience)
            {
                m_audience = h.m_arg;
                RecomputeLayout();
                Layout(m_rcItem);     // 区块位置变了，重排输入框
                // 切到「指定部门 / 员工」且尚未定作用域企业 → 自动取第一个。
                if ((m_audience == kTargetDept || m_audience == kTargetUser)
                    && m_scopeEntId.IsEmpty())
                {
                    const std::vector<Enterprise>& ents =
                        DataStore::Instance().Enterprises();
                    if (!ents.empty())
                    {
                        m_scopeEntId       = ents.front().m_id;
                        m_scopeTreeLoaded  = false;
                        m_pendingTreeEntId = m_scopeEntId;
                        NotifyParent(DUIN_CLICK);
                    }
                }
                Invalidate();
            }
        }
        else if (h.m_kind == 2)       // 置顶
        {
            m_pinned = !m_pinned;
            Invalidate();
        }
        else if (h.m_kind == 3)       // 发送方式
        {
            if (h.m_arg != m_sendMode)
            {
                m_sendMode = h.m_arg;
                m_editSchedule->SetVisible(m_sendMode == 1);
                DuiButton* submit =
                    static_cast<DuiButton*>(m_children.back().get());
                submit->SetText(m_sendMode == 1 ? _T("计划发送") : _T("立即发布"));
                Invalidate();
            }
        }
        else if (h.m_kind == 4)       // 切换目标作用域企业
        {
            if (h.m_tgtId != m_scopeEntId)
            {
                m_scopeEntId       = h.m_tgtId;
                m_scopeTreeLoaded  = false;
                m_scopeTree        = OrgNode();
                m_pickerScroll     = 0;
                m_pendingTreeEntId = m_scopeEntId;
                NotifyParent(DUIN_CLICK);
                InvalidatePicker();
            }
        }
        else if (h.m_kind == 5)       // 勾选 / 取消一个目标
        {
            ToggleTarget(h.m_tgtType, h.m_tgtId);
            InvalidatePicker();
        }
        return true;
    }
    return true;
}

bool ComposeModal::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    if (!TargetPickerVisible())
    {
        return true;
    }
    const int fl = m_panelRect.left + kBodyPad;
    const int fr = m_panelRect.right - kBodyPad;
    const int bodyTop = m_panelRect.top + kHeaderH + kBodyPad;
    RECT picker = { fl, bodyTop + m_yPicker + kLabelH + kFieldGap,
                    fr, bodyTop + m_yPicker + kLabelH + kFieldGap + kPickerH };
    if (!::PtInRect(&picker, pt))
    {
        return true;
    }
    m_pickerScroll -= (zDelta / WHEEL_DELTA) * kPickerRowH * 2;
    int maxS = m_pickerContentH - (kPickerH - kPickerInnerPadV * 2);
    if (maxS < 0)
    {
        maxS = 0;
    }
    if (m_pickerScroll < 0)
    {
        m_pickerScroll = 0;
    }
    if (m_pickerScroll > maxS)
    {
        m_pickerScroll = maxS;
    }
    // 同步滚动量给悬浮滚动条并令其淡入（停止滚动后自动淡出）。
    m_pickerSb->SetMetrics(m_pickerContentH, m_pickerScroll);
    m_pickerSb->Flash();
    // 只刷新投放目标面板，不整窗重绘——否则会牵连标题 / 正文 / 定时时间
    // 三个真子窗一起重绘，造成它们闪烁。
    InvalidatePicker();
    return true;
}

} // namespace flamingoadmin
