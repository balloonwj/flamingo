/**
 *  v2 群组报文编解码（客户端侧）实现，GroupProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-02
 */
#include "GroupProtocolClient.h"
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "Protocol.h"                   // group_type：群对象的 grouptype 字段

namespace net {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本。
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID 以字符串编码进 JSON，避免数值精度丢失。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//把 JSON 文本解析成对象 Json::Value；非法 JSON 或顶层非对象返回 false。
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//从一个含群资料字段的 JSON 对象里填出 GroupInfoResult 的资料部分（不含 code/msg）。
//
//服务端的 cmd_get_group_info 应答与 cmd_group_info_changed_notify 推送共用同一个
//fillGroupValue 构造，两边字段完全一致，故客户端这边也共用一份读取，免得日后
//加字段时改了应答忘了推送。
static void fillGroupInfoFromValue(const Json::Value& root, GroupInfoResult& out)
{
    out.m_groupid      = jsonToInt64(root["groupid"]);
    out.m_name         = root["name"].isString() ? root["name"].asString() : std::string();
    out.m_announcement = root["announcement"].isString() ? root["announcement"].asString() : std::string();
    out.m_ownerid      = jsonToInt64(root["ownerid"]);
    out.m_joinmode     = root["joinmode"].isInt() ? root["joinmode"].asInt() : 0;
    out.m_muteall      = root["muteall"].isInt() ? root["muteall"].asInt() : 0;
    out.m_membercount  = root["membercount"].isInt() ? root["membercount"].asInt() : 0;
    out.m_facetype     = root["facetype"].isInt() ? root["facetype"].asInt() : 0;
    out.m_customface   = root["customface"].isString() ? root["customface"].asString() : std::string();

    //myrole 只有 cmd_get_group_info 应答才有 —— 那是发给某一个人的，服务端知道
    //该带谁的角色；cmd_group_info_changed_notify 推送是同一份报文发给全群，角色
    //因人而异，服务端不可能在里面带。字段缺失时<u>保留</u>构造函数里的 -1（未知），
    //而不是填 0 —— 填 0 等于断言"本人是普通成员"，会让收到推送的群主 / 管理员
    //被降级（曾表现为：群管理窗点保存后左侧只剩成员管理与基本信息两项）。
    if (root["myrole"].isInt())
        out.m_myrole = root["myrole"].asInt();

    //以下六项为群管理功能新增。老服务端不回这些字段，此时保留构造函数里设好的
    //默认值（与服务端各列 DEFAULT 一致），而不是一律填 0 —— 否则界面上会把
    //"新成员可看历史"错显成关闭。
    if (root["intro"].isString())
        out.m_intro = root["intro"].asString();
    if (root["atallscope"].isInt())
        out.m_atallscope = root["atallscope"].asInt();
    if (root["newseehistory"].isInt())
        out.m_newseehistory = root["newseehistory"].asInt();
    if (root["memberinvite"].isInt())
        out.m_memberinvite = root["memberinvite"].asInt();
    if (root["welcomeon"].isInt())
        out.m_welcomeon = root["welcomeon"].asInt();
    if (root["welcometext"].isString())
        out.m_welcometext = root["welcometext"].asString();

    //以下三项为群公告功能新增的"最新公告快照"。老服务端不回这些字段，此时保留
    //构造函数里的 0 / 空串（即"本群还没有公告"），界面上表现为不显示公告摘要行。
    if (root.isMember("announceid"))
        out.m_announceid = jsonToInt64(root["announceid"]);
    if (root["announcetitle"].isString())
        out.m_announcetitle = root["announcetitle"].asString();
    if (root.isMember("announcetime"))
        out.m_announcetime = jsonToInt64(root["announcetime"]);

    //入群必读的公告ID（2026-10-07 起）；老服务端不带时保留 0
    if (root.isMember("mustreadid"))
        out.m_mustreadid = jsonToInt64(root["mustreadid"]);

    //群类型（2026-10-07 起，docs/帖子群方案.md 6.3）；老服务端不带、或取值不认识时都按普通群
    if (root["grouptype"].isInt() && root["grouptype"].asInt() == group_type_thread)
        out.m_grouptype = group_type_thread;
}

std::string GroupProtocolClient::buildCreateGroupRequest(const std::string& groupname,
                                                         const std::vector<int64_t>& memberids,
                                                         int grouptype)
{
    Json::Value root;
    root["groupname"] = groupname;
    //群类型只在建帖子群时带上：普通群的请求与 2026-10-07 之前完全相同，老服务端照常处理
    if (grouptype == group_type_thread)
        root["grouptype"] = grouptype;
    //成员 userid 列表以字符串数组编码（避免大整数精度丢失）；不含创建者自己。
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < memberids.size(); ++i)
    {
        arr.append(std::to_string(memberids[i]));
    }
    root["memberids"] = arr;
    return writeCompact(root);
}

