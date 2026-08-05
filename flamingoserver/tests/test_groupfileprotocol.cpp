/**
 *  v2 群文件报文编解码单元测试（Suite D），test_groupfileprotocol.cpp
 *  覆盖：add / get / delete 三类请求的解析与字段纠正（钳位、缺省、越界），
 *       四类应答与两类推送的构造，分页 hasmore 边界，以及按扩展名判定
 *       文件类别 classifyFileType 的六类匹配、兜底与大小写不敏感。
 *  balloonwj@qq.com   2026-05-22
 */
#include "doctest.h"
#include "../chatserversrc/GroupFileProtocol.h"
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

//构造一条样例群文件条目，供构造类用例复用
static GroupFileEntry sampleEntry()
{
    GroupFileEntry e;
    e.entryid      = 31;
    e.fileid       = 10005;
    e.name         = "需求评审.pptx";
    e.ext          = "pptx";
    e.size         = 4404019;
    e.md5          = "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5";
    e.filetype     = group_file_type_document;
    e.uploaderid   = 100007;
    e.uploadername = "谢知行";
    e.uploadtime   = 1716268800000LL;
    return e;
}

TEST_CASE("D1 parseAddGroupFileRequest 解析与校验")
{
    AddGroupFileRequest req;
    REQUIRE(GroupFileProtocol::parseAddGroupFileRequest(
        "{\"groupid\":\"268435457\",\"fileid\":\"10005\"}", req));
    CHECK(req.groupid == 268435457);
    CHECK(req.fileid == 10005);

    //groupid / fileid 缺失或非正一律被拒
    CHECK_FALSE(GroupFileProtocol::parseAddGroupFileRequest("{\"fileid\":\"10005\"}", req));
    CHECK_FALSE(GroupFileProtocol::parseAddGroupFileRequest("{\"groupid\":\"268435457\"}", req));
    CHECK_FALSE(GroupFileProtocol::parseAddGroupFileRequest(
        "{\"groupid\":\"0\",\"fileid\":\"10005\"}", req));
    CHECK_FALSE(GroupFileProtocol::parseAddGroupFileRequest("not a json", req));
}

TEST_CASE("D2 parseGetGroupFilesRequest 正常解析")
{
    GetGroupFilesRequest req;
    REQUIRE(GroupFileProtocol::parseGetGroupFilesRequest(
        "{\"groupid\":\"268435457\",\"filetype\":2,\"offset\":40,\"count\":20}", req));
    CHECK(req.groupid == 268435457);
    CHECK(req.filetype == 2);
    CHECK(req.offset == 40);
    CHECK(req.count == 20);
}

TEST_CASE("D3 parseGetGroupFilesRequest 字段纠正与 groupid 校验")
{
    GetGroupFilesRequest req;

    //filetype 越界（<0 或 >6）归 0（全部）
    REQUIRE(GroupFileProtocol::parseGetGroupFilesRequest(
        "{\"groupid\":\"1\",\"filetype\":99}", req));
    CHECK(req.filetype == group_file_type_all);
    REQUIRE(GroupFileProtocol::parseGetGroupFilesRequest(
        "{\"groupid\":\"1\",\"filetype\":-3}", req));
    CHECK(req.filetype == group_file_type_all);

    //count 缺省取 GROUP_FILE_PAGE_DEFAULT
    REQUIRE(GroupFileProtocol::parseGetGroupFilesRequest("{\"groupid\":\"1\"}", req));
    CHECK(req.count == GROUP_FILE_PAGE_DEFAULT);
    //count 非正同样取缺省
    REQUIRE(GroupFileProtocol::parseGetGroupFilesRequest(
        "{\"groupid\":\"1\",\"count\":0}", req));
    CHECK(req.count == GROUP_FILE_PAGE_DEFAULT);
    //count 超上限钳到 GROUP_FILE_PAGE_MAX
    REQUIRE(GroupFileProtocol::parseGetGroupFilesRequest(
        "{\"groupid\":\"1\",\"count\":9999}", req));
    CHECK(req.count == GROUP_FILE_PAGE_MAX);

    //offset 负数归 0
    REQUIRE(GroupFileProtocol::parseGetGroupFilesRequest(
        "{\"groupid\":\"1\",\"offset\":-10}", req));
    CHECK(req.offset == 0);

    //groupid 缺失或非正被拒
    CHECK_FALSE(GroupFileProtocol::parseGetGroupFilesRequest("{\"filetype\":1}", req));
    CHECK_FALSE(GroupFileProtocol::parseGetGroupFilesRequest("{\"groupid\":\"0\"}", req));
}

TEST_CASE("D4 parseDeleteGroupFileRequest 解析与校验")
{
    DeleteGroupFileRequest req;
    REQUIRE(GroupFileProtocol::parseDeleteGroupFileRequest(
        "{\"groupid\":\"268435457\",\"entryid\":\"31\"}", req));
    CHECK(req.groupid == 268435457);
    CHECK(req.entryid == 31);

    //groupid / entryid 缺失或非正被拒
    CHECK_FALSE(GroupFileProtocol::parseDeleteGroupFileRequest("{\"entryid\":\"31\"}", req));
    CHECK_FALSE(GroupFileProtocol::parseDeleteGroupFileRequest("{\"groupid\":\"1\"}", req));
    CHECK_FALSE(GroupFileProtocol::parseDeleteGroupFileRequest(
        "{\"groupid\":\"1\",\"entryid\":\"0\"}", req));
}

