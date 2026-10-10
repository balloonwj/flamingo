/**
 *  MemoClock.h
 *
 *  「备忘与提醒」与系统时钟打交道的几个函数：读本地当前时间、把服务端的 Unix 毫秒换算成本地分钟序号、
 *  算出离某个本地分钟还有多少毫秒（主面板据此设一次性定时器）。其余计算都在 MemoRule / MemoScheduler 里，
 *  那两处不读系统时钟。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include "MemoRule.h"

namespace memo {

/**
 *  现在的本地时间，精确到分钟（秒数舍去）。
 */
LocalMinute NowLocalMinute();

/**
 *  Unix 毫秒换算成本地分钟序号（按本机时区）。unixMs 不大于 0 时返回 0。
 */
LocalMinute LocalMinuteFromUnixMs(int64_t unixMs);

/**
 *  从现在到本地时间 target 这一分钟开始还有多少毫秒；target 不晚于现在时返回 0。
 */
int64_t MillisUntilLocalMinute(LocalMinute target);

} // namespace memo