//全角空格 U+3000 的 UTF-8 编码，占 3 个字节。中文输入法下按空格键常常输入的是它。
static const char kFullWidthSpace[] = "\xE3\x80\x80";
static const size_t kFullWidthSpaceLen = sizeof(kFullWidthSpace) - 1;

//text 的 pos 处是否以全角空格开头
static bool startsWithFullWidthSpace(const std::string& text, size_t pos)
{
    return text.compare(pos, kFullWidthSpaceLen, kFullWidthSpace) == 0;
}

//单字节的半角空白：空格、制表符、回车、换行、垂直制表符、换页符
static bool isAsciiBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string GroupProtocolClient::normalizeGroupNameInput(const std::string& utf8Input)
{
    //从头往后跳过空白
    size_t begin = 0;
    while (begin < utf8Input.size())
    {
        if (isAsciiBlank(utf8Input[begin]))
            ++begin;
        else if (startsWithFullWidthSpace(utf8Input, begin))
            begin += kFullWidthSpaceLen;
        else
            break;
    }

    //从尾往前跳过空白；end 为结果的尾后下标
    size_t end = utf8Input.size();
    while (end > begin)
    {
        if (isAsciiBlank(utf8Input[end - 1]))
            --end;
        else if (end - begin >= kFullWidthSpaceLen
                 && startsWithFullWidthSpace(utf8Input, end - kFullWidthSpaceLen))
            end -= kFullWidthSpaceLen;
        else
            break;
    }
    return utf8Input.substr(begin, end - begin);
}

bool GroupProtocolClient::parseCreateGroupResponse(const std::string& jsonbody, CreateGroupResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //code 为必有字段，缺失或非整型视为非法应答。
    if (!root["code"].isInt())
        return false;

    resp.m_code = root["code"].asInt();
    resp.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //失败应答到此为止，groupid 不再解析。
    if (resp.m_code != 0)
        return true;

    //成功应答：groupid 以字符串编码。
    resp.m_groupid = jsonToInt64(root["groupid"]);
    return true;
}

std::string GroupProtocolClient::buildGroupIdRequest(int64_t groupid)
{
    Json::Value root;
    //群ID 以字符串编码，避免大整数精度丢失。
    root["groupid"] = std::to_string(groupid);
    return writeCompact(root);
}

std::string GroupProtocolClient::buildSetMemberCardRequest(int64_t groupid, const std::string& card)
{
    Json::Value root;
    //群ID 以字符串编码，避免大整数精度丢失；card 原样写入（UTF-8，允许空串清名片）。
    root["groupid"] = std::to_string(groupid);
    root["card"]    = card;
    return writeCompact(root);
}

bool GroupProtocolClient::parseCodeMsgResponse(const std::string& jsonbody, int& code, std::string& msg)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //code 为必有字段，缺失或非整型视为非法应答。
    if (!root["code"].isInt())
        return false;

    code = root["code"].asInt();
    msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    return true;
}

bool GroupProtocolClient::parseGroupInfoResponse(const std::string& jsonbody, GroupInfoResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //失败应答到此为止。
    if (out.m_code != 0)
        return true;

    fillGroupInfoFromValue(root, out);
    return true;
}

