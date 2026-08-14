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
#include "../net/ByteBuffer.h"
#include "../net/TimerId.h"
#include "TcpSession.h"

using namespace net;

//cmd_send_message 请求结构，定义见 MessageProtocol.h；此处前向声明供 sendGroupMessage 形参使用
struct SendMessageRequest;
struct GroupRecord;   //群资料记录，定义在 GroupProtocol.h；此处仅按 const 引用传递
//群公告记录，定义在 GroupProtocol.h；此处仅按 const 引用传递
struct GroupAnnouncementRecord;
//一条完整消息体，定义见 MessageProtocol.h；前向声明供卡片互动相关 helper 形参使用
struct MessageRecord;
//一条活跃的单聊文件夹传输会话，定义在 FolderTransferManager.h；此处仅按 const 引用传递。
//与上面几个前向声明同理：本头文件不方便再多引一个业务头，用到实体的地方都在 .cpp 里
struct FolderTransferSessionInfo;
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
     *  幂等：clientmsgid 取由「会话 id + 本次尝试的序号」派生的固定串（见
     *  FolderTransferProtocol::buildRecordClientMsgId），落库前先按「发送方 userid +
     *  clientmsgid」查一次，已有记录即跳过。<u>同一次尝试</u>被判定多次终态（典型是双方各
     *  发一帧 cmd_folder_close）只会留下一条记录，以最先到达的那个终态为准；而<u>不同次
     *  尝试</u>的序号不同，故一笔传输先失败（序号 1）、在保留期内重试又成功（序号 2）时，
     *  聊天记录里会依次留下失败与完成两条，过程完整。序号由 FolderTransferManager 维护
     *  （见其 beginRetryAttempt），本方法只从会话快照里取用。
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
     *  @param state       终态取值，见 enum folder_transfer_state 的 3~8；传入过程态
     *                     （响铃中 / 传输中）时本方法只推送、不留记录，并记一条告警日志
     *  @param errcode     失败原因，取值见 error_code_folder_*（606~620）；仅
     *                     state 为 folder_state_failed 时写入，其余终态一律按 0 处理
     *  @param errmsg      面向排查的英文诊断串，可为空；同样仅失败态写入
     *  @param notifyUsers 要收到 cmd_folder_state_notify 的 userid 列表；空列表表示本次
     *                     不推状态变更（例如重试时发起方由 1817 的应答得知结果、对端已离线），
     *                     此时仍照常留记录
     */
    static void finishFolderTransferSession(const FolderTransferSessionInfo& s,
                                            int state, int errcode, const std::string& errmsg,
                                            const std::vector<int64_t>& notifyUsers);

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
    //拉取发给本人的平台公告（cmd_get_announcements）：主面板「消息盒子」的数据源，
    //见 docs/消息盒子接入方案.md
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
    //加入/取消特别关注（cmd_set_starred）：对象只能是联系人，见 docs/特别关注接入方案.md
    void onSetStarred(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //发送消息（cmd_send_message）：按 ctype 分流到单聊 / 群聊
    void onSendMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //发送群聊消息：onSendMessage 在 ctype=2 时调用
    void sendGroupMessage(const SendMessageRequest& req);
    //收到客户端送达回执（cmd_message_received_ack）
    void onReceivedAck(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //同步最近会话列表（cmd_sync_conversations）
    void onSyncConversations(const std::shared_ptr<TcpConnection>& conn);
    //按锚点拉取某会话的历史消息（cmd_sync_messages）
    void onSyncMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
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
     *  @param s   目标会话的快照，只读取其 sessionid 与双方 userid
     *  @param now 当前时间，Unix 毫秒；用于计算令牌的过期时刻
     *  @return 两枚令牌均签发并推送成功返回 true；会话已不在会话表里（签发失败）返回 false
     */
    bool pushFolderRelayInfo(const FolderTransferSessionInfo& s, int64_t now);

    //通话中切换媒体类型（cmd_call_switch_media）：校验由 CallManager::switchMedia 统一做，
    //本方法只负责回应答、并在真的发生变更时向双方（含发起者）推 cmd_call_media_changed_notify
    void onCallSwitchMedia(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //设置会话置顶/免打扰/删除（cmd_set_conversation）
    void onSetConversation(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //会话内消息记录搜索（cmd_search_messages）
    void onSearchMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //转发 / 合并转发消息（cmd_forward_message）
    void onForwardMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除本端消息（cmd_delete_message）
    void onDeleteMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
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
     *  cmd_get_sensitive_words：拉取本群敏感词库。仅群主 / 管理员可读 ——
     *  词库内容本身也算群管理信息，不该让普通成员看到（否则等于把过滤规则
     *  直接告诉想绕过它的人）。
     */
    void onGetSensitiveWords(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    /**
     *  cmd_operate_sensitive_word：敏感词增 / 删。仅群主 / 管理员可写。
     */
    void onOperateSensitiveWord(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

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
     *    item：      变更涉及的那条公告（publishername 须已补好）；
     *    changetype：取值见 group_announcement_change_type。
     */
    void notifyAnnouncementChanged(const GroupAnnouncementRecord& item, int changetype);

    /**
     *  公告发布后往群里插一条系统提示消息（"「张三」发布了群公告：xxx"）。
     *
     *  推送只能触达此刻在线的人，而系统消息会进历史与会话预览 —— 当时不在线的
     *  成员之后进群也看得到。做法与 sendGroupWelcomeMessage 一致。
     *    item：刚发布的那条公告（publishername 须已补好）。
     */
    void sendAnnouncementSystemMessage(const GroupAnnouncementRecord& item);
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
    //转让群主（cmd_transfer_owner）
    void onTransferOwner(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //对投票卡片投票（cmd_vote_poll）
    void onVotePoll(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //参与接龙（cmd_join_relay）
    void onJoinRelay(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
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
    //判断当前登录用户能否参与某卡片的互动（须在该卡片所在会话内）
    bool canAccessCard(const MessageRecord& card);
    //向某卡片所在会话的全体成员推送一条通知帧（群→群成员，单聊→收发双方）
    void notifyCardConversation(const MessageRecord& card, uint16_t cmd, const std::string& body);
    //把一条消息存入会话并推送，返回新消息ID（失败返回 0）：ctype=1 推给对端、
    //ctype=2 fan-out 给全体群成员。供 cmd_forward_message / cmd_mass_send 复用
    int64_t deliverConversationMessage(int ctype, int64_t cid, int32_t msgtype,
                                       const std::string& content, const std::string& clientmsgid);
    //向指定 userid 的所有在线会话推送一条通知帧（seq=0）
    void sendToUser(int32_t userid, uint16_t cmd, const std::string& body);

private:
    int32_t        m_id;                 //会话ID
    OnlineUserInfo m_userinfo;           //登录用户信息
    int32_t        m_seq;                //当前正在处理的请求的序列号，应答时回填
    bool           m_isLogin;            //本会话是否已登录
    time_t         m_lastPackageTime;    //上一次收到数据包的时间，用于掉线检测
    TimerId        m_checkOnlineTimerId; //掉线检测定时器ID
    //本连接最近一次参与的那笔单聊文件夹传输的会话 id；未参与任何传输时为空串。
    //本端发出邀请（1810）或应答同意（1812）时写入，会话正常收尾（拒绝 / 撤回 / 成功结束）
    //时清空；失败结束时<u>不清</u>——会话要保留 folderretrywindow 供重试续传，期内本端掉线
    //仍须通知对端。生命周期与本 ChatSession 相同。
    //一个用户现在可同时参与至多 foldermaxperuser 笔传输（默认 5 笔），本字段只记得住
    //最近的那一笔，故 cleanupFolderTransferOnOffline 除了据它定位，还要按 userid 反查
    //本用户参与的全部会话，逐笔收尾
    std::string    m_folderTransferSessionId;
};
