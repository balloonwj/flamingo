/**
 *  聊天会话类（v2 协议），ChatSession.h
 *
 *  每个客户端 TCP 连接对应一个 ChatSession，由 ChatServer 的会话表持有。
 *  负责 v2 帧的收取与按 cmd 分发，以及登录 / 心跳 / 登出的处理。
 *  v1 的好友 / 群 / 消息 / 组织架构等业务尚未迁移到 v2，迁移完成前相应
 *  cmd 收到后仅记录日志、不处理。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <time.h>
#include <atomic>
#include "../net/ByteBuffer.h"
#include "../net/TimerId.h"
#include "TcpSession.h"
#include "FileShareSessionManager.h"   //ShareConnKey

//P2P 信令处理产出的一帧（定义在 P2PSignalService.h）。这里只做前置声明、不包含 P2P 的头文件：
//P2PProtocol.h 包含了 Protocol.h，而 HttpSession.cpp 经 ChatServer.h 包含本文件，它用的是 AdminApiCodes.h 里
//同名、取值不同的 error_code，两者不能出现在同一个编译单元里
struct P2POutFrame;

//密聊处理产出的一帧（定义在 SecretChatManager.h）。理由同上，这里只做前置声明
struct SecretOutFrame;

//帖子群的帖子、图片与卡片状态（定义在 ThreadProtocol.h）。ChatSessionThread.cpp 之外不需要它们的定义，这里只做前置声明
struct ThreadPost;
struct ThreadImage;
struct ThreadCardState;

using namespace net;

//cmd_send_message 请求结构，定义见 MessageProtocol.h；此处前向声明供 sendGroupMessage 形参使用
struct SendMessageRequest;
struct GroupRecord;   //群资料记录，定义在 GroupProtocol.h；此处仅按 const 引用传递
struct GroupHistoryBound;   //本人在群里能看到的历史范围，定义在 GroupProtocol.h；此处仅作 groupHistoryBound 的返回类型
struct GroupInviteRecord;   //一张入群邀请，定义在 GroupInviteProtocol.h；此处仅按 const 引用传递
struct GroupLinkRecord;     //一条加群链接，定义在 GroupLinkProtocol.h；此处仅按引用传递
//群公告记录，定义在 GroupProtocol.h；此处仅按 const 引用传递
struct GroupAnnouncementRecord;
//一条完整消息体，定义见 MessageProtocol.h；前向声明供卡片互动相关 helper 形参使用
struct MessageRecord;
//接龙卡片在某一时刻的状态，定义在 CardProtocol.h；此处仅按引用传递
struct RelayStatus;
//一条活跃的单聊文件夹传输会话，定义在 FolderTransferManager.h；此处仅按 const 引用传递。
//与上面几个前向声明同理：本头文件不方便再多引一个业务头，用到实体的地方都在 .cpp 里
struct FolderTransferSessionInfo;
//文件共享的共享记录与「看的人」的事实，定义在 FileShareManager.h / FileSharePolicy.h；此处仅按引用传递
struct ShareRecord;
struct ShareViewerFacts;
//一条备忘与群成员的角色，定义在 MemoProtocol.h；此处仅按引用传递
struct MemoRecord;
struct MemoMemberRole;
//v2 帧头，定义在 Protocol.h；此处仅按 const 引用传递。
//注意<u>不能</u>在本头文件里直接包含 Protocol.h ——它与遗留的 v1 协议头 AdminApiCodes.h
//定义了同名的 msg_type / error_code 等枚举，同时进一个翻译单元会重定义冲突。
struct frame_header;

//在线用户信息：登录成功后填充
struct OnlineUserInfo
{
    int32_t     userid;      //用户ID；0 表示尚未登录或会话已失效
    std::string username;    //登录账号
    std::string nickname;    //昵称
    std::string password;    //口令存储哈希（登录时从 UserManager 取得，备用）
    int32_t     clienttype;  //客户端类型，取值见 enum platform_type
    int32_t     status;      //在线状态，取值见 enum presence_type
    std::string deviceid;    //登录请求里上报的设备标识；文件共享据此判断「是不是共享绑定的那台电脑」，可能为空
};

/**
 *  聊天会话类。一个客户端连接对应一个实例。
 */
class ChatSession : public TcpSession
{
public:
    ChatSession(const std::shared_ptr<TcpConnection>& conn, int sessionid);
    virtual ~ChatSession();

    ChatSession(const ChatSession& rhs) = delete;
    ChatSession& operator =(const ChatSession& rhs) = delete;

    //有数据可读时由网络层回调；可能被多个工作 loop 线程调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

    int32_t getSessionId() { return m_id; }
    int32_t getUserId() { return m_userinfo.userid; }
    std::string getUsername() { return m_userinfo.username; }
    std::string getNickname() { return m_userinfo.nickname; }
    std::string getPassword() { return m_userinfo.password; }
    int32_t getClientType() { return m_userinfo.clienttype; }
    int32_t getUserStatus() { return m_userinfo.status; }
    int32_t getUserClientType() { return m_userinfo.clienttype; }
    //登录时上报的设备标识；未上报时为空串
    std::string getDeviceId() { return m_userinfo.deviceid; }
    //本会话是否处于登录态（登录成功后为 true，退出登录后为 false）
    bool isLoggedIn() { return m_isLogin; }
    //本次登录成功的时间，Unix 毫秒；未登录时为 0。管理控制台的在线名单（AdminLinkServer）在推送线程上读取
    int64_t getLoginTimeMs() const { return m_loginTimeMs.load(); }

    /**
     *  向本会话的客户端推送某联系人的在线状态变更（cmd_presence_changed_notify）。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用。</b>本方法内部要
     *  调 ChatServer::getUserClientTypeByUserId 查该用户的客户端类型，那里会再加一次
     *  m_sessionMutex；该锁是非递归锁，同一线程重复加锁会抛 resource_deadlock_would_occur
     *  并终止进程。需要在持锁期间推送时，先把目标会话收集到局部容器，解锁后再逐个推送。
     *
     *  注：ChatServer::onDisconnected 原先正是在持锁期间调用本方法，已于 2026-08-03
     *  改为「锁内收集、锁外推送」。改完后本方法暂无调用方，保留备用。
     *
     *  @param userid 状态发生变更的用户ID
     *  @param type   变更类型：1 上线 2 下线 3 资料变更
     *  @param status 在线状态，type=1 时有效；type=2 时忽略，一律按 0（离线）推送
     */
    void sendUserStatusChangeMsg(int32_t userid, int type, int status = 0);

    //将会话标记为失效（用于被顶号踢下线的旧会话）
    void makeSessionInvalid();
    //会话是否有效（userid>0 视为有效）
    bool isSessionValid();

    /**
     *  本连接下线（正常登出或链路断开）时，清理本会话名下正在进行的那笔单聊文件夹传输。
     *
     *  文件夹传输的续传位点存在双方客户端的内存与接收端的半成品文件里，服务端只记会话，
     *  故任一端的连接一断，这笔传输就不可能再继续（见 docs/协议设计-v2.md 12.7 不变式 2）。
     *  本方法按 docs/协议设计-v2.md 12.2.6 的推送时机表向仍在线的对端推一帧
     *  cmd_folder_state_notify，随后销毁会话：会话还在响铃中时推 folder_state_peer_offline
     *  （对方离线），已经进入传输中 / 失败待重试时推 folder_state_failed 并带
     *  error_code_folder_peer_offline，使对端与本端看到的失败原因一致。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用。</b>本方法内部经
     *  sendToUser 遍历在线会话表，那里会再加一次 m_sessionMutex；该锁是非递归锁，同一
     *  线程重复加锁会抛 resource_deadlock_would_occur 并终止进程。ChatServer::onDisconnected
     *  为此在锁内先留一份会话的 shared_ptr，出锁之后才调用本方法。
     *
     *  本会话从未参与过文件夹传输、或那笔传输早已收尾时，本方法什么都不做，可安全重复调用。
     */
    void cleanupFolderTransferOnOffline();

