/**
 *  管理控制台长连接报文与规则单元测试（Suite ADL），test_adminlink.cpp
 *  覆盖 docs/FlamingoLink方案.md 第 9.1 节的服务端用例：
 *    - ADL1 2501 鉴权请求解析：正常；缺 token、token 非字符串、为空、超长、非 JSON 均失败；
 *    - ADL2 2501 应答：成功时带字符串 ID、心跳与超时；失败时只有 code 与 msg；
 *    - ADL3 2502 订阅请求解析：企业 ID 为十进制字符串；缺失、非字符串、非数字、为 0 均失败；
 *    - ADL4 订阅权限：超管任意存在的企业通过；企业管理员本企业通过、别的企业 1101（先于存在性）；不存在 1102；
 *    - ADL5 按用户归并设备：多设备合并且按类型排序；注销的、userid 为 0 的会话不计；无会话的用户设备为空；
 *    - ADL6 资料：账号、昵称、部门取自组织架构，主部门在前、同部门不重复；查不到的用户资料为空；
 *    - ADL7 分帧：2500 人每帧 1000 人分 3 帧、只有首帧 reset=1；空的完整名单发一帧、空的变化推送不发；
 *    - ADL8 来源地址去端口；
 *    - ADL9 空闲与未鉴权超时的边界；
 *    - ADL10 login.do 的 linkport / linkpubkey 取值。
 *  典型用法：Tests.exe --test-case="ADL*"
 *  balloonwj@qq.com   2026-10-04
 */
#include <string>
#include <vector>
#include <map>

#include "doctest.h"
#include "../chatserversrc/AdminLinkProtocol.h"
#include "../chatserversrc/DepartmentMsg.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//把 JSON 文本解析成对象，失败时让用例失败
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

//示例里的管理员与企业 ID
const int64_t kAdminId = 7;
const int64_t kEnterpriseA = 123;
const int64_t kEnterpriseB = 456;

//示例里的员工 ID
const int64_t kUserZhang = 10001;
const int64_t kUserLi = 10002;
const int64_t kUserWang = 10003;

//示例里的登录时间（Unix 毫秒）
const int64_t kLoginTimePc = 1759550000000LL;
const int64_t kLoginTimeAndroid = 1759551000000LL;

//ADL7 的名单人数与每帧人数，以及据此应分的帧数
const size_t kBigListUsers = 2500;
const size_t kUsersPerFrame = 1000;
const size_t kExpectedFrames = 3;

//ADL9 的示例时间（Unix 秒）
const int64_t kConnectSec = 1000;

//ADL10 的示例端口
const int kLinkPort = 20006;

//构造一条会话事实
AdminLinkSessionFact makeFact(int64_t userid, bool loggedIn, int32_t clienttype, int32_t presence,
                              int64_t logintime, const char* ip)
{
    AdminLinkSessionFact f;
    f.userid = userid;
    f.loggedIn = loggedIn;
    f.clienttype = clienttype;
    f.presence = presence;
    f.logintime = logintime;
    f.ip = ip;
    return f;
}

//构造一个部门成员
dept::DepartmentMember makeMember(int64_t userid, const char* account, const char* nickname, int32_t isprimary)
{
    dept::DepartmentMember m;
    m.userid = userid;
    m.username = account;
    m.nickname = nickname;
    m.isprimary = isprimary;
    return m;
}

} // namespace