bool GroupProtocolClient::parseGroupMembersResponse(const std::string& jsonbody, GroupMembersResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    if (out.m_code != 0)
        return true;

    out.m_groupid = jsonToInt64(root["groupid"]);

    out.m_members.clear();
    const Json::Value& arr = root["members"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jm = arr[i];
            if (!jm.isObject())
                continue;

            GroupMemberItem m;
            m.m_userid     = jsonToInt64(jm["userid"]);
            m.m_role       = jm["role"].isInt() ? jm["role"].asInt() : 0;
            m.m_card       = jm["card"].isString() ? jm["card"].asString() : std::string();
            m.m_muted      = jm["muted"].isInt() ? jm["muted"].asInt() : 0;
            m.m_nickname   = jm["nickname"].isString() ? jm["nickname"].asString() : std::string();
            m.m_facetype   = jm["facetype"].isInt() ? jm["facetype"].asInt() : 0;
            m.m_customface = jm["customface"].isString() ? jm["customface"].asString() : std::string();
            //以下三项为群管理功能新增；老服务端不回时留 0，界面按"无入群时间 /
            //永久禁言 / 无原因"处理，不会误显示。
            m.m_jointime   = jsonToInt64(jm["jointime"]);
            m.m_muteuntil  = jsonToInt64(jm["muteuntil"]);
            m.m_mutereason = jm["mutereason"].isString() ? jm["mutereason"].asString() : std::string();
            //入群必读的确认状态（2026-10-07 起）；老服务端不带时为 0
            m.m_mustread   = jm["mustread"].isInt() ? jm["mustread"].asInt() : 0;

            //userid 非法的成员跳过。
            if (m.m_userid <= 0)
                continue;
            out.m_members.push_back(m);
        }
    }
    return true;
}

bool GroupProtocolClient::parseUserGroupsResponse(const std::string& jsonbody, UserGroupsResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    if (out.m_code != 0)
        return true;

    out.m_groups.clear();
    const Json::Value& arr = root["groups"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jg = arr[i];
            if (!jg.isObject())
                continue;

            UserGroupItem g;
            g.m_groupid     = jsonToInt64(jg["groupid"]);
            g.m_name        = jg["name"].isString() ? jg["name"].asString() : std::string();
            g.m_ownerid     = jsonToInt64(jg["ownerid"]);
            g.m_membercount = jg["membercount"].isInt() ? jg["membercount"].asInt() : 0;
            g.m_facetype    = jg["facetype"].isInt() ? jg["facetype"].asInt() : 0;
            //老服务端不带 customface 时取空串，配合 facetype 默认 0 即回落到自动头像。
            g.m_customface  = jg["customface"].isString() ? jg["customface"].asString() : std::string();
            //本人的置顶状态；老服务端不带时按未置顶处理。
            g.m_pinned      = (jg["pinned"].isInt() && jg["pinned"].asInt() != 0) ? 1 : 0;
            //群简介；老服务端不带时取空串，群聊窗头部那一行留空。
            g.m_intro       = jg["intro"].isString() ? jg["intro"].asString() : std::string();
            //仅通知模式开关；老服务端不带时按关闭处理。
            g.m_muteall     = (jg["muteall"].isInt() && jg["muteall"].asInt() != 0) ? 1 : 0;
            //群类型；老服务端不带、或取值不认识时都按普通群。
            g.m_grouptype   = (jg["grouptype"].isInt() && jg["grouptype"].asInt() == group_type_thread)
                              ? group_type_thread : group_type_normal;

            //groupid 非法的群跳过。
            if (g.m_groupid <= 0)
                continue;
            out.m_groups.push_back(g);
        }
    }
    return true;
}

// ====================================================================
// 群管理：请求构造
// ====================================================================

std::string GroupProtocolClient::buildGroupMembersOpRequest(int64_t groupid,
                                                            const std::vector<int64_t>& userids,
                                                            int historyLimit)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);

    //userid 列表以字符串数组编码，避免大整数精度丢失
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < userids.size(); ++i)
    {
        arr.append(std::to_string(userids[i]));
    }
    root["userids"] = arr;

    //同步全部（-1）时不带，与老客户端的请求相同；其余取值原样带上，由服务端校验范围
    const int kHistoryLimitAll = -1;
    if (historyLimit != kHistoryLimitAll)
        root["historylimit"] = historyLimit;
    return writeCompact(root);
}

