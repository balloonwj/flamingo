/**
 *  分享者上线比对单元测试（Suite SRC，标签 [sharereconcile]），ShareReconcileTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 12 步的 12a，方案 8.6 的四种情况）：
 *    - SRC1 服务端有、本机有、绑定本机：列入报告，带出本机的共享根；2210 的一项按共享根是否可用给出原因；
 *    - SRC2 服务端有、绑定本机、本机没有记录：列入「本机缺少记录」，不报告；
 *    - SRC3 本机有、服务端已没有：列入删除；还没有 shareid 的本机记录既不删除也不报告；
 *    - SRC4 绑定别的设备：列入「位于另一台电脑」，不报告，本机即便有记录也不删除；
 *    - SRC5 四种情况混在一起、服务端列表里有重复项时，各列表互不重叠且保持输入顺序。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Engine/ShareReconcile.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//构造服务端「我的共享」里的一项
ShareListEntry ServerShare(int64_t shareId, bool onThisDevice)
{
    ShareListEntry e;
    e.m_shareId = shareId;
    e.m_kind = share_kind_folder;
    e.m_onThisDevice = onThisDevice;
    e.m_state = share_state_available;
    return e;
}

//构造一条本机记录
ShareLocalRecord LocalShare(int64_t shareId, const std::wstring& root)
{
    ShareLocalRecord r;
    r.m_shareId = shareId;
    r.m_clientShareId = "client-" + std::to_string(static_cast<long long>(shareId));
    r.m_rootPath = root;
    r.m_kind = share_kind_folder;
    return r;
}

} // namespace

TEST_CASE("SRC1 服务端有本机有且绑定本机时报告", "[sharereconcile]")
{
    std::vector<ShareListEntry> server;
    server.push_back(ServerShare(101, true));
    std::vector<ShareLocalRecord> local;
    local.push_back(LocalShare(101, L"D:\\Projects"));

    const ShareReconcilePlan plan = ShareReconcile::Plan(server, local);
    REQUIRE(plan.m_report.size() == 1);
    CHECK(plan.m_report[0].m_shareId == 101);
    CHECK(plan.m_report[0].m_rootPath == L"D:\\Projects");
    CHECK(plan.m_missingLocal.empty());
    CHECK(plan.m_removeLocal.empty());
    CHECK(plan.m_otherDevice.empty());

    //共享根可用：available 为真、原因为 0
    const ShareServeItem ok = ShareReconcile::MakeServeItem(101, true);
    CHECK(ok.m_shareId == 101);
    CHECK(ok.m_available);
    CHECK(ok.m_reason == 0);
    //共享根不存在：原因为「共享根不存在」
    const ShareServeItem missing = ShareReconcile::MakeServeItem(101, false);
    CHECK_FALSE(missing.m_available);
    CHECK(missing.m_reason == share_unavailable_root_missing);
}

TEST_CASE("SRC2 绑定本机但本机没有记录时标注缺少记录", "[sharereconcile]")
{
    std::vector<ShareListEntry> server;
    server.push_back(ServerShare(202, true));
    //本机没有任何记录（例如个人目录被清空过）
    std::vector<ShareLocalRecord> local;

    const ShareReconcilePlan plan = ShareReconcile::Plan(server, local);
    CHECK(plan.m_report.empty());
    REQUIRE(plan.m_missingLocal.size() == 1);
    CHECK(plan.m_missingLocal[0] == 202);
    CHECK(plan.m_removeLocal.empty());
    CHECK(plan.m_otherDevice.empty());
}

TEST_CASE("SRC3 服务端已没有时删除本机记录且跳过未拿到 shareid 的记录", "[sharereconcile]")
{
    std::vector<ShareListEntry> server;
    std::vector<ShareLocalRecord> local;
    local.push_back(LocalShare(303, L"D:\\Old"));
    //发起后应答尚未回来：shareid 为 0
    ShareLocalRecord pending = LocalShare(0, L"D:\\Pending");
    pending.m_clientShareId = "pending-uuid";
    local.push_back(pending);

    const ShareReconcilePlan plan = ShareReconcile::Plan(server, local);
    REQUIRE(plan.m_removeLocal.size() == 1);
    CHECK(plan.m_removeLocal[0] == 303);
    CHECK(plan.m_report.empty());
    CHECK(plan.m_missingLocal.empty());
    CHECK(plan.m_otherDevice.empty());
}

TEST_CASE("SRC4 绑定别的设备时不报告也不删除本机记录", "[sharereconcile]")
{
    std::vector<ShareListEntry> server;
    server.push_back(ServerShare(404, false));
    server.push_back(ServerShare(405, false));
    //本机恰好也有 404 的记录（例如设备标识被重新生成过）
    std::vector<ShareLocalRecord> local;
    local.push_back(LocalShare(404, L"E:\\Media"));

    const ShareReconcilePlan plan = ShareReconcile::Plan(server, local);
    REQUIRE(plan.m_otherDevice.size() == 2);
    CHECK(plan.m_otherDevice[0] == 404);
    CHECK(plan.m_otherDevice[1] == 405);
    CHECK(plan.m_report.empty());
    CHECK(plan.m_removeLocal.empty());
    CHECK(plan.m_missingLocal.empty());
}

TEST_CASE("SRC5 四种情况混合时各列表互不重叠且保持输入顺序", "[sharereconcile]")
{
    std::vector<ShareListEntry> server;
    server.push_back(ServerShare(3, true));    //报告
    server.push_back(ServerShare(1, true));    //报告
    server.push_back(ServerShare(7, true));    //本机缺少记录
    server.push_back(ServerShare(8, false));   //别的设备
    server.push_back(ServerShare(3, true));    //重复项，只按第一次处理

    std::vector<ShareLocalRecord> local;
    local.push_back(LocalShare(1, L"D:\\A"));
    local.push_back(LocalShare(9, L"D:\\Gone1"));   //删除
    local.push_back(LocalShare(3, L"D:\\B"));
    local.push_back(LocalShare(5, L"D:\\Gone2"));   //删除
    local.push_back(LocalShare(9, L"D:\\Gone1"));   //本机重复的记录只删一次

    const ShareReconcilePlan plan = ShareReconcile::Plan(server, local);
    REQUIRE(plan.m_report.size() == 2);
    CHECK(plan.m_report[0].m_shareId == 3);
    CHECK(plan.m_report[0].m_rootPath == L"D:\\B");
    CHECK(plan.m_report[1].m_shareId == 1);
    REQUIRE(plan.m_missingLocal.size() == 1);
    CHECK(plan.m_missingLocal[0] == 7);
    REQUIRE(plan.m_otherDevice.size() == 1);
    CHECK(plan.m_otherDevice[0] == 8);
    REQUIRE(plan.m_removeLocal.size() == 2);
    CHECK(plan.m_removeLocal[0] == 9);
    CHECK(plan.m_removeLocal[1] == 5);
}
