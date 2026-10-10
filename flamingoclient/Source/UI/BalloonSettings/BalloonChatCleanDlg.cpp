/**
 *  「清理聊天记录」对话框实现，BalloonChatCleanDlg.cpp
 *  设计与职责分工见 BalloonChatCleanDlg.h。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "BalloonChatCleanDlg.h"
#include "../../App/BrandText.h"   // g_strAppTitle（窗口与弹窗标题用的产品名称）

#include <memory>

#include "../../net/IUSocket.h"            // CIUSocket::DeleteMessagesByTime（发 1521）
#include "../../Common/Log/IULog.h"

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using balloonsettings::LocalMinute;

namespace {

// ---- 控件 id ----
// DuiFrameWindow 内部 min/max/close 标题按钮固定占用 1/2/3，客户区控件不能用。
const UINT kIdPreset      = 10;   // 「时间范围」档位下拉框
const UINT kIdStartBase   = 20;   // 开始那组：20 年、21 月、22 日、23 时、24 分
const UINT kIdEndBase     = 30;   // 结束那组：30 年、31 月、32 日、33 时、34 分
const UINT kIdSummary     = 40;   // 说明将要删除什么的文字
const UINT kIdHint        = 41;   // 按钮上方的提示文字（为什么不能清理、失败原因等）
const UINT kIdBtnCancel   = 50;   // 「取消」
const UINT kIdBtnClean    = 51;   // 「清理」

// 一组时间下拉框里各字段相对第一个 id 的偏移
const UINT kOffYear   = 0;
const UINT kOffMonth  = 1;
const UINT kOffDay    = 2;
const UINT kOffHour   = 3;
const UINT kOffMinute = 4;

// 等应答超时的定时器 id：本窗口只有这一个定时器，取一个醒目的值便于排查
const UINT_PTR kCleanTimerId = 0x5301;

// 打开对话框时默认选中的档位：与方案界面稿一致，最常见的用途是清掉较早的记录
const int kDefaultPreset = balloonsettings::kCleanBefore30Days;

// ---- 尺寸（像素）----
const int kDlgWidth     = 580;   // 窗口宽：够放"标签 + 年月日时分五个下拉框"一行
const int kDlgHeight    = 310;   // 内容区高（标题栏以下）：三行下拉框 + 说明 + 提示 + 按钮行，下方不留大块空白
const int kTitleBarH    = 38;    // 标题栏高
const int kPadH         = 20;    // 客户区左右内边距
const int kPadV         = 16;    // 客户区上下内边距
const int kRowH         = 44;    // 下拉框各行的高度
const int kRowGap       = 6;     // 行内各块的水平间距
const int kLabelW       = 70;    // 行首标签宽（"时间范围"四个字）
const int kPresetW      = 220;   // 档位下拉框宽
const int kYearW        = 76;    // 年份下拉框宽
const int kFieldW       = 58;    // 月 / 日 / 时 / 分下拉框宽
const int kComboH       = 30;    // 下拉框高
const int kUnitW        = 16;    // "年 / 月 / 日"单位文字宽
const int kColonW       = 10;    // 时与分之间冒号的宽
const int kDateTimeGap  = 8;     // 日期与时刻之间多留的空白
const int kSummaryGapH  = 8;     // 下拉框与说明文字之间的空白
const int kSummaryH     = 60;    // 说明文字高（最多三行）
const int kHintH        = 22;    // 提示文字高（一行）
const int kFooterH      = 52;    // 底部按钮行高
const int kBtnW         = 96;    // 按钮宽
const int kBtnH         = 32;    // 按钮高
const int kBtnGap       = 10;    // 两个按钮之间的间距
const int kMaxVisibleItems = 8;  // 下拉列表一屏显示的选项数（分钟有 60 项，不宜全部展开）

// 一天的小时数、一小时的分钟数、一年的月数、一个月最多的天数：生成下拉选项用
const int kHoursPerDay    = 24;
const int kMinutesPerHour = 60;
const int kMonthsPerYear  = 12;
const int kMaxDaysInMonth = 31;

// ---- 配色 ----
const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorTitle = RGB(0x22, 0x22, 0x22);
const COLORREF kColorSub   = RGB(0x66, 0x64, 0x60);
const COLORREF kColorHint  = RGB(0xD9, 0x48, 0x3B);   // 提示文字：说明为什么不能清理，用醒目的红色

// FILETIME（自 1601-01-01 起的 100 纳秒数）换算 Unix 毫秒用的常量
const unsigned long long kUnixEpochAsFileTime = 116444736000000000ULL;   // 1970-01-01 对应的 FILETIME 值
const unsigned long long kFileTimeUnitsPerMs  = 10000ULL;                // 1 毫秒 = 10000 个 100 纳秒

// 当前时刻（Unix 毫秒）。
int64_t NowUnixMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return (int64_t)((li.QuadPart - kUnixEpochAsFileTime) / kFileTimeUnitsPerMs);
}

// 造一个文本标签。
std::unique_ptr<balloonwjui::DuiControl> MakeLabel(LPCTSTR text, COLORREF color, DWORD align, UINT ctrlId)
{
    std::unique_ptr<balloonwjui::DuiLabel> lbl(new balloonwjui::DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(align);
    if (ctrlId != 0)
    {
        lbl->SetCtrlId(ctrlId);
    }
    return std::unique_ptr<balloonwjui::DuiControl>(lbl.release());
}

// 造一个下拉框，选项为 [first, first + count) 的整数，按 width 位补零显示（如 "01"）。
std::unique_ptr<balloonwjui::DuiComboBox> MakeNumberCombo(UINT ctrlId, int first, int count, int width)
{
    std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
    combo->SetCtrlId(ctrlId);
    combo->SetMaxVisibleItems(kMaxVisibleItems);
    for (int i = 0; i < count; ++i)
    {
        CString text;
        text.Format(_T("%0*d"), width, first + i);
        combo->AddString(text);
    }
    return combo;
}

// 两位数字的显示宽度（月、日、时、分）与四位数字（年）
const int kTwoDigits  = 2;
const int kFourDigits = 4;

// 某个 id 是否属于以 idBase 起始的一组时间下拉框。
bool InTimeGroup(UINT ctrlId, UINT idBase)
{
    return ctrlId >= idBase + kOffYear && ctrlId <= idBase + kOffMinute;
}

} // anonymous namespace

CBalloonChatCleanDlg::CBalloonChatCleanDlg()
    : m_ownerHwnd(NULL)
    , m_timerOn(false)
{
}

CBalloonChatCleanDlg::~CBalloonChatCleanDlg()
{
}

void CBalloonChatCleanDlg::OpenDialog(HWND owner, bool connected)
{
    m_ownerHwnd = owner;

    if (IsWindow())
    {
        SetConnected(connected);
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
        return;
    }

    m_flow.Reset(kDefaultPreset);
    m_flow.SetConnected(connected);
    m_timerOn = false;

    if (Create(NULL, CWindow::rcDefault, g_strAppTitle.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("Chat clean dialog: Create failed.");
        return;
    }

    SetTitle(TR(L"清理聊天记录"));
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);   // 只留关闭按钮
    SetResizable(false);
    // 标题栏左侧的程序图标：标题栏不透明，底色用 kOpaqueTitleBarColor；本窗口没有所有者、会出现在任务栏，同时设好任务栏图标
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);

    InitUi();
    ApplyPreset(kDefaultPreset);
    RefreshTexts();

    //kDlgHeight 是标题栏以下的内容高度，整窗高度要加上标题栏（DuiFrameWindow::ResizeClient 设的是整窗）
    ResizeClient(kDlgWidth, kDlgHeight + kTitleBarH);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("Chat clean dialog opened, connected=%d.", connected ? 1 : 0);
}

void CBalloonChatCleanDlg::SetConnected(bool connected)
{
    if (!IsWindow())
    {
        return;
    }

    const int stageBefore = m_flow.Stage();
    m_flow.SetConnected(connected);
    //清理中断线：这次请求的应答不会再到达，超时定时器也不必再等
    if (stageBefore == balloonsettings::kCleanStageRunning
        && m_flow.Stage() != balloonsettings::kCleanStageRunning && m_timerOn)
    {
        KillTimer(kCleanTimerId);
        m_timerOn = false;
        LOG_WARNING("Chat clean dialog: connection lost while waiting for the result.");
    }
    RefreshTexts();
}

bool CBalloonChatCleanDlg::OnCleanResult(int code, int64_t beginTime, int64_t endTime)
{
    if (!IsWindow())
    {
        return false;
    }
    if (!m_flow.OnResult(code))
    {
        LOG_INFO("Chat clean dialog ignores a result without a pending request, code=%d.", code);
        return false;
    }

    if (m_timerOn)
    {
        KillTimer(kCleanTimerId);
        m_timerOn = false;
    }

    if (m_flow.Stage() == balloonsettings::kCleanStageDone)
    {
        LOG_INFO("Chat clean dialog: clean succeeded, range=[%lld, %lld].",
                 (long long)beginTime, (long long)endTime);
        const std::wstring done = balloonsettings::BuildCleanDoneText(beginTime, endTime);
        ::MessageBox(m_hWnd, done.c_str(), TR(L"清理聊天记录"), MB_OK | MB_ICONINFORMATION);
        DestroyWindow();
        return true;
    }

    LOG_ERROR("Chat clean dialog: clean failed, code=%d.", code);
    RefreshTexts();
    return true;
}

void CBalloonChatCleanDlg::InitUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadV, kPadH, kPadV);
    root->SetGap(0);

    // 第一行：时间范围档位
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);
        row->AddChild(MakeLabel(TR(L"时间范围"), kColorTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0),
                      balloonwjui::DuiLayout::Hint().Fixed(kLabelW));

        std::unique_ptr<balloonwjui::DuiComboBox> preset(new balloonwjui::DuiComboBox());
        preset->SetCtrlId(kIdPreset);
        preset->SetMaxVisibleItems(balloonsettings::kCleanPresetCount);
        for (int i = 0; i < balloonsettings::kCleanPresetCount; ++i)
        {
            preset->AddString(CString(balloonsettings::ChatCleanPresetLabel(i)));
        }
        preset->SetCurSel(kDefaultPreset, /*notify*/false);
        row->AddChild(std::move(preset),
                      balloonwjui::DuiLayout::Hint().Fixed(kPresetW, kComboH)
                          .AlignC(balloonwjui::DuiLayout::AlignCenter));
        row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }

    // 第二、三行：开始与结束两组年月日时分
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);
        row->AddChild(MakeLabel(TR(L"开始"), kColorTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0),
                      balloonwjui::DuiLayout::Hint().Fixed(kLabelW));
        AppendTimeCombos(row.get(), kIdStartBase);
        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kRowGap);
        row->AddChild(MakeLabel(TR(L"结束"), kColorTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0),
                      balloonwjui::DuiLayout::Hint().Fixed(kLabelW));
        AppendTimeCombos(row.get(), kIdEndBase);
        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }

    // 说明文字：讲清删什么、删哪里、对方是否受影响、能否恢复 —— 用户点「清理」前唯一的依据
    root->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Fixed(kSummaryGapH));
    root->AddChild(MakeLabel(_T(""), kColorSub, DT_LEFT | DT_TOP | DT_WORDBREAK, kIdSummary),
                   balloonwjui::DuiLayout::Hint().Fixed(kSummaryH));

    // 中间弹性空白，把提示与按钮行压到底部
    root->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));

    // 提示文字：未连接、区间不合法、失败、超时、断线时说明原因，其余时候为空
    root->AddChild(MakeLabel(_T(""), kColorHint, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, kIdHint),
                   balloonwjui::DuiLayout::Hint().Fixed(kHintH));

    // 底部按钮行：右对齐的「取消」「清理」
    std::unique_ptr<balloonwjui::DuiHBox> footer(new balloonwjui::DuiHBox());
    footer->SetGap(kBtnGap);
    footer->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                     balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
    cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
    cancel->SetText(TR(L"取消"));
    cancel->SetCtrlId(kIdBtnCancel);
    footer->AddChild(std::move(cancel),
                     balloonwjui::DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                         .AlignC(balloonwjui::DuiLayout::AlignCenter));

    // 清理不可恢复，按钮用危险色
    std::unique_ptr<balloonwjui::DuiButton> clean(new balloonwjui::DuiButton());
    clean->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    clean->SetVariant(balloonwjui::DuiButton::Variant::Danger);
    clean->SetText(TR(L"清理"));
    clean->SetCtrlId(kIdBtnClean);
    footer->AddChild(std::move(clean),
                     balloonwjui::DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                         .AlignC(balloonwjui::DuiLayout::AlignCenter));

    root->AddChild(std::move(footer), balloonwjui::DuiLayout::Hint().Fixed(kFooterH));

    SetClientContent(std::move(root));
}

