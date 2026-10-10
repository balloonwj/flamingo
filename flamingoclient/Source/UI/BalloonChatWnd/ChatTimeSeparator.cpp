/**
 *  聊天消息「时间分隔行」判定与文案的实现，ChatTimeSeparator.cpp
 *  分档口径与各参数含义见 ChatTimeSeparator.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "ChatTimeSeparator.h"

#include <time.h>
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//毫秒 → 秒的换算基数。
const long long kMsPerSecond = 1000LL;

//把 Unix 毫秒转成本地时间。转换失败返回 false，调用方据此放弃格式化。
bool ToLocalTm(long long timestampMs, struct tm& out)
{
    if (timestampMs <= 0)
    {
        return false;
    }
    const time_t secs = (time_t)(timestampMs / kMsPerSecond);
    return localtime_s(&out, &secs) == 0;
}

//两个本地时间是不是同一天。
bool IsSameDay(const struct tm& a, const struct tm& b)
{
    return a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
}

} // anonymous namespace

bool ShouldInsertTimeSeparator(long long prevMs, long long curMs, long long thresholdMs)
{
    //本条时间未知：插了也写不出文案，不插。
    if (curMs <= 0)
    {
        return false;
    }

    //没有上一条（消息流的第一条）：开头总该有个时间。
    if (prevMs <= 0)
    {
        return true;
    }

    //时间倒挂（服务器时间回拨、或消息乱序）时也插一行 —— 时间倒着走本身就该让用户
    //看见，闷着不显示只会更费解。
    if (curMs < prevMs)
    {
        return true;
    }

    //用 >= ：正好满阈值就算"隔了一会儿"，不必多等一毫秒。
    return (curMs - prevMs) >= thresholdMs;
}

CString FormatChatSeparatorTime(long long timestampMs, long long nowMs)
{
    struct tm msgTm;
    if (!ToLocalTm(timestampMs, msgTm))
    {
        return CString(_T(""));
    }

    //当前时刻取不到时退化成"今年更早"的写法：宁可多写一截日期，也不要显示成
    //一个孤零零的时分让人误以为是今天的。
    struct tm nowTm;
    const bool nowOk = ToLocalTm(nowMs, nowTm);

    CString text;
    if (nowOk && IsSameDay(msgTm, nowTm))
    {
        text.Format(_T("%02d:%02d"), msgTm.tm_hour, msgTm.tm_min);
        return text;
    }

    if (nowOk)
    {
        //昨天：把"现在"往前推一天再比同一天，可绕开月末、年末与闰年的边界。
        const time_t yesterdaySecs = (time_t)(nowMs / kMsPerSecond) - 24 * 60 * 60;
        struct tm yesterdayTm;
        if (localtime_s(&yesterdayTm, &yesterdaySecs) == 0 && IsSameDay(msgTm, yesterdayTm))
        {
            text.Format(TR(L"昨天 %02d:%02d"), msgTm.tm_hour, msgTm.tm_min);
            return text;
        }
    }

    if (nowOk && msgTm.tm_year == nowTm.tm_year)
    {
        text.Format(_T("%02d-%02d %02d:%02d"),
                    msgTm.tm_mon + 1, msgTm.tm_mday, msgTm.tm_hour, msgTm.tm_min);
        return text;
    }

    //往年（或当前时刻取不到）：带上年份，跨年之后光看月日分不清是哪一年的。
    text.Format(_T("%d-%02d-%02d %02d:%02d"),
                msgTm.tm_year + 1900, msgTm.tm_mon + 1, msgTm.tm_mday,
                msgTm.tm_hour, msgTm.tm_min);
    return text;
}

} // namespace balloonchatwnd
