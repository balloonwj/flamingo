/**
 *  v2 群组报文编解码（客户端侧）单元测试（Suite GP），GroupProtocolTests.cpp
 *  覆盖：buildCreateGroupRequest 字段完整性与类型、memberids 字符串编码、大 id
 *       不丢精度、空成员列表；parseCreateGroupResponse 成功/失败应答、groupid
 *       字符串→int64、畸形输入安全失败。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-02
 */
#include "catch_amalgamated.hpp"
#include "../net/GroupProtocolClient.h"
//见 GroupProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查 buildCreateGroupRequest 的产物。
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("GP1 buildCreateGroupRequest 字段完整且 memberids 为字符串数组")
{
    std::vector<int64_t> members;
    members.push_back(5001);
    members.push_back(5002);
    std::string json = GroupProtocolClient::buildCreateGroupRequest(u8"项目组", members);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //群名（UTF-8）原样写入 groupname。
    CHECK(root["groupname"].isString());
    CHECK(root["groupname"].asString() == u8"项目组");
    //memberids 为字符串数组（防大整数精度丢失）。
    REQUIRE(root["memberids"].isArray());
    REQUIRE(root["memberids"].size() == 2);
    CHECK(root["memberids"][0].isString());
    CHECK(root["memberids"][0].asString() == "5001");
    CHECK(root["memberids"][1].asString() == "5002");
}

TEST_CASE("GP2 buildCreateGroupRequest 空成员列表：memberids 为空数组")
{
    std::vector<int64_t> members;   // 允许建只有群主的群
    std::string json = GroupProtocolClient::buildCreateGroupRequest(u8"我的群", members);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    REQUIRE(root["memberids"].isArray());
    CHECK(root["memberids"].size() == 0);
}

TEST_CASE("GP3 buildCreateGroupRequest 大 id 不丢精度")
{
    std::vector<int64_t> members;
    members.push_back(9007199254740993LL);   // 2^53 + 1
    std::string json = GroupProtocolClient::buildCreateGroupRequest(u8"g", members);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    REQUIRE(root["memberids"].size() == 1);
    CHECK(root["memberids"][0].asString() == "9007199254740993");
}

TEST_CASE("GP4 parseCreateGroupResponse 解析成功应答（groupid 字符串）")
{
    std::string body = "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"700001\"}";
    CreateGroupResponse resp;
    REQUIRE(GroupProtocolClient::parseCreateGroupResponse(body, resp));
    CHECK(resp.m_code == 0);
    CHECK(resp.m_msg == "ok");
    CHECK(resp.m_groupid == 700001);
}

TEST_CASE("GP5 parseCreateGroupResponse 大 groupid 不丢精度")
{
    std::string body = "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"9007199254740993\"}";
    CreateGroupResponse resp;
    REQUIRE(GroupProtocolClient::parseCreateGroupResponse(body, resp));
    CHECK(resp.m_groupid == 9007199254740993LL);
}

TEST_CASE("GP6 parseCreateGroupResponse 失败应答（仅 code/msg 有效）")
{
    std::string body = "{\"code\":300,\"msg\":\"create group failed\"}";
    CreateGroupResponse resp;
    REQUIRE(GroupProtocolClient::parseCreateGroupResponse(body, resp));
    CHECK(resp.m_code == 300);
    CHECK(resp.m_msg == "create group failed");
    //失败应答不带 groupid，保持默认 0。
    CHECK(resp.m_groupid == 0);
}

TEST_CASE("GP7 parseCreateGroupResponse 畸形输入安全失败")
{
    CreateGroupResponse resp;
    CHECK_FALSE(GroupProtocolClient::parseCreateGroupResponse("not json", resp));
    CHECK_FALSE(GroupProtocolClient::parseCreateGroupResponse("", resp));
    CHECK_FALSE(GroupProtocolClient::parseCreateGroupResponse("[1,2]", resp));
    //缺 code / code 类型错误。
    CHECK_FALSE(GroupProtocolClient::parseCreateGroupResponse("{\"msg\":\"x\"}", resp));
    CHECK_FALSE(GroupProtocolClient::parseCreateGroupResponse("{\"code\":\"0\"}", resp));
}

TEST_CASE("GP8 buildGroupIdRequest groupid 字符串编码且不丢精度")
{
    std::string json = GroupProtocolClient::buildGroupIdRequest(9007199254740993LL);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["groupid"].isString());
    CHECK(root["groupid"].asString() == "9007199254740993");
}

