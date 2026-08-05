/**
 *  v2 通讯录报文编解码（客户端侧）单元测试（Suite CT），ContactsProtocolTests.cpp
 *  覆盖：parseContactsResponse 成功/失败应答、联系人字段与 64 位 userid 精度、
 *       空联系人池、畸形输入安全失败、跳过非法 userid 条目。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-01
 */
#include "catch_amalgamated.hpp"
#include "../net/ContactsProtocolClient.h"

using namespace net;

TEST_CASE("CT1 parseContactsResponse 解析成功应答")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"synctime\":1716268800000,"
        "\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"\xE6\x98\x8E\xE8\xBF\x9C\xE7\xA7\x91\xE6\x8A\x80\"},"
        "\"contacts\":["
        "{\"userid\":\"5002\",\"username\":\"MY0002\",\"nickname\":\"yelei\",\"usertype\":0,"
        "\"facetype\":1,\"customface\":\"L|3\",\"signature\":\"hi\",\"address\":\"Shanghai\",\"presence\":1,\"clienttype\":1},"
        "{\"userid\":\"5003\",\"username\":\"MY0003\",\"nickname\":\"hufm\",\"usertype\":0,"
        "\"facetype\":0,\"signature\":\"\",\"presence\":4,\"clienttype\":0}"
        "],\"departments\":[],\"favorites\":[],\"serviceaccounts\":[],\"groups\":[]}";

    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_enterpriseid == 1);
    CHECK(out.m_contacts.size() == 2);
    CHECK(out.m_contacts[0].m_userid == 5002);
    CHECK(out.m_contacts[0].m_username == "MY0002");
    CHECK(out.m_contacts[0].m_nickname == "yelei");
    CHECK(out.m_contacts[0].m_address == "Shanghai");
    CHECK(out.m_contacts[0].m_presence == 1);
    CHECK(out.m_contacts[0].m_facetype == 1);
    CHECK(out.m_contacts[0].m_customface == "L|3");   // 字形头像 customface 被解析、不再丢弃
    CHECK(out.m_contacts[1].m_customface.empty());     // 缺字段时为空串
    CHECK(out.m_contacts[1].m_userid == 5003);
    CHECK(out.m_contacts[1].m_presence == 4);
}

TEST_CASE("CT1b 解析联系人备注名")
{
    // 备注名经 cmd_set_friend_remark(1209) 设置、随通讯录应答读回。不解析它的话，
    // 用户改完备注重登就丢了 —— 界面会退回显示昵称。
    // "产品组同事" 的 UTF-8 字节，与本文件其余中文串写法一致（源码走 /utf-8 编译，
    // 但这里沿用显式字节，避免依赖编译选项）。
    const char* kRemarkUtf8 = "\xE4\xBA\xA7\xE5\x93\x81\xE7\xBB\x84\xE5\x90\x8C\xE4\xBA\x8B";

    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"x\"},"
        "\"contacts\":["
        "{\"userid\":\"5002\",\"nickname\":\"yelei\",\"presence\":1,\"remark\":\"";
    body += kRemarkUtf8;
    body += "\"},"
        "{\"userid\":\"5003\",\"nickname\":\"hufm\",\"presence\":0,\"remark\":\"\"}"
        "]}";

    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    REQUIRE(out.m_contacts.size() == 2);
    CHECK(out.m_contacts[0].m_remark == kRemarkUtf8);
    // 空串 = 未设置 / 已清除，两者显示效果相同，不必区分
    CHECK(out.m_contacts[1].m_remark.empty());
}

TEST_CASE("CT1c 应答不带 remark 字段时备注为空串")
{
    // 兼容旧服务端：字段整体缺失是合法情形，按"未设备注"处理，显示时回落到昵称，
    // 不能因此把整条联系人丢掉、也不能读出脏值。
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"x\"},"
        "\"contacts\":[{\"userid\":\"5002\",\"nickname\":\"yelei\",\"presence\":1}]}";

    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    REQUIRE(out.m_contacts.size() == 1);
    CHECK(out.m_contacts[0].m_remark.empty());
}

