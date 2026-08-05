/**
 *  传输信令归属登记表单元测试（Suite TOR），TransferOwnerRegistryTests.cpp
 *  覆盖：register→take 命中并移除、take 不存在返回 false、同一 seq 重复登记以最后
 *       一次为准、超容量上限时淘汰最旧（seq 最小）条目。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-05
 */
#include "catch_amalgamated.hpp"
#include "../net/TransferOwnerRegistry.h"

using namespace net;

TEST_CASE("TOR1 register 后 take 命中并移除")
{
    TransferOwnerRegistry reg;
    reg.registerSeq(100, transfer_owner_image);
    reg.registerSeq(101, transfer_owner_file);
    CHECK(reg.size() == 2);

    int owner = 0;
    REQUIRE(reg.takeOwner(100, owner));
    CHECK(owner == transfer_owner_image);
    //取走后即移除：size 减一，再取同一 seq 落空
    CHECK(reg.size() == 1);
    CHECK_FALSE(reg.takeOwner(100, owner));

    REQUIRE(reg.takeOwner(101, owner));
    CHECK(owner == transfer_owner_file);
    CHECK(reg.size() == 0);
}

TEST_CASE("TOR2 take 不存在的 seq 返回 false 且不改写出参")
{
    TransferOwnerRegistry reg;
    int owner = -7;
    CHECK_FALSE(reg.takeOwner(999, owner));
    //未命中不改写出参，仍为调用前的值
    CHECK(owner == -7);
}

TEST_CASE("TOR3 同一 seq 重复登记以最后一次为准")
{
    TransferOwnerRegistry reg;
    reg.registerSeq(50, transfer_owner_image);
    reg.registerSeq(50, transfer_owner_file);
    CHECK(reg.size() == 1);
    int owner = 0;
    REQUIRE(reg.takeOwner(50, owner));
    CHECK(owner == transfer_owner_file);
}

TEST_CASE("TOR4 超容量上限时淘汰最旧（seq 最小）的条目")
{
    TransferOwnerRegistry reg;
    //连灌 1024+5 条（1024 须与 TransferOwnerRegistry::kMaxEntries 保持一致）：
    //超过上限后只保留最新的 1024 条，最旧 5 条被淘汰。
    const uint32_t total = 1024 + 5;
    for (uint32_t s = 1; s <= total; ++s)
        reg.registerSeq(s, transfer_owner_file);
    CHECK(reg.size() == 1024);

    //最旧的 seq 1..5 已被淘汰，取不到
    int owner = 0;
    for (uint32_t s = 1; s <= 5; ++s)
        CHECK_FALSE(reg.takeOwner(s, owner));
    //仍在册的边界：seq 6 是最旧的保留项，seq total 是最新项
    CHECK(reg.takeOwner(6, owner));
    CHECK(reg.takeOwner(total, owner));
}
