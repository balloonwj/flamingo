/**
 *  FlamingoIM v2 通信协议定义：帧头、消息类型、错误码及各类枚举。
 *
 *  本文件是客户端与服务端 v2 协议的单一事实来源，是旧版 Msg.h 的后继。
 *  完整设计见 docs/协议设计-v2.md。v1 与 v2 协议不兼容，迁移期内本文件
 *  不得与旧版 Msg.h 在同一编译单元（.cpp）内同时包含，否则枚举名冲突。
 *
 *  典型用法：帧层用 frame_header 收发；消息层按 msg_type 分发，包体为
 *  UTF-8 JSON 文本（不带 BOM）。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>

//==================== 协议常量 ====================

//帧头魔数：每个 v2 帧以此开头，用于帧定位与脏数据丢弃
constexpr uint16_t FRAME_MAGIC = 0xF1A3;
//当前协议版本号，写入帧头 version 字段
constexpr uint8_t  PROTOCOL_VERSION_V2 = 0x02;
//帧头固定长度，单位：字节
constexpr uint32_t FRAME_HEADER_SIZE = 24;
//包体超过该字节数才尝试 zlib 压缩，未达阈值直接明文发送
constexpr uint32_t COMPRESS_THRESHOLD = 256;
//chatserver 单帧包体长度上限：1 MiB
constexpr uint32_t MAX_BODY_CHATSERVER = 1u << 20;
//fileserver / imgserver 单帧包体长度上限：2 MiB
constexpr uint32_t MAX_BODY_FILESERVER = 2u << 20;
//文件上传 / 下载的分片字节数：512 KiB（沿用设计稿"512 KB 上传分片上限"，
//见 服务器端更新日志.txt）。chatserver 在 cmd_request_upload 应答中下发，
//fileserver / imgserver 下载时按此长度分片回传。
constexpr uint32_t FILE_CHUNK_SIZE = 512u * 1024u;

//==================== 帧标志位 ====================

//帧头 flags 字段的位定义，可按位或组合
enum frame_flag
{
    frame_flag_compressed     = 0x01,   //包体已用 zlib 压缩，此时 bodylen < originlen
    frame_flag_encrypted      = 0x02,   //包体已加密（本期保留不实现，优先用传输层 TLS）
    frame_flag_has_attachment = 0x04,   //包体含二进制附件（文件/图片传输用）
};

//==================== 帧头 ====================

#pragma pack(push, 1)
//v2 协议帧头，固定 24 字节；所有多字节整型字段在网络上以大端序传输
struct frame_header
{
    uint16_t magic;       //魔数，固定为 FRAME_MAGIC，用于帧定位与脏数据丢弃
    uint8_t  version;     //协议版本号，当前为 PROTOCOL_VERSION_V2
    uint8_t  flags;       //位标志，取值见 enum frame_flag
    uint16_t cmd;         //消息类型，取值见 enum msg_type
    uint16_t reserved;    //保留字段，必须填 0，供后续扩展与对齐使用
    uint32_t seq;         //序列号：请求与应答的关联号；服务器主动推送时为 0
    uint32_t bodylen;     //线上包体长度（压缩/加密后实际传输字节数）
    uint32_t originlen;   //包体原始长度（未压缩）；未压缩时等于 bodylen
    uint32_t headercrc;   //帧头前 20 字节的 CRC-32 校验值；为 0 表示发送方不提供校验
};
#pragma pack(pop)

//==================== 消息类型 ====================

/**
 *  消息类型 cmd。请求与其应答共用同一取值，收发方向由上下文隐含决定；
 *  名称以 _notify 结尾的为服务器主动推送（推送帧 seq 恒为 0）。
 *  分段：1000 会话连接 / 1100 账号资料 / 1200 好友 / 1300 群组 /
 *       1400 企业组织 / 1500 消息会话 / 1600 群互动 / 1700 音视频 /
 *       1800 文件图片 / 1900 收藏其它 / 2000 服务号。
 */
enum msg_type
{
    cmd_unknown = 0,                            //未知/无效消息类型，用于错误兜底

    //==== 会话与连接 1000-1099 ====
    cmd_handshake                = 1001,        //保留：原独立握手；v2 已将版本协商折叠进 cmd_login，暂不启用
    cmd_heartbeat                = 1002,        //心跳保活，空包体，双向，默认 30 秒一次
    cmd_login                    = 1003,        //账号登录鉴权，请求携带客户端版本号
    cmd_logout                   = 1004,        //客户端主动登出
    cmd_resume                   = 1005,        //断线重连、恢复会话（后续阶段实现）
    cmd_kickout                  = 1006,        //推送：当前连接被踢下线（异地登录/账号停用/服务器维护）
    cmd_get_devices              = 1007,        //查询本账号当前在线设备列表
    cmd_kick_device              = 1008,        //踢掉本账号的指定在线设备
    cmd_device_changed_notify    = 1009,        //推送：本账号其他设备上线/下线