void GroupProtocolClient::parseInviteToGroupCounts(const std::string& jsonbody, GroupOpResult& out)
{
    out.m_added = 0;
    out.m_invited = 0;
    out.m_pending = 0;
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return;
    if (root["added"].isInt())
        out.m_added = root["added"].asInt();
    if (root["invited"].isInt())
        out.m_invited = root["invited"].asInt();
    if (root["pending"].isInt())
        out.m_pending = root["pending"].asInt();
}

std::string GroupProtocolClient::buildUpdateGroupInfoRequest(const UpdateGroupInfoParams& req)
{
    Json::Value root;
    root["groupid"] = std::to_string(req.m_groupid);

    //只写 has 为 true 的项。没写进去的字段服务端解析后 has_* 为假、不会更新，
    //于是"只改一项"就真的只改那一项。
    if (req.m_hasName)
        root["name"] = req.m_name;
    //群头像的两项由 m_has 开关决定写不写，<u>不能</u>因为"值是 0 / 空串"就省略：
    //恢复默认头像发出去的正是 facetype=0 + customface=""，少写任何一项，服务端
    //对应的 has_* 就是假，库里旧的 fileid 清不掉，界面上头像看着像没退回去。
    if (req.m_hasFacetype)
        root["facetype"] = req.m_facetype;
    if (req.m_hasCustomface)
        root["customface"] = req.m_customface;
    if (req.m_hasAnnouncement)
        root["announcement"] = req.m_announcement;
    if (req.m_hasIntro)
        root["intro"] = req.m_intro;
    if (req.m_hasJoinMode)
        root["joinmode"] = req.m_joinMode;
    if (req.m_hasAtAllScope)
        root["atallscope"] = req.m_atAllScope;
    if (req.m_hasNewSeeHistory)
        root["newseehistory"] = req.m_newSeeHistory;
    if (req.m_hasMemberInvite)
        root["memberinvite"] = req.m_memberInvite;
    if (req.m_hasWelcomeOn)
        root["welcomeon"] = req.m_welcomeOn;
    if (req.m_hasWelcomeText)
        root["welcometext"] = req.m_welcomeText;

    return writeCompact(root);
}

std::string GroupProtocolClient::buildSetMemberRoleRequest(int64_t groupid, int64_t userid, int role)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["userid"]  = std::to_string(userid);
    root["role"]    = role;
    return writeCompact(root);
}

std::string GroupProtocolClient::buildMuteMemberRequest(int64_t groupid, int64_t userid, int muted,
                                                        int duration, const std::string& reason)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["userid"]  = std::to_string(userid);
    root["muted"]   = muted;

    //解禁时不带时长与原因 —— 服务端会把库里的这两项一并清空，多发也无意义。
    //禁言时始终带上：duration 为 0 即永久禁言，是个有效取值，不能因为它是 0 就省略。
    if (muted != 0)
    {
        root["duration"] = duration;
        root["reason"]   = reason;
    }
    return writeCompact(root);
}

std::string GroupProtocolClient::buildMuteAllRequest(int64_t groupid, int muteall)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["muteall"] = muteall;
    return writeCompact(root);
}

std::string GroupProtocolClient::buildTransferOwnerRequest(int64_t groupid, int64_t userid)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["userid"]  = std::to_string(userid);
    return writeCompact(root);
}

std::string GroupProtocolClient::buildHandleJoinGroupRequest(int64_t requestid, bool accept)
{
    Json::Value root;
    root["requestid"] = std::to_string(requestid);
    root["action"]    = accept ? 1 : 0;
    return writeCompact(root);
}

// ====================================================================
// 群管理：应答与推送解析
// ====================================================================

