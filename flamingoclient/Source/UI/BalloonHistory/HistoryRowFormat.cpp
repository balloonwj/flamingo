/**
 *  消息记录列表行的显示格式，见 HistoryRowFormat.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

// ---- ATL 头文件要求的前置依赖，与同目录 HistoryWindowLayout.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <tchar.h>
#include <time.h>

#include "HistoryRowFormat.h"

namespace balloonhistory {

namespace {

//一秒的毫秒数：消息时间是 Unix 毫秒，换算本地时间要先折成秒
const int64_t kMsPerSecond = 1000;
//LocalDayKey 拼日期键用的位权：年 * 10000 + 月 * 100 + 日
const int64_t kDayKeyYearWeight  = 10000;
const int64_t kDayKeyMonthWeight = 100;
//struct tm 的 tm_year 从 1900 年起算
const int kTmYearBase = 1900;
//"HH:MM" 文字的缓冲区长度（含结尾的 0），留足余量
const int kTimeTextBufLen = 16;

} // namespace

CString FormatTimeShort(int64_t timestampMs)
{
    __time64_t t = (__time64_t)(timestampMs / kMsPerSecond);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return CString();
    }
    TCHAR buf[kTimeTextBufLen];
    _stprintf_s(buf, _countof(buf), _T("%02d:%02d"), tmv.tm_hour, tmv.tm_min);
    return CString(buf);
}

int64_t LocalDayKey(int64_t timestampMs)
{
    __time64_t t = (__time64_t)(timestampMs / kMsPerSecond);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return 0;
    }
    return (int64_t)(tmv.tm_year + kTmYearBase) * kDayKeyYearWeight
         + (int64_t)(tmv.tm_mon + 1) * kDayKeyMonthWeight
         + (int64_t)tmv.tm_mday;
}

COLORREF ColorForId(int64_t id)
{
    //六种底色，按 userid 取模分配
    static const COLORREF kPalette[] =
    {
        RGB(0xF2, 0x6B, 0x3E), RGB(0xF2, 0x5C, 0x54), RGB(0x2F, 0xA4, 0x7A),
        RGB(0x4F, 0x6C, 0xDF), RGB(0x8A, 0x5C, 0xF2), RGB(0xE0, 0x8A, 0x2E),
    };
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    //先取模再加 n 再取模：负数 id 也落在 [0, n) 内
    const int idx = (int)(((id % n) + n) % n);
    return kPalette[idx];
}

} // namespace balloonhistory
