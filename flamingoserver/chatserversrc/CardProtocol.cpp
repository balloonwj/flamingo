/**
 *  v2 群互动卡片报文编解码实现，CardProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "CardProtocol.h"
#include "../jsoncpp1.9.0/json.h"
#include "Protocol.h"   //卡片消息类型常量 message_type_poll / _relay / _todo

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

//把一段原始JSON文本解析回 Json::Value；空串或解析失败返回 null 值
static Json::Value rawToValue(const std::string& raw)
{
    if (raw.empty())
        return Json::Value(Json::nullValue);

    Json::Value v;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(raw.c_str(), raw.c_str() + raw.size(), &v, &errs);
    delete reader;
    if (!ok || !errs.empty())
        return Json::Value(Json::nullValue);
    return v;
}

//聚合投票互动行：votes[选项下标] = 票数，totalcount = 去重投票人数
static void aggregatePoll(const std::vector<CardInteractionRecord>& rows,
                          Json::Value& votes, int& totalcount)
{
    votes = Json::Value(Json::objectValue);
    //每行即一名投票人（UNIQUE(msgid,userid) 保证）
    totalcount = static_cast<int>(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        //row.data 形如 [0,2]：本投票人所投的选项下标
        Json::Value chosen = rawToValue(rows[i].data);
        if (!chosen.isArray())
            continue;
        for (Json::ArrayIndex j = 0; j < chosen.size(); ++j)
        {
            if (!chosen[j].isInt())
                continue;
            std::string key = std::to_string(chosen[j].asInt());
            int prev = votes[key].isInt() ? votes[key].asInt() : 0;
            votes[key] = prev + 1;
        }
    }
}

//聚合接龙互动行：items = [{userid,text}]，按互动时间（行顺序）排列
static Json::Value aggregateRelay(const std::vector<CardInteractionRecord>& rows)
{
    Json::Value items(Json::arrayValue);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        Json::Value jo;
        jo["userid"] = std::to_string(rows[i].userid);
        jo["text"]   = rows[i].data;
        items.append(jo);
    }
    return items;
}

//聚合待办互动行：doneusers = [已勾选完成的userid]
static Json::Value aggregateTodo(const std::vector<CardInteractionRecord>& rows)
{
    Json::Value users(Json::arrayValue);
    for (size_t i = 0; i < rows.size(); ++i)
        users.append(std::to_string(rows[i].userid));
    return users;
}

bool CardProtocol::parseVotePollRequest(const std::string& json, VotePollRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //options 必填且非空：所投选项下标列表
    const Json::Value& arr = root["options"];
    if (!arr.isArray() || arr.size() == 0)
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i].isInt())
            req.options.push_back(arr[i].asInt());
    }
    if (req.options.empty())
        return false;
    return true;
}

bool CardProtocol::parseJoinRelayRequest(const std::string& json, JoinRelayRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //text 必填且非空
    if (!root["text"].isString())
        return false;
    req.text = root["text"].asString();
    if (req.text.empty())
        return false;
    return true;
}

bool CardProtocol::parseUpdateTodoRequest(const std::string& json, UpdateTodoRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //done 必填：1 勾选完成 0 取消勾选
    if (!root["done"].isInt())
        return false;
    req.done = root["done"].asInt();
    return true;
}

std::string CardProtocol::buildVoteData(const std::vector<int>& options)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < options.size(); ++i)
        arr.append(options[i]);
    return writeCompact(arr);
}

bool CardProtocol::getPollMeta(const std::string& content, int& optioncount,
                               int& multi, int64_t& expiretime)
{
    optioncount = 0;
    multi       = 0;
    expiretime  = 0;

    Json::Value root;
    if (!parseObject(content, root))
        return false;

    //options 数组个数即选项数
    if (!root["options"].isArray())
        return false;
    optioncount = static_cast<int>(root["options"].size());

    multi      = (root["multi"].isBool() && root["multi"].asBool()) ? 1 : 0;
    expiretime = root["expiretime"].isIntegral()
               ? static_cast<int64_t>(root["expiretime"].asInt64()) : 0;
    return true;
}

bool CardProtocol::isTodoAssignee(const std::string& content, int64_t userid)
{
    Json::Value root;
    if (!parseObject(content, root))
        return false;

    const Json::Value& arr = root["assignees"];
    if (!arr.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i].isString() &&
            static_cast<int64_t>(atoll(arr[i].asString().c_str())) == userid)
        {
            return true;
        }
    }
    return false;
}

std::string CardProtocol::buildPollChangedNotify(int64_t msgid,
                                                 const std::vector<CardInteractionRecord>& rows)
{
    Json::Value votes;
    int totalcount = 0;
    aggregatePoll(rows, votes, totalcount);

    Json::Value root;
    root["msgid"]      = std::to_string(msgid);
    root["votes"]      = votes;
    root["totalcount"] = totalcount;
    return writeCompact(root);
}

std::string CardProtocol::buildRelayChangedNotify(int64_t msgid,
                                                  const std::vector<CardInteractionRecord>& rows)
{
    Json::Value root;
    root["msgid"] = std::to_string(msgid);
    root["items"] = aggregateRelay(rows);
    return writeCompact(root);
}

std::string CardProtocol::buildTodoChangedNotify(int64_t msgid,
                                                 const std::vector<CardInteractionRecord>& rows)
{
    Json::Value root;
    root["msgid"]     = std::to_string(msgid);
    root["doneusers"] = aggregateTodo(rows);
    return writeCompact(root);
}

std::string CardProtocol::augmentCardContent(int msgtype, const std::string& content,
                                             const std::vector<CardInteractionRecord>& rows)
{
    Json::Value root;
    //content 非法（非对象）则原样返回，不做合并
    if (!parseObject(content, root))
        return content;

    if (msgtype == message_type_poll)
    {
        Json::Value votes;
        int totalcount = 0;
        aggregatePoll(rows, votes, totalcount);
        root["votes"]      = votes;
        root["totalcount"] = totalcount;
    }
    else if (msgtype == message_type_relay)
    {
        root["items"] = aggregateRelay(rows);
    }
    else if (msgtype == message_type_todo)
    {
        root["doneusers"] = aggregateTodo(rows);
    }
    else
    {
        //非卡片消息：原样返回
        return content;
    }
    return writeCompact(root);
}

std::string CardProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}
