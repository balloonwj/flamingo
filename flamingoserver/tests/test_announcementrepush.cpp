/**
 *  公告「重新推送」的服务端测试（Suite ARP），test_announcementrepush.cpp
 *
 *  admin 控制台的 announcement/repush.do（2026-10-04 新增）把一条已发布的公告再推一次，
 *  只推给受众里当前在线、且还没读过它的人。推给谁由 AnnouncementManager 决定，本套用例
 *  对着临时库真的跑它的 SQL：
 *    - ARP1：filterUnreadAudienceAmong 从候选（在线名单）里只挑出受众中未读的人 ——
 *            受众之外的、已读的、库里不存在的都不算；全部读过之后为空；
 *    - ARP2：checkRepushable 只放行已发布的公告，草稿与已撤回的公告返回
 *            error_code_announcement_notpublished，不存在的公告返回 error_code_unknown。
 *
 *  每个用例先用 testdb::resetDb 把 CDbConfig 指到本套专用的库文件上。员工直接写入 t_user，
 *  公告经 AnnouncementManager 的保存、发布、撤回、已读接口写入（与产品代码同一条路径）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/AnnouncementManager.h"
#include "../chatserversrc/AdminApiCodes.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件（SQLite 模式），以及 MySQL 模式下会被整个重建的库名
static const char* const ARP_DB_PATH = "flamingo_sqlite_announcementrepush_test.db";
static const char* const ARP_MYSQL_DB = "flamingo_test_announcementrepush";

//两家测试企业的业务 ID：公告投给前一家，后一家的员工不在受众里
static const int64_t ARP_ENT_TARGET = 7;
static const int64_t ARP_ENT_OTHER  = 8;

//测试员工的用户 ID：前两名属于受众企业，第三名属于另一家，第四名在库里不存在
static const int32_t ARP_USER_A       = 101;
static const int32_t ARP_USER_B       = 102;
static const int32_t ARP_USER_OTHER   = 103;
static const int32_t ARP_USER_MISSING = 104;

//受众类型「指定企业」与目标类型「企业」的编码（见 AnnouncementManager.h 的 SaveAnnouncementReq）
static const int32_t ARP_AUDIENCE_ENTERPRISE = 1;
static const int32_t ARP_TARGET_ENTERPRISE   = 1;

//发布公告的管理员（只写进 t_announcement 的发送者字段，不参与断言）
static const int64_t ARP_SENDER_ID = 1;

//直接插入一名员工
//  userId：用户 ID；enterpriseId：所属企业
static void arpInsertUser(int32_t userId, int64_t enterpriseId)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    const std::string id = std::to_string(userId);
    //f_password 非空（table_mysql.sql）；这里只是准备测试数据，不需要能登录，填空串
    const std::string sql =
        "INSERT INTO t_user(f_user_id, f_username, f_nickname, f_password, f_enterprise_id, f_register_time) VALUES("
        + id + ", 'arp" + id + "', 'arp" + id + "', '', " + std::to_string(enterpriseId) + ", NOW())";
    REQUIRE(pConn->execute(sql.c_str()));
}

//保存一条投给 ARP_ENT_TARGET 的公告（草稿），返回公告 ID
static int64_t arpSaveDraft()
{
    SaveAnnouncementReq req;
    req.title = "repush";
    req.body = "body";
    req.audienceType = ARP_AUDIENCE_ENTERPRISE;
    AnnouncementTarget target;
    target.type = ARP_TARGET_ENTERPRISE;
    target.id = ARP_ENT_TARGET;
    req.targets.push_back(target);
    int64_t annId = 0;
    REQUIRE(Singleton<AnnouncementManager>::Instance().saveAnnouncement(req, ARP_SENDER_ID, "arp", annId)
            == error_code_ok);
    REQUIRE(annId > 0);
    return annId;
}

//立即发布一条公告
static void arpPublish(int64_t annId)
{
    REQUIRE(Singleton<AnnouncementManager>::Instance().sendAnnouncement(annId, false, std::string())
            == error_code_ok);
}

TEST_CASE("ARP1 重新推送只挑出在线名单里属于受众且未读的人")
{
    testdb::resetDb(ARP_DB_PATH, ARP_MYSQL_DB);
    arpInsertUser(ARP_USER_A, ARP_ENT_TARGET);
    arpInsertUser(ARP_USER_B, ARP_ENT_TARGET);
    arpInsertUser(ARP_USER_OTHER, ARP_ENT_OTHER);

    AnnouncementManager& mgr = Singleton<AnnouncementManager>::Instance();
    const int64_t annId = arpSaveDraft();
    arpPublish(annId);
    REQUIRE(mgr.markAnnouncementRead(annId, ARP_USER_A) == error_code_ok);

    std::vector<int32_t> online;
    online.push_back(ARP_USER_A);
    online.push_back(ARP_USER_B);
    online.push_back(ARP_USER_OTHER);
    online.push_back(ARP_USER_MISSING);

    //对照：发布时的筛选不看已读，受众企业的两人都在
    std::vector<int32_t> audience;
    mgr.filterAudienceAmong(annId, online, audience);
    std::sort(audience.begin(), audience.end());
    REQUIRE(audience.size() == 2);
    CHECK(audience[0] == ARP_USER_A);
    CHECK(audience[1] == ARP_USER_B);

    //重新推送：已读的 A、受众之外的 OTHER、库里没有的 MISSING 都不算，只剩 B
    std::vector<int32_t> unread;
    mgr.filterUnreadAudienceAmong(annId, online, unread);
    REQUIRE(unread.size() == 1);
    CHECK(unread[0] == ARP_USER_B);

    //B 也读过之后，没有可推的人
    REQUIRE(mgr.markAnnouncementRead(annId, ARP_USER_B) == error_code_ok);
    mgr.filterUnreadAudienceAmong(annId, online, unread);
    CHECK(unread.empty());

    //候选为空时直接返回空
    std::vector<int32_t> none;
    mgr.filterUnreadAudienceAmong(annId, none, unread);
    CHECK(unread.empty());

    testdb::cleanupDb(ARP_DB_PATH);
}

TEST_CASE("ARP2 只有已发布的公告可以重新推送，草稿与已撤回的被拒绝")
{
    testdb::resetDb(ARP_DB_PATH, ARP_MYSQL_DB);
    arpInsertUser(ARP_USER_B, ARP_ENT_TARGET);

    AnnouncementManager& mgr = Singleton<AnnouncementManager>::Instance();
    const int64_t annId = arpSaveDraft();

    //草稿
    CHECK(mgr.checkRepushable(annId) == error_code_announcement_notpublished);

    //已发布
    arpPublish(annId);
    CHECK(mgr.checkRepushable(annId) == error_code_ok);

    //已撤回：筛选同样得不到任何人（filterAudienceAmong 本来就只认已发布）
    REQUIRE(mgr.recallAnnouncement(annId) == error_code_ok);
    CHECK(mgr.checkRepushable(annId) == error_code_announcement_notpublished);
    std::vector<int32_t> online(1, ARP_USER_B);
    std::vector<int32_t> unread;
    mgr.filterUnreadAudienceAmong(annId, online, unread);
    CHECK(unread.empty());

    //不存在的公告与非法 ID
    CHECK(mgr.checkRepushable(annId + 1000) == error_code_unknown);
    CHECK(mgr.checkRepushable(0) == error_code_unknown);

    testdb::cleanupDb(ARP_DB_PATH);
}