    //==== 账号与资料 1100-1199 ====
    cmd_register                 = 1101,        //注册新账号
    cmd_send_sms_code            = 1102,        //发送注册/找回密码用的短信验证码
    cmd_get_user_info            = 1103,        //获取指定用户的资料（自己或他人）
    cmd_update_profile           = 1104,        //更新本人资料（昵称/签名/性别等）
    cmd_change_password          = 1105,        //修改登录密码
    cmd_change_avatar            = 1106,        //修改头像
    cmd_change_presence          = 1107,        //修改在线状态（在线/离开/勿扰/隐身）
    cmd_user_info_changed_notify = 1108,        //推送：某联系人资料发生变更
    cmd_presence_changed_notify  = 1109,        //推送：某联系人在线状态发生变更
    cmd_reset_password           = 1110,        //忘记密码：凭短信验证码重置登录密码

    //==== 好友与联系人 1200-1299 ====
    cmd_get_contacts                   = 1201,  //拉取通讯录（企业部门树 + 部门成员 + 群组 + 服务号 + 收藏）
    cmd_search                         = 1202,  //查找用户/群/企业
    //1203~1208 原为好友关系子系统（cmd_add_friend_request / cmd_friend_request_notify /
    //cmd_handle_friend_request / cmd_friend_request_result_notify / cmd_delete_friend /
    //cmd_friend_relation_changed_notify），企业版已于 2026-08-05 删除：通讯录直接来自
    //企业组织架构（t_department_member），同事之间不需要"先加好友"这一步。
    //编号留空<u>不要复用</u>，避免与仍在使用旧协议的存量客户端撞号。
    cmd_set_friend_remark              = 1209,  //设置联系人备注名（备注名单向，落 t_contact_remark）
    //1210 原为 cmd_set_friend_tags（好友标签），随好友关系子系统一并删除，编号留空不复用。
    //1211 / 1212 原为 cmd_move_friend_team / cmd_operate_team（QQ 式好友分组），v2 已取消：
    //通讯录以企业组织架构为中心，联系人的归属由部门表达，不再有好友分组这一层。
    //编号留空不复用。详见 docs/协议设计-v2.md。
    cmd_set_blacklist                  = 1213,  //加入/移出黑名单
    //1214 原为 cmd_get_notifications（拉取加好友申请的消息盒子），随好友关系子系统一并删除，
    //编号留空不复用。注意平台公告的消息盒子是 cmd_get_announcements（2101），与本条无关。
    cmd_set_favorite                   = 1215,  //添加/取消收藏（收藏对象为联系人或群组）

    //==== 群组 1300-1399 ====
    cmd_create_group                 = 1301,    //创建群聊
    cmd_dismiss_group                = 1302,    //解散群聊（仅群主）
    cmd_get_group_info               = 1303,    //获取群资料
    cmd_get_group_members            = 1304,    //获取群成员列表
    cmd_invite_to_group              = 1305,    //邀请好友入群
    cmd_join_group_request           = 1306,    //申请加入群聊
    cmd_join_group_request_notify    = 1307,    //推送：加群申请送达群管理员
    cmd_handle_join_group            = 1308,    //审批加群申请
    cmd_leave_group                  = 1309,    //退出群聊
    cmd_kick_group_member            = 1310,    //移出群成员
    cmd_update_group_info            = 1311,    //修改群名/头像/公告/加群方式等
    cmd_set_member_role              = 1312,    //设置/取消管理员
    cmd_mute_member                  = 1313,    //禁言/解禁单个成员
    cmd_mute_all                     = 1314,    //全员禁言开关
    cmd_set_member_card              = 1315,    //修改本人群昵称
    cmd_transfer_owner               = 1316,    //转让群主
    cmd_group_member_changed_notify  = 1317,    //推送：群成员变动（加入/退出/被踢/角色变更）
    cmd_group_info_changed_notify    = 1318,    //推送：群资料变更
    cmd_get_group_join_requests      = 1319,    //拉取待审批的加群申请列表
    cmd_get_user_groups              = 1320,    //拉取本人加入的全部群列表（含无消息的空群）
    cmd_get_sensitive_words          = 1321,    //拉取本群敏感词库（仅群主 / 管理员）
    cmd_operate_sensitive_word       = 1322,    //敏感词增 / 删（仅群主 / 管理员）
    cmd_get_group_announcements      = 1323,    //拉取本群公告列表（全体群成员可拉）
    cmd_publish_group_announcement   = 1324,    //发布 / 重新发布群公告（仅群主 / 管理员）
    cmd_delete_group_announcement    = 1325,    //删除群公告（仅群主 / 管理员，真删不可恢复）
    cmd_group_announcement_notify    = 1326,    //推送：群公告发布 / 被删除

