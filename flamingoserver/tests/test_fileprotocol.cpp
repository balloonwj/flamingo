/**
 *  v2 文件信令报文编解码单元测试（Suite L），test_fileprotocol.cpp
 *  覆盖：request_upload / request_download 请求解析与校验，秒传命中 /
 *       需上传两种上传应答，下载应答，通用错误应答的构造。
 *  balloonwj@qq.com   2026-05-22
 */
#include "doctest.h"
#include "../chatserversrc/FileProtocol.h"
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

//一个合法的 32 位十六进制 MD5，供各用例复用
static const char* const VALID_MD5 = "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5";

TEST_CASE("L1 parseRequestUploadRequest 正常解析")
{
    std::string json = std::string("{\"scene\":4,\"md5\":\"") + VALID_MD5
        + "\",\"size\":4404019,\"name\":\"banner-final.fig\",\"ext\":\"fig\"}";
    RequestUploadRequest req;
    REQUIRE(FileProtocol::parseRequestUploadRequest(json, req));
    CHECK(req.scene == 4);
    CHECK(req.md5 == VALID_MD5);
    CHECK(req.size == 4404019);
    CHECK(req.name == "banner-final.fig");
    CHECK(req.ext == "fig");

    //name / ext 可缺省
    std::string json2 = std::string("{\"scene\":3,\"md5\":\"") + VALID_MD5
        + "\",\"size\":2048}";
    RequestUploadRequest req2;
    REQUIRE(FileProtocol::parseRequestUploadRequest(json2, req2));
    CHECK(req2.name.empty());
    CHECK(req2.ext.empty());
}

TEST_CASE("L2 parseRequestUploadRequest 缺字段与非法 scene 被拒")
{
    RequestUploadRequest req;
    //scene 缺失
    CHECK_FALSE(FileProtocol::parseRequestUploadRequest(
        std::string("{\"md5\":\"") + VALID_MD5 + "\",\"size\":1}", req));
    //scene 越界（0 与 5 均非法）
    CHECK_FALSE(FileProtocol::parseRequestUploadRequest(
        std::string("{\"scene\":0,\"md5\":\"") + VALID_MD5 + "\",\"size\":1}", req));
    CHECK_FALSE(FileProtocol::parseRequestUploadRequest(
        std::string("{\"scene\":5,\"md5\":\"") + VALID_MD5 + "\",\"size\":1}", req));
    //md5 缺失
    CHECK_FALSE(FileProtocol::parseRequestUploadRequest(
        "{\"scene\":4,\"size\":1}", req));
    //md5 长度非 32
    CHECK_FALSE(FileProtocol::parseRequestUploadRequest(
        "{\"scene\":4,\"md5\":\"abcd\",\"size\":1}", req));
    //size 缺失
    CHECK_FALSE(FileProtocol::parseRequestUploadRequest(
        std::string("{\"scene\":4,\"md5\":\"") + VALID_MD5 + "\"}", req));
    //size 非正
    CHECK_FALSE(FileProtocol::parseRequestUploadRequest(
        std::string("{\"scene\":4,\"md5\":\"") + VALID_MD5 + "\",\"size\":0}", req));
}

TEST_CASE("L3 buildRequestUploadResponse 秒传命中应答")
{
    //exists 为 true 时不应携带 uploadtoken / server / chunksize
    std::string text = FileProtocol::buildRequestUploadResponse(
        9007199254740993LL, true, "", "", 0, false, 0);
    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["fileid"].asString() == "9007199254740993");
    CHECK(root["exists"].asBool() == true);
    CHECK_FALSE(root.isMember("uploadtoken"));
    CHECK_FALSE(root.isMember("server"));
    CHECK_FALSE(root.isMember("chunksize"));
}

TEST_CASE("L4 buildRequestUploadResponse 需上传应答")
{
    std::string text = FileProtocol::buildRequestUploadResponse(
        10001LL, false, "ut_5f8c", "img.flamingo.io", 20002, false, 524288);
    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["fileid"].asString() == "10001");
    CHECK(root["exists"].asBool() == false);
    CHECK(root["uploadtoken"].asString() == "ut_5f8c");
    REQUIRE(root["server"].isObject());
    CHECK(root["server"]["host"].asString() == "img.flamingo.io");
    CHECK(root["server"]["port"].asInt() == 20002);
    CHECK(root["server"]["tls"].asBool() == false);
    CHECK(root["chunksize"].asInt() == 524288);
}

TEST_CASE("L5 parseRequestDownloadRequest 解析与校验")
{
    RequestDownloadRequest req;
    REQUIRE(FileProtocol::parseRequestDownloadRequest(
        "{\"fileid\":\"10001\"}", req));
    CHECK(req.fileid == 10001);
    //fileid 缺失或非正被拒
    CHECK_FALSE(FileProtocol::parseRequestDownloadRequest("{}", req));
    CHECK_FALSE(FileProtocol::parseRequestDownloadRequest("{\"fileid\":\"0\"}", req));
}

TEST_CASE("L6 buildRequestDownloadResponse 应答")
{
    std::string text = FileProtocol::buildRequestDownloadResponse(
        10001LL, "dt_abc", VALID_MD5, 4404019LL, "img.flamingo.io", 20002, false);
    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["fileid"].asString() == "10001");
    CHECK(root["downloadtoken"].asString() == "dt_abc");
    CHECK(root["md5"].asString() == VALID_MD5);
    CHECK(root["size"].asInt64() == 4404019LL);
    REQUIRE(root["server"].isObject());
    CHECK(root["server"]["host"].asString() == "img.flamingo.io");
    CHECK(root["server"]["port"].asInt() == 20002);
}

TEST_CASE("L7 buildSimpleResponse 错误应答")
{
    //文件超大错误
    std::string text = FileProtocol::buildSimpleResponse(602, "file too large");
    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == 602);
    CHECK(root["msg"].asString() == "file too large");
}
