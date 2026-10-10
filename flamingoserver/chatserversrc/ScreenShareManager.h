/**
 *  屏幕共享房间管理器，ScreenShareManager.h
 *
 *  chatserver 端维护「屏幕共享房间表」。一次屏幕共享对应一个房间：发起人建房后是唯一的屏幕画面发送方，
 *  会话里的其他人加入后成为观看者。本类管理房间、成员（座位号、麦克风 / 摄像头 / 电脑声音开关、录制状态）、
 *  发起人设置的标注与录制权限，以及发起人掉线后的宽限计时；并按成员签发 imgserver 房间中转用的 RelayToken。
 *
 *  规则（docs/屏幕共享与录制方案.md 第 5 节与第 11.4 节 2a-1，2026-10-02 与用户确认）：
 *    - 同一会话同时只能有一个进行中的共享；单聊按双方 userid 配对，两边查到的是同一个房间；
 *    - 同一个人同时只能在一个房间里（不论发起人还是观看者），否则发起 / 加入返回 714；
 *    - 房间人数（含发起人）不超过 m_maxMembers，同时开摄像头的人数不超过 m_maxCameras，超出分别返回 711 / 715；
 *    - 只有发起人能结束共享、设置权限、全员静音，其他人返回 716；发起人「离开」等同结束共享；
 *    - 收回录制权限时，正在录制的观看者一律停止录制（由客户端保存已录部分）；
 *    - 观看者掉线等同离开；发起人掉线后房间保留并标记离线，宽限期内回来则恢复，超时由定时清理解散。
 *    - 强制模式（第 13 期）：观看者不能离开（718），要等发起人结束；观看者掉线时记下，重新登录、共享仍在时让其回到观看；
 *    - 发起人可以单独关掉某个观看者的麦克风（第 13 期），被关掉的人之后可以自己重新打开。
 *    - 控制键鼠（第 18 期）：每块画面同一时间只有一人控制（applyControl）；控制者离开、被移出、掉线时，以及画面主人
 *      掉线时，控制关系随之清除。
 *  与一对一通话互斥的判断不在本类里做（需要查 CallManager），本类只提供 isUserInShare 供调用方判断。
 *
 *  本类为纯逻辑（不触网、不查库），全部公开方法线程安全（内部一把互斥量），便于单元测试，见
 *  tests/test_screensharemanager.cpp（Suite SCM）。全局实例经 Singleton<ScreenShareManager> 访问；
 *  测试里可直接构造独立实例。
 *
 *  典型用法（ChatSession 的处理函数里）：
 *      ScreenShareManager& mgr = Singleton<ScreenShareManager>::Instance();
 *      std::string roomId;
 *      int code = mgr.startShare(myId, ctype, cid, mic, camera, sysAudio, nowMs, roomId);
 *      if (code == 0) { ...签发令牌、推送 cmd_screenshare_relay_info... }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

//运行参数，main.cpp 读取 etc/chatserver.conf 后经 configure 设置一次
struct ScreenShareConfig
{
    std::string m_relayTokenKey;   //relay 令牌 HMAC 密钥，与 imgserver 共享（relaytokenkey 配置项）
    std::string m_relayHost;       //imgserver 房间中转对客户端公布的主机地址
    int         m_relayPort;       //imgserver 房间中转端口
    int         m_maxMembers;      //房间人数上限（含发起人），取值 2~254；越界时按默认值处理
    int         m_maxCameras;      //同时开摄像头的人数上限，取值 1~m_maxMembers；越界时按默认值处理
    int         m_maxSharers;      //同时共享屏幕的人数上限（第 22 期，含主持人），取值 1~m_maxMembers；越界时按默认值处理
    int64_t     m_hostGraceMs;     //发起人掉线后保留房间的宽限期，单位毫秒；不大于 0 时按默认值处理

    ScreenShareConfig();
};

//房间里的一名成员（发起人也是成员，座位号固定为 0）
struct ScreenShareMember
{
    int64_t m_userId;        //成员 userid
    int     m_seat;          //座位号：发起人 0，观看者 1~254（取最小的空闲号），写进中转帧头的「发送者编号」
    bool    m_mic;           //是否开着麦克风
    bool    m_camera;        //是否开着摄像头
    bool    m_systemAudio;   //是否在共享电脑声音（只有发起人会开）
    bool    m_recording;     //是否正在录制
    int64_t m_joinTime;      //加入时间，Unix 毫秒
    std::string m_p2pSessionId;   //单聊共享里服务端为这名观看者与发起人建的 P2P 会话号（P2P 方案第 6 期）；没建时为空。
                                  //随房间快照下发，发起人据此认领链路槽
    int64_t m_controlledBy;  //正在控制此人屏幕的人的 userid（第 18 期控制键鼠）；0 表示没人控制。只对正在共享屏幕的人有意义；
                             //控制者离开房间、画面主人掉线、收回控制时清零
    bool    m_sharing;       //是否正在共享自己的屏幕（第 22 期）：主持人发起时为 true，其他人经主持人同意后为 true
    int     m_screenSeat;    //这块画面在中转里的发送座位（第 22 期）：主持人用自己的座位（推流管线就是他的中转连接）；其他共享者由
                             //服务端另分配一个与成员座位不重复的座位，凭「只推流」令牌另连一条中转连接推屏幕；不共享时为 -1
    bool    m_shareRequested;   //是否申请了共享自己的屏幕、等主持人处理（第 22 期）；同意、拒绝、撤回或离开房间时清除
    bool    m_boardRequested;   //是否申请了新建白板、等主持人处理（第 23 期）；同意、拒绝、撤回或离开房间时清除
    bool    m_viaLink;          //是否凭分享链接加入、且不是该会话的成员（第 25 期「链接访客」），随 1725 快照下发

    ScreenShareMember();
};

//房间里的一块白板（第 23 期，docs/屏幕共享与录制方案.md 第 2 节第 32 条）：只有笔迹、没有视频的共享画面，占一个共享名额。
//每人至多建一块；建立者与主持人都能关掉它；建立者离开房间时随之关闭
struct ScreenShareWhiteboard
{
    int     m_seat;      //白板的画面座位：与成员座位、其他画面座位都不重复，标注按它区分画面
    int64_t m_ownerId;   //建立者 userid

    ScreenShareWhiteboard();
};

//一个屏幕共享房间（getRoom 等方法拷贝出的快照）
struct ScreenShareRoom
{
    std::string m_roomId;            //房间号（不含令牌分隔符 '.' / '|'，可直接作 RelayToken 的 sessionid）
    int         m_ctype;             //所在会话类型：conversation_single(1) / conversation_group(2)
    int64_t     m_convKeyA;          //会话键：单聊为两人中较小的 userid，群聊为群 id
    int64_t     m_convKeyB;          //会话键：单聊为两人中较大的 userid，群聊为 0
    int64_t     m_hostId;            //当前主持人 userid：发起时为发起人，移交主持人（第 22 期）后为新主持人
    int64_t     m_starterId;         //最初的发起人 userid（2026-10-08）：发起时与 m_hostId 相同，移交主持人后不变；
                                     //新主持人修改标题时改写会话里的共享卡片，卡片上「某某发起的共享」仍按它写
    int64_t     m_startTime;         //发起时间，Unix 毫秒（结束时据此算时长）
    bool        m_annotateAllowed;   //观看者能否标注（发起人设置，默认允许；发起人自己始终可以）
    bool        m_recordAllowed;     //观看者能否录制（发起人设置，默认允许；发起人自己始终可以）
    int64_t     m_hostOfflineSince;  //发起人掉线的时刻，Unix 毫秒；0 表示发起人在线
    std::string m_password;          //加入密码（6 位数字，第 12 期）；空表示不设密码。只存在内存里，不写日志、不随推送下发
    bool        m_forced;            //是否强制模式（第 13 期）：观看者加入后不能离开，要等发起人结束
    std::string m_hostName;          //当前主持人昵称（UTF-8，第 13 期）：强制模式下掉线的观看者重新登录时随 rejoin 推送带上，
                                     //凭链接查共享概要时也带上；移交主持人时换成新主持人的昵称
    std::string m_starterName;       //最初的发起人昵称（UTF-8）：发起时由 setHostName 一并写入，移交主持人后不变
    std::vector<int64_t> m_forcedAway;   //强制模式下掉线的观看者（第 13 期）：重新登录时让其回到观看，重新加入后移除
    std::vector<int64_t> m_banned;       //被发起人移出且不允许再次加入的人（第 14 期）：再加入时回 719；只属于这一场，房间解散后随之清除
    std::string m_title;             //共享标题（第 15 期，UTF-8）：发起时设置，发起人可在共享中修改（1733）
    std::string m_desc;              //共享描述（第 15 期，UTF-8，选填）；空表示没填
    int64_t     m_cardMsgId;         //会话里那张共享卡片的消息 id（第 15 期）：修改标题与描述时据此改写卡片；0 表示卡片没写成功
    int         m_maxSharers;        //同时共享屏幕的人数上限（第 22 期，发起时取自配置），随 1725 下发
    std::vector<ScreenShareMember> m_members;   //全部成员，含发起人；按座位号升序
    std::vector<ScreenShareWhiteboard> m_whiteboards;   //白板（第 23 期），按建立的先后
    int64_t     m_cloudRecordId;     //进行中的云端录制的录像 id（第 26 期）；0 表示没有在云端录制
    int64_t     m_cloudRecordOwner;  //开启这次云端录制的人（录像归他，主持人移交后不变）；没有在录时为 0
    bool        m_cloudRecordPaused; //云端录制是否暂停中
    int64_t     m_cloudRecordSince;  //这次云端录制开始的时刻，Unix 毫秒；没有在录时为 0
    int         m_cloudRecordEndCode;//最近一次云端录制被动结束的原因（随 1725 事件 15 下发）：0 为主持人停止，738 为云端存储不可用

    ScreenShareRoom();

    //查某个成员；不在房间里返回 NULL（指针指向本快照内部，随快照销毁失效）
    const ScreenShareMember* findMember(int64_t userId) const;

    //此人是否是强制模式下掉线、还没回来的观看者
    bool isForcedAway(int64_t userId) const;

    //此人是否已被发起人移出、且不允许再次加入（第 14 期）
    bool isBanned(int64_t userId) const;
};

//ScreenShareManager::applyControl 的处理结果（第 18 期控制键鼠）
struct ScreenShareControlResult
{
    int64_t         m_notifyUser;       //要经 1736 单独通知的人：申请与撤回为画面主人，拒绝为申请人；其余动作为 0
    bool            m_changed;          //授予或收回是否确实改变了控制关系；重复授予同一人、没人控制时收回为 false，调用方不必广播
    int64_t         m_prevController;   //动作之前正在控制这块画面的人；没有为 0
    ScreenShareRoom m_room;             //处理之后的房间快照（返回 0 时回填，供调用方广播）

    ScreenShareControlResult();
};

//ScreenShareManager::applySharing 的处理结果（第 22 期多人共享）
struct ScreenShareSharingResult
{
    int64_t         m_notifyUser;    //要经 1738 单独通知的人：申请与撤回为主持人，同意、拒绝与被主持人停止为当事人；其余为 0
    bool            m_changed;       //正在共享的人是否有变化（同意、停止、主持人恢复自己的画面）；有变化时调用方经 1725 广播
    int64_t         m_publishUser;   //刚被同意、要另连中转连接推屏幕的人（主持人以外的共享者）；调用方为他签「只推流」令牌；没有为 0
    int64_t         m_subject;       //这次动作针对的人（申请人、被同意 / 拒绝 / 停止的人）；广播时作为 target
    int             m_boardSeat;     //新建的白板的画面座位（第 23 期：同意、或主持人自己直接新建时）；没有为 -1
    ScreenShareRoom m_room;          //处理之后的房间快照（返回 0 时回填）

    ScreenShareSharingResult();
};

//onUserOffline 的处理结果
enum screenshare_offline_result
{
    screenshare_offline_none        = 0,   //此人不在任何房间里，什么也没做
    screenshare_offline_member_left = 1,   //此人是观看者，已按「离开」移出房间
    screenshare_offline_host_away   = 2,   //此人是发起人，房间保留并标记发起人离线，等宽限期
};

//屏幕共享房间管理器；全局单例经 Singleton<ScreenShareManager> 访问
class ScreenShareManager final
{
public:
    //房间人数上限的默认值（含发起人），与方案第 2 节第 14 条一致
    static const int kDefaultMaxMembers = 50;
    //同时开摄像头人数上限的默认值
    static const int kDefaultMaxCameras = 9;
    //同时共享屏幕人数上限的默认值（第 22 期，含主持人），与方案第 2 节第 27 条一致
    static const int kDefaultMaxSharers = 4;
    //发起人掉线宽限期的默认值，单位毫秒
    static const int64_t kDefaultHostGraceMs = 30 * 1000;
    //座位号上限：中转帧头的发送者编号只有 1 字节，255 留作「无效」
    static const int kMaxSeat = 254;
    //relay 令牌有效期（毫秒）：只覆盖「拿令牌去连 imgserver」这一步，与通话一致取 5 分钟
    static const int64_t kRelayTokenTtlMs = 5 * 60 * 1000;

    ScreenShareManager();
    ~ScreenShareManager();

    ScreenShareManager(const ScreenShareManager& rhs) = delete;
    ScreenShareManager& operator=(const ScreenShareManager& rhs) = delete;

    /**
     *  设置运行参数；main.cpp 启动阶段调用一次。越界的上限按默认值修正。
     *  @param config 运行参数
     */
    void configure(const ScreenShareConfig& config);

    //取当前运行参数（已修正后的值）
    ScreenShareConfig getConfig() const;

    /**
     *  发起屏幕共享：建房间，发起人坐 0 号座位。
     *  @param hostId    发起人 userid（>0）
     *  @param ctype     会话类型：conversation_single(1) / conversation_group(2)
     *  @param cid       会话 id：单聊为对端 userid（不能是自己），群聊为群 id
     *  @param mic       发起人初始是否开麦克风
     *  @param camera    发起人初始是否开摄像头（受摄像头上限约束，房间刚建时不会超限）
     *  @param sysAudio  发起人初始是否共享电脑声音
     *  @param now       当前时间，Unix 毫秒
     *  @param outRoomId [出参] 成功时回填新房间号
     *  @param password  加入密码（第 12 期）：空表示不设密码，否则须是 6 位数字（由调用方先校验）；随房间一起建好，不存在没有密码的间隙
     *  @param forced    是否强制模式（第 13 期）
     *  @param recordAllowed 观看者初始能否录制（第 13 期「禁止观看者录制」取反）；共享中发起人仍可经 setPermission 修改
     *  @return 0 成功；参数非法返回 error_code_bad_request(3)；该会话已有共享返回 710；发起人已在别的房间返回 714
     */
    int startShare(int64_t hostId, int ctype, int64_t cid, bool mic, bool camera, bool sysAudio, int64_t now,
                   std::string& outRoomId, const std::string& password = std::string(), bool forced = false,
                   bool recordAllowed = true);

    /**
     *  观看者加入房间，分配最小的空闲座位号。同一人重复加入是幂等的（座位号不变、返回 0）。
     *  是否有资格加入（单聊须是对端、群聊须是群成员）由调用方先校验，本方法不查库。
     *  @param roomId  房间号
     *  @param userId  加入者 userid
     *  @param now     当前时间，Unix 毫秒
     *  @param outSeat [出参] 成功时回填座位号
     *  强制模式下掉线的观看者重新加入后，从房间的 m_forcedAway 里移除。
     *  @param viaLink 是否以链接访客的身份加入（第 25 期：不是会话成员、凭分享链接放行，由调用方经 ScreenShareRules::checkLinkJoin
     *                 判定）；重复加入时不改原来的标记
     *  @return 0 成功；房间不存在返回 708；人数已满返回 711；此人已在别的房间返回 714；
     *          此人已被发起人移出且不允许再次加入时返回 719（第 14 期）
     */
    int joinShare(const std::string& roomId, int64_t userId, int64_t now, int& outSeat, bool viaLink = false);

    /**
     *  成员离开房间。观看者离开后座位释放；发起人离开等同结束共享（房间解散）。
     *  @param roomId       房间号
     *  @param userId       离开者 userid
     *  @param outDissolved [出参] 房间是否因此解散（离开者是发起人时为 true）
     *  @param outRoom      [出参] 离开之前的房间快照（成功时回填，供调用方给其余成员推送）
     *  @return 0 成功；房间不存在返回 708；此人不在房间里返回 709；强制模式下观看者离开返回 718（此人仍在房间里）
     */
    int leaveShare(const std::string& roomId, int64_t userId, bool& outDissolved, ScreenShareRoom& outRoom);

    /**
     *  发起人结束共享，房间解散。
     *  @param roomId      房间号
     *  @param requesterId 请求者 userid
     *  @param outRoom     [出参] 解散前的房间快照（成功时回填，供调用方推送、算时长）
     *  @return 0 成功；房间不存在返回 708；请求者不是发起人返回 716
     */
    int stopShare(const std::string& roomId, int64_t requesterId, ScreenShareRoom& outRoom);

    /**
     *  按房间号取房间快照。
     *  @return 房间存在返回 true
     */
    bool getRoom(const std::string& roomId, ScreenShareRoom& out) const;

    /**
     *  按会话查当前有无进行中的共享。
     *  @param ctype  会话类型
     *  @param selfId 查询者 userid（单聊时与 cid 组成会话键）
     *  @param cid    会话 id：单聊为对端 userid，群聊为群 id
     *  @param out    [出参] 命中时回填房间快照
     *  @return 该会话有进行中的共享返回 true
     */
    bool findRoomByConversation(int ctype, int64_t selfId, int64_t cid, ScreenShareRoom& out) const;

    /**
     *  成员开关麦克风、摄像头、电脑声音（三项一起设置）。
     *  @return 0 成功；房间不存在返回 708；此人不在房间里返回 709；打开摄像头时人数已达上限返回 715（此时三项都不改）
     */
    int setMemberMedia(const std::string& roomId, int64_t userId, bool mic, bool camera, bool sysAudio);

    /**
     *  发起人设置观看者的标注、录制权限。收回录制权限时，正在录制的观看者一律清除录制状态。
     *  @param annotate          标注权限：1 允许，0 收回，-1 不变
     *  @param record            录制权限：1 允许，0 收回，-1 不变
     *  @param outStoppedRecords [出参] 因收回录制权限而被停止录制的观看者 userid
     *  @return 0 成功；房间不存在返回 708；请求者不是发起人返回 716
     */
    int setPermission(const std::string& roomId, int64_t requesterId, int annotate, int record,
                      std::vector<int64_t>& outStoppedRecords);

    /**
     *  发起人全员静音：关掉发起人以外所有成员的麦克风。被静音的人之后可以自己重新打开。
     *  @param outMuted [出参] 原本开着麦克风、被关掉的成员 userid
     *  @return 0 成功；房间不存在返回 708；请求者不是发起人返回 716
     */
    int muteAll(const std::string& roomId, int64_t requesterId, std::vector<int64_t>& outMuted);

    /**
     *  发起人单独关掉某个观看者的麦克风（第 13 期）。被关掉的人之后可以自己重新打开。
     *  @param targetId   要静音的观看者 userid
     *  @param outChanged [出参] 对方原本开着麦克风、确实被关掉时为 true；本来就关着时为 false（调用方据此决定是否广播）
     *  @return 0 成功；房间不存在返回 708；请求者不是发起人返回 716；对方不在房间里返回 709；对方是发起人返回 error_code_bad_request(3)
     */
    int muteMember(const std::string& roomId, int64_t requesterId, int64_t targetId, bool& outChanged);

    /**
     *  发起人移出某个观看者（第 14 期）：此人离开房间、座位释放；强制模式下同样可以移出。此人若是强制模式下掉线、
     *  还没回来的观看者（发起人点「移出」时对方恰好掉线），同样处理：从待回到观看的名单里清除。
     *  @param requesterId 请求者 userid
     *  @param targetId    要移出的观看者 userid
     *  @param allowRejoin 是否允许此人再次加入；为 false 时记入房间的 m_banned，此后加入回 719
     *  @return 0 成功；房间不存在返回 708；请求者不是发起人返回 716；对方是发起人返回 error_code_bad_request(3)；
     *          对方既不在房间里、也不是待回到观看的人时返回 709
     */
    int removeMember(const std::string& roomId, int64_t requesterId, int64_t targetId, bool allowRejoin);

    /**
     *  成员开始或停止录制。发起人随时可以录；观看者开始录制须有录制权限；停止录制总是允许。
     *  @return 0 成功；房间不存在返回 708；此人不在房间里返回 709；观看者无录制权限时开始录制返回 713
     */
    int setRecording(const std::string& roomId, int64_t userId, bool recording);

    /**
     *  控制键鼠的五种动作（第 18 期，docs/屏幕共享与录制方案.md 第 2 节第 24～26 条）。每块画面同一时间只有一人控制；
     *  控制关系记在画面主人的 ScreenShareMember::m_controlledBy 上。规则：
     *    - 画面主人须是正在共享屏幕且在线的人（目前只有发起人），否则返回 729；
     *    - 申请、撤回：发起动作的人须是房间成员且不是画面主人；已在控制这块画面时申请不再通知画面主人；
     *    - 授予、拒绝：只有画面主人能做（730），对象须是房间成员且不是画面主人自己；授予时原先另有人在控制则被替换；
     *    - 收回：画面主人或正在控制的人能做（730）；没人控制时什么也不改。
     *  @param roomId   房间号
     *  @param actorId  发起动作的人 userid
     *  @param action   动作，取值见 ScreenShareProtocol.h 的 enum screenshare_control_action（1~5）
     *  @param ownerId  画面主人 userid
     *  @param targetId 授予、拒绝的对象 userid；其余动作不用
     *  @param out      [出参] 处理结果，见 ScreenShareControlResult
     *  @return 0 成功；房间不存在返回 708；发起动作的人或对象不在房间里返回 709；画面主人不在共享屏幕返回 729；
     *          无权授予、拒绝、收回返回 730；动作取值非法、对象是画面主人自己、画面主人申请控制自己时返回 error_code_bad_request(3)
     */
    int applyControl(const std::string& roomId, int64_t actorId, int action, int64_t ownerId, int64_t targetId,
                     ScreenShareControlResult& out);

    /**
     *  给房间成员签发 imgserver 房间中转用的 relay 令牌。发起人的角色为 RelayToken::role_screenshare_host，
     *  观看者为 role_screenshare_member；令牌的 sessionid 为房间号，uid 为成员 userid，peeruid 字段承载座位号。
     *  @param now      当前时间，Unix 毫秒（过期 = now + kRelayTokenTtlMs）
     *  @param outToken [出参] 签好的令牌
     *  @param outSeat  [出参] 成员的座位号
     *  @return 房间存在且此人是成员时返回 true
     */
    bool signTokenFor(const std::string& roomId, int64_t userId, int64_t now, std::string& outToken, int& outSeat) const;

    /**
     *  多人共享（第 22 期）：申请、撤回、同意、拒绝、停止共享屏幕。规则：
     *    - 申请：观看者申请后转给主持人；正在共享的人申请是空操作；主持人停掉自己的画面后再申请时直接恢复（仍用自己的座位）；
     *      共享人数已满时直接返回 731；
     *    - 撤回：申请人撤回还没处理的申请，转给主持人；没有申请时是空操作；
     *    - 同意、拒绝：只有主持人能做，对方须有待处理的申请（否则 732）；同意时人数已满返回 731（申请保留），否则为他分配画面座位；
     *    - 停止：共享者停自己的，主持人可停任何人的（targetId 为 0 表示停自己的）；对方没在共享返回 729。停止后控制随之结束。
     *  @param roomId   房间号
     *  @param actorId  发起动作的人
     *  @param action   动作，取值见 enum screenshare_sharing_action（ScreenShareProtocol.h）
     *  @param targetId 同意、拒绝、停止的对象；申请、撤回不用
     *  @param out      [出参] 处理结果
     *  @return 0 成功；708 房间不存在、709 不是成员、716 不是主持人、729 对方没在共享、731 人数已满、732 没有申请、5 参数非法
     */
    int applySharing(const std::string& roomId, int64_t actorId, int action, int64_t targetId,
                     ScreenShareSharingResult& out);

    /**
     *  白板（第 23 期，第 32 条）：申请、撤回、同意、拒绝、关闭，与共享屏幕同一套规则。规则：
     *    - 申请：已有白板的人是空操作；共享名额（正在共享屏幕的人数加白板数）已满返回 731；主持人直接新建，其他人转给主持人；
     *    - 撤回：申请人撤回还没处理的申请，转给主持人；没有申请时是空操作；
     *    - 同意、拒绝：只有主持人能做，对方须有待处理的申请（否则 732）；同意时名额已满返回 731（申请保留），否则为他新建白板；
     *    - 关闭：targetId 为白板的建立者（0 表示自己的），建立者或主持人能关（716）；对方没有白板返回 729。
     *  新建白板时分配与成员座位、画面座位都不重复的座位；结果的 m_notifyUser 为要经 1738 通知的人（新建时为建立者本人）。
     *  @return 0 成功；708 房间不存在、709 不是成员、711 座位已满、716 无权操作、729 没有白板、731 名额已满、732 没有申请、5 参数非法
     */
    int applyWhiteboard(const std::string& roomId, int64_t actorId, int action, int64_t targetId,
                        ScreenShareSharingResult& out);

    /**
     *  移交主持人（第 22 期）：主持人把身份交给一位正在共享屏幕的人，之后原主持人离开房间（他的画面随之结束）。
     *  主持人昵称随之换成 targetName；最初的发起人（m_starterId / m_starterName）不变。
     *  @param targetName 新主持人的昵称（UTF-8），由调用方查用户资料得到；查不到时可传空串
     *  @param outRoom    [出参] 移交之后的房间快照
     *  @return 0 成功；708 房间不存在、716 不是主持人、709 对方不是成员、729 对方没在共享、5 交给自己
     */
    int transferHost(const std::string& roomId, int64_t actorId, int64_t targetId, const std::string& targetName,
                     ScreenShareRoom& outRoom);

    /**
     *  为主持人以外的共享者签「只推流」的中转令牌（第 22 期）：角色 role_screenshare_publisher，peeruid 为画面座位。
     *  @param outScreenSeat [出参] 画面座位
     *  @return 此人正在共享、且不是用自己座位推流的主持人时返回 true
     */
    bool signPublishToken(const std::string& roomId, int64_t userId, int64_t now, std::string& outToken,
                          int& outScreenSeat) const;

    //同时共享屏幕的人数上限（configure 修正后的值，第 22 期）
    int maxSharers() const;

    /**
     *  此人当前是否在某个房间里（发起人或观看者）。供「与一对一通话互斥」等判断使用。
     *  @param outRoomId [出参] 可为 NULL；非 NULL 且在房间里时回填房间号
     */
    bool isUserInShare(int64_t userId, std::string* outRoomId = NULL) const;

    /**
     *  用户的最后一条连接断开时调用。观看者按「离开」移出（强制模式下另记入 m_forcedAway）；发起人则标记离线、房间保留等宽限期。
     *  @param now       当前时间，Unix 毫秒
     *  @param outRoom   [出参] 处理之后的房间快照（结果不是 none 时回填，供调用方推送）
     *  @return 处理结果，取值见 enum screenshare_offline_result
     */
    int onUserOffline(int64_t userId, int64_t now, ScreenShareRoom& outRoom);

    /**
     *  用户重新上线时调用。若此人是某个房间里标记为离线的发起人，清除离线标记。
     *  @param outRoom [出参] 清除标记后的房间快照（返回 true 时回填）
     *  @return 清除了离线标记返回 true；此人不是离线中的发起人返回 false
     */
    bool onUserOnline(int64_t userId, ScreenShareRoom& outRoom);

    /**
     *  查此人是否是强制模式下掉线、共享还在、需要回到观看的观看者（第 13 期，登录时调用）。此人已在别的房间里时返回 false。
     *  @param outRoom [出参] 要回到的房间快照（返回 true 时回填）
     *  @return 需要回到观看返回 true
     */
    bool findForcedRejoin(int64_t userId, ScreenShareRoom& outRoom) const;

    /**
     *  记下发起人昵称（第 13 期，发起成功后调用一次）：写入当前主持人昵称；最初的发起人昵称还没记下时一并写入，
     *  之后再调用不改变发起人昵称。
     *  @param hostName 发起人昵称（UTF-8）
     *  @return 房间存在返回 true
     */
    bool setHostName(const std::string& roomId, const std::string& hostName);

    /**
     *  设置共享标题与描述（第 15 期）：发起成功后设置一次，共享中发起人修改时（1733）再设置。
     *  标题与描述的格式与敏感词由调用方先用 ScreenShareRules::checkShareInfo 检查，本方法原样保存。
     *  @param requesterId 请求者 userid，须是发起人
     *  @param title       共享标题（UTF-8）
     *  @param desc        共享描述（UTF-8）；空表示没填
     *  @return 0 成功；房间不存在返回 708；请求者不是发起人返回 716
     */
    int setInfo(const std::string& roomId, int64_t requesterId, const std::string& title, const std::string& desc);

    /**
     *  记下会话里那张共享卡片的消息 id（第 15 期，发起成功、卡片写入后调用一次）。
     *  @param cardMsgId 卡片的消息 id
     *  @return 房间存在返回 true
     */
    bool setCardMsgId(const std::string& roomId, int64_t cardMsgId);

    /**
     *  记下服务端为某名观看者与发起人建的 P2P 会话号（P2P 方案第 6 期，单聊共享有人加入时调用）。
     *  @param roomId       房间号
     *  @param userId       观看者 userid
     *  @param p2pSessionId P2P 会话号
     *  @return 房间存在且此人在房间里返回 true
     */
    bool setMemberP2PSession(const std::string& roomId, int64_t userId, const std::string& p2pSessionId);

    /**
     *  记下房间开始了云端录制（第 26 期）：只有主持人能开，同一房间同时只有一份。
     *  @param requesterId 请求者，须是主持人；他是录像的开启者
     *  @param recordId    已建好的录像 id（>0）
     *  @param nowMs       当前时间，Unix 毫秒
     *  @param outRoom     [出参] 成功时回填改动之后的房间快照
     *  @return 0 成功；房间不存在 708；不是主持人 716；已在云端录制 737
     */
    int beginCloudRecord(const std::string& roomId, int64_t requesterId, int64_t recordId, int64_t nowMs,
                         ScreenShareRoom& outRoom);

    /**
     *  暂停或继续云端录制（主持人）。
     *  @param recordId [出参] 进行中的录像 id
     *  @param outRoom  [出参] 成功时回填改动之后的房间快照
     *  @return 0 成功（含本来就是这个状态）；房间不存在 708；不是主持人 716；没有在云端录制 737
     */
    int setCloudRecordPaused(const std::string& roomId, int64_t requesterId, bool paused, int64_t& recordId,
                             ScreenShareRoom& outRoom);

    /**
     *  结束房间的云端录制。
     *  @param requesterId    请求者；为 0 表示服务端自己结束（imgserver 回报失败或停止），不检查主持人
     *  @param expectRecordId 只在房间正在录这条录像时才结束；为 0 表示不限
     *  @param endCode        结束原因，记进房间随 1725 事件 15 下发：0 为主持人停止，738 为云端存储不可用
     *  @param recordId       [出参] 被结束的录像 id
     *  @param outRoom        [出参] 成功时回填改动之后的房间快照
     *  @return 0 成功；房间不存在 708；不是主持人 716；没有在录（或不是 expectRecordId）737
     */
    int endCloudRecord(const std::string& roomId, int64_t requesterId, int64_t expectRecordId, int endCode,
                       int64_t& recordId, ScreenShareRoom& outRoom);

    /**
     *  定时清理：发起人离线超过宽限期的房间一律解散。
     *  @param now        当前时间，Unix 毫秒
     *  @param outExpired [出参] 被解散的房间（解散前的快照）
     */
    void reapExpiredHosts(int64_t now, std::vector<ScreenShareRoom>& outExpired);

