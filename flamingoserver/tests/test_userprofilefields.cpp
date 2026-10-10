/**
 *  联系人资料字段扩充（docs/联系人资料字段扩充方案.md）服务端单元测试（Suite UPF），test_userprofilefields.cpp
 *
 *  覆盖第 1a 步（存储）：
 *    - UPF1 新建的 SQLite 库 t_user 有 f_post（岗位）列；
 *    - UPF2 updateUserInfoInDb：签名、所在地、岗位、座机含单引号与反斜杠时能保存，读回逐字相同
 *           （此前这些文字直接拼进 SQL，带一个单引号整条语句就失败，2026-10-04 修复）；
 *    - UPF3 用户缓存与数据库里的座机、岗位一致：更新后缓存立即可见，按库重新加载后也相同。
 *  第 1b 步（协议）：
 *    - UPF4 改资料请求里座机、岗位带了才改，不带时保持原值，带空串表示清空；
 *    - UPF5 昵称、岗位、座机 64 字，签名、所在地、邮箱 256 字，按 UTF-8 字符数计，多一个字被拒；
 *    - UPF6 性别只接受 0、1、2，生日只接受 0 与存在的 YYYYMMDD 日期；
 *    - UPF7 请求里带手机号时被忽略，库里的手机号不变；
 *    - UPF8 通讯录列表、登录应答、资料 JSON 三处都带座机与岗位，未填为空串；
 *    - UPF9 岗位参与敏感词检查（命中时被拦下），座机不参与。
 *  第 9 节「手机号、座机对同事的公开范围」（2026-10-06）：
 *    - UPF10 t_user 有 f_phone_visible / f_landline_visible 两列，新库默认 0 与 1；
 *            新注册用户的缓存与库里都是默认值；
 *    - UPF11 两个标志经 updateUserInfoInDb 写进库与缓存，按库重新加载后一致；
 *    - UPF12 改资料请求里两个标志带了才改，只接受 0 与 1，类型不符视为没带；
 *    - UPF13 按查看者处理号码的纯函数：本人看自己原样、同事看时按两个标志分别隐藏，标志只发给本人；
 *    - UPF14 通讯录列表：同事的号码按其公开范围处理，请求人自己那一行完整，不下发标志；
 *    - UPF15 取资料（1103）按查看者处理，登录应答带两个标志；
 *    - UPF16 资料变更推送（1108）：本人一份完整带标志，同事一份按公开范围处理、不带标志。
 *
 *  这些用例会真的打开一个临时 SQLite 库跑 SQL：UserManager 经 DbFactory 取连接，DbFactory 读全局 CDbConfig，
 *  故每个用例先用 testdb::resetDb 把 CDbConfig 指到本套专用的库文件上。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <stdio.h>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/UserManager.h"
#include "../chatserversrc/ProfileProtocol.h"
#include "../chatserversrc/LoginProtocol.h"
#include "../chatserversrc/ContactsProtocol.h"
#include "../chatserversrc/Protocol.h"               //enum profile_gender
#include "../chatserversrc/SensitiveWordManager.h"   //UPF9 用企业词库检查岗位
#include "../jsoncpp1.9.0/json.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/DatabaseSqlite.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件（SQLite 模式），以及 MySQL 模式下会被整个重建的库名
static const char* const UPF_DB_PATH = "flamingo_sqlite_userprofile_test.db";
static const char* const UPF_MYSQL_DB = "flamingo_test_userprofile";

//新建测试用户所属的企业 ID（只用于落库，不参与断言）
static const int64_t UPF_ENTERPRISE_ID = 1;

//删除一个 SQLite 库文件，连同 WAL 模式产生的两个附属文件
static void removeUpfDbFiles(const char* path)
{
    const std::string base(path);
    remove(base.c_str());
    remove((base + "-wal").c_str());
    remove((base + "-shm").c_str());
}

//t_user 里是否有某一列（PRAGMA table_info 逐行比对列名）
static bool upfHasColumn(CDatabaseSqlite& db, const char* column)
{
    QueryResult* pResult = db.query("PRAGMA table_info(t_user)");
    if (pResult == NULL)
        return false;
    bool found = false;
    Field* pRow = pResult->fetch();
    while (pRow != NULL)
    {
        if (pRow[1].getString() == column)
        {
            found = true;
            break;
        }
        if (!pResult->nextRow())
            break;
        pRow = pResult->fetch();
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

//按 userid 从库里读一列文字（绕过 UserManager 缓存，确认真的写进了库）；取不到时返回 "<none>"
static std::string upfReadColumn(int32_t userid, const char* column)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return "<none>";
    const std::string sql = std::string("SELECT ") + column + " FROM t_user WHERE f_user_id = " + std::to_string(userid);
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return "<none>";
    std::string value = "<none>";
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = pRow[0].getString();
    pResult->endQuery();
    delete pResult;
    return value;
}

//在当前测试库里新建一个用户（账号由服务端生成），返回其 userid；失败时让用例停下
static int32_t upfAddUser(const char* nickname)
{
    User u;
    u.nickname = nickname;
    u.password = "x";
    u.salt = "s";
    u.facetype = 0;
    u.gender = 0;
    u.birthday = 0;
    u.status = 0;
    u.usertype = 0;
    u.ownerid = 0;
    REQUIRE(Singleton<UserManager>::Instance().addUser(u, UPF_ENTERPRISE_ID));
    User cached;
    REQUIRE(Singleton<UserManager>::Instance().getUserInfoByUsername(u.username, cached));
    return cached.userid;
}

TEST_CASE("UPF1 t_user 有岗位列 f_post：新库直接建出")
{
    //新库：按完整结构建出
    removeUpfDbFiles(UPF_DB_PATH);
    {
        CSqliteManager manager;
        REQUIRE(manager.init(UPF_DB_PATH));
        CDatabaseSqlite db;
        REQUIRE(db.initialize(UPF_DB_PATH));
        CHECK(upfHasColumn(db, "f_post"));
        CHECK(upfHasColumn(db, "f_landline"));
    }
    removeUpfDbFiles(UPF_DB_PATH);
}

TEST_CASE("UPF2 签名、所在地、岗位、座机含单引号与反斜杠时能保存，读回逐字相同")
{
    testdb::resetDb(UPF_DB_PATH, UPF_MYSQL_DB);
    UserManager& userMgr = Singleton<UserManager>::Instance();
    const int32_t userid = upfAddUser("upf2");

    User u;
    REQUIRE(userMgr.getUserInfoByUserId(userid, u));
    u.signature = "it's a \\path\\ and 'quote'";
    u.address   = "O'Brien Street \\ 2";
    u.post      = "Lead's \"designer\"";
    u.landline  = "010-8888'6666";
    u.nickname  = "Tom's";
    REQUIRE(userMgr.updateUserInfoInDb(userid, u));

    //直接查库：内容一个字不差（没有被转义符污染，也没有截断）
    CHECK(upfReadColumn(userid, "f_signature") == u.signature);
    CHECK(upfReadColumn(userid, "f_address") == u.address);
    CHECK(upfReadColumn(userid, "f_post") == u.post);
    CHECK(upfReadColumn(userid, "f_landline") == u.landline);
    CHECK(upfReadColumn(userid, "f_nickname") == u.nickname);

    testdb::cleanupDb(UPF_DB_PATH);
}

TEST_CASE("UPF3 座机与岗位：更新后缓存立即可见，按库重新加载后与库一致")
{
    testdb::resetDb(UPF_DB_PATH, UPF_MYSQL_DB);
    UserManager& userMgr = Singleton<UserManager>::Instance();
    const int32_t userid = upfAddUser("upf3");

    //新用户：两个字段为空
    User fresh;
    REQUIRE(userMgr.getUserInfoByUserId(userid, fresh));
    CHECK(fresh.landline.empty());
    CHECK(fresh.post.empty());

    User u = fresh;
    u.landline = "021-6666 8888";
    u.post     = u8"交互设计师";
    REQUIRE(userMgr.updateUserInfoInDb(userid, u));

    //缓存立即可见
    User cached;
    REQUIRE(userMgr.getUserInfoByUserId(userid, cached));
    CHECK(cached.landline == "021-6666 8888");
    CHECK(cached.post == u8"交互设计师");

    //库里另行改一次，再按库重新加载：缓存取到的是库里的值（加载路径确实读了这两列）
    {
        std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
        REQUIRE(pConn);
        const std::string sql = std::string("UPDATE t_user SET f_landline = '0755-1234', f_post = 'PM' "
                                            "WHERE f_user_id = ") + std::to_string(userid);
        REQUIRE(pConn->execute(sql.c_str()));
    }
    REQUIRE(userMgr.reloadUserCache(userid));
    User reloaded;
    REQUIRE(userMgr.getUserInfoByUserId(userid, reloaded));
    CHECK(reloaded.landline == "0755-1234");
    CHECK(reloaded.post == "PM");

    testdb::cleanupDb(UPF_DB_PATH);
}

//把一段文字重复 n 次，用来造长度恰好到上限或超过一个字的输入
static std::string upfRepeat(const std::string& unit, int n)
{
    std::string s;
    for (int i = 0; i < n; ++i)
        s += unit;
    return s;
}

//只带一个字段的改资料请求是否能通过解析与校验
static bool upfParseOne(const std::string& field, const std::string& value)
{
    Json::Value root(Json::objectValue);
    root[field] = value;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    UpdateProfileRequest req;
    return ProfileProtocol::parseUpdateProfileRequest(Json::writeString(builder, root), req);
}

//只带一个整数字段的改资料请求是否能通过解析与校验
static bool upfParseInt(const char* field, int value)
{
    const std::string body = std::string("{\"") + field + "\":" + std::to_string(value) + "}";
    UpdateProfileRequest req;
    return ProfileProtocol::parseUpdateProfileRequest(body, req);
}

//把 JSON 文本解析成 Json::Value，供 UPF8 检查三处下发的字段
static bool upfParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("UPF4 改资料请求：座机、岗位带了才改，不带时保持原值")
{
    UpdateProfileRequest req;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest(
        u8"{\"landline\":\"010-8888 6666\",\"post\":\"交互设计师\"}", req));
    CHECK(req.has_landline);
    CHECK(req.landline == "010-8888 6666");
    CHECK(req.has_post);
    CHECK(req.post == u8"交互设计师");
    CHECK_FALSE(req.has_nickname);
    CHECK_FALSE(req.has_signature);

    //合并：带了的两项改掉，其余保持
    User u;
    u.nickname = "keep";
    u.signature = "keep-sign";
    u.landline = "old-landline";
    u.post = "old-post";
    u.gender = profile_gender_male;
    u.birthday = 19900101;
    ProfileProtocol::applyProfileUpdate(req, u);
    CHECK(u.landline == "010-8888 6666");
    CHECK(u.post == u8"交互设计师");
    CHECK(u.nickname == "keep");
    CHECK(u.signature == "keep-sign");
    CHECK(u.gender == profile_gender_male);
    CHECK(u.birthday == 19900101);

    //不带这两项的请求：原值保持；类型不符（数字）视为没带
    UpdateProfileRequest other;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest("{\"nickname\":\"n2\",\"post\":123}", other));
    CHECK_FALSE(other.has_landline);
    CHECK_FALSE(other.has_post);
    ProfileProtocol::applyProfileUpdate(other, u);
    CHECK(u.nickname == "n2");
    CHECK(u.landline == "010-8888 6666");
    CHECK(u.post == u8"交互设计师");

    //带空串表示清空
    UpdateProfileRequest cleared;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest("{\"landline\":\"\",\"post\":\"\"}", cleared));
    ProfileProtocol::applyProfileUpdate(cleared, u);
    CHECK(u.landline.empty());
    CHECK(u.post.empty());
}

TEST_CASE("UPF5 长度校验：按 UTF-8 字符数计，恰好到上限通过，多一个字被拒")
{
    const char* const kShortFields[] = { "nickname", "post", "landline" };
    const char* const kLongFields[] = { "signature", "address", "mail" };
    //一个中文字（3 字节）与一个英文字母都算一个字
    const std::string kHan = u8"岗";
    const std::string kAscii = "a";

    for (size_t i = 0; i < sizeof(kShortFields) / sizeof(kShortFields[0]); ++i)
    {
        CAPTURE(kShortFields[i]);
        CHECK(upfParseOne(kShortFields[i], upfRepeat(kHan, kProfileShortTextMaxChars)));
        CHECK_FALSE(upfParseOne(kShortFields[i], upfRepeat(kHan, kProfileShortTextMaxChars + 1)));
        CHECK(upfParseOne(kShortFields[i], upfRepeat(kAscii, kProfileShortTextMaxChars)));
        CHECK_FALSE(upfParseOne(kShortFields[i], upfRepeat(kAscii, kProfileShortTextMaxChars + 1)));
    }
    for (size_t i = 0; i < sizeof(kLongFields) / sizeof(kLongFields[0]); ++i)
    {
        CAPTURE(kLongFields[i]);
        CHECK(upfParseOne(kLongFields[i], upfRepeat(kHan, kProfileLongTextMaxChars)));
        CHECK_FALSE(upfParseOne(kLongFields[i], upfRepeat(kHan, kProfileLongTextMaxChars + 1)));
        CHECK(upfParseOne(kLongFields[i], upfRepeat(kAscii, kProfileLongTextMaxChars)));
        CHECK_FALSE(upfParseOne(kLongFields[i], upfRepeat(kAscii, kProfileLongTextMaxChars + 1)));
    }
}

TEST_CASE("UPF6 取值校验：性别只接受 0、1、2，生日只接受 0 与存在的日期")
{
    CHECK(upfParseInt("gender", profile_gender_unset));
    CHECK(upfParseInt("gender", profile_gender_male));
    CHECK(upfParseInt("gender", profile_gender_female));
    CHECK_FALSE(upfParseInt("gender", -1));
    CHECK_FALSE(upfParseInt("gender", 3));

    //0 表示清空生日
    CHECK(upfParseInt("birthday", 0));
    CHECK(upfParseInt("birthday", 19960812));
    CHECK(upfParseInt("birthday", 19000101));
    //闰年二月有 29 日；1900 年不是闰年，2000 年是
    CHECK(upfParseInt("birthday", 20000229));
    CHECK(upfParseInt("birthday", 20240229));
    CHECK_FALSE(upfParseInt("birthday", 19000229));
    CHECK_FALSE(upfParseInt("birthday", 20230229));
    //月份、日期越界
    CHECK_FALSE(upfParseInt("birthday", 20261332));
    CHECK_FALSE(upfParseInt("birthday", 20260431));
    CHECK_FALSE(upfParseInt("birthday", 20260100));
    CHECK_FALSE(upfParseInt("birthday", 20260015));
    //年份超出 1900～2100，或根本不是 8 位日期
    CHECK_FALSE(upfParseInt("birthday", 18991231));
    CHECK_FALSE(upfParseInt("birthday", 21010101));
    CHECK_FALSE(upfParseInt("birthday", 1996));
    CHECK_FALSE(upfParseInt("birthday", -19960812));
}

TEST_CASE("UPF7 请求里带手机号时被忽略：合并后与库里的手机号都不变")
{
    testdb::resetDb(UPF_DB_PATH, UPF_MYSQL_DB);
    UserManager& userMgr = Singleton<UserManager>::Instance();
    const int32_t userid = upfAddUser("upf7");

    //先给这个用户设上手机号（注册时填的那个）
    {
        std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
        REQUIRE(pConn);
        const std::string sql = std::string("UPDATE t_user SET f_phonenumber = '13800000007' WHERE f_user_id = ")
                                + std::to_string(userid);
        REQUIRE(pConn->execute(sql.c_str()));
    }
    REQUIRE(userMgr.reloadUserCache(userid));

    UpdateProfileRequest req;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest(
        "{\"phonenumber\":\"13900000000\",\"post\":\"PM\"}", req));
    CHECK(req.has_post);

    User u;
    REQUIRE(userMgr.getUserInfoByUserId(userid, u));
    ProfileProtocol::applyProfileUpdate(req, u);
    CHECK(u.phonenumber == "13800000007");
    REQUIRE(userMgr.updateUserInfoInDb(userid, u));

    CHECK(upfReadColumn(userid, "f_phonenumber") == "13800000007");
    CHECK(upfReadColumn(userid, "f_post") == "PM");
    User cached;
    REQUIRE(userMgr.getUserInfoByUserId(userid, cached));
    CHECK(cached.phonenumber == "13800000007");

    testdb::cleanupDb(UPF_DB_PATH);
}

TEST_CASE("UPF8 三处下发都带座机与岗位：通讯录列表、登录应答、资料 JSON，未填为空串")
{
    //通讯录列表：一个填了、一个没填
    ContactsData data;
    data.enterpriseid = 5001;
    data.enterprisename = "ent";
    data.synctime = 0;
    ContactInfo filled = ContactInfo();
    filled.userid = 100001;
    filled.nickname = "a";
    filled.landline = "010-1234";
    filled.post = u8"岗位甲";
    ContactInfo blank = ContactInfo();
    blank.userid = 100002;
    blank.nickname = "b";
    data.contacts.push_back(filled);
    data.contacts.push_back(blank);
    Json::Value contacts;
    REQUIRE(upfParseJson(ContactsProtocol::buildContactsResponse(data), contacts));
    REQUIRE(contacts["contacts"].size() == 2);
    CHECK(contacts["contacts"][0]["landline"].asString() == "010-1234");
    CHECK(contacts["contacts"][0]["post"].asString() == u8"岗位甲");
    CHECK(contacts["contacts"][1].isMember("landline"));
    CHECK(contacts["contacts"][1].isMember("post"));
    CHECK(contacts["contacts"][1]["landline"].asString().empty());
    CHECK(contacts["contacts"][1]["post"].asString().empty());

    //登录应答
    LoginProfile lp = LoginProfile();
    lp.nickname = "me";
    lp.landline = "021-5555";
    lp.post = "PM";
    Json::Value login;
    REQUIRE(upfParseJson(LoginProtocol::buildLoginSuccessResponse(8, lp, 0), login));
    CHECK(login["profile"]["landline"].asString() == "021-5555");
    CHECK(login["profile"]["post"].asString() == "PM");
    LoginProfile lpBlank = LoginProfile();
    Json::Value loginBlank;
    REQUIRE(upfParseJson(LoginProtocol::buildLoginSuccessResponse(8, lpBlank, 0), loginBlank));
    CHECK(loginBlank["profile"].isMember("landline"));
    CHECK(loginBlank["profile"].isMember("post"));
    CHECK(loginBlank["profile"]["landline"].asString().empty());

    //资料 JSON：1103 应答与 1108 推送共用 makeProfileJson
    UserProfile p = UserProfile();
    p.userid = 100001;
    p.landline = "0755-1";
    p.post = u8"测试";
    Json::Value info;
    REQUIRE(upfParseJson(ProfileProtocol::buildGetUserInfoResponse(0, "ok", p), info));
    CHECK(info["profile"]["landline"].asString() == "0755-1");
    CHECK(info["profile"]["post"].asString() == u8"测试");
    Json::Value notify;
    REQUIRE(upfParseJson(ProfileProtocol::buildUserInfoChangedNotify(p), notify));
    CHECK(notify["profile"]["landline"].asString() == "0755-1");
    CHECK(notify["profile"]["post"].asString() == u8"测试");
}

TEST_CASE("UPF9 改岗位命中敏感词时被拦下；座机不参与敏感词检查")
{
    //需要检查的文字只取到岗位，不取座机
    UserProfile current = UserProfile();
    current.post = "old";
    current.landline = "old";
    UpdateProfileRequest req;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest(
        u8"{\"post\":\"违禁岗位\",\"landline\":\"违禁\"}", req));
    std::vector<std::string> texts;
    ProfileProtocol::collectChangedProfileTexts(req, current, texts);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0] == u8"违禁岗位");

    //岗位与当前值相同（整份资料原样带上来）时不检查
    UserProfile same = UserProfile();
    same.post = u8"违禁岗位";
    std::vector<std::string> none;
    ProfileProtocol::collectChangedProfileTexts(req, same, none);
    CHECK(none.empty());

    //交给企业词库检查：岗位那一段命中；只改座机时没有需要检查的文字，不会被拦下
    testdb::resetDb(UPF_DB_PATH, UPF_MYSQL_DB);
    {
        std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
        REQUIRE(pConn);
        REQUIRE(pConn->execute("INSERT INTO t_enterprise(f_enterprise_id, f_name, f_create_time) "
                               "VALUES(9101, 'upf9', '2026-10-04 10:00:00')"));
    }
    const int64_t kUpfEnterprise = 9101;
    const int64_t kUpfAdminId = 7;
    const int64_t kUpfNow = 1700000000000LL;
    SensitiveWordManager& mgr = Singleton<SensitiveWordManager>::Instance();
    REQUIRE(mgr.init());
    mgr.invalidate(kUpfEnterprise);
    int64_t wordId = 0;
    REQUIRE(mgr.addWord(kUpfEnterprise, u8"违禁", chatserver::sensitive_category_illegal, "", kUpfAdminId,
                        "admin", kUpfNow, &wordId) == chatserver::sensitive_word_op_ok);
    SensitiveCheckResult r;
    CHECK(mgr.checkTexts(kUpfEnterprise, texts, &r));
    CHECK(r.m_word == std::string(u8"违禁"));

    UpdateProfileRequest landlineOnly;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest(u8"{\"landline\":\"违禁\"}", landlineOnly));
    std::vector<std::string> landlineTexts;
    ProfileProtocol::collectChangedProfileTexts(landlineOnly, current, landlineTexts);
    CHECK(landlineTexts.empty());
    CHECK_FALSE(mgr.checkTexts(kUpfEnterprise, landlineTexts, &r));

    mgr.invalidate(kUpfEnterprise);
    testdb::cleanupDb(UPF_DB_PATH);
}

// =====================================================================
// 第 9 节：手机号、座机对同事的公开范围（2026-10-06）
// =====================================================================

//造一个各字段都有值的用户记录（不落库），供 UPF14～UPF15 填通讯录与资料
static User upfMakeUser(int32_t userid, const char* phone, const char* landline, int phonevisible, int landlinevisible)
{
    User u;
    u.userid = userid;
    u.username = "EMP" + std::to_string(userid);
    u.password = "x";
    u.nickname = "nick" + std::to_string(userid);
    u.facetype = 0;
    u.gender = profile_gender_female;
    u.birthday = 19960812;
    u.signature = "sign";
    u.address = "addr";
    u.phonenumber = phone;
    u.mail = "a@b.c";
    u.landline = landline;
    u.post = "post";
    u.phonevisible = phonevisible;
    u.landlinevisible = landlinevisible;
    u.ownerid = 0;
    u.status = 0;
    u.usertype = 0;
    return u;
}

TEST_CASE("UPF10 t_user 有两个公开范围列：新库默认 0 与 1")
{
    //新库：按完整结构建出
    removeUpfDbFiles(UPF_DB_PATH);
    {
        CSqliteManager manager;
        REQUIRE(manager.init(UPF_DB_PATH));
        CDatabaseSqlite db;
        REQUIRE(db.initialize(UPF_DB_PATH));
        CHECK(upfHasColumn(db, "f_phone_visible"));
        CHECK(upfHasColumn(db, "f_landline_visible"));
    }
    removeUpfDbFiles(UPF_DB_PATH);

    //新注册的用户：缓存与库里都是默认值
    testdb::resetDb(UPF_DB_PATH, UPF_MYSQL_DB);
    const int32_t userid = upfAddUser("upf10");
    User cached;
    REQUIRE(Singleton<UserManager>::Instance().getUserInfoByUserId(userid, cached));
    CHECK(cached.phonevisible == profile_visibility_self_only);
    CHECK(cached.landlinevisible == profile_visibility_colleagues);
    CHECK(upfReadColumn(userid, "f_phone_visible") == "0");
    CHECK(upfReadColumn(userid, "f_landline_visible") == "1");
    testdb::cleanupDb(UPF_DB_PATH);
}

TEST_CASE("UPF11 公开范围：updateUserInfoInDb 写进库与缓存，按库重新加载后一致")
{
    testdb::resetDb(UPF_DB_PATH, UPF_MYSQL_DB);
    UserManager& userMgr = Singleton<UserManager>::Instance();
    const int32_t userid = upfAddUser("upf11");

    User u;
    REQUIRE(userMgr.getUserInfoByUserId(userid, u));
    u.phonevisible = profile_visibility_colleagues;
    u.landlinevisible = profile_visibility_self_only;
    REQUIRE(userMgr.updateUserInfoInDb(userid, u));

    //库里与缓存里都是新值
    CHECK(upfReadColumn(userid, "f_phone_visible") == "1");
    CHECK(upfReadColumn(userid, "f_landline_visible") == "0");
    User cached;
    REQUIRE(userMgr.getUserInfoByUserId(userid, cached));
    CHECK(cached.phonevisible == profile_visibility_colleagues);
    CHECK(cached.landlinevisible == profile_visibility_self_only);

    //库里另行改回，再按库重新加载：缓存取到的是库里的值（加载路径确实读了这两列）
    {
        std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
        REQUIRE(pConn);
        const std::string sql = std::string("UPDATE t_user SET f_phone_visible = 0, f_landline_visible = 1 "
                                            "WHERE f_user_id = ") + std::to_string(userid);
        REQUIRE(pConn->execute(sql.c_str()));
    }
    REQUIRE(userMgr.reloadUserCache(userid));
    User reloaded;
    REQUIRE(userMgr.getUserInfoByUserId(userid, reloaded));
    CHECK(reloaded.phonevisible == profile_visibility_self_only);
    CHECK(reloaded.landlinevisible == profile_visibility_colleagues);

    testdb::cleanupDb(UPF_DB_PATH);
}

TEST_CASE("UPF12 改资料请求：两个公开范围带了才改，只接受 0 与 1，类型不符视为没带")
{
    UpdateProfileRequest req;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest("{\"phonevisible\":1,\"landlinevisible\":0}", req));
    CHECK(req.has_phonevisible);
    CHECK(req.phonevisible == profile_visibility_colleagues);
    CHECK(req.has_landlinevisible);
    CHECK(req.landlinevisible == profile_visibility_self_only);
    CHECK_FALSE(req.has_nickname);
    CHECK_FALSE(req.has_landline);

    //合并：两个标志改掉，号码与其它字段不变
    User u = upfMakeUser(100012, "13800000012", "010-12", profile_visibility_self_only,
                         profile_visibility_colleagues);
    ProfileProtocol::applyProfileUpdate(req, u);
    CHECK(u.phonevisible == profile_visibility_colleagues);
    CHECK(u.landlinevisible == profile_visibility_self_only);
    CHECK(u.phonenumber == "13800000012");
    CHECK(u.landline == "010-12");
    CHECK(u.nickname == "nick100012");

    //只带一个：另一个保持
    UpdateProfileRequest one;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest("{\"landlinevisible\":1}", one));
    CHECK_FALSE(one.has_phonevisible);
    CHECK(one.has_landlinevisible);
    ProfileProtocol::applyProfileUpdate(one, u);
    CHECK(u.phonevisible == profile_visibility_colleagues);
    CHECK(u.landlinevisible == profile_visibility_colleagues);

    //都不带：两个标志不变
    UpdateProfileRequest none;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest("{\"nickname\":\"n12\"}", none));
    CHECK_FALSE(none.has_phonevisible);
    CHECK_FALSE(none.has_landlinevisible);
    ProfileProtocol::applyProfileUpdate(none, u);
    CHECK(u.phonevisible == profile_visibility_colleagues);
    CHECK(u.landlinevisible == profile_visibility_colleagues);

    //取值越界：整个请求被拒
    CHECK(upfParseInt("phonevisible", profile_visibility_self_only));
    CHECK(upfParseInt("phonevisible", profile_visibility_colleagues));
    CHECK_FALSE(upfParseInt("phonevisible", 2));
    CHECK_FALSE(upfParseInt("phonevisible", -1));
    CHECK(upfParseInt("landlinevisible", profile_visibility_self_only));
    CHECK(upfParseInt("landlinevisible", profile_visibility_colleagues));
    CHECK_FALSE(upfParseInt("landlinevisible", 2));
    CHECK_FALSE(upfParseInt("landlinevisible", -1));

    //类型不符（字符串、布尔）：与其它字段一样视为没带，请求本身仍合法
    UpdateProfileRequest wrongType;
    REQUIRE(ProfileProtocol::parseUpdateProfileRequest("{\"phonevisible\":\"1\",\"landlinevisible\":true}", wrongType));
    CHECK_FALSE(wrongType.has_phonevisible);
    CHECK_FALSE(wrongType.has_landlinevisible);
}

TEST_CASE("UPF13 按查看者处理号码：本人看自己原样，同事看时按两个公开范围分别隐藏，标志只发给本人")
{
    const int kValues[] = { profile_visibility_self_only, profile_visibility_colleagues };
    const size_t kValueCount = sizeof(kValues) / sizeof(kValues[0]);
    const std::string kPhone = "13800000013";
    const std::string kLandline = "010-13";
    for (size_t i = 0; i < kValueCount; ++i)
    {
        for (size_t j = 0; j < kValueCount; ++j)
        {
            CAPTURE(kValues[i]);
            CAPTURE(kValues[j]);
            //本人看自己：四种组合都原样
            std::string phone = kPhone;
            std::string landline = kLandline;
            ProfileProtocol::maskNumbersForViewer(kValues[i], kValues[j], true, phone, landline);
            CHECK(phone == kPhone);
            CHECK(landline == kLandline);

            //同事看：只有「对同事可见」的号码保留
            phone = kPhone;
            landline = kLandline;
            ProfileProtocol::maskNumbersForViewer(kValues[i], kValues[j], false, phone, landline);
            CHECK(phone == (kValues[i] == profile_visibility_colleagues ? kPhone : std::string()));
            CHECK(landline == (kValues[j] == profile_visibility_colleagues ? kLandline : std::string()));
        }
    }

    //约定之外的取值按不公开处理
    std::string phone = kPhone;
    std::string landline = kLandline;
    ProfileProtocol::maskNumbersForViewer(2, -1, false, phone, landline);
    CHECK(phone.empty());
    CHECK(landline.empty());

    //applyViewer：只有号码与 hasvisibility 随查看者变化，其它字段不动
    UserProfile p = UserProfile();
    p.userid = 100013;
    p.nickname = "n13";
    p.post = "PM";
    p.phonenumber = kPhone;
    p.landline = kLandline;
    p.phonevisible = profile_visibility_self_only;
    p.landlinevisible = profile_visibility_colleagues;

    UserProfile mine = p;
    ProfileProtocol::applyViewer(mine, 100013);
    CHECK(mine.hasvisibility);
    CHECK(mine.phonenumber == kPhone);
    CHECK(mine.landline == kLandline);

    UserProfile other = p;
    ProfileProtocol::applyViewer(other, 100014);
    CHECK_FALSE(other.hasvisibility);
    CHECK(other.phonenumber.empty());
    CHECK(other.landline == kLandline);
    CHECK(other.nickname == "n13");
    CHECK(other.post == "PM");

    //资料 JSON：本人那份带两个标志，同事那份不带
    Json::Value mineJson;
    REQUIRE(upfParseJson(ProfileProtocol::buildGetUserInfoResponse(0, "ok", mine), mineJson));
    CHECK(mineJson["profile"]["phonevisible"].asInt() == profile_visibility_self_only);
    CHECK(mineJson["profile"]["landlinevisible"].asInt() == profile_visibility_colleagues);
    Json::Value otherJson;
    REQUIRE(upfParseJson(ProfileProtocol::buildGetUserInfoResponse(0, "ok", other), otherJson));
    CHECK_FALSE(otherJson["profile"].isMember("phonevisible"));
    CHECK_FALSE(otherJson["profile"].isMember("landlinevisible"));
    CHECK(otherJson["profile"]["phonenumber"].asString().empty());
}

TEST_CASE("UPF14 通讯录列表：同事的号码按其公开范围处理，请求人自己那一行完整，不下发公开范围标志")
{
    //A 手机号不公开、座机公开（默认）；B 手机号公开、座机不公开
    const int64_t kUserA = 100021;
    const int64_t kUserB = 100022;
    const User a = upfMakeUser(static_cast<int32_t>(kUserA), "13800000021", "010-21",
                               profile_visibility_self_only, profile_visibility_colleagues);
    const User b = upfMakeUser(static_cast<int32_t>(kUserB), "13800000022", "010-22",
                               profile_visibility_colleagues, profile_visibility_self_only);

    //A 请求通讯录：自己那一行完整，B 的座机被隐藏
    ContactInfo aSelf = ContactInfo();
    ContactsProtocol::fillContactFromUser(aSelf, a, kUserA);
    CHECK(aSelf.phonenumber == "13800000021");
    CHECK(aSelf.landline == "010-21");
    ContactInfo bForA = ContactInfo();
    ContactsProtocol::fillContactFromUser(bForA, b, kUserA);
    CHECK(bForA.phonenumber == "13800000022");
    CHECK(bForA.landline.empty());
    //其它资料字段照常填
    CHECK(bForA.userid == kUserB);
    CHECK(bForA.nickname == "nick100022");
    CHECK(bForA.post == "post");
    CHECK(bForA.mail == "a@b.c");
    CHECK(bForA.gender == profile_gender_female);
    CHECK(bForA.birthday == 19960812);

    //B 请求通讯录：A 的手机号被隐藏、座机保留
    ContactInfo aForB = ContactInfo();
    //在线状态与备注名由会话层另行填写，本函数不改动它们
    aForB.presence = presence_online;
    aForB.remark = "remark";
    ContactsProtocol::fillContactFromUser(aForB, a, kUserB);
    CHECK(aForB.phonenumber.empty());
    CHECK(aForB.landline == "010-21");
    CHECK(aForB.presence == presence_online);
    CHECK(aForB.remark == "remark");

    //应答 JSON：号码按上面的结果下发，不带公开范围标志
    ContactsData data;
    data.enterpriseid = 5001;
    data.enterprisename = "ent";
    data.synctime = 0;
    data.contacts.push_back(aSelf);
    data.contacts.push_back(bForA);
    Json::Value contacts;
    REQUIRE(upfParseJson(ContactsProtocol::buildContactsResponse(data), contacts));
    REQUIRE(contacts["contacts"].size() == 2);
    CHECK(contacts["contacts"][0]["phonenumber"].asString() == "13800000021");
    CHECK(contacts["contacts"][1]["phonenumber"].asString() == "13800000022");
    CHECK(contacts["contacts"][1]["landline"].asString().empty());
    CHECK_FALSE(contacts["contacts"][0].isMember("phonevisible"));
    CHECK_FALSE(contacts["contacts"][1].isMember("landlinevisible"));
}

TEST_CASE("UPF15 取资料按查看者处理：取别人的不公开号码为空，取自己的完整且带两个标志；登录应答带两个标志")
{
    const int64_t kUserA = 100031;
    const int64_t kUserB = 100032;
    //B 两个号码都不公开
    const User b = upfMakeUser(static_cast<int32_t>(kUserB), "13800000032", "010-32",
                               profile_visibility_self_only, profile_visibility_self_only);

    //从用户记录得到的是完整资料；企业ID与在线状态由会话层另行填写，本函数不改动它们
    UserProfile full = UserProfile();
    full.enterpriseid = 5001;
    full.presence = presence_busy;
    ProfileProtocol::fillProfileFromUser(full, b);
    CHECK(full.phonenumber == "13800000032");
    CHECK(full.landline == "010-32");
    CHECK(full.phonevisible == profile_visibility_self_only);
    CHECK(full.landlinevisible == profile_visibility_self_only);
    CHECK_FALSE(full.hasvisibility);
    CHECK(full.enterpriseid == 5001);
    CHECK(full.presence == presence_busy);
    CHECK(full.nickname == "nick100032");

    //A 取 B 的资料
    UserProfile forA = full;
    ProfileProtocol::applyViewer(forA, kUserA);
    Json::Value forAJson;
    REQUIRE(upfParseJson(ProfileProtocol::buildGetUserInfoResponse(0, "ok", forA), forAJson));
    CHECK(forAJson["profile"]["phonenumber"].asString().empty());
    CHECK(forAJson["profile"]["landline"].asString().empty());
    CHECK_FALSE(forAJson["profile"].isMember("phonevisible"));
    CHECK(forAJson["profile"]["nickname"].asString() == "nick100032");

    //B 取自己的资料
    UserProfile forB = full;
    ProfileProtocol::applyViewer(forB, kUserB);
    Json::Value forBJson;
    REQUIRE(upfParseJson(ProfileProtocol::buildGetUserInfoResponse(0, "ok", forB), forBJson));
    CHECK(forBJson["profile"]["phonenumber"].asString() == "13800000032");
    CHECK(forBJson["profile"]["landline"].asString() == "010-32");
    CHECK(forBJson["profile"]["phonevisible"].asInt() == profile_visibility_self_only);
    CHECK(forBJson["profile"]["landlinevisible"].asInt() == profile_visibility_self_only);

    //登录应答只发给本人：总是带两个标志，号码完整
    LoginProfile lp = LoginProfile();
    lp.phonenumber = "13800000031";
    lp.landline = "010-31";
    lp.phonevisible = profile_visibility_colleagues;
    lp.landlinevisible = profile_visibility_self_only;
    Json::Value login;
    REQUIRE(upfParseJson(LoginProtocol::buildLoginSuccessResponse(kUserA, lp, 0), login));
    CHECK(login["profile"]["phonenumber"].asString() == "13800000031");
    CHECK(login["profile"]["phonevisible"].asInt() == profile_visibility_colleagues);
    CHECK(login["profile"]["landlinevisible"].asInt() == profile_visibility_self_only);
    //值初始化的 LoginProfile（标志为 0）同样下发，不会缺字段
    Json::Value loginBlank;
    REQUIRE(upfParseJson(LoginProtocol::buildLoginSuccessResponse(kUserA, LoginProfile(), 0), loginBlank));
    CHECK(loginBlank["profile"].isMember("phonevisible"));
    CHECK(loginBlank["profile"].isMember("landlinevisible"));
}

TEST_CASE("UPF16 资料变更推送：本人一份号码完整且带标志，同事一份按公开范围处理且不带标志")
{
    UserProfile full = UserProfile();
    full.userid = 100041;
    full.nickname = "n41";
    full.signature = "s41";
    full.phonenumber = "13800000041";
    full.landline = "010-41";
    full.phonevisible = profile_visibility_self_only;
    full.landlinevisible = profile_visibility_colleagues;

    std::string ownerBody;
    std::string colleagueBody;
    ProfileProtocol::buildUserInfoChangedNotifies(full, ownerBody, colleagueBody);

    Json::Value owner;
    REQUIRE(upfParseJson(ownerBody, owner));
    CHECK(owner["userid"].asString() == "100041");
    CHECK(owner["profile"]["phonenumber"].asString() == "13800000041");
    CHECK(owner["profile"]["landline"].asString() == "010-41");
    CHECK(owner["profile"]["phonevisible"].asInt() == profile_visibility_self_only);
    CHECK(owner["profile"]["landlinevisible"].asInt() == profile_visibility_colleagues);

    Json::Value colleague;
    REQUIRE(upfParseJson(colleagueBody, colleague));
    CHECK(colleague["userid"].asString() == "100041");
    CHECK(colleague["profile"]["phonenumber"].asString().empty());
    CHECK(colleague["profile"]["landline"].asString() == "010-41");
    CHECK_FALSE(colleague["profile"].isMember("phonevisible"));
    CHECK_FALSE(colleague["profile"].isMember("landlinevisible"));
    //其它资料字段两份相同
    CHECK(colleague["profile"]["nickname"].asString() == "n41");
    CHECK(colleague["profile"]["signature"].asString() == "s41");

    //本人把手机号改成公开、座机改成不公开后：同事那份随之变化
    full.phonevisible = profile_visibility_colleagues;
    full.landlinevisible = profile_visibility_self_only;
    ProfileProtocol::buildUserInfoChangedNotifies(full, ownerBody, colleagueBody);
    Json::Value colleague2;
    REQUIRE(upfParseJson(colleagueBody, colleague2));
    CHECK(colleague2["profile"]["phonenumber"].asString() == "13800000041");
    CHECK(colleague2["profile"]["landline"].asString().empty());
    Json::Value owner2;
    REQUIRE(upfParseJson(ownerBody, owner2));
    CHECK(owner2["profile"]["landline"].asString() == "010-41");
    CHECK(owner2["profile"]["phonevisible"].asInt() == profile_visibility_colleagues);
}
