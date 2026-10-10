/**
 *  MemoViewModel.h
 *
 *  「备忘与提醒」各窗口共用的纯逻辑：月历的 6×7 排法与翻月、编辑对话框的默认值与校验、提醒文字、
 *  右下角提醒弹窗的队列、全屏提醒窗口选哪块显示器以及何时转为右下角弹窗。不创建窗口，便于 FlamingoUnit
 *  直接断言（Tests/MemoViewModelTests.cpp，用例 MEM-U1～MEM-U6）。
 *
 *  窗口本身在同目录的 BalloonMemoWindow / MemoEditDialog / MemoReminderPopup / MemoFullscreenWindow 里，
 *  它们只负责把这里算出来的结果画出来、把按钮事件交回这里。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <set>
#include <string>
#include <vector>
#include "../../Feature/Memo/MemoRule.h"
#include "../../Feature/Memo/MemoScheduler.h"
#include "../../net/MemoProtocolClient.h"

namespace balloonmemo {

// ---------------- 月历 ----------------

//月历的行数与列数：6 行 × 7 列，第一列是周一
const int kCalendarRows = 6;
const int kCalendarCols = 7;
const int kCalendarCells = kCalendarRows * kCalendarCols;

/**
 *  月历的一格。
 */
struct CalendarCell
{
    int64_t m_day;          //这一格的日子（从 1970-01-01 起的天数）
    int     m_dayOfMonth;   //几号，1～31
    bool    m_inMonth;      //是否属于当前显示的月份（不属于的是上个月末、下个月初，画成浅色）
    bool    m_isToday;      //是否是今天
    bool    m_isSelected;   //是否是选中的那天
    bool    m_hasEvent;     //这一天有没有备忘事件（画圆点）

    CalendarCell() : m_day(0), m_dayOfMonth(0), m_inMonth(false), m_isToday(false), m_isSelected(false), m_hasEvent(false) {}
};

/**
 *  排出某年某月的 42 格：第一格是该月 1 号所在那一周的周一（1 号恰好是周一时就是 1 号），之后逐日递增。
 *    year / month：当前显示的年月；today / selected：今天与选中的那天（天数）；
 *    eventDays：有事件的日子；out：[出参] 先清空再填入 kCalendarCells 格。
 */
void BuildMonthCells(int year, int month, int64_t today, int64_t selected, const std::set<int64_t>& eventDays,
                     std::vector<CalendarCell>& out);

/**
 *  月历一屏覆盖的日子范围 [firstDay, endDay)，取有事件的日子时用。
 */
void MonthGridRange(int year, int month, int64_t& firstDay, int64_t& endDay);

/**
 *  翻月：delta 为 -1 上个月、+1 下个月，跨年时年份随之变化。
 */
void ShiftMonth(int& year, int& month, int delta);

/**
 *  标题栏里的「yyyy 年 m 月」。
 */
std::wstring MonthTitle(int year, int month);

//月历年份下拉框列出今年往前、往后各几年（2026-10-04 与用户确认用年、月两个下拉框切换）
const int kCalendarYearsBack = 5;
const int kCalendarYearsAhead = 10;

//月历能切到的年份上下限：与编辑对话框年份微调框的上限、服务端接受的四位年份一致
const int kCalendarYearMin = 2000;
const int kCalendarYearMax = 2099;

/**
 *  月历年份下拉框列出的年份范围 [first, last]：今年往前 kCalendarYearsBack 年到往后 kCalendarYearsAhead 年；
 *  当前显示的年份不在其中时（用「‹」「›」翻出了这个范围）把它也包进来。结果收在
 *  [kCalendarYearMin, kCalendarYearMax] 之内。纯函数。
 *    thisYear：今年；shownYear：月历当前显示的年份；first / last：[出参] 第一项与最后一项的年份。
 */
void CalendarYearRange(int thisYear, int shownYear, int& first, int& last);

/**
 *  切到另一个年月时选中的那一天：保持原来选中日的「几号」，那个月没有这一天（如 31 号、2 月 29 号）时
 *  取当月最后一天。纯函数。
 *    year / month：切到的年月；oldDay：原来选中的日子（从 1970-01-01 起的天数）。
 *  @return 新的选中日（天数）
 */
int64_t KeepDayOfMonth(int year, int month, int64_t oldDay);

// ---------------- 编辑对话框 ----------------

