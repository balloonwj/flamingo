/**
 *  v2 群组报文编解码实现，GroupProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "GroupProtocol.h"
#include <limits>
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

    //以下六项为群管理功能新增的群设置。老客户端不认识这些字段、直接忽略，
    //故加在这里不影响它们。（原先还有一项群敏感词过滤开关 sensitiveon，随群级
    //敏感词于 2026-10-02 删除，老客户端读不到时按默认值 0 处理）
    jo["intro"]         = g.intro;
    jo["atallscope"]    = g.atallscope;
    jo["newseehistory"] = g.newseehistory;
    jo["memberinvite"]  = g.memberinvite;
    jo["welcomeon"]     = g.welcomeon;
    jo["welcometext"]   = g.welcometext;

    //以下三项为群公告功能新增，是"最新一条已发布公告"的冗余快照。加在这里的收益
    //是：客户端拿到群资料的同时就有了最新公告的标题与时间，群聊窗头部那行
    //「📌 公告摘要」不必再单独拉一次公告列表。announcement 字段（上面已写）此时
    //存的是这条公告的正文。
    jo["announceid"]    = std::to_string(g.announceid);
    jo["announcetitle"] = g.announcetitle;
    jo["announcetime"]  = static_cast<Json::Int64>(g.announcetime);

    //入群必读公告ID（docs/群公告接入方案.md §14），0 表示没有；老客户端不认识，照常忽略
    jo["mustreadid"]    = std::to_string(g.mustreadid);

    //群类型（docs/帖子群方案.md 6.3）：0 普通群、1 帖子群。老客户端不认识，会把帖子群当普通群列出
    jo["grouptype"]     = g.grouptype;
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

    //群类型可缺省（老客户端不带），缺省为普通群；不认识的取值按报文非法处理（docs/帖子群方案.md 6.3）
    if (root.isMember("grouptype"))
    {
        if (!root["grouptype"].isInt())
            return false;
        req.grouptype = root["grouptype"].asInt();
        if (req.grouptype != group_type_normal && req.grouptype != group_type_thread)
            return false;
    }

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

    //---- 以下六项为群管理功能新增，同样是可选字段 ----
    //老客户端可能仍带着已删除的群敏感词开关 sensitiveon，这里不再识别，按未知字段忽略
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
    return true;
}

//本次提交了该字段、值与当前值不同、且不为空串时，把新值追加到 out
static void appendIfChanged(bool submitted, const std::string& newValue, const std::string& currentValue,
                            std::vector<std::string>& out)
{
    if (submitted && !newValue.empty() && newValue != currentValue)
        out.push_back(newValue);
}

void GroupProtocol::collectChangedGroupInfoTexts(const UpdateGroupInfoRequest& req, const GroupRecord& current,
                                                 std::vector<std::string>& out)
{
    //群名为空串表示恢复自动群名，appendIfChanged 对空串不追加，这里无需单独判断
    appendIfChanged(req.has_name, req.name, current.name, out);
    appendIfChanged(req.has_intro, req.intro, current.intro, out);
    appendIfChanged(req.has_welcometext, req.welcometext, current.welcometext, out);
    appendIfChanged(req.has_announcement, req.announcement, current.announcement, out);
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
        //入群必读待确认（docs/群公告接入方案.md §14）：1 表示还没确认，群主、管理员的界面据此加标记
        jo["mustread"]   = m.mustread;
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
                                                   int changetype,
                                                   int64_t cardMsgId,
                                                   const std::string& cardContent)
{
    Json::Value root;
    root["changetype"] = changetype;
    root["groupid"]    = std::to_string(item.groupid);

    Json::Value jo;
    fillAnnouncementValue(jo, item);
    root["announcement"] = jo;

    //公告卡片（2026-10-05 起）：删除推送带上卡片的 msgid 与改写后的 content。老客户端不认识这两个字段，行为不变
    if (cardMsgId > 0)
        root["cardmsgid"] = std::to_string(cardMsgId);
    Json::Value card;
    if (!cardContent.empty() && parseObject(cardContent, card))
        root["card"] = card;
    return writeCompact(root);
}

std::string GroupProtocol::announcementCardClientMsgId(int64_t groupid, int64_t announcementId)
{
    return std::string(kAnnouncementCardClientMsgIdPrefix) + std::to_string(groupid) + "-"
           + std::to_string(announcementId);
}

std::string GroupProtocol::buildAnnouncementCardContent(const GroupAnnouncementRecord& item, bool republish)
{
    Json::Value root;
    root["announcementid"] = std::to_string(item.id);
    root["groupid"]        = std::to_string(item.groupid);
    root["title"]          = item.title;
    //正文放全文：客户端点开卡片时详情窗直接取它，不必再向服务端请求
    root["body"]           = item.content;
    root["severity"]       = item.severity;
    root["publisherid"]    = std::to_string(item.publisher);
    root["publisher"]      = item.publishername;
    root["publishtime"]    = static_cast<Json::Int64>(item.publishtime);
    root["republish"]      = republish;
    //老客户端不认识本类型时取 text 画成文字气泡（客户端 MessageProtocolClient 解析 content.text 的分支）
    root["text"]           = std::string(u8"[群公告] ") + item.title;
    return writeCompact(root);
}

bool GroupProtocol::buildDeletedAnnouncementCardContent(const std::string& cardContent, std::string& out)
{
    Json::Value root;
    if (!parseObject(cardContent, root))
        return false;

    //去掉正文：公告的删除语义是彻底消失（docs/群公告接入方案.md §11.1），正文不能继续留在聊天记录里。
    //标题、优先级、发布人与时间保留，卡片据此显示「该公告已被删除」
    root.removeMember("body");
    root["deleted"] = true;
    const std::string title = root["title"].isString() ? root["title"].asString() : std::string();
    root["text"] = std::string(u8"[群公告已删除] ") + title;
    out = writeCompact(root);
    return true;
}

std::string GroupProtocol::buildOwnerTransferTip(const std::string& fromNickname,
                                                 const std::string& toNickname)
{
    if (!fromNickname.empty() && !toNickname.empty())
        return u8"「" + fromNickname + u8"」已将群主转让给「" + toNickname + u8"」";
    if (!toNickname.empty())
        return u8"群主已转让给「" + toNickname + u8"」";
    if (!fromNickname.empty())
        return u8"「" + fromNickname + u8"」已转让群主";
    return u8"群主已变更";
}

std::string GroupProtocol::buildWelcomeTip(const std::vector<std::string>& names, const std::string& welcome)
{
    //把新成员的昵称拼在欢迎语前面，形如"张三、李四 欢迎加入本群"；空昵称跳过，不让它污染这句话
    std::string joined;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i].empty())
            continue;
        if (!joined.empty())
            joined += u8"、";
        joined += names[i];
    }
    if (joined.empty())
        return welcome;
    return joined + u8" " + welcome;
}

//构造群系统提示的完整 content：{ text, tipkind, tipargs }（多语言第 5 期）。tipargs 恒为对象，可能为空对象
static std::string buildTipContent(const std::string& text, const char* tipkind, const Json::Value& tipargs)
{
    Json::Value root;
    root["text"]    = text;
    root["tipkind"] = tipkind;
    root["tipargs"] = tipargs;
    return writeCompact(root);
}

//参数非空时才写进 tipargs：取不到昵称等情况下客户端按缺省写法生成，与中文 text 的规则一致
static void setTipArg(Json::Value& args, const char* key, const std::string& value)
{
    if (!value.empty())
        args[key] = value;
}

std::string GroupProtocol::buildOwnerTransferTipContent(const std::string& fromNickname,
                                                        const std::string& toNickname)
{
    Json::Value args(Json::objectValue);
    setTipArg(args, "from", fromNickname);
    setTipArg(args, "to", toNickname);
    return buildTipContent(buildOwnerTransferTip(fromNickname, toNickname), kSystemTipOwnerTransfer, args);
}

std::string GroupProtocol::buildMuteAllTip(int muteall, const std::string& operatorNickname)
{
    if (muteall != 0)
    {
        if (operatorNickname.empty())
            return u8"已开启仅通知模式，仅群主和管理员可以发言";
        return u8"「" + operatorNickname + u8"」开启了仅通知模式，仅群主和管理员可以发言";
    }
    if (operatorNickname.empty())
        return u8"已关闭仅通知模式";
    return u8"「" + operatorNickname + u8"」关闭了仅通知模式";
}

std::string GroupProtocol::buildMuteAllTipContent(int muteall, const std::string& operatorNickname, int64_t operatorId)
{
    Json::Value args(Json::objectValue);
    setTipArg(args, "operator", operatorNickname);
    if (operatorId > 0)
        args["operatorid"] = std::to_string(operatorId);
    return buildTipContent(buildMuteAllTip(muteall, operatorNickname),
                           (muteall != 0) ? kSystemTipMuteAllOn : kSystemTipMuteAllOff, args);
}

std::string GroupProtocol::buildWelcomeTipContent(const std::vector<std::string>& names,
                                                  const std::string& welcome)
{
    Json::Value args(Json::objectValue);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (!names[i].empty())
            arr.append(names[i]);
    }
    args["names"]   = arr;
    args["welcome"] = welcome;
    return buildTipContent(buildWelcomeTip(names, welcome), kSystemTipGroupWelcome, args);
}

std::string GroupProtocol::buildMemberJoinTip(int64_t inviterId, const std::string& inviterNickname,
                                              const std::vector<JoinTipMember>& members)
{
    if (members.empty())
        return std::string();

    //名字只从前 kMemberJoinTipMaxNames 位里取，昵称为空的跳过；人数按全部新成员计
    std::string joined;
    size_t listed = 0;
    for (size_t i = 0; i < members.size() && i < kMemberJoinTipMaxNames; ++i)
    {
        if (members[i].m_nickname.empty())
            continue;
        if (!joined.empty())
            joined += u8"、";
        joined += u8"「" + members[i].m_nickname + u8"」";
        ++listed;
    }

    //新成员这一段：一个名字都列不出时只写人数；列出的少于实际人数时补「等 N 人」
    const std::string total = std::to_string(members.size());
    std::string who;
    if (listed == 0)
        who = total + u8" 位新成员";
    else if (listed < members.size())
        who = joined + u8"等 " + total + u8" 人";
    else
        who = joined;

    if (inviterId == 0)
        return who + u8"加入了群聊";

    //邀请人昵称取不到时退而用"有人"，与发布群公告的提示一致。新成员这一段以数字开头时，
    //「邀请」与数字之间留一个空格，与「等 N 人」里数字两侧留空格的写法一致
    const std::string inviter = inviterNickname.empty() ? std::string(u8"有人") : inviterNickname;
    const std::string gap = (listed == 0) ? std::string(" ") : std::string();
    return u8"「" + inviter + u8"」邀请" + gap + who + u8"加入了群聊";
}

std::string GroupProtocol::buildMemberJoinTipContent(int64_t inviterId, const std::string& inviterNickname,
                                                     const std::vector<JoinTipMember>& members)
{
    Json::Value args(Json::objectValue);
    if (inviterId != 0)
    {
        args["inviterid"] = std::to_string(inviterId);
        if (!inviterNickname.empty())
            args["inviter"] = inviterNickname;
    }

    //members 带全部新成员的 userid；nickname 只给前 kMemberJoinTipMaxNames 位中非空的，与正文列出的名单一致
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < members.size(); ++i)
    {
        Json::Value jo(Json::objectValue);
        jo["userid"] = std::to_string(members[i].m_userid);
        if (i < kMemberJoinTipMaxNames && !members[i].m_nickname.empty())
            jo["nickname"] = members[i].m_nickname;
        arr.append(jo);
    }
    args["members"] = arr;
    return buildTipContent(buildMemberJoinTip(inviterId, inviterNickname, members), kSystemTipMemberJoin, args);
}

bool GroupProtocol::isMemberJoinTipFor(const std::string& storedContent, int64_t userid)
{
    if (userid <= 0)
        return false;
    Json::Value root;
    if (!parseObject(storedContent, root))
        return false;
    //入库的 f_content 是 { content: {...}, mentions, mentionall } 的包装；没有包装时 root 本身就是 content
    const Json::Value& content = root["content"].isObject() ? root["content"] : root;
    if (!content["tipkind"].isString() || content["tipkind"].asString() != kSystemTipMemberJoin)
        return false;
    const Json::Value& members = content["tipargs"]["members"];
    if (!members.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        if (members[i].isObject() && getStringId(members[i], "userid") == userid)
            return true;
    }
    return false;
}

int64_t GroupProtocol::joinTipInviterForRequest(int source, int64_t recommender)
{
    if (recommender <= 0)
        return 0;
    //好友邀请与扫码 / 链接两种来源的推荐人都是把人带进群的那个人；本人搜索后申请的没有邀请人
    if (source == group_join_source_friend || source == group_join_source_qrcode)
        return recommender;
    return 0;
}

GroupHistoryBound GroupProtocol::computeHistoryBound(int role, int newseehistory, int64_t jointime,
                                                     int64_t historyfrom)
{
    GroupHistoryBound bound;
    //群主与管理员不受限：两个下界都保持 0
    if (role >= group_role_admin)
        return bound;

    if (newseehistory == 0)
        bound.m_minCreateTime = jointime;
    bound.m_minMsgId = historyfrom;
    return bound;
}

bool GroupProtocol::canMentionAll(int atallscope, int role)
{
    //群主与管理员不受群设置限制
    if (role >= group_role_admin)
        return true;
    //普通成员：群设为「仅群主和管理员」时不可；其余取值（目前只有「所有人」）都可以
    return atallscope != group_at_all_owner_admin;
}

std::string GroupProtocol::buildInviteToGroupResponse(int code, const std::string& msg, int added, int invited,
                                                      int pending)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["added"]   = added;
    root["invited"] = invited;
    root["pending"] = pending;
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

    //historylimit 只有邀请用到，可缺省；带了就必须是整数且在 [-1, kInviteHistoryLimitMax] 之内
    //（docs/邀请入群方案.md §4.2），否则整条请求判为非法，免得把一个看不懂的值当成「全部」悄悄放过
    if (root.isMember("historylimit"))
    {
        const Json::Value& v = root["historylimit"];
        if (!v.isInt())
            return false;
        const int limit = v.asInt();
        if (limit < kInviteHistoryLimitAll || limit > kInviteHistoryLimitMax)
            return false;
        req.historylimit = limit;
    }
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

std::string GroupProtocol::buildJoinRequestHandledNotify(int64_t groupid, int64_t requestid, int status,
                                                         int64_t handler, const std::string& handlerName)
{
    Json::Value root;
    root["groupid"]     = std::to_string(groupid);
    root["requestid"]   = std::to_string(requestid);
    root["status"]      = status;
    root["handler"]     = std::to_string(handler);
    root["handlername"] = handlerName;
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

bool GroupProtocol::parseSetGroupFollowRequest(const std::string& json, GroupFollowList& out)
{
    out = GroupFollowList();

    Json::Value root;
    if (!parseObject(json, root))
        return false;

    out.groupid = getStringId(root, "groupid");
    if (out.groupid <= 0)
        return false;

    //targets 必须出现且为数组；清空名单时传空数组，不能省略这个字段
    const Json::Value& arr = root["targets"];
    if (!arr.isArray())
        return false;

    std::set<int32_t> seen;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        int64_t id = 0;
        if (arr[i].isString())
            id = static_cast<int64_t>(atoll(arr[i].asString().c_str()));
        else if (arr[i].isInt())
            id = arr[i].asInt();

        //userid 是 int32 范围的正数；非法元素整单拒绝，不悄悄丢掉
        if (id <= 0 || id > std::numeric_limits<int32_t>::max())
            return false;

        const int32_t userid = static_cast<int32_t>(id);
        if (seen.insert(userid).second)
            out.targets.push_back(userid);
    }
    return true;
}

std::string GroupProtocol::buildGroupFollowChangedNotify(const GroupFollowList& list)
{
    Json::Value root;
    root["groupid"] = std::to_string(list.groupid);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.targets.size(); ++i)
        arr.append(std::to_string(list.targets[i]));
    root["targets"] = arr;
    return writeCompact(root);
}

bool GroupProtocol::parseSetMustReadRequest(const std::string& json, int64_t& groupid, int64_t& announcementId)
{
    groupid = 0;
    announcementId = 0;
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    groupid = getStringId(root, "groupid");
    if (groupid <= 0)
        return false;

    //公告ID 按字符串写；也接受整数。缺省或 0 表示取消
    if (root["announcementid"].isString())
        announcementId = static_cast<int64_t>(atoll(root["announcementid"].asString().c_str()));
    else if (root["announcementid"].isIntegral())
        announcementId = root["announcementid"].asInt64();
    return announcementId >= 0;
}

std::string GroupProtocol::buildGetMustReadResponse(int code, const std::string& msg, int64_t groupid,
                                                    int64_t mustreadid, bool pending,
                                                    const GroupAnnouncementRecord* item)
{
    Json::Value root;
    root["code"]       = code;
    root["msg"]        = msg;
    root["groupid"]    = std::to_string(groupid);
    root["mustreadid"] = std::to_string(mustreadid);
    root["pending"]    = pending;
    if (item != NULL)
    {
        Json::Value jo;
        fillAnnouncementValue(jo, *item);
        root["announcement"] = jo;
    }
    return writeCompact(root);
}

std::string GroupProtocol::buildMustReadCardContent(const GroupAnnouncementRecord& item)
{
    Json::Value root;
    if (!parseObject(buildAnnouncementCardContent(item, false), root))
        return std::string();
    //客户端据它把卡片标成「入群必读」，点开时按本人是否待确认显示「我已阅读」
    root["mustread"] = true;
    return writeCompact(root);
}