TEST_CASE("D5 buildAddGroupFileResponse 字段齐全、int64 以字符串编码")
{
    std::string text = GroupFileProtocol::buildAddGroupFileResponse(sampleEntry());
    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");
    REQUIRE(root["file"].isObject());

    const Json::Value& f = root["file"];
    //大整数 ID 一律字符串编码，避免 JSON number 精度问题
    CHECK(f["entryid"].asString() == "31");
    CHECK(f["fileid"].asString() == "10005");
    CHECK(f["uploaderid"].asString() == "100007");
    CHECK(f["name"].asString() == "需求评审.pptx");
    CHECK(f["ext"].asString() == "pptx");
    CHECK(f["size"].asInt64() == 4404019LL);
    CHECK(f["md5"].asString() == "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5");
    CHECK(f["filetype"].asInt() == group_file_type_document);
    CHECK(f["uploadername"].asString() == "谢知行");
    CHECK(f["uploadtime"].asInt64() == 1716268800000LL);
}

TEST_CASE("D6 buildGetGroupFilesResponse 字段与 hasmore 边界")
{
    std::vector<GroupFileEntry> files;
    files.push_back(sampleEntry());
    files.push_back(sampleEntry());

    //还有后续页：offset(0) + 本页(2) < total(37)
    std::string text = GroupFileProtocol::buildGetGroupFilesResponse(
        268435457LL, group_file_type_all, 0, 37, files);
    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["groupid"].asString() == "268435457");
    CHECK(root["filetype"].asInt() == 0);
    CHECK(root["offset"].asInt() == 0);
    CHECK(root["total"].asInt() == 37);
    CHECK(root["hasmore"].asBool() == true);
    REQUIRE(root["files"].isArray());
    CHECK(root["files"].size() == 2);

    //恰好取完最后一页：offset(35) + 本页(2) == total(37) → 无后续
    std::string text2 = GroupFileProtocol::buildGetGroupFilesResponse(
        268435457LL, group_file_type_all, 35, 37, files);
    Json::Value root2;
    REQUIRE(parseJson(text2, root2));
    CHECK(root2["hasmore"].asBool() == false);

    //空结果集
    std::vector<GroupFileEntry> empty;
    std::string text3 = GroupFileProtocol::buildGetGroupFilesResponse(
        1LL, group_file_type_image, 0, 0, empty);
    Json::Value root3;
    REQUIRE(parseJson(text3, root3));
    CHECK(root3["total"].asInt() == 0);
    CHECK(root3["hasmore"].asBool() == false);
    CHECK(root3["files"].size() == 0);
}

TEST_CASE("D7 buildDeleteGroupFileResponse 与 buildSimpleResponse")
{
    std::string text = GroupFileProtocol::buildDeleteGroupFileResponse(268435457LL, 31LL);
    Json::Value root;
    REQUIRE(parseJson(text, root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["groupid"].asString() == "268435457");
    CHECK(root["entryid"].asString() == "31");

    //通用错误应答
    std::string err = GroupFileProtocol::buildSimpleResponse(302, "not a group member");
    Json::Value eroot;
    REQUIRE(parseJson(err, eroot));
    CHECK(eroot["code"].asInt() == 302);
    CHECK(eroot["msg"].asString() == "not a group member");
}

TEST_CASE("D8 buildFileAddedNotify / buildFileRemovedNotify 推送构造")
{
    //新增推送：changetype=1，携 file 详情
    std::string added = GroupFileProtocol::buildFileAddedNotify(268435457LL, sampleEntry());
    Json::Value aroot;
    REQUIRE(parseJson(added, aroot));
    CHECK(aroot["groupid"].asString() == "268435457");
    CHECK(aroot["changetype"].asInt() == group_file_change_added);
    REQUIRE(aroot["file"].isObject());
    CHECK(aroot["file"]["entryid"].asString() == "31");

    //删除推送：changetype=2，携 entryid，不携 file
    std::string removed = GroupFileProtocol::buildFileRemovedNotify(268435457LL, 31LL);
    Json::Value rroot;
    REQUIRE(parseJson(removed, rroot));
    CHECK(rroot["groupid"].asString() == "268435457");
    CHECK(rroot["changetype"].asInt() == group_file_change_removed);
    CHECK(rroot["entryid"].asString() == "31");
    CHECK_FALSE(rroot.isMember("file"));
}

TEST_CASE("D9 classifyFileType 六类匹配、兜底与大小写不敏感")
{
    //文档
    CHECK(GroupFileProtocol::classifyFileType("docx") == group_file_type_document);
    CHECK(GroupFileProtocol::classifyFileType("pdf") == group_file_type_document);
    //图片
    CHECK(GroupFileProtocol::classifyFileType("png") == group_file_type_image);
    //视频
    CHECK(GroupFileProtocol::classifyFileType("mp4") == group_file_type_video);
    //音频
    CHECK(GroupFileProtocol::classifyFileType("mp3") == group_file_type_audio);
    //压缩包
    CHECK(GroupFileProtocol::classifyFileType("zip") == group_file_type_archive);

    //未知扩展名归「其它」
    CHECK(GroupFileProtocol::classifyFileType("xyz") == group_file_type_other);
    //空扩展名归「其它」
    CHECK(GroupFileProtocol::classifyFileType("") == group_file_type_other);

    //大小写不敏感：.PNG 与 .png 同类
    CHECK(GroupFileProtocol::classifyFileType("PNG") == group_file_type_image);
    CHECK(GroupFileProtocol::classifyFileType("Mp4") == group_file_type_video);
    CHECK(GroupFileProtocol::classifyFileType("ZIP") == group_file_type_archive);
}