    //==== 企业组织架构 1400-1499 ====
    cmd_get_org_structure   = 1401,             //拉取企业组织架构（部门树 + 成员）
    cmd_operate_department  = 1402,             //部门新建/修改/删除/移动
    cmd_operate_dept_member = 1403,             //部门成员添加/修改/移除/移动
    cmd_org_structure_notify = 1404,            //推送：组织架构发生变更

    //==== 会话与消息 1500-1599 ====
    cmd_sync_conversations         = 1501,      //同步最近会话列表
    cmd_sync_messages             = 1502,       //增量/翻页拉取某会话消息（漫游）
    cmd_send_message              = 1503,       //发送消息，应答即为发送回执
    cmd_message_notify            = 1504,       //推送：收到新消息
    cmd_message_received_ack      = 1505,       //送达回执（客户端 → 服务器）
    cmd_mark_read                 = 1506,       //上报已读位点
    cmd_message_read_notify       = 1507,       //推送：对方已读你的消息
    cmd_recall_message            = 1508,       //撤回消息
    cmd_message_recalled_notify   = 1509,       //推送：某条消息被撤回
    cmd_typing_notify             = 1510,       //“正在输入”提示（双向，不落库、不可靠）
    cmd_forward_message           = 1511,       //转发/合并转发消息
    cmd_delete_message            = 1512,       //删除本端消息（多端同步删除状态）
    cmd_set_conversation          = 1513,       //会话置顶/免打扰/删除
    cmd_conversation_changed_notify = 1514,     //推送：会话属性多端同步
    cmd_search_messages           = 1515,       //会话内消息记录搜索（筛选+翻页）
    cmd_mass_send                 = 1516,       //群发助手：一条消息发给多个联系人
    cmd_file_transfer_ack         = 1517,       //文件收取进度回执（双向）：接收方上报"开始接收/已接收/已拒绝"，服务端落库后转推发送方
    cmd_pin_message               = 1518,       //Pin / 取消 Pin 一条消息（会话内共享；pin 字段取 1 钉住、0 取消，一条命令兼管两个方向）
    cmd_message_pinned_notify     = 1519,       //推送：某条消息被 Pin / 取消 Pin。单聊推双方、群聊推全体成员，操作者本人也会收到
    cmd_get_pinned_messages       = 1520,       //拉取某会话被 Pin 的消息（带完整消息体，供客户端本地库缺行时补齐；单次最多返回 PIN_FETCH_LIMIT 条，应答的 total 带回真实总数）

    //==== 群互动卡片 1600-1699 ====
    cmd_vote_poll            = 1601,            //对投票卡片投票
    cmd_poll_changed_notify  = 1602,            //推送：投票结果更新
    cmd_join_relay           = 1603,            //参与接龙
    cmd_relay_changed_notify = 1604,            //推送：接龙内容更新
    cmd_update_todo          = 1605,            //待办勾选/完成
    cmd_todo_changed_notify  = 1606,            //推送：待办状态更新

    //==== 音视频通话 1700-1799 ====
    cmd_call_invite        = 1701,              //发起语音/视频通话
    cmd_call_invite_notify = 1702,              //推送：收到通话邀请
    cmd_call_response      = 1703,              //接听/拒绝通话
    cmd_call_cancel        = 1704,              //取消呼叫（接通前）
    cmd_call_hangup        = 1705,              //挂断（接通后）
    cmd_call_state_notify  = 1706,              //推送：通话状态变更（响铃/接通/结束/超时）
    cmd_call_signaling     = 1707,              //RTC 信令透传（SDP/ICE/屏幕共享控制）
    cmd_call_switch_media  = 1708,              //通话中切换媒体类型（语音<->视频），仅已接通态可用
    cmd_call_media_changed_notify = 1709,       //推送：通话媒体类型已变更，双方（含发起者）均收到