TEST_CASE("GP9 parseGroupInfoResponse 解析群资料")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"700001\",\"name\":\"\xE5\xBC\x80\xE5\x8F\x91\xE7\xBE\xA4\","
        "\"announcement\":\"hi\",\"ownerid\":\"12345\",\"joinmode\":1,\"muteall\":0,"
        "\"membercount\":25,\"myrole\":2,\"facetype\":0,\"customface\":\"\"}";
    GroupInfoResult out;
    REQUIRE(GroupProtocolClient::parseGroupInfoResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_groupid == 700001);
    CHECK(out.m_name == "\xE5\xBC\x80\xE5\x8F\x91\xE7\xBE\xA4");   // "开发群"（UTF-8 字节）
    CHECK(out.m_announcement == "hi");
    CHECK(out.m_ownerid == 12345);
    CHECK(out.m_joinmode == 1);
    CHECK(out.m_muteall == 0);
    CHECK(out.m_membercount == 25);
    CHECK(out.m_myrole == 2);
}

TEST_CASE("GP10 parseGroupInfoResponse 大 groupid/ownerid 不丢精度 + 失败/畸形")
{
    std::string body =
        "{\"code\":0,\"groupid\":\"9007199254740993\",\"ownerid\":\"9223372036854775807\","
        "\"name\":\"g\"}";
    GroupInfoResult out;
    REQUIRE(GroupProtocolClient::parseGroupInfoResponse(body, out));
    CHECK(out.m_groupid == 9007199254740993LL);
    CHECK(out.m_ownerid == 9223372036854775807LL);

    //失败应答：仅 code/msg 有效。
    GroupInfoResult fail;
    REQUIRE(GroupProtocolClient::parseGroupInfoResponse("{\"code\":2,\"msg\":\"no\"}", fail));
    CHECK(fail.m_code == 2);
    CHECK(fail.m_groupid == 0);

    //畸形输入安全失败。
    GroupInfoResult bad;
    CHECK_FALSE(GroupProtocolClient::parseGroupInfoResponse("not json", bad));
    CHECK_FALSE(GroupProtocolClient::parseGroupInfoResponse("{\"msg\":\"x\"}", bad));
}

TEST_CASE("GP11 parseGroupMembersResponse 解析成员数组")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"700001\",\"members\":["
        "{\"userid\":\"12345\",\"role\":2,\"card\":\"\xE7\xBE\xA4\xE4\xB8\xBB\",\"muted\":0,\"nickname\":\"a\"},"
        "{\"userid\":\"9007199254740993\",\"role\":0,\"card\":\"\",\"muted\":1,\"nickname\":\"b\"}"
        "]}";
    GroupMembersResult out;
    REQUIRE(GroupProtocolClient::parseGroupMembersResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_groupid == 700001);
    REQUIRE(out.m_members.size() == 2);
    CHECK(out.m_members[0].m_userid == 12345);
    CHECK(out.m_members[0].m_role == 2);
    CHECK(out.m_members[0].m_card == "\xE7\xBE\xA4\xE4\xB8\xBB");   // "群主"
    CHECK(out.m_members[0].m_nickname == "a");
    CHECK(out.m_members[1].m_userid == 9007199254740993LL);   // 大 id 不丢精度
    CHECK(out.m_members[1].m_muted == 1);
}

TEST_CASE("GP12 parseGroupMembersResponse 空成员/跳过非法 userid/畸形")
{
    GroupMembersResult empty;
    REQUIRE(GroupProtocolClient::parseGroupMembersResponse(
        "{\"code\":0,\"groupid\":\"1\",\"members\":[]}", empty));
    CHECK(empty.m_members.empty());

    //userid 缺失/为 0 的成员跳过。
    GroupMembersResult skip;
    REQUIRE(GroupProtocolClient::parseGroupMembersResponse(
        "{\"code\":0,\"groupid\":\"1\",\"members\":["
        "{\"role\":0},{\"userid\":\"0\"},{\"userid\":\"888\",\"nickname\":\"x\"}]}", skip));
    REQUIRE(skip.m_members.size() == 1);
    CHECK(skip.m_members[0].m_userid == 888);

    GroupMembersResult bad;
    CHECK_FALSE(GroupProtocolClient::parseGroupMembersResponse("nope", bad));
}

