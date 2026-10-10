/**
 *  文件共享的校验规则（服务端侧，纯函数）实现，FileSharePolicy.cpp
 *  balloonwj@qq.com   2026-10-02
 */
#include "FileSharePolicy.h"

#include "Protocol.h"
#include "../utils/RelayToken.h"   //role_share_owner / role_share_visitor

namespace {

//UTF-8 续字节的判定：与 0xC0 相与等于 0x80（即 10xxxxxx）的字节是多字节字符的后续字节
const unsigned char kUtf8ContinuationMask = 0xC0;
const unsigned char kUtf8ContinuationBits = 0x80;

//把 UTF-8 文本截断到不超过 maxBytes 字节；截断点落在多字节字符中间时整个字符一起舍去
std::string truncateUtf8(const std::string& text, size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return text;

    size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & kUtf8ContinuationMask) == kUtf8ContinuationBits)
        --end;
    return text.substr(0, end);
}

//查某人所属企业；facts 里没有这个人时按 0（不属于任何企业）处理
int64_t enterpriseOf(const ShareCheckFacts& facts, int64_t userId)
{
    std::map<int64_t, int64_t>::const_iterator it = facts.m_enterpriseOf.find(userId);
    if (it == facts.m_enterpriseOf.end())
        return 0;
    return it->second;
}

//检查一批新成员：不在会话里或就是分享者本人的放进 notInConversation，与分享者不同企业的放进
//otherEnterprise（两份名单都按 userid 升序）。返回应回的错误码：前一类优先回 819，其次回 821
int checkNewMembers(const std::vector<ShareMemberSpec>& members, int64_t ownerId, const ShareCheckFacts& facts,
                    std::vector<int64_t>& invalidMembers)
{
    std::set<int64_t> notInConversation;
    std::set<int64_t> otherEnterprise;
    for (size_t i = 0; i < members.size(); ++i)
    {
        const int64_t userId = members[i].m_userId;
        if (userId == ownerId || facts.m_conversationUsers.count(userId) == 0)
        {
            notInConversation.insert(userId);
            continue;
        }
        if (enterpriseOf(facts, userId) != facts.m_ownerEnterpriseId)
            otherEnterprise.insert(userId);
    }

    if (!notInConversation.empty())
    {
        invalidMembers.assign(notInConversation.begin(), notInConversation.end());
        return error_code_share_invalid_member;
    }
    if (!otherEnterprise.empty())
    {
        invalidMembers.assign(otherEnterprise.begin(), otherEnterprise.end());
        return error_code_share_cross_enterprise;
    }
    return error_code_ok;
}

} // namespace

//static const 整型成员被按引用使用（如测试里的 CHECK）时需要类外定义，否则 GCC 下链接失败
const size_t FileSharePolicy::kAuditPathMaxBytes;

int FileSharePolicy::checkCreate(const ShareCreateRequest& req, int64_t ownerId, const ShareCheckFacts& facts,
                                 std::vector<int64_t>& invalidMembers)
{
    invalidMembers.clear();

    //本企业的文件共享已被管理员关闭（docs/文件共享方案.md 5.8）
    if (!facts.m_featureEnabled)
        return error_code_share_disabled;

    //不属于任何企业的账号不能发起共享（文件共享禁止跨企业，没有企业就无从判定）
    if (facts.m_ownerEnterpriseId == 0)
        return error_code_share_cross_enterprise;

    const int memberCode = checkNewMembers(req.m_members, ownerId, facts, invalidMembers);
    if (memberCode != error_code_ok)
        return memberCode;

    if (facts.m_activeShareCount >= facts.m_maxSharesPerOwner)
        return error_code_share_too_many;
    return error_code_ok;
}

int FileSharePolicy::checkCancel(const ShareRecord& share, int64_t actorId, int groupRole, int& reason)
{
    reason = 0;
    if (share.m_state != FileShareManager::kShareRecordActive)
        return error_code_share_canceled;

    if (actorId == share.m_ownerId)
    {
        reason = share_invalid_owner_canceled;
        return error_code_ok;
    }

    //群里的共享，群主与管理员也能取消（见方案 5.1 的 2202 一行）
    if (share.m_ctype == conversation_group && groupRole >= group_role_admin)
    {
        reason = share_invalid_group_admin_canceled;
        return error_code_ok;
    }
    return error_code_permission_denied;
}