TEST_CASE("CT2 userid 大整数不丢精度（字符串编码）")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"x\"},"
        "\"contacts\":[{\"userid\":\"9007199254740993\",\"nickname\":\"big\",\"presence\":0}]}";
    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    REQUIRE(out.m_contacts.size() == 1);
    CHECK(out.m_contacts[0].m_userid == 9007199254740993LL);   // 2^53 + 1
}

TEST_CASE("CT3 空联系人池：成功但 0 条")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"x\"},"
        "\"contacts\":[]}";
    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_contacts.empty());
}

TEST_CASE("CT4 失败应答：仅 code/msg 有效")
{
    std::string body = "{\"code\":2,\"msg\":\"server error\"}";
    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    CHECK(out.m_code == 2);
    CHECK(out.m_msg == "server error");
    CHECK(out.m_contacts.empty());
}

TEST_CASE("CT5 畸形输入安全失败、跳过非法 userid 条目")
{
    ContactsResult out;
    //非 JSON / 空串 / 非对象 / 缺 code
    CHECK_FALSE(ContactsProtocolClient::parseContactsResponse("not json", out));
    CHECK_FALSE(ContactsProtocolClient::parseContactsResponse("", out));
    CHECK_FALSE(ContactsProtocolClient::parseContactsResponse("[1,2]", out));
    CHECK_FALSE(ContactsProtocolClient::parseContactsResponse("{\"msg\":\"x\"}", out));

    //userid 缺失或为 0 的联系人条目应被跳过，不产生死项
    std::string body =
        "{\"code\":0,\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"x\"},"
        "\"contacts\":[{\"nickname\":\"noid\",\"presence\":0},"
        "{\"userid\":\"0\",\"nickname\":\"zero\"},"
        "{\"userid\":\"5005\",\"nickname\":\"ok\",\"presence\":0}]}";
    ContactsResult out2;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out2));
    REQUIRE(out2.m_contacts.size() == 1);
    CHECK(out2.m_contacts[0].m_userid == 5005);
}

TEST_CASE("CT6 解析部门树与成员")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\","
        "\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"x\"},"
        "\"departments\":["
        "{\"deptid\":\"1610612737\",\"name\":\"\xE6\x80\xBB\xE9\x83\xA8\",\"parentdeptid\":\"0\","
        "\"deptorder\":0,\"leaderuserid\":\"5001\",\"members\":["
        "{\"userid\":\"5001\",\"position\":\"CEO\",\"isprimary\":1,\"memberorder\":0},"
        "{\"userid\":\"5002\",\"position\":\"\",\"isprimary\":1,\"memberorder\":3}]},"
        "{\"deptid\":\"1610612738\",\"name\":\"\xE6\x8A\x80\xE6\x9C\xAF\",\"parentdeptid\":\"1610612737\","
        "\"deptorder\":1,\"leaderuserid\":\"5002\",\"members\":[]}"
        "],\"contacts\":[]}";

    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    REQUIRE(out.m_departments.size() == 2);
    CHECK(out.m_departments[0].m_deptid == 1610612737LL);
    CHECK(out.m_departments[0].m_parentdeptid == 0);
    CHECK(out.m_departments[0].m_leaderuserid == 5001);
    REQUIRE(out.m_departments[0].m_members.size() == 2);
    CHECK(out.m_departments[0].m_members[0].m_userid == 5001);
    CHECK(out.m_departments[0].m_members[0].m_isprimary == 1);
    CHECK(out.m_departments[0].m_members[1].m_userid == 5002);
    CHECK(out.m_departments[0].m_members[1].m_memberorder == 3);
    //第二个部门：子部门，挂在 1610612737 下，成员为空
    CHECK(out.m_departments[1].m_deptid == 1610612738LL);
    CHECK(out.m_departments[1].m_parentdeptid == 1610612737LL);
    CHECK(out.m_departments[1].m_members.empty());
}