TEST_CASE("GP13 parseUserGroupsResponse 解析我的群列表")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groups\":["
        "{\"groupid\":\"268435456\",\"name\":\"\xE8\xAE\xA8\xE8\xAE\xBA\xE7\xBB\x84\",\"ownerid\":\"5308\",\"membercount\":4},"
        "{\"groupid\":\"268435457\",\"name\":\"\xE7\xBE\xA4\xE8\x81\x8A\",\"ownerid\":\"5308\",\"membercount\":1}"
        "]}";
    UserGroupsResult out;
    REQUIRE(GroupProtocolClient::parseUserGroupsResponse(body, out));
    CHECK(out.m_code == 0);
    REQUIRE(out.m_groups.size() == 2);
    CHECK(out.m_groups[0].m_groupid == 268435456);
    CHECK(out.m_groups[0].m_name == "\xE8\xAE\xA8\xE8\xAE\xBA\xE7\xBB\x84");   // "讨论组"
    CHECK(out.m_groups[0].m_ownerid == 5308);
    CHECK(out.m_groups[0].m_membercount == 4);
    CHECK(out.m_groups[1].m_groupid == 268435457);
    CHECK(out.m_groups[1].m_membercount == 1);
}

TEST_CASE("GP14 parseUserGroupsResponse 空列表/跳过非法 groupid/失败/畸形")
{
    //空群列表（用户没加任何群）：成功但 0 条。
    UserGroupsResult empty;
    REQUIRE(GroupProtocolClient::parseUserGroupsResponse(
        "{\"code\":0,\"groups\":[]}", empty));
    CHECK(empty.m_groups.empty());

    //groupid 缺失/为 0 的项跳过。
    UserGroupsResult skip;
    REQUIRE(GroupProtocolClient::parseUserGroupsResponse(
        "{\"code\":0,\"groups\":[{\"name\":\"x\"},{\"groupid\":\"0\"},{\"groupid\":\"77\"}]}", skip));
    REQUIRE(skip.m_groups.size() == 1);
    CHECK(skip.m_groups[0].m_groupid == 77);

    //大 groupid 不丢精度。
    UserGroupsResult big;
    REQUIRE(GroupProtocolClient::parseUserGroupsResponse(
        "{\"code\":0,\"groups\":[{\"groupid\":\"9007199254740993\"}]}", big));
    REQUIRE(big.m_groups.size() == 1);
    CHECK(big.m_groups[0].m_groupid == 9007199254740993LL);

    //失败应答 + 畸形输入。
    UserGroupsResult fail;
    REQUIRE(GroupProtocolClient::parseUserGroupsResponse("{\"code\":3,\"msg\":\"no\"}", fail));
    CHECK(fail.m_code == 3);
    CHECK(fail.m_groups.empty());
    UserGroupsResult bad;
    CHECK_FALSE(GroupProtocolClient::parseUserGroupsResponse("nope", bad));
}

TEST_CASE("GP15 buildSetMemberCardRequest 含 groupid(字符串) 与 card(UTF-8)")
{
    std::string json = GroupProtocolClient::buildSetMemberCardRequest(
        700001, "\xE6\x9E\x97\xE7\x9F\xA5\xE5\xA4\x8F");   // "林知夏"
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //groupid 以字符串编码（防大整数精度丢失）。
    CHECK(root["groupid"].isString());
    CHECK(root["groupid"].asString() == "700001");
    //card 原样写入（UTF-8）。
    CHECK(root["card"].isString());
    CHECK(root["card"].asString() == "\xE6\x9E\x97\xE7\x9F\xA5\xE5\xA4\x8F");

    //大 groupid 不丢精度。
    Json::Value big;
    REQUIRE(parseJsonText(
        GroupProtocolClient::buildSetMemberCardRequest(9007199254740993LL, "x"), big));
    CHECK(big["groupid"].asString() == "9007199254740993");

    //空名片合法（清空名片），card 字段仍在且为空串。
    Json::Value empty;
    REQUIRE(parseJsonText(
        GroupProtocolClient::buildSetMemberCardRequest(1, ""), empty));
    REQUIRE(empty["card"].isString());
    CHECK(empty["card"].asString() == "");
}

