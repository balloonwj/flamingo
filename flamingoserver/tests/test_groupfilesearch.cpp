/**
 *  按文件名搜索群文件（cmd_search_group_files 1807）单元测试（Suite GFS），test_groupfilesearch.cpp
 *
 *  覆盖 PC 客户端主面板全局搜索「文件」标签用到的群文件搜索：
 *    · GFS1～GFS2：请求解析（关键字去首尾空白、空白与超长拒绝、limit 缺省与钳位）与应答构造；
 *    · GFS3：LIKE 匹配串的构造（! % _ 前加转义符 !）；
 *    · GFS4～GFS7：对着临时 SQLite 库真实查询 —— 只搜本人所在的群、中文文件名可搜到、按登记
 *      时间倒序、limit 与 hasmore、通配符与单引号按普通字符匹配、英文不区分大小写、退群后搜不到。
 *  临时库经 DbFactory 访问，与 ADB 等套件一样先把全局 CDbConfig 指到本套专用的库文件上。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/GroupFileProtocol.h"
#include "../chatserversrc/GroupFileManager.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时数据库文件，与其它套件的文件分开，避免互相干扰
const char* const GFS_DB_PATH = "flamingo_sqlite_groupfilesearch_test.db";

//用例里的用户与群：本人在群 A、群 B，不在群 C
const int64_t GFS_ME       = 1001;
const int64_t GFS_OTHER    = 1002;
const int64_t GFS_GROUP_A  = 268435457;
const int64_t GFS_GROUP_B  = 268435458;
const int64_t GFS_GROUP_C  = 268435459;

//t_file.f_scene 的「群文件」取值、f_status 的「已完成」取值（见 table_mysql.sql 的字段注释）
const int GFS_SCENE_GROUP_FILE = 2;
const int GFS_STATUS_DONE      = 1;

//把 JSON 文本解析成 Json::Value，供断言检查
bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
void removeGfsDbFiles()
{
    std::string basePath(GFS_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面
void resetGfsDb()
{
    removeGfsDbFiles();
    REQUIRE(Singleton<CSqliteManager>::Instance().init(GFS_DB_PATH));
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL, NULL, GFS_DB_PATH);
}

//执行一条不取结果的 SQL；失败返回 false
bool execSql(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return false;
    return pConn->execute(sql.c_str());
}

//让某用户成为某群的成员
bool addMember(int64_t groupid, int64_t userid)
{
    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES(%lld, %lld, 0, 0)",
        (long long)groupid, (long long)userid);
    return execSql(sql);
}

//在 t_file 插一条文件记录、并把它登记进某群的群文件库
//  fileid / entryid：文件记录与群文件条目的ID；sqlName：已按 SQL 字面量转义的文件名（单引号写两个）
//  ext：扩展名；uploadtime：登记时间（毫秒）
bool addGroupFile(int64_t groupid, int64_t fileid, int64_t entryid, const std::string& sqlName,
                  const std::string& ext, int64_t uploadtime)
{
    char sql[1024] = { 0 };
    snprintf(sql, sizeof(sql),
        "INSERT INTO t_file(f_id, f_md5, f_name, f_ext, f_size, f_scene, f_uploaderid, f_status, f_createtime) "
        "VALUES(%lld, 'md5%lld', '%s', '%s', 1024, %d, %lld, %d, %lld)",
        (long long)fileid, (long long)fileid, sqlName.c_str(), ext.c_str(), GFS_SCENE_GROUP_FILE,
        (long long)GFS_OTHER, GFS_STATUS_DONE, (long long)uploadtime);
    if (!execSql(sql))
        return false;

    snprintf(sql, sizeof(sql),
        "INSERT INTO t_group_file(f_id, f_groupid, f_fileid, f_filetype, f_uploaderid, f_uploadtime) "
        "VALUES(%lld, %lld, %lld, %d, %lld, %lld)",
        (long long)entryid, (long long)groupid, (long long)fileid, group_file_type_document,
        (long long)GFS_OTHER, (long long)uploadtime);
    return execSql(sql);
}

//按本人搜索，返回命中的 entryid 列表（保持结果顺序）
std::vector<int64_t> searchEntryIds(const std::string& keyword, int limit, bool& hasMore)
{
    std::vector<SearchedGroupFile> files;
    hasMore = false;
    REQUIRE(Singleton<GroupFileManager>::Instance().searchGroupFilesByName(
        GFS_ME, keyword, limit, files, hasMore));
    std::vector<int64_t> ids;
    for (size_t i = 0; i < files.size(); ++i)
        ids.push_back(files[i].entry.entryid);
    return ids;
}

} // namespace

TEST_CASE("GFS1 parseSearchGroupFilesRequest：去首尾空白、拒绝空白与超长关键字、limit 缺省与钳位")
{
    SearchGroupFilesRequest req;

    //正常：关键字去掉首尾空白，limit 原样
    REQUIRE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"  设计稿 \",\"limit\":10}", req));
    CHECK(req.keyword == "设计稿");
    CHECK(req.limit == 10);

    //limit 缺省、非正取缺省值；超过上限钳到上限
    REQUIRE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"a\"}", req));
    CHECK(req.limit == GROUP_FILE_SEARCH_LIMIT_DEFAULT);
    REQUIRE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"a\",\"limit\":0}", req));
    CHECK(req.limit == GROUP_FILE_SEARCH_LIMIT_DEFAULT);
    REQUIRE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"a\",\"limit\":-3}", req));
    CHECK(req.limit == GROUP_FILE_SEARCH_LIMIT_DEFAULT);
    REQUIRE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"a\",\"limit\":100000}", req));
    CHECK(req.limit == GROUP_FILE_SEARCH_LIMIT_MAX);

    //关键字缺失、不是字符串、全是空白：拒绝
    CHECK_FALSE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"limit\":10}", req));
    CHECK_FALSE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":12}", req));
    CHECK_FALSE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"   \"}", req));
    CHECK_FALSE(GroupFileProtocol::parseSearchGroupFilesRequest("not json", req));

    //关键字超过上限字节数：拒绝；正好等于上限：接受
    std::string atMax(GROUP_FILE_SEARCH_KEYWORD_MAX_BYTES, 'a');
    CHECK(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"" + atMax + "\"}", req));
    std::string overMax(GROUP_FILE_SEARCH_KEYWORD_MAX_BYTES + 1, 'a');
    CHECK_FALSE(GroupFileProtocol::parseSearchGroupFilesRequest("{\"keyword\":\"" + overMax + "\"}", req));
}

TEST_CASE("GFS2 buildSearchGroupFilesResponse：回显关键字、hasmore、每项带 groupid 与完整条目")
{
    SearchedGroupFile f;
    f.groupid            = GFS_GROUP_A;
    f.entry.entryid      = 31;
    f.entry.fileid       = 10005;
    f.entry.name         = "设计稿.pdf";
    f.entry.ext          = "pdf";
    f.entry.size         = 4096;
    f.entry.md5          = "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5";
    f.entry.filetype     = group_file_type_document;
    f.entry.uploaderid   = GFS_OTHER;
    f.entry.uploadername = "谢知行";
    f.entry.uploadtime   = 1716268800000LL;
    std::vector<SearchedGroupFile> files(1, f);

    Json::Value root;
    REQUIRE(parseJson(GroupFileProtocol::buildSearchGroupFilesResponse("设计稿", true, files), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["keyword"].asString() == "设计稿");
    CHECK(root["hasmore"].asBool());
    REQUIRE(root["files"].isArray());
    REQUIRE(root["files"].size() == 1);
    const Json::Value& jo = root["files"][0];
    CHECK(jo["groupid"].asString() == "268435457");
    CHECK(jo["entryid"].asString() == "31");
    CHECK(jo["fileid"].asString() == "10005");
    CHECK(jo["name"].asString() == "设计稿.pdf");
    CHECK(jo["size"].asInt64() == 4096);
    CHECK(jo["uploaderid"].asString() == "1002");
    CHECK(jo["uploadername"].asString() == "谢知行");
    CHECK(jo["uploadtime"].asInt64() == 1716268800000LL);

    //没有命中：files 是空数组而不是缺失，hasmore 为 false
    std::vector<SearchedGroupFile> none;
    REQUIRE(parseJson(GroupFileProtocol::buildSearchGroupFilesResponse("x", false, none), root));
    REQUIRE(root["files"].isArray());
    CHECK(root["files"].size() == 0);
    CHECK_FALSE(root["hasmore"].asBool());
}

TEST_CASE("GFS3 buildNameLikePattern：前后加 %，并在 ! % _ 前加转义符 !")
{
    CHECK(GroupFileManager::buildNameLikePattern("设计稿") == "%设计稿%");
    CHECK(GroupFileManager::buildNameLikePattern("50%") == "%50!%%");
    CHECK(GroupFileManager::buildNameLikePattern("a_b") == "%a!_b%");
    CHECK(GroupFileManager::buildNameLikePattern("x!y") == "%x!!y%");
}

TEST_CASE("GFS4 只搜本人所在的群，中文文件名可搜到，按登记时间倒序")
{
    resetGfsDb();
    REQUIRE(addMember(GFS_GROUP_A, GFS_ME));
    REQUIRE(addMember(GFS_GROUP_B, GFS_ME));
    REQUIRE(addMember(GFS_GROUP_C, GFS_OTHER));

    REQUIRE(addGroupFile(GFS_GROUP_A, 1, 11, "需求设计稿.pdf", "pdf", 1000));
    REQUIRE(addGroupFile(GFS_GROUP_B, 2, 12, "设计稿终版.sketch", "sketch", 3000));
    REQUIRE(addGroupFile(GFS_GROUP_C, 3, 13, "别的群的设计稿.pdf", "pdf", 5000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 4, 14, "报价单.xlsx", "xlsx", 4000));

    std::vector<SearchedGroupFile> files;
    bool hasMore = true;
    REQUIRE(Singleton<GroupFileManager>::Instance().searchGroupFilesByName(GFS_ME, "设计稿", 10, files, hasMore));
    CHECK_FALSE(hasMore);
    REQUIRE(files.size() == 2);
    //登记时间倒序：群 B 的那条更晚
    CHECK(files[0].groupid == GFS_GROUP_B);
    CHECK(files[0].entry.entryid == 12);
    CHECK(files[0].entry.fileid == 2);
    CHECK(files[0].entry.name == "设计稿终版.sketch");
    CHECK(files[0].entry.ext == "sketch");
    CHECK(files[0].entry.uploadtime == 3000);
    CHECK(files[1].groupid == GFS_GROUP_A);
    CHECK(files[1].entry.entryid == 11);
}

TEST_CASE("GFS5 命中条数超过 limit 时只返回最近的 limit 条，hasmore 为 true")
{
    resetGfsDb();
    REQUIRE(addMember(GFS_GROUP_A, GFS_ME));
    REQUIRE(addGroupFile(GFS_GROUP_A, 1, 11, "周报1.docx", "docx", 1000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 2, 12, "周报2.docx", "docx", 2000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 3, 13, "周报3.docx", "docx", 3000));

    bool hasMore = false;
    std::vector<int64_t> ids = searchEntryIds("周报", 2, hasMore);
    CHECK(hasMore);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 13);
    CHECK(ids[1] == 12);

    ids = searchEntryIds("周报", 3, hasMore);
    CHECK_FALSE(hasMore);
    CHECK(ids.size() == 3);
}

TEST_CASE("GFS6 关键字里的 % _ ! 与单引号按普通字符匹配，英文不区分大小写")
{
    resetGfsDb();
    REQUIRE(addMember(GFS_GROUP_A, GFS_ME));
    REQUIRE(addGroupFile(GFS_GROUP_A, 1, 11, "完成50%.doc", "doc", 1000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 2, 12, "完成500.doc", "doc", 2000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 3, 13, "a_b.txt", "txt", 3000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 4, 14, "aXb.txt", "txt", 4000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 5, 15, "x!y.txt", "txt", 5000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 6, 16, "it''s.txt", "txt", 6000));
    REQUIRE(addGroupFile(GFS_GROUP_A, 7, 17, "REPORT.pdf", "pdf", 7000));

    bool hasMore = false;
    std::vector<int64_t> ids = searchEntryIds("50%", 10, hasMore);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 11);

    ids = searchEntryIds("a_b", 10, hasMore);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 13);

    ids = searchEntryIds("x!y", 10, hasMore);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 15);

    ids = searchEntryIds("it's", 10, hasMore);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 16);

    ids = searchEntryIds("report", 10, hasMore);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 17);
}

TEST_CASE("GFS7 退群之后搜不到该群的文件")
{
    resetGfsDb();
    REQUIRE(addMember(GFS_GROUP_A, GFS_ME));
    REQUIRE(addGroupFile(GFS_GROUP_A, 1, 11, "设计稿.pdf", "pdf", 1000));

    bool hasMore = false;
    CHECK(searchEntryIds("设计稿", 10, hasMore).size() == 1);

    //退群：成员行被删掉（GroupManager 退群与解散群都是删 t_group_member 行）
    char sql[256] = { 0 };
    snprintf(sql, sizeof(sql), "DELETE FROM t_group_member WHERE f_groupid = %lld AND f_userid = %lld",
             (long long)GFS_GROUP_A, (long long)GFS_ME);
    REQUIRE(execSql(sql));
    CHECK(searchEntryIds("设计稿", 10, hasMore).empty());
}