    //==== 远程桌面 1710-1716（信令走 chatserver；画面流 / 控制流走 imgserver 中转）====
    cmd_rd_invite          = 1710,              //发起远程桌面：控制端→服务器，应答回带本次会话 sessionid
    cmd_rd_invite_notify   = 1711,              //推送：被控端收到远程桌面邀请（带控制端 userid / 昵称）
    cmd_rd_response        = 1712,              //被控端应答邀请：accept 字段 1 接受 / 0 拒绝
    cmd_rd_cancel          = 1713,              //控制端在被控端应答前取消邀请
    cmd_rd_close           = 1714,              //任一端结束远程桌面会话（挂断 / 关闭窗口）
    cmd_rd_state_notify    = 1715,              //推送：会话状态变更（响铃 / 拒绝 / 取消 / 对方离线 / 超时 / 断开）
    cmd_rd_relay_info      = 1716,              //推送：下发 imgserver 中转地址 + relay 令牌 + 角色，双方据此连中转通道

    //==== 文件与图片 1800-1899 ====
    cmd_request_upload            = 1801,       //申请上传（含秒传判断，返回上传凭证）
    cmd_request_download          = 1802,       //申请下载（返回下载凭证）
    cmd_get_group_files           = 1803,       //群文件列表（分页+类型筛选）
    cmd_add_group_file            = 1804,       //将已上传文件登记为群文件
    cmd_delete_group_file         = 1805,       //删除群文件
    cmd_group_files_changed_notify = 1806,      //推送：群文件列表变更
    cmd_file_upload               = 1851,       //上传文件分片（仅 fileserver/imgserver 连接）
    cmd_file_download             = 1852,       //下载文件分片（仅 fileserver/imgserver 连接）
    cmd_rd_relay_hello            = 1853,       //远程桌面中转握手：连上 imgserver 后首帧报到，body 带 relay 令牌
    cmd_rd_relay_data             = 1854,       //远程桌面中转数据：屏幕帧 / 控制命令，imgserver 按会话配对原样转发、不解析
    cmd_av_relay_hello            = 1855,       //音视频中转握手：连上 imgserver 后首帧报到，body 带 relay 令牌（与 cmd_rd_relay_hello 同结构，imgserver 同样处理）
    cmd_av_relay_data             = 1856,       //音视频中转数据：编码后的音频 / 视频帧，imgserver 按会话配对原样转发、不解析（与 cmd_rd_relay_data 同处理）

    //==== 收藏与其它 1900-1999 ====
    cmd_add_favorite    = 1901,                 //收藏消息
    cmd_get_favorites   = 1902,                 //拉取收藏列表
    cmd_delete_favorite = 1903,                 //删除收藏
    cmd_global_search   = 1904,                 //全局搜索（联系人/群/消息/文件聚合）
    cmd_check_update    = 1905,                 //检查客户端更新

    //==== 服务号 2000-2099 ====
    cmd_get_service_accounts = 2001,            //拉取服务号列表（系统通知/客服/公告等）

    //==== 平台公告 2100-2199 ====
    //平台公告由企业管理控制台(flamingoAdmin)发布、经 chatserver 投递给 IM 客户端，
    //在 PC 主面板的「消息盒子」里展示。注意与群公告(1323~1326)不是一回事：群公告
    //归属某个具体的群，平台公告归属整个平台，受众由 t_announcement.f_audience_type 决定。
    cmd_get_announcements      = 2101,          //拉取发给本人的平台公告：客户端登录后与打开消息盒子时发，可带 sinceid 做增量拉取
    cmd_mark_announcement_read = 2102,          //上报某条平台公告已读：用户在消息盒子里点开该条时发，服务端据此写 t_announcement_read
    cmd_announcement_notify    = 2103,          //推送：平台公告有变动，changetype 取 1 表示新发布、取 2 表示已被管理员撤回
};

//==================== 错误码 ====================

/**
 *  统一错误码，置于各应答 JSON 包体的 code 字段。0 为成功，其余按业务域分段：
 *  0-99 通用 / 100-199 账号 / 200-299 好友 / 300-399 群组 /
 *  400-499 企业组织 / 500-599 消息 / 600-699 文件 / 700-799 音视频。
 */
enum error_code
{
    //==== 通用 0-99 ====
    error_code_ok                  = 0,    //成功
    error_code_server_internal     = 1,    //服务器内部错误
    error_code_not_login           = 2,    //尚未登录或登录态已失效
    error_code_bad_request         = 3,    //报文格式错误，无法解析
    error_code_version_too_old     = 4,    //客户端版本过低，需升级
    error_code_rate_limited        = 5,    //操作过于频繁，被限流
    error_code_permission_denied   = 6,    //无权限执行该操作
    error_code_not_found           = 7,    //目标对象不存在（通用）
    error_code_session_expired     = 8,    //会话凭证失效，需重新登录