TEST_CASE("GP16 退群请求复用 buildGroupIdRequest：仅 groupid 字段")
{
    //cmd_leave_group(1309) 请求体与 buildGroupIdRequest 同形：只含 groupid。
    std::string json = GroupProtocolClient::buildGroupIdRequest(268435460LL);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["groupid"].isString());
    CHECK(root["groupid"].asString() == "268435460");
    //不应混入其它字段（退群只需群id）。
    REQUIRE(root.isObject());
    CHECK(root.getMemberNames().size() == 1);
}

TEST_CASE("GP17 parseCodeMsgResponse 通用 {code,msg} 解析")
{
    //成功应答。
    int code = -99;
    std::string msg;
    REQUIRE(GroupProtocolClient::parseCodeMsgResponse("{\"code\":0,\"msg\":\"ok\"}", code, msg));
    CHECK(code == 0);
    CHECK(msg == "ok");

    //失败应答：code/msg 均透出（含 UTF-8 描述）。
    int code2 = 0;
    std::string msg2;
    REQUIRE(GroupProtocolClient::parseCodeMsgResponse(
        "{\"code\":5,\"msg\":\"\xE9\x80\x80\xE7\xBE\xA4\xE5\xA4\xB1\xE8\xB4\xA5\"}", code2, msg2));   // "退群失败"
    CHECK(code2 == 5);
    CHECK(msg2 == "\xE9\x80\x80\xE7\xBE\xA4\xE5\xA4\xB1\xE8\xB4\xA5");

    //缺 msg：code 有效、msg 为空串，仍返回 true。
    int code3 = -1;
    std::string msg3 = "dirty";
    REQUIRE(GroupProtocolClient::parseCodeMsgResponse("{\"code\":0}", code3, msg3));
    CHECK(code3 == 0);
    CHECK(msg3 == "");

    //畸形 / 缺 code / code 类型错误：安全失败。
    int c = 123;
    std::string m = "untouched";
    CHECK_FALSE(GroupProtocolClient::parseCodeMsgResponse("not json", c, m));
    CHECK_FALSE(GroupProtocolClient::parseCodeMsgResponse("", c, m));
    CHECK_FALSE(GroupProtocolClient::parseCodeMsgResponse("[1,2]", c, m));
    CHECK_FALSE(GroupProtocolClient::parseCodeMsgResponse("{\"msg\":\"x\"}", c, m));
    CHECK_FALSE(GroupProtocolClient::parseCodeMsgResponse("{\"code\":\"0\"}", c, m));
}

// =====================================================================
// 群公告（1323 / 1324 / 1325 / 1326）
// =====================================================================

TEST_CASE("GP40 buildGetAnnouncementsRequest 分页字段", "[group][announcement]")
{
    Json::Value root;
    REQUIRE(parseJsonText(
        GroupProtocolClient::buildGetAnnouncementsRequest(10000000001LL, 20, 10), root));
    //64 位群ID 以字符串编码，避免大整数在 JSON 里丢精度
    CHECK(root["groupid"].asString() == "10000000001");
    CHECK(root["offset"].asInt() == 20);
    CHECK(root["count"].asInt() == 10);

    //群聊窗边栏一次只看一条：offset 走当前下标、count 取 1，公告再多也不受上限影响
    Json::Value single;
    REQUIRE(parseJsonText(
        GroupProtocolClient::buildGetAnnouncementsRequest(10000000001LL, 137, 1), single));
    CHECK(single["offset"].asInt() == 137);
    CHECK(single["count"].asInt() == 1);
}

TEST_CASE("GP41 buildPublishAnnouncementRequest 与重新发布的区别", "[group][announcement]")
{
    Json::Value root;
    REQUIRE(parseJsonText(
        GroupProtocolClient::buildPublishAnnouncementRequest(
            10000000001LL, "title", "body", 2), root));
    CHECK(root["groupid"].asString() == "10000000001");
    CHECK(root["title"].asString() == "title");
    CHECK(root["content"].asString() == "body");
    CHECK(root["severity"].asInt() == 2);

    //重新发布只带 id：本地那份标题正文可能已经是旧的，不能拿它当新公告发出去
    Json::Value again;
    REQUIRE(parseJsonText(
        GroupProtocolClient::buildRepublishAnnouncementRequest(10000000001LL, 12), again));
    CHECK(again["id"].asString() == "12");
    CHECK_FALSE(again.isMember("title"));
    CHECK_FALSE(again.isMember("content"));

    Json::Value del;
    REQUIRE(parseJsonText(
        GroupProtocolClient::buildDeleteAnnouncementRequest(10000000001LL, 12), del));
    CHECK(del["groupid"].asString() == "10000000001");
    CHECK(del["id"].asString() == "12");
}

