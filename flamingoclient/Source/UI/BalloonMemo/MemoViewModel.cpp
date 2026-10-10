/**
 *  MemoViewModel.cpp
 *
 *  「备忘与提醒」窗口共用的纯逻辑实现，说明见 MemoViewModel.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 AutoReplyPolicy.cpp 同例。
#include "MemoViewModel.h"
#include <stdio.h>
#include <wchar.h>
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Feature/Memo/MemoBook.h"   // RuleOfItem：备忘的重复规则

namespace balloonmemo {

//一年的月数、一天的小时数、一小时的分钟数
const int kMonthsPerYear = 12;
const int kHoursPerDay = 24;
const int kMinutesPerHour = 60;

//今天新建时下一个整点已经到了次日，改取今天的最后一分钟 23:59
const int kLastHourOfDay = 23;
const int kLastMinuteOfHour = 59;

//组装显示文字用的缓冲区大小（宽字符个数）
const size_t kTextBufSize = 64;

//UTF-16 低位代理的范围：一个由两个 UTF-16 字符组成的字符只按一个计数
const wchar_t kLowSurrogateFirst = 0xDC00;
const wchar_t kLowSurrogateLast = 0xDFFF;

//全角空格，与半角空白一样在标题首尾去掉
const wchar_t kFullwidthSpace = 0x3000;

//group_member_role 里的管理员取值（群主为 2，也满足「≥ 管理员」）
const int kGroupRoleAdmin = 1;

//memo_repeat_type 的取值
const int kRepeatNone = 0;
const int kRepeatDaily = 1;
const int kRepeatWorkday = 2;
const int kRepeatWeekly = 3;
const int kRepeatMonthly = 4;
const int kRepeatYearly = 5;

//memo_style 的右下角弹窗
const int kStylePopup = 1;

//十进制 yyyymmddhhmm 里月、日的位权与每段的进位基数
const int64_t kStartMonthUnit = 1000000LL;
const int64_t kStartDayUnit = 10000LL;
const int64_t kStartPartBase = 100LL;

// ---------------- 月历 ----------------

void MonthGridRange(int year, int month, int64_t& firstDay, int64_t& endDay)
{
    const int64_t first = memo::DaysFromCivil(year, month, 1);
    firstDay = first - memo::WeekdayOfDays(first);
    endDay = firstDay + kCalendarCells;
}

void BuildMonthCells(int year, int month, int64_t today, int64_t selected, const std::set<int64_t>& eventDays,
                     std::vector<CalendarCell>& out)
{
    out.clear();
    int64_t firstDay = 0;
    int64_t endDay = 0;
    MonthGridRange(year, month, firstDay, endDay);
    for (int64_t d = firstDay; d < endDay; ++d)
    {
        int y = 0;
        int m = 0;
        int dom = 0;
        memo::CivilFromDays(d, y, m, dom);
        CalendarCell cell;
        cell.m_day = d;
        cell.m_dayOfMonth = dom;
        cell.m_inMonth = (y == year && m == month);
        cell.m_isToday = (d == today);
        cell.m_isSelected = (d == selected);
        cell.m_hasEvent = (eventDays.find(d) != eventDays.end());
        out.push_back(cell);
    }
}

void ShiftMonth(int& year, int& month, int delta)
{
    int total = year * kMonthsPerYear + (month - 1) + delta;
    year = total / kMonthsPerYear;
    month = total % kMonthsPerYear + 1;
}

std::wstring MonthTitle(int year, int month)
{
    wchar_t buf[kTextBufSize] = { 0 };
    swprintf(buf, kTextBufSize, TR(L"%d年%d月"), year, month);
    return std::wstring(buf);
}

void CalendarYearRange(int thisYear, int shownYear, int& first, int& last)
{
    first = thisYear - kCalendarYearsBack;
    last = thisYear + kCalendarYearsAhead;
    if (shownYear < first)
        first = shownYear;
    if (shownYear > last)
        last = shownYear;
    if (first < kCalendarYearMin)
        first = kCalendarYearMin;
    if (last > kCalendarYearMax)
        last = kCalendarYearMax;
    if (first > last)
        first = last;
}

int64_t KeepDayOfMonth(int year, int month, int64_t oldDay)
{
    int oldYear = 0;
    int oldMonth = 0;
    int dayOfMonth = 0;
    memo::CivilFromDays(oldDay, oldYear, oldMonth, dayOfMonth);
    const int daysInMonth = memo::DaysInMonth(year, month);
    if (dayOfMonth > daysInMonth)
        dayOfMonth = daysInMonth;
    return memo::DaysFromCivil(year, month, dayOfMonth);
}

// ---------------- 编辑对话框 ----------------

net::MemoItem DefaultMemoForDay(int64_t selectedDay, memo::LocalMinute now, int ctype, int64_t groupid)
{
    net::MemoItem item;
    item.m_ctype = ctype;
    item.m_groupid = (ctype == net::kMemoCtypeGroup) ? groupid : 0;
    item.m_repeat = kRepeatNone;
    item.m_count = 1;
    item.m_advance = 0;
    item.m_style = kStylePopup;
    item.m_targetAll = false;

    int hour = kMemoDefaultHour;
    int minute = 0;
    const int64_t today = memo::LocalMinute(now) / memo::kMinutesPerDay;
    if (selectedDay == today)
    {
        const int64_t tod = now - today * memo::kMinutesPerDay;
        const int nextHour = static_cast<int>(tod / kMinutesPerHour) + 1;
        if (nextHour >= kHoursPerDay)
        {
            hour = kLastHourOfDay;
            minute = kLastMinuteOfHour;
        }
        else
        {
            hour = nextHour;
        }
    }
    item.m_start = memo::StartFromLocalMinute(selectedDay * memo::kMinutesPerDay + hour * kMinutesPerHour + minute);
    return item;
}

//按字符数统计：两个 UTF-16 字符组成的一个字符只计一次
static size_t CharCount(const std::wstring& text)
{
    size_t n = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < kLowSurrogateFirst || text[i] > kLowSurrogateLast)
            ++n;
    }
    return n;
}

static bool IsBlank(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == kFullwidthSpace;
}

static std::wstring Trim(const std::wstring& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && IsBlank(text[begin]))
        ++begin;
    while (end > begin && IsBlank(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

static std::wstring TrimRight(const std::wstring& text)
{
    size_t end = text.size();
    while (end > 0 && IsBlank(text[end - 1]))
        --end;
    return text.substr(0, end);
}

MemoEditError ValidateMemoEdit(const std::wstring& title, const std::wstring& note, const net::MemoItem& memo)
{
    const std::wstring t = Trim(title);
    if (t.empty())
        return kMemoEditTitleEmpty;
    if (CharCount(t) > kMemoTitleMaxChars)
        return kMemoEditTitleTooLong;
    if (CharCount(TrimRight(note)) > kMemoNoteMaxChars)
        return kMemoEditNoteTooLong;

    memo::LocalMinute start = 0;
    if (!memo::LocalMinuteFromStart(memo.m_start, start))
        return kMemoEditStartInvalid;
    if (memo.m_advance < 0 || memo.m_advance > kMemoAdvanceMaxMinutes)
        return kMemoEditAdvanceInvalid;
    if (memo.m_repeat == kRepeatWeekly && (memo.m_weekdays & memo::kMemoWeekdayAllMask) == 0)
        return kMemoEditWeekdaysEmpty;
    if (memo.m_repeat != kRepeatNone && (memo.m_count < 0 || memo.m_count > kMemoCountMax))
        return kMemoEditCountInvalid;
    if (memo.m_ctype == net::kMemoCtypeGroup && !memo.m_targetAll && memo.m_targets.empty())
        return kMemoEditTargetsEmpty;
    return kMemoEditOk;
}

std::wstring MemoEditErrorText(MemoEditError err)
{
    switch (err)
    {
    //标题为空
    case kMemoEditTitleEmpty:
        return TR(L"请填写标题");
    //标题过长
    case kMemoEditTitleTooLong:
        return TR(L"标题不能超过 50 个字");
    //备注过长
    case kMemoEditNoteTooLong:
        return TR(L"备注不能超过 200 个字");
    //开始时间不存在（如 2 月 30 日）
    case kMemoEditStartInvalid:
        return TR(L"请选择一个有效的日期和时间");
    //提前分钟越界
    case kMemoEditAdvanceInvalid:
        return TR(L"提前提醒最多 1440 分钟（一天）");
    //每周重复没选星期
    case kMemoEditWeekdaysEmpty:
        return TR(L"每周重复至少要选一天");
    //次数越界
    case kMemoEditCountInvalid:
        return TR(L"提醒次数请填 1～999");
    //群备忘没有提醒对象
    case kMemoEditTargetsEmpty:
        return TR(L"请选择要提醒的成员");
    //可以保存，不需要提示
    default:
        return std::wstring();
    }
}

std::wstring MemoFailureText(int code, bool isDelete)
{
    //Protocol.h 的 error_code 里与备忘有关的取值。本文件不引 Protocol.h（与测试工程共用时依赖最少），按数值比较
    const int kErrNotGroupMember = 302;
    const int kErrNotGroupAdmin = 303;
    const int kErrGroupNotExist = 301;
    const int kErrGroupSensitive = 312;
    const int kErrMemoNotExist = 1000;
    const int kErrMemoInvalid = 1001;
    const int kErrMemoTooMany = 1002;
    const int kErrMemoTargetInvalid = 1003;

    wchar_t buf[kTextBufSize] = { 0 };
    switch (code)
    {
    //本端超时：发出之后 10 秒没有应答，多半是服务端版本太旧、不认识备忘命令
    case kMemoCodeTimeout:
        return isDelete ? TR(L"服务器没有应答，备忘没有删除，请稍后重试")
                        : TR(L"服务器没有应答，备忘没有保存，请稍后重试");
    //备忘已被别人删除，或所属不一致
    case kErrMemoNotExist:
        return TR(L"这条备忘不存在或已被删除");
    //字段不合法（本端校验通过、服务端仍拒绝时才会出现）
    case kErrMemoInvalid:
        return TR(L"备忘内容不合法，请检查后重试");
    //新建时已达上限
    case kErrMemoTooMany:
        return TR(L"备忘数量已达上限，请先删除一些不再需要的备忘");
    //提醒对象里有人已不在本群
    case kErrMemoTargetInvalid:
        return TR(L"提醒对象里有人已不在本群，请重新选择");
    //群不存在（已解散）
    case kErrGroupNotExist:
        return TR(L"该群已不存在");
    //本人已不是群成员
    case kErrNotGroupMember:
        return TR(L"你已不是该群成员");
    //本人不是群主或管理员
    case kErrNotGroupAdmin:
        return isDelete ? TR(L"只有群主或管理员能删除群备忘") : TR(L"只有群主或管理员能设置群备忘");
    //标题或备注命中企业敏感词
    case kErrGroupSensitive:
        return TR(L"标题或备注包含敏感词，未保存");
    //其它错误：带上错误码便于反馈
    default:
        swprintf(buf, kTextBufSize, isDelete ? TR(L"删除失败（错误码 %d）") : TR(L"保存失败（错误码 %d）"), code);
        return std::wstring(buf);
    }
}

bool CanEditGroupMemo(int role)
{
    return role >= kGroupRoleAdmin;
}

// ---------------- 显示用的文字 ----------------

std::wstring RepeatText(const net::MemoItem& memo)
{
    //星期掩码各位对应的汉字：bit0 周一 ... bit6 周日
    static const wchar_t* const kWeekdayNames[memo::kDaysPerWeek] = { TR(L"一"), TR(L"二"), TR(L"三"), TR(L"四"), TR(L"五"), TR(L"六"), TR(L"日") };

    std::wstring text;
    const int month = static_cast<int>((memo.m_start / kStartMonthUnit) % kStartPartBase);
    const int day = static_cast<int>((memo.m_start / kStartDayUnit) % kStartPartBase);
    wchar_t buf[kTextBufSize] = { 0 };
    switch (memo.m_repeat)
    {
    //不重复：只提醒一次，不追加次数
    case kRepeatNone:
        return TR(L"不重复");
    //每天
    case kRepeatDaily:
        text = TR(L"每天");
        break;
    //每个工作日
    case kRepeatWorkday:
        text = TR(L"每个工作日");
        break;
    //每周选中的那几天，如「每周一、三」
    case kRepeatWeekly:
        text = TR(L"每周");
        for (int i = 0; i < memo::kDaysPerWeek; ++i)
        {
            if (((memo.m_weekdays >> i) & 1) == 0)
                continue;
            if (text.size() > 2)
                text += L"、";
            text += kWeekdayNames[i];
        }
        break;
    //每月几号
    case kRepeatMonthly:
        swprintf(buf, kTextBufSize, TR(L"每月 %d 日"), day);
        text = buf;
        break;
    //每年几月几号
    case kRepeatYearly:
        swprintf(buf, kTextBufSize, TR(L"每年 %d 月 %d 日"), month, day);
        text = buf;
        break;
    //未知的重复方式（新版本服务端加的取值），按不重复显示
    default:
        return TR(L"不重复");
    }

    if (memo.m_count > 0)
    {
        swprintf(buf, kTextBufSize, TR(L"，共 %d 次"), memo.m_count);
        text += buf;
    }
    return text;
}

std::wstring AdvanceText(int advanceMinutes)
{
    if (advanceMinutes <= 0)
        return TR(L"到点提醒");
    wchar_t buf[kTextBufSize] = { 0 };
    if (advanceMinutes % memo::kMinutesPerDay == 0)
        swprintf(buf, kTextBufSize, TR(L"提前 %d 天"), static_cast<int>(advanceMinutes / memo::kMinutesPerDay));
    else if (advanceMinutes % kMinutesPerHour == 0)
        swprintf(buf, kTextBufSize, TR(L"提前 %d 小时"), advanceMinutes / kMinutesPerHour);
    else
        swprintf(buf, kTextBufSize, TR(L"提前 %d 分钟"), advanceMinutes);
    return std::wstring(buf);
}

std::wstring EventTimeText(memo::LocalMinute event, memo::LocalMinute now)
{
    int y = 0;
    int m = 0;
    int d = 0;
    int hh = 0;
    int mm = 0;
    memo::PartsFromLocalMinute(event, y, m, d, hh, mm);
    int ny = 0;
    int nm = 0;
    int nd = 0;
    int nhh = 0;
    int nmm = 0;
    memo::PartsFromLocalMinute(now, ny, nm, nd, nhh, nmm);

    wchar_t buf[kTextBufSize] = { 0 };
    if (y == ny && m == nm && d == nd)
        swprintf(buf, kTextBufSize, TR(L"今天 %02d:%02d"), hh, mm);
    else if (y == ny)
        swprintf(buf, kTextBufSize, TR(L"%d月%d日 %02d:%02d"), m, d, hh, mm);
    else
        swprintf(buf, kTextBufSize, TR(L"%d年%d月%d日 %02d:%02d"), y, m, d, hh, mm);
    return std::wstring(buf);
}

FullscreenReminderText BuildFullscreenReminderText(const std::wstring& title, const std::wstring& note,
                                                   memo::LocalMinute event, memo::LocalMinute now,
                                                   const std::wstring& groupName)
{
    FullscreenReminderText text;
    //保存时标题必填，真实提醒不会走到占位文字；编辑对话框里还没填标题就点「预览」时才用得上
    text.m_title = Trim(title).empty() ? std::wstring(TR(L"（未填写标题）")) : title;
    text.m_time = EventTimeText(event, now);
    if (!groupName.empty())
    {
        text.m_time += TR(L" · 群：");
        text.m_time += groupName;
    }
    text.m_note = note;
    return text;
}

bool BuildPreviewReminderText(const std::wstring& title, const std::wstring& note, int64_t start,
                              memo::LocalMinute now, const std::wstring& groupName, FullscreenReminderText& out)
{
    //start 是 yyyymmddhhmm，不能直接当本地分钟序号用（直接传给 EventTimeText 会显示成几十万年以后）
    memo::LocalMinute event = 0;
    if (!memo::LocalMinuteFromStart(start, event))
    {
        return false;
    }
    out = BuildFullscreenReminderText(title, note, event, now, groupName);
    return true;
}

namespace {

//UTF-8 转为 UTF-16；空串或无法转换时返回空串
std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring text((size_t)len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &text[0], len);
    return text;
}

} // anonymous namespace

std::wstring TargetsText(const net::MemoItem& memo)
{
    if (memo.m_ctype != net::kMemoCtypeGroup)
        return std::wstring();
    if (memo.m_targetAll)
        return TR(L"提醒全体成员");
    return TRF(L"提醒 {0} 人", (unsigned)memo.m_targets.size());
}

std::wstring NoteOneLine(const std::string& noteUtf8)
{
    std::wstring text = Utf8ToWide(noteUtf8);
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\r' || text[i] == L'\n')
            text[i] = L' ';
    }
    return text;
}

std::wstring CreatorText(const std::wstring& name, memo::LocalMinute createdAt, memo::LocalMinute now)
{
    if (createdAt <= 0)
        return TRF(L"{0} 创建", name);
    return TRF(L"{0} · {1} 创建", name, EventTimeText(createdAt, now));
}

MemoCardText BuildGroupTabCard(const net::MemoItem& memo, memo::LocalMinute now, const std::wstring& creatorName,
                               memo::LocalMinute createdAt, bool editable)
{
    MemoCardText card;
    card.m_memoid = memo.m_memoid;
    card.m_title = Utf8ToWide(memo.m_title);
    card.m_note = NoteOneLine(memo.m_note);
    //提醒时刻（事件时间减去提前量）晚于现在的下一次事件
    memo::MemoOccurrence occ;
    if (memo::FindEventAtOrAfter(memo::RuleOfItem(memo), now + memo.m_advance + 1, occ))
    {
        card.m_when = EventTimeText(occ.m_event, now);
        card.m_remindAt = occ.m_event - memo.m_advance;
    }
    else
    {
        card.m_when = TR(L"已结束");
        card.m_ended = true;
    }
    card.m_rule = RepeatText(memo);
    const std::wstring targets = TargetsText(memo);
    if (!targets.empty())
    {
        card.m_rule += L" · ";
        card.m_rule += targets;
    }
    card.m_creator = CreatorText(creatorName, createdAt, now);
    card.m_editable = editable;
    return card;
}

MemoCardText BuildDayCard(const net::MemoItem& memo, memo::LocalMinute event, const std::wstring& groupName,
                          const std::wstring& creatorName, memo::LocalMinute createdAt, memo::LocalMinute now)
{
    MemoCardText card;
    card.m_memoid = memo.m_memoid;
    card.m_title = Utf8ToWide(memo.m_title);
    card.m_note = NoteOneLine(memo.m_note);
    const int64_t tod = event % memo::kMinutesPerDay;
    wchar_t buf[kTextBufSize] = { 0 };
    swprintf(buf, kTextBufSize, L"%02d:%02d", (int)(tod / kMinutesPerHour), (int)(tod % kMinutesPerHour));
    card.m_when = buf;
    card.m_rule = RepeatText(memo);
    card.m_rule += L" · ";
    card.m_rule += AdvanceText(memo.m_advance);
    const bool isGroup = (memo.m_ctype == net::kMemoCtypeGroup);
    if (isGroup)
    {
        card.m_rule += TR(L" · 群：");
        card.m_rule += groupName;
        card.m_creator = CreatorText(creatorName, createdAt, now);
    }
    card.m_editable = !isGroup;
    return card;
}

// ---------------- 右下角提醒弹窗 ----------------

bool CReminderQueue::Add(const std::vector<memo::MemoFire>& fires)
{
    bool added = false;
    for (size_t i = 0; i < fires.size(); ++i)
    {
        if (AddOne(fires[i].m_memoid, fires[i].m_event, fires[i].m_kind == memo::kMemoFireMissed))
            added = true;
    }
    return added;
}

bool CReminderQueue::AddOne(int64_t memoid, memo::LocalMinute event, bool missed)
{
    //同一条备忘只列一行：新的一次事件替换旧的；到点的提醒优先于错过的
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        ReminderEntry& e = m_entries[i];
        if (e.m_memoid != memoid)
            continue;
        if (e.m_event != event)
        {
            e.m_event = event;
            e.m_missed = missed;
            return true;
        }
        if (e.m_missed == missed || !e.m_missed)
            return false;
        e.m_missed = false;
        return true;
    }
    ReminderEntry entry;
    entry.m_memoid = memoid;
    entry.m_event = event;
    entry.m_missed = missed;
    m_entries.push_back(entry);
    return true;
}

bool CReminderQueue::Dismiss(int64_t memoid)
{
    for (std::vector<ReminderEntry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->m_memoid == memoid)
        {
            m_entries.erase(it);
            return true;
        }
    }
    return false;
}

void CReminderQueue::DismissMissed()
{
    std::vector<ReminderEntry> kept;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (!m_entries[i].m_missed)
            kept.push_back(m_entries[i]);
    }
    m_entries.swap(kept);
}

bool CReminderQueue::TakeForSnooze(int64_t memoid, memo::LocalMinute& outEvent)
{
    for (std::vector<ReminderEntry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->m_memoid == memoid)
        {
            outEvent = it->m_event;
            m_entries.erase(it);
            return true;
        }
    }
    return false;
}

void CReminderQueue::RemoveMemo(int64_t memoid)
{
    Dismiss(memoid);
}

std::vector<ReminderEntry> CReminderQueue::DueEntries() const
{
    std::vector<ReminderEntry> out;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (!m_entries[i].m_missed)
            out.push_back(m_entries[i]);
    }
    return out;
}

std::vector<ReminderEntry> CReminderQueue::MissedEntries() const
{
    std::vector<ReminderEntry> out;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].m_missed)
            out.push_back(m_entries[i]);
    }
    return out;
}

bool CReminderQueue::IsEmpty() const
{
    return m_entries.empty();
}

void CReminderQueue::Clear()
{
    m_entries.clear();
}

// ---------------- 全屏提醒 ----------------

RECT ChooseFullscreenMonitor(bool panelVisible, const RECT& panelMonitor, const RECT& primaryMonitor)
{
    return panelVisible ? panelMonitor : primaryMonitor;
}

CFullscreenTimeout::CFullscreenTimeout()
    : m_started(false)
    , m_lastActivity(0)
{
}

void CFullscreenTimeout::Start(DWORD nowMs)
{
    m_started = true;
    m_lastActivity = nowMs;
}

void CFullscreenTimeout::OnUserActivity(DWORD nowMs)
{
    if (m_started)
        m_lastActivity = nowMs;
}

bool CFullscreenTimeout::ShouldFallback(DWORD nowMs) const
{
    //GetTickCount 约 49.7 天回绕一次，无符号减法在回绕后仍得到正确的间隔
    return m_started && (nowMs - m_lastActivity) >= kFullscreenFallbackMs;
}

DWORD CFullscreenTimeout::RemainingMs(DWORD nowMs) const
{
    if (!m_started)
        return kFullscreenFallbackMs;
    const DWORD elapsed = nowMs - m_lastActivity;
    return (elapsed >= kFullscreenFallbackMs) ? 0 : (kFullscreenFallbackMs - elapsed);
}

} // namespace balloonmemo