void CBalloonChatCleanDlg::AppendTimeCombos(balloonwjui::DuiControl* row, UINT idBase)
{
    balloonwjui::DuiHBox* box = dynamic_cast<balloonwjui::DuiHBox*>(row);
    if (box == NULL)
    {
        return;
    }

    //年份选项到今年为止；取不到本地时间时（极少见）只给起始年份一项
    LocalMinute today;
    const int currentYear = balloonsettings::MsToLocalMinute(NowUnixMs(), today)
                          ? today.m_year : balloonsettings::kCleanYearStart;
    const std::vector<int> years = balloonsettings::CleanYearOptions(currentYear);

    const balloonwjui::DuiLayout::Hint comboHintYear =
        balloonwjui::DuiLayout::Hint().Fixed(kYearW, kComboH).AlignC(balloonwjui::DuiLayout::AlignCenter);
    const balloonwjui::DuiLayout::Hint comboHint =
        balloonwjui::DuiLayout::Hint().Fixed(kFieldW, kComboH).AlignC(balloonwjui::DuiLayout::AlignCenter);
    const DWORD unitAlign = DT_CENTER | DT_VCENTER | DT_SINGLELINE;

    box->AddChild(MakeNumberCombo(idBase + kOffYear, years.front(), (int)years.size(), kFourDigits),
                  comboHintYear);
    box->AddChild(MakeLabel(TR(L"年"), kColorTitle, unitAlign, 0), balloonwjui::DuiLayout::Hint().Fixed(kUnitW));
    box->AddChild(MakeNumberCombo(idBase + kOffMonth, 1, kMonthsPerYear, kTwoDigits), comboHint);
    box->AddChild(MakeLabel(TR(L"月"), kColorTitle, unitAlign, 0), balloonwjui::DuiLayout::Hint().Fixed(kUnitW));
    // 日的选项随年月变化，先按最多的 31 天建好，填值时再按实际年月重建
    box->AddChild(MakeNumberCombo(idBase + kOffDay, 1, kMaxDaysInMonth, kTwoDigits), comboHint);
    box->AddChild(MakeLabel(TR(L"日"), kColorTitle, unitAlign, 0), balloonwjui::DuiLayout::Hint().Fixed(kUnitW));
    box->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Fixed(kDateTimeGap));
    box->AddChild(MakeNumberCombo(idBase + kOffHour, 0, kHoursPerDay, kTwoDigits), comboHint);
    box->AddChild(MakeLabel(_T(":"), kColorTitle, unitAlign, 0), balloonwjui::DuiLayout::Hint().Fixed(kColonW));
    box->AddChild(MakeNumberCombo(idBase + kOffMinute, 0, kMinutesPerHour, kTwoDigits), comboHint);
    box->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Weight(1));
}

