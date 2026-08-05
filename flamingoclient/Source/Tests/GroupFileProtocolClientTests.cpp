/**
 *  v2 群文件库信令报文编解码（客户端侧）单元测试（Suite GFP），
 *  GroupFileProtocolClientTests.cpp
 *
 *  覆盖：cmd_get_group_files(1803) 请求构造 + 应答解析（多条目全字段 / 空列表 /
 *       hasmore=false / 错误码）、cmd_add_group_file(1804) 请求构造 + 应答解析
 *       （成功含 file / 错误码无 file）、cmd_group_files_changed_notify(1806) 推送
 *       解析（新增带 file / 删除带 entryid）、cmd_delete_group_file(1805) 请求构造、
 *       字符串编码的大整数ID与 int64 大小/时间不丢精度、畸形输入安全失败。
 *  各报文字段与服务端 chatserversrc/GroupFileProtocol.cpp 的 fillEntryValue /
 *  buildXxxResponse 逐字对齐。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-08
 */
#include "catch_amalgamated.hpp"
#include "../net/GroupFileProtocolClient.h"
#include "../net/Protocol.h"            // error_code_ok / group_file_type_* / group_file_change_*
//见 FileProtocolClientTests.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
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

//把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的应答 / 推送报文
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//按服务端 GroupFileProtocol.cpp 的 fillEntryValue 同款编码写一条群文件条目：
//大整数ID（entryid/fileid/uploaderid）为字符串，size/uploadtime 为 JSON 整型。
static Json::Value makeEntryJson(const char* entryid, const char* fileid,
                                 const char* name, const char* ext,
                                 int64_t size, const char* md5, int filetype,
                                 const char* uploaderid, const char* uploadername,
                                 int64_t uploadtime)
{
    Json::Value jo;
    jo["entryid"]      = std::string(entryid);
    jo["fileid"]       = std::string(fileid);
    jo["name"]         = std::string(name);
    jo["ext"]          = std::string(ext);
    jo["size"]         = static_cast<Json::Int64>(size);
    jo["md5"]          = std::string(md5);
    jo["filetype"]     = filetype;
    jo["uploaderid"]   = std::string(uploaderid);
    jo["uploadername"] = std::string(uploadername);
    jo["uploadtime"]   = static_cast<Json::Int64>(uploadtime);
    return jo;
}

// ============================ 请求构造 ============================

TEST_CASE("GFP1 buildGetGroupFilesRequest 字段完整且类型正确")
{
    //groupid 字符串、filetype/offset/count 整型
    std::string json = GroupFileProtocolClient::buildGetGroupFilesRequest(
        12345, group_file_type_all, 0, 50);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //群ID以字符串编码（服务端 getStringId 解析）
    CHECK(root["groupid"].isString());
    CHECK(root["groupid"].asString() == "12345");
    CHECK(root["filetype"].isInt());
    CHECK(root["filetype"].asInt() == 0);
    CHECK(root["offset"].isInt());
    CHECK(root["offset"].asInt() == 0);
    CHECK(root["count"].isInt());
    CHECK(root["count"].asInt() == 50);
}

TEST_CASE("GFP1b buildGetGroupFilesRequest 大群ID与分页参数透传")
{
    //群ID超 2^53，须以字符串编码精确传出
    const int64_t bigGroup = 9007199254740993LL;   // 2^53 + 1
    std::string json = GroupFileProtocolClient::buildGetGroupFilesRequest(
        bigGroup, group_file_type_document, 40, 20);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["groupid"].asString() == "9007199254740993");
    CHECK(root["filetype"].asInt() == group_file_type_document);
    CHECK(root["offset"].asInt() == 40);
    CHECK(root["count"].asInt() == 20);
}

TEST_CASE("GFP3 buildAddGroupFileRequest groupid 与 fileid 字段正确")
{
    std::string json = GroupFileProtocolClient::buildAddGroupFileRequest(
        12345, "67890");

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["groupid"].isString());
    CHECK(root["groupid"].asString() == "12345");
    //fileid 来自上传应答、本就是字符串，原样透传
    CHECK(root["fileid"].isString());
    CHECK(root["fileid"].asString() == "67890");
}

TEST_CASE("GFP6 buildDeleteGroupFileRequest groupid 与 entryid 字段正确")
{
    std::string json = GroupFileProtocolClient::buildDeleteGroupFileRequest(
        12345, "101");

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["groupid"].asString() == "12345");
    CHECK(root["entryid"].isString());
    CHECK(root["entryid"].asString() == "101");
}

