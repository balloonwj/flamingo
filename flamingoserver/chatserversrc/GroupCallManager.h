/**
 *  群通话房间管理器，GroupCallManager.h
 *
 *  chatserver 端维护「群通话房间表」。一场群通话对应一个房间，一个群同时只有一个房间。房间里的人分两种状态：
 *  振铃中（被邀请、还没接听，不占座位）与已加入（取最小的空闲座位号 1~254，座位号写进 imgserver 房间中转帧头的
 *  发送者编号）。本类管理房间、成员状态、麦克风与摄像头开关、振铃超时、已加入成员的掉线宽限，并按成员签发 imgserver
 *  房间中转用的 RelayToken（角色 role_groupcall_member）。
 *
 *  规则（docs/群音视频通话方案.md 第 2 节、docs/协议设计-v2.md 第 21 节，2026-10-04 与用户确认）：
 *    - 一个群同时只能有一场群通话，再发起返回 722 并给出现有房间号；
 *    - 一个人同时只能在一个群通话房间里（振铃中也算），否则发起 / 加入返回 723，被邀请时记为忙线；
 *    - 人数（振铃中与已加入合计）不超过 m_maxMembers，同时开摄像头的人数不超过 m_maxCameras；
 *    - 最后一个已加入的人离开时房间结束（只剩一人时照常保留）；振铃中的人不算「已加入」；
 *    - 振铃 kRingTimeoutMs 无应答自动移除；已加入的人掉线后保留座位 m_offlineGraceMs，超时按离开处理。
 *  与一对一通话、屏幕共享的互斥判断不在本类里做（需要查 CallManager 与 ScreenShareManager），本类只提供 isUserInCall。
 *
 *  本类为纯逻辑（不触网、不查库），全部公开方法线程安全（内部一把互斥量），便于单元测试，见
 *  tests/test_groupcallmanager.cpp（Suite GCM）。全局实例经 Singleton<GroupCallManager> 访问；测试里可直接构造独立实例。
 *
 *  典型用法（GroupCallService 里）：
 *      GroupCallManager& mgr = Singleton<GroupCallManager>::Instance();
 *      std::string roomId, existing;
 *      std::vector<int64_t> busy;
 *      int code = mgr.startCall(myId, groupId, media, invitees, true, false, nowMs, roomId, existing, busy);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

//运行参数，main.cpp 读取 etc/chatserver.conf 后经 configure 设置一次
struct GroupCallConfig
{
    std::string m_relayTokenKey;   //relay 令牌 HMAC 密钥，与 imgserver 共享（relaytokenkey 配置项）
    std::string m_relayHost;       //imgserver 房间中转对客户端公布的主机地址（imgserverhost 配置项）
    int         m_relayPort;       //imgserver 房间中转端口（relayport 配置项）
    int         m_maxMembers;      //人数上限（振铃中与已加入合计），取值 2~254；越界时按默认值处理
    int         m_maxCameras;      //同时开摄像头的人数上限，取值 1~m_maxMembers；越界时按默认值处理
    int64_t     m_offlineGraceMs;  //已加入的人掉线后保留座位的宽限期，单位毫秒；不大于 0 时按默认值处理

    GroupCallConfig();
};

//房间里一个人的状态
enum groupcall_member_state
{
    groupcall_member_ringing = 1,   //被邀请、正在振铃，还没接听；不占座位
    groupcall_member_joined  = 2,   //已加入通话，占一个座位
};

//房间里的一个人（getRoom 等方法拷贝出的快照里的一项）
struct GroupCallMember
{
    int64_t m_userId;         //userid
    int     m_state;          //状态，取值见 enum groupcall_member_state
    int     m_seat;           //座位号：已加入时 1~254；振铃中为 0
    bool    m_mic;            //是否开着麦克风（振铃中恒为 false）
    bool    m_camera;         //是否开着摄像头（振铃中恒为 false）
    int64_t m_inviterId;      //邀请他的人；不是被邀请进来的（发起人、从横幅加入）为 0
    int64_t m_inviteTime;     //开始振铃的时刻，Unix 毫秒；振铃超时据此计算。不是被邀请进来的为 0
    int64_t m_joinTime;       //加入通话的时刻，Unix 毫秒；振铃中为 0
    int64_t m_offlineSince;   //已加入的人最后一条登录连接断开的时刻，Unix 毫秒；0 表示在线

    GroupCallMember();
};

//一个群通话房间（getRoom 等方法拷贝出的快照）
struct GroupCallRoom
{
    std::string m_roomId;          //房间号（gc- 开头，不含令牌分隔符 '.' / '|'，可直接作 RelayToken 的 sessionid）
    int64_t     m_groupId;         //所在群 id
    int64_t     m_initiatorId;     //发起人 userid
    std::string m_initiatorName;   //发起人显示名（UTF-8）：发起成功后记下，通话结束改写卡片时用
    int         m_media;           //发起时的媒体类型，取值见 enum groupcall_media（GroupCallProtocol.h）
    int64_t     m_startTime;       //发起时间，Unix 毫秒（结束时据此算时长）
    int64_t     m_cardMsgId;       //群里那张通话卡片的消息 id；0 表示卡片没写成功
    bool        m_allMuted;        //是否处于全员静音（2026-10-05）：开启期间发起人、群主、管理员之外的人不能自己开麦克风，
                                   //后加入的人也默认静音；由 muteAll 置位、unmuteAll 清除，房间结束随之消失
    std::vector<GroupCallMember> m_members;   //房间里的人：已加入的按座位号升序在前，振铃中的按被邀请的先后在后

    GroupCallRoom();

    //查某个人；不在房间里返回 NULL（指针指向本快照内部，随快照销毁失效）
    const GroupCallMember* findMember(int64_t userId) const;

    //已加入的人数（含掉线宽限期内的人）
    int joinedCount() const;

    //开着摄像头的人数
    int cameraCount() const;

    //房间里全部人的 userid（已加入与振铃中），顺序同 m_members
    std::vector<int64_t> participantIds() const;

    //已加入的人的 userid，按座位号升序
    std::vector<int64_t> joinedIds() const;
};

//onUserOffline 的处理结果
enum groupcall_offline_result
{
    groupcall_offline_none            = 0,   //此人不在任何房间里，什么也没做
    groupcall_offline_ringing_removed = 1,   //此人在振铃中，已从房间里移除（按振铃未接处理）
    groupcall_offline_joined_away     = 2,   //此人已加入，座位保留并标记离线，等宽限期
};

//定时清理移除的一个人
struct GroupCallReaped
{
    GroupCallRoom m_room;     //处理后的房间快照；m_ended 为 true 时是房间结束之前的快照（仍含仍在振铃的人，供调用方通知他们）
    int64_t       m_userId;   //被移除的人
    bool          m_ended;    //房间是否因此结束（已加入的人降到 0）

    GroupCallReaped();
};

//群通话房间管理器；全局单例经 Singleton<GroupCallManager> 访问
class GroupCallManager final
{
public:
    //人数上限的默认值（振铃中与已加入合计），与方案第 2 节第 3 条一致
    static const int kDefaultMaxMembers = 16;
    //同时开摄像头人数上限的默认值
    static const int kDefaultMaxCameras = 9;
    //已加入的人掉线宽限期的默认值，单位毫秒
    static const int64_t kDefaultOfflineGraceMs = 30 * 1000;
    //振铃超时，单位毫秒：与一对一通话一致
    static const int64_t kRingTimeoutMs = 45 * 1000;
    //座位号上限：中转帧头的发送者编号只有 1 字节，255 留给服务端生成的帧
    static const int kMaxSeat = 254;
    //群通话的座位号从 1 开始（0 号在屏幕共享里是发起人的座位，群通话不使用）
    static const int kFirstSeat = 1;
    //relay 令牌有效期（毫秒）：只覆盖「拿令牌去连 imgserver」这一步，与通话、屏幕共享一致取 5 分钟
    static const int64_t kRelayTokenTtlMs = 5 * 60 * 1000;

    GroupCallManager();
    ~GroupCallManager();

    GroupCallManager(const GroupCallManager& rhs) = delete;
    GroupCallManager& operator=(const GroupCallManager& rhs) = delete;

    /**
     *  设置运行参数；main.cpp 启动阶段调用一次。越界的上限按默认值修正。
     *  @param config 运行参数
     */
    void configure(const GroupCallConfig& config);

    //取当前运行参数（已修正后的值）
    GroupCallConfig getConfig() const;

    /**
     *  发起群通话：建房间，发起人直接加入（取 1 号座位），被邀请人进入振铃中。
     *  被邀请人须由调用方先筛好（群成员、在线、不在一对一通话与屏幕共享里、去重、不含发起人）；已在某个群通话房间里的人
     *  本方法再挑出来记为忙线、不振铃。
     *  @param initiatorId    发起人 userid（>0）
     *  @param groupId        群 id（>0）
     *  @param media          媒体类型，取值见 enum groupcall_media（调用方须已校验）
     *  @param invitees       要振铃的人
     *  @param mic            发起人初始是否开麦克风
     *  @param camera         发起人初始是否开摄像头（房间刚建时不会超限）
     *  @param now            当前时间，Unix 毫秒
     *  @param outRoomId      [出参] 成功时回填新房间号
     *  @param outExistingId  [出参] 该群已有群通话（返回 722）时回填那一场的房间号
     *  @param outBusy        [出参] 因已在别的群通话房间里而没有振铃的被邀请人
     *  @return 0 成功；参数非法返回 error_code_bad_request(3)；该群已有群通话返回 722；发起人已在某个房间里返回 723；
     *          发起人加上要振铃的人超过人数上限返回 724（此时不建房间）
     */
    int startCall(int64_t initiatorId, int64_t groupId, int media, const std::vector<int64_t>& invitees, bool mic,
                  bool camera, int64_t now, std::string& outRoomId, std::string& outExistingId,
                  std::vector<int64_t>& outBusy);

    /**
     *  加入房间：振铃中的人接听，或群成员从横幅 / 卡片直接加入。已加入的人重复加入是幂等的（座位不变、返回 0）。
     *  是否有资格（群成员、企业开关、不在一对一通话与屏幕共享里）由调用方先校验，本方法不查库。
     *  @param roomId       房间号
     *  @param userId       加入者 userid
     *  @param mic          初始是否开麦克风（重复加入时忽略，保持原状）
     *  @param camera       初始是否开摄像头；摄像头人数已达上限时按关闭处理、不报错（重复加入时忽略）
     *  @param now          当前时间，Unix 毫秒
     *  @param outSeat      [出参] 成功时回填座位号
     *  @param outAlready   [出参] 此人原本就已加入（重复加入）时为 true
     *  @param canSpeakWhenAllMuted 此人在全员静音期间能否开麦克风（发起人、群主、管理员为 true，由调用方判断）；
     *                      房间处于全员静音且此值为 false 时，不论 mic 传什么都按关闭加入
     *  @return 0 成功；房间不存在返回 721；此人在别的群通话房间里返回 723；人数已满返回 724（原本在振铃中的人不受此限）；
     *          座位号用尽返回 724
     */
    int joinCall(const std::string& roomId, int64_t userId, bool mic, bool camera, int64_t now, int& outSeat,
                 bool& outAlready, bool canSpeakWhenAllMuted = true);

    /**
     *  振铃中的人拒绝：从房间里移除。
     *  @param outRoom [出参] 移除之后的房间快照（成功时回填）
     *  @return 0 成功；房间不存在返回 721；此人不在振铃中返回 725
     */
    int declineCall(const std::string& roomId, int64_t userId, GroupCallRoom& outRoom);

    /**
     *  离开房间。已加入的人离开后座位释放；振铃中的人离开按拒绝处理。已加入的人降到 0 时房间结束。
     *  @param outWasRinging [出参] 离开者原本是否在振铃中
     *  @param outEnded      [出参] 房间是否因此结束
     *  @param outRoom       [出参] 成功时回填：房间没结束为移除之后的快照；房间结束为结束之前、移除离开者之后的快照
     *                       （仍含振铃中的人，供调用方通知他们收起来电）
     *  @return 0 成功；房间不存在返回 721；此人不在房间里返回 725
     */
    int leaveCall(const std::string& roomId, int64_t userId, bool& outWasRinging, bool& outEnded,
                  GroupCallRoom& outRoom);

    /**
     *  通话中追加邀请。被邀请人须由调用方先按 startCall 的规则筛好；已在本房间里的人忽略，已在别的群通话房间里的人记为忙线。
     *  @param requesterId 请求者，须已加入
     *  @param invitees    要振铃的人
     *  @param now         当前时间，Unix 毫秒
     *  @param outAdded    [出参] 实际开始振铃的人
     *  @param outBusy     [出参] 因已在别的群通话房间里而没有振铃的人
     *  @return 0 成功（outAdded 可以为空）；房间不存在返回 721；请求者没有加入返回 725；加上新振铃的人超过人数上限返回 724
     *          （此时一个都不加）
     */
    int inviteMore(const std::string& roomId, int64_t requesterId, const std::vector<int64_t>& invitees, int64_t now,
                   std::vector<int64_t>& outAdded, std::vector<int64_t>& outBusy);

    /**
     *  已加入的人开关麦克风与摄像头（两项一起设置）。
     *  @param canSpeakWhenAllMuted 此人在全员静音期间能否开麦克风（发起人、群主、管理员为 true，由调用方判断）
     *  @return 0 成功；房间不存在返回 721；此人没有加入返回 725；打开摄像头时人数已达上限返回 726；全员静音期间
     *          canSpeakWhenAllMuted 为 false 的人要打开麦克风返回 728（这两种情况两项都不改）
     */
    int setMemberMedia(const std::string& roomId, int64_t userId, bool mic, bool camera,
                       bool canSpeakWhenAllMuted = true);

    /**
     *  关掉某个已加入的人的麦克风（第 6 期管理操作用；权限由调用方判断）。
     *  @param outChanged [出参] 对方原本开着麦克风、确实被关掉时为 true
     *  @return 0 成功；房间不存在返回 721；对方没有加入返回 725
     */
    int muteMember(const std::string& roomId, int64_t targetId, bool& outChanged);

    /**
     *  开启全员静音（2026-10-05 起是房间状态；权限由调用方判断）：置房间的 m_allMuted，并关掉 exemptIds 以外所有已加入的人
     *  的麦克风。已经开启时再调用是幂等的（仍会关掉此刻开着麦克风的非豁免者）。
     *  @param exemptIds 不受全员静音约束的人（发起人、群主、管理员，由调用方按群角色算出）
     *  @param outMuted  [出参] 原本开着麦克风、被关掉的人
     *  @return 0 成功；房间不存在返回 721
     */
    int muteAll(const std::string& roomId, const std::vector<int64_t>& exemptIds, std::vector<int64_t>& outMuted);

    /**
     *  解除全员静音（2026-10-05）：清房间的 m_allMuted。只解除开麦限制，不替任何人打开麦克风。
     *  @param outChanged [出参] 原本处于全员静音、确实被解除时为 true
     *  @return 0 成功；房间不存在返回 721
     */
    int unmuteAll(const std::string& roomId, bool& outChanged);

    /**
     *  把某人移出房间（第 6 期管理操作用；权限由调用方判断）。已加入或振铃中的人都可以移出，之后可以再加入。
     *  移出后已加入的人降到 0 时房间结束（只在操作者把自己以外的人移光、自己又不算已加入时才会发生，正常不会出现）。
     *  @param outEnded [出参] 房间是否因此结束
     *  @param outRoom  [出参] 规则同 leaveCall
     *  @return 0 成功；房间不存在返回 721；对方不在房间里返回 725
     */
    int removeMember(const std::string& roomId, int64_t targetId, bool& outEnded, GroupCallRoom& outRoom);

    /**
     *  按房间号取房间快照。
     *  @return 房间存在返回 true
     */
    bool getRoom(const std::string& roomId, GroupCallRoom& out) const;

    /**
     *  按群取进行中的群通话。
     *  @param groupId 群 id
     *  @param out     [出参] 命中时回填房间快照
     *  @return 该群有进行中的群通话返回 true
     */
    bool findRoomByGroup(int64_t groupId, GroupCallRoom& out) const;

    /**
     *  此人当前是否在某个群通话房间里（已加入或振铃中）。供「一人只能在一处」的判断使用。
     *  @param outRoomId [出参] 可为 NULL；非 NULL 且在房间里时回填房间号
     */
    bool isUserInCall(int64_t userId, std::string* outRoomId = NULL) const;

    /**
     *  给已加入的人签发 imgserver 房间中转用的 relay 令牌：角色 RelayToken::role_groupcall_member，sessionid 为房间号，
     *  uid 为本人 userid，peeruid 字段承载座位号。
     *  @param now      当前时间，Unix 毫秒（过期 = now + kRelayTokenTtlMs）
     *  @param outToken [出参] 签好的令牌
     *  @param outSeat  [出参] 座位号
     *  @return 房间存在且此人已加入时返回 true
     */
    bool signTokenFor(const std::string& roomId, int64_t userId, int64_t now, std::string& outToken, int& outSeat) const;

    /**
     *  振铃中的人还剩多少毫秒超时（随邀请推送下发，来电窗据此显示倒计时）。
     *  @return 剩余毫秒数，已到或已过超时为 0；房间不存在或此人不在振铃中返回 -1
     */
    int64_t ringRemainingMs(const std::string& roomId, int64_t userId, int64_t now) const;

    /**
     *  记下发起人显示名（发起成功后调用一次）。
     *  @param name 发起人显示名（UTF-8）
     *  @return 房间存在返回 true
     */
    bool setInitiatorName(const std::string& roomId, const std::string& name);

    /**
     *  记下群里那张通话卡片的消息 id（发起成功、卡片写入后调用一次）。
     *  @return 房间存在返回 true
     */
    bool setCardMsgId(const std::string& roomId, int64_t cardMsgId);

    /**
     *  用户的最后一条登录连接断开时调用。振铃中的人从房间里移除；已加入的人标记离线、座位保留等宽限期。
     *  @param now     当前时间，Unix 毫秒
     *  @param outRoom [出参] 处理之后的房间快照（结果不是 none 时回填）
     *  @return 处理结果，取值见 enum groupcall_offline_result
     */
    int onUserOffline(int64_t userId, int64_t now, GroupCallRoom& outRoom);

    /**
     *  用户重新登录时调用。若此人是某个房间里标记为离线的已加入成员，清除离线标记。
     *  @param outRoom [出参] 清除标记后的房间快照（返回 true 时回填）
     *  @return 清除了离线标记返回 true
     */
    bool onUserOnline(int64_t userId, GroupCallRoom& outRoom);

    /**
     *  定时清理振铃超时的人：振铃满 kRingTimeoutMs 的一律移除（振铃中的人不影响房间存续，不会因此结束房间）。
     *  @param now 当前时间，Unix 毫秒
     *  @param out [出参] 追加被移除的人与移除后的房间快照；调用前不清空
     */
    void reapRingTimeouts(int64_t now, std::vector<GroupCallReaped>& out);

    /**
     *  定时清理掉线超过宽限期的已加入成员：按离开处理，已加入的人降到 0 时房间结束。
     *  @param now 当前时间，Unix 毫秒
     *  @param out [出参] 追加被移除的人；调用前不清空
     */
    void reapOfflineMembers(int64_t now, std::vector<GroupCallReaped>& out);

    //当前的房间数（测试与日志用）
    size_t roomCount() const;