void CBalloonChatCleanDlg::FillTimeCombos(UINT idBase, const LocalMinute& t)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiComboBox* year = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(idBase + kOffYear));
    balloonwjui::DuiComboBox* month = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(idBase + kOffMonth));
    balloonwjui::DuiComboBox* day = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(idBase + kOffDay));
    balloonwjui::DuiComboBox* hour = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(idBase + kOffHour));
    balloonwjui::DuiComboBox* minute = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(idBase + kOffMinute));
    if (year == NULL || month == NULL || day == NULL || hour == NULL || minute == NULL)
    {
        return;
    }

    //年份超出下拉框的范围时（如本机时钟被调到将来）取最近的一端
    int yearIdx = t.m_year - balloonsettings::kCleanYearStart;
    if (yearIdx < 0)
    {
        yearIdx = 0;
    }
    if (yearIdx >= year->GetCount())
    {
        yearIdx = year->GetCount() - 1;
    }

    //全部以 notify=false 赋值，不产生变化通知；外层另有 BeginProgramFill 作为第二层保护
    year->SetCurSel(yearIdx, false);
    month->SetCurSel(t.m_month - 1, false);
    RebuildDayOptions(idBase);
    day->SetCurSel(t.m_day - 1, false);
    hour->SetCurSel(t.m_hour, false);
    minute->SetCurSel(t.m_minute, false);
}

