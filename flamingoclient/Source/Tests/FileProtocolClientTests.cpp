/**
 *  v2 文件 / 图片信令与分片报文编解码（客户端侧）单元测试（Suite FP），
 *  FileProtocolClientTests.cpp
 *
 *  覆盖：cmd_request_upload(1801) 请求构造 + 应答解析（需上传 / 秒传命中 /
 *       失败）、cmd_request_download(1802) 请求构造 + 应答解析、
 *       cmd_file_upload(1851) 分片描述符构造 + 回执解析、cmd_file_download(1852)
 *       分片描述符构造 + 应答帧描述符解析、64 位 size 不丢精度、畸形输入安全失败。
 *  各报文字段与服务端 chatserversrc/FileProtocol.cpp、fileserversrc/FileSession.cpp
 *  逐字对齐。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-03
 */
#include "catch_amalgamated.hpp"
#include "../net/FileProtocolClient.h"
#include "../net/Protocol.h"            // file_scene_image_msg / error_code
//见 MessageProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查 build 出的报文
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的应答报文
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

TEST_CASE("FP1 buildRequestUploadRequest 图片场景字段完整且类型正确")
{
    //图片消息场景 scene=4，含 md5 / size / name / ext
    std::string json = FileProtocolClient::buildRequestUploadRequest(
        file_scene_image_msg, "9b1c0000000000000000000000000e7a", 18324, "shot.png", "png");

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["scene"].isInt());
    CHECK(root["scene"].asInt() == 4);
    CHECK(root["md5"].isString());
    CHECK(root["md5"].asString() == "9b1c0000000000000000000000000e7a");
    //size 为整型字节数（与服务端 isNumeric 解析一致）
    CHECK(root["size"].isNumeric());
    CHECK(root["size"].asInt64() == 18324);
    CHECK(root["name"].asString() == "shot.png");
    CHECK(root["ext"].asString() == "png");
}

TEST_CASE("FP1b buildRequestUploadRequest 大文件 size 不丢精度")
{
    //超 2^32 的大小（约 5GB），验证 int64 编码精确
    const int64_t bigSize = 5368709120LL;
    std::string json = FileProtocolClient::buildRequestUploadRequest(
        file_scene_chat_file, "0123456789abcdef0123456789abcdef", bigSize, "", "");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["size"].asInt64() == bigSize);
    //name / ext 为空也照常写出空串
    CHECK(root["name"].isString());
    CHECK(root["name"].asString().empty());
}

TEST_CASE("FP2 parseRequestUploadResponse 解析需上传应答")
{
    //服务端 buildRequestUploadResponse(exists=false) 的真实结构
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"fileid\":\"10005\",\"exists\":false,"
        "\"uploadtoken\":\"ut_5f8c\",\"server\":{\"host\":\"img.flamingo.io\","
        "\"port\":20002,\"tls\":false},\"chunksize\":524288}";
    RequestUploadResult out;
    REQUIRE(FileProtocolClient::parseRequestUploadResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_msg == "ok");
    CHECK(out.m_fileid == "10005");
    CHECK(out.m_exists == false);
    CHECK(out.m_uploadtoken == "ut_5f8c");
    CHECK(out.m_host == "img.flamingo.io");
    CHECK(out.m_port == 20002);
    CHECK(out.m_tls == false);
    CHECK(out.m_chunksize == 524288);
}

TEST_CASE("FP3 parseRequestUploadResponse 解析秒传命中应答")
{
    //秒传命中：仅 fileid + exists=true，无令牌与目标服务器
    std::string body = "{\"code\":0,\"msg\":\"ok\",\"fileid\":\"10005\",\"exists\":true}";
    RequestUploadResult out;
    REQUIRE(FileProtocolClient::parseRequestUploadResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_fileid == "10005");
    CHECK(out.m_exists == true);
    //缺失的上传字段保持默认，不应误判
    CHECK(out.m_uploadtoken.empty());
    CHECK(out.m_host.empty());
    CHECK(out.m_chunksize == 0);
}