//标题、备注的长度上限（按 UTF-16 字符计，与服务端按 Unicode 字符计在常用汉字上一致）
const size_t kMemoTitleMaxChars = 50;
const size_t kMemoNoteMaxChars = 200;

//提前分钟数与次数的上限
const int kMemoAdvanceMaxMinutes = 1440;
const int kMemoCountMax = 999;

//新建备忘时默认的钟点：选中的不是今天时取 9:00
const int kMemoDefaultHour = 9;

/**
 *  在月历上选中某天后点「添加备忘」时的默认值：开始时间在选中的那天；那天是今天时取下一个整点
 *  （已过 23 点则取 23:59），否则取 9:00；不重复、到点提醒、右下角弹窗。
 *    selectedDay：选中的日子；now：现在（本地分钟序号）；ctype / groupid：个人或某群。
 */
net::MemoItem DefaultMemoForDay(int64_t selectedDay, memo::LocalMinute now, int ctype, int64_t groupid);

//编辑对话框的校验结果
enum MemoEditError
{
    kMemoEditOk = 0,              //可以保存
    kMemoEditTitleEmpty,          //标题为空（或只有空白）
    kMemoEditTitleTooLong,        //标题超过 50 字
    kMemoEditNoteTooLong,         //备注超过 200 字
    kMemoEditStartInvalid,        //开始时间不是一个存在的日期时间
    kMemoEditAdvanceInvalid,      //提前分钟不在 0～1440
    kMemoEditWeekdaysEmpty,       //每周重复但一天都没选
    kMemoEditCountInvalid,        //次数不在 0～999
    kMemoEditTargetsEmpty,        //群备忘既没选「提醒全体」也没选任何成员
};

/**
 *  保存前的校验。
 *    title / note：界面上输入的文字（UTF-16）；memo：其余字段已填好的备忘（标题、备注不看）。
 */
MemoEditError ValidateMemoEdit(const std::wstring& title, const std::wstring& note, const net::MemoItem& memo);

/**
 *  校验结果对应的提示文字；kMemoEditOk 返回空串。
 */
std::wstring MemoEditErrorText(MemoEditError err);

//保存超时：发出 2402 之后 10 秒没有应答时交给 MemoFailureText 的结果码（服务端不会返回负数）
const int kMemoCodeTimeout = -1;

/**
 *  保存、删除失败时给用户看的原因。
 *    code：应答里的错误码（取值见 Protocol.h 的 error_code）；isDelete：是不是删除的应答。
 */
std::wstring MemoFailureText(int code, bool isDelete);

/**
 *  群里的这个角色能不能新建、修改、删除群备忘（群主、管理员能）。
 *    role：取值见 group_member_role，不是群成员传 -1。
 */
bool CanEditGroupMemo(int role);

// ---------------- 显示用的文字 ----------------

/**
 *  重复规则的说明，如「不重复」「每天」「每周一、三」「每月 31 日」「每年 2 月 29 日」，带次数时追加「，共 3 次」。
 */
std::wstring RepeatText(const net::MemoItem& memo);

/**
 *  提醒方式与提前量的说明，如「到点提醒」「提前 10 分钟」「提前 1 天」。
 */
std::wstring AdvanceText(int advanceMinutes);

/**
 *  事件时间的显示，如「10月5日 09:30」；与 now 同一天时只显示「今天 09:30」。
 */
std::wstring EventTimeText(memo::LocalMinute event, memo::LocalMinute now);

/**
 *  全屏提醒窗口里标题、时间、备注三行的文字。真实提醒与编辑对话框里的「预览」共用同一份拼法
 *  （2026-10-04 起），预览看到的就是到时真正弹出的样子。
 */
struct FullscreenReminderText
{
    std::wstring m_title;   //标题；去掉首尾空白后为空时是占位文字「（未填写标题）」
    std::wstring m_time;    //事件时间（EventTimeText），群备忘后面拼上「 · 群：群名」
    std::wstring m_note;    //备注，原样保留（多行照原样换行）
};

/**
 *  拼出全屏提醒窗口的文字。
 *    title：    标题（UTF-16）；
 *    note：     备注（UTF-16）；
 *    event：    这次事件的时间；now：当前时间（决定显示「今天」还是日期）；
 *    groupName：群备忘的群名；个人备忘传空串，时间后面不拼群名。
 */