int FileSharePolicy::checkUpdateMembers(const ShareRecord& share, int64_t actorId, ShareUpdateMembersRequest& req,
                                        const ShareCheckFacts& facts, std::vector<int64_t>& invalidMembers)
{
    invalidMembers.clear();
    //本企业的文件共享已被管理员关闭：调整成员同样不允许（取消共享仍然允许，见 checkCancel）
    if (!facts.m_featureEnabled)
        return error_code_share_disabled;
    if (share.m_state != FileShareManager::kShareRecordActive)
        return error_code_share_canceled;
    if (actorId != share.m_ownerId)
        return error_code_permission_denied;

    //单聊共享只有对方一个成员，移除对方等于取消共享，应走 cmd_share_cancel
    if (share.m_ctype == conversation_single && !req.m_remove.empty())
        return error_code_bad_request;

    if (!req.m_add.empty())
    {
        if (facts.m_ownerEnterpriseId == 0)
            return error_code_share_cross_enterprise;
        const int memberCode = checkNewMembers(req.m_add, share.m_ownerId, facts, invalidMembers);
        if (memberCode != error_code_ok)
            return memberCode;
    }

    //单文件共享没有写操作可言，写权限一律清零（与 2201 的解析一致）
    if (share.m_kind == share_kind_file)
    {
        for (size_t i = 0; i < req.m_add.size(); ++i)
            req.m_add[i].m_perms = 0;
        for (size_t i = 0; i < req.m_update.size(); ++i)
            req.m_update[i].m_perms = 0;
    }
    return error_code_ok;
}

bool FileSharePolicy::acceptServeReport(const ShareRecord& share, int64_t reporterId, const std::string& reporterDeviceId)
{
    if (share.m_state != FileShareManager::kShareRecordActive)
        return false;
    if (reporterId != share.m_ownerId)
        return false;
    //设备标识为空无法证明是绑定的那台电脑，一律不接受
    if (reporterDeviceId.empty() || share.m_deviceId.empty())
        return false;
    return reporterDeviceId == share.m_deviceId;
}

std::vector<int64_t> FileSharePolicy::cardAudience(const std::vector<ShareMemberSpec>& members, int64_t ownerId)
{
    std::set<int64_t> users;
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].m_userId != ownerId)
            users.insert(members[i].m_userId);
    }
    return std::vector<int64_t>(users.begin(), users.end());
}

std::vector<int64_t> FileSharePolicy::pushTargets(const std::vector<int64_t>& audience, int64_t ownerId)
{
    std::set<int64_t> users(audience.begin(), audience.end());
    users.insert(ownerId);
    return std::vector<int64_t>(users.begin(), users.end());
}

ShareStateInput FileSharePolicy::buildStateInput(const ShareRecord& share, const ShareViewerFacts& facts)
{
    ShareStateInput in;
    in.m_canceled = (share.m_state != FileShareManager::kShareRecordActive);
    in.m_cancelReason = share.m_cancelReason;
    in.m_viewerIsOwner = (facts.m_viewerId == share.m_ownerId);

    //单聊共享的成员就是对方，没有「离开会话」一说
    const bool isGroup = (share.m_ctype == conversation_group);
    in.m_viewerInConversation = isGroup ? facts.m_inConversation : true;
    in.m_viewerIsMember = facts.m_isMember && in.m_viewerInConversation;

    in.m_featureEnabled = facts.m_featureEnabled;
    in.m_ownerOnline = facts.m_ownerOnline || facts.m_hasProvideState;
    in.m_ownerOnBoundDevice = facts.m_ownerOnBoundDevice || facts.m_hasProvideState;
    in.m_rootAvailable = facts.m_hasProvideState && facts.m_provideAvailable;
    return in;
}

ShareStateView FileSharePolicy::viewFor(const ShareRecord& share, const ShareViewerFacts& facts)
{
    return FileShareProtocol::computeShareState(buildStateInput(share, facts));
}

std::string FileSharePolicy::augmentCardFor(const std::string& content, const ShareRecord& share,
                                            const ShareViewerFacts& facts)
{
    const ShareStateInput in = buildStateInput(share, facts);
    const ShareStateView view = FileShareProtocol::computeShareState(in);
    //myperms 只给仍然有效的成员看；分享者本人与已被移出、已离群的人不带
    const bool includePerms = !in.m_viewerIsOwner && in.m_viewerIsMember;
    //真实路径只给分享者本人看，其余的人收到的卡片不带（2026-10-04 起）
    const std::string* ownerPath = in.m_viewerIsOwner ? &share.m_displayPath : NULL;
    return FileShareProtocol::augmentCardContent(content, view, includePerms, includePerms ? facts.m_perms : 0,
                                                 ownerPath);
}