private:
    //以下私有方法都要求调用方已持有 m_mutex

    //按会话类型与双方 id 算会话键（单聊按两人 userid 排序，群聊为群 id 与 0）
    static void makeConvKey(int ctype, int64_t selfId, int64_t cid, int64_t& keyA, int64_t& keyB);

    //取房间里最小的空闲座位号（1 起）；成员座位与画面座位（第 22 期）都算占用；已满返回 -1
    static int lowestFreeSeat(const ScreenShareRoom& room);

    //房间里占用的共享名额（第 22 期）：正在共享屏幕的人数，第 23 期起加上白板数
    static int sharingCount(const ScreenShareRoom& room);

    //某人建的白板在房间白板表里的下标（第 23 期）；没有返回 -1
    static int findBoardIndex(const ScreenShareRoom& room, int64_t ownerId);

    //按 userid 取房间里成员的可写记录；不在房间里返回 NULL（指针指向房间内部，房间变动前有效）
    static ScreenShareMember* findMemberLocked(ScreenShareRoom& room, int64_t userId);

    //解散房间：从房间表与成员索引里删除，回填解散前的快照
    void dissolveLocked(const std::string& roomId, ScreenShareRoom& outRoom);

    //把某个成员移出房间（不处理发起人），更新成员索引；他正在控制的画面随之解除控制（第 18 期）
    void removeMemberLocked(ScreenShareRoom& room, int64_t userId);

    //取正在共享屏幕且在线的画面主人的成员记录（第 18 期控制键鼠，第 22 期起任何正在共享的人都是画面主人）。
    //没在共享、或画面主人是暂时掉线的主持人时返回 NULL。指针指向房间内部，房间变动前有效
    static ScreenShareMember* findScreenOwnerLocked(ScreenShareRoom& room, int64_t ownerId);

    mutable std::mutex                    m_mutex;        //保护以下全部成员
    ScreenShareConfig                     m_config;       //运行参数（configure 修正后的值）
    std::map<std::string, ScreenShareRoom> m_rooms;        //房间号 → 房间，生命周期从发起到解散
    std::map<int64_t, std::string>        m_userRoom;     //userid → 所在房间号，保证一人只在一个房间
    int64_t                               m_roomSeq;      //房间号的递增序号，保证同一毫秒内发起的房间号也不重复
};
