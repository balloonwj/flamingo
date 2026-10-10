/**
 *  云端录制的协议层，格式见 CloudRecordProtocol.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordProtocol.h"

#include "Protocol.h"

namespace {

std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析 JSON 对象；空包体视为空对象
bool parseObject(const std::string& json, Json::Value& root)
{
    if (json.empty())
    {
        root = Json::Value(Json::objectValue);
        return true;
    }
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//读整数字段：缺失或类型不对时返回 defaultValue
int64_t readInt64(const Json::Value& root, const char* key, int64_t defaultValue)
{
    const Json::Value& v = root[key];
    return v.isInt64() ? v.asInt64() : defaultValue;
}

int readInt(const Json::Value& root, const char* key, int defaultValue)
{
    const Json::Value& v = root[key];
    return v.isInt() ? v.asInt() : defaultValue;
}

} // namespace

CloudRecordOpRequest::CloudRecordOpRequest()
    : m_op(0)
    , m_recordId(0)
    , m_kind(0)
    , m_ctype(0)
    , m_cid(0)
    , m_bytes(0)
    , m_audioOnly(false)
    , m_durationMs(0)
    , m_startTime(0)
{
}

CloudRecordTokenInfo::CloudRecordTokenInfo()
    : m_port(0)
    , m_bytes(0)
{
}

bool CloudRecordProtocol::parseControlRequest(const std::string& json, std::string& roomId, int& action)
{
    Json::Value root;
    if (!parseObject(json, root) || !root["roomid"].isString())
        return false;
    roomId = root["roomid"].asString();
    action = readInt(root, "action", 0);
    return !roomId.empty() && action >= cloudrecord_control_start && action <= cloudrecord_control_stop;
}

std::string CloudRecordProtocol::buildControlResponse(int code, const std::string& msg, const std::string& roomId,
                                                      int action, int64_t recordId)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"] = msg;
    root["roomid"] = roomId;
    root["action"] = action;
    if (recordId > 0)
        root["recordid"] = static_cast<Json::Int64>(recordId);
    return writeCompact(root);
}

bool CloudRecordProtocol::parseListRequest(const std::string& json, int64_t& beforeId, int& limit)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    beforeId = readInt64(root, "beforeid", 0);
    if (beforeId < 0)
        beforeId = 0;
    limit = readInt(root, "limit", CloudRecordRules::kListPageDefault);
    if (limit <= 0)
        limit = CloudRecordRules::kListPageDefault;
    if (limit > CloudRecordRules::kListPageMax)
        limit = CloudRecordRules::kListPageMax;
    return true;
}

Json::Value CloudRecordProtocol::recordToJson(const CloudRecordRow& row)
{
    Json::Value r(Json::objectValue);
    r["recordid"] = static_cast<Json::Int64>(row.m_id);
    r["owner"] = static_cast<Json::Int64>(row.m_owner);
    r["source"] = row.m_source;
    r["roomid"] = row.m_roomId;
    r["ctype"] = row.m_ctype;
    r["cid"] = static_cast<Json::Int64>(row.m_cid);
    r["title"] = row.m_title;
    r["starttime"] = static_cast<Json::Int64>(row.m_startTime);
    r["endtime"] = static_cast<Json::Int64>(row.m_endTime);
    r["durationms"] = static_cast<Json::Int64>(row.m_durationMs);
    r["bytes"] = static_cast<Json::Int64>(row.m_bytes);
    r["frecbytes"] = static_cast<Json::Int64>(row.m_frecBytes);
    r["audioonly"] = row.m_audioOnly ? 1 : 0;
    r["status"] = row.m_status;
    r["expiretime"] = static_cast<Json::Int64>(row.m_expireTime);
    r["createtime"] = static_cast<Json::Int64>(row.m_createTime);
    return r;
}

std::string CloudRecordProtocol::buildListResponse(int code, const std::string& msg,
                                                   const std::vector<CloudRecordRow>& rows, bool hasMore, bool enabled,
                                                   int64_t usedBytes, int64_t quotaBytes, int64_t retainDays)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"] = msg;
    if (code == error_code_ok)
    {
        Json::Value list(Json::arrayValue);
        for (size_t i = 0; i < rows.size(); ++i)
            list.append(recordToJson(rows[i]));
        root["records"] = list;
        root["hasmore"] = hasMore ? 1 : 0;
        root["enabled"] = enabled ? 1 : 0;
        root["usedbytes"] = static_cast<Json::Int64>(usedBytes);
        root["quotabytes"] = static_cast<Json::Int64>(quotaBytes);
        root["retaindays"] = static_cast<Json::Int64>(retainDays);
    }
    return writeCompact(root);
}

bool CloudRecordProtocol::parseOpRequest(const std::string& json, CloudRecordOpRequest& out)
{
    out = CloudRecordOpRequest();
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    out.m_op = readInt(root, "op", 0);
    out.m_recordId = readInt64(root, "recordid", 0);
    switch (out.m_op)
    {
    //重命名：标题的长度与内容由业务层校验
    case cloudrecord_op_rename:
        if (!root["title"].isString())
            return false;
        out.m_title = root["title"].asString();
        return out.m_recordId > 0;

    //删除
    case cloudrecord_op_delete:
        return out.m_recordId > 0;

    //取下载令牌
    case cloudrecord_op_download_token:
        out.m_kind = readInt(root, "kind", 0);
        return out.m_recordId > 0 && (out.m_kind == cloudrecord_file_frec || out.m_kind == cloudrecord_file_final);

    //取上传令牌：recordid 为 0 时新建一条来源为上传的录像
    case cloudrecord_op_upload_token:
        out.m_bytes = readInt64(root, "bytes", 0);
        out.m_durationMs = readInt64(root, "durationms", 0);
        out.m_startTime = readInt64(root, "starttime", 0);
        out.m_audioOnly = readInt(root, "audioonly", 0) != 0;
        if (root["title"].isString())
            out.m_title = root["title"].asString();
        return out.m_recordId >= 0 && out.m_bytes > 0 && out.m_durationMs >= 0;

    //发到会话
    case cloudrecord_op_send:
        out.m_ctype = readInt(root, "ctype", 0);
        out.m_cid = readInt64(root, "cid", 0);
        return out.m_recordId > 0 && (out.m_ctype == conversation_single || out.m_ctype == conversation_group) &&
               out.m_cid > 0;

    //不认识的操作
    default:
        return false;
    }
}

std::string CloudRecordProtocol::buildOpResponse(int code, const std::string& msg, int op, int64_t recordId,
                                                 const CloudRecordRow* row, const CloudRecordTokenInfo* token)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"] = msg;
    root["op"] = op;
    root["recordid"] = static_cast<Json::Int64>(recordId);
    if (row != NULL)
        root["record"] = recordToJson(*row);
    if (token != NULL)
    {
        root["token"] = token->m_token;
        root["host"] = token->m_host;
        root["port"] = token->m_port;
        root["filename"] = token->m_fileName;
        root["bytes"] = static_cast<Json::Int64>(token->m_bytes);
    }
    return writeCompact(root);
}

std::string CloudRecordProtocol::buildRecordNotify(int type, const CloudRecordRow& row)
{
    Json::Value root(Json::objectValue);
    root["type"] = type;
    root["recordid"] = static_cast<Json::Int64>(row.m_id);
    root["record"] = recordToJson(row);
    return writeCompact(root);
}

std::string CloudRecordProtocol::buildDeleteNotify(int64_t recordId)
{
    Json::Value root(Json::objectValue);
    root["type"] = cloudrecord_notify_delete;
    root["recordid"] = static_cast<Json::Int64>(recordId);
    return writeCompact(root);
}