// =====================================================================
// CT.PRESENCE. cmd_presence_changed_notify(1109) 推送解析契约
// =====================================================================

TEST_CASE("CT7 parsePresenceChangedNotify 正常解析（userid 字符串编码）")
{
    PresenceChangedNotify out;
    bool ok = ContactsProtocolClient::parsePresenceChangedNotify(
        "{\"userid\":\"5002\",\"presence\":2,\"clienttype\":1}", out);
    REQUIRE(ok);
    CHECK(out.m_userid == 5002);
    CHECK(out.m_presence == 2);
    CHECK(out.m_clienttype == 1);
}

TEST_CASE("CT8 parsePresenceChangedNotify userid 大整数不丢精度")
{
    PresenceChangedNotify out;
    bool ok = ContactsProtocolClient::parsePresenceChangedNotify(
        "{\"userid\":\"9007199254740993\",\"presence\":0}", out);
    REQUIRE(ok);
    CHECK(out.m_userid == 9007199254740993LL);   // 2^53 + 1
    CHECK(out.m_presence == 0);
    CHECK(out.m_clienttype == 0);                // 缺 clienttype 取 0
}

TEST_CASE("CT9 parsePresenceChangedNotify 缺 presence / 非法 userid / 畸形输入 → false")
{
    PresenceChangedNotify out;
    //缺 presence
    CHECK_FALSE(ContactsProtocolClient::parsePresenceChangedNotify("{\"userid\":\"5002\"}", out));
    //userid 缺失
    CHECK_FALSE(ContactsProtocolClient::parsePresenceChangedNotify("{\"presence\":1}", out));
    //userid 为 0
    CHECK_FALSE(ContactsProtocolClient::parsePresenceChangedNotify("{\"userid\":\"0\",\"presence\":1}", out));
    //非 JSON / 空串 / 非对象
    CHECK_FALSE(ContactsProtocolClient::parsePresenceChangedNotify("not json", out));
    CHECK_FALSE(ContactsProtocolClient::parsePresenceChangedNotify("", out));
    CHECK_FALSE(ContactsProtocolClient::parsePresenceChangedNotify("[1,2]", out));
}

// =====================================================================
// CT.USERINFO. cmd_user_info_changed_notify(1108) 推送解析契约
// =====================================================================

TEST_CASE("CT10 parseUserInfoChangedNotify 解出 userid + profile 内头像/昵称/签名")
{
    UserInfoChangedNotify out;
    bool ok = ContactsProtocolClient::parseUserInfoChangedNotify(
        "{\"userid\":\"5002\",\"profile\":{\"facetype\":2,\"customface\":\"123456\","
        "\"nickname\":\"yelei\",\"signature\":\"hello world\"}}", out);
    REQUIRE(ok);
    CHECK(out.m_userid == 5002);
    CHECK(out.m_facetype == 2);
    CHECK(out.m_customface == "123456");
    CHECK(out.m_nickname == "yelei");
    CHECK(out.m_signature == "hello world");
}

TEST_CASE("CT10b parseUserInfoChangedNotify profile 缺 signature → 空串")
{
    UserInfoChangedNotify out;
    bool ok = ContactsProtocolClient::parseUserInfoChangedNotify(
        "{\"userid\":\"5002\",\"profile\":{\"nickname\":\"yelei\"}}", out);
    REQUIRE(ok);
    CHECK(out.m_nickname == "yelei");
    CHECK(out.m_signature.empty());
}

TEST_CASE("CT11 parseUserInfoChangedNotify 大 userid 不丢精度 / profile 缺字段取默认")
{
    UserInfoChangedNotify out;
    bool ok = ContactsProtocolClient::parseUserInfoChangedNotify(
        "{\"userid\":\"9007199254740993\",\"profile\":{\"facetype\":1,\"customface\":\"L|3\"}}", out);
    REQUIRE(ok);
    CHECK(out.m_userid == 9007199254740993LL);
    CHECK(out.m_facetype == 1);
    CHECK(out.m_customface == "L|3");
    CHECK(out.m_nickname.empty());   // 缺 nickname → 空串
}

