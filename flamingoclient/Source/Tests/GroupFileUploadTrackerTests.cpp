/**
 *  群文件「正在上传」行跟踪器单元测试（Suite GFUT），GroupFileUploadTrackerTests.cpp
 *
 *  覆盖 balloonchatwnd::GroupFileUploadTracker 的纯逻辑：
 *    GFUT1 Begin 后 Items 含该笔、Count==1、Find 命中且 name/ext/percent 初值正确；
 *    GFUT2 多笔 Begin 按「最近发起在最前」排序，Count 正确；
 *    GFUT3 SetProgress 钳到 0~100；对不存在 id 无操作；
 *    GFUT4 Remove 按 id 删除，删后 Find 返回空、Count 减一；删不存在 id 返回 false；
 *    GFUT5 Find 区分「在途（命中）」与「已移除（落空）」——窗口据此区分取消 vs 真失败。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonChatWnd/GroupFileUploadTracker.h"

using namespace balloonchatwnd;

TEST_CASE("GFUT1 Begin 后可查到且初值正确")
{
    GroupFileUploadTracker t;
    REQUIRE(t.Count() == 0);

    t.Begin(1, L"report.pdf", L"pdf");
    REQUIRE(t.Count() == 1);
    REQUIRE(t.Items().size() == 1);

    const GroupUploadEntry* e = t.Find(1);
    REQUIRE(e != NULL);
    REQUIRE(e->m_uploadId == 1);
    REQUIRE(e->m_name == L"report.pdf");
    REQUIRE(e->m_ext == L"pdf");
    REQUIRE(e->m_percent == 0);

    // 不存在的 id 查空
    REQUIRE(t.Find(2) == NULL);
}

TEST_CASE("GFUT2 多笔 Begin 最近发起排最前")
{
    GroupFileUploadTracker t;
    t.Begin(1, L"a.txt", L"txt");
    t.Begin(2, L"b.doc", L"doc");
    t.Begin(3, L"c.xlsx", L"xlsx");

    REQUIRE(t.Count() == 3);
    // index 0 = 最近发起（3），其后依次 2、1
    REQUIRE(t.Items()[0].m_uploadId == 3);
    REQUIRE(t.Items()[1].m_uploadId == 2);
    REQUIRE(t.Items()[2].m_uploadId == 1);
}

TEST_CASE("GFUT3 SetProgress 钳位且对未知 id 无操作")
{
    GroupFileUploadTracker t;
    t.Begin(1, L"a.txt", L"txt");

    t.SetProgress(1, 42);
    REQUIRE(t.Find(1)->m_percent == 42);

    // 越界钳位
    t.SetProgress(1, -5);
    REQUIRE(t.Find(1)->m_percent == 0);
    t.SetProgress(1, 150);
    REQUIRE(t.Find(1)->m_percent == 100);

    // 未知 id：无操作，不崩、不新增
    t.SetProgress(999, 50);
    REQUIRE(t.Count() == 1);
    REQUIRE(t.Find(1)->m_percent == 100);
}

TEST_CASE("GFUT4 Remove 按 id 删除")
{
    GroupFileUploadTracker t;
    t.Begin(1, L"a.txt", L"txt");
    t.Begin(2, L"b.doc", L"doc");
    REQUIRE(t.Count() == 2);

    REQUIRE(t.Remove(1) == true);
    REQUIRE(t.Count() == 1);
    REQUIRE(t.Find(1) == NULL);
    REQUIRE(t.Find(2) != NULL);          // 另一笔不受影响

    // 删不存在的 id 返回 false、不改变
    REQUIRE(t.Remove(1) == false);
    REQUIRE(t.Remove(999) == false);
    REQUIRE(t.Count() == 1);
}

TEST_CASE("GFUT5 Find 区分在途与已移除（取消 vs 失败判据）")
{
    GroupFileUploadTracker t;
    t.Begin(7, L"big.zip", L"zip");

    // 在途：Find 命中 → 窗口据此判定为「真失败要提示」
    REQUIRE(t.Find(7) != NULL);

    // 用户取消：窗口先 Remove 该行
    REQUIRE(t.Remove(7) == true);

    // 之后后端回投的失败结果再来时 Find 落空 → 窗口判定为「已取消，静默忽略」
    REQUIRE(t.Find(7) == NULL);
    REQUIRE(t.Count() == 0);
}
