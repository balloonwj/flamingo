/**
 *  StarredModelTests.cpp
 *
 *  「特别关注」内存模型（StarredModel）与其协议编解码（StarredProtocolClient）、
 *  以及通讯录应答里 starred 名单解析（ContactsProtocolClient）的测试：
 *    · 名单的判定、增删、整体替换，以及乐观更新失败后的回滚。
 *    · 右键菜单文案随关注状态取反（四处菜单共用这一份措辞）。
 *    · 导出名单写回缓存 —— 少了这一步，乐观更新会被上一次应答里的旧名单覆盖。
 *    · cmd_set_starred 请求构造与应答解析。
 *    · cmd_get_contacts 应答里的 starred 扁平数组解析与容错。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cwchar>          // wcscmp —— 比对中文菜单文案

#include "../UI/BalloonMain/StarredModel.h"
#include "../net/StarredProtocolClient.h"
#include "../net/ContactsProtocolClient.h"
#include "../net/Protocol.h"   // error_code_ok / error_code_starred_limit

using namespace balloonmain;

namespace {

//几个测试里反复出现的 userid。取值刻意不连续，免得下标与 id 混起来看不出错。
const int64_t kUidA = 100007;
const int64_t kUidB = 100031;
const int64_t kUidC = 268435456LL;   //超过 32 位的大 id，用来盯住 JSON 精度

} // anonymous namespace

TEST_CASE("STM-C1 空模型里谁都不算被关注", "[starred]")
{
    StarredModel m;
    CHECK(m.Count() == 0);
    CHECK_FALSE(m.IsStarred(kUidA));

    //非法 id 一律返回 false，而不是让调用方自己先判一遍
    CHECK_FALSE(m.IsStarred(0));
    CHECK_FALSE(m.IsStarred(-1));
}

TEST_CASE("STM-C2 标记 / 取消是幂等的", "[starred]")
{
    StarredModel m;

    m.SetStarred(kUidA, true);
    CHECK(m.IsStarred(kUidA));
    CHECK(m.Count() == 1);

    //重复标记同一个人不该把人数加成 2
    m.SetStarred(kUidA, true);
    CHECK(m.Count() == 1);

    m.SetStarred(kUidA, false);
    CHECK_FALSE(m.IsStarred(kUidA));
    CHECK(m.Count() == 0);

    //取消一个本来就没标记的人不该出错、也不该把人数减成负数
    m.SetStarred(kUidB, false);
    CHECK(m.Count() == 0);

    //非法 id 什么都不做
    m.SetStarred(0, true);
    m.SetStarred(-5, true);
    CHECK(m.Count() == 0);
}

TEST_CASE("STM-C3 Reset 整体替换，且丢掉脏数据", "[starred]")
{
    StarredModel m;
    m.SetStarred(kUidA, true);

    //服务端那份是权威：Reset 之后 A 不在名单里就该消失，本地乐观更新的中间态作废
    std::vector<int64_t> fresh;
    fresh.push_back(kUidB);
    fresh.push_back(kUidC);
    fresh.push_back(0);    //脏数据：应被跳过，而不是让整份名单作废
    fresh.push_back(-3);
    m.Reset(fresh);

    CHECK_FALSE(m.IsStarred(kUidA));
    CHECK(m.IsStarred(kUidB));
    CHECK(m.IsStarred(kUidC));
    CHECK(m.Count() == 2);

    //空名单也要能整体替换掉（服务端下发"一个都没有"）
    m.Reset(std::vector<int64_t>());
    CHECK(m.Count() == 0);
}

TEST_CASE("STM-C4 Clear 清空，退出登录时不留上一个账号的名单", "[starred]")
{
    StarredModel m;
    m.SetStarred(kUidA, true);
    m.SetStarred(kUidB, true);
    REQUIRE(m.Count() == 2);

    m.Clear();
    CHECK(m.Count() == 0);
    CHECK_FALSE(m.IsStarred(kUidA));
}

TEST_CASE("STM-C5 菜单文案随关注状态取反", "[starred]")
{
    //四处右键菜单（主面板联系人 / 主面板会话 / 群成员列表 / 气泡的 @片段与头像）
    //共用这一份措辞，改文案只该改这一处
    CHECK(::wcscmp(StarredModel::GetMenuLabel(false), L"特别关注") == 0);
    CHECK(::wcscmp(StarredModel::GetMenuLabel(true), L"取消特别关注") == 0);
}

TEST_CASE("STM-C6 导出名单写回缓存", "[starred]")
{
    StarredModel m;
    m.SetStarred(kUidB, true);
    m.SetStarred(kUidA, true);

    std::vector<int64_t> out;
    out.push_back(999);   //导出前的内容必须被清掉，不能追加
    m.ExportItems(out);

    REQUIRE(out.size() == 2);
    //导出顺序是 userid 升序，与服务端按标记时间倒序下发的顺序不同 —— 这不要紧，
    //名单只用来判定"这个人关注了没有"，没有按序展示的地方。
    CHECK(out[0] == kUidA);
    CHECK(out[1] == kUidB);

    //导出的名单能原样灌回去，两边一致
    StarredModel m2;
    m2.Reset(out);
    CHECK(m2.IsStarred(kUidA));
    CHECK(m2.IsStarred(kUidB));
    CHECK(m2.Count() == 2);
}

TEST_CASE("STM-C7 应答失败时按回带的目标状态回滚", "[starred]")
{
    //复现主面板 ToggleStarred → OnSetStarredResult 的那条路：
    //先乐观更新，应答失败后把这一条设成相反值即完成回滚。
    StarredModel m;

    const bool want = !m.IsStarred(kUidA);   //本次意图：标记
    m.SetStarred(kUidA, want);
    REQUIRE(m.IsStarred(kUidA));

    net::SetStarredResult res;
    REQUIRE(net::StarredProtocolClient::parseSetStarredResponse(
        "{\"code\":210,\"msg\":\"limit\",\"target\":\"100007\",\"starred\":1}", res));
    REQUIRE(res.m_code == error_code_starred_limit);

    m.SetStarred(res.m_target, !(res.m_starred != 0));
    CHECK_FALSE(m.IsStarred(kUidA));   //回到点之前的样子
}

TEST_CASE("STM-C8 请求构造：target 走字符串，starred 走整数 0/1", "[starred][protocol]")
{
    const std::string reqAdd =
        net::StarredProtocolClient::buildSetStarredRequest(kUidA, true);

    //target 必须是字符串（大整数走 JSON number 会丢精度），starred 必须是整数 ——
    //服务端 parseSetStarredRequest 读的是 isInt()，写成 true/false 会被判非法。
    CHECK(reqAdd.find("\"target\":\"100007\"") != std::string::npos);
    CHECK(reqAdd.find("\"starred\":1") != std::string::npos);
    CHECK(reqAdd.find("true") == std::string::npos);

    const std::string reqDel =
        net::StarredProtocolClient::buildSetStarredRequest(kUidC, false);
    CHECK(reqDel.find("\"target\":\"268435456\"") != std::string::npos);
    CHECK(reqDel.find("\"starred\":0") != std::string::npos);
}

TEST_CASE("STM-C9 应答解析：成功与失败都回带 target / starred", "[starred][protocol]")
{
    net::SetStarredResult res;

    REQUIRE(net::StarredProtocolClient::parseSetStarredResponse(
        "{\"code\":0,\"msg\":\"ok\",\"target\":\"268435456\",\"starred\":1}", res));
    CHECK(res.m_code == error_code_ok);
    CHECK(res.m_msg == "ok");
    CHECK(res.m_target == kUidC);
    CHECK(res.m_starred == 1);

    //失败应答同样要能对上号，否则乐观更新没法正确回滚
    REQUIRE(net::StarredProtocolClient::parseSetStarredResponse(
        "{\"code\":210,\"msg\":\"limit\",\"target\":\"100007\",\"starred\":0}", res));
    CHECK(res.m_code == error_code_starred_limit);
    CHECK(res.m_target == kUidA);
    CHECK(res.m_starred == 0);
}

TEST_CASE("STM-C10 应答解析的容错", "[starred][protocol]")
{
    net::SetStarredResult res;

    //JSON 非法 / 不是对象 / 缺 code：一律判失败，调用方丢弃
    CHECK_FALSE(net::StarredProtocolClient::parseSetStarredResponse("", res));
    CHECK_FALSE(net::StarredProtocolClient::parseSetStarredResponse("not json", res));
    CHECK_FALSE(net::StarredProtocolClient::parseSetStarredResponse("[1,2]", res));
    CHECK_FALSE(net::StarredProtocolClient::parseSetStarredResponse(
        "{\"msg\":\"ok\"}", res));

    //只有 code 也算可用 —— 至少知道成没成，回带两项缺失时取 0
    REQUIRE(net::StarredProtocolClient::parseSetStarredResponse("{\"code\":0}", res));
    CHECK(res.m_code == error_code_ok);
    CHECK(res.m_target == 0);
    CHECK(res.m_starred == 0);
}

TEST_CASE("STM-C11 通讯录应答里的 starred 名单解析", "[starred][protocol]")
{
    //名单是扁平的字符串数组（只装人，不像 favorites 那样带 ctype），
    //服务端已按标记时间倒序排好，本端保持原序。
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":\"1\",\"enterprisename\":\"测试\","
        "\"departments\":[],\"contacts\":[],"
        "\"starred\":[\"100031\",\"100007\",\"268435456\"]}";

    net::ContactsResult res;
    REQUIRE(net::ContactsProtocolClient::parseContactsResponse(body, res));
    REQUIRE(res.m_starred.size() == 3);
    CHECK(res.m_starred[0] == kUidB);
    CHECK(res.m_starred[1] == kUidA);
    CHECK(res.m_starred[2] == kUidC);   //大 id 不能在解析途中丢精度
}

TEST_CASE("STM-C12 starred 字段缺失 / 混进脏数据都不该让整条应答失败", "[starred][protocol]")
{
    //老服务端不带这个字段：合法情形，名单为空、不会弹任何上下线提示
    {
        const std::string body =
            "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":\"1\",\"enterprisename\":\"测试\","
            "\"departments\":[],\"contacts\":[]}";
        net::ContactsResult res;
        REQUIRE(net::ContactsProtocolClient::parseContactsResponse(body, res));
        CHECK(res.m_starred.empty());
    }

    //混进一条脏数据只跳过那一条，不该连整个通讯录都收不到
    {
        const std::string body =
            "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":\"1\",\"enterprisename\":\"测试\","
            "\"departments\":[],\"contacts\":[],"
            "\"starred\":[\"100007\",\"0\",\"-3\",\"abc\",\"100031\"]}";
        net::ContactsResult res;
        REQUIRE(net::ContactsProtocolClient::parseContactsResponse(body, res));
        REQUIRE(res.m_starred.size() == 2);
        CHECK(res.m_starred[0] == kUidA);
        CHECK(res.m_starred[1] == kUidB);
    }
}
