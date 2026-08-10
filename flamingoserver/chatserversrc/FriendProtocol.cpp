/**
 *  v2 联系人报文编解码实现，FriendProtocol.cpp
 *  （cmd_search / cmd_set_friend_remark / cmd_set_blacklist，说明见 FriendProtocol.h）
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
        }
        arr.append(jo);
    }
    root["results"] = arr;
    return writeCompact(root);
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