TEST_CASE("GFP7 parseDeleteGroupFileResponse 成功带 entryid / 失败带 code+msg")
{
    // 成功应答（与服务端 buildDeleteGroupFileResponse 对齐）：code=0 + entryid 回带
    {
        Json::Value root;
        root["code"]    = 0;
        root["msg"]     = "ok";
        root["groupid"] = "12345";
        root["entryid"] = "101";
        DeleteGroupFileResult out;
        REQUIRE(GroupFileProtocolClient::parseDeleteGroupFileResponse(writeCompact(root), out));
        CHECK(out.m_code == 0);
        CHECK(out.m_entryid == "101");
    }
    // 失败应答（与服务端 buildSimpleResponse 对齐）：仅 code+msg，无 entryid
    {
        Json::Value root;
        root["code"] = error_code_permission_denied;
        root["msg"]  = "only uploader / owner / admin can delete";
        DeleteGroupFileResult out;
        REQUIRE(GroupFileProtocolClient::parseDeleteGroupFileResponse(writeCompact(root), out));
        CHECK(out.m_code == (int)error_code_permission_denied);
        CHECK(out.m_msg == "only uploader / owner / admin can delete");
        CHECK(out.m_entryid.empty());
    }
    // 畸形输入（非对象 / 无 code）安全失败
    {
        DeleteGroupFileResult out;
        CHECK_FALSE(GroupFileProtocolClient::parseDeleteGroupFileResponse("not json", out));
    }
}

// ============================ 应答解析：列表 ============================

TEST_CASE("GFP2 parseGetGroupFilesResponse 多条目全字段且大整数不丢精度")
{
    //按服务端 buildGetGroupFilesResponse 同款构造一页两条目的成功应答
    Json::Value root;
    root["code"]     = error_code_ok;
    root["msg"]      = "ok";
    root["groupid"]  = "12345";
    root["filetype"] = group_file_type_all;
    root["offset"]   = 0;
    root["total"]    = 100;
    root["hasmore"]  = true;

    Json::Value arr(Json::arrayValue);
    //第1条：大 entryid/fileid（>2^53）与大 size（>2^32）、ms 级 uploadtime
    arr.append(makeEntryJson(
        "9007199254740993", "9007199254740994", "v3.2-发布计划.xlsx", "xlsx",
        5368709120LL, "abc123def4560000000000000000abcd", group_file_type_document,
        "10001", "谢知行", 1716374400000LL));
    //第2条：普通值
    arr.append(makeEntryJson(
        "102", "202", "登录页 final.fig", "fig",
        13002342LL, "0123456789abcdef0123456789abcdef", group_file_type_other,
        "10002", "我", 1716370920000LL));
    root["files"] = arr;

    GetGroupFilesResult out;
    REQUIRE(GroupFileProtocolClient::parseGetGroupFilesResponse(writeCompact(root), out));
    CHECK(out.m_code == 0);
    CHECK(out.m_msg == "ok");
    CHECK(out.m_groupid == "12345");
    CHECK(out.m_filetype == group_file_type_all);
    CHECK(out.m_offset == 0);
    CHECK(out.m_total == 100);
    CHECK(out.m_hasmore == true);
    REQUIRE(out.m_files.size() == 2);

    const GroupFileEntry& e0 = out.m_files[0];
    //字符串编码的大整数ID须原样保真
    CHECK(e0.m_entryid == "9007199254740993");
    CHECK(e0.m_fileid == "9007199254740994");
    CHECK(e0.m_name == "v3.2-发布计划.xlsx");
    CHECK(e0.m_ext == "xlsx");
    CHECK(e0.m_size == 5368709120LL);
    CHECK(e0.m_md5 == "abc123def4560000000000000000abcd");
    CHECK(e0.m_filetype == group_file_type_document);
    CHECK(e0.m_uploaderid == "10001");
    CHECK(e0.m_uploadername == "谢知行");
    CHECK(e0.m_uploadtime == 1716374400000LL);

    const GroupFileEntry& e1 = out.m_files[1];
    CHECK(e1.m_entryid == "102");
    CHECK(e1.m_fileid == "202");
    CHECK(e1.m_name == "登录页 final.fig");
    CHECK(e1.m_ext == "fig");
    CHECK(e1.m_size == 13002342LL);
    CHECK(e1.m_uploadername == "我");
    CHECK(e1.m_uploadtime == 1716370920000LL);
}

TEST_CASE("GFP2b parseGetGroupFilesResponse 空列表与 hasmore=false")
{
    Json::Value root;
    root["code"]     = error_code_ok;
    root["msg"]      = "ok";
    root["groupid"]  = "12345";
    root["filetype"] = group_file_type_image;
    root["offset"]   = 0;
    root["total"]    = 0;
    root["hasmore"]  = false;
    root["files"]    = Json::Value(Json::arrayValue);   //空数组

    GetGroupFilesResult out;
    REQUIRE(GroupFileProtocolClient::parseGetGroupFilesResponse(writeCompact(root), out));
    CHECK(out.m_code == 0);
    CHECK(out.m_total == 0);
    CHECK(out.m_hasmore == false);
    CHECK(out.m_files.empty());
}

TEST_CASE("GFP2c parseGetGroupFilesResponse 错误码应答返回 true 但 code 非 0")
{
    //失败应答可能只有 code/msg，无 groupid/files
    Json::Value root;
    root["code"] = 1003;     //假定某错误码（非群成员/无权限等）
    root["msg"]  = "not a group member";

    GetGroupFilesResult out;
    REQUIRE(GroupFileProtocolClient::parseGetGroupFilesResponse(writeCompact(root), out));
    CHECK(out.m_code == 1003);
    CHECK(out.m_msg == "not a group member");
    CHECK(out.m_files.empty());
}

