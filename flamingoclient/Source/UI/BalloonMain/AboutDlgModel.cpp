/**
 *  AboutDlgModel.cpp
 *
 *  AboutDlgModel.h 里纯函数的实现。只装配字符串，不碰任何窗口 / GDI / 业务对象，
 *  故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。
 *
 *  本文件含中文宽字符字面量（标题前缀），工程已开启 /utf-8 编译选项，源码按 UTF-8
 *  解析，L"中文" 字面量正常，无需 BOM。在 Flamingo.vcxproj 里本文件标
 *  PrecompiledHeader=NotUsing（不 include stdafx.h）。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "AboutDlgModel.h"
#include "../../App/BrandText.h"   // kDefaultAppTitle（产品名称为空时的回退值）

#include <cwchar>                 // swprintf_s（拼构建时间戳）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

//十进制的基数。逐位把 __DATE__ / __TIME__ 里的数字字符拼成整数时用。
const int kDecimalBase = 10;

//一年的月份数。__DATE__ 给的月份是英文缩写，要按下表反查成数字。
const int kMonthsInYear = 12;

//__DATE__ 的形式固定为 "Mmm dd yyyy"（日为个位数时前面补空格而不是 0），
//下面几个是各字段在其中的起始下标与长度。
const int kDateMonthOffset = 0;
const int kDateDayOffset   = 4;
const int kDateYearOffset  = 7;
const int kDateYearDigits  = 4;

//__TIME__ 的形式固定为 "hh:mm:ss"，冒号占第 2、5 位。
const int kTimeHourOffset   = 0;
const int kTimeMinuteOffset = 3;

//月、日、时、分在 __DATE__ / __TIME__ 里都占两位。
const int kTwoDigitFieldLen = 2;

//构建时间戳的字符数上限。实际形如 "20260819-1530" 共 13 个字符，留出富余。
const int kBuildStampCapacity = 32;

/**
 *  把 __DATE__ 里的月份英文缩写转成 1~12。
 *    abbrev  指向三个字母的月份缩写（如 "Aug"），不要求以 0 结尾。
 *  返回 0 表示没认出来 —— 编译器给的月份必在表内，正常不会发生。
 */
int MonthFromAbbrev(const char* abbrev)
{
    static const char* const kMonthAbbrev[kMonthsInYear] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    for (int i = 0; i < kMonthsInYear; ++i)
    {
        if (abbrev[0] == kMonthAbbrev[i][0]
            && abbrev[1] == kMonthAbbrev[i][1]
            && abbrev[2] == kMonthAbbrev[i][2])
        {
            return i + 1;
        }
    }
    return 0;
}

/**
 *  把一段数字字符转成整数。
 *    text    起始位置。
 *    count   位数。
 *  非数字字符（__DATE__ 的日为个位数时前面那个空格）按 0 处理。
 */
int DigitsToInt(const char* text, int count)
{
    int value = 0;
    for (int i = 0; i < count; ++i)
    {
        const char c = text[i];
        const int digit = (c >= '0' && c <= '9') ? (c - '0') : 0;
        value = value * kDecimalBase + digit;
    }
    return value;
}

} // namespace

const wchar_t* AboutBuildStamp()
{
    // __DATE__ / __TIME__ 是编译<u>本文件</u>时展开的，工程的预生成步骤保证本文件每次
    // 构建都被重新编译，故这里取到的就是当次构建的时间。只算一次，此后复用。
    static wchar_t s_stamp[kBuildStampCapacity] = { 0 };
    if (s_stamp[0] != 0)
    {
        return s_stamp;
    }

    const char* const date = __DATE__;
    const char* const time = __TIME__;

    const int year   = DigitsToInt(date + kDateYearOffset, kDateYearDigits);
    const int month  = MonthFromAbbrev(date + kDateMonthOffset);
    const int day    = DigitsToInt(date + kDateDayOffset, kTwoDigitFieldLen);
    const int hour   = DigitsToInt(time + kTimeHourOffset, kTwoDigitFieldLen);
    const int minute = DigitsToInt(time + kTimeMinuteOffset, kTwoDigitFieldLen);

    swprintf_s(s_stamp, L"%04d%02d%02d-%02d%02d", year, month, day, hour, minute);
    return s_stamp;
}

std::wstring AboutVersionLine()
{
    //{0} 为版本号，{1} 为编译时间戳
    return TRF(L"版本 {0} (Build {1})", kAboutVersion, AboutBuildStamp());
}

std::wstring BuildAboutCaption(const wchar_t* appName)
{
    // 「关于 <产品名称>」；产品名称为空时回退到缺省名，避免只剩「关于 」两个字
    const wchar_t* name = (appName != NULL && appName[0] != L'\0') ? appName : brandtext::kDefaultAppTitle;
    return TRF(L"关于 {0}", name);
}

std::wstring BuildAboutTitle(const wchar_t* appName, unsigned long processId)
{
    // 形如「关于 FlamingoIM [12345]」：「关于 <产品名称>」+「 [」+ pid +「]」。
    std::wstring title = BuildAboutCaption(appName);
    title += L" [";
    title += std::to_wstring(processId);
    title += L"]";
    return title;
}

} // namespace balloonmain