TEST_CASE("FP4 parseRequestUploadResponse 解析失败应答")
{
    //文件过大等失败：服务端只回 code/msg（见 buildSimpleResponse）
    std::string body = "{\"code\":602,\"msg\":\"file too large\"}";
    RequestUploadResult out;
    REQUIRE(FileProtocolClient::parseRequestUploadResponse(body, out));
    CHECK(out.m_code == 602);
    CHECK(out.m_msg == "file too large");
    //失败应答不解析 fileid 等，保持默认
    CHECK(out.m_fileid.empty());
    CHECK(out.m_exists == false);
}

TEST_CASE("FP4b parseRequestUploadResponse fileid 容错数字编码")
{
    //防御：即便服务端把 fileid 写成数字也能解析成字符串
    std::string body = "{\"code\":0,\"fileid\":10005,\"exists\":true}";
    RequestUploadResult out;
    REQUIRE(FileProtocolClient::parseRequestUploadResponse(body, out));
    CHECK(out.m_fileid == "10005");
}

TEST_CASE("FP5 buildUploadChunkDescriptor 字段与类型正确")
{
    std::string json = FileProtocolClient::buildUploadChunkDescriptor("ut_5f8c", 524288);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["uploadtoken"].isString());
    CHECK(root["uploadtoken"].asString() == "ut_5f8c");
    CHECK(root["offset"].isNumeric());
    CHECK(root["offset"].asInt64() == 524288);
}

TEST_CASE("FP6 parseUploadChunkAck 解析进度与完成两态")
{
    //进度回执：done=false，nextoffset 指向下一片
    UploadChunkAck prog;
    REQUIRE(FileProtocolClient::parseUploadChunkAck(
        "{\"code\":0,\"done\":false,\"nextoffset\":524288}", prog));
    CHECK(prog.m_code == 0);
    CHECK(prog.m_done == false);
    CHECK(prog.m_nextoffset == 524288);

    //完成回执：done=true，nextoffset=总大小
    UploadChunkAck done;
    REQUIRE(FileProtocolClient::parseUploadChunkAck(
        "{\"code\":0,\"done\":true,\"nextoffset\":1048576}", done));
    CHECK(done.m_done == true);
    CHECK(done.m_nextoffset == 1048576);

    //错误回执：仅 code/msg
    UploadChunkAck err;
    REQUIRE(FileProtocolClient::parseUploadChunkAck(
        "{\"code\":605,\"msg\":\"md5 mismatch\"}", err));
    CHECK(err.m_code == 605);
    CHECK(err.m_done == false);
}

TEST_CASE("FP7 buildRequestDownloadRequest fileid 字符串编码")
{
    std::string json = FileProtocolClient::buildRequestDownloadRequest("10001");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //fileid 必须字符串编码（与服务端 getStringId 解析一致）
    CHECK(root["fileid"].isString());
    CHECK(root["fileid"].asString() == "10001");
}

TEST_CASE("FP8 parseRequestDownloadResponse 解析下载凭证应答")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"fileid\":\"10001\",\"downloadtoken\":\"dt_abc\","
        "\"md5\":\"9b1c0000000000000000000000000e7a\",\"size\":4404019,"
        "\"server\":{\"host\":\"img.flamingo.io\",\"port\":20002,\"tls\":false}}";
    RequestDownloadResult out;
    REQUIRE(FileProtocolClient::parseRequestDownloadResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_fileid == "10001");
    CHECK(out.m_downloadtoken == "dt_abc");
    CHECK(out.m_md5 == "9b1c0000000000000000000000000e7a");
    CHECK(out.m_size == 4404019);
    CHECK(out.m_host == "img.flamingo.io");
    CHECK(out.m_port == 20002);
    CHECK(out.m_tls == false);
}

