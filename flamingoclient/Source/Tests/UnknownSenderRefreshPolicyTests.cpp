/**
 *  未知发送方触发通讯录刷新的节流策略单元测试（Suite UR），UnknownSenderRefreshPolicyTests.cpp
 *
 *  背景：企业版的通讯录只在登录时拉一次，之后新注册 / 新入职的同事不会自动出现。
 *  收到这类"不认识的人"发来的消息时，本端要补拉一次通讯录才能把名字显示出来，
 *  否则界面上就是"用户<userid>"。本策略负责决定"补拉"这个动作什么时候真的发生，
 *  免得一串陌生消息把通讯录请求打成风暴。
 *
 *  覆盖：首次触发、同一发送方去重、冷却期内换人、冷却期满、刷新后仍未知的账号、
 *       时钟回退、非法 userid、Reset 语义。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-11
 */
#include "catch_amalgamated.hpp"
#include "../BalloonMain/UnknownSenderRefreshPolicy.h"

using namespace balloonmain;

//测试里用的基准时刻。取一个非 0 的值，免得跟"还没刷过"的初值 0 混淆。
static const int64_t kBaseMs = 1000000;

TEST_CASE("UR1 首次收到未知发送方的消息：应当刷新通讯录", "[unknownsender]")
{
    UnknownSenderRefreshPolicy policy;
    CHECK(policy.ShouldRefreshContacts(5320, kBaseMs));
}

TEST_CASE("UR2 同一个未知发送方连发多条：只刷一次", "[unknownsender]")
{
    //新同事上来连打三句话，不该换来三次通讯录请求。
    UnknownSenderRefreshPolicy policy;
    REQUIRE(policy.ShouldRefreshContacts(5320, kBaseMs));
    CHECK_FALSE(policy.ShouldRefreshContacts(5320, kBaseMs + 1));
    CHECK_FALSE(policy.ShouldRefreshContacts(5320, kBaseMs + 100));
}

TEST_CASE("UR3 冷却期内换一个未知发送方：仍然不刷", "[unknownsender]")
{
    //两个新同事前后脚说话。第一次刷新的应答会把两人都带回来，所以第二个人
    //不必再触发一次请求。
    UnknownSenderRefreshPolicy policy;
    REQUIRE(policy.ShouldRefreshContacts(5320, kBaseMs));
    CHECK_FALSE(policy.ShouldRefreshContacts(5321, kBaseMs + 1));
    CHECK_FALSE(policy.ShouldRefreshContacts(5322, kBaseMs + kContactsRefreshCooldownMs - 1));
}

TEST_CASE("UR4 冷却期满后遇到新的未知发送方：允许再刷一次", "[unknownsender]")
{
    UnknownSenderRefreshPolicy policy;
    REQUIRE(policy.ShouldRefreshContacts(5320, kBaseMs));
    CHECK(policy.ShouldRefreshContacts(5321, kBaseMs + kContactsRefreshCooldownMs));
}

TEST_CASE("UR5 刷新之后仍然不认识的发送方：不再重复刷", "[unknownsender]")
{
    //典型情形是账号已被管理台删除、但历史会话还在：拉多少次通讯录都不会有他。
    //这种人必须只放行一次，否则他每发一条消息就是一次全量通讯录请求。
    UnknownSenderRefreshPolicy policy;
    REQUIRE(policy.ShouldRefreshContacts(5320, kBaseMs));

    //冷却早就过了，但这个 userid 已经问过一次，仍然不放行。
    CHECK_FALSE(policy.ShouldRefreshContacts(5320, kBaseMs + kContactsRefreshCooldownMs * 100));
}

TEST_CASE("UR6 时钟回退：冷却立即作废，策略不被卡死", "[unknownsender]")
{
    //调用方若用了会回绕的 32 位 tick、或者用户改了系统时间，传进来的时刻会变小。
    //此时"now - last"是负数，若不处理就会被当成"还在冷却中"而永远不刷。
    UnknownSenderRefreshPolicy policy;
    REQUIRE(policy.ShouldRefreshContacts(5320, kBaseMs));
    CHECK(policy.ShouldRefreshContacts(5321, kBaseMs - 500000));
}

TEST_CASE("UR7 非法 userid：一律不刷", "[unknownsender]")
{
    //发送方ID都没有的消息，补拉通讯录也补不出名字。
    UnknownSenderRefreshPolicy policy;
    CHECK_FALSE(policy.ShouldRefreshContacts(0, kBaseMs));
    CHECK_FALSE(policy.ShouldRefreshContacts(-1, kBaseMs));
    //非法ID不该占用冷却额度：紧接着来的合法陌生人照样要能触发刷新。
    CHECK(policy.ShouldRefreshContacts(5320, kBaseMs));
}

TEST_CASE("UR8 Reset 后一切重来", "[unknownsender]")
{
    //换账号登录时调用：上一个账号攒下的"这些人问过了"不再作数。
    UnknownSenderRefreshPolicy policy;
    REQUIRE(policy.ShouldRefreshContacts(5320, kBaseMs));
    REQUIRE_FALSE(policy.ShouldRefreshContacts(5320, kBaseMs + 1));

    policy.Reset();
    CHECK(policy.ShouldRefreshContacts(5320, kBaseMs + 2));
}
