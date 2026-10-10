/**
 *  加群链接的报文编解码（客户端侧）实现，GroupLinkProtocolClient.cpp。说明见头文件。
 *  balloonwj@qq.com   2026-10-07
 */
#include "GroupLinkProtocolClient.h"
#include <stdlib.h>
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

namespace {

//链接前缀（小写）：协议名 + 主机名。比较前把输入转成小写
const wchar_t* const kJoinUrlPrefix = L"flamingo://joingroup";

//随机码参数名
const wchar_t* const kCodeParam = L"code=";

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

//取 code / msg 两项；code 不是整数返回 false
bool ReadCodeMsg(const Json::Value& root, int& code, std::string& msg)
{
    if (!root["code"].isInt())
        return false;
    code = root["code"].asInt();
    msg = root["msg"].isString() ? root["msg"].asString() : std::string();
    return true;
}

//读一条链接
void ReadLink(const Json::Value& jo, GroupLinkItem& out)
{
    out.m_id          = ToInt64(jo["id"]);
    out.m_code        = jo["code"].isString() ? jo["code"].asString() : std::string();
    out.m_url         = jo["url"].isString() ? jo["url"].asString() : std::string();
    out.m_groupid     = ToInt64(jo["groupid"]);
    out.m_creator     = ToInt64(jo["creator"]);
    out.m_creatorname = jo["creatorname"].isString() ? jo["creatorname"].asString() : std::string();
    out.m_createtime  = ToInt64(jo["createtime"]);
    out.m_expiretime  = ToInt64(jo["expiretime"]);
}

//ASCII 字母转小写，其余字符原样返回
wchar_t LowerAscii(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
        return static_cast<wchar_t>(c - L'A' + L'a');
    return c;
}

//去掉首尾的空白与引号（命令行参数可能带引号）
std::wstring TrimUrl(const std::wstring& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'"' || s[b] == L'\''
                     || s[b] == L'\r' || s[b] == L'\n'))
    {
        ++b;
    }
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'"' || s[e - 1] == L'\''
                     || s[e - 1] == L'\r' || s[e - 1] == L'\n'))
    {
        --e;
    }
    return s.substr(b, e - b);
}

} // anonymous namespace

std::string GroupLinkProtocolClient::buildCreateRequest(int64_t groupid, int days)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["validdays"] = days;
    return WriteCompact(root);
}

std::string GroupLinkProtocolClient::buildRevokeRequest(int64_t groupid, int64_t id)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    root["id"] = std::to_string(id);
    return WriteCompact(root);
}

std::string GroupLinkProtocolClient::buildCodeRequest(const std::string& code)
{
    Json::Value root;
    root["code"] = code;
    return WriteCompact(root);
}

bool GroupLinkProtocolClient::parseCreateResponse(const std::string& jsonbody, CreateGroupLinkResult& out)
{
    Json::Value root;
    if (!ParseObject(jsonbody, root) || !ReadCodeMsg(root, out.m_code, out.m_msg))
        return false;
    out.m_hasLink = false;
    if (root["link"].isObject())
    {
        ReadLink(root["link"], out.m_link);
        out.m_hasLink = (out.m_link.m_id > 0 && !out.m_link.m_code.empty());
    }
    return true;
}

bool GroupLinkProtocolClient::parseListResponse(const std::string& jsonbody, GroupLinksResult& out)
{
    Json::Value root;
    if (!ParseObject(jsonbody, root) || !ReadCodeMsg(root, out.m_code, out.m_msg))
        return false;
    out.m_groupid = ToInt64(root["groupid"]);
    out.m_links.clear();
    const Json::Value& arr = root["links"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            if (!arr[i].isObject())
                continue;
            GroupLinkItem item;
            ReadLink(arr[i], item);
            if (item.m_id > 0)
                out.m_links.push_back(item);
        }
    }
    return true;
}

bool GroupLinkProtocolClient::parseInfoResponse(const std::string& jsonbody, GroupLinkInfoResult& out)
{
    Json::Value root;
    if (!ParseObject(jsonbody, root) || !ReadCodeMsg(root, out.m_code, out.m_msg))
        return false;
    out.m_groupid     = ToInt64(root["groupid"]);
    out.m_name        = root["name"].isString() ? root["name"].asString() : std::string();
    out.m_facetype    = root["facetype"].isInt() ? root["facetype"].asInt() : 0;
    out.m_customface  = root["customface"].isString() ? root["customface"].asString() : std::string();
    out.m_membercount = root["membercount"].isInt() ? root["membercount"].asInt() : 0;
    out.m_joinmode    = root["joinmode"].isInt() ? root["joinmode"].asInt() : 0;
    out.m_member      = root["member"].isBool() ? root["member"].asBool() : false;
    if (out.m_code != 0)
        return true;
    return out.m_groupid > 0;
}

bool GroupLinkProtocolClient::parseJoinResponse(const std::string& jsonbody, JoinGroupByLinkResult& out)
{
    Json::Value root;
    if (!ParseObject(jsonbody, root) || !ReadCodeMsg(root, out.m_code, out.m_msg))
        return false;
    out.m_groupid = ToInt64(root["groupid"]);
    out.m_joined = root["joined"].isBool() ? root["joined"].asBool() : false;
    return true;
}

bool GroupLinkProtocolClient::isJoinUrl(const std::wstring& text)
{
    const std::wstring s = TrimUrl(text);
    const std::wstring prefix(kJoinUrlPrefix);
    if (s.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (LowerAscii(s[i]) != prefix[i])
            return false;
    }
    //前缀之后只能紧跟 ? 或 /，避免把 flamingo://joingroupxyz 当成加群链接
    if (s.size() == prefix.size())
        return true;
    const wchar_t next = s[prefix.size()];
    return next == L'?' || next == L'/';
}

bool GroupLinkProtocolClient::parseJoinUrl(const std::wstring& url, std::string& code)
{
    code.clear();
    if (!isJoinUrl(url))
        return false;
    const std::wstring s = TrimUrl(url);
    const size_t q = s.find(L'?');
    if (q == std::wstring::npos)
        return false;

    //逐个参数查找 code=，参数名不区分大小写
    const std::wstring param(kCodeParam);
    size_t pos = q + 1;
    while (pos < s.size())
    {
        size_t amp = s.find(L'&', pos);
        if (amp == std::wstring::npos)
            amp = s.size();
        const std::wstring kv = s.substr(pos, amp - pos);
        bool match = kv.size() > param.size();
        for (size_t i = 0; match && i < param.size(); ++i)
        {
            if (LowerAscii(kv[i]) != param[i])
                match = false;
        }
        if (match)
        {
            std::string value;
            for (size_t i = param.size(); i < kv.size(); ++i)
            {
                const wchar_t c = LowerAscii(kv[i]);
                const bool alnum = (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9');
                //随机码只含字母与数字；链接末尾常被带上标点（聊天里写「链接：xxx。」），遇到别的字符就截止
                if (!alnum)
                    break;
                value.push_back(static_cast<char>(c));
            }
            if (value.size() != kGroupLinkCodeLen)
                return false;
            code = value;
            return true;
        }
        pos = amp + 1;
    }
    return false;
}

} // namespace net