    /**
     *  单聊文件夹传输会话走到终态时的<u>唯一</u>收口：把「向相关方推一帧
     *  cmd_folder_state_notify」与「在双方单聊会话里留下一条传输记录」这两件事绑在一起，
     *  任何一条终态分支都必须经由本方法，不得自己另写一份推送。
     *
     *  之所以收口：终态分支散落在六处（1812 拒绝、1813 撤回、1814 结束、1817 重试时对端
     *  已离线、连接断开时的收尾、以及主循环定时器判定的邀请超时），各写各的推送时，日后
     *  新增一条终态分支极容易漏掉留记录，而漏掉的表现只是"聊天记录里少了一条"，不报错、
     *  不崩溃，几乎不可能被测出来。
     *
     *  记录条由服务端生成而不是由客户端补发：邀请超时是服务端定时器判定的、对方离线是
     *  服务端发现的，这两种结局下发送方客户端可能根本不在线。记录只写这次传输的动作与
     *  结局（会话 id、目录名、目录数 / 文件数 / 总字节数、终态、失败原因），<u>不写</u>
     *  文件清单与文件内容，故"字节不落盘、不入库"这条不变式没有被打破。
     *
     *  幂等由两道防线保证，缺一不可：
     *
     *  <b>第一道（主防线）是 FolderTransferManager::finishAttempt 的原子判定。</b>本方法
     *  一进来就调它，「本次尝试是否已经终结过」的判断与「标记为已终结」在会话表那把互斥量
     *  的同一个临界区内一次做完，只有第一个调用方拿到 true 并继续做推送与留记录，后到的
     *  一律拿到 false 并直接返回。少了这道防线就会出现 2026-08-14 实测到的那个缺陷：一次
     *  传输结束时双方各发一帧 cmd_folder_close，两帧落在两个不同的工作线程上，都查到"还
     *  没写过记录"，于是各插一条 —— 双方界面上因而看到两条一模一样的记录条，会话终态也被
     *  各置了一遍、跟着多推一帧 cmd_folder_state_notify。
     *
     *  <b>第二道是 clientmsgid 幂等键。</b>clientmsgid 取由「会话 id + 本次尝试的序号」
     *  派生的固定串（见 FolderTransferProtocol::buildRecordClientMsgId），落库前先按
     *  「发送方 userid + clientmsgid」查一次，已有记录即跳过。它挡的是日后新增的、绕开
     *  第一道防线的调用路径，以及进程重启前后的重复落库。
     *
     *  两道防线的去重粒度都是「会话 id + 尝试序号」而不是「会话 id」：<u>同一次尝试</u>
     *  被判定多次终态只会留下一条记录，以最先到达的那个终态为准；而<u>不同次尝试</u>的
     *  序号不同，故一笔传输先失败（序号 1）、在保留期内重试又成功（序号 2）时，聊天记录里
     *  会依次留下失败与完成两条，过程完整。序号由 FolderTransferManager 维护（见其
     *  beginRetryAttempt），本方法只从会话快照里取用。
     *
     *  写成静态方法是因为主循环那条超时清理路径没有 ChatSession 可用（超时与任何一条
     *  客户端连接都无关，发起邀请的那一端完全可能早已断开）；本方法只用到 ChatServer 与
     *  MessageManager 两个单例，不依赖任何会话状态。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用。</b>本方法内部经
     *  ChatServer::pushToUsers 遍历在线会话表，那里会再加一次 m_sessionMutex，该锁不可重入。
     *
     *  本方法内部会做一次数据库写入，耗时与 cmd_send_message 的落库相当。
     *
     *  @param s           会话快照，须含双方 userid、目录名与三项规模统计；调用方通常在
     *                     销毁会话之前取一份（本方法只读不持有）
     *  @param state       终态取值，见 enum folder_transfer_state 的 3~9；传入过程态
     *                     （响铃中 / 传输中）时本方法只推送、不留记录，并记一条告警日志
     *  @param errcode     失败原因，取值见 error_code_folder_*（606~620）；仅
     *                     state 为 folder_state_failed 时写入，其余终态一律按 0 处理
     *  @param errmsg      面向排查的英文诊断串，可为空；同样仅失败态写入
     *  @param notifyUsers 要收到 cmd_folder_state_notify 的 userid 列表；空列表表示本次
     *                     不推状态变更（例如重试时发起方由 1817 的应答得知结果、对端已离线），
     *                     此时仍照常留记录
     *  @param linkPath    数据实际走的 P2P 链路路径（1814 的 linkpath，2026-10-04 起），取值 0~3；写进记录供
     *                     「P2P」徽章，0 表示没有走 P2P
     *  @param md5         单文件直传成功时文件的 MD5（1814 的 md5）；写进直传留下的文件消息，可为空
     *
     *  留不留记录、留哪种记录按会话的传输种类区分（FolderTransferProtocol::shouldWriteRecord）：文件夹传输每个终态
     *  留一条 msgtype=18 的记录条；单文件直传只在完成与拒绝时留一条 msgtype=3 的文件消息
     *  @return true 表示本次调用就是这一次尝试的终结者，推送与留记录都由它做完了；
     *          false 表示本次调用被去重挡下（这一次尝试已被别的线程或别的路径终结过），
     *          或传入的 state 不是终态。调用方多数情况下不必关心返回值 —— 会话的销毁与
     *          应答照常走；只有需要"确实由本次调用收尾时才记一条日志"的调用点才用得上它
     */
    static bool finishFolderTransferSession(const FolderTransferSessionInfo& s,
                                            int state, int errcode, const std::string& errmsg,
                                            const std::vector<int64_t>& notifyUsers,
                                            int linkPath = 0, const std::string& md5 = std::string());

    /**
     *  按某企业的敏感词库检查一组文字，命中时写一条拦截记录并返回 true。供没有会话的服务端模块（机器人的外部内容，
     *  docs/机器人方案.md §5.6）复用本文件的检查与记录逻辑。
     *  @param enterpriseId 企业ID；不大于 0 时视为未命中
     *  @param userId       记为提交者的 userid（机器人账号）
     *  @param userName     记为提交者的名称（机器人名称）
     *  @param source       拦截来源，取值见 chatserver::sensitive_hit_source
     *  @param ctype        会话类型；与会话无关时传 0
     *  @param cid          会话ID；与会话无关时传 0
     *  @param texts        待检查的文字（UTF-8）
     *  @return 命中返回 true
     */
    static bool checkSensitiveForServer(int64_t enterpriseId, int64_t userId, const std::string& userName, int source,
                                        int ctype, int64_t cid, const std::vector<std::string>& texts);

    /**
     *  取消一个文件共享并完成全部收尾，是所有取消入口的<u>唯一</u>收口（分享者取消、群主或管理员取消，
     *  以及日后的群解散、分享者退群、后台强制取消）：置共享为已取消 → 清除在线提供状态 →
     *  结束它的全部访问会话与传输子会话并通知两端 → 向全体成员与分享者推 cmd_share_state_notify →
     *  记审计。任何一个入口自己另写一份推送都容易漏（见 docs/文件共享方案.md 6.1）。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>，理由同 cleanupFolderTransferOnOffline。
     *  @param shareId    共享 id
     *  @param reason     取消原因，取值见 share_invalid_reason（1~5）
     *  @param operatorId 操作人：用户 userid 或后台管理员 id，写进共享记录与审计
     *  @param nowMs      当前时间（Unix 毫秒）
     *  @return error_code_ok；共享不存在为 error_code_share_not_exist；已经取消过为 error_code_share_canceled
     *          （此时不推送、不记审计）；数据库失败为 error_code_server_internal
     */
    static int cancelShareAndNotify(int64_t shareId, int reason, int64_t operatorId, int64_t nowMs);

    /**
     *  某人离开群（退群或被踢）之后的文件共享处理：他在本群作为分享者的有效共享取消（原因「分享者已退群」）；
     *  他作为成员的本群共享把他移出、结束他在这些共享上的访问，并向他推「已失效，你已退出该群」。
     *  在群操作的数据库修改成功之后调用，失败只记日志、不回滚群操作，残留由启动时的一致性清扫补齐。
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>。
     *  @param groupId 群 id
     *  @param userId  离开的人
     */
    static void handleShareMemberLeftGroup(int64_t groupId, int64_t userId);

    /**
     *  群解散之后的文件共享处理：本群全部有效共享取消（原因「群已解散」），经 cancelShareAndNotify 通知
     *  各共享的成员与分享者。<b>调用约束同上。</b>
     *  @param groupId    群 id
     *  @param operatorId 解散群的人（群主），写进共享记录与审计
     */
    static void handleShareGroupDismissed(int64_t groupId, int64_t operatorId);

    /**
     *  某企业的文件共享开关写库之后的处理（docs/文件共享方案.md 5.8、8.8）：向该企业的全体在线用户推 1013
     *  （包体是该企业当前的 features 对象）；开关确有变化时，对该企业的每个有效共享：关闭时结束它的全部访问会话
     *  与传输子会话（原因 800），并向成员与分享者推按新开关计算的状态（关闭时为暂不可用，原因「功能已被关闭」；
     *  打开时按分享者的在线提供状态恢复）。<b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>。
     *  @param enterpriseId  企业 id
     *  @param enabledBefore 写库之前是否开启
     *  @param enabledAfter  写库之后是否开启
     */
    static void applyFileShareSwitch(int64_t enterpriseId, bool enabledBefore, bool enabledAfter);

    /**
     *  向某企业的全体在线用户推 1013，包体是该企业当前的 features 对象。文件共享开关由 applyFileShareSwitch 调用；
     *  P2P 开关（docs/P2P通信方案.md 9.4）写库之后只调用本函数，已经建立的 P2P 会话不强制断开。
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>。
     *  @param enterpriseId 企业 id；不大于 0 时不推送
     */
    static void pushFeaturesNotify(int64_t enterpriseId);

    /**
     *  本连接下线（链路断开或退出登录）时的文件共享收尾：本连接作为提供端的共享清除在线提供状态，
     *  并向其成员与分享者推新状态（暂不可用）；结束涉及本连接的全部访问会话与传输子会话并通知另一端。
     *  共享记录本身不动。按本连接自己的标识收尾，不按 userid 反查，多端在线时不会误收另一端的会话。
     *  计算新状态时不把本连接算作分享者在线（退出登录时本会话还在在线会话表里）。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>，理由同 cleanupFolderTransferOnOffline。
     *  本连接没有提供任何共享、也没有参与任何访问时什么都不做，可安全重复调用。
     */
    void cleanupFileShareOnOffline();

    /**
     *  本连接下线（链路断开或退出登录）时的屏幕共享收尾：若本用户已没有其它登录态的连接，交给
     *  ScreenShareService::onUserOffline —— 观看者按离开处理，发起人则房间保留、进入宽限期。
     *  同一连接只处理一次（退出登录之后通常紧跟连接断开）；被顶号的旧会话 userid 已清零，直接跳过。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>，理由同 cleanupFolderTransferOnOffline。
     */
    void cleanupScreenShareOnOffline();

    /**
     *  本连接下线（链路断开或退出登录）时的通话收尾：若本用户已没有其它登录态的连接，给他参与的
     *  通话记下掉线时刻；超过掉线宽限期仍未重新登录时，由 main.cpp 的周期任务结束通话并通知双方。
     *  被顶号的旧会话 userid 已清零，直接跳过。可安全重复调用（掉线时刻以第一次为准）。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>，理由同 cleanupFolderTransferOnOffline。
     */
    void cleanupCallOnOffline();

    /**
     *  本连接下线（链路断开或退出登录）时的群通话收尾：若本用户已没有其它登录态的连接，交给
     *  GroupCallService::onUserOffline —— 振铃中的人从房间移除，已加入的人进入掉线宽限期，超时由 main.cpp 的周期任务
     *  按离开处理。同一连接只处理一次（退出登录之后通常紧跟连接断开）；被顶号的旧会话 userid 已清零，直接跳过。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>，理由同 cleanupFolderTransferOnOffline。
     */
    void cleanupGroupCallOnOffline();

    /**
     *  本连接下线（链路断开或退出登录）时的远程桌面收尾：若本用户已没有其它登录态的连接，给他参与的
     *  远程桌面会话记下掉线时刻；超过掉线宽限期仍未重新登录时，由 main.cpp 的周期任务结束会话并通知双方。
     *  被顶号的旧会话 userid 已清零，直接跳过。可安全重复调用（掉线时刻以第一次为准）。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>，理由同 cleanupFolderTransferOnOffline。
     */
    void cleanupRemoteDesktopOnOffline();

