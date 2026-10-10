/**
 *  文件共享控制通道应用帧编解码 ShareControlFraming 的实现，见 ShareControlFraming.h。
 *
 *  解码时取字段一律先判类型再取值：包体来自另一端的客户端，jsoncpp 的 asString() / asInt64() 在类型不符时
 *  会抛异常，而解码发生在中转通道的接收线程上，一旦抛出就会打断整条通道。任何不合约定的输入都以返回 false
 *  的方式安全失败。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareControlFraming.h"

#include <memory>
#include <utility>
#include "../../Source/net/FrameCodec.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace fileshare {

namespace {

//应用帧固定头部的字节数：1 字节帧类型（其后的 4 字节长度由 encodeAttachmentBody 写入）
const size_t kTypeBytes = 1;

//按批拆分时给 JSON 外层字段（reqid、code、cursor、more 等）与条目之间的逗号预留的字节数
const size_t kBatchOverheadBytes = 1024;

//列目录游标（十进制条目下标）的最大位数：9 位足以表示任何实际目录的条目数，又不会让 size_t 溢出
const size_t kShareListCursorMaxDigits = 9;

//十进制的进位基数（解析游标用）
const size_t kDecimalBase = 10;

//st_job 的传输方向：1 下载、2 上传（与 Protocol.h 的 share_transfer_direction 一致）
const int kDirectionDownload = 1;
const int kDirectionUpload = 2;

//一个帧类型是否在 share_frame_type 之内
bool IsKnownType(int type)
{
    switch (type)
    {
    //控制通道的各种帧
    case sf_hello:
    case sf_list:
    case sf_list_result:
    case sf_stat:
    case sf_stat_result:
    case sf_search:
    case sf_search_result:
    case sf_cancel:
    case sf_thumb:
    case sf_thumb_result:
    case sf_mkdir:
    case sf_rename:
    case sf_delete:
    case sf_move:
    case sf_op_result:
    case sf_perms:
    case sf_bye:
    //传输子会话的首帧及其应答
    case st_job:
    case st_job_ack:
        return true;
    //其余取值（含文件夹传输的 ft_*）都不是本模块的帧
    default:
        return false;
    }
}

//把 Json::Value 写成紧凑 JSON；非 ASCII 字符按 UTF-8 原样写出，不转成 \u 转义，节省单帧空间
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    wb["emitUTF8"] = true;
    return Json::writeString(wb, v);
}

//取必填的整数（可为 64 位）
bool GetInt64(const Json::Value& root, const char* key, int64_t& out)
{
    if (!root.isMember(key) || !root[key].isInt64())
        return false;
    out = root[key].asInt64();
    return true;
}

//取必填的 32 位整数
bool GetInt(const Json::Value& root, const char* key, int& out)
{
    if (!root.isMember(key) || !root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//取选填的 32 位整数：缺失时取默认值，存在但类型不对时失败
bool GetOptionalInt(const Json::Value& root, const char* key, int defaultValue, int& out)
{
    if (!root.isMember(key))
    {
        out = defaultValue;
        return true;
    }
    return GetInt(root, key, out);
}

//取选填的 64 位整数
bool GetOptionalInt64(const Json::Value& root, const char* key, int64_t& out)
{
    if (!root.isMember(key))
    {
        out = 0;
        return true;
    }
    return GetInt64(root, key, out);
}

//取必填的字符串
bool GetString(const Json::Value& root, const char* key, std::string& out)
{
    if (!root.isMember(key) || !root[key].isString())
        return false;
    out = root[key].asString();
    return true;
}

//取选填的字符串：缺失时为空串
bool GetOptionalString(const Json::Value& root, const char* key, std::string& out)
{
    if (!root.isMember(key))
    {
        out.clear();
        return true;
    }
    return GetString(root, key, out);
}

//取选填的布尔值：缺失时为假
bool GetOptionalBool(const Json::Value& root, const char* key, bool& out)
{
    if (!root.isMember(key))
    {
        out = false;
        return true;
    }
    if (!root[key].isBool())
        return false;
    out = root[key].asBool();
    return true;
}

//取必填的字符串数组
bool GetStringArray(const Json::Value& root, const char* key, std::vector<std::string>& out)
{
    out.clear();
    if (!root.isMember(key) || !root[key].isArray())
        return false;
    const Json::Value& arr = root[key];
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (!arr[i].isString())
            return false;
        out.push_back(arr[i].asString());
    }
    return true;
}

//条目在 JSON 里用作名字的字段：列目录与查条目为 name，搜索命中为 path
const char* EntryNameKey(bool isSearchHit)
{
    return isSearchHit ? "path" : "name";
}

//把一个条目写成 JSON 对象；搜索命中不写 attrs
Json::Value EntryToJson(const ShareEntry& e, bool isSearchHit)
{
    Json::Value v(Json::objectValue);
    v[EntryNameKey(isSearchHit)] = e.m_name;
    v["isdir"] = e.m_isDir;
    v["size"] = Json::Int64(e.m_size);
    v["mtime"] = Json::Int64(e.m_mtime);
    if (!isSearchHit)
        v["attrs"] = e.m_attrs;
    return v;
}

//解析一个条目：名字与 isdir 必填，其余选填
bool EntryFromJson(const Json::Value& v, bool isSearchHit, ShareEntry& e)
{
    if (!v.isObject())
        return false;
    if (!GetString(v, EntryNameKey(isSearchHit), e.m_name))
        return false;
    if (!v.isMember("isdir") || !v["isdir"].isBool())
        return false;
    e.m_isDir = v["isdir"].asBool();
    return GetOptionalInt64(v, "size", e.m_size) && GetOptionalInt64(v, "mtime", e.m_mtime)
        && GetOptionalInt(v, "attrs", 0, e.m_attrs);
}

//解析条目数组（必填）
bool EntriesFromJson(const Json::Value& root, bool isSearchHit, std::vector<ShareEntry>& out)
{
    out.clear();
    if (!root.isMember("entries") || !root["entries"].isArray())
        return false;
    const Json::Value& arr = root["entries"];
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        ShareEntry e;
        if (!EntryFromJson(arr[i], isSearchHit, e))
            return false;
        out.push_back(e);
    }
    return true;
}

//字符串数组写成 JSON 数组
Json::Value StringsToJson(const std::vector<std::string>& items)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
        arr.append(items[i]);
    return arr;
}

//按帧类型生成描述符；类型非法时返回 false
bool BuildDescriptor(const ShareControlMessage& m, Json::Value& root)
{
    root = Json::Value(Json::objectValue);
    switch (m.m_type)
    {
    //握手首帧
    case sf_hello:
        root["version"] = m.m_version;
        root["kind"] = m.m_kind;
        root["rootname"] = m.m_rootName;
        root["includehidden"] = m.m_includeHidden;
        root["perms"] = m.m_perms;
        return true;
    //列目录请求
    case sf_list:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["path"] = m.m_path;
        root["cursor"] = m.m_cursor;
        return true;
    //列目录结果
    case sf_list_result:
    {
        root["reqid"] = Json::Int64(m.m_reqId);
        root["code"] = m.m_code;
        Json::Value arr(Json::arrayValue);
        for (size_t i = 0; i < m.m_entries.size(); ++i)
            arr.append(EntryToJson(m.m_entries[i], false));
        root["entries"] = arr;
        root["cursor"] = m.m_cursor;
        root["more"] = m.m_more;
        return true;
    }
    //查条目请求、取消、新建文件夹：只带请求号与（除取消外的）路径
    case sf_stat:
    case sf_mkdir:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["path"] = m.m_path;
        return true;
    case sf_cancel:
        root["reqid"] = Json::Int64(m.m_reqId);
        return true;
    //查条目结果：成功时带一个条目
    case sf_stat_result:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["code"] = m.m_code;
        if (!m.m_entries.empty())
            root["entry"] = EntryToJson(m.m_entries[0], false);
        return true;
    //搜索请求
    case sf_search:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["path"] = m.m_path;
        root["keyword"] = m.m_keyword;
        //列整棵目录树（第 2 期第 5 步补充）：只在需要时带，普通搜索的帧格式不变
        if (m.m_matchAll)
            root["all"] = true;
        if (m.m_maxItems > 0)
            root["max"] = m.m_maxItems;
        return true;
    //搜索结果
    case sf_search_result:
    {
        root["reqid"] = Json::Int64(m.m_reqId);
        Json::Value arr(Json::arrayValue);
        for (size_t i = 0; i < m.m_entries.size(); ++i)
            arr.append(EntryToJson(m.m_entries[i], true));
        root["entries"] = arr;
        root["done"] = m.m_done;
        root["truncated"] = m.m_truncated;
        //结果码只在失败时带（第 2 期第 3 步补充：路径不存在、关键字不合法、没有权限时访问者需要知道原因）
        if (m.m_code != 0)
            root["code"] = m.m_code;
        return true;
    }
    //缩略图请求
    case sf_thumb:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["path"] = m.m_path;
        root["maxedge"] = m.m_maxEdge;
        return true;
    //缩略图结果：JPEG 数据放在附件里
    case sf_thumb_result:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["code"] = m.m_code;
        root["width"] = m.m_width;
        root["height"] = m.m_height;
        return true;
    //重命名
    case sf_rename:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["path"] = m.m_path;
        root["newname"] = m.m_newName;
        return true;
    //删除若干条目
    case sf_delete:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["paths"] = StringsToJson(m.m_paths);
        return true;
    //移动若干条目
    case sf_move:
        root["reqid"] = Json::Int64(m.m_reqId);
        root["paths"] = StringsToJson(m.m_paths);
        root["todir"] = m.m_toDir;
        return true;
    //写操作结果
    case sf_op_result:
    {
        root["reqid"] = Json::Int64(m.m_reqId);
        root["code"] = m.m_code;
        Json::Value arr(Json::arrayValue);
        for (size_t i = 0; i < m.m_items.size(); ++i)
        {
            Json::Value item(Json::objectValue);
            item["path"] = m.m_items[i].m_path;
            item["code"] = m.m_items[i].m_code;
            arr.append(item);
        }
        root["items"] = arr;
        return true;
    }
    //权限变化
    case sf_perms:
        root["perms"] = m.m_perms;
        return true;
    //结束访问会话
    case sf_bye:
        root["errcode"] = m.m_errCode;
        return true;
    //传输子会话首帧
    case st_job:
        root["direction"] = m.m_direction;
        root["items"] = StringsToJson(m.m_paths);
        root["todir"] = m.m_toDir;
        return true;
    //传输子会话首帧的应答
    case st_job_ack:
        root["code"] = m.m_code;
        return true;
    //未知类型
    default:
        return false;
    }
}

//按帧类型解析描述符并检查必填字段
bool ParseDescriptor(int type, const Json::Value& root, ShareControlMessage& m)
{
    switch (type)
    {
    //握手首帧：版本、类型、权限必填
    case sf_hello:
        return GetInt(root, "version", m.m_version) && GetInt(root, "kind", m.m_kind)
            && GetInt(root, "perms", m.m_perms) && GetOptionalString(root, "rootname", m.m_rootName)
            && GetOptionalBool(root, "includehidden", m.m_includeHidden);
    //列目录请求
    case sf_list:
        return GetInt64(root, "reqid", m.m_reqId) && GetString(root, "path", m.m_path)
            && GetOptionalString(root, "cursor", m.m_cursor);
    //列目录结果
    case sf_list_result:
        return GetInt64(root, "reqid", m.m_reqId) && GetInt(root, "code", m.m_code)
            && EntriesFromJson(root, false, m.m_entries) && GetOptionalString(root, "cursor", m.m_cursor)
            && GetOptionalBool(root, "more", m.m_more);
    //查条目请求与新建文件夹
    case sf_stat:
    case sf_mkdir:
        return GetInt64(root, "reqid", m.m_reqId) && GetString(root, "path", m.m_path);
    //查条目结果：entry 选填（失败时不带）
    case sf_stat_result:
    {
        if (!GetInt64(root, "reqid", m.m_reqId) || !GetInt(root, "code", m.m_code))
            return false;
        if (!root.isMember("entry"))
            return true;
        ShareEntry e;
        if (!EntryFromJson(root["entry"], false, e))
            return false;
        m.m_entries.push_back(e);
        return true;
    }
    //搜索请求
    case sf_search:
        return GetInt64(root, "reqid", m.m_reqId) && GetString(root, "path", m.m_path)
            && GetString(root, "keyword", m.m_keyword) && GetOptionalBool(root, "all", m.m_matchAll)
            && GetOptionalInt(root, "max", 0, m.m_maxItems);
    //搜索结果
    case sf_search_result:
        return GetInt64(root, "reqid", m.m_reqId) && EntriesFromJson(root, true, m.m_entries)
            && GetOptionalBool(root, "done", m.m_done) && GetOptionalBool(root, "truncated", m.m_truncated)
            && GetOptionalInt(root, "code", 0, m.m_code);
    //取消
    case sf_cancel:
        return GetInt64(root, "reqid", m.m_reqId);
    //缩略图请求
    case sf_thumb:
        return GetInt64(root, "reqid", m.m_reqId) && GetString(root, "path", m.m_path)
            && GetInt(root, "maxedge", m.m_maxEdge);
    //缩略图结果
    case sf_thumb_result:
        return GetInt64(root, "reqid", m.m_reqId) && GetInt(root, "code", m.m_code)
            && GetOptionalInt(root, "width", 0, m.m_width) && GetOptionalInt(root, "height", 0, m.m_height);
    //重命名
    case sf_rename:
        return GetInt64(root, "reqid", m.m_reqId) && GetString(root, "path", m.m_path)
            && GetString(root, "newname", m.m_newName);
    //删除
    case sf_delete:
        return GetInt64(root, "reqid", m.m_reqId) && GetStringArray(root, "paths", m.m_paths);
    //移动
    case sf_move:
        return GetInt64(root, "reqid", m.m_reqId) && GetStringArray(root, "paths", m.m_paths)
            && GetString(root, "todir", m.m_toDir);
    //写操作结果：items 选填
    case sf_op_result:
    {
        if (!GetInt64(root, "reqid", m.m_reqId) || !GetInt(root, "code", m.m_code))
            return false;
        if (!root.isMember("items"))
            return true;
        if (!root["items"].isArray())
            return false;
        const Json::Value& arr = root["items"];
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            ShareOpItem item;
            if (!arr[i].isObject() || !GetString(arr[i], "path", item.m_path) || !GetInt(arr[i], "code", item.m_code))
                return false;
            m.m_items.push_back(item);
        }
        return true;
    }
    //权限变化
    case sf_perms:
        return GetInt(root, "perms", m.m_perms);
    //结束访问会话：errcode 选填
    case sf_bye:
        return GetOptionalInt(root, "errcode", 0, m.m_errCode);
    //传输子会话首帧：方向只能是下载或上传
    case st_job:
        return GetInt(root, "direction", m.m_direction)
            && (m.m_direction == kDirectionDownload || m.m_direction == kDirectionUpload)
            && GetStringArray(root, "items", m.m_paths) && GetOptionalString(root, "todir", m.m_toDir);
    //传输子会话首帧的应答：只有结果码
    case st_job_ack:
        return GetInt(root, "code", m.m_code);
    //未知类型
    default:
        return false;
    }
}

//从 begin 起确定一批的结束下标 end（不含）：条数不超过 kShareListBatch，字节数不超过单帧上限。每条的字节数按它
//单独序列化后的长度加 1（逗号）估算，外层字段预留 kBatchOverheadBytes。begin 等于条目数时 end 也等于它（空批）；
//某一条单独成帧仍超限时返回 false
bool FindBatchEnd(const std::vector<ShareEntry>& entries, bool isSearchHit, size_t begin, size_t& end)
{
    const size_t budget = kShareControlMaxFrameBytes - kBatchOverheadBytes;
    size_t used = 0;
    end = begin;
    while (end < entries.size() && (end - begin) < kShareListBatch)
    {
        //每条再加 1 字节逗号
        const size_t entryBytes = WriteCompact(EntryToJson(entries[end], isSearchHit)).size() + 1;
        if (entryBytes > budget)
            return false;
        if (used + entryBytes > budget)
            break;
        used += entryBytes;
        ++end;
    }
    return true;
}

//把全部条目分批，结果为每批在 entries 里的 [起, 止) 下标；条目为空时得到一个空批
bool SplitIntoBatches(const std::vector<ShareEntry>& entries, bool isSearchHit,
                      std::vector<std::pair<size_t, size_t> >& batches)
{
    batches.clear();
    size_t begin = 0;
    do
    {
        size_t end = begin;
        if (!FindBatchEnd(entries, isSearchHit, begin, end))
            return false;
        batches.push_back(std::make_pair(begin, end));
        begin = end;
    } while (begin < entries.size());
    return true;
}

//构造一个 sf_list_result 帧，内容为 entries 的 [begin, end)；游标为 end，end 之后还有条目时 more 为真
bool BuildListResultFrame(int64_t reqId, const std::vector<ShareEntry>& entries, size_t begin, size_t end,
                          std::string& frame)
{
    ShareControlMessage m;
    m.m_type = sf_list_result;
    m.m_reqId = reqId;
    m.m_entries.assign(entries.begin() + begin, entries.begin() + end);
    m.m_more = (end < entries.size());
    //游标是已发出的条目数，访问者带着它取下一批
    m.m_cursor = std::to_string(static_cast<unsigned long long>(end));
    return ShareControlFraming::BuildFrame(m, frame);
}

} // namespace

bool ShareControlFraming::BuildFrame(const ShareControlMessage& msg, std::string& out)
{
    out.clear();
    if (!IsKnownType(msg.m_type))
        return false;
    //只有缩略图结果可以带附件
    if (msg.m_type != sf_thumb_result && !msg.m_binary.empty())
        return false;

    Json::Value root;
    if (!BuildDescriptor(msg, root))
        return false;

    std::string attachment;
    if (!net::encodeAttachmentBody(WriteCompact(root), msg.m_binary.data(),
                                   static_cast<uint32_t>(msg.m_binary.size()), attachment))
        return false;
    if (kTypeBytes + attachment.size() > kShareControlMaxFrameBytes)
        return false;

    out.reserve(kTypeBytes + attachment.size());
    out.push_back(static_cast<char>(msg.m_type));
    out += attachment;
    return true;
}

bool ShareControlFraming::DecodeFrame(const char* body, size_t len, ShareControlMessage& msg)
{
    msg = ShareControlMessage();
    if (body == NULL || len <= kTypeBytes)
        return false;

    const int type = static_cast<unsigned char>(body[0]);
    if (!IsKnownType(type))
        return false;

    std::string descriptor;
    size_t binaryOff = 0;
    size_t binaryLen = 0;
    if (!net::decodeAttachmentBody(body + kTypeBytes, len - kTypeBytes, descriptor, binaryOff, binaryLen))
        return false;
    if (type != sf_thumb_result && binaryLen != 0)
        return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(descriptor.data(), descriptor.data() + descriptor.size(), &root, &errs) || !root.isObject())
        return false;

    msg.m_type = type;
    if (!ParseDescriptor(type, root, msg))
        return false;
    if (binaryLen != 0)
        msg.m_binary.assign(body + kTypeBytes + binaryOff, binaryLen);
    return true;
}

bool ShareControlFraming::BuildListResultFrames(int64_t reqId, const std::vector<ShareEntry>& entries,
                                                std::vector<std::string>& frames)
{
    frames.clear();
    std::vector<std::pair<size_t, size_t> > batches;
    if (!SplitIntoBatches(entries, false, batches))
        return false;

    for (size_t b = 0; b < batches.size(); ++b)
    {
        std::string frame;
        if (!BuildListResultFrame(reqId, entries, batches[b].first, batches[b].second, frame))
            return false;
        frames.push_back(frame);
    }
    return true;
}

bool ShareControlFraming::BuildListResultBatch(int64_t reqId, const std::vector<ShareEntry>& entries, size_t start,
                                               std::string& frame, size_t& next)
{
    frame.clear();
    next = start;
    if (start > entries.size())
        return false;
    size_t end = start;
    if (!FindBatchEnd(entries, false, start, end))
        return false;
    if (!BuildListResultFrame(reqId, entries, start, end, frame))
        return false;
    next = end;
    return true;
}

bool ShareControlFraming::ParseListCursor(const std::string& cursor, size_t& offset)
{
    offset = 0;
    if (cursor.empty())
        return true;
    if (cursor.size() > kShareListCursorMaxDigits)
        return false;
    for (size_t i = 0; i < cursor.size(); ++i)
    {
        if (cursor[i] < '0' || cursor[i] > '9')
            return false;
        offset = offset * kDecimalBase + static_cast<size_t>(cursor[i] - '0');
    }
    return true;
}

bool ShareControlFraming::BuildSearchResultFrames(int64_t reqId, const std::vector<ShareEntry>& hits, bool finished,
                                                  bool truncated, std::vector<std::string>& frames)
{
    frames.clear();
    std::vector<std::pair<size_t, size_t> > batches;
    if (!SplitIntoBatches(hits, true, batches))
        return false;

    for (size_t b = 0; b < batches.size(); ++b)
    {
        const bool last = (b + 1 == batches.size());
        ShareControlMessage m;
        m.m_type = sf_search_result;
        m.m_reqId = reqId;
        m.m_entries.assign(hits.begin() + batches[b].first, hits.begin() + batches[b].second);
        m.m_done = last && finished;
        m.m_truncated = last && truncated;
        std::string frame;
        if (!BuildFrame(m, frame))
            return false;
        frames.push_back(frame);
    }
    return true;
}

} // namespace fileshare
