/**
 *  v2 群文件报文编解码实现，GroupFileProtocol.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "GroupFileProtocol.h"
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

//把一条群文件条目写入 Json 对象（应答列表项与推送共用）
static void fillEntryValue(Json::Value& jo, const GroupFileEntry& e)
{
    //int64 一律以字符串编码，避免 JSON number 精度问题
    jo["entryid"]      = std::to_string(e.entryid);
    jo["fileid"]       = std::to_string(e.fileid);
    jo["name"]         = e.name;
    jo["ext"]          = e.ext;
    jo["size"]         = static_cast<Json::Int64>(e.size);
    jo["md5"]          = e.md5;
    jo["filetype"]     = e.filetype;
    jo["uploaderid"]   = std::to_string(e.uploaderid);
    jo["uploadername"] = e.uploadername;
    jo["uploadtime"]   = static_cast<Json::Int64>(e.uploadtime);
}

bool GroupFileProtocol::parseAddGroupFileRequest(const std::string& json, AddGroupFileRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    req.fileid  = getStringId(root, "fileid");
    //群ID与文件ID均必填且为正
    return req.groupid > 0 && req.fileid > 0;
}

bool GroupFileProtocol::parseGetGroupFilesRequest(const std::string& json, GetGroupFilesRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    if (req.groupid <= 0)
        return false;

    //filetype 可缺省；越界（不在 0~6）一律归 0（不筛选，返回全部）
    int filetype = group_file_type_all;
    if (root["filetype"].isInt())
        filetype = root["filetype"].asInt();
    if (filetype < group_file_type_all || filetype > group_file_type_other)
        filetype = group_file_type_all;
    req.filetype = filetype;

    //offset 可缺省；负数归 0
    int offset = 0;
    if (root["offset"].isInt())
        offset = root["offset"].asInt();
    if (offset < 0)
        offset = 0;
    req.offset = offset;

    //count 可缺省：未给或非正取缺省值；超过上限钳到上限
    int count = 0;
    if (root["count"].isInt())
        count = root["count"].asInt();
    if (count <= 0)
        count = GROUP_FILE_PAGE_DEFAULT;
    else if (count > GROUP_FILE_PAGE_MAX)
        count = GROUP_FILE_PAGE_MAX;
    req.count = count;

    return true;
}

bool GroupFileProtocol::parseDeleteGroupFileRequest(const std::string& json, DeleteGroupFileRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.groupid = getStringId(root, "groupid");
    req.entryid = getStringId(root, "entryid");
    //群ID与条目ID均必填且为正
    return req.groupid > 0 && req.entryid > 0;
}

//去掉字符串首尾的空格与制表符（搜索关键字用；中间的空白保留，文件名里可能就有空格）
static std::string trimBlank(const std::string& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t'))
        ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t'))
        --end;
    return s.substr(begin, end - begin);
}

bool GroupFileProtocol::parseSearchGroupFilesRequest(const std::string& json, SearchGroupFilesRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //关键字必填：去掉首尾空白后不能为空，也不能长到不可能命中任何文件名
    if (!root["keyword"].isString())
        return false;
    req.keyword = trimBlank(root["keyword"].asString());
    if (req.keyword.empty() || req.keyword.size() > static_cast<size_t>(GROUP_FILE_SEARCH_KEYWORD_MAX_BYTES))
        return false;

    //limit 可缺省：未给或非正取缺省值；超过上限钳到上限
    int limit = 0;
    if (root["limit"].isInt())
        limit = root["limit"].asInt();
    if (limit <= 0)
        limit = GROUP_FILE_SEARCH_LIMIT_DEFAULT;
    if (limit > GROUP_FILE_SEARCH_LIMIT_MAX)
        limit = GROUP_FILE_SEARCH_LIMIT_MAX;
    req.limit = limit;
    return true;
}

std::string GroupFileProtocol::buildAddGroupFileResponse(const GroupFileEntry& entry)
{
    Json::Value root;
    root["code"] = error_code_ok;
    root["msg"]  = "ok";

    Json::Value file;
    fillEntryValue(file, entry);
    root["file"] = file;

    return writeCompact(root);
}

std::string GroupFileProtocol::buildGetGroupFilesResponse(int64_t groupid, int filetype,
                                                          int offset, int total,
                                                          const std::vector<GroupFileEntry>& files)
{
    Json::Value root;
    root["code"]     = error_code_ok;
    root["msg"]      = "ok";
    root["groupid"]  = std::to_string(groupid);
    root["filetype"] = filetype;
    root["offset"]   = offset;
    root["total"]    = total;
    //还有后续页：本页末尾位置仍小于总数
    root["hasmore"]  = (offset + static_cast<int>(files.size())) < total;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < files.size(); ++i)
    {
        Json::Value jo;
        fillEntryValue(jo, files[i]);
        arr.append(jo);
    }
    root["files"] = arr;

    return writeCompact(root);
}

std::string GroupFileProtocol::buildDeleteGroupFileResponse(int64_t groupid, int64_t entryid)
{
    Json::Value root;
    root["code"]    = error_code_ok;
    root["msg"]     = "ok";
    root["groupid"] = std::to_string(groupid);
    root["entryid"] = std::to_string(entryid);
    return writeCompact(root);
}

std::string GroupFileProtocol::buildSearchGroupFilesResponse(const std::string& keyword, bool hasMore,
                                                             const std::vector<SearchedGroupFile>& files)
{
    Json::Value root;
    root["code"]    = error_code_ok;
    root["msg"]     = "ok";
    root["keyword"] = keyword;
    root["hasmore"] = hasMore;

    //没有命中时也给一个空数组，客户端不必区分「缺字段」与「零条」
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < files.size(); ++i)
    {
        Json::Value jo;
        fillEntryValue(jo, files[i].entry);
        jo["groupid"] = std::to_string(files[i].groupid);
        arr.append(jo);
    }
    root["files"] = arr;

    return writeCompact(root);
}

std::string GroupFileProtocol::buildFileAddedNotify(int64_t groupid, const GroupFileEntry& entry)
{
    Json::Value root;
    root["groupid"]    = std::to_string(groupid);
    root["changetype"] = group_file_change_added;

    Json::Value file;
    fillEntryValue(file, entry);
    root["file"] = file;

    return writeCompact(root);
}

std::string GroupFileProtocol::buildFileRemovedNotify(int64_t groupid, int64_t entryid)
{
    Json::Value root;
    root["groupid"]    = std::to_string(groupid);
    root["changetype"] = group_file_change_removed;
    root["entryid"]    = std::to_string(entryid);
    return writeCompact(root);
}

std::string GroupFileProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

int GroupFileProtocol::classifyFileType(const std::string& ext)
{
    //空扩展名无从判断，归「其它」
    if (ext.empty())
        return group_file_type_other;

    //转为小写后比较，使判定大小写不敏感（如 .PNG 与 .png 同类）
    std::string low;
    low.reserve(ext.size());
    for (size_t i = 0; i < ext.size(); ++i)
    {
        char c = ext[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        low.push_back(c);
    }

    //文档：办公文档与纯文本
    static const char* const kDocExt[] =
    {
        "doc", "docx", "xls", "xlsx", "ppt", "pptx",
        "pdf", "txt", "rtf", "csv", "md"
    };
    //图片
    static const char* const kImageExt[] =
    {
        "jpg", "jpeg", "png", "gif", "bmp", "webp"
    };
    //视频
    static const char* const kVideoExt[] =
    {
        "mp4", "avi", "mov", "mkv", "wmv", "flv"
    };
    //音频
    static const char* const kAudioExt[] =
    {
        "mp3", "wav", "aac", "flac", "ogg", "m4a"
    };
    //压缩包
    static const char* const kArchiveExt[] =
    {
        "zip", "rar", "7z", "tar", "gz"
    };

    for (size_t i = 0; i < sizeof(kDocExt) / sizeof(kDocExt[0]); ++i)
    {
        if (low == kDocExt[i])
            return group_file_type_document;
    }
    for (size_t i = 0; i < sizeof(kImageExt) / sizeof(kImageExt[0]); ++i)
    {
        if (low == kImageExt[i])
            return group_file_type_image;
    }
    for (size_t i = 0; i < sizeof(kVideoExt) / sizeof(kVideoExt[0]); ++i)
    {
        if (low == kVideoExt[i])
            return group_file_type_video;
    }
    for (size_t i = 0; i < sizeof(kAudioExt) / sizeof(kAudioExt[0]); ++i)
    {
        if (low == kAudioExt[i])
            return group_file_type_audio;
    }
    for (size_t i = 0; i < sizeof(kArchiveExt) / sizeof(kArchiveExt[0]); ++i)
    {
        if (low == kArchiveExt[i])
            return group_file_type_archive;
    }
    //未匹配任何已知扩展名
    return group_file_type_other;
}