TEST_CASE("CT12 parseUserInfoChangedNotify 非法 userid / 畸形输入 → false")
{
    UserInfoChangedNotify out;
    CHECK_FALSE(ContactsProtocolClient::parseUserInfoChangedNotify("{\"profile\":{\"facetype\":1}}", out)); //缺 userid
    CHECK_FALSE(ContactsProtocolClient::parseUserInfoChangedNotify("{\"userid\":\"0\"}", out));            //userid 为 0
    CHECK_FALSE(ContactsProtocolClient::parseUserInfoChangedNotify("not json", out));
    CHECK_FALSE(ContactsProtocolClient::parseUserInfoChangedNotify("", out));
}

TEST_CASE("CT13 parseContactsResponse 解出 favorites：联系人与群混排")
{
    //「我的收藏」随通讯录一起下发。每项是 {ctype, cid} 对象 —— 只给裸 ID 分不出
    //收藏的是人还是群。
    const char* body =
        "{\"code\":0,\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"明远科技\"},"
        "\"departments\":[],\"contacts\":[],\"groups\":[],\"serviceaccounts\":[],"
        "\"favorites\":["
        "{\"ctype\":1,\"cid\":\"5081\"},"
        "{\"ctype\":2,\"cid\":\"268435456\"},"
        "{\"ctype\":1,\"cid\":\"10000\"}"
        "]}";

    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    REQUIRE(out.m_favorites.size() == 3);

    //顺序保持服务端给的（按收藏时间倒序），本端不再排序
    CHECK(out.m_favorites[0].m_ctype == 1);
    CHECK(out.m_favorites[0].m_cid == 5081);
    CHECK(out.m_favorites[1].m_ctype == 2);
    CHECK(out.m_favorites[1].m_cid == 268435456LL);
    CHECK(out.m_favorites[2].m_ctype == 1);
    CHECK(out.m_favorites[2].m_cid == 10000);   //服务号也能收藏，走 ctype=1
}

TEST_CASE("CT14 parseContactsResponse 缺 favorites 字段 → 收藏为空，不报错")
{
    //老服务端不带这个字段。此时收藏为空、界面隐藏收藏分区，但整条通讯录必须照常可用。
    const char* body =
        "{\"code\":0,\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"明远科技\"},"
        "\"departments\":[],\"contacts\":[],\"groups\":[]}";

    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    CHECK(out.m_favorites.empty());
}

TEST_CASE("CT15 parseContactsResponse favorites 里的非法项被跳过，其余照常解析")
{
    //收藏里混进一条脏数据，不该连整个通讯录都收不到。
    const char* body =
        "{\"code\":0,\"enterprise\":{\"enterpriseid\":\"1\",\"name\":\"明远科技\"},"
        "\"departments\":[],\"contacts\":[],\"groups\":[],"
        "\"favorites\":["
        "{\"ctype\":1,\"cid\":\"5081\"},"
        "{\"ctype\":3,\"cid\":\"10000\"},"      //ctype 越界（3 是服务号会话，不是收藏对象）
        "{\"ctype\":1,\"cid\":\"0\"},"          //cid 非法
        "{\"ctype\":1,\"cid\":\"abc\"},"        //cid 不是数字
        "\"not an object\","                    //整项不是对象
        "{\"ctype\":2,\"cid\":\"700001\"}"
        "]}";

    ContactsResult out;
    REQUIRE(ContactsProtocolClient::parseContactsResponse(body, out));
    REQUIRE(out.m_favorites.size() == 2);
    CHECK(out.m_favorites[0].m_cid == 5081);
    CHECK(out.m_favorites[1].m_cid == 700001);
}
