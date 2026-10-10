/**
 *  群问答的报文编解码（客户端侧）实现，GroupQaProtocolClient.cpp。说明见头文件。
 *  balloonwj@qq.com   2026-10-07
 */
#include "GroupQaProtocolClient.h"
#include <stdlib.h>
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

namespace {

//序列化为紧凑 JSON
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//64 位 ID：字符串按十进制解析，整数直接取，其它返回 0
int64_t ToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//解析顶层为对象的 JSON
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

} // anonymous namespace

std::string GroupQaProtocolClient::buildGetRequest(int64_t groupid)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    return WriteCompact(root);
}

std::string GroupQaProtocolClient::buildSaveRequest(int64_t groupid, int64_t id, const std::string& question,
                                                    const std::string& answer)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["id"] = std::to_string(id);
    root["question"] = question;
    root["answer"] = answer;
    return WriteCompact(root);
}

std::string GroupQaProtocolClient::buildDeleteRequest(int64_t groupid, int64_t id)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["id"] = std::to_string(id);
    return WriteCompact(root);
}

bool GroupQaProtocolClient::parseGetResponse(const std::string& jsonbody, GroupQaResult& out)
{
    Json::Value root;
    if (!ParseObject(jsonbody, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_msg = root["msg"].isString() ? root["msg"].asString() : std::string();
    out.m_groupid = ToInt64(root["groupid"]);
    out.m_items.clear();
    const Json::Value& arr = root["items"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jo = arr[i];
            if (!jo.isObject() || !jo["question"].isString() || !jo["answer"].isString())
                continue;
            GroupQaItem item;
            item.m_id = ToInt64(jo["id"]);
            if (item.m_id <= 0)
                continue;
            item.m_question = jo["question"].asString();
            item.m_answer = jo["answer"].asString();
            //来源只认 "enterprise"，其余（含缺失）都按本群问答处理
            item.m_source = (jo["source"].isString() && jo["source"].asString() == "enterprise")
                            ? kGroupQaSourceEnterprise : kGroupQaSourceGroup;
            item.m_editorname = jo["editorname"].isString() ? jo["editorname"].asString() : std::string();
            item.m_updatetime = ToInt64(jo["updatetime"]);
            out.m_items.push_back(item);
        }
    }
    if (out.m_code != 0)
        return true;
    return out.m_groupid > 0;
}

bool GroupQaProtocolClient::parseChangedNotify(const std::string& jsonbody, int64_t& groupid)
{
    Json::Value root;
    if (!ParseObject(jsonbody, root))
        return false;
    groupid = ToInt64(root["groupid"]);
    return groupid > 0;
}

} // namespace net