private:
    //以下私有方法都要求调用方已持有 m_mutex

    //取房间里最小的空闲座位号（kFirstSeat 起）；已用尽返回 -1
    static int lowestFreeSeat(const GroupCallRoom& room);

    //整理成员顺序：已加入的按座位号升序在前，振铃中的保持原有先后在后
    static void sortMembers(GroupCallRoom& room);

    //把某人从房间与成员索引里移除（不判断房间是否结束）
    void removeMemberLocked(GroupCallRoom& room, int64_t userId);

    //房间结束：从房间表、群索引与成员索引里删除
    void dissolveLocked(const std::string& roomId);

    //把某人移出房间，已加入的人降到 0 时结束房间；回填规则同 leaveCall 的 outRoom
    void removeAndMaybeEndLocked(GroupCallRoom& room, int64_t userId, bool& outEnded, GroupCallRoom& outRoom);

    mutable std::mutex                       m_mutex;       //保护以下全部成员
    GroupCallConfig                          m_config;      //运行参数（configure 修正后的值）
    std::map<std::string, GroupCallRoom>     m_rooms;       //房间号 → 房间，生命周期从发起到结束
    std::map<int64_t, std::string>           m_groupRoom;   //群 id → 房间号，保证一个群只有一场群通话
    std::map<int64_t, std::string>           m_userRoom;    //userid → 所在房间号（已加入与振铃中），保证一人只在一个房间
    int64_t                                  m_roomSeq;     //房间号的递增序号，保证同一毫秒内发起的房间号也不重复
};
