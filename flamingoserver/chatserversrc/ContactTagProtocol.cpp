/**
 *  v2「联系人标签」报文编解码实现，ContactTagProtocol.cpp
 *  设计与约定见 ContactTagProtocol.h。
 *  balloonwj@qq.com   2026-08-13
 */
#include "ContactTagProtocol.h"
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

bool ContactTagProtocol::parseSetContactTagsRequest(const std::string& json,
                                                    SetContactTagsRequest& req)
{
    req.tags.clear();

    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.userid = getStringId(root, "userid");
    if (req.userid <= 0)
        return false;

    //tags 必填且必须是数组。缺字段不按"空数组"处理，否则一个漏发字段的客户端
    //会把用户贴好的标签整份清空（详见头文件里的说明）。
    const Json::Value& tags = root["tags"];
    if (!tags.isArray())
        return false;

    for (Json::ArrayIndex i = 0; i < tags.size(); ++i)
    {
        //数组里混进数字 / 对象一律判整个请求非法，而不是跳过那一条：客户端把
        //标签发错了类型，多半是它自己的序列化出了问题，静默丢弃只会让人更晚发现。
        if (!tags[i].isString())
        {
            req.tags.clear();
            return false;
        }

        //归一（去首尾空白、丢空串、忽略大小写去重）留给存储层统一做，本层原样收下，
        //避免两处各归一一次、日后改规则时漏改一处。
        req.tags.push_back(tags[i].asString());
    }

    return true;
}

std::string ContactTagProtocol::buildSetContactTagsResponse(int code, const std::string& msg,
                                                            int64_t userid,
                                                            const std::vector<std::string>& tags)
{
    Json::Value root;
    root["code"]   = code;
    root["msg"]    = msg;
    root["userid"] = std::to_string(userid);   //64 位 ID 一律以字符串编码进 JSON

    //标签列表一律写出，空列表也写成 []，不做"空则省略字段"的优化：客户端只需
    //一条解析路径，"清空了标签"与"从来没贴过标签"在显示上本就是同一种效果。
    Json::Value jtags(Json::arrayValue);
    for (std::vector<std::string>::const_iterator it = tags.begin(); it != tags.end(); ++it)
    {
        jtags.append(*it);
    }
    root["tags"] = jtags;

    return writeCompact(root);
}
