/**
 *  FlamingoIM v2 通信协议定义：帧头、消息类型、错误码及各类枚举。
 *
 *  本文件是客户端与服务端 v2 协议的单一事实来源，是旧版 Msg.h（现已缩减改名为 AdminApiCodes.h）的后继。
 *  完整设计见 docs/协议设计-v2.md。v1 与 v2 协议不兼容，迁移期内本文件
 *  不得与 AdminApiCodes.h 在同一编译单元（.cpp）内同时包含，否则 error_code 枚举名冲突。
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
    frame_flag_encrypted      = 0x02,   //包体已用会话密钥加密（XChaCha20-Poly1305），
                                        //此时 bodylen = 加密前长度 + 17，originlen 仍为
                                        //最原始的明文长度。详见 docs/协议加密方案.md
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
 *       1800 文件图片 / 1900 收藏其它 / 2000 服务号 / 2100 平台公告 / 2200 文件共享 / 2300 P2P 连接。
 */
enum msg_type
{
    cmd_unknown = 0,                            //未知/无效消息类型，用于错误兜底

    //==== 会话与连接 1000-1099 ====
    cmd_handshake                = 1001,        //协议加密握手，连接建立后、任何业务帧之前的三帧交换，
                                                //帧本身为明文。版本协商仍在 cmd_login 里做
    cmd_heartbeat                = 1002,        //心跳保活，空包体，双向，默认 30 秒一次
    cmd_login                    = 1003,        //账号登录鉴权，请求携带客户端版本号
    cmd_logout                   = 1004,        //客户端主动登出
    cmd_resume                   = 1005,        //断线重连、恢复会话（后续阶段实现）
    cmd_kickout                  = 1006,        //推送：当前连接被踢下线（异地登录/账号停用/服务器维护）
    cmd_get_devices              = 1007,        //查询本账号当前在线设备列表
    cmd_kick_device              = 1008,        //踢掉本账号的指定在线设备
    cmd_device_changed_notify    = 1009,        //推送：本账号其他设备上线/下线
    //1010~1012 已在 docs/协议设计-v2.md 7.1 节预留给扫码登录（cmd_qrcode_get / cmd_qrcode_report /
    //cmd_qrcode_status_notify），代码里尚未定义，但不得挪作他用
    cmd_features_notify          = 1013,        //推送：本人所属企业的功能开关有变化，包体与登录应答里的 features 对象同构，
                                                //如 {"features":{"fileshare":1}}；只推给该企业的在线用户。
                                                //放在会话段而不是各功能自己的段，是因为功能模块未加载时客户端也要收得到

    //==== 账号与资料 1100-1199 ====
    cmd_register                 = 1101,        //注册新账号
    cmd_send_sms_code            = 1102,        //发送注册/找回密码用的短信验证码
    cmd_get_user_info            = 1103,        //获取指定用户的资料（自己或他人）
    cmd_update_profile           = 1104,        //更新本人资料（昵称/签名/性别等）
    cmd_change_password          = 1105,        //修改登录密码
    cmd_change_avatar            = 1106,        //修改头像
    cmd_change_presence          = 1107,        //修改在线状态（在线/离开/勿扰）
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
    //编号留空不复用。注意平台公告（公告中心）是 cmd_get_announcements（2101），与本条无关。
    cmd_set_favorite                   = 1215,  //添加/取消收藏（收藏对象为联系人或群组）
    //1216 原为 cmd_set_starred（添加 / 取消特别关注，对象只能是联系人，名单存 t_starred）。2026-10-03 起
    //特别关注并入会话的消息提醒模式（notify_mode_starred），改由 cmd_set_conversation / cmd_set_notify_modes
    //设置，t_starred 表一并删除；编号留空不复用。见 docs/消息提醒模式与消息盒子方案.md §5.5。
    cmd_set_contact_tags               = 1217,  //设置某个联系人的标签（形如"设计""外包"，落 t_contact_tag）。标签与备注名一样是<u>单向</u>的：只影响本人这一侧的显示，对方看不到也不受影响。请求体带的是该联系人在本人视角下的<u>完整</u>标签列表，服务端整份覆盖，空数组表示清空；增、改、删共用本条命令。读取不另设命令，标签随 cmd_get_contacts 应答的每个 contacts[] 元素一起下发

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
    //1321、1322 原为 cmd_get_sensitive_words（拉取本群敏感词库）与 cmd_operate_sensitive_word
    //（敏感词增删），随群级敏感词于 2026-10-02 删除，编号留空不得复用。敏感词改为企业级词库、
    //由管理端维护，见 docs/敏感词方案.md。
    cmd_get_group_announcements      = 1323,    //拉取本群公告列表（全体群成员可拉）
    cmd_publish_group_announcement   = 1324,    //发布 / 重新发布群公告（仅群主 / 管理员）
    cmd_delete_group_announcement    = 1325,    //删除群公告（仅群主 / 管理员，真删不可恢复）
    cmd_group_announcement_notify    = 1326,    //推送：群公告发布 / 被删除
    cmd_accept_group_invite          = 1327,    //被邀请人点入群邀请卡片上的「加入群聊」（「邀请入群需被邀请人同意」开启时；
                                                //按点击那一刻的情况直接入群或转为加群申请，见 docs/邀请入群方案.md §3.4、§4.4）
    cmd_group_invite_changed_notify  = 1328,    //推送：入群邀请卡片的状态变了，推给邀请人与被邀请人的全部在线设备（§4.5）
    cmd_join_request_handled_notify  = 1329,    //推送：一条加群申请已被处理（同意或拒绝），推给该群全体群主与管理员（含处理人本人的
                                                //其他设备），他们的待审批列表据此去掉这一条（docs/邀请入群方案.md 第 4 期）
    cmd_set_group_follow             = 1330,    //设置本人在某个群里关注的成员（整份覆盖，最多 20 人，只能是本群其他成员）：{groupid, targets:["uid",...]}；
                                                //群的提醒模式为 5 时这些成员发言会提醒（docs/消息提醒模式与消息盒子方案.md §12）
    cmd_group_follow_changed_notify  = 1331,    //推送：本人在某个群里关注的成员变了，推给本人全部在线设备（含发起端），包体同 1330 的请求
    cmd_set_must_read                = 1332,    //设置 / 取消入群必读（仅群主 / 管理员）：{groupid, announcementid}，announcementid 为 0 表示取消；
                                                //设定之后入群的新成员确认前不能发言（docs/群公告接入方案.md §14）
    cmd_confirm_must_read            = 1333,    //本人确认已阅读入群必读：{groupid}
    cmd_release_must_read            = 1334,    //群主 / 管理员代为确认入群必读：{groupid, userids:["uid",...]}
    cmd_get_must_read                = 1335,    //取本群的入群必读与本人是否待确认：{groupid} → {mustreadid, pending, announcement}
    cmd_get_group_qa                 = 1336,    //拉取本群问答（本群问答 + 应用到本群的企业问答，docs/群问答方案.md）
    cmd_save_group_qa                = 1337,    //新增 / 修改一条本群问答（仅群主 / 管理员）：{groupid, id, question, answer}，id 为 0 表示新增
    cmd_delete_group_qa              = 1338,    //删除一条本群问答（仅群主 / 管理员）：{groupid, id}
    cmd_group_qa_changed_notify      = 1339,    //推送：本群问答变了，推给全体群成员：{groupid}，打开着的群聊窗据此重拉
    cmd_create_group_link            = 1340,    //生成加群链接：{groupid, validdays}，validdays 取 1 / 7 / 0（0 表示永久）（docs/邀请入群方案.md §12）
    cmd_get_group_links              = 1341,    //拉取本群的有效加群链接：群主 / 管理员取全部，普通成员只取本人生成的
    cmd_revoke_group_link            = 1342,    //作废一条加群链接（群主 / 管理员，或生成者本人）：{groupid, id}
    cmd_get_group_link_info          = 1343,    //点加群链接后查群的样子：{code} → {groupid, name, facetype, customface, membercount, joinmode, member}
    cmd_join_group_by_link           = 1344,    //凭加群链接入群：{code} → {groupid, joined}，按群的加群方式直接入群或转为加群申请

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
    cmd_delete_message            = 1512,       //删除本端消息（本人视角软删除；成功后向本人全部在线设备推送 cmd_messages_deleted_notify）
    cmd_set_conversation          = 1513,       //会话置顶/消息提醒模式/删除（单个会话；模式取值见 enum conversation_notify_mode）
    cmd_conversation_changed_notify = 1514,     //推送：会话属性多端同步
    cmd_search_messages           = 1515,       //会话内消息记录搜索（筛选+翻页）
    cmd_mass_send                 = 1516,       //群发助手：一条消息发给多个联系人
    cmd_file_transfer_ack         = 1517,       //文件收取进度回执（双向）：接收方上报"开始接收/已接收/已拒绝"，服务端落库后转推发送方
    cmd_pin_message               = 1518,       //Pin / 取消 Pin 一条消息（会话内共享；pin 字段取 1 钉住、0 取消，一条命令兼管两个方向）
    cmd_message_pinned_notify     = 1519,       //推送：某条消息被 Pin / 取消 Pin。单聊推双方、群聊推全体成员，操作者本人也会收到
    cmd_get_pinned_messages       = 1520,       //拉取某会话被 Pin 的消息（带完整消息体，供客户端本地库缺行时补齐；单次最多返回 PIN_FETCH_LIMIT 条，应答的 total 带回真实总数）
    cmd_delete_messages_by_time   = 1521,       //按时间范围清理本人的聊天记录（本人视角软删除，作用于全部单聊与群聊，对方不受影响；服务端只记一条时间区间）
    cmd_messages_deleted_notify   = 1522,       //推送：本人删除了消息（单条形式带 ctype/cid/msgids，区间形式带 begintime/endtime，清空形式带 ctype/cid/clearedmsgid），发给本人全部在线设备，发起删除的设备也会收到
    cmd_clear_conversation        = 1523,       //按会话清空本人的聊天记录（本人视角，对方与其他群成员不受影响；服务端只记一个 msgid 锚点，成功后向本人全部在线设备推送 cmd_messages_deleted_notify 的清空形式）
    cmd_set_notify_modes          = 1524,       //批量设置多个会话的消息提醒模式（批量管理窗口用，单次最多 NOTIFY_MODE_BATCH_MAX 条）。逐条校验、逐条回结果码，合法的条目在一个事务里写入；成功后向本人全部在线设备推送 cmd_notify_modes_changed_notify。单个会话仍用 cmd_set_conversation（见 docs/消息提醒模式与消息盒子方案.md §5.4）
    cmd_notify_modes_changed_notify = 1525,     //推送：本人一批会话的消息提醒模式变了（只含改成功的条目），发给本人全部在线设备，发起批量设置的设备也会收到
    cmd_sync_rewritten_messages   = 1526,       //按改写序号拉取本人可见范围内「自某序号以来被改写过或被撤回的消息」，按序号升序分页；客户端登录后用它补上离线期间的卡片改写与撤回（docs/消息改写离线同步方案.md）

    //==== 群互动卡片 1600-1699 ====
    cmd_vote_poll            = 1601,            //对投票卡片投票
    cmd_poll_changed_notify  = 1602,            //推送：投票结果更新
    cmd_join_relay           = 1603,            //参与接龙
    cmd_relay_changed_notify = 1604,            //推送：接龙内容更新
    cmd_update_todo          = 1605,            //待办勾选/完成
    cmd_todo_changed_notify  = 1606,            //推送：待办状态更新
    cmd_withdraw_relay       = 1607,            //撤回本人在某张接龙卡片里的那一条；本人没参与过时直接回成功、不推送。成功后向卡片所在会话推送 cmd_relay_changed_notify（见 docs/群接龙方案.md §4.3）
    cmd_close_relay          = 1608,            //提前结束一张接龙：发起人、群主、管理员可操作，不受禁言限制；已结束或已截止时回 error_code_card_closed。成功后向卡片所在会话推送 cmd_relay_changed_notify（见 docs/群接龙方案.md §4.4）

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

    //==== 一对一通话卡片 1717（编号接在远程桌面之后：1701~1709 是一对一通话、1710~1716 是远程桌面）====
    cmd_call_card_notify   = 1717,              //推送：一对一通话卡片已改写成结束状态（结果、时长），推给主叫与被叫的全部在线设备

