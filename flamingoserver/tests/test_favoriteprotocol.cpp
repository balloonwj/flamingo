/**
 *  v2「我的收藏」报文编解码单元测试，test_favoriteprotocol.cpp
 *  覆盖：cmd_set_favorite 请求解析（合法与各类非法输入）、应答构造（成功与失败分支）。
 *
 *  收藏项在 cmd_get_contacts 应答里的编码（favorites 数组）另见
 *  test_contactsprotocol.cpp 的 G4b / G4c 两条用例。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#include "doctest.h"
#include "../chatserversrc/FavoriteProtocol.h"
#include "../chatserversrc/Protocol.h"   //conversation_single / conversation_group / error_code
#include "../jsoncpp1.9.0/json.h"

//把 JSON 文本解析成 Json::Value，供断言检查应答内容
static bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

// =====================================================================
// FAV1 ~ FAV3 —— cmd_set_favorite 请求解析
// =====================================================================

TEST_CASE("FAV1 解析合法的收藏请求：联系人与群两种")
{
    SetFavoriteRequest req;

    REQUIRE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":\"5081\",\"favorite\":1}", req));
    CHECK(req.ctype == conversation_single);
    CHECK(req.cid == 5081);
    CHECK(req.favorite == 1);

    REQUIRE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":2,\"cid\":\"268435456\",\"favorite\":0}", req));
    CHECK(req.ctype == conversation_group);
    CHECK(req.cid == 268435456LL);
    CHECK(req.favorite == 0);
}

TEST_CASE("FAV2 ctype 只认 1 与 2")
{
    SetFavoriteRequest req;

    //0 / 负数 / 越界值一律拒绝
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":0,\"cid\":\"5081\",\"favorite\":1}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":-1,\"cid\":\"5081\",\"favorite\":1}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":99,\"cid\":\"5081\",\"favorite\":1}", req));

    //3 是"服务号会话"(conversation_service)，但服务号作为联系人被收藏时走 ctype=1，
    //所以收藏不接受 3 —— 这条专门盯住"顺手把 conversation_service 也放行"的写法。
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":3,\"cid\":\"10000\",\"favorite\":1}", req));

    //ctype 是字符串而不是整数
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":\"1\",\"cid\":\"5081\",\"favorite\":1}", req));
}

TEST_CASE("FAV3 缺字段、cid 非法、favorite 越界一律判非法")
{
    SetFavoriteRequest req;

    //整体不是合法 JSON / 不是对象
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest("", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest("not json", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest("[1,2,3]", req));

    //缺字段
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest("{}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"cid\":\"5081\",\"favorite\":1}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"favorite\":1}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":\"5081\"}", req));

    //cid 按协议约定须是字符串；给数字要拒绝
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":5081,\"favorite\":1}", req));

    //cid 是字符串但内容不是正整数
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":\"abc\",\"favorite\":1}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":\"0\",\"favorite\":1}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":\"-5\",\"favorite\":1}", req));

    //favorite 只有 0 / 1 两种意图，2 或 -1 不能被稀里糊涂当成"加入收藏"
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":\"5081\",\"favorite\":2}", req));
    CHECK_FALSE(FavoriteProtocol::parseSetFavoriteRequest(
        "{\"ctype\":1,\"cid\":\"5081\",\"favorite\":-1}", req));
}

// =====================================================================
// FAV4 ~ FAV5 —— cmd_set_favorite 应答构造
// =====================================================================

TEST_CASE("FAV4 成功应答回带 ctype / cid / favorite")
{
    //回带这三项是为了让客户端在并发多条收藏请求时能对上号（用户连点几个人）。
    Json::Value root;
    REQUIRE(parseJson(FavoriteProtocol::buildSetFavoriteResponse(
        error_code_ok, "ok", conversation_group, 268435456LL, 1), root));

    CHECK(root["code"].asInt() == error_code_ok);
    CHECK(root["msg"].asString() == "ok");
    CHECK(root["ctype"].asInt() == conversation_group);
    //64 位 ID 一律以字符串编码
    REQUIRE(root["cid"].isString());
    CHECK(root["cid"].asString() == "268435456");
    CHECK(root["favorite"].asInt() == 1);
}

TEST_CASE("FAV5 各错误分支的 code 与 msg 都带得回来")
{
    Json::Value root;

    REQUIRE(parseJson(FavoriteProtocol::buildSetFavoriteResponse(
        error_code_favorite_limit, "收藏数量已达上限", conversation_single, 5081, 1), root));
    CHECK(root["code"].asInt() == error_code_favorite_limit);
    CHECK(root["msg"].asString() == "收藏数量已达上限");
    //失败时同样要回带这三项，客户端据此回滚对应那一条的乐观更新
    CHECK(root["ctype"].asInt() == conversation_single);
    CHECK(root["cid"].asString() == "5081");
    CHECK(root["favorite"].asInt() == 1);

    REQUIRE(parseJson(FavoriteProtocol::buildSetFavoriteResponse(
        error_code_not_group_member, "不是群成员", conversation_group, 268435456LL, 1), root));
    CHECK(root["code"].asInt() == error_code_not_group_member);

    //error_code_not_friend 的口径在企业版里改成了"够不着该联系人（不在同一企业）"，
    //错误码本身沿用（客户端还按它提示），只是不再有"好友"这层含义。
    REQUIRE(parseJson(FavoriteProtocol::buildSetFavoriteResponse(
        error_code_not_friend, "不是同一企业的同事", conversation_single, 5081, 1), root));
    CHECK(root["code"].asInt() == error_code_not_friend);

    //msg 允许为空串，不应把 JSON 写坏
    REQUIRE(parseJson(FavoriteProtocol::buildSetFavoriteResponse(
        error_code_bad_request, "", conversation_single, 5081, 0), root));
    CHECK(root["msg"].asString() == "");
}

TEST_CASE("FAV6 收藏数量上限是具名常量且取值合理")
{
    //上限写死在协议模块里、客户端也会提示"最多收藏 N 个"，这里钉住它不被随手改小。
    CHECK(kMaxFavoriteCount == 200);
    CHECK(kMaxFavoriteCount > 0);
}
