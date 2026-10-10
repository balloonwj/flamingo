/**
 *  按企业的功能开关单元测试（Suite FSW），test_featureswitch.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 3 期第 1 步（5.8 的取值规则与下发形态）：
 *    - FSW1 取值规则：企业 id 为 0 恒为关闭；有记录按记录（"1" 开启、其余关闭）；没有记录取内置默认值；
 *    - FSW2 设置项校验与 features 对象的 JSON：只认 fileshare.enabled，值只能是 "0" / "1"；
 *    - FSW3 内存表：未设置的企业默认开启、是默认值；设置关闭后只影响这一个企业；删掉记录恢复默认；
 *           不认识的功能一律关闭；
 *    - FSW4 读写数据库：setSetting 写库并同步内存、给出写之前与之后是否开启；loadFromDb 重新载入后结果一致；
 *           非法参数不写库；
 *    - FSW5 登录应答：带 features 对象（与 1013 的包体同构）；不给开关时不写这个字段。
 *  覆盖 docs/P2P通信方案.md 第 2 期 2d（9.4 节的 p2p.enabled）：
 *    - FSW6 P2P 开关的内存表：p2p.enabled 只认 "0" / "1"；未设置时默认开启；与文件共享开关互不影响；
 *           features 对象与读设置接口同时带上两项；
 *    - FSW7 P2P 开关读写数据库：写之前与之后是否开启；重新载入后 P2P 关闭、文件共享不变；
 *    - FSW8 设置项对应的功能名（后台改设置时按它分流）与管理员操作日志的说明文字。
 *  FSW1~3、FSW5、FSW6、FSW8 是纯逻辑；FSW4、FSW7 用 tests/test_dbbackend.h 的测试库（默认 SQLite 临时文件）。
 *  覆盖 2026-10-04 加入的「允许用户自助注册」开关（register.enabled，只在服务端注册流程里检查）：
 *    - RSW1 设置项校验、未设置时默认开启、读设置接口带上它、功能名与操作日志文字，以及它不下发给客户端；
 *    - RSW5 无论开关打开还是关闭，登录应答与 1013 的 features 对象里都没有它。
 *  RSW2、RSW3 在 test_registerservice.cpp（注册流程按开关拒绝），RSW4 在 test_employeegender.cpp（管理台新建员工不受影响）。
 *  覆盖 2026-10-06 加入的「语音字幕」开关（caption.enabled，docs/语音字幕方案.md 第 2 节第 8 条，只在客户端使用）：
 *    - CAPSW1 键与功能名、默认开启、按企业关闭互不影响、下发给客户端、操作日志文字。
 *
 *  典型用法：Tests.exe --test-case="FSW*"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../jsoncpp1.9.0/json.h"
#include "../chatserversrc/FeatureSwitchManager.h"
#include "../chatserversrc/LoginProtocol.h"
#include "../base/Singleton.h"

namespace {

//本套用例专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kFswDbPath = "flamingo_sqlite_featureswitch_test.db";
const char* const kFswMysqlDb = "flamingo_test_fsw";

//两个企业与操作的管理员
const int64_t kEnterpriseA = 7;
const int64_t kEnterpriseB = 8;
const int64_t kAdminId = 1;

//写设置的时间
const int64_t kNowMs = 1700000000000LL;

//解析一段 JSON
bool ParseJson(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.data(), text.data() + text.size(), &out, &errs);
}

//某企业的某个设置项；没有这一项时返回 m_key 为空的默认对象
FeatureSettingView SettingOf(const FeatureSwitchManager& mgr, int64_t enterpriseId, const std::string& key)
{
    const std::vector<FeatureSettingView> all = mgr.getSettings(enterpriseId);
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_key == key)
            return all[i];
    }
    return FeatureSettingView();
}

//某企业的 fileshare.enabled 设置项
FeatureSettingView FileShareSetting(const FeatureSwitchManager& mgr, int64_t enterpriseId)
{
    return SettingOf(mgr, enterpriseId, kSettingFileShareEnabled);
}

} // anonymous namespace

TEST_CASE("FSW1 取值规则")
{
    //不属于任何企业：无论有没有记录、默认值是什么都关闭
    CHECK_FALSE(FeatureSwitchManager::resolveEnabled(0, false, "", true));
    CHECK_FALSE(FeatureSwitchManager::resolveEnabled(0, true, "1", true));
    //有记录按记录
    CHECK(FeatureSwitchManager::resolveEnabled(kEnterpriseA, true, "1", false));
    CHECK_FALSE(FeatureSwitchManager::resolveEnabled(kEnterpriseA, true, "0", true));
    CHECK_FALSE(FeatureSwitchManager::resolveEnabled(kEnterpriseA, true, "yes", true));
    //没有记录取内置默认值
    CHECK(FeatureSwitchManager::resolveEnabled(kEnterpriseA, false, "", true));
    CHECK_FALSE(FeatureSwitchManager::resolveEnabled(kEnterpriseA, false, "", false));
}

TEST_CASE("FSW2 设置项校验与 features 对象")
{
    CHECK(FeatureSwitchManager::isValidSetting(kSettingFileShareEnabled, "1"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingFileShareEnabled, "0"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingFileShareEnabled, "2"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingFileShareEnabled, ""));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting("fileshare.unknown", "1"));

    std::map<std::string, int> features;
    features[kFeatureFileShare] = 1;
    CHECK(FeatureSwitchManager::buildFeaturesJson(features) == "{\"fileshare\":1}");
    CHECK(FeatureSwitchManager::buildFeaturesJson(std::map<std::string, int>()) == "{}");
}

TEST_CASE("FSW3 内存表的取值")
{
    FeatureSwitchManager mgr;
    //未设置：默认开启、是默认值
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureFileShare));
    CHECK(mgr.featuresFor(kEnterpriseA)[kFeatureFileShare] == 1);
    FeatureSettingView view = FileShareSetting(mgr, kEnterpriseA);
    CHECK(view.m_isDefault);
    CHECK(view.m_value == "1");
    //不属于任何企业：关闭
    CHECK_FALSE(mgr.isEnabled(0, kFeatureFileShare));
    CHECK(mgr.featuresFor(0)[kFeatureFileShare] == 0);

    //A 关闭：只影响 A
    mgr.setMemoryValue(kEnterpriseA, kSettingFileShareEnabled, "0");
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, kFeatureFileShare));
    CHECK(mgr.isEnabled(kEnterpriseB, kFeatureFileShare));
    view = FileShareSetting(mgr, kEnterpriseA);
    CHECK_FALSE(view.m_isDefault);
    CHECK(view.m_value == "0");
    //删掉记录：恢复默认
    mgr.setMemoryValue(kEnterpriseA, kSettingFileShareEnabled, "");
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureFileShare));
    CHECK(FileShareSetting(mgr, kEnterpriseA).m_isDefault);

    //不认识的功能一律关闭
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, "unknown"));
}

TEST_CASE("FSW4 读写数据库")
{
    testdb::resetDb(kFswDbPath, kFswMysqlDb);
    FeatureSwitchManager mgr;
    REQUIRE(mgr.loadFromDb());
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureFileShare));

    //关闭 A：写之前开启、之后关闭；内存同步
    bool before = false;
    bool after = true;
    REQUIRE(mgr.setSetting(kEnterpriseA, kSettingFileShareEnabled, "0", kAdminId, kNowMs, before, after));
    CHECK(before);
    CHECK_FALSE(after);
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, kFeatureFileShare));
    //再写一次同样的值：前后都关闭
    REQUIRE(mgr.setSetting(kEnterpriseA, kSettingFileShareEnabled, "0", kAdminId, kNowMs, before, after));
    CHECK_FALSE(before);
    CHECK_FALSE(after);

    //另一个实例从库里载入：A 关闭、B 默认开启
    FeatureSwitchManager reloaded;
    REQUIRE(reloaded.loadFromDb());
    CHECK_FALSE(reloaded.isEnabled(kEnterpriseA, kFeatureFileShare));
    CHECK(reloaded.isEnabled(kEnterpriseB, kFeatureFileShare));
    CHECK_FALSE(FileShareSetting(reloaded, kEnterpriseA).m_isDefault);

    //重新打开 A：写之前关闭、之后开启
    REQUIRE(mgr.setSetting(kEnterpriseA, kSettingFileShareEnabled, "1", kAdminId, kNowMs, before, after));
    CHECK_FALSE(before);
    CHECK(after);

    //非法参数：不写库、内存不变
    CHECK_FALSE(mgr.setSetting(0, kSettingFileShareEnabled, "0", kAdminId, kNowMs, before, after));
    CHECK_FALSE(mgr.setSetting(kEnterpriseB, kSettingFileShareEnabled, "x", kAdminId, kNowMs, before, after));
    CHECK_FALSE(mgr.setSetting(kEnterpriseB, "other.key", "0", kAdminId, kNowMs, before, after));
    CHECK(mgr.isEnabled(kEnterpriseB, kFeatureFileShare));
    testdb::cleanupDb(kFswDbPath);
}

TEST_CASE("FSW5 登录应答带 features 对象")
{
    LoginProfile p;
    p.enterpriseid = kEnterpriseA;
    std::map<std::string, int> features;
    features[kFeatureFileShare] = 0;
    Json::Value root;
    REQUIRE(ParseJson(LoginProtocol::buildLoginSuccessResponse(8, p, kNowMs, features), root));
    REQUIRE(root.isMember("features"));
    CHECK(root["features"]["fileshare"].asInt() == 0);
    //与 1013 的包体同构
    Json::Value notify;
    REQUIRE(ParseJson(FeatureSwitchManager::buildFeaturesJson(features), notify));
    CHECK(notify == root["features"]);

    //不给开关：不写这个字段
    Json::Value plain;
    REQUIRE(ParseJson(LoginProtocol::buildLoginSuccessResponse(8, p, kNowMs), plain));
    CHECK_FALSE(plain.isMember("features"));
}

TEST_CASE("FSW6 P2P 开关的内存表")
{
    //设置项校验：只认 "0" / "1"
    CHECK(FeatureSwitchManager::isValidSetting(kSettingP2PEnabled, "1"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingP2PEnabled, "0"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingP2PEnabled, "2"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingP2PEnabled, ""));

    FeatureSwitchManager mgr;
    //未设置：默认开启；不属于任何企业：关闭
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureP2P));
    CHECK_FALSE(mgr.isEnabled(0, kFeatureP2P));

    //A 关闭 P2P：A 的文件共享不变，B 的 P2P 不变
    mgr.setMemoryValue(kEnterpriseA, kSettingP2PEnabled, "0");
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, kFeatureP2P));
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureFileShare));
    CHECK(mgr.isEnabled(kEnterpriseB, kFeatureP2P));
    //B 关闭文件共享：B 的 P2P 不变；随后删掉记录恢复默认
    mgr.setMemoryValue(kEnterpriseB, kSettingFileShareEnabled, "0");
    CHECK(mgr.isEnabled(kEnterpriseB, kFeatureP2P));
    CHECK_FALSE(mgr.isEnabled(kEnterpriseB, kFeatureFileShare));
    mgr.setMemoryValue(kEnterpriseB, kSettingFileShareEnabled, "");

    //features 对象带全部认识的功能（登录应答与 1013 的包体）。2026-10-04 起多了「邀请入群需被邀请人同意」
    //（默认关闭，docs/邀请入群方案.md §3.5）与「群语音 / 群视频通话」（默认开启，docs/群音视频通话方案.md），
    //2026-10-06 起又多了「语音字幕」（默认开启，docs/语音字幕方案.md）与「云端录制」（默认关闭，屏幕共享与录制方案 12.6.5），
    //2026-10-07 起又多了「单聊密聊」（docs/密聊方案.md）与「帖子群」（docs/帖子群方案.md），都默认开启；
    //2026-10-08 起又多了机器人的四项（docs/机器人方案.md §4.1）：机器人总开关、允许群主添加群机器人、允许员工配置个人机器人
    //（三项默认开启）与允许群主给群机器人配置外部服务（默认关闭），共十二项
    std::map<std::string, int> featuresA = mgr.featuresFor(kEnterpriseA);
    CHECK(featuresA.size() == 12);
    CHECK(featuresA[kFeatureCloudRecord] == 0);
    CHECK(featuresA[kFeatureCaption] == 1);
    CHECK(featuresA[kFeatureGroupCall] == 1);
    CHECK(featuresA[kFeatureFileShare] == 1);
    CHECK(featuresA[kFeatureP2P] == 0);
    CHECK(featuresA[kFeatureInviteConfirm] == 0);
    CHECK(FeatureSwitchManager::buildFeaturesJson(mgr.featuresFor(kEnterpriseB))
          == "{\"bot\":1,\"botgroup\":1,\"botgroupexternal\":0,\"botpersonal\":1,\"caption\":1,\"cloudrecord\":0,\"fileshare\":1,\"groupcall\":1,\"inviteconfirm\":0,\"p2p\":1,\"secretchat\":1,\"threadgroup\":1}");

    //读设置接口：九项（2026-10-04 起多了只在服务端使用的「允许用户自助注册」与群通话，2026-10-06 起多了语音字幕、云端录制，
    //以及云端录像配额与保留天数两个数值项；2026-10-07 起多了单聊密聊与帖子群；2026-10-08 起多了机器人的四项，共十五项），
    //文件共享为默认值，P2P 不是默认值
    CHECK(mgr.getSettings(kEnterpriseA).size() == 15);
    FeatureSettingView fileShare = FileShareSetting(mgr, kEnterpriseA);
    CHECK(fileShare.m_isDefault);
    CHECK(fileShare.m_value == "1");
    FeatureSettingView p2p = SettingOf(mgr, kEnterpriseA, kSettingP2PEnabled);
    CHECK(p2p.m_key == kSettingP2PEnabled);
    CHECK_FALSE(p2p.m_isDefault);
    CHECK(p2p.m_value == "0");
}

TEST_CASE("FSW7 P2P 开关读写数据库")
{
    testdb::resetDb(kFswDbPath, kFswMysqlDb);
    FeatureSwitchManager mgr;
    REQUIRE(mgr.loadFromDb());

    //关闭 A 的 P2P：写之前开启、之后关闭
    bool before = false;
    bool after = true;
    REQUIRE(mgr.setSetting(kEnterpriseA, kSettingP2PEnabled, "0", kAdminId, kNowMs, before, after));
    CHECK(before);
    CHECK_FALSE(after);

    //另一个实例从库里载入：A 的 P2P 关闭，文件共享仍是默认的开启
    FeatureSwitchManager reloaded;
    REQUIRE(reloaded.loadFromDb());
    CHECK_FALSE(reloaded.isEnabled(kEnterpriseA, kFeatureP2P));
    CHECK(reloaded.isEnabled(kEnterpriseA, kFeatureFileShare));
    CHECK(FileShareSetting(reloaded, kEnterpriseA).m_isDefault);
    CHECK_FALSE(SettingOf(reloaded, kEnterpriseA, kSettingP2PEnabled).m_isDefault);

    //重新打开：写之前关闭、之后开启
    REQUIRE(mgr.setSetting(kEnterpriseA, kSettingP2PEnabled, "1", kAdminId, kNowMs, before, after));
    CHECK_FALSE(before);
    CHECK(after);
    testdb::cleanupDb(kFswDbPath);
}

TEST_CASE("FSW8 设置项对应的功能名与操作日志文字")
{
    //后台改设置时按功能名分流：文件共享要结束访问会话并推新状态，P2P 只推 1013
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingFileShareEnabled) == kFeatureFileShare);
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingP2PEnabled) == kFeatureP2P);
    CHECK(FeatureSwitchManager::featureOfSetting("other.key").empty());

    //管理员操作日志的说明文字
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingFileShareEnabled, true) == u8"打开文件共享");
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingFileShareEnabled, false) == u8"关闭文件共享");
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingP2PEnabled, true) == u8"打开客户端点对点直连");
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingP2PEnabled, false) == u8"关闭客户端点对点直连");
    CHECK(FeatureSwitchManager::describeSettingChange("other.key", true).empty());
}

TEST_CASE("RSW1 允许用户自助注册开关：校验、默认开启、读设置接口与操作日志文字、不下发给客户端")
{
    //设置项校验：只认 "0" / "1"
    CHECK(std::string(kSettingRegisterEnabled) == "register.enabled");
    CHECK(FeatureSwitchManager::isValidSetting(kSettingRegisterEnabled, "1"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingRegisterEnabled, "0"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingRegisterEnabled, "2"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingRegisterEnabled, ""));

    FeatureSwitchManager mgr;
    //未设置：默认开启、读设置接口里是默认值；不属于任何企业：关闭
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureRegister));
    CHECK_FALSE(mgr.isEnabled(0, kFeatureRegister));
    FeatureSettingView view = SettingOf(mgr, kEnterpriseA, kSettingRegisterEnabled);
    CHECK(view.m_key == kSettingRegisterEnabled);
    CHECK(view.m_isDefault);
    CHECK(view.m_value == "1");

    //A 关闭：只影响 A 的这一项
    mgr.setMemoryValue(kEnterpriseA, kSettingRegisterEnabled, "0");
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, kFeatureRegister));
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureFileShare));
    CHECK(mgr.isEnabled(kEnterpriseB, kFeatureRegister));
    view = SettingOf(mgr, kEnterpriseA, kSettingRegisterEnabled);
    CHECK_FALSE(view.m_isDefault);
    CHECK(view.m_value == "0");

    //功能名与操作日志文字
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingRegisterEnabled) == kFeatureRegister);
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingRegisterEnabled, true) == u8"打开允许用户自助注册");
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingRegisterEnabled, false) == u8"关闭允许用户自助注册");

    //是否下发给客户端：只有它不下发；不认识的设置项也不下发
    CHECK_FALSE(FeatureSwitchManager::isSentToClient(kSettingRegisterEnabled));
    CHECK(FeatureSwitchManager::isSentToClient(kSettingFileShareEnabled));
    CHECK(FeatureSwitchManager::isSentToClient(kSettingP2PEnabled));
    CHECK(FeatureSwitchManager::isSentToClient(kSettingInviteConfirm));
    CHECK_FALSE(FeatureSwitchManager::isSentToClient("other.key"));
}

TEST_CASE("RSW5 允许用户自助注册开关不进 features 对象")
{
    FeatureSwitchManager mgr;
    //未设置（默认开启）
    std::map<std::string, int> features = mgr.featuresFor(kEnterpriseA);
    CHECK(features.find(kFeatureRegister) == features.end());
    CHECK(features.size() == 12);
    const std::string before = FeatureSwitchManager::buildFeaturesJson(features);
    CHECK(before == "{\"bot\":1,\"botgroup\":1,\"botgroupexternal\":0,\"botpersonal\":1,\"caption\":1,\"cloudrecord\":0,\"fileshare\":1,\"groupcall\":1,\"inviteconfirm\":0,\"p2p\":1,\"secretchat\":1,\"threadgroup\":1}");

    //关闭后 features 对象不变：客户端看不到这一项，也就不必为它推 1013
    mgr.setMemoryValue(kEnterpriseA, kSettingRegisterEnabled, "0");
    features = mgr.featuresFor(kEnterpriseA);
    CHECK(features.find(kFeatureRegister) == features.end());
    CHECK(FeatureSwitchManager::buildFeaturesJson(features) == before);
}

TEST_CASE("CAPSW1 语音字幕开关：键与功能名、默认开启、按企业关闭、下发给客户端、操作日志文字")
{
    //键与功能名（与客户端 LoginProtocolClient 的 kFeatureCaption、flamingoAdmin 的 kSettingCaptionEnabled 一致）
    CHECK(std::string(kSettingCaptionEnabled) == "caption.enabled");
    CHECK(std::string(kFeatureCaption) == "caption");
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingCaptionEnabled) == kFeatureCaption);
    CHECK(FeatureSwitchManager::isValidSetting(kSettingCaptionEnabled, "0"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingCaptionEnabled, "1"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingCaptionEnabled, "2"));
    //下发给客户端：字幕全在客户端本机完成，开关只有客户端用得上
    CHECK(FeatureSwitchManager::isSentToClient(kSettingCaptionEnabled));
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingCaptionEnabled, false) == u8"关闭语音字幕");
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingCaptionEnabled, true) == u8"打开语音字幕");

    FeatureSwitchManager mgr;
    //未设置：默认开启，读设置接口里写明是默认值；不属于任何企业：关闭
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureCaption));
    CHECK_FALSE(mgr.isEnabled(0, kFeatureCaption));
    FeatureSettingView view = SettingOf(mgr, kEnterpriseA, kSettingCaptionEnabled);
    CHECK(view.m_key == kSettingCaptionEnabled);
    CHECK(view.m_isDefault);
    CHECK(view.m_value == "1");

    //A 关闭：A 的 features 对象里为 0，B 不受影响，A 的其它功能不变
    mgr.setMemoryValue(kEnterpriseA, kSettingCaptionEnabled, "0");
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, kFeatureCaption));
    CHECK(mgr.featuresFor(kEnterpriseA)[kFeatureCaption] == 0);
    CHECK(mgr.featuresFor(kEnterpriseA)[kFeatureGroupCall] == 1);
    CHECK(mgr.featuresFor(kEnterpriseB)[kFeatureCaption] == 1);
    view = SettingOf(mgr, kEnterpriseA, kSettingCaptionEnabled);
    CHECK_FALSE(view.m_isDefault);
    CHECK(view.m_value == "0");
}

TEST_CASE("CRSW1 云端录制开关与两个数值项：默认值、校验、按企业取值、读设置接口、操作日志文字")
{
    //开关：键与功能名、默认关闭（第 52 条）、下发给客户端
    CHECK(std::string(kSettingCloudRecordEnabled) == "cloudrecord.enabled");
    CHECK(std::string(kFeatureCloudRecord) == "cloudrecord");
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingCloudRecordEnabled) == kFeatureCloudRecord);
    CHECK(FeatureSwitchManager::isSentToClient(kSettingCloudRecordEnabled));
    CHECK_FALSE(FeatureSwitchManager::isNumberSetting(kSettingCloudRecordEnabled));
    CHECK(FeatureSwitchManager::describeSettingWrite(kSettingCloudRecordEnabled, "1", true) == u8"打开云端录制");

    //数值项：键名、范围校验（不带符号的十进制整数）、不下发、不对应任何功能
    CHECK(std::string(kSettingCloudRecordQuotaGb) == "cloudrecord.quotagb");
    CHECK(std::string(kSettingCloudRecordRetainDays) == "cloudrecord.retaindays");
    CHECK(FeatureSwitchManager::isNumberSetting(kSettingCloudRecordQuotaGb));
    CHECK(FeatureSwitchManager::isNumberSetting(kSettingCloudRecordRetainDays));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, "0"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, "100"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, "1000000"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, "1000001"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, "-1"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, ""));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, "1.5"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingCloudRecordQuotaGb, "99999999999999999999"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingCloudRecordRetainDays, "0"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingCloudRecordRetainDays, "3650"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingCloudRecordRetainDays, "3651"));
    CHECK_FALSE(FeatureSwitchManager::isSentToClient(kSettingCloudRecordQuotaGb));
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingCloudRecordQuotaGb).empty());
    CHECK(FeatureSwitchManager::describeSettingWrite(kSettingCloudRecordQuotaGb, "100", false) == u8"云端录像配额设为 100 GB");
    CHECK(FeatureSwitchManager::describeSettingWrite(kSettingCloudRecordRetainDays, "0", false) == u8"云端录像保留天数设为 0 天");
    CHECK(FeatureSwitchManager::describeSettingWrite("other.key", "1", true).empty());

    FeatureSwitchManager mgr;
    //未设置：开关关闭，配额 50 GB，保留 30 天；读设置接口里都写明是默认值
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, kFeatureCloudRecord));
    CHECK(mgr.featuresFor(kEnterpriseA)[kFeatureCloudRecord] == 0);
    CHECK(mgr.getNumber(kEnterpriseA, kSettingCloudRecordQuotaGb) == 50);
    CHECK(mgr.getNumber(kEnterpriseA, kSettingCloudRecordRetainDays) == 30);
    CHECK(mgr.getNumber(kEnterpriseA, "other.key") == 0);
    FeatureSettingView quota = SettingOf(mgr, kEnterpriseA, kSettingCloudRecordQuotaGb);
    CHECK(quota.m_key == kSettingCloudRecordQuotaGb);
    CHECK(quota.m_isDefault);
    CHECK(quota.m_value == "50");

    //A 打开开关、配额改为 200、保留改为 0（永久）：只影响 A
    mgr.setMemoryValue(kEnterpriseA, kSettingCloudRecordEnabled, "1");
    mgr.setMemoryValue(kEnterpriseA, kSettingCloudRecordQuotaGb, "200");
    mgr.setMemoryValue(kEnterpriseA, kSettingCloudRecordRetainDays, "0");
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureCloudRecord));
    CHECK(mgr.getNumber(kEnterpriseA, kSettingCloudRecordQuotaGb) == 200);
    CHECK(mgr.getNumber(kEnterpriseA, kSettingCloudRecordRetainDays) == 0);
    CHECK_FALSE(mgr.isEnabled(kEnterpriseB, kFeatureCloudRecord));
    CHECK(mgr.getNumber(kEnterpriseB, kSettingCloudRecordQuotaGb) == 50);
    quota = SettingOf(mgr, kEnterpriseA, kSettingCloudRecordQuotaGb);
    CHECK_FALSE(quota.m_isDefault);
    CHECK(quota.m_value == "200");

    //记录被改坏（不是数字、超出范围）：按默认值处理，读设置接口里也显示默认值
    mgr.setMemoryValue(kEnterpriseA, kSettingCloudRecordQuotaGb, "abc");
    CHECK(mgr.getNumber(kEnterpriseA, kSettingCloudRecordQuotaGb) == 50);
    CHECK(SettingOf(mgr, kEnterpriseA, kSettingCloudRecordQuotaGb).m_isDefault);
    mgr.setMemoryValue(kEnterpriseA, kSettingCloudRecordRetainDays, "99999");
    CHECK(mgr.getNumber(kEnterpriseA, kSettingCloudRecordRetainDays) == 30);
}

TEST_CASE("SCSW1 单聊密聊与帖子群开关：键与功能名、默认开启、按企业关闭、下发给客户端、操作日志文字")
{
    //键与功能名（与客户端 LoginProtocolClient、flamingoAdmin 的设置键一致；docs/密聊方案.md 4.9、docs/帖子群方案.md 4.7）
    CHECK(std::string(kSettingSecretChatEnabled) == "secretchat.enabled");
    CHECK(std::string(kFeatureSecretChat) == "secretchat");
    CHECK(std::string(kSettingThreadGroupEnabled) == "threadgroup.enabled");
    CHECK(std::string(kFeatureThreadGroup) == "threadgroup");
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingSecretChatEnabled) == kFeatureSecretChat);
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingThreadGroupEnabled) == kFeatureThreadGroup);
    CHECK(FeatureSwitchManager::isValidSetting(kSettingSecretChatEnabled, "0"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingThreadGroupEnabled, "1"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingThreadGroupEnabled, "2"));
    //两项都要下发：客户端据此显示或隐藏「密聊」按钮与帖子群
    CHECK(FeatureSwitchManager::isSentToClient(kSettingSecretChatEnabled));
    CHECK(FeatureSwitchManager::isSentToClient(kSettingThreadGroupEnabled));
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingSecretChatEnabled, false) == u8"关闭单聊密聊");
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingThreadGroupEnabled, true) == u8"打开帖子群");

    FeatureSwitchManager mgr;
    //未设置：默认开启；不属于任何企业：关闭
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureSecretChat));
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureThreadGroup));
    CHECK_FALSE(mgr.isEnabled(0, kFeatureSecretChat));
    CHECK_FALSE(mgr.isEnabled(0, kFeatureThreadGroup));
    FeatureSettingView view = SettingOf(mgr, kEnterpriseA, kSettingSecretChatEnabled);
    CHECK(view.m_isDefault);
    CHECK(view.m_value == "1");

    //A 关闭密聊：只影响 A 的密聊，帖子群与 B 都不变
    mgr.setMemoryValue(kEnterpriseA, kSettingSecretChatEnabled, "0");
    CHECK_FALSE(mgr.isEnabled(kEnterpriseA, kFeatureSecretChat));
    CHECK(mgr.isEnabled(kEnterpriseA, kFeatureThreadGroup));
    CHECK(mgr.isEnabled(kEnterpriseB, kFeatureSecretChat));
    CHECK(mgr.featuresFor(kEnterpriseA)[kFeatureSecretChat] == 0);
    CHECK(mgr.featuresFor(kEnterpriseA)[kFeatureThreadGroup] == 1);

    //B 关闭帖子群
    mgr.setMemoryValue(kEnterpriseB, kSettingThreadGroupEnabled, "0");
    CHECK_FALSE(mgr.isEnabled(kEnterpriseB, kFeatureThreadGroup));
    CHECK(mgr.featuresFor(kEnterpriseB)[kFeatureThreadGroup] == 0);
    view = SettingOf(mgr, kEnterpriseB, kSettingThreadGroupEnabled);
    CHECK_FALSE(view.m_isDefault);
    CHECK(view.m_value == "0");
}