// ============================ 应答解析：登记 ============================

TEST_CASE("GFP4 parseAddGroupFileResponse 成功携 file 详情")
{
    Json::Value root;
    root["code"] = error_code_ok;
    root["msg"]  = "ok";
    root["file"] = makeEntryJson(
        "303", "404", "群文件功能 PRD.docx", "docx",
        90112LL, "fedcba9876543210fedcba9876543210", group_file_type_document,
        "10003", "方亦初", 1716288000000LL);

    AddGroupFileResult out;
    REQUIRE(GroupFileProtocolClient::parseAddGroupFileResponse(writeCompact(root), out));
    CHECK(out.m_code == 0);
    CHECK(out.m_file.m_entryid == "303");
    CHECK(out.m_file.m_fileid == "404");
    CHECK(out.m_file.m_name == "群文件功能 PRD.docx");
    CHECK(out.m_file.m_ext == "docx");
    CHECK(out.m_file.m_size == 90112LL);
    CHECK(out.m_file.m_filetype == group_file_type_document);
    CHECK(out.m_file.m_uploadername == "方亦初");
    CHECK(out.m_file.m_uploadtime == 1716288000000LL);
}

TEST_CASE("GFP4b parseAddGroupFileResponse 错误码无 file 安全解析")
{
    Json::Value root;
    root["code"] = 1010;     //假定登记失败错误码
    root["msg"]  = "file not found";

    AddGroupFileResult out;
    REQUIRE(GroupFileProtocolClient::parseAddGroupFileResponse(writeCompact(root), out));
    CHECK(out.m_code == 1010);
    CHECK(out.m_msg == "file not found");
    //无 file 时条目保持默认值
    CHECK(out.m_file.m_entryid.empty());
    CHECK(out.m_file.m_size == 0);
}

// ============================ 推送解析 ============================

TEST_CASE("GFP5 parseGroupFilesChangedNotify 新增携 file 详情")
{
    Json::Value root;
    root["groupid"]    = "12345";
    root["changetype"] = group_file_change_added;
    root["file"]       = makeEntryJson(
        "505", "606", "品牌色规范.pdf", "pdf",
        1887436LL, "11112222333344445555666677778888", group_file_type_document,
        "10004", "江屿白", 1716200000000LL);

    GroupFilesChangedNotify out;
    REQUIRE(GroupFileProtocolClient::parseGroupFilesChangedNotify(writeCompact(root), out));
    CHECK(out.m_groupid == "12345");
    CHECK(out.m_changetype == group_file_change_added);
    CHECK(out.m_file.m_entryid == "505");
    CHECK(out.m_file.m_name == "品牌色规范.pdf");
    CHECK(out.m_file.m_ext == "pdf");
    CHECK(out.m_file.m_uploadername == "江屿白");
}

TEST_CASE("GFP5b parseGroupFilesChangedNotify 删除携 entryid")
{
    Json::Value root;
    root["groupid"]    = "12345";
    root["changetype"] = group_file_change_removed;
    root["entryid"]    = "505";

    GroupFilesChangedNotify out;
    REQUIRE(GroupFileProtocolClient::parseGroupFilesChangedNotify(writeCompact(root), out));
    CHECK(out.m_groupid == "12345");
    CHECK(out.m_changetype == group_file_change_removed);
    CHECK(out.m_entryid == "505");
    //删除推送无 file，条目保持默认
    CHECK(out.m_file.m_entryid.empty());
}

// ============================ 畸形输入安全失败 ============================

TEST_CASE("GFP7 畸形输入安全失败不崩")
{
    GetGroupFilesResult g;
    //非法 JSON 文本
    CHECK_FALSE(GroupFileProtocolClient::parseGetGroupFilesResponse("not a json", g));
    //顶层非对象
    CHECK_FALSE(GroupFileProtocolClient::parseGetGroupFilesResponse("[1,2,3]", g));
    //缺 code
    CHECK_FALSE(GroupFileProtocolClient::parseGetGroupFilesResponse("{\"msg\":\"ok\"}", g));

    AddGroupFileResult a;
    CHECK_FALSE(GroupFileProtocolClient::parseAddGroupFileResponse("", a));
    CHECK_FALSE(GroupFileProtocolClient::parseAddGroupFileResponse("{\"file\":{}}", a)); //缺 code

    GroupFilesChangedNotify n;
    //缺 changetype
    CHECK_FALSE(GroupFileProtocolClient::parseGroupFilesChangedNotify("{\"groupid\":\"1\"}", n));
    //缺 groupid
    CHECK_FALSE(GroupFileProtocolClient::parseGroupFilesChangedNotify("{\"changetype\":1}", n));
    //非法 JSON
    CHECK_FALSE(GroupFileProtocolClient::parseGroupFilesChangedNotify("###", n));
}