    //==== 屏幕共享 1720-1734（信令走 chatserver；画面、声音、标注走 imgserver 房间中转，见 docs/屏幕共享与录制方案.md）====
    //一次屏幕共享对应一个「房间」：发起人是唯一的屏幕画面发送方，会话里的其他人加入后成为观看者。
    //前缀用 screenshare_ 而不是 share_：cmd_share_* 2201~2230 已归文件共享。
    cmd_screenshare_start          = 1720,      //发起人发起共享：带 ctype / cid 与麦克风、摄像头、电脑声音初始状态；应答带 roomid
    cmd_screenshare_started_notify = 1721,      //推送：会话里有人开始共享（发给会话里除发起人以外的成员）
    cmd_screenshare_join           = 1722,      //观看者申请加入：单聊只允许对端，群聊须是群成员，人数满则拒绝
    cmd_screenshare_leave          = 1723,      //成员离开房间；发起人离开等同结束共享
    cmd_screenshare_stop           = 1724,      //发起人结束共享并解散房间
    cmd_screenshare_state_notify   = 1725,      //推送：房间状态变化（成员进出、媒体开关、权限、录制、发起人掉线 / 回来），附完整房间快照
    cmd_screenshare_relay_info     = 1726,      //推送：下发 imgserver 房间中转地址、relay 令牌与本人座位号（只发给本人）
    cmd_screenshare_query          = 1727,      //查询某会话当前有无进行中的共享（打开聊天窗时调用，用于显示横幅）
    cmd_screenshare_member_media   = 1728,      //成员上报麦克风、摄像头、电脑声音的开关状态；服务端记录后经 1725 广播
    cmd_screenshare_ended_notify   = 1729,      //推送：共享已结束（发给会话成员，带时长与结束原因）
    cmd_screenshare_set_permission = 1730,      //发起人设置标注权限、录制权限，或执行全员静音；服务端记录后经 1725 广播
    cmd_screenshare_record_state   = 1731,      //成员开始 / 停止录制：服务端检查录制权限，有权限则广播，无权限则拒绝
    cmd_screenshare_remove_member  = 1732,      //发起人移出某个观看者（第 14 期），可选是否允许再次加入；被移出的人收到 1729（原因 3 / 4），其余成员收到 1725 事件 10
    cmd_screenshare_set_info       = 1733,      //发起人修改共享标题与描述（第 15 期）：过敏感词，成功后改写会话里的共享卡片并推 1734
    cmd_screenshare_info_notify    = 1734,      //推送：共享标题与描述变了（第 15 期），发给会话成员与房间成员，带改写后的卡片内容
    cmd_screenshare_control        = 1735,      //控制键鼠（第 18 期）：观看者申请 / 撤回申请，画面主人授予 / 拒绝 / 收回，控制者结束控制；
                                                //申请与撤回经 1736 转给画面主人、拒绝经 1736 转给申请人，授予与收回经 1725 事件 11 / 12 广播
    cmd_screenshare_sharing        = 1737,      //多人共享（第 22 期）：观看者申请 / 撤回共享自己的屏幕，主持人同意 / 拒绝，停止共享（共享者停自己的，
                                                //主持人可停任何人的）；主持人停掉自己的画面后再申请时直接恢复
    cmd_screenshare_sharing_notify = 1738,      //推送（第 22 期）：共享申请、撤回转给主持人，同意、拒绝、被主持人停止转给当事人；同意时带
                                                //「只推流」的中转令牌与画面座位，被同意的人据此另连一条中转连接推自己的屏幕
    cmd_screenshare_transfer_host  = 1739,      //主持人把主持人身份移交给一位正在共享屏幕的人（第 22 期），之后原主持人离开房间
    //分享链接（第 25 期，docs/屏幕共享与录制方案.md 第 12.4 节）：号段 1740～1751 已归群通话，接在其后取 1752、1753
    cmd_screenshare_link           = 1752,      //主持人取得（没有就生成）、重置、停用分享链接，或设置「点链接加入免输密码」；应答带链接码
    cmd_screenshare_link_resolve   = 1753,      //已登录的人凭链接码查共享概要（标题、发起人、是否要密码、是否强制模式、进行中或已结束）
    //云端录制（第 26 期，docs/屏幕共享与录制方案.md 第 12.6 节）：接着分享链接取 1754～1757
    cmd_cloudrecord_control        = 1754,      //主持人开始、暂停、继续、停止共享中的云端录制；服务端经控制连接通知 imgserver，经 1725 事件 15 广播
    cmd_cloudrecord_list           = 1755,      //分页列出本人的云端录像（含状态），附本企业已用空间、配额与保留天数
    cmd_cloudrecord_op             = 1756,      //对一条云端录像操作：重命名、删除、取下载令牌、取上传令牌、登记合成完成、发到会话
    cmd_cloudrecord_notify         = 1757,      //推送（发给开启者）：云端录像状态变化（等待合成、可观看、出错）或即将到期
    cmd_screenshare_control_notify = 1736,      //推送（第 18 期）：控制申请、撤回申请转给画面主人，拒绝转给申请人，带动作、画面主人与发起动作的人

    //==== 群通话 1740-1751（信令走 chatserver；声音、摄像头画面复用 imgserver 房间中转 1859 / 1860，见 docs/群音视频通话方案.md）====
    //一场群通话对应一个「房间」：发起人选人振铃，其余群成员可从横幅自行加入；一个群同时只有一场，最后一个已加入的人离开时结束。
    cmd_groupcall_start            = 1740,      //发起群通话：带群号、媒体类型与被邀请人；应答带 roomid 与没振铃的忙线 / 离线名单，随后推 1746
    cmd_groupcall_invite_notify    = 1741,      //推送：被邀请加入群通话（振铃），带发起人、群名、剩余振铃毫秒数与房间快照
    cmd_groupcall_join             = 1742,      //接听邀请，或从横幅 / 卡片加入：须是群成员、不忙、人数未满；成功后推 1746
    cmd_groupcall_decline          = 1743,      //被邀请人拒绝：从房间里移除，房间里的人与本人的其它设备收到 1745
    cmd_groupcall_leave            = 1744,      //离开群通话；最后一个已加入的人离开时通话结束，群成员收到 active 为 0 的 1750
    cmd_groupcall_state_notify     = 1745,      //推送：房间状态变化（加入、离开、拒绝、振铃超时、追加邀请、媒体开关、掉线与回来、管理操作），附完整快照
    cmd_groupcall_relay_info       = 1746,      //推送：下发 imgserver 房间中转地址、relay 令牌与本人座位号（只发给本人这条连接）
    cmd_groupcall_invite_more      = 1747,      //通话中追加邀请：请求者须已加入，筛选规则与 1740 相同
    cmd_groupcall_member_media     = 1748,      //成员上报麦克风、摄像头开关；服务端记录后经 1745 广播，摄像头超限回 726
    cmd_groupcall_query            = 1749,      //查询某群当前有无进行中的群通话（打开群聊窗时调用，用于显示横幅与卡片状态）
    cmd_groupcall_status_notify    = 1750,      //推送：发给全体群成员，群通话开始、已加入人数变化、结束（结束时带改写后的通话卡片）
    cmd_groupcall_manage           = 1751,      //发起人、群主、管理员静音某人、全员静音或移出某人（第 6 期），结果经 1745 广播

    //==== 文件与图片 1800-1899 ====
    cmd_request_upload            = 1801,       //申请上传（含秒传判断，返回上传凭证）
    cmd_request_download          = 1802,       //申请下载（返回下载凭证）
    cmd_get_group_files           = 1803,       //群文件列表（分页+类型筛选）
    cmd_add_group_file            = 1804,       //将已上传文件登记为群文件
    cmd_delete_group_file         = 1805,       //删除群文件
    cmd_group_files_changed_notify = 1806,      //推送：群文件列表变更
    cmd_search_group_files        = 1807,       //按文件名搜索本人所在全部群的群文件（PC 主面板全局搜索「文件」标签用）

    //==== 单聊文件夹传输 1810-1817（信令走 chatserver；文件字节走 fileserver 中转）====
    //一整个目录的传输须先征得接收方同意，同意后双方各建 1 条控制通道 + N 条数据通道连到
    //fileserver，字节只在服务端内存里转发，<u>不落盘、不入库</u>；会话随任一端退出登录作废。
    //完整交互见 docs/协议设计-v2.md「文件夹传输协议（单聊）」一章与 docs/单聊文件夹传输方案.md。
    cmd_folder_invite        = 1810,            //发起文件夹传输：发送端→服务器，带目录名与规模统计，
                                                //应答回带本次会话 sessionid；规模超限时回带服务端配置的上限值
    cmd_folder_invite_notify = 1811,            //推送：接收端收到文件夹传输邀请（带发送端 userid / 昵称与规模统计）
    cmd_folder_response      = 1812,            //接收端应答邀请：accept 字段 1 接收 / 0 拒绝
    cmd_folder_cancel        = 1813,            //发送端在接收端应答前撤回邀请
    cmd_folder_close         = 1814,            //任一端结束本次传输：带成功 / 失败结果与失败原因错误码
    cmd_folder_state_notify  = 1815,            //推送：会话状态变更，取值见 enum folder_transfer_state
                                                //（响铃 / 传输中 / 拒绝 / 取消 / 对方离线 / 超时 / 失败 / 完成）
    cmd_folder_relay_info    = 1816,            //推送：接收端同意后下发给双方的中转参数（fileserver 地址 +
                                                //relay 令牌 + 角色 + 数据通道条数），双方据此建通道
    cmd_folder_retry         = 1817,            //失败会话在保留期内重试：服务端校验会话未过期、双方仍在线后
                                                //重签令牌并再下发一次 cmd_folder_relay_info，不再打扰接收方

    cmd_file_upload               = 1851,       //上传文件分片（仅 fileserver/imgserver 连接）
    cmd_file_download             = 1852,       //下载文件分片（仅 fileserver/imgserver 连接）
    cmd_rd_relay_hello            = 1853,       //远程桌面中转握手：连上 imgserver 后首帧报到，body 带 relay 令牌
    cmd_rd_relay_data             = 1854,       //远程桌面中转数据：屏幕帧 / 控制命令，imgserver 按会话配对原样转发、不解析
    cmd_av_relay_hello            = 1855,       //音视频中转握手：连上 imgserver 后首帧报到，body 带 relay 令牌（与 cmd_rd_relay_hello 同结构，imgserver 同样处理）
    cmd_av_relay_data             = 1856,       //音视频中转数据：编码后的音频 / 视频帧，imgserver 按会话配对原样转发、不解析（与 cmd_rd_relay_data 同处理）
    cmd_folder_relay_hello        = 1857,       //文件夹中转握手：连上 fileserver 后首帧报到，body 为 {token, lane}；
                                                //服务端验签、验角色，按 (sessionid, lane) 把两端配对。与远程桌面 /
                                                //音视频不同的是一次会话有多条通道，故握手帧多一个 lane 序号
    cmd_folder_relay_data         = 1858,       //文件夹中转数据：包体为通道内的应用帧（清单 / 分片 / 回执等），
                                                //fileserver 不解析包体，解密后用配对端的会话密钥重新加密转发
    cmd_screenshare_relay_hello   = 1859,       //屏幕共享房间中转握手：连上 imgserver 后首帧报到，body 带 relay 令牌；
                                                //令牌的 sessionid 为房间号、peeruid 承载座位号，imgserver 据此把连接挂进房间
    cmd_screenshare_relay_data    = 1860,       //屏幕共享房间中转数据：帧头为 [轨道类型][发送者座位号][标志][时间戳]，
                                                //imgserver 只读帧头决定丢帧，负载不解析，转发给房间里的其他所有成员

    cmd_p2p_relay_hello           = 1861,       //P2P 回落中转握手：打洞不通时连上 imgserver 后首帧报到，body 为 {token}；
                                                //令牌角色只能是 role_p2p_controlling / role_p2p_controlled，按 sessionid 两端配对
    cmd_p2p_relay_data            = 1862,       //P2P 回落中转数据：包体是两端之间端到端加密的 P2P 报文，imgserver 只做配对与转发，
                                                //既不解析也解不开（docs/P2P通信方案.md 6.4）
    cmd_cloudrec_ctl_hello        = 1863,       //云端录制控制连接的报到（只在 chatserver 与 imgserver 之间）：chatserver 连上 imgserver 的中转端口、
                                                //完成加密握手后首帧报到，body 为 {token}，令牌角色只能是 role_server_control（15）；
                                                //客户端永远不发也不收（屏幕共享与录制方案 12.6.2）
    cmd_cloudrec_ctl_data         = 1864,       //云端录制控制连接上的命令与事件（只在 chatserver 与 imgserver 之间），body 为 JSON，
                                                //op 区分开始 / 暂停 / 继续 / 停止录制、删除文件、录制结束、上传完成等，格式见 utils/CloudRecordCtlMsg.h

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
    //在 PC 主面板的「公告中心」里展示。注意与群公告(1323~1326)不是一回事：群公告
    //归属某个具体的群，平台公告归属整个平台，受众由 t_announcement.f_audience_type 决定。
    cmd_get_announcements      = 2101,          //拉取发给本人的平台公告：客户端登录后与打开公告中心时发，可带 sinceid 做增量拉取
    cmd_mark_announcement_read = 2102,          //上报某条平台公告已读：用户在公告中心里点开该条时发，服务端据此写 t_announcement_read
    cmd_announcement_notify    = 2103,          //推送：平台公告有变动，changetype 取 1 表示新发布、取 2 表示已被管理员撤回