LocalMinute CBalloonChatCleanDlg::ReadTimeCombos(UINT idBase)
{
    LocalMinute t;
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return t;
    }

    const UINT offsets[] = { kOffYear, kOffMonth, kOffDay, kOffHour, kOffMinute };
    int sel[sizeof(offsets) / sizeof(offsets[0])] = { 0 };
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
    {
        balloonwjui::DuiComboBox* combo = dynamic_cast<balloonwjui::DuiComboBox*>(
            content->FindCtrlById(idBase + offsets[i]));
        sel[i] = (combo != NULL && combo->GetCurSel() >= 0) ? combo->GetCurSel() : 0;
    }

    t.m_year = balloonsettings::kCleanYearStart + sel[kOffYear];
    t.m_month = sel[kOffMonth] + 1;
    t.m_day = sel[kOffDay] + 1;
    t.m_hour = sel[kOffHour];
    t.m_minute = sel[kOffMinute];
    return t;
}

void CBalloonChatCleanDlg::RebuildDayOptions(UINT idBase)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiComboBox* day = dynamic_cast<balloonwjui::DuiComboBox*>(
        content->FindCtrlById(idBase + kOffDay));
    if (day == NULL)
    {
        return;
    }

    LocalMinute t = ReadTimeCombos(idBase);
    const int days = balloonsettings::DaysInMonth(t.m_year, t.m_month);
    if (days <= 0 || days == day->GetCount())
    {
        return;
    }

    //选项数变了才重建：超出当月天数的日改成当月最后一天，其余保持不变
    balloonsettings::ClampDayToMonth(t);
    day->ResetContent();
    for (int d = 1; d <= days; ++d)
    {
        CString text;
        text.Format(_T("%02d"), d);
        day->AddString(text);
    }
    day->SetCurSel(t.m_day - 1, false);
}

