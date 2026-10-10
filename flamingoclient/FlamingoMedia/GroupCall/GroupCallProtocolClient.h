/**
 *  v2 群通话信令编解码（客户端侧），GroupCallProtocolClient.h
 *
 *  构造发往 chatserver 的群通话请求 JSON（发起 / 加入 / 拒绝 / 离开 / 追加邀请 / 成员媒体 / 查询 / 管理，
 *  cmd_groupcall_* 1740~1751），解析 chatserver 回来的应答与推送（通用应答、带名单的应答、邀请 1741、房间状态 1745、
 *  中转参数 1746、查询应答 1749、群内状态 1750），以及解析群通话卡片（message_type_rtc，kind 为 groupcall）的 content。
 *
 *  本模块与服务端 chatserversrc/GroupCallProtocol 互为镜像：服务端解析请求、构造应答与推送，客户端反之。
 *  64 位 userid 与群 id 一律以字符串编码进 JSON；开关类字段一律用整数 0 / 1。字段与示例以 docs/协议设计-v2.md
 *  第 21 节为准。本模块为纯逻辑，只依赖 jsoncpp，便于单元测试，见 flamingoclient/Source/Tests/GroupCallProtocolClientTests.cpp。
 *  FlamingoUnit 直接编译本文件，所以不得包含只属于 DLL 的头文件（exe 解析卡片 content 用的是 Source/UI/BalloonChatWnd/ChatGroupCallCard）。
 *
 *  典型用法：
 *      std::string body = groupcall::GroupCallProtocolClient::buildStart(groupId, groupcall::gc_media_voice, invitees);
 *      host->SendSignal(cmd_groupcall_start, body.data(), (uint32_t)body.size());
 *      ...
 *      groupcall::GroupCallState state;
 *      if (groupcall::GroupCallProtocolClient::parseStateNotify(json, state)) 刷新成员;
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace groupcall {

//群通话的媒体类型，与服务端 groupcall_media 取值一致：只表示发起时的意图，决定加入时摄像头默认开不开
enum gc_media
{
    gc_media_voice = 1,   //群语音：加入时摄像头默认关闭
    gc_media_video = 2,   //群视频：加入时摄像头默认打开
};

//房间里一个人的状态，与服务端 groupcall_member_state 取值一致
enum gc_member_state
{
    gc_member_ringing = 1,   //被邀请、正在振铃，还没接听；没有座位
    gc_member_joined  = 2,   //已加入通话，占一个座位
};

//房间状态推送（cmd_groupcall_state_notify=1745）的事件类型，与服务端 groupcall_event 取值一致
enum gc_event
{
    gc_event_member_joined  = 1,    //有人加入（含发起人建房）；actor 为加入者
    gc_event_member_left    = 2,    //有人离开（含掉线超过宽限期）；actor 为离开者
    gc_event_declined       = 3,    //被邀请人拒绝；actor 为拒绝者
    gc_event_ring_timeout   = 4,    //被邀请人振铃超时未接（含振铃中掉线）；actor 为被邀请人
    gc_event_invited        = 5,    //追加了邀请；actor 为邀请人
    gc_event_media_changed  = 6,    //有人开关了麦克风或摄像头；actor 为该成员
    gc_event_member_offline = 7,    //已加入的人掉线，进入宽限期；actor 为掉线者
    gc_event_member_back    = 8,    //掉线的人在宽限期内回来；actor 为回来者
    gc_event_member_muted   = 9,    //某人被静音；actor 为操作者，target 为被静音的人
    gc_event_member_removed = 10,   //某人被移出；actor 为操作者，target 为被移出的人，快照里已没有此人
    gc_event_muted_all      = 11,   //开启全员静音；actor 为操作者
    gc_event_unmuted_all    = 12,   //解除全员静音；actor 为操作者
};

//管理请求（cmd_groupcall_manage=1751）的操作，与服务端 groupcall_manage_action 取值一致
enum gc_manage_action
{
    gc_manage_mute       = 1,   //静音某人
    gc_manage_mute_all   = 2,   //开启全员静音（房间状态：发起人、群主、管理员之外的人全部静音且不能自己开麦克风）
    gc_manage_remove     = 3,   //移出某人
    gc_manage_unmute_all = 4,   //解除全员静音（只解除开麦限制，不替任何人打开麦克风）
};

//房间里的一个人（1741 / 1745 的 members 数组的一项）
struct GroupCallMember
{
    int64_t m_userId;   //userid
    int     m_seat;     //座位号：已加入时 1~254，振铃中为 0
    int     m_state;    //状态，取值见 enum gc_member_state
    bool    m_mic;      //是否开着麦克风
    bool    m_camera;   //是否开着摄像头
    bool    m_online;   //是否在线；false 表示已加入的人掉线、处于宽限期

    GroupCallMember();
};

//房间快照：1741 与 1745 共有的房间字段与成员
struct GroupCallState
{
    std::string                  m_roomId;        //房间号
    int64_t                      m_groupId;       //群 id
    int                          m_media;         //发起时的媒体类型，取值见 enum gc_media
    int64_t                      m_initiatorId;   //发起人 userid
    int64_t                      m_startTime;     //发起时间，Unix 毫秒
    int                          m_event;         //事件类型（只 1745 有），取值见 enum gc_event；1741 时为 0
    int64_t                      m_actor;         //触发事件的人（只 1745 有）
    int64_t                      m_target;        //事件针对的人（事件 9、10）；没有时为 0
    bool                         m_allMuted;      //房间是否处于全员静音（allmuted 为 1）；老服务端不带此字段时为 false
    std::vector<GroupCallMember> m_members;       //房间里的人：已加入的按座位号在前，振铃中的在后

    GroupCallState();

    //查某个人；不在房间里返回 NULL（指针指向本对象内部，随本对象销毁失效）
    const GroupCallMember* findMember(int64_t userId) const;

    //已加入的人数
    int joinedCount() const;
};

//邀请推送（cmd_groupcall_invite_notify=1741）
struct GroupCallInvite
{
    GroupCallState m_room;          //邀请发出时的房间快照
    std::string    m_groupName;     //群名（UTF-8）
    int64_t        m_inviterId;     //邀请人 userid
    std::string    m_inviterName;   //邀请人显示名（UTF-8）
    int64_t        m_ringMs;        //剩余振铃毫秒数

    GroupCallInvite();
};

//应答：通用应答与带名单的应答（1740 / 1747）共用
struct GroupCallResult
{
    int                  m_code;      //错误码，0 为成功
    std::string          m_msg;       //说明文字
    std::string          m_roomId;    //房间号；失败且没有房间号时为空（722 时为现有房间号）
    std::vector<int64_t> m_busy;      //正忙、没有振铃的被邀请人（只 1740 / 1747 有）
    std::vector<int64_t> m_offline;   //不在线、没有振铃的被邀请人（只 1740 / 1747 有）

    GroupCallResult();
};

//中转参数（cmd_groupcall_relay_info=1746）
struct GroupCallRelayInfo
{
    std::string m_roomId;       //房间号
    std::string m_relayIp;      //imgserver 中转地址
    int         m_relayPort;    //imgserver 中转端口
    std::string m_token;        //relay 令牌，连上中转后经 1859 报到
    int         m_seat;         //本人的座位号
    int         m_maxCameras;   //同时开摄像头的人数上限
    int64_t     m_elapsedMs;    //服务端发出时通话已进行的毫秒数

    GroupCallRelayInfo();
};

//查询应答（cmd_groupcall_query=1749）
struct GroupCallQueryResult
{
    int                  m_code;          //错误码，0 为成功
    bool                 m_exists;        //该群是否有进行中的群通话
    std::string          m_roomId;        //房间号（m_exists 为 true 时有效）
    int                  m_media;         //媒体类型（同上）
    int64_t              m_initiatorId;   //发起人（同上）
    int64_t              m_startTime;     //发起时间，Unix 毫秒（同上）
    int                  m_count;         //已加入人数（同上）
    std::vector<int64_t> m_members;       //已加入的人（同上）

    GroupCallQueryResult();
};

//群内状态（cmd_groupcall_status_notify=1750）
struct GroupCallStatus
{
    std::string m_roomId;            //房间号
    int64_t     m_groupId;           //群 id
    bool        m_active;            //通话是否仍在进行
    int         m_media;             //媒体类型
    int64_t     m_initiatorId;       //发起人
    int64_t     m_startTime;         //发起时间，Unix 毫秒
    int         m_count;             //已加入人数；结束时为 0
    int64_t     m_durationSeconds;   //通话时长（秒），只在结束时有
    int64_t     m_cardMsgId;         //改写后的通话卡片消息 id；只在结束且服务端改写成功时有，否则为 0
    std::string m_cardContent;       //改写后的卡片 content（JSON 文本）

    GroupCallStatus();
};

//群通话卡片（message_type_rtc，content 的 kind 为 groupcall）
struct GroupCallCard
{
    std::string m_roomId;            //房间号
    int         m_media;             //媒体类型
    int64_t     m_initiatorId;       //发起人
    std::string m_initiatorName;     //发起人显示名（UTF-8）
    int64_t     m_startTime;         //发起时间，Unix 毫秒
    bool        m_ended;             //是否已结束（content 里有 duration）
    int64_t     m_durationSeconds;   //通话时长（秒），m_ended 为 true 时有效

    GroupCallCard();
};

//群通话信令编解码工具类；全部为静态方法，不可实例化
class GroupCallProtocolClient
{
public:
    //==== 构造 客户端→服务端 的请求 ====

    /**
     *  发起群通话（cmd_groupcall_start=1740），形如 {"groupid":"600001","media":1,"invitees":["5308"],"mic":1,"camera":0}。
     *  @param groupId  群 id
     *  @param media    媒体类型，取值见 enum gc_media
     *  @param invitees 要振铃的人
     *  @param mic      发起人初始是否开麦克风
     *  @param camera   发起人初始是否开摄像头
     */
    static std::string buildStart(int64_t groupId, int media, const std::vector<int64_t>& invitees, bool mic,
                                  bool camera);

    /**
     *  加入（cmd_groupcall_join=1742），形如 {"roomid":"...","mic":1,"camera":0}。
     */
    static std::string buildJoin(const std::string& roomId, bool mic, bool camera);

    //只带房间号的请求：拒绝（1743）、离开（1744），形如 {"roomid":"..."}
    static std::string buildRoomRequest(const std::string& roomId);

    //追加邀请（cmd_groupcall_invite_more=1747），形如 {"roomid":"...","invitees":["5311"]}
    static std::string buildInviteMore(const std::string& roomId, const std::vector<int64_t>& invitees);

    //上报本人的媒体开关（cmd_groupcall_member_media=1748），两项都给出
    static std::string buildMemberMedia(const std::string& roomId, bool mic, bool camera);

    //查询某群有无进行中的群通话（cmd_groupcall_query=1749），形如 {"groupid":"600001"}
    static std::string buildQuery(int64_t groupId);

    /**
     *  管理（cmd_groupcall_manage=1751），形如 {"roomid":"...","action":1,"target":"5308"}。
     *  @param action 取值见 enum gc_manage_action
     *  @param target 目标 userid；全员静音时传 0，不带该字段
     */
    static std::string buildManage(const std::string& roomId, int action, int64_t target);

    //==== 解析 服务端→客户端 的应答与推送 ====

    //应答（通用与带名单的都用它）；JSON 非法或缺 code 返回 false
    static bool parseResult(const std::string& json, GroupCallResult& out);

    //邀请推送 1741；缺 roomid / groupid 或 members 不是数组返回 false
    static bool parseInvite(const std::string& json, GroupCallInvite& out);

    //房间状态 1745；缺 roomid / event 或 members 不是数组返回 false
    static bool parseStateNotify(const std::string& json, GroupCallState& out);

    //中转参数 1746；缺 roomid / token / relayport 返回 false
    static bool parseRelayInfo(const std::string& json, GroupCallRelayInfo& out);

    //查询应答 1749；缺 code 返回 false
    static bool parseQueryResult(const std::string& json, GroupCallQueryResult& out);

    //群内状态 1750；缺 roomid / groupid / active 返回 false
    static bool parseStatus(const std::string& json, GroupCallStatus& out);

    /**
     *  解析群通话卡片的 content。
     *  @return content 是 JSON 对象、kind 为 groupcall 且带 roomid 时返回 true
     */
    static bool parseCard(const std::string& content, GroupCallCard& out);

private:
    GroupCallProtocolClient();
};

} // namespace groupcall
