/**
 *  备忘编辑对话框，见 MemoEditDialog.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "MemoEditDialog.h"

#include <time.h>

#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../BalloonMain/TitleBarIcon.h"       // CreateProgramIconBitmap（标题栏左侧图标）
#include "MemoEditLayout.h"
#include "MemoViewModel.h"
#include "MemoFullscreenWindow.h"           // 「预览效果」用的全屏提醒窗口
#include "../../Feature/Memo/MemoClock.h"   // NowLocalMinute：预览里的「今天」按当前时间判断
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;
using balloonmemo::CMemoCenter;

namespace {

// ---- 标题栏：透明、穿透显示客户区白底，与「发起接龙」窗口一致 ----
const COLORREF kCaptionColor  = RGB(0x55, 0x55, 0x55);
const int      kTitleIconSize = 16;
const COLORREF kTitleBarBg    = RGB(0xFF, 0xFF, 0xFF);

// 年微调框的范围：今年的前一年起（编辑去年建的备忘时不至于被限制），到服务端接受的最大年份
const int kYearsBack = 1;
const int kMaxYear = 2099;
// struct tm 的 tm_year 从 1900 年起算
const int kTmYearBase = 1900;

// 十进制 yyyymmddhhmm 里各段的位权与每段的进位基数
const int64_t kYearUnit = 100000000LL;
const int64_t kMonthUnit = 1000000LL;
const int64_t kDayUnit = 10000LL;
const int64_t kHourUnit = 100LL;
const int64_t kPartBase = 100LL;

// memo_repeat_type / memo_style 的取值
const int kRepeatNone = 0;
const int kRepeatWeekly = 3;
const int kStylePopup = 1;
const int kStyleFullscreen = 2;

// 把微调框输入框里手输的数字写回微调框（SetValue 会限制在范围内），返回最终的值
int CommitTypedValue(DuiSpinBox* spin)
{
    if (spin == NULL)
    {
        return 0;
    }
    //输入框为空或不是数字时 _ttoi 返回 0，同样被限制到下限
    if (spin->GetEdit() != NULL)
    {
        spin->SetValue(_ttoi(spin->GetEdit()->GetText()), false);
    }
    return spin->GetValue();
}

int CurrentYear()
{
    const time_t now = ::time(NULL);
    struct tm local;
    if (localtime_s(&local, &now) != 0)
    {
        return kMaxYear;
    }
    return local.tm_year + kTmYearBase;
}

LPCTSTR WindowTitleFor(balloonmemo::MemoEditMode mode, bool groupMode)
{
    if (mode == balloonmemo::kMemoEditNew)
    {
        return groupMode ? TR(L"新建群备忘") : TR(L"新建备忘");
    }
    if (mode == balloonmemo::kMemoEditExisting)
    {
        return groupMode ? TR(L"修改群备忘") : TR(L"修改备忘");
    }
    return groupMode ? TR(L"查看群备忘") : TR(L"查看备忘");
}

} // anonymous namespace

CMemoEditDialog::CMemoEditDialog()
    : m_mode(balloonmemo::kMemoEditNew)
    , m_groupMode(false)
    , m_saving(false)
    , m_ownerHwnd(NULL)
    , m_pTitle(NULL)
    , m_pYear(NULL)
    , m_pMonth(NULL)
    , m_pDay(NULL)
    , m_pHour(NULL)
    , m_pMinute(NULL)
    , m_pAdvance(NULL)
    , m_pRepeat(NULL)
    , m_pEndNever(NULL)
    , m_pEndCount(NULL)
    , m_pCount(NULL)
    , m_pStylePopup(NULL)
    , m_pStyleFull(NULL)
    , m_pTargetAll(NULL)
    , m_pTargetPick(NULL)
    , m_pPickBtn(NULL)
    , m_pTargetText(NULL)
    , m_pNote(NULL)
    , m_pError(NULL)
    , m_pCancel(NULL)
    , m_pSave(NULL)
    , m_pDelete(NULL)
    , m_pWeekdayRow(NULL)
    , m_pButtonRow(NULL)
    , m_hTitleIcon(NULL)
{
    for (int i = 0; i < memo::kDaysPerWeek; ++i)
    {
        m_pWeekdays[i] = NULL;
    }
}

CMemoEditDialog::~CMemoEditDialog()
{
    ClosePreview();
    if (m_picker && m_picker->IsWindow())
    {
        m_picker->DestroyWindow();
    }
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

bool CMemoEditDialog::Open(HWND owner, const net::MemoItem& item, balloonmemo::MemoEditMode mode,
                           const std::vector<net::ContactItem>& members)
{
    if (IsWindow())
    {
        RestoreOwner();
        DestroyWindow();
    }
    m_item = item;
    m_mode = mode;
    m_groupMode = (item.m_ctype == net::kMemoCtypeGroup);
    m_members = members;
    m_targets = item.m_targets;
    m_saving = false;
    m_ownerHwnd = ::IsWindow(owner) ? owner : NULL;
    if (!BuildWindow(m_ownerHwnd, m_groupMode))
    {
        return false;
    }
    FillControls();
    ShowError(std::wstring());
    SetSaving(false);
    if (m_ownerHwnd != NULL)
    {
        CenterWindow(m_ownerHwnd);
    }
    DisableOwner();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (m_pTitle != NULL && m_mode != balloonmemo::kMemoEditReadOnly)
    {
        m_pTitle->SetFocus();
    }
    LOG_INFO("memo edit dialog opened: memoid=%lld, ctype=%d, groupid=%lld, mode=%d, candidates=%u.",
             (long long)item.m_memoid, item.m_ctype, (long long)item.m_groupid, (int)mode,
             (uint32_t)members.size());
    return true;
}

bool CMemoEditDialog::BuildWindow(HWND owner, bool groupMode)
{
    //以发起打开的窗口为 owner：本窗口恒在它之上。控件通知仍回到本窗口：WS_OVERLAPPEDWINDOW 的顶层窗口
    //即便带 owner，::GetParent 也返回 NULL（与「发起接龙」窗口同一做法）
    LPCTSTR title = WindowTitleFor(m_mode, groupMode);
    if (Create(owner, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("memo edit dialog: Create failed.");
        return false;
    }
    SetTitle(title);
    SetTitleBarHeight(balloonmemo::kMemoEditTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    SetButtons(false, false, true);
    SetResizable(false);

    SetClientContent(balloonmemo::BuildMemoEditBody(groupMode));
    CaptureControls();
    //整窗尺寸（含标题栏），见 MemoEditLayout.h；布局单测 MEM-L* 校验按这个尺寸排得下
    const int height = groupMode ? balloonmemo::kMemoEditWindowHeightGroup : balloonmemo::kMemoEditWindowHeightPersonal;
    SetMinSize(balloonmemo::kMemoEditWindowWidth, height);
    ResizeClient(balloonmemo::kMemoEditWindowWidth, height);
    return true;
}

void CMemoEditDialog::CaptureControls()
{
    DuiControl* root = GetClientContent();
    if (root == NULL)
    {
        return;
    }
    m_pTitle      = dynamic_cast<DuiEdit*>(root->FindCtrlById(balloonmemo::kMemoEditIdTitle));
    m_pYear       = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdYear));
    m_pMonth      = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdMonth));
    m_pDay        = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdDay));
    m_pHour       = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdHour));
    m_pMinute     = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdMinute));
    m_pAdvance    = dynamic_cast<DuiComboBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdAdvance));
    m_pRepeat     = dynamic_cast<DuiComboBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdRepeat));
    for (int i = 0; i < memo::kDaysPerWeek; ++i)
    {
        m_pWeekdays[i] = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdWeekdayFirst + i));
    }
    m_pEndNever   = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdEndNever));
    m_pEndCount   = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdEndCount));
    m_pCount      = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(balloonmemo::kMemoEditIdCount));
    m_pStylePopup = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdStylePopup));
    m_pStyleFull  = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdStyleFull));
    m_pTargetAll  = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdTargetAll));
    m_pTargetPick = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdTargetPick));
    m_pPickBtn    = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdPickButton));
    m_pTargetText = dynamic_cast<DuiLabel*>(root->FindCtrlById(balloonmemo::kMemoEditIdTargetText));
    m_pNote       = dynamic_cast<DuiEdit*>(root->FindCtrlById(balloonmemo::kMemoEditIdNote));
    m_pError      = dynamic_cast<DuiLabel*>(root->FindCtrlById(balloonmemo::kMemoEditIdError));
    m_pCancel     = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdCancel));
    m_pSave       = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdSave));
    m_pDelete     = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoEditIdDelete));
    m_pWeekdayRow = root->FindCtrlById(balloonmemo::kMemoEditIdWeekdayRow);
    m_pButtonRow  = root->FindCtrlById(balloonmemo::kMemoEditIdButtonRow);
}

void CMemoEditDialog::FillControls()
{
    if (m_pTitle == NULL || m_pYear == NULL || m_pMonth == NULL || m_pDay == NULL || m_pHour == NULL
        || m_pMinute == NULL || m_pAdvance == NULL || m_pRepeat == NULL || m_pEndNever == NULL
        || m_pEndCount == NULL || m_pCount == NULL || m_pStylePopup == NULL || m_pStyleFull == NULL
        || m_pNote == NULL)
    {
        return;
    }
    m_pTitle->SetText(EncodeUtil::Utf8ToUnicode(m_item.m_title).c_str());
    m_pNote->SetText(EncodeUtil::Utf8ToUnicode(m_item.m_note).c_str());

    const int year = (int)(m_item.m_start / kYearUnit);
    const int thisYear = CurrentYear();
    const int minYear = (year > 0 && year < thisYear - kYearsBack) ? year : thisYear - kYearsBack;
    m_pYear->SetRange(minYear, kMaxYear);
    m_pYear->SetValue(year, false);
    m_pMonth->SetValue((int)((m_item.m_start / kMonthUnit) % kPartBase), false);
    m_pDay->SetValue((int)((m_item.m_start / kDayUnit) % kPartBase), false);
    m_pHour->SetValue((int)((m_item.m_start / kHourUnit) % kPartBase), false);
    m_pMinute->SetValue((int)(m_item.m_start % kPartBase), false);

    //提前量不在下拉的几档里时（别的客户端设的），取不超过它的最大一档
    int advanceSel = 0;
    for (int i = 0; i < balloonmemo::kMemoAdvanceChoiceCount; ++i)
    {
        if (balloonmemo::kMemoAdvanceChoices[i] <= m_item.m_advance)
        {
            advanceSel = i;
        }
    }
    m_pAdvance->SetCurSel(advanceSel, false);
    m_pRepeat->SetCurSel(m_item.m_repeat, false);

    for (int i = 0; i < memo::kDaysPerWeek; ++i)
    {
        if (m_pWeekdays[i] != NULL)
        {
            m_pWeekdays[i]->SetCheck(((m_item.m_weekdays >> i) & 1) != 0, false);
        }
    }

    const bool never = (m_item.m_repeat != kRepeatNone && m_item.m_count == 0);
    m_pEndNever->SetCheck(never, false);
    m_pEndCount->SetCheck(!never, false);
    m_pCount->SetValue(m_item.m_count > 0 ? m_item.m_count : 1, false);

    m_pStylePopup->SetCheck(m_item.m_style != kStyleFullscreen, false);
    m_pStyleFull->SetCheck(m_item.m_style == kStyleFullscreen, false);

    if (m_pTargetAll != NULL && m_pTargetPick != NULL)
    {
        m_pTargetAll->SetCheck(m_item.m_targetAll, false);
        m_pTargetPick->SetCheck(!m_item.m_targetAll, false);
    }

    SyncRepeatDependents();
    SyncTargetDependents();

    //左下角「删除」：只在修改已有的一条时显示（新建时还没有可删的，只读时没有权限）。显隐变化之后对按钮行
    //强制重排（开发约定文档 UI 约定第 5 条）
    if (m_pDelete != NULL)
    {
        m_pDelete->SetVisible(m_mode == balloonmemo::kMemoEditExisting && m_item.m_memoid > 0);
    }

    //只读：所有输入都置灰，「保存」隐藏，「取消」改为「关闭」
    if (m_mode == balloonmemo::kMemoEditReadOnly)
    {
        DuiControl* inputs[] = { m_pTitle, m_pYear, m_pMonth, m_pDay, m_pHour, m_pMinute, m_pAdvance, m_pRepeat,
                                 m_pEndNever, m_pEndCount, m_pCount, m_pStylePopup, m_pStyleFull, m_pTargetAll,
                                 m_pTargetPick, m_pPickBtn, m_pNote };
        for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        {
            if (inputs[i] != NULL)
            {
                inputs[i]->SetEnabled(false);
            }
        }
        for (int i = 0; i < memo::kDaysPerWeek; ++i)
        {
            if (m_pWeekdays[i] != NULL)
            {
                m_pWeekdays[i]->SetEnabled(false);
            }
        }
        if (m_pSave != NULL)
        {
            m_pSave->SetVisible(false);
        }
        if (m_pCancel != NULL)
        {
            m_pCancel->SetText(TR(L"关闭"));
        }
    }
    if (m_pButtonRow != NULL)
    {
        m_pButtonRow->ForceLayout(m_pButtonRow->GetRect());
    }
}

void CMemoEditDialog::SyncRepeatDependents()
{
    if (m_pRepeat == NULL)
    {
        return;
    }
    const int repeat = m_pRepeat->GetCurSel();
    const bool weekly = (repeat == kRepeatWeekly);

    //「每周」一行：只在每周重复时显示行内控件；行本身始终占位，下面各行不上下移动。
    //显隐变化之后对这一行强制重排（开发约定文档 UI 约定第 5 条）
    if (m_pWeekdayRow != NULL)
    {
        for (size_t i = 0; i < m_pWeekdayRow->Children().size(); ++i)
        {
            m_pWeekdayRow->Children()[i]->SetVisible(weekly);
        }
        m_pWeekdayRow->ForceLayout(m_pWeekdayRow->GetRect());
        m_pWeekdayRow->Invalidate();
    }
    if (m_mode == balloonmemo::kMemoEditReadOnly)
    {
        return;
    }
    bool anyDay = false;
    for (int i = 0; i < memo::kDaysPerWeek; ++i)
    {
        if (m_pWeekdays[i] != NULL)
        {
            m_pWeekdays[i]->SetEnabled(weekly);
            anyDay = anyDay || m_pWeekdays[i]->IsChecked();
        }
    }
    //刚切到「每周」、一天都没选时，替用户选上开始那天是星期几
    if (weekly && !anyDay && m_pYear != NULL && m_pMonth != NULL && m_pDay != NULL)
    {
        const int year = CommitTypedValue(m_pYear);
        const int month = CommitTypedValue(m_pMonth);
        int day = CommitTypedValue(m_pDay);
        const int dim = memo::DaysInMonth(year, month);
        if (day > dim)
        {
            day = dim;
        }
        const int weekday = memo::WeekdayOfDays(memo::DaysFromCivil(year, month, day));
        if (weekday >= 0 && weekday < memo::kDaysPerWeek && m_pWeekdays[weekday] != NULL)
        {
            m_pWeekdays[weekday]->SetCheck(true, false);
        }
    }
    const bool repeats = (repeat != kRepeatNone);
    if (m_pEndNever != NULL)
    {
        m_pEndNever->SetEnabled(repeats);
    }
    if (m_pEndCount != NULL)
    {
        m_pEndCount->SetEnabled(repeats);
    }
    if (m_pCount != NULL)
    {
        m_pCount->SetEnabled(repeats && m_pEndCount != NULL && m_pEndCount->IsChecked());
    }
}

void CMemoEditDialog::SyncTargetDependents()
{
    if (!m_groupMode || m_pTargetPick == NULL || m_pPickBtn == NULL || m_pTargetText == NULL)
    {
        return;
    }
    const bool pick = m_pTargetPick->IsChecked();
    if (m_mode != balloonmemo::kMemoEditReadOnly)
    {
        m_pPickBtn->SetEnabled(pick);
    }
    CString text;
    if (pick)
    {
        if (m_targets.empty())
        {
            text = TR(L"还没有选择成员");
        }
        else
        {
            text.Format(TR(L"已选 %u 人"), (unsigned)m_targets.size());
        }
    }
    m_pTargetText->SetText(text);
}

net::MemoItem CMemoEditDialog::ReadControls() const
{
    net::MemoItem memo = m_item;
    const int year = CommitTypedValue(m_pYear);
    const int month = CommitTypedValue(m_pMonth);
    const int day = CommitTypedValue(m_pDay);
    const int hour = CommitTypedValue(m_pHour);
    const int minute = CommitTypedValue(m_pMinute);
    memo.m_start = year * kYearUnit + month * kMonthUnit + day * kDayUnit + hour * kHourUnit + minute;

    const int advanceSel = (m_pAdvance != NULL) ? m_pAdvance->GetCurSel() : 0;
    memo.m_advance = (advanceSel >= 0 && advanceSel < balloonmemo::kMemoAdvanceChoiceCount)
                         ? balloonmemo::kMemoAdvanceChoices[advanceSel] : 0;
    memo.m_repeat = (m_pRepeat != NULL && m_pRepeat->GetCurSel() >= 0) ? m_pRepeat->GetCurSel() : kRepeatNone;

    memo.m_weekdays = 0;
    if (memo.m_repeat == kRepeatWeekly)
    {
        for (int i = 0; i < memo::kDaysPerWeek; ++i)
        {
            if (m_pWeekdays[i] != NULL && m_pWeekdays[i]->IsChecked())
            {
                memo.m_weekdays |= (1 << i);
            }
        }
    }

    if (memo.m_repeat == kRepeatNone)
    {
        memo.m_count = 1;
    }
    else if (m_pEndNever != NULL && m_pEndNever->IsChecked())
    {
        memo.m_count = 0;
    }
    else
    {
        memo.m_count = CommitTypedValue(m_pCount);
    }

    memo.m_style = (m_pStyleFull != NULL && m_pStyleFull->IsChecked()) ? kStyleFullscreen : kStylePopup;

    if (m_groupMode)
    {
        memo.m_targetAll = (m_pTargetAll != NULL && m_pTargetAll->IsChecked());
        memo.m_targets = memo.m_targetAll ? std::vector<int64_t>() : m_targets;
    }
    else
    {
        memo.m_targetAll = false;
        memo.m_targets.clear();
    }
    return memo;
}

void CMemoEditDialog::OnSubmit()
{
    if (m_saving || m_mode == balloonmemo::kMemoEditReadOnly || m_pTitle == NULL || m_pNote == NULL)
    {
        return;
    }
    net::MemoItem memo = ReadControls();
    const std::wstring title = (LPCTSTR)m_pTitle->GetText();
    const std::wstring note = (LPCTSTR)m_pNote->GetText();
    const balloonmemo::MemoEditError err = balloonmemo::ValidateMemoEdit(title, note, memo);
    if (err != balloonmemo::kMemoEditOk)
    {
        LOG_INFO("memo edit check failed: memoid=%lld, error=%d, start=%lld.", (long long)memo.m_memoid, (int)err,
                 (long long)memo.m_start);
        ShowError(balloonmemo::MemoEditErrorText(err));
        return;
    }
    memo.m_title = EncodeUtil::UnicodeToUtf8(title);
    memo.m_note = EncodeUtil::UnicodeToUtf8(note);
    if (!CMemoCenter::Instance().Save(memo))
    {
        ShowError(TR(L"网络未连接，请稍后重试。"));
        return;
    }
    ShowError(std::wstring());
    SetSaving(true);
}

void CMemoEditDialog::ShowFullscreenPreview()
{
    if (!IsWindow() || m_pTitle == NULL || m_pNote == NULL)
    {
        return;
    }
    //时间取当前填写的开始时间。m_start 是十进制 yyyymmddhhmm，须先换算成本地分钟序号再交给
    //EventTimeText；日期不存在（如 2 月 30 日）时提示与保存时相同的错误，不弹预览
    const net::MemoItem edited = ReadControls();
    const memo::LocalMinute now = memo::NowLocalMinute();
    const std::wstring groupName = m_groupMode ? CMemoCenter::Instance().GroupNameOf(m_item.m_groupid) : std::wstring();
    balloonmemo::FullscreenReminderText text;
    if (!balloonmemo::BuildPreviewReminderText((LPCTSTR)m_pTitle->GetText(), (LPCTSTR)m_pNote->GetText(),
                                               edited.m_start, now, groupName, text))
    {
        LOG_INFO("memo fullscreen preview refused: start %lld is not a valid date time.", (long long)edited.m_start);
        ShowError(balloonmemo::MemoEditErrorText(balloonmemo::kMemoEditStartInvalid));
        return;
    }
    ShowError(std::wstring());
    //创建人一行：已保存的群备忘按备忘本里的创建人与创建时间显示；新建的还没有创建人，留空
    std::wstring creator;
    if (m_groupMode && m_item.m_memoid > 0)
    {
        creator = CMemoCenter::Instance().CreatorLineOf(m_item, now);
    }

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
    {
        LOG_ERROR("memo fullscreen preview: GetMonitorInfo failed, error=%u.", (unsigned)::GetLastError());
        return;
    }
    if (!m_previewWindow)
    {
        m_previewWindow.reset(new CMemoFullscreenWindow());
    }
    LOG_INFO("memo fullscreen preview requested: memoid=%lld, group=%d, start=%lld.", (long long)m_item.m_memoid,
             m_groupMode ? 1 : 0, (long long)edited.m_start);
    m_previewWindow->ShowPreview(text, creator, mi.rcMonitor, m_hWnd);
}

void CMemoEditDialog::ClosePreview()
{
    if (m_previewWindow && m_previewWindow->IsPreviewing())
    {
        m_previewWindow->CloseWindow();
    }
}

void CMemoEditDialog::OnSaveResult(int code)
{
    if (!m_saving)
    {
        return;
    }
    SetSaving(false);
    if (code == 0)
    {
        CloseDialog();
        return;
    }
    LOG_ERROR("memo save failed: memoid=%lld, code=%d.", (long long)m_item.m_memoid, code);
    if (IsWindow())
    {
        ShowError(balloonmemo::MemoFailureText(code, false));
    }
}

void CMemoEditDialog::OnMemoDeleted(int64_t memoid)
{
    if (IsWindow() && memoid > 0 && memoid == m_item.m_memoid)
    {
        LOG_INFO("memo edit dialog closed: memo %lld was deleted.", (long long)memoid);
        CloseDialog();
    }
}

void CMemoEditDialog::CloseDialog()
{
    ClosePreview();
    if (m_picker && m_picker->IsWindow())
    {
        m_picker->DestroyWindow();
    }
    if (IsWindow())
    {
        RestoreOwner();
        DestroyWindow();
    }
    m_saving = false;
}

void CMemoEditDialog::OpenTargetPicker()
{
    if (!m_groupMode || m_members.empty())
    {
        ShowError(TR(L"成员列表还没有加载，请稍后再试。"));
        return;
    }
    if (!m_picker)
    {
        m_picker.reset(new CCreateGroupDlg(NULL));
    }
    m_picker->SetPickSink(this);
    m_picker->SetInitialSelection(m_targets);
    m_picker->OpenForPick(m_hWnd, CCreateGroupDlg::kPurposeMemoTargets, m_item.m_groupid,
                          (UINT)CMemoCenter::Instance().SelfId(), m_members, std::vector<net::DepartmentItem>());
}

void CMemoEditDialog::OnMembersPicked(const std::vector<int64_t>& userIds)
{
    if (!IsWindow())
    {
        return;
    }
    m_targets = userIds;
    if (m_pTargetPick != NULL && m_pTargetAll != NULL)
    {
        m_pTargetPick->SetCheck(true, false);
        m_pTargetAll->SetCheck(false, false);
    }
    SyncTargetDependents();
    ShowError(std::wstring());
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("memo targets picked: memoid=%lld, count=%u.", (long long)m_item.m_memoid, (uint32_t)userIds.size());
}

void CMemoEditDialog::ShowError(const std::wstring& text)
{
    if (m_pError != NULL)
    {
        m_pError->SetText(text.c_str());
    }
}

void CMemoEditDialog::SetSaving(bool saving)
{
    m_saving = saving;
    if (m_pSave != NULL)
    {
        m_pSave->SetEnabled(!saving);
        m_pSave->SetText(saving ? TR(L"正在保存") : TR(L"保存"));
    }
}

void CMemoEditDialog::DisableOwner()
{
    const bool ownerIsWindow  = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    const bool ownerIsEnabled = ownerIsWindow && (::IsWindowEnabled(m_ownerHwnd) != FALSE);
    if (m_ownerGuard.OnOpen(ownerIsWindow, ownerIsEnabled))
    {
        ::EnableWindow(m_ownerHwnd, FALSE);
    }
}

void CMemoEditDialog::RestoreOwner()
{
    const bool ownerIsWindow = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    if (!m_ownerGuard.OnClose(ownerIsWindow))
    {
        return;
    }
    ::EnableWindow(m_ownerHwnd, TRUE);
}

LRESULT CMemoEditDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //「保存」
        case balloonmemo::kMemoEditIdSave:
            OnSubmit();
            return 0;

        //「取消」/「关闭」：已发出的保存照常等应答，应答到达时对话框已关，只记日志
        case balloonmemo::kMemoEditIdCancel:
            PostMessage(WM_CLOSE);
            return 0;

        //左下角「删除」：确认后发 2403；删除成功时中枢调 OnMemoDeleted 关闭本对话框
        case balloonmemo::kMemoEditIdDelete:
            if (m_mode == balloonmemo::kMemoEditExisting && m_item.m_memoid > 0)
            {
                CMemoCenter::Instance().ConfirmAndDelete(m_hWnd, m_item.m_memoid);
            }
            return 0;

        //「选择成员」
        case balloonmemo::kMemoEditIdPickButton:
            OpenTargetPicker();
            return 0;

        //「方式」一行的「预览效果」：按当前填写的内容播放全屏提醒，新建、修改、只读三种模式都可用
        case balloonmemo::kMemoEditIdPreviewFull:
            ShowFullscreenPreview();
            return 0;

        //「结束」两个单选：切换后「提醒 N 次」的次数框跟着可用或置灰
        case balloonmemo::kMemoEditIdEndNever:
        case balloonmemo::kMemoEditIdEndCount:
            SyncRepeatDependents();
            return 0;

        //「提醒对象」两个单选
        case balloonmemo::kMemoEditIdTargetAll:
        case balloonmemo::kMemoEditIdTargetPick:
            SyncTargetDependents();
            return 0;

        //其它控件的点击：交给框架
        default:
            break;
        }
    }
    //「重复」下拉换了选项：星期勾选与「结束」一行跟着可用或置灰。ctrlId 写进条件里，免得截住别的控件的同码通知
    else if (pNotify->code == DUIN_VALUECHANGED && pNotify->ctrlId == balloonmemo::kMemoEditIdRepeat)
    {
        SyncRepeatDependents();
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMemoEditDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_saving)
    {
        LOG_INFO("memo edit dialog closed while saving: memoid=%lld.", (long long)m_item.m_memoid);
    }
    CloseDialog();
    return 0;
}

LRESULT CMemoEditDialog::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //owner 关闭时会直接销毁本窗口，不经过 WM_CLOSE，在这里兜底恢复（已恢复过时不做事）
    RestoreOwner();
    ClosePreview();
    bHandled = FALSE;
    return 0;
}
