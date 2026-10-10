/**
 *  文件共享管理后台接口的报文编解码与纯规则的实现，见 FileShareAdminProtocol.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "FileShareAdminProtocol.h"

#include <stdlib.h>   //strtoll

namespace fileshareadmin {

namespace {

//取一个整数字段（数字或数字字符串都认）；缺失或非法时取 def
int64_t jsonInt(const Json::Value& root, const char* key, int64_t def)
{
    if (!root.isObject() || !root.isMember(key))
        return def;
    const Json::Value& v = root[key];
    if (v.isIntegral())
        return v.asInt64();
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty())
            return def;
        char* end = NULL;
        const long long value = strtoll(text.c_str(), &end, 10);
        if (end != NULL && *end == '\0')
            return static_cast<int64_t>(value);
    }
    return def;
}

//取一个文本字段并截到 kKeywordMaxBytes 个字节（不截断 UTF-8 多字节字符的中间）
std::string jsonKeyword(const Json::Value& root, const char* key)
{
    if (!root.isObject() || !root[key].isString())
        return std::string();
    std::string text = root[key].asString();
    if (text.size() <= kKeywordMaxBytes)
        return text;
    size_t cut = kKeywordMaxBytes;
    //UTF-8 的后续字节形如 10xxxxxx：往前退到一个字符的开头
    const unsigned char kContinuationMask = 0xC0;
    const unsigned char kContinuationBits = 0x80;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & kContinuationMask) == kContinuationBits)
        --cut;
    return text.substr(0, cut);
}

} // namespace

bool ResolveTargetEnterprise(int64_t adminEnterpriseId, int64_t requested, int64_t& enterpriseId)
{
    //企业管理员只能操作本企业，请求里写了别的企业也不理会
    if (adminEnterpriseId != 0)
    {
        enterpriseId = adminEnterpriseId;
        return true;
    }
    //平台管理员必须指明是哪个企业
    if (requested <= 0)
    {
        enterpriseId = 0;
        return false;
    }
    enterpriseId = requested;
    return true;
}

int64_t ResolveListEnterprise(int64_t adminEnterpriseId, int64_t requested)
{
    if (adminEnterpriseId != 0)
        return adminEnterpriseId;
    return (requested > 0) ? requested : 0;
}

bool ShareInScope(int64_t adminEnterpriseId, int64_t shareEnterpriseId)
{
    return adminEnterpriseId == 0 || adminEnterpriseId == shareEnterpriseId;
}

void PageToRange(int64_t page, int64_t pageSize, int& offset, int& limit)
{
    if (page < 1)
        page = 1;
    if (pageSize < 1)
        pageSize = kDefaultPageSize;
    if (pageSize > kMaxPageSize)
        pageSize = kMaxPageSize;
    limit = static_cast<int>(pageSize);
    offset = static_cast<int>((page - 1) * pageSize);
}

ShareAdminQuery ParseShareQuery(const Json::Value& root, int64_t enterpriseFilter)
{
    ShareAdminQuery q;
    q.m_enterpriseId = enterpriseFilter;
    q.m_keyword = jsonKeyword(root, "keyword");
    const int64_t state = jsonInt(root, "state", 0);
    q.m_state = (state == FileShareManager::kShareRecordActive || state == FileShareManager::kShareRecordCanceled)
                ? static_cast<int>(state) : 0;
    PageToRange(jsonInt(root, "page", 1), jsonInt(root, "pagesize", kDefaultPageSize), q.m_offset, q.m_limit);
    return q;
}

ShareAuditQuery ParseAuditQuery(const Json::Value& root, int64_t enterpriseFilter)
{
    ShareAuditQuery q;
    q.m_enterpriseId = enterpriseFilter;
    q.m_actorKeyword = jsonKeyword(root, "actorkeyword");
    const int64_t action = jsonInt(root, "action", 0);
    q.m_action = (action > 0) ? static_cast<int>(action) : 0;
    const int64_t start = jsonInt(root, "starttime", 0);
    const int64_t end = jsonInt(root, "endtime", 0);
    q.m_startMs = (start > 0) ? start : 0;
    q.m_endMs = (end > 0) ? end : 0;
    q.m_shareId = ParseId(root, "shareid");
    PageToRange(jsonInt(root, "page", 1), jsonInt(root, "pagesize", kDefaultPageSize), q.m_offset, q.m_limit);
    return q;
}

int64_t ParseId(const Json::Value& root, const char* key)
{
    const int64_t id = jsonInt(root, key, 0);
    return (id > 0) ? id : 0;
}

Json::Value BuildSettings(const std::vector<FeatureSettingView>& settings)
{
    Json::Value out(Json::objectValue);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < settings.size(); ++i)
    {
        Json::Value item;
        item["key"] = settings[i].m_key;
        item["value"] = settings[i].m_value;
        item["isdefault"] = settings[i].m_isDefault ? 1 : 0;
        arr.append(item);
    }
    out["settings"] = arr;
    return out;
}

Json::Value BuildShareList(const std::vector<ShareAdminView>& list, int64_t total)
{
    Json::Value out(Json::objectValue);
    out["total"] = static_cast<Json::Int64>(total);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const ShareRecord& s = list[i].m_share;
        Json::Value item;
        item["shareid"] = static_cast<Json::Int64>(s.m_shareId);
        item["enterpriseid"] = static_cast<Json::Int64>(s.m_enterpriseId);
        item["ownerid"] = static_cast<Json::Int64>(s.m_ownerId);
        item["ownername"] = list[i].m_ownerName;
        item["owneraccount"] = list[i].m_ownerAccount;
        item["ctype"] = s.m_ctype;
        item["cid"] = static_cast<Json::Int64>(s.m_cid);
        item["conversationname"] = list[i].m_conversationName;
        item["kind"] = s.m_kind;
        item["name"] = s.m_name;
        item["displaypath"] = s.m_displayPath;
        item["membercount"] = list[i].m_memberCount;
        item["state"] = s.m_state;
        item["cancelreason"] = s.m_cancelReason;
        item["createtime"] = static_cast<Json::Int64>(s.m_createTime);
        item["canceltime"] = static_cast<Json::Int64>(s.m_cancelTime);
        arr.append(item);
    }
    out["list"] = arr;
    return out;
}

Json::Value BuildMembers(const std::vector<ShareMemberView>& members)
{
    Json::Value out(Json::objectValue);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < members.size(); ++i)
    {
        Json::Value item;
        item["userid"] = static_cast<Json::Int64>(members[i].m_userId);
        item["name"] = members[i].m_name;
        item["account"] = members[i].m_account;
        item["perms"] = members[i].m_perms;
        arr.append(item);
    }
    out["members"] = arr;
    return out;
}

Json::Value BuildAuditList(const std::vector<ShareAuditView>& list, int64_t total)
{
    Json::Value out(Json::objectValue);
    out["total"] = static_cast<Json::Int64>(total);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const ShareAuditRecord& r = list[i].m_record;
        Json::Value item;
        item["id"] = static_cast<Json::Int64>(r.m_id);
        item["time"] = static_cast<Json::Int64>(r.m_createTime);
        item["shareid"] = static_cast<Json::Int64>(r.m_shareId);
        item["sharename"] = list[i].m_shareName;
        item["ownerid"] = static_cast<Json::Int64>(r.m_ownerId);
        item["ownername"] = list[i].m_ownerName;
        item["actorid"] = static_cast<Json::Int64>(r.m_actorId);
        item["actorname"] = list[i].m_actorName;
        item["actorisadmin"] = list[i].m_actorIsAdmin ? 1 : 0;
        item["action"] = r.m_action;
        item["source"] = r.m_source;
        item["path"] = r.m_path;
        item["topath"] = r.m_toPath;
        item["size"] = static_cast<Json::Int64>(r.m_size);
        item["result"] = r.m_result;
        item["accessid"] = r.m_accessId;
        arr.append(item);
    }
    out["list"] = arr;
    return out;
}

} // namespace fileshareadmin