FullscreenReminderText BuildFullscreenReminderText(const std::wstring& title, const std::wstring& note,
                                                   memo::LocalMinute event, memo::LocalMinute now,
                                                   const std::wstring& groupName);

/**
 *  编辑对话框「预览效果」用：开始时间是对话框里填写的十进制 yyyymmddhhmm（MemoItem::m_start 的格式），
 *  先换算成本地分钟序号，再按 BuildFullscreenReminderText 拼出文字。
 *    start：开始时间，十进制 yyyymmddhhmm；其余参数同 BuildFullscreenReminderText；
 *    out：  [出参] 拼好的文字，返回 false 时不改动。
 *  @return start 是一个存在的日期时间时返回 true；不存在（如 2 月 30 日）时返回 false，调用方应提示
 *          kMemoEditStartInvalid 而不是预览
 */
bool BuildPreviewReminderText(const std::wstring& title, const std::wstring& note, int64_t start,
                              memo::LocalMinute now, const std::wstring& groupName, FullscreenReminderText& out);

/**
 *  群备忘提醒谁：「提醒全体成员」或「提醒 3 人」。个人备忘返回空串。
 */
std::wstring TargetsText(const net::MemoItem& memo);

/**
 *  备注压成一行显示：UTF-8 转为 UTF-16，换行（CR、LF）换成空格。空备注返回空串。
 */
std::wstring NoteOneLine(const std::string& noteUtf8);

/**
 *  创建人一行：「张勇 · 10月3日 22:59 创建」，与 now 同一天时为「张勇 · 今天 22:59 创建」。
 *    name：创建人的显示名，调用方按通讯录或群成员表解析好；
 *    createdAt：创建时间（本地分钟序号），不大于 0 表示不知道，此时只写「张勇 创建」；
 *    now：现在（本地分钟序号）。
 */
std::wstring CreatorText(const std::wstring& name, memo::LocalMinute createdAt, memo::LocalMinute now);

// ---------------- 备忘卡片 ----------------

/**
 *  群「备忘」页签与本人备忘窗口里一张卡片要显示的内容（2026-10-03 与用户确认的四行卡片）：
 *  标题；备注（有才显示）；提醒时间或「已结束」；规则一行；创建人一行（只有群备忘有）。
 */
struct MemoCardText
{
    int64_t      m_memoid;     //备忘 ID
    std::wstring m_title;      //第一行：标题
    std::wstring m_note;       //备注压成的一行；空串时这一行不显示
    std::wstring m_when;       //提醒时间：群页签里是下一次事件「10月4日 09:30」，本人窗口里是当天的「09:30」
    bool         m_ended;      //没有将来的事件了（m_when 为「已结束」，画成灰色）
    std::wstring m_rule;       //规则一行：群页签「每周一 · 提醒 3 人」，本人窗口「每天 · 提前 10 分钟 · 群：XX」
    std::wstring m_creator;    //创建人一行；个人备忘为空串，这一行不显示
    bool         m_editable;   //悬停时是否显示「修改」「删除」：个人备忘总是可以，群备忘只有群主与管理员在群页签里可以
    memo::LocalMinute m_remindAt;  //群页签：m_when 那次事件的提醒时刻（事件时间减提前量，本地分钟序号）。到了这一分钟
                                   //m_when 就该换成下一次事件，窗口据此安排重画；已结束、本人窗口的卡片为 0

    MemoCardText() : m_memoid(0), m_ended(false), m_editable(false), m_remindAt(0) {}
};

/**
 *  群「备忘」页签里的一张卡片。
 *    memo：群备忘；now：现在（本地分钟序号）；
 *    creatorName：创建人显示名；createdAt：创建时间（本地分钟序号，不知道时传 0）；
 *    editable：本人能否修改、删除群备忘（群主、管理员能，见 CanEditGroupMemo）。
 *  提醒时间取提醒时刻晚于 now 的下一次事件；没有这样的事件时为「已结束」。
 */
MemoCardText BuildGroupTabCard(const net::MemoItem& memo, memo::LocalMinute now, const std::wstring& creatorName,
                               memo::LocalMinute createdAt, bool editable);

/**
 *  本人备忘窗口当天列表里的一张卡片。
 *    memo：备忘；event：这一天的这次事件（本地分钟序号）；
 *    groupName：群备忘所属群的名称（个人备忘不看）；creatorName / createdAt：群备忘的创建人与创建时间
 *    （个人备忘不看；createdAt 不知道时传 0）；now：现在（本地分钟序号）。
 *  个人备忘可以修改、删除；群备忘在这里只能查看，悬停时不显示按钮。
 */