    //==== 文件共享 2200-2299（信令走 chatserver；文件字节走 fileserver 中转）====
    //把本机的磁盘、文件夹或文件共享给单聊对方或指定的群成员，对方按需浏览、下载，经授权后还可写入。
    //共享记录持久保存，只在被主动取消时失效。段内分档：2201~2209 共享管理，2210~2219 在线提供，
    //2220~2229 访问与传输，2230~2239 审计。完整交互见 docs/文件共享方案.md 第 5 节。
    cmd_share_create              = 2201,       //发起共享：带 clientshareid、会话、类型、显示名与路径、成员与权限；
                                                //服务端建记录并生成卡片，应答回带 shareid；同一 clientshareid 重发返回同一个 shareid
    cmd_share_cancel              = 2202,       //取消共享：分享者本人，或群共享所在群的群主、管理员可以调用
    cmd_share_state_notify        = 2203,       //推送：某个共享对「收到这一帧的人」的状态变更（可用 / 暂不可用 / 已失效及原因）
    cmd_share_update_members      = 2204,       //调整成员：追加、移除成员或修改成员权限，仅分享者本人可以调用
    cmd_share_list                = 2205,       //拉取共享列表：scope 取 1 我的共享 / 2 共享给我的，可按会话筛选
    cmd_share_perms_notify        = 2206,       //推送：某成员在某共享上的权限变化，推给该成员本人与分享者在线的客户端
    cmd_share_serve_report        = 2210,       //分享者客户端报告各共享能否提供（登录后、共享根可用性变化时）；
                                                //服务端只接受来自共享所绑定设备的报告
    cmd_share_open                = 2220,       //访问者打开共享：校验通过后建访问会话，应答回带 accessid，随后向双方推 cmd_share_relay_info
    cmd_share_close_access        = 2221,       //任一端结束访问会话（关闭浏览窗口、空闲超时、共享被取消）
    cmd_share_relay_info          = 2222,       //推送：中转参数（fileserver 地址 + relay 令牌 + 角色 + 通道条数）；
                                                //transferid 为空表示访问会话本身，非空表示传输子会话
    cmd_share_access_state_notify = 2223,       //推送：访问会话或传输子会话的状态变更（对端断开、名额收回、成员被移除等）
    cmd_share_request_transfer    = 2224,       //访问者申请一次下载或上传：服务端校验权限与名额后建传输子会话，应答回带 transferid
    cmd_share_transfer_close      = 2225,       //任一端结束传输子会话，带成败结果与错误码
    cmd_share_audit_report        = 2230,       //分享者客户端批量上报各请求的执行结果，服务端补上服务端时间后写入审计表

    //==== P2P 连接 2300-2399（信令走 chatserver；数据走客户端之间的 UDP 直连，打不通时走 imgserver 中转）====
    //两个在线客户端之间的点对点链路，供文件夹传输、文件共享、一对一通话、远程桌面、单聊屏幕共享复用。
    //P2P 会话由服务端在业务会话建立后创建，推给双方正在进行该业务的那条连接；完整交互见 docs/P2P通信方案.md 第 5 节。
    cmd_p2p_register              = 2301,       //登录后 FlamingoP2P.dll 报到 P2P 协议版本，服务端据此把这条连接标记为具备 P2P 能力；
                                                //应答带本企业是否开启 P2P 与地址探测服务器列表
    cmd_p2p_session_notify        = 2302,       //推送：新建了一个 P2P 会话（会话号、本方角色、用途与业务会话号、对端 uid 与 deviceid、
                                                //回落中转参数、各项超时），只推给双方正在进行该业务的那条连接
    cmd_p2p_signal                = 2303,       //交换候选地址与临时公钥：服务端只核对发送者是该会话成员并检查长度，原样转给对端那条连接
    cmd_p2p_result                = 2304,       //上报建链结果（最终路径、耗时、NAT 判断），服务端只记日志与统计
    cmd_p2p_close                 = 2305,       //结束 P2P 会话，双向：客户端在业务结束或建链失败时发；服务端在一方掉线时推给另一方
    cmd_p2p_debug_open            = 2306,       //仅供联调：不经业务直接与指定用户建一个用途为 test 的会话；服务端配置 p2pdebugopen=1 时才受理

    //==== 备忘与提醒 2400-2499（docs/备忘与提醒方案.md 第 3 节）====
    //个人备忘归本人；群备忘只有群主、管理员能增删改，只有被提醒的成员与群主、管理员能看。提醒由客户端按本机时间计算
    cmd_memo_list                 = 2401,       //拉取本人可见的全部备忘；请求带 groupid 时只返回该群里本人可见的群备忘
    cmd_memo_save                 = 2402,       //新建（memoid 为 "0"）或修改一条备忘；应答回带服务端保存后的完整备忘
    cmd_memo_delete               = 2403,       //删除一条备忘（真删）；个人备忘只有本人可删，群备忘只有群主、管理员可删
    cmd_memo_notify               = 2404,       //推送：备忘新增、修改或删除，changetype 取值见 memo_change_type；
                                                //推给变更后能看到它的人，变更前能看到、变更后看不到的人收到删除

    //==== 管理控制台长连接 2500-2599（docs/FlamingoLink方案.md、协议文档第 22 节）====
    //只在 chatserver 的管理端口（adminlinklistenport）上收发，员工客户端的 20000 端口不受理。
    //控制台为 flamingoAdmin 的 FlamingoLink.dll；PC 客户端不收发这一段，两份协议头同步定义只为守住编号唯一
    cmd_adminlink_auth               = 2501,    //控制台用 HTTP 登录得到的 token 鉴权；鉴权前只放行 1001 握手、1002 心跳与本命令
    cmd_adminlink_subscribe_online   = 2502,    //订阅某企业的在线名单（每条连接同一时刻只订阅一个企业）；成功应答之后紧跟完整名单
    cmd_adminlink_unsubscribe_online = 2503,    //退订在线名单
    cmd_adminlink_online_notify      = 2504,    //推送：在线名单。订阅后的完整名单首帧 reset=1；之后推送每个用户全部在线设备的变化，
                                                //设备列表为空表示该用户已离线

    //==== 单聊密聊 2600-2699（docs/密聊方案.md 第 5 节）====
    //服务端只在内存里维护密聊会话，负责邀请、应答、超时与转发，不写数据库，日志里不打印本段的包体。
    //消息在客户端端到端加密，服务端转发的是它解不开的密文。客户端一侧由 FlamingoSecretChat.dll 收发本段
    cmd_secret_register           = 2601,       //登录后 FlamingoSecretChat.dll 报到协议版本与本机的防截图级别，服务端据此把这条连接标记为可密聊；
                                                //密聊中本机的防截图级别变化时再发一次，服务端经 2605（状态 6）转告对方
    cmd_secret_invite             = 2602,       //发起密聊：带对方 userid 与本次的临时公钥；应答回带 sessionid 与邀请剩余毫秒数
    cmd_secret_invite_notify      = 2603,       //推送：收到密聊邀请，发给被邀请人全部已报到的连接，带发起方的临时公钥与防截图级别
    cmd_secret_respond            = 2604,       //被邀请人同意（带临时公钥）或忽略邀请
    cmd_secret_state_notify       = 2605,       //推送：密聊会话状态变化，state 取值见 secret_state
    cmd_secret_cancel             = 2606,       //发起方在对方应答前撤回邀请
    cmd_secret_close              = 2607,       //任一方结束进行中的密聊（主动退出、关窗）；另一方收到 2605（已结束、对方退出）
    cmd_secret_message            = 2608,       //发送一帧密文（一条文字或一张图片的一块）；服务端只转给对方绑定的那条连接，不落库
    cmd_secret_message_notify     = 2609,       //推送：收到一帧密文

    //==== 帖子群 2700-2799（docs/帖子群方案.md 第 6 节）====
    //群类型为帖子群（group_type_thread）的群没有普通聊天：成员发帖、留言、回复、楼主补充、点赞与表情回应。
    //发帖、留言、回复、补充时服务端另在该群写一张卡片消息（message_type_thread_post / message_type_thread_reply），
    //借此复用会话列表、未读与提醒。客户端一侧由 FlamingoThreadGroup.dll 收发本段
    cmd_thread_list_posts         = 2701,       //分页拉取帖子摘要列表（按最新动态或最新发布排序，置顶在前）
    cmd_thread_get_post           = 2702,       //拉取一条帖子的完整内容（含全部补充与互动汇总）
    cmd_thread_publish_post       = 2703,       //发帖；按 clientpostid 去重
    cmd_thread_edit_post          = 2704,       //作者修改帖子的标题、正文与图片
    cmd_thread_delete_post        = 2705,       //删除帖子（作者、群主、管理员），连同它的补充、留言、回复与互动
    cmd_thread_pin_post           = 2706,       //置顶 / 取消置顶（群主、管理员，最多 3 条）
    cmd_thread_list_comments      = 2707,       //分页拉取某帖的留言，每条带最近几条回复与回复总数
    cmd_thread_list_replies       = 2708,       //分页拉取某条留言下的回复
    cmd_thread_add_comment        = 2709,       //留言（parentid 为 0）或回复某条留言
    cmd_thread_delete_comment     = 2710,       //删除留言或回复（作者、群主、管理员）
    cmd_thread_react              = 2711,       //对帖子、留言或回复点赞 / 加表情回应，或取消
    cmd_thread_get_reactors       = 2712,       //拉取对某个目标加了某种互动的用户
    cmd_thread_watch              = 2713,       //登记 / 撤销「正在查看」某个帖子群：留言与互动的实时推送只发给登记了的连接
    cmd_thread_add_addendum       = 2714,       //楼主在自己的帖子正文下追加一段补充
    cmd_thread_delete_addendum    = 2715,       //删除一段补充（楼主、群主、管理员）
    cmd_thread_check_cards        = 2716,       //核对本地存的帖子卡片与动态卡片：返回其中已改写为已删除形态的（每批最多 500 个）。客户端 2026-10-08 起不再发送，改由 cmd_sync_rewritten_messages 补同步；服务端仍保留处理，编号不得复用
    cmd_thread_changed_notify     = 2720,       //推送：帖子群的内容变化，changetype 取值见 thread_change_type
    cmd_thread_cards_notify       = 2721,       //推送：帖子卡片或动态卡片被改写（删帖、删留言、删补充、编辑帖子之后），推给该群全体在线成员

    //==== 机器人 2800-2899（docs/机器人方案.md 第 6 节）====
    //企业机器人、群机器人与个人机器人。判定与执行都在服务端；客户端 exe 处理 2801 / 2820（显示要用），
    //其余由 FlamingoBot.dll 经宿主收发
    cmd_get_group_bots            = 2801,       //拉取某个群可用的机器人（企业机器人与本群的群机器人），用于显示、@ 候选与名字解析
    cmd_get_bot_detail            = 2802,       //拉取一个机器人及其规则：群机器人只给群主，个人机器人只给主人
    cmd_save_group_bot            = 2803,       //群主新建或修改本群的群机器人（含规则）
    cmd_delete_group_bot          = 2804,       //群主删除本群的群机器人
    cmd_set_group_bot_off         = 2805,       //群主在本群关闭或打开某个企业机器人，或停用、启用本群的群机器人
    cmd_get_my_bots               = 2806,       //拉取本人的个人机器人（含规则）
    cmd_save_my_bot               = 2807,       //新建或修改本人的个人机器人
    cmd_delete_my_bot             = 2808,       //删除本人的个人机器人
    cmd_try_bot                   = 2809,       //「试一试」：按给定规则判定一段模拟输入，返回命中的规则与回复预览，不发消息
    cmd_get_auto_reply            = 2810,       //拉取本人的「离开 / 忙碌自动回复」设置（服务端执行，docs/机器人方案.md §11 Q2）
    cmd_set_auto_reply            = 2811,       //保存本人的「离开 / 忙碌自动回复」设置
    cmd_group_bots_changed_notify = 2820,       //推送：某个群可用的机器人有变化，打开着的群聊窗重拉 2801
};

//==================== 错误码 ====================

