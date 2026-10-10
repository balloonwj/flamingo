/**
 *  FlamingoLink.dll —— 管理端口报文与纯规则的实现，说明见 LinkProtocol.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/Core/LinkProtocol.h"

#include <stdlib.h>
#include <windows.h>

#include "json.h"
//错误码取值与服务端一致，取自 PC 客户端那份协议头（与服务端那份由 PP 组测试保证一致）
#include "../../../flamingoclient/Source/net/Protocol.h"

namespace flamingolink {

namespace {

//百分比的分母
const int kPercentBase = 100;

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为对象；解析失败或不是对象时返回 false
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//判断文本是否全由十进制数字组成（空串返回 false）
bool IsAllDigits(const std::string& text)
{
    if (text.empty())
        return false;

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }
    return true;
}

//读取字符串型的正整数 ID；字段缺失、不是字符串、不是正整数时返回 false
bool ReadPositiveIdText(const Json::Value& root, const char* key, int64_t& out)
{
    out = 0;
    if (!root.isMember(key) || !root[key].isString())
        return false;

    const std::string text = root[key].asString();
    if (!IsAllDigits(text))
        return false;

    const int64_t id = static_cast<int64_t>(_atoi64(text.c_str()));
    if (id <= 0)
        return false;

    out = id;
    return true;
}

//读取字符串型的 ID，允许为 "0"（平台超管的企业ID）；字段缺失或格式不对时为 0
int64_t ReadIdTextOrZero(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isString())
        return 0;

    const std::string text = root[key].asString();
    if (!IsAllDigits(text))
        return 0;
    return static_cast<int64_t>(_atoi64(text.c_str()));
}

//读取整数字段；缺失或不是整数时返回 defaultValue
int ReadInt(const Json::Value& root, const char* key, int defaultValue)
{
    if (!root.isMember(key) || !root[key].isInt())
        return defaultValue;
    return root[key].asInt();
}

//读取字符串字段并转成 CString；缺失或不是字符串时为空串
CString ReadText(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isString())
        return CString();
    return Utf8ToCString(root[key].asString());
}

//把 v 夹到 [lo, hi]
int Clamp(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

//解析一台设备
LinkDevice ParseDevice(const Json::Value& d)
{
    LinkDevice dev;
    if (!d.isObject())
        return dev;

    dev.m_clientType = ReadInt(d, "clienttype", 0);
    dev.m_presence   = ReadInt(d, "presence", 0);
    if (d.isMember("logintime") && (d["logintime"].isInt64() || d["logintime"].isUInt64()))
        dev.m_loginTimeMs = d["logintime"].asInt64();
    dev.m_ip = ReadText(d, "ip");
    return dev;
}

} // anonymous namespace

std::string BuildAuthRequest(const std::string& token, const std::string& clientVersion)
{
    Json::Value root(Json::objectValue);
    root["token"] = token;
    root["clientversion"] = clientVersion;
    return WriteCompact(root);
}

std::string BuildSubscribeRequest(int64_t enterpriseId)
{
    Json::Value root(Json::objectValue);
    char buf[32] = { 0 };
    _i64toa_s(enterpriseId, buf, sizeof(buf), 10);
    root["enterpriseid"] = buf;
    return WriteCompact(root);
}

std::string BuildUnsubscribeRequest()
{
    return WriteCompact(Json::Value(Json::objectValue));
}

bool ParseAuthResponse(const std::string& json, AuthResult& out)
{
    out = AuthResult();

    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    if (!root.isMember("code") || !root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_adminId = ReadIdTextOrZero(root, "adminid");
    out.m_enterpriseId = ReadIdTextOrZero(root, "enterpriseid");
    out.m_heartbeatSec = Clamp(ReadInt(root, "heartbeat", kDefaultHeartbeatSec), kMinHeartbeatSec, kMaxHeartbeatSec);
    out.m_timeoutSec = Clamp(ReadInt(root, "timeout", kDefaultTimeoutSec), kMinTimeoutSec, kMaxTimeoutSec);
    return true;
}

AuthOutcome ClassifyAuth(int code)
{
    switch (code)
    {
    //鉴权成功
    case error_code_ok:
        return auth_ok;

    //token 无效：再试也不会成功，交给宿主回登录页
    case error_code_adminlink_token_invalid:
        return auth_expired;

    //其余（请求格式错误、服务端内部错误等）：可能是暂时的，按重连间隔再试
    default:
        return auth_retry;
    }
}

bool ParseCodeResponse(const std::string& json, int& outCode, int64_t& outEnterpriseId)
{
    outCode = -1;
    outEnterpriseId = 0;

    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    if (!root.isMember("code") || !root["code"].isInt())
        return false;

    outCode = root["code"].asInt();
    outEnterpriseId = ReadIdTextOrZero(root, "enterpriseid");
    return true;
}

bool ParseOnlineNotify(const std::string& json, OnlineNotify& out)
{
    out = OnlineNotify();

    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    if (!ReadPositiveIdText(root, "enterpriseid", out.m_enterpriseId))
        return false;
    if (!root.isMember("users") || !root["users"].isArray())
        return false;

    out.m_reset = (ReadInt(root, "reset", 0) != 0);

    const Json::Value& users = root["users"];
    for (Json::ArrayIndex i = 0; i < users.size(); ++i)
    {
        const Json::Value& u = users[i];
        if (!u.isObject())
            continue;

        LinkUser user;
        if (!ReadPositiveIdText(u, "userid", user.m_userId))
            continue;

        user.m_account  = ReadText(u, "account");
        user.m_nickname = ReadText(u, "nickname");

        if (u.isMember("depts") && u["depts"].isArray())
        {
            const Json::Value& depts = u["depts"];
            for (Json::ArrayIndex k = 0; k < depts.size(); ++k)
            {
                if (depts[k].isString())
                    user.m_depts.push_back(Utf8ToCString(depts[k].asString()));
            }
        }

        if (u.isMember("devices") && u["devices"].isArray())
        {
            const Json::Value& devices = u["devices"];
            for (Json::ArrayIndex k = 0; k < devices.size(); ++k)
                user.m_devices.push_back(ParseDevice(devices[k]));
        }

        out.m_users.push_back(user);
    }
    return true;
}

int ComputeRetryDelayMs(int attempt, double randomUnit)
{
    if (attempt < 1)
        attempt = 1;
    if (randomUnit < 0.0)
        randomUnit = 0.0;
    if (randomUnit >= 1.0)
        randomUnit = 0.999999;

    //基准值逐次翻倍，到上限后不再增长（先判上限，避免移位溢出）
    int base = kRetryBaseMs;
    for (int i = 1; i < attempt && base < kRetryMaxMs; ++i)
        base *= 2;
    if (base > kRetryMaxMs)
        base = kRetryMaxMs;

    //浮动范围 [base * (1 - p), base * (1 + p))
    const double low = base * (kPercentBase - kRetryJitterPercent) / static_cast<double>(kPercentBase);
    const double span = base * (2 * kRetryJitterPercent) / static_cast<double>(kPercentBase);
    return static_cast<int>(low + span * randomUnit);
}

int64_t DesiredEnterprise(bool pageActive, int64_t enterpriseId)
{
    if (!pageActive || enterpriseId <= 0)
        return 0;
    return enterpriseId;
}

SubscribeAction DecideSubscription(int64_t desired, int64_t sent, bool generationChanged)
{
    if (desired == 0)
        return (sent == 0) ? subscribe_none : subscribe_unsubscribe;
    if (desired != sent || generationChanged)
        return subscribe_send;
    return subscribe_none;
}

bool ParseEnterpriseIdText(const wchar_t* text, int64_t& out)
{
    out = 0;
    if (text == NULL || text[0] == L'\0')
        return false;

    for (const wchar_t* p = text; *p != L'\0'; ++p)
    {
        if (*p < L'0' || *p > L'9')
            return false;
    }

    const int64_t id = _wtoi64(text);
    if (id <= 0)
        return false;

    out = id;
    return true;
}

CString Utf8ToCString(const std::string& text)
{
    if (text.empty())
        return CString();

    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0);
    if (len <= 0)
        return CString();

    CString out;
    wchar_t* buf = out.GetBuffer(len);
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), buf, len);
    out.ReleaseBuffer(len);
    return out;
}

std::string CStringToUtf8(const CString& text)
{
    if (text.IsEmpty())
        return std::string();

    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(), NULL, 0, NULL, NULL);
    if (len <= 0)
        return std::string();

    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.GetString(), text.GetLength(), &out[0], len, NULL, NULL);
    return out;
}

} // namespace flamingolink