bool GroupProtocolClient::parseJoinRequestsResponse(const std::string& jsonbody,
                                                    GroupJoinRequestsResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    if (out.m_code != 0)
        return true;

    out.m_groupid = jsonToInt64(root["groupid"]);

    const Json::Value& arr = root["requests"];
    if (!arr.isArray())
        return true;   //没有待审批申请时服务端仍会带一个空数组；缺字段也按空处理

    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& jo = arr[i];
        if (!jo.isObject())
            continue;

        GroupJoinRequestItem item;
        item.m_requestid = jsonToInt64(jo["requestid"]);
        if (item.m_requestid <= 0)
            continue;   //没有申请ID就无法审批，这样的项留着只会让界面出现点不动的卡片

        //项内不带 groupid，取外层应答的
        item.m_groupid    = out.m_groupid;
        item.m_userid     = jsonToInt64(jo["userid"]);
        item.m_nickname   = jo["nickname"].isString() ? jo["nickname"].asString() : std::string();
        item.m_facetype   = jo["facetype"].isInt() ? jo["facetype"].asInt() : 0;
        item.m_customface = jo["customface"].isString() ? jo["customface"].asString() : std::string();
        item.m_verifymsg  = jo["verifymsg"].isString() ? jo["verifymsg"].asString() : std::string();
        item.m_timestamp  = jsonToInt64(jo["timestamp"]);
        if (jo["source"].isInt())
            item.m_source = jo["source"].asInt();
        item.m_recommender = jsonToInt64(jo["recommender"]);

        out.m_requests.push_back(item);
    }
    return true;
}

bool GroupProtocolClient::parseJoinGroupRequestNotify(const std::string& jsonbody,
                                                      GroupJoinRequestItem& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_requestid = jsonToInt64(root["requestid"]);
    out.m_groupid   = jsonToInt64(root["groupid"]);
    if (out.m_requestid <= 0 || out.m_groupid <= 0)
        return false;

    //推送体里申请人各字段带 from 前缀，与 1319 应答里的项名不同
    out.m_userid     = jsonToInt64(root["fromuserid"]);
    out.m_nickname   = root["fromnickname"].isString() ? root["fromnickname"].asString() : std::string();
    out.m_facetype   = root["fromfacetype"].isInt() ? root["fromfacetype"].asInt() : 0;
    out.m_customface = root["fromcustomface"].isString() ? root["fromcustomface"].asString() : std::string();
    out.m_verifymsg  = root["verifymsg"].isString() ? root["verifymsg"].asString() : std::string();
    out.m_timestamp  = jsonToInt64(root["timestamp"]);
    if (root["source"].isInt())
        out.m_source = root["source"].asInt();
    out.m_recommender = jsonToInt64(root["recommender"]);
    return true;
}

bool GroupProtocolClient::parseGroupMemberChangedNotify(const std::string& jsonbody,
                                                        GroupMemberChangedNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_groupid = jsonToInt64(root["groupid"]);
    if (out.m_groupid <= 0)
        return false;

    if (!root["changetype"].isInt())
        return false;
    out.m_changetype = root["changetype"].asInt();

    out.m_operatorid = jsonToInt64(root["operatorid"]);

    const Json::Value& arr = root["userids"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const int64_t uid = jsonToInt64(arr[i]);
            if (uid > 0)
                out.m_userids.push_back(uid);
        }
    }
    return true;
}

bool GroupProtocolClient::parseGroupInviteChangedNotify(const std::string& jsonbody, GroupInviteChangedNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    out.m_inviteid  = jsonToInt64(root["inviteid"]);
    out.m_msgid     = jsonToInt64(root["msgid"]);
    out.m_inviterid = jsonToInt64(root["inviterid"]);
    out.m_inviteeid = jsonToInt64(root["inviteeid"]);
    if (out.m_inviteid <= 0 || out.m_msgid <= 0 || !root["state"].isInt())
        return false;
    out.m_state = root["state"].asInt();
    return true;
}

bool GroupProtocolClient::parseJoinRequestHandledNotify(const std::string& jsonbody, JoinRequestHandledNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    out.m_groupid   = jsonToInt64(root["groupid"]);
    out.m_requestid = jsonToInt64(root["requestid"]);
    out.m_handler   = jsonToInt64(root["handler"]);
    if (out.m_groupid <= 0 || out.m_requestid <= 0 || !root["status"].isInt())
        return false;
    out.m_status = root["status"].asInt();
    out.m_handlerName = root["handlername"].isString() ? root["handlername"].asString() : std::string();
    return true;
}