TEST_CASE("GP42 parseAnnouncementsResponse 正常应答逐字段还原", "[group][announcement]")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"total\":5,"
        "\"announcements\":[{\"id\":\"12\",\"groupid\":\"10000000001\",\"title\":\"t1\","
        "\"content\":\"c1\",\"severity\":1,\"publisher\":\"100023\",\"publishername\":\"lin\","
        "\"publishtime\":1700000000000}]}";

    GroupAnnouncementsResult out;
    REQUIRE(GroupProtocolClient::parseAnnouncementsResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_groupid == 10000000001LL);
    //total 是"本群公告总条数"，与本页条数各归各 —— 界面上的页数与导航徽章用它
    CHECK(out.m_total == 5);
    REQUIRE(out.m_announcements.size() == 1);
    CHECK(out.m_announcements[0].m_id == 12);
    CHECK(out.m_announcements[0].m_title == "t1");
    CHECK(out.m_announcements[0].m_content == "c1");
    CHECK(out.m_announcements[0].m_severity == 1);
    CHECK(out.m_announcements[0].m_publisher == 100023);
    CHECK(out.m_announcements[0].m_publishername == "lin");
    CHECK(out.m_announcements[0].m_publishtime == 1700000000000LL);
}

TEST_CASE("GP43 parseAnnouncementsResponse 空列表与失败应答", "[group][announcement]")
{
    //空列表是正常状态（本群还没发过公告），不是错误
    GroupAnnouncementsResult empty;
    REQUIRE(GroupProtocolClient::parseAnnouncementsResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"total\":0,\"announcements\":[]}",
        empty));
    CHECK(empty.m_code == 0);
    CHECK(empty.m_announcements.empty());
    CHECK(empty.m_total == 0);

    //失败应答：code / msg 有效即可，不再往下解析
    GroupAnnouncementsResult denied;
    REQUIRE(GroupProtocolClient::parseAnnouncementsResponse(
        "{\"code\":302,\"msg\":\"not group member\"}", denied));
    CHECK(denied.m_code == 302);
    CHECK(denied.m_announcements.empty());

    //畸形输入安全失败
    GroupAnnouncementsResult bad;
    CHECK_FALSE(GroupProtocolClient::parseAnnouncementsResponse("not json", bad));
    CHECK_FALSE(GroupProtocolClient::parseAnnouncementsResponse("[1,2]", bad));
    CHECK_FALSE(GroupProtocolClient::parseAnnouncementsResponse("{\"msg\":\"x\"}", bad));
}

TEST_CASE("GP44 parseAnnouncementsResponse 跳过无ID项并兜住偏小的 total", "[group][announcement]")
{
    //没有公告ID的项撤不掉也重发不了，留在列表里只会让按钮点了没反应，故跳过。
    //本例服务端给的 total=1 比留下的条数还小，客户端要把它抬到实际条数，
    //否则界面上会画出"2/1 页"这种读不通的东西。
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"total\":1,"
        "\"announcements\":["
        "{\"id\":\"0\",\"title\":\"no id\"},"
        "{\"id\":\"12\",\"title\":\"t1\"},"
        "{\"id\":\"13\",\"title\":\"t2\"}]}";

    GroupAnnouncementsResult out;
    REQUIRE(GroupProtocolClient::parseAnnouncementsResponse(body, out));
    REQUIRE(out.m_announcements.size() == 2);
    CHECK(out.m_announcements[0].m_id == 12);
    CHECK(out.m_announcements[1].m_id == 13);
    CHECK(out.m_total == 2);
}

