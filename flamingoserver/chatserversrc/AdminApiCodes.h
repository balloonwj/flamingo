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
    //以下五个为企业级敏感词的管理后台接口（/admin/sensitive/*.do）所用，见 docs/敏感词方案.md 第 6.7 节；
    //由 SensitiveWordManager 的写操作结果换算而来（sensitiveadmin::ToAdminCode），导入时也作为跳过原因
    error_code_sensitive_word_invalid  = 120, //词条为空，或去掉空白、标点符号与表情之后为空
    error_code_sensitive_word_toolong  = 121, //词条去掉首尾空白后超过 32 个字符
    error_code_sensitive_word_exist    = 122, //归一化后与本企业已有词条重复（导入时也包括与同一批里靠前的行重复）
    error_code_sensitive_word_full     = 123, //本企业的词条数已达 10000 条上限
    error_code_sensitive_word_notexist = 124, //要修改的词条不存在，或不属于该企业

    //以下四个为文件共享的管理后台接口（/admin/setting/*、/admin/fileshare/*）所用，见 docs/文件共享方案.md 6.5
    error_code_fileshare_notexist      = 130, //共享不存在（shareid 无效）
    error_code_fileshare_canceled      = 131, //共享已经取消过，不能再强制取消
    error_code_fileshare_denied        = 132, //共享不属于企业管理员的本企业
    error_code_feature_setting_invalid = 133, //功能设置项或取值不认识：开关类设置项以 FeatureSwitchManager.cpp 的 kKnownFeatures
                                              //为准（如 fileshare.enabled、p2p.enabled、group.inviteconfirm、secretchat.enabled），取值 0 / 1

    //以下为平台公告的管理后台接口（/admin/announcement/*）所用；140 起与上面按顺序追加的编号错开
    error_code_announcement_notpublished = 140, //公告不是「已发布」状态（草稿或已撤回），不能重新推送

    //以下为员工手机号与企业账号前缀所用（2026-10-06，docs/登录账号与手机号登录方案.md）；150 起另开一段
    error_code_phone_invalid           = 150, //新建或编辑员工时手机号为空，或不是 11 位以 1 开头的数字
    error_code_phone_taken             = 151, //新建或编辑员工时手机号已被其他账号使用（手机号可用于登录，必须全局唯一）
    error_code_account_prefix_invalid  = 152, //企业账号前缀不合法：只能由字母与点号组成、1～10 个字符、至少一个字母

    //以下为企业问答的管理后台接口（/admin/qa/*.do）所用（2026-10-07，docs/群问答方案.md）；160 起另开一段
    error_code_qa_invalid              = 160, //问题或答案为空，或超过长度上限（问题 128 字、答案 2048 字）
    error_code_qa_notexist             = 161, //要修改或删除的企业问答不存在，或不属于该企业
    error_code_qa_full                 = 162, //本企业的企业问答已达上限
    error_code_qa_group_invalid        = 163, //应用到所选的群时没有选群，或选了不属于本企业的群

    //以下为机器人的管理后台接口（/admin/bot/*.do）所用（2026-10-08，docs/机器人方案.md 6.3）；170 起另开一段。
    //与 Protocol.h 里给客户端用的 error_code_bot_* 不是一回事，故名称带 botadmin，免得两个头文件同时被引用时撞名
    error_code_botadmin_invalid        = 170, //机器人或规则的字段不合法：名称为空或超长、关键词为空、回复为空、时间格式错误等
    error_code_botadmin_notexist       = 171, //机器人不存在、已删除，或不是本企业的企业机器人
    error_code_botadmin_full           = 172, //本企业的企业机器人已达上限（50 个），或规则、关键词超出上限
    error_code_botadmin_group_invalid  = 173, //应用到所选的群时没有选群，或选了不属于本企业的群
    error_code_botadmin_not_allowed    = 174, //规则用了不允许的触发方式或动作，或外部服务地址不在域名白名单内
    error_code_botadmin_domain_invalid = 175, //域名白名单里有不合法的域名，或超过 50 条
    error_code_botadmin_readonly       = 176, //仓库通知的机器人只能在「仓库通知」页修改
};
