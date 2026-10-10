/**
 *  文件共享功能开关的客户端判定与解析单元测试（Suite FSG，标签 [fileshareswitch]），FileShareGateTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 3 期第 1 步）：
 *    - FSG1 模块动作的判定：已登录且开关打开时激活（已激活则不动）；退出登录或开关关闭时收起（未激活则不动）；
 *    - FSG2 登录应答里的 features：有 fileshare=1 时开启；为 0、不是整数、没有这个字段（老服务端）时一律关闭；
 *    - FSG3 1013 包体：与登录应答的 features 同构；非法 JSON 解析失败。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <string>

#include "../Feature/FileShare/FileShareGate.h"
#include "../net/LoginProtocolClient.h"

using namespace balloonfileshare;

TEST_CASE("FSG1 module action decided by login and switch", "[fileshareswitch]")
{
    //已登录、开关打开：未激活则激活，已激活则不动
    CHECK(FileShareGateAction(true, true, false) == file_share_gate_activate);
    CHECK(FileShareGateAction(true, true, true) == file_share_gate_none);
    //开关关闭：已激活则收起，未激活则不动
    CHECK(FileShareGateAction(true, false, true) == file_share_gate_deactivate);
    CHECK(FileShareGateAction(true, false, false) == file_share_gate_none);
    //未登录：无论开关如何都不激活，已激活则收起
    CHECK(FileShareGateAction(false, true, false) == file_share_gate_none);
    CHECK(FileShareGateAction(false, true, true) == file_share_gate_deactivate);
    CHECK(FileShareGateAction(false, false, false) == file_share_gate_none);
}

TEST_CASE("FSG2 features in the login response", "[fileshareswitch]")
{
    const std::string base = "{\"code\":0,\"msg\":\"ok\",\"userid\":\"8\",\"servertime\":1,\"profile\":{\"enterpriseid\":\"7\"}";
    net::LoginResponse on;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(base + ",\"features\":{\"fileshare\":1}}", on));
    CHECK(net::LoginProtocolClient::isFeatureOn(on.features, net::kFeatureFileShare));

    net::LoginResponse off;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(base + ",\"features\":{\"fileshare\":0}}", off));
    CHECK_FALSE(net::LoginProtocolClient::isFeatureOn(off.features, net::kFeatureFileShare));

    //不是整数的项略去
    net::LoginResponse odd;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(base + ",\"features\":{\"fileshare\":\"1\"}}", odd));
    CHECK(odd.features.empty());
    CHECK_FALSE(net::LoginProtocolClient::isFeatureOn(odd.features, net::kFeatureFileShare));

    //老服务端：没有 features 字段，视同全部关闭
    net::LoginResponse old;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(base + "}", old));
    CHECK(old.features.empty());
    CHECK_FALSE(net::LoginProtocolClient::isFeatureOn(old.features, net::kFeatureFileShare));
    CHECK_FALSE(net::LoginProtocolClient::isFeatureOn(old.features, NULL));
}

TEST_CASE("FSG3 features notify body", "[fileshareswitch]")
{
    net::FeaturesNotify notify;
    REQUIRE(net::LoginProtocolClient::parseFeaturesNotify("{\"fileshare\":0}", notify));
    REQUIRE(notify.m_features.size() == 1);
    CHECK(notify.m_features["fileshare"] == 0);
    REQUIRE(net::LoginProtocolClient::parseFeaturesNotify("{\"fileshare\":1,\"future\":1}", notify));
    CHECK(net::LoginProtocolClient::isFeatureOn(notify.m_features, net::kFeatureFileShare));
    CHECK(notify.m_features.size() == 2);

    net::FeaturesNotify bad;
    CHECK_FALSE(net::LoginProtocolClient::parseFeaturesNotify("not json", bad));
    CHECK_FALSE(net::LoginProtocolClient::parseFeaturesNotify("[1]", bad));
}
