/**
 *  v2 好友管理报文编解码（客户端侧）实现，FriendProtocolClient.cpp
 *  balloonwj@qq.com   2026-08-05
 */
#include "FriendProtocolClient.h"
#include <stdlib.h>                     // strtoll
//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元
//时 MSVC 会对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地
//屏蔽，不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace net {

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID（userid 等）以字符串编码进 JSON，避免精度丢失。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本。
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

std::string FriendProtocolClient::buildSetFriendRemarkRequest(int64_t userid,
                                                              const std::string& remarkUtf8)
{
    Json::Value root;
    //64 位 ID 一律以字符串编码，与服务端 FriendProtocol::parseSetFriendRemarkRequest
    //的 getStringId 对齐 —— 写成数字会被判为格式非法。
    root["userid"] = std::to_string(userid);
    //空串是合法取值（表示清除备注），照写不误；服务端 parseSetFriendRemarkRequest
    //也明确允许 remark 缺省或为空。
    root["remark"] = remarkUtf8;
    return writeCompact(root);
}

bool FriendProtocolClient::parseSetFriendRemarkResponse(const std::string& jsonbody,
                                                        SetFriendRemarkResult& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //code 是判定成败的唯一依据，缺了就没法用，直接判失败。
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //userid 由服务端原样回带，用来把应答对上是哪一条请求。缺失也不判整条应答失败
    //——code 已经拿到了，调用方至少知道成没成，只是没法精确回滚（老服务端）。
    out.m_userid = jsonToInt64(root["userid"]);

    return true;
}

} // namespace net
