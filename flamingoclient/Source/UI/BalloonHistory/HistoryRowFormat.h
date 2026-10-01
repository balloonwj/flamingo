/**
 *  消息记录列表行的显示格式，HistoryRowFormat.h
 *
 *  三件事：行内时间文字、按本地日历天分组用的日期键、按发送者取头像底色。消息记录窗
 *  （CBalloonHistoryWindow）与合并记录查看窗（CMergedRecordWindow）的列表行都用它们，
 *  两个窗口里同一个人、同一时刻的显示因此一致。原先这三个函数是消息记录窗的文件内函数，
 *  做查看窗时移到这里共用。
 *
 *  典型用法：
 *      row.m_timeText    = balloonhistory::FormatTimeShort(timestampMs);
 *      row.m_avatarColor = balloonhistory::ColorForId(fromid);
 *      int64_t day       = balloonhistory::LocalDayKey(timestampMs);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL（CString）。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <stdint.h>

namespace balloonhistory {

/**
 *  把消息时间格式化成列表行里显示的"HH:MM"（本地时区），日期交给行上方的日期分组头。
 *  导出的文本脱离界面单独阅读、必须带完整日期，不要用本函数。
 *  @param timestampMs Unix 毫秒时间
 *  @return 形如 "09:05" 的文字；时间换算失败时返回空串
 */
CString FormatTimeShort(int64_t timestampMs);

/**
 *  把消息时间换算成"本地时区下的哪一天"，形如 20260804，供日期分组与折叠判断两条消息
 *  是否同一天。必须按本地日历天算：直接拿毫秒数除以一天，在时区偏移不是整小时的地区
 *  （如 +5:30）会切错位置。
 *  @param timestampMs Unix 毫秒时间
 *  @return 年 * 10000 + 月 * 100 + 日；时间换算失败时返回 0
 */
int64_t LocalDayKey(int64_t timestampMs);

/**
 *  由发送者 userid 取一个稳定的头像底色（小调色板取模），同一个人每次取到同一种颜色。
 *  @param id 发送者 userid，可为任意整数（负数也按取模落到调色板内）
 *  @return 头像底色
 */
COLORREF ColorForId(int64_t id);

} // namespace balloonhistory
