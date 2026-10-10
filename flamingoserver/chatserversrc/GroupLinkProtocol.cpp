/**
 *  加群链接的纯逻辑与报文编解码实现，GroupLinkProtocol.cpp。说明见 GroupLinkProtocol.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "GroupLinkProtocol.h"

#include <random>
#include <stdlib.h>
#include <string.h>
#include "../jsoncpp1.9.0/json.h"

namespace {

//把 Json::Value 序列化为紧凑的 JSON 文本
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 文本解析成对象；解析失败或不是对象返回 false
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取 64 位 ID：字符串按十进制解析，整数直接取；缺失或类型不对返回 0
int64_t GetId(const Json::Value& root, const char* key)
{
    if (root[key].isString())
        return static_cast<int64_t>(atoll(root[key].asString().c_str()));
    if (root[key].isIntegral())
        return root[key].asInt64();
    return 0;
}

//一条链接转成 JSON
Json::Value LinkToJson(const GroupLinkRecord& r)
{
    Json::Value jo;
    jo["id"]          = std::to_string(r.id);
    jo["code"]        = r.code;
    jo["url"]         = GroupLinkProtocol::buildUrl(r.code);
    jo["groupid"]     = std::to_string(r.groupid);
    jo["creator"]     = std::to_string(r.creator);
    jo["creatorname"] = r.creatorname;
    jo["createtime"]  = static_cast<Json::Int64>(r.createtime);
    jo["expiretime"]  = static_cast<Json::Int64>(r.expiretime);
    return jo;
}

} // anonymous namespace

std::string GroupLinkProtocol::makeCodeFromBytes(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() < kGroupLinkCodeLength)
        return std::string();
    const size_t alphabetLen = strlen(kGroupLinkCodeAlphabet);
    std::string code;
    for (size_t i = 0; i < kGroupLinkCodeLength; ++i)
        code.push_back(kGroupLinkCodeAlphabet[bytes[i] % alphabetLen]);
    return code;
}

std::string GroupLinkProtocol::makeRandomCode()
{
    //std::random_device 在 Windows 与 Linux 上都取自系统的密码学随机源
    std::random_device rd;
    std::vector<unsigned char> bytes;
    const unsigned int kByteMask = 0xFF;
    for (size_t i = 0; i < kGroupLinkCodeLength; ++i)
        bytes.push_back(static_cast<unsigned char>(rd() & kByteMask));
    return makeCodeFromBytes(bytes);
}

bool GroupLinkProtocol::isValidCode(const std::string& code)
{
    if (code.size() != kGroupLinkCodeLength)
        return false;
    for (size_t i = 0; i < code.size(); ++i)
    {
        if (strchr(kGroupLinkCodeAlphabet, code[i]) == NULL || code[i] == '\0')
            return false;
    }
    return true;
}

bool GroupLinkProtocol::isValidDays(int days)
{
    return days == kGroupLinkDaysOne || days == kGroupLinkDaysSeven || days == kGroupLinkDaysForever;
}

int64_t GroupLinkProtocol::computeExpire(int64_t nowMs, int days)
{
    if (days == kGroupLinkDaysForever)
        return 0;
    return nowMs + static_cast<int64_t>(days) * kGroupLinkMillisPerDay;
}

bool GroupLinkProtocol::isUsable(const GroupLinkRecord& rec, int64_t nowMs)
{
    if (rec.revoked != 0)
        return false;
    return rec.expiretime == 0 || nowMs < rec.expiretime;
}

std::string GroupLinkProtocol::buildUrl(const std::string& code)
{
    return std::string(kGroupLinkUrlPrefix) + code;
}

bool GroupLinkProtocol::parseCreateRequest(const std::string& json, int64_t& groupid, int& days)
{
    groupid = 0;
    days = kGroupLinkDaysSeven;
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    groupid = GetId(root, "groupid");
    if (root["validdays"].isIntegral())
        days = root["validdays"].asInt();
    return groupid > 0 && isValidDays(days);
}

bool GroupLinkProtocol::parseRevokeRequest(const std::string& json, int64_t& groupid, int64_t& id)
{
    groupid = 0;
    id = 0;
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    groupid = GetId(root, "groupid");
    id = GetId(root, "id");
    return groupid > 0 && id > 0;
}

bool GroupLinkProtocol::parseCodeRequest(const std::string& json, std::string& code)
{
    code.clear();
    Json::Value root;
    if (!ParseObject(json, root) || !root["code"].isString())
        return false;
    code = root["code"].asString();
    //随机码只有小写字母与数字；从浏览器、邮件里抄来的可能被改成了大写
    for (size_t i = 0; i < code.size(); ++i)
    {
        if (code[i] >= 'A' && code[i] <= 'Z')
            code[i] = static_cast<char>(code[i] - 'A' + 'a');
    }
    return isValidCode(code);
}

std::string GroupLinkProtocol::buildCreateResponse(int code, const std::string& msg, const GroupLinkRecord* rec)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (rec != NULL)
        root["link"] = LinkToJson(*rec);
    return WriteCompact(root);
}

std::string GroupLinkProtocol::buildListResponse(int code, const std::string& msg, int64_t groupid,
                                                 const std::vector<GroupLinkRecord>& links)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    root["groupid"] = std::to_string(groupid);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < links.size(); ++i)
        arr.append(LinkToJson(links[i]));
    root["links"] = arr;
    return WriteCompact(root);
}

std::string GroupLinkProtocol::buildInfoResponse(int code, const std::string& msg, const GroupLinkInfo* info)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (info != NULL)
    {
        root["groupid"]     = std::to_string(info->groupid);
        root["name"]        = info->name;
        root["facetype"]    = info->facetype;
        root["customface"]  = info->customface;
        root["membercount"] = info->membercount;
        root["joinmode"]    = info->joinmode;
        root["member"]      = info->member;
    }
    return WriteCompact(root);
}

std::string GroupLinkProtocol::buildJoinResponse(int code, const std::string& msg, int64_t groupid, bool joined)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    root["groupid"] = std::to_string(groupid);
    root["joined"] = joined;
    return WriteCompact(root);
}
