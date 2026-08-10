/**
 *  flamingoAdmin —— 列表分页纯函数 Util/ListPaging 单元测试。
 *
 *  覆盖 ListPageCount / ListClampPage / ListPageRange 的常规与边界情形
 *  (空表、整除、末页不足、页号越界 / 为负、pageSize 非法)。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/ListPaging.h"

namespace flamingoadmin { namespace test {

void RunAllListPagingTests()
{
    // ---- ListPageCount ----
    BeginCase("ListPaging.PageCount:页数计算(含空表与向上取整)");
    {
        FA_EXPECT_EQ(ListPageCount(0, 10), 1);    // 空表也占 1 页
        FA_EXPECT_EQ(ListPageCount(1, 10), 1);
        FA_EXPECT_EQ(ListPageCount(10, 10), 1);   // 整除
        FA_EXPECT_EQ(ListPageCount(11, 10), 2);   // 向上取整
        FA_EXPECT_EQ(ListPageCount(25, 10), 3);
        FA_EXPECT_EQ(ListPageCount(25, 0), 25);   // pageSize<=0 按 1 处理 → 25 页
    }

    // ---- ListClampPage ----
    BeginCase("ListPaging.ClampPage:页号夹取到合法区间");
    {
        // 25 条 / 每页 10 → 3 页(合法页号 0,1,2)
        FA_EXPECT_EQ(ListClampPage(-1, 25, 10), 0);   // 负 → 0
        FA_EXPECT_EQ(ListClampPage(0,  25, 10), 0);
        FA_EXPECT_EQ(ListClampPage(2,  25, 10), 2);
        FA_EXPECT_EQ(ListClampPage(5,  25, 10), 2);   // 越界 → 末页
        FA_EXPECT_EQ(ListClampPage(3,  0,  10), 0);   // 空表 → 第 0 页
    }

    // ---- ListPageRange ----
    BeginCase("ListPaging.PageRange:本页项区间(含末页不足与越界)");
    {
        int begin = -1;
        int count = -1;

        // 空表:区间为空。
        ListPageRange(0, 10, 0, begin, count);
        FA_EXPECT_EQ(begin, 0);
        FA_EXPECT_EQ(count, 0);

        // 25 条 / 每页 10:第 0 页 → [0,10)。
        ListPageRange(0, 10, 25, begin, count);
        FA_EXPECT_EQ(begin, 0);
        FA_EXPECT_EQ(count, 10);

        // 第 1 页 → [10,20)。
        ListPageRange(1, 10, 25, begin, count);
        FA_EXPECT_EQ(begin, 10);
        FA_EXPECT_EQ(count, 10);

        // 第 2 页(末页)→ [20,25),仅 5 条。
        ListPageRange(2, 10, 25, begin, count);
        FA_EXPECT_EQ(begin, 20);
        FA_EXPECT_EQ(count, 5);

        // 越界页号 → 夹取到末页。
        ListPageRange(9, 10, 25, begin, count);
        FA_EXPECT_EQ(begin, 20);
        FA_EXPECT_EQ(count, 5);

        // 负页号 → 第 0 页。
        ListPageRange(-3, 10, 25, begin, count);
        FA_EXPECT_EQ(begin, 0);
        FA_EXPECT_EQ(count, 10);
    }
}

}} // namespace flamingoadmin::test