TEST_CASE("ADL1 2501 鉴权请求解析")
{
    std::string token;
    std::string ver;

    CHECK(AdminLinkProtocol::parseAuthRequest("{\"token\":\"abcdef0123456789\",\"clientversion\":\"1.0.0\"}", token, ver));
    CHECK(token == "abcdef0123456789");
    CHECK(ver == "1.0.0");

    //clientversion 可省略
    CHECK(AdminLinkProtocol::parseAuthRequest("{\"token\":\"t1\"}", token, ver));
    CHECK(token == "t1");
    CHECK(ver.empty());

    CHECK_FALSE(AdminLinkProtocol::parseAuthRequest("{}", token, ver));
    CHECK_FALSE(AdminLinkProtocol::parseAuthRequest("{\"token\":123}", token, ver));
    CHECK_FALSE(AdminLinkProtocol::parseAuthRequest("{\"token\":\"\"}", token, ver));
    CHECK_FALSE(AdminLinkProtocol::parseAuthRequest("not json", token, ver));
    CHECK_FALSE(AdminLinkProtocol::parseAuthRequest("[\"token\"]", token, ver));

    //超长 token 按格式错误拒绝
    std::string longToken(kAdminLinkTokenMaxBytes + 1, 'a');
    CHECK_FALSE(AdminLinkProtocol::parseAuthRequest("{\"token\":\"" + longToken + "\"}", token, ver));
    std::string maxToken(kAdminLinkTokenMaxBytes, 'a');
    CHECK(AdminLinkProtocol::parseAuthRequest("{\"token\":\"" + maxToken + "\"}", token, ver));
}

TEST_CASE("ADL2 2501 应答组装")
{
    Json::Value ok = parseJson(AdminLinkProtocol::buildAuthResponse(error_code_ok, "ok", kAdminId, 0));
    CHECK(ok["code"].asInt() == error_code_ok);
    CHECK(ok["msg"].asString() == "ok");
    REQUIRE(ok["adminid"].isString());
    CHECK(ok["adminid"].asString() == "7");
    REQUIRE(ok["enterpriseid"].isString());
    CHECK(ok["enterpriseid"].asString() == "0");
    CHECK(ok["heartbeat"].asInt() == kAdminLinkHeartbeatSec);
    CHECK(ok["timeout"].asInt() == kAdminLinkIdleTimeoutSec);

    Json::Value bad = parseJson(AdminLinkProtocol::buildAuthResponse(error_code_adminlink_token_invalid,
                                                                     "token invalid", kAdminId, kEnterpriseA));
    CHECK(bad["code"].asInt() == error_code_adminlink_token_invalid);
    CHECK_FALSE(bad.isMember("adminid"));
    CHECK_FALSE(bad.isMember("heartbeat"));
}

TEST_CASE("ADL3 2502 订阅请求解析")
{
    int64_t ent = -1;
    CHECK(AdminLinkProtocol::parseSubscribeRequest("{\"enterpriseid\":\"123\"}", ent));
    CHECK(ent == kEnterpriseA);

    CHECK_FALSE(AdminLinkProtocol::parseSubscribeRequest("{}", ent));
    CHECK(ent == 0);
    CHECK_FALSE(AdminLinkProtocol::parseSubscribeRequest("{\"enterpriseid\":123}", ent));
    CHECK_FALSE(AdminLinkProtocol::parseSubscribeRequest("{\"enterpriseid\":\"\"}", ent));
    CHECK_FALSE(AdminLinkProtocol::parseSubscribeRequest("{\"enterpriseid\":\"12a\"}", ent));
    CHECK_FALSE(AdminLinkProtocol::parseSubscribeRequest("{\"enterpriseid\":\"-5\"}", ent));
    CHECK_FALSE(AdminLinkProtocol::parseSubscribeRequest("{\"enterpriseid\":\"0\"}", ent));
    CHECK_FALSE(AdminLinkProtocol::parseSubscribeRequest("oops", ent));

    Json::Value resp = parseJson(AdminLinkProtocol::buildSubscribeResponse(error_code_ok, "ok", kEnterpriseA));
    CHECK(resp["code"].asInt() == error_code_ok);
    CHECK(resp["enterpriseid"].asString() == "123");

    Json::Value fail = parseJson(AdminLinkProtocol::buildSubscribeResponse(error_code_adminlink_forbidden,
                                                                           "subscribe rejected", kEnterpriseA));
    CHECK(fail["code"].asInt() == error_code_adminlink_forbidden);
    CHECK_FALSE(fail.isMember("enterpriseid"));

    Json::Value simple = parseJson(AdminLinkProtocol::buildSimpleResponse(error_code_ok, "ok"));
    CHECK(simple["code"].asInt() == error_code_ok);
    CHECK(simple["msg"].asString() == "ok");
}

