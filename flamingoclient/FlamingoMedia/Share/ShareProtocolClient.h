/**
 *  v2 屏幕共享信令编解码（客户端侧），ShareProtocolClient.h
 *
 *  构造发往 chatserver 的屏幕共享请求 JSON（发起 / 加入 / 离开 / 结束 / 查询 / 成员媒体 / 设置权限 / 录制状态，
 *  cmd_screenshare_* 1720~1731），解析 chatserver 回来的应答与推送（通用应答、开始推送 1721、房间状态 1725、
 *  中转参数 1726、查询应答 1727、结束推送 1729），以及构造发往 imgserver 房间中转的报到包体（1859）、解析其应答。
 *
 *  本模块与服务端 chatserversrc/ScreenShareProtocol 互为镜像：服务端解析请求、构造应答与推送，客户端反之。
 *  64 位 userid 与会话 id 一律以字符串编码进 JSON；开关类字段一律用整数 0 / 1。字段与示例以
 *  docs/协议设计-v2.md 第 17 节为准。本模块为纯逻辑，只依赖 jsoncpp，便于单元测试，见
 *  flamingoclient/Source/Tests/ShareProtocolClientTests.cpp。
 *
 *  典型用法：
 *      std::string body = screenshare::ShareProtocolClient::buildStart(conversation_single, peerId, true, false, false);
 *      host->SendSignal(cmd_screenshare_start, body.data(), (uint32_t)body.size());
 *      ...
 *      screenshare::ShareStateNotify state;
 *      if (screenshare::ShareProtocolClient::parseStateNotify(json, state)) 刷新成员列表;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace screenshare {

//房间状态推送（cmd_screenshare_state_notify=1725）的事件类型，与服务端 screenshare_event 取值一致
enum share_state_event
{
    share_event_member_joined      = 1,   //有成员加入；actor 为加入者
    share_event_member_left        = 2,   //有成员离开（含观看者掉线）；actor 为离开者
    share_event_media_changed      = 3,   //有成员开关了麦克风、摄像头或电脑声音；actor 为该成员
    share_event_permission_changed = 4,   //发起人改了标注或录制权限；actor 为发起人
    share_event_muted_all          = 5,   //发起人执行了全员静音；actor 为发起人
    share_event_record_changed     = 6,   //有成员开始或停止录制（含收回权限导致的停止）
    share_event_host_offline       = 7,   //发起人掉线，房间进入宽限期
    share_event_host_back          = 8,   //发起人在宽限期内重新上线
    share_event_member_muted       = 9,   //发起人单独关掉了某人的麦克风（第 13 期）；actor 为发起人，target 为被静音的人
    share_event_member_removed     = 10,  //发起人移出了某个观看者（第 14 期）；actor 为发起人，target 为被移出的人
    share_event_control_granted    = 11,  //画面主人把控制权交给了某人（第 18 期）；actor 为画面主人，target 为控制者
    share_event_sharing_changed    = 13,  //正在共享屏幕的人变了（第 22 期）：主持人同意了申请、有人停止共享、主持人恢复自己的画面，
                                          //以及申请、撤回、拒绝（名单上的「申请中」）；actor 为发起动作的人，target 为相关的人
    share_event_host_changed       = 14,  //主持人移交（第 22 期）：actor 为原主持人（已离开房间），target 为新主持人
    share_event_cloudrecord_changed = 15, //云端录制开始、暂停、继续或停止（第 26 期）：actor 为主持人；imgserver 出错等被动结束时 actor 为 0，
                                          //并带 cloudrecordcode（738 云端存储不可用）。当前状态见 ShareStateNotify 的 m_cloudRecord*
    share_event_control_revoked    = 12,  //控制结束（第 18 期）：画面主人收回或控制者自己结束；actor 为发起动作的人，target 为原控制者
};

//控制键鼠请求（1735）与推送（1736）的动作（第 18 期），与服务端 screenshare_control_action 取值一致
enum share_control_action
{
    share_control_request = 1,   //观看者申请控制某块画面；服务端经 1736 转给画面主人
    share_control_cancel  = 2,   //申请人撤回还没得到答复的申请；画面主人据此关掉申请提示
    share_control_grant   = 3,   //画面主人把控制权交给某人（同意申请或在名单里主动授予）
    share_control_deny    = 4,   //画面主人拒绝申请；服务端经 1736 转给申请人
    share_control_revoke  = 5,   //结束控制：画面主人收回，或控制者自己结束
};

//共享结束推送（cmd_screenshare_ended_notify=1729）的结束原因，与服务端 screenshare_end_reason 取值一致
enum share_end_reason
{
    share_end_host_stopped = 1,   //发起人主动结束（含发起人离开房间）
    share_end_host_timeout = 2,   //发起人掉线后超过宽限期未回来，服务端解散房间
    share_end_removed        = 3, //本人被发起人移出，共享仍在进行、可以再次加入（第 14 期；只发给被移出的人）
    share_end_removed_banned = 4, //本人被发起人移出，且发起人不允许再次加入这场共享（第 14 期；只发给被移出的人）
};

//设置权限请求里的权限取值：允许 / 收回 / 不变（省略该字段）
enum share_permission_value
{
    share_perm_unchanged = -1,   //不改这一项，请求里不带该字段
    share_perm_revoke    = 0,    //收回
    share_perm_allow     = 1,    //允许
};

//房间里的一名成员（来自 1725 的 members 数组）
struct ShareMember
{
    int64_t m_userId;        //成员 userid
    int     m_seat;          //座位号：发起人 0，观看者 1~254
    bool    m_mic;           //是否开着麦克风
    bool    m_camera;        //是否开着摄像头
    bool    m_systemAudio;   //是否在共享电脑声音
    bool    m_recording;     //是否正在录制
    std::string m_p2pSessionId;   //单聊共享里服务端为这名观看者与发起人建的 P2P 会话号（P2P 方案第 6 期）；没建时为空
    int64_t m_controlledBy;  //正在控制此人屏幕的人的 userid（第 18 期）；没人控制或老服务端不带时为 0
    bool    m_sharing;       //是否正在共享自己的屏幕（第 22 期）；老服务端不带时只有主持人为 true
    int     m_screenSeat;    //这块画面在中转里的发送座位（第 22 期），观看端按它认出一路屏幕画面是谁的；不共享时为 -1
    bool    m_requesting;    //是否申请了共享、等主持人处理（第 22 期）
    bool    m_boardRequesting;   //是否申请了新建白板、等主持人处理（第 23 期）
    bool    m_viaLink;           //是否凭分享链接加入、不是该会话的成员（第 25 期链接访客）；老服务端不带时为 false

    ShareMember();
};

//通用应答：发起、加入、离开、结束、成员媒体、设置权限、录制状态的应答都是这个结构
struct ShareResult
{
    int         m_code;     //错误码，0 为成功
    std::string m_msg;      //说明文字
    std::string m_roomId;   //房间号；失败且没有房间号时为空

    ShareResult();
};

//有人开始共享（cmd_screenshare_started_notify=1721）
struct ShareStartedNotify
{
    std::string m_roomId;      //房间号
    int         m_ctype;       //会话类型：conversation_single / conversation_group
    int64_t     m_cid;         //从本人角度看的会话 id：单聊为发起人 userid，群聊为群 id
    int64_t     m_hostId;      //发起人 userid
    std::string m_hostName;    //发起人显示名（UTF-8）
    int64_t     m_startTime;   //发起时间，Unix 毫秒
    bool        m_locked;      //加入是否需要密码（第 12 期）；老服务端不带该字段时为 false
    bool        m_forced;      //是否强制模式（第 13 期）；老服务端不带该字段时为 false
    bool        m_rejoin;      //是否是强制模式下掉线后重新登录、服务端让本人回到观看的推送（第 13 期），客户端直接加入
    std::string m_title;       //共享标题（第 15 期，UTF-8）；老服务端不带时为空
    std::string m_desc;        //共享描述（第 15 期，UTF-8）；没填或老服务端不带时为空

    ShareStartedNotify();
};

//房间里的一块白板（第 23 期）：只有笔迹、没有视频的共享画面
struct ShareWhiteboardInfo
{
    int     m_seat;      //白板的画面座位（标注按它区分画面）
    int64_t m_ownerId;   //建立者 userid

    ShareWhiteboardInfo();
};

//房间状态（cmd_screenshare_state_notify=1725），附事件发生后的完整快照
struct ShareStateNotify
{
    std::string              m_roomId;            //房间号
    int                      m_event;             //事件类型，取值见 enum share_state_event
    int64_t                  m_actor;             //触发事件的成员 userid
    int64_t                  m_hostId;            //发起人 userid
    bool                     m_hostOnline;        //发起人是否在线（false 表示房间处于宽限期）
    bool                     m_annotateAllowed;   //观看者能否标注
    bool                     m_recordAllowed;     //观看者能否录制
    std::vector<ShareMember> m_members;           //全部成员，含发起人
    bool                     m_forced;            //是否强制模式（第 13 期）
    int64_t                  m_target;            //事件针对的成员（第 13 期，事件 9 为被静音的人）；没有时为 0
    std::string              m_title;             //共享标题（第 15 期，UTF-8）；老服务端不带时为空
    std::string              m_desc;              //共享描述（第 15 期，UTF-8）；没填或老服务端不带时为空
    int                      m_maxSharers;        //同时共享屏幕的人数上限（第 22 期）；老服务端不带时为 1
    std::vector<ShareWhiteboardInfo> m_whiteboards;   //白板（第 23 期），按建立的先后；没有或老服务端不带时为空
    int64_t                  m_cloudRecordId;     //进行中的云端录制的录像 id（第 26 期）；没有在录或老服务端时为 0
    int64_t                  m_cloudRecordOwner;  //开启这次云端录制的人；没有在录时为 0
    bool                     m_cloudRecordPaused; //云端录制是否暂停中
    int64_t                  m_cloudRecordSince;  //这次云端录制开始的时刻，Unix 毫秒
    int                      m_cloudRecordCode;   //云端录制被动结束的原因（只随事件 15 下发）：738 云端存储不可用；没有时为 0

    ShareStateNotify();

    //查某个成员；不在房间里返回 NULL（指针指向本对象内部，随本对象销毁失效）
    const ShareMember* findMember(int64_t userId) const;
};

//房间中转参数（cmd_screenshare_relay_info=1726），只发给本人
struct ShareRelayInfo
{
    std::string m_roomId;       //房间号
    std::string m_relayIp;      //imgserver 中转地址
    int         m_relayPort;    //imgserver 中转端口
    std::string m_token;        //relay 令牌，连上中转后经 1859 报到
    int         m_seat;         //本人的座位号
    int         m_maxCameras;   //同时开摄像头的人数上限
    int64_t     m_elapsedMs;    //服务端发出时共享已进行的毫秒数（第 13 期），据此显示共享时长；老服务端没有时为 0
    std::string m_p2pSessionId; //单聊共享里服务端为本人（观看者）与发起人建的 P2P 会话号（P2P 方案第 6 期）；没建时为空

    ShareRelayInfo();
};

//查询应答（cmd_screenshare_query=1727）
struct ShareQueryResult
{
    int         m_code;          //错误码，0 为成功
    std::string m_msg;           //说明文字
    bool        m_exists;        //该会话是否有进行中的共享
    std::string m_roomId;        //房间号（m_exists 为 true 时有效）
    int64_t     m_hostId;        //发起人 userid（同上）
    int         m_memberCount;   //当前成员数，含发起人（同上）
    int64_t     m_startTime;     //发起时间，Unix 毫秒（同上）
    bool        m_locked;        //加入是否需要密码（第 12 期，同上）；没有进行中的共享时为 false
    bool        m_forced;        //是否强制模式（第 13 期，同上）；没有进行中的共享时为 false
    std::string m_title;         //共享标题（第 15 期，UTF-8，同上）；没有进行中的共享或老服务端时为空
    std::string m_desc;          //共享描述（第 15 期，UTF-8，同上）

    ShareQueryResult();
};

//共享已结束（cmd_screenshare_ended_notify=1729）
struct ShareEndedNotify
{
    std::string m_roomId;            //房间号
    int         m_ctype;             //会话类型
    int64_t     m_cid;               //从本人角度看的会话 id（同 1721）
    int64_t     m_hostId;            //发起人 userid
    int64_t     m_durationSeconds;   //共享时长，单位秒
    int         m_reason;            //结束原因，取值见 enum share_end_reason

    ShareEndedNotify();
};

//共享标题与描述变了（cmd_screenshare_info_notify=1734，第 15 期）
struct ShareInfoNotify
{
    std::string m_roomId;        //房间号
    int         m_ctype;         //会话类型
    int64_t     m_cid;           //从本人角度看的会话 id（同 1721）
    std::string m_title;         //新的共享标题（UTF-8）
    std::string m_desc;          //新的共享描述（UTF-8）；没填时为空
    int64_t     m_cardMsgId;     //会话里那张共享卡片的消息 id；服务端没能改写卡片时为 0
    std::string m_cardContent;   //改写后的卡片 content（JSON 文本）；m_cardMsgId 为 0 时为空

    ShareInfoNotify();
};

//控制键鼠推送（cmd_screenshare_control_notify=1736，第 18 期）：申请、撤回发给画面主人，拒绝发给申请人
struct ShareControlNotify
{
    std::string m_roomId;      //房间号
    int         m_action;      //动作，取值见 enum share_control_action（只会是申请、撤回、拒绝）
    int64_t     m_owner;       //画面主人 userid
    int64_t     m_actor;       //发起动作的人 userid：申请、撤回时为申请人，拒绝时为画面主人
    std::string m_actorName;   //发起动作的人的昵称（UTF-8）

    ShareControlNotify();
};

//多人共享的动作（第 22 期，1737 / 1738 的 action 字段，与服务端 screenshare_sharing_action 取值一致）
enum share_sharing_action
{
    share_sharing_request = 1,   //观看者申请共享自己的屏幕，服务端经 1738 转给主持人；主持人停掉自己的画面后再申请直接恢复
    share_sharing_cancel  = 2,   //申请人撤回还没得到答复的申请，服务端经 1738 转给主持人
    share_sharing_approve = 3,   //主持人同意：被同意的人收到带「只推流」令牌与画面座位的 1738，另连一条中转连接推屏幕
    share_sharing_reject  = 4,   //主持人拒绝，服务端经 1738 转给申请人
    share_sharing_stop    = 5,   //停止共享：共享者停自己的，主持人可停任何人的（被停的人收到 1738）
};

//1737 / 1738 共享的是什么（第 23 期，kind 字段；不带时为屏幕）
enum share_sharing_kind
{
    share_kind_screen     = 1,   //共享自己的屏幕（第 22 期）
    share_kind_whiteboard = 2,   //新建白板（第 23 期）：同意时 1738 带白板的画面座位、不带令牌；关闭时 target 为白板的建立者
};

//多人共享的推送（cmd_screenshare_sharing_notify=1738，第 22 期）
struct ShareSharingNotify
{
    std::string m_roomId;      //房间号
    int         m_action;      //动作，取值见 enum share_sharing_action
    int64_t     m_actor;       //发起动作的人：申请、撤回时为申请人，同意、拒绝、停止时为主持人
    std::string m_actorName;   //发起动作的人的昵称（UTF-8）
    std::string m_token;       //同意时的「只推流」中转令牌；其余为空
    std::string m_relayIp;     //同意时的中转地址
    int         m_relayPort;   //同意时的中转端口
    int         m_screenSeat;  //同意时本人这块画面的座位（白板为白板的座位）；其余为 -1
    int         m_kind;        //共享的是什么（第 23 期），取值见 enum share_sharing_kind；不带时为屏幕

    ShareSharingNotify();

    //是否带了推流所需的中转参数（同意时才有）
    bool HasPublishInfo() const;
};

//分享链接（1752）的动作，与服务端 screenshare_link_action 一致（第 25 期）
enum share_link_action
{
    share_link_get            = 1,   //取得当前链接，没有就生成
    share_link_reset          = 2,   //重置：旧链接作废，生成新链接
    share_link_disable        = 3,   //停用：链接作废，直到再次取得
    share_link_set_nopassword = 4,   //设置「点链接加入免输密码」
};

//凭链接码查到的链接状态（1753 的 status），与服务端 screenshare_link_status 一致（第 25 期）
enum share_link_status
{
    share_link_status_invalid = 0,   //无效（应答为错误码 733 时）
    share_link_status_active  = 1,   //共享正在进行
    share_link_status_ended   = 2,   //共享已经结束
};

//分享链接请求（1752）的应答（第 25 期）
struct ShareLinkResult
{
    int         m_code;         //错误码，0 为成功
    std::string m_roomId;       //房间号
    std::string m_linkCode;     //链接码；停用后为空
    bool        m_noPassword;   //是否打开了「点链接加入免输密码」

    ShareLinkResult();
};

//共享中云端录制（1754）的应答（第 26 期）
struct ShareCloudRecordResult
{
    int         m_code;       //错误码，0 为成功；736 没开通、737 已在录或没在录、738 云端存储不可用、604 配额已满、716 不是主持人
    std::string m_roomId;     //房间号
    int         m_action;     //动作，取值见 Protocol.h 的 cloudrecord_control_action
    int64_t     m_recordId;   //录像 id；失败或老服务端时为 0

    ShareCloudRecordResult();
};

//凭链接码查共享概要（1753）的应答（第 25 期）
struct ShareLinkSummary
{
    int         m_code;       //错误码，0 为成功；733 链接无效、734 不同企业、735 太频繁
    int         m_status;     //链接状态，取值见 share_link_status（成功时才有意义）
    std::string m_roomId;     //房间号（进行中时才有）
    std::string m_title;      //共享标题（UTF-8）
    std::string m_desc;       //共享描述（UTF-8），没填时为空
    int64_t     m_hostId;     //发起人 userid
    std::string m_hostName;   //发起人显示名（UTF-8）
    bool        m_locked;     //加入是否要密码
    bool        m_forced;     //是否强制模式
    bool        m_member;     //本人是否是该会话的成员
    int         m_ctype;      //会话类型；不是会话成员时为 0
    int64_t     m_cid;        //从本人角度看的会话 id；不是会话成员时为 0

    ShareLinkSummary();
};

//屏幕共享信令编解码工具类；全部为静态方法，不可实例化
class ShareProtocolClient
{
public:
    //==== 构造 客户端→服务端 的请求 ====

    /**
     *  发起共享（cmd_screenshare_start=1720），形如 {"ctype":1,"cid":"5308","mic":1,"camera":0,"sysaudio":0}。
     *  @param ctype    会话类型：conversation_single / conversation_group
     *  @param cid      会话 id：单聊为对端 userid，群聊为群 id
     *  @param mic      是否同时开启麦克风
     *  @param camera   是否同时开启摄像头
     *  @param sysAudio 是否同时共享电脑声音
     *  @param password 加入密码（第 12 期，6 位 ASCII 数字）；为空表示不设密码，此时请求里不带 password 字段
     *  @param forced   是否强制模式（第 13 期）；为 false 时不带 forced 字段
     *  @param recordAllowed 观看者能否录制（第 13 期「禁止观看者录制」取反）；为 true 时不带 recordallowed 字段
     *  @param title    共享标题（第 15 期，UTF-8，必填）；title 与 desc 两个字段总是带上
     *  @param desc     共享描述（第 15 期，UTF-8，选填）；没填时为空串
     */
    static std::string buildStart(int ctype, int64_t cid, bool mic, bool camera, bool sysAudio,
                                  const std::string& password = std::string(), bool forced = false,
                                  bool recordAllowed = true, const std::string& title = std::string(),
                                  const std::string& desc = std::string());

    /**
     *  只带房间号的请求：加入（1722）、离开（1723）、结束（1724），形如 {"roomid":"..."}。
     */
    static std::string buildRoomRequest(const std::string& roomId);

    /**
     *  加入共享（cmd_screenshare_join=1722），形如 {"roomid":"...","password":"482915"}（第 12 期）。
     *  @param roomId   房间号
     *  @param password 用户输入的加入密码；为空时不带 password 字段，与 buildRoomRequest 的结果相同
     */
    static std::string buildJoin(const std::string& roomId, const std::string& password);

    /**
     *  凭分享链接加入（1722，第 25 期）：在 buildJoin 的基础上带 linkcode。
     *  @param linkCode 链接码；为空时与 buildJoin(roomId, password) 相同
     */
    static std::string buildJoin(const std::string& roomId, const std::string& password, const std::string& linkCode);

    /**
     *  分享链接请求（cmd_screenshare_link=1752，第 25 期），形如 {"roomid":"...","action":1}；设置免输密码时带 nopassword。
     *  @param action     取值见 share_link_action
     *  @param noPassword 是否免输密码（只在 action 为 share_link_set_nopassword 时写入）
     */
    static std::string buildLink(const std::string& roomId, int action, bool noPassword);

    //凭链接码查共享概要（cmd_screenshare_link_resolve=1753，第 25 期），形如 {"linkcode":"..."}
    static std::string buildLinkResolve(const std::string& linkCode);

    //分享链接请求的应答（1752）；没有 code 时返回 false
    static bool parseLinkResult(const std::string& json, ShareLinkResult& out);

    //凭链接码查共享概要的应答（1753）；没有 code 时返回 false
    static bool parseLinkSummary(const std::string& json, ShareLinkSummary& out);

    /**
     *  共享中云端录制的请求（1754，第 26 期）：{"roomid":"...","action":1}。
     *  @param action 取值见 Protocol.h 的 cloudrecord_control_action
     */
    static std::string buildCloudRecordControl(const std::string& roomId, int action);

    //解析 1754 的应答；不是 JSON 或没有 code 时返回 false
    static bool parseCloudRecordResult(const std::string& json, ShareCloudRecordResult& out);

    /**
     *  查询某会话有无进行中的共享（cmd_screenshare_query=1727），形如 {"ctype":2,"cid":"600001"}。
     */
    static std::string buildQuery(int ctype, int64_t cid);

    /**
     *  上报本人的媒体开关（cmd_screenshare_member_media=1728），三项都给出。
     */
    static std::string buildMemberMedia(const std::string& roomId, bool mic, bool camera, bool sysAudio);

    /**
     *  发起人设置权限或全员静音（cmd_screenshare_set_permission=1730）。
     *  @param annotate 标注权限，取值见 enum share_permission_value；share_perm_unchanged 时不带该字段
     *  @param record   录制权限，同上
     *  @param muteAll  是否执行全员静音；false 时不带该字段
     *  @param muteUser 要单独静音的观看者 userid（第 13 期）；0 表示不带该字段
     */
    static std::string buildSetPermission(const std::string& roomId, int annotate, int record, bool muteAll,
                                          int64_t muteUser = 0);

    /**
     *  开始 / 停止录制（cmd_screenshare_record_state=1731），形如 {"roomid":"...","recording":1}。
     */
    static std::string buildRecordState(const std::string& roomId, bool recording);

    /**
     *  发起人移出某个观看者（cmd_screenshare_remove_member=1732，第 14 期），形如
     *  {"roomid":"...","userid":"5308","allowrejoin":1}。allowrejoin 总是带上，不依赖服务端的缺省值。
     *  @param userId      要移出的观看者 userid
     *  @param allowRejoin 是否允许此人再次加入（确认框里「允许他再次加入」勾选项，默认勾选）
     */
    static std::string buildRemoveMember(const std::string& roomId, int64_t userId, bool allowRejoin);

    /**
     *  发起人修改共享标题与描述（cmd_screenshare_set_info=1733，第 15 期），形如 {"roomid":"...","title":"...","desc":"..."}。
     *  @param title 新标题（UTF-8，必填）
     *  @param desc  新描述（UTF-8）；为空串表示清空描述，字段照样带上
     */
    static std::string buildSetInfo(const std::string& roomId, const std::string& title, const std::string& desc);

    /**
     *  控制键鼠（cmd_screenshare_control=1735，第 18 期），形如 {"roomid":"...","action":3,"owner":"5127","target":"5308"}。
     *  @param action 动作，取值见 enum share_control_action
     *  @param owner  画面主人 userid（被控制的是谁的屏幕）
     *  @param target 授予、拒绝的对象 userid；为 0 时不带 target 字段（申请、撤回、收回）
     */
    static std::string buildControl(const std::string& roomId, int action, int64_t owner, int64_t target);

    /**
     *  多人共享（cmd_screenshare_sharing=1737，第 22 期），形如 {"roomid":"ss-1","action":3,"target":"5308"}。
     *  @param action 动作，取值见 enum share_sharing_action
     *  @param target 同意、拒绝的对象，主持人停别人的共享时的对象（白板为建立者）；为 0 时不带 target 字段
     *  @param kind   共享的是什么（第 23 期），取值见 enum share_sharing_kind；屏幕时不带 kind 字段（与第 22 期相同）
     */
    static std::string buildSharing(const std::string& roomId, int action, int64_t target, int kind = share_kind_screen);

    /**
     *  移交主持人（cmd_screenshare_transfer_host=1739，第 22 期），形如 {"roomid":"ss-1","target":"5308"}。
     *  @param target 新主持人，须正在共享屏幕
     */
    static std::string buildTransferHost(const std::string& roomId, int64_t target);

    /**
     *  房间中转报到（cmd_screenshare_relay_hello=1859）的包体，形如 {"token":"..."}。
     */
    static std::string buildRelayHello(const std::string& token);

    //==== 解析 服务端→客户端 的应答与推送 ====

    //通用应答；JSON 非法或缺 code 返回 false
    static bool parseResult(const std::string& json, ShareResult& out);

    //开始推送 1721；缺 roomid / host 返回 false
    static bool parseStartedNotify(const std::string& json, ShareStartedNotify& out);

    //房间状态 1725；缺 roomid / event 或 members 不是数组返回 false
    static bool parseStateNotify(const std::string& json, ShareStateNotify& out);

    //中转参数 1726；缺 roomid / token / relayport 返回 false
    static bool parseRelayInfo(const std::string& json, ShareRelayInfo& out);

    //查询应答 1727；缺 code 返回 false
    static bool parseQueryResult(const std::string& json, ShareQueryResult& out);

    //结束推送 1729；缺 roomid 返回 false
    static bool parseEndedNotify(const std::string& json, ShareEndedNotify& out);

    //标题与描述变了 1734（第 15 期）；缺 roomid 返回 false
    static bool parseInfoNotify(const std::string& json, ShareInfoNotify& out);

    //控制键鼠推送 1736（第 18 期）；缺 roomid / owner、动作不在 1~5 之间返回 false
    static bool parseControlNotify(const std::string& json, ShareControlNotify& out);

    //多人共享的推送（1738，第 22 期）；动作越界、缺房间号时返回 false
    static bool parseSharingNotify(const std::string& json, ShareSharingNotify& out);

    /**
     *  房间中转报到应答（1859），形如 {"code":0,"msg":"ok"}。
     *  @param code [出参] 错误码
     *  @return JSON 合法且带 code 返回 true
     */
    static bool parseRelayHelloAck(const std::string& json, int& code);

private:
    ShareProtocolClient();
};

} // namespace screenshare