std::string GroupProtocolClient::buildAcceptGroupInviteRequest(int64_t inviteid)
{
    Json::Value root;
    //邀请 id 以字符串编码，与群组段其它报文一致
    root["inviteid"] = std::to_string(inviteid);
    return writeCompact(root);
}

bool GroupProtocolClient::parseAcceptGroupInviteResponse(const std::string& jsonbody, AcceptGroupInviteResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    if (!root["code"].isInt())
        return false;
    out.m_code     = root["code"].asInt();
    out.m_msg      = root["msg"].isString() ? root["msg"].asString() : std::string();
    out.m_inviteid = jsonToInt64(root["inviteid"]);
    out.m_groupid  = jsonToInt64(root["groupid"]);
    out.m_joined   = root["joined"].isInt() && root["joined"].asInt() != 0;
    return true;
}

bool GroupProtocolClient::parseGroupInfoChangedNotify(const std::string& jsonbody,
                                                      GroupInfoChangedNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["changetype"].isInt())
        return false;
    out.m_changetype = root["changetype"].asInt();

    //推送体的群资料部分与 cmd_get_group_info 应答同构（服务端两处共用
    //fillGroupValue），故复用同一套字段读取。只是推送体没有 code / msg。
    fillGroupInfoFromValue(root, out.m_info);
    if (out.m_info.m_groupid <= 0)
        return false;

    return true;
}

// ====================================================================
// 群公告
// ====================================================================

//从一个含公告字段的 JSON 对象里填出一条公告。应答里的数组项与推送体里的
//announcement 子对象同构（服务端两处共用 fillAnnouncementValue），故共用一份读取。
static void fillAnnouncementFromValue(const Json::Value& jo, GroupAnnouncementItem& out)
{
    out.m_id            = jsonToInt64(jo["id"]);
    out.m_groupid       = jsonToInt64(jo["groupid"]);
    out.m_title         = jo["title"].isString() ? jo["title"].asString() : std::string();
    out.m_content       = jo["content"].isString() ? jo["content"].asString() : std::string();
    out.m_severity      = jo["severity"].isInt() ? jo["severity"].asInt() : 0;
    out.m_publisher     = jsonToInt64(jo["publisher"]);
    out.m_publishername = jo["publishername"].isString() ? jo["publishername"].asString() : std::string();
    out.m_publishtime   = jsonToInt64(jo["publishtime"]);
}

std::string GroupProtocolClient::buildGetAnnouncementsRequest(int64_t groupId, int offset,
                                                              int count)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupId);
    root["offset"]  = offset;
    root["count"]   = count;
    return writeCompact(root);
}

std::string GroupProtocolClient::buildPublishAnnouncementRequest(int64_t groupId,
                                                                 const std::string& titleUtf8,
                                                                 const std::string& contentUtf8,
                                                                 int severity)
{
    Json::Value root;
    root["groupid"]  = std::to_string(groupId);
    root["title"]    = titleUtf8;
    root["content"]  = contentUtf8;
    root["severity"] = severity;
    return writeCompact(root);
}

std::string GroupProtocolClient::buildRepublishAnnouncementRequest(int64_t groupId,
                                                                   int64_t announceId)
{
    //只带 id：标题正文由服务端读回原件补齐。这里<u>不</u>把本地那份标题正文一起发过去，
    //免得本地列表是旧的、把过时内容当成新公告发出去。
    Json::Value root;
    root["groupid"] = std::to_string(groupId);
    root["id"]      = std::to_string(announceId);
    return writeCompact(root);
}

std::string GroupProtocolClient::buildDeleteAnnouncementRequest(int64_t groupId,
                                                                int64_t announceId)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupId);
    root["id"]      = std::to_string(announceId);
    return writeCompact(root);
}