    //==== 账号 100-199 ====
    error_code_register_fail       = 100,  //注册失败
    error_code_already_registered  = 101,  //账号已被注册
    error_code_not_registered      = 102,  //账号不存在
    error_code_wrong_password      = 103,  //密码错误
    error_code_account_disabled    = 104,  //账号被管理员停用
    error_code_sms_code_error      = 105,  //短信验证码错误或过期
    error_code_update_profile_fail = 106,  //更新资料失败
    error_code_change_pwd_fail     = 107,  //修改密码失败
    error_code_avatar_too_large    = 108,  //头像文件超过大小限制

    //==== 好友 200-299 ====
    error_code_friend_op_fail      = 200,  //联系人相关操作失败（拉黑失败等）
    //201 原 error_code_already_friend（对方已是好友），随好友关系子系统删除，编号留空不要复用
    error_code_not_friend          = 202,  //够不着该联系人（不在同一企业）；名字沿用旧称，客户端还按它提示
    //203 原 error_code_request_not_exist（加好友申请不存在或已处理），已删除，编号留空不要复用
    error_code_in_blacklist        = 204,  //已被对方拉黑
    //205 原 error_code_cannot_add_self（不能添加自己为好友），已删除，编号留空不要复用
    //206 原 error_code_friend_limit（好友数量已达上限），已删除，编号留空不要复用
    //207 / 208 原为好友分组错误码（同名分组已存在 / 分组不存在），随 QQ 式分组取消而删除，
    //编号留空不要复用
    error_code_favorite_limit      = 209,  //收藏数量已达上限（见 kMaxFavoriteCount）

    //==== 群组 300-399 ====
    error_code_create_group_fail   = 300,  //创建群失败
    error_code_group_not_exist     = 301,  //群不存在
    error_code_not_group_member    = 302,  //不是群成员
    error_code_not_group_admin     = 303,  //不是群主或管理员
    error_code_group_full          = 304,  //群成员已满
    error_code_already_in_group    = 305,  //已在群中
    error_code_member_muted        = 306,  //已被禁言，不能发言
    error_code_cannot_operate_owner = 307, //不能对群主执行该操作（移出 / 禁言 / 降级）
    error_code_sensitive_word      = 308,  //消息命中本群敏感词，已拒绝发送
    error_code_at_all_denied       = 309,  //无权 @ 全体成员（群设为仅群主和管理员可 @）
    error_code_join_need_approval  = 307,  //加群需管理员审批
    error_code_join_req_not_exist  = 308,  //加群申请不存在或已处理
    error_code_announcement_404    = 310,  //群公告不存在，或不属于本群（撤回 / 重新发布时）

    //==== 企业组织架构 400-499 ====
    error_code_dept_not_exist      = 400,  //部门不存在
    error_code_dept_name_exist     = 401,  //同级已存在同名部门
    error_code_dept_has_sub        = 402,  //部门下仍有子部门或在职成员，不能删除
    error_code_dept_invalid_parent = 403,  //非法父部门（不存在或会使部门树成环）
    error_code_deptmember_exist    = 404,  //该用户已在此部门
    error_code_deptmember_notexist = 405,  //该用户不在此部门
    error_code_org_permission      = 406,  //无操作企业组织架构的权限
    error_code_enterprise_not_exist = 407, //按公司全称找不到对应企业；注册时用户填错公司名会返回它
    error_code_enterprise_disabled = 408,  //企业存在但不可用（f_status 为 0 待审核或 2 已停用）；注册时禁止加入这类企业
    error_code_enterprise_no_root_dept = 409, //企业存在但尚未建立公司根部门（f_root_dept_id 为 0），新员工无处可挂，需运维补建
    //说明：企业"全称重名"没有在本表里占位。v2 客户端协议不含任何新建企业的命令，
    //建企业只走 admin 控制台的 HTTP 接口，那条链路用的是 Msg.h 里的
    //error_code_enterprise_nameexist(119)。这里不留一个服务端永远不会下发的码，
    //免得客户端写错误码映射表时凭空多出一个死分支。

