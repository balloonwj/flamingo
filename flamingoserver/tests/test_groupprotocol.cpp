/**
 *  v2 群组报文编解码单元测试（Suite X），test_groupprotocol.cpp
 *  覆盖：create_group / get_group_info / get_group_members / update_group_info
 *       请求解析与应答构造，group_info_changed / group_member_changed 通知构造；
 *       invite / join / handle_join / get_join_requests 报文编解码；
 *       set_member_role / mute_member / mute_all / set_member_card / transfer_owner 解析。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../jsoncpp1.9.0/json.h"

#include <set>

//把 JSON 文本解析成 Json::Value，供断言检查
static bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("X1 parseCreateGroupRequest 解析与校验")
{
    CreateGroupRequest req;
    REQUIRE(GroupProtocol::parseCreateGroupRequest(
        "{\"groupname\":\"dev team\",\"memberids\":[\"100001\",\"100002\"]}", req));
    CHECK(req.name == "dev team");
    REQUIRE(req.memberids.size() == 2);
    CHECK(req.memberids[0] == 100001);
    CHECK(req.memberids[1] == 100002);
    //memberids 可缺省（只有群主的群）
    CreateGroupRequest req2;
    REQUIRE(GroupProtocol::parseCreateGroupRequest("{\"groupname\":\"solo\"}", req2));
    CHECK(req2.memberids.empty());
    //groupname 缺失或不是字符串被拒
    CHECK_FALSE(GroupProtocol::parseCreateGroupRequest("{\"memberids\":[\"1\"]}", req));
    CHECK_FALSE(GroupProtocol::parseCreateGroupRequest("{\"groupname\":1}", req));
    //groupname 为空串合法：表示由服务端按成员昵称生成自动群名
    CreateGroupRequest req3;
    REQUIRE(GroupProtocol::parseCreateGroupRequest(
        "{\"groupname\":\"\",\"memberids\":[\"100001\"]}", req3));
    CHECK(req3.name.empty());
    REQUIRE(req3.memberids.size() == 1);
    CHECK(req3.memberids[0] == 100001);
}

TEST_CASE("X2 parseGroupIdRequest 解析与校验")
{
    int64_t groupid = 0;
    REQUIRE(GroupProtocol::parseGroupIdRequest("{\"groupid\":\"268435457\"}", groupid));
    CHECK(groupid == 268435457);
    //groupid 缺失或非正被拒
    CHECK_FALSE(GroupProtocol::parseGroupIdRequest("{}", groupid));
    CHECK_FALSE(GroupProtocol::parseGroupIdRequest("{\"groupid\":\"0\"}", groupid));
}

TEST_CASE("X3 parseUpdateGroupInfoRequest 可选字段")
{
    UpdateGroupInfoRequest req;
    REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
        "{\"groupid\":\"268435457\",\"name\":\"new name\",\"muteall\":1}", req));
    CHECK(req.groupid == 268435457);
    CHECK(req.has_name == true);
    CHECK(req.name == "new name");
    CHECK(req.has_muteall == true);
    CHECK(req.muteall == 1);
    //未提交的字段 has_* 为 false
    CHECK(req.has_facetype == false);
    CHECK(req.has_announcement == false);
    CHECK(req.has_joinmode == false);
    //groupid 缺失被拒
    CHECK_FALSE(GroupProtocol::parseUpdateGroupInfoRequest("{\"name\":\"x\"}", req));
}

TEST_CASE("X4 buildCreateGroupResponse")
{
    Json::Value root;
    REQUIRE(parseJson(GroupProtocol::buildCreateGroupResponse(0, "ok", 268435457), root));
    CHECK(root["code"].asInt() == 0);
    //groupid 以字符串形式落入 JSON
    CHECK(root["groupid"].asString() == "268435457");
}

TEST_CASE("X5 buildGroupInfoResponse 群资料 + myrole")
{
    GroupRecord g;
    g.groupid      = 268435457;
    g.name         = "dev team";
    g.facetype     = 0;
    g.customface   = "";
    g.announcement = "welcome";
    g.ownerid      = 100007;
    g.joinmode     = group_join_approval;
    g.muteall      = 0;
    g.membercount  = 12;

    Json::Value root;
    REQUIRE(parseJson(GroupProtocol::buildGroupInfoResponse(0, "ok", g, group_role_owner), root));
    CHECK(root["groupid"].asString() == "268435457");
    CHECK(root["name"].asString() == "dev team");
    CHECK(root["ownerid"].asString() == "100007");
    CHECK(root["announcement"].asString() == "welcome");
    CHECK(root["joinmode"].asInt() == 1);
    CHECK(root["membercount"].asInt() == 12);
    //myrole 为请求者在群内的角色
    CHECK(root["myrole"].asInt() == 2);
}

TEST_CASE("X6 buildGroupMembersResponse 成员列表")
{
    GroupMemberRecord m1;
    m1.userid     = 100007;
    m1.role       = group_role_owner;
    m1.card       = "leader";
    m1.muted      = 0;
    m1.nickname   = "su";
    m1.facetype   = 0;
    GroupMemberRecord m2;
    m2.userid     = 100001;
    m2.role       = group_role_member;
    m2.card       = "";
    m2.muted      = 1;
    m2.nickname   = "lin";
    m2.facetype   = 0;
    std::vector<GroupMemberRecord> members;
    members.push_back(m1);
    members.push_back(m2);

    Json::Value root;
    REQUIRE(parseJson(GroupProtocol::buildGroupMembersResponse(0, "ok", 268435457, members), root));
    CHECK(root["groupid"].asString() == "268435457");
    REQUIRE(root["members"].size() == 2);
    CHECK(root["members"][0]["userid"].asString() == "100007");
    CHECK(root["members"][0]["role"].asInt() == 2);
    CHECK(root["members"][0]["card"].asString() == "leader");
    CHECK(root["members"][1]["userid"].asString() == "100001");
    CHECK(root["members"][1]["muted"].asInt() == 1);
    CHECK(root["members"][1]["nickname"].asString() == "lin");
}

TEST_CASE("X7 buildGroupInfoChangedNotify / buildGroupMemberChangedNotify")
{
    GroupRecord g;
    g.groupid = 268435457;
    g.name    = "renamed";
    Json::Value r1;
    REQUIRE(parseJson(
        GroupProtocol::buildGroupInfoChangedNotify(g, group_info_change_updated), r1));
    CHECK(r1["groupid"].asString() == "268435457");
    CHECK(r1["name"].asString() == "renamed");
    CHECK(r1["changetype"].asInt() == 1);

    std::vector<int64_t> userids;
    userids.push_back(100001);
    userids.push_back(100002);
    Json::Value r2;
    REQUIRE(parseJson(
        GroupProtocol::buildGroupMemberChangedNotify(
            268435457, group_member_change_join, userids, 100007), r2));
    CHECK(r2["groupid"].asString() == "268435457");
    CHECK(r2["changetype"].asInt() == 1);
    CHECK(r2["operatorid"].asString() == "100007");
    REQUIRE(r2["userids"].size() == 2);
    CHECK(r2["userids"][0].asString() == "100001");
    CHECK(r2["userids"][1].asString() == "100002");
}

TEST_CASE("X8 parseGroupMembersOpRequest 解析与校验")
{
    GroupMembersOpRequest req;
    REQUIRE(GroupProtocol::parseGroupMembersOpRequest(
        "{\"groupid\":\"268435457\",\"userids\":[\"100001\",\"100002\"]}", req));
    CHECK(req.groupid == 268435457);
    REQUIRE(req.userids.size() == 2);
    CHECK(req.userids[0] == 100001);
    CHECK(req.userids[1] == 100002);
    //groupid 缺失、userids 缺失 / 为空均被拒
    CHECK_FALSE(GroupProtocol::parseGroupMembersOpRequest("{\"userids\":[\"1\"]}", req));
    CHECK_FALSE(GroupProtocol::parseGroupMembersOpRequest("{\"groupid\":\"268435457\"}", req));
    CHECK_FALSE(GroupProtocol::parseGroupMembersOpRequest(
        "{\"groupid\":\"268435457\",\"userids\":[]}", req));
}

TEST_CASE("X9 parseJoinGroupRequest 解析与校验")
{
    JoinGroupRequest req;
    REQUIRE(GroupProtocol::parseJoinGroupRequest(
        "{\"groupid\":\"268435457\",\"verifymsg\":\"let me in\"}", req));
    CHECK(req.groupid == 268435457);
    CHECK(req.verifymsg == "let me in");
    //verifymsg 可缺省
    REQUIRE(GroupProtocol::parseJoinGroupRequest("{\"groupid\":\"268435457\"}", req));
    CHECK(req.verifymsg == "");
    //groupid 缺失被拒
    CHECK_FALSE(GroupProtocol::parseJoinGroupRequest("{\"verifymsg\":\"x\"}", req));
}

TEST_CASE("X10 parseHandleJoinGroupRequest 解析与校验")
{
    HandleJoinGroupRequest req;
    REQUIRE(GroupProtocol::parseHandleJoinGroupRequest(
        "{\"requestid\":\"5001\",\"action\":1}", req));
    CHECK(req.requestid == 5001);
    CHECK(req.action == 1);
    //requestid 缺失、action 缺失均被拒
    CHECK_FALSE(GroupProtocol::parseHandleJoinGroupRequest("{\"action\":1}", req));
    CHECK_FALSE(GroupProtocol::parseHandleJoinGroupRequest("{\"requestid\":\"5001\"}", req));
}

TEST_CASE("X11 buildJoinGroupResponse")
{
    //joined=true：自由加入的群，已直接入群
    Json::Value r1;
    REQUIRE(parseJson(GroupProtocol::buildJoinGroupResponse(0, "ok", true), r1));
    CHECK(r1["code"].asInt() == 0);
    CHECK(r1["joined"].asBool() == true);
    //joined=false：申请已提交、待审批
    Json::Value r2;
    REQUIRE(parseJson(GroupProtocol::buildJoinGroupResponse(0, "ok", false), r2));
    CHECK(r2["joined"].asBool() == false);
}

TEST_CASE("X12 buildJoinGroupRequestNotify")
{
    GroupJoinRequestRecord item;
    item.id         = 5001;
    item.groupid    = 268435457;
    item.userid     = 100009;
    item.verifymsg  = "let me in";
    item.createtime = 1716268800000LL;
    item.nickname   = "lin";
    item.facetype   = 0;

    Json::Value root;
    REQUIRE(parseJson(GroupProtocol::buildJoinGroupRequestNotify(item), root));
    CHECK(root["requestid"].asString() == "5001");
    CHECK(root["groupid"].asString() == "268435457");
    CHECK(root["fromuserid"].asString() == "100009");
    CHECK(root["fromnickname"].asString() == "lin");
    CHECK(root["verifymsg"].asString() == "let me in");
}

TEST_CASE("X13 buildGetJoinRequestsResponse")
{
    GroupJoinRequestRecord a;
    a.id         = 5001;
    a.userid     = 100009;
    a.verifymsg  = "msg a";
    a.createtime = 1716268800000LL;
    a.nickname   = "lin";
    a.facetype   = 0;
    GroupJoinRequestRecord b;
    b.id         = 5002;
    b.userid     = 100010;
    b.verifymsg  = "msg b";
    b.createtime = 1716268900000LL;
    b.nickname   = "su";
    b.facetype   = 0;
    std::vector<GroupJoinRequestRecord> items;
    items.push_back(a);
    items.push_back(b);

    Json::Value root;
    REQUIRE(parseJson(
        GroupProtocol::buildGetJoinRequestsResponse(0, "ok", 268435457, items), root));
    CHECK(root["groupid"].asString() == "268435457");
    REQUIRE(root["requests"].size() == 2);
    CHECK(root["requests"][0]["requestid"].asString() == "5001");
    CHECK(root["requests"][0]["userid"].asString() == "100009");
    CHECK(root["requests"][0]["nickname"].asString() == "lin");
    CHECK(root["requests"][1]["verifymsg"].asString() == "msg b");
}

TEST_CASE("X14 parseGroupMemberValueRequest 解析与校验")
{
    GroupMemberValueRequest req;
    //set_member_role 用 "role" 键
    REQUIRE(GroupProtocol::parseGroupMemberValueRequest(
        "{\"groupid\":\"268435457\",\"userid\":\"100001\",\"role\":1}", "role", req));
    CHECK(req.groupid == 268435457);
    CHECK(req.userid == 100001);
    CHECK(req.value == 1);
    //mute_member 用 "muted" 键
    GroupMemberValueRequest req2;
    REQUIRE(GroupProtocol::parseGroupMemberValueRequest(
        "{\"groupid\":\"268435457\",\"userid\":\"100001\",\"muted\":1}", "muted", req2));
    CHECK(req2.value == 1);
    //值字段名不匹配时该字段视为缺失，被拒
    CHECK_FALSE(GroupProtocol::parseGroupMemberValueRequest(
        "{\"groupid\":\"268435457\",\"userid\":\"100001\",\"role\":1}", "muted", req));
    //groupid / userid 缺失被拒
    CHECK_FALSE(GroupProtocol::parseGroupMemberValueRequest(
        "{\"userid\":\"100001\",\"role\":1}", "role", req));
    CHECK_FALSE(GroupProtocol::parseGroupMemberValueRequest(
        "{\"groupid\":\"268435457\",\"role\":1}", "role", req));
}

TEST_CASE("X15 parseMuteAllRequest")
{
    int64_t groupid = 0;
    int muteall = -1;
    REQUIRE(GroupProtocol::parseMuteAllRequest(
        "{\"groupid\":\"268435457\",\"muteall\":1}", groupid, muteall));
    CHECK(groupid == 268435457);
    CHECK(muteall == 1);
    //groupid / muteall 缺失被拒
    CHECK_FALSE(GroupProtocol::parseMuteAllRequest("{\"muteall\":1}", groupid, muteall));
    CHECK_FALSE(GroupProtocol::parseMuteAllRequest("{\"groupid\":\"268435457\"}", groupid, muteall));
}

TEST_CASE("X16 parseSetMemberCardRequest")
{
    int64_t groupid = 0;
    std::string card;
    REQUIRE(GroupProtocol::parseSetMemberCardRequest(
        "{\"groupid\":\"268435457\",\"card\":\"my card\"}", groupid, card));
    CHECK(groupid == 268435457);
    CHECK(card == "my card");
    //card 可为空串（清除群名片）
    REQUIRE(GroupProtocol::parseSetMemberCardRequest("{\"groupid\":\"268435457\"}", groupid, card));
    CHECK(card == "");
    //groupid 缺失被拒
    CHECK_FALSE(GroupProtocol::parseSetMemberCardRequest("{\"card\":\"x\"}", groupid, card));
}

TEST_CASE("X17 parseTransferOwnerRequest")
{
    int64_t groupid = 0;
    int64_t userid = 0;
    REQUIRE(GroupProtocol::parseTransferOwnerRequest(
        "{\"groupid\":\"268435457\",\"userid\":\"100009\"}", groupid, userid));
    CHECK(groupid == 268435457);
    CHECK(userid == 100009);
    //groupid / userid 缺失被拒
    CHECK_FALSE(GroupProtocol::parseTransferOwnerRequest("{\"userid\":\"100009\"}", groupid, userid));
    CHECK_FALSE(GroupProtocol::parseTransferOwnerRequest("{\"groupid\":\"268435457\"}", groupid, userid));
}

TEST_CASE("X18 parseAdminCreateGroupRequest 解析与校验")
{
    //完整合法请求：群名 + 群主 + 管理员 + 普通成员
    AdminCreateGroupRequest req;
    REQUIRE(GroupProtocol::parseAdminCreateGroupRequest(
        "{\"groupname\":\"研发一组\",\"ownerid\":\"100007\","
        "\"adminids\":[\"100002\"],\"memberids\":[\"100001\",\"100002\",\"100003\"]}", req));
    CHECK(req.name == "研发一组");
    CHECK(req.ownerid == 100007);
    REQUIRE(req.adminids.size() == 1);
    CHECK(req.adminids[0] == 100002);
    REQUIRE(req.memberids.size() == 3);
    CHECK(req.memberids[0] == 100001);
    CHECK(req.memberids[2] == 100003);

    //adminids / memberids 均可缺省（只有群主的群）
    AdminCreateGroupRequest req2;
    REQUIRE(GroupProtocol::parseAdminCreateGroupRequest(
        "{\"groupname\":\"独狼群\",\"ownerid\":\"100007\"}", req2));
    CHECK(req2.adminids.empty());
    CHECK(req2.memberids.empty());

    //ownerid 缺省时解析仍成功，但 ownerid 取 0（留给 validate 拒绝）
    AdminCreateGroupRequest req3;
    REQUIRE(GroupProtocol::parseAdminCreateGroupRequest("{\"groupname\":\"无主群\"}", req3));
    CHECK(req3.ownerid == 0);

    //groupname 缺失或为空被拒
    AdminCreateGroupRequest tmp;
    CHECK_FALSE(GroupProtocol::parseAdminCreateGroupRequest(
        "{\"ownerid\":\"100007\"}", tmp));
    CHECK_FALSE(GroupProtocol::parseAdminCreateGroupRequest(
        "{\"groupname\":\"\",\"ownerid\":\"100007\"}", tmp));
}

TEST_CASE("X19 validateAdminCreateGroup 角色规整与校验")
{
    //正常情形：群主 100007，成员含 100001/100002/100003，其中 100002 为管理员
    {
        AdminCreateGroupRequest req;
        req.ownerid = 100007;
        req.adminids.push_back(100002);
        req.memberids.push_back(100001);
        req.memberids.push_back(100002);
        req.memberids.push_back(100003);
        std::vector<int64_t> admins;
        std::vector<int64_t> members;
        REQUIRE(GroupProtocol::validateAdminCreateGroup(req, admins, members));
        //管理员从普通成员里拆出，不再重复出现在 members 里
        REQUIRE(admins.size() == 1);
        CHECK(admins[0] == 100002);
        REQUIRE(members.size() == 2);
        CHECK(members[0] == 100001);
        CHECK(members[1] == 100003);
    }

    //群主非正（缺省）被拒
    {
        AdminCreateGroupRequest req;
        req.ownerid = 0;
        req.memberids.push_back(100001);
        std::vector<int64_t> admins;
        std::vector<int64_t> members;
        CHECK_FALSE(GroupProtocol::validateAdminCreateGroup(req, admins, members));
    }

    //管理员不在成员集内被拒（防传入未选中的成员）
    {
        AdminCreateGroupRequest req;
        req.ownerid = 100007;
        req.adminids.push_back(100002);  //100002 未出现在 memberids
        req.memberids.push_back(100001);
        std::vector<int64_t> admins;
        std::vector<int64_t> members;
        CHECK_FALSE(GroupProtocol::validateAdminCreateGroup(req, admins, members));
    }

    //群主同时出现在 members / admins 中：群主优先，从两个出参里都剔除
    {
        AdminCreateGroupRequest req;
        req.ownerid = 100007;
        req.adminids.push_back(100007);
        req.memberids.push_back(100007);
        req.memberids.push_back(100001);
        std::vector<int64_t> admins;
        std::vector<int64_t> members;
        REQUIRE(GroupProtocol::validateAdminCreateGroup(req, admins, members));
        CHECK(admins.empty());
        REQUIRE(members.size() == 1);
        CHECK(members[0] == 100001);
    }

    //成员列表含重复 userid：按首次出现顺序去重
    {
        AdminCreateGroupRequest req;
        req.ownerid = 100007;
        req.memberids.push_back(100001);
        req.memberids.push_back(100001);
        req.memberids.push_back(100002);
        std::vector<int64_t> admins;
        std::vector<int64_t> members;
        REQUIRE(GroupProtocol::validateAdminCreateGroup(req, admins, members));
        CHECK(admins.empty());
        REQUIRE(members.size() == 2);
        CHECK(members[0] == 100001);
        CHECK(members[1] == 100002);
    }
}

// ====================================================================
// 禁言时长（群管理功能新增）
//
// 禁言态由 f_muted 与 f_mute_until 两列共同表达，"到期时间为 0"表示永久禁言。
// 这个约定的用意是让升级前只有 f_muted 一列的老数据自动落在"永久禁言"上，
// 下面几组用例把这层语义钉住。
// ====================================================================

TEST_CASE("isMuteExpired: 未禁言与永久禁言都不算过期")
{
    const int64_t now = 1000000;

    //根本没禁言 —— 无论到期时间是什么都返回 false
    CHECK(GroupProtocol::isMuteExpired(0, 0, now) == false);
    CHECK(GroupProtocol::isMuteExpired(0, now - 1, now) == false);

    //永久禁言（到期时间 0）—— 永不过期。老库升级后 f_muted=1 的行正是这一形态。
    CHECK(GroupProtocol::isMuteExpired(1, 0, now) == false);

    //负的到期时间是脏数据，同样按永久禁言处理，不能因为"小于 now"就放行
    CHECK(GroupProtocol::isMuteExpired(1, -1, now) == false);
}

TEST_CASE("isMuteExpired: 定时禁言按时刻判定")
{
    const int64_t now = 1000000;

    //还没到点
    CHECK(GroupProtocol::isMuteExpired(1, now + 1, now) == false);

    //正好到点即算过期（边界取闭区间，避免"刚好等于"时禁言多挂一瞬）
    CHECK(GroupProtocol::isMuteExpired(1, now, now) == true);

    //已经过点
    CHECK(GroupProtocol::isMuteExpired(1, now - 1, now) == true);
}

TEST_CASE("computeMuteUntil: 解禁与永久禁言都记 0")
{
    const int64_t now = 1000000;

    //解禁：时长无论传什么都不产生到期时间
    CHECK(GroupProtocol::computeMuteUntil(0, 60, now) == 0);
    CHECK(GroupProtocol::computeMuteUntil(0, 0, now) == 0);

    //禁言但时长为 0 —— 永久禁言。老客户端不带 duration 字段时正是这一路。
    CHECK(GroupProtocol::computeMuteUntil(1, 0, now) == 0);

    //负时长视为永久，不能算出一个早于现在的到期时刻、变成"一发即过期"
    CHECK(GroupProtocol::computeMuteUntil(1, -60, now) == 0);
}

TEST_CASE("computeMuteUntil: 定时禁言按秒换算成毫秒")
{
    const int64_t now = 1000000;

    CHECK(GroupProtocol::computeMuteUntil(1, 1, now) == now + 1000);
    CHECK(GroupProtocol::computeMuteUntil(1, 600, now) == now + 600 * 1000);

    //一天：验证换算不会在 int 范围内溢出（86400 * 1000 已超过 int16，且中间量须是 int64）
    CHECK(GroupProtocol::computeMuteUntil(1, 86400, now) == now + 86400LL * 1000);

    //算出来的到期时刻拿去判定，应当还没过期
    const int64_t until = GroupProtocol::computeMuteUntil(1, 600, now);
    CHECK(GroupProtocol::isMuteExpired(1, until, now) == false);
    CHECK(GroupProtocol::isMuteExpired(1, until, until) == true);
}

TEST_CASE("parseGroupMemberValueRequest: 老格式三字段请求仍然合法")
{
    //扩展前的客户端只发 groupid / userid / muted 三个字段，解析后应当得到
    //"永久禁言、无原因"，与扩展前的行为一字不差。
    const std::string json = "{\"groupid\":\"10000000001\",\"userid\":\"100001\",\"muted\":1}";

    GroupMemberValueRequest req;
    REQUIRE(GroupProtocol::parseGroupMemberValueRequest(json, "muted", req));
    CHECK(req.groupid == 10000000001LL);
    CHECK(req.userid == 100001);
    CHECK(req.value == 1);
    CHECK(req.duration == 0);
    CHECK(req.reason.empty());
    CHECK(GroupProtocol::computeMuteUntil(req.value, req.duration, 1000000) == 0);
}

TEST_CASE("parseGroupMemberValueRequest: 新格式带时长与原因")
{
    const std::string json =
        "{\"groupid\":\"10000000001\",\"userid\":\"100001\",\"muted\":1,"
        "\"duration\":600,\"reason\":\"频繁刷屏\"}";

    GroupMemberValueRequest req;
    REQUIRE(GroupProtocol::parseGroupMemberValueRequest(json, "muted", req));
    CHECK(req.value == 1);
    CHECK(req.duration == 600);
    CHECK(req.reason == "频繁刷屏");
}

TEST_CASE("parseGroupMemberValueRequest: 非正时长按没填处理")
{
    //时长为 0 或负数都不该算出一个到期时刻，落到永久禁言上
    const std::string zero =
        "{\"groupid\":\"10000000001\",\"userid\":\"100001\",\"muted\":1,\"duration\":0}";
    const std::string negative =
        "{\"groupid\":\"10000000001\",\"userid\":\"100001\",\"muted\":1,\"duration\":-600}";

    GroupMemberValueRequest req1;
    REQUIRE(GroupProtocol::parseGroupMemberValueRequest(zero, "muted", req1));
    CHECK(req1.duration == 0);

    GroupMemberValueRequest req2;
    REQUIRE(GroupProtocol::parseGroupMemberValueRequest(negative, "muted", req2));
    CHECK(req2.duration == 0);
}

TEST_CASE("parseGroupMemberValueRequest: 设管理员不受新字段影响")
{
    //cmd_set_member_role 与 cmd_mute_member 共用本解析函数，前者不带
    //duration / reason，解析后这两项应当是初值。
    const std::string json = "{\"groupid\":\"10000000001\",\"userid\":\"100001\",\"role\":1}";

    GroupMemberValueRequest req;
    REQUIRE(GroupProtocol::parseGroupMemberValueRequest(json, "role", req));
    CHECK(req.value == 1);
    CHECK(req.duration == 0);
    CHECK(req.reason.empty());
}

// ====================================================================
// 群设置七项与新字段的编解码（群管理功能新增）
// ====================================================================

TEST_CASE("parseUpdateGroupInfoRequest: 七个新可选项各自独立")
{
    //只带其中一项时，只有它的 has_* 为真，其余全假 —— 保证"改一项不会顺手
    //把别的项覆盖成默认值"。
    {
        UpdateGroupInfoRequest req;
        REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
            "{\"groupid\":\"10000000001\",\"intro\":\"aaa\"}", req));
        CHECK(req.has_intro);
        CHECK(req.intro == "aaa");
        CHECK(req.has_atallscope == false);
        CHECK(req.has_newseehistory == false);
        CHECK(req.has_memberinvite == false);
        CHECK(req.has_welcomeon == false);
        CHECK(req.has_welcometext == false);
        CHECK(req.has_sensitiveon == false);
        //老字段也不该被误置
        CHECK(req.has_name == false);
        CHECK(req.has_announcement == false);
    }

    {
        UpdateGroupInfoRequest req;
        REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
            "{\"groupid\":\"10000000001\",\"atallscope\":1}", req));
        CHECK(req.has_atallscope);
        CHECK(req.atallscope == group_at_all_everyone);
        CHECK(req.has_intro == false);
    }

    {
        UpdateGroupInfoRequest req;
        REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
            "{\"groupid\":\"10000000001\",\"newseehistory\":0,\"memberinvite\":1}", req));
        CHECK(req.has_newseehistory);
        CHECK(req.newseehistory == 0);
        CHECK(req.has_memberinvite);
        CHECK(req.memberinvite == 1);
    }

    {
        UpdateGroupInfoRequest req;
        REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
            "{\"groupid\":\"10000000001\",\"welcomeon\":1,\"welcometext\":\"hi\","
            "\"sensitiveon\":1}", req));
        CHECK(req.has_welcomeon);
        CHECK(req.welcomeon == 1);
        CHECK(req.has_welcometext);
        CHECK(req.welcometext == "hi");
        CHECK(req.has_sensitiveon);
        CHECK(req.sensitiveon == 1);
    }
}

TEST_CASE("parseUpdateGroupInfoRequest: 群名为空串表示恢复自动群名")
{
    //空串不能被当成"没带群名"而丢掉，否则群主清空群名后什么也不会发生
    UpdateGroupInfoRequest req;
    REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
        "{\"groupid\":\"10000000001\",\"name\":\"\"}", req));
    CHECK(req.has_name);
    CHECK(req.name.empty());

    //不带 name 字段时不改群名
    UpdateGroupInfoRequest req2;
    REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
        "{\"groupid\":\"10000000001\",\"muteall\":1}", req2));
    CHECK(req2.has_name == false);
}

TEST_CASE("parseUpdateGroupInfoRequest: 只带老字段时七项全不更新")
{
    //老客户端只改群名时发来的请求形态，不该把新设置项一并重置
    UpdateGroupInfoRequest req;
    REQUIRE(GroupProtocol::parseUpdateGroupInfoRequest(
        "{\"groupid\":\"10000000001\",\"name\":\"newname\"}", req));
    CHECK(req.has_name);
    CHECK(req.has_intro == false);
    CHECK(req.has_atallscope == false);
    CHECK(req.has_newseehistory == false);
    CHECK(req.has_memberinvite == false);
    CHECK(req.has_welcomeon == false);
    CHECK(req.has_welcometext == false);
    CHECK(req.has_sensitiveon == false);
}

TEST_CASE("buildGroupInfoResponse: 带上七项新设置且 64 位 id 仍为字符串")
{
    GroupRecord g;
    g.groupid       = 10000000001LL;
    g.name          = "designteam";
    g.ownerid       = 100001;
    g.membercount   = 3;
    g.intro         = "intro-text";
    g.atallscope    = group_at_all_everyone;
    g.newseehistory = 0;
    g.memberinvite  = 1;
    g.welcomeon     = 1;
    g.welcometext   = "welcome-text";
    g.sensitiveon   = 1;

    const std::string out = GroupProtocol::buildGroupInfoResponse(0, "ok", g, group_role_owner);

    //64 位 id 必须以字符串编码，避免客户端 JSON 解析丢精度
    CHECK(out.find("\"groupid\":\"10000000001\"") != std::string::npos);
    CHECK(out.find("\"ownerid\":\"100001\"") != std::string::npos);
    CHECK(out.find("\"intro\":\"intro-text\"") != std::string::npos);
    CHECK(out.find("\"atallscope\":1") != std::string::npos);
    CHECK(out.find("\"newseehistory\":0") != std::string::npos);
    CHECK(out.find("\"memberinvite\":1") != std::string::npos);
    CHECK(out.find("\"welcomeon\":1") != std::string::npos);
    CHECK(out.find("\"welcometext\":\"welcome-text\"") != std::string::npos);
    CHECK(out.find("\"sensitiveon\":1") != std::string::npos);
    CHECK(out.find("\"myrole\":2") != std::string::npos);
}

TEST_CASE("buildGroupInfoChangedNotify 与 buildUserGroupsResponse 同样带新字段")
{
    //三处共用 fillGroupValue，这条用例把"共用"这件事本身钉住 —— 日后若有人
    //给某一处单独加字段而没走 fillGroupValue，这里会先炸。
    GroupRecord g;
    g.groupid    = 10000000002LL;
    g.name       = "testgroup";
    g.intro      = "intro-text";
    g.atallscope = group_at_all_everyone;

    const std::string notify = GroupProtocol::buildGroupInfoChangedNotify(
        g, group_info_change_updated);
    CHECK(notify.find("\"intro\":\"intro-text\"") != std::string::npos);
    CHECK(notify.find("\"atallscope\":1") != std::string::npos);
    CHECK(notify.find("\"changetype\":1") != std::string::npos);

    std::vector<GroupRecord> groups;
    groups.push_back(g);
    const std::string list = GroupProtocol::buildUserGroupsResponse(0, "ok", groups,
                                                                    std::set<int64_t>());
    CHECK(list.find("\"intro\":\"intro-text\"") != std::string::npos);
    CHECK(list.find("\"atallscope\":1") != std::string::npos);
}

TEST_CASE("UGP1 buildUserGroupsResponse 每个群带本人的置顶状态")
{
    //群 Tab 要把置顶的群排在最前面；没有消息的空群不在会话列表里，置顶状态只能随群列表下发
    GroupRecord pinnedGroup;
    pinnedGroup.groupid = 10000000011LL;
    pinnedGroup.name    = "group-a";
    GroupRecord normalGroup;
    normalGroup.groupid = 10000000012LL;
    normalGroup.name    = "group-b";

    std::vector<GroupRecord> groups;
    groups.push_back(pinnedGroup);
    groups.push_back(normalGroup);
    std::set<int64_t> pinnedIds;
    pinnedIds.insert(pinnedGroup.groupid);

    Json::Value root;
    REQUIRE(parseJson(GroupProtocol::buildUserGroupsResponse(0, "ok", groups, pinnedIds), root));
    REQUIRE(root["groups"].size() == 2);
    CHECK(root["groups"][0]["groupid"].asString() == "10000000011");
    CHECK(root["groups"][0]["pinned"].asInt() == 1);
    CHECK(root["groups"][1]["groupid"].asString() == "10000000012");
    CHECK(root["groups"][1]["pinned"].asInt() == 0);

    //群资料变更推送（1318）是发给全群的，不带某个人的置顶状态
    Json::Value notify;
    REQUIRE(parseJson(GroupProtocol::buildGroupInfoChangedNotify(
        pinnedGroup, group_info_change_updated), notify));
    CHECK_FALSE(notify.isMember("pinned"));
}

TEST_CASE("buildGroupMembersResponse: 带禁言到期时间与原因")
{
    std::vector<GroupMemberRecord> members;

    GroupMemberRecord permanent;
    permanent.userid     = 100001;
    permanent.role       = group_role_member;
    permanent.muted      = 1;
    permanent.muteuntil  = 0;             //永久禁言
    permanent.mutereason = "spam";
    members.push_back(permanent);

    GroupMemberRecord timed;
    timed.userid    = 100002;
    timed.muted     = 1;
    timed.muteuntil = 1800000000000LL;    //定时禁言
    members.push_back(timed);

    const std::string out = GroupProtocol::buildGroupMembersResponse(
        0, "ok", 10000000001LL, members);

    CHECK(out.find("\"userid\":\"100001\"") != std::string::npos);
    CHECK(out.find("\"muteuntil\":0") != std::string::npos);
    CHECK(out.find("\"mutereason\":\"spam\"") != std::string::npos);
    CHECK(out.find("\"muteuntil\":1800000000000") != std::string::npos);
}

TEST_CASE("加群申请的来源与推荐人出现在应答与推送里")
{
    GroupJoinRequestRecord item;
    item.id          = 55;
    item.groupid     = 10000000001LL;
    item.userid      = 100003;
    item.nickname    = "wangwu";
    item.verifymsg   = "let me in";
    item.createtime  = 1700000000000LL;
    item.source      = group_join_source_friend;
    item.recommender = 100001;

    const std::string notify = GroupProtocol::buildJoinGroupRequestNotify(item);
    CHECK(notify.find("\"source\":0") != std::string::npos);
    CHECK(notify.find("\"recommender\":\"100001\"") != std::string::npos);

    std::vector<GroupJoinRequestRecord> items;
    items.push_back(item);
    const std::string list = GroupProtocol::buildGetJoinRequestsResponse(
        0, "ok", 10000000001LL, items);
    CHECK(list.find("\"requestid\":\"55\"") != std::string::npos);
    CHECK(list.find("\"source\":0") != std::string::npos);
    CHECK(list.find("\"recommender\":\"100001\"") != std::string::npos);
}

// ====================================================================
// 敏感词库报文（cmd_get_sensitive_words / cmd_operate_sensitive_word）
// ====================================================================

TEST_CASE("parseSensitiveWordOpRequest: 新增操作")
{
    SensitiveWordOpRequest req;
    REQUIRE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"operation\":1,\"word\":\"badword\"}", req));
    CHECK(req.groupid == 10000000001LL);
    CHECK(req.operation == sensitive_word_add);
    CHECK(req.word == "badword");
}

TEST_CASE("parseSensitiveWordOpRequest: 删除操作")
{
    SensitiveWordOpRequest req;
    REQUIRE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"operation\":2,\"id\":\"77\"}", req));
    CHECK(req.operation == sensitive_word_delete);
    CHECK(req.id == 77);
}

TEST_CASE("parseSensitiveWordOpRequest: 缺字段与非法 operation 一律拒绝")
{
    SensitiveWordOpRequest req;

    //群ID缺失或非正
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"operation\":1,\"word\":\"x\"}", req));
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"0\",\"operation\":1,\"word\":\"x\"}", req));

    //operation 缺失 / 不认识
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"word\":\"x\"}", req));
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"operation\":9,\"word\":\"x\"}", req));

    //新增却没给词、或给了空词
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"operation\":1}", req));
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"operation\":1,\"word\":\"\"}", req));

    //删除却没给词条ID、或ID非正
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"operation\":2}", req));
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest(
        "{\"groupid\":\"10000000001\",\"operation\":2,\"id\":\"0\"}", req));

    //根本不是 JSON
    CHECK_FALSE(GroupProtocol::parseSensitiveWordOpRequest("not json", req));
}

TEST_CASE("buildSensitiveWordsResponse: 词条ID以字符串编码")
{
    std::vector<GroupSensitiveWordRecord> words;

    GroupSensitiveWordRecord w1;
    w1.id         = 1;
    w1.word       = "badword";
    w1.createtime = 1700000000000LL;
    words.push_back(w1);

    const std::string out = GroupProtocol::buildSensitiveWordsResponse(
        0, "ok", 10000000001LL, words);
    CHECK(out.find("\"groupid\":\"10000000001\"") != std::string::npos);
    CHECK(out.find("\"id\":\"1\"") != std::string::npos);
    CHECK(out.find("\"word\":\"badword\"") != std::string::npos);
    CHECK(out.find("\"timestamp\":1700000000000") != std::string::npos);

    //空词库也要有 words 数组，客户端不必区分"没有该字段"与"空列表"
    std::vector<GroupSensitiveWordRecord> empty;
    const std::string emptyOut = GroupProtocol::buildSensitiveWordsResponse(
        0, "ok", 10000000001LL, empty);
    CHECK(emptyOut.find("\"words\":[]") != std::string::npos);
}

// =====================================================================
// 群公告（cmd_get_group_announcements / cmd_publish_group_announcement /
//         cmd_delete_group_announcement / cmd_group_announcement_notify）
// =====================================================================

TEST_CASE("parseGetAnnouncementsRequest 分页两项均可缺省")
{
    GetAnnouncementsRequest req;
    REQUIRE(GroupProtocol::parseGetAnnouncementsRequest(
        "{\"groupid\":\"10000000001\",\"offset\":20,\"count\":10}", req));
    CHECK(req.groupid == 10000000001LL);
    CHECK(req.offset == 20);
    CHECK(req.count == 10);

    //两项全缺省时取 0，由数据层夹到一页上限
    GetAnnouncementsRequest bare;
    REQUIRE(GroupProtocol::parseGetAnnouncementsRequest("{\"groupid\":\"10000000001\"}", bare));
    CHECK(bare.offset == 0);
    CHECK(bare.count == 0);

    //groupid 缺失 / 非正 / 根本不是 JSON 一律拒绝
    CHECK_FALSE(GroupProtocol::parseGetAnnouncementsRequest("{\"offset\":0}", req));
    CHECK_FALSE(GroupProtocol::parseGetAnnouncementsRequest("{\"groupid\":\"0\"}", req));
    CHECK_FALSE(GroupProtocol::parseGetAnnouncementsRequest("not json", req));
}

TEST_CASE("parsePublishAnnouncementRequest 标题必填且不得只有空白")
{
    PublishAnnouncementRequest req;
    REQUIRE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"  v3.2 online  \",\"content\":\"body\",\"severity\":1}",
        req));
    //标题首尾空白被 trim 掉
    CHECK(req.title == "v3.2 online");
    CHECK(req.content == "body");
    CHECK(req.severity == group_announcement_important);
    CHECK(req.id == 0);

    //正文可空，severity 缺省时为"通知"
    PublishAnnouncementRequest noBody;
    REQUIRE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"only title\"}", noBody));
    CHECK(noBody.content.empty());
    CHECK(noBody.severity == group_announcement_normal);

    //标题缺失、空串、纯空白都要被拒 —— 界面上它们看起来是同一回事
    CHECK_FALSE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"content\":\"body\"}", req));
    CHECK_FALSE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"\"}", req));
    CHECK_FALSE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"   \\t \"}", req));

    //groupid 非正照旧拒绝
    CHECK_FALSE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"0\",\"title\":\"t\"}", req));
}

TEST_CASE("parsePublishAnnouncementRequest 长度按字符计，纯中文标题不会被误判超长")
{
    //128 个汉字（正好压线）在 UTF-8 里是 384 字节；若按字节校验这里就会被拒
    std::string title;
    for (int i = 0; i < kAnnouncementTitleMaxChars; ++i)
        title += u8"公";

    PublishAnnouncementRequest req;
    REQUIRE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"" + title + "\"}", req));

    //再多一个字就超了
    PublishAnnouncementRequest tooLong;
    CHECK_FALSE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"" + title + u8"公" + "\"}", tooLong));

    //正文同理，超过上限即拒
    std::string content;
    for (int i = 0; i < kAnnouncementContentMaxChars + 1; ++i)
        content += "a";
    PublishAnnouncementRequest bigBody;
    CHECK_FALSE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"t\",\"content\":\"" + content + "\"}", bigBody));
}

TEST_CASE("parsePublishAnnouncementRequest 重新发布只需 id，未知优先级归为通知")
{
    //重新发布：只带原公告ID，标题正文由会话层读回原件补齐，这里不校验它们
    PublishAnnouncementRequest again;
    REQUIRE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"id\":\"12\"}", again));
    CHECK(again.id == 12);
    CHECK(again.title.empty());

    //未知的 severity 归为"通知"而不是判整条请求非法 —— 公告内容本身是有价值的，
    //不该因为多一个不认识的档位就丢掉
    PublishAnnouncementRequest weird;
    REQUIRE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"t\",\"severity\":99}", weird));
    CHECK(weird.severity == group_announcement_normal);

    PublishAnnouncementRequest negative;
    REQUIRE(GroupProtocol::parsePublishAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"title\":\"t\",\"severity\":-1}", negative));
    CHECK(negative.severity == group_announcement_normal);
}

TEST_CASE("parseDeleteAnnouncementRequest groupid 与 id 都要为正")
{
    int64_t groupid = 0;
    int64_t id = 0;
    REQUIRE(GroupProtocol::parseDeleteAnnouncementRequest(
        "{\"groupid\":\"10000000001\",\"id\":\"12\"}", groupid, id));
    CHECK(groupid == 10000000001LL);
    CHECK(id == 12);

    //删除不可恢复，缺哪个都不能猜着执行
    CHECK_FALSE(GroupProtocol::parseDeleteAnnouncementRequest(
        "{\"groupid\":\"10000000001\"}", groupid, id));
    CHECK_FALSE(GroupProtocol::parseDeleteAnnouncementRequest("{\"id\":\"12\"}", groupid, id));
    CHECK_FALSE(GroupProtocol::parseDeleteAnnouncementRequest("not json", groupid, id));
}

TEST_CASE("buildAnnouncementsResponse 64 位 id 以字符串编码，total 独立于本页条数")
{
    std::vector<GroupAnnouncementRecord> items;

    GroupAnnouncementRecord a;
    a.id            = 12;
    a.groupid       = 10000000001LL;
    a.title         = "v3.2";
    a.content       = "body";
    a.severity      = group_announcement_urgent;
    a.publisher     = 100023;
    a.publishername = "lin";
    a.publishtime   = 1700000000000LL;
    items.push_back(a);

    //total 传 5、本页只有 1 条 —— 客户端要靠 total 画"1/5"，两者必须各归各
    const std::string out = GroupProtocol::buildAnnouncementsResponse(
        0, "ok", 10000000001LL, 5, 0, 8, items);
    CHECK(out.find("\"groupid\":\"10000000001\"") != std::string::npos);
    CHECK(out.find("\"total\":5") != std::string::npos);
    CHECK(out.find("\"id\":\"12\"") != std::string::npos);
    CHECK(out.find("\"publisher\":\"100023\"") != std::string::npos);
    CHECK(out.find("\"publishername\":\"lin\"") != std::string::npos);
    CHECK(out.find("\"publishtime\":1700000000000") != std::string::npos);
    CHECK(out.find("\"severity\":2") != std::string::npos);

    //空列表也要有 announcements 数组，客户端不必区分"没有该字段"与"空列表"
    std::vector<GroupAnnouncementRecord> empty;
    const std::string emptyOut = GroupProtocol::buildAnnouncementsResponse(
        0, "ok", 10000000001LL, 0, 0, 8, empty);
    CHECK(emptyOut.find("\"announcements\":[]") != std::string::npos);
    CHECK(emptyOut.find("\"total\":0") != std::string::npos);
}

TEST_CASE("buildAnnouncementsResponse 原样回显请求的分页参数")
{
    //同一个客户端可能有两处同时在拉公告（群管理窗按页拉、群聊窗边栏一次拉一条），
    //而应答是广播给两处的。回显 offset / count 是两处认领自己那一份的唯一依据 ——
    //没有它，后到的应答会把另一处的列表覆盖掉。
    std::vector<GroupAnnouncementRecord> items;

    const std::string page2 = GroupProtocol::buildAnnouncementsResponse(
        0, "ok", 10000000001LL, 30, 8, 8, items);
    CHECK(page2.find("\"offset\":8") != std::string::npos);
    CHECK(page2.find("\"count\":8") != std::string::npos);

    //边栏那种"只要第 5 条"的请求同样原样回显
    const std::string single = GroupProtocol::buildAnnouncementsResponse(
        0, "ok", 10000000001LL, 30, 5, 1, items);
    CHECK(single.find("\"offset\":5") != std::string::npos);
    CHECK(single.find("\"count\":1") != std::string::npos);
}

TEST_CASE("buildPublishAnnouncementResponse 新公告ID以字符串编码")
{
    const std::string ok = GroupProtocol::buildPublishAnnouncementResponse(0, "ok", 15);
    CHECK(ok.find("\"code\":0") != std::string::npos);
    CHECK(ok.find("\"id\":\"15\"") != std::string::npos);

    //失败时 id 为 0，字段仍在，客户端不必判字段有无
    const std::string bad = GroupProtocol::buildPublishAnnouncementResponse(
        error_code_not_group_admin, "denied", 0);
    CHECK(bad.find("\"id\":\"0\"") != std::string::npos);
}

TEST_CASE("buildAnnouncementNotify 两种 changetype 都带完整公告")
{
    GroupAnnouncementRecord a;
    a.id            = 15;
    a.groupid       = 10000000001LL;
    a.title         = "v3.2";
    a.content       = "body";
    a.severity      = group_announcement_normal;
    a.publisher     = 100023;
    a.publishername = "lin";
    a.publishtime   = 1700000000000LL;

    const std::string pub = GroupProtocol::buildAnnouncementNotify(
        a, group_announcement_change_publish);
    CHECK(pub.find("\"changetype\":1") != std::string::npos);
    CHECK(pub.find("\"groupid\":\"10000000001\"") != std::string::npos);
    //推送体里带完整公告，客户端收到即可直接刷新边栏 / 弹窗，不必再拉一次
    CHECK(pub.find("\"title\":\"v3.2\"") != std::string::npos);
    CHECK(pub.find("\"content\":\"body\"") != std::string::npos);
    CHECK(pub.find("\"publishername\":\"lin\"") != std::string::npos);

    //删除推送同样带完整公告 —— 那一条已经从库里没了，客户端只能靠推送体里的
    //id 把它从列表里摘掉，靠标题告诉用户是哪条被删了
    const std::string del = GroupProtocol::buildAnnouncementNotify(
        a, group_announcement_change_delete);
    CHECK(del.find("\"changetype\":2") != std::string::npos);
    CHECK(del.find("\"id\":\"15\"") != std::string::npos);
    CHECK(del.find("\"title\":\"v3.2\"") != std::string::npos);
}

TEST_CASE("fillGroupValue 群资料顺带带出最新公告快照三项")
{
    //群聊窗头部那行公告摘要直接读这三项，不必单独拉一次公告列表
    GroupRecord g;
    g.groupid       = 10000000001LL;
    g.name          = "team";
    g.announcement  = "latest body";
    g.announceid    = 15;
    g.announcetitle = "latest title";
    g.announcetime  = 1700000000000LL;

    const std::string out = GroupProtocol::buildGroupInfoResponse(0, "ok", g, group_role_owner);
    CHECK(out.find("\"announceid\":\"15\"") != std::string::npos);
    CHECK(out.find("\"announcetitle\":\"latest title\"") != std::string::npos);
    CHECK(out.find("\"announcetime\":1700000000000") != std::string::npos);
    CHECK(out.find("\"announcement\":\"latest body\"") != std::string::npos);

    //推送体与应答同构，两处共用 fillGroupValue，故这里一并检查
    const std::string notify = GroupProtocol::buildGroupInfoChangedNotify(
        g, group_info_change_updated);
    CHECK(notify.find("\"announceid\":\"15\"") != std::string::npos);
    CHECK(notify.find("\"announcetitle\":\"latest title\"") != std::string::npos);
}
