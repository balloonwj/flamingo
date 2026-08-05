/**
 *  v2 账号资料报文编解码单元测试（Suite U），test_profileprotocol.cpp
 *  覆盖：资料更新解析（全字段/部分字段/非法）、改密码/头像/状态解析、
 *       get_user_info 应答、资料变更通知、在线状态变更通知。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/ProfileProtocol.h"
#include "../jsoncpp1.9.0/json.h"

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

TEST_CASE("U1 parseUpdateProfileRequest 解析全部字段")
{
    std::string body = "{\"nickname\":\"林知夏\",\"signature\":\"hi\",\"gender\":0,"
                       "\"birthday\":19960812,\"address\":\"上海\",\"mail\":\"a@b.c\"}";
    UpdateProfileRequest req;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest(body, req));
    CHECK(req.has_nickname);
    CHECK(req.nickname == "林知夏");
    CHECK(req.has_signature);
    CHECK(req.has_gender);
    CHECK(req.gender == 0);
    CHECK(req.has_birthday);
    CHECK(req.birthday == 19960812);
    CHECK(req.has_address);
    CHECK(req.has_mail);
}

TEST_CASE("U2 parseUpdateProfileRequest 部分字段：只改昵称")
{
    UpdateProfileRequest req;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest("{\"nickname\":\"新昵称\"}", req));
    CHECK(req.has_nickname);
    CHECK(req.nickname == "新昵称");
    //其余字段未出现，has_* 应为 false
    CHECK_FALSE(req.has_signature);
    CHECK_FALSE(req.has_gender);
    CHECK_FALSE(req.has_birthday);
    CHECK_FALSE(req.has_address);
    CHECK_FALSE(req.has_mail);
}

TEST_CASE("U3 parseUpdateProfileRequest 非法 JSON 被拒绝")
{
    UpdateProfileRequest req;
    CHECK_FALSE(ProfileProtocol::parseUpdateProfileRequest("not a json", req));
    CHECK_FALSE(ProfileProtocol::parseUpdateProfileRequest("[1,2,3]", req));
}

TEST_CASE("U4 parseChangePasswordRequest 解析与校验")
{
    ChangePasswordRequest req;
    REQUIRE(ProfileProtocol::parseChangePasswordRequest(
        "{\"oldpassword\":\"oldhash\",\"newpassword\":\"newhash\"}", req));
    CHECK(req.oldpassword == "oldhash");
    CHECK(req.newpassword == "newhash");
    //缺字段、空串均拒绝
    CHECK_FALSE(ProfileProtocol::parseChangePasswordRequest("{\"oldpassword\":\"x\"}", req));
    CHECK_FALSE(ProfileProtocol::parseChangePasswordRequest(
        "{\"oldpassword\":\"\",\"newpassword\":\"y\"}", req));
}

TEST_CASE("U5 parseChangeAvatarRequest / parseChangePresenceRequest")
{
    ChangeAvatarRequest avatar;
    REQUIRE(ProfileProtocol::parseChangeAvatarRequest(
        "{\"facetype\":2,\"customface\":\"img_8f3c\"}", avatar));
    CHECK(avatar.facetype == 2);
    CHECK(avatar.customface == "img_8f3c");
    //facetype 缺失被拒绝
    CHECK_FALSE(ProfileProtocol::parseChangeAvatarRequest("{\"customface\":\"x\"}", avatar));

    ChangePresenceRequest presence;
    REQUIRE(ProfileProtocol::parseChangePresenceRequest("{\"presence\":3}", presence));
    CHECK(presence.presence == 3);
    CHECK_FALSE(ProfileProtocol::parseChangePresenceRequest("{}", presence));
}

TEST_CASE("U6 buildGetUserInfoResponse 含完整 profile，64 位 id 为字符串")
{
    UserProfile p;
    p.userid = 100001;
    p.usertype = 0;
    p.username = "72100345";
    p.nickname = "苏沐辰";
    p.facetype = 0;
    p.customface = "";
    p.gender = 1;
    p.birthday = 19900101;
    p.signature = "今天也要好好生活";
    p.address = "杭州";
    p.phonenumber = "";
    p.mail = "";
    p.enterpriseid = 5001;
    p.presence = 1;
    p.clienttype = 1;

    Json::Value root;
    REQUIRE(parseJson(ProfileProtocol::buildGetUserInfoResponse(0, "ok", p), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["profile"]["userid"].asString() == "100001");
    CHECK(root["profile"]["nickname"].asString() == "苏沐辰");
    CHECK(root["profile"]["usertype"].asInt() == 0);
    CHECK(root["profile"]["presence"].asInt() == 1);
    CHECK(root["profile"]["enterpriseid"].asString() == "5001");
}

TEST_CASE("U7 buildUserInfoChangedNotify 含 userid 与 profile")
{
    UserProfile p;
    p.userid = 100001;
    p.usertype = 0;
    p.nickname = "苏沐辰";
    p.enterpriseid = 0;
    p.presence = 1;
    p.clienttype = 1;
    p.facetype = 0;
    p.gender = 0;
    p.birthday = 19900101;

    Json::Value root;
    REQUIRE(parseJson(ProfileProtocol::buildUserInfoChangedNotify(p), root));
    CHECK(root["userid"].asString() == "100001");
    CHECK(root["profile"]["nickname"].asString() == "苏沐辰");
}

TEST_CASE("U8 buildPresenceChangedNotify")
{
    Json::Value root;
    REQUIRE(parseJson(ProfileProtocol::buildPresenceChangedNotify(100001, 3, 1), root));
    CHECK(root["userid"].asString() == "100001");
    CHECK(root["presence"].asInt() == 3);
    CHECK(root["clienttype"].asInt() == 1);
}
