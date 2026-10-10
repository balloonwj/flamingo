/**
 *  v2 文件信令报文编解码实现，FileProtocol.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "FileProtocol.h"
#include <stdlib.h>
#include "../jsoncpp1.9.0/json.h"
#include "Protocol.h"   //file_scene 枚举值

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

//判断字符串是否为合法的 32 位十六进制 MD5
static bool isValidMd5Hex(const std::string& s)
{
    if (s.size() != FILE_MD5_HEX_LEN)
        return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        bool isHex = (c >= '0' && c <= '9')
                  || (c >= 'a' && c <= 'f')
                  || (c >= 'A' && c <= 'F');
        if (!isHex)
            return false;
    }
    return true;
}

bool FileProtocol::parseRequestUploadRequest(const std::string& json, RequestUploadRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //scene 必填且须为合法的 file_scene 取值
    if (!root["scene"].isInt())
        return false;
    req.scene = root["scene"].asInt();
    if (req.scene < file_scene_chat_file || req.scene > file_scene_image_msg)
        return false;

    //md5 必填且须为 32 位十六进制
    if (!root["md5"].isString())
        return false;
    req.md5 = root["md5"].asString();
    if (!isValidMd5Hex(req.md5))
        return false;

    //size 必填且须为正
    if (!root["size"].isNumeric())
        return false;
    req.size = root["size"].asInt64();
    if (req.size <= 0)
        return false;

    //name / ext 可缺省
    req.name = root["name"].isString() ? root["name"].asString() : "";
    req.ext  = root["ext"].isString() ? root["ext"].asString() : "";
    return true;
}

bool FileProtocol::parseRequestDownloadRequest(const std::string& json, RequestDownloadRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.fileid = getStringId(root, "fileid");
    if (req.fileid <= 0)
        return false;
    return true;
}

std::string FileProtocol::buildRequestUploadResponse(int64_t fileid, bool exists,
                                                     const std::string& uploadtoken,
                                                     const std::string& host, int port,
                                                     bool tls, int chunksize)
{
    Json::Value root;
    root["code"]   = 0;
    root["msg"]    = "ok";
    root["fileid"] = std::to_string(fileid);
    root["exists"] = exists;

    //秒传命中时，客户端无需上传，故不下发令牌与目标服务器
    if (!exists)
    {
        root["uploadtoken"] = uploadtoken;

        Json::Value server;
        server["host"] = host;
        server["port"] = port;
        server["tls"]  = tls;
        root["server"] = server;

        root["chunksize"] = chunksize;
    }
    return writeCompact(root);
}

std::string FileProtocol::buildRequestDownloadResponse(int64_t fileid,
                                                       const std::string& downloadtoken,
                                                       const std::string& md5, int64_t size,
                                                       const std::string& host, int port,
                                                       bool tls)
{
    Json::Value root;
    root["code"]          = 0;
    root["msg"]           = "ok";
    root["fileid"]        = std::to_string(fileid);
    root["downloadtoken"] = downloadtoken;
    root["md5"]           = md5;
    root["size"]          = static_cast<Json::Int64>(size);

    Json::Value server;
    server["host"] = host;
    server["port"] = port;
    server["tls"]  = tls;
    root["server"] = server;
    return writeCompact(root);
}

std::string FileProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

bool FileProtocol::isNameCheckedScene(int scene)
{
    return scene == file_scene_chat_file || scene == file_scene_group_file;
}
