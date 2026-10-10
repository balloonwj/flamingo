/**
 *  v2 群文件库信令报文编解码（客户端侧）实现，GroupFileProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "GroupFileProtocolClient.h"
#include <stdlib.h>                     // atoll
#include "../third-party/jsoncpp-1.9.0/json.h"

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
//v2 约定大整数ID以字符串编码进 JSON，此处对服务端可能写成数字的情形也做容错。
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

//从一个 JSON 对象解析出一条群文件条目（应答列表项 / file / 推送 file 共用）。
//字段名与服务端 GroupFileProtocol.cpp 的 fillEntryValue 逐字对齐。
static void parseEntry(const Json::Value& jo, GroupFileEntry& e)
{
    e.m_entryid      = jsonToStringId(jo["entryid"]);
    e.m_fileid       = jsonToStringId(jo["fileid"]);
    e.m_name         = jo["name"].isString() ? jo["name"].asString() : std::string();
    e.m_ext          = jo["ext"].isString() ? jo["ext"].asString() : std::string();
    e.m_size         = jsonToInt64(jo["size"]);
    e.m_md5          = jo["md5"].isString() ? jo["md5"].asString() : std::string();
    e.m_filetype     = jo["filetype"].isIntegral() ? jo["filetype"].asInt() : 0;
    e.m_uploaderid   = jsonToStringId(jo["uploaderid"]);
    e.m_uploadername = jo["uploadername"].isString() ? jo["uploadername"].asString() : std::string();
    e.m_uploadtime   = jsonToInt64(jo["uploadtime"]);
}

std::string GroupFileProtocolClient::buildGetGroupFilesRequest(int64_t groupid, int filetype,
                                                               int offset, int count)
{
    Json::Value root;
    //群ID以字符串编码（服务端 getStringId 解析）
    root["groupid"]  = std::to_string(groupid);
    //filetype / offset / count 即便取缺省值也照常写出，使报文字段稳定
    root["filetype"] = filetype;
    root["offset"]   = offset;
    root["count"]    = count;
    return writeCompact(root);
}

bool GroupFileProtocolClient::parseGetGroupFilesResponse(const std::string& jsonbody,
                                                         GetGroupFilesResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    //缺少整型 code 视为非法应答
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //以下字段失败应答可能缺省，按容错取值，不影响返回 true
    out.m_groupid  = jsonToStringId(root["groupid"]);
    out.m_filetype = root["filetype"].isIntegral() ? root["filetype"].asInt() : 0;
    out.m_offset   = root["offset"].isIntegral() ? root["offset"].asInt() : 0;
    out.m_total    = root["total"].isIntegral() ? root["total"].asInt() : 0;
    out.m_hasmore  = root["hasmore"].isBool() ? root["hasmore"].asBool() : false;

    out.m_files.clear();
    const Json::Value& files = root["files"];
    if (files.isArray())
    {
        out.m_files.reserve(files.size());
        for (Json::ArrayIndex i = 0; i < files.size(); ++i)
        {
            if (!files[i].isObject())
                continue;
            GroupFileEntry e;
            parseEntry(files[i], e);
            out.m_files.push_back(e);
        }
    }
    return true;
}

std::string GroupFileProtocolClient::buildAddGroupFileRequest(int64_t groupid, const std::string& fileid)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    //fileid 来自上传应答、本就是字符串编码，原样写入
    root["fileid"]  = fileid;
    return writeCompact(root);
}

bool GroupFileProtocolClient::parseAddGroupFileResponse(const std::string& jsonbody,
                                                        AddGroupFileResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //成功应答携 file 详情；失败应答可能无 file，按对象存在与否决定是否解析
    if (root["file"].isObject())
        parseEntry(root["file"], out.m_file);
    return true;
}

std::string GroupFileProtocolClient::buildDeleteGroupFileRequest(int64_t groupid, const std::string& entryid)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    //entryid 来自列表项、本就是字符串编码，原样写入
    root["entryid"] = entryid;
    return writeCompact(root);
}

bool GroupFileProtocolClient::parseDeleteGroupFileResponse(const std::string& jsonbody,
                                                           DeleteGroupFileResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    //成功应答回带 entryid（字符串或整型编码）；失败应答可能无此字段，容错取空。
    out.m_entryid = jsonToStringId(root["entryid"]);
    return true;
}

bool GroupFileProtocolClient::parseGroupFilesChangedNotify(const std::string& jsonbody,
                                                           GroupFilesChangedNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    //推送须含字符串 groupid 与整型 changetype，否则视为非法
    if (!root["groupid"].isString() && !root["groupid"].isIntegral())
        return false;
    if (!root["changetype"].isIntegral())
        return false;

    out.m_groupid    = jsonToStringId(root["groupid"]);
    out.m_changetype = root["changetype"].asInt();

    //新增携 file 详情；删除携 entryid。各取对应字段，另一字段保持默认
    if (root["file"].isObject())
        parseEntry(root["file"], out.m_file);
    out.m_entryid = jsonToStringId(root["entryid"]);
    return true;
}

std::string GroupFileProtocolClient::buildSearchGroupFilesRequest(const std::string& keywordUtf8, int limit)
{
    Json::Value root;
    root["keyword"] = keywordUtf8;
    root["limit"]   = limit;
    return writeCompact(root);
}

bool GroupFileProtocolClient::parseSearchGroupFilesResponse(const std::string& jsonbody,
                                                            SearchGroupFilesResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    //缺少整型 code 视为非法应答
    if (!root["code"].isIntegral())
        return false;

    out.m_code    = root["code"].asInt();
    out.m_msg     = root["msg"].isString() ? root["msg"].asString() : std::string();
    //以下字段失败应答没有，按容错取值，不影响返回 true
    out.m_keyword = root["keyword"].isString() ? root["keyword"].asString() : std::string();
    out.m_hasmore = root["hasmore"].isBool() ? root["hasmore"].asBool() : false;

    out.m_files.clear();
    const Json::Value& files = root["files"];
    if (files.isArray())
    {
        out.m_files.reserve(files.size());
        for (Json::ArrayIndex i = 0; i < files.size(); ++i)
        {
            if (!files[i].isObject())
                continue;
            SearchedGroupFileItem item;
            item.m_groupid = jsonToStringId(files[i]["groupid"]);
            parseEntry(files[i], item.m_entry);
            out.m_files.push_back(item);
        }
    }
    return true;
}

} // namespace net
