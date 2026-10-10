/**
 *  企业部门相关协议报文的结构体与 JSON 收发实现，DepartmentMsg.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "DepartmentMsg.h"
#include "../jsoncpp1.9.0/json.h"

namespace dept {

//把一段 JSON 文本解析为 Json::Value。
//jsoncpp 1.9.0 的 CharReader 由 newCharReader() 在堆上创建，需手动 delete，
//故在此统一封装，避免每个解析函数重复这段样板代码。
//返回 true 表示解析成功，结果写入 outRoot。
static bool ParseJson(const std::string& json, Json::Value& outRoot)
{
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    JSONCPP_STRING errs;
    bool ok = reader->parse(json.c_str(), json.c_str() + json.length(), &outRoot, &errs);
    delete reader;
    return ok && errs.empty();
}

//把一个 Json::Value 序列化为紧凑（无缩进）JSON 文本，与现有处理器风格一致。
static std::string DumpJson(const Json::Value& root)
{
    Json::StreamWriterBuilder builder;
    //indentation 置空表示不换行不缩进，输出单行紧凑 JSON。
    builder.settings_["indentation"] = "";
    return Json::writeString(builder, root);
}

//安全地取整型字段：字段存在且为整型时返回其值，否则返回默认值 0。
static int64_t GetInt64(const Json::Value& obj, const char* key)
{
    if (obj.isMember(key) && obj[key].isIntegral())
    {
        return obj[key].asInt64();
    }

    return 0;
}

//安全地取字符串字段：字段存在且为字符串时返回其值，否则返回空串。
static std::string GetString(const Json::Value& obj, const char* key)
{
    if (obj.isMember(key) && obj[key].isString())
    {
        return obj[key].asString();
    }

    return std::string();
}

//把一个 DepartmentMember 写入 Json::Value 对象。
static void MemberToJson(const DepartmentMember& m, Json::Value& out)
{
    out["userid"]      = static_cast<Json::Int64>(m.userid);
    out["username"]    = m.username;
    out["employeeno"]  = m.employeeno;
    out["nickname"]    = m.nickname;
    out["facetype"]    = m.facetype;
    out["customface"]  = m.customface;
    out["status"]      = m.status;
    out["clienttype"]  = m.clienttype;
    out["position"]    = m.position;
    out["isprimary"]   = m.isprimary;
    out["memberorder"] = m.memberorder;
    out["accountstatus"] = m.accountstatus;
}

//从一个 Json::Value 对象读出一个 DepartmentMember。
static void MemberFromJson(const Json::Value& in, DepartmentMember& m)
{
    m.userid      = GetInt64(in, "userid");
    m.username    = GetString(in, "username");
    m.nickname    = GetString(in, "nickname");
    m.facetype    = static_cast<int32_t>(GetInt64(in, "facetype"));
    m.customface  = GetString(in, "customface");
    m.status      = static_cast<int32_t>(GetInt64(in, "status"));
    m.clienttype  = static_cast<int32_t>(GetInt64(in, "clienttype"));
    m.position    = GetString(in, "position");
    m.isprimary   = static_cast<int32_t>(GetInt64(in, "isprimary"));
    m.memberorder = static_cast<int32_t>(GetInt64(in, "memberorder"));
    m.accountstatus = static_cast<int32_t>(GetInt64(in, "accountstatus"));
}

bool ParseOperateDepartmentReq(const std::string& json, OperateDepartmentReq& out)
{
    Json::Value root;
    if (!ParseJson(json, root) || !root.isObject())
    {
        return false;
    }

    //operationtype 是所有分支都必须携带的字段，缺失或类型不符即视为非法请求。
    if (!root.isMember("operationtype") || !root["operationtype"].isIntegral())
    {
        return false;
    }

    out.operationtype   = static_cast<int32_t>(root["operationtype"].asInt());
    out.deptid          = GetInt64(root, "deptid");
    out.parentdeptid    = GetInt64(root, "parentdeptid");
    out.newparentdeptid = GetInt64(root, "newparentdeptid");
    out.deptname        = GetString(root, "deptname");
    out.deptdesc        = GetString(root, "deptdesc");
    out.leaderuserid    = GetInt64(root, "leaderuserid");
    out.deptorder       = static_cast<int32_t>(GetInt64(root, "deptorder"));
    return true;
}

bool ParseOperateDeptMemberReq(const std::string& json, OperateDeptMemberReq& out)
{
    Json::Value root;
    if (!ParseJson(json, root) || !root.isObject())
    {
        return false;
    }

    //operationtype 是所有分支都必须携带的字段，缺失或类型不符即视为非法请求。
    if (!root.isMember("operationtype") || !root["operationtype"].isIntegral())
    {
        return false;
    }

    out.operationtype = static_cast<int32_t>(root["operationtype"].asInt());
    out.deptid        = GetInt64(root, "deptid");
    out.newdeptid     = GetInt64(root, "newdeptid");
    out.userid        = GetInt64(root, "userid");
    out.position      = GetString(root, "position");
    out.isprimary     = static_cast<int32_t>(GetInt64(root, "isprimary"));
    out.memberorder   = static_cast<int32_t>(GetInt64(root, "memberorder"));
    return true;
}

bool ParseGetOrgStructureResp(const std::string& json, std::vector<DepartmentInfo>& outDepts)
{
    outDepts.clear();

    Json::Value root;
    if (!ParseJson(json, root) || !root.isObject())
    {
        return false;
    }

    //departments 字段缺失或不是数组，都视为非法应答。
    if (!root.isMember("departments") || !root["departments"].isArray())
    {
        return false;
    }

    const Json::Value& deptArray = root["departments"];
    for (Json::ArrayIndex i = 0; i < deptArray.size(); ++i)
    {
        const Json::Value& deptNode = deptArray[i];
        DepartmentInfo d;
        d.deptid       = GetInt64(deptNode, "deptid");
        d.deptname     = GetString(deptNode, "deptname");
        d.parentdeptid = GetInt64(deptNode, "parentdeptid");
        d.deptlevel    = static_cast<int32_t>(GetInt64(deptNode, "deptlevel"));
        d.deptorder    = static_cast<int32_t>(GetInt64(deptNode, "deptorder"));
        d.leaderuserid = GetInt64(deptNode, "leaderuserid");
        d.deptdesc     = GetString(deptNode, "deptdesc");

        //members 可缺省（只挂子部门不挂人的部门），缺省时该部门成员数为 0。
        if (deptNode.isMember("members") && deptNode["members"].isArray())
        {
            const Json::Value& memberArray = deptNode["members"];
            for (Json::ArrayIndex j = 0; j < memberArray.size(); ++j)
            {
                DepartmentMember m;
                MemberFromJson(memberArray[j], m);
                d.members.push_back(m);
            }
        }

        outDepts.push_back(d);
    }

    return true;
}

std::string BuildGetOrgStructureResp(int32_t code, const std::string& msg,
                                     const std::vector<DepartmentInfo>& depts)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;

    //departments 始终输出为数组：即使为空也输出 []，保证客户端取字段时类型稳定。
    Json::Value deptArray(Json::arrayValue);
    for (size_t i = 0; i < depts.size(); ++i)
    {
        const DepartmentInfo& d = depts[i];
        Json::Value deptNode;
        deptNode["deptid"]       = static_cast<Json::Int64>(d.deptid);
        deptNode["deptname"]     = d.deptname;
        deptNode["parentdeptid"] = static_cast<Json::Int64>(d.parentdeptid);
        deptNode["deptlevel"]    = d.deptlevel;
        deptNode["deptorder"]    = d.deptorder;
        deptNode["leaderuserid"] = static_cast<Json::Int64>(d.leaderuserid);
        deptNode["deptdesc"]     = d.deptdesc;

        Json::Value memberArray(Json::arrayValue);
        for (size_t j = 0; j < d.members.size(); ++j)
        {
            Json::Value memberNode;
            MemberToJson(d.members[j], memberNode);
            memberArray.append(memberNode);
        }
        deptNode["members"] = memberArray;

        deptArray.append(deptNode);
    }
    root["departments"] = deptArray;

    return DumpJson(root);
}

std::string BuildOperateDepartmentResp(int32_t code, const std::string& msg,
                                       int32_t operationtype, int64_t deptid)
{
    Json::Value root;
    root["code"]          = code;
    root["msg"]           = msg;
    root["operationtype"] = operationtype;
    root["deptid"]        = static_cast<Json::Int64>(deptid);
    return DumpJson(root);
}

std::string BuildOperateDeptMemberResp(int32_t code, const std::string& msg,
                                       int32_t operationtype, int64_t deptid, int64_t userid)
{
    Json::Value root;
    root["code"]          = code;
    root["msg"]           = msg;
    root["operationtype"] = operationtype;
    root["deptid"]        = static_cast<Json::Int64>(deptid);
    root["userid"]        = static_cast<Json::Int64>(userid);
    return DumpJson(root);
}

std::string BuildOrgStructureNotify(int32_t changetype, int64_t deptid)
{
    Json::Value root;
    root["changetype"] = changetype;
    root["deptid"]     = static_cast<Json::Int64>(deptid);
    return DumpJson(root);
}

} // namespace dept