void CBalloonChatCleanDlg::ApplyPreset(int preset)
{
    int64_t beginMs = 0;
    int64_t endMs = 0;
    if (!balloonsettings::PresetToRange(preset, NowUnixMs(), beginMs, endMs))
    {
        return;
    }
    LocalMinute startShown;
    LocalMinute endShown;
    if (!balloonsettings::RangeToDisplay(beginMs, endMs, startShown, endShown))
    {
        return;
    }

    m_flow.BeginProgramFill();
    FillTimeCombos(kIdStartBase, startShown);
    FillTimeCombos(kIdEndBase, endShown);
    m_flow.EndProgramFill();
}

int CBalloonChatCleanDlg::ComputeRange(int64_t& beginMs, int64_t& endMs)
{
    const int64_t now = NowUnixMs();
    if (m_flow.Preset() != balloonsettings::kCleanCustom)
    {
        //预设档位按此刻重新换算：对话框开着的这段时间里"当前时刻"一直在走
        if (balloonsettings::PresetToRange(m_flow.Preset(), now, beginMs, endMs))
        {
            return balloonsettings::kCleanRangeOk;
        }
        return balloonsettings::kCleanRangeBadTime;
    }
    return balloonsettings::CustomToRange(ReadTimeCombos(kIdStartBase), ReadTimeCombos(kIdEndBase),
                                          now, beginMs, endMs);
}

void CBalloonChatCleanDlg::RefreshTexts()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    int64_t beginMs = 0;
    int64_t endMs = 0;
    const int err = ComputeRange(beginMs, endMs);
    if (err == balloonsettings::kCleanRangeOk)
    {
        SetLabelText(kIdSummary, balloonsettings::BuildCleanSummaryText(beginMs, endMs).c_str());
    }
    else
    {
        SetLabelText(kIdSummary, balloonsettings::ChatCleanRangeErrorText(err));
    }
    SetLabelText(kIdHint, m_flow.Hint().c_str());

    balloonwjui::DuiButton* clean = dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(kIdBtnClean));
    if (clean != NULL)
    {
        clean->SetEnabled(m_flow.IsCleanEnabled());
        clean->SetText(CString(m_flow.CleanButtonText()));
        clean->Invalidate();
    }
}