MemoCardText BuildDayCard(const net::MemoItem& memo, memo::LocalMinute event, const std::wstring& groupName,
                          const std::wstring& creatorName, memo::LocalMinute createdAt, memo::LocalMinute now);

// ---------------- 右下角提醒弹窗 ----------------

/**
 *  右下角提醒弹窗里的一项。
 */
struct ReminderEntry
{
    int64_t           m_memoid;   //备忘 ID
    memo::LocalMinute m_event;    //这次事件的时间
    bool              m_missed;   //是不是错过的提醒

    ReminderEntry() : m_memoid(0), m_event(0), m_missed(false) {}
};

/**
 *  右下角提醒弹窗的内容队列：同时有多条时合并在一个窗口里逐条列出；同一条备忘的同一次事件只列一次；
 *  错过的提醒合并成一行「你有 N 条错过的提醒」，排在最后。点「知道了」移除一条、点「10 分钟后再提醒」
 *  移除并交给调度器；队列空了窗口就关。
 */
class CReminderQueue
{
public:
    /**
     *  加入调度器这次弹出的提醒。全屏方式的到点提醒不进队列（由全屏窗口显示），由调用方先挑出来。
     *  @return 加入后队列里新增了条目返回 true
     */
    bool Add(const std::vector<memo::MemoFire>& fires);

    /**
     *  加入一条（全屏窗口超时后转过来的那条也走这里）。
     */
    bool AddOne(int64_t memoid, memo::LocalMinute event, bool missed);

    /**
     *  点了某条的「知道了」。
     *  @return 确实移除了一条返回 true
     */
    bool Dismiss(int64_t memoid);

    /**
     *  点了「错过的提醒」那一行的「知道了」：全部错过的一起移除。
     */
    void DismissMissed();

    /**
     *  点了某条的「10 分钟后再提醒」：从队列移除，返回这次事件的时间交给调度器。
     *  @return 队列里有这条返回 true
     */
    bool TakeForSnooze(int64_t memoid, memo::LocalMinute& outEvent);

    /**
     *  某条备忘被删除：从队列里拿掉（错过的也拿掉）。
     */
    void RemoveMemo(int64_t memoid);

    /**
     *  到点的提醒（不含错过的），按加入顺序。
     */
    std::vector<ReminderEntry> DueEntries() const;

    /**
     *  错过的提醒，按加入顺序。
     */
    std::vector<ReminderEntry> MissedEntries() const;

    bool IsEmpty() const;
    void Clear();

private:
    std::vector<ReminderEntry> m_entries;   //当前列出的提醒，按加入顺序
};

// ---------------- 全屏提醒 ----------------

//全屏提醒窗口无人操作多久后收起、转为右下角弹窗，毫秒（与用户确认的 60 秒）
const DWORD kFullscreenFallbackMs = 60000;

/**
 *  全屏提醒窗口要盖住的显示器：主面板可见（没有收在托盘、没有最小化）时取主面板所在的显示器，
 *  否则取主显示器。纯函数：调用方把两块显示器的矩形算好传进来。
 *    panelVisible：主面板是否可见；panelMonitor：主面板所在显示器的整块矩形；primaryMonitor：主显示器的矩形。
 */
RECT ChooseFullscreenMonitor(bool panelVisible, const RECT& panelMonitor, const RECT& primaryMonitor);

/**
 *  全屏提醒窗口什么时候转为右下角弹窗：显示后 kFullscreenFallbackMs 毫秒内没有任何操作。
 *  鼠标移动、按键都算操作，重新计时。
 */
class CFullscreenTimeout
{
public:
    CFullscreenTimeout();

    //窗口显示时调用
    void Start(DWORD nowMs);

    //有操作时调用
    void OnUserActivity(DWORD nowMs);

    //该不该收起转为右下角弹窗；未 Start 时恒为 false
    bool ShouldFallback(DWORD nowMs) const;

    //离该收起还有多少毫秒；已经该收起时返回 0
    DWORD RemainingMs(DWORD nowMs) const;

private:
    bool  m_started;       //是否已经开始计时
    DWORD m_lastActivity;  //最后一次操作（或显示）的时刻，GetTickCount 毫秒
};

} // namespace balloonmemo