/**
 *  统一错误码，置于各应答 JSON 包体的 code 字段。0 为成功，其余按业务域分段：
 *  0-99 通用 / 100-199 账号 / 200-299 联系人 / 300-399 群组 /
 *  400-499 企业组织 / 500-599 消息 / 600-699 文件 / 700-799 音视频 / 800-899 文件共享 / 900-999 P2P 连接。
 *
 *  号段规约（完整版见 docs/协议设计-v2.md 第 8.1 节「错误码号段规约」）：
 *    - 新增错误码必须落在它所属功能段的区间内，并写在该段的末尾。错误码是一个
 *      扁平的、跨进程传输的命名空间，收到 308 无从分辨服务端指的是哪一种，段外
 *      取号既让排查的人按段去找却找不到，也更容易与别的段撞号；
 *    - 作废的错误码留空不复用，在原处留一行注释写明它原来是什么。存量客户端仍会
 *      按旧含义解读该编号，复用等于让它显示一条张冠李戴的提示。200 段已有多处先例；
 *    - 本枚举与客户端副本 flamingoclient/Source/net/Protocol.h 必须逐条同步，
 *      由 flamingoserver/tests/test_protocolparity.cpp（Suite PP）在测试里守住。
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
    error_code_profile_sensitive_word = 109, //资料内容包含敏感词，未保存：改资料（cmd_update_profile）的昵称、签名、
                                           //所在地、邮箱、岗位，或自助注册（cmd_register）的昵称命中提交者所属企业的词库
                                           //（2026-10-02 新增，见 docs/敏感词方案.md）

    //==== 联系人 200-299 ====
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
    error_code_starred_limit       = 210,  //特别关注的会话数已达上限（见 MessageProtocol.h 的 NOTIFY_MODE_STARRED_MAX，单聊与群聊合计）。特别关注的会话一来消息就弹出聊天窗，数量多了等于没关注；上限同时也防刷表。2026-10-03 之前只对联系人计数（原 cmd_set_starred）
    error_code_tag_limit           = 211,  //给单个联系人贴的标签条数超出上限（见 kMaxContactTagCount）。判定发生在<u>归一之后</u>：先去掉首尾空白、丢掉空串、不区分大小写去重，再数条数，所以提交里含重复标签不会白白占掉名额。单条标签文本超长归 error_code_bad_request，不占本码

    //==== 群组 300-399 ====
    error_code_create_group_fail   = 300,  //创建群失败
    error_code_group_not_exist     = 301,  //群不存在
    error_code_not_group_member    = 302,  //不是群成员
    error_code_not_group_admin     = 303,  //不是群主或管理员
    error_code_group_full          = 304,  //群成员已满
    error_code_already_in_group    = 305,  //已在群中
    error_code_member_muted        = 306,  //已被禁言，不能发言
    error_code_cannot_operate_owner = 307, //不能对群主执行该操作（移出 / 禁言 / 降级）
    error_code_join_req_not_exist  = 308,  //加群申请不存在或已处理
    error_code_at_all_denied       = 309,  //无权 @ 全体成员（群设为仅群主和管理员可 @）
    error_code_announcement_404    = 310,  //群公告不存在，或不属于本群（撤回 / 重新发布时）
    //311 原为 error_code_join_need_approval（加群需管理员审批），2026-08-16 作废，
    //号位留空不复用（规约见 docs/协议设计-v2.md 第 8.1 节）。
    //作废的理由是它从来没有下发路径：服务端「需审批」那条分支返回的是<u>成功应答</u>
    //加一个 joined=false 标志（ChatSession::onJoinGroupRequest 里的
    //buildJoinGroupResponse(0, "ok", false)），用它表达「申请已提交、等待审批」，
    //而不是返回一个错误码。这条码此前先占 307（与 error_code_cannot_operate_owner
    //撞号）、后挪到 311，两处都从未真正发出去过。
    //说明：本段原先还有 error_code_sensitive_word = 308（与 error_code_join_req_not_exist 撞号）。
    //它的语义是「消息被拒收」，本就属于消息段，2026-08-16 已改号并移到 500 段（现为 507）。
    //308 归 error_code_join_req_not_exist 独占。
    error_code_group_sensitive_word = 312, //群资料、群公告、群名片、加群验证消息、禁言原因或群备忘包含敏感词，未保存：
                                           //建群与改群资料的群名 / 简介 / 欢迎语 / 公告字段、发布群公告、设群名片、
                                           //需审批的加群申请、禁言原因命中提交者所属企业的词库
                                           //（2026-10-02 新增，见 docs/敏感词方案.md）
    error_code_group_invite_expired = 313, //入群邀请已超过有效期（发出后 7 天），点「加入群聊」时返回（docs/邀请入群方案.md §4.7）
    error_code_group_invite_invalid = 314, //入群邀请已失效：邀请不存在或不属于本人、已处理过、群已解散、邀请人已退出群聊
    error_code_must_read_pending   = 315,  //还没有确认入群必读，不能在本群发言（docs/群公告接入方案.md §14）
    error_code_group_link_invalid  = 316,  //加群链接不存在、已作废或已过期（docs/邀请入群方案.md §12）
    error_code_group_link_enterprise = 317, //加群链接只对生成者所在企业的同事有效
    error_code_group_qa_not_exist  = 318,  //群问答不存在，或不属于本群（修改 / 删除时，docs/群问答方案.md）
    error_code_group_qa_limit      = 319,  //本群问答已达上限，不能再新增

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
    error_code_register_closed     = 410,  //企业关闭了「允许用户自助注册」（register.enabled 为 0）：只能由管理员新建账号，客户端注册被拒绝
    //说明：企业"全称重名"没有在本表里占位。v2 客户端协议不含任何新建企业的命令，
    //建企业只走 admin 控制台的 HTTP 接口，那条链路用的是 AdminApiCodes.h 里的
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
    error_code_sensitive_word      = 507,  //消息内容包含敏感词，已拒绝发送（2026-10-02 起由群级词库
                                           //改为发送者所属企业的词库，见 docs/敏感词方案.md）。本码原先取 308，
                                           //既与 error_code_join_req_not_exist 撞号，又归错了段
                                           //（它是「发消息被拒」，属消息段而非群组段），
                                           //2026-08-16 一并订正。改号前客户端从未定义过本码，
                                           //也无人按它分支，故兼容风险最小
    error_code_card_closed         = 508,  //接龙已被提前结束或已过截止时间，不再接受参与、修改、撤回本人那条与再次结束
                                           //（cmd_join_relay / cmd_withdraw_relay / cmd_close_relay，见 docs/群接龙方案.md §4.7）
    error_code_card_full           = 509,  //接龙人数已满（每张最多 RELAY_MAX_PARTICIPANTS 条，见 CardProtocol.h），新人参与被拒；
                                           //已在名单里的人修改不受影响
    error_code_relay_deadline_invalid = 510,  //发起接龙时截止时间不合要求：没有设置（缺省、null 或 0），或早于服务端
                                           //当前时间 + RELAY_DEADLINE_MIN_LEAD_MS（5 分钟，见 CardProtocol.h）。
                                           //2026-10-03 起发起接龙必须设置截止时间；之前发出的不限截止时间的卡片不受影响

    //==== 文件 600-699 ====
    error_code_upload_fail         = 600,  //上传失败
    error_code_file_not_exist      = 601,  //文件不存在
    error_code_file_too_large      = 602,  //文件超过大小限制
    error_code_invalid_file_token  = 603,  //上传/下载凭证无效或过期
    error_code_storage_quota       = 604,  //存储空间不足
    error_code_md5_mismatch        = 605,  //文件 MD5 校验不一致
    //606-619 为单聊文件夹传输（cmd_folder_* 1810~1817）专用。这组码同时出现在两个地方：
    //信令应答的 code 字段，以及失败时经 cmd_folder_close / cmd_folder_state_notify 与中转
    //通道内 ft_abort 帧带给双方的 errcode，两条路径用同一张表，保证双方看到的是同一个原因。
    //上面 600-605 那几个是"走 fileserver 上传下载链路"的错误，与本组语义不同，不要混用：
    //例如 604 指服务端存储配额不足，而 612 指接收端本机磁盘写不下；605 指服务端校验上传文件
    //的 MD5 不符，而 615 指接收端校验收到的文件内容不符。
    error_code_folder_rejected       = 606, //对方拒绝接收本文件夹（接收端在气泡上点了「拒绝」）
    error_code_folder_peer_offline   = 607, //对方不在线：发起邀请时对方无在线连接，或传输中对方退出登录
    error_code_folder_invite_timeout = 608, //邀请在 folderinvitetimeout 毫秒内无应答，服务端自动结束会话
    error_code_folder_too_many_files = 609, //文件数超过服务端配置的 foldermaxfiles 上限，应答回带 maxfiles 供客户端提示
    error_code_folder_too_large      = 610, //总字节数超过服务端配置的 foldermaxsize 上限，应答回带 maxsize 供客户端提示
    error_code_folder_mkdir_fail     = 611, //接收端建目录失败（无写权限、目标只读、路径过长、含非法字符等），传字节之前即失败
    error_code_folder_disk_full      = 612, //接收端本机磁盘剩余空间不足以容纳整个文件夹
    error_code_folder_write_fail     = 613, //接收端写盘失败（磁盘 I/O 出错、文件被占用、中途被删等）
    error_code_folder_read_fail      = 614, //发送端读源文件失败（文件在扫描后被删 / 被移走 / 被独占打开）
    error_code_folder_verify_fail    = 615, //接收端校验收到的文件内容不符：字节数与清单不等，或 MD5 与发送端给的不一致
    error_code_folder_relay_broken   = 616, //中转通道断开：任一条 lane 断链即整个会话失败，本功能<u>不做</u>自动重连
    error_code_folder_canceled       = 617, //用户取消：任一端在传输过程中点了「取消」，双方气泡显示"已取消"
    error_code_folder_session_expired = 618,//会话已超出 folderretrywindow 保留期（默认 10 分钟），不能再重试续传，只能重新发起
    error_code_folder_server_busy    = 619, //服务器繁忙：整台服务器同时进行的文件夹传输会话数已达 foldermaxsessions 上限，拒绝新会话
    error_code_folder_busy           = 620, //发起方或接收方同时参与的文件夹传输笔数已达 foldermaxperuser 上限（默认 5 笔，
                                            //作发送方与作接收方各算一笔，失败待重试的会话在保留期内仍占名额）。
                                            //与 619 的区别：619 是整台服务器的会话总数到顶，本码是这两个人当中有一个名额已满；
                                            //两道闸门同时到顶时先报本码，因为它对用户更有指导性。
                                            //客户端本地已先拦一道，本码兜住绕过本地拦截直接发上来的请求
    error_code_file_sensitive_word   = 621, //文件名或文件夹名包含敏感词：申请上传（cmd_request_upload）聊天文件 / 群文件的
                                            //文件名，或单聊发文件夹（cmd_folder_invite）的文件夹名命中提交者所属企业的词库
                                            //（2026-10-02 新增，见 docs/敏感词方案.md）
    error_code_folder_direct_unavailable = 622, //单聊单文件直传（cmd_folder_invite 的 kind 为 1）不可用：双方之间建不了 P2P 链路
                                            //（对方只有手机端在线、对方是旧版客户端没报到用途 file、任一方企业关闭了 P2P），
                                            //或同意接收后 P2P 会话没有建成。发送端据此立即改为上传服务器的离线文件
                                            //（2026-10-04 新增，见 docs/单聊文件P2P直传与P2P徽章方案.md）

    //==== 音视频 700-799 ====
    error_code_call_fail           = 700,  //通话发起失败
    error_code_peer_busy           = 701,  //对方正在通话中
    error_code_peer_offline        = 702,  //对方不在线
    error_code_call_timeout        = 703,  //呼叫超时无应答
    error_code_call_rejected       = 704,  //对方拒绝通话
    error_code_call_not_exist      = 705,  //通话会话不存在（切换媒体时会话已结束/id 无效）
    error_code_call_not_member     = 706,  //请求者不是本次通话的双方之一，无权操作
    error_code_call_state_invalid  = 707,  //通话状态不允许该操作（如未接通就切换媒体）
    //以下 708~720 归屏幕共享（cmd_screenshare_* 1720~1734）
    error_code_screenshare_not_exist      = 708,  //屏幕共享房间不存在（已结束或 roomid 无效）
    error_code_screenshare_not_member     = 709,  //不是房间成员，或没有资格加入（单聊不是对端、群聊不是群成员）
    error_code_screenshare_exists         = 710,  //该会话已有进行中的屏幕共享，同一会话同时只能一人共享
    error_code_screenshare_room_full      = 711,  //房间人数已达上限（服务端配置项，默认 50，含发起人）
    error_code_screenshare_in_call        = 712,  //正在一对一通话中，只能与已接通的通话对方在单聊里发起或加入屏幕共享（第 8 期：通话中共享屏幕）；在共享房间里不能发起通话
    error_code_screenshare_no_record_perm = 713,  //发起人已收回录制权限，观看者不能开始录制
    error_code_screenshare_in_other_room  = 714,  //已在另一个屏幕共享房间里，须先离开才能发起或加入新的
    error_code_screenshare_camera_limit   = 715,  //房间里同时开摄像头的人数已达上限（服务端配置项，默认 9）
    error_code_screenshare_not_host       = 716,  //只有发起人能执行该操作（结束共享、设置权限、全员静音）
    error_code_screenshare_bad_password   = 717,  //共享设了密码，加入时没带或带错（第 12 期；发起人、已在房间里的人、通话对方免校验）
    error_code_screenshare_forced_leave   = 718,  //强制模式的共享里观看者不能离开，要等发起人结束（第 13 期）
    error_code_screenshare_removed        = 719,  //已被发起人移出这场共享，且发起人没有允许再次加入（第 14 期）
    error_code_screenshare_sensitive      = 720,  //共享标题或描述包含敏感词，未发起 / 未修改（第 15 期）
    //729、730 归屏幕共享（第 18 期控制键鼠）；721~728 已由群通话占用
    error_code_screenshare_not_sharing    = 729,  //要控制的那块画面不存在：对方不在共享屏幕，或画面主人暂时掉线（第 18 期）
    error_code_screenshare_sharers_full   = 731,  //同时共享屏幕的人数已达上限（第 22 期；服务端配置项，默认 4，含主持人）
    error_code_screenshare_no_request     = 732,  //对方没有待处理的共享申请（第 22 期：主持人同意 / 拒绝时申请已撤回或已处理）
    error_code_screenshare_link_invalid   = 733,  //分享链接无效或已过期（第 25 期）：链接码格式不对、已被重置或停用、共享结束已超过 24 小时
    error_code_screenshare_link_enterprise = 734, //凭分享链接加入时与发起人不在同一企业（第 25 期）
    error_code_screenshare_link_too_frequent = 735, //解析分享链接过于频繁（第 25 期，每人每分钟最多 30 次）
    //736～739 归云端录制（第 26 期）；配额超出沿用 error_code_storage_quota（604）
    error_code_cloudrecord_disabled       = 736,  //本企业没有开通云端录制（功能开关 cloudrecord，默认关闭）
    error_code_cloudrecord_busy           = 737,  //这场共享已经在云端录制；或要暂停 / 继续 / 停止的录制不在进行中
    error_code_cloudrecord_unavailable    = 738,  //云端存储服务不可用：chatserver 与 imgserver 的控制连接断开，或 imgserver 拒绝开始（磁盘空间不足等）
    error_code_cloudrecord_not_found      = 739,  //云端录像不存在，或本人无权访问（不是开启者，录像也没有发到本人所在的会话）
    error_code_screenshare_not_owner      = 730,  //只有画面主人能授予、拒绝控制；只有画面主人或正在控制的人能收回（第 18 期）
    //以下 721~728 归群通话（cmd_groupcall_* 1740~1751，见 docs/协议设计-v2.md 第 21 节）
    error_code_groupcall_not_exist        = 721,  //群通话不存在（已结束或 roomid 无效）
    error_code_groupcall_exists           = 722,  //该群已有进行中的群通话；应答带那一场的 roomid，客户端改为加入
    error_code_groupcall_busy             = 723,  //本人正在别的通话、屏幕共享或群通话里（含响铃、振铃中）；在群通话里发起一对一通话、屏幕共享也回本码
    error_code_groupcall_room_full        = 724,  //人数已达上限（振铃中与已加入合计，服务端配置项，默认 16）
    error_code_groupcall_not_member       = 725,  //不是群成员，或不在这场群通话里（拒绝时没在振铃、离开 / 追加邀请 / 上报媒体时没加入）
    error_code_groupcall_camera_limit     = 726,  //同时开摄像头的人数已达上限（服务端配置项，默认 9）
    error_code_groupcall_disabled         = 727,  //本企业关闭了群通话功能（功能开关 groupcall）
    error_code_groupcall_no_permission    = 728,  //只有发起人、群主、管理员能静音或移出别人、开启或解除全员静音（第 6 期）；全员静音期间其他人开麦克风也回本码

    //==== 文件共享 800-899 ====
    //cmd_share_* 2201~2230 的应答、cmd_share_transfer_close 与传输子会话里 ft_abort 帧带的 errcode
    //共用这一张表。注意与 600 段文件夹传输的区别：那一组描述「把一个目录推给对方」的失败，
    //本组描述「访问别人共享出来的目录」的失败，例如 812 与 612 都是磁盘写不下，但发生的场景不同。
    error_code_share_disabled            = 800, //本企业的文件共享功能已被管理员关闭
    error_code_share_not_exist           = 801, //共享不存在（shareid 无效）
    error_code_share_canceled            = 802, //共享已被取消
    error_code_share_not_member          = 803, //不是该共享的成员，或已被移出成员
    error_code_share_unavailable         = 804, //分享者不在线，或不在共享所绑定的那台电脑上登录
    error_code_share_root_missing        = 805, //共享根在分享者电脑上已不存在（U 盘被拔出、目录被删除或改名）
    error_code_share_permission_denied   = 806, //没有这项操作的权限（上传、新建、重命名、删除、移动之一）
    error_code_share_name_conflict       = 807, //目标位置已有同名的文件或文件夹（上传与新建一律不覆盖）
    error_code_share_path_invalid        = 808, //路径非法或越出共享范围（含 ..、绝对路径、经重解析点指向共享根之外等）
    error_code_share_path_not_found      = 809, //文件或目录不存在；共享未勾选包含隐藏文件时，隐藏条目也按不存在处理
    error_code_share_recycle_unavailable = 810, //文件无法放入分享者电脑的回收站，已取消删除，不做彻底删除
    error_code_share_io_fail             = 811, //分享者电脑上读写失败（文件被占用、无系统权限等）
    error_code_share_disk_full           = 812, //接收方磁盘空间不足（上传时为分享者一端，下载时为访问者一端）
    error_code_share_transfer_busy       = 813, //该共享同时传输的人数已达 sharemaxtransferpershare 上限
    error_code_share_server_busy         = 814, //整台服务器的访问会话或传输子会话总数已达上限（sharemaxaccess / sharemaxtransfers）
    error_code_share_too_many            = 815, //本人同时持有的共享数已达 sharemaxpershareowner 上限
    error_code_share_owner_timeout       = 816, //分享者在 shareopentimeout 内没有在中转通道上响应
    error_code_share_relay_broken        = 817, //中转通道断开，本次访问或传输结束，不做自动重连
    error_code_share_transfer_canceled   = 818, //用户取消了传输
    error_code_share_invalid_member      = 819, //指定的成员不属于这个会话（单聊不是对方、群聊不是群成员）
    error_code_share_kind_mismatch       = 820, //操作与共享类型不符（例如向单文件共享上传或新建文件夹）
    error_code_share_cross_enterprise    = 821, //成员与分享者不属于同一个企业，或分享者不属于任何企业；文件共享禁止跨企业
    error_code_share_name_sensitive      = 822, //共享名称包含敏感词，未发起（2201；按分享者所属企业的词库检查，2026-10-04 起）

    //==== P2P 连接 900-999 ====
    //cmd_p2p_* 2301~2306 的应答与 cmd_p2p_close 带的 errcode 共用这一张表。业务路径上企业关闭了 P2P 时，
    //服务端不建 P2P 会话、业务照旧走原来的中转，不返回错误；900 只出现在 2301 与 2306 的应答里。
    error_code_p2p_disabled              = 900, //本企业的 P2P 已被管理员关闭
    error_code_p2p_session_not_exist     = 901, //P2P 会话不存在或已结束（会话号无效）
    error_code_p2p_not_member            = 902, //发送者不是该 P2P 会话的成员，或不是会话绑定的那条连接
    error_code_p2p_peer_offline          = 903, //对端那条连接已断开
    error_code_p2p_too_many              = 904, //本人同时进行的 P2P 会话数已达 p2pmaxperuser 上限
    error_code_p2p_signal_too_large      = 905, //cmd_p2p_signal 的内容超过服务端允许的长度
    error_code_p2p_version_unsupported   = 906, //客户端报到的 P2P 协议版本服务端不支持
    error_code_p2p_debug_disabled        = 907, //服务端未开启联调命令 cmd_p2p_debug_open（p2pdebugopen=0）
    error_code_p2p_purpose_unsupported   = 908, //有一方的客户端没有在 2301 里报到这个用途（该版本还不支持这项业务走 P2P，或本机关闭了它）；
                                            //只出现在服务端日志里：业务照旧走原来的中转，不建会话、不返回给客户端

    //==== 备忘与提醒 1000-1099 ====
    error_code_memo_not_exist            = 1000, //备忘不存在、已删除，或不是本人可操作的那一条
    error_code_memo_invalid              = 1001, //备忘字段不合法：标题、备注长度，开始时间，提前分钟，重复规则，次数，提醒方式
    error_code_memo_too_many             = 1002, //备忘数量已达上限（每人个人备忘 500 条、每群群备忘 200 条）
    error_code_memo_target_invalid       = 1003, //群备忘指定的提醒对象为空，或其中有人不是本群成员

    //==== 管理控制台长连接 1100-1199 ====
    //只出现在管理端口 2501 / 2502 的应答里（docs/FlamingoLink方案.md 第 6.5 节）
    error_code_adminlink_token_invalid   = 1100, //token 无效、已过期、已注销，或管理员已被停用；控制台据此回到登录页、不再重连
    error_code_adminlink_forbidden       = 1101, //企业管理员订阅了别的企业的在线名单
    error_code_adminlink_enterprise_not_exist = 1102, //订阅的企业不存在

    //==== 单聊密聊 1200-1299（docs/密聊方案.md 5.3）====
    error_code_secret_disabled            = 1200, //本企业或对方企业关闭了单聊密聊
    error_code_secret_peer_unavailable    = 1201, //对方不在线，或没有可密聊（已报到）的客户端
    error_code_secret_not_exist           = 1202, //密聊会话不存在或已结束（sessionid 无效、邀请已超时或已取消）
    error_code_secret_busy                = 1203, //与对方已有待应答或进行中的密聊
    error_code_secret_not_member          = 1204, //不是该会话的成员，或不是会话绑定的那条连接
    error_code_secret_too_large           = 1205, //单帧超过服务端允许的长度（配置项 secretmaxframebytes）
    error_code_secret_too_many            = 1206, //发起过于频繁、同时进行的密聊数达到上限，或单个会话转发过快
    error_code_secret_blocked             = 1207, //已被对方加入黑名单
    error_code_secret_version_unsupported = 1208, //客户端报到的密聊协议版本服务端不支持

    //==== 帖子群 1300-1399（docs/帖子群方案.md 6.4）====
    error_code_thread_disabled            = 1300, //本企业关闭了帖子群
    error_code_thread_not_exist           = 1301, //帖子、补充、留言或回复不存在（已被删除），或不在本人所在的群里
    error_code_thread_no_permission       = 1302, //没有权限：修改别人的帖子、非管理员删除别人的内容或置顶、非楼主追加补充
    error_code_thread_invalid             = 1303, //字段不合法：长度、图片数、表情编号、空内容
    error_code_thread_pin_full            = 1304, //置顶已满 3 条
    error_code_thread_not_thread_group    = 1305, //目标群不是帖子群，或向帖子群发送普通聊天消息
    error_code_thread_addendum_full       = 1306, //这个帖子的补充已满 20 段

    //==== 机器人 1400-1499（docs/机器人方案.md 6.1）====
    error_code_bot_not_exist              = 1401, //机器人不存在、已删除，或不属于这个群 / 这个人
    error_code_bot_no_permission          = 1402, //没有权限：不是群主、不是机器人的主人，或企业机器人不允许在本群关闭
    error_code_bot_full                   = 1403, //数量超出上限：每群 5 个群机器人、每人 3 个个人机器人、每个机器人 50 条规则
    error_code_bot_invalid                = 1404, //机器人或规则不合法：名称为空或超长、关键词为空、回复为空、时间格式错误、触发方式与动作不匹配等
    error_code_bot_disabled               = 1405, //本企业关闭了机器人，或关闭了群机器人 / 个人机器人 / 群机器人的外部服务
    error_code_bot_sensitive_word         = 1406, //名称、简介或回复文字含企业敏感词
};

//==================== 业务枚举 ====================

//用户类型，对应数据库 t_user.f_usertype
enum user_type
{
    user_type_normal  = 0,   //普通用户：真实自然人账号
    user_type_service = 1,   //服务号用户：系统通知/客服/公告等服务型账号
    user_type_bot     = 2,   //机器人：每个企业机器人、群机器人对应一个这样的账号，不能登录；个人机器人以主人的身份代答，不建账号（BotManager::createBot，docs/机器人方案.md §3.2）
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

//用户在线状态，登录及状态变更时使用。
//取值 4 曾经是「隐身」，本产品不提供该功能，已于 2026-08-13 移除；
//该编号就此作废，不得再用于其它状态 —— 老客户端仍可能发上来 4，
//服务端与新客户端一律按越界值处理（归到离线），不要为它恢复语义。
enum presence_type
{
    presence_offline   = 0,   //离线
    presence_online    = 1,   //在线
    presence_away      = 2,   //离开
    presence_busy      = 3,   //勿扰（busy / Do-Not-Disturb）
};

//用户资料的性别取值，位于资料 JSON（登录应答、通讯录列表、1103 / 1108）与改资料请求的 gender 字段。
//2026-10-04 统一（docs/联系人资料字段扩充方案.md）：与管理后台一致；此前 PC 客户端按 0 男 / 1 女写入，
//那些历史值不做迁移，由本人重新选择
enum profile_gender
{
    profile_gender_unset  = 0,   //未设置：没有填写或选择不公开，界面显示「保密」
    profile_gender_male   = 1,   //男
    profile_gender_female = 2,   //女
};

//手机号、座机对同事的公开范围，位于登录应答、本人的 1103 应答、推给本人的 1108 与改资料请求的
//phonevisible / landlinevisible 字段（只发给本人）。2026-10-06 起（docs/联系人资料字段扩充方案.md 第 9 节）。
//不公开时服务端发给同事的资料里该号码为空串；默认手机号不公开、座机公开
enum profile_visibility
{
    profile_visibility_self_only  = 0,   //仅本人可见：同事拿到的资料里这个号码为空串
    profile_visibility_colleagues = 1,   //对同事可见：同事拿到的资料里带这个号码
};

//被踢下线原因，对应 cmd_kickout 推送包体的 reason 字段
enum kickout_reason
{
    kickout_reason_other_device = 1,   //账号在其他同类型设备登录，本连接被顶下线
    kickout_reason_disabled     = 2,   //账号被管理员停用
    kickout_reason_maintenance  = 3,   //服务器维护，主动断开
    kickout_reason_password_changed = 4,   //账号密码已修改，本账号其它设备需用新密码重新登录
    kickout_reason_kicked_by_user   = 5,   //本账号在另一设备上主动踢掉了本设备（cmd_kick_device）
    kickout_reason_enterprise_disabled = 6,//所属企业被平台管理员停用，该企业全员下线；企业恢复为活跃后可再登录
    kickout_reason_enterprise_deleted  = 7,//所属企业被平台管理员删除，该企业全员下线；账号虽保留但已无企业归属，无法再登录
    kickout_reason_account_deleted     = 8,//账号被管理员删除，不可恢复；与 kickout_reason_disabled（停用，可恢复）区分
    kickout_reason_kicked_by_admin     = 9,//设备被管理员在控制台强制注销；与 kickout_reason_kicked_by_user（本人在别的设备上踢的）区分
    kickout_reason_password_reset_by_admin = 10,//管理员在控制台重置了本账号的密码，全部设备下线，须用管理员给的新密码重新登录（2026-10-07 起）
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
    message_type_rtc        = 16,   //通话记录：群通话卡片（content 的 kind 为 groupcall，发起群通话时由服务器写入群聊、结束时改写）与一对一通话卡片（kind 为 call，呼叫时由服务器写入两人的单聊、结束时改写），客户端不得自行发送
    message_type_system     = 17,   //系统提示（入群欢迎、撤回提示等，服务器生成）
    message_type_folder     = 18,   //文件夹传输记录条（一次单聊文件夹传输结束后留下的记录，服务器生成）：
                                    //content 只记这次传输的动作与结局（会话 id、文件夹名、目录数 / 文件数 /
                                    //总字节数、终态与失败原因），<u>不记</u>文件清单、路径与文件内容，
                                    //故历史消息里也不提供重新下载。字段表见 docs/协议设计-v2.md 12.8
    message_type_screenshare = 19,  //屏幕共享卡片（发起共享时由服务器生成，客户端不得自行发送）：content 带 roomid、
                                    //发起人与开始时间；卡片上的「加入」按钮在共享结束后置灰。字段见 docs/协议设计-v2.md
    message_type_fileshare  = 20,   //文件共享卡片（发起共享或追加成员时由服务器生成，客户端不得自行发送）：
                                    //content 只记创建后不变的信息（shareid、类型、显示名与路径、分享者），
                                    //状态 / 原因 / 本人权限由服务端按「看的人」在下发时合并进去。
                                    //群聊里只有分享者与被指定的成员可见。字段见 docs/文件共享方案.md 5.6
    message_type_group_invite = 21, //入群邀请卡片（「邀请入群需被邀请人同意」开启时由服务器在邀请人与被邀请人的单聊里生成，
                                    //客户端不得自行发送，也不提供撤回、转发、置顶）：content 记邀请 id、群、邀请人、被邀请人、
                                    //到期时间，状态与群的当前资料由服务端在下发时合并进去。字段见 docs/邀请入群方案.md §4.3
    message_type_announcement = 22, //群公告卡片（发布或重新发布群公告时由服务器以发布人的名义写入群聊，客户端不得自行发送，
                                    //也不提供撤回、转发、置顶）：content 记公告 id、标题、正文、优先级、发布人与时间；
                                    //公告被删除时服务器把卡片改写为已删除形态（去掉正文、带 deleted）。字段见 docs/群公告接入方案.md §13.2
    message_type_memo       = 23,   //群备忘卡片（新建或修改群备忘时由服务器以操作人的名义写入群聊，客户端不得自行发送，
                                    //也不提供撤回、转发、置顶）：只有该备忘的可见者（被提醒的成员与群主、管理员）看得到；
                                    //content 是保存那一刻的备忘快照，之后不改写。字段见 docs/备忘与提醒方案.md §7.2
    message_type_cloudrecord = 24,  //云端录像卡片（开启者经 cmd_cloudrecord_op 发到会话时由服务器以他的名义写入，客户端不得自行发送）：
                                    //content 带 recordid、owner、title、durationms、bytes、audioonly；会话成员凭卡片取下载令牌观看
                                    //（docs/屏幕共享与录制方案.md 12.6.5）
    message_type_thread_post = 25,  //帖子卡片（帖子群里发帖后由服务器以作者的名义写入，客户端不得自行发送，也不提供撤回、转发、置顶）：
                                    //content 带 postid、title、digest、imagecount；删帖时改写为已删除形态（带 deleted）。
                                    //全员可见、对作者以外的人计未读，用于会话列表摘要与提醒。字段见 docs/帖子群方案.md 6.2
    message_type_thread_reply = 26, //帖子动态卡片（留言、回复、楼主补充后由服务器以动作发起人的名义写入，客户端不得自行发送）：
                                    //只对相关的人可见（楼主、被回复的人、被 @ 的人；补充时为在该帖下留过言或回复过的人）；
                                    //content 带 postid、commentid、addid、posttitle、digest、kind（取值见 thread_reply_kind）。
                                    //字段见 docs/帖子群方案.md 6.2
};

//群类型，位于群对象（1303 / 1320 / 1318）与建群请求（1301）的 grouptype 字段，落 t_group.f_group_type
enum group_type
{
    group_type_normal = 0,   //普通群：有聊天消息流。建群请求不带 grouptype 时取此值
    group_type_thread = 1,   //帖子群：没有普通聊天，只有帖子、补充、留言、回复与互动（docs/帖子群方案.md）
};

//帖子动态卡片（message_type_thread_reply）content 的 kind 字段
enum thread_reply_kind
{
    thread_reply_comment  = 1,   //有人在帖子下留言：提醒楼主
    thread_reply_reply    = 2,   //有人回复了某条留言：提醒楼主与被回复的人
    thread_reply_addendum = 3,   //楼主追加了补充：提醒在该帖下留过言或回复过的人
};

//帖子群互动的目标类型，位于 2711 / 2712 的 targettype 字段，落 t_thread_reaction.f_target_type
enum thread_target_type
{
    thread_target_post    = 1,   //帖子本身（补充不单独互动，算在帖子上）
    thread_target_comment = 2,   //留言或回复
};

//cmd_thread_changed_notify 的 changetype 字段
enum thread_change_type
{
    thread_change_post_new        = 1,  //有人发了新帖：推给该群全体在线成员
    thread_change_post_edit       = 2,  //帖子被作者修改：推给全体在线成员
    thread_change_post_delete     = 3,  //帖子被删除：推给全体在线成员
    thread_change_post_pin        = 4,  //帖子被置顶或取消置顶：推给全体在线成员
    thread_change_addendum_new    = 5,  //楼主追加了补充：推给全体在线成员
    thread_change_addendum_delete = 6,  //补充被删除：推给全体在线成员
    thread_change_comment_new     = 7,  //新的留言或回复：只推给登记了正在查看该群的连接
    thread_change_comment_delete  = 8,  //留言或回复被删除：只推给登记了正在查看该群的连接
    thread_change_reaction        = 9,  //某个目标的互动汇总变了：只推给登记了正在查看该群的连接
};

//帖子列表的排序方式，位于 2701 的 sort 字段
enum thread_sort
{
    thread_sort_activity = 1,   //最新动态：按最近一次发帖、留言、回复或补充的时间倒序（缺省）
    thread_sort_publish  = 2,   //最新发布：按发帖时间倒序
};

//密聊会话状态，位于 cmd_secret_state_notify 的 state 字段（docs/密聊方案.md 5.2）
enum secret_state
{
    secret_state_active            = 1,  //已建立：双方可以收发
    secret_state_not_accepted      = 2,  //未接受：被邀请人忽略或邀请超时，发给发起方时不区分两者
    secret_state_canceled          = 3,  //已取消：发起方撤回了邀请，发给被邀请人
    secret_state_ended             = 4,  //已结束：会话在服务端结束，原因见 secret_end_reason；收到的一方转为「只能查看」
    secret_state_handled_elsewhere = 5,  //已在你的其他设备上处理：只发给被邀请人其余已报到的连接
    secret_state_peer_protect      = 6,  //对方的防截图级别变了，会话状态不变，peerprotect 带新级别
};

//密聊结束原因，位于 cmd_secret_state_notify（state 为 secret_state_ended）的 reason 字段
enum secret_end_reason
{
    secret_end_peer_left    = 1,  //对方主动退出（点「退出密聊」、关闭窗口、登出）
    secret_end_peer_offline = 2,  //对方离线（连接断开）
    secret_end_disabled     = 3,  //管理员关闭了单聊密聊
    secret_end_maintenance  = 4,  //服务器维护
    secret_end_protocol     = 5,  //协议错误（序号异常、帧超长等），服务端强制结束
};

//本机对一个窗口能做到的防截图级别，位于 2601 的 protect 与 2603 / 2605 的 peerprotect 字段
enum secret_protect_level
{
    secret_protect_none    = 0,  //设置失败（如 Win7 关闭了桌面合成）：仍可密聊，但双方都会看到提示
    secret_protect_monitor = 1,  //WDA_MONITOR：截图里这个窗口显示为一块黑
    secret_protect_exclude = 2,  //WDA_EXCLUDEFROMCAPTURE：截图里不出现这个窗口
};

//群备忘卡片（message_type_memo）content 的 change 字段：这张卡片对应的是哪一种操作
enum memo_card_change
{
    memo_card_created  = 1,   //新建了这条群备忘：卡片顶部写「新建」
    memo_card_modified = 2,   //修改了这条群备忘：卡片顶部写「修改」；同一条备忘每修改一次就发一张新卡片
};

//入群邀请卡片的状态，位于卡片 content（下发时由服务端合并）与 cmd_group_invite_changed_notify 的 state 字段
//（docs/邀请入群方案.md §4.3）。0 ~ 3 落库（t_group_invite.f_state），4、5 由服务端下发时计算、不落库
enum group_invite_state
{
    group_invite_pending   = 0,   //待加入：被邀请人还没点「加入群聊」
    group_invite_joined    = 1,   //已加入：点了加入并已入群，或被邀请人已经通过别的途径在群里
    group_invite_approving = 2,   //等待审批：点了加入，但邀请人是普通成员且群需审批，已转为加群申请
    group_invite_rejected  = 3,   //未通过审批：群主或管理员拒绝了由本卡片转来的加群申请
    group_invite_expired   = 4,   //已过期：待加入且超过了到期时间
    group_invite_invalid   = 5,   //已失效：待加入，但群已解散或邀请人已退出群聊
};

//系统提示（message_type_system）的结构化类型，写在系统提示 content 的 tipkind 字段（多语言第 5 期，2026-10-04）。
//content 仍带服务端拼好的中文 text，另加 tipkind 与 tipargs（参数对象）：认得 tipkind 的客户端按 tipargs 用当前
//界面语言的模板生成文字；认不出的类型、以及此前落库的历史消息仍显示 text。参数缺省（如取不到昵称）时不写该字段，
//客户端按缺省的写法生成。各类型的参数与示例见 docs/协议设计-v2.md「系统提示的结构化字段」一节
constexpr const char* kSystemTipMemberLeave      = "member_leave";          //成员退群：{ nickname }（2026-10-04 起服务端不再产生，已落库的旧提示仍按此显示）
constexpr const char* kSystemTipOwnerTransfer    = "owner_transfer";        //转让群主：{ from, to }，两者都可缺省
constexpr const char* kSystemTipAnnouncement     = "announcement_publish";  //发布群公告：{ publisher, title }（2026-10-05 起服务端改发群公告卡片、不再产生，已落库的旧提示仍按此显示）
constexpr const char* kSystemTipGroupWelcome     = "group_welcome";         //入群欢迎语：{ names: [新成员昵称...], welcome: 群主设置的欢迎词 }
constexpr const char* kSystemTipRelayJoin        = "relay_join";            //参与接龙：{ nickname, title }，content 另带 cardmsgid
constexpr const char* kSystemTipRelayModify      = "relay_modify";          //修改接龙里本人那条：{ nickname, title }
constexpr const char* kSystemTipRelayWithdraw    = "relay_withdraw";        //撤回接龙里本人那条：{ nickname, title }
constexpr const char* kSystemTipRelayClose       = "relay_close";           //提前结束接龙：{ nickname, title, count: 参与人数 }
constexpr const char* kSystemTipRelayUpdate      = "relay_update";          //接龙的其它更新（兜底种类）：{ nickname, title }
constexpr const char* kSystemTipScreenShareEnd   = "screenshare_end";       //屏幕共享结束：{ duration: 时长，单位秒 }
constexpr const char* kSystemTipMemberJoin       = "member_join";           //有人入群：{ inviter, inviterid, members: [{ userid, nickname }] }，
                                                                            //没有邀请人（自由加群、本人申请经审批）时不写 inviter 与 inviterid；
                                                                            //members 带全部新成员，只有前 kMemberJoinTipMaxNames 位带 nickname
                                                                            //（见 docs/邀请入群方案.md §4.1）
constexpr const char* kSystemTipMuteAllOn        = "muteall_on";            //开启仅通知模式（原称全员禁言）：{ operator, operatorid }，
                                                                            //operator 是操作人昵称，取不到时不写（docs/群管理接入方案.md §14）
constexpr const char* kSystemTipMuteAllOff       = "muteall_off";           //关闭仅通知模式：{ operator, operatorid }，规则同上
constexpr const char* kSystemTipBotAdded         = "bot_added";             //群主添加了群机器人：{ operator, bot, external }；external 为 "1" 表示
                                                                            //它会把 @ 它的消息发送到外部服务（docs/机器人方案.md §4.2、§5.6）
constexpr const char* kSystemTipBotRemoved       = "bot_removed";           //群主删除了群机器人：{ operator, bot }
constexpr const char* kSystemTipBotOff           = "bot_off";               //群主在本群关闭了企业机器人，或停用了群机器人：{ operator, bot }
constexpr const char* kSystemTipBotOn            = "bot_on";                //群主在本群重新打开了企业机器人，或启用了群机器人：{ operator, bot }

//会话类型，位于会话/消息 JSON 的 ctype 字段
enum conversation_type
{
    conversation_single  = 1,   //单聊（对端为用户）
    conversation_group   = 2,   //群聊（对端为群）
    conversation_service = 3,   //服务号会话（系统通知/客服/公告等）
};

//会话的消息提醒模式，位于 cmd_sync_conversations / cmd_set_conversation /
//cmd_conversation_changed_notify / cmd_set_notify_modes 的 notifymode（或 mode）字段，
//也是 t_conversation.f_notify_mode 的取值。五种模式两两互斥，每个会话只取其一。
//服务端只负责保存与校验，收到消息后提不提醒、怎么提醒全部由客户端按本值决定，
//行为表见 docs/消息提醒模式与消息盒子方案.md §3.1。
//0、1 两个值与 2026-10-03 之前的免打扰开关 f_mute 含义相同，迁移时直接沿用。
enum conversation_notify_mode
{
    notify_mode_allow   = 0,    //允许消息提醒（默认）：计未读、红色角标、闪托盘、响提示音；没有 t_conversation 行的会话也按本值处理
    notify_mode_silent  = 1,    //接收消息但不提醒（原「消息免打扰」）：照常计未读但角标为灰色，不闪托盘、不响；群里被 @ 时例外，照常提醒
    notify_mode_msgbox  = 2,    //收进消息盒子且不提醒：会话不单独出现在会话列表里，汇总到「消息盒子」一行；不闪不响，群里被 @ 也只在摘要前加标注
    notify_mode_block   = 3,    //屏蔽消息（仅群聊）：不计未读、不提醒、不更新会话列表，消息仍照常保存，主动打开群聊窗能看到；改出本模式时服务端把已读位点推进到最新一条
    notify_mode_starred = 4,    //特别关注（2026-10-03 由原 cmd_set_starred 并入）：收到消息时客户端直接弹出聊天窗；联系人还会在上下线时弹提示。单聊与群聊合计最多 NOTIFY_MODE_STARRED_MAX 个（MessageProtocol.h）
    notify_mode_mention_follow = 5,   //只提醒 @我和关注的人（仅群聊，2026-10-06，docs/消息提醒模式与消息盒子方案.md §12）：未读照计、胶囊灰色，
                                      //只有被 @ 或本人在该群关注的成员（t_group_follow）发言时才闪托盘、响提示音
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

//接龙状态，位于接龙卡片 content（下发时由服务端合并）与 cmd_relay_changed_notify 的 state 字段。
//由服务端按 t_card_state 的结束记录、卡片内容里的截止时间与当前时间算出（CardProtocol::computeRelayStatus），
//不落库；手动结束优先于截止。见 docs/群接龙方案.md §4.1。
enum relay_state
{
    relay_state_open    = 0,   //进行中：可以参与、修改、撤回本人那条，有权限的人可以提前结束
    relay_state_closed  = 1,   //已被提前结束：closedby / closetime 记录操作者与结束时间，名单不再变化
    relay_state_expired = 2,   //已过截止时间：closetime 取截止时间，名单不再变化，到点后服务端不发提示
};

//文件用途场景，位于 cmd_request_upload 请求的 scene 字段。
//决定上传到 fileserver 还是 imgserver、以及大小上限。
enum file_scene
{
    file_scene_chat_file  = 1,   //聊天文件：单聊/群聊里发送的普通文件，走 fileserver
    file_scene_group_file = 2,   //群文件：登记进群文件库的文件，走 fileserver
    file_scene_avatar     = 3,   //头像：用户/群头像图片，走 imgserver
    file_scene_image_msg  = 4,   //图片消息：聊天里发送的图片/截图，走 imgserver
    file_scene_update     = 5,   //客户端升级包：cmd_check_update 应答里直下发下载令牌用的场景，走 fileserver。
                                 //与前四种不同，本场景的令牌不经 cmd_request_upload / cmd_request_download 申请，
                                 //而是由 chatserver 在检查更新时直接签发（升级包对所有人是同一份公开内容，
                                 //且检查更新须在未登录时可用，此时令牌里的 uid 为 0）
    file_scene_cloud_record = 6, //云端录像（第 26 期，docs/屏幕共享与录制方案.md 12.6）：走 imgserver，文件在 cloudrecorddir。
                                 //令牌由 cmd_cloudrecord_op（1756）签发，md5 字段放的不是内容 MD5 而是文件名（rec_<id>.mp4 写作
                                 //rec_<id>_mp4，见 utils/CloudRecordCtlMsg.h 的 CloudRecordTokenKey）；不做秒传，不校验整文件 MD5
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

//单聊文件夹传输的会话状态，随 cmd_folder_state_notify（1815）推送给两端，
//也是服务端会话表里那条记录的当前状态。写法与取值语义对齐远程桌面的 enum rd_state
//（见 chatserversrc/RemoteDesktopProtocol.h），区别在于文件夹传输多出"传输中"与
//"已完成 / 已失败"这几个态：远程桌面接通之后只有"连着"和"断了"，而文件夹传输
//有明确的终点，且失败会话还要保留一段时间供重试续传。
//状态推送里除 state 外还带 errcode / errmsg：只有 folder_state_failed 会填有意义的
//错误码，其余状态 errcode 为 0。
enum folder_transfer_state
{
    folder_state_ringing      = 1,   //响铃中：邀请已送达接收端，正等待其接收 / 拒绝；超过 folderinvitetimeout 无应答则转 folder_state_timeout
    folder_state_transferring = 2,   //传输中：接收端已同意，中转参数已下发，双方正在通道上收发文件字节；重试续传成功后也回到本状态
    folder_state_rejected     = 3,   //已拒绝：接收端在气泡上点了「拒绝」，会话立即结束，不产生任何中转连接
    folder_state_canceled     = 4,   //已取消：发送端在接收端应答前撤回邀请（cmd_folder_cancel）；传输中途取消<u>不</u>用本状态，而是按失败处理、错误码为 error_code_folder_canceled
    folder_state_peer_offline = 5,   //对方离线：发起邀请时对方无在线连接，或响铃期间对方退出登录，会话作废
    folder_state_timeout      = 6,   //已超时：邀请在限定时间内无人应答，服务端自动结束会话并通知发送端
    folder_state_failed       = 7,   //已失败：传输过程中任一端报错或中转通道断开，整个文件夹判失败；会话保留 folderretrywindow（默认 10 分钟）供重试续传，期满销毁
    folder_state_completed    = 8,   //已完成：全部文件传完并通过校验，双方气泡转完成态，会话正常销毁
    folder_state_to_offline   = 9,   //已改为离线发送：只用于单聊单文件直传（cmd_folder_invite 的 kind 为 1）。发送端在对方接收之前
                                     //撤回直传邀请并改走上传服务器（cmd_folder_cancel 带 tooffline），会话结束、<u>不</u>写聊天记录，
                                     //随后到达的那条离线文件消息代替它（docs/单聊文件P2P直传与P2P徽章方案.md 3.3 节）
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
    group_join_source_search = 1,   //群搜索（t_group_join_request.f_source 的默认值）
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

//原 enum sensitive_word_operation（cmd_operate_sensitive_word 的 operation 字段）随群级
//敏感词于 2026-10-02 删除，敏感词改为企业级词库，见 docs/敏感词方案.md

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
    group_member_change_must_read = 7, //成员的入群必读状态变化：本人确认、群主 / 管理员代为确认、取消必读时一并解除（docs/群公告接入方案.md §14）
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
    announcement_change_recalled  = 2,   //已撤回：管理员撤回了这条公告；推送体只带 announcementid，客户端据此把它从公告中心里移除
};

//平台公告的级别，对应数据库 t_announcement.f_severity。
//只决定公告中心列表里那一条的强调色与图标，不影响投递逻辑。
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
    announcement_audience_department     = 2,   //指定部门：投给这些部门的在职成员；当前<u>不</u>向下级部门递归，见 docs/公告中心接入方案.md 第 9 节
    announcement_audience_user           = 3,   //指定员工：只投给 t_announcement_target 里逐个列出的那些 userid
    announcement_audience_platform       = 4,   //平台内部：仅供运营在管理控制台内部查看，<u>一律不投</u>给任何 IM 用户
};

//==================== 屏幕共享房间中转 ====================
//以下四个枚举描述 cmd_screenshare_relay_data（1860）的帧头与控制帧，帧头为
//[轨道类型 1B][发送者座位号 1B][标志 1B][时间戳 8B 大端]，其后是载荷。服务端只读帧头决定转发与丢帧，载荷不解析
//（控制帧除外）。取值由 flamingoserver/tests/test_protocolparity.cpp 的 PP5 守住两份协议头一致。
//设计见 docs/屏幕共享与录制方案.md 5.4。

//帧头第 1 字节：轨道类型
enum screenshare_track
{
    screenshare_track_screen       = 1,   //屏幕画面（H.264）：主持人用自己的连接发送，其他共享者用「只推流」连接发送（第 22 期），按发送者座位区分画面
    screenshare_track_camera       = 2,   //摄像头画面（H.264），任一成员都可发送
    screenshare_track_mic          = 3,   //麦克风声音（Opus），任一成员都可发送
    screenshare_track_system_audio = 4,   //电脑声音（Opus），只有发起人发送
    screenshare_track_annotation   = 5,   //标注
    screenshare_track_control      = 6,   //控制帧：载荷第 1 字节为 screenshare_control_type
};

//帧头第 3 字节：标志位，可组合。一个视频帧太大时由发送端拆成多个分片依次发送，每个分片各占一帧 1860
enum screenshare_frame_flag
{
    screenshare_flag_keyframe   = 0x01,   //视频关键帧；一个关键帧的每个分片都带此位
    screenshare_flag_first_frag = 0x02,   //一帧的第一个分片；不拆分的帧同时带首、尾两位。服务端只在首分片上决定丢不丢这一帧
    screenshare_flag_last_frag  = 0x04,   //一帧的最后一个分片
};

//控制帧（screenshare_track_control）载荷的第 1 字节
enum screenshare_control_type
{
    screenshare_control_keyframe_request = 1,   //请求关键帧：载荷第 2 字节为要求的视频轨（screen 或 camera）。服务端因
                                                //慢观看者丢帧或新成员加入而发出，发给这路视频的发送者
    screenshare_control_source_state     = 2,   //共享内容的状态：载荷第 2 字节取值见 screenshare_source_state。发起人在状态
                                                //变化时、以及每次发关键帧时发出（后加入的观看者据此得知当前状态），服务端只转发
    screenshare_control_subscribe        = 3,   //订阅（群通话第 5 期，docs/协议设计-v2.md 21.8）：接收端发给服务端，载荷为
                                                //[3][座位数 N][N 个座位号]，表示只要这些座位的摄像头画面；服务端消费、不转发
    screenshare_control_input            = 4,   //键鼠输入（第 18 期控制键鼠）：载荷为 [4][目标座位号][一条输入命令]，输入命令的
                                                //格式同远程桌面（鼠标坐标换成相对画面的 0~65535）。服务端只转发给目标座位（画面
                                                //主人），不广播，避免键盘输入被其他观看者收到；画面主人按控制者座位核对后才注入
    screenshare_control_caption          = 5,   //语音字幕（docs/语音字幕方案.md 第 9 节）：发言人本机识别自己的话后发出，载荷为
                                                //[5][标志 1B][句子编号 4B 大端][时长 2B 大端，毫秒][UTF-8 文字]，标志 0 为中间结果、
                                                //1 为最终结果。服务端改写座位号后直接转发，不解析载荷；不认识它的老客户端忽略
};

//控制帧 screenshare_control_source_state 载荷第 2 字节：共享内容当前的状态，观看端据此显示提示
enum screenshare_source_state
{
    screenshare_source_sharing   = 0,   //正常共享中
    screenshare_source_minimized = 1,   //共享的窗口已最小化，暂停发新帧
    screenshare_source_paused    = 2,   //发起人暂停了共享
    screenshare_source_closed    = 3,   //共享的窗口已关闭，发起人正在改选共享内容
};

//帧头第 2 字节（发送者座位号）的特殊取值
enum screenshare_seat
{
    screenshare_seat_host   = 0,     //发起人的座位号；观看者为 1~254，按加入顺序取最小的空闲号
    screenshare_seat_max    = 254,   //座位号上限（含）
    screenshare_seat_server = 255,   //由服务端生成的帧（如关键帧请求），不是任何成员发出的
};

//==================== 云端录制 ====================
//以下五个枚举随 cmd_cloudrecord_* 1754~1757、1725 事件 15 与表 t_cloud_record 使用，取值由
//flamingoserver/tests/test_protocolparity.cpp 的 PP5 守住两份协议头一致。设计见 docs/屏幕共享与录制方案.md 12.6。

//云端录像的来源（t_cloud_record.f_source）
enum cloudrecord_source
{
    cloudrecord_source_share  = 1,   //屏幕共享中由 imgserver 录下原始帧，开启者的客户端合成后上传
    cloudrecord_source_upload = 2,   //独立录制在本机录好后上传（MP4 或只录声音的 m4a）
};

//云端录像的状态（t_cloud_record.f_status，1755 列表与 1757 推送里的 status）
enum cloudrecord_status
{
    cloudrecord_status_recording = 1,   //共享中正在录（含暂停）
    cloudrecord_status_pending   = 2,   //录制已结束、原始帧已保存，等开启者的客户端合成
    cloudrecord_status_ready     = 3,   //最终文件已上传，可观看、可发到会话
    cloudrecord_status_failed    = 4,   //录制出错且没有录到任何画面与声音（如开始后立即磁盘不足），只能删除
    cloudrecord_status_uploading = 5,   //独立录制已申请上传、还没传完；上传中断后可以续传
};

//1754 的动作（action 字段），只有主持人能执行
enum cloudrecord_control_action
{
    cloudrecord_control_start  = 1,   //开始云端录制：检查开关、配额、控制连接，建录像记录，通知 imgserver 开始
    cloudrecord_control_pause  = 2,   //暂停：暂停期间的帧不写入录像
    cloudrecord_control_resume = 3,   //继续
    cloudrecord_control_stop   = 4,   //停止：imgserver 写完文件后回报大小与时长，状态改为等待合成
};

//1756 的操作（op 字段）
enum cloudrecord_op_type
{
    cloudrecord_op_rename         = 1,   //开启者重命名（标题 1~50 字，过敏感词）
    cloudrecord_op_delete         = 2,   //开启者删除：删库记录与分享记录，经控制连接通知 imgserver 删文件
    cloudrecord_op_download_token = 3,   //取下载令牌：开启者可取原始帧文件（合成用）与最终文件，会话成员只能取最终文件
    cloudrecord_op_upload_token   = 4,   //取上传令牌：开启者上传合成后的 MP4（来源为共享），或新建一条来源为上传的录像
    cloudrecord_op_send           = 5,   //发到会话：只能发可观看的录像，服务端写分享记录并发一条云端录像卡片消息
};

//1757 推送的类型（type 字段）
enum cloudrecord_notify_type
{
    cloudrecord_notify_status = 1,   //状态变了（等待合成、可观看、出错），附整条录像
    cloudrecord_notify_expire = 2,   //即将到期（到期前 3 天推送一次），附整条录像
    cloudrecord_notify_delete = 3,   //已被删除（到期清理，或开启者在另一台电脑上删除），只带录像 id
};

//==================== 文件共享 ====================
//以下七个枚举随 cmd_share_* 2201~2230、共享卡片（message_type_fileshare）与审计表
//t_file_share_audit 使用，取值由 flamingoserver/tests/test_protocolparity.cpp 的 PP5
//守住两份协议头一致。设计见 docs/文件共享方案.md 5.7。

//被共享对象的类型，位于 cmd_share_create 请求与共享卡片的 kind 字段，对应 t_file_share.f_kind
enum share_kind
{
    share_kind_disk   = 1,   //整个磁盘：共享根为驱动器根目录（如 D:\），访问方按目录浏览
    share_kind_folder = 2,   //一个文件夹：共享根为该目录，访问方按目录浏览
    share_kind_file   = 3,   //单个文件：访问方只能下载，没有浏览窗口，也没有任何写权限
};

//成员在某个共享上的写权限位，可按位组合，位于 members[].perms 与卡片的 myperms 字段，
//对应 t_file_share_member.f_perms。下载不占位，成员恒可下载；单文件共享的权限恒为 0。
enum share_perm
{
    share_perm_upload = 1,    //上传：把本机的文件或目录传进共享；顶层与目标位置同名时整个拒绝
    share_perm_mkdir  = 2,    //新建文件夹：在共享内建一个空目录，同名即拒绝
    share_perm_rename = 4,    //重命名：改共享内某个文件或文件夹的名字（共享根本身除外）
    share_perm_delete = 8,    //删除：放进分享者电脑的回收站，放不进去时拒绝删除
    share_perm_move   = 16,   //移动：把条目挪到同一个共享内的另一个目录
};

//共享对某个人的状态，位于共享卡片的 sharestate 与 cmd_share_state_notify 的 state 字段。
//同一个共享对不同的人可能状态不同（例如被移出的成员看到已失效、其他成员看到可用）。
enum share_state
{
    share_state_available   = 1,   //可用：分享者在绑定的电脑上在线，且已报告共享根可以提供
    share_state_unavailable = 2,   //暂不可用：共享仍然有效，但暂时无法访问，原因取 share_unavailable_reason
    share_state_invalid     = 3,   //已失效：共享已被取消，或「我」已不再是成员，原因取 share_invalid_reason
};

//「暂不可用」的原因，位于 sharestate 为 share_state_unavailable 时的 sharereason 字段
enum share_unavailable_reason
{
    share_unavailable_owner_offline    = 1,   //分享者不在线（断网、关机、关闭客户端、退出登录都属此类）
    share_unavailable_other_device     = 2,   //分享者在线，但登录在别的电脑上，不是共享所绑定的那一台
    share_unavailable_root_missing     = 3,   //分享者在线且在原电脑上，但共享根已不存在（U 盘拔出、目录被删除或改名）
    share_unavailable_feature_disabled = 4,   //本企业的文件共享功能已被管理员关闭
};

//「已失效」的原因，位于 sharestate 为 share_state_invalid 时的 sharereason 字段，
//前五项同时记入 t_file_share.f_cancel_reason
enum share_invalid_reason
{
    share_invalid_owner_canceled       = 1,   //分享者本人取消了共享
    share_invalid_group_admin_canceled = 2,   //群主或群管理员取消了这个群里的共享
    share_invalid_admin_canceled       = 3,   //管理员在后台强制取消了共享
    share_invalid_group_dismissed      = 4,   //共享所在的群已解散，群内的共享全部取消
    share_invalid_owner_left_group     = 5,   //分享者已退出共享所在的群，他在该群的共享全部取消
    share_invalid_member_removed       = 6,   //共享仍然有效，但分享者把「我」从成员中移除了
    share_invalid_member_left_group    = 7,   //共享仍然有效，但「我」已退出或被移出共享所在的群
};

//审计动作，对应 t_file_share_audit.f_action，也是 cmd_share_audit_report 里 items[].action 的取值
enum share_audit_action
{
    share_audit_open             = 1,    //访问者打开共享（cmd_share_open 成功），服务端记录
    share_audit_request_download = 2,    //访问者申请一次下载（cmd_share_request_transfer），服务端记录，带条目与总字节数
    share_audit_request_upload   = 3,    //访问者申请一次上传，服务端记录，带条目与总字节数
    share_audit_download_file    = 4,    //分享者一端发完一个文件，分享者客户端上报
    share_audit_upload_file      = 5,    //分享者一端收完并改成正式名一个文件，分享者客户端上报
    share_audit_mkdir            = 6,    //新建文件夹，分享者客户端上报执行结果
    share_audit_rename           = 7,    //重命名，分享者客户端上报执行结果，带原路径与新路径
    share_audit_delete           = 8,    //删除（放入回收站），分享者客户端上报执行结果
    share_audit_move             = 9,    //移动，分享者客户端上报执行结果，带原路径与目标目录
    share_audit_create_share     = 10,   //发起共享，服务端记录
    share_audit_cancel_share     = 11,   //取消共享（含群主、管理员、后台、群解散、分享者退群各入口），服务端记录
    share_audit_update_members   = 12,   //追加或移除成员、调整成员权限，服务端记录
};

//审计记录的来源，对应 t_file_share_audit.f_source
enum share_audit_source
{
    share_audit_source_server       = 1,   //服务端亲眼所见：动作经过 chatserver，记录不依赖任何客户端如实上报
    share_audit_source_owner_client = 2,   //分享者客户端上报：执行结果只有分享者一端知道，被篡改的客户端可以不报
};

//==================== P2P 连接 ====================
//以下三个枚举是 cmd_p2p_session_notify / cmd_p2p_result / cmd_p2p_close 包体里的取值（docs/P2P通信方案.md 第 5 节），
//服务端与客户端的 FlamingoP2P.dll 都按它们解读，两份协议头由 PP5 逐条比对。

//P2P 会话里本方的角色，cmd_p2p_session_notify 的 role 字段
enum p2p_role
{
    p2p_role_controlling = 1,   //发起方：负责在连通的地址对里选定一对并通知对方；回落中转时用令牌角色 role_p2p_controlling
    p2p_role_controlled  = 2,   //应答方：等待发起方选定；回落中转时用令牌角色 role_p2p_controlled
};

//P2P 链路最终选定的路径，cmd_p2p_result 的 path 字段
enum p2p_path
{
    p2p_path_lan     = 1,   //经本机网卡地址直连（同一局域网或同一 VPN 内）
    p2p_path_punched = 2,   //经地址探测得到的公网映射地址打洞直连
    p2p_path_relay   = 3,   //打洞超时，改走 imgserver 的一对一中转（cmd_p2p_relay_hello / cmd_p2p_relay_data）
};

//P2P 会话结束的原因，cmd_p2p_close 的 reason 字段
enum p2p_close_reason
{
    p2p_close_business_end = 1,   //业务正常结束（传输完成、通话挂断等），由业务方主动关闭链路
    p2p_close_connect_fail = 2,   //建链失败：打洞超时，回落中转也没有成功
    p2p_close_peer_offline = 3,   //对端与 chatserver 的连接已断开，由服务端推给仍在线的一方
    p2p_close_link_timeout = 4,   //链路建立后，超过断开判定时间收不到对方的任何报文
    p2p_close_self_logout  = 5,   //本方注销或退出程序
};

//==================== 备忘与提醒 ====================
//cmd_memo_save / cmd_memo_list / cmd_memo_notify 里备忘对象的取值，服务端与客户端按同一份解读。

//备忘的重复方式，备忘对象的 repeat 字段（t_memo.f_repeat）
enum memo_repeat_type
{
    memo_repeat_none    = 0,   //不重复：只在开始时间提醒一次
    memo_repeat_daily   = 1,   //每天同一时刻
    memo_repeat_workday = 2,   //每个工作日（周一到周五）同一时刻，不考虑法定节假日
    memo_repeat_weekly  = 3,   //每周：weekdays 掩码里选中的那几天，同一时刻
    memo_repeat_monthly = 4,   //每月：开始那天的「几号」，当月没有这一天时取当月最后一天
    memo_repeat_yearly  = 5,   //每年：开始那天的月日，2 月 29 日在平年取 2 月 28 日
};

//备忘的提醒方式，备忘对象的 style 字段（t_memo.f_style）
enum memo_style
{
    memo_style_popup      = 1,   //桌面右下角弹窗，一直留着，点「知道了」才关
    memo_style_fullscreen = 2,   //盖住主面板所在显示器的全屏动画，60 秒无操作后转为右下角弹窗
};

//cmd_memo_notify 的 changetype 字段
enum memo_change_type
{
    memo_change_upsert = 1,   //新增或修改：包体带完整的 memo 对象，客户端按 memoid 覆盖或插入
    memo_change_delete = 2,   //删除，或收到这一帧的人从此看不到这条备忘：包体只带 memoid
};
