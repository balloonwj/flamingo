/**
 *  DepartmentMsg 报文 JSON 收发单元测试（Suite B），test_department_msg.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "doctest.h"
#include "../chatserversrc/DepartmentMsg.h"
#include "../jsoncpp1.9.0/json.h"

//测试辅助：把 JSON 文本解析为 Json::Value，解析失败时直接断言失败。
static Json::Value ParseToJson(const std::string& text)
{
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value root;
    JSONCPP_STRING errs;
    bool ok = reader->parse(text.c_str(), text.c_str() + text.length(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

TEST_CASE("B1 parse 1201 create-department request")
{
    const std::string json =
        "{\"operationtype\":1,\"parentdeptid\":1001,\"deptname\":\"测试部\","
        "\"deptdesc\":\"\",\"leaderuserid\":0,\"deptorder\":0}";
    dept::OperateDepartmentReq req;
    REQUIRE(dept::ParseOperateDepartmentReq(json, req));
    CHECK(req.operationtype == 1);
    CHECK(req.parentdeptid == 1001);
    CHECK(req.deptname == "测试部");
}

TEST_CASE("B2 parse 1201 move-department request")
{
    const std::string json =
        "{\"operationtype\":4,\"deptid\":1009,\"newparentdeptid\":1005}";
    dept::OperateDepartmentReq req;
    REQUIRE(dept::ParseOperateDepartmentReq(json, req));
    CHECK(req.operationtype == 4);
    CHECK(req.deptid == 1009);
    CHECK(req.newparentdeptid == 1005);
}

TEST_CASE("B3 parse 1202 add-member request")
{
    const std::string json =
        "{\"operationtype\":1,\"deptid\":1005,\"userid\":20,"
        "\"position\":\"工程师\",\"isprimary\":0,\"memberorder\":0}";
    dept::OperateDeptMemberReq req;
    REQUIRE(dept::ParseOperateDeptMemberReq(json, req));
    CHECK(req.operationtype == 1);
    CHECK(req.deptid == 1005);
    CHECK(req.userid == 20);
    CHECK(req.position == "工程师");
    CHECK(req.isprimary == 0);
}

TEST_CASE("B4 parse invalid json and missing operationtype")
{
    dept::OperateDepartmentReq req;
    //非法 JSON 文本
    CHECK_FALSE(dept::ParseOperateDepartmentReq("not a json", req));
    //合法 JSON 但缺 operationtype 字段
    CHECK_FALSE(dept::ParseOperateDepartmentReq("{\"deptid\":1009}", req));
    //operationtype 类型不是整型
    CHECK_FALSE(dept::ParseOperateDepartmentReq("{\"operationtype\":\"x\"}", req));

    dept::OperateDeptMemberReq mreq;
    //空字符串
    CHECK_FALSE(dept::ParseOperateDeptMemberReq("", mreq));
}

TEST_CASE("B5 build 1200 response and round-trip parse")
{
    //构造两个部门：技术部挂 1 名成员，行政部不挂人（只挂子部门的场景）。
    std::vector<dept::DepartmentInfo> depts;

    dept::DepartmentInfo tech;
    tech.deptid = 1005;
    tech.deptname = "技术部";
    tech.parentdeptid = 1001;
    tech.deptlevel = 2;
    dept::DepartmentMember m;
    m.userid = 12;
    m.nickname = "李工";
    m.position = "后端工程师";
    m.isprimary = 1;
    tech.members.push_back(m);
    depts.push_back(tech);

    dept::DepartmentInfo admin;
    admin.deptid = 1006;
    admin.deptname = "行政部";
    admin.parentdeptid = 1001;
    admin.deptlevel = 2;
    depts.push_back(admin);

    const std::string json = dept::BuildGetOrgStructureResp(0, "ok", depts);

    std::vector<dept::DepartmentInfo> parsed;
    REQUIRE(dept::ParseGetOrgStructureResp(json, parsed));
    REQUIRE(parsed.size() == 2);

    CHECK(parsed[0].deptid == 1005);
    CHECK(parsed[0].deptname == "技术部");
    CHECK(parsed[0].parentdeptid == 1001);
    REQUIRE(parsed[0].members.size() == 1);
    CHECK(parsed[0].members[0].userid == 12);
    CHECK(parsed[0].members[0].nickname == "李工");
    CHECK(parsed[0].members[0].position == "后端工程师");
    CHECK(parsed[0].members[0].isprimary == 1);

    //行政部只挂子部门不挂人，成员数应为 0。
    CHECK(parsed[1].deptid == 1006);
    CHECK(parsed[1].members.size() == 0);
}

TEST_CASE("B6 build 1201 and 1202 responses")
{
    //1201 应答含 code / operationtype / deptid
    Json::Value r1 = ParseToJson(dept::BuildOperateDepartmentResp(0, "ok", 1, 1009));
    CHECK(r1["code"].asInt() == 0);
    CHECK(r1["operationtype"].asInt() == 1);
    CHECK(r1["deptid"].asInt64() == 1009);

    //1202 应答含 code / operationtype / deptid / userid
    Json::Value r2 = ParseToJson(dept::BuildOperateDeptMemberResp(114, "exist", 1, 1005, 20));
    CHECK(r2["code"].asInt() == 114);
    CHECK(r2["operationtype"].asInt() == 1);
    CHECK(r2["deptid"].asInt64() == 1005);
    CHECK(r2["userid"].asInt64() == 20);
}

TEST_CASE("B7 build 1203 org-structure-changed notify")
{
    Json::Value n = ParseToJson(dept::BuildOrgStructureNotify(1, 1009));
    CHECK(n["changetype"].asInt() == 1);
    CHECK(n["deptid"].asInt64() == 1009);
}

TEST_CASE("B8 org-structure-changed notify 覆盖成员变更与无部门归属两种取值")
{
    //changetype=2（成员归属变更）：管理控制台改员工资料 / 启停用 / 设角色时用这一种。
    Json::Value member = ParseToJson(
        dept::BuildOrgStructureNotify(dept::org_change_member, 2001));
    CHECK(member["changetype"].asInt() == dept::org_change_member);
    CHECK(member["deptid"].asInt64() == 2001);

    //deptid=0：本次变更不针对某个具体部门（典型：改员工资料、删除员工、改企业名称）。
    //0 是合法取值而非"缺失"，客户端据此整棵通讯录重拉，不能把它当成解析失败。
    Json::Value noDept = ParseToJson(
        dept::BuildOrgStructureNotify(dept::org_change_member, 0));
    CHECK(noDept["changetype"].asInt() == dept::org_change_member);
    CHECK(noDept["deptid"].asInt64() == 0);

    //部门ID 超出 32 位后仍须原样保留，不能在 JSON 编码环节被截断。
    const int64_t BIG_DEPT_ID = 4300000000LL;   //略大于 2^32，用来暴露 32 位截断
    Json::Value big = ParseToJson(
        dept::BuildOrgStructureNotify(dept::org_change_department, BIG_DEPT_ID));
    CHECK(big["deptid"].asInt64() == BIG_DEPT_ID);
}

//成员的账号状态（accountstatus，0 启用 / 1 停用）与在线状态（status）是两个独立字段，
//管理控制台曾把在线状态 1 当成停用。两者取不同值时要各自编码、往返解析后保持不变。
TEST_CASE("B9 1200 response carries accountstatus independent of presence status")
{
    std::vector<dept::DepartmentInfo> depts;
    dept::DepartmentInfo tech;
    tech.deptid = 1005;
    tech.deptname = "技术部";

    //在线、账号启用
    dept::DepartmentMember online;
    online.userid = 21;
    online.status = 1;
    online.accountstatus = 0;
    tech.members.push_back(online);

    //离线、账号已停用
    dept::DepartmentMember disabled;
    disabled.userid = 22;
    disabled.status = 0;
    disabled.accountstatus = 1;
    tech.members.push_back(disabled);
    depts.push_back(tech);

    const std::string json = dept::BuildGetOrgStructureResp(0, "ok", depts);
    Json::Value root = ParseToJson(json);
    const Json::Value& members = root["departments"][0]["members"];
    REQUIRE(members.size() == 2);
    CHECK(members[0]["status"].asInt() == 1);
    CHECK(members[0]["accountstatus"].asInt() == 0);
    CHECK(members[1]["status"].asInt() == 0);
    CHECK(members[1]["accountstatus"].asInt() == 1);

    std::vector<dept::DepartmentInfo> parsed;
    REQUIRE(dept::ParseGetOrgStructureResp(json, parsed));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].members.size() == 2);
    CHECK(parsed[0].members[0].status == 1);
    CHECK(parsed[0].members[0].accountstatus == 0);
    CHECK(parsed[0].members[1].status == 0);
    CHECK(parsed[0].members[1].accountstatus == 1);
}