void CBalloonChatCleanDlg::OnCleanClicked()
{
    int64_t beginMs = 0;
    int64_t endMs = 0;
    int err = ComputeRange(beginMs, endMs);
    if (!m_flow.CanStartClean(err))
    {
        RefreshTexts();
        return;
    }

    CString confirm(balloonsettings::BuildCleanSummaryText(beginMs, endMs).c_str());
    confirm += TR(L"\n\n确定要清理吗？");
    const int answer = ::MessageBox(m_hWnd, confirm, TR(L"清理聊天记录"),
                                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDOK || !IsWindow())
    {
        return;
    }

    //确认框停留期间连接可能已断开、时间也在走：按点确定的这一刻重新校验与换算
    err = ComputeRange(beginMs, endMs);
    if (!m_flow.CanStartClean(err))
    {
        RefreshTexts();
        return;
    }
    if (!CIUSocket::GetInstance().DeleteMessagesByTime(beginMs, endMs))
    {
        LOG_ERROR("Chat clean dialog: request rejected locally, range=[%lld, %lld].",
                  (long long)beginMs, (long long)endMs);
        return;
    }

    m_flow.OnRequestSent();
    SetTimer(kCleanTimerId, balloonsettings::kChatCleanTimeoutMs, NULL);
    m_timerOn = true;
    RefreshTexts();
    LOG_INFO("Chat clean dialog requests delete by time, preset=%d, range=[%lld, %lld].",
             m_flow.Preset(), (long long)beginMs, (long long)endMs);
}

void CBalloonChatCleanDlg::SetLabelText(UINT ctrlId, LPCWSTR text)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiLabel* label = dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(ctrlId));
    if (label != NULL)
    {
        label->SetText(CString(text));
    }
}

LRESULT CBalloonChatCleanDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //清理中关窗不影响清理本身：请求已发出，本机记录由主面板收到删除推送后照常删除
    if (m_timerOn)
    {
        KillTimer(kCleanTimerId);
        m_timerOn = false;
    }
    DestroyWindow();
    return 0;
}

LRESULT CBalloonChatCleanDlg::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam,
                                         LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kCleanTimerId)
    {
        bHandled = FALSE;
        return 0;
    }

    //定时器只用来判超时，到点即停
    KillTimer(kCleanTimerId);
    m_timerOn = false;
    m_flow.OnTimeout();
    RefreshTexts();
    LOG_WARNING("Chat clean dialog: no result within %u ms.", balloonsettings::kChatCleanTimeoutMs);
    return 0;
}

LRESULT CBalloonChatCleanDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM lParam, BOOL& bHandled)
{
    balloonwjui::DuiNotify* pNotify = (balloonwjui::DuiNotify*)lParam;
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //「清理」：校验、确认后发请求
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdBtnClean)
    {
        OnCleanClicked();
        return 0;
    }

    //「取消」：直接关窗（清理中关窗不影响已发出的请求）
    if (pNotify->code == balloonwjui::DUIN_CLICK && pNotify->ctrlId == kIdBtnCancel)
    {
        PostMessage(WM_CLOSE);
        return 0;
    }

    //用户在「时间范围」里选了一个档位：按档位填好两组时间
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdPreset)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiComboBox* preset = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiComboBox*>(content->FindCtrlById(kIdPreset)) : NULL;
        if (preset != NULL && m_flow.OnPresetSelected(preset->GetCurSel()))
        {
            ApplyPreset(m_flow.Preset());
        }
        RefreshTexts();
        return 0;
    }

    //用户改了开始或结束里的某一个时间：年月变化时重建日的选项，档位切到「自定义」
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && (InTimeGroup(pNotify->ctrlId, kIdStartBase) || InTimeGroup(pNotify->ctrlId, kIdEndBase)))
    {
        const UINT idBase = InTimeGroup(pNotify->ctrlId, kIdStartBase) ? kIdStartBase : kIdEndBase;
        const UINT offset = pNotify->ctrlId - idBase;
        if (offset == kOffYear || offset == kOffMonth)
        {
            m_flow.BeginProgramFill();
            RebuildDayOptions(idBase);
            m_flow.EndProgramFill();
        }
        if (m_flow.OnTimeFieldChanged())
        {
            balloonwjui::DuiControl* content = GetClientContent();
            balloonwjui::DuiComboBox* preset = (content != NULL)
                ? dynamic_cast<balloonwjui::DuiComboBox*>(content->FindCtrlById(kIdPreset)) : NULL;
            if (preset != NULL)
            {
                m_flow.BeginProgramFill();
                preset->SetCurSel(balloonsettings::kCleanCustom, false);
                preset->Invalidate();
                m_flow.EndProgramFill();
            }
        }
        RefreshTexts();
        return 0;
    }

    //其余（标题栏关闭按钮等）交回基类
    bHandled = FALSE;
    return 0;
}
