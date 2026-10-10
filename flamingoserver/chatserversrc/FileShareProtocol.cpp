/**
 *  v2 文件共享信令编解码与共享状态计算（服务端侧）实现，FileShareProtocol.cpp
 *
 *  取字段的内部辅助函数一律先判类型再取值：JSON 是客户端可以任意伪造的输入，jsoncpp 的
 *  asString() / asInt64() 在类型不符时会抛异常，而本模块在会话层的分发路径上被调用，一旦抛出
 *  就会打断整条连接的收包流程。任何不合契约的输入都以返回 false 的方式安全失败。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "FileShareProtocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <set>
#include "../jsoncpp1.9.0/json.h"

namespace {

//64 位整数的十进制文本最多 20 个字符（含负号），留余量
const int kInt64TextChars = 32;

//审计上报里客户端可报的动作区间（share_audit_download_file ~ share_audit_move）
const int kFirstClientAuditAction = share_audit_download_file;
const int kLastClientAuditAction  = share_audit_move;

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或顶层不是对象返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 64 位 ID 写成十进制字符串（v2 惯例，避免客户端按双精度解析丢精度）
std::string idToString(int64_t id)
{
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(id));
    return std::string(buf);
}

//从一个 JSON 值里取 64 位 ID：十进制字符串与整数两种写法都接受；其余类型、空串、非数字返回 0
int64_t valueToId(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty())
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<int64_t>(atoll(text.c_str()));
    }
    if (v.isInt64())
        return v.asInt64();
    return 0;
}

//取出某个字段里的 64 位 ID；字段缺失返回 0，由调用方按「ID 必须为正」判定非法
int64_t getId(const Json::Value& root, const char* key)
{
    if (!root.isMember(key))
        return 0;
    return valueToId(root[key]);
}

//取出必填字符串字段：缺失、不是字符串、空串、超过 maxBytes 字节一律返回 false
bool getRequiredString(const Json::Value& root, const char* key, size_t maxBytes, std::string& out)
{
    if (!root.isMember(key) || !root[key].isString())
        return false;
    out = root[key].asString();
    return !out.empty() && out.size() <= maxBytes;
}

//取出可选字符串字段：缺失按空串处理；存在但不是字符串、或超过 maxBytes 字节返回 false
bool getOptionalString(const Json::Value& root, const char* key, size_t maxBytes, std::string& out)
{
    out.clear();
    if (!root.isMember(key))
        return true;
    if (!root[key].isString())
        return false;
    out = root[key].asString();
    return out.size() <= maxBytes;
}

//取出必填 32 位整数字段；缺失或不是整数返回 false
bool getRequiredInt(const Json::Value& root, const char* key, int& out)
{
    if (!root.isMember(key) || !root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//取出可选 32 位整数字段；缺失按 defaultValue 处理，存在但不是整数返回 false
bool getOptionalInt(const Json::Value& root, const char* key, int defaultValue, int& out)
{
    out = defaultValue;
    if (!root.isMember(key))
        return true;
    if (!root[key].isInt())
        return false;
    out = root[key].asInt();
    return true;
}

//取出可选的 0 / 1 开关字段；缺失按 false 处理，取值不是 0 或 1 返回 false
bool getOptionalFlag(const Json::Value& root, const char* key, bool& out)
{
    int value = 0;
    if (!getOptionalInt(root, key, 0, value))
        return false;
    if (value != 0 && value != 1)
        return false;
    out = (value == 1);
    return true;
}

//取出必填的 0 / 1 开关字段
bool getRequiredFlag(const Json::Value& root, const char* key, bool& out)
{
    if (!root.isMember(key))
        return false;
    return getOptionalFlag(root, key, out);
}

//取出非负的 64 位整数字段；缺失、不是整数或为负返回 false
bool getNonNegativeInt64(const Json::Value& root, const char* key, int64_t& out)
{
    if (!root.isMember(key) || !root[key].isInt64())
        return false;
    out = root[key].asInt64();
    return out >= 0;
}

//解析一个成员数组（[{userid, perms}, ...]），并把出现过的 userid 记进 seen；
//userid 非正、perms 越界、或与 seen 里已有的重复都返回 false。数组缺失按空数组处理
bool parseMemberArray(const Json::Value& root, const char* key, std::set<int64_t>& seen,
                      std::vector<ShareMemberSpec>& out)
{
    out.clear();
    if (!root.isMember(key))
        return true;
    const Json::Value& arr = root[key];
    if (!arr.isArray())
        return false;

    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject())
            return false;

        ShareMemberSpec spec;
        spec.m_userId = getId(item, "userid");
        if (spec.m_userId <= 0)
            return false;
        if (!getRequiredInt(item, "perms", spec.m_perms) || !FileShareProtocol::isValidPerms(spec.m_perms))
            return false;
        if (!seen.insert(spec.m_userId).second)
            return false;
        out.push_back(spec);
    }
    return true;
}

//写出一个成员数组
Json::Value membersToJson(const std::vector<ShareMemberSpec>& members)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < members.size(); ++i)
    {
        Json::Value item;
        item["userid"] = idToString(members[i].m_userId);
        item["perms"] = members[i].m_perms;
        arr.append(item);
    }
    return arr;
}

//会话类型是否为单聊或群聊
bool isValidConversationType(int ctype)
{
    return ctype == conversation_single || ctype == conversation_group;
}

//「暂不可用」原因是否合法
bool isValidUnavailableReason(int reason)
{
    return reason >= share_unavailable_owner_offline && reason <= share_unavailable_feature_disabled;
}

} // namespace

//==== 解析 ====

bool FileShareProtocol::parseCreateRequest(const std::string& json, ShareCreateRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!getRequiredString(root, "clientshareid", kShareClientShareIdMaxBytes, req.m_clientShareId))
        return false;
    if (!getRequiredInt(root, "ctype", req.m_ctype) || !isValidConversationType(req.m_ctype))
        return false;
    req.m_cid = getId(root, "cid");
    if (req.m_cid <= 0)
        return false;
    if (!getRequiredInt(root, "kind", req.m_kind) || !isValidKind(req.m_kind))
        return false;
    if (!getRequiredString(root, "name", kShareNameMaxBytes, req.m_name))
        return false;
    if (!getRequiredString(root, "displaypath", kShareDisplayPathMaxBytes, req.m_displayPath))
        return false;
    if (!getOptionalFlag(root, "includehidden", req.m_includeHidden))
        return false;

    //成员数组是必填的
    if (!root.isMember("members"))
        return false;
    std::set<int64_t> seen;
    if (!parseMemberArray(root, "members", seen, req.m_members))
        return false;
    if (req.m_members.empty() || req.m_members.size() > kShareMaxMembersPerRequest)
        return false;

    //单文件共享没有任何写操作可言，带来的写权限一律清零后照常处理（2026-10-02 与用户确认：
    //清零而不是拒绝，与 cmd_share_update_members 在校验层的处理一致）
    if (req.m_kind == share_kind_file)
    {
        for (size_t i = 0; i < req.m_members.size(); ++i)
            req.m_members[i].m_perms = 0;
    }

    //单聊只能共享给对方一人
    if (req.m_ctype == conversation_single)
    {
        if (req.m_members.size() != 1 || req.m_members[0].m_userId != req.m_cid)
            return false;
    }
    return true;
}

bool FileShareProtocol::parseShareIdRequest(const std::string& json, int64_t& shareId)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    shareId = getId(root, "shareid");
    return shareId > 0;
}

bool FileShareProtocol::parseUpdateMembersRequest(const std::string& json, ShareUpdateMembersRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.m_shareId = getId(root, "shareid");
    if (req.m_shareId <= 0)
        return false;

    //三组共用一个 seen 集合：同一个人在三组里合计只能出现一次
    std::set<int64_t> seen;
    if (!parseMemberArray(root, "add", seen, req.m_add))
        return false;
    if (!parseMemberArray(root, "update", seen, req.m_update))
        return false;

    req.m_remove.clear();
    if (root.isMember("remove"))
    {
        const Json::Value& arr = root["remove"];
        if (!arr.isArray())
            return false;
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            int64_t userId = valueToId(arr[i]);
            if (userId <= 0 || !seen.insert(userId).second)
                return false;
            req.m_remove.push_back(userId);
        }
    }

    const size_t total = req.m_add.size() + req.m_remove.size() + req.m_update.size();
    return total > 0 && total <= kShareMaxMembersPerRequest;
}

bool FileShareProtocol::parseListRequest(const std::string& json, ShareListRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!getRequiredInt(root, "scope", req.m_scope))
        return false;
    if (req.m_scope != share_list_scope_mine && req.m_scope != share_list_scope_shared_me)
        return false;

    const bool hasCtype = root.isMember("ctype");
    const bool hasCid = root.isMember("cid");
    //要么都不给（不按会话筛选），要么都给
    if (hasCtype != hasCid)
        return false;
    req.m_ctype = 0;
    req.m_cid = 0;
    if (!hasCtype)
        return true;

    if (!getRequiredInt(root, "ctype", req.m_ctype) || !isValidConversationType(req.m_ctype))
        return false;
    req.m_cid = getId(root, "cid");
    return req.m_cid > 0;
}

bool FileShareProtocol::parseServeReport(const std::string& json, std::vector<ShareServeReportItem>& items)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root.isMember("items") || !root["items"].isArray())
        return false;

    const Json::Value& arr = root["items"];
    if (arr.size() == 0 || arr.size() > kShareMaxMembersPerRequest)
        return false;

    items.clear();
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject())
            return false;

        ShareServeReportItem report;
        report.m_shareId = getId(item, "shareid");
        if (report.m_shareId <= 0)
            return false;
        if (!getRequiredFlag(item, "available", report.m_available))
            return false;
        if (!getOptionalInt(item, "reason", 0, report.m_reason))
            return false;

        //可提供时不该带原因；不可提供时必须给出合法的原因
        if (report.m_available && report.m_reason != 0)
            return false;
        if (!report.m_available && !isValidUnavailableReason(report.m_reason))
            return false;
        items.push_back(report);
    }
    return true;
}

bool FileShareProtocol::parseCloseAccessRequest(const std::string& json, std::string& accessId, int& errCode)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!getRequiredString(root, "accessid", kShareSessionIdMaxBytes, accessId))
        return false;
    return getOptionalInt(root, "reason", 0, errCode);
}

bool FileShareProtocol::parseTransferRequest(const std::string& json, ShareTransferRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!getRequiredString(root, "accessid", kShareSessionIdMaxBytes, req.m_accessId))
        return false;
    if (!getRequiredInt(root, "direction", req.m_direction))
        return false;
    if (req.m_direction != share_transfer_download && req.m_direction != share_transfer_upload)
        return false;
    if (!getNonNegativeInt64(root, "totalbytes", req.m_totalBytes))
        return false;
    if (!getNonNegativeInt64(root, "totalfiles", req.m_totalFiles))
        return false;

    if (!root.isMember("items") || !root["items"].isArray())
        return false;
    const Json::Value& arr = root["items"];
    if (arr.size() == 0 || arr.size() > kShareMaxTransferItems)
        return false;

    req.m_items.clear();
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject())
            return false;

        ShareTransferItem entry;
        if (!getOptionalString(item, "path", kShareDisplayPathMaxBytes, entry.m_path))
            return false;
        if (!getRequiredFlag(item, "isdir", entry.m_isDir))
            return false;
        if (!getNonNegativeInt64(item, "size", entry.m_size))
            return false;
        //上传时条目名就是要在共享里新建的名字，不能为空；下载时空路径表示单文件共享的根
        if (req.m_direction == share_transfer_upload && entry.m_path.empty())
            return false;
        req.m_items.push_back(entry);
    }
    return true;
}

bool FileShareProtocol::parseTransferClose(const std::string& json, std::string& transferId, int& result, int& errCode)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!getRequiredString(root, "transferid", kShareSessionIdMaxBytes, transferId))
        return false;
    if (!getRequiredInt(root, "result", result))
        return false;
    if (result != share_transfer_result_success && result != share_transfer_result_failed)
        return false;
    if (!getOptionalInt(root, "errcode", 0, errCode))
        return false;
    //成功结束时错误码必须为 0
    return result != share_transfer_result_success || errCode == 0;
}

bool FileShareProtocol::parseAuditReport(const std::string& json, std::vector<ShareAuditItem>& items)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;
    if (!root.isMember("items") || !root["items"].isArray())
        return false;

    const Json::Value& arr = root["items"];
    if (arr.size() == 0 || arr.size() > kShareAuditBatchMax)
        return false;

    items.clear();
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject())
            return false;

        ShareAuditItem audit;
        audit.m_shareId = getId(item, "shareid");
        if (audit.m_shareId <= 0)
            return false;
        if (!getRequiredString(item, "accessid", kShareSessionIdMaxBytes, audit.m_accessId))
            return false;
        audit.m_visitorId = getId(item, "visitorid");
        if (audit.m_visitorId <= 0)
            return false;
        if (!getRequiredInt(item, "action", audit.m_action) || !isClientReportableAction(audit.m_action))
            return false;
        if (!getOptionalString(item, "path", kShareDisplayPathMaxBytes, audit.m_path))
            return false;
        if (!getOptionalString(item, "topath", kShareDisplayPathMaxBytes, audit.m_toPath))
            return false;
        if (!getNonNegativeInt64(item, "size", audit.m_size))
            return false;
        if (!getOptionalInt(item, "result", 0, audit.m_result))
            return false;
        audit.m_clientTime = 0;
        if (item.isMember("clienttime") && !getNonNegativeInt64(item, "clienttime", audit.m_clientTime))
            return false;
        items.push_back(audit);
    }
    return true;
}

//==== 构造 ====

std::string FileShareProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    return writeCompact(root);
}

std::string FileShareProtocol::buildCreateResponse(int code, const std::string& msg, int64_t shareId,
                                                   const std::string& clientShareId,
                                                   const std::vector<int64_t>& invalidMembers)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    root["clientshareid"] = clientShareId;
    if (shareId > 0)
        root["shareid"] = idToString(shareId);
    if (!invalidMembers.empty())
    {
        Json::Value arr(Json::arrayValue);
        for (size_t i = 0; i < invalidMembers.size(); ++i)
            arr.append(idToString(invalidMembers[i]));
        root["invalidmembers"] = arr;
    }
    return writeCompact(root);
}

std::string FileShareProtocol::buildUpdateMembersResponse(int code, const std::string& msg,
                                                        const std::vector<int64_t>& invalidMembers)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (!invalidMembers.empty())
    {
        Json::Value arr(Json::arrayValue);
        for (size_t i = 0; i < invalidMembers.size(); ++i)
            arr.append(idToString(invalidMembers[i]));
        root["invalidmembers"] = arr;
    }
    return writeCompact(root);
}

std::string FileShareProtocol::buildListResponse(int code, const std::string& msg, int scope,
                                                 const std::vector<ShareListItem>& items)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ShareListItem& it = items[i];
        Json::Value item;
        item["shareid"] = idToString(it.m_shareId);
        item["ownerid"] = idToString(it.m_ownerId);
        item["ctype"] = it.m_ctype;
        item["cid"] = idToString(it.m_cid);
        item["kind"] = it.m_kind;
        item["name"] = it.m_name;
        //真实路径只写给分享者本人（「我的共享」）；「共享给我的」只有共享名称（2026-10-04 起）
        if (scope == share_list_scope_mine)
            item["displaypath"] = it.m_displayPath;
        item["includehidden"] = it.m_includeHidden ? 1 : 0;
        item["state"] = it.m_state;
        item["reason"] = it.m_reason;
        item["createtime"] = static_cast<Json::Int64>(it.m_createTime);
        if (scope == share_list_scope_mine)
        {
            item["onthisdevice"] = it.m_onThisDevice ? 1 : 0;
            item["members"] = membersToJson(it.m_members);
        }
        else
        {
            item["myperms"] = it.m_myPerms;
        }
        arr.append(item);
    }
    root["shares"] = arr;
    return writeCompact(root);
}

std::string FileShareProtocol::buildOpenResponse(int code, const std::string& msg, const std::string& accessId)
{
    return buildOpenResponse(code, msg, accessId, std::string());
}

std::string FileShareProtocol::buildOpenResponse(int code, const std::string& msg, const std::string& accessId,
                                                 const std::string& name)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (!accessId.empty())
        root["accessid"] = accessId;
    if (!name.empty())
        root["name"] = name;
    return writeCompact(root);
}

std::string FileShareProtocol::buildTransferResponse(int code, const std::string& msg, const std::string& transferId)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    if (!transferId.empty())
        root["transferid"] = transferId;
    return writeCompact(root);
}

std::string FileShareProtocol::buildStateNotify(int64_t shareId, const ShareStateView& view)
{
    Json::Value root;
    root["shareid"] = idToString(shareId);
    root["state"] = view.m_state;
    root["reason"] = view.m_reason;
    return writeCompact(root);
}

std::string FileShareProtocol::buildPermsNotify(int64_t shareId, int64_t userId, int perms)
{
    Json::Value root;
    root["shareid"] = idToString(shareId);
    root["userid"] = idToString(userId);
    root["perms"] = perms;
    return writeCompact(root);
}

std::string FileShareProtocol::buildRelayInfo(const ShareRelayInfo& info)
{
    Json::Value root;
    root["accessid"] = info.m_accessId;
    root["transferid"] = info.m_transferId;
    //地址字段名沿用远程桌面、通话与文件夹传输的中转参数推送，客户端解析代码可以复用
    root["relayip"] = info.m_relayIp;
    root["relayport"] = info.m_relayPort;
    root["token"] = info.m_token;
    root["role"] = info.m_role;
    root["lanes"] = info.m_lanes;
    root["opentimeoutms"] = info.m_openTimeoutMs;
    root["idletimeoutms"] = info.m_idleTimeoutMs;
    //传输方向只对传输子会话有意义
    if (!info.m_transferId.empty())
        root["direction"] = info.m_direction;
    //P2P 会话只为传输子会话建；没建时不写这个字段，老客户端与不走 P2P 的帧保持原样
    if (!info.m_transferId.empty() && !info.m_p2pSessionId.empty())
        root["p2psessionid"] = info.m_p2pSessionId;
    if (info.m_forOwner)
    {
        root["shareid"] = idToString(info.m_shareId);
        root["visitorid"] = idToString(info.m_visitorId);
        root["visitorname"] = info.m_visitorName;
        root["perms"] = info.m_perms;
        root["searchmaxresults"] = info.m_searchMaxResults;
        root["searchtimeoutms"] = info.m_searchTimeoutMs;
    }
    return writeCompact(root);
}

std::string FileShareProtocol::buildAccessEndedNotify(const std::string& accessId, const std::string& transferId,
                                                      int errCode)
{
    Json::Value root;
    root["accessid"] = accessId;
    root["transferid"] = transferId;
    root["errcode"] = errCode;
    return writeCompact(root);
}

//==== 共享卡片 ====

std::string FileShareProtocol::buildCardContent(const ShareCardContent& card)
{
    Json::Value root;
    root["shareid"] = idToString(card.m_shareId);
    root["kind"] = card.m_kind;
    root["name"] = card.m_name;
    //真实路径不随卡片入库（2026-10-04 起），只在下发给分享者本人时由 augmentCardContent 补上
    if (!card.m_displayPath.empty())
        root["displaypath"] = card.m_displayPath;
    root["ownerid"] = idToString(card.m_ownerId);
    return writeCompact(root);
}

bool FileShareProtocol::parseCardContent(const std::string& json, ShareCardContent& card)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    card.m_shareId = getId(root, "shareid");
    card.m_ownerId = getId(root, "ownerid");
    if (card.m_shareId <= 0 || card.m_ownerId <= 0)
        return false;
    if (!getRequiredInt(root, "kind", card.m_kind) || !isValidKind(card.m_kind))
        return false;
    if (!getRequiredString(root, "name", kShareNameMaxBytes, card.m_name))
        return false;
    return getOptionalString(root, "displaypath", kShareDisplayPathMaxBytes, card.m_displayPath);
}

std::string FileShareProtocol::augmentCardContent(const std::string& content, const ShareStateView& view,
                                                  bool includePerms, int perms, const std::string* ownerDisplayPath)
{
    Json::Value root;
    if (!parseObject(content, root))
        return content;

    root["sharestate"] = view.m_state;
    root["sharereason"] = view.m_reason;
    if (includePerms)
        root["myperms"] = perms;
    else
        root.removeMember("myperms");
    //真实路径只给分享者本人；其余的人一律去掉，包括 2026-10-04 之前入库、正文里带着路径的旧卡片
    if (ownerDisplayPath != NULL && !ownerDisplayPath->empty())
        root["displaypath"] = *ownerDisplayPath;
    else
        root.removeMember("displaypath");
    return writeCompact(root);
}

std::string FileShareProtocol::removeCardDisplayPath(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root) || !root.isMember("displaypath"))
        return content;
    root.removeMember("displaypath");
    return writeCompact(root);
}

//==== 状态计算与取值校验 ====

ShareStateView FileShareProtocol::computeShareState(const ShareStateInput& in)
{
    ShareStateView view;

    //已取消：对所有人都是已失效，原因就是取消原因
    if (in.m_canceled)
    {
        view.m_state = share_state_invalid;
        view.m_reason = in.m_cancelReason;
        return view;
    }

    //看的人已不是成员（分享者本人恒视为成员）：仍在会话里说明是被分享者移出，否则是自己离开了群
    if (!in.m_viewerIsOwner && !in.m_viewerIsMember)
    {
        view.m_state = share_state_invalid;
        view.m_reason = in.m_viewerInConversation ? share_invalid_member_removed : share_invalid_member_left_group;
        return view;
    }

    //以下都是共享仍然有效、但暂时无法访问的情形，按判定顺序取第一个命中的原因
    view.m_state = share_state_unavailable;
    if (!in.m_featureEnabled)
    {
        view.m_reason = share_unavailable_feature_disabled;
        return view;
    }
    if (!in.m_ownerOnline)
    {
        view.m_reason = share_unavailable_owner_offline;
        return view;
    }
    if (!in.m_ownerOnBoundDevice)
    {
        view.m_reason = share_unavailable_other_device;
        return view;
    }
    if (!in.m_rootAvailable)
    {
        view.m_reason = share_unavailable_root_missing;
        return view;
    }

    view.m_state = share_state_available;
    view.m_reason = 0;
    return view;
}

bool FileShareProtocol::isValidPerms(int perms)
{
    return perms >= 0 && perms <= kSharePermAll;
}

bool FileShareProtocol::isValidKind(int kind)
{
    return kind == share_kind_disk || kind == share_kind_folder || kind == share_kind_file;
}

bool FileShareProtocol::isClientReportableAction(int action)
{
    return action >= kFirstClientAuditAction && action <= kLastClientAuditAction;
}