    //==== 消息 500-599 ====
    error_code_send_msg_fail       = 500,  //消息发送失败
    error_code_recall_timeout      = 501,  //超出可撤回时限
    error_code_recall_not_allowed  = 502,  //无权撤回该消息
    error_code_message_not_exist   = 503,  //消息不存在
    error_code_conversation_404    = 504,  //会话不存在
    error_code_msg_too_large       = 505,  //消息内容超过大小限制
    error_code_pin_not_allowed     = 506,  //无权取消该消息的Pin：群里只有Pin的操作者本人与群主/管理员可取消，别人取消他人钉的消息时返回本码

    //==== 文件 600-699 ====
    error_code_upload_fail         = 600,  //上传失败
    error_code_file_not_exist      = 601,  //文件不存在
    error_code_file_too_large      = 602,  //文件超过大小限制
    error_code_invalid_file_token  = 603,  //上传/下载凭证无效或过期
    error_code_storage_quota       = 604,  //存储空间不足
    error_code_md5_mismatch        = 605,  //文件 MD5 校验不一致

    //==== 音视频 700-799 ====
    error_code_call_fail           = 700,  //通话发起失败
    error_code_peer_busy           = 701,  //对方正在通话中
    error_code_peer_offline        = 702,  //对方不在线
    error_code_call_timeout        = 703,  //呼叫超时无应答
    error_code_call_rejected       = 704,  //对方拒绝通话
    error_code_call_not_exist      = 705,  //通话会话不存在（切换媒体时会话已结束/id 无效）
    error_code_call_not_member     = 706,  //请求者不是本次通话的双方之一，无权操作
    error_code_call_state_invalid  = 707,  //通话状态不允许该操作（如未接通就切换媒体）
};

//==================== 业务枚举 ====================

//用户类型，对应数据库 t_user.f_usertype
enum user_type
{
    user_type_normal  = 0,   //普通用户：真实自然人账号
    user_type_service = 1,   //服务号用户：系统通知/客服/公告等服务型账号
};

//客户端平台/设备类型，用于 cmd_login 的 clienttype 字段与多设备区分
enum platform_type
{
    platform_pc      = 1,   //Windows PC 客户端
    platform_android = 2,   //Android 客户端
    platform_ios     = 3,   //iOS 客户端
    platform_mac     = 4,   //macOS 客户端
    platform_web     = 5,   //Web 客户端
};

//用户在线状态，登录及状态变更时使用
enum presence_type
{
    presence_offline   = 0,   //离线
    presence_online    = 1,   //在线
    presence_away      = 2,   //离开
    presence_busy      = 3,   //勿扰（busy / Do-Not-Disturb）
    presence_invisible = 4,   //隐身（对外显示离线，实际在线）
};

//被踢下线原因，对应 cmd_kickout 推送包体的 reason 字段
enum kickout_reason
{
    kickout_reason_other_device = 1,   //账号在其他同类型设备登录，本连接被顶下线
    kickout_reason_disabled     = 2,   //账号被管理员停用
    kickout_reason_maintenance  = 3,   //服务器维护，主动断开
    kickout_reason_password_changed = 4,   //账号密码已修改，本账号其它设备需用新密码重新登录
    kickout_reason_kicked_by_user   = 5,   //本账号在另一设备上主动踢掉了本设备（cmd_kick_device）
};

//cmd_device_changed_notify 的 changetype 字段取值
enum device_change_type
{
    device_change_online  = 1,   //本账号某设备上线
    device_change_offline = 2,   //本账号某设备下线（登出 / 被踢 / 掉线）
};

//消息内容类型，位于消息体 JSON 的 msgtype 字段，决定 content 的结构
enum message_type
{
    message_type_text       = 1,    //纯文本
    message_type_image      = 2,    //图片
    message_type_file       = 3,    //文件
    message_type_voice      = 4,    //语音
    message_type_video      = 5,    //短视频
    message_type_emoji      = 6,    //大表情/动画表情
    message_type_location   = 7,    //地理位置
    message_type_card       = 8,    //名片（推荐联系人/群）
    message_type_screenshot = 9,    //截图（带标注，本质为图片子类）
    message_type_poll       = 10,   //群投票卡片
    message_type_relay      = 11,   //群接龙卡片
    message_type_todo       = 12,   //群待办卡片
    message_type_shake      = 13,   //窗口抖动
    message_type_redpacket  = 14,   //红包（设计稿预留，本期不实现）
    message_type_merged     = 15,   //合并转发（内容为子消息列表）
    message_type_rtc        = 16,   //通话记录（语音/视频通话结束后留下的记录条）
    message_type_system     = 17,   //系统提示（入群欢迎、撤回提示等，服务器生成）
};

