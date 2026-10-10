/**
 *  文件共享卡片按看的人合并状态、会话列表摘要与断线收尾的单元测试（Suite SHC），test_filesharecard.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 1 期第 7 步（2026-10-02 与用户确认的用例）：
 *    - SHC1 由共享记录与看的人的事实组装状态输入，合并进卡片正文（sharestate / sharereason / myperms）；
 *    - SHC2 会话列表里共享卡片的摘要显示为「[共享] 名称」；
 *    - SHC3 断线收尾：提供端连接断开后，取走它提供的共享并给出要通知的人（打开临时库跑 SQL）；
 *    - SHC4 卡片里分享者的真实路径只给分享者本人（2026-10-04 起）：成员、被移出的人看不到，旧卡片也一样。
 *  SHC1、SHC2 是纯函数；SHC3 默认用 SQLite 临时库，设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL（见 test_dbbackend.h）。
 *
 *  典型用法：Tests.exe --test-case="SHC*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdint.h>
#include <string>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../base/Singleton.h"
#include "../chatserversrc/FileSharePolicy.h"
#include "../chatserversrc/FileShareProtocol.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//A 是分享者，B 是成员
const int64_t kUserA = 100001;
const int64_t kUserB = 100002;

//群 id 与共享 id
const int64_t kGroupG = 900001;
const int64_t kShareId = 42;

//一条有效的群共享记录
ShareRecord GroupShare()
{
    ShareRecord rec;
    rec.m_shareId = kShareId;
    rec.m_ownerId = kUserA;
    rec.m_enterpriseId = 7;
    rec.m_deviceId = "PC-0001";
    rec.m_ctype = conversation_group;
    rec.m_cid = kGroupG;
    rec.m_kind = share_kind_folder;
    rec.m_name = "Projects";
    rec.m_state = FileShareManager::kShareRecordActive;
    return rec;
}

//看的人的事实：默认是一名有上传权限、仍在群里的成员，分享者在绑定的电脑上在线且已报告可以提供
ShareViewerFacts MemberFacts()
{
    ShareViewerFacts facts;
    facts.m_viewerId = kUserB;
    facts.m_isMember = true;
    facts.m_perms = share_perm_upload;
    facts.m_inConversation = true;
    facts.m_featureEnabled = true;
    facts.m_ownerOnline = true;
    facts.m_ownerOnBoundDevice = true;
    facts.m_hasProvideState = true;
    facts.m_provideAvailable = true;
    return facts;
}

//卡片正文（入库的那份）
std::string CardContent()
{
    ShareCardContent card;
    card.m_shareId = kShareId;
    card.m_kind = share_kind_folder;
    card.m_name = "Projects";
    card.m_displayPath = "D:\\Projects";
    card.m_ownerId = kUserA;
    return FileShareProtocol::buildCardContent(card);
}

//解析合并后的正文
Json::Value Parse(const std::string& json)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    Json::CharReader* reader = builder.newCharReader();
    reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return root;
}

//按事实算出的状态
ShareStateView ViewOf(const ShareRecord& share, const ShareViewerFacts& facts)
{
    return FileSharePolicy::viewFor(share, facts);
}

} // anonymous namespace

TEST_CASE("SHC1 按看的人组装状态并合并进卡片")
{
    const ShareRecord share = GroupShare();

    SUBCASE("成员：可用，带上自己的权限")
    {
        const Json::Value card = Parse(FileSharePolicy::augmentCardFor(CardContent(), share, MemberFacts()));
        CHECK(card["sharestate"].asInt() == share_state_available);
        CHECK(card["sharereason"].asInt() == 0);
        CHECK(card["myperms"].asInt() == share_perm_upload);
        CHECK(card["name"].asString() == "Projects");
    }

    SUBCASE("分享者本人：不带 myperms")
    {
        ShareViewerFacts facts = MemberFacts();
        facts.m_viewerId = kUserA;
        facts.m_isMember = false;
        facts.m_perms = 0;
        const Json::Value card = Parse(FileSharePolicy::augmentCardFor(CardContent(), share, facts));
        CHECK(card["sharestate"].asInt() == share_state_available);
        CHECK_FALSE(card.isMember("myperms"));
    }

    SUBCASE("暂不可用的几种原因")
    {
        //分享者不在线：没有在线连接，也没有在线提供记录
        ShareViewerFacts offline = MemberFacts();
        offline.m_ownerOnline = false;
        offline.m_ownerOnBoundDevice = false;
        offline.m_hasProvideState = false;
        CHECK(ViewOf(share, offline).m_state == share_state_unavailable);
        CHECK(ViewOf(share, offline).m_reason == share_unavailable_owner_offline);

        //分享者在线，但不在绑定的电脑上
        ShareViewerFacts otherDevice = offline;
        otherDevice.m_ownerOnline = true;
        CHECK(ViewOf(share, otherDevice).m_reason == share_unavailable_other_device);

        //分享者在绑定的电脑上，但还没有报告（刚登录的那一小段时间）
        ShareViewerFacts notReported = otherDevice;
        notReported.m_ownerOnBoundDevice = true;
        CHECK(ViewOf(share, notReported).m_reason == share_unavailable_root_missing);

        //报告了共享根不能提供
        ShareViewerFacts rootMissing = MemberFacts();
        rootMissing.m_provideAvailable = false;
        CHECK(ViewOf(share, rootMissing).m_reason == share_unavailable_root_missing);

        //有在线提供记录就说明分享者正在绑定的电脑上，即便会话扫描没有找到（例如扫描与报告交错）
        ShareViewerFacts recordOnly = MemberFacts();
        recordOnly.m_ownerOnline = false;
        recordOnly.m_ownerOnBoundDevice = false;
        CHECK(ViewOf(share, recordOnly).m_state == share_state_available);
    }

    SUBCASE("已失效：被移出、离开了群、共享已取消")
    {
        ShareViewerFacts removed = MemberFacts();
        removed.m_isMember = false;
        const Json::Value card = Parse(FileSharePolicy::augmentCardFor(CardContent(), share, removed));
        CHECK(card["sharestate"].asInt() == share_state_invalid);
        CHECK(card["sharereason"].asInt() == share_invalid_member_removed);
        CHECK_FALSE(card.isMember("myperms"));

        ShareViewerFacts left = removed;
        left.m_inConversation = false;
        CHECK(ViewOf(share, left).m_reason == share_invalid_member_left_group);

        //成员表里还有他、但他已不在群里（成员清理尚未完成）：同样按离开了群处理
        ShareViewerFacts lagging = MemberFacts();
        lagging.m_inConversation = false;
        CHECK(ViewOf(share, lagging).m_state == share_state_invalid);
        CHECK(ViewOf(share, lagging).m_reason == share_invalid_member_left_group);

        //共享已取消：对分享者本人同样是已失效
        ShareRecord canceled = share;
        canceled.m_state = FileShareManager::kShareRecordCanceled;
        canceled.m_cancelReason = share_invalid_group_admin_canceled;
        ShareViewerFacts owner = MemberFacts();
        owner.m_viewerId = kUserA;
        owner.m_isMember = false;
        CHECK(ViewOf(canceled, owner).m_state == share_state_invalid);
        CHECK(ViewOf(canceled, owner).m_reason == share_invalid_group_admin_canceled);
    }

    SUBCASE("单聊共享：不看是否仍在会话里")
    {
        ShareRecord single = share;
        single.m_ctype = conversation_single;
        single.m_cid = kUserB;
        ShareViewerFacts facts = MemberFacts();
        facts.m_inConversation = false;
        CHECK(ViewOf(single, facts).m_state == share_state_available);
    }
}

TEST_CASE("SHC2 会话列表里共享卡片的摘要显示为「[共享] 名称」")
{
    CHECK(MessageProtocol::buildMessageDigest(message_type_fileshare, CardContent()) == u8"[共享] Projects");
    //正文解析不了或没有名称时只显示类型
    CHECK(MessageProtocol::buildMessageDigest(message_type_fileshare, "not json") == u8"[共享]");
    CHECK(MessageProtocol::buildMessageDigest(message_type_fileshare, "{\"shareid\":\"1\"}") == u8"[共享]");
}

namespace {

//SHC3 专用的临时库文件与 MySQL 测试库名；MySQL 模式下该库会被整个 DROP 重建
const char* const kShcDbPath = "flamingo_sqlite_filesharecard_test.db";
const char* const kShcMysqlDb = "flamingo_test_shc";

//C 是另一名成员
const int64_t kUserC = 100003;

//两条 chatserver 连接：连接 1 提供两个共享，连接 2 提供一个
const ShareConnKey kConn1 = 11;
const ShareConnKey kConn2 = 12;

//建一个群共享并要求成功，返回 shareid
int64_t CreateShare(const char* clientShareId, const std::vector<ShareMemberSpec>& members)
{
    ShareRecord rec = GroupShare();
    rec.m_clientShareId = clientShareId;
    int64_t shareId = 0;
    bool created = false;
    REQUIRE(Singleton<FileShareManager>::Instance().createShare(rec, members, shareId, created) == error_code_ok);
    REQUIRE(created);
    return shareId;
}

ShareMemberSpec Member(int64_t userId, int perms)
{
    ShareMemberSpec m;
    m.m_userId = userId;
    m.m_perms = perms;
    return m;
}

} // anonymous namespace

TEST_CASE("SHC3 断线收尾：取走断开的连接提供的共享，给出要通知的人，别的连接提供的不受影响")
{
    testdb::resetDb(kShcDbPath, kShcMysqlDb);
    FileShareManager& mgr = Singleton<FileShareManager>::Instance();

    std::vector<ShareMemberSpec> members;
    members.push_back(Member(kUserC, 0));
    members.push_back(Member(kUserB, share_perm_upload));
    const int64_t s1 = CreateShare("u1", members);
    const int64_t s2 = CreateShare("u2", std::vector<ShareMemberSpec>(1, Member(kUserB, 0)));
    const int64_t s3 = CreateShare("u3", std::vector<ShareMemberSpec>(1, Member(kUserC, 0)));
    mgr.setProvideState(s1, kConn1, true, 0);
    mgr.setProvideState(s2, kConn1, false, share_unavailable_root_missing);
    mgr.setProvideState(s3, kConn2, true, 0);

    std::vector<ShareOfflineNotice> notices = mgr.takeOfflineNotices(kConn1);
    REQUIRE(notices.size() == 2);
    //按 shareid 升序
    CHECK(notices[0].m_share.m_shareId == s1);
    std::vector<int64_t> expected;
    expected.push_back(kUserA);
    expected.push_back(kUserB);
    expected.push_back(kUserC);
    CHECK(notices[0].m_notifyUsers == expected);
    CHECK(notices[1].m_share.m_shareId == s2);
    expected.clear();
    expected.push_back(kUserA);
    expected.push_back(kUserB);
    CHECK(notices[1].m_notifyUsers == expected);

    //在线提供记录已清除：分享者断线后成员看到「暂不可用，分享者不在线」
    ShareProvideState state;
    CHECK_FALSE(mgr.getProvideState(s1, state));
    ShareViewerFacts facts = MemberFacts();
    facts.m_ownerOnline = false;
    facts.m_ownerOnBoundDevice = false;
    facts.m_hasProvideState = false;
    ShareRecord share = notices[0].m_share;
    CHECK(ViewOf(share, facts).m_state == share_state_unavailable);
    CHECK(ViewOf(share, facts).m_reason == share_unavailable_owner_offline);

    //连接 2 提供的共享不受影响；再收尾一次没有任何共享
    CHECK(mgr.getProvideState(s3, state));
    CHECK(mgr.takeOfflineNotices(kConn1).empty());

    //已取消的共享不再通知（取消时在线提供状态本应已被清除，这里防御记录残留的情况）
    REQUIRE(mgr.cancelShare(s3, share_invalid_owner_canceled, kUserA, 0) == error_code_ok);
    CHECK(mgr.takeOfflineNotices(kConn2).empty());
    CHECK_FALSE(mgr.getProvideState(s3, state));

    mgr.clearProvideState(s1);
    testdb::cleanupDb(kShcDbPath);
}

TEST_CASE("SHC4 卡片里的真实路径只给分享者本人：成员看不到，旧卡片也一样")
{
    ShareRecord share = GroupShare();
    share.m_displayPath = "D:\\Projects";

    //2026-10-04 起入库的卡片：不带路径
    ShareCardContent fresh;
    fresh.m_shareId = kShareId;
    fresh.m_kind = share_kind_folder;
    fresh.m_name = "Projects";
    fresh.m_ownerId = kUserA;
    const std::string stored = FileShareProtocol::buildCardContent(fresh);
    REQUIRE_FALSE(Parse(stored).isMember("displaypath"));

    ShareViewerFacts ownerFacts = MemberFacts();
    ownerFacts.m_viewerId = kUserA;
    ownerFacts.m_isMember = false;
    ownerFacts.m_perms = 0;

    //新卡片：分享者本人看到共享记录里的真实路径，成员看不到
    CHECK(Parse(FileSharePolicy::augmentCardFor(stored, share, ownerFacts))["displaypath"].asString() == "D:\\Projects");
    CHECK_FALSE(Parse(FileSharePolicy::augmentCardFor(stored, share, MemberFacts())).isMember("displaypath"));

    //旧卡片（CardContent 带路径）：成员收到的路径被去掉，分享者本人照样看到
    CHECK_FALSE(Parse(FileSharePolicy::augmentCardFor(CardContent(), share, MemberFacts())).isMember("displaypath"));
    CHECK(Parse(FileSharePolicy::augmentCardFor(CardContent(), share, ownerFacts))["displaypath"].asString()
          == "D:\\Projects");

    //已不是成员的人（被移出）同样看不到
    ShareViewerFacts removed = MemberFacts();
    removed.m_isMember = false;
    removed.m_perms = 0;
    CHECK_FALSE(Parse(FileSharePolicy::augmentCardFor(CardContent(), share, removed)).isMember("displaypath"));
}
