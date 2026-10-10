/**
 *  文件共享信令客户端编解码单元测试（Suite SPC，标签 [sharecodec]），ShareProtocolCodecTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 11 步的 11d，2026-10-02 与用户确认的用例）：
 *    - SPC1 2201~2230 各条请求的编码结果与共用样例逐字相同（服务端 FSP6 断言能解析同一批样例）；
 *    - SPC2 解析服务端构造的应答与推送样例（服务端 FSP6 断言构造结果与样例逐字相同）；
 *    - SPC3 超过 2^53 的 shareid 不丢精度；
 *    - SPC4 2222 的 p2psessionid（P2P 方案第 6 期）：带了就读出，没带为空。
 *  共用样例在 flamingoserver/tests/fileshare_wire_samples.h。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Engine/ShareProtocolCodec.h"
#include "../../../flamingoserver/tests/fileshare_wire_samples.h"

using namespace fileshare;

namespace {

//样例里用到的数值（与 fileshare_wire_samples.h 一致）
const int64_t kShareId = 42;
const int64_t kBigShareId = 9007199254740993LL;
const int64_t kGroupId = 900001;
const int64_t kOwnerId = 10001;
const int64_t kUserB = 10002;
const int64_t kUserC = 10003;
const int64_t kUserD = 10004;
const int64_t kUserE = 10005;
const int64_t kCreateTime = 1759370000000LL;
const int kCtypeSingle = 1;
const int kCtypeGroup = 2;
const int kKindFolder = 2;
const int kPermUpload = 1;
const int kPermMkdir = 2;
const int kPermUploadRename = 5;
const int kPermAll = 31;
const int kReasonRootMissing = 3;
const int kErrIoFail = 811;
const int kErrCrossEnterprise = 821;
const int kErrRelayBroken = 817;
const int kStateAvailable = 1;
const int kStateUnavailable = 2;
const int kStateInvalid = 3;
const int kReasonOwnerOffline = 1;
const int kReasonMemberRemoved = 6;
const int kActionRename = 7;
const int kRoleOwner = 9;
const int kRoleVisitor = 10;
const int kRelayPort = 20001;
const int kOpenTimeoutMs = 15000;
const int kIdleTimeoutMs = 600000;
const int kSearchMaxResults = 500;
const int kSearchTimeoutMs = 10000;
const int kTransferLanes = 2;
const int kDirectionDownload = 1;
const int kResultFailed = 1;
const int64_t kTotalBytes = 2100;
const int64_t kTotalFiles = 5;
const int64_t kDirBytes = 2000;
const int64_t kFileBytes = 100;
const char* const kAccessId = "share-3-1759370000000";
const char* const kTransferId = "share-3-1759370000000-t1";

ShareMemberPerm Member(int64_t userId, int perms)
{
    ShareMemberPerm m;
    m.m_userId = userId;
    m.m_perms = perms;
    return m;
}

} // namespace

TEST_CASE("SPC1 各条请求的编码结果与共用样例逐字相同", "[sharecodec]")
{
    ShareCreateParams create;
    create.m_clientShareId = "u-1";
    create.m_ctype = kCtypeGroup;
    create.m_cid = kGroupId;
    create.m_kind = kKindFolder;
    create.m_name = "Projects";
    create.m_displayPath = "D:\\Projects";
    create.m_includeHidden = true;
    create.m_members.push_back(Member(kUserB, kPermUpload));
    create.m_members.push_back(Member(kUserC, kPermAll));
    CHECK(ShareProtocolCodec::BuildCreateRequest(create) == kWireReqCreate);

    CHECK(ShareProtocolCodec::BuildShareIdRequest(kShareId) == kWireReqShareId);

    CHECK(ShareProtocolCodec::BuildUpdateMembersRequest(kShareId, std::vector<ShareMemberPerm>(1, Member(kUserD, kPermMkdir)),
                                                        std::vector<int64_t>(1, kUserC),
                                                        std::vector<ShareMemberPerm>(1, Member(kUserB, 0)))
          == kWireReqUpdateMembers);

    CHECK(ShareProtocolCodec::BuildListRequest(kShareListScopeSharedMe, kCtypeSingle, kOwnerId) == kWireReqListSharedMe);
    CHECK(ShareProtocolCodec::BuildListRequest(kShareListScopeMine, 0, 0) == kWireReqListMine);

    std::vector<ShareServeItem> serve(2);
    serve[0].m_shareId = kShareId;
    serve[0].m_available = true;
    serve[1].m_shareId = kShareId + 1;
    serve[1].m_available = false;
    serve[1].m_reason = kReasonRootMissing;
    CHECK(ShareProtocolCodec::BuildServeReport(serve) == kWireReqServeReport);

    CHECK(ShareProtocolCodec::BuildCloseAccessRequest(kAccessId, 0) == kWireReqCloseAccess);

    std::vector<ShareTransferEntry> items(2);
    items[0].m_path = "docs/a.txt";
    items[0].m_size = kFileBytes;
    items[1].m_path = "pics";
    items[1].m_isDir = true;
    items[1].m_size = kDirBytes;
    CHECK(ShareProtocolCodec::BuildTransferRequest(kAccessId, kDirectionDownload, items, kTotalBytes, kTotalFiles)
          == kWireReqTransfer);

    CHECK(ShareProtocolCodec::BuildTransferClose(kTransferId, kResultFailed, kErrIoFail) == kWireReqTransferClose);

    ShareAuditEntry audit;
    audit.m_shareId = kShareId;
    audit.m_accessId = kAccessId;
    audit.m_visitorId = kUserB;
    audit.m_action = kActionRename;
    audit.m_path = "a.txt";
    audit.m_toPath = "b.txt";
    audit.m_clientTime = kCreateTime;
    CHECK(ShareProtocolCodec::BuildAuditReport(std::vector<ShareAuditEntry>(1, audit)) == kWireReqAuditReport);
}

TEST_CASE("SPC2 解析服务端构造的应答与推送", "[sharecodec]")
{
    int code = -1;
    std::string msg;
    REQUIRE(ShareProtocolCodec::ParseSimpleResponse(kWireRespSimple, code, msg));
    CHECK(code == 0);
    CHECK(msg == "ok");

    ShareCreateResponse create;
    REQUIRE(ShareProtocolCodec::ParseCreateResponse(kWireRespCreateOk, create));
    CHECK(create.m_code == 0);
    CHECK(create.m_shareId == kShareId);
    CHECK(create.m_clientShareId == "u-1");
    REQUIRE(ShareProtocolCodec::ParseCreateResponse(kWireRespCreateRejected, create));
    CHECK(create.m_code == kErrCrossEnterprise);
    CHECK(create.m_shareId == 0);
    REQUIRE(create.m_invalidMembers.size() == 2);
    CHECK(create.m_invalidMembers[1] == kUserE);

    //2204：被拒时带不满足条件的新增成员，成功时与只有结果码的应答相同
    std::vector<int64_t> invalid;
    REQUIRE(ShareProtocolCodec::ParseUpdateMembersResponse(kWireRespUpdateMembersRejected, code, msg, invalid));
    CHECK(code == kErrCrossEnterprise);
    REQUIRE(invalid.size() == 1);
    CHECK(invalid[0] == kUserC);
    REQUIRE(ShareProtocolCodec::ParseUpdateMembersResponse(kWireRespSimple, code, msg, invalid));
    CHECK(code == 0);
    CHECK(invalid.empty());

    ShareListResponse mine;
    REQUIRE(ShareProtocolCodec::ParseListResponse(kWireRespListMine, mine));
    REQUIRE(mine.m_shares.size() == 1);
    const ShareListEntry& m = mine.m_shares[0];
    CHECK(m.m_shareId == kShareId);
    CHECK(m.m_ownerId == kOwnerId);
    CHECK(m.m_ctype == kCtypeGroup);
    CHECK(m.m_cid == kGroupId);
    CHECK(m.m_kind == kKindFolder);
    CHECK(m.m_name == "Projects");
    CHECK(m.m_displayPath == "D:\\Projects");
    CHECK(m.m_includeHidden);
    CHECK(m.m_state == kStateAvailable);
    CHECK(m.m_createTime == kCreateTime);
    CHECK(m.m_onThisDevice);
    REQUIRE(m.m_members.size() == 1);
    CHECK(m.m_members[0].m_userId == kUserB);
    CHECK(m.m_members[0].m_perms == kPermUpload);

    ShareListResponse shared;
    REQUIRE(ShareProtocolCodec::ParseListResponse(kWireRespListSharedMe, shared));
    REQUIRE(shared.m_shares.size() == 1);
    CHECK(shared.m_shares[0].m_myPerms == kPermUploadRename);
    CHECK(shared.m_shares[0].m_state == kStateUnavailable);
    CHECK(shared.m_shares[0].m_reason == kReasonOwnerOffline);
    CHECK_FALSE(shared.m_shares[0].m_includeHidden);
    //「共享给我的」不带分享者的真实路径（2026-10-04 起）
    CHECK(shared.m_shares[0].m_displayPath.empty());

    std::string accessId;
    REQUIRE(ShareProtocolCodec::ParseOpenResponse(kWireRespOpen, code, msg, accessId));
    CHECK(accessId == kAccessId);
    //2220 成功时带共享名称（2026-10-04 起）；不带时为空
    std::string openName;
    REQUIRE(ShareProtocolCodec::ParseOpenResponse(kWireRespOpen, code, msg, accessId, openName));
    CHECK(openName == "Projects");
    REQUIRE(ShareProtocolCodec::ParseOpenResponse("{\"code\":804,\"msg\":\"x\"}", code, msg, accessId, openName));
    CHECK(openName.empty());
    std::string transferId;
    REQUIRE(ShareProtocolCodec::ParseTransferResponse(kWireRespTransfer, code, msg, transferId));
    CHECK(transferId == kTransferId);

    int64_t shareId = 0;
    int state = 0;
    int reason = 0;
    REQUIRE(ShareProtocolCodec::ParseStateNotify(kWireNotifyState, shareId, state, reason));
    CHECK(shareId == kShareId);
    CHECK(state == kStateInvalid);
    CHECK(reason == kReasonMemberRemoved);

    int64_t userId = 0;
    int perms = 0;
    REQUIRE(ShareProtocolCodec::ParsePermsNotify(kWireNotifyPerms, shareId, userId, perms));
    CHECK(userId == kUserB);
    CHECK(perms == kPermUploadRename);

    ShareRelayParams owner;
    REQUIRE(ShareProtocolCodec::ParseRelayInfo(kWireNotifyRelayOwner, owner));
    CHECK(owner.m_forOwner);
    CHECK(owner.m_accessId == kAccessId);
    CHECK(owner.m_transferId.empty());
    CHECK(owner.m_relayIp == "10.0.0.9");
    CHECK(owner.m_relayPort == kRelayPort);
    CHECK(owner.m_token == "tok");
    CHECK(owner.m_role == kRoleOwner);
    CHECK(owner.m_lanes == 0);
    CHECK(owner.m_openTimeoutMs == kOpenTimeoutMs);
    CHECK(owner.m_idleTimeoutMs == kIdleTimeoutMs);
    CHECK(owner.m_shareId == kShareId);
    CHECK(owner.m_visitorId == kUserB);
    CHECK(owner.m_visitorName == "Bob");
    CHECK(owner.m_perms == kPermUpload);
    CHECK(owner.m_searchMaxResults == kSearchMaxResults);
    CHECK(owner.m_searchTimeoutMs == kSearchTimeoutMs);

    ShareRelayParams visitor;
    REQUIRE(ShareProtocolCodec::ParseRelayInfo(kWireNotifyRelayVisitor, visitor));
    CHECK_FALSE(visitor.m_forOwner);
    CHECK(visitor.m_transferId == kTransferId);
    CHECK(visitor.m_role == kRoleVisitor);
    CHECK(visitor.m_lanes == kTransferLanes);
    //传输子会话带传输方向（第 14 步补充），访问会话本身不带
    CHECK(visitor.m_direction == share_transfer_download);
    CHECK(owner.m_direction == 0);

    int errCode = 0;
    REQUIRE(ShareProtocolCodec::ParseAccessEnded(kWireNotifyAccessEnded, accessId, transferId, errCode));
    CHECK(accessId == kAccessId);
    CHECK(transferId.empty());
    CHECK(errCode == kErrRelayBroken);

    ShareCardInfo stored;
    REQUIRE(ShareProtocolCodec::ParseCardContent(kWireCardStored, stored));
    CHECK(stored.m_shareId == kShareId);
    CHECK(stored.m_ownerId == kOwnerId);
    CHECK(stored.m_name == "Projects");
    CHECK(stored.m_displayPath.empty());
    CHECK_FALSE(stored.m_hasState);
    CHECK_FALSE(stored.m_hasPerms);
    ShareCardInfo augmented;
    REQUIRE(ShareProtocolCodec::ParseCardContent(kWireCardAugmented, augmented));
    CHECK(augmented.m_hasState);
    CHECK(augmented.m_state == kStateAvailable);
    CHECK(augmented.m_hasPerms);
    CHECK(augmented.m_myPerms == kPermUploadRename);
    CHECK(augmented.m_displayPath.empty());
    //分享者本人收到的卡片带真实路径、不带权限
    ShareCardInfo ownerCard;
    REQUIRE(ShareProtocolCodec::ParseCardContent(kWireCardAugmentedOwner, ownerCard));
    CHECK(ownerCard.m_displayPath == "D:\\Projects");
    CHECK_FALSE(ownerCard.m_hasPerms);

    //畸形输入一律返回 false，不抛异常
    CHECK_FALSE(ShareProtocolCodec::ParseListResponse("not json", shared));
    CHECK_FALSE(ShareProtocolCodec::ParseListResponse("{\"code\":0,\"shares\":\"x\"}", shared));
    CHECK_FALSE(ShareProtocolCodec::ParseStateNotify("{\"state\":1}", shareId, state, reason));
    CHECK_FALSE(ShareProtocolCodec::ParseRelayInfo("{\"accessid\":1}", owner));
    CHECK_FALSE(ShareProtocolCodec::ParseCardContent("{\"shareid\":\"42\"}", stored));
}

TEST_CASE("SPC3 超过 2^53 的 shareid 不丢精度", "[sharecodec]")
{
    CHECK(ShareProtocolCodec::BuildShareIdRequest(kBigShareId) == kWireReqShareIdBig);

    int64_t shareId = 0;
    int state = 0;
    int reason = 0;
    REQUIRE(ShareProtocolCodec::ParseStateNotify(kWireNotifyStateBig, shareId, state, reason));
    CHECK(shareId == kBigShareId);

    //整数写法也接受（老格式或手工构造的报文）
    REQUIRE(ShareProtocolCodec::ParseStateNotify("{\"reason\":0,\"shareid\":42,\"state\":1}", shareId, state, reason));
    CHECK(shareId == kShareId);
}

TEST_CASE("SPC4 2222 的 p2psessionid：带了就读出，没带为空", "[sharecodec]")
{
    ShareRelayParams p2p;
    REQUIRE(ShareProtocolCodec::ParseRelayInfo(kWireNotifyRelayVisitorP2P, p2p));
    CHECK(p2p.m_transferId == kTransferId);
    CHECK(p2p.m_p2pSessionId == "p2p-10002-10001-1759370000000-7");
    CHECK(p2p.m_lanes == kTransferLanes);

    ShareRelayParams plain;
    REQUIRE(ShareProtocolCodec::ParseRelayInfo(kWireNotifyRelayVisitor, plain));
    CHECK(plain.m_p2pSessionId.empty());
}
