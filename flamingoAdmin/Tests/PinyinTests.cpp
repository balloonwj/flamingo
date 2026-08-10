/**
 *  flamingoAdmin —— 拼音首字母工具 Util/Pinyin 单元测试。
 *
 *  验证 PinyinInitials 对常见部门 / 人名取首字母正确,并覆盖 ASCII 混排、
 *  标点跳过、空串等情形。这些"期望值"是语言学上正确的拼音首字母,若 GB2312
 *  区间表 / 转码有偏差,测试会失败以暴露问题(先测后改)。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/Pinyin.h"

namespace flamingoadmin { namespace test {

void RunAllPinyinTests()
{
    BeginCase("Pinyin.Initials_CommonNames:常见部门 / 人名拼音首字母");
    {
        FA_EXPECT_STREQ(PinyinInitials(_T("研发部")), _T("yfb"));
        FA_EXPECT_STREQ(PinyinInitials(_T("市场部")), _T("scb"));
        FA_EXPECT_STREQ(PinyinInitials(_T("张三")),   _T("zs"));
        FA_EXPECT_STREQ(PinyinInitials(_T("李四")),   _T("ls"));
        FA_EXPECT_STREQ(PinyinInitials(_T("王五")),   _T("ww"));
    }

    BeginCase("Pinyin.Initials_MixedAndPunct:ASCII 混排、标点跳过、空串");
    {
        // ASCII 字母保留并小写;汉字取首字母。
        FA_EXPECT_STREQ(PinyinInitials(_T("IT部")),   _T("itb"));
        // 数字保留;空格 / 标点跳过。
        FA_EXPECT_STREQ(PinyinInitials(_T("研发 部")), _T("yfb"));
        FA_EXPECT_STREQ(PinyinInitials(_T("A组")),    _T("az"));
        // 空串 → 空。
        FA_EXPECT_STREQ(PinyinInitials(_T("")),       _T(""));
    }

    BeginCase("Pinyin.InitialOf_OutOfRange:越界码返回 0");
    {
        FA_EXPECT_EQ(static_cast<int>(PyInitialOf(0x0041)), 0);   // ASCII 'A' 的码,不在汉字区
        FA_EXPECT_EQ(static_cast<int>(PyInitialOf(0xFFFF)), 0);   // 越界
    }
}

}} // namespace flamingoadmin::test