TEST_CASE("ADL4 订阅权限")
{
    //平台超管：存在的企业都能看，不存在报 1102
    CHECK(AdminLinkProtocol::checkSubscribePermission(0, kEnterpriseA, true) == error_code_ok);
    CHECK(AdminLinkProtocol::checkSubscribePermission(0, kEnterpriseB, true) == error_code_ok);
    CHECK(AdminLinkProtocol::checkSubscribePermission(0, kEnterpriseA, false)
          == error_code_adminlink_enterprise_not_exist);

    //企业管理员：本企业通过，别的企业不论存在与否都是 1101
    CHECK(AdminLinkProtocol::checkSubscribePermission(kEnterpriseA, kEnterpriseA, true) == error_code_ok);
    CHECK(AdminLinkProtocol::checkSubscribePermission(kEnterpriseA, kEnterpriseB, true)
          == error_code_adminlink_forbidden);
    CHECK(AdminLinkProtocol::checkSubscribePermission(kEnterpriseA, kEnterpriseB, false)
          == error_code_adminlink_forbidden);

    //企业管理员自己的企业被删了：1102
    CHECK(AdminLinkProtocol::checkSubscribePermission(kEnterpriseA, kEnterpriseA, false)
          == error_code_adminlink_enterprise_not_exist);
}

TEST_CASE("ADL5 按用户归并设备")
{
    std::vector<AdminLinkSessionFact> facts;
    //张三：Android 先列、PC 后列，归并后应按 clienttype 升序
    facts.push_back(makeFact(kUserZhang, true, platform_android, presence_away, kLoginTimeAndroid, "10.0.0.8"));
    facts.push_back(makeFact(kUserZhang, true, platform_pc, presence_online, kLoginTimePc, "192.168.1.10"));
    //李四：只有一个已注销的会话，不计入
    facts.push_back(makeFact(kUserLi, false, platform_pc, presence_online, kLoginTimePc, "192.168.1.11"));
    //被置为失效的会话（userid 为 0）不计入
    facts.push_back(makeFact(0, true, platform_pc, presence_online, kLoginTimePc, "192.168.1.12"));

    std::vector<int64_t> online;
    AdminLinkProtocol::collectOnlineUserIds(facts, online);
    REQUIRE(online.size() == 1);
    CHECK(online[0] == kUserZhang);

    std::map<int64_t, AdminLinkUserProfile> profiles;
    std::vector<int64_t> ids;
    ids.push_back(kUserZhang);
    ids.push_back(kUserLi);
    ids.push_back(kUserZhang);   //重复的只出一条
    std::vector<AdminLinkUser> users;
    AdminLinkProtocol::buildUsers(ids, facts, profiles, users);

    REQUIRE(users.size() == 2);
    CHECK(users[0].userid == kUserZhang);
    REQUIRE(users[0].devices.size() == 2);
    CHECK(users[0].devices[0].clienttype == platform_pc);
    CHECK(users[0].devices[0].presence == presence_online);
    CHECK(users[0].devices[0].logintime == kLoginTimePc);
    CHECK(users[0].devices[0].ip == "192.168.1.10");
    CHECK(users[0].devices[1].clienttype == platform_android);
    CHECK(users[0].devices[1].presence == presence_away);

    //李四没有登录态的会话：条目照出，设备为空，表示离线
    CHECK(users[1].userid == kUserLi);
    CHECK(users[1].devices.empty());

    //别的用户的会话不会混进来
    std::vector<int64_t> onlyWang;
    onlyWang.push_back(kUserWang);
    AdminLinkProtocol::buildUsers(onlyWang, facts, profiles, users);
    REQUIRE(users.size() == 1);
    CHECK(users[0].devices.empty());
}

