/**
 *  机器人运行环境的生产实现，BotServerEnv.cpp。设计见 BotServerEnv.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotServerEnv.h"

#include <time.h>
#include <map>
#include <set>

#include "ChatServer.h"
#include "ChatSession.h"
#include "DepartmentManager.h"
#include "DepartmentMsg.h"
#include "FeatureSwitchManager.h"
#include "GroupManager.h"
#include "GroupProtocol.h"
#include "GroupQaManager.h"
#include "GroupQaProtocol.h"
#include "Protocol.h"
#include "SensitiveWordDefs.h"
#include "UserManager.h"
#include "../utils/HttpsClient.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

namespace {

//外部服务应答体最多保留的字节数
const size_t kMaxResponseBytes = 65536;
//每秒的毫秒数
const int64_t kMsPerSecond = 1000;

} // anonymous namespace

BotServerEnv::BotServerEnv()
{
}

BotServerEnv::~BotServerEnv()
{
}

int64_t BotServerEnv::nowMs()
{
    return static_cast<int64_t>(time(NULL)) * kMsPerSecond;
}

bool BotServerEnv::groupInfo(int64_t groupid, int64_t& enterpriseId, std::string& name, bool& isThread)
{
    enterpriseId = 0;
    isThread = false;
    name.clear();
    GroupRecord group;
    if (!Singleton<GroupManager>::Instance().getGroup(groupid, group))
        return false;
    //群所属企业按群主所属企业认定（t_group 没有企业列）
    enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid);
    name = group.name;
    isThread = group.grouptype == group_type_thread;
    return true;
}

int64_t BotServerEnv::userEnterprise(int64_t userId)
{
    return Singleton<DepartmentManager>::Instance().getUserEnterpriseId(userId);
}

std::string BotServerEnv::displayName(int64_t userId)
{
    User u;
    u.userid = 0;
    if (Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(userId), u))
        return u.nickname;
    return std::string();
}

std::string BotServerEnv::groupDisplayName(int64_t groupid, int64_t userId)
{
    GroupMemberRecord member;
    if (Singleton<GroupManager>::Instance().getMember(groupid, static_cast<int32_t>(userId), member)
        && !member.card.empty())
        return member.card;
    return displayName(userId);
}

int BotServerEnv::presence(int64_t userId)
{
    return Singleton<ChatServer>::Instance().getMergedPresence(static_cast<int32_t>(userId));
}

void BotServerEnv::userDeptChain(int64_t userId, std::vector<int64_t>& out)
{
    out.clear();
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    const int64_t enterpriseId = deptMgr.getUserEnterpriseId(userId);
    if (enterpriseId <= 0)
        return;
    std::vector<dept::DepartmentInfo> depts;
    deptMgr.getOrgStructure(enterpriseId, depts);
    std::map<int64_t, int64_t> parentOf;
    std::vector<int64_t> direct;
    for (size_t i = 0; i < depts.size(); ++i)
    {
        parentOf[depts[i].deptid] = depts[i].parentdeptid;
        for (size_t k = 0; k < depts[i].members.size(); ++k)
        {
            if (depts[i].members[k].userid == userId)
                direct.push_back(depts[i].deptid);
        }
    }
    //从所在部门一路往上走到顶级部门；用集合去重，也防止数据异常时出现环
    std::set<int64_t> seen;
    for (size_t i = 0; i < direct.size(); ++i)
    {
        int64_t cur = direct[i];
        while (cur > 0 && seen.insert(cur).second)
        {
            out.push_back(cur);
            std::map<int64_t, int64_t>::const_iterator it = parentOf.find(cur);
            cur = it == parentOf.end() ? 0 : it->second;
        }
    }
}

void BotServerEnv::groupQa(int64_t groupid, int64_t enterpriseId, std::vector<bot::QaItem>& out)
{
    out.clear();
    GroupQaManager& qaMgr = Singleton<GroupQaManager>::Instance();
    std::vector<GroupQaRecord> groupItems;
    std::vector<GroupQaRecord> entItems;
    if (groupid > 0)
        qaMgr.listGroupQa(groupid, groupItems);
    qaMgr.listEnterpriseQaForGroup(enterpriseId, groupid, entItems);
    const std::vector<GroupQaRecord> merged = GroupQaProtocol::mergeForGroup(groupItems, entItems);
    for (size_t i = 0; i < merged.size(); ++i)
        out.push_back(bot::QaItem(merged[i].question, merged[i].answer));
}

void BotServerEnv::enterpriseGroups(int64_t enterpriseId, std::vector<int64_t>& out)
{
    out.clear();
    std::vector<std::pair<int64_t, std::pair<std::string, int64_t> > > all;
    if (!Singleton<GroupQaManager>::Instance().listAllGroups(all))
        return;
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (deptMgr.getUserEnterpriseId(all[i].second.second) == enterpriseId)
            out.push_back(all[i].first);
    }
}

bool BotServerEnv::featureOn(int64_t enterpriseId, const std::string& feature)
{
    return Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, feature);
}

int64_t BotServerEnv::postMessage(int ctype, int64_t from, int64_t target, const std::string& content,
                                  const std::string& clientMsgId)
{
    return Singleton<ChatServer>::Instance().postMessageAs(ctype, static_cast<int32_t>(from), target, message_type_text,
                                                           content, clientMsgId);
}

bool BotServerEnv::hitSensitive(int64_t enterpriseId, int64_t botUserId, const std::string& botName, int64_t groupid,
                                const std::string& text)
{
    std::vector<std::string> texts(1, text);
    return ChatSession::checkSensitiveForServer(enterpriseId, botUserId, botName, chatserver::sensitive_source_bot_external,
                                                conversation_group, groupid, texts);
}

bool BotServerEnv::httpPost(const std::string& url, const std::vector<std::pair<std::string, std::string> >& headers,
                            const std::string& body, int timeoutMs, int& status, std::string& respBody)
{
    httpclient::HttpResponse resp;
    const bool ok = httpclient::Post(url, headers, body, timeoutMs, kMaxResponseBytes, resp);
    status = resp.m_status;
    respBody = resp.m_body;
    if (!ok)
        LOGW("bot http post failed: %s", resp.m_error.c_str());
    return ok;
}