TEST_CASE("GP45 parsePublishAnnouncementResponse 取回新公告ID", "[group][announcement]")
{
    int code = -1;
    std::string msg;
    int64_t id = 0;
    REQUIRE(GroupProtocolClient::parsePublishAnnouncementResponse(
        "{\"code\":0,\"msg\":\"ok\",\"id\":\"15\"}", code, msg, id));
    CHECK(code == 0);
    CHECK(msg == "ok");
    CHECK(id == 15);

    //失败应答：id 为 0
    int code2 = -1;
    std::string msg2;
    int64_t id2 = 99;
    REQUIRE(GroupProtocolClient::parsePublishAnnouncementResponse(
        "{\"code\":303,\"msg\":\"denied\",\"id\":\"0\"}", code2, msg2, id2));
    CHECK(code2 == 303);
    CHECK(id2 == 0);

    int c = 1;
    std::string m;
    int64_t i = 0;
    CHECK_FALSE(GroupProtocolClient::parsePublishAnnouncementResponse("not json", c, m, i));
    CHECK_FALSE(GroupProtocolClient::parsePublishAnnouncementResponse("{\"msg\":\"x\"}", c, m, i));
}

TEST_CASE("GP46 parseAnnouncementNotify 两种 changetype", "[group][announcement]")
{
    const std::string publish =
        "{\"changetype\":1,\"groupid\":\"10000000001\","
        "\"announcement\":{\"id\":\"15\",\"groupid\":\"10000000001\",\"title\":\"t\","
        "\"content\":\"c\",\"severity\":0,\"publisher\":\"100023\",\"publishername\":\"lin\","
        "\"publishtime\":1700000000000}}";

    GroupAnnouncementNotify out;
    REQUIRE(GroupProtocolClient::parseAnnouncementNotify(publish, out));
    CHECK(out.m_changetype == 1);
    CHECK(out.m_groupid == 10000000001LL);
    //推送体里带完整公告，界面收到即可直接刷新 / 弹窗，不必再拉一次列表
    CHECK(out.m_announcement.m_id == 15);
    CHECK(out.m_announcement.m_title == "t");
    CHECK(out.m_announcement.m_publishername == "lin");

    //删除推送：那一条已经从库里没了，客户端只能靠推送体里的 id 把它从列表摘掉
    const std::string del =
        "{\"changetype\":2,\"groupid\":\"10000000001\","
        "\"announcement\":{\"id\":\"15\",\"title\":\"t\"}}";
    GroupAnnouncementNotify delOut;
    REQUIRE(GroupProtocolClient::parseAnnouncementNotify(del, delOut));
    CHECK(delOut.m_changetype == 2);
    CHECK(delOut.m_announcement.m_id == 15);
    CHECK(delOut.m_announcement.m_title == "t");
}

TEST_CASE("GP47 parseAnnouncementNotify 缺关键字段时安全失败", "[group][announcement]")
{
    GroupAnnouncementNotify out;

    //缺 changetype
    CHECK_FALSE(GroupProtocolClient::parseAnnouncementNotify(
        "{\"groupid\":\"10000000001\",\"announcement\":{\"id\":\"15\"}}", out));

    //缺 announcement 子对象
    CHECK_FALSE(GroupProtocolClient::parseAnnouncementNotify(
        "{\"changetype\":1,\"groupid\":\"10000000001\"}", out));

    //公告ID非正：界面要按它去重（同一条公告只弹一次窗）、按它定位列表项，缺它没法用
    CHECK_FALSE(GroupProtocolClient::parseAnnouncementNotify(
        "{\"changetype\":1,\"groupid\":\"10000000001\",\"announcement\":{\"id\":\"0\"}}", out));

    CHECK_FALSE(GroupProtocolClient::parseAnnouncementNotify("not json", out));

    //外层 groupid 缺失时用公告自身的 groupid 兜底
    GroupAnnouncementNotify fallback;
    REQUIRE(GroupProtocolClient::parseAnnouncementNotify(
        "{\"changetype\":1,\"announcement\":{\"id\":\"15\",\"groupid\":\"10000000001\"}}",
        fallback));
    CHECK(fallback.m_groupid == 10000000001LL);
}

TEST_CASE("GP48 群资料应答带出最新公告快照三项", "[group][announcement]")
{
    //群聊窗头部那行公告摘要直接读这三项，开窗时不必为一行摘要额外发一条请求
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"name\":\"team\","
        "\"announcement\":\"latest body\",\"announceid\":\"15\","
        "\"announcetitle\":\"latest title\",\"announcetime\":1700000000000,\"myrole\":2}";

    GroupInfoResult out;
    REQUIRE(GroupProtocolClient::parseGroupInfoResponse(body, out));
    CHECK(out.m_announceid == 15);
    CHECK(out.m_announcetitle == "latest title");
    CHECK(out.m_announcetime == 1700000000000LL);
    CHECK(out.m_announcement == "latest body");

    //老服务端不回这三项时保留"本群还没有公告"的初值，界面上不显示摘要行
    const std::string old =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"name\":\"team\",\"myrole\":2}";
    GroupInfoResult legacy;
    REQUIRE(GroupProtocolClient::parseGroupInfoResponse(old, legacy));
    CHECK(legacy.m_announceid == 0);
    CHECK(legacy.m_announcetitle.empty());
    CHECK(legacy.m_announcetime == 0);
}