int FileSharePolicy::checkOpen(const ShareRecord& share, const ShareViewerFacts& facts,
                               const ShareEnterprisePair& enterprise)
{
    //分享者本人在提供共享的电脑上直接打开，不经中转；访问会话的两端也不能是同一个人
    if (facts.m_viewerId == share.m_ownerId)
        return error_code_bad_request;

    const ShareStateView view = viewFor(share, facts);
    if (view.m_state == share_state_invalid)
    {
        if (share.m_state != FileShareManager::kShareRecordActive)
            return error_code_share_canceled;
        return error_code_share_not_member;
    }

    //成员中途被调到别的企业：禁止跨企业共享，再核对一次
    if (enterprise.m_viewerEnterpriseId == 0 || enterprise.m_viewerEnterpriseId != enterprise.m_ownerEnterpriseId)
        return error_code_share_cross_enterprise;

    if (view.m_state == share_state_unavailable)
    {
        if (view.m_reason == share_unavailable_feature_disabled)
            return error_code_share_disabled;
        return error_code_share_unavailable;
    }
    return error_code_ok;
}

int FileSharePolicy::checkTransfer(const ShareRecord& share, const ShareViewerFacts& facts,
                                   const ShareEnterprisePair& enterprise, int direction)
{
    const int code = checkOpen(share, facts, enterprise);
    if (code != error_code_ok)
        return code;

    if (direction == share_transfer_upload)
    {
        //单文件共享没有可以写入的目录，先于权限判定：即便权限位里有上传也不行
        if (share.m_kind == share_kind_file)
            return error_code_share_kind_mismatch;
        if ((facts.m_perms & share_perm_upload) == 0)
            return error_code_share_permission_denied;
    }
    return error_code_ok;
}

void FileSharePolicy::makeRelayInfos(const FileShareSessionManager& sessions, const ShareAccessInfo& access,
                                     const std::string& transferId, int direction, int perms,
                                     const std::string& visitorName, int64_t nowMs, ShareRelayInfo& toOwner,
                                     ShareRelayInfo& toVisitor)
{
    const FileShareSessionConfig& config = sessions.getConfig();
    const std::string relaySessionId = transferId.empty() ? access.m_accessId : transferId;
    std::string ownerToken;
    std::string visitorToken;
    sessions.signTokens(relaySessionId, access.m_ownerId, access.m_visitorId, nowMs, ownerToken, visitorToken);

    //两帧共有的部分
    ShareRelayInfo common;
    common.m_accessId = access.m_accessId;
    common.m_transferId = transferId;
    common.m_relayIp = config.m_relayHost;
    common.m_relayPort = config.m_relayPort;
    common.m_lanes = transferId.empty() ? 0 : config.m_lanes;
    common.m_openTimeoutMs = config.m_openTimeoutMs;
    common.m_idleTimeoutMs = config.m_idleTimeoutMs;
    common.m_direction = transferId.empty() ? 0 : direction;

    toVisitor = common;
    toVisitor.m_token = visitorToken;
    toVisitor.m_role = RelayToken::role_share_visitor;
    toVisitor.m_forOwner = false;

    //给分享者的那一帧另带访问者是谁、他的权限与搜索限制，分享者据此检查每个请求
    toOwner = common;
    toOwner.m_token = ownerToken;
    toOwner.m_role = RelayToken::role_share_owner;
    toOwner.m_forOwner = true;
    toOwner.m_shareId = access.m_shareId;
    toOwner.m_visitorId = access.m_visitorId;
    toOwner.m_visitorName = visitorName;
    toOwner.m_perms = perms;
    toOwner.m_searchMaxResults = config.m_searchMaxResults;
    toOwner.m_searchTimeoutMs = config.m_searchTimeoutMs;
}

ShareLeavePlan FileSharePolicy::planUserLeftGroup(const std::vector<ShareRecord>& shares,
                                                  const std::map<int64_t, std::vector<ShareMemberSpec> >& membersOf,
                                                  int64_t groupId, int64_t userId)
{
    std::set<int64_t> cancel;
    std::set<int64_t> removeFrom;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        const ShareRecord& share = shares[i];
        if (share.m_state != FileShareManager::kShareRecordActive)
            continue;
        if (share.m_ctype != conversation_group || share.m_cid != groupId)
            continue;

        if (share.m_ownerId == userId)
        {
            cancel.insert(share.m_shareId);
            continue;
        }

        std::map<int64_t, std::vector<ShareMemberSpec> >::const_iterator it = membersOf.find(share.m_shareId);
        if (it == membersOf.end())
            continue;
        for (size_t k = 0; k < it->second.size(); ++k)
        {
            if (it->second[k].m_userId == userId)
            {
                removeFrom.insert(share.m_shareId);
                break;
            }
        }
    }

    ShareLeavePlan plan;
    plan.m_cancelShareIds.assign(cancel.begin(), cancel.end());
    plan.m_removeFromShareIds.assign(removeFrom.begin(), removeFrom.end());
    return plan;
}

