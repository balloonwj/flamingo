/**
 *  客户端版本发布记录管理器单元测试（Suite CVM），test_clientversionmanager.cpp
 *
 *  覆盖 ClientVersionManager::getLatestVersion 的取数规则：
 *    - 一条已发布记录都没有时返回"查不到"，而不是报错；
 *    - 草稿（f_enabled=0）不参与选取；
 *    - 多条已发布时取 f_publish_time 最大的一条，而不是版本号最大的一条；
 *    - 不同平台互不干扰；
 *    - 各字段原样读回，含中文更新说明与超过 2GiB 的包大小；
 *    - 更新说明里含单引号 / 反斜杠时读回内容不被改动。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— ClientVersionManager 经 DbFactory
 *  取连接，而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套
 *  专用的库文件上。写法与 test_contactremarkmanager.cpp 同构。
 *
 *  注意：ClientVersionManager <u>只读不写</u>（发版是手工插库，将来由 admin 控制台负责），
 *  所以 SQL 转义的责任在写入方，不在本类。CVM6 验的是读回路径不会改动内容，
 *  插入时的转义由本文件的 insertVersion 辅助函数自己负责。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include <stdio.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/ClientVersionManager.h"
#include "../chatserversrc/Protocol.h"       //enum platform_type
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/SqlDialect.h"             //EscapeString
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与其它几套用的文件分开，避免互相干扰
static const char* const CVM_DB_PATH = "flamingo_sqlite_clientversion_test.db";

//本套用例的时间基准（Unix 毫秒）。发布时间只用于比较先后，取固定值即可，
//避免用例之间因取当前时间而结果不可复现。
static const int64_t CVM_BASE_TIME = 1723363200000LL;

//f_enabled 取值，与产品代码里的约定一致：1 已发布、0 草稿
static const int CVM_ENABLED = 1;
static const int CVM_DRAFT   = 0;

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeCvmDbFiles()
{
    std::string basePath(CVM_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次，保证从确定的初始状态开始 —— ClientVersionManager 是单例，
//用例之间共享，唯一能隔离的就是底下这个库文件。
static void resetCvmDb()
{
    removeCvmDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(CVM_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, CVM_DB_PATH);
}

//往 t_client_version 插一行。发版在产品代码里没有写入路径（本期手工插库），
//故插入逻辑由测试自己实现；文本字段一律转义后再拼进 SQL。
static void insertVersion(int platform, const std::string& version,
                          const std::string& minVersion, const std::string& releaseNote,
                          const std::string& md5, int64_t size,
                          const std::string& packageName, int enabled, int64_t publishTime)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);

    std::ostringstream osSql;
    osSql << "INSERT INTO t_client_version(f_platform, f_version, f_min_version, f_release_note,"
          << " f_package_md5, f_package_size, f_package_name, f_enabled, f_publish_time) VALUES("
          << platform
          << ", '" << dbapi::EscapeString(version) << "'"
          << ", '" << dbapi::EscapeString(minVersion) << "'"
          << ", '" << dbapi::EscapeString(releaseNote) << "'"
          << ", '" << dbapi::EscapeString(md5) << "'"
          << ", " << size
          << ", '" << dbapi::EscapeString(packageName) << "'"
          << ", " << enabled
          << ", " << publishTime << ")";
    REQUIRE(pConn->execute(osSql.str().c_str()));
}

// ---------------------------------------------------------------------------
// CVM1  空表
// ---------------------------------------------------------------------------

TEST_CASE("CVM1 一条已发布记录都没有时返回查不到")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    ClientVersionInfo info;
    CHECK_FALSE(mgr.getLatestVersion(platform_pc, info));
    //出参须被清空，调用方不会读到上一次查询的残留
    CHECK(info.version.empty());
    CHECK(info.packageSize == 0);

    //平台号非法时同样返回查不到，且不去连库
    CHECK_FALSE(mgr.getLatestVersion(0, info));
    CHECK_FALSE(mgr.getLatestVersion(-1, info));
}

// ---------------------------------------------------------------------------
// CVM2  草稿不参与选取
// ---------------------------------------------------------------------------

TEST_CASE("CVM2 只取已发布记录，草稿不下发")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    //3.4.0 更新更晚但还是草稿，3.3.0 已发布 —— 应当取到 3.3.0
    insertVersion(platform_pc, "3.3.0", "3.1.0", "已发布的版本", "aaa", 100,
                  "Flamingo-3.3.0.zip", CVM_ENABLED, CVM_BASE_TIME);
    insertVersion(platform_pc, "3.4.0", "3.1.0", "还在草稿", "bbb", 200,
                  "Flamingo-3.4.0.zip", CVM_DRAFT, CVM_BASE_TIME + 1000);

    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.3.0");
    CHECK(info.packageMd5 == "aaa");

    //把草稿放出去之后，取到的就变成它了
    resetCvmDb();
    insertVersion(platform_pc, "3.4.0", "3.1.0", "改成已发布", "bbb", 200,
                  "Flamingo-3.4.0.zip", CVM_ENABLED, CVM_BASE_TIME + 1000);
    ClientVersionInfo info2;
    REQUIRE(mgr.getLatestVersion(platform_pc, info2));
    CHECK(info2.version == "3.4.0");
}

// ---------------------------------------------------------------------------
// CVM3  取发布时间最新的一条
// ---------------------------------------------------------------------------

TEST_CASE("CVM3 多条已发布时取发布时间最新的一条，而不是版本号最大的一条")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    //3.4.0 先发，之后把 3.3.0 重新发布了一次（典型的回滚操作）：
    //取数规则按发布时间，故应当取到版本号更小的 3.3.0
    insertVersion(platform_pc, "3.4.0", "3.1.0", "有问题的版本", "newer", 100,
                  "Flamingo-3.4.0.zip", CVM_ENABLED, CVM_BASE_TIME + 1000);
    insertVersion(platform_pc, "3.3.0", "3.1.0", "回滚回来的稳定版", "older", 100,
                  "Flamingo-3.3.0.zip", CVM_ENABLED, CVM_BASE_TIME + 2000);

    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.3.0");
    CHECK(info.releaseNote == "回滚回来的稳定版");

    //发布时间相同时按 f_id 兜底取后插入的那条，保证结果确定
    resetCvmDb();
    insertVersion(platform_pc, "3.5.0", "3.1.0", "先插入", "first", 100,
                  "a.zip", CVM_ENABLED, CVM_BASE_TIME);
    insertVersion(platform_pc, "3.6.0", "3.1.0", "后插入", "second", 100,
                  "b.zip", CVM_ENABLED, CVM_BASE_TIME);
    ClientVersionInfo tie;
    REQUIRE(mgr.getLatestVersion(platform_pc, tie));
    CHECK(tie.version == "3.6.0");
}

// ---------------------------------------------------------------------------
// CVM4  平台隔离
// ---------------------------------------------------------------------------

TEST_CASE("CVM4 不同平台各取各的")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    insertVersion(platform_pc, "3.3.0", "3.1.0", "PC 版", "pcmd5", 100,
                  "Flamingo-3.3.0.zip", CVM_ENABLED, CVM_BASE_TIME);
    insertVersion(platform_android, "2.1.0", "2.0.0", "安卓版", "andmd5", 200,
                  "Flamingo-2.1.0.apk", CVM_ENABLED, CVM_BASE_TIME + 5000);

    ClientVersionInfo pc;
    REQUIRE(mgr.getLatestVersion(platform_pc, pc));
    CHECK(pc.version == "3.3.0");
    CHECK(pc.packageMd5 == "pcmd5");

    //安卓那条发布时间更晚，但不该影响 PC 的取值
    ClientVersionInfo android;
    REQUIRE(mgr.getLatestVersion(platform_android, android));
    CHECK(android.version == "2.1.0");
    CHECK(android.packageMd5 == "andmd5");

    //没发过版的平台仍然查不到
    ClientVersionInfo ios;
    CHECK_FALSE(mgr.getLatestVersion(platform_ios, ios));
}

// ---------------------------------------------------------------------------
// CVM5  字段完整读回
// ---------------------------------------------------------------------------

TEST_CASE("CVM5 各字段原样读回，含中文说明与超过 2GiB 的包大小")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    //3GiB：超过 int32 上界，用来确认取值路径按 64 位处理，不会截断成负数
    const int64_t bigSize = 3221225472LL;
    insertVersion(platform_pc, "3.3.0", "3.1.0",
                  "新增群接龙；修复若干问题。本次为全量包，约 3GB。",
                  "9f2c8ab1d4e5f60718293a4b5c6d7e8f", bigSize,
                  "Flamingo-3.3.0-全量包.zip", CVM_ENABLED, CVM_BASE_TIME);

    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.3.0");
    CHECK(info.minVersion == "3.1.0");
    CHECK(info.releaseNote == "新增群接龙；修复若干问题。本次为全量包，约 3GB。");
    CHECK(info.packageMd5 == "9f2c8ab1d4e5f60718293a4b5c6d7e8f");
    CHECK(info.packageSize == bigSize);
    CHECK(info.packageName == "Flamingo-3.3.0-全量包.zip");
}

// ---------------------------------------------------------------------------
// CVM6  特殊字符原样读回
// ---------------------------------------------------------------------------

TEST_CASE("CVM6 更新说明里含单引号与反斜杠时读回内容不被改动")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    const std::string tricky = "修复了 C:\\Program Files\\ 下的安装问题；"
                               "并修正 O'Brien 这类名字的显示";
    insertVersion(platform_pc, "3.3.1", "3.1.0", tricky, "ccc", 100,
                  "Flamingo-3.3.1.zip", CVM_ENABLED, CVM_BASE_TIME);

    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.releaseNote == tricky);
}

// ---------------------------------------------------------------------------
// CVM7  新建记录
// ---------------------------------------------------------------------------

//构造一条字段齐备的待写入记录，供写操作的几个用例复用
static ClientVersionRecord makeRecord(const std::string& version, int enabled)
{
    ClientVersionRecord rec;
    rec.platform       = platform_pc;
    rec.version        = version;
    rec.minVersion     = "3.1.0";
    rec.releaseNote    = "新增群接龙；修复 O'Brien 这类名字的显示";
    rec.packageMd5     = "9f2c8ab1d4e5f60718293a4b5c6d7e8f";
    rec.packageSize    = 3221225472LL;   //3GiB：确认大包字节数不被截断
    rec.packageName    = "Flamingo-全量包.zip";
    rec.enabled        = enabled;
    rec.creatorAdminId = 7;
    return rec;
}

TEST_CASE("CVM7 新建记录：字段落对，草稿不带发布时间")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    //直接发布：发布时间取传入的时刻
    int64_t id = 0;
    int err = -1;
    REQUIRE(mgr.createVersion(makeRecord("3.3.0", 1), CVM_BASE_TIME, id, err));
    CHECK(err == error_code_ok);
    CHECK(id > 0);

    std::vector<ClientVersionRecord> list;
    REQUIRE(mgr.listVersions(platform_pc, list));
    REQUIRE(list.size() == 1);
    CHECK(list[0].id == id);
    CHECK(list[0].version == "3.3.0");
    CHECK(list[0].minVersion == "3.1.0");
    CHECK(list[0].releaseNote == "新增群接龙；修复 O'Brien 这类名字的显示");
    CHECK(list[0].packageMd5 == "9f2c8ab1d4e5f60718293a4b5c6d7e8f");
    CHECK(list[0].packageSize == 3221225472LL);
    CHECK(list[0].packageName == "Flamingo-全量包.zip");
    CHECK(list[0].enabled == 1);
    CHECK(list[0].publishTime == CVM_BASE_TIME);
    CHECK(list[0].creatorAdminId == 7);
    CHECK(list[0].createTime == CVM_BASE_TIME);

    //存草稿：发布时间留 0，它还没被放出去
    int64_t draftId = 0;
    REQUIRE(mgr.createVersion(makeRecord("3.4.0", 0), CVM_BASE_TIME + 1000, draftId, err));
    list.clear();
    REQUIRE(mgr.listVersions(platform_pc, list));
    REQUIRE(list.size() == 2);
    const ClientVersionRecord* draft = (list[0].id == draftId) ? &list[0] : &list[1];
    CHECK(draft->enabled == 0);
    CHECK(draft->publishTime == 0);
    CHECK(draft->createTime == CVM_BASE_TIME + 1000);

    //草稿不下发：取数只看已发布的那条
    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.3.0");
}

// ---------------------------------------------------------------------------
// CVM8  新建时的入参校验
// ---------------------------------------------------------------------------

TEST_CASE("CVM8 新建时拒绝会导致客户端下载失败的入参")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    int64_t id = 0;
    int err = -1;

    //版本号为空：客户端无从比较
    ClientVersionRecord noVersion = makeRecord("", 1);
    CHECK_FALSE(mgr.createVersion(noVersion, CVM_BASE_TIME, id, err));
    CHECK(err == error_code_bad_request);

    //MD5 长度不对
    ClientVersionRecord shortMd5 = makeRecord("3.3.0", 1);
    shortMd5.packageMd5 = "9f2c";
    CHECK_FALSE(mgr.createVersion(shortMd5, CVM_BASE_TIME, id, err));
    CHECK(err == error_code_bad_request);

    //MD5 含非十六进制字符
    ClientVersionRecord badMd5 = makeRecord("3.3.0", 1);
    badMd5.packageMd5 = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";
    CHECK_FALSE(mgr.createVersion(badMd5, CVM_BASE_TIME, id, err));
    CHECK(err == error_code_bad_request);

    //包字节数非正
    ClientVersionRecord zeroSize = makeRecord("3.3.0", 1);
    zeroSize.packageSize = 0;
    CHECK_FALSE(mgr.createVersion(zeroSize, CVM_BASE_TIME, id, err));
    CHECK(err == error_code_bad_request);

    //平台号非法
    ClientVersionRecord badPlatform = makeRecord("3.3.0", 1);
    badPlatform.platform = 0;
    CHECK_FALSE(mgr.createVersion(badPlatform, CVM_BASE_TIME, id, err));
    CHECK(err == error_code_bad_request);

    //一条都不该写进去
    std::vector<ClientVersionRecord> list;
    REQUIRE(mgr.listVersions(0, list));
    CHECK(list.empty());
}

// ---------------------------------------------------------------------------
// CVM9  列表
// ---------------------------------------------------------------------------

TEST_CASE("CVM9 列表按平台过滤、按发布时间倒序，草稿同样可见")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    int64_t id = 0;
    int err = -1;
    REQUIRE(mgr.createVersion(makeRecord("3.3.0", 1), CVM_BASE_TIME, id, err));
    REQUIRE(mgr.createVersion(makeRecord("3.4.0", 1), CVM_BASE_TIME + 2000, id, err));
    REQUIRE(mgr.createVersion(makeRecord("3.5.0", 0), CVM_BASE_TIME + 3000, id, err));

    ClientVersionRecord android = makeRecord("2.1.0", 1);
    android.platform = platform_android;
    REQUIRE(mgr.createVersion(android, CVM_BASE_TIME + 4000, id, err));

    //按平台过滤：只拿 PC 的三条（含草稿）
    std::vector<ClientVersionRecord> pc;
    REQUIRE(mgr.listVersions(platform_pc, pc));
    REQUIRE(pc.size() == 3);
    //按发布时间倒序：3.4.0(+2000) → 3.3.0(+0) → 3.5.0 草稿（发布时间 0，排最后）
    CHECK(pc[0].version == "3.4.0");
    CHECK(pc[1].version == "3.3.0");
    CHECK(pc[2].version == "3.5.0");
    CHECK(pc[2].enabled == 0);

    //平台传 0 表示不过滤，四条全在
    std::vector<ClientVersionRecord> all;
    REQUIRE(mgr.listVersions(0, all));
    CHECK(all.size() == 4);

    //没发过版的平台：查询成功但列表为空
    std::vector<ClientVersionRecord> ios;
    REQUIRE(mgr.listVersions(platform_ios, ios));
    CHECK(ios.empty());
}

// ---------------------------------------------------------------------------
// CVM10  发布与撤回
// ---------------------------------------------------------------------------

TEST_CASE("CVM10 撤回之后取到次新的一条已发布记录")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    int64_t oldId = 0;
    int64_t newId = 0;
    int err = -1;
    REQUIRE(mgr.createVersion(makeRecord("3.3.0", 1), CVM_BASE_TIME, oldId, err));
    REQUIRE(mgr.createVersion(makeRecord("3.4.0", 1), CVM_BASE_TIME + 2000, newId, err));

    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.4.0");

    //撤回 3.4.0：取数落回 3.3.0
    REQUIRE(mgr.setEnabled(newId, false, CVM_BASE_TIME + 5000, err));
    CHECK(err == error_code_ok);
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.3.0");

    //把两条都撤回：没有可下发的版本
    REQUIRE(mgr.setEnabled(oldId, false, CVM_BASE_TIME + 6000, err));
    CHECK_FALSE(mgr.getLatestVersion(platform_pc, info));

    //记录不存在时如实报 not_found，而不是笼统的服务器错误
    CHECK_FALSE(mgr.setEnabled(999999, true, CVM_BASE_TIME, err));
    CHECK(err == error_code_not_found);
}

// ---------------------------------------------------------------------------
// CVM11  回滚
// ---------------------------------------------------------------------------

TEST_CASE("CVM11 重新发布旧版本即完成回滚，创建时间不受影响")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    int64_t oldId = 0;
    int64_t newId = 0;
    int err = -1;
    REQUIRE(mgr.createVersion(makeRecord("3.3.0", 1), CVM_BASE_TIME, oldId, err));
    REQUIRE(mgr.createVersion(makeRecord("3.4.0", 1), CVM_BASE_TIME + 2000, newId, err));

    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.4.0");

    //3.4.0 出了问题，把 3.3.0 重新发布一次：它立刻变回"最新发布"
    REQUIRE(mgr.setEnabled(oldId, true, CVM_BASE_TIME + 9000, err));
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.version == "3.3.0");

    //发布时间被刷新，创建时间保持原样 —— 列表页据此仍能看出这条记录是什么时候建的
    std::vector<ClientVersionRecord> list;
    REQUIRE(mgr.listVersions(platform_pc, list));
    REQUIRE(list.size() == 2);
    const ClientVersionRecord* rolled = (list[0].id == oldId) ? &list[0] : &list[1];
    CHECK(rolled->publishTime == CVM_BASE_TIME + 9000);
    CHECK(rolled->createTime == CVM_BASE_TIME);
    //3.4.0 并没有被撤回，只是不再是最新的那条
    const ClientVersionRecord* previous = (list[0].id == newId) ? &list[0] : &list[1];
    CHECK(previous->enabled == 1);
}

// ---------------------------------------------------------------------------
// CVM12  修改
// ---------------------------------------------------------------------------

TEST_CASE("CVM12 只改说明、最低版本与强制升级，不动包信息与发布状态")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    int64_t id = 0;
    int err = -1;
    REQUIRE(mgr.createVersion(makeRecord("3.3.0", 1), CVM_BASE_TIME, id, err));

    const std::string newNote = "改过的说明：含反斜杠 C:\\Program Files\\ 与单引号 O'Brien";
    REQUIRE(mgr.updateVersion(id, "3.2.0", newNote, CLIENT_VERSION_FORCED_KEEP, err));
    CHECK(err == error_code_ok);

    std::vector<ClientVersionRecord> list;
    REQUIRE(mgr.listVersions(platform_pc, list));
    REQUIRE(list.size() == 1);
    CHECK(list[0].minVersion == "3.2.0");
    CHECK(list[0].releaseNote == newNote);
    //包信息与发布状态不受影响
    CHECK(list[0].version == "3.3.0");
    CHECK(list[0].packageMd5 == "9f2c8ab1d4e5f60718293a4b5c6d7e8f");
    CHECK(list[0].packageSize == 3221225472LL);
    CHECK(list[0].enabled == 1);
    CHECK(list[0].publishTime == CVM_BASE_TIME);

    //最低版本与说明都允许清空：最低版本为空表示不另设门槛，与新建时留空的含义一致
    REQUIRE(mgr.updateVersion(id, "", "", CLIENT_VERSION_FORCED_KEEP, err));
    list.clear();
    REQUIRE(mgr.listVersions(platform_pc, list));
    CHECK(list[0].minVersion.empty());
    CHECK(list[0].releaseNote.empty());

    //记录不存在
    CHECK_FALSE(mgr.updateVersion(999999, "3.0.0", "x", CLIENT_VERSION_FORCED_KEEP, err));
    CHECK(err == error_code_not_found);
}

TEST_CASE("CVM13 编辑时改强制升级：改成 1、改回 0 都能读回，不带该字段时保持原值")
{
    resetCvmDb();
    ClientVersionManager& mgr = Singleton<ClientVersionManager>::Instance();

    int64_t id = 0;
    int err = -1;
    REQUIRE(mgr.createVersion(makeRecord("3.3.0", 1), CVM_BASE_TIME, id, err));

    std::vector<ClientVersionRecord> list;
    REQUIRE(mgr.listVersions(platform_pc, list));
    REQUIRE(list.size() == 1);
    CHECK(list[0].forced == 0);

    //改成强制升级
    REQUIRE(mgr.updateVersion(id, "3.1.0", "说明", 1, err));
    CHECK(err == error_code_ok);
    list.clear();
    REQUIRE(mgr.listVersions(platform_pc, list));
    CHECK(list[0].forced == 1);

    //不带 forced（老调用方）：其余字段照改，强制升级保持为 1
    REQUIRE(mgr.updateVersion(id, "3.0.0", "说明二", CLIENT_VERSION_FORCED_KEEP, err));
    list.clear();
    REQUIRE(mgr.listVersions(platform_pc, list));
    CHECK(list[0].forced == 1);
    CHECK(list[0].minVersion == "3.0.0");
    CHECK(list[0].releaseNote == "说明二");

    //改回不强制
    REQUIRE(mgr.updateVersion(id, "3.0.0", "说明二", 0, err));
    list.clear();
    REQUIRE(mgr.listVersions(platform_pc, list));
    CHECK(list[0].forced == 0);

    //下发给客户端的最新版本信息同样反映改动
    REQUIRE(mgr.updateVersion(id, "3.0.0", "说明二", 1, err));
    ClientVersionInfo info;
    REQUIRE(mgr.getLatestVersion(platform_pc, info));
    CHECK(info.forced == 1);
}
