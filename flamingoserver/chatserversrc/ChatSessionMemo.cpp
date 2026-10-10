/**
 *  ChatSession 的「备忘与提醒」部分，ChatSessionMemo.cpp
 *
 *  处理 cmd_memo_list(2401) / cmd_memo_save(2402) / cmd_memo_delete(2403)，并在群成员与角色变化时
 *  补推 cmd_memo_notify(2404)。ChatSession.cpp 已近九千行，备忘相关的成员函数单独放在本文件，
 *  声明仍在 ChatSession.h。规则见 docs/备忘与提醒方案.md 第 3 节：
 *    - 个人备忘只有本人能增删改查；
 *    - 群备忘只有群主、管理员能增删改，对群主、管理员与被提醒的成员可见；
 *    - 推送给变更后能看到它的人，变更前能看到、变更后看不到的人收到删除。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "ChatSession.h"
#include <time.h>
#include <algorithm>
#include <vector>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "GroupProtocol.h"
#include "GroupManager.h"
#include "MemoProtocol.h"
#include "MemoManager.h"
#include "MessageManager.h"    //sendMemoCard：落库群备忘卡片
#include "MessageProtocol.h"   //sendMemoCard：落库外壳与 cmd_message_notify 推送体
#include "UserManager.h"       //sendMemoCard：取操作人的最新昵称
#include "SensitiveWordDefs.h"

//秒换算成毫秒
static const int64_t kMemoMillisPerSecond = 1000;

//当前时间，Unix 毫秒
static int64_t memoNowMs()
{
    return static_cast<int64_t>(time(NULL)) * kMemoMillisPerSecond;
}

//读出某群全部成员与角色；outMyRole 为 userid 在群里的角色，不是成员时为 -1
static bool loadMemoGroupMembers(int64_t groupid, int64_t userid,
                                 std::vector<MemoMemberRole>& out, int& outMyRole)
{
    out.clear();
    outMyRole = -1;
    std::vector<GroupMemberRecord> members;
    if (!Singleton<GroupManager>::Instance().getGroupMembers(groupid, members))
        return false;

    for (size_t i = 0; i < members.size(); ++i)
    {
        out.push_back(MemoMemberRole(members[i].userid, members[i].role));
        if (members[i].userid == userid)
            outMyRole = members[i].role;
    }
    return true;
}

//提醒对象是否都是本群成员
static bool memoTargetsAreMembers(const MemoRecord& memo, const std::vector<MemoMemberRole>& members)
{
    for (size_t i = 0; i < memo.targets.size(); ++i)
    {
        bool found = false;
        for (size_t j = 0; j < members.size(); ++j)
        {
            if (members[j].userid == memo.targets[i])
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

void ChatSession::onMemoList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    const std::vector<MemoRecord> kNoMemos;
    int64_t groupid = 0;
    if (!MemoProtocol::parseListRequest(body, groupid))
    {
        send(cmd_memo_list, m_seq, MemoProtocol::buildListResponse(error_code_bad_request, "bad request", kNoMemos));
        return;
    }

    const int64_t userid = m_userinfo.userid;
    if (groupid > 0 && Singleton<GroupManager>::Instance().getMemberRole(groupid, m_userinfo.userid) < 0)
    {
        send(cmd_memo_list, m_seq,
             MemoProtocol::buildListResponse(error_code_not_group_member, "not group member", kNoMemos));
        return;
    }

    MemoManager& memoMgr = Singleton<MemoManager>::Instance();
    std::vector<MemoRecord> result;
    //只拉某个群时不带个人备忘：群聊窗侧边栏「备忘」页签只显示本群的
    if (groupid == 0 && !memoMgr.listPersonalMemos(userid, result))
    {
        send(cmd_memo_list, m_seq,
             MemoProtocol::buildListResponse(error_code_server_internal, "list memo failed", kNoMemos));
        return;
    }
    const size_t personalCount = result.size();

    std::vector<MemoRecord> groupMemos;
    std::vector<int> roles;
    if (!memoMgr.listGroupMemosOfMember(userid, groupid, groupMemos, roles))
    {
        send(cmd_memo_list, m_seq,
             MemoProtocol::buildListResponse(error_code_server_internal, "list memo failed", kNoMemos));
        return;
    }
    for (size_t i = 0; i < groupMemos.size(); ++i)
    {
        if (MemoProtocol::isVisibleTo(groupMemos[i], userid, roles[i]))
            result.push_back(groupMemos[i]);
    }

    send(cmd_memo_list, m_seq, MemoProtocol::buildListResponse(error_code_ok, "ok", result));
    LOGI("memo list ok, userid: %d, groupid: %lld, personal: %u, group: %u",
         m_userinfo.userid, (long long)groupid, static_cast<unsigned>(personalCount),
         static_cast<unsigned>(result.size() - personalCount));
}

void ChatSession::onMemoSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    MemoRecord memo;
    if (!MemoProtocol::parseSaveRequest(body, memo))
    {
        send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_bad_request, "bad request", NULL));
        return;
    }

    const int normalized = MemoProtocol::normalizeMemo(memo);
    if (normalized != error_code_ok)
    {
        LOGI("memo save rejected, userid: %d, memoid: %lld, code: %d",
             m_userinfo.userid, (long long)memo.memoid, normalized);
        send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(normalized, "invalid memo", NULL));
        return;
    }

    const int64_t userid = m_userinfo.userid;
    GroupRecord group;
    std::vector<MemoMemberRole> members;
    int myrole = -1;
    if (memo.ctype == conversation_single)
    {
        memo.owner = userid;
    }
    else
    {
        GroupManager& groupMgr = Singleton<GroupManager>::Instance();
        if (!groupMgr.getGroup(memo.owner, group))
        {
            send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_group_not_exist, "group not exist", NULL));
            return;
        }
        if (!loadMemoGroupMembers(memo.owner, userid, members, myrole))
        {
            send(cmd_memo_save, m_seq,
                 MemoProtocol::buildSaveResponse(error_code_server_internal, "load group members failed", NULL));
            return;
        }
        if (myrole < group_role_admin)
        {
            send(cmd_memo_save, m_seq,
                 MemoProtocol::buildSaveResponse(error_code_not_group_admin, "only owner or admin can edit group memo", NULL));
            return;
        }
        if (!memoTargetsAreMembers(memo, members))
        {
            send(cmd_memo_save, m_seq,
                 MemoProtocol::buildSaveResponse(error_code_memo_target_invalid, "memo target is not group member", NULL));
            return;
        }
    }

    MemoManager& memoMgr = Singleton<MemoManager>::Instance();
    const bool isUpdate = (memo.memoid > 0);
    MemoRecord old;
    if (isUpdate)
    {
        bool found = false;
        if (!memoMgr.getMemo(memo.memoid, old, found))
        {
            send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_server_internal, "load memo failed", NULL));
            return;
        }
        //所属不一致按「不存在」回：既不允许把备忘挪到别的群，也不向调用方透露别人的备忘是否存在
        if (!found || old.ctype != memo.ctype || old.owner != memo.owner)
        {
            send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_memo_not_exist, "memo not exist", NULL));
            return;
        }
    }
    else
    {
        int count = 0;
        if (!memoMgr.countMemos(memo.ctype, memo.owner, count))
        {
            send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_server_internal, "count memo failed", NULL));
            return;
        }
        const int limit = (memo.ctype == conversation_single) ? kMaxPersonalMemos : kMaxGroupMemos;
        if (count >= limit)
        {
            send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_memo_too_many, "too many memos", NULL));
            return;
        }
    }

    //企业级敏感词：群备忘会推给其他成员，标题与备注都要查；个人备忘只有本人能看，不查
    std::vector<std::string> sensitiveTexts;
    MemoProtocol::collectSensitiveTexts(memo, sensitiveTexts);
    if (!sensitiveTexts.empty()
        && hitSensitive(chatserver::sensitive_source_memo, conversation_group, memo.owner, group.name, sensitiveTexts))
    {
        send(cmd_memo_save, m_seq,
             MemoProtocol::buildSaveResponse(error_code_group_sensitive_word, "memo contains sensitive word", NULL));
        return;
    }

    const int64_t now = memoNowMs();
    memo.editor = userid;
    memo.updateTime = now;
    bool saved = false;
    if (isUpdate)
    {
        memo.creator = old.creator;
        memo.createTime = old.createTime;
        saved = memoMgr.updateMemo(memo);
    }
    else
    {
        memo.creator = userid;
        memo.createTime = now;
        saved = memoMgr.insertMemo(memo);
    }
    if (!saved)
    {
        send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_server_internal, "save memo failed", NULL));
        return;
    }

    send(cmd_memo_save, m_seq, MemoProtocol::buildSaveResponse(error_code_ok, "ok", &memo));

    std::vector<int64_t> before;
    if (isUpdate)
        MemoProtocol::collectViewers(old, members, before);
    std::vector<int64_t> after;
    MemoProtocol::collectViewers(memo, members, after);
    pushMemoChange(memo, before, after);

    //群备忘：在群里给接收者发一张卡片（2026-10-05 起）。个人备忘不发
    int64_t cardMsgId = 0;
    if (memo.ctype == conversation_group)
        cardMsgId = sendMemoCard(memo, isUpdate ? memo_card_modified : memo_card_created, after);

    LOGI("memo save ok, userid: %d, memoid: %lld, ctype: %d, owner: %lld, update: %d, viewers: %u, card msgid: %lld",
         m_userinfo.userid, (long long)memo.memoid, memo.ctype, (long long)memo.owner,
         isUpdate ? 1 : 0, static_cast<unsigned>(after.size()), (long long)cardMsgId);
}

void ChatSession::onMemoDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t memoid = 0;
    if (!MemoProtocol::parseDeleteRequest(body, memoid))
    {
        send(cmd_memo_delete, m_seq, MemoProtocol::buildDeleteResponse(error_code_bad_request, "bad request", 0));
        return;
    }

    MemoManager& memoMgr = Singleton<MemoManager>::Instance();
    MemoRecord memo;
    bool found = false;
    if (!memoMgr.getMemo(memoid, memo, found))
    {
        send(cmd_memo_delete, m_seq, MemoProtocol::buildDeleteResponse(error_code_server_internal, "load memo failed", memoid));
        return;
    }
    if (!found)
    {
        send(cmd_memo_delete, m_seq, MemoProtocol::buildDeleteResponse(error_code_memo_not_exist, "memo not exist", memoid));
        return;
    }

    const int64_t userid = m_userinfo.userid;
    std::vector<MemoMemberRole> members;
    int myrole = -1;
    if (memo.ctype == conversation_group && !loadMemoGroupMembers(memo.owner, userid, members, myrole))
    {
        send(cmd_memo_delete, m_seq,
             MemoProtocol::buildDeleteResponse(error_code_server_internal, "load group members failed", memoid));
        return;
    }

    if (!MemoProtocol::canEdit(memo, userid, myrole))
    {
        //别人的个人备忘、或不在其群里的群备忘，按「不存在」回；本群普通成员明确告诉他没有权限
        const bool knownToCaller = (memo.ctype == conversation_group && myrole >= group_role_member);
        send(cmd_memo_delete, m_seq,
             knownToCaller
                 ? MemoProtocol::buildDeleteResponse(error_code_not_group_admin, "only owner or admin can delete group memo", memoid)
                 : MemoProtocol::buildDeleteResponse(error_code_memo_not_exist, "memo not exist", memoid));
        return;
    }

    std::vector<int64_t> before;
    MemoProtocol::collectViewers(memo, members, before);

    if (!memoMgr.deleteMemo(memoid))
    {
        send(cmd_memo_delete, m_seq, MemoProtocol::buildDeleteResponse(error_code_server_internal, "delete memo failed", memoid));
        return;
    }

    send(cmd_memo_delete, m_seq, MemoProtocol::buildDeleteResponse(error_code_ok, "ok", memoid));

    const std::string notify = MemoProtocol::buildDeleteNotify(memoid);
    for (size_t i = 0; i < before.size(); ++i)
        sendToUser(static_cast<int32_t>(before[i]), cmd_memo_notify, notify);

    LOGI("memo delete ok, userid: %d, memoid: %lld, ctype: %d, owner: %lld, notified: %u",
         m_userinfo.userid, (long long)memoid, memo.ctype, (long long)memo.owner,
         static_cast<unsigned>(before.size()));
}

void ChatSession::pushMemoChange(const MemoRecord& memo, const std::vector<int64_t>& before,
                                 const std::vector<int64_t>& after)
{
    const std::string upsert = MemoProtocol::buildUpsertNotify(memo);
    for (size_t i = 0; i < after.size(); ++i)
        sendToUser(static_cast<int32_t>(after[i]), cmd_memo_notify, upsert);

    std::vector<int64_t> removed;
    MemoProtocol::collectRemovedViewers(before, after, removed);
    if (removed.empty())
        return;
    const std::string del = MemoProtocol::buildDeleteNotify(memo.memoid);
    for (size_t i = 0; i < removed.size(); ++i)
        sendToUser(static_cast<int32_t>(removed[i]), cmd_memo_notify, del);
}

int64_t ChatSession::sendMemoCard(const MemoRecord& memo, int change, const std::vector<int64_t>& after)
{
    //操作人昵称按库里的最新值取：会话里缓存的是登录时的昵称，登录后改过名就过时了；查不到时退回缓存
    std::string editorName = m_userinfo.nickname;
    User editor;
    if (Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(memo.editor), editor))
        editorName = editor.nickname;

    const std::string content = MemoProtocol::buildMemoCardContent(memo, change, editorName);
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    const std::string clientMsgId = MemoProtocol::memoCardClientMsgId(memo.memoid, memo.updateTime);

    //可见名单：提醒全体成员时不限定范围（传 NULL），否则只写入修改后的可见者
    std::vector<int32_t> audience;
    const bool limited = MemoProtocol::memoCardAudience(memo, after, audience);

    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, conversation_group, static_cast<int32_t>(memo.editor), static_cast<int32_t>(memo.owner),
        message_type_memo, stored, memo.updateTime, limited ? &audience : NULL);
    if (msgId == 0)
    {
        LOGE("save memo card failed, groupid: %lld, memoid: %lld", (long long)memo.owner, (long long)memo.memoid);
        return 0;
    }

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype       = conversation_group;
    rec.from        = memo.editor;
    rec.to          = memo.owner;
    rec.msgtype     = message_type_memo;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = memo.updateTime;
    rec.recalled    = false;
    const std::string body = MessageProtocol::buildMessageNotify(rec, memo.owner);
    if (limited)
    {
        //只推给可见名单里的人；名单外的成员此刻收不到，之后同步、搜索也看不到（visibleToUserClause）
        for (size_t i = 0; i < after.size(); ++i)
            sendToUser(static_cast<int32_t>(after[i]), cmd_message_notify, body);
    }
    else
    {
        notifyGroupMembers(memo.owner, cmd_message_notify, body);
    }

    LOGI("memo card sent, groupid: %lld, memoid: %lld, msgid: %lld, change: %d, limited: %d, viewers: %u",
         (long long)memo.owner, (long long)memo.memoid, (long long)msgId, change, limited ? 1 : 0,
         static_cast<unsigned>(after.size()));
    return msgId;
}

void ChatSession::handleMemoMembersLeftGroup(int64_t groupid, const std::vector<MemoMemberRole>& leavers)
{
    if (leavers.empty())
        return;

    MemoManager& memoMgr = Singleton<MemoManager>::Instance();
    std::vector<MemoRecord> memos;
    if (!memoMgr.listGroupMemos(groupid, memos))
    {
        LOGE("memo member left: list group memos failed, groupid: %lld", (long long)groupid);
        return;
    }
    if (memos.empty())
        return;

    //离开的人按离开前的角色判断原先能不能看到，能看到的推删除
    std::vector<int64_t> leaverIds;
    for (size_t j = 0; j < leavers.size(); ++j)
        leaverIds.push_back(leavers[j].userid);
    for (size_t i = 0; i < memos.size(); ++i)
    {
        const std::string del = MemoProtocol::buildDeleteNotify(memos[i].memoid);
        for (size_t j = 0; j < leavers.size(); ++j)
        {
            if (MemoProtocol::isVisibleTo(memos[i], leavers[j].userid, leavers[j].role))
                sendToUser(static_cast<int32_t>(leavers[j].userid), cmd_memo_notify, del);
        }
    }

    if (!memoMgr.removeTargetsInGroup(groupid, leaverIds))
        LOGE("memo member left: remove targets failed, groupid: %lld", (long long)groupid);

    //提醒对象里去掉了人的那些备忘，给仍能看到它的人推一次修改，群主、管理员看到的名单随之更新
    std::vector<MemoMemberRole> members;
    int unusedRole = -1;
    if (!loadMemoGroupMembers(groupid, 0, members, unusedRole))
        return;
    for (size_t i = 0; i < memos.size(); ++i)
    {
        MemoRecord& memo = memos[i];
        const size_t oldSize = memo.targets.size();
        std::vector<int64_t> kept;
        for (size_t t = 0; t < memo.targets.size(); ++t)
        {
            if (std::find(leaverIds.begin(), leaverIds.end(), memo.targets[t]) == leaverIds.end())
                kept.push_back(memo.targets[t]);
        }
        if (kept.size() == oldSize)
            continue;
        memo.targets = kept;

        std::vector<int64_t> viewers;
        MemoProtocol::collectViewers(memo, members, viewers);
        const std::string upsert = MemoProtocol::buildUpsertNotify(memo);
        for (size_t v = 0; v < viewers.size(); ++v)
            sendToUser(static_cast<int32_t>(viewers[v]), cmd_memo_notify, upsert);
    }

    LOGI("memo member left handled, groupid: %lld, leavers: %u, memos: %u",
         (long long)groupid, static_cast<unsigned>(leavers.size()), static_cast<unsigned>(memos.size()));
}

void ChatSession::handleMemoMembersJoinedGroup(int64_t groupid, const std::vector<int64_t>& joiners)
{
    if (joiners.empty())
        return;

    std::vector<MemoRecord> memos;
    if (!Singleton<MemoManager>::Instance().listGroupMemos(groupid, memos))
    {
        LOGE("memo member joined: list group memos failed, groupid: %lld", (long long)groupid);
        return;
    }

    //新成员是普通成员，只能看到提醒全体成员的那些
    unsigned pushed = 0;
    for (size_t i = 0; i < memos.size(); ++i)
    {
        if (!memos[i].targetAll)
            continue;
        const std::string upsert = MemoProtocol::buildUpsertNotify(memos[i]);
        for (size_t j = 0; j < joiners.size(); ++j)
            sendToUser(static_cast<int32_t>(joiners[j]), cmd_memo_notify, upsert);
        ++pushed;
    }

    if (pushed > 0)
    {
        LOGI("memo member joined handled, groupid: %lld, joiners: %u, memos: %u",
             (long long)groupid, static_cast<unsigned>(joiners.size()), pushed);
    }
}

void ChatSession::handleMemoRoleChanged(int64_t groupid, int64_t userid, int oldRole, int newRole)
{
    //可见范围只取决于「是不是群主或管理员」，群主与管理员之间互换不影响
    if ((oldRole >= group_role_admin) == (newRole >= group_role_admin))
        return;

    std::vector<MemoRecord> memos;
    if (!Singleton<MemoManager>::Instance().listGroupMemos(groupid, memos))
    {
        LOGE("memo role changed: list group memos failed, groupid: %lld", (long long)groupid);
        return;
    }

    unsigned added = 0;
    unsigned removed = 0;
    for (size_t i = 0; i < memos.size(); ++i)
    {
        const bool before = MemoProtocol::isVisibleTo(memos[i], userid, oldRole);
        const bool after = MemoProtocol::isVisibleTo(memos[i], userid, newRole);
        if (!before && after)
        {
            sendToUser(static_cast<int32_t>(userid), cmd_memo_notify, MemoProtocol::buildUpsertNotify(memos[i]));
            ++added;
        }
        else if (before && !after)
        {
            sendToUser(static_cast<int32_t>(userid), cmd_memo_notify, MemoProtocol::buildDeleteNotify(memos[i].memoid));
            ++removed;
        }
    }

    LOGI("memo role changed handled, groupid: %lld, userid: %lld, role: %d -> %d, added: %u, removed: %u",
         (long long)groupid, (long long)userid, oldRole, newRole, added, removed);
}

void ChatSession::handleMemoGroupDismissed(int64_t groupid, const std::vector<MemoMemberRole>& members)
{
    MemoManager& memoMgr = Singleton<MemoManager>::Instance();
    std::vector<MemoRecord> memos;
    if (!memoMgr.listGroupMemos(groupid, memos))
    {
        LOGE("memo group dismissed: list group memos failed, groupid: %lld", (long long)groupid);
        return;
    }
    if (memos.empty())
        return;

    //群已解散、成员表已清空，可见范围按解散前的成员名单算
    for (size_t i = 0; i < memos.size(); ++i)
    {
        std::vector<int64_t> viewers;
        MemoProtocol::collectViewers(memos[i], members, viewers);
        const std::string del = MemoProtocol::buildDeleteNotify(memos[i].memoid);
        for (size_t v = 0; v < viewers.size(); ++v)
            sendToUser(static_cast<int32_t>(viewers[v]), cmd_memo_notify, del);
    }

    //删除失败时残留的行不会再被任何人读到：拉取经 t_group_member 关联，而解散后该群已没有成员行，
    //群号也不会被新群复用
    if (!memoMgr.deleteGroupMemos(groupid))
        LOGE("memo group dismissed: delete group memos failed, groupid: %lld", (long long)groupid);

    LOGI("memo group dismissed handled, groupid: %lld, memos: %u",
         (long long)groupid, static_cast<unsigned>(memos.size()));
}
