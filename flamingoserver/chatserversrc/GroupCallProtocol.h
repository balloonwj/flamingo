/**
 *  v2 群通话信令编解码（服务端侧），GroupCallProtocol.h
 *
 *  解析客户端发来的群通话请求（cmd_groupcall_* 1740~1751），构造回给客户端的应答、推送与群通话卡片的 content。
 *  客户端的镜像实现在 FlamingoMedia.dll 的 GroupCall/GroupCallProtocolClient。64 位 userid、群 id 与毫秒时间一律以字符串
 *  编码进 JSON，避免大整数精度丢失；开关类字段一律用整数 0 / 1。请求与应答的完整示例见 docs/协议设计-v2.md 第 21 节。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp，不触及网络与数据库，便于单元测试，见 tests/test_groupcallprotocol.cpp（Suite GCP）。
 *
 *  典型用法（GroupCallService 里）：
 *      GroupCallStartRequest req;
 *      if (!GroupCallProtocol::parseStartRequest(body, req))
 *          { 回 GroupCallProtocol::buildResult(error_code_bad_request, "bad request", "") }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "GroupCallManager.h"

//群通话的媒体类型（1740 请求、1741 / 1745 / 1749 / 1750 与通话卡片的 media 字段）。只表示发起时的意图，
//决定加入时摄像头默认开不开；通话中每个人的麦克风、摄像头经 1748 各自开关
enum groupcall_media
{
    groupcall_media_voice = 1,   //群语音：加入时摄像头默认关闭
    groupcall_media_video = 2,   //群视频：加入时摄像头默认打开
};

//cmd_groupcall_state_notify（1745）的事件类型（event 字段），告诉房间里的人这一帧推送是因为什么而发
enum groupcall_event
{
    groupcall_event_member_joined  = 1,    //有人加入（含发起人建房）；actor 为加入者
    groupcall_event_member_left    = 2,    //有人离开（含掉线超过宽限期）；actor 为离开者
    groupcall_event_declined       = 3,    //被邀请人拒绝；actor 为拒绝者
    groupcall_event_ring_timeout   = 4,    //被邀请人振铃超时未接（含振铃中掉线）；actor 为被邀请人
    groupcall_event_invited        = 5,    //追加了邀请；actor 为邀请人
    groupcall_event_media_changed  = 6,    //有人开关了麦克风或摄像头；actor 为该成员
    groupcall_event_member_offline = 7,    //已加入的人掉线，进入宽限期；actor 为掉线者
    groupcall_event_member_back    = 8,    //掉线的人在宽限期内回来；actor 为回来者
    groupcall_event_member_muted   = 9,    //某人被静音（第 6 期）；actor 为操作者，target 为被静音的人
    groupcall_event_member_removed = 10,   //某人被移出（第 6 期）；actor 为操作者，target 为被移出的人，快照里已没有此人
    groupcall_event_muted_all      = 11,   //开启全员静音（第 6 期；2026-10-05 起是房间状态，快照里 allmuted 为 1）；actor 为操作者
    groupcall_event_unmuted_all    = 12,   //解除全员静音（2026-10-05）：只解除开麦限制，不替任何人打开麦克风；actor 为操作者
};

//cmd_groupcall_manage（1751，第 6 期）的操作（action 字段）
enum groupcall_manage_action
{
    groupcall_manage_mute     = 1,   //静音某人：把他的麦克风开关置 0，对方之后可以自己重新打开；须带 target
    //开启全员静音（房间状态，2026-10-05 起）：发起人、群主、管理员之外的已加入的人全部静音；开启期间这些人不能自己
    //打开麦克风（1748 回 728），之后加入的人也默认静音；不带 target
    groupcall_manage_mute_all = 2,
    groupcall_manage_remove   = 3,   //移出某人（已加入或振铃中），之后可以再加入；须带 target
    groupcall_manage_unmute_all = 4, //解除全员静音（2026-10-05）：只解除开麦限制，不替任何人打开麦克风；不带 target
};

//发起群通话请求（1740）解析出的全部字段
struct GroupCallStartRequest
{
    int64_t              m_groupId;    //群 id（>0）
    int                  m_media;      //媒体类型，取值见 enum groupcall_media
    std::vector<int64_t> m_invitees;   //要振铃的人（原样，未去重；服务端另行筛选）
    bool                 m_mic;        //发起人初始是否开麦克风；缺省 true
    bool                 m_camera;     //发起人初始是否开摄像头；缺省按 media（群视频为 true）

    GroupCallStartRequest();
};

//群通话信令编解码工具类；全部为静态方法，不可实例化
class GroupCallProtocol
{
public:
    //一次请求最多带多少个被邀请人：座位号只有 1 字节，超过这个数一定超过人数上限，直接按请求格式错误处理
    static const size_t kMaxInviteesPerRequest = 254;

    //==== 解析 客户端→服务端 的请求 ====

    /**
     *  解析发起请求（cmd_groupcall_start=1740），形如 {"groupid":"600001","media":1,"invitees":["5308"],"mic":1,"camera":0}。
     *  @param out [出参] 解析结果
     *  @return JSON 合法、groupid>0、media 为 1 或 2、invitees 若出现须是字符串 id 数组（不超过 kMaxInviteesPerRequest 项、
     *          每项 >0）、mic / camera 若出现须是整数时返回 true
     */
    static bool parseStartRequest(const std::string& json, GroupCallStartRequest& out);

    /**
     *  解析加入请求（cmd_groupcall_join=1742），形如 {"roomid":"...","mic":1,"camera":0}。
     *  @param roomId [出参] 房间号
     *  @param mic    [出参] 初始是否开麦克风；缺省 true
     *  @param camera [出参] 初始是否开摄像头；缺省 false
     *  @return JSON 合法、roomid 为非空字符串，且 mic / camera 若出现须是整数时返回 true
     */
    static bool parseJoinRequest(const std::string& json, std::string& roomId, bool& mic, bool& camera);

    /**
     *  解析只带房间号的请求：拒绝（1743）、离开（1744），形如 {"roomid":"..."}。
     *  @return JSON 合法且 roomid 为非空字符串返回 true
     */
    static bool parseRoomRequest(const std::string& json, std::string& roomId);

    /**
     *  解析追加邀请请求（cmd_groupcall_invite_more=1747），形如 {"roomid":"...","invitees":["5311"]}。
     *  @return JSON 合法、roomid 非空、invitees 是非空的字符串 id 数组（规则同 1740）时返回 true
     */
    static bool parseInviteMoreRequest(const std::string& json, std::string& roomId, std::vector<int64_t>& invitees);

    /**
     *  解析成员媒体请求（cmd_groupcall_member_media=1748），形如 {"roomid":"...","mic":1,"camera":0}。两项须全部给出。
     *  @return JSON 合法、roomid 非空且两项都是整数时返回 true
     */
    static bool parseMemberMediaRequest(const std::string& json, std::string& roomId, bool& mic, bool& camera);

    /**
     *  解析查询请求（cmd_groupcall_query=1749），形如 {"groupid":"600001"}。
     *  @return JSON 合法且 groupid>0 返回 true
     */
    static bool parseQueryRequest(const std::string& json, int64_t& groupId);

    /**
     *  解析管理请求（cmd_groupcall_manage=1751，第 6 期），形如 {"roomid":"...","action":1,"target":"5308"}。
     *  @param action [出参] 取值见 enum groupcall_manage_action
     *  @param target [出参] 目标 userid；全员静音时为 0
     *  @return JSON 合法、roomid 非空、action 取值合法，且静音某人与移出须带 >0 的 target 时返回 true
     */
    static bool parseManageRequest(const std::string& json, std::string& roomId, int& action, int64_t& target);

    //==== 构造 服务端→客户端 的应答与推送 ====

    /**
     *  构造通用应答（1742~1744、1748、1751 等），形如 {"code":0,"msg":"ok","roomid":"..."}。
     *  @param roomId 房间号；为空串时不带 roomid 字段（722 时传现有房间号）
     */
    static std::string buildResult(int code, const std::string& msg, const std::string& roomId);

    /**
     *  构造发起（1740）与追加邀请（1747）的应答，在通用应答之外带 busy / offline 两个名单（字符串 userid 数组）。
     *  @param busy    正忙、没有振铃的被邀请人
     *  @param offline 不在线、没有振铃的被邀请人
     */
    static std::string buildInviteResult(int code, const std::string& msg, const std::string& roomId,
                                         const std::vector<int64_t>& busy, const std::vector<int64_t>& offline);

    /**
     *  构造邀请推送（cmd_groupcall_invite_notify=1741），发给被邀请人。
     *  @param room        邀请发出时的房间快照（members 字段据此生成）
     *  @param inviterId   邀请人 userid
     *  @param inviterName 邀请人显示名（UTF-8）
     *  @param groupName   群名（UTF-8）
     *  @param ringMs      剩余振铃毫秒数；负数按 0
     */
    static std::string buildInviteNotify(const GroupCallRoom& room, int64_t inviterId, const std::string& inviterName,
                                         const std::string& groupName, int64_t ringMs);

    /**
     *  构造房间状态推送（cmd_groupcall_state_notify=1745），附事件发生后的完整房间快照。
     *  @param event  事件类型，取值见 enum groupcall_event
     *  @param actor  触发事件的人
     *  @param room   事件发生后的房间快照
     *  @param target 事件针对的人（事件 9、10）；0 表示没有，不带该字段
     */
    static std::string buildStateNotify(int event, int64_t actor, const GroupCallRoom& room, int64_t target = 0);

    /**
     *  构造中转参数推送（cmd_groupcall_relay_info=1746），只发给本人这条连接，形如
     *  {"roomid":"...","relayip":"...","relayport":20003,"token":"...","seat":2,"maxcameras":9,"elapsedms":36500}。
     *  @param elapsedMs 通话已进行的毫秒数；负数按 0
     */
    static std::string buildRelayInfo(const std::string& roomId, const std::string& relayIp, int relayPort,
                                      const std::string& token, int seat, int maxCameras, int64_t elapsedMs);

    /**
     *  构造查询应答（cmd_groupcall_query=1749）。有进行中的群通话时带 exists=1 与房间概况（已加入人数与名单）；
     *  没有时 exists 为 0。
     *  @param room 进行中的房间快照；没有时传 NULL
     */
    static std::string buildQueryResponse(int code, const std::string& msg, const GroupCallRoom* room);

    /**
     *  构造群内状态推送（cmd_groupcall_status_notify=1750），发给全体群成员与房间里的人。
     *  @param room            房间快照（结束时为结束之前的快照）
     *  @param active          通话是否仍在进行
     *  @param durationSeconds 通话时长（秒），只在 active 为 false 时写入
     *  @param cardMsgId       改写后的通话卡片消息 id；只在 active 为 false 且大于 0 时写入 msgid 与 content
     *  @param cardContent     改写后的卡片 content（JSON 文本）
     */
    static std::string buildStatusNotify(const GroupCallRoom& room, bool active, int64_t durationSeconds,
                                         int64_t cardMsgId, const std::string& cardContent);

    /**
     *  构造群通话卡片（msgtype=message_type_rtc(16)）的 content，形如
     *  {"kind":"groupcall","roomid":"...","media":1,"initiator":"5127","initiatorname":"张勇","starttime":"..."}。
     *  @param initiatorName 发起人显示名（UTF-8）
     */
    static std::string buildCardContent(const std::string& roomId, int media, int64_t initiatorId,
                                        const std::string& initiatorName, int64_t startTime);

    /**
     *  通话结束时改写卡片：在原 content 上加 endtime（Unix 毫秒，字符串）与 duration（秒，整数）。
     *  原 content 解析失败时按 room 重新生成（发起人名字取 initiatorName）再加这两项。
     *  @param oldContent      原卡片 content（JSON 文本）
     *  @param room            房间快照
     *  @param initiatorName   原 content 不可用时使用的发起人显示名
     *  @param endTime         结束时刻，Unix 毫秒
     *  @param durationSeconds 时长，单位秒；负数按 0
     */
    static std::string buildEndedCardContent(const std::string& oldContent, const GroupCallRoom& room,
                                             const std::string& initiatorName, int64_t endTime,
                                             int64_t durationSeconds);

    /**
     *  从卡片 content 里取出群通话的媒体类型（服务端生成会话列表摘要时用）。
     *  @return content 是群通话卡片时返回它的 media；解析失败或不是群通话卡片返回 0
     */
    static int cardMedia(const std::string& content);

private:
    GroupCallProtocol();
};
