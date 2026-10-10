/**
 *  消息记录列表行的显示格式，HistoryRowFormat.h
 *
 *  三件事：行内时间文字、按本地日历天分组用的日期键、按发送者取头像底色（另有系统提示行的
 *  发送人显示名与头像底色，见 HistorySenderName / HistoryAvatarColor）。消息记录窗
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

//系统提示行的头像底色：中性灰，与各发送人的彩色头像区分开
const COLORREF kHistorySystemAvatarColor = RGB(0xA6, 0xA6, 0xA6);

/**
 *  消息记录窗与导出里一行的发送人显示名。系统提示（msgtype = 17：入群、退群、转让群主、接龙的参与 /
 *  结束、屏幕共享结束等）一律写「系统消息」：群里的系统提示发送方为 0，按 userid 解析会得到「用户0」；
 *  单聊的屏幕共享结束提示记在发起人名下，但它并不是发起人说的话。其余消息原样
 *  返回调用方解析好的名字。头像字形取名字的首字，所以系统提示行的头像是「系」。
 *  @param msgtype      消息类型，取值见 enum message_type
 *  @param resolvedName 按发送方 userid 解析出的显示名
 *  @return 显示名
 */
CString HistorySenderName(int msgtype, const CString& resolvedName);

/**
 *  消息记录窗里一行的头像底色：系统提示用 kHistorySystemAvatarColor，其余同 ColorForId。
 *  @param msgtype 消息类型，取值见 enum message_type
 *  @param fromid  发送方 userid
 *  @return 头像底色
 */
COLORREF HistoryAvatarColor(int msgtype, int64_t fromid);

} // namespace balloonhistory
