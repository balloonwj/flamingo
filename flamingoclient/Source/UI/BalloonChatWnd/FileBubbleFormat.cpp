/**
 *  FileBubbleFormat.cpp —— 聊天文件气泡显示格式化助手实现。
 *  balloonwj@qq.com   2026-06-05
 */
#include "FileBubbleFormat.h"

#include <cstdio>     // swprintf_s
#include <ctime>      // time_t / localtime_s / mktime（相对时间格式化）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

std::wstring FormatFileSize(long long bytes)
{
    if (bytes < 0)
        bytes = 0;

    const long long kKB = 1024LL;
    const long long kMB = 1024LL * 1024;
    const long long kGB = 1024LL * 1024 * 1024;

    wchar_t buf[32] = { 0 };

    //不足 1 KiB：直接按整数字节显示。
    if (bytes < kKB)
    {
        swprintf_s(buf, L"%lld B", bytes);
        return std::wstring(buf);
    }

    //选定单位与后缀，再保留一位小数（四舍五入）。
    long long       unit   = kKB;
    const wchar_t*  suffix = L"KB";
    if (bytes >= kGB)
    {
        unit   = kGB;
        suffix = L"GB";
    }
    else if (bytes >= kMB)
    {
        unit   = kMB;
        suffix = L"MB";
    }

    //四舍五入到一位小数：tenths = round(bytes / unit * 10) = (bytes*10 + unit/2) / unit。
    //聊天文件不限大小，但受磁盘容量所限 bytes 不会接近 int64 上限的十分之一
    //（约 922 PB），故 *10 不会溢出。
    long long tenths = (bytes * 10 + unit / 2) / unit;
    long long whole  = tenths / 10;
    long long frac   = tenths % 10;
    swprintf_s(buf, L"%lld.%lld %s", whole, frac, suffix);
    return std::wstring(buf);
}

std::wstring FileTypeLabelFromName(const std::wstring& fileName)
{
    size_t dot = fileName.find_last_of(L'.');
    //无 '.'、'.' 在首位（如 ".gitignore" 视为无扩展名）、'.' 在末尾：无类型标签。
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= fileName.size())
        return std::wstring();

    std::wstring ext;
    for (size_t i = dot + 1; i < fileName.size(); ++i)
    {
        wchar_t c = fileName[i];
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c - L'A' + L'a');
        //扩展名按 ASCII 处理，异常字符直接丢弃。
        if (c < 128)
            ext.push_back(c);
    }
    return ext;
}

std::wstring FormatRelativeDayTime(long long uploadMs, long long nowMs)
{
    //非正时间戳无意义，返回空串（调用方据此回退不显示时间段）。
    if (uploadMs <= 0)
        return std::wstring();

    time_t up  = static_cast<time_t>(uploadMs / 1000);
    time_t now = static_cast<time_t>(nowMs / 1000);

    struct tm tmUp;
    struct tm tmNow;
    //localtime_s 失败（极端越界时间）→ 回退空串，避免读未初始化的 tm。
    if (localtime_s(&tmUp, &up) != 0 || localtime_s(&tmNow, &now) != 0)
        return std::wstring();

    wchar_t buf[40] = { 0 };

    //同一天：显示"今天 HH:MM"。
    if (tmUp.tm_year == tmNow.tm_year && tmUp.tm_yday == tmNow.tm_yday)
    {
        swprintf_s(buf, TR(L"今天 %02d:%02d"), tmUp.tm_hour, tmUp.tm_min);
        return std::wstring(buf);
    }

    //按本地「日历日」算天数差：把两者都归零到当日 0 点再相减（避免按 86400 秒整除
    //在夏令时切换日产生 ±1 误差）。mktime 会按本地时区解释并归一化。
    struct tm dUp = tmUp;
    dUp.tm_hour = 0;
    dUp.tm_min  = 0;
    dUp.tm_sec  = 0;
    dUp.tm_isdst = -1;
    struct tm dNow = tmNow;
    dNow.tm_hour = 0;
    dNow.tm_min  = 0;
    dNow.tm_sec  = 0;
    dNow.tm_isdst = -1;
    time_t tUp0  = mktime(&dUp);
    time_t tNow0 = mktime(&dNow);

    long dayDiff = 0;
    if (tUp0 != (time_t)-1 && tNow0 != (time_t)-1)
    {
        //加半天再整除，吸收夏令时导致的非整 86400 秒日长偏差。
        dayDiff = static_cast<long>((tNow0 - tUp0 + 43200) / 86400);
    }

    //前一天：显示"昨天"。
    if (dayDiff == 1)
        return std::wstring(TR(L"昨天"));

    //更早但 6 天内：显示中文星期几。
    if (dayDiff >= 2 && dayDiff <= 6)
    {
        static const wchar_t* const kWeek[7] =
        {
            TR(L"周日"), TR(L"周一"), TR(L"周二"), TR(L"周三"), TR(L"周四"), TR(L"周五"), TR(L"周六")
        };
        int wday = tmUp.tm_wday;
        if (wday < 0 || wday > 6)
            wday = 0;
        return std::wstring(kWeek[wday]);
    }

    //更早：同年显示"M/D"，跨年显示"YYYY/M/D"。
    if (tmUp.tm_year == tmNow.tm_year)
        swprintf_s(buf, L"%d/%d", tmUp.tm_mon + 1, tmUp.tm_mday);
    else
        swprintf_s(buf, L"%d/%d/%d", tmUp.tm_year + 1900, tmUp.tm_mon + 1, tmUp.tm_mday);
    return std::wstring(buf);
}

} // namespace balloonchatwnd