// =====================================================================
// 群头像（cmd_update_group_info 的 facetype / customface 两项）
// =====================================================================

TEST_CASE("GP50 只改群头像时请求里不夹带群名 / 简介等其它项", "[group][groupavatar]")
{
    //管理页的「更换头像」是独立入口，与「保存修改」互不相干：只改头像发出去的
    //请求就该只有 groupid + 头像两项，免得把界面上没碰过的项按当前显示值回写。
    UpdateGroupInfoParams p;
    p.m_groupid       = 10000000001LL;
    p.m_hasFacetype   = true;
    p.m_facetype      = 2;
    p.m_hasCustomface = true;
    p.m_customface    = "9f8e7d6c5b4a39281706";

    Json::Value root;
    REQUIRE(parseJsonText(GroupProtocolClient::buildUpdateGroupInfoRequest(p), root));
    CHECK(root["groupid"].asString() == "10000000001");
    CHECK(root["facetype"].asInt() == 2);
    CHECK(root["customface"].asString() == "9f8e7d6c5b4a39281706");
    //以下各项本次没改，一个都不该出现。
    CHECK_FALSE(root.isMember("name"));
    CHECK_FALSE(root.isMember("intro"));
    CHECK_FALSE(root.isMember("announcement"));
    CHECK_FALSE(root.isMember("joinmode"));
}

TEST_CASE("GP51 头像与群名同时改时两者都在", "[group][groupavatar]")
{
    UpdateGroupInfoParams p;
    p.m_groupid       = 10000000002LL;
    p.m_hasName       = true;
    p.m_name          = u8"技术部";
    p.m_hasFacetype   = true;
    p.m_facetype      = 1;
    p.m_hasCustomface = true;
    p.m_customface    = u8"技|3";

    Json::Value root;
    REQUIRE(parseJsonText(GroupProtocolClient::buildUpdateGroupInfoRequest(p), root));
    CHECK(root["name"].asString() == u8"技术部");
    CHECK(root["facetype"].asInt() == 1);
    CHECK(root["customface"].asString() == u8"技|3");
}

TEST_CASE("GP52 恢复默认头像：facetype=0 与空 customface 两个键都必须写出", "[group][groupavatar]")
{
    //这是本组最要紧的一条。恢复默认发出去的正是 facetype=0 + customface=""，
    //若实现把"零值 / 空串"当成没改而省略，服务端对应的 has_* 就是假，库里旧的
    //fileid 清不掉 —— 界面上看着就是"点了恢复默认但头像没变"。
    UpdateGroupInfoParams p;
    p.m_groupid       = 10000000003LL;
    p.m_hasFacetype   = true;
    p.m_facetype      = 0;
    p.m_hasCustomface = true;
    p.m_customface    = "";

    Json::Value root;
    REQUIRE(parseJsonText(GroupProtocolClient::buildUpdateGroupInfoRequest(p), root));
    REQUIRE(root.isMember("facetype"));
    REQUIRE(root.isMember("customface"));
    CHECK(root["facetype"].asInt() == 0);
    CHECK(root["customface"].asString().empty());
}

TEST_CASE("GP53 头像两项各自独立：开关为假时不写出", "[group][groupavatar]")
{
    //只改群名时不能捎带头像两项，否则会把别人刚设的群头像按本端旧值覆盖掉。
    UpdateGroupInfoParams p;
    p.m_groupid = 10000000004LL;
    p.m_hasName = true;
    p.m_name    = u8"运维组";

    Json::Value root;
    REQUIRE(parseJsonText(GroupProtocolClient::buildUpdateGroupInfoRequest(p), root));
    CHECK_FALSE(root.isMember("facetype"));
    CHECK_FALSE(root.isMember("customface"));
}

