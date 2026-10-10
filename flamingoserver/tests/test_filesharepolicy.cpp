/**
 *  文件共享校验规则与在线提供状态表的单元测试（Suite SHP），test_filesharepolicy.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 1 期第 7 步的 7b（2026-10-02 与用户确认的用例）：
 *    - SHP1 发起共享的校验（成员是否在会话里、是否同属一个企业、名额）；
 *    - SHP2 取消共享的权限与原因；
 *    - SHP3 调整成员的校验（只有分享者、新成员资格、单聊限制、单文件共享权限清零）；
 *    - SHP4 在线提供状态表（FileShareManager 的内存部分）；
 *    - SHP5 在线提供报告的接受规则；
 *    - SHP6 卡片可见范围与推送名单；
 *    - SHP7 本企业的文件共享已关闭（第 3 期第 1 步）：发起与调整成员回 800，先于其余判定；取消不受影响。
 *  SHP4 用独立的 FileShareManager 实例，不碰进程内的单例；其余全是纯函数。
 *
 *  典型用法：Tests.exe --test-case="SHP*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdint.h>
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/FileSharePolicy.h"
#include "../chatserversrc/FileShareManager.h"
#include "../chatserversrc/Protocol.h"

namespace {

//A 是分享者，B、C 与 A 同企业同群，D 在群里但属于另一个企业，E 不在群里
const int64_t kUserA = 100001;
const int64_t kUserB = 100002;
const int64_t kUserC = 100003;
const int64_t kUserD = 100004;
const int64_t kUserE = 100005;

//两个企业
const int64_t kEnterprise = 7;
const int64_t kOtherEnterprise = 8;

//群 id
const int64_t kGroupG = 900001;

//每人最多同时持有的共享数
const int kMaxSharesPerOwner = 20;

//两条 chatserver 连接的标识
const ShareConnKey kConn1 = 1;
const ShareConnKey kConn2 = 2;

ShareMemberSpec Member(int64_t userId, int perms)
{
    ShareMemberSpec m;
    m.m_userId = userId;
    m.m_perms = perms;
    return m;
}

//群 G 里的事实：A、B、C、D 在群里，D 属于另一个企业；A 目前有 activeShares 个有效共享
ShareCheckFacts GroupFacts(int activeShares)
{
    ShareCheckFacts facts;
    facts.m_ownerEnterpriseId = kEnterprise;
    facts.m_conversationUsers.insert(kUserA);
    facts.m_conversationUsers.insert(kUserB);
    facts.m_conversationUsers.insert(kUserC);
    facts.m_conversationUsers.insert(kUserD);
    facts.m_enterpriseOf[kUserA] = kEnterprise;
    facts.m_enterpriseOf[kUserB] = kEnterprise;
    facts.m_enterpriseOf[kUserC] = kEnterprise;
    facts.m_enterpriseOf[kUserD] = kOtherEnterprise;
    facts.m_enterpriseOf[kUserE] = kEnterprise;
    facts.m_activeShareCount = activeShares;
    facts.m_maxSharesPerOwner = kMaxSharesPerOwner;
    return facts;
}

//A 在群 G 发起共享的请求
ShareCreateRequest GroupCreate(const std::vector<ShareMemberSpec>& members)
{
    ShareCreateRequest req;
    req.m_clientShareId = "uuid-1";
    req.m_ctype = conversation_group;
    req.m_cid = kGroupG;
    req.m_kind = share_kind_folder;
    req.m_name = "Projects";
    req.m_displayPath = "D:\\Projects";
    req.m_members = members;
    return req;
}

//一条有效的共享记录：A 在会话 (ctype, cid) 里共享 kind 类型，绑定设备 PC-0001
ShareRecord ActiveShare(int ctype, int64_t cid, int kind)
{
    ShareRecord rec;
    rec.m_shareId = 42;
    rec.m_ownerId = kUserA;
    rec.m_enterpriseId = kEnterprise;
    rec.m_deviceId = "PC-0001";
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_kind = kind;
    rec.m_state = FileShareManager::kShareRecordActive;
    return rec;
}

std::vector<int64_t> Ids(int64_t a)
{
    return std::vector<int64_t>(1, a);
}

std::vector<int64_t> Ids(int64_t a, int64_t b)
{
    std::vector<int64_t> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

std::vector<int64_t> Ids(int64_t a, int64_t b, int64_t c)
{
    std::vector<int64_t> v = Ids(a, b);
    v.push_back(c);
    return v;
}

std::vector<int64_t> Sorted(std::vector<int64_t> v)
{
    std::sort(v.begin(), v.end());
    return v;
}

} // anonymous namespace

TEST_CASE("SHP1 发起共享的校验")
{
    std::vector<int64_t> invalid;

    SUBCASE("群聊：成员都在群里、同企业，名额未满时通过")
    {
        std::vector<ShareMemberSpec> members;
        members.push_back(Member(kUserB, share_perm_upload));
        members.push_back(Member(kUserC, 0));
        CHECK(FileSharePolicy::checkCreate(GroupCreate(members), kUserA, GroupFacts(0), invalid) == error_code_ok);
        CHECK(invalid.empty());
    }

    SUBCASE("群聊：成员不在群里、或包含分享者本人时回 819 并列出这些人")
    {
        std::vector<ShareMemberSpec> members;
        members.push_back(Member(kUserB, 0));
        members.push_back(Member(kUserE, 0));
        members.push_back(Member(kUserA, 0));
        CHECK(FileSharePolicy::checkCreate(GroupCreate(members), kUserA, GroupFacts(0), invalid)
              == error_code_share_invalid_member);
        CHECK(Sorted(invalid) == Ids(kUserA, kUserE));
    }

    SUBCASE("成员与分享者不在同一个企业时回 821 并列出这些人")
    {
        std::vector<ShareMemberSpec> members;
        members.push_back(Member(kUserB, 0));
        members.push_back(Member(kUserD, 0));
        CHECK(FileSharePolicy::checkCreate(GroupCreate(members), kUserA, GroupFacts(0), invalid)
              == error_code_share_cross_enterprise);
        CHECK(invalid == Ids(kUserD));
    }

    SUBCASE("分享者不属于任何企业时回 821")
    {
        ShareCheckFacts facts = GroupFacts(0);
        facts.m_ownerEnterpriseId = 0;
        CHECK(FileSharePolicy::checkCreate(GroupCreate(std::vector<ShareMemberSpec>(1, Member(kUserB, 0))),
                                           kUserA, facts, invalid) == error_code_share_cross_enterprise);
        CHECK(invalid.empty());
    }

    SUBCASE("有效共享数到达上限时回 815")
    {
        CHECK(FileSharePolicy::checkCreate(GroupCreate(std::vector<ShareMemberSpec>(1, Member(kUserB, 0))),
                                           kUserA, GroupFacts(kMaxSharesPerOwner), invalid)
              == error_code_share_too_many);
        CHECK(FileSharePolicy::checkCreate(GroupCreate(std::vector<ShareMemberSpec>(1, Member(kUserB, 0))),
                                           kUserA, GroupFacts(kMaxSharesPerOwner - 1), invalid) == error_code_ok);
    }

    SUBCASE("单聊：对端在会话里且同企业时通过；对端属于另一个企业时回 821")
    {
        ShareCreateRequest req = GroupCreate(std::vector<ShareMemberSpec>(1, Member(kUserB, 0)));
        req.m_ctype = conversation_single;
        req.m_cid = kUserB;
        ShareCheckFacts facts = GroupFacts(0);
        facts.m_conversationUsers.clear();
        facts.m_conversationUsers.insert(kUserB);
        CHECK(FileSharePolicy::checkCreate(req, kUserA, facts, invalid) == error_code_ok);

        req.m_cid = kUserD;
        req.m_members[0].m_userId = kUserD;
        facts.m_conversationUsers.clear();
        facts.m_conversationUsers.insert(kUserD);
        CHECK(FileSharePolicy::checkCreate(req, kUserA, facts, invalid) == error_code_share_cross_enterprise);
        CHECK(invalid == Ids(kUserD));
    }
}

TEST_CASE("SHP2 取消共享的权限与原因")
{
    int reason = 0;
    const ShareRecord group = ActiveShare(conversation_group, kGroupG, share_kind_folder);

    CHECK(FileSharePolicy::checkCancel(group, kUserA, group_role_member, reason) == error_code_ok);
    CHECK(reason == share_invalid_owner_canceled);

    CHECK(FileSharePolicy::checkCancel(group, kUserB, group_role_owner, reason) == error_code_ok);
    CHECK(reason == share_invalid_group_admin_canceled);
    CHECK(FileSharePolicy::checkCancel(group, kUserC, group_role_admin, reason) == error_code_ok);
    CHECK(reason == share_invalid_group_admin_canceled);

    CHECK(FileSharePolicy::checkCancel(group, kUserB, group_role_member, reason) == error_code_permission_denied);
    //不在群里（会话层传 -1）
    CHECK(FileSharePolicy::checkCancel(group, kUserE, -1, reason) == error_code_permission_denied);

    //单聊里的对端不能取消（单聊没有群角色，会话层传 -1）
    const ShareRecord single = ActiveShare(conversation_single, kUserB, share_kind_folder);
    CHECK(FileSharePolicy::checkCancel(single, kUserB, -1, reason) == error_code_permission_denied);
    CHECK(FileSharePolicy::checkCancel(single, kUserA, -1, reason) == error_code_ok);

    //已经取消的共享
    ShareRecord canceled = group;
    canceled.m_state = FileShareManager::kShareRecordCanceled;
    CHECK(FileSharePolicy::checkCancel(canceled, kUserA, group_role_member, reason) == error_code_share_canceled);
}

TEST_CASE("SHP3 调整成员的校验")
{
    std::vector<int64_t> invalid;
    const ShareRecord group = ActiveShare(conversation_group, kGroupG, share_kind_folder);

    SUBCASE("只有分享者本人可以调整，已取消的共享不能调整")
    {
        ShareUpdateMembersRequest req;
        req.m_shareId = group.m_shareId;
        req.m_add.push_back(Member(kUserC, 0));
        CHECK(FileSharePolicy::checkUpdateMembers(group, kUserB, req, GroupFacts(0), invalid)
              == error_code_permission_denied);
        CHECK(FileSharePolicy::checkUpdateMembers(group, kUserA, req, GroupFacts(0), invalid) == error_code_ok);

        ShareRecord canceled = group;
        canceled.m_state = FileShareManager::kShareRecordCanceled;
        CHECK(FileSharePolicy::checkUpdateMembers(canceled, kUserA, req, GroupFacts(0), invalid)
              == error_code_share_canceled);
    }

    SUBCASE("新增成员同样要求在会话里、与分享者同属一个企业；移除与改权限不查这两项")
    {
        ShareUpdateMembersRequest req;
        req.m_shareId = group.m_shareId;
        req.m_add.push_back(Member(kUserE, 0));
        CHECK(FileSharePolicy::checkUpdateMembers(group, kUserA, req, GroupFacts(0), invalid)
              == error_code_share_invalid_member);
        CHECK(invalid == Ids(kUserE));

        req.m_add.clear();
        req.m_add.push_back(Member(kUserD, 0));
        CHECK(FileSharePolicy::checkUpdateMembers(group, kUserA, req, GroupFacts(0), invalid)
              == error_code_share_cross_enterprise);
        CHECK(invalid == Ids(kUserD));

        //已经离群的人仍可以被移除、被改权限（例如清理残留成员）
        ShareUpdateMembersRequest cleanup;
        cleanup.m_shareId = group.m_shareId;
        cleanup.m_remove.push_back(kUserE);
        cleanup.m_update.push_back(Member(kUserD, share_perm_upload));
        CHECK(FileSharePolicy::checkUpdateMembers(group, kUserA, cleanup, GroupFacts(0), invalid) == error_code_ok);
    }

    SUBCASE("单聊共享：不能追加别人、不能移除对方，但可以改对方的权限")
    {
        const ShareRecord single = ActiveShare(conversation_single, kUserB, share_kind_folder);
        ShareCheckFacts facts = GroupFacts(0);
        facts.m_conversationUsers.clear();
        facts.m_conversationUsers.insert(kUserB);

        ShareUpdateMembersRequest add;
        add.m_shareId = single.m_shareId;
        add.m_add.push_back(Member(kUserC, 0));
        CHECK(FileSharePolicy::checkUpdateMembers(single, kUserA, add, facts, invalid)
              == error_code_share_invalid_member);

        ShareUpdateMembersRequest remove;
        remove.m_shareId = single.m_shareId;
        remove.m_remove.push_back(kUserB);
        CHECK(FileSharePolicy::checkUpdateMembers(single, kUserA, remove, facts, invalid) == error_code_bad_request);

        ShareUpdateMembersRequest update;
        update.m_shareId = single.m_shareId;
        update.m_update.push_back(Member(kUserB, share_perm_upload | share_perm_mkdir));
        CHECK(FileSharePolicy::checkUpdateMembers(single, kUserA, update, facts, invalid) == error_code_ok);
        CHECK(update.m_update[0].m_perms == (share_perm_upload | share_perm_mkdir));
    }

    SUBCASE("单文件共享：追加与改权限里的写权限一律清零")
    {
        const ShareRecord file = ActiveShare(conversation_group, kGroupG, share_kind_file);
        ShareUpdateMembersRequest req;
        req.m_shareId = file.m_shareId;
        req.m_add.push_back(Member(kUserC, share_perm_upload));
        req.m_update.push_back(Member(kUserB, kSharePermAll));
        CHECK(FileSharePolicy::checkUpdateMembers(file, kUserA, req, GroupFacts(0), invalid) == error_code_ok);
        CHECK(req.m_add[0].m_perms == 0);
        CHECK(req.m_update[0].m_perms == 0);
    }
}

TEST_CASE("SHP4 在线提供状态表：查询、覆盖、按连接清除、按共享清除")
{
    FileShareManager mgr;
    ShareProvideState state;
    CHECK_FALSE(mgr.getProvideState(1, state));

    mgr.setProvideState(1, kConn1, true, 0);
    mgr.setProvideState(2, kConn1, false, share_unavailable_root_missing);
    mgr.setProvideState(3, kConn2, true, 0);
    REQUIRE(mgr.getProvideState(1, state));
    CHECK(state.m_available);
    CHECK(state.m_reason == 0);
    CHECK(state.m_conn == kConn1);
    REQUIRE(mgr.getProvideState(2, state));
    CHECK_FALSE(state.m_available);
    CHECK(state.m_reason == share_unavailable_root_missing);

    //同一共享再报告一次：覆盖
    mgr.setProvideState(1, kConn1, false, share_unavailable_root_missing);
    REQUIRE(mgr.getProvideState(1, state));
    CHECK_FALSE(state.m_available);

    //连接 1 断开：共享 1、2 的状态被清除并交还调用方，共享 3 不受影响
    CHECK(Sorted(mgr.clearProvideStatesForConnection(kConn1)) == Ids(1, 2));
    CHECK_FALSE(mgr.getProvideState(1, state));
    CHECK_FALSE(mgr.getProvideState(2, state));
    CHECK(mgr.getProvideState(3, state));
    CHECK(mgr.clearProvideStatesForConnection(kConn1).empty());

    //取消共享 3
    mgr.clearProvideState(3);
    CHECK_FALSE(mgr.getProvideState(3, state));

    //每人有效共享数上限的配置：缺省 20，小于 1 时回落到缺省值
    CHECK(mgr.getMaxSharesPerOwner() == FileShareManager::kDefaultMaxSharesPerOwner);
    mgr.setMaxSharesPerOwner(3);
    CHECK(mgr.getMaxSharesPerOwner() == 3);
    mgr.setMaxSharesPerOwner(0);
    CHECK(mgr.getMaxSharesPerOwner() == FileShareManager::kDefaultMaxSharesPerOwner);
}

TEST_CASE("SHP5 在线提供报告的接受规则")
{
    const ShareRecord share = ActiveShare(conversation_group, kGroupG, share_kind_folder);

    CHECK(FileSharePolicy::acceptServeReport(share, kUserA, "PC-0001"));
    //不是分享者本人
    CHECK_FALSE(FileSharePolicy::acceptServeReport(share, kUserB, "PC-0001"));
    //分享者在别的电脑上
    CHECK_FALSE(FileSharePolicy::acceptServeReport(share, kUserA, "PC-0002"));
    //连接没有带设备标识
    CHECK_FALSE(FileSharePolicy::acceptServeReport(share, kUserA, ""));
    //共享绑定的设备标识为空（不应出现，防御）
    ShareRecord noDevice = share;
    noDevice.m_deviceId = "";
    CHECK_FALSE(FileSharePolicy::acceptServeReport(noDevice, kUserA, ""));
    //已取消的共享
    ShareRecord canceled = share;
    canceled.m_state = FileShareManager::kShareRecordCanceled;
    CHECK_FALSE(FileSharePolicy::acceptServeReport(canceled, kUserA, "PC-0001"));
}

TEST_CASE("SHP6 卡片可见范围与推送名单")
{
    std::vector<ShareMemberSpec> members;
    members.push_back(Member(kUserC, 0));
    members.push_back(Member(kUserB, share_perm_upload));

    //可见范围：全体成员，按 userid 升序；分享者作为发送者本来就看得到，不写进范围
    const std::vector<int64_t> audience = FileSharePolicy::cardAudience(members, kUserA);
    CHECK(audience == Ids(kUserB, kUserC));

    //推送名单：成员加分享者本人（分享者的其他设备也要收到这张卡片）
    CHECK(FileSharePolicy::pushTargets(audience, kUserA) == Ids(kUserA, kUserB, kUserC));

    //追加成员：新卡片的可见范围只含新成员
    ShareMemberChanges changes;
    changes.m_added.push_back(Member(kUserD, 0));
    changes.m_updated.push_back(Member(kUserB, 0));
    CHECK(FileSharePolicy::cardAudience(changes.m_added, kUserA) == Ids(kUserD));
    CHECK(FileSharePolicy::pushTargets(FileSharePolicy::cardAudience(changes.m_added, kUserA), kUserA)
          == Ids(kUserA, kUserD));
}

TEST_CASE("SHP7 文件共享已关闭时发起与调整成员回 800")
{
    std::vector<int64_t> invalid;
    ShareCheckFacts off = GroupFacts(0);
    off.m_featureEnabled = false;

    //发起：先于成员与名额的判定
    CHECK(FileSharePolicy::checkCreate(GroupCreate(std::vector<ShareMemberSpec>(1, Member(kUserB, 0))), kUserA, off, invalid)
          == error_code_share_disabled);
    CHECK(invalid.empty());
    ShareCheckFacts full = GroupFacts(kMaxSharesPerOwner);
    full.m_featureEnabled = false;
    CHECK(FileSharePolicy::checkCreate(GroupCreate(std::vector<ShareMemberSpec>(1, Member(kUserE, 0))), kUserA, full, invalid)
          == error_code_share_disabled);

    //调整成员
    const ShareRecord group = ActiveShare(conversation_group, kGroupG, share_kind_folder);
    ShareUpdateMembersRequest req;
    req.m_shareId = group.m_shareId;
    req.m_add.push_back(Member(kUserC, 0));
    CHECK(FileSharePolicy::checkUpdateMembers(group, kUserA, req, off, invalid) == error_code_share_disabled);

    //取消共享不受开关影响（关闭期间分享者仍可清理自己的共享）
    int reason = 0;
    CHECK(FileSharePolicy::checkCancel(group, kUserA, -1, reason) == error_code_ok);
}
