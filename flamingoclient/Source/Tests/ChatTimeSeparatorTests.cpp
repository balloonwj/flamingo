/**
 *  聊天「时间分隔行」判定与文案的单元测试（Suite CTS），ChatTimeSeparatorTests.cpp
 *
 *  覆盖：
 *    CTS1  没有上一条时插（消息流开头总该有个时间）；
 *    CTS2  间隔不足阈值不插、正好满阈值就插（边界用 >=）；
 *    CTS3  时间未知（<= 0）不插；
 *    CTS4  时间倒挂时插 —— 服务器时间回拨或乱序时该让用户看见；
 *    CTS5  今天只给 HH:MM；
 *    CTS6  昨天给「昨天 HH:MM」，且跨月同样成立；
 *    CTS7  今年更早给 MM-DD HH:MM，往年带年份；
 *    CTS8  时间戳非法或当前时刻取不到时的退化行为。
 *
 *  时间戳一律用本地时间经 mktime 构造，使被测代码与用例处在同一时区，结果不随机器
 *  时区变化。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonChatWnd/ChatTimeSeparator.h"

#include <time.h>

using balloonchatwnd::FormatChatSeparatorTime;
using balloonchatwnd::kChatTimeSeparatorThresholdMs;
using balloonchatwnd::ShouldInsertTimeSeparator;

namespace {

//按本地时间构造一个 Unix 毫秒时间戳。
long long LocalMs(int year, int mon, int day, int hour, int minute)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = year - 1900;
    t.tm_mon   = mon - 1;
    t.tm_mday  = day;
    t.tm_hour  = hour;
    t.tm_min   = minute;
    t.tm_isdst = -1;            //由运行库按本地时区判断夏令时
    const time_t secs = mktime(&t);
    return (long long)secs * 1000LL;
}

} // namespace

TEST_CASE("CTS1 没有上一条时插一行", "[chattime]")
{
    const long long cur = LocalMs(2026, 8, 12, 10, 0);
    CHECK(ShouldInsertTimeSeparator(0, cur, kChatTimeSeparatorThresholdMs));
}

TEST_CASE("CTS2 间隔不足不插、正好满阈值就插", "[chattime]")
{
    const long long prev = LocalMs(2026, 8, 12, 10, 0);

    //差一毫秒不到阈值：不插。连续对话时就是这种情形，一行都不该冒出来。
    CHECK_FALSE(ShouldInsertTimeSeparator(prev, prev + kChatTimeSeparatorThresholdMs - 1,
                                          kChatTimeSeparatorThresholdMs));

    //正好满：插。边界用 >=，不必多等一毫秒。
    CHECK(ShouldInsertTimeSeparator(prev, prev + kChatTimeSeparatorThresholdMs,
                                    kChatTimeSeparatorThresholdMs));
}

TEST_CASE("CTS3 本条时间未知时不插", "[chattime]")
{
    const long long prev = LocalMs(2026, 8, 12, 10, 0);
    //插了也写不出文案。
    CHECK_FALSE(ShouldInsertTimeSeparator(prev, 0, kChatTimeSeparatorThresholdMs));
    CHECK_FALSE(ShouldInsertTimeSeparator(0, 0, kChatTimeSeparatorThresholdMs));
}

TEST_CASE("CTS4 时间倒挂时插", "[chattime]")
{
    const long long prev = LocalMs(2026, 8, 12, 10, 0);
    const long long cur  = LocalMs(2026, 8, 12, 9, 0);   //比上一条还早
    //服务器时间回拨或消息乱序时会出现。时间倒着走本身就该让用户看见。
    CHECK(ShouldInsertTimeSeparator(prev, cur, kChatTimeSeparatorThresholdMs));
}

TEST_CASE("CTS5 今天只显示时分", "[chattime]")
{
    const long long now = LocalMs(2026, 8, 12, 15, 30);
    CHECK(FormatChatSeparatorTime(LocalMs(2026, 8, 12, 9, 5), now) == CString(_T("09:05")));
    //同一天的凌晨与深夜都算今天。
    CHECK(FormatChatSeparatorTime(LocalMs(2026, 8, 12, 0, 0), now) == CString(_T("00:00")));
    CHECK(FormatChatSeparatorTime(LocalMs(2026, 8, 12, 23, 59), now) == CString(_T("23:59")));
}

TEST_CASE("CTS6 昨天带前缀，跨月同样成立", "[chattime]")
{
    const long long now = LocalMs(2026, 8, 12, 15, 30);
    CHECK(FormatChatSeparatorTime(LocalMs(2026, 8, 11, 22, 10), now)
          == CString(_T("昨天 22:10")));

    //月初的"昨天"落在上个月：把当前时刻往前推一天再比同一天，边界自动成立。
    const long long nowFirstOfMonth = LocalMs(2026, 9, 1, 8, 0);
    CHECK(FormatChatSeparatorTime(LocalMs(2026, 8, 31, 20, 0), nowFirstOfMonth)
          == CString(_T("昨天 20:00")));
}

TEST_CASE("CTS7 今年更早给月日、往年带年份", "[chattime]")
{
    const long long now = LocalMs(2026, 8, 12, 15, 30);

    CHECK(FormatChatSeparatorTime(LocalMs(2026, 8, 10, 14, 3), now)
          == CString(_T("08-10 14:03")));
    CHECK(FormatChatSeparatorTime(LocalMs(2026, 1, 1, 0, 1), now)
          == CString(_T("01-01 00:01")));

    //跨年之后光看月日分不清是哪一年的，必须带年份。
    CHECK(FormatChatSeparatorTime(LocalMs(2025, 12, 31, 23, 50), now)
          == CString(_T("2025-12-31 23:50")));
}

TEST_CASE("CTS8 非法时间戳与当前时刻取不到时的退化行为", "[chattime]")
{
    const long long now = LocalMs(2026, 8, 12, 15, 30);

    //消息时间非法：返回空串，调用方据此不插这一行。
    CHECK(FormatChatSeparatorTime(0, now) == CString(_T("")));
    CHECK(FormatChatSeparatorTime(-1, now) == CString(_T("")));

    //当前时刻取不到时退化成带年份的完整写法：宁可多写一截，也不要显示成一个孤零零的
    //时分让人误以为是今天的。
    CHECK(FormatChatSeparatorTime(LocalMs(2026, 8, 12, 9, 5), 0)
          == CString(_T("2026-08-12 09:05")));
}