bool GroupProtocolClient::parseAnnouncementsResponse(const std::string& jsonbody,
                                                     GroupAnnouncementsResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    if (out.m_code != 0)
        return true;

    out.m_groupid = jsonToInt64(root["groupid"]);
    out.m_total   = root["total"].isInt() ? root["total"].asInt() : 0;

    //服务端回显的本次请求分页参数；老服务端不回这两项，保持 -1 让 MatchesRequest
    //退回"来者不拒"
    if (root["offset"].isInt())
        out.m_offset = root["offset"].asInt();
    if (root["count"].isInt())
        out.m_count = root["count"].asInt();

    const Json::Value& arr = root["announcements"];
    if (!arr.isArray())
        return true;

    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& jo = arr[i];
        if (!jo.isObject())
            continue;

        GroupAnnouncementItem item;
        fillAnnouncementFromValue(jo, item);
        //没有公告ID就撤不掉也重发不了，留在列表里只会让按钮点了没反应
        if (item.m_id <= 0)
            continue;

        out.m_announcements.push_back(item);
    }

    //服务端给的 total 比本页条数还小时以本页为准 —— 两者对不上多半是服务端算漏了，
    //而界面上的页数至少不该小于已经拿到的条数。注意这里只能与<u>本页</u>条数比，
    //分页拉取时 total 本就该大于本页条数，不能反过来把它压回去。
    if (out.m_total < static_cast<int>(out.m_announcements.size()))
        out.m_total = static_cast<int>(out.m_announcements.size());

    return true;
}

bool GroupProtocolClient::parsePublishAnnouncementResponse(const std::string& jsonbody, int& code,
                                                           std::string& msgUtf8, int64_t& idOut)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    code    = root["code"].asInt();
    msgUtf8 = root["msg"].isString() ? root["msg"].asString() : std::string();
    idOut   = jsonToInt64(root["id"]);
    return true;
}

bool GroupProtocolClient::parseAnnouncementNotify(const std::string& jsonbody,
                                                  GroupAnnouncementNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["changetype"].isInt())
        return false;
    out.m_changetype = root["changetype"].asInt();

    out.m_groupid = jsonToInt64(root["groupid"]);

    const Json::Value& jo = root["announcement"];
    if (!jo.isObject())
        return false;
    fillAnnouncementFromValue(jo, out.m_announcement);

    //公告ID为正是底线：界面要按它去重（同一条公告只弹一次窗）、要按它定位列表项
    if (out.m_announcement.m_id <= 0)
        return false;

    //推送体外层的 groupid 与公告自身的 groupid 是同一个值，外层缺失时用里层兜底
    if (out.m_groupid <= 0)
        out.m_groupid = out.m_announcement.m_groupid;

    //公告卡片（删除推送才带）：msgid 与改写后的卡片要成对出现才有用，缺一个就都不认
    out.m_cardMsgId = 0;
    out.m_cardContent.clear();
    const int64_t cardMsgId = jsonToInt64(root["cardmsgid"]);
    if (cardMsgId > 0 && root["card"].isObject())
    {
        out.m_cardMsgId = cardMsgId;
        out.m_cardContent = writeCompact(root["card"]);
    }

    return out.m_groupid > 0;
}

// ====================================================================
// 入群必读（docs/群公告接入方案.md §14）
// ====================================================================

std::string GroupProtocolClient::buildSetMustReadRequest(int64_t groupId, int64_t announceId)
{
    Json::Value root;
    root["groupid"]        = std::to_string(groupId);
    root["announcementid"] = std::to_string(announceId);
    return writeCompact(root);
}

bool GroupProtocolClient::parseGetMustReadResponse(const std::string& jsonbody, MustReadResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_msg = root["msg"].isString() ? root["msg"].asString() : std::string();
    out.m_groupid = jsonToInt64(root["groupid"]);
    out.m_mustreadid = jsonToInt64(root["mustreadid"]);
    out.m_pending = root["pending"].isBool() ? root["pending"].asBool() : false;
    out.m_hasAnnouncement = false;
    const Json::Value& jo = root["announcement"];
    if (jo.isObject())
    {
        fillAnnouncementFromValue(jo, out.m_announcement);
        out.m_hasAnnouncement = (out.m_announcement.m_id > 0);
    }
    if (out.m_code != 0)
        return true;
    return out.m_groupid > 0;
}

} // namespace net
