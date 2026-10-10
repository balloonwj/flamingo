/**
 *  「邀请入群需被邀请人同意」开关与入群邀请卡片的单元测试（Suite GV），test_groupinvite.cpp
 *
 *  背景（docs/邀请入群方案.md 第 3 期）：开关开启时，被邀请人在与邀请人的单聊里收到一张入群邀请卡片，点「加入群聊」
 *  后才入群；邀请人是普通成员且群需审批时，点加入后转为加群申请（先发卡片，后审批）。
 *
 *    GV1  开关：group.inviteconfirm 默认关闭；开启后 isEnabled 为真；features 带 inviteconfirm；后台读设置多出这一项；
 *         操作日志文字
 *    GV2  卡片 content 的组装：邀请 id、群、邀请人、被邀请人、到期时间都在，id 写成字符串
 *    GV3  卡片显示状态的计算：待加入 / 已过期 / 已失效 / 被邀请人已在群里按已加入 / 落库的终态原样返回
 *    GV4  下发时合并：状态、成员数、群当前的名字与头像；群已解散时保留卡片里的原值
 *    GV5  t_group_invite 的读写：新增、按 id 读、记卡片 msgid、改状态
 *    GV6  1327 请求解析与应答组装、1328 推送组装
 *    GV7  点「加入群聊」的判定：314 / 313 / 305 的几种情况、直接入群、转为审批
 *    GV8  由卡片转来的加群申请记下邀请 id 与同步条数，读回一致
 *    GV9  卡片类型不允许客户端操作（发送、撤回、置顶、转发），会话摘要为「[入群邀请] 群名」
 *
 *  GV5、GV8 会打开一个临时数据库跑 SQL（默认 SQLite，设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL，见 test_dbbackend.h）。
 *
 *  用法：Tests.exe --test-case="GV*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/FeatureSwitchManager.h"
#include "../chatserversrc/GroupInviteProtocol.h"
#include "../chatserversrc/GroupInviteManager.h"
#include "../chatserversrc/GroupManager.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时数据库文件与 MySQL 测试库名
const char* const GV_DB_PATH  = "flamingo_sqlite_groupinvite_test.db";
const char* const GV_MYSQL_DB = "flamingo_test_gv";

//样例：企业、群、邀请人、被邀请人、旁人、邀请 id、卡片 msgid
const int64_t kEnterprise = 7;
const int64_t kGroupId = 268435463;
const int64_t kInviter = 5127;
const int64_t kInvitee = 5308;
const int64_t kOther = 5010;
const int64_t kInviteId = 31;
const int64_t kCardMsgId = 9007199254741011LL;

//样例时间：发出时间、到期时间（发出后 7 天）、到期前与到期后的某一刻
const int64_t kCreate = 1760000000000LL;
const int64_t kExpire = kCreate + kGroupInviteExpireMs;
const int64_t kBeforeExpire = kExpire - 1;
const int64_t kAfterExpire = kExpire + 1;

//样例：同步条数、群成员数、群头像类型
const int kHistoryLimit = 100;
const int kMemberCount = 32;
const int kFaceType = 3;

//把 JSON 文本解析成对象；失败时让用例直接失败
Json::Value parse(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    REQUIRE(reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs));
    REQUIRE(root.isObject());
    return root;
}

//一张样例邀请
GroupInviteRecord sampleInvite()
{
    GroupInviteRecord rec;
    rec.m_id           = kInviteId;
    rec.m_groupid      = kGroupId;
    rec.m_inviter      = kInviter;
    rec.m_invitee      = kInvitee;
    rec.m_historyLimit = kHistoryLimit;
    rec.m_state        = group_invite_pending;
    rec.m_msgid        = kCardMsgId;
    rec.m_createTime   = kCreate;
    rec.m_expireTime   = kExpire;
    return rec;
}

//样例卡片 content
std::string sampleCard()
{
    GroupInviteCardNames names;
    names.m_groupName   = u8"产品研发部周会群";
    names.m_facetype    = kFaceType;
    names.m_customface  = "face-1";
    names.m_inviterName = u8"张三";
    names.m_inviteeName = u8"李四";
    return GroupInviteProtocol::buildCardContent(sampleInvite(), names);
}

//一组「可以入群」的事实：邀请发给本人、待加入、未过期、群在、本人不在群里、邀请人在群里且是群主
GroupInviteAcceptFacts okFacts()
{
    GroupInviteAcceptFacts f;
    f.m_found = true;
    f.m_invite = sampleInvite();
    f.m_selfId = kInvitee;
    f.m_nowMs = kBeforeExpire;
    f.m_groupExists = true;
    f.m_selfIsMember = false;
    f.m_inviterIsMember = true;
    f.m_inviterRole = group_role_owner;
    f.m_memberInviteFree = false;
    return f;
}

} // namespace

TEST_CASE("GV1 「邀请入群需被邀请人同意」开关")
{
    FeatureSwitchManager mgr;
    //默认关闭；不属于任何企业也关闭
    CHECK_FALSE(mgr.isEnabled(kEnterprise, kFeatureInviteConfirm));
    CHECK_FALSE(mgr.isEnabled(0, kFeatureInviteConfirm));
    //设置项校验与对应的功能名
    CHECK(FeatureSwitchManager::isValidSetting(kSettingInviteConfirm, "1"));
    CHECK(FeatureSwitchManager::isValidSetting(kSettingInviteConfirm, "0"));
    CHECK_FALSE(FeatureSwitchManager::isValidSetting(kSettingInviteConfirm, "2"));
    CHECK(FeatureSwitchManager::featureOfSetting(kSettingInviteConfirm) == kFeatureInviteConfirm);
    CHECK(std::string(kSettingInviteConfirm) == "group.inviteconfirm");
    CHECK(std::string(kFeatureInviteConfirm) == "inviteconfirm");

    //开启后为真，features 对象与后台读设置都带这一项
    mgr.setMemoryValue(kEnterprise, kSettingInviteConfirm, "1");
    CHECK(mgr.isEnabled(kEnterprise, kFeatureInviteConfirm));
    std::map<std::string, int> features = mgr.featuresFor(kEnterprise);
    CHECK(features[kFeatureInviteConfirm] == 1);
    bool found = false;
    std::vector<FeatureSettingView> settings = mgr.getSettings(kEnterprise);
    for (size_t i = 0; i < settings.size(); ++i)
    {
        if (settings[i].m_key == kSettingInviteConfirm)
        {
            found = true;
            CHECK(settings[i].m_value == "1");
            CHECK_FALSE(settings[i].m_isDefault);
        }
    }
    CHECK(found);

    //操作日志文字
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingInviteConfirm, true) == u8"打开邀请入群需被邀请人同意");
    CHECK(FeatureSwitchManager::describeSettingChange(kSettingInviteConfirm, false) == u8"关闭邀请入群需被邀请人同意");
}

TEST_CASE("GV2 卡片 content 的组装")
{
    const Json::Value c = parse(sampleCard());
    //id 一律写成字符串
    CHECK(c["inviteid"].asString() == std::to_string(kInviteId));
    CHECK(c["groupid"].asString() == std::to_string(kGroupId));
    CHECK(c["inviterid"].asString() == std::to_string(kInviter));
    CHECK(c["inviteeid"].asString() == std::to_string(kInvitee));
    CHECK(c["groupname"].asString() == std::string(u8"产品研发部周会群"));
    CHECK(c["facetype"].asInt() == kFaceType);
    CHECK(c["customface"].asString() == std::string("face-1"));
    CHECK(c["inviter"].asString() == std::string(u8"张三"));
    CHECK(c["invitee"].asString() == std::string(u8"李四"));
    CHECK(c["expiretime"].asInt64() == kExpire);
    //落库的 content 不带状态：状态在下发时合并
    CHECK_FALSE(c.isMember("state"));
    CHECK(GroupInviteProtocol::parseCardInviteId(sampleCard()) == kInviteId);
    CHECK(GroupInviteProtocol::parseCardInviteId("not json") == 0);
}

TEST_CASE("GV3 卡片显示状态的计算")
{
    //待加入：一切正常
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_pending, kExpire, kBeforeExpire, true, true, false)
          == group_invite_pending);
    //待加入：超过到期时间
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_pending, kExpire, kAfterExpire, true, true, false)
          == group_invite_expired);
    //待加入：群已解散、邀请人已退群
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_pending, kExpire, kBeforeExpire, false, false, false)
          == group_invite_invalid);
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_pending, kExpire, kBeforeExpire, true, false, false)
          == group_invite_invalid);
    //待加入与等待审批：被邀请人已在群里，按已加入显示
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_pending, kExpire, kAfterExpire, true, true, true)
          == group_invite_joined);
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_approving, kExpire, kBeforeExpire, true, true, true)
          == group_invite_joined);
    //等待审批：还没入群时保持等待审批，过期也不影响
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_approving, kExpire, kAfterExpire, true, true, false)
          == group_invite_approving);
    //落库的终态原样返回
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_joined, kExpire, kAfterExpire, false, false, false)
          == group_invite_joined);
    CHECK(GroupInviteProtocol::computeDisplayState(group_invite_rejected, kExpire, kBeforeExpire, true, true, false)
          == group_invite_rejected);
}

TEST_CASE("GV4 下发时合并状态与群的当前资料")
{
    //群还在：带状态与成员数，群名与头像用群的当前资料覆盖
    Json::Value c = parse(GroupInviteProtocol::augmentCardContent(sampleCard(), group_invite_pending, true,
                                                                  u8"新群名", 0, "face-2", kMemberCount));
    CHECK(c["state"].asInt() == group_invite_pending);
    CHECK(c["membercount"].asInt() == kMemberCount);
    CHECK(c["groupname"].asString() == std::string(u8"新群名"));
    CHECK(c["facetype"].asInt() == 0);
    CHECK(c["customface"].asString() == std::string("face-2"));
    CHECK(c["inviteid"].asString() == std::to_string(kInviteId));

    //群已解散：保留卡片里的原值，不带成员数
    c = parse(GroupInviteProtocol::augmentCardContent(sampleCard(), group_invite_invalid, false, "", 0, "", 0));
    CHECK(c["state"].asInt() == group_invite_invalid);
    CHECK_FALSE(c.isMember("membercount"));
    CHECK(c["groupname"].asString() == std::string(u8"产品研发部周会群"));
    CHECK(c["facetype"].asInt() == kFaceType);

    //content 解析不了：原样返回
    CHECK(GroupInviteProtocol::augmentCardContent("oops", group_invite_pending, true, "x", 0, "", 1) == "oops");
}

TEST_CASE("GV5 t_group_invite 的读写")
{
    testdb::resetDb(GV_DB_PATH, GV_MYSQL_DB);
    GroupInviteManager& mgr = Singleton<GroupInviteManager>::Instance();

    GroupInviteRecord rec = sampleInvite();
    rec.m_id = 0;
    rec.m_msgid = 0;
    const int64_t id = mgr.addInvite(rec);
    REQUIRE(id > 0);

    GroupInviteRecord out;
    REQUIRE(mgr.getInvite(id, out));
    CHECK(out.m_id == id);
    CHECK(out.m_groupid == kGroupId);
    CHECK(out.m_inviter == kInviter);
    CHECK(out.m_invitee == kInvitee);
    CHECK(out.m_historyLimit == kHistoryLimit);
    CHECK(out.m_state == group_invite_pending);
    CHECK(out.m_msgid == 0);
    CHECK(out.m_createTime == kCreate);
    CHECK(out.m_expireTime == kExpire);

    //卡片落库后记下 msgid；状态变化记下时间
    REQUIRE(mgr.setMsgId(id, kCardMsgId));
    REQUIRE(mgr.updateState(id, group_invite_approving, kBeforeExpire));
    REQUIRE(mgr.getInvite(id, out));
    CHECK(out.m_msgid == kCardMsgId);
    CHECK(out.m_state == group_invite_approving);
    CHECK(out.m_handleTime == kBeforeExpire);

    //不存在的 id
    CHECK_FALSE(mgr.getInvite(id + 1000, out));

    //「不同步」的条数为 0、「全部」为 -1，都能原样读回
    rec.m_historyLimit = kInviteHistoryLimitAll;
    const int64_t allId = mgr.addInvite(rec);
    REQUIRE(mgr.getInvite(allId, out));
    CHECK(out.m_historyLimit == kInviteHistoryLimitAll);

    testdb::cleanupDb(GV_DB_PATH);
}

TEST_CASE("GV6 1327 / 1328 的报文")
{
    int64_t inviteid = 0;
    CHECK(GroupInviteProtocol::parseAcceptRequest("{\"inviteid\":\"31\"}", inviteid));
    CHECK(inviteid == kInviteId);
    CHECK_FALSE(GroupInviteProtocol::parseAcceptRequest("{\"inviteid\":31}", inviteid));
    CHECK_FALSE(GroupInviteProtocol::parseAcceptRequest("{}", inviteid));
    CHECK_FALSE(GroupInviteProtocol::parseAcceptRequest("{\"inviteid\":\"0\"}", inviteid));

    //应答：成功时带 joined；失败时不带
    Json::Value ok = parse(GroupInviteProtocol::buildAcceptResponse(0, "ok", kInviteId, kGroupId, true));
    CHECK(ok["code"].asInt() == 0);
    CHECK(ok["groupid"].asString() == std::to_string(kGroupId));
    CHECK(ok["joined"].asInt() == 1);
    CHECK(ok["inviteid"].asString() == std::to_string(kInviteId));
    Json::Value pending = parse(GroupInviteProtocol::buildAcceptResponse(0, "ok", kInviteId, kGroupId, false));
    CHECK(pending["joined"].asInt() == 0);
    Json::Value fail = parse(GroupInviteProtocol::buildAcceptResponse(error_code_group_invite_expired, "x", kInviteId, 0, false));
    CHECK(fail["code"].asInt() == error_code_group_invite_expired);
    CHECK_FALSE(fail.isMember("joined"));
    CHECK_FALSE(fail.isMember("groupid"));
    //失败时也回带邀请 id，客户端据此恢复那张卡片
    CHECK(fail["inviteid"].asString() == std::to_string(kInviteId));

    //推送
    Json::Value n = parse(GroupInviteProtocol::buildChangedNotify(sampleInvite(), group_invite_joined));
    CHECK(n["inviteid"].asString() == std::to_string(kInviteId));
    CHECK(n["msgid"].asString() == std::to_string(kCardMsgId));
    CHECK(n["inviterid"].asString() == std::to_string(kInviter));
    CHECK(n["inviteeid"].asString() == std::to_string(kInvitee));
    CHECK(n["state"].asInt() == group_invite_joined);
}

TEST_CASE("GV7 点「加入群聊」的判定")
{
    //邀请人是群主：直接入群
    GroupInviteAcceptDecision d = GroupInviteProtocol::decideAccept(okFacts());
    CHECK(d.m_action == group_invite_accept_join);
    CHECK(d.m_errorCode == 0);

    //邀请人是管理员：直接入群
    GroupInviteAcceptFacts f = okFacts();
    f.m_inviterRole = group_role_admin;
    CHECK(GroupInviteProtocol::decideAccept(f).m_action == group_invite_accept_join);

    //邀请人是普通成员：群允许直接拉人时入群，否则转为审批
    f = okFacts();
    f.m_inviterRole = group_role_member;
    f.m_memberInviteFree = true;
    CHECK(GroupInviteProtocol::decideAccept(f).m_action == group_invite_accept_join);
    f.m_memberInviteFree = false;
    CHECK(GroupInviteProtocol::decideAccept(f).m_action == group_invite_accept_approval);

    //314：邀请不存在、不属于本人、已处理过、群已解散、邀请人已退群
    f = okFacts();
    f.m_found = false;
    d = GroupInviteProtocol::decideAccept(f);
    CHECK(d.m_action == group_invite_accept_reject);
    CHECK(d.m_errorCode == error_code_group_invite_invalid);
    f = okFacts();
    f.m_selfId = kOther;
    CHECK(GroupInviteProtocol::decideAccept(f).m_errorCode == error_code_group_invite_invalid);
    f = okFacts();
    f.m_invite.m_state = group_invite_approving;
    CHECK(GroupInviteProtocol::decideAccept(f).m_errorCode == error_code_group_invite_invalid);
    f = okFacts();
    f.m_groupExists = false;
    CHECK(GroupInviteProtocol::decideAccept(f).m_errorCode == error_code_group_invite_invalid);
    f = okFacts();
    f.m_inviterIsMember = false;
    CHECK(GroupInviteProtocol::decideAccept(f).m_errorCode == error_code_group_invite_invalid);

    //305：本人已在群中（先于过期判断）
    f = okFacts();
    f.m_selfIsMember = true;
    f.m_nowMs = kAfterExpire;
    CHECK(GroupInviteProtocol::decideAccept(f).m_errorCode == error_code_already_in_group);

    //313：超过有效期
    f = okFacts();
    f.m_nowMs = kAfterExpire;
    d = GroupInviteProtocol::decideAccept(f);
    CHECK(d.m_action == group_invite_accept_reject);
    CHECK(d.m_errorCode == error_code_group_invite_expired);
}

TEST_CASE("GV8 由卡片转来的加群申请记下邀请 id 与同步条数")
{
    testdb::resetDb(GV_DB_PATH, GV_MYSQL_DB);
    GroupManager& gm = Singleton<GroupManager>::Instance();

    const int64_t fromCard = gm.addJoinRequest(kGroupId, static_cast<int32_t>(kInvitee), "", kCreate,
                                               group_join_source_friend, kInviter, kHistoryLimit, kInviteId);
    REQUIRE(fromCard > 0);
    GroupJoinRequestRecord rec;
    REQUIRE(gm.getJoinRequestById(fromCard, rec));
    CHECK(rec.inviteid == kInviteId);
    CHECK(rec.historylimit == kHistoryLimit);
    CHECK(rec.recommender == kInviter);
    CHECK(rec.source == group_join_source_friend);

    //本人主动申请：没有邀请 id
    const int64_t applied = gm.addJoinRequest(kGroupId, static_cast<int32_t>(kOther), "hi", kCreate);
    REQUIRE(gm.getJoinRequestById(applied, rec));
    CHECK(rec.inviteid == 0);

    testdb::cleanupDb(GV_DB_PATH);
}

TEST_CASE("GV9 卡片类型不允许客户端操作，摘要带群名")
{
    CHECK_FALSE(MessageProtocol::isClientOperableMsgType(message_type_group_invite));
    CHECK(MessageProtocol::isClientOperableMsgType(message_type_text));

    //转发源：卡片不能转发
    MessageRecord src;
    src.msgtype = message_type_group_invite;
    src.recalled = false;
    CHECK_FALSE(MessageProtocol::isForwardableSource(src));

    //摘要
    CHECK(MessageProtocol::buildMessageDigest(message_type_group_invite, sampleCard())
          == std::string(u8"[入群邀请] 产品研发部周会群"));
    CHECK(MessageProtocol::buildMessageDigest(message_type_group_invite, "{}") == std::string(u8"[入群邀请]"));
}
