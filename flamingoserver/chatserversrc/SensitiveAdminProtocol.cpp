/**
 *  敏感词管理后台接口的报文编解码，SensitiveAdminProtocol.cpp
 *
 *  说明见 SensitiveAdminProtocol.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "SensitiveAdminProtocol.h"

#include "AdminApiCodes.h"

namespace sensitiveadmin {

int32_t ToAdminCode(int opResult)
{
    switch (opResult)
    {
    //写入成功
    case chatserver::sensitive_word_op_ok:
        return error_code_ok;

    //词条为空，或去掉空白、符号与表情之后为空
    case chatserver::sensitive_word_op_empty:
        return error_code_sensitive_word_invalid;

    //词条超过 32 个字符
    case chatserver::sensitive_word_op_too_long:
        return error_code_sensitive_word_toolong;

    //归一化后与本企业已有词条重复
    case chatserver::sensitive_word_op_exist:
        return error_code_sensitive_word_exist;

    //本企业词条数已达上限
    case chatserver::sensitive_word_op_full:
        return error_code_sensitive_word_full;

    //要修改的词条不存在，或不属于该企业
    case chatserver::sensitive_word_op_not_exist:
        return error_code_sensitive_word_notexist;

    //数据库访问失败，以及不认识的取值：都按服务端内部错误回给管理端
    default:
        return error_code_unknown;
    }
}

bool ResolveEnterprise(int64_t adminEnterpriseId, int64_t requested, int64_t& enterpriseId)
{
    //企业管理员只能操作本企业，请求里写了别的企业也不理会
    if (adminEnterpriseId != 0)
    {
        enterpriseId = adminEnterpriseId;
        return true;
    }
    //平台超级管理员必须指明是哪个企业的词库
    if (requested <= 0)
    {
        enterpriseId = 0;
        return false;
    }
    enterpriseId = requested;
    return true;
}

bool ParseIds(const Json::Value& value, size_t maxCount, std::vector<int64_t>& ids)
{
    ids.clear();
    if (!value.isArray() || value.empty() || value.size() > maxCount)
        return false;
    for (Json::ArrayIndex i = 0; i < value.size(); ++i)
    {
        if (value[i].isIntegral())
            ids.push_back(value[i].asInt64());
    }
    return true;
}

bool ParseLines(const Json::Value& value, size_t maxCount, std::vector<std::string>& lines)
{
    lines.clear();
    if (!value.isArray() || value.empty() || value.size() > maxCount)
        return false;
    for (Json::ArrayIndex i = 0; i < value.size(); ++i)
    {
        //有一个元素不是字符串就整体拒绝：行号与客户端看到的对不上会让跳过原因无从对照
        if (!value[i].isString())
        {
            lines.clear();
            return false;
        }
        lines.push_back(value[i].asString());
    }
    return true;
}

Json::Value WordToJson(const SensitiveWordRecord& record)
{
    Json::Value jo(Json::objectValue);
    jo["id"]          = static_cast<Json::Int64>(record.m_id);
    jo["word"]        = record.m_word;
    jo["category"]    = record.m_category;
    jo["remark"]      = record.m_remark;
    jo["creatorname"] = record.m_creatorName;
    jo["createtime"]  = static_cast<Json::Int64>(record.m_createTime);
    jo["updatetime"]  = static_cast<Json::Int64>(record.m_updateTime);
    return jo;
}

Json::Value HitToJson(const SensitiveHitRecord& record)
{
    Json::Value jo(Json::objectValue);
    jo["id"]         = static_cast<Json::Int64>(record.m_id);
    jo["time"]       = static_cast<Json::Int64>(record.m_createTime);
    jo["userid"]     = static_cast<Json::Int64>(record.m_userId);
    jo["username"]   = record.m_userName;
    jo["source"]     = record.m_source;
    jo["ctype"]      = record.m_ctype;
    jo["cid"]        = static_cast<Json::Int64>(record.m_cid);
    jo["targetname"] = record.m_targetName;
    jo["word"]       = record.m_word;
    jo["category"]   = record.m_category;
    jo["snippet"]    = record.m_snippet;
    return jo;
}

Json::Value ImportResultToJson(int added, const std::vector<SensitiveImportSkip>& skipped)
{
    Json::Value root(Json::objectValue);
    root["code"]  = error_code_ok;
    root["msg"]   = "ok";
    root["added"] = added;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < skipped.size(); ++i)
    {
        Json::Value jo(Json::objectValue);
        jo["line"]   = skipped[i].m_line;
        jo["word"]   = skipped[i].m_word;
        jo["reason"] = ToAdminCode(skipped[i].m_reason);
        arr.append(jo);
    }
    root["skipped"] = arr;
    return root;
}

} // namespace sensitiveadmin
