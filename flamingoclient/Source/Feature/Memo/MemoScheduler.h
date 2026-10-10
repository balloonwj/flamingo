/**
 *  MemoScheduler.h
 *
 *  「备忘与提醒」的提醒调度（纯逻辑）：给定全部备忘与「现在」，算出此刻该弹出哪些提醒、哪些归入
 *  「错过的提醒」，以及下一次该在什么时候再检查。不碰窗口与定时器，由主面板按返回的时间设一次性定时器、
 *  按返回的结果弹窗（docs/备忘与提醒方案.md 第 4 节）。便于 FlamingoUnit 直接断言（Tests/MemoSchedulerTests.cpp）。
 *
 *  规则：
 *    · 每台电脑为每条备忘记下「已提醒过的最后一次事件」（FiredRecords，由调用方存进本人配置），
 *      事件时间不晚于这条记录的不再提醒，重启、改系统时间、休眠唤醒后都不会重复弹出；
 *    · 检查时只看每条备忘「提醒时间 ≤ 现在」的最后一次事件：提醒时间在最近 kMemoDueWindowMinutes 分钟内的
 *      按备忘设定的方式提醒；更早、但在 kMemoMissedWindowMinutes 分钟内的归入「错过的提醒」；再早的不再提示；
 *    · 提醒时间早于备忘最后修改时间的那些事件不提醒：新建一条开始时间已过的备忘、或把时间改到过去时不会立刻弹出；
 *    · 「稍后提醒」在 Snooze 指定的时刻再提醒一次，只保存在内存里，退出客户端后不保留。
 *
 *  典型用法：
 *      memo::CMemoScheduler sched;           //主面板成员
 *      sched.LoadFiredRecord(id, event);     //登录后从配置读回
 *      std::vector<memo::MemoFire> fires;
 *      if (sched.Check(inputs, now, fires))  //记录有变化时存回配置
 *          ...SaveFiredRecords(sched.FiredRecords())...
 *      memo::LocalMinute wake = 0;
 *      if (sched.NextWake(inputs, now, wake)) ...定时器设到 wake...
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <map>
#include <string>
#include <vector>
#include "MemoRule.h"

namespace memo {

//提醒时间在最近这么多分钟内的按备忘设定的方式提醒；主面板最长 60 秒检查一次，留出余量
const int64_t kMemoDueWindowMinutes = 5;

//更早、但在这么多分钟（24 小时）内的归入「错过的提醒」
const int64_t kMemoMissedWindowMinutes = 1440;

//「稍后提醒」推迟的分钟数（与用户确认的「10 分钟后再提醒」）
const int64_t kMemoSnoozeMinutes = 10;

/**
 *  调度要用到的一条备忘。
 */
struct MemoScheduleInput
{
    int64_t      m_memoid;      //备忘 ID
    MemoRuleSpec m_rule;        //提醒规则
    LocalMinute  m_updatedAt;   //最后修改时间，换算成本地分钟序号；提醒时间早于它的事件不提醒

    MemoScheduleInput() : m_memoid(0), m_updatedAt(0) {}
};

//一次提醒的类别
enum MemoFireKind
{
    kMemoFireDue    = 1,   //到点（或提前 N 分钟）的提醒，按备忘设定的方式弹出
    kMemoFireMissed = 2,   //错过的提醒（客户端没开、没登录、电脑休眠期间到期），合并成一条右下角弹窗
};

/**
 *  一次要弹出的提醒。
 */
struct MemoFire
{
    int64_t     m_memoid;    //备忘 ID
    LocalMinute m_event;     //事件时间（未减提前分钟），弹窗里显示的是它
    LocalMinute m_remindAt;  //提醒时间（事件时间减去提前分钟）
    int         m_kind;      //类别，取值见 MemoFireKind
    bool        m_snoozed;   //是不是「稍后提醒」到期的那一次

    MemoFire() : m_memoid(0), m_event(0), m_remindAt(0), m_kind(kMemoFireDue), m_snoozed(false) {}
};

/**
 *  提醒调度器。只保存「已提醒记录」与「稍后提醒」两份状态，备忘本身每次由调用方传入。
 */
class CMemoScheduler
{
public:
    CMemoScheduler();

    /**
     *  载入一条已提醒记录（登录后从本人配置读回时逐条调用）。
     *    memoid：备忘 ID；event：已提醒过的最后一次事件时间（本地分钟序号）。
     */
    void LoadFiredRecord(int64_t memoid, LocalMinute event);

    /**
     *  全部已提醒记录（备忘 ID → 已提醒过的最后一次事件时间），调用方据此存回配置。
     */
    const std::map<int64_t, LocalMinute>& FiredRecords() const;

    /**
     *  清空全部状态（已提醒记录与稍后提醒）。切换账号、注销时调用。
     */
    void Reset();

    /**
     *  检查此刻该弹出的提醒。
     *    memos：全部备忘；now：现在（本地分钟序号）；
     *    out：  [出参] 先清空，再按备忘顺序填入要弹出的提醒，稍后提醒到期的排在最后。
     *  @return 已提醒记录有变化时返回 true，调用方应存回配置
     *  副作用：被弹出或因过旧而不再提示的事件写进已提醒记录；到期的稍后提醒被移除。
     */
    bool Check(const std::vector<MemoScheduleInput>& memos, LocalMinute now, std::vector<MemoFire>& out);

    /**
     *  下一次需要检查的时刻：各备忘下一次提醒时间与各稍后提醒中最早的那个。
     *    out：[出参] 晚于 now 的本地分钟序号。
     *  @return 没有任何将来的提醒时返回 false
     */
    bool NextWake(const std::vector<MemoScheduleInput>& memos, LocalMinute now, LocalMinute& out) const;

    /**
     *  「稍后提醒」：在 until 时刻再提醒一次这条备忘的这次事件。同一条备忘只保留最后一次设置。
     *    memoid：备忘 ID；event：这次事件的时间；until：再次提醒的时刻（本地分钟序号）。
     */
    void Snooze(int64_t memoid, LocalMinute event, LocalMinute until);

    /**
     *  这条备忘有没有尚未到期的稍后提醒。
     */
    bool HasSnooze(int64_t memoid) const;

    /**
     *  丢掉已不存在的备忘的已提醒记录与稍后提醒（全量拉取备忘之后、或删除备忘之后调用）。
     *  @return 已提醒记录有变化时返回 true
     */
    bool Prune(const std::vector<MemoScheduleInput>& memos);

    /**
     *  丢掉一条备忘的稍后提醒（备忘被修改或删除时调用：修改后按新规则重新计算）。
     */
    void CancelSnooze(int64_t memoid);

private:
    //一条稍后提醒
    struct SnoozeEntry
    {
        LocalMinute m_event;   //这次事件的时间
        LocalMinute m_until;   //再次提醒的时刻
    };

    std::map<int64_t, LocalMinute> m_fired;    //已提醒记录：备忘 ID → 已提醒过的最后一次事件时间；随本人配置持久化
    std::map<int64_t, SnoozeEntry> m_snoozes;  //稍后提醒：备忘 ID → 再次提醒的时刻；只在本次运行内有效
};

/**
 *  把已提醒记录写成文本：每行一条「备忘ID=事件时间（本地分钟序号）」，按备忘 ID 升序。
 */
std::string EncodeFiredRecords(const std::map<int64_t, LocalMinute>& records);

/**
 *  从 EncodeFiredRecords 写出的文本读回已提醒记录。格式不对的行跳过。
 *    out：[出参] 先清空再填入。
 */
void DecodeFiredRecords(const std::string& text, std::map<int64_t, LocalMinute>& out);

} // namespace memo