TEST_CASE("ADL6 资料取自组织架构")
{
    std::vector<dept::DepartmentInfo> depts(2);
    depts[0].deptid = 1;
    depts[0].deptname = "研发部";
    depts[0].members.push_back(makeMember(kUserZhang, "zhangsan", "张三", 0));
    depts[0].members.push_back(makeMember(kUserLi, "lisi", "李四", 1));
    depts[1].deptid = 2;
    depts[1].deptname = "测试组";
    depts[1].members.push_back(makeMember(kUserZhang, "zhangsan", "张三", 1));

    std::map<int64_t, AdminLinkUserProfile> profiles;
    AdminLinkProtocol::buildProfilesFromOrg(depts, profiles);

    REQUIRE(profiles.count(kUserZhang) == 1);
    CHECK(profiles[kUserZhang].account == "zhangsan");
    CHECK(profiles[kUserZhang].nickname == "张三");
    //张三的主部门是测试组，虽然在组织架构里排在后面，也要放在最前
    REQUIRE(profiles[kUserZhang].depts.size() == 2);
    CHECK(profiles[kUserZhang].depts[0] == "测试组");
    CHECK(profiles[kUserZhang].depts[1] == "研发部");

    REQUIRE(profiles[kUserLi].depts.size() == 1);
    CHECK(profiles[kUserLi].depts[0] == "研发部");

    //资料并入名单条目；组织架构里没有的人资料为空
    std::vector<AdminLinkSessionFact> facts;
    facts.push_back(makeFact(kUserWang, true, platform_pc, presence_busy, kLoginTimePc, "1.2.3.4"));
    std::vector<int64_t> ids;
    ids.push_back(kUserZhang);
    ids.push_back(kUserWang);
    std::vector<AdminLinkUser> users;
    AdminLinkProtocol::buildUsers(ids, facts, profiles, users);
    REQUIRE(users.size() == 2);
    CHECK(users[0].account == "zhangsan");
    CHECK(users[0].depts.size() == 2);
    CHECK(users[1].account.empty());
    CHECK(users[1].depts.empty());
    REQUIRE(users[1].devices.size() == 1);
    CHECK(users[1].devices[0].presence == presence_busy);
}

TEST_CASE("ADL7 在线名单分帧")
{
    std::vector<AdminLinkUser> users(kBigListUsers);
    for (size_t i = 0; i < users.size(); ++i)
        users[i].userid = static_cast<int64_t>(kUserZhang + i);

    std::vector<std::string> bodies;
    AdminLinkProtocol::buildOnlineNotifyBodies(kEnterpriseA, true, users, kUsersPerFrame, bodies);
    REQUIRE(bodies.size() == kExpectedFrames);

    size_t total = 0;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        Json::Value root = parseJson(bodies[i]);
        CHECK(root["enterpriseid"].asString() == "123");
        CHECK(root["reset"].asInt() == ((i == 0) ? 1 : 0));
        REQUIRE(root["users"].isArray());
        total += root["users"].size();
    }
    CHECK(total == kBigListUsers);

    //变化推送：同样分帧，但没有一帧 reset=1
    AdminLinkProtocol::buildOnlineNotifyBodies(kEnterpriseA, false, users, kUsersPerFrame, bodies);
    REQUIRE(bodies.size() == kExpectedFrames);
    CHECK(parseJson(bodies[0])["reset"].asInt() == 0);

    //空名单：完整名单发一帧 reset=1、users 为空数组；变化推送不发
    std::vector<AdminLinkUser> none;
    AdminLinkProtocol::buildOnlineNotifyBodies(kEnterpriseA, true, none, kUsersPerFrame, bodies);
    REQUIRE(bodies.size() == 1);
    Json::Value empty = parseJson(bodies[0]);
    CHECK(empty["reset"].asInt() == 1);
    REQUIRE(empty["users"].isArray());
    CHECK(empty["users"].size() == 0);
    AdminLinkProtocol::buildOnlineNotifyBodies(kEnterpriseA, false, none, kUsersPerFrame, bodies);
    CHECK(bodies.empty());

    //字段：userid 为字符串，设备字段齐全
    std::vector<AdminLinkUser> one(1);
    one[0].userid = kUserZhang;
    one[0].account = "zhangsan";
    one[0].nickname = "张三";
    one[0].depts.push_back("研发部");
    AdminLinkDevice d;
    d.clienttype = platform_pc;
    d.presence = presence_online;
    d.logintime = kLoginTimePc;
    d.ip = "192.168.1.10";
    one[0].devices.push_back(d);
    AdminLinkProtocol::buildOnlineNotifyBodies(kEnterpriseA, false, one, kUsersPerFrame, bodies);
    REQUIRE(bodies.size() == 1);
    Json::Value u = parseJson(bodies[0])["users"][0];
    CHECK(u["userid"].asString() == "10001");
    CHECK(u["account"].asString() == "zhangsan");
    CHECK(u["nickname"].asString() == "张三");
    CHECK(u["depts"][0].asString() == "研发部");
    CHECK(u["devices"][0]["clienttype"].asInt() == platform_pc);
    CHECK(u["devices"][0]["presence"].asInt() == presence_online);
    CHECK(u["devices"][0]["logintime"].asInt64() == kLoginTimePc);
    CHECK(u["devices"][0]["ip"].asString() == "192.168.1.10");
}