TEST_CASE("FP9 buildDownloadChunkDescriptor 字段与类型正确")
{
    std::string json = FileProtocolClient::buildDownloadChunkDescriptor("dt_abc", 0);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["downloadtoken"].asString() == "dt_abc");
    CHECK(root["offset"].isNumeric());
    CHECK(root["offset"].asInt64() == 0);
}

TEST_CASE("FP10 parseDownloadChunkInfo 解析中间片与末片")
{
    //中间片：done=false
    DownloadChunkInfo mid;
    REQUIRE(FileProtocolClient::parseDownloadChunkInfo(
        "{\"code\":0,\"offset\":0,\"totalsize\":1048576,\"done\":false}", mid));
    CHECK(mid.m_code == 0);
    CHECK(mid.m_offset == 0);
    CHECK(mid.m_totalsize == 1048576);
    CHECK(mid.m_done == false);

    //末片：done=true
    DownloadChunkInfo last;
    REQUIRE(FileProtocolClient::parseDownloadChunkInfo(
        "{\"code\":0,\"offset\":524288,\"totalsize\":1048576,\"done\":true}", last));
    CHECK(last.m_done == true);
    CHECK(last.m_offset == 524288);

    //错误应答：仅 code/msg（服务端 sendDownloadError 的描述符）
    DownloadChunkInfo err;
    REQUIRE(FileProtocolClient::parseDownloadChunkInfo(
        "{\"code\":601,\"msg\":\"file not exist\"}", err));
    CHECK(err.m_code == 601);
}

TEST_CASE("FP10b parseDownloadChunkInfo 大文件 totalsize 不丢精度")
{
    const int64_t bigTotal = 5368709120LL;   // 约 5GB
    Json::Value d;
    d["code"]      = 0;
    d["offset"]    = static_cast<Json::Int64>(0);
    d["totalsize"] = static_cast<Json::Int64>(bigTotal);
    d["done"]      = false;
    DownloadChunkInfo out;
    REQUIRE(FileProtocolClient::parseDownloadChunkInfo(writeCompact(d), out));
    CHECK(out.m_totalsize == bigTotal);
}

TEST_CASE("FP11 各解析对畸形 / 缺字段输入安全失败、不崩溃")
{
    RequestUploadResult ru;
    //非 JSON / 空串 / 合法 JSON 但非对象
    CHECK_FALSE(FileProtocolClient::parseRequestUploadResponse("not a json", ru));
    CHECK_FALSE(FileProtocolClient::parseRequestUploadResponse("", ru));
    CHECK_FALSE(FileProtocolClient::parseRequestUploadResponse("[1,2,3]", ru));
    //缺 code / code 类型错误
    CHECK_FALSE(FileProtocolClient::parseRequestUploadResponse("{\"msg\":\"x\"}", ru));
    CHECK_FALSE(FileProtocolClient::parseRequestUploadResponse("{\"code\":\"0\"}", ru));

    RequestDownloadResult rd;
    CHECK_FALSE(FileProtocolClient::parseRequestDownloadResponse("", rd));
    CHECK_FALSE(FileProtocolClient::parseRequestDownloadResponse("{}", rd));

    UploadChunkAck ua;
    CHECK_FALSE(FileProtocolClient::parseUploadChunkAck("nope", ua));
    CHECK_FALSE(FileProtocolClient::parseUploadChunkAck("{}", ua));

    DownloadChunkInfo dc;
    CHECK_FALSE(FileProtocolClient::parseDownloadChunkInfo("", dc));
    CHECK_FALSE(FileProtocolClient::parseDownloadChunkInfo("{}", dc));

    //server 字段缺失时下载应答不应崩溃，host 保持空、其余字段照常解析
    RequestDownloadResult noserver;
    REQUIRE(FileProtocolClient::parseRequestDownloadResponse(
        "{\"code\":0,\"fileid\":\"1\",\"downloadtoken\":\"t\",\"md5\":\"m\",\"size\":1}", noserver));
    CHECK(noserver.m_host.empty());
    CHECK(noserver.m_downloadtoken == "t");
}
