/**
 *  入群邀请卡片的纯逻辑实现，说明见 GroupInviteProtocol.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupInviteProtocol.h"
#include <stdlib.h>
#include "../jsoncpp1.9.0/json.h"

namespace {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 里的字符串型 64 位 ID 取出；字段缺失或非字符串返回 0
int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return 0;
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

} // namespace

std::string GroupInviteProtocol::buildCardContent(const GroupInviteRecord& rec, const GroupInviteCardNames& names)
{
    Json::Value root;
    root["inviteid"]   = std::to_string(rec.m_id);
    root["groupid"]    = std::to_string(rec.m_groupid);
    root["groupname"]  = names.m_groupName;
    root["facetype"]   = names.m_facetype;
    root["customface"] = names.m_customface;
    root["inviterid"]  = std::to_string(rec.m_inviter);
    root["inviter"]    = names.m_inviterName;
    root["inviteeid"]  = std::to_string(rec.m_invitee);
    root["invitee"]    = names.m_inviteeName;
    root["expiretime"] = static_cast<Json::Int64>(rec.m_expireTime);
    return writeCompact(root);
}

int64_t GroupInviteProtocol::parseCardInviteId(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root))
        return 0;
    return getStringId(root, "inviteid");
}

int GroupInviteProtocol::computeDisplayState(int storedState, int64_t expireTime, int64_t nowMs, bool groupExists,
                                             bool inviterIsMember, bool inviteeIsMember)
{
    //已加入、未通过审批是终态，原样显示
    if (storedState == group_invite_joined || storedState == group_invite_rejected)
        return storedState;

    //待加入与等待审批：被邀请人已经在群里（通过别的途径、或审批刚通过）都按已加入显示
    if (inviteeIsMember)
        return group_invite_joined;
    if (storedState == group_invite_approving)
        return group_invite_approving;

    //待加入：群没了或邀请人走了，邀请失效；否则看有没有过期
    if (!groupExists || !inviterIsMember)
        return group_invite_invalid;
    if (nowMs > expireTime)
        return group_invite_expired;
    return group_invite_pending;
}

std::string GroupInviteProtocol::augmentCardContent(const std::string& content, int state, bool groupExists,
                                                    const std::string& groupName, int facetype,
                                                    const std::string& customface, int memberCount)
{
    Json::Value root;
    if (!parseObject(content, root))
        return content;

    root["state"] = state;
    //群还在：用群的当前资料覆盖（群名、头像可能在邀请发出之后改过），并带上当前成员数
    if (groupExists)
    {
        root["groupname"]   = groupName;
        root["facetype"]    = facetype;
        root["customface"]  = customface;
        root["membercount"] = memberCount;
    }
    return writeCompact(root);
}

GroupInviteAcceptDecision GroupInviteProtocol::decideAccept(const GroupInviteAcceptFacts& facts)
{
    GroupInviteAcceptDecision d;
    d.m_action = group_invite_accept_reject;

    //邀请不存在、不是发给本人的、或已经处理过（加入过、在审批中、被拒绝过）
    if (!facts.m_found || facts.m_invite.m_invitee != facts.m_selfId
        || facts.m_invite.m_state != group_invite_pending)
    {
        d.m_errorCode = error_code_group_invite_invalid;
        return d;
    }
    //群已解散
    if (!facts.m_groupExists)
    {
        d.m_errorCode = error_code_group_invite_invalid;
        return d;
    }
    //本人已在群中（通过别的途径进来了）：会话层顺带把卡片记为已加入
    if (facts.m_selfIsMember)
    {
        d.m_errorCode = error_code_already_in_group;
        return d;
    }
    //超过有效期
    if (facts.m_nowMs > facts.m_invite.m_expireTime)
    {
        d.m_errorCode = error_code_group_invite_expired;
        return d;
    }
    //邀请人已退出群聊
    if (!facts.m_inviterIsMember)
    {
        d.m_errorCode = error_code_group_invite_invalid;
        return d;
    }

    //按点击这一刻邀请人的角色与群设置决定：群主、管理员或群允许普通成员直接拉人时直接入群，否则转为加群申请
    d.m_errorCode = 0;
    if (facts.m_inviterRole >= group_role_admin || facts.m_memberInviteFree)
        d.m_action = group_invite_accept_join;
    else
        d.m_action = group_invite_accept_approval;
    return d;
}

bool GroupInviteProtocol::parseAcceptRequest(const std::string& json, int64_t& inviteid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    inviteid = getStringId(root, "inviteid");
    return inviteid > 0;
}

std::string GroupInviteProtocol::buildAcceptResponse(int code, const std::string& msg, int64_t inviteid,
                                                     int64_t groupid, bool joined)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    if (inviteid > 0)
        root["inviteid"] = std::to_string(inviteid);
    if (groupid > 0)
        root["groupid"] = std::to_string(groupid);
    if (code == 0)
        root["joined"] = joined ? 1 : 0;
    return writeCompact(root);
}

std::string GroupInviteProtocol::buildChangedNotify(const GroupInviteRecord& rec, int state)
{
    Json::Value root;
    root["inviteid"]  = std::to_string(rec.m_id);
    root["msgid"]     = std::to_string(rec.m_msgid);
    root["inviterid"] = std::to_string(rec.m_inviter);
    root["inviteeid"] = std::to_string(rec.m_invitee);
    root["state"]     = state;
    return writeCompact(root);
}
