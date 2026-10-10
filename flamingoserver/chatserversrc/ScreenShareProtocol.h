/**
 *  v2 屏幕共享信令编解码（服务端侧），ScreenShareProtocol.h
 *
 *  解析客户端发来的屏幕共享请求（cmd_screenshare_* 1720~1731），构造回给客户端的应答与推送。客户端的镜像实现
 *  在 FlamingoMedia.dll 里（第 2c 步）。64 位 userid 与会话 id 一律以字符串编码进 JSON，避免大整数精度丢失；
 *  开关类字段一律用整数 0 / 1。请求与应答的完整示例见 docs/协议设计-v2.md 的屏幕共享一节。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp，不触及网络与数据库，便于单元测试，见 tests/test_screenshareprotocol.cpp
 *  （Suite SCP）。
 *
 *  典型用法（ChatSession 的处理函数里）：
 *      int ctype = 0; int64_t cid = 0; bool mic = false, camera = false, sysAudio = false;
 *      if (!ScreenShareProtocol::parseStartRequest(body, ctype, cid, mic, camera, sysAudio))
 *          { send(cmd_screenshare_start, seq, ScreenShareProtocol::buildResult(error_code_bad_request, "bad request", "")); }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include "ScreenShareManager.h"

//cmd_screenshare_state_notify 的事件类型（event 字段），告诉房间成员这一帧推送是因为什么而发
enum screenshare_event
{
    screenshare_event_member_joined      = 1,   //有成员加入；actor 为加入者
    screenshare_event_member_left        = 2,   //有成员离开（含观看者掉线）；actor 为离开者
    screenshare_event_media_changed      = 3,   //有成员开关了麦克风、摄像头或电脑声音；actor 为该成员
    screenshare_event_permission_changed = 4,   //发起人改了标注或录制权限；actor 为发起人
    screenshare_event_muted_all          = 5,   //发起人执行了全员静音；actor 为发起人
    screenshare_event_record_changed     = 6,   //有成员开始或停止录制（含收回权限导致的停止）；actor 为该成员或发起人
    screenshare_event_host_offline       = 7,   //发起人掉线，房间进入宽限期；actor 为发起人
    screenshare_event_host_back          = 8,   //发起人在宽限期内重新上线；actor 为发起人
    screenshare_event_member_muted       = 9,   //发起人单独关掉了某人的麦克风（第 13 期）；actor 为发起人，target 为被静音的人
    screenshare_event_member_removed     = 10,  //发起人移出了某个观看者（第 14 期）；actor 为发起人，target 为被移出的人
    screenshare_event_control_granted    = 11,  //画面主人把控制权交给了某人（第 18 期）；actor 为画面主人，target 为控制者。
                                                //原先另有人在控制时由新控制者替换，快照里各成员的 controlledby 是替换之后的值
    screenshare_event_sharing_changed    = 13,  //正在共享屏幕的人变了（第 22 期）：主持人同意了申请、有人停止共享或主持人恢复自己的画面；
                                                //actor 为发起动作的人，target 为共享状态变了的人
    screenshare_event_host_changed       = 14,  //主持人移交（第 22 期）：actor 为原主持人（已离开房间），target 为新主持人
    screenshare_event_cloudrecord_changed = 15, //云端录制开始、暂停、继续或停止（第 26 期）：actor 为主持人；imgserver 出错等被动结束时
                                                //actor 为 0，并带 cloudrecordcode（738 云端存储不可用）。当前状态见快照的 cloudrecord
    screenshare_event_control_revoked    = 12,  //控制结束（第 18 期）：画面主人收回或控制者自己结束；actor 为发起动作的人，target 为原控制者
};

//cmd_screenshare_control（1735）与 cmd_screenshare_control_notify（1736）的动作（action 字段，第 18 期）
enum screenshare_control_action
{
    screenshare_control_request = 1,   //观看者申请控制某块画面；服务端经 1736 转给画面主人
    screenshare_control_cancel  = 2,   //申请人撤回还没得到答复的申请；服务端经 1736 转给画面主人，画面主人据此关掉申请提示
    screenshare_control_grant   = 3,   //画面主人把控制权交给某人（同意申请或在名单里主动授予）；服务端记录后经 1725 事件 11 广播
    screenshare_control_deny    = 4,   //画面主人拒绝某人的申请；服务端经 1736 转给申请人
    screenshare_control_revoke  = 5,   //结束控制：画面主人收回，或控制者自己结束；服务端记录后经 1725 事件 12 广播
};

//多人共享的动作（第 22 期，1737 的 action 字段）
enum screenshare_sharing_action
{
    screenshare_sharing_request = 1,   //观看者申请共享自己的屏幕，服务端经 1738 转给主持人；主持人停掉自己的画面后再申请时直接恢复
    screenshare_sharing_cancel  = 2,   //申请人撤回还没得到答复的申请，服务端经 1738 转给主持人
    screenshare_sharing_approve = 3,   //主持人同意某人的申请：服务端分配画面座位，经 1738 把「只推流」令牌发给他，并经 1725 事件 13 广播
    screenshare_sharing_reject  = 4,   //主持人拒绝某人的申请，服务端经 1738 转给申请人
    screenshare_sharing_stop    = 5,   //停止共享：共享者停自己的；主持人可停任何人的，被停的人另收到 1738；经 1725 事件 13 广播
};

//1737 / 1738 共享的是什么（第 23 期，kind 字段；不带时为屏幕）
enum screenshare_sharing_kind
{
    screenshare_kind_screen     = 1,   //共享自己的屏幕（第 22 期）
    screenshare_kind_whiteboard = 2,   //新建白板（第 23 期）：只有笔迹、没有视频的共享画面，占一个共享名额；关闭时 target 为白板的建立者
};

//分享链接（1752）的动作（第 25 期，docs/屏幕共享与录制方案.md 第 12.4 节）；只有主持人能操作
enum screenshare_link_action
{
    screenshare_link_get          = 1,   //取得当前链接，没有就生成；应答带链接码
    screenshare_link_reset        = 2,   //重置：旧链接码作废，生成新码（免输密码的设置沿用）
    screenshare_link_disable      = 3,   //停用：链接码作废且不再生成，直到再次取得
    screenshare_link_set_nopassword = 4, //设置「点链接加入免输密码」（请求带 nopassword 0 / 1）；没有链接时先生成
};

//凭链接码查到的共享概要（1753 的应答内容，第 25 期）
struct ScreenShareLinkSummary
{
    int         m_status;      //链接状态，取值见 screenshare_link_status（1 进行中、2 已结束）
    std::string m_roomId;      //房间号（进行中时才带）
    std::string m_title;       //共享标题（UTF-8，进行中时才带）
    std::string m_desc;        //共享描述（UTF-8，进行中时才带，没填为空）
    int64_t     m_hostId;      //发起人 userid（进行中时才带）
    std::string m_hostName;    //发起人显示名（UTF-8，进行中时才带）
    bool        m_locked;      //加入是否要密码：房间有密码、且链接没有打开免输密码
    bool        m_forced;      //是否强制模式
    bool        m_member;      //本人是否是该会话的成员
    int         m_ctype;       //会话类型；只对会话成员下发，否则为 0
    int64_t     m_cid;         //从本人角度看的会话 id；只对会话成员下发，否则为 0

    ScreenShareLinkSummary();
};

//发起共享请求（1720）解析出的全部字段（第 13 期起用它，避免参数越加越多）
struct ScreenShareStartRequest
{
    int         m_ctype;           //会话类型：conversation_single(1) / conversation_group(2)
    int64_t     m_cid;             //会话 id：单聊为对端 userid，群聊为群 id
    bool        m_mic;             //是否同时开启麦克风；缺省 false
    bool        m_camera;          //是否同时开启摄像头；缺省 false
    bool        m_sysAudio;        //是否同时共享电脑声音；缺省 false
    std::string m_password;        //加入密码（第 12 期）；没设为空
    bool        m_forced;          //是否强制模式（第 13 期）；缺省 false
    bool        m_recordAllowed;   //观看者能否录制（第 13 期，字段 recordallowed）；缺省 true
    std::string m_title;           //共享标题（第 15 期，UTF-8）；没带时为空，是否必填、长度由 ScreenShareRules::checkShareInfo 判断
    std::string m_desc;            //共享描述（第 15 期，UTF-8，选填）；没带时为空

    ScreenShareStartRequest();
};

//cmd_screenshare_ended_notify 的结束原因（reason 字段）
enum screenshare_end_reason
{
    screenshare_end_host_stopped = 1,   //发起人主动结束（含发起人离开房间）
    screenshare_end_host_timeout = 2,   //发起人掉线后超过宽限期未回来，服务端解散房间
    screenshare_end_removed        = 3, //本人被发起人移出，共享仍在进行、可以再次加入（第 14 期；只发给被移出的人）
    screenshare_end_removed_banned = 4, //本人被发起人移出，且发起人不允许再次加入这场共享（第 14 期；只发给被移出的人）
};

//屏幕共享信令编解码工具类；全部为静态方法，不可实例化
class ScreenShareProtocol
{
public:
    //==== 解析 客户端→服务端 的请求 ====

    /**
     *  解析发起共享请求（cmd_screenshare_start=1720），形如
     *  {"ctype":1,"cid":"100001","mic":1,"camera":0,"sysaudio":0}。
     *  @param ctype    [出参] 会话类型，只接受 conversation_single(1) / conversation_group(2)
     *  @param cid      [出参] 会话 id（字符串编码的 64 位整数，须 >0）
     *  @param mic      [出参] 是否同时开启麦克风；字段缺失按 false
     *  @param camera   [出参] 是否同时开启摄像头；字段缺失按 false
     *  @param sysAudio [出参] 是否同时共享电脑声音；字段缺失按 false
     *  @return JSON 合法、ctype 取值合法且 cid>0 返回 true
     */
    static bool parseStartRequest(const std::string& json, int& ctype, int64_t& cid, bool& mic, bool& camera,
                                  bool& sysAudio);

    /**
     *  同上，并解析可选的加入密码（第 12 期），形如 {...,"password":"482915"}。
     *  @param password [出参] 没带或为空串时为空；带了但不是 6 位数字时整个请求按非法处理（返回 false）
     *  @return 请求合法返回 true
     */
    static bool parseStartRequest(const std::string& json, int& ctype, int64_t& cid, bool& mic, bool& camera,
                                  bool& sysAudio, std::string& password);

    /**
     *  同上，另解析强制模式与禁止观看者录制（第 13 期），形如 {...,"forced":1,"recordallowed":0}；
     *  以及共享标题与描述（第 15 期），形如 {...,"title":"周会演示","desc":"第三季度汇报"}。
     *  @param out [出参] 解析结果
     *  @return 请求合法返回 true；forced / recordallowed 出现但不是整数、title / desc 出现但不是字符串时按非法处理
     */
    static bool parseStartRequest(const std::string& json, ScreenShareStartRequest& out);

    /**
     *  解析加入请求（1722），形如 {"roomid":"...","password":"482915"}，密码可选（第 12 期）。
     *  @param roomId   [出参] 房间号
     *  @param password [出参] 加入密码；没带时为空串
     *  @return JSON 合法、roomid 为非空字符串，且 password 若出现须为字符串时返回 true
     */
    static bool parseJoinRequest(const std::string& json, std::string& roomId, std::string& password);

    /**
     *  解析加入请求（1722），第 25 期起可带分享链接码：{"roomid":"...","password":"482915","linkcode":"Ab3dE7kQ9z"}。
     *  @param roomId   [出参] 房间号
     *  @param password [出参] 加入密码；没带时为空串
     *  @param linkCode [出参] 分享链接码；没带时为空串（格式由 ScreenShareLinkTable 判定，这里只取字符串）
     *  @return JSON 合法、roomid 为非空字符串，且 password、linkcode 若出现须为字符串时返回 true
     */
    static bool parseJoinRequest(const std::string& json, std::string& roomId, std::string& password,
                                 std::string& linkCode);

    /**
     *  解析分享链接请求（1752，第 25 期）：{"roomid":"...","action":1,"nopassword":1}，nopassword 只在 action 为 4 时有意义。
     *  @param roomId     [出参] 房间号
     *  @param action     [出参] 动作，取值见 screenshare_link_action
     *  @param noPassword [出参] 是否免输密码（没带时为 false）
     *  @return JSON 合法、roomid 为非空字符串、action 取值合法时返回 true
     */
    static bool parseLinkRequest(const std::string& json, std::string& roomId, int& action, bool& noPassword);

    /**
     *  构造分享链接的应答（1752）：{"code":0,"msg":"","roomid":"...","linkcode":"...","nopassword":0}；停用后 linkcode 为空。
     */
    static std::string buildLinkResponse(int code, const std::string& msg, const std::string& roomId,
                                         const std::string& linkCode, bool noPassword);

    /**
     *  解析凭链接码查共享的请求（1753，第 25 期）：{"linkcode":"Ab3dE7kQ9z"}。
     *  @param linkCode [出参] 链接码
     *  @return JSON 合法且 linkcode 为非空字符串返回 true
     */
    static bool parseLinkResolveRequest(const std::string& json, std::string& linkCode);

    /**
     *  构造凭链接码查共享的应答（1753）：成功时带 status；进行中时再带房间号、标题、描述、发起人、locked、forced、member，
     *  会话成员另带 ctype 与 cid。失败时只有 code 与 msg。
     */
    static std::string buildLinkResolveResponse(int code, const std::string& msg, const ScreenShareLinkSummary& summary);

    /**
     *  解析只带房间号的请求：加入（1722）、离开（1723）、结束（1724），形如 {"roomid":"..."}。
     *  @param roomId [出参] 房间号
     *  @return JSON 合法且 roomid 为非空字符串返回 true
     */
    static bool parseRoomRequest(const std::string& json, std::string& roomId);

    /**
     *  解析查询请求（cmd_screenshare_query=1727），形如 {"ctype":2,"cid":"600001"}。
     *  @return JSON 合法、ctype 取值合法且 cid>0 返回 true
     */
    static bool parseQueryRequest(const std::string& json, int& ctype, int64_t& cid);

    /**
     *  解析成员媒体开关请求（cmd_screenshare_member_media=1728），形如
     *  {"roomid":"...","mic":1,"camera":0,"sysaudio":0}。三项须全部给出（客户端每次上报完整状态）。
     *  @return JSON 合法、roomid 非空且三项都是整数时返回 true
     */
    static bool parseMemberMediaRequest(const std::string& json, std::string& roomId, bool& mic, bool& camera,
                                        bool& sysAudio);

    /**
     *  解析设置权限请求（cmd_screenshare_set_permission=1730），形如
     *  {"roomid":"...","annotate":0,"record":1,"muteall":0}。annotate、record 可省略（省略即不变），
     *  muteall 可省略（省略按 0）；三项至少要有一项表达了操作。
     *  @param annotate [出参] 1 允许，0 收回，-1 不变
     *  @param record   [出参] 1 允许，0 收回，-1 不变
     *  @param muteAll  [出参] 是否执行全员静音
     *  @return JSON 合法、roomid 非空且至少有一项操作时返回 true
     */
    static bool parsePermissionRequest(const std::string& json, std::string& roomId, int& annotate, int& record,
                                       bool& muteAll);

    /**
     *  同上，另解析按人静音（第 13 期），形如 {"roomid":"...","muteuser":"5308"}；muteuser 也算一项操作。
     *  @param muteUser [出参] 要静音的观看者 userid；没带时为 0
     *  @return 请求合法返回 true；muteuser 出现但不是大于 0 的数字字符串时按非法处理
     */
    static bool parsePermissionRequest(const std::string& json, std::string& roomId, int& annotate, int& record,
                                       bool& muteAll, int64_t& muteUser);

    /**
     *  解析录制状态请求（cmd_screenshare_record_state=1731），形如 {"roomid":"...","recording":1}。
     *  @return JSON 合法、roomid 非空且 recording 是整数时返回 true
     */
    static bool parseRecordStateRequest(const std::string& json, std::string& roomId, bool& recording);

    /**
     *  解析移出观看者请求（cmd_screenshare_remove_member=1732，第 14 期），形如
     *  {"roomid":"...","userid":"5308","allowrejoin":0}。
     *  @param userId      [出参] 要移出的观看者 userid（字符串编码的 64 位整数，须 >0）
     *  @param allowRejoin [出参] 是否允许此人再次加入；字段缺失按 true
     *  @return JSON 合法、roomid 非空、userid 合法，且 allowrejoin 若出现须为整数时返回 true
     */
    static bool parseRemoveMemberRequest(const std::string& json, std::string& roomId, int64_t& userId,
                                         bool& allowRejoin);

    /**
     *  解析修改共享标题与描述的请求（cmd_screenshare_set_info=1733，第 15 期），形如
     *  {"roomid":"...","title":"周会演示","desc":"第三季度汇报"}。
     *  @param title [出参] 标题；必须带上且是字符串（是否为空、长度、敏感词由 ScreenShareRules::checkShareInfo 判断）
     *  @param desc  [出参] 描述；没带时为空串，带了必须是字符串
     *  @return JSON 合法、roomid 非空、title 是字符串、desc 若出现也是字符串时返回 true
     */
    static bool parseSetInfoRequest(const std::string& json, std::string& roomId, std::string& title,
                                    std::string& desc);

    /**
     *  解析控制键鼠请求（cmd_screenshare_control=1735，第 18 期），形如
     *  {"roomid":"...","action":3,"owner":"5301","target":"5308"}。
     *  @param action [出参] 动作，取值见 enum screenshare_control_action
     *  @param owner  [出参] 画面主人 userid（被控制的是谁的屏幕），须 >0
     *  @param target [出参] 动作针对的人：授予、拒绝时为控制者或申请人，须 >0；申请、撤回、收回时不用，没带为 0
     *  @return JSON 合法、roomid 非空、action 在 1~5 之间、owner 合法，授予与拒绝另带合法 target 时返回 true
     */
    /**
     *  解析多人共享的请求（1737，第 22 期）：{"roomid", "action", "target"}。
     *  @param action [出参] 动作，取值见 enum screenshare_sharing_action
     *  @param target [出参] 同意、拒绝必须带；停止时可选（不带表示停自己的）；申请、撤回不用，一律回 0
     *  @return 格式正确返回 true
     */
    static bool parseSharingRequest(const std::string& json, std::string& roomId, int& action, int64_t& target);

    /**
     *  解析移交主持人的请求（1739，第 22 期）：{"roomid", "target"}。
     *  @return 格式正确且带了对象返回 true
     */
    static bool parseTransferHostRequest(const std::string& json, std::string& roomId, int64_t& target);

    /**
     *  取 1737 请求共享的是什么（第 23 期）。
     *  @return 取值见 enum screenshare_sharing_kind；不带 kind、或值不认识时为 screenshare_kind_screen
     */
    static int parseSharingKind(const std::string& json);

    /**
     *  组白板的 1738 推送（第 23 期）：{"roomid","kind":2,"action","actor","actorname","screenseat"}。
     *  @param action    动作：申请、撤回发给主持人，同意（含主持人自己直接新建）、拒绝、关闭发给当事人
     *  @param boardSeat 新建的白板的画面座位（同意时）；其余动作传 -1，不带 screenseat
     */
    static std::string buildBoardNotify(const std::string& roomId, int action, int64_t actor, const std::string& actorName,
                                        int boardSeat);

    /**
     *  组多人共享的推送（1738，第 22 期）。
     *  @param action    动作：申请、撤回发给主持人，同意、拒绝、停止发给当事人
     *  @param actor     发起动作的人
     *  @param actorName 发起动作的人的昵称（UTF-8）
     *  @param token     同意时的「只推流」令牌；其余动作为空、不带中转字段
     *  @param relayIp、relayPort、screenSeat 同意时的中转地址与画面座位
     */
    static std::string buildSharingNotify(const std::string& roomId, int action, int64_t actor, const std::string& actorName,
                                          const std::string& token = std::string(), const std::string& relayIp = std::string(),
                                          int relayPort = 0, int screenSeat = -1);

    static bool parseControlRequest(const std::string& json, std::string& roomId, int& action, int64_t& owner,
                                    int64_t& target);

    //==== 构造 服务端→客户端 的应答与推送 ====

    /**
     *  构造通用应答：发起（1720）、加入、离开、结束、成员媒体、设置权限、录制状态的应答都用它，
     *  形如 {"code":0,"msg":"ok","roomid":"..."}。
     *  @param roomId 房间号；失败且无房间号时传空串，此时不带 roomid 字段
     */
    static std::string buildResult(int code, const std::string& msg, const std::string& roomId);

    /**
     *  构造「有人开始共享」推送（cmd_screenshare_started_notify=1721），发给会话里的其他人，形如
     *  {"roomid":"...","ctype":1,"cid":"...","host":"...","hostname":"...","starttime":"..."}。
     *  @param cid       从接收者角度看的会话 id：单聊为发起人 userid，群聊为群 id
     *  @param hostName  发起人显示名（UTF-8）
     *  @param startTime 发起时间，Unix 毫秒
     *  @param locked    加入是否需要密码（第 12 期）
     *  @param forced    是否强制模式（第 13 期）
     *  @param rejoin    是否是让强制模式下掉线的观看者回到观看的推送（第 13 期），客户端据此直接加入、不弹确认
     *  @param title     共享标题（第 15 期，UTF-8）
     *  @param desc      共享描述（第 15 期，UTF-8）；没填时为空串，字段照样带上
     */
    static std::string buildStartedNotify(const std::string& roomId, int ctype, int64_t cid, int64_t hostId,
                                          const std::string& hostName, int64_t startTime, bool locked = false,
                                          bool forced = false, bool rejoin = false,
                                          const std::string& title = std::string(),
                                          const std::string& desc = std::string());

    /**
     *  构造房间状态推送（cmd_screenshare_state_notify=1725），发给房间里的全体成员。附带完整的房间快照，
     *  客户端据此整体刷新成员列表，不必自己累计增量。形如
     *  {"roomid":"...","event":1,"actor":"...","host":"...","hostonline":1,"annotateallowed":1,
     *   "recordallowed":1,"members":[{"uid":"...","seat":0,"mic":1,"camera":0,"sysaudio":0,"recording":0},...]}。
     *  另带 forced（第 13 期）、title 与 desc（第 15 期）；target 大于 0 时带 target（事件 9 被静音的人）。
     *  成员项里有人正在控制此人的屏幕时另带 controlledby（第 18 期，控制者 userid），没人控制时不带。
     *  @param event  事件类型，取值见 enum screenshare_event
     *  @param actor  触发事件的成员 userid
     *  @param room   事件发生后的房间快照
     *  @param target 事件针对的成员 userid（事件 9 为被静音的人）；0 表示没有，不带该字段
     */
    static std::string buildStateNotify(int event, int64_t actor, const ScreenShareRoom& room, int64_t target = 0);

    /**
     *  构造中转参数推送（cmd_screenshare_relay_info=1726），只发给本人，形如
     *  {"roomid":"...","relayip":"...","relayport":20003,"token":"...","seat":1,"maxcameras":9}。
     */
    /*  elapsedMs（第 13 期，字段 elapsedms）：发出这条推送时共享已进行的毫秒数，客户端据此计时，不受两边时钟误差影响；
     *  负数按 0。 */
    /*  p2pSessionId（P2P 方案第 6 期，字段 p2psessionid）：单聊共享里服务端为这名观看者与发起人建的 P2P 会话号；为空时
     *  不带该字段（发起人自己的 1726、群聊共享、没建成时）。 */
    static std::string buildRelayInfo(const std::string& roomId, const std::string& relayIp, int relayPort,
                                      const std::string& token, int seat, int maxCameras, int64_t elapsedMs = 0,
                                      const std::string& p2pSessionId = std::string());

    /**
     *  构造查询应答（cmd_screenshare_query=1727）。有进行中的共享时形如
     *  {"code":0,"msg":"ok","exists":1,"roomid":"...","host":"...","membercount":3,"starttime":"..."}；
     *  没有时 exists 为 0，不带房间字段。
     *  @param room 进行中的房间快照；没有共享时传 NULL
     */
    static std::string buildQueryResponse(int code, const std::string& msg, const ScreenShareRoom* room);

    /**
     *  构造「共享已结束」推送（cmd_screenshare_ended_notify=1729），发给会话成员，形如
     *  {"roomid":"...","ctype":2,"cid":"...","host":"...","duration":125,"reason":1}。
     *  @param cid             从接收者角度看的会话 id（同 buildStartedNotify）
     *  @param durationSeconds 共享时长，单位秒
     *  @param reason          结束原因，取值见 enum screenshare_end_reason
     */
    static std::string buildEndedNotify(const std::string& roomId, int ctype, int64_t cid, int64_t hostId,
                                        int64_t durationSeconds, int reason);

    /**
     *  构造「共享标题与描述变了」推送（cmd_screenshare_info_notify=1734，第 15 期），发给会话成员与房间成员，形如
     *  {"roomid":"...","ctype":2,"cid":"...","title":"...","desc":"...","msgid":"...","content":"{...}"}。
     *  移交主持人后也推这一条（2026-10-08）：不带卡片，另带 "hostid":"..." 与 "hostname":"..."，
     *  不在房间里的会话成员据此把横幅上的主持人换成新主持人。
     *  @param cid         从接收者角度看的会话 id（同 buildStartedNotify）
     *  @param cardMsgId   会话里那张共享卡片的消息 id；不大于 0 表示卡片没有写成功或这次不改写卡片，此时不带 msgid 与 content
     *  @param cardContent 改写后的卡片 content（JSON 文本），客户端据此替换本地保存的那张卡片
     *  @param hostId      新主持人 userid；不大于 0 表示主持人没变，此时不带 hostid 与 hostname（与第 15 期的格式相同）
     *  @param hostName    新主持人昵称（UTF-8），hostId 大于 0 时才带
     */
    static std::string buildInfoNotify(const std::string& roomId, int ctype, int64_t cid, const std::string& title,
                                       const std::string& desc, int64_t cardMsgId, const std::string& cardContent,
                                       int64_t hostId = 0, const std::string& hostName = std::string());

    /**
     *  构造控制键鼠推送（cmd_screenshare_control_notify=1736，第 18 期），只发给一个人，形如
     *  {"roomid":"...","action":1,"owner":"5301","actor":"5308","actorname":"李四"}。
     *  申请与撤回发给画面主人（actor 为申请人），拒绝发给申请人（actor 为画面主人）。
     *  @param action    动作，取值见 enum screenshare_control_action（只会是申请、撤回、拒绝）
     *  @param owner     画面主人 userid
     *  @param actor     发起动作的人 userid
     *  @param actorName 发起动作的人的昵称（UTF-8），接收方据此显示「某某申请控制你的屏幕」
     */
    static std::string buildControlNotify(const std::string& roomId, int action, int64_t owner, int64_t actor,
                                          const std::string& actorName);

    /**
     *  构造屏幕共享卡片消息（msgtype=message_type_screenshare(19)）的 content 字段，形如
     *  {"roomid":"...","host":"...","hostname":"...","starttime":"..."}。卡片由服务端在发起共享时写入会话，
     *  随消息推送（1504）送达，客户端据房间号显示「加入」按钮；共享结束后卡片仍保留，客户端以查询结果判断能否加入。
     *  @param hostName  发起人显示名（UTF-8）
     *  @param startTime 发起时间，Unix 毫秒
     *  @return content 的 JSON 文本
     */
    static std::string buildCardContent(const std::string& roomId, int64_t hostId, const std::string& hostName,
                                        int64_t startTime, bool locked = false, bool forced = false,
                                        const std::string& title = std::string(),
                                        const std::string& desc = std::string());

private:
    ScreenShareProtocol();
};