ShareSweepPlan FileSharePolicy::planGroupSweep(const std::vector<ShareRecord>& shares,
                                               const std::map<int64_t, std::vector<ShareMemberSpec> >& membersOf,
                                               const std::map<int64_t, std::set<int64_t> >& groupMembersOf)
{
    //按 shareid 收集，结果天然升序
    std::map<int64_t, int> cancels;
    ShareSweepPlan plan;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        const ShareRecord& share = shares[i];
        if (share.m_state != FileShareManager::kShareRecordActive || share.m_ctype != conversation_group)
            continue;

        std::map<int64_t, std::set<int64_t> >::const_iterator group = groupMembersOf.find(share.m_cid);
        if (group == groupMembersOf.end())
        {
            cancels[share.m_shareId] = share_invalid_group_dismissed;
            continue;
        }
        if (group->second.count(share.m_ownerId) == 0)
        {
            cancels[share.m_shareId] = share_invalid_owner_left_group;
            continue;
        }

        std::map<int64_t, std::vector<ShareMemberSpec> >::const_iterator members = membersOf.find(share.m_shareId);
        if (members == membersOf.end())
            continue;
        std::set<int64_t> gone;
        for (size_t k = 0; k < members->second.size(); ++k)
        {
            if (group->second.count(members->second[k].m_userId) == 0)
                gone.insert(members->second[k].m_userId);
        }
        if (!gone.empty())
            plan.m_removeMembers[share.m_shareId] = std::vector<int64_t>(gone.begin(), gone.end());
    }

    for (std::map<int64_t, int>::const_iterator it = cancels.begin(); it != cancels.end(); ++it)
    {
        ShareCancelAction action;
        action.m_shareId = it->first;
        action.m_reason = it->second;
        plan.m_cancels.push_back(action);
    }
    return plan;
}

bool FileSharePolicy::acceptAuditItem(const ShareRecord& share, int64_t reporterId)
{
    return reporterId == share.m_ownerId;
}

ShareAuditRecord FileSharePolicy::buildClientAuditRecord(const ShareRecord& share, const ShareAuditItem& item,
                                                         int64_t nowMs)
{
    ShareAuditRecord rec;
    rec.m_shareId = share.m_shareId;
    rec.m_ownerId = share.m_ownerId;
    rec.m_actorId = item.m_visitorId;
    rec.m_enterpriseId = share.m_enterpriseId;
    rec.m_action = item.m_action;
    rec.m_source = share_audit_source_owner_client;
    rec.m_path = truncateUtf8(item.m_path, kAuditPathMaxBytes);
    rec.m_toPath = truncateUtf8(item.m_toPath, kAuditPathMaxBytes);
    rec.m_size = item.m_size;
    rec.m_result = item.m_result;
    rec.m_accessId = item.m_accessId;
    rec.m_createTime = nowMs;
    return rec;
}

ShareAuditRecord FileSharePolicy::buildTransferAuditRecord(const ShareRecord& share, int64_t visitorId,
                                                           const ShareTransferRequest& req, int64_t nowMs)
{
    ShareAuditRecord rec;
    rec.m_shareId = share.m_shareId;
    rec.m_ownerId = share.m_ownerId;
    rec.m_actorId = visitorId;
    rec.m_enterpriseId = share.m_enterpriseId;
    rec.m_action = (req.m_direction == share_transfer_upload) ? share_audit_request_upload
                                                              : share_audit_request_download;
    rec.m_source = share_audit_source_server;
    rec.m_size = req.m_totalBytes;
    rec.m_accessId = req.m_accessId;
    rec.m_createTime = nowMs;

    const std::string first = req.m_items.empty() ? std::string() : req.m_items[0].m_path;
    if (req.m_items.size() <= 1)
    {
        rec.m_path = truncateUtf8(first, kAuditPathMaxBytes);
        return rec;
    }

    //多个条目：首个条目的路径加「等 N 项」，先给后缀留出位置再截断路径
    const std::string suffix = std::string(u8" 等 ") + std::to_string(static_cast<unsigned long long>(req.m_items.size()))
        + u8" 项";
    const size_t room = (kAuditPathMaxBytes > suffix.size()) ? (kAuditPathMaxBytes - suffix.size()) : 0;
    rec.m_path = truncateUtf8(first, room) + suffix;
    return rec;
}