//会话类型，位于会话/消息 JSON 的 ctype 字段
enum conversation_type
{
    conversation_single  = 1,   //单聊（对端为用户）
    conversation_group   = 2,   //群聊（对端为群）
    conversation_service = 3,   //服务号会话（系统通知/客服/公告等）
};

//文件收取状态，位于 cmd_file_transfer_ack 的 state 字段，也是 t_file_ack.f_state 的取值。
//由文件消息的接收方上报，服务端落库并转推发送方，使发送方知道对方到底收没收。
//状态只进不退：接收方重下时可由 2 退回 1 再到 2，但拒绝后又改主意接收也照常上报，
//服务端一律以最后一次上报为准，不做状态机校验（客户端不会乱发，且乱发也只是显示问题）。
enum file_ack_state
{
    file_ack_receiving = 1,   //对方点了「接收」、正在下载（大文件可能持续一段时间）
    file_ack_received  = 2,   //对方已完整收到并校验通过（下载成功后上报）
    file_ack_rejected  = 3,   //对方点了「拒绝」，不打算接收本文件
};

//文件用途场景，位于 cmd_request_upload 请求的 scene 字段。
//决定上传到 fileserver 还是 imgserver、以及大小上限。
enum file_scene
{
    file_scene_chat_file  = 1,   //聊天文件：单聊/群聊里发送的普通文件，走 fileserver
    file_scene_group_file = 2,   //群文件：登记进群文件库的文件，走 fileserver
    file_scene_avatar     = 3,   //头像：用户/群头像图片，走 imgserver
    file_scene_image_msg  = 4,   //图片消息：聊天里发送的图片/截图，走 imgserver
};

//群文件类别，位于 t_group_file.f_filetype 与 cmd_get_group_files 的 filetype 字段。
//由文件扩展名经 GroupFileProtocol::classifyFileType 算定，用于群文件列表按类型筛选。
enum group_file_type
{
    group_file_type_all      = 0,   //仅用于 cmd_get_group_files 请求：不筛选、返回全部类别
    group_file_type_document = 1,   //文档：doc/docx/xls/xlsx/ppt/pptx/pdf/txt 等
    group_file_type_image    = 2,   //图片：jpg/png/gif/bmp/webp 等
    group_file_type_video    = 3,   //视频：mp4/avi/mov/mkv 等
    group_file_type_audio    = 4,   //音频：mp3/wav/aac/flac 等
    group_file_type_archive  = 5,   //压缩包：zip/rar/7z/tar/gz 等
    group_file_type_other    = 6,   //其它：不属于以上任何类别的扩展名
};

//群文件变更通知 cmd_group_files_changed_notify 的 changetype 字段取值
enum group_file_change_type
{
    group_file_change_added   = 1,  //新增：某文件登记进群文件库（推送体携 file 详情）
    group_file_change_removed = 2,  //删除：某群文件被移除（推送体携 entryid）
};

//群成员角色
enum group_member_role
{
    group_role_member = 0,   //普通成员
    group_role_admin  = 1,   //管理员
    group_role_owner  = 2,   //群主
};

//群的加群方式，对应数据库 t_group.f_join_mode
enum group_join_mode
{
    group_join_free     = 0,   //自由加入，无需审批
    group_join_approval = 1,   //需群主 / 管理员审批
    group_join_forbid   = 2,   //禁止任何人加入
};

//谁能 @ 全体成员，对应数据库 t_group.f_at_all_scope。
//发言时若消息带 mentionall，服务端按本项校验发送者角色，越权以
//error_code_at_all_denied 拒发。
enum group_at_all_scope
{
    group_at_all_owner_admin = 0,   //仅群主和管理员可 @ 全体（默认）
    group_at_all_everyone    = 1,   //群内所有人都可 @ 全体
};

//加群申请的来源，对应数据库 t_group_join_request.f_source。
//供加群审批页显示"来源：xxx"一行，以及好友邀请时一并显示推荐人。
enum group_join_source
{
    group_join_source_friend = 0,   //好友邀请（此时 f_recommender 记邀请人userid）
    group_join_source_search = 1,   //群搜索（默认；老库里已有的申请都算这一类）
    group_join_source_qrcode = 2,   //扫码加群
};

//群公告的优先级，对应数据库 t_group_announcement.f_severity。
//目前只影响客户端公告卡上那枚徽章的文案与配色，不改变服务端行为
//（不因"紧急"而额外推送或置顶）。
enum group_announcement_severity
{
    group_announcement_normal   = 0,   //通知（默认）
    group_announcement_important = 1,  //重要
    group_announcement_urgent   = 2,   //紧急
};

