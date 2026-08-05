/**
 *  聊天文件气泡格式化助手单元测试（Suite FBF），FileBubbleFormatTests.cpp
 *  覆盖：FormatFileSize 各量级（B/KB/MB/GB）与四舍五入、边界；FileTypeLabelFromName
 *       转小写 / 多重扩展名 / 无扩展名 / 中文名。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-05
 */
#include "catch_amalgamated.hpp"
#include "../BalloonChatWnd/FileBubbleFormat.h"

#include <ctime>

using namespace balloonchatwnd;

namespace {

// 用本地时区把「年-月-日 时:分」造成 Unix 毫秒时间戳。now 与 up 都经此构造，
// 与 FormatRelativeDayTime 内部一致按本地时区解释，故测试结果与机器时区无关。
long long LocalMs(int year, int mon, int day, int hour, int minute)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = year - 1900;
    t.tm_mon   = mon - 1;
    t.tm_mday  = day;
    t.tm_hour  = hour;
    t.tm_min   = minute;
    t.tm_sec   = 0;
    t.tm_isdst = -1;
    time_t e = mktime(&t);
    return (long long)e * 1000LL;
}

} // anonymous namespace

TEST_CASE("FBF1 FormatFileSize 各量级与边界")
{
    //不足 1 KiB：整数字节
    CHECK(FormatFileSize(0)    == L"0 B");
    CHECK(FormatFileSize(1)    == L"1 B");
    CHECK(FormatFileSize(512)  == L"512 B");
    CHECK(FormatFileSize(1023) == L"1023 B");
    //KB 量级，一位小数
    CHECK(FormatFileSize(1024) == L"1.0 KB");
    CHECK(FormatFileSize(1536) == L"1.5 KB");   // 1.5 KiB 整
    //MB 量级，一位小数、四舍五入
    CHECK(FormatFileSize(4404019)  == L"4.2 MB");   // 4.2001 MiB → 4.2
    CHECK(FormatFileSize(1887436)  == L"1.8 MB");   // 1.7999 MiB → 1.8
    CHECK(FormatFileSize(29675357) == L"28.3 MB");  // 28.30 MiB
    //GB 量级
    CHECK(FormatFileSize(1073741824LL) == L"1.0 GB");   // 1 GiB
    CHECK(FormatFileSize(2147483648LL) == L"2.0 GB");   // 2 GiB
    //负数按 0 处理
    CHECK(FormatFileSize(-5) == L"0 B");
}

TEST_CASE("FBF2 FormatFileSize 单位切换边界")
{
    //刚到 1 MiB：进位到 MB
    CHECK(FormatFileSize(1024LL * 1024) == L"1.0 MB");
    //差 1 字节不足 1 MiB：仍是 KB（1048575/1024=1023.99→四舍五入 1024.0 KB）
    CHECK(FormatFileSize(1024LL * 1024 - 1) == L"1024.0 KB");
}

TEST_CASE("FBF3 FileTypeLabelFromName 转小写与多重扩展名")
{
    CHECK(FileTypeLabelFromName(L"banner-final.fig") == L"fig");
    CHECK(FileTypeLabelFromName(L"a.PDF")            == L"pdf");   // 大写转小写
    CHECK(FileTypeLabelFromName(L"report.DOCX")      == L"docx");
    CHECK(FileTypeLabelFromName(L"archive.tar.gz")   == L"gz");    // 取最后一段
}

TEST_CASE("FBF4 FileTypeLabelFromName 无扩展名情形返回空")
{
    CHECK(FileTypeLabelFromName(L"README")     == L"");   // 无 '.'
    CHECK(FileTypeLabelFromName(L".gitignore") == L"");   // '.' 在首位（隐藏文件）
    CHECK(FileTypeLabelFromName(L"noext.")     == L"");   // '.' 在末尾
    CHECK(FileTypeLabelFromName(L"")           == L"");   // 空名
}

TEST_CASE("FBF5 FileTypeLabelFromName 中文文件名取扩展名正常")
{
    CHECK(FileTypeLabelFromName(L"季度报告（终稿）.docx") == L"docx");
    CHECK(FileTypeLabelFromName(L"品牌色规范.pdf")        == L"pdf");
}

TEST_CASE("FBF6 FormatRelativeDayTime 同一天显示「今天 HH:MM」")
{
    long long now = LocalMs(2026, 6, 8, 14, 31);
    long long up  = LocalMs(2026, 6, 8,  9,  5);
    CHECK(FormatRelativeDayTime(up, now) == L"今天 09:05");
    //同天但分钟个位 → 仍补零
    CHECK(FormatRelativeDayTime(LocalMs(2026, 6, 8, 0, 3), now) == L"今天 00:03");
}

TEST_CASE("FBF7 FormatRelativeDayTime 前一天显示「昨天」")
{
    long long now = LocalMs(2026, 6, 8, 14, 31);
    long long up  = LocalMs(2026, 6, 7, 23, 50);
    CHECK(FormatRelativeDayTime(up, now) == L"昨天");
}

TEST_CASE("FBF8 FormatRelativeDayTime 6 天内显示中文星期几")
{
    long long now = LocalMs(2026, 6, 8, 14, 31);
    long long up  = LocalMs(2026, 6, 4, 10, 0);   // 4 天前
    std::wstring r = FormatRelativeDayTime(up, now);
    //结果应为"周X"两字（具体星期与日历相关，此处只校形态，避免硬编码错星期）。
    REQUIRE(r.size() == 2);
    CHECK(r[0] == L'周');
    CHECK(r != L"今天");
    CHECK(r != L"昨天");
}

TEST_CASE("FBF9 FormatRelativeDayTime 更早同年显示「M/D」、跨年显示「YYYY/M/D」")
{
    long long now = LocalMs(2026, 6, 8, 14, 31);
    //同年、超过 6 天 → "M/D"
    CHECK(FormatRelativeDayTime(LocalMs(2026, 5, 15, 9, 0), now) == L"5/15");
    //跨年 → "YYYY/M/D"
    CHECK(FormatRelativeDayTime(LocalMs(2025, 12, 3, 9, 0), now) == L"2025/12/3");
}

TEST_CASE("FBF10 FormatRelativeDayTime 非正时间戳返回空")
{
    long long now = LocalMs(2026, 6, 8, 14, 31);
    CHECK(FormatRelativeDayTime(0, now)   == L"");
    CHECK(FormatRelativeDayTime(-1, now)  == L"");
}
