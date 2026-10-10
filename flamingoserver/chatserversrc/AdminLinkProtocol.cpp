/**
 *  管理控制台长连接报文编解码与纯规则的实现，AdminLinkProtocol.cpp
 *  说明见 AdminLinkProtocol.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "AdminLinkProtocol.h"

#include <algorithm>
#include <set>
#include <stdlib.h>

#include "Protocol.h"       //error_code 的取值
#include "DepartmentMsg.h"  //dept::DepartmentInfo / DepartmentMember
#include "../jsoncpp1.9.0/json.h"

namespace {

//部门成员记录里 isprimary 表示「主部门」的取值
const int32_t kPrimaryDeptFlag = 1;

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或不是对象时返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//判断一段文本是否全由十进制数字组成（空串返回 false）
bool isAllDigits(const std::string& text)
{
    if (text.empty())
        return false;

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    return true;
}

//按 clienttype 升序排列设备，供 std::sort 使用
bool deviceLess(const AdminLinkDevice& a, const AdminLinkDevice& b)
{
    return a.clienttype < b.clienttype;
}

//把一个用户条目写成 JSON 对象
Json::Value userToJson(const AdminLinkUser& user)
{
    Json::Value u(Json::objectValue);
    u["userid"]   = std::to_string(user.userid);
    u["account"]  = user.account;
    u["nickname"] = user.nickname;

    Json::Value depts(Json::arrayValue);
    for (size_t i = 0; i < user.depts.size(); ++i)
        depts.append(user.depts[i]);
    u["depts"] = depts;

    Json::Value devices(Json::arrayValue);
    for (size_t i = 0; i < user.devices.size(); ++i)
    {
        const AdminLinkDevice& d = user.devices[i];
        Json::Value jd(Json::objectValue);
        jd["clienttype"] = d.clienttype;
        jd["presence"]   = d.presence;
        jd["logintime"]  = static_cast<Json::Int64>(d.logintime);
        jd["ip"]         = d.ip;
        devices.append(jd);
    }
    u["devices"] = devices;
    return u;
}

//组一条 2504 包体：users 取 [begin, end) 区间
std::string buildOneNotify(int64_t enterpriseId, bool reset, const std::vector<AdminLinkUser>& users,
                           size_t begin, size_t end)
{
    Json::Value root(Json::objectValue);
    root["enterpriseid"] = std::to_string(enterpriseId);
    root["reset"] = reset ? 1 : 0;

    Json::Value arr(Json::arrayValue);
    for (size_t i = begin; i < end; ++i)
        arr.append(userToJson(users[i]));
    root["users"] = arr;
    return writeCompact(root);
}

} // anonymous namespace

bool AdminLinkProtocol::parseAuthRequest(const std::string& json, std::string& outToken,
                                         std::string& outClientVersion)
{
    outToken.clear();
    outClientVersion.clear();

    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!root["token"].isString())
        return false;

    const std::string token = root["token"].asString();
    if (token.empty() || token.size() > kAdminLinkTokenMaxBytes)
        return false;

    outToken = token;
    if (root["clientversion"].isString())
        outClientVersion = root["clientversion"].asString();
    return true;
}

std::string AdminLinkProtocol::buildAuthResponse(int32_t code, const char* msg, int64_t adminid,
                                                 int64_t enterpriseid)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"]  = (msg != NULL) ? msg : "";
    if (code == error_code_ok)
    {
        root["adminid"]      = std::to_string(adminid);
        root["enterpriseid"] = std::to_string(enterpriseid);
        root["heartbeat"]    = kAdminLinkHeartbeatSec;
        root["timeout"]      = kAdminLinkIdleTimeoutSec;
    }
    return writeCompact(root);
}

bool AdminLinkProtocol::parseSubscribeRequest(const std::string& json, int64_t& outEnterpriseId)
{
    outEnterpriseId = 0;

    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!root["enterpriseid"].isString())
        return false;

    const std::string text = root["enterpriseid"].asString();
    if (!isAllDigits(text))
        return false;

    const int64_t id = static_cast<int64_t>(atoll(text.c_str()));
    if (id <= 0)
        return false;

    outEnterpriseId = id;
    return true;
}

std::string AdminLinkProtocol::buildSubscribeResponse(int32_t code, const char* msg, int64_t enterpriseId)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"]  = (msg != NULL) ? msg : "";
    if (code == error_code_ok)
        root["enterpriseid"] = std::to_string(enterpriseId);
    return writeCompact(root);
}

std::string AdminLinkProtocol::buildSimpleResponse(int32_t code, const char* msg)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"]  = (msg != NULL) ? msg : "";
    return writeCompact(root);
}

int32_t AdminLinkProtocol::checkSubscribePermission(int64_t adminEnterpriseId, int64_t requestedEnterpriseId,
                                                    bool enterpriseExists)
{
    //企业管理员只能看本企业。先判越权再判存在：否则企业管理员可以拿任意 ID 来试，
    //从「不存在」与「无权」两种回答的差别里探出别的企业 ID 是否存在
    if (adminEnterpriseId != 0 && adminEnterpriseId != requestedEnterpriseId)
        return error_code_adminlink_forbidden;

    if (!enterpriseExists)
        return error_code_adminlink_enterprise_not_exist;

    return error_code_ok;
}

void AdminLinkProtocol::collectOnlineUserIds(const std::vector<AdminLinkSessionFact>& facts,
                                             std::vector<int64_t>& outIds)
{
    std::set<int64_t> ids;
    for (size_t i = 0; i < facts.size(); ++i)
    {
        if (facts[i].userid > 0 && facts[i].loggedIn)
            ids.insert(facts[i].userid);
    }
    outIds.assign(ids.begin(), ids.end());
}

void AdminLinkProtocol::buildUsers(const std::vector<int64_t>& userids,
                                   const std::vector<AdminLinkSessionFact>& facts,
                                   const std::map<int64_t, AdminLinkUserProfile>& profiles,
                                   std::vector<AdminLinkUser>& outUsers)
{
    outUsers.clear();

    //先把相关会话按 userid 分组，避免对每个用户都扫一遍全部会话
    std::set<int64_t> wanted(userids.begin(), userids.end());
    std::map<int64_t, std::vector<AdminLinkDevice> > devicesByUser;
    for (size_t i = 0; i < facts.size(); ++i)
    {
        const AdminLinkSessionFact& f = facts[i];
        if (f.userid <= 0 || !f.loggedIn)
            continue;
        if (wanted.find(f.userid) == wanted.end())
            continue;

        AdminLinkDevice d;
        d.clienttype = f.clienttype;
        d.presence   = f.presence;
        d.logintime  = f.logintime;
        d.ip         = f.ip;
        devicesByUser[f.userid].push_back(d);
    }

    std::set<int64_t> emitted;
    for (size_t i = 0; i < userids.size(); ++i)
    {
        const int64_t uid = userids[i];
        if (uid <= 0 || emitted.find(uid) != emitted.end())
            continue;
        emitted.insert(uid);

        AdminLinkUser user;
        user.userid = uid;

        std::map<int64_t, AdminLinkUserProfile>::const_iterator pit = profiles.find(uid);
        if (pit != profiles.end())
        {
            user.account  = pit->second.account;
            user.nickname = pit->second.nickname;
            user.depts    = pit->second.depts;
        }

        std::map<int64_t, std::vector<AdminLinkDevice> >::iterator dit = devicesByUser.find(uid);
        if (dit != devicesByUser.end())
        {
            user.devices = dit->second;
            std::sort(user.devices.begin(), user.devices.end(), deviceLess);
        }

        outUsers.push_back(user);
    }
}

void AdminLinkProtocol::buildProfilesFromOrg(const std::vector<dept::DepartmentInfo>& depts,
                                             std::map<int64_t, AdminLinkUserProfile>& outProfiles)
{
    outProfiles.clear();

    for (size_t i = 0; i < depts.size(); ++i)
    {
        const dept::DepartmentInfo& d = depts[i];
        for (size_t j = 0; j < d.members.size(); ++j)
        {
            const dept::DepartmentMember& m = d.members[j];
            if (m.userid <= 0)
                continue;

            AdminLinkUserProfile& p = outProfiles[m.userid];
            //账号与昵称在每条成员记录里都一样，取第一次遇到的
            if (p.account.empty())
                p.account = m.username;
            if (p.nickname.empty())
                p.nickname = m.nickname;

            //同一部门只记一次（理论上同一部门不会出现两条同一成员的记录，这里只做防御）
            if (std::find(p.depts.begin(), p.depts.end(), d.deptname) != p.depts.end())
                continue;

            if (m.isprimary == kPrimaryDeptFlag)
                p.depts.insert(p.depts.begin(), d.deptname);
            else
                p.depts.push_back(d.deptname);
        }
    }
}

void AdminLinkProtocol::buildOnlineNotifyBodies(int64_t enterpriseId, bool snapshot,
                                                const std::vector<AdminLinkUser>& users, size_t usersPerFrame,
                                                std::vector<std::string>& outBodies)
{
    outBodies.clear();

    const size_t perFrame = (usersPerFrame == 0) ? 1 : usersPerFrame;

    //完整名单即使一个人都不在线也要发一帧 reset=1，让控制台清掉上一个企业留下的行
    if (users.empty())
    {
        if (snapshot)
            outBodies.push_back(buildOneNotify(enterpriseId, true, users, 0, 0));
        return;
    }

    for (size_t begin = 0; begin < users.size(); begin += perFrame)
    {
        size_t end = begin + perFrame;
        if (end > users.size())
            end = users.size();

        const bool reset = snapshot && (begin == 0);
        outBodies.push_back(buildOneNotify(enterpriseId, reset, users, begin, end));
    }
}

std::string AdminLinkProtocol::stripPort(const std::string& ipPort)
{
    const size_t pos = ipPort.rfind(':');
    if (pos == std::string::npos)
        return ipPort;
    return ipPort.substr(0, pos);
}

bool AdminLinkProtocol::isIdleTimeout(int64_t nowSec, int64_t lastRecvSec, int timeoutSec)
{
    return nowSec - lastRecvSec >= static_cast<int64_t>(timeoutSec);
}

bool AdminLinkProtocol::isAuthTimeout(int64_t nowSec, int64_t connectSec, bool authed, int timeoutSec)
{
    if (authed)
        return false;
    return nowSec - connectSec >= static_cast<int64_t>(timeoutSec);
}

void AdminLinkProtocol::resolveLoginLinkInfo(bool listening, int port, bool encryptionEnabled,
                                             const std::string& pubkeyBase64,
                                             int& outPort, std::string& outPubkey)
{
    outPort = 0;
    outPubkey.clear();

    if (!listening || port <= 0)
        return;

    outPort = port;
    if (encryptionEnabled)
        outPubkey = pubkeyBase64;
}
