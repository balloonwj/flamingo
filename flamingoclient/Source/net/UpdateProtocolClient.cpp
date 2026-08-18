/**
 *  v2 检查客户端更新报文编解码（客户端侧）实现，UpdateProtocolClient.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "UpdateProtocolClient.h"
#include <vector>
//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元
//时 MSVC 会对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地
//屏蔽，不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace net {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//取 JSON 字符串字段；缺失或非字符串返回空串
static std::string jsonStr(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return std::string();
    return root[key].asString();
}

//把点分版本号解析成各段整数；非数字、非点的字符忽略。
//规则与服务端 LoginProtocol::parseVersionSegments 一致。
static void parseVersionSegments(const std::string& v, std::vector<int>& out)
{
    int cur = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const char c = v[i];
        if (c >= '0' && c <= '9')
            cur = cur * 10 + (c - '0');
        else if (c == '.')
        {
            out.push_back(cur);
            cur = 0;
        }
    }
    out.push_back(cur);
}

std::string UpdateProtocolClient::buildCheckUpdateRequest(int platform,
                                                          const std::string& version)
{
    Json::Value root;
    root["platform"] = platform;
    root["version"]  = version;
    return writeCompact(root);
}

bool UpdateProtocolClient::parseCheckUpdateResponse(const std::string& jsonbody,
                                                    CheckUpdateResult& out)
{
    out = CheckUpdateResult();

    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(),
                                  &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //缺 code 视为报文异常：与其当成"成功且无更新"静悄悄放过，不如让调用方看到失败
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = jsonStr(root, "msg");
    if (out.m_code != 0)
        return true;   //业务失败也算解析成功，错误码已带回

    out.m_hasUpdate     = root["hasupdate"].isBool() ? root["hasupdate"].asBool() : false;
    out.m_latestVersion = jsonStr(root, "latestversion");
    if (!out.m_hasUpdate)
        return true;   //无更新时服务端不下发其余字段，不必再取

    out.m_minVersion    = jsonStr(root, "minversion");
    out.m_forced        = root["forced"].isBool() ? root["forced"].asBool() : false;
    out.m_releaseNote   = jsonStr(root, "releasenote");
    out.m_md5           = jsonStr(root, "md5");
    //按 64 位读：升级包超过 2GiB 时 32 位会溢出成负数
    out.m_size          = root["size"].isIntegral() ? root["size"].asInt64() : 0;
    out.m_packageName   = jsonStr(root, "packagename");
    out.m_downloadToken = jsonStr(root, "downloadtoken");

    const Json::Value& server = root["server"];
    if (server.isObject())
    {
        out.m_host = jsonStr(server, "host");
        out.m_port = server["port"].isIntegral() ? server["port"].asInt() : 0;
        out.m_tls  = server["tls"].isBool() ? server["tls"].asBool() : false;
    }
    return true;
}

bool UpdateProtocolClient::isVersionAtLeast(const std::string& ver, const std::string& minver)
{
    std::vector<int> a;
    std::vector<int> b;
    parseVersionSegments(ver, a);
    parseVersionSegments(minver, b);

    const size_t n = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
    {
        //段数不足的一方按 0 补齐："3.2" 等价于 "3.2.0"
        const int av = (i < a.size()) ? a[i] : 0;
        const int bv = (i < b.size()) ? b[i] : 0;
        if (av != bv)
            return av > bv;
    }
    //逐段完全相等，视为满足"不低于"
    return true;
}

} // namespace net
