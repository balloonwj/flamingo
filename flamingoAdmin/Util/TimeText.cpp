/**
 *  flamingoAdmin —— 界面上的时间显示格式，规则见 TimeText.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Util/TimeText.h"

namespace flamingoadmin {

namespace timetext {

namespace {

// 「YYYY-MM-DD HH:MM:SS」中各段的长度与分隔符位置。
const int kDateLen   = 10;   // 「YYYY-MM-DD」的长度
const int kMinuteLen = 16;   // 「YYYY-MM-DD HH:MM」的长度
const int kSecondLen = 19;   // 「YYYY-MM-DD HH:MM:SS」的长度
const int kDash1     = 4;    // 年月之间的「-」
const int kDash2     = 7;    // 月日之间的「-」
const int kSep       = 10;   // 日期与时间之间的空格（或「T」）
const int kColon1    = 13;   // 时分之间的「:」
const int kColon2    = 16;   // 分秒之间的「:」

// 判断 text 的 [begin, end) 区间是否全为数字。
bool AllDigits(const CString& text, int begin, int end)
{
    for (int i = begin; i < end; ++i)
    {
        if (text[i] < _T('0') || text[i] > _T('9'))
        {
            return false;
        }
    }
    return true;
}

// 判断 text 是否以「YYYY-MM-DD」开头。
bool StartsWithDate(const CString& text)
{
    return text.GetLength() >= kDateLen
        && text[kDash1] == _T('-') && text[kDash2] == _T('-')
        && AllDigits(text, 0, kDash1)
        && AllDigits(text, kDash1 + 1, kDash2)
        && AllDigits(text, kDash2 + 1, kDateLen);
}

// 判断以日期开头的 text 是否还带着「 HH:MM」。
bool HasMinute(const CString& text)
{
    return text.GetLength() >= kMinuteLen
        && text[kSep] == _T(' ') && text[kColon1] == _T(':')
        && AllDigits(text, kSep + 1, kColon1)
        && AllDigits(text, kColon1 + 1, kMinuteLen);
}

// 判断带「 HH:MM」的 text 是否还带着「:SS」。
bool HasSecond(const CString& text)
{
    return text.GetLength() >= kSecondLen
        && text[kColon2] == _T(':')
        && AllDigits(text, kColon2 + 1, kSecondLen);
}

} // anonymous namespace

CString FormatTimeText(const CString& text, TimeStyle style)
{
    CString t = text;
    t.Trim();
    if (!StartsWithDate(t))
    {
        return text;
    }
    // 日期与时间之间的「T」规整为空格。
    if (t.GetLength() > kSep && t[kSep] == _T('T'))
    {
        t.SetAt(kSep, _T(' '));
    }

    if (style == TimeStyle::Date || !HasMinute(t))
    {
        return t.Left(kDateLen);
    }
    if (style == TimeStyle::Minute || !HasSecond(t))
    {
        return t.Left(kMinuteLen);
    }
    return t.Left(kSecondLen);
}

CString FormatSystemTime(const SYSTEMTIME& st, TimeStyle style)
{
    CString text;
    switch (style)
    {
    //只显示日期
    case TimeStyle::Date:
        text.Format(_T("%04d-%02d-%02d"), st.wYear, st.wMonth, st.wDay);
        break;
    //显示到分钟
    case TimeStyle::Minute:
        text.Format(_T("%04d-%02d-%02d %02d:%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        break;
    //显示到秒
    case TimeStyle::Second:
    default:
        text.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"), st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond);
        break;
    }
    return text;
}

} // namespace timetext

} // namespace flamingoadmin
