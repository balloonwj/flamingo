/**
 *  HTTP admin 接口的错误码与部门 / 成员状态取值，AdminApiCodes.h
 *
 *  本文件原名 Msg.h，是 v1 时代的聊天协议定义。v2 协议改用 Protocol.h 之后，里面与
 *  聊天相关的部分（msg_type、online_type、chat_msg_header、好友与部门的各种
 *  operation_type，以及文件尾部那一大段 v1 协议的 JSON 示例）已全部无人使用，
 *  2026-08-14 整段删除；剩下的三个枚举并不是残留，而是 admin 控制台走的那条 HTTP
 *  短连接接口在用的东西，故连同文件一起改成现名。v2 协议的报文示例见
 *  docs/协议设计-v2.md。
 *
 *  为什么这些取值不并进 Protocol.h：两个头定义了同名的 error_code 枚举<u>且取值不同</u>
 *  （例如 error_code_account_disabled 在此处是 117、在 Protocol.h 里是 104），同时进一个
 *  翻译单元会重定义冲突。HttpSession 及其下游的各个 Manager 因此只包含本文件、不包含
 *  Protocol.h（见 HttpSession.cpp 顶部的说明，以及 ChatSession.h 里的同类注释）。
 *  要统一编号得连 flamingoAdmin 一起改，属于另一件事。
 *
 *  改动本文件的错误码取值即改变 HTTP 接口的对外行为，flamingoAdmin 必须同步。
 *
 *  zhangyl 2017.03.02
 */
#pragma once

#include <stdint.h>

//部门状态（对应数据库 t_department.f_status）
enum department_status
{
    //正常：在用、对客户端可见
    department_status_normal = 0,
    //停用：软删除，保留历史数据，不下发给普通客户端
    department_status_disabled = 1
};

//部门成员状态（对应数据库 t_department_member.f_status）
enum deptmember_status
{
    //在职：有效的部门归属关系
    deptmember_status_active = 0,
    //已移出/离职：软删除，保留历史数据，不下发给普通客户端
    deptmember_status_removed = 1
};

/** 
 *  错误码
 *  0   成功
 *  1   未知失败
 *  2   用户未登录
 *  100 注册失败
 *  101 已注册
 *  102 未注册
 *  103 密码错误
 *  104 更新用户信息失败
 *  105 修改密码失败
 *  106 创建群失败
 *  107 客户端版本太旧，需要升级成新版本
 *  108 修改备注失败
 *  109 分组已经存在
 *  110 部门不存在
 *  111 同级已存在同名部门
 *  112 部门下仍有在用子部门或在职成员，不能删除
 *  113 非法父部门（不存在，或会使部门树成环）
 *  114 该用户已在此部门
 *  115 该用户不在此部门
 *  116 无操作企业组织架构的权限
 */
//TODO: 其他的地方改成这个错误码
enum error_code
{
    error_code_ok                   = 0,
    error_code_unknown              = 1,
    error_code_notlogin             = 2,
    error_code_registerfail         = 100,
    error_code_registeralready      = 101,
    error_code_notregister          = 102,
    error_code_invalidpassword      = 103,
    error_code_updateuserinfofail   = 104,
    error_code_modifypasswordfail   = 105,
    error_code_creategroupfail      = 106,
    error_code_toooldversion        = 107,
    error_code_modifymarknamefail   = 108,
    error_code_teamname_exist       = 109, //分组已经存在
    error_code_dept_notexist         = 110, //部门不存在
    error_code_dept_nameexist        = 111, //同级已存在同名部门
    error_code_dept_hassub           = 112, //部门下仍有在用子部门或在职成员，不能删除
    error_code_dept_invalidparent    = 113, //非法父部门：不存在，或会使部门树成环
    error_code_deptmember_exist      = 114, //该用户已在此部门
    error_code_deptmember_notexist   = 115, //该用户不在此部门
    error_code_dept_permissiondenied = 116, //无操作企业组织架构的权限
    error_code_account_disabled      = 117, //账号被 admin 控制台管理员停用，禁止登录任何客户端
    error_code_dept_isroot           = 118, //目标部门是代表企业本身的"公司根部门"，不允许删除或移动
    error_code_enterprise_nameexist  = 119, //企业全称已被占用；admin 控制台新建企业或给企业改名时重名会返回它
};