//cmd_group_announcement_notify 的 changetype 字段取值
enum group_announcement_change_type
{
    group_announcement_change_publish = 1,   //有新公告发布，客户端据此弹窗提示
    group_announcement_change_delete  = 2,   //某条公告被删除，客户端据此从列表摘掉
};

//群敏感词库的操作类型，对应 cmd_operate_sensitive_word 的 operation 字段
enum sensitive_word_operation
{
    sensitive_word_add    = 1,   //新增一条敏感词（请求带 word）
    sensitive_word_delete = 2,   //删除一条敏感词（请求带 id）
};

//cmd_group_info_changed_notify 的 changetype 字段取值
enum group_info_change_type
{
    group_info_change_updated   = 1,   //群资料（名称 / 头像 / 公告等）被修改
    group_info_change_dismissed = 2,   //群已被解散
};

//cmd_group_member_changed_notify 的 changetype 字段取值
enum group_member_change_type
{
    group_member_change_join  = 1,   //成员加入
    group_member_change_leave = 2,   //成员主动退出
    group_member_change_kick  = 3,   //成员被移出
    group_member_change_role  = 4,   //成员角色变更（设 / 撤管理员、转让群主）
    group_member_change_muted = 5,   //成员禁言状态变更
    group_member_change_card  = 6,   //成员群名片变更
};

//部门操作类型，对应 cmd_operate_department 的 operationtype 字段
enum department_operation_type
{
    department_operation_create = 1,   //新建部门：请求带 parentdeptid、deptname，应答回传 deptid
    department_operation_update = 2,   //修改部门：改名称/描述/负责人/排序
    department_operation_delete = 3,   //删除部门：软删除（置为停用）
    department_operation_move   = 4,   //移动部门：改父部门，需校验不成环
};

//部门成员操作类型，对应 cmd_operate_dept_member 的 operationtype 字段
enum deptmember_operation_type
{
    deptmember_operation_add    = 1,   //添加成员：把 userid 挂到 deptid 下
    deptmember_operation_update = 2,   //修改成员：改职位/是否主部门/排序
    deptmember_operation_remove = 3,   //移除成员：把 userid 从 deptid 移出（软删除）
    deptmember_operation_move   = 4,   //移动成员：把 userid 从 deptid 移到 newdeptid
};

//部门状态，对应数据库 t_department.f_status
enum department_status
{
    department_status_normal   = 0,   //正常：在用、对客户端可见
    department_status_disabled = 1,   //停用：软删除，保留历史数据
};

//部门成员状态，对应数据库 t_department_member.f_status
enum deptmember_status
{
    deptmember_status_active  = 0,   //在职：有效的部门归属关系
    deptmember_status_removed = 1,   //已移出/离职：软删除，保留历史数据
};

//平台公告变更通知 cmd_announcement_notify 的 changetype 字段取值
enum announcement_change_type
{
    announcement_change_published = 1,   //新发布：管理员点了立即发布、或定时公告到点自动发布；推送体携完整公告内容
    announcement_change_recalled  = 2,   //已撤回：管理员撤回了这条公告；推送体只带 announcementid，客户端据此把它从消息盒子里移除
};

//平台公告的级别，对应数据库 t_announcement.f_severity。
//只决定消息盒子列表里那一条的强调色与图标，不影响投递逻辑。
enum announcement_severity
{
    announcement_severity_info    = 0,   //普通：一般性通知，列表里不做强调
    announcement_severity_notice  = 1,   //提醒：需要用户留意，用醒目色标注
    announcement_severity_warning = 2,   //警告：影响正常使用的重要通知（如停机维护），强调等级最高
};

//平台公告的受众类型，对应数据库 t_announcement.f_audience_type。
//决定一条已发布公告投给哪些 IM 用户，服务端 AnnouncementManager::getAnnouncementsForUser 据此做匹配。
enum announcement_audience_type
{
    announcement_audience_all_enterprise = 0,   //全部企业：投给全部 IM 自然人用户
    announcement_audience_enterprise     = 1,   //指定企业：投给 t_announcement_target 里列出的那些企业下的全部用户
    announcement_audience_department     = 2,   //指定部门：投给这些部门的在职成员；当前<u>不</u>向下级部门递归，见 docs/消息盒子接入方案.md 第 9 节
    announcement_audience_user           = 3,   //指定员工：只投给 t_announcement_target 里逐个列出的那些 userid
    announcement_audience_platform       = 4,   //平台内部：仅供运营在管理控制台内部查看，<u>一律不投</u>给任何 IM 用户
};
