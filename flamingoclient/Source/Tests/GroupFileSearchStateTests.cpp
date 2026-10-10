/**
 *  主面板搜索下拉群文件请求状态的单元测试（Suite GFSS），GroupFileSearchStateTests.cpp
 *
 *  覆盖 balloonsearch::GroupFileSearchState 与 GroupFileStatusHint：
 *    GFSS1  发出请求后等应答；回显关键字相同的应答被采纳，成功与失败分别进入对应状态；
 *    GFSS2  连续输入时上一个关键字的应答迟到，被丢弃，不影响当前关键字的等待状态；
 *    GFSS3  同一次请求的应答重复到达、收起下拉（Reset）之后到达、没连上服务器时到达，都被丢弃；
 *    GFSS4  StatusFor 只认最近一次记录的关键字，其余关键字（含空串）一律视为还没发请求；
 *    GFSS5  各状态的提示文字：等待、没连上服务器、失败三种有提示，其余为空串。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/GroupFileSearchState.h"

#include <string>

using balloonsearch::GroupFileSearchState;

TEST_CASE("GFSS1 回显关键字相同的应答被采纳", "[globalsearch]")
{
    GroupFileSearchState s;
    s.OnRequestSent("report");
    CHECK(s.StatusFor("report") == balloonsearch::kGroupFilePending);

    CHECK(s.OnResponse("report", true));
    CHECK(s.StatusFor("report") == balloonsearch::kGroupFileDone);

    GroupFileSearchState f;
    f.OnRequestSent("report");
    CHECK(f.OnResponse("report", false));
    CHECK(f.StatusFor("report") == balloonsearch::kGroupFileFailed);
}

TEST_CASE("GFSS2 上一个关键字的应答迟到时被丢弃", "[globalsearch]")
{
    GroupFileSearchState s;
    s.OnRequestSent("rep");
    s.OnRequestSent("report");

    //"rep" 的应答这时才到：丢弃，"report" 仍在等待
    CHECK_FALSE(s.OnResponse("rep", true));
    CHECK(s.StatusFor("report") == balloonsearch::kGroupFilePending);
    CHECK(s.StatusFor("rep") == balloonsearch::kGroupFileIdle);

    CHECK(s.OnResponse("report", true));
    CHECK(s.StatusFor("report") == balloonsearch::kGroupFileDone);
}

TEST_CASE("GFSS3 重复应答、收起后的应答、未发出时的应答都被丢弃", "[globalsearch]")
{
    GroupFileSearchState s;
    s.OnRequestSent("合同");
    CHECK(s.OnResponse("合同", true));
    //同一次请求的应答重复到达
    CHECK_FALSE(s.OnResponse("合同", false));
    CHECK(s.StatusFor("合同") == balloonsearch::kGroupFileDone);

    //收起下拉之后应答才到
    s.OnRequestSent("预算");
    s.Reset();
    CHECK_FALSE(s.OnResponse("预算", true));
    CHECK(s.StatusFor("预算") == balloonsearch::kGroupFileIdle);

    //没连上服务器、请求没有发出
    s.OnOffline("预算");
    CHECK(s.StatusFor("预算") == balloonsearch::kGroupFileOffline);
    CHECK_FALSE(s.OnResponse("预算", true));
    CHECK(s.StatusFor("预算") == balloonsearch::kGroupFileOffline);
}

TEST_CASE("GFSS4 StatusFor 只认最近一次记录的关键字", "[globalsearch]")
{
    GroupFileSearchState s;
    CHECK(s.StatusFor("") == balloonsearch::kGroupFileIdle);
    CHECK(s.StatusFor("abc") == balloonsearch::kGroupFileIdle);

    s.OnRequestSent("abc");
    CHECK(s.StatusFor("abcd") == balloonsearch::kGroupFileIdle);
    //关键字按字节比较，大小写不同即视为不同的请求
    CHECK(s.StatusFor("ABC") == balloonsearch::kGroupFileIdle);
    CHECK(s.StatusFor("") == balloonsearch::kGroupFileIdle);
}

TEST_CASE("GFSS5 各状态的提示文字", "[globalsearch]")
{
    CHECK(balloonsearch::GroupFileStatusHint(balloonsearch::kGroupFilePending) == L"正在搜索群文件…");
    CHECK(balloonsearch::GroupFileStatusHint(balloonsearch::kGroupFileOffline) == L"群文件需联网搜索");
    CHECK(balloonsearch::GroupFileStatusHint(balloonsearch::kGroupFileFailed) == L"群文件搜索失败");
    CHECK(balloonsearch::GroupFileStatusHint(balloonsearch::kGroupFileIdle).empty());
    CHECK(balloonsearch::GroupFileStatusHint(balloonsearch::kGroupFileDone).empty());
}
