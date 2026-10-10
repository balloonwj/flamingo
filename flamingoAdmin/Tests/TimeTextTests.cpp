/**
 *  flamingoAdmin —— 时间显示格式的单元测试（FMT1）。
 *
 *  控制台原先有四种时间写法：公告列表与仪表板卡片截成「MM-DD HH:MM」、审计列表截成
 *  「MM-DD HH:MM:SS」、详情显示完整的「YYYY-MM-DD HH:MM:SS」、企业详情的「开通」把整个
 *  时间都显示出来。2026-10-04 起统一由 Util/TimeText.h 按三种样式生成。本文件覆盖：
 *    - FMT1：服务端时间文字按日期 / 分钟 / 秒三种样式格式化；只有日期或只到分钟的输入不会
 *            补出不存在的部分；分隔符为「T」时规整为空格；无法识别的文字原样返回；
 *            SYSTEMTIME 按三种样式格式化。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/TimeText.h"

namespace flamingoadmin { namespace test {

namespace {

// FMT1：时间格式化。
void TestFormatTimeText()
{
    BeginCase("FMT1 时间按日期 / 分钟 / 秒三种样式格式化，无法识别的文字原样返回");

    const CString full = _T("2026-10-04 16:46:39");
    FA_EXPECT_STREQ(timetext::FormatTimeText(full, timetext::TimeStyle::Date), _T("2026-10-04"));
    FA_EXPECT_STREQ(timetext::FormatTimeText(full, timetext::TimeStyle::Minute), _T("2026-10-04 16:46"));
    FA_EXPECT_STREQ(timetext::FormatTimeText(full, timetext::TimeStyle::Second), _T("2026-10-04 16:46:39"));

    // 只到分钟的输入：要秒时不补出「:00」。
    const CString minute = _T("2026-12-01 09:30");
    FA_EXPECT_STREQ(timetext::FormatTimeText(minute, timetext::TimeStyle::Second), _T("2026-12-01 09:30"));
    FA_EXPECT_STREQ(timetext::FormatTimeText(minute, timetext::TimeStyle::Minute), _T("2026-12-01 09:30"));

    // 只有日期的输入：要时间时只给日期。
    const CString date = _T("2026-10-04");
    FA_EXPECT_STREQ(timetext::FormatTimeText(date, timetext::TimeStyle::Minute), _T("2026-10-04"));
    FA_EXPECT_STREQ(timetext::FormatTimeText(date, timetext::TimeStyle::Date), _T("2026-10-04"));

    // 首尾空白去掉；日期与时间之间的「T」规整为空格；秒之后的小数部分去掉。
    FA_EXPECT_STREQ(timetext::FormatTimeText(_T(" 2026-10-04T08:05:06 "), timetext::TimeStyle::Second),
                    _T("2026-10-04 08:05:06"));
    FA_EXPECT_STREQ(timetext::FormatTimeText(_T("2026-10-04 08:05:06.123"), timetext::TimeStyle::Second),
                    _T("2026-10-04 08:05:06"));

    // 无法识别的文字（空串、横杠占位、格式不对）原样返回，由调用方决定怎么显示。
    FA_EXPECT_TRUE(timetext::FormatTimeText(CString(), timetext::TimeStyle::Minute).IsEmpty());
    FA_EXPECT_STREQ(timetext::FormatTimeText(_T("—"), timetext::TimeStyle::Minute), _T("—"));
    FA_EXPECT_STREQ(timetext::FormatTimeText(_T("2026/10/04 16:46"), timetext::TimeStyle::Date),
                    _T("2026/10/04 16:46"));
    FA_EXPECT_STREQ(timetext::FormatTimeText(_T("10-04 16:46"), timetext::TimeStyle::Date), _T("10-04 16:46"));

    // SYSTEMTIME 按三种样式格式化。
    SYSTEMTIME st = {};
    st.wYear   = 2026;
    st.wMonth  = 3;
    st.wDay    = 7;
    st.wHour   = 9;
    st.wMinute = 5;
    st.wSecond = 2;
    FA_EXPECT_STREQ(timetext::FormatSystemTime(st, timetext::TimeStyle::Date), _T("2026-03-07"));
    FA_EXPECT_STREQ(timetext::FormatSystemTime(st, timetext::TimeStyle::Minute), _T("2026-03-07 09:05"));
    FA_EXPECT_STREQ(timetext::FormatSystemTime(st, timetext::TimeStyle::Second), _T("2026-03-07 09:05:02"));
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllTimeTextTests()
{
    TestFormatTimeText();
}

}} // namespace flamingoadmin::test
