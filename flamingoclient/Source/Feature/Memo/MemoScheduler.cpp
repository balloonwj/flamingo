/**
 *  MemoScheduler.cpp
 *
 *  提醒调度器的实现，规则见 MemoScheduler.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 AutoReplyPolicy.cpp 同例。
#include "MemoScheduler.h"
#include <stdlib.h>
#include <sstream>

namespace memo {

//在备忘列表里按 ID 找一条；找不到返回 NULL
static const MemoScheduleInput* FindInput(const std::vector<MemoScheduleInput>& memos, int64_t memoid)
{
    for (size_t i = 0; i < memos.size(); ++i)
    {
        if (memos[i].m_memoid == memoid)
            return &memos[i];
    }
    return NULL;
}

CMemoScheduler::CMemoScheduler()
{
}

void CMemoScheduler::LoadFiredRecord(int64_t memoid, LocalMinute event)
{
    m_fired[memoid] = event;
}

const std::map<int64_t, LocalMinute>& CMemoScheduler::FiredRecords() const
{
    return m_fired;
}

void CMemoScheduler::Reset()
{
    m_fired.clear();
    m_snoozes.clear();
}

bool CMemoScheduler::Check(const std::vector<MemoScheduleInput>& memos, LocalMinute now, std::vector<MemoFire>& out)
{
    out.clear();
    bool changed = false;

    for (size_t i = 0; i < memos.size(); ++i)
    {
        const MemoScheduleInput& memo = memos[i];
        //提醒时间 ≤ 现在，即事件时间 ≤ 现在 + 提前分钟
        MemoOccurrence occ;
        if (!FindEventAtOrBefore(memo.m_rule, now + memo.m_rule.m_advance, occ))
            continue;

        std::map<int64_t, LocalMinute>::const_iterator it = m_fired.find(memo.m_memoid);
        if (it != m_fired.end() && occ.m_event <= it->second)
            continue;

        const LocalMinute remindAt = occ.m_event - memo.m_rule.m_advance;
        //提醒时间早于最后修改时间：这次事件在保存时就已经过去了，不提醒，也不记录，
        //这样把时间改回将来之后，将来的事件照常提醒
        if (remindAt < memo.m_updatedAt)
            continue;

        const LocalMinute late = now - remindAt;
        if (late <= kMemoDueWindowMinutes)
        {
            MemoFire fire;
            fire.m_memoid = memo.m_memoid;
            fire.m_event = occ.m_event;
            fire.m_remindAt = remindAt;
            fire.m_kind = kMemoFireDue;
            out.push_back(fire);
            //新的一次事件到了，之前那次的稍后提醒不再需要
            m_snoozes.erase(memo.m_memoid);
        }
        else if (late <= kMemoMissedWindowMinutes)
        {
            MemoFire fire;
            fire.m_memoid = memo.m_memoid;
            fire.m_event = occ.m_event;
            fire.m_remindAt = remindAt;
            fire.m_kind = kMemoFireMissed;
            out.push_back(fire);
        }
        //更早的不再提示，同样记下，免得每次检查都重新算一遍
        m_fired[memo.m_memoid] = occ.m_event;
        changed = true;
    }

    std::map<int64_t, SnoozeEntry>::iterator sit = m_snoozes.begin();
    while (sit != m_snoozes.end())
    {
        if (sit->second.m_until > now)
        {
            ++sit;
            continue;
        }
        const MemoScheduleInput* memo = FindInput(memos, sit->first);
        if (memo != NULL)
        {
            MemoFire fire;
            fire.m_memoid = sit->first;
            fire.m_event = sit->second.m_event;
            fire.m_remindAt = sit->second.m_until;
            fire.m_kind = kMemoFireDue;
            fire.m_snoozed = true;
            out.push_back(fire);
        }
        m_snoozes.erase(sit++);
    }
    return changed;
}

bool CMemoScheduler::NextWake(const std::vector<MemoScheduleInput>& memos, LocalMinute now, LocalMinute& out) const
{
    bool found = false;
    LocalMinute best = 0;
    for (size_t i = 0; i < memos.size(); ++i)
    {
        const MemoScheduleInput& memo = memos[i];
        //提醒时间 > 现在，即事件时间 > 现在 + 提前分钟
        MemoOccurrence occ;
        if (!FindEventAtOrAfter(memo.m_rule, now + memo.m_rule.m_advance + 1, occ))
            continue;
        const LocalMinute remindAt = occ.m_event - memo.m_rule.m_advance;
        if (!found || remindAt < best)
        {
            best = remindAt;
            found = true;
        }
    }
    for (std::map<int64_t, SnoozeEntry>::const_iterator it = m_snoozes.begin(); it != m_snoozes.end(); ++it)
    {
        if (it->second.m_until <= now)
            continue;
        if (!found || it->second.m_until < best)
        {
            best = it->second.m_until;
            found = true;
        }
    }
    if (found)
        out = best;
    return found;
}

void CMemoScheduler::Snooze(int64_t memoid, LocalMinute event, LocalMinute until)
{
    SnoozeEntry entry;
    entry.m_event = event;
    entry.m_until = until;
    m_snoozes[memoid] = entry;
}

bool CMemoScheduler::HasSnooze(int64_t memoid) const
{
    return m_snoozes.find(memoid) != m_snoozes.end();
}

bool CMemoScheduler::Prune(const std::vector<MemoScheduleInput>& memos)
{
    bool changed = false;
    std::map<int64_t, LocalMinute>::iterator it = m_fired.begin();
    while (it != m_fired.end())
    {
        if (FindInput(memos, it->first) == NULL)
        {
            m_fired.erase(it++);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    std::map<int64_t, SnoozeEntry>::iterator sit = m_snoozes.begin();
    while (sit != m_snoozes.end())
    {
        if (FindInput(memos, sit->first) == NULL)
            m_snoozes.erase(sit++);
        else
            ++sit;
    }
    return changed;
}

void CMemoScheduler::CancelSnooze(int64_t memoid)
{
    m_snoozes.erase(memoid);
}

//已提醒记录文本里的数字按十进制书写
const int kDecimalRadix = 10;

std::string EncodeFiredRecords(const std::map<int64_t, LocalMinute>& records)
{
    std::ostringstream os;
    for (std::map<int64_t, LocalMinute>::const_iterator it = records.begin(); it != records.end(); ++it)
        os << it->first << '=' << it->second << '\n';
    return os.str();
}

void DecodeFiredRecords(const std::string& text, std::map<int64_t, LocalMinute>& out)
{
    out.clear();
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= line.size())
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key.find_first_not_of("0123456789") != std::string::npos
            || value.find_first_not_of("0123456789") != std::string::npos)
            continue;
        const int64_t memoid = static_cast<int64_t>(_strtoi64(key.c_str(), NULL, kDecimalRadix));
        const LocalMinute event = static_cast<LocalMinute>(_strtoi64(value.c_str(), NULL, kDecimalRadix));
        if (memoid > 0)
            out[memoid] = event;
    }
}

} // namespace memo
