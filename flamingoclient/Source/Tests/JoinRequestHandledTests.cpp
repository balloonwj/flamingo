/**
 *  加群申请审批的客户端单元测试（Suite JQC，标签 [joinrequest]），JoinRequestHandledTests.cpp
 *
 *  背景（docs/邀请入群方案.md 第 4 期）：一条申请只能被处理一次，没抢到或晚一步的管理员会收到 308；处理生效后服务端
 *  向该群全体群主与管理员推「加群申请已处理」（1329），群管理窗据此重拉待审批列表。
 *
 *    JQC1  审批应答回 308：提示「该申请已被其他管理员处理。」，不露出服务端的英文原文；其他操作回 308、
 *          审批的其他错误码仍按原来的规则提示
 *    JQC2  1329 推送的解析：字段齐全时成功；处理人昵称缺省时为空串；缺群 id、缺申请 id、status 不是整数时失败
 *
 *  框架 Catch2 v3。用例里的界面文字按未加载语言包时的简体中文核对。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonGroupManage/GroupManageMapping.h"
#include "../net/GroupProtocolClient.h"

namespace {

//命令号与错误码：与 net/Protocol.h 的 cmd_handle_join_group、cmd_kick_group_member、
//error_code_join_req_not_exist、error_code_not_group_admin 一致
const int kCmdHandleJoinGroup = 1308;
const int kCmdKickMember = 1310;
const int kErrJoinRequestHandled = 308;
const int kErrNotGroupAdmin = 303;

//JQC2 的样例：群与申请
const int64_t kGroupId = 268435463;
const int64_t kRequestId = 5001;
const int64_t kHandler = 5127;

//处理结果：1 已通过 2 已拒绝
const int kStatusAccepted = 1;
const int kStatusRejected = 2;

} // namespace

TEST_CASE("JQC1 审批应答回 308 时提示申请已被其他管理员处理", "[joinrequest][groupmanage][failuretip]")
{
    const CString tip = balloongroupmanage::DescribeGroupOpFailure(kCmdHandleJoinGroup, kErrJoinRequestHandled,
                                                                   "join request already handled");
    CHECK(tip == CString(L"该申请已被其他管理员处理。"));
    //不该把服务端的英文原文直接甩给用户
    CHECK(tip.Find(_T("already handled")) < 0);

    //其他操作回 308（不应出现）：仍走兜底，附上服务端原文
    const CString other = balloongroupmanage::DescribeGroupOpFailure(kCmdKickMember, kErrJoinRequestHandled, "x");
    CHECK(other.Find(_T("操作失败")) >= 0);

    //审批的其他错误码照旧
    const CString notAdmin = balloongroupmanage::DescribeGroupOpFailure(kCmdHandleJoinGroup, kErrNotGroupAdmin, "");
    CHECK(notAdmin.Find(_T("群主")) >= 0);
}

TEST_CASE("JQC2 「加群申请已处理」推送的解析", "[joinrequest]")
{
    net::JoinRequestHandledNotify n;
    REQUIRE(net::GroupProtocolClient::parseJoinRequestHandledNotify(
        u8"{\"groupid\":\"268435463\",\"requestid\":\"5001\",\"status\":1,\"handler\":\"5127\",\"handlername\":\"张勇\"}",
        n));
    CHECK(n.m_groupid == kGroupId);
    CHECK(n.m_requestid == kRequestId);
    CHECK(n.m_status == kStatusAccepted);
    CHECK(n.m_handler == kHandler);
    CHECK(n.m_handlerName == u8"张勇");

    //处理人昵称缺省：为空串
    net::JoinRequestHandledNotify noName;
    REQUIRE(net::GroupProtocolClient::parseJoinRequestHandledNotify(
        "{\"groupid\":\"268435463\",\"requestid\":\"5001\",\"status\":2,\"handler\":\"5010\"}", noName));
    CHECK(noName.m_status == kStatusRejected);
    CHECK(noName.m_handlerName.empty());

    //缺群 id、缺申请 id、status 不是整数、不是 JSON：解析失败
    net::JoinRequestHandledNotify bad;
    CHECK_FALSE(net::GroupProtocolClient::parseJoinRequestHandledNotify(
        "{\"requestid\":\"5001\",\"status\":1}", bad));
    CHECK_FALSE(net::GroupProtocolClient::parseJoinRequestHandledNotify(
        "{\"groupid\":\"268435463\",\"status\":1}", bad));
    CHECK_FALSE(net::GroupProtocolClient::parseJoinRequestHandledNotify(
        "{\"groupid\":\"268435463\",\"requestid\":\"5001\",\"status\":\"1\"}", bad));
    CHECK_FALSE(net::GroupProtocolClient::parseJoinRequestHandledNotify("not json", bad));
}
