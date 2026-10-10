/**
 *  文件共享访问与传输相关判定的单元测试（Suite SHT），test_fileshareaccess.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 1 期第 8 步的 8b（2026-10-02 与用户确认的用例）：
 *    - SHT1 打开共享（2220）的判定；
 *    - SHT2 申请传输（2224）的判定；
 *    - SHT3 中转参数（2222）的构造：给访问者与给分享者的两帧、通道条数、令牌；
 *    - SHT4 审计上报（2230）的接受规则与入库字段；
 *    - SHT5 申请传输时服务端自己记的审计；
 *    - SHT6 本企业的文件共享已关闭（第 3 期第 1 步）：打开与申请传输回 800，状态为暂不可用、原因「功能已被关闭」。
 *  全部是纯函数（SHT3 用一个独立的 FileShareSessionManager 实例签令牌），不碰数据库与网络。
 *
 *  典型用法：Tests.exe --test-case="SHT*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <stdint.h>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/FileSharePolicy.h"
#include "../chatserversrc/FileShareSessionManager.h"
#include "../chatserversrc/Protocol.h"
#include "../utils/RelayToken.h"

namespace {

//A 是分享者，B 是成员
const int64_t kUserA = 100001;
const int64_t kUserB = 100002;

//两个企业、群与共享
const int64_t kEnterprise = 7;
const int64_t kOtherEnterprise = 8;
const int64_t kGroupG = 900001;
const int64_t kShareId = 42;

//测试用的令牌密钥、中转地址与当前时间
const char* const kRelayKey = "sht-relay-key";
const char* const kRelayHost = "10.0.0.9";
const int kRelayPort = 20001;
const int64_t kNowMs = 1759370000000LL;

//测试配置里的数据通道条数与超时、搜索参数
const int kLanes = 3;
const int kOpenTimeoutMs = 12000;
const int kIdleTimeoutMs = 300000;
const int kSearchMaxResults = 200;
const int kSearchTimeoutMs = 8000;

//一条有效的群共享记录（文件夹）
ShareRecord GroupShare()
{
    ShareRecord rec;
    rec.m_shareId = kShareId;
    rec.m_ownerId = kUserA;
    rec.m_enterpriseId = kEnterprise;
    rec.m_deviceId = "PC-0001";
    rec.m_ctype = conversation_group;
    rec.m_cid = kGroupG;
    rec.m_kind = share_kind_folder;
    rec.m_name = "Projects";
    rec.m_state = FileShareManager::kShareRecordActive;
    return rec;
}

//成员 B 的事实：perms 为他的写权限，分享者在绑定的电脑上在线且已报告可以提供
ShareViewerFacts MemberFacts(int perms)
{
    ShareViewerFacts facts;
    facts.m_viewerId = kUserB;
    facts.m_isMember = true;
    facts.m_perms = perms;
    facts.m_inConversation = true;
    facts.m_featureEnabled = true;
    facts.m_ownerOnline = true;
    facts.m_ownerOnBoundDevice = true;
    facts.m_hasProvideState = true;
    facts.m_provideAvailable = true;
    return facts;
}

//同属一个企业时的企业归属
ShareEnterprisePair SameEnterprise()
{
    ShareEnterprisePair ent;
    ent.m_ownerEnterpriseId = kEnterprise;
    ent.m_viewerEnterpriseId = kEnterprise;
    return ent;
}

//配置好的会话管理器
void ConfigureSessions(FileShareSessionManager& mgr)
{
    FileShareSessionConfig cfg;
    cfg.m_relayTokenKey = kRelayKey;
    cfg.m_relayHost = kRelayHost;
    cfg.m_relayPort = kRelayPort;
    cfg.m_lanes = kLanes;
    cfg.m_openTimeoutMs = kOpenTimeoutMs;
    cfg.m_idleTimeoutMs = kIdleTimeoutMs;
    cfg.m_searchMaxResults = kSearchMaxResults;
    cfg.m_searchTimeoutMs = kSearchTimeoutMs;
    mgr.configure(cfg);
}

//一条传输条目
ShareTransferItem Item(const char* path, bool isDir, int64_t size)
{
    ShareTransferItem item;
    item.m_path = path;
    item.m_isDir = isDir;
    item.m_size = size;
    return item;
}

} // anonymous namespace

TEST_CASE("SHT1 打开共享的判定")
{
    const ShareRecord share = GroupShare();

    CHECK(FileSharePolicy::checkOpen(share, MemberFacts(0), SameEnterprise()) == error_code_ok);

    //已取消
    ShareRecord canceled = share;
    canceled.m_state = FileShareManager::kShareRecordCanceled;
    canceled.m_cancelReason = share_invalid_owner_canceled;
    CHECK(FileSharePolicy::checkOpen(canceled, MemberFacts(0), SameEnterprise()) == error_code_share_canceled);

    //已被移出、已离开了群
    ShareViewerFacts removed = MemberFacts(0);
    removed.m_isMember = false;
    CHECK(FileSharePolicy::checkOpen(share, removed, SameEnterprise()) == error_code_share_not_member);
    ShareViewerFacts left = MemberFacts(0);
    left.m_inConversation = false;
    CHECK(FileSharePolicy::checkOpen(share, left, SameEnterprise()) == error_code_share_not_member);

    //暂不可用：分享者不在线、在别的电脑上、共享根不能提供
    ShareViewerFacts offline = MemberFacts(0);
    offline.m_ownerOnline = false;
    offline.m_ownerOnBoundDevice = false;
    offline.m_hasProvideState = false;
    CHECK(FileSharePolicy::checkOpen(share, offline, SameEnterprise()) == error_code_share_unavailable);
    ShareViewerFacts otherDevice = offline;
    otherDevice.m_ownerOnline = true;
    CHECK(FileSharePolicy::checkOpen(share, otherDevice, SameEnterprise()) == error_code_share_unavailable);
    ShareViewerFacts rootMissing = MemberFacts(0);
    rootMissing.m_provideAvailable = false;
    CHECK(FileSharePolicy::checkOpen(share, rootMissing, SameEnterprise()) == error_code_share_unavailable);

    //访问者与分享者已不在同一个企业、访问者不属于任何企业
    ShareEnterprisePair moved = SameEnterprise();
    moved.m_viewerEnterpriseId = kOtherEnterprise;
    CHECK(FileSharePolicy::checkOpen(share, MemberFacts(0), moved) == error_code_share_cross_enterprise);
    ShareEnterprisePair none = SameEnterprise();
    none.m_viewerEnterpriseId = 0;
    CHECK(FileSharePolicy::checkOpen(share, MemberFacts(0), none) == error_code_share_cross_enterprise);

    //分享者本人打开自己的共享：本机直接打开，不经中转
    ShareViewerFacts owner = MemberFacts(0);
    owner.m_viewerId = kUserA;
    owner.m_isMember = false;
    CHECK(FileSharePolicy::checkOpen(share, owner, SameEnterprise()) == error_code_bad_request);
}

TEST_CASE("SHT2 申请传输的判定")
{
    const ShareRecord share = GroupShare();

    //下载一律允许
    CHECK(FileSharePolicy::checkTransfer(share, MemberFacts(0), SameEnterprise(), share_transfer_download)
          == error_code_ok);

    //上传要求有上传权限
    CHECK(FileSharePolicy::checkTransfer(share, MemberFacts(share_perm_mkdir | share_perm_delete), SameEnterprise(),
                                         share_transfer_upload) == error_code_share_permission_denied);
    CHECK(FileSharePolicy::checkTransfer(share, MemberFacts(share_perm_upload), SameEnterprise(),
                                         share_transfer_upload) == error_code_ok);

    //单文件共享不能上传，即便权限位里有上传
    ShareRecord file = share;
    file.m_kind = share_kind_file;
    CHECK(FileSharePolicy::checkTransfer(file, MemberFacts(share_perm_upload), SameEnterprise(),
                                         share_transfer_upload) == error_code_share_kind_mismatch);
    CHECK(FileSharePolicy::checkTransfer(file, MemberFacts(0), SameEnterprise(), share_transfer_download)
          == error_code_ok);

    //共享状态的判定与打开相同
    ShareViewerFacts offline = MemberFacts(share_perm_upload);
    offline.m_hasProvideState = false;
    offline.m_ownerOnline = false;
    CHECK(FileSharePolicy::checkTransfer(share, offline, SameEnterprise(), share_transfer_download)
          == error_code_share_unavailable);
    ShareViewerFacts removed = MemberFacts(share_perm_upload);
    removed.m_isMember = false;
    CHECK(FileSharePolicy::checkTransfer(share, removed, SameEnterprise(), share_transfer_upload)
          == error_code_share_not_member);
}

TEST_CASE("SHT3 中转参数：给访问者与给分享者的两帧")
{
    FileShareSessionManager sessions;
    ConfigureSessions(sessions);
    std::string accessId;
    REQUIRE(sessions.openAccess(kShareId, kUserA, 1, kUserB, 2, kNowMs, accessId) == error_code_ok);
    ShareAccessInfo access;
    REQUIRE(sessions.getAccess(accessId, access));

    SUBCASE("访问会话本身：只有控制通道")
    {
        ShareRelayInfo toOwner;
        ShareRelayInfo toVisitor;
        FileSharePolicy::makeRelayInfos(sessions, access, std::string(), 0, share_perm_upload, "B 的名字", kNowMs,
                                        toOwner, toVisitor);

        CHECK(toVisitor.m_accessId == accessId);
        CHECK(toVisitor.m_transferId.empty());
        CHECK(toVisitor.m_relayIp == kRelayHost);
        CHECK(toVisitor.m_relayPort == kRelayPort);
        CHECK(toVisitor.m_lanes == 0);
        CHECK(toVisitor.m_role == RelayToken::role_share_visitor);
        CHECK(toVisitor.m_openTimeoutMs == kOpenTimeoutMs);
        CHECK(toVisitor.m_idleTimeoutMs == kIdleTimeoutMs);
        CHECK_FALSE(toVisitor.m_forOwner);

        CHECK(toOwner.m_forOwner);
        CHECK(toOwner.m_role == RelayToken::role_share_owner);
        CHECK(toOwner.m_lanes == 0);
        CHECK(toOwner.m_shareId == kShareId);
        CHECK(toOwner.m_visitorId == kUserB);
        CHECK(toOwner.m_visitorName == "B 的名字");
        CHECK(toOwner.m_perms == share_perm_upload);
        CHECK(toOwner.m_searchMaxResults == kSearchMaxResults);
        CHECK(toOwner.m_searchTimeoutMs == kSearchTimeoutMs);

        //两枚令牌都能校验通过，sessionid 就是访问会话 id，uid 与 peeruid 对调
        RelayTokenPayload owner;
        RelayTokenPayload visitor;
        REQUIRE(RelayToken::verify(toOwner.m_token, kRelayKey, kNowMs, owner));
        REQUIRE(RelayToken::verify(toVisitor.m_token, kRelayKey, kNowMs, visitor));
        CHECK(owner.sessionid == accessId);
        CHECK(visitor.sessionid == accessId);
        CHECK(owner.role == RelayToken::role_share_owner);
        CHECK(visitor.role == RelayToken::role_share_visitor);
        CHECK(owner.uid == kUserA);
        CHECK(visitor.uid == kUserB);
    }

    SUBCASE("传输子会话：通道条数取配置，令牌的 sessionid 是传输子会话 id")
    {
        std::string transferId;
        REQUIRE(sessions.requestTransfer(accessId, kUserB, share_transfer_download, transferId) == error_code_ok);
        ShareRelayInfo toOwner;
        ShareRelayInfo toVisitor;
        FileSharePolicy::makeRelayInfos(sessions, access, transferId, share_transfer_download, 0, "B", kNowMs, toOwner,
                                        toVisitor);

        CHECK(toVisitor.m_transferId == transferId);
        //两帧都带传输方向（第 14 步补充：分享者据此决定以发送端还是接收端建通道）
        CHECK(toVisitor.m_direction == share_transfer_download);
        CHECK(toOwner.m_direction == share_transfer_download);
        CHECK(toOwner.m_transferId == transferId);
        CHECK(toVisitor.m_lanes == kLanes);
        CHECK(toOwner.m_lanes == kLanes);
        RelayTokenPayload visitor;
        REQUIRE(RelayToken::verify(toVisitor.m_token, kRelayKey, kNowMs, visitor));
        CHECK(visitor.sessionid == transferId);
    }
}

TEST_CASE("SHT4 审计上报：只接受分享者本人的记录，入库字段由服务端补齐")
{
    const ShareRecord share = GroupShare();

    ShareAuditItem item;
    item.m_shareId = kShareId;
    item.m_accessId = "share-3-1759370000000";
    item.m_visitorId = kUserB;
    item.m_action = share_audit_upload_file;
    item.m_path = "docs/a.txt";
    item.m_toPath = "";
    item.m_size = 1024;
    item.m_result = error_code_share_disk_full;
    item.m_clientTime = kNowMs - 60000;

    CHECK(FileSharePolicy::acceptAuditItem(share, kUserA));
    CHECK_FALSE(FileSharePolicy::acceptAuditItem(share, kUserB));
    //共享已取消后才到达的上报照样接受：执行发生在取消之前
    ShareRecord canceled = share;
    canceled.m_state = FileShareManager::kShareRecordCanceled;
    CHECK(FileSharePolicy::acceptAuditItem(canceled, kUserA));

    const ShareAuditRecord rec = FileSharePolicy::buildClientAuditRecord(share, item, kNowMs);
    CHECK(rec.m_shareId == kShareId);
    CHECK(rec.m_ownerId == kUserA);
    CHECK(rec.m_actorId == kUserB);
    CHECK(rec.m_enterpriseId == kEnterprise);
    CHECK(rec.m_action == share_audit_upload_file);
    CHECK(rec.m_source == share_audit_source_owner_client);
    CHECK(rec.m_path == "docs/a.txt");
    CHECK(rec.m_size == 1024);
    CHECK(rec.m_result == error_code_share_disk_full);
    CHECK(rec.m_accessId == item.m_accessId);
    //入库用服务端时间，不用客户端时间
    CHECK(rec.m_createTime == kNowMs);
}

TEST_CASE("SHT5 申请传输时服务端记的审计")
{
    const ShareRecord share = GroupShare();

    ShareTransferRequest req;
    req.m_accessId = "share-3-1759370000000";
    req.m_direction = share_transfer_download;
    req.m_items.push_back(Item("docs/报告.docx", false, 1000));
    req.m_totalBytes = 1000;
    req.m_totalFiles = 1;

    ShareAuditRecord rec = FileSharePolicy::buildTransferAuditRecord(share, kUserB, req, kNowMs);
    CHECK(rec.m_action == share_audit_request_download);
    CHECK(rec.m_source == share_audit_source_server);
    CHECK(rec.m_actorId == kUserB);
    CHECK(rec.m_ownerId == kUserA);
    CHECK(rec.m_enterpriseId == kEnterprise);
    CHECK(rec.m_path == "docs/报告.docx");
    CHECK(rec.m_size == 1000);
    CHECK(rec.m_accessId == req.m_accessId);
    CHECK(rec.m_createTime == kNowMs);

    //多个条目：首个条目加「等 N 项」
    req.m_direction = share_transfer_upload;
    req.m_items.push_back(Item("pics", true, 5000));
    req.m_items.push_back(Item("b.txt", false, 10));
    req.m_totalBytes = 6010;
    rec = FileSharePolicy::buildTransferAuditRecord(share, kUserB, req, kNowMs);
    CHECK(rec.m_action == share_audit_request_upload);
    CHECK(rec.m_path == "docs/报告.docx 等 3 项");
    CHECK(rec.m_size == 6010);

    //超长的首个条目按字符边界截断，摘要不超过审计表路径列的上限
    req.m_items[0].m_path = std::string(1020, 'x') + "中文";
    rec = FileSharePolicy::buildTransferAuditRecord(share, kUserB, req, kNowMs);
    CHECK(rec.m_path.size() <= FileSharePolicy::kAuditPathMaxBytes);
    CHECK(rec.m_path.find("等 3 项") != std::string::npos);
}

TEST_CASE("SHT6 文件共享已关闭时打开与传输回 800")
{
    const ShareRecord share = GroupShare();
    ShareViewerFacts off = MemberFacts(share_perm_upload);
    off.m_featureEnabled = false;

    const ShareStateView view = FileSharePolicy::viewFor(share, off);
    CHECK(view.m_state == share_state_unavailable);
    CHECK(view.m_reason == share_unavailable_feature_disabled);
    CHECK(FileSharePolicy::checkOpen(share, off, SameEnterprise()) == error_code_share_disabled);
    CHECK(FileSharePolicy::checkTransfer(share, off, SameEnterprise(), share_transfer_download) == error_code_share_disabled);
    CHECK(FileSharePolicy::checkTransfer(share, off, SameEnterprise(), share_transfer_upload) == error_code_share_disabled);
}
