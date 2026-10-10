/**
 *  文件共享信令客户端编解码 ShareProtocolCodec 的实现，见 ShareProtocolCodec.h。
 *
 *  编码与服务端 chatserversrc/FileShareProtocol.cpp 用同一种写法（jsoncpp 紧凑输出，只把缩进设为空），
 *  对 ASCII 内容两边的输出逐字相同。解析时取字段一律先判类型再取值，避免 jsoncpp 在类型不符时抛异常。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareProtocolCodec.h"

#include <stdlib.h>
#include <memory>
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace fileshare {

namespace {

//64 位整数的十进制文本缓冲区字节数（最多 20 位数字加负号，留余量）
const int kInt64TextChars = 32;

//把 Json::Value 写成紧凑 JSON；设置与服务端 FileShareProtocol 的 writeCompact 完全一致
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析 JSON 文本，要求顶层为对象
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(json.data(), json.data() + json.size(), &root, &errs))
        return false;
    return root.isObject();
}

//64 位 ID 写成十进制字符串
std::string IdToString(int64_t id)
{
    char buf[kInt64TextChars] = { 0 };
    ::_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lld", static_cast<long long>(id));
    return buf;
}

//把一个 JSON 值解释成 64 位 ID：十进制字符串与整数两种写法都接受；不合法返回 0
int64_t ValueToId(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty())
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<int64_t>(::_atoi64(text.c_str()));
    }
    if (v.isInt64())
        return v.asInt64();
    return 0;
}

//取 64 位 ID 字段；缺失或不合法返回 0
int64_t GetId(const Json::Value& root, const char* key)
{
    if (!root.isMember(key))
        return 0;
    return ValueToId(root[key]);
}

//取必填的 32 位整数
bool GetInt(const Json::Value& root, const char* key, int& out)
{
    if (!root.isMember(key) || !root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//取选填的 32 位整数：缺失时取默认值
bool GetOptionalInt(const Json::Value& root, const char* key, int defaultValue, int& out)
{
    if (!root.isMember(key))
    {
        out = defaultValue;
        return true;
    }
    return GetInt(root, key, out);
}

//取选填的 64 位整数：缺失时为 0
bool GetOptionalInt64(const Json::Value& root, const char* key, int64_t& out)
{
    out = 0;
    if (!root.isMember(key))
        return true;
    if (!root[key].isInt64())
        return false;
    out = root[key].asInt64();
    return true;
}

//取必填的字符串
bool GetString(const Json::Value& root, const char* key, std::string& out)
{
    if (!root.isMember(key) || !root[key].isString())
        return false;
    out = root[key].asString();
    return true;
}

//取选填的字符串：缺失时为空串
bool GetOptionalString(const Json::Value& root, const char* key, std::string& out)
{
    out.clear();
    if (!root.isMember(key))
        return true;
    return GetString(root, key, out);
}

//取选填的 0 / 1 开关：缺失为假，取值不是 0 或 1 返回 false
bool GetOptionalFlag(const Json::Value& root, const char* key, bool& out)
{
    int value = 0;
    if (!GetOptionalInt(root, key, 0, value) || (value != 0 && value != 1))
        return false;
    out = (value == 1);
    return true;
}

//取 code 与 msg（应答的公共部分）；code 必填，msg 选填
bool GetCodeAndMsg(const Json::Value& root, int& code, std::string& msg)
{
    return GetInt(root, "code", code) && GetOptionalString(root, "msg", msg);
}

//成员数组写成 JSON
Json::Value MembersToJson(const std::vector<ShareMemberPerm>& members)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < members.size(); ++i)
    {
        Json::Value item;
        item["userid"] = IdToString(members[i].m_userId);
        item["perms"] = members[i].m_perms;
        arr.append(item);
    }
    return arr;
}

//解析成员数组（选填，缺失按空数组）
bool MembersFromJson(const Json::Value& root, const char* key, std::vector<ShareMemberPerm>& out)
{
    out.clear();
    if (!root.isMember(key))
        return true;
    const Json::Value& arr = root[key];
    if (!arr.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (!arr[i].isObject())
            return false;
        ShareMemberPerm m;
        m.m_userId = GetId(arr[i], "userid");
        if (m.m_userId <= 0 || !GetInt(arr[i], "perms", m.m_perms))
            return false;
        out.push_back(m);
    }
    return true;
}

//读取可选的 invalidmembers（2201 / 2204 被拒时带）；没有该字段返回 true 且 out 为空，有但格式不对返回 false
bool InvalidMembersFromJson(const Json::Value& root, std::vector<int64_t>& out)
{
    out.clear();
    if (!root.isMember("invalidmembers"))
        return true;
    const Json::Value& arr = root["invalidmembers"];
    if (!arr.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const int64_t id = ValueToId(arr[i]);
        if (id <= 0)
            return false;
        out.push_back(id);
    }
    return true;
}

} // namespace

//==== 编码请求 ====

std::string ShareProtocolCodec::BuildCreateRequest(const ShareCreateParams& params)
{
    Json::Value root;
    root["clientshareid"] = params.m_clientShareId;
    root["ctype"] = params.m_ctype;
    root["cid"] = IdToString(params.m_cid);
    root["kind"] = params.m_kind;
    root["name"] = params.m_name;
    root["displaypath"] = params.m_displayPath;
    root["includehidden"] = params.m_includeHidden ? 1 : 0;
    root["members"] = MembersToJson(params.m_members);
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildShareIdRequest(int64_t shareId)
{
    Json::Value root;
    root["shareid"] = IdToString(shareId);
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildUpdateMembersRequest(int64_t shareId, const std::vector<ShareMemberPerm>& add,
                                                          const std::vector<int64_t>& remove,
                                                          const std::vector<ShareMemberPerm>& update)
{
    Json::Value root;
    root["shareid"] = IdToString(shareId);
    if (!add.empty())
        root["add"] = MembersToJson(add);
    if (!remove.empty())
    {
        Json::Value arr(Json::arrayValue);
        for (size_t i = 0; i < remove.size(); ++i)
            arr.append(IdToString(remove[i]));
        root["remove"] = arr;
    }
    if (!update.empty())
        root["update"] = MembersToJson(update);
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildListRequest(int scope, int ctype, int64_t cid)
{
    Json::Value root;
    root["scope"] = scope;
    //ctype 与 cid 要么都给、要么都不给（服务端 parseListRequest 的约定）
    if (ctype != 0)
    {
        root["ctype"] = ctype;
        root["cid"] = IdToString(cid);
    }
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildServeReport(const std::vector<ShareServeItem>& items)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value item;
        item["shareid"] = IdToString(items[i].m_shareId);
        item["available"] = items[i].m_available ? 1 : 0;
        item["reason"] = items[i].m_available ? 0 : items[i].m_reason;
        arr.append(item);
    }
    Json::Value root;
    root["items"] = arr;
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildCloseAccessRequest(const std::string& accessId, int reason)
{
    Json::Value root;
    root["accessid"] = accessId;
    root["reason"] = reason;
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildTransferRequest(const std::string& accessId, int direction,
                                                     const std::vector<ShareTransferEntry>& items,
                                                     int64_t totalBytes, int64_t totalFiles)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value item;
        item["path"] = items[i].m_path;
        item["isdir"] = items[i].m_isDir ? 1 : 0;
        item["size"] = static_cast<Json::Int64>(items[i].m_size);
        arr.append(item);
    }
    Json::Value root;
    root["accessid"] = accessId;
    root["direction"] = direction;
    root["items"] = arr;
    root["totalbytes"] = static_cast<Json::Int64>(totalBytes);
    root["totalfiles"] = static_cast<Json::Int64>(totalFiles);
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildTransferClose(const std::string& transferId, int result, int errCode)
{
    Json::Value root;
    root["transferid"] = transferId;
    root["result"] = result;
    root["errcode"] = errCode;
    return WriteCompact(root);
}

std::string ShareProtocolCodec::BuildAuditReport(const std::vector<ShareAuditEntry>& items)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ShareAuditEntry& e = items[i];
        Json::Value item;
        item["shareid"] = IdToString(e.m_shareId);
        item["accessid"] = e.m_accessId;
        item["visitorid"] = IdToString(e.m_visitorId);
        item["action"] = e.m_action;
        item["path"] = e.m_path;
        item["topath"] = e.m_toPath;
        item["size"] = static_cast<Json::Int64>(e.m_size);
        item["result"] = e.m_result;
        item["clienttime"] = static_cast<Json::Int64>(e.m_clientTime);
        arr.append(item);
    }
    Json::Value root;
    root["items"] = arr;
    return WriteCompact(root);
}

//==== 解析应答与推送 ====

bool ShareProtocolCodec::ParseSimpleResponse(const std::string& json, int& code, std::string& msg)
{
    Json::Value root;
    return ParseObject(json, root) && GetCodeAndMsg(root, code, msg);
}

bool ShareProtocolCodec::ParseCreateResponse(const std::string& json, ShareCreateResponse& out)
{
    out = ShareCreateResponse();
    Json::Value root;
    if (!ParseObject(json, root) || !GetCodeAndMsg(root, out.m_code, out.m_msg))
        return false;
    if (!GetOptionalString(root, "clientshareid", out.m_clientShareId))
        return false;
    out.m_shareId = GetId(root, "shareid");
    return InvalidMembersFromJson(root, out.m_invalidMembers);
}

bool ShareProtocolCodec::ParseUpdateMembersResponse(const std::string& json, int& code, std::string& msg,
                                                    std::vector<int64_t>& invalid)
{
    code = 0;
    msg.clear();
    invalid.clear();
    Json::Value root;
    if (!ParseObject(json, root) || !GetCodeAndMsg(root, code, msg))
        return false;
    return InvalidMembersFromJson(root, invalid);
}

bool ShareProtocolCodec::ParseListResponse(const std::string& json, ShareListResponse& out)
{
    out = ShareListResponse();
    Json::Value root;
    if (!ParseObject(json, root) || !GetCodeAndMsg(root, out.m_code, out.m_msg))
        return false;
    //失败的应答不带 shares
    if (!root.isMember("shares"))
        return true;
    const Json::Value& arr = root["shares"];
    if (!arr.isArray())
        return false;

    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject())
            return false;
        ShareListEntry e;
        e.m_shareId = GetId(item, "shareid");
        e.m_ownerId = GetId(item, "ownerid");
        e.m_cid = GetId(item, "cid");
        if (e.m_shareId <= 0 || e.m_ownerId <= 0)
            return false;
        if (!GetInt(item, "ctype", e.m_ctype) || !GetInt(item, "kind", e.m_kind)
            || !GetString(item, "name", e.m_name) || !GetOptionalString(item, "displaypath", e.m_displayPath)
            || !GetOptionalFlag(item, "includehidden", e.m_includeHidden) || !GetInt(item, "state", e.m_state)
            || !GetOptionalInt(item, "reason", 0, e.m_reason) || !GetOptionalInt64(item, "createtime", e.m_createTime)
            || !GetOptionalFlag(item, "onthisdevice", e.m_onThisDevice) || !MembersFromJson(item, "members", e.m_members)
            || !GetOptionalInt(item, "myperms", 0, e.m_myPerms))
            return false;
        out.m_shares.push_back(e);
    }
    return true;
}

bool ShareProtocolCodec::ParseOpenResponse(const std::string& json, int& code, std::string& msg, std::string& accessId)
{
    std::string name;
    return ParseOpenResponse(json, code, msg, accessId, name);
}

bool ShareProtocolCodec::ParseOpenResponse(const std::string& json, int& code, std::string& msg, std::string& accessId,
                                           std::string& name)
{
    Json::Value root;
    return ParseObject(json, root) && GetCodeAndMsg(root, code, msg) && GetOptionalString(root, "accessid", accessId)
        && GetOptionalString(root, "name", name);
}

bool ShareProtocolCodec::ParseTransferResponse(const std::string& json, int& code, std::string& msg,
                                               std::string& transferId)
{
    Json::Value root;
    return ParseObject(json, root) && GetCodeAndMsg(root, code, msg)
        && GetOptionalString(root, "transferid", transferId);
}

bool ShareProtocolCodec::ParseStateNotify(const std::string& json, int64_t& shareId, int& state, int& reason)
{
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    shareId = GetId(root, "shareid");
    return shareId > 0 && GetInt(root, "state", state) && GetOptionalInt(root, "reason", 0, reason);
}

bool ShareProtocolCodec::ParsePermsNotify(const std::string& json, int64_t& shareId, int64_t& userId, int& perms)
{
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    shareId = GetId(root, "shareid");
    userId = GetId(root, "userid");
    return shareId > 0 && userId > 0 && GetInt(root, "perms", perms);
}

bool ShareProtocolCodec::ParseRelayInfo(const std::string& json, ShareRelayParams& out)
{
    out = ShareRelayParams();
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    if (!GetString(root, "accessid", out.m_accessId) || !GetOptionalString(root, "transferid", out.m_transferId)
        || !GetString(root, "relayip", out.m_relayIp) || !GetInt(root, "relayport", out.m_relayPort)
        || !GetString(root, "token", out.m_token) || !GetInt(root, "role", out.m_role)
        || !GetOptionalInt(root, "lanes", 0, out.m_lanes) || !GetOptionalInt(root, "opentimeoutms", 0, out.m_openTimeoutMs)
        || !GetOptionalInt(root, "idletimeoutms", 0, out.m_idleTimeoutMs)
        || !GetOptionalInt(root, "direction", 0, out.m_direction)
        || !GetOptionalString(root, "p2psessionid", out.m_p2pSessionId))
        return false;

    //带 shareid 的是推给分享者的那一帧
    out.m_forOwner = root.isMember("shareid");
    if (!out.m_forOwner)
        return true;
    out.m_shareId = GetId(root, "shareid");
    out.m_visitorId = GetId(root, "visitorid");
    return out.m_shareId > 0 && out.m_visitorId > 0 && GetOptionalString(root, "visitorname", out.m_visitorName)
        && GetOptionalInt(root, "perms", 0, out.m_perms)
        && GetOptionalInt(root, "searchmaxresults", 0, out.m_searchMaxResults)
        && GetOptionalInt(root, "searchtimeoutms", 0, out.m_searchTimeoutMs);
}

bool ShareProtocolCodec::ParseAccessEnded(const std::string& json, std::string& accessId, std::string& transferId,
                                          int& errCode)
{
    Json::Value root;
    return ParseObject(json, root) && GetString(root, "accessid", accessId)
        && GetOptionalString(root, "transferid", transferId) && GetOptionalInt(root, "errcode", 0, errCode);
}

bool ShareProtocolCodec::ParseCardContent(const std::string& json, ShareCardInfo& out)
{
    out = ShareCardInfo();
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    out.m_shareId = GetId(root, "shareid");
    out.m_ownerId = GetId(root, "ownerid");
    if (out.m_shareId <= 0 || out.m_ownerId <= 0)
        return false;
    if (!GetInt(root, "kind", out.m_kind) || !GetString(root, "name", out.m_name)
        || !GetOptionalString(root, "displaypath", out.m_displayPath))
        return false;

    //合并字段：服务端按看的人合并后才有
    out.m_hasState = root.isMember("sharestate");
    if (out.m_hasState && (!GetInt(root, "sharestate", out.m_state) || !GetOptionalInt(root, "sharereason", 0, out.m_reason)))
        return false;
    out.m_hasPerms = root.isMember("myperms");
    if (out.m_hasPerms && !GetInt(root, "myperms", out.m_myPerms))
        return false;
    return true;
}

} // namespace fileshare
