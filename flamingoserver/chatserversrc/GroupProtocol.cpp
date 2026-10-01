/**
 *  v2 群组报文编解码实现，GroupProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "GroupProtocol.h"
#include <set>
#include "../jsoncpp1.9.0/json.h"

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 里的字符串型 64 位 ID 取出；字段缺失或非字符串返回 0
static int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return 0;
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

//把一条群记录写入 Json 对象。get_group_info 应答、get_user_groups 应答、
//group_info_changed_notify 三处共用，改这一个函数三处一起变。
static void fillGroupValue(Json::Value& jo, const GroupRecord& g)
{
    jo["groupid"]      = std::to_string(g.groupid);
    jo["name"]         = g.name;
    jo["facetype"]     = g.facetype;
    jo["customface"]   = g.customface;
    jo["announcement"] = g.announcement;
    jo["ownerid"]      = std::to_string(g.ownerid);
    jo["joinmode"]     = g.joinmode;
    jo["muteall"]      = g.muteall;
    jo["membercount"]  = g.membercount;

    //以下七项为群管理功能新增的群设置。老客户端不认识这些字段、直接忽略，
    //故加在这里不影响它们。
    jo["intro"]         = g.intro;
    jo["atallscope"]    = g.atallscope;
    jo["newseehistory"] = g.newseehistory;
    jo["memberinvite"]  = g.memberinvite;
    jo["welcomeon"]     = g.welcomeon;
    jo["welcometext"]   = g.welcometext;
    jo["sensitiveon"]   = g.sensitiveon;

    //以下三项为群公告功能新增，是"最新一条已发布公告"的冗余快照。加在这里的收益
    //是：客户端拿到群资料的同时就有了最新公告的标题与时间，群聊窗头部那行
    //「📌 公告摘要」不必再单独拉一次公告列表。announcement 字段（上面已写）此时
    //存的是这条公告的正文。
    jo["announceid"]    = std::to_string(g.announceid);
    jo["announcetitle"] = g.announcetitle;
    jo["announcetime"]  = static_cast<Json::Int64>(g.announcetime);
}

bool GroupProtocol::parseCreateGroupRequest(const std::string& json, CreateGroupRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //群名称字段必须带上；值可以为空串，表示由服务端按成员昵称生成自动群名
    //（见 GroupAutoName.h），此后群名随成员变化重算
    if (!root["groupname"].isString())
        return false;
    req.name = root["groupname"].asString();

    //初始成员列表可缺省（允许建一个只有群主的群）
    const Json::Value& arr = root["memberids"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            if (arr[i].isString())
                req.memberids.push_back(static_cast<int64_t>(atoll(arr[i].asString().c_str())));
        }
    }
    return true;
}

bool GroupProtocol::parseAdminCreateGroupRequest(const std::string& json, AdminCreateGroupRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //群名称必填且非空
    if (!root["groupname"].isString())
        return false;
    req.name = root["groupname"].asString();
    if (req.name.empty())
        return false;

    //群主userid 以字符串形式传入；缺失时取 0，由 validateAdminCreateGroup 拒绝
    req.ownerid = getStringId(root, "ownerid");

    //管理员列表（字符串数组），缺省视为空
    const Json::Value& admins = root["adminids"];
    if (admins.isArray())
    {
        for (Json::ArrayIndex i = 0; i < admins.size(); ++i)
        {
            if (admins[i].isString())
                req.adminids.push_back(static_cast<int64_t>(atoll(admins[i].asString().c_str())));
        }
    }

    //普通成员列表（字符串数组），缺省视为空
    const Json::Value& members = root["memberids"];
    if (members.isArray())
    {
        for (Json::ArrayIndex i = 0; i < members.size(); ++i)
        {
            if (members[i].isString())
                req.memberids.push_back(static_cast<int64_t>(atoll(members[i].asString().c_str())));
        }
    }
    return true;
}

bool GroupProtocol::validateAdminCreateGroup(const AdminCreateGroupRequest& req,
                                             std::vector<int64_t>& outAdmins,
                                             std::vector<int64_t>& outMembers)
{
    outAdmins.clear();
    outMembers.clear();

    //群主必填且为正
    if (req.ownerid <= 0)
        return false;

    //先收集去重后的成员集合，用于校验"管理员必须是已选成员"
    std::set<int64_t> memberSet;
    for (size_t i = 0; i < req.memberids.size(); ++i)
    {
        if (req.memberids[i] > 0)
            memberSet.insert(req.memberids[i]);
    }

    //规整管理员：群主出现在管理员列表里则忽略（群主优先级更高）；
    //其余管理员必须出现在成员集内，否则视为非法请求。按首次出现顺序去重。
    std::set<int64_t> adminSet;
    for (size_t i = 0; i < req.adminids.size(); ++i)
    {
        int64_t aid = req.adminids[i];
        if (aid <= 0)
            continue;
        if (aid == req.ownerid)
            continue;
        if (memberSet.count(aid) == 0)
            return false;
        if (adminSet.insert(aid).second)
            outAdmins.push_back(aid);
    }

    //规整普通成员：剔除群主与管理员，按首次出现顺序去重
    std::set<int64_t> emitted;
    for (size_t i = 0; i < req.memberids.size(); ++i)
    {
        int64_t mid = req.memberids[i];
        if (mid <= 0)
            continue;
        if (mid == req.ownerid)
            continue;
        if (adminSet.count(mid) > 0)
            continue;
        if (emitted.insert(mid).second)
            outMembers.push_back(mid);
    }

    return true;
}

bool GroupProtocol::parseGroupIdRequest(const std::string& json, int64_t& groupid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    groupid = getStringId(root, "groupid");
    return groupid > 0;
}

bool GroupProtocol::parseUpdateGroupInfoRequest(const std::string& json, UpdateGroupInfoRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    //以下字段均可选，仅出现的字段会被更新。
    //name 为空串也算带了群名，表示恢复为自动群名，由 GroupManager::updateGroupInfo 处理
    if (root["name"].isString())
    {
        req.has_name = true;
        req.name     = root["name"].asString();
    }
    if (root["facetype"].isInt())
    {
        req.has_facetype = true;
        req.facetype     = root["facetype"].asInt();
    }
    if (root["customface"].isString())
    {
        req.has_customface = true;
        req.customface     = root["customface"].asString();
    }
    if (root["announcement"].isString())
    {
        req.has_announcement = true;
        req.announcement     = root["announcement"].asString();
    }
    if (root["joinmode"].isInt())
    {
        req.has_joinmode = true;
        req.joinmode     = root["joinmode"].asInt();
    }
    if (root["muteall"].isInt())
    {
        req.has_muteall = true;
        req.muteall     = root["muteall"].asInt();
    }

    //---- 以下七项为群管理功能新增，同样是可选字段 ----
    if (root["intro"].isString())
    {
        req.has_intro = true;
        req.intro     = root["intro"].asString();
    }
    if (root["atallscope"].isInt())
    {
        req.has_atallscope = true;
        req.atallscope     = root["atallscope"].asInt();
    }
    if (root["newseehistory"].isInt())
    {
        req.has_newseehistory = true;
        req.newseehistory     = root["newseehistory"].asInt();
    }
    if (root["memberinvite"].isInt())
    {
        req.has_memberinvite = true;
        req.memberinvite     = root["memberinvite"].asInt();
    }
    if (root["welcomeon"].isInt())
    {
        req.has_welcomeon = true;
        req.welcomeon     = root["welcomeon"].asInt();
    }
    if (root["welcometext"].isString())
    {
        req.has_welcometext = true;
        req.welcometext     = root["welcometext"].asString();
    }
    if (root["sensitiveon"].isInt())
    {
        req.has_sensitiveon = true;
        req.sensitiveon     = root["sensitiveon"].asInt();
    }
    return true;
}

std::string GroupProtocol::buildCreateGroupResponse(int code, const std::string& msg, int64_t groupid)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["groupid"] = std::to_string(groupid);
    return writeCompact(root);
}

std::string GroupProtocol::buildGroupInfoResponse(int code, const std::string& msg,
                                                  const GroupRecord& group, int myrole)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    fillGroupValue(root, group);
    //请求者在该群的角色
    root["myrole"] = myrole;
    return writeCompact(root);
}

std::string GroupProtocol::buildGroupMembersResponse(int code, const std::string& msg,
                                                     int64_t groupid,
                                                     const std::vector<GroupMemberRecord>& members)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["groupid"] = std::to_string(groupid);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < members.size(); ++i)
    {
        const GroupMemberRecord& m = members[i];
        Json::Value jo;
        jo["userid"]     = std::to_string(m.userid);
        jo["role"]       = m.role;
        jo["card"]       = m.card;
        jo["muted"]      = m.muted;
        jo["nickname"]   = m.nickname;
        jo["facetype"]   = m.facetype;
        jo["customface"] = m.customface;
        jo["jointime"]   = static_cast<Json::Int64>(m.jointime);

        //禁言到期时间与原因（群管理功能新增）。muteuntil 为 0 且 muted 为 1
        //表示永久禁言；客户端据它显示"剩余 N 分钟"或"永久"。
        jo["muteuntil"]  = static_cast<Json::Int64>(m.muteuntil);
        jo["mutereason"] = m.mutereason;
        arr.append(jo);
    }
    root["members"] = arr;
    return writeCompact(root);
}

std::string GroupProtocol::buildUserGroupsResponse(int code, const std::string& msg,
                                                   const std::vector<GroupRecord>& groups,
                                                   const std::set<int64_t>& pinnedGroupIds)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;

    //groups 数组：逐群写群资料（与 get_group_info 同字段，复用 fillGroupValue）。
    //pinned 是本人对该群的置顶状态，因人而异，所以只在这里加，不放进 fillGroupValue ——
    //后者还要用于发给全群的 cmd_group_info_changed_notify。
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < groups.size(); ++i)
    {
        Json::Value jo;
        fillGroupValue(jo, groups[i]);
        jo["pinned"] = (pinnedGroupIds.count(groups[i].groupid) != 0) ? 1 : 0;
        arr.append(jo);
    }
    root["groups"] = arr;
    return writeCompact(root);
}

std::string GroupProtocol::buildGroupInfoChangedNotify(const GroupRecord& group, int changetype)
{
    Json::Value root;
    fillGroupValue(root, group);
    root["changetype"] = changetype;
    return writeCompact(root);
}

std::string GroupProtocol::buildGroupMemberChangedNotify(int64_t groupid, int changetype,
                                                         const std::vector<int64_t>& userids,
                                                         int64_t operatorId)
{
    Json::Value root;
    root["groupid"]    = std::to_string(groupid);
    root["changetype"] = changetype;
    root["operatorid"] = std::to_string(operatorId);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < userids.size(); ++i)
        arr.append(std::to_string(userids[i]));
    root["userids"] = arr;
    return writeCompact(root);
}

bool GroupProtocol::parseSensitiveWordOpRequest(const std::string& json,
                                                SensitiveWordOpRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    if (!root["operation"].isInt())
        return false;
    req.operation = root["operation"].asInt();

    //按操作类型校验各自必需的字段：新增要有非空的词，删除要有正的词条ID。
    //不认识的 operation 一律拒绝 —— 与其猜用户想干什么，不如让它失败得明显。
    if (req.operation == sensitive_word_add)
    {
        if (!root["word"].isString())
            return false;
        req.word = root["word"].asString();
        if (req.word.empty())
            return false;
        return true;
    }

    if (req.operation == sensitive_word_delete)
    {
        req.id = getStringId(root, "id");
        return req.id > 0;
    }

    return false;
}

std::string GroupProtocol::buildSensitiveWordsResponse(int code, const std::string& msg,
                                                       int64_t groupid,
                                                       const std::vector<GroupSensitiveWordRecord>& words)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["groupid"] = std::to_string(groupid);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < words.size(); ++i)
    {
        const GroupSensitiveWordRecord& w = words[i];
        Json::Value jo;
        jo["id"]        = std::to_string(w.id);
        jo["word"]      = w.word;
        jo["timestamp"] = static_cast<Json::Int64>(w.createtime);
        arr.append(jo);
    }
    root["words"] = arr;
    return writeCompact(root);
}

std::string GroupProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

// ====================================================================
// 群公告
// ====================================================================

//数一段 UTF-8 文本有多少个字符（码点）。
//只数首字节 —— UTF-8 的后续字节一律形如 10xxxxxx（即 & 0xC0 == 0x80），跳过它们
//剩下的就是字符数。非法字节序列不会让本函数越界，最差是数出一个偏大的值，而长度
//校验偏严一点是可以接受的。
static int utf8CharCount(const std::string& s)
{
    int count = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            ++count;
    }
    return count;
}

//去掉一段文本首尾的空白（空格、制表、回车、换行）。
//公告标题是必填项，而"一串空格"在界面上与空标题看不出区别，故先 trim 再判空。
static std::string trimWhitespace(const std::string& s)
{
    size_t begin = 0;
    while (begin < s.size()
           && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r' || s[begin] == '\n'))
    {
        ++begin;
    }

    size_t end = s.size();
    while (end > begin
           && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
    {
        --end;
    }
    return s.substr(begin, end - begin);
}

//把一条公告写入 Json 对象。应答与推送两处共用，改这一个函数两处一起变。
static void fillAnnouncementValue(Json::Value& jo, const GroupAnnouncementRecord& a)
{
    jo["id"]            = std::to_string(a.id);
    jo["groupid"]       = std::to_string(a.groupid);
    jo["title"]         = a.title;
    jo["content"]       = a.content;
    jo["severity"]      = a.severity;
    jo["publisher"]     = std::to_string(a.publisher);
    jo["publishername"] = a.publishername;
    jo["publishtime"]   = static_cast<Json::Int64>(a.publishtime);
}

bool GroupProtocol::parseGetAnnouncementsRequest(const std::string& json,
                                                 GetAnnouncementsRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    //分页两项都可缺省：缺 offset / count 时取 0，由数据层夹到一页的上限
    if (root["offset"].isInt())
        req.offset = root["offset"].asInt();
    if (root["count"].isInt())
        req.count = root["count"].asInt();

    return true;
}

bool GroupProtocol::parsePublishAnnouncementRequest(const std::string& json,
                                                    PublishAnnouncementRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    //severity 越界归为"通知"。它只影响客户端那枚徽章的显示，为一个不认识的档位
    //让整条发布失败不划算 —— 公告本身是有价值的内容，不该因此丢掉。
    if (root["severity"].isInt())
    {
        const int severity = root["severity"].asInt();
        if (severity == group_announcement_important || severity == group_announcement_urgent)
            req.severity = severity;
        else
            req.severity = group_announcement_normal;
    }

    //重新发布：只带原公告ID，标题正文由会话层读回原件补齐，这里不再校验它们。
    req.id = getStringId(root, "id");
    if (req.id > 0)
        return true;

    //发布新公告：标题必填（trim 后非空），标题与正文各自不得超过列宽
    if (!root["title"].isString())
        return false;
    req.title = trimWhitespace(root["title"].asString());
    if (req.title.empty())
        return false;
    if (utf8CharCount(req.title) > kAnnouncementTitleMaxChars)
        return false;

    if (root["content"].isString())
        req.content = root["content"].asString();
    if (utf8CharCount(req.content) > kAnnouncementContentMaxChars)
        return false;

    return true;
}

bool GroupProtocol::parseDeleteAnnouncementRequest(const std::string& json,
                                                   int64_t& groupid, int64_t& id)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    groupid = getStringId(root, "groupid");
    id      = getStringId(root, "id");
    return groupid > 0 && id > 0;
}

std::string GroupProtocol::buildAnnouncementsResponse(int code, const std::string& msg,
                                                      int64_t groupid, int total,
                                                      int offset, int count,
                                                      const std::vector<GroupAnnouncementRecord>& items)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["groupid"] = std::to_string(groupid);
    root["total"]   = total;
    //原样回显请求的分页参数，供客户端认领属于自己那一份应答（见头文件说明）
    root["offset"]  = offset;
    root["count"]   = count;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value jo;
        fillAnnouncementValue(jo, items[i]);
        arr.append(jo);
    }
    root["announcements"] = arr;
    return writeCompact(root);
}

std::string GroupProtocol::buildPublishAnnouncementResponse(int code, const std::string& msg,
                                                            int64_t id)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    root["id"]   = std::to_string(id);
    return writeCompact(root);
}

std::string GroupProtocol::buildAnnouncementNotify(const GroupAnnouncementRecord& item,
                                                   int changetype)
{
    Json::Value root;
    root["changetype"] = changetype;
    root["groupid"]    = std::to_string(item.groupid);

    Json::Value jo;
    fillAnnouncementValue(jo, item);
    root["announcement"] = jo;
    return writeCompact(root);
}

bool GroupProtocol::isMuteExpired(int muted, int64_t muteUntil, int64_t nowMs)
{
    //没禁言、或永久禁言（到期时间为 0）都谈不上过期
    if (muted == 0 || muteUntil <= 0)
        return false;

    return nowMs >= muteUntil;
}

int64_t GroupProtocol::computeMuteUntil(int muted, int durationSeconds, int64_t nowMs)
{
    //解禁不需要到期时间；时长非正视为永久禁言，同样记 0
    if (muted == 0 || durationSeconds <= 0)
        return 0;

    const int64_t kMillisPerSecond = 1000;
    return nowMs + static_cast<int64_t>(durationSeconds) * kMillisPerSecond;
}

bool GroupProtocol::parseGroupMembersOpRequest(const std::string& json, GroupMembersOpRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    //userids 必填且非空
    const Json::Value& arr = root["userids"];
    if (!arr.isArray() || arr.size() == 0)
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i].isString())
            req.userids.push_back(static_cast<int64_t>(atoll(arr[i].asString().c_str())));
    }
    if (req.userids.empty())
        return false;
    return true;
}

bool GroupProtocol::parseJoinGroupRequest(const std::string& json, JoinGroupRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    //验证留言可缺省
    req.verifymsg = root["verifymsg"].isString() ? root["verifymsg"].asString() : "";
    return true;
}

bool GroupProtocol::parseHandleJoinGroupRequest(const std::string& json, HandleJoinGroupRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.requestid = getStringId(root, "requestid");
    if (req.requestid <= 0)
        return false;

    if (!root["action"].isInt())
        return false;
    req.action = root["action"].asInt();
    return true;
}

std::string GroupProtocol::buildJoinGroupResponse(int code, const std::string& msg, bool joined)
{
    Json::Value root;
    root["code"]   = code;
    root["msg"]    = msg;
    root["joined"] = joined;
    return writeCompact(root);
}

std::string GroupProtocol::buildJoinGroupRequestNotify(const GroupJoinRequestRecord& item)
{
    Json::Value root;
    root["requestid"]      = std::to_string(item.id);
    root["groupid"]        = std::to_string(item.groupid);
    root["fromuserid"]     = std::to_string(item.userid);
    root["fromnickname"]   = item.nickname;
    root["fromfacetype"]   = item.facetype;
    root["fromcustomface"] = item.customface;
    root["verifymsg"]      = item.verifymsg;
    root["timestamp"]      = static_cast<Json::Int64>(item.createtime);
    //来源与推荐人（群管理功能新增），供审批界面显示"来源：好友邀请（张三）"
    root["source"]         = item.source;
    root["recommender"]    = std::to_string(item.recommender);
    return writeCompact(root);
}

std::string GroupProtocol::buildGetJoinRequestsResponse(int code, const std::string& msg,
                                                        int64_t groupid,
                                                        const std::vector<GroupJoinRequestRecord>& items)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["groupid"] = std::to_string(groupid);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const GroupJoinRequestRecord& it = items[i];
        Json::Value jo;
        jo["requestid"]   = std::to_string(it.id);
        jo["userid"]      = std::to_string(it.userid);
        jo["nickname"]    = it.nickname;
        jo["facetype"]    = it.facetype;
        jo["customface"]  = it.customface;
        jo["verifymsg"]   = it.verifymsg;
        jo["timestamp"]   = static_cast<Json::Int64>(it.createtime);
        //来源与推荐人（群管理功能新增）
        jo["source"]      = it.source;
        jo["recommender"] = std::to_string(it.recommender);
        arr.append(jo);
    }
    root["requests"] = arr;
    return writeCompact(root);
}

bool GroupProtocol::parseGroupMemberValueRequest(const std::string& json, const char* valueKey,
                                                 GroupMemberValueRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    req.userid = getStringId(root, "userid");
    if (req.userid <= 0)
        return false;

    //整型值字段必填
    if (!root[valueKey].isInt())
        return false;
    req.value = root[valueKey].asInt();

    //禁言时长与原因为可选字段，仅 cmd_mute_member 会带；缺席即"永久禁言、不填原因"，
    //与扩展前只有三个字段时的行为一致，故老客户端的请求仍然合法。
    //负的 duration 视为没填 —— 不能让它算出一个早于现在的到期时间、变成"一发即过期"。
    if (root["duration"].isInt() && root["duration"].asInt() > 0)
        req.duration = root["duration"].asInt();
    if (root["reason"].isString())
        req.reason = root["reason"].asString();

    return true;
}

bool GroupProtocol::parseMuteAllRequest(const std::string& json, int64_t& groupid, int& muteall)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    groupid = getStringId(root, "groupid");
    if (groupid <= 0)
        return false;

    if (!root["muteall"].isInt())
        return false;
    muteall = root["muteall"].asInt();
    return true;
}

bool GroupProtocol::parseSetMemberCardRequest(const std::string& json,
                                              int64_t& groupid, std::string& card)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    groupid = getStringId(root, "groupid");
    if (groupid <= 0)
        return false;

    //群名片可为空串（清除群名片）
    card = root["card"].isString() ? root["card"].asString() : "";
    return true;
}

bool GroupProtocol::parseTransferOwnerRequest(const std::string& json,
                                              int64_t& groupid, int64_t& userid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    groupid = getStringId(root, "groupid");
    if (groupid <= 0)
        return false;

    userid = getStringId(root, "userid");
    return userid > 0;
}
