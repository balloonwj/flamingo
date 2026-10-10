/**
 *  企业问答管理接口的纯逻辑实现，QaAdminProtocol.cpp。说明见 QaAdminProtocol.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "QaAdminProtocol.h"

#include <stdlib.h>
#include "AdminApiCodes.h"

namespace qaadmin {

namespace {

//取 64 位 ID：字符串按十进制解析，整数直接取；其它返回 0
int64_t ToId(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(atoll(v.asString().c_str()));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

} // anonymous namespace

bool ParseQaFields(const Json::Value& root, EnterpriseQaRecord& out)
{
    if (!root["question"].isString() || !root["answer"].isString())
        return false;
    out.question = GroupQaProtocol::trim(root["question"].asString());
    out.answer = GroupQaProtocol::trim(root["answer"].asString());
    out.scope = root["scope"].isIntegral() ? root["scope"].asInt() : static_cast<int>(enterprise_qa_scope_all);
    if (out.scope != enterprise_qa_scope_all && out.scope != enterprise_qa_scope_selected)
        return false;
    out.groupids.clear();
    const Json::Value& arr = root["groupids"];
    if (arr.isNull())
        return true;
    if (!arr.isArray())
        return false;
    std::set<int64_t> seen;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const int64_t id = ToId(arr[i]);
        if (id <= 0)
            return false;
        if (seen.insert(id).second)
            out.groupids.push_back(id);
    }
    return true;
}

int32_t CheckQa(const EnterpriseQaRecord& rec, const std::set<int64_t>& enterpriseGroups)
{
    if (GroupQaProtocol::checkContent(rec.question, rec.answer) != group_qa_check_ok)
        return error_code_qa_invalid;
    if (rec.scope != enterprise_qa_scope_selected)
        return error_code_ok;
    if (rec.groupids.empty())
        return error_code_qa_group_invalid;
    for (size_t i = 0; i < rec.groupids.size(); ++i)
    {
        if (enterpriseGroups.count(rec.groupids[i]) == 0)
            return error_code_qa_group_invalid;
    }
    return error_code_ok;
}

Json::Value QaToJson(const EnterpriseQaRecord& rec)
{
    Json::Value jo;
    jo["id"] = std::to_string(rec.id);
    jo["question"] = rec.question;
    jo["answer"] = rec.answer;
    jo["scope"] = rec.scope;
    Json::Value groups(Json::arrayValue);
    for (size_t i = 0; i < rec.groupids.size(); ++i)
        groups.append(std::to_string(rec.groupids[i]));
    jo["groupids"] = groups;
    jo["creatorname"] = rec.creatorname;
    jo["updatetime"] = static_cast<Json::Int64>(rec.updatetime);
    return jo;
}

Json::Value GroupToJson(int64_t groupid, const std::string& name)
{
    Json::Value jo;
    jo["groupid"] = std::to_string(groupid);
    jo["name"] = name;
    return jo;
}

} // namespace qaadmin
