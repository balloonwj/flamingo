/**
 *  v2 文件 / 图片信令与分片报文编解码（客户端侧）实现，FileProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-03
 */
#include "FileProtocolClient.h"
#include <stdlib.h>                     // atoll
//vendored 的 jsoncpp/json.h 含非 UTF-8 字节，当本文件作为 UTF-8 编译单元
//时 MSVC 会对其报 C4828；该告警仅涉及 json.h 内注释、不影响代码，故就地
//屏蔽，不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace net {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 文本解析成对象 Json::Value；非法 JSON 或顶层非对象返回 false。
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 值取成字符串型 ID：字符串原样返回，整型转十进制字符串，其它返回空串。
//v2 约定文件ID等大整数以字符串编码进 JSON，但为兼容服务端可能写成数字的情形，
//此处对两种写法都做容错。
static std::string jsonToStringId(const Json::Value& v)
{
    if (v.isString())
        return v.asString();
    if (v.isIntegral())
        return std::to_string(v.asInt64());
    return std::string();
}

//取 JSON 整型字段：整型直接取，字符串按十进制解析，其它返回 0。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
        return static_cast<int64_t>(atoll(v.asString().c_str()));
    return 0;
}

std::string FileProtocolClient::buildRequestUploadRequest(int scene, const std::string& md5,
                                                          int64_t size, const std::string& name,
                                                          const std::string& ext)
{
    Json::Value root;
    root["scene"] = scene;
    root["md5"]   = md5;
    //size 为字节数，直接以整型编码（与服务端 isNumeric 解析一致）
    root["size"]  = static_cast<Json::Int64>(size);
    //name / ext 即便为空也照常写出，服务端按缺省处理，便于报文字段稳定
    root["name"]  = name;
    root["ext"]   = ext;
    return writeCompact(root);
}

bool FileProtocolClient::parseRequestUploadResponse(const std::string& jsonbody, RequestUploadResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //code 为必有字段，缺失或非整型视为非法应答
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //失败应答到此为止，其余字段不再解析
    if (out.m_code != 0)
        return true;

    out.m_fileid = jsonToStringId(root["fileid"]);
    out.m_exists = root["exists"].isBool() ? root["exists"].asBool() : false;

    //秒传命中时服务端不下发令牌与目标服务器，相关字段保持默认
    if (out.m_exists)
        return true;

    out.m_uploadtoken = root["uploadtoken"].isString() ? root["uploadtoken"].asString() : std::string();

    //目标服务器是嵌套对象 server{host,port,tls}
    const Json::Value& server = root["server"];
    if (server.isObject())
    {
        out.m_host = server["host"].isString() ? server["host"].asString() : std::string();
        out.m_port = server["port"].isInt() ? server["port"].asInt() : 0;
        out.m_tls  = server["tls"].isBool() ? server["tls"].asBool() : false;
    }

    out.m_chunksize = root["chunksize"].isInt() ? root["chunksize"].asInt() : 0;
    return true;
}

std::string FileProtocolClient::buildRequestDownloadRequest(const std::string& fileid)
{
    Json::Value root;
    //fileid 以字符串编码，避免大整数精度丢失（与服务端 getStringId 解析一致）
    root["fileid"] = fileid;
    return writeCompact(root);
}

bool FileProtocolClient::parseRequestDownloadResponse(const std::string& jsonbody, RequestDownloadResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    if (out.m_code != 0)
        return true;

    out.m_fileid        = jsonToStringId(root["fileid"]);
    out.m_downloadtoken = root["downloadtoken"].isString() ? root["downloadtoken"].asString() : std::string();
    out.m_md5           = root["md5"].isString() ? root["md5"].asString() : std::string();
    out.m_size          = jsonToInt64(root["size"]);

    const Json::Value& server = root["server"];
    if (server.isObject())
    {
        out.m_host = server["host"].isString() ? server["host"].asString() : std::string();
        out.m_port = server["port"].isInt() ? server["port"].asInt() : 0;
        out.m_tls  = server["tls"].isBool() ? server["tls"].asBool() : false;
    }
    return true;
}

std::string FileProtocolClient::buildUploadChunkDescriptor(const std::string& uploadtoken, int64_t offset)
{
    Json::Value root;
    root["uploadtoken"] = uploadtoken;
    root["offset"]      = static_cast<Json::Int64>(offset);
    return writeCompact(root);
}

bool FileProtocolClient::parseUploadChunkAck(const std::string& jsonbody, UploadChunkAck& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    out.m_done = root["done"].isBool() ? root["done"].asBool() : false;
    //nextoffset 为整型字节偏移；错误应答可能不带此字段，缺省 0
    out.m_nextoffset = jsonToInt64(root["nextoffset"]);
    return true;
}

std::string FileProtocolClient::buildDownloadChunkDescriptor(const std::string& downloadtoken, int64_t offset)
{
    Json::Value root;
    root["downloadtoken"] = downloadtoken;
    root["offset"]        = static_cast<Json::Int64>(offset);
    return writeCompact(root);
}

bool FileProtocolClient::parseDownloadChunkInfo(const std::string& descriptor, DownloadChunkInfo& out)
{
    Json::Value root;
    if (!parseObject(descriptor, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    if (out.m_code != 0)
        return true;

    out.m_offset    = jsonToInt64(root["offset"]);
    out.m_totalsize = jsonToInt64(root["totalsize"]);
    out.m_done      = root["done"].isBool() ? root["done"].asBool() : false;
    return true;
}

} // namespace net