TEST_CASE("ADL8 来源地址去端口")
{
    CHECK(AdminLinkProtocol::stripPort("1.2.3.4:5678") == "1.2.3.4");
    CHECK(AdminLinkProtocol::stripPort("1.2.3.4") == "1.2.3.4");
    CHECK(AdminLinkProtocol::stripPort("") == "");
}

TEST_CASE("ADL9 空闲与未鉴权超时")
{
    //空闲：差值不足超时不断开，恰好等于即断开
    CHECK_FALSE(AdminLinkProtocol::isIdleTimeout(kConnectSec + kAdminLinkIdleTimeoutSec - 1, kConnectSec,
                                                 kAdminLinkIdleTimeoutSec));
    CHECK(AdminLinkProtocol::isIdleTimeout(kConnectSec + kAdminLinkIdleTimeoutSec, kConnectSec,
                                           kAdminLinkIdleTimeoutSec));

    //未鉴权：已鉴权恒不超时；未鉴权到点即超时
    CHECK_FALSE(AdminLinkProtocol::isAuthTimeout(kConnectSec + kAdminLinkAuthTimeoutSec - 1, kConnectSec, false,
                                                 kAdminLinkAuthTimeoutSec));
    CHECK(AdminLinkProtocol::isAuthTimeout(kConnectSec + kAdminLinkAuthTimeoutSec, kConnectSec, false,
                                           kAdminLinkAuthTimeoutSec));
    CHECK_FALSE(AdminLinkProtocol::isAuthTimeout(kConnectSec + kAdminLinkAuthTimeoutSec * 100, kConnectSec, true,
                                                 kAdminLinkAuthTimeoutSec));
}

TEST_CASE("ADL10 login.do 的 linkport 与 linkpubkey")
{
    int port = -1;
    std::string pubkey = "stale";

    //监听且加密：端口与公钥都给
    AdminLinkProtocol::resolveLoginLinkInfo(true, kLinkPort, true, "QLcj=", port, pubkey);
    CHECK(port == kLinkPort);
    CHECK(pubkey == "QLcj=");

    //监听但未加密：公钥为空
    AdminLinkProtocol::resolveLoginLinkInfo(true, kLinkPort, false, "QLcj=", port, pubkey);
    CHECK(port == kLinkPort);
    CHECK(pubkey.empty());

    //未监听：端口 0、公钥为空
    AdminLinkProtocol::resolveLoginLinkInfo(false, kLinkPort, true, "QLcj=", port, pubkey);
    CHECK(port == 0);
    CHECK(pubkey.empty());

    //端口非法也按未开启处理
    AdminLinkProtocol::resolveLoginLinkInfo(true, 0, true, "QLcj=", port, pubkey);
    CHECK(port == 0);
}
