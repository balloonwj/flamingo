/**
 *  flamingoAdmin —— 登录页上随状态变化的文字，实现见 LoginText.h 的说明。
 *
 *  本文件用到 __DATE__，工程的预生成步骤会在每次构建前更新本文件的时间戳，使它每次都被
 *  重新编译（做法与 PC 客户端关于页的构建时间戳相同）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Util/LoginText.h"
#include "Util/AppConfig.h"

#include <cstring>

namespace flamingoadmin {

namespace logintext {

namespace {

// __DATE__ 的固定格式「Mmm dd yyyy」中各字段的位置与长度。
const size_t kDateLen          = 11;   // 整个字符串的长度
const size_t kDateMonthOffset  = 0;    // 月份缩写的起始位置
const size_t kDateMonthLen     = 3;    // 月份缩写的长度
const size_t kDateDayOffset    = 4;    // 日的起始位置
const size_t kDateDayLen       = 2;    // 日的位数（不足两位时首位是空格）
const size_t kDateYearOffset   = 7;    // 年的起始位置
const size_t kDateYearLen      = 4;    // 年的位数
const int    kMonthsPerYear    = 12;   // 一年的月数
const int    kDecimalBase      = 10;   // 十进制基数

// 按 __DATE__ 的写法排列的十二个月份缩写，下标加 1 即月份。
const char* const kMonthAbbrevs[kMonthsPerYear] =
{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

// 把一段数字解析成整数，前导空格当作 0；遇到其它非数字字符返回 -1。
//   text：起始位置；len：字符个数。
int ParseDigits(const char* text, size_t len)
{
    int value = 0;
    for (size_t i = 0; i < len; ++i)
    {
        const char c = text[i];
        if (c == ' ')
        {
            continue;
        }
        if (c < '0' || c > '9')
        {
            return -1;
        }
        value = value * kDecimalBase + (c - '0');
    }
    return value;
}

} // anonymous namespace

CString FormatBuildDate(const char* compilerDate)
{
    if (compilerDate == nullptr || std::strlen(compilerDate) < kDateLen)
    {
        return CString();
    }

    int month = 0;
    for (int i = 0; i < kMonthsPerYear; ++i)
    {
        if (std::strncmp(compilerDate + kDateMonthOffset, kMonthAbbrevs[i], kDateMonthLen) == 0)
        {
            month = i + 1;
            break;
        }
    }
    const int day  = ParseDigits(compilerDate + kDateDayOffset, kDateDayLen);
    const int year = ParseDigits(compilerDate + kDateYearOffset, kDateYearLen);
    if (month == 0 || day <= 0 || year <= 0)
    {
        return CString();
    }

    CString text;
    text.Format(_T("%04d.%02d.%02d"), year, month, day);
    return text;
}

CString MakeVersionLine(const CString& version, const CString& buildDate)
{
    CString line = CString(_T("v")) + version;
    if (!buildDate.IsEmpty())
    {
        line += _T(" · 构建 ");
        line += buildDate;
    }
    return line;
}

CString VersionLine()
{
    // __DATE__ 在编译本文件时展开；预生成步骤保证本文件每次构建都被重新编译。
    return MakeVersionLine(kAdminVersion, FormatBuildDate(__DATE__));
}

void ResolvePrefill(const LoginConfig& saved, CString& outAccount, CString& outPassword)
{
    outAccount = saved.m_account;
    // 没勾「记住密码」时 m_password 本应为空，这里仍按勾选状态判断，使意图明确：
    // 密码只在用户要求记住时才回填。
    outPassword = saved.m_rememberPassword ? saved.m_password : CString();
}

} // namespace logintext

} // namespace flamingoadmin
