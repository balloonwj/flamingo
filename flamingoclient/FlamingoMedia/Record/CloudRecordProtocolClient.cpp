/**
 *  云端录像的客户端协议层，见 CloudRecordProtocolClient.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordProtocolClient.h"

#include <stdlib.h>
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "../../Source/net/Protocol.h"

namespace screenrecord {

namespace {

std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

//64 位整数：字符串形式与数值形式都接受；缺失或非法时为 0
int64_t toInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(_strtoi64(v.asCString(), NULL, 10));
    if (v.isInt64())
        return static_cast<int64_t>(v.asInt64());
    if (v.isUInt64())
        return static_cast<int64_t>(v.asUInt64());
    return 0;
}

int toInt(const Json::Value& v, int defaultValue)
{
    return v.isInt() ? v.asInt() : defaultValue;
}

std::string toStr(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

void readRecord(const Json::Value& r, CloudRecordInfo& out)
{
    out.m_recordId = toInt64(r["recordid"]);
    out.m_owner = toInt64(r["owner"]);
    out.m_source = toInt(r["source"], 0);
    out.m_roomId = toStr(r["roomid"]);
    out.m_ctype = toInt(r["ctype"], 0);
    out.m_cid = toInt64(r["cid"]);
    out.m_title = toStr(r["title"]);
    out.m_startTime = toInt64(r["starttime"]);
    out.m_endTime = toInt64(r["endtime"]);
    out.m_durationMs = toInt64(r["durationms"]);
    out.m_bytes = toInt64(r["bytes"]);
    out.m_frecBytes = toInt64(r["frecbytes"]);
    out.m_audioOnly = toInt(r["audioonly"], 0) != 0;
    out.m_status = toInt(r["status"], 0);
    out.m_expireTime = toInt64(r["expiretime"]);
    out.m_createTime = toInt64(r["createtime"]);
}

Json::Value opRoot(int op, int64_t recordId)
{
    Json::Value root(Json::objectValue);
    root["op"] = op;
    root["recordid"] = static_cast<Json::Int64>(recordId);
    return root;
}

} // namespace

CloudRecordInfo::CloudRecordInfo()
    : m_recordId(0)
    , m_owner(0)
    , m_source(0)
    , m_ctype(0)
    , m_cid(0)
    , m_startTime(0)
    , m_endTime(0)
    , m_durationMs(0)
    , m_bytes(0)
    , m_frecBytes(0)
    , m_audioOnly(false)
    , m_status(0)
    , m_expireTime(0)
    , m_createTime(0)
{
}

CloudRecordListResult::CloudRecordListResult()
    : m_code(0)
    , m_hasMore(false)
    , m_enabled(false)
    , m_usedBytes(0)
    , m_quotaBytes(0)
    , m_retainDays(0)
{
}

CloudRecordOpResult::CloudRecordOpResult()
    : m_code(0)
    , m_op(0)
    , m_recordId(0)
    , m_hasRecord(false)
    , m_port(0)
    , m_bytes(0)
{
}

CloudRecordNotify::CloudRecordNotify()
    : m_type(0)
    , m_recordId(0)
    , m_hasRecord(false)
{
}

PendingCloudUpload::PendingCloudUpload()
    : m_recordId(0)
    , m_bytes(0)
    , m_audioOnly(false)
    , m_durationMs(0)
    , m_startTime(0)
{
}

std::string BuildPendingUploadsJson(const std::vector<PendingCloudUpload>& list)
{
    Json::Value root(Json::objectValue);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const PendingCloudUpload& p = list[i];
        if (p.m_recordId <= 0)
            continue;
        Json::Value item(Json::objectValue);
        item["recordid"] = static_cast<Json::Int64>(p.m_recordId);
        item["path"] = p.m_pathUtf8;
        item["bytes"] = static_cast<Json::Int64>(p.m_bytes);
        item["audioonly"] = p.m_audioOnly ? 1 : 0;
        item["durationms"] = static_cast<Json::Int64>(p.m_durationMs);
        item["starttime"] = static_cast<Json::Int64>(p.m_startTime);
        item["title"] = p.m_title;
        arr.append(item);
    }
    root["uploads"] = arr;
    return writeCompact(root);
}

bool ParsePendingUploadsJson(const std::string& json, std::vector<PendingCloudUpload>& out)
{
    out.clear();
    Json::Value root;
    if (!parseObject(json, root) || !root["uploads"].isArray())
        return false;
    const Json::Value& arr = root["uploads"];
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject())
            continue;
        PendingCloudUpload p;
        p.m_recordId = toInt64(item["recordid"]);
        p.m_pathUtf8 = toStr(item["path"]);
        if (p.m_recordId <= 0 || p.m_pathUtf8.empty())
            continue;
        p.m_bytes = toInt64(item["bytes"]);
        p.m_audioOnly = toInt(item["audioonly"], 0) != 0;
        p.m_durationMs = toInt64(item["durationms"]);
        p.m_startTime = toInt64(item["starttime"]);
        p.m_title = toStr(item["title"]);
        out.push_back(p);
    }
    return true;
}

int DecidePendingResume(bool found, int status, bool listComplete, bool fileOk)
{
    if (!found)
        return listComplete ? pending_drop : pending_keep;
    if (status != cloudrecord_status_uploading)
        return pending_drop;
    return fileOk ? pending_resume : pending_drop;
}

std::string BuildCloudRecordList(int64_t beforeId, int limit)
{
    Json::Value root(Json::objectValue);
    root["beforeid"] = static_cast<Json::Int64>(beforeId);
    root["limit"] = limit;
    return writeCompact(root);
}

std::string BuildCloudRecordRename(int64_t recordId, const std::string& titleUtf8)
{
    Json::Value root = opRoot(cloudrecord_op_rename, recordId);
    root["title"] = titleUtf8;
    return writeCompact(root);
}

std::string BuildCloudRecordDelete(int64_t recordId)
{
    return writeCompact(opRoot(cloudrecord_op_delete, recordId));
}

std::string BuildCloudRecordDownloadToken(int64_t recordId, int kind)
{
    Json::Value root = opRoot(cloudrecord_op_download_token, recordId);
    root["kind"] = kind;
    return writeCompact(root);
}

std::string BuildCloudRecordSend(int64_t recordId, int ctype, int64_t cid)
{
    Json::Value root = opRoot(cloudrecord_op_send, recordId);
    root["ctype"] = ctype;
    root["cid"] = static_cast<Json::Int64>(cid);
    return writeCompact(root);
}

std::string BuildCloudRecordUploadToken(int64_t recordId, int64_t bytes, bool audioOnly, const std::string& titleUtf8,
                                        int64_t durationMs, int64_t startTime)
{
    Json::Value root = opRoot(cloudrecord_op_upload_token, recordId);
    root["bytes"] = static_cast<Json::Int64>(bytes);
    root["durationms"] = static_cast<Json::Int64>(durationMs);
    if (recordId == 0)
    {
        root["audioonly"] = audioOnly ? 1 : 0;
        root["title"] = titleUtf8;
        root["starttime"] = static_cast<Json::Int64>(startTime);
    }
    return writeCompact(root);
}

bool ParseCloudRecordList(const std::string& json, CloudRecordListResult& out)
{
    out = CloudRecordListResult();
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    const Json::Value& records = root["records"];
    if (records.isArray())
    {
        for (Json::ArrayIndex i = 0; i < records.size(); ++i)
        {
            if (!records[i].isObject())
                continue;
            CloudRecordInfo info;
            readRecord(records[i], info);
            out.m_records.push_back(info);
        }
    }
    out.m_hasMore = toInt(root["hasmore"], 0) != 0;
    out.m_enabled = toInt(root["enabled"], 0) != 0;
    out.m_usedBytes = toInt64(root["usedbytes"]);
    out.m_quotaBytes = toInt64(root["quotabytes"]);
    out.m_retainDays = toInt64(root["retaindays"]);
    return true;
}

bool ParseCloudRecordOpResult(const std::string& json, CloudRecordOpResult& out)
{
    out = CloudRecordOpResult();
    Json::Value root;
    if (!parseObject(json, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_op = toInt(root["op"], 0);
    out.m_recordId = toInt64(root["recordid"]);
    if (root["record"].isObject())
    {
        out.m_hasRecord = true;
        readRecord(root["record"], out.m_record);
    }
    out.m_token = toStr(root["token"]);
    out.m_host = toStr(root["host"]);
    out.m_port = toInt(root["port"], 0);
    out.m_fileName = toStr(root["filename"]);
    out.m_bytes = toInt64(root["bytes"]);
    return true;
}

bool ParseCloudRecordNotify(const std::string& json, CloudRecordNotify& out)
{
    out = CloudRecordNotify();
    Json::Value root;
    if (!parseObject(json, root) || !root["type"].isInt())
        return false;
    out.m_type = root["type"].asInt();
    out.m_recordId = toInt64(root["recordid"]);
    if (root["record"].isObject())
    {
        out.m_hasRecord = true;
        readRecord(root["record"], out.m_record);
        if (out.m_recordId == 0)
            out.m_recordId = out.m_record.m_recordId;
    }
    return out.m_recordId > 0;
}

} // namespace screenrecord