    /**
     *  本连接下线（链路断开或退出登录）时的 P2P 收尾（docs/协议设计-v2.md 18.3 的 2305）：移除本连接的 P2P 登记，
     *  结束它参与的全部 P2P 会话，给仍在线的每个对端推 2305（原因 p2p_close_peer_offline，错误码 903）。
     *  可安全重复调用：第二次调用时本连接已没有登记，不再推送。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>（要按连接标识查找对端会话）。
     */
    void cleanupP2POnOffline();

    /**
     *  本连接下线（链路断开或退出登录）时的密聊收尾（docs/密聊方案.md 4.3）：移除本连接的报到登记，结束它参与的
     *  全部密聊——进行中的给另一方推 2605（已结束、对方离线），它发起的待应答邀请给被邀请人推 2605（已取消）。
     *  可安全重复调用：第二次调用时本连接已没有登记，不再推送。
     *
     *  <b>调用约束：禁止在持有 ChatServer::m_sessionMutex 期间调用</b>（要按连接标识查找对端会话）。
     */
    void cleanupSecretOnOffline();

    /**
     *  发出密聊处理产出的帧：按连接标识找到会话再发送（发给请求者自己的应答也走这里）；找不到（对端恰好已断开）时
     *  记一条日志后丢弃。日志只记命令号与目标 userid，不记包体。静态函数：main.cpp 的邀请超时扫描与企业关闭开关的
     *  收尾没有请求者会话，也要经它发帧。
     *  @param frames 要发出的帧（SecretChatManager 各方法的产出）
     */
    static void sendSecretFrames(const std::vector<SecretOutFrame>& frames);

    /**
     *  企业关闭了单聊密聊开关后的收尾（docs/密聊方案.md 4.9）：结束该企业成员参与的全部密聊并推 2605（已结束、
     *  管理员已关闭）。由开关写库成功后的分流代码在推 1013 之前调用。
     *  @param enterpriseId 企业 id
     */
    static void endSecretChatsOfEnterprise(int64_t enterpriseId);

    /**
     *  本连接下线（链路断开或退出登录）时撤销它对帖子群的全部「正在查看」登记（docs/帖子群方案.md 6.1 的 2713）。
     *  可安全重复调用。
     */
    void cleanupThreadWatchOnOffline();

    //启用掉线检测定时器
    void enableHearbeatCheck();
    //停止掉线检测定时器
    void disableHeartbeatCheck();
    //定时器回调：若超过约定时间未收到任何数据包，则主动断开连接
    void checkHeartbeat(const std::shared_ptr<TcpConnection>& conn);

private:
    /**
     *  协议加密的收包入口：在业务分发之前处理握手帧、校验加密状态、把密文包体
     *  还原成明文。<u>必须在 zlib 解压之前调用</u>——线上顺序是先压缩后加密，
     *  收包时自然要先解密再解压。
     *
     *  @param conn    当前连接
     *  @param header  已解出的帧头，其中的字段会被用来构造附加认证数据
     *  @param body    入参为线上包体；本帧为密文且解密成功时，被替换为明文
     *  @param handled 出参。为 true 表示本帧是握手帧、已在本函数内应答完毕，
     *                 调用方不应再往下走解压与业务分发
     *  @return false 表示协议出错且<u>本函数已关闭连接</u>，调用方须立即返回
     */
    bool processFrameSecurity(const std::shared_ptr<TcpConnection>& conn,
                              const frame_header& header,
                              std::string& body, bool& handled);

    /**
     *  按 cmd 分发一个已解出的 v2 帧。
     *  @param conn 当前连接
     *  @param cmd  消息类型，取值见 enum msg_type
     *  @param seq  请求序列号，应答时回填
     *  @param body 包体（UTF-8 JSON 文本）
     *  @return false 表示遇到协议错误，调用方应关闭连接
     */
    bool process(const std::shared_ptr<TcpConnection>& conn,
                 uint16_t cmd, uint32_t seq, const std::string& body);

