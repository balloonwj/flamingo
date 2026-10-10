/**
 *  帖子、留言、补充的时间显示规则（纯函数）：把 Unix 毫秒时间按「刚刚 / N 分钟前 / 今天 14:20 / 昨天 14:20 /
 *  10-06 14:20 / 2025-10-06」分档。这里只做分档与取出年月日时分，文字由界面按译文拼（TRF）。
 *
 *  本地时间按「UTC 时间 + 本地偏移」换算，偏移由调用方传入：界面用 LocalOffsetMs() 取当前时区（含夏令时）的偏移，
 *  单元测试传固定值，结果与运行测试的电脑所在时区无关。
 *
 *  典型用法：
 *      threadgroup::TimeParts t = threadgroup::ClassifyTime(post.m_createTime, NowMs(), threadgroup::LocalOffsetMs());
 *      switch (t.m_kind) { case threadgroup::time_today: ... }
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>

namespace threadgroup {

//时间显示的分档
enum TimeKind
{
    time_just_now    = 1,   //1 分钟以内（含将来的时间：本机时钟比服务端慢时）
    time_minutes_ago = 2,   //1 小时以内：「N 分钟前」
    time_today       = 3,   //今天更早：「今天 14:20」
    time_yesterday   = 4,   //昨天：「昨天 14:20」
    time_this_year   = 5,   //今年更早：「10-06 14:20」
    time_older       = 6,   //往年：「2025-10-06」
};

//分档结果：本地时间的年月日时分与「N 分钟前」的 N
struct TimeParts
{
    int m_kind;      //分档，取值见 TimeKind
    int m_minutes;   //time_minutes_ago 时的分钟数（1~59）
    int m_year;      //本地年
    int m_month;     //本地月（1~12）
    int m_day;       //本地日（1~31）
    int m_hour;      //本地时（0~23）
    int m_minute;    //本地分（0~59）

    TimeParts()
        : m_kind(time_just_now)
        , m_minutes(0)
        , m_year(0)
        , m_month(0)
        , m_day(0)
        , m_hour(0)
        , m_minute(0)
    {
    }
};

/**
 *  给一个时间分档。
 *  @param timeMs        要显示的时间，Unix 毫秒（UTC）
 *  @param nowMs         当前时间，Unix 毫秒（UTC）
 *  @param localOffsetMs 本地时间相对 UTC 的偏移（毫秒，东八区为 8 小时）
 */
TimeParts ClassifyTime(int64_t timeMs, int64_t nowMs, int64_t localOffsetMs);

//当前时间，Unix 毫秒（UTC）
int64_t NowMs();

//本机当前时区（含夏令时）相对 UTC 的偏移，毫秒
int64_t LocalOffsetMs();

} // namespace threadgroup