TEST_CASE("GP54 parseUserGroupsResponse 取出群头像两项", "[group][groupavatar]")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groups\":["
        "{\"groupid\":\"10000000001\",\"name\":\"team\",\"ownerid\":\"9\","
        "\"membercount\":8,\"facetype\":2,\"customface\":\"abc123fileid\"},"
        "{\"groupid\":\"10000000002\",\"name\":\"ops\",\"ownerid\":\"9\","
        "\"membercount\":3,\"facetype\":1,\"customface\":\"A|5\"}]}";

    UserGroupsResult out;
    REQUIRE(GroupProtocolClient::parseUserGroupsResponse(body, out));
    REQUIRE(out.m_groups.size() == 2);
    CHECK(out.m_groups[0].m_facetype == 2);
    CHECK(out.m_groups[0].m_customface == "abc123fileid");
    CHECK(out.m_groups[1].m_facetype == 1);
    CHECK(out.m_groups[1].m_customface == "A|5");
}

TEST_CASE("GP55 老服务端不带 customface 时取空串且不影响其它字段", "[group][groupavatar]")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groups\":["
        "{\"groupid\":\"10000000001\",\"name\":\"team\",\"ownerid\":\"9\",\"membercount\":8}]}";

    UserGroupsResult out;
    REQUIRE(GroupProtocolClient::parseUserGroupsResponse(body, out));
    REQUIRE(out.m_groups.size() == 1);
    CHECK(out.m_groups[0].m_facetype == 0);
    CHECK(out.m_groups[0].m_customface.empty());
    CHECK(out.m_groups[0].m_name == "team");
    CHECK(out.m_groups[0].m_membercount == 8);
}

TEST_CASE("GP49 公告应答回显分页参数，各处据此认领自己那一份", "[group][announcement]")
{
    //背景：1323 应答由主面板同时投给群管理窗与群聊窗的公告边栏，而两边要的页大小
    //不同（群管理窗每页 8 条、边栏一次 1 条）。没有回显时两处只能来者不拒，后到的
    //那份会把另一处的列表覆盖掉 —— 现场表现为群管理页显示「共 3 条」但列表里只有
    //1 条（那 1 条正是边栏 count=1 拉回来的）。
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"total\":3,"
        "\"offset\":0,\"count\":1,"
        "\"announcements\":[{\"id\":\"12\",\"title\":\"t1\"}]}";

    GroupAnnouncementsResult out;
    REQUIRE(GroupProtocolClient::parseAnnouncementsResponse(body, out));
    CHECK(out.m_offset == 0);
    CHECK(out.m_count == 1);

    //边栏（offset=0, count=1）认得出这是自己的
    CHECK(out.MatchesRequest(0, 1));
    //群管理窗（offset=0, count=8）要能把它挡掉 —— 这就是那个 bug 的修复点
    CHECK_FALSE(out.MatchesRequest(0, 8));
    //翻到第二页的群管理窗（offset=8）同样挡掉
    CHECK_FALSE(out.MatchesRequest(8, 8));
}

TEST_CASE("GP50 老服务端不回显分页参数时退回来者不拒", "[group][announcement]")
{
    //回显是本轮新加的字段。连老服务端时两项都缺，此时不能一律拒收 —— 那会让公告
    //列表整个空掉，比"偶尔被对方覆盖"糟得多。
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"total\":3,"
        "\"announcements\":[{\"id\":\"12\",\"title\":\"t1\"}]}";

    GroupAnnouncementsResult out;
    REQUIRE(GroupProtocolClient::parseAnnouncementsResponse(body, out));
    CHECK(out.m_offset == -1);
    CHECK(out.m_count == -1);

    //谁问都认
    CHECK(out.MatchesRequest(0, 1));
    CHECK(out.MatchesRequest(0, 8));
    CHECK(out.MatchesRequest(80, 8));
}

TEST_CASE("GP51 还没发过请求的窗口不认领任何应答", "[group][announcement]")
{
    //两个窗口都用 -1 表示"还没发过拉公告的请求"。此时收到的应答必定是别人的，
    //收下就会平白显示出一份自己没要过的数据。
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"10000000001\",\"total\":3,"
        "\"offset\":0,\"count\":8,"
        "\"announcements\":[{\"id\":\"12\",\"title\":\"t1\"}]}";

    GroupAnnouncementsResult out;
    REQUIRE(GroupProtocolClient::parseAnnouncementsResponse(body, out));
    CHECK_FALSE(out.MatchesRequest(-1, -1));
}