    //登录请求处理（cmd_login）
    void onLogin(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //心跳请求处理（cmd_heartbeat）
    void onHeartbeat(const std::shared_ptr<TcpConnection>& conn);
    //登出请求处理（cmd_logout）
    void onLogout(const std::shared_ptr<TcpConnection>& conn);
    //查询本账号当前在线设备列表（cmd_get_devices）
    void onGetDevices(const std::shared_ptr<TcpConnection>& conn);
    //踢掉本账号的指定在线设备（cmd_kick_device）
    void onKickDevice(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //注册新账号（cmd_register）
    void onRegister(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //发送短信验证码（cmd_send_sms_code）
    void onSendSmsCode(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //凭短信验证码重置密码（cmd_reset_password）
    void onResetPassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取通讯录请求处理（cmd_get_contacts）：部门树 + 部门成员 + 群组
    void onGetContacts(const std::shared_ptr<TcpConnection>& conn);
    //获取用户资料（cmd_get_user_info）
    void onGetUserInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //更新本人资料（cmd_update_profile）
    void onUpdateProfile(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //修改登录密码（cmd_change_password）
    void onChangePassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //修改头像（cmd_change_avatar）
    void onChangeAvatar(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //修改在线状态（cmd_change_presence）
    void onChangePresence(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //查找用户/群（cmd_search）
    void onSearch(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取发给本人的平台公告（cmd_get_announcements）：主面板「公告中心」的数据源，
    //见 docs/公告中心接入方案.md
    void onGetAnnouncements(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //上报某条平台公告已读（cmd_mark_announcement_read）：写 t_announcement_read，
    //管理控制台的「已读进度」统计的就是这张表。
    void onMarkAnnouncementRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //设置联系人备注名（cmd_set_friend_remark）：落 t_contact_remark，见 ContactRemarkManager
    void onSetFriendRemark(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //设置联系人标签（cmd_set_contact_tags）：整份覆盖某个联系人的标签列表，落 t_contact_tag，
    //见 ContactTagManager。标签与备注名一样是单向的，只影响本人这一侧的显示
    void onSetContactTags(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //加入/移出黑名单（cmd_set_blacklist）
    void onSetBlacklist(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //添加/取消收藏（cmd_set_favorite）：收藏对象为联系人或群组，见 docs/收藏接入方案.md
    void onSetFavorite(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //发送消息（cmd_send_message）：按 ctype 分流到单聊 / 群聊
    void onSendMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //发送群聊消息：onSendMessage 在 ctype=2 时调用
    void sendGroupMessage(const SendMessageRequest& req);
    //本人引用某个 fileid 发出消息、或把它设为头像之后调用：若本人正是该文件的上传者，
    //把文件记录标记为已完成，使后续同内容、同场景的上传能命中秒传；不是上传者（例如
    //转发别人的图片）时不改动记录。客户端总是上传成功后才引用 fileid，故此时文件
    //已完整落在文件服务器上。
    //  fileid：被引用的文件ID
    //  source：引用来源，只用于日志（如 "message"、"avatar"、"group avatar"）
    void markReferencedFileCompleted(int64_t fileid, const char* source);
    //消息落库成功后调用：从 content 里取出所引用的 fileid，交给 markReferencedFileCompleted；
    //消息类型不带文件、或 content 里没有合法的 fileid 时什么都不做。
    //  msgtype：消息类型，取值见 enum message_type
    //  content：content 对象的紧凑 JSON 文本
    void markMessageFileCompleted(int32_t msgtype, const std::string& content);
    //收到客户端送达回执（cmd_message_received_ack）
    void onReceivedAck(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //同步最近会话列表（cmd_sync_conversations）
    void onSyncConversations(const std::shared_ptr<TcpConnection>& conn);
    //按锚点拉取某会话的历史消息（cmd_sync_messages）
    void onSyncMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //按改写序号拉取本人可见范围内被改写过或被撤回的消息（cmd_sync_rewritten_messages）
    void onSyncRewrittenMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    /**
     *  下发一条历史消息之前按本人视角加工它：卡片消息（投票 / 接龙 / 待办 / 文件共享 / 入群邀请）合并当前状态，
     *  本人发出的文件消息补上对方的收取状态。cmd_sync_messages 与 cmd_sync_rewritten_messages 共用，两者的
     *  条目因此逐字段一致；须在按体积截断之前调用（合并后体积会变大）。
     *    rec：           [入参 / 出参] 一条消息
     *    nowMs：         服务端当前时间，Unix毫秒，判断接龙是否已截止用
     *    cardAugments：  [入参 / 出参] 合并了卡片状态时加一（计时日志用）
     *    fileAckLookups：[入参 / 出参] 查了文件收取状态时加一（计时日志用）
     */
    void augmentSyncedRecord(MessageRecord& rec, int64_t nowMs, unsigned& cardAugments, unsigned& fileAckLookups);
    //上报会话已读位点（cmd_mark_read）
    void onMarkRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //撤回一条消息（cmd_recall_message）
    void onRecallMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //Pin / 取消 Pin 一条消息（cmd_pin_message）。Pin 是会话内共享的状态：单聊推双方、
    //群聊推全体成员。群里谁都能钉，但只有钉它的人本人与群主 / 管理员能取消
    void onPinMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取某会话当前被 Pin 的消息（cmd_get_pinned_messages），带完整消息体，
    //供客户端把本地库里缺的那些老消息补齐
    void onGetPinnedMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //转发"正在输入"提示（cmd_typing_notify）
    void onTypingNotify(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件收取状态上报（cmd_file_transfer_ack）：接收方告知"开始接收/已接收/已拒绝"，
    //校验其确为该文件消息的收件人后落库，并转推给发送方
    void onFileTransferAck(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //远程桌面：控制端发起邀请（校验对端在线 → 建会话 → 回应答 → 推送邀请给被控端）
    void onRemoteDesktopInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //远程桌面：被控端应答邀请（接受则给双方签 relay 令牌并下发中转参数，拒绝则通知控制端）
    void onRemoteDesktopResponse(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //远程桌面：控制端在接通前取消邀请（通知被控端、清会话）
    void onRemoteDesktopCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //远程桌面：任一端结束会话（通知对端、清会话）
    void onRemoteDesktopClose(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //音视频通话：主叫发起呼叫（校验对端在线 / 不忙 → 建会话 → 回应答 → 推送邀请给被叫）
    void onCallInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //音视频通话：被叫应答（接听则置接通态、给双方签 relay 令牌并下发中转参数；拒绝则通知主叫）
    void onCallResponse(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //音视频通话：主叫在接通前取消呼叫（通知被叫、清会话）
    void onCallCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //音视频通话：接通后任一端挂断（通知对端、清会话）
    void onCallHangup(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //单聊文件夹传输：发送端发起邀请（cmd_folder_invite）。规模上限、双方是否已有传输在进行、
    //服务器活跃会话数三项准入校验集中在 FolderTransferManager::checkInvite 里，本方法只负责
    //判对端在线、建会话、回应答（带 sessionid），并向接收端推 cmd_folder_invite_notify
    void onFolderInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //单聊文件夹传输：接收端应答邀请（cmd_folder_response）。同意则把会话置为传输中、给双方
    //各签一枚角色相反的中转令牌并分别下发 cmd_folder_relay_info；拒绝则向发送端推
    //cmd_folder_state_notify（已拒绝）并立即销毁会话
    void onFolderResponse(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //单聊文件夹传输：发送端在接收端应答前撤回邀请（cmd_folder_cancel）。只在会话仍处于
    //响铃中时可用；传输开始之后要取消须走 cmd_folder_close，错误码填 error_code_folder_canceled
    void onFolderCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //单聊文件夹传输：任一端结束本次传输（cmd_folder_close）。成功结束即向对端推"已完成"
    //并销毁会话；失败结束则置为失败态并<u>保留会话</u>（保留期内发送端可重试续传），
    //同时把请求里的 errcode / errmsg 原样透传给对端，保证两端看到同一条失败原因
    void onFolderClose(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //单聊文件夹传输：发送端在保留期内重试续传（cmd_folder_retry）。受理条件由
    //FolderTransferManager::canRetry 统一判定，通过后重新签发两枚令牌、向双方各再推一帧
    //cmd_folder_relay_info，接收方不再被询问是否同意
    void onFolderRetry(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  给一笔文件夹传输的两端各签一枚中转令牌，并各推一帧 cmd_folder_relay_info。
     *  接收端同意（1812）与保留期内重试（1817）都要做这件事，抽成一处避免两边口径不一致。
     *  推送之前先为两端建 P2P 会话：文件夹传输用途 folder（建不成照样下发，客户端走 fileserver 中转）；
     *  单文件直传用途 file（2026-10-04 起），建不成则不下发 1816，直接返回 622。
     *  @param s   目标会话的快照，读取其 sessionid、双方 userid 与传输种类
     *  @param now 当前时间，Unix 毫秒；用于计算令牌的过期时刻
     *  @return error_code_ok 表示已推送；error_code_server_internal 表示会话已不在会话表里（令牌签发失败）；
     *          error_code_folder_direct_unavailable 表示单文件直传建不起 P2P 会话（未推送）
     */
    int pushFolderRelayInfo(const FolderTransferSessionInfo& s, int64_t now);

    //文件共享：发起共享（cmd_share_create）。重发先按 clientshareid 去重；规则判定在
    //FileSharePolicy::checkCreate，本方法负责查好事实、写库、生成只给成员看的卡片并逐人推送
    void onShareCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：取消共享（cmd_share_cancel）。权限判定在 FileSharePolicy::checkCancel，收尾统一走
    //cancelShareAndNotify
    void onShareCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：调整成员（cmd_share_update_members）。新成员另发一张只给他们看的卡片，被移除的人推
    //「已失效」并结束其访问会话，权限有变化的人推 cmd_share_perms_notify
    void onShareUpdateMembers(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：拉取共享列表（cmd_share_list），每项带上该共享对请求者的状态
    void onShareList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：分享者报告各共享能否提供（cmd_share_serve_report）。只接受从共享绑定的那台电脑发来的
    //报告（FileSharePolicy::acceptServeReport），状态有变化时向成员与分享者推 cmd_share_state_notify
    void onShareServeReport(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：访问者打开共享（cmd_share_open）。判定在 FileSharePolicy::checkOpen，通过后建访问会话，
    //向访问者本连接与分享者提供该共享的那条连接各推一帧 cmd_share_relay_info
    void onShareOpen(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：任一端结束访问会话（cmd_share_close_access），连带结束其传输，通知另一端
    void onShareCloseAccess(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：访问者申请一次下载或上传（cmd_share_request_transfer）。判定在 FileSharePolicy::checkTransfer，
    //名额在 FileShareSessionManager::requestTransfer，通过后向两端推传输子会话的中转参数，并记审计
    void onShareRequestTransfer(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：任一端结束传输子会话（cmd_share_transfer_close），通知另一端
    void onShareTransferClose(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //文件共享：分享者客户端批量上报执行结果（cmd_share_audit_report），只接受分享者本人的记录，补上服务端时间后入库
    void onShareAuditReport(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  按文件共享的连接标识（shareConnKey）在在线会话里找到对应的会话。
     *  @param key 连接标识
     *  @return 找到的会话；该连接已断开时返回空指针
     */
    static std::shared_ptr<ChatSession> findSessionByShareConnKey(ShareConnKey key);

    //P2P 信令（cmd_p2p_* 2301~2306，docs/协议设计-v2.md 第 18 节）：组装请求上下文交给 P2PSignalService，
    //企业开关经 P2PFeatureGate 查询，再把产出的应答与推送逐帧发出
    void onP2PCommand(uint16_t cmd, const std::string& body);

    //单聊密聊（cmd_secret_* 2601~2609，docs/密聊方案.md 第 5 节）：组装请求上下文交给 SecretChatManager，企业开关、
    //所属企业与黑名单经 ChatSessionSecret.cpp 里的查询对象取，再把产出的应答与推送逐帧发出。本段的包体不写日志
    void onSecretCommand(uint16_t cmd, const std::string& body);

    //帖子群（cmd_thread_* 2701~2716，docs/帖子群方案.md 第 6 节与 14.6），实现在 ChatSessionThread.cpp。
    //onThreadCommand 按命令号分给下面各处理函数，各函数的参数都是请求包体
    void onThreadCommand(uint16_t cmd, const std::string& body);
    void onThreadListPosts(const std::string& body);       //2701 拉一页帖子
    void onThreadGetPost(const std::string& body);         //2702 拉一条完整帖子
    void onThreadPublishPost(const std::string& body);     //2703 发帖
    void onThreadEditPost(const std::string& body);        //2704 修改帖子
    void onThreadDeletePost(const std::string& body);      //2705 删除帖子
    void onThreadPinPost(const std::string& body);         //2706 置顶 / 取消置顶
    void onThreadListComments(const std::string& body);    //2707 拉一页留言
    void onThreadListReplies(const std::string& body);     //2708 拉一页回复
    void onThreadAddComment(const std::string& body);      //2709 留言或回复
    void onThreadDeleteComment(const std::string& body);   //2710 删除留言或回复
    void onThreadReact(const std::string& body);           //2711 点赞 / 表情回应
    void onThreadGetReactors(const std::string& body);     //2712 拉互动名单
    void onThreadWatch(const std::string& body);           //2713 登记 / 撤销「正在查看」
    void onThreadAddAddendum(const std::string& body);     //2714 楼主追加补充
    void onThreadDeleteAddendum(const std::string& body);  //2715 删除补充
    void onThreadCheckCards(const std::string& body);      //2716 核对本地卡片

    /**
     *  把一张帖子卡片或动态卡片改写为已删除形态并落库（删帖、删留言、删补充之后，docs/帖子群方案.md 14.6）。
     *  已经是已删除形态的卡片也照常返回它的当前内容，便于重复推送。
     *  @param msgid 卡片的 msgid，大于 0
     *  @param out   [出参] 改写后的卡片
     *  @return 卡片存在、类型是 25 或 26、改写成功返回 true；失败时记日志
     */
    bool rewriteThreadCardDeleted(int64_t msgid, ThreadCardState& out);

    /**
     *  把一批改写过的卡片经 2721 推给该群全体在线成员；cards 为空时不推。
     */
    void notifyThreadCards(int64_t groupid, const std::vector<ThreadCardState>& cards);

    /**
     *  帖子群只读请求的公共校验：本人企业开着帖子群开关、群存在且是帖子群、本人是群成员。
     *  @param groupid 帖子群
     *  @param group   [出参] 群资料
     *  @param myrole  [出参] 本人角色；不是成员时为 -1
     *  @return error_code_ok 或对应错误码（1300 / 群不存在 / 1305 / 不是群成员）
     */
    int checkThreadAccess(int64_t groupid, GroupRecord& group, int& myrole);

    /**
     *  帖子群写请求（发帖、修改、留言、补充）的公共校验：在 checkThreadAccess 之外另查禁言与入群必读，
     *  判定与群聊发言（checkGroupSpeak）相同。
     *  @param cmd     请求的命令号，只用于日志
     *  @return error_code_ok 或对应错误码
     */
    int checkThreadSpeak(uint16_t cmd, int64_t groupid, GroupRecord& group, int& myrole);

    /**
     *  把一帧 2720 推给登记了正在查看该帖子群的连接（留言、回复与互动变化用）。
     */
    void notifyThreadWatchers(int64_t groupid, const std::string& body);

    /**
     *  在帖子群里写一张帖子卡片（msgtype 25）并推给全体成员。
     *  @param post 刚发出的帖子（postid、groupid、author、body、createTime 已填）
     *  @return 卡片的 msgid；写库失败返回 0
     */
    int64_t sendThreadPostCard(const ThreadPost& post);

    /**
     *  在帖子群里写一张帖子动态卡片（msgtype 26），只对 audience 里的人可见，并推给他们与本人。
     *  @param groupid     帖子群
     *  @param postid      帖子
     *  @param commentid   留言或回复（补充时为 0）
     *  @param addid       补充（留言、回复时为 0）
     *  @param postTitle   帖子标题（没有标题时为正文摘要）
     *  @param digest      本次内容的摘要
     *  @param kind        种类，取值见 thread_reply_kind
     *  @param audience    可见者（已去掉本人）；为空时不写卡片
     *  @param mentions    @ 到的人
     *  @param createTime  时间，Unix 毫秒
     *  @param clientMsgId 卡片的 clientmsgid
     *  @param images      补充的图片（写进卡片，消息记录显示缩略图，docs/帖子群方案.md 14.5）；留言与回复为空
     *  @param mentionAll  留言、回复或补充是否 @ 了所有人（§15）：为真时卡片 content 与消息记录都带 mentionall，
     *                     调用方须已把全体成员并入 audience（ThreadProtocol::mergeMentionAllAudience）
     *  @return 卡片的 msgid；不写或写库失败返回 0
     */
    int64_t sendThreadReplyCard(int64_t groupid, int64_t postid, int64_t commentid, int64_t addid,
                                const std::string& postTitle, const std::string& digest, int kind,
                                const std::vector<int64_t>& audience, const std::vector<int64_t>& mentions,
                                int64_t createTime, const std::string& clientMsgId,
                                const std::vector<ThreadImage>& images, bool mentionAll);

    /**
     *  发出 P2P 信令处理产出的帧：发给本连接的直接发送，发给别的连接的按连接标识找到会话再发送；
     *  找不到（对端恰好已断开）时记一条日志后丢弃。
     *  @param frames 要发出的帧（P2PSignalService::handle / onDisconnect 的产出）
     */
    void sendP2PFrames(const std::vector<P2POutFrame>& frames);

    /**
     *  按 P2P 的连接标识（p2pConnKey）在在线会话里找到对应的会话。
     *  @param key 连接标识
     *  @return 找到的会话；该连接已断开时返回空指针
     */
    static std::shared_ptr<ChatSession> findSessionByP2PConnKey(uint64_t key);

    /**
     *  查齐「某人看某个共享」所需的事实：是否成员与权限、是否仍在群里、分享者是否在线、
     *  是否在绑定的电脑上、在线提供状态。
     *  @param share    共享记录
     *  @param viewerId 看的人 userid
     *  @param facts    [出参] 事实
     */
    static void loadShareViewerFacts(const ShareRecord& share, int64_t viewerId, ShareViewerFacts& facts);

    /**
     *  批量查齐若干人看同一个共享时的事实。与逐人调用 loadShareViewerFacts 的结果相同，但成员表、
     *  群成员表与分享者的在线情况各只查一次，群里人多时开销与人数无关。
     *  @param share       共享记录
     *  @param users       看的人
     *  @param out         [出参] 与 users 一一对应的事实
     *  @param excludeConn 判断分享者是否在线时不计入的那条连接（正在下线的连接）；0 表示不排除
     */
    static void loadShareViewerFactsForUsers(const ShareRecord& share, const std::vector<int64_t>& users,
                                             std::vector<ShareViewerFacts>& out, ShareConnKey excludeConn = 0);

    /**
     *  向若干人各推一帧该共享对他本人的状态（cmd_share_state_notify），每人的内容按他自己的身份计算。
     *  @param share       共享记录
     *  @param users       推送对象
     *  @param excludeConn 判断分享者是否在线时不计入的那条连接（正在下线的连接）；0 表示不排除
     */
    static void pushShareStateToUsers(const ShareRecord& share, const std::vector<int64_t>& users,
                                      ShareConnKey excludeConn = 0);

    /**
     *  为一个共享生成一张卡片消息并落库（可见范围为 audience），再向推送名单里的每个人各推一帧
     *  按他本人合并过状态的 cmd_message_notify。
     *  @param share    共享记录
     *  @param audience 卡片的可见范围（FileSharePolicy::cardAudience 的结果），不含分享者
     *  @param nowMs    当前时间（Unix 毫秒），作为消息时间
     *  @return 新卡片的 msgid；落库失败返回 0（不推送）
     */
    static int64_t sendShareCard(const ShareRecord& share, const std::vector<int64_t>& audience, int64_t nowMs);

    /**
     *  通知被结束的访问会话的两端（cmd_share_access_state_notify），收到即表示该访问会话及其传输已经结束。
     *  @param closed  被结束的访问会话（FileShareSessionManager 批量结束方法的返回值）
     *  @param errCode 结束原因，如 error_code_share_canceled、error_code_share_not_member
     */
    static void notifyShareAccessEnded(const std::vector<ShareAccessInfo>& closed, int errCode);

    /**
     *  写一条服务端亲眼所见的审计记录（发起、取消、调整成员等），写入失败只记日志。
     *  @param share   共享记录
     *  @param actorId 操作人
     *  @param action  动作，取值见 share_audit_action
     *  @param path     附加说明（如调整成员时的名单摘要）；没有时传空串
     *  @param nowMs    当前时间（Unix 毫秒）
     *  @param accessId 所属访问会话 id（打开共享时填写）；与访问会话无关的动作传空串
     */
    static void addShareServerAudit(const ShareRecord& share, int64_t actorId, int action,
                                    const std::string& path, int64_t nowMs,
                                    const std::string& accessId = std::string());

    //本连接作为文件共享的提供端时，在 FileShareManager 在线提供状态表里使用的连接标识（取本会话对象的地址）
    ShareConnKey shareConnKey() const;

    //本连接在 P2PSessionManager 里的连接标识（取本会话对象的地址，类型同 P2PConnKey）
    uint64_t p2pConnKey() const;

    /**
     *  把共享卡片（message_type_fileshare）按本会话的登录用户合并状态与权限（同步、搜索等下发消息前调用）。
     *  卡片正文解析不了、或共享记录已经查不到时原样保留，不让一条坏消息挡住整次同步。
     *  @param message [入出参] 卡片消息，content 被替换为合并后的正文
     */
    void augmentShareCardRecord(MessageRecord& message);

    /**
     *  把入群邀请卡片（message_type_group_invite）的显示状态与群的当前资料合并进 content（docs/邀请入群方案.md §4.3）。
     *  状态按落库状态、到期时间、群是否还在、邀请人与被邀请人此刻是否在群里算出（GroupInviteProtocol::computeDisplayState）。
     *  卡片正文解析不了、或邀请记录已经查不到时原样保留。
     *  @param message [入出参] 卡片消息，content 被替换为合并后的正文
     *  @param nowMs   当前时间，Unix 毫秒
     */
    void augmentGroupInviteCardRecord(MessageRecord& message, int64_t nowMs);

    //通话中切换媒体类型（cmd_call_switch_media）：校验由 CallManager::switchMedia 统一做，
    //本方法只负责回应答、并在真的发生变更时向双方（含发起者）推 cmd_call_media_changed_notify
    void onCallSwitchMedia(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //屏幕共享客户端发来的八个请求（cmd_screenshare_* 中的发起、加入、离开、结束、查询、成员媒体、设置权限、
    //录制状态）：交给 ScreenShareService 处理，再把它给出的应答与中转参数按顺序发回本连接
    void onScreenShare(uint16_t cmd, const std::string& body);
    //云端录制的列表（1755）与操作（1756）（第 26 期）：交给 CloudRecordService 处理，把应答发回本连接。
    //1754（共享中的开始 / 暂停 / 继续 / 停止）与房间状态有关，走 onScreenShare
    void onCloudRecord(uint16_t cmd, const std::string& body);
    //群通话客户端发来的请求（cmd_groupcall_* 中的发起、加入、拒绝、离开、追加邀请、成员媒体、查询、管理）：
    //交给 GroupCallService 处理，再把它给出的应答与中转参数按顺序发回本连接
    void onGroupCall(uint16_t cmd, const std::string& body);
    //设置会话置顶/消息提醒模式/删除（cmd_set_conversation）
    void onSetConversation(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //批量设置多个会话的消息提醒模式（cmd_set_notify_modes），见 docs/消息提醒模式与消息盒子方案.md §5.4
    void onSetNotifyModes(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    /**
     *  校验能否把与某位联系人的单聊设为某种提醒模式（cmd_set_conversation 与 cmd_set_notify_modes 共用）。
     *  设回允许提醒（notify_mode_allow）一律放行，便于清掉对方离职后留下的设置；其余模式要求对方
     *  存在、不是自己、且与本人同属一个企业。
     *  @param peerId 单聊对端 userid
     *  @param mode   目标模式，取值已由 MessageProtocol::isValidNotifyMode 校验
     *  @return error_code_ok；对象是自己时 error_code_bad_request；用户不存在时 error_code_not_found；
     *          不是同企业同事时 error_code_not_friend
     */
    int checkSingleChatNotifyTarget(int64_t peerId, int mode);
    //会话内消息记录搜索（cmd_search_messages）
    void onSearchMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    /**
     *  本人在某个群里能看到的历史消息范围（docs/邀请入群方案.md §5.3）：时间下界在群设为「新成员不可查看入群前的
     *  历史消息」时取本人的入群时间，msgid 下界取邀请时按「同步最近 N 条」算出的边界；本人是群主或管理员时两项
     *  都不设（规则见 GroupProtocol::computeHistoryBound）。同步群历史（cmd_sync_messages）、会话内搜索
     *  （cmd_search_messages）、Pin 列表（cmd_get_pinned_messages）用这一份；全局搜索（cmd_global_search）
     *  一次取全部群的成员行，按同一规则逐群计算。
     *  @param groupid 群 id；调用方须已确认本人是群成员（不是成员时返回两项都为 0 的结果）
     *  @return 两个下界；为 0 的一项表示不限
     */
    GroupHistoryBound groupHistoryBound(int64_t groupid);

    /**
     *  邀请新成员入群时，按邀请人选择的同步条数算新成员的历史边界（写进 t_group_member.f_history_from）。
     *  群设为「新成员不可查看入群前的历史消息」时以群设置为准，不另设边界（docs/邀请入群方案.md §3.2：群设置优先）。
     *  @param group        目标群的资料
     *  @param historyLimit 同步条数，取值见 kInviteHistoryLimitAll / kInviteHistoryLimitMax
     *  @return 历史边界；0 表示不限（含查库失败的情况）
     */
    int64_t inviteHistoryFrom(const GroupRecord& group, int historyLimit);
    //全局搜索（cmd_global_search）：在本人能看到的全部聊天记录里按关键字搜索、按会话汇总，并附带
    //本人所在群的群公告命中（报文见 GlobalSearchProtocol.h）
    void onGlobalSearch(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //转发 / 合并转发消息（cmd_forward_message）
    void onForwardMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除本端消息（cmd_delete_message）；成功后向本人全部在线设备推送单条形式的
    //cmd_messages_deleted_notify
    void onDeleteMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //按时间范围清理本人的聊天记录（cmd_delete_messages_by_time）：校验并截断区间、并入
    //t_message_deleted_range，成功后向本人全部在线设备推送区间形式的 cmd_messages_deleted_notify。
    //方案见 docs/清理聊天记录方案.md §5
    void onDeleteMessagesByTime(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //群发助手：一条消息发给多个对端（cmd_mass_send）
    void onMassSend(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //创建群聊（cmd_create_group）
    void onCreateGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //解散群聊（cmd_dismiss_group）
    void onDismissGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //获取群资料（cmd_get_group_info）
    void onGetGroupInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //获取群成员列表（cmd_get_group_members）
    void onGetGroupMembers(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取本人加入的全部群列表（cmd_get_user_groups），无请求体
    void onGetUserGroups(const std::shared_ptr<TcpConnection>& conn);
    //修改群资料（cmd_update_group_info）
    void onUpdateGroupInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //邀请好友入群（cmd_invite_to_group）
    void onInviteToGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  cmd_accept_group_invite（1327）：被邀请人点入群邀请卡片上的「加入群聊」（docs/邀请入群方案.md §3.4、§4.4）。
     *  按 GroupInviteProtocol::decideAccept 的判定直接入群（走 completeGroupJoin，提示里的邀请人取卡片的邀请人）、
     *  转为加群申请（来源好友邀请、推荐人为邀请人、带同步条数与邀请 id）或回错误码；卡片状态有变化时推 1328。
     */
    void onAcceptGroupInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  「邀请入群需被邀请人同意」开启时，给一位被邀请人发入群邀请卡片：写一行 t_group_invite，在邀请人与被邀请人的
     *  单聊里生成一条 message_type_group_invite 消息并推给双方（docs/邀请入群方案.md §3.3、§5.4）。
     *  @param group        目标群的资料（群名、头像写进卡片）
     *  @param invitee      被邀请人 userid
     *  @param historyLimit 邀请人选择的同步条数，随邀请记下，入群时再计算历史边界
     *  @param nowMs        当前时间，Unix 毫秒
     *  @return 卡片发出返回 true；写库失败返回 false（已记错误日志）
     */
    bool sendGroupInviteCard(const GroupRecord& group, int64_t invitee, int historyLimit, int64_t nowMs);

    /**
     *  向邀请人与被邀请人的全部在线设备推 cmd_group_invite_changed_notify（1328）。
     *  @param invite 邀请（用其中的 id、卡片 msgid、邀请人与被邀请人）
     *  @param state  新的显示状态，取值见 group_invite_state
     */
    void notifyGroupInviteChanged(const GroupInviteRecord& invite, int state);

    /**
     *  cmd_get_group_announcements：拉取本群公告列表（分页）。
     *  全体群成员都可以拉 —— 公告本来就是发给所有人看的。
     */
    void onGetGroupAnnouncements(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  cmd_publish_group_announcement：发布新公告 / 重新发布一条历史公告。
     *  仅群主 / 管理员可发。成功后向全群推 cmd_group_announcement_notify 与
     *  cmd_group_info_changed_notify（后者因为 t_group 的公告快照四列变了），
     *  并往群里插一条 message_type_system 的系统提示。
     */
    void onPublishGroupAnnouncement(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  cmd_delete_group_announcement：删除一条公告（真删，不可恢复）。
     *  仅群主 / 管理员可删，且可删任意一条（不限于自己发的）。成功后推送同上。
     */
    void onDeleteGroupAnnouncement(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  公告发布 / 删除后的统一收尾：推 cmd_group_announcement_notify，再读回最新
     *  群资料推一条 cmd_group_info_changed_notify。
     *
     *  两条推送都要发是有原因的：前者给公告界面（边栏列表、群管理页、弹窗）用，
     *  后者给所有"只认群资料"的展示处（群资料对话框、群聊窗头部摘要行）用 ——
     *  t_group 上的公告快照四列确实变了，不推这一条它们就还显示着旧公告。
     *
     *    item：       变更涉及的那条公告（publishername 须已补好）；
     *    changetype： 取值见 group_announcement_change_type；
     *    cardMsgId：  删除推送时这条公告的卡片 msgid，0 表示没有卡片（发布推送、或本功能上线前发布的公告）；
     *    cardContent：删除推送时改写后的卡片 content，空串表示不带。
     */
    void notifyAnnouncementChanged(const GroupAnnouncementRecord& item, int changetype,
                                   int64_t cardMsgId = 0, const std::string& cardContent = std::string());

    /**
     *  公告发布后，以发布人的名义往群里写一张群公告卡片（message_type_announcement）并推给全体成员
     *  （2026-10-05 起取代原先那条「「张三」发布了群公告：xxx」系统提示，docs/群公告接入方案.md §13）。
     *
     *  推送只能触达此刻在线的人，而卡片会进历史与会话预览 —— 当时不在线的成员之后进群也看得到。
     *  发送者是发布人而不是系统，所以卡片对其他成员计未读，并按各自的会话提醒模式提醒。
     *    item：     刚发布的那条公告（publishername 须已补好）；
     *    republish：是否由「重新发布」产生，卡片据此写「重新发布」。
     *  返回：卡片的 msgid；落库失败时返回 0（已记错误日志，不推送）。
     */
    int64_t sendAnnouncementCard(const GroupAnnouncementRecord& item, bool republish);

    /**
     *  公告被删除后，把它的卡片改写为已删除形态（去掉正文、带 deleted），见 docs/群公告接入方案.md §13.3。
     *  卡片按「发布人 + announcementCardClientMsgId」找回；本功能上线前发布的公告没有卡片，什么也不改。
     *    item：      被删除的那条公告（删除之前读出的完整内容）；
     *    outContent：[出参] 改写后的卡片 content；没有卡片或改写失败时为空串。
     *  返回：卡片的 msgid；没有卡片或改写失败时返回 0。
     */
    int64_t rewriteAnnouncementCardAsDeleted(const GroupAnnouncementRecord& item, std::string& outContent);

    // ---------------- 备忘与提醒（实现在 ChatSessionMemo.cpp，docs/备忘与提醒方案.md） ----------------

    /**
     *  cmd_memo_list：拉取本人可见的备忘。请求不带 groupid 时返回本人全部个人备忘与所在各群里本人可见的群备忘；
     *  带 groupid 时只返回该群里本人可见的群备忘，不是该群成员回 error_code_not_group_member。
     */
    void onMemoList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  cmd_memo_save：新建或修改一条备忘。个人备忘归本人；群备忘只有群主、管理员能保存，提醒对象须都是本群成员，
     *  标题与备注要过企业敏感词。成功后应答回带完整备忘，并按可见范围的变化推 cmd_memo_notify。
     */
    void onMemoSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  cmd_memo_delete：删除一条备忘（真删）。权限同保存；成功后向删除前能看到它的人推删除。
     */
    void onMemoDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  保存备忘之后的推送：after 里的人收到新增或修改，before 有而 after 没有的人收到删除。
     *    memo：  保存后的备忘；
     *    before：保存前能看到它的人，升序（新建时为空）；
     *    after： 保存后能看到它的人，升序。
     */
    void pushMemoChange(const MemoRecord& memo, const std::vector<int64_t>& before,
                        const std::vector<int64_t>& after);

    /**
     *  群备忘保存成功后，以本次操作人的名义往该群写一张群备忘卡片（message_type_memo），只给这条备忘的
     *  可见者看（docs/备忘与提醒方案.md §7）。个人备忘不调用本函数。
     *    memo：  保存后的群备忘（memoid、editor、updateTime 须已填好）；
     *    change：这次操作，取值见 memo_card_change；
     *    after： 保存后能看到这条备忘的人（collectViewers 的结果），升序。提醒全体成员时卡片全员可见、推给全体成员，
     *            否则只写入并推给这些人（操作人本人是群主或管理员，一定在其中）。
     *  返回：卡片的 msgid；落库失败时返回 0（已记错误日志，不推送）。
     */
    int64_t sendMemoCard(const MemoRecord& memo, int change, const std::vector<int64_t>& after);

    /**
     *  有人离开群（退群或被踢）之后的备忘处理：按离开前的角色，给他原先能看到的群备忘推删除；把他从各条群备忘的
     *  提醒对象里移除，并给仍能看到这些备忘的人推一次修改。在成员行删除成功之后调用，失败只记日志。
     *    groupid：群 id；
     *    leavers：离开的人与他们离开前的角色。
     */
    void handleMemoMembersLeftGroup(int64_t groupid, const std::vector<MemoMemberRole>& leavers);

    /**
     *  有人加入群之后的备忘处理：给新成员推本群「提醒全体成员」的那些群备忘。在成员行写入成功之后调用。
     *    groupid：群 id；
     *    joiners：新加入的成员 userid。
     */
    void handleMemoMembersJoinedGroup(int64_t groupid, const std::vector<int64_t>& joiners);

    /**
     *  某成员角色变化（设置或取消管理员、转让群主）之后的备忘处理：按新旧角色补推他新能看到的、推删除他从此看不到的。
     *  只有「是不是群主或管理员」变化时才会有推送。
     *    groupid / userid：群 id 与该成员；
     *    oldRole / newRole：变化前后的角色，取值见 group_member_role。
     */
    void handleMemoRoleChanged(int64_t groupid, int64_t userid, int oldRole, int newRole);

    /**
     *  群解散之后的备忘处理：按解散前的成员名单给能看到各条群备忘的人推删除，再删除该群全部群备忘。
     *    groupid：群 id；
     *    members：解散前的成员与角色。
     */
    void handleMemoGroupDismissed(int64_t groupid, const std::vector<MemoMemberRole>& members);
    //申请加入群聊（cmd_join_group_request）
    void onJoinGroupRequest(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //审批加群申请（cmd_handle_join_group）
    void onHandleJoinGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取待审批加群申请列表（cmd_get_group_join_requests）
    void onGetGroupJoinRequests(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //退出群聊（cmd_leave_group）
    void onLeaveGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //移出群成员（cmd_kick_group_member）
    void onKickGroupMember(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //设置/取消管理员（cmd_set_member_role）
    void onSetMemberRole(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //禁言/解禁单个成员（cmd_mute_member）
    void onMuteMember(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //全员禁言开关（cmd_mute_all）
    void onMuteAll(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //修改本人群名片（cmd_set_member_card）
    void onSetMemberCard(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //设置本人在某个群里关注的成员（cmd_set_group_follow，整份覆盖）：校验本人是群成员、名单不超过
    //kGroupFollowMax 人、不含自己且都是本群成员，写库后读回名单经 cmd_group_follow_changed_notify
    //推给本人全部在线设备（docs/消息提醒模式与消息盒子方案.md §12.4）
    void onSetGroupFollow(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //设置 / 取消入群必读（cmd_set_must_read，仅群主与管理员）：公告须属于本群；成功后推群资料变更，取消时被解除限制的人
    //另推成员变更（changetype = 7）（docs/群公告接入方案.md §14）
    void onSetMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //本人确认已阅读入群必读（cmd_confirm_must_read）：之后可以在本群发言，推成员变更（changetype = 7）
    void onConfirmMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //群主 / 管理员代为确认入群必读（cmd_release_must_read），推成员变更（changetype = 7）
    void onReleaseMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //取本群的入群必读与本人是否待确认（cmd_get_must_read），群聊窗打开时调用
    void onGetMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //给一批新成员各写一张只有本人可见的入群必读卡片（系统的名义、f_audience 只登记这位新成员），并推给他本人
    //   group：      所在群（mustreadid 须大于 0）
    //   newMembers： 新成员
    //   nowMs：      当前时间，Unix 毫秒
    void sendMustReadCards(const GroupRecord& group, const std::vector<int64_t>& newMembers, int64_t nowMs);
    //拉取本群问答（cmd_get_group_qa）：本群问答加上应用到本群的企业问答，按 GroupQaProtocol::mergeForGroup 合并
    //（docs/群问答方案.md）
    void onGetGroupQa(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //新增 / 修改一条本群问答（cmd_save_group_qa，仅群主与管理员）：校验长度、上限与企业敏感词，成功后推 1339
    void onSaveGroupQa(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除一条本群问答（cmd_delete_group_qa，仅群主与管理员），成功后推 1339
    void onDeleteGroupQa(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    // ---------------- 机器人（ChatSessionBot.cpp，docs/机器人方案.md 第 6 节） ----------------
    //拉取本群可用的机器人（cmd_get_group_bots）：群成员才能拉；帖子群与关闭了机器人的企业返回空列表
    void onGetGroupBots(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取一个机器人及其规则（cmd_get_bot_detail）：群机器人只给群主，个人机器人只给主人
    void onGetBotDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //新建或修改群机器人（cmd_save_group_bot，只有群主）：校验开关、数量、规则与敏感词，成功后推 2820 并写群系统提示
    void onSaveGroupBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除群机器人（cmd_delete_group_bot，只有群主），成功后推 2820 并写群系统提示
    void onDeleteGroupBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //在本群关闭 / 打开企业机器人，或停用 / 启用群机器人（cmd_set_group_bot_off，只有群主）
    void onSetGroupBotOff(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取本人的个人机器人（cmd_get_my_bots），不含「自动回复」的预置机器人
    void onGetMyBots(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //新建或修改本人的个人机器人（cmd_save_my_bot）
    void onSaveMyBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除本人的个人机器人（cmd_delete_my_bot）
    void onDeleteMyBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //「试一试」（cmd_try_bot）：判定一段模拟输入，不发消息
    void onTryBot(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拉取本人的「离开 / 忙碌自动回复」设置（cmd_get_auto_reply）
    void onGetAutoReply(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //保存本人的「离开 / 忙碌自动回复」设置（cmd_set_auto_reply），换算成预置个人机器人的两条规则
    void onSetAutoReply(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //向群成员推 2820（本群可用的机器人有变化）
    void notifyGroupBotsChanged(int64_t groupid);
    //生成加群链接（cmd_create_group_link）：群成员都能生成，禁止加入的群不能生成（docs/邀请入群方案.md §12）
    void onCreateGroupLink(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //列出本群可用的加群链接（cmd_get_group_links）：群主与管理员看全部，普通成员只看本人生成的
    void onGetGroupLinks(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //作废一条加群链接（cmd_revoke_group_link）：群主、管理员或生成者本人
    void onRevokeGroupLink(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //点加群链接后查群的样子（cmd_get_group_link_info）
    void onGetGroupLinkInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //凭加群链接入群（cmd_join_group_by_link）：按群的加群方式直接入群或转为加群申请
    void onJoinGroupByLink(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //查一条加群链接对本人是否可用：存在、没作废没过期、群还在、与生成者同一企业。
    //   code： 随机码
    //   link： [出参] 链接
    //   group：[出参] 链接所属的群
    //@return 可用返回 error_code_ok，否则为错误码（不回包，由调用方回）
    int resolveGroupLink(const std::string& code, GroupLinkRecord& link, GroupRecord& group);
    //转让群主（cmd_transfer_owner）
    void onTransferOwner(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //对投票卡片投票（cmd_vote_poll）
    void onVotePoll(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //参与接龙（cmd_join_relay）
    void onJoinRelay(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //撤回本人在某张接龙卡片里的那一条（cmd_withdraw_relay）；本人没参与过时直接回成功、不推送
    void onWithdrawRelay(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //提前结束一张接龙（cmd_close_relay）：发起人、群主、管理员可操作，不受禁言限制
    void onCloseRelay(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //待办勾选/取消（cmd_update_todo）
    void onUpdateTodo(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //申请上传文件（cmd_request_upload）：秒传判断、签发上传令牌、指明目标服务器
    void onRequestUpload(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //申请下载文件（cmd_request_download）：签发下载令牌、指明目标服务器
    void onRequestDownload(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //检查客户端更新（cmd_check_update）：查版本表、比版本号，有新版则直接签发下载令牌。
    //本命令<u>允许未登录调用</u>——版本过低的客户端登录会被拒，若检查更新也要求登录，
    //这类客户端就再无自救途径。未登录时签出的令牌载荷里 uid 为 0。
    void onCheckUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //查询群文件列表（cmd_get_group_files）：分页 + 按类型筛选
    void onGetGroupFiles(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //登记群文件（cmd_add_group_file）：把已上传文件登记进群文件库并推送全群
    void onAddGroupFile(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除群文件（cmd_delete_group_file）：登记者本人 / 群主 / 管理员可删，并推送全群
    void onDeleteGroupFile(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //按文件名搜索本人所在全部群的群文件（cmd_search_group_files）：PC 主面板全局搜索「文件」标签用
    void onSearchGroupFiles(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //向某群全体成员推送一条通知帧（seq=0）
    void notifyGroupMembers(int64_t groupid, uint16_t cmd, const std::string& body);

    /**
     *  只向群主与管理员推送。加群申请通知只有他们需要看到（也只有他们能处理）。
     *    groupid：目标群ID；
     *    cmd：    协议号；
     *    body：   已构造好的包体。
     */
    void notifyGroupAdmins(int64_t groupid, uint16_t cmd, const std::string& body);

    /**
     *  新成员入群后按群设置发一条入群欢迎语（message_type_system 的群消息）。
     *
     *  三个入群入口（邀请入群 / 自由加入 / 审批通过）都要发，故抽成一处。
     *  群未开启欢迎语、或欢迎语正文为空、或本次没有人真的加进来时什么都不做。
     *  欢迎语是服务器生成的系统提示，发送者 userid 记 0。
     *
     *    group：      目标群的最新资料，据其 welcomeon / welcometext 决定发不发；
     *    newMembers： 本次真正加入的成员userid列表，用于把名字拼进欢迎语。
     */
    void sendGroupWelcomeMessage(const GroupRecord& group,
                                 const std::vector<int64_t>& newMembers);

    /**
     *  新成员入群后的统一收尾（docs/邀请入群方案.md §5.2）。邀请直接入群、自由加群、审批通过都在
     *  GroupManager::addMembers 成功、并给请求者回完应答之后调用本函数，按以下顺序执行：
     *    1. 把新成员在本群的已读位点推到本群最新一条，入群前的消息不算未读；
     *    2. 向全体成员（含新成员）推送成员变动通知 1317（加入）；
     *    3. 自动命名的群重算群名，群名变化时推送资料变更；
     *    4. 插入「有人入群」系统提示（kSystemTipMemberJoin）；
     *    5. 按群设置发入群欢迎语；
     *    6. 让新成员能看到本群提醒全体成员的群备忘。
     *  系统提示与欢迎语都排在 1317 之后：客户端要先把新成员并进成员列表，才能对上提示里的人。
     *    group：      目标群的资料，欢迎语开关与正文从这里读；
     *    newMembers： 本次真正加入的成员 userid，按入群顺序排列；为空时什么都不做；
     *    inviterId：  邀请人 userid，写进系统提示；自由加群、本人申请经审批时为 0；
     *    operatorId： 写进 1317 的操作者 userid（邀请人、审批人或自由加群的本人）。
     */
    void completeGroupJoin(const GroupRecord& group, const std::vector<int64_t>& newMembers,
                           int64_t inviterId, int64_t operatorId);

    /**
     *  cmd_clear_conversation：按会话本人视角清空聊天记录。
     *  群聊须为该群成员；单聊不能以自己为对象。成功后应答带回生效的锚点，并向本人全部在线设备
     *  推送 cmd_messages_deleted_notify 的清空形式。消息本体不动，对方与其他群成员不受影响。
     */
    void onClearConversation(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  往群里插一条系统提示消息（message_type_system）：落库后推给此刻在线的全体群成员。
     *
     *  推送只能触达在线的人，而落库的系统消息会进历史与会话预览，当时不在线的成员之后也看得到。
     *  公告提示、入群欢迎语、退出群聊、转让群主四处共用这一份实现。发送者 userid 记
     *  kSystemMessageSenderId（0），客户端据此把它渲染成居中的灰色提示行。
     *    groupid：    目标群ID；推送对象是调用此函数时 t_group_member 里的全体成员；
     *    content：    提示的完整 content（紧凑 JSON 文本），由各提示的构造函数生成：至少带中文 text，
     *                 另带 tipkind / tipargs（多语言第 5 期）；接龙提示还带 cardmsgid
     *                 （CardProtocol::buildRelayTipContent），客户端据此让提示行可以点击；
     *    clientMsgId：占位的客户端消息ID。系统消息没有真实的客户端消息ID，由调用方拼一个以
     *                 "sys-" 开头、能区分来源的串，保证 saveMessage 的幂等去重键不会与真实消息相撞。
     *  返回：新消息的 msgid；落库失败时返回 0（已记错误日志，不推送）。
     */
    int64_t sendGroupSystemMessage(int64_t groupid, const std::string& content,
                                   const std::string& clientMsgId);
    /**
     *  为一次接龙操作在卡片所在的群里发一条系统提示（docs/群接龙方案.md §3.4）。只对群卡片发：
     *  服务端没有单聊系统提示的发送路径，客户端也只在群聊里提供接龙入口。发送失败只记日志，
     *  不影响操作本身的结果，调用方在回完应答、推送完名单之后再调用。
     *    card：  接龙卡片消息（content 为规范化后的卡片内容，从中取标题）
     *    kind：  提示种类，取值见 relay_tip_kind
     *    count： 当前参与人数，只在 relay_tip_close 时用到
     */
    void sendRelayTip(const MessageRecord& card, int kind, size_t count);
    //判断当前登录用户能否参与某卡片的互动（须在该卡片所在会话内）
    bool canAccessCard(const MessageRecord& card);
    //向某卡片所在会话的全体成员推送一条通知帧（群→群成员，单聊→收发双方）
    void notifyCardConversation(const MessageRecord& card, uint16_t cmd, const std::string& body);
    //取接龙卡片的当前名单，向卡片所在会话推送 cmd_relay_changed_notify。推送里带收件人视角的
    //会话ID：群卡片全体成员共用一份，单聊卡片收发双方各发一份（双方的会话ID互为对方）
    void notifyRelayChanged(const MessageRecord& card);
    /**
     *  接龙操作（参与、撤回本人那条、提前结束）共用的前置校验：取卡片，并依次判断卡片存在、
     *  未撤回、类型是接龙、本人在卡片所在会话内、（按需）群卡片本人不在禁言中、接龙仍在进行中。
     *  不发应答，结论交给调用方，调用方用 CardProtocol::buildRelayOpResponse 带上卡片定位字段回复。
     *    cmd：       触发校验的命令号，仅用于日志
     *    msgid：     接龙卡片所在消息id
     *    checkSpeak：是否检查禁言。参与与撤回会改变群里可见的内容，要查；提前结束是管理动作，不查
     *    card：      [出参] 取到的卡片消息；返回 0 时有效
     *    status：    [出参] 接龙当前的状态；返回 0 时有效（此时必为进行中）
     *    reason：    [出参] 不通过时的英文原因，供应答的 msg 字段使用
     *  @return 0 表示可以操作；否则为 error_code_message_not_exist（卡片不存在或已撤回）、
     *          error_code_bad_request（不是接龙卡片）、error_code_permission_denied（不在会话内）、
     *          error_code_not_group_member / error_code_group_not_exist / error_code_member_muted、
     *          error_code_card_closed（已提前结束或已截止）、error_code_server_internal（读结束记录失败）之一
     */
    int loadRelayCardForOp(uint16_t cmd, int64_t msgid, bool checkSpeak, MessageRecord& card,
                           RelayStatus& status, std::string& reason);
    /**
     *  读取一张接龙卡片的结束记录，结合卡片内容里的截止时间与当前时间算出接龙状态。
     *    card：  接龙卡片消息（content 为规范化后的卡片内容）
     *    nowMs： 服务端当前时间，Unix毫秒
     *    status：[出参] 算出的接龙状态；读库失败时按"没有结束记录"计算，只看截止时间
     *  @return 读结束记录成功返回 true；失败返回 false（已记错误日志）
     */
    bool loadRelayStatus(const MessageRecord& card, int64_t nowMs, RelayStatus& status);
    /**
     *  把卡片消息（投票 / 接龙 / 待办）的当前互动状态合并进它的 content，使客户端拿到的
     *  消息体直接带最新结果。接龙合并名单与状态，投票、待办合并互动结果；其它类型、以及已撤回
     *  的卡片不处理（见 CardProtocol::shouldAugmentCard）。
     *  cmd_sync_messages、cmd_get_pinned_messages、cmd_search_messages 下发消息体前调用，
     *  必须放在按体积截断之前（合并后体积会变大）。
     *    message：[入参 / 出参] 一条消息；只改动其 content
     *    nowMs：  服务端当前时间，Unix毫秒，用于判断接龙是否已截止
     */
    void augmentCardRecord(MessageRecord& message, int64_t nowMs);
    //对一批消息逐条调用 augmentCardRecord，当前时间只取一次
    void augmentCardRecords(std::vector<MessageRecord>& messages);
    //把一条消息存入会话并推送，返回新消息ID（失败返回 0）：ctype=1 推给对端、
    //ctype=2 fan-out 给全体群成员。供 cmd_forward_message / cmd_mass_send 复用。
    //outRec 非空且投递成功时，写入新消息的完整记录（转发应答要回带消息体）；失败时不改动它
    int64_t deliverConversationMessage(int ctype, int64_t cid, int32_t msgtype,
                                       const std::string& content, const std::string& clientmsgid,
                                       MessageRecord* outRec = NULL);
    /**
     *  群内发言的前置校验，普通群发（sendGroupMessage）与转发到群（onForwardMessage）共用：
     *  本人须为该群成员、群须存在、本人不在禁言中（规则见 chatserver::IsMutedForSpeaking）。
     *  顺带清掉本人已经到期的定时禁言，并把状态变更推给全群。
     *    cmd：         校验不通过时回错误应答所用的命令号（cmd_send_message 或 cmd_forward_message）
     *    groupid：     目标群ID
     *    clientmsgid： 被拒消息的客户端ID。cmd 为 cmd_send_message 时用 replySendRejected 回包、
     *                  带回该ID；其余命令回 { code, msg }，忽略本参数
     *    myrole：      [出参] 本人在该群的角色，取值见 group_member_role
     *    group：       [出参] 群资料
     *  @return 允许发言返回 true；否则已按 cmd 回过错误应答，返回 false，调用方直接结束处理
     */
    bool checkGroupSpeakAllowed(uint16_t cmd, int64_t groupid, const std::string& clientmsgid,
                                int& myrole, GroupRecord& group);
    /**
     *  checkGroupSpeakAllowed 的判断部分：规则与副作用（清掉已到期的定时禁言并推给全群、
     *  被禁言时记日志）完全相同，只是不发应答，把结论交给调用方。应答格式与通用
     *  { code, msg } 不同的命令（如 cmd_join_relay 要带回卡片定位字段）用它自行回复。
     *    cmd：     仅用于日志，标明是哪条命令触发的校验
     *    groupid： 目标群ID
     *    myrole：  [出参] 本人在该群的角色，取值见 group_member_role
     *    group：   [出参] 群资料
     *    reason：  [出参] 不通过时的英文原因，供应答的 msg 字段使用；通过时不改动
     *  @return 0 表示允许发言；否则为 error_code_not_group_member / error_code_group_not_exist /
     *          error_code_member_muted 之一
     */
    int checkGroupSpeak(uint16_t cmd, int64_t groupid, int& myrole, GroupRecord& group, std::string& reason);
    /**
     *  cmd_send_message 被拒时回包：用发送回执的格式，带回 clientmsgid，msgid 为 "0"、
     *  timestamp 为 0，客户端据 clientmsgid 找到被拒的那一条并标为发送失败
     *  （docs/敏感词方案.md 第 5.2 节）。请求解析失败时拿不到 clientmsgid，不走本方法。
     *    code：        错误码，取值见 error_code
     *    msg：         英文原因
     *    clientmsgid： 被拒消息的客户端ID
     */
    void replySendRejected(int code, const std::string& msg, const std::string& clientmsgid);
    /**
     *  按提交者（本会话的登录用户）所属企业的敏感词库检查一组文字，任意一段命中即返回 true。
     *  命中时写一条拦截记录、记一条日志（日志只记词条ID、分类与来源，不记原文）；调用方负责
     *  按各自命令的格式回错误码。企业的总开关关闭、词库为空、或词库装载失败时一律视为未命中。
     *    source：     拦截来源，取值见 chatserver::sensitive_hit_source
     *    ctype：      会话类型：单聊 1、群聊 2；与会话无关的检查（改资料等）以及群发助手传 0
     *    cid：        会话ID：单聊为对方 userid，群聊为群ID；ctype 为 0 时传 0
     *    targetName： 写进拦截记录的会话名称。传空串时，单聊按 cid 取对方昵称、群聊按 cid 取群名，
     *                 只在命中时才去取；ctype 为 0 时原样写入
     *    texts：      待检查的文字（UTF-8），空串跳过
     *  @return 命中返回 true，调用方应拒绝本次请求
     */
    bool hitSensitive(int source, int ctype, int64_t cid, const std::string& targetName,
                      const std::vector<std::string>& texts);
    //向指定 userid 的所有在线会话推送一条通知帧（seq=0）
    void sendToUser(int32_t userid, uint16_t cmd, const std::string& body);

private:
    int32_t        m_id;                 //会话ID
    OnlineUserInfo m_userinfo;           //登录用户信息
    int32_t        m_seq;                //当前正在处理的请求的序列号，应答时回填
    bool           m_isLogin;            //本会话是否已登录
    time_t         m_lastPackageTime;    //上一次收到数据包的时间，用于掉线检测
    //本次登录成功的时间，Unix 毫秒；构造时为 0，每次登录成功时更新。I/O 线程写、管理端口的推送线程读，故用原子量
    std::atomic<int64_t> m_loginTimeMs;
    TimerId        m_checkOnlineTimerId; //掉线检测定时器ID
    //本连接最近一次参与的那笔单聊文件夹传输的会话 id；未参与任何传输时为空串。
    //本端发出邀请（1810）或应答同意（1812）时写入，会话正常收尾（拒绝 / 撤回 / 成功结束）
    //时清空；失败结束时<u>不清</u>——会话要保留 folderretrywindow 供重试续传，期内本端掉线
    //仍须通知对端。生命周期与本 ChatSession 相同。
    //一个用户现在可同时参与至多 foldermaxperuser 笔传输（默认 5 笔），本字段只记得住
    //最近的那一笔，故 cleanupFolderTransferOnOffline 除了据它定位，还要按 userid 反查
    //本用户参与的全部会话，逐笔收尾
    std::string    m_folderTransferSessionId;
    //本连接的屏幕共享下线处理是否已经做过：退出登录与随后的连接断开都会调用 cleanupScreenShareOnOffline，
    //只处理第一次；登录成功时复位为 false。生命周期与本 ChatSession 相同
    bool           m_screenShareOfflineDone;
    //本连接的群通话下线处理是否已经做过：用法与 m_screenShareOfflineDone 相同，登录成功时复位为 false
    bool           m_groupCallOfflineDone;
};
