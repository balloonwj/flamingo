/**
 *  v2 好友关系报文编解码实现，FriendProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "FriendProtocol.h"
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

bool FriendProtocol::parseSearchRequest(const std::string& json, std::string& keyword, int& type)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root["keyword"].isString())
        return false;

    keyword = root["keyword"].asString();
    if (keyword.empty())
        return false;

    type = root["type"].isInt() ? root["type"].asInt() : 1;
    return true;
}

bool FriendProtocol::parseAddFriendRequest(const std::string& json, AddFriendRequestData& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.userid = getStringId(root, "userid");
    if (req.userid <= 0)
        return false;

    req.source    = root["source"].isString() ? root["source"].asString() : "";
    req.verifymsg = root["verifymsg"].isString() ? root["verifymsg"].asString() : "";
    return true;
}

bool FriendProtocol::parseHandleFriendRequest(const std::string& json, HandleFriendRequestData& req)
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

std::string FriendProtocol::buildSearchResponse(int code, const std::string& msg,
                                                int type, const std::vector<SearchResultItem>& items)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    root["type"] = type;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const SearchResultItem& it = items[i];
        Json::Value jo;
        if (type == 2)
        {
            //群结果
            jo["groupid"]     = std::to_string(it.id);
            jo["name"]        = it.name;
            jo["facetype"]    = it.facetype;
            jo["customface"]  = it.customface;
            jo["membercount"] = it.membercount;
            jo["ownerid"]     = std::to_string(it.ownerid);
        }
        else
        {
            //用户结果（type=1，其余类型暂同用户卡片）
            jo["userid"]     = std::to_string(it.id);
            jo["username"]   = it.username;
            jo["nickname"]   = it.name;
            jo["facetype"]   = it.facetype;
            jo["customface"] = it.customface;
            jo["gender"]     = it.gender;
            jo["signature"]  = it.signature;
            jo["address"]    = it.address;
            jo["usertype"]   = it.usertype;
            jo["isfriend"]   = it.isfriend;
        }
        arr.append(jo);
    }
    root["results"] = arr;
    return writeCompact(root);
}

std::string FriendProtocol::buildAddFriendResponse(int code, const std::string& msg, int64_t requestid)
{
    Json::Value root;
    root["code"]      = code;
    root["msg"]       = msg;
    root["requestid"] = std::to_string(requestid);
    return writeCompact(root);
}

//把一条申请的发起方信息写入 Json 对象，notify 与 notifications 共用
static void fillRequestItem(Json::Value& jo, const FriendRequestItem& item, bool withStatus)
{
    jo["requestid"]      = std::to_string(item.requestid);
    jo["fromuserid"]     = std::to_string(item.fromuserid);
    jo["fromusername"]   = item.fromusername;
    jo["fromnickname"]   = item.fromnickname;
    jo["fromfacetype"]   = item.fromfacetype;
    jo["fromcustomface"] = item.fromcustomface;
    jo["verifymsg"]      = item.verifymsg;
    jo["timestamp"]      = static_cast<Json::Int64>(item.timestamp);
    if (withStatus)
        jo["status"] = item.status;
}

std::string FriendProtocol::buildFriendRequestNotify(const FriendRequestItem& item)
{
    Json::Value root;
    fillRequestItem(root, item, false);
    return writeCompact(root);
}

std::string FriendProtocol::buildFriendRequestResultNotify(int64_t requestid, int64_t fromuserid, int action)
{
    Json::Value root;
    root["requestid"]  = std::to_string(requestid);
    root["fromuserid"] = std::to_string(fromuserid);
    root["action"]     = action;
    return writeCompact(root);
}

std::string FriendProtocol::buildFriendRelationChangedNotify(int64_t userid, int changetype)
{
    Json::Value root;
    root["userid"]     = std::to_string(userid);
    root["changetype"] = changetype;
    return writeCompact(root);
}

std::string FriendProtocol::buildGetNotificationsResponse(int code, const std::string& msg,
                                                          const std::vector<FriendRequestItem>& items)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value jo;
        fillRequestItem(jo, items[i], true);
        arr.append(jo);
    }
    root["notifications"] = arr;
    return writeCompact(root);
}

bool FriendProtocol::parseDeleteFriendRequest(const std::string& json, DeleteFriendRequestData& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.userid = getStringId(root, "userid");
    if (req.userid <= 0)
        return false;
    return true;
}

bool FriendProtocol::parseSetFriendRemarkRequest(const std::string& json, SetFriendRemarkRequestData& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.userid = getStringId(root, "userid");
    if (req.userid <= 0)
        return false;

    //remark 可缺省；缺省或非字符串一律按空串处理，表示清除备注
    req.remark = root["remark"].isString() ? root["remark"].asString() : "";
    return true;
}

bool FriendProtocol::parseSetBlacklistRequest(const std::string& json, SetBlacklistRequestData& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.userid = getStringId(root, "userid");
    if (req.userid <= 0)
        return false;

    //action 必填：1 加入黑名单 0 移出黑名单
    if (!root["action"].isInt())
        return false;
    req.action = root["action"].asInt();
    return true;
}

bool FriendProtocol::parseSetFriendTagsRequest(const std::string& json, SetFriendTagsRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.userid = getStringId(root, "userid");
    if (req.userid <= 0)
        return false;

    //收集 tags 数组里的字符串元素，重新序列化为紧凑JSON数组；
    //tags 缺省或为空数组即清空标签
    Json::Value arr(Json::arrayValue);
    const Json::Value& tags = root["tags"];
    if (tags.isArray())
    {
        for (Json::ArrayIndex i = 0; i < tags.size(); ++i)
        {
            if (tags[i].isString())
                arr.append(tags[i].asString());
        }
    }
    req.tagsJson = writeCompact(arr);
    return true;
}

std::string FriendProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

std::string FriendProtocol::buildSetFriendRemarkResponse(int code, const std::string& msg,
                                                         int64_t userid)
{
    Json::Value root;
    root["code"]   = code;
    root["msg"]    = msg;
    //64 位 ID 一律以字符串编码，与本协议其余地方保持一致。
    root["userid"] = std::to_string(userid);
    return writeCompact(root);
}
