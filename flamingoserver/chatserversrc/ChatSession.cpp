/**
 *  聊天会话类（v2 协议）实现，ChatSession.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "ChatSession.h"
#include <stdio.h>
#include <time.h>
#include <list>
#include <set>
#include <map>
#include <vector>
#include <chrono>   //onDeleteMessagesByTime 记录写库耗时
#include "../net/TcpConnection.h"
#include "../net/EventLoop.h"
#include "../net/FrameCodec.h"
#include "../zlib1.3.2/ZlibUtil.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"
#include "LoginProtocol.h"
#include "UserManager.h"
#include "ChatServer.h"
#include "../utils/PasswordHasher.h"
#include "ContactsProtocol.h"
#include "FavoriteProtocol.h"
#include "FavoriteManager.h"
//联系人备注名（cmd_set_friend_remark 落库 / cmd_get_contacts 读出）
#include "ContactRemarkManager.h"
//联系人标签（cmd_set_contact_tags 落库 / cmd_get_contacts 读出）
#include "ContactTagProtocol.h"
#include "ContactTagManager.h"
#include "UpdateProtocol.h"
#include "ClientVersionManager.h"
#include "ProfileProtocol.h"
#include "RegisterProtocol.h"
#include "RegisterService.h"
#include "SmsCodeManager.h"
#include "FriendProtocol.h"
//平台公告的投递（公告中心）：报文编解码 + 受众匹配与已读落库。
#include "AnnouncementProtocol.h"
#include "AnnouncementManager.h"
#include "MessageProtocol.h"
#include "MessageManager.h"
#include "MessagePinManager.h"
#include "MessageDeleteRange.h"   //onDeleteMessagesByTime：区间的校验与截断
#include "GroupProtocol.h"
#include "GroupPermission.h"
#include "GroupManager.h"
#include "GroupFollowManager.h"
#include "GroupQaManager.h"      //群问答（docs/群问答方案.md）
#include "BotEngine.h"           //机器人的判定入口（docs/机器人方案.md §5.2）
#include "BotManager.h"          //机器人存储与群解散清理
#include "BotRules.h"            //删除客户端伪造的机器人标记
#include "GroupLinkManager.h"    //加群链接（docs/邀请入群方案.md §12）
#include "GroupInviteProtocol.h"    //入群邀请卡片的组装、状态与点「加入群聊」的判定（docs/邀请入群方案.md 第 3 期）
#include "GroupInviteManager.h"     //t_group_invite 的读写
#include "EmployeeManager.h"        //getEmployeeEnterpriseId：「邀请入群需被邀请人同意」按被邀请人所属企业取值
#include "CardProtocol.h"
#include "CardManager.h"
#include "FileProtocol.h"
#include "FileStoreManager.h"
#include "RemoteDesktopProtocol.h"
#include "RemoteDesktopManager.h"
#include "CallProtocol.h"
#include "CallManager.h"
#include "CallCardWriter.h"   //一对一通话卡片：呼叫时写入单聊，结束时改写
#include "ScreenShareService.h"
#include "ScreenShareRules.h"   // IScreenShareSensitiveChecker（第 15 期，共享标题与描述的敏感词检查）
//群通话（cmd_groupcall_* 1740~1751）：房间状态、处理流程与线上环境
#include "GroupCallManager.h"
#include "GroupCallService.h"
#include "GroupCallServerEnv.h"
//单聊文件夹传输（cmd_folder_* 1810~1817）：八条信令的 JSON 编解码 + 内存会话表
#include "FolderTransferProtocol.h"
#include "FolderTransferManager.h"
#include "FileShareProtocol.h"
#include "FileShareManager.h"
#include "FileSharePolicy.h"
#include "FileShareSessionManager.h"
#include "FeatureSwitchManager.h"
#include "P2PSessionManager.h"
#include "P2PSignalService.h"
#include "SecretChatManager.h"
#include "ThreadWatchRegistry.h"
#include "P2PServerSetup.h"          //P2PFeatureGate：按用户所属企业的开关判断能否建 P2P 会话
#include "../utils/RelayToken.h"
#include "GroupFileProtocol.h"
#include "GlobalSearchProtocol.h"   //cmd_global_search 的报文编解码
#include "MessageSearchText.h"      //MakeSearchExcerpt：群公告命中的正文摘录
#include "GroupFileManager.h"
#include "../utils/FileToken.h"
#include "DepartmentManager.h"
#include "EnterpriseManager.h"
#include "SensitiveWordManager.h"   //企业级敏感词检查（hitSensitive）
#include "MemoProtocol.h"           //MemoMemberRole：群成员变化时的备忘推送（实现在 ChatSessionMemo.cpp）
#include "AdminLinkServer.h"        //管理控制台的在线名单：登录、注销、改状态、会话置为失效时通知
#include "CloudRecordService.h"     //云端录制的列表与操作（第 26 期）

using namespace std;
using namespace net;

//系统提示消息（message_type_system）的发送者 userid。这类消息由服务器生成、
//没有真实发送人，统一记 0 —— 真实 userid 都是正数，客户端据此把它渲染成
//居中的灰色提示行而不是某人发的气泡。
static const int32_t kSystemMessageSenderId = 0;

//允许的最大收包间隔（秒）：超过则判定为掉线
constexpr int MAX_NO_PACKAGE_INTERVAL = 30;
//掉线检测定时器触发周期（微秒）：每 15 秒检测一次
constexpr int HEARTBEAT_CHECK_INTERVAL_US = 15000000;
//客户端最低版本号的兜底值；低于此版本拒绝登录。
//正常情况下最低版本取自版本表（t_client_version 里该平台最新发布记录的 f_min_version），
//本常量只在"该平台还没发过版"或"库不可用"时启用。
//取值须与 PC 客户端当前的版本编号体系一致：客户端版本号已于 2026-08-19 由 3.2.1
//重新编号为 2.0.0（见 flamingoclient/Source/ClientVersion.h），原兜底值 3.0.0 会使
//所有 2.x 客户端在版本表无已发布记录时被拒绝登录，故同步下调为 2.0.0。
constexpr const char* MIN_CLIENT_VERSION_FALLBACK = "2.0.0";
//cmd_search 单次最多返回的结果条数
constexpr int SEARCH_MAX_RESULTS = 50;
//cmd_set_blacklist 的 action 值：1=加入黑名单 0=移出黑名单
constexpr int BLACKLIST_ACTION_ADD = 1;
//会话类型 ctype 值：1=单聊
constexpr int CHAT_TYPE_SINGLE = 1;
//会话类型 ctype 值：2=群聊
constexpr int CHAT_TYPE_GROUP = 2;
//cmd_sync_messages 单次默认返回条数（请求未带 limit 时套用）
constexpr int SYNC_MESSAGES_DEFAULT_LIMIT = 20;
//cmd_sync_messages 单次返回条数上限（请求 limit 超出则夹到此值）
constexpr int SYNC_MESSAGES_MAX_LIMIT = 100;
//cmd_message_read_notify 的 readcount 字段：单聊只有一个对端，已读人数恒为 1
constexpr int SINGLE_CHAT_READ_COUNT = 1;
//消息可撤回的时限（毫秒）：发出后 2 分钟内允许撤回，超时返回 error_code_recall_timeout
constexpr int64_t RECALL_TIME_LIMIT_MS = 2 * 60 * 1000;
//合并转发生成的消息其内容类型 msgtype 取 15（聊天记录），见协议 §9.4
constexpr int MSGTYPE_MERGED_FORWARD = 15;
//cmd_handle_join_group 的 action 值：1=同意加群申请 0=拒绝
constexpr int GROUP_JOIN_ACTION_ACCEPT = 1;

//一秒折合的毫秒数。文件夹传输的会话表、令牌过期与保留期一律以 Unix 毫秒计时，
//而 time(NULL) 给出的是秒，两者之间的换算统一走本常量
constexpr int64_t MILLISECONDS_PER_SECOND = 1000;

ChatSession::ChatSession(const std::shared_ptr<TcpConnection>& conn, int sessionid) :
    TcpSession(conn),
    m_id(sessionid),
    m_seq(0),
    m_isLogin(false),
    m_screenShareOfflineDone(false),
    m_groupCallOfflineDone(false)
{
    m_userinfo.userid = 0;
    m_userinfo.clienttype = 0;
    m_userinfo.status = 0;
    m_lastPackageTime = time(NULL);
    m_loginTimeMs.store(0);

    //掉线检测暂不在构造时启用，便于联调；需要时调用 enableHearbeatCheck()
    //enableHearbeatCheck();
}

ChatSession::~ChatSession()
{
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    if (conn)
    {
        LOGI("remove check online timerId, userid: %d, clientType: %d, client: %s",
             m_userinfo.userid, m_userinfo.clienttype, conn->peerAddress().toIpPort().c_str());
        conn->getLoop()->remove(m_checkOnlineTimerId);
    }
}

void ChatSession::onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime)
{
    //一个缓冲区可能含 0~N 个完整帧，循环解出每一个
    while (true)
    {
        frame_header header;
        std::string body;
        size_t consumed = 0;
        net::frame_decode_result result =
            net::decodeFrame(pBuffer->peek(), pBuffer->readableBytes(), header, body, consumed);

        //数据不足一个完整帧，等待后续数据
        if (result == net::frame_decode_need_more)
            return;

        //协议错误：魔数 / 版本 / 长度 / CRC 非法，主动关闭连接
        if (result == net::frame_decode_error)
        {
            LOGE("decode frame error, close connection, client: %s",
                 conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }

        //成功解出一帧：从缓冲区移除已消费字节，刷新收包时间
        pBuffer->retrieve(consumed);
        m_lastPackageTime = time(NULL);

        //协议加密：握手、加密状态校验与解密。必须排在解压之前——线上顺序是
        //先压缩后加密，收包时反过来走。
        bool securityHandled = false;
        if (!processFrameSecurity(conn, header, body, securityHandled))
            return;

        //本帧是握手帧，已在上面处理并应答完毕，不进业务分发
        if (securityHandled)
            continue;

        //走到这里 body 已是明文：解密与解压都在 processFrameSecurity 里做完了，
        //顺序（先解密、后解压）由 net::decodeFramePayload 一处定死。

        if (!process(conn, header.cmd, header.seq, body))
        {
            LOGE("process v2 frame error, cmd: %d, close connection, client: %s",
                 header.cmd, conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }
    }
}

bool ChatSession::processFrameSecurity(const std::shared_ptr<TcpConnection>& conn,
                                       const frame_header& header,
                                       std::string& body, bool& handled)
{
    handled = false;

    const std::string peer = conn->peerAddress().toIpPort();

    std::string response;
    std::string err;
    net::frame_security_result r =
        net::processInboundFrame(m_handshake, header.cmd, header.seq, header.flags,
                                 header.originlen, body, response, err);

    if (r == net::frame_security_error)
    {
        LOGE("frame security check failed: %s, cmd: %u, close connection, client: %s",
             err.c_str(), (unsigned)header.cmd, peer.c_str());
        conn->forceClose();
        return false;
    }

    if (r == net::frame_security_handled)
    {
        handled = true;

        //三帧握手里只有第 2 帧有应答，第 3 帧没有。
        //这条应答必须明文发出，而此刻 m_handshake.ready() 仍为 false（产出应答的那一步
        //只把状态推进到「等客户端流头」），send 走的正是明文分支——顺序天然成立。
        //改动握手状态机时<u>不要破坏这个前提</u>，否则客户端会收到一个它还解不开的包。
        if (!response.empty())
            send(cmd_handshake, static_cast<int32_t>(header.seq), response);

        if (m_handshake.ready())
        {
            LOGI("handshake done, frame encryption is now active, client: %s, key fingerprint: %s",
                 peer.c_str(), net::cryptoServerIdentity().fingerprint().c_str());
        }
    }

    return true;
}

bool ChatSession::process(const std::shared_ptr<TcpConnection>& conn,
                          uint16_t cmd, uint32_t seq, const std::string& body)
{
    //记录请求序列号，应答时回填，使客户端能将应答与请求关联
    m_seq = static_cast<int32_t>(seq);

    //心跳过于频繁，不打印日志。密聊号段（2600~2699）只记长度、不记包体：包体里是端到端加密的密文与临时公钥，
    //按方案不留任何记录（docs/密聊方案.md 4.5）
    if (cmd != cmd_heartbeat)
    {
        if (SecretChatManager::isSecretCommand(cmd))
        {
            LOGI("recv v2 frame: userid=%d, cmd=%u, seq=%u, bodylen=%u, body=<secret>, client=%s",
                 m_userinfo.userid, (unsigned)cmd, seq, (unsigned)body.size(),
                 conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOGI("recv v2 frame: userid=%d, cmd=%u, seq=%u, bodylen=%u, body=%s, client=%s",
                 m_userinfo.userid, (unsigned)cmd, seq, (unsigned)body.size(),
                 body.c_str(), conn->peerAddress().toIpPort().c_str());
        }
    }

    switch (cmd)
    {
    case cmd_heartbeat:
        onHeartbeat(conn);
        break;

    case cmd_login:
        onLogin(body, conn);
        break;

    case cmd_logout:
        onLogout(conn);
        break;

    case cmd_register:
        onRegister(body, conn);
        break;

    case cmd_send_sms_code:
        onSendSmsCode(body, conn);
        break;

    case cmd_reset_password:
        onResetPassword(body, conn);
        break;

    //检查更新必须在登录前也能用：版本过低的客户端会被 onLogin 以
    //error_code_version_too_old 拒绝，如果检查更新还要求先登录，它就既登不上
    //也查不到升级包，只能靠用户自己去官网下载。故与注册 / 找回密码一样放行。
    case cmd_check_update:
        onCheckUpdate(body, conn);
        break;

    default:
        //登录前收到除登录 / 心跳外的任何 cmd，视为非法
        if (!m_isLogin)
        {
            LOGE("recv cmd %u before login, client: %s",
                 (unsigned)cmd, conn->peerAddress().toIpPort().c_str());
            return false;
        }
        //已登录用户的业务 cmd 分发
        switch (cmd)
        {
        case cmd_get_contacts:
            onGetContacts(conn);
            break;

        case cmd_get_devices:
            onGetDevices(conn);
            break;

        case cmd_kick_device:
            onKickDevice(body, conn);
            break;

        case cmd_get_user_info:
            onGetUserInfo(body, conn);
            break;

        case cmd_update_profile:
            onUpdateProfile(body, conn);
            break;

        case cmd_change_password:
            onChangePassword(body, conn);
            break;

        case cmd_change_avatar:
            onChangeAvatar(body, conn);
            break;

        case cmd_change_presence:
            onChangePresence(body, conn);
            break;

        case cmd_search:
            onSearch(body, conn);
            break;

        case cmd_get_announcements:
            onGetAnnouncements(body, conn);
            break;

        case cmd_mark_announcement_read:
            onMarkAnnouncementRead(body, conn);
            break;

        case cmd_set_friend_remark:
            onSetFriendRemark(body, conn);
            break;

        //整份覆盖本人给某个联系人贴的标签列表。增、改、删共用本条命令，
        //请求带的是完整列表、空数组即清空；标签只影响本人这一侧的显示，不推送给对方
        case cmd_set_contact_tags:
            onSetContactTags(body, conn);
            break;

        case cmd_set_blacklist:
            onSetBlacklist(body, conn);
            break;

        case cmd_set_favorite:
            onSetFavorite(body, conn);
            break;

        case cmd_send_message:
            onSendMessage(body, conn);
            break;

        case cmd_message_received_ack:
            onReceivedAck(body, conn);
            break;

        case cmd_sync_conversations:
            onSyncConversations(conn);
            break;

        case cmd_sync_messages:
            onSyncMessages(body, conn);
            break;

        //客户端登录后按改写序号补上离线期间被改写过或被撤回的消息
        case cmd_sync_rewritten_messages:
            onSyncRewrittenMessages(body, conn);
            break;

        case cmd_mark_read:
            onMarkRead(body, conn);
            break;

        case cmd_recall_message:
            onRecallMessage(body, conn);
            break;

        case cmd_pin_message:
            onPinMessage(body, conn);
            break;

        case cmd_get_pinned_messages:
            onGetPinnedMessages(body, conn);
            break;

        case cmd_typing_notify:
            onTypingNotify(body, conn);
            break;

        case cmd_file_transfer_ack:
            onFileTransferAck(body, conn);
            break;

        case cmd_rd_invite:
            onRemoteDesktopInvite(body, conn);
            break;

        case cmd_rd_response:
            onRemoteDesktopResponse(body, conn);
            break;

        case cmd_rd_cancel:
            onRemoteDesktopCancel(body, conn);
            break;

        case cmd_rd_close:
            onRemoteDesktopClose(body, conn);
            break;

        //远程桌面控制权限：控制端申请 / 撤回 / 放弃，被控端授予 / 拒绝 / 收回（2026-10-12）
        case cmd_rd_control:
            onRemoteDesktopControl(body, conn);
            break;

        case cmd_call_invite:
            onCallInvite(body, conn);
            break;

        case cmd_call_response:
            onCallResponse(body, conn);
            break;

        case cmd_call_cancel:
            onCallCancel(body, conn);
            break;

        case cmd_call_hangup:
            onCallHangup(body, conn);
            break;

        case cmd_call_switch_media:
            onCallSwitchMedia(body, conn);
            break;

        //屏幕共享：发起人发起共享
        case cmd_screenshare_start:
        //屏幕共享：观看者申请加入
        case cmd_screenshare_join:
        //屏幕共享：成员离开（发起人离开等同结束）
        case cmd_screenshare_leave:
        //屏幕共享：发起人结束共享
        case cmd_screenshare_stop:
        //屏幕共享：查询某会话有无进行中的共享
        case cmd_screenshare_query:
        //屏幕共享：成员上报麦克风、摄像头、电脑声音开关
        case cmd_screenshare_member_media:
        //屏幕共享：发起人设置标注 / 录制权限或全员静音
        case cmd_screenshare_set_permission:
        //屏幕共享：成员开始 / 停止录制
        case cmd_screenshare_record_state:
        //屏幕共享：发起人移出某个观看者（第 14 期）
        case cmd_screenshare_remove_member:
        //屏幕共享：发起人修改共享标题与描述（第 15 期）
        case cmd_screenshare_set_info:
        //屏幕共享：控制键鼠的申请、撤回、授予、拒绝、收回（第 18 期）
        case cmd_screenshare_control:
        //屏幕共享：多人共享的申请、撤回、同意、拒绝、停止（第 22 期）
        case cmd_screenshare_sharing:
        //屏幕共享：移交主持人（第 22 期）
        case cmd_screenshare_transfer_host:
        //屏幕共享：主持人取得、重置、停用分享链接或设置免输密码（第 25 期）
        case cmd_screenshare_link:
        //屏幕共享：凭分享链接码查共享概要（第 25 期）
        case cmd_screenshare_link_resolve:
        //屏幕共享：主持人开始、暂停、继续、停止云端录制（第 26 期）
        case cmd_cloudrecord_control:
            onScreenShare(cmd, body);
            break;

        //云端录制：分页列出本人的云端录像（第 26 期）
        case cmd_cloudrecord_list:
        //云端录制：重命名、删除、取下载 / 上传令牌、发到会话（第 26 期）
        case cmd_cloudrecord_op:
            onCloudRecord(cmd, body);
            break;

        //群通话：发起人选人振铃、建房间（docs/协议设计-v2.md 第 21 节）
        case cmd_groupcall_start:
        //群通话：接听邀请，或从横幅 / 卡片加入
        case cmd_groupcall_join:
        //群通话：被邀请人拒绝
        case cmd_groupcall_decline:
        //群通话：离开；最后一个已加入的人离开时结束
        case cmd_groupcall_leave:
        //群通话：通话中追加邀请
        case cmd_groupcall_invite_more:
        //群通话：成员上报麦克风、摄像头开关
        case cmd_groupcall_member_media:
        //群通话：查询某群有无进行中的群通话
        case cmd_groupcall_query:
        //群通话：发起人、群主、管理员静音或移出别人（第 6 期）
        case cmd_groupcall_manage:
            onGroupCall(cmd, body);
            break;

        case cmd_set_conversation:
            onSetConversation(body, conn);
            break;

        //批量设置多个会话的消息提醒模式（批量管理窗口）。单个会话走上面的 cmd_set_conversation
        case cmd_set_notify_modes:
            onSetNotifyModes(body, conn);
            break;

        case cmd_search_messages:
            onSearchMessages(body, conn);
            break;

        case cmd_forward_message:
            onForwardMessage(body, conn);
            break;

        case cmd_delete_message:
            onDeleteMessage(body, conn);
            break;

        //按时间范围清理本人的全部单聊与群聊记录（系统设置 → 聊天 → 清理聊天记录）
        case cmd_delete_messages_by_time:
            onDeleteMessagesByTime(body, conn);
            break;

        //按会话清空本人的聊天记录（联系人 / 群 / 会话右键、聊天窗头部的「清空聊天记录」）
        case cmd_clear_conversation:
            onClearConversation(body, conn);
            break;

        case cmd_mass_send:
            onMassSend(body, conn);
            break;

        case cmd_create_group:
            onCreateGroup(body, conn);
            break;

        case cmd_dismiss_group:
            onDismissGroup(body, conn);
            break;

        case cmd_get_group_info:
            onGetGroupInfo(body, conn);
            break;

        case cmd_get_group_members:
            onGetGroupMembers(body, conn);
            break;

        case cmd_get_user_groups:
            onGetUserGroups(conn);
            break;

        case cmd_update_group_info:
            onUpdateGroupInfo(body, conn);
            break;

        case cmd_invite_to_group:
            onInviteToGroup(body, conn);
            break;

        //被邀请人点入群邀请卡片上的「加入群聊」
        case cmd_accept_group_invite:
            onAcceptGroupInvite(body, conn);
            break;

        case cmd_join_group_request:
            onJoinGroupRequest(body, conn);
            break;

        case cmd_handle_join_group:
            onHandleJoinGroup(body, conn);
            break;

        case cmd_get_group_join_requests:
            onGetGroupJoinRequests(body, conn);
            break;

        case cmd_get_group_announcements:
            onGetGroupAnnouncements(body, conn);
            break;

        case cmd_publish_group_announcement:
            onPublishGroupAnnouncement(body, conn);
            break;

        case cmd_delete_group_announcement:
            onDeleteGroupAnnouncement(body, conn);
            break;

        case cmd_leave_group:
            onLeaveGroup(body, conn);
            break;

        case cmd_kick_group_member:
            onKickGroupMember(body, conn);
            break;

        case cmd_set_member_role:
            onSetMemberRole(body, conn);
            break;

        case cmd_mute_member:
            onMuteMember(body, conn);
            break;

        case cmd_mute_all:
            onMuteAll(body, conn);
            break;

        case cmd_set_member_card:
            onSetMemberCard(body, conn);
            break;

        //设置本人在某个群里关注的成员（模式 5「只提醒 @我和关注的人」用，整份覆盖）
        case cmd_set_group_follow:
            onSetGroupFollow(body, conn);
            break;

        //设置 / 取消入群必读（仅群主与管理员）
        case cmd_set_must_read:
            onSetMustRead(body, conn);
            break;

        //本人确认已阅读入群必读
        case cmd_confirm_must_read:
            onConfirmMustRead(body, conn);
            break;

        //群主 / 管理员代为确认入群必读
        case cmd_release_must_read:
            onReleaseMustRead(body, conn);
            break;

        //取本群的入群必读与本人是否待确认
        case cmd_get_must_read:
            onGetMustRead(body, conn);
            break;

        //拉取本群问答
        case cmd_get_group_qa:
            onGetGroupQa(body, conn);
            break;

        //新增 / 修改一条本群问答（仅群主与管理员）
        case cmd_save_group_qa:
            onSaveGroupQa(body, conn);
            break;

        //删除一条本群问答（仅群主与管理员）
        case cmd_delete_group_qa:
            onDeleteGroupQa(body, conn);
            break;

        //拉取本群可用的机器人（docs/机器人方案.md 6.1）
        case cmd_get_group_bots:
            onGetGroupBots(body, conn);
            break;

        //拉取一个机器人及其规则（群机器人给群主，个人机器人给主人）
        case cmd_get_bot_detail:
            onGetBotDetail(body, conn);
            break;

        //群主新建或修改群机器人
        case cmd_save_group_bot:
            onSaveGroupBot(body, conn);
            break;

        //群主删除群机器人
        case cmd_delete_group_bot:
            onDeleteGroupBot(body, conn);
            break;

        //群主在本群关闭 / 打开企业机器人，或停用 / 启用群机器人
        case cmd_set_group_bot_off:
            onSetGroupBotOff(body, conn);
            break;

        //拉取本人的个人机器人
        case cmd_get_my_bots:
            onGetMyBots(body, conn);
            break;

        //新建或修改本人的个人机器人
        case cmd_save_my_bot:
            onSaveMyBot(body, conn);
            break;

        //删除本人的个人机器人
        case cmd_delete_my_bot:
            onDeleteMyBot(body, conn);
            break;

        //「试一试」：判定一段模拟输入，不发消息
        case cmd_try_bot:
            onTryBot(body, conn);
            break;

        //拉取本人的「离开 / 忙碌自动回复」设置
        case cmd_get_auto_reply:
            onGetAutoReply(body, conn);
            break;

        //保存本人的「离开 / 忙碌自动回复」设置
        case cmd_set_auto_reply:
            onSetAutoReply(body, conn);
            break;

        //生成加群链接
        case cmd_create_group_link:
            onCreateGroupLink(body, conn);
            break;

        //列出本群可用的加群链接
        case cmd_get_group_links:
            onGetGroupLinks(body, conn);
            break;

        //作废一条加群链接
        case cmd_revoke_group_link:
            onRevokeGroupLink(body, conn);
            break;

        //点加群链接后查群的样子
        case cmd_get_group_link_info:
            onGetGroupLinkInfo(body, conn);
            break;

        //凭加群链接入群
        case cmd_join_group_by_link:
            onJoinGroupByLink(body, conn);
            break;

        case cmd_transfer_owner:
            onTransferOwner(body, conn);
            break;

        case cmd_vote_poll:
            onVotePoll(body, conn);
            break;

        case cmd_join_relay:
            onJoinRelay(body, conn);
            break;

        //撤回本人在某张接龙卡片里的那一条
        case cmd_withdraw_relay:
            onWithdrawRelay(body, conn);
            break;

        //提前结束一张接龙（发起人、群主、管理员）
        case cmd_close_relay:
            onCloseRelay(body, conn);
            break;

        case cmd_update_todo:
            onUpdateTodo(body, conn);
            break;

        case cmd_request_upload:
            onRequestUpload(body, conn);
            break;

        case cmd_request_download:
            onRequestDownload(body, conn);
            break;

        case cmd_get_group_files:
            onGetGroupFiles(body, conn);
            break;

        //PC 主面板全局搜索：按文件名搜本人所在全部群的群文件
        case cmd_search_group_files:
            onSearchGroupFiles(body, conn);
            break;

        //PC 主面板全局搜索：在服务器上的全部聊天记录与群公告里搜关键字
        case cmd_global_search:
            onGlobalSearch(body, conn);
            break;

        case cmd_add_group_file:
            onAddGroupFile(body, conn);
            break;

        case cmd_delete_group_file:
            onDeleteGroupFile(body, conn);
            break;

        //发送端选中一个目录、发起文件夹传输：校验规模上限与双方是否已有传输在进行，
        //建会话后把邀请推给接收端。字节本身不走这条连接，走 fileserver 的中转通道
        case cmd_folder_invite:
            onFolderInvite(body, conn);
            break;

        //接收端在会话气泡上点了「接收」或「拒绝」：同意则给双方下发中转参数，
        //拒绝则通知发送端并销毁会话
        case cmd_folder_response:
            onFolderResponse(body, conn);
            break;

        //发送端在接收端还没应答（会话仍在响铃）时撤回邀请，让接收端那侧的气泡收起
        case cmd_folder_cancel:
            onFolderCancel(body, conn);
            break;

        //任一端结束本次传输：全部传完是成功结束，中途出错或用户取消是失败结束，
        //后者带错误码并保留会话供重试续传
        case cmd_folder_close:
            onFolderClose(body, conn);
            break;

        //发送端在失败气泡上点「重试」：保留期内重新签发令牌、再下发一次中转参数，
        //接收端复用上次那个目录做断点续传，不再被询问是否同意
        case cmd_folder_retry:
            onFolderRetry(body, conn);
            break;

        //文件共享：分享者在单聊或群聊里发起共享，服务端建记录、给被指定的成员发一张只有他们看得到的卡片
        case cmd_share_create:
            onShareCreate(body, conn);
            break;

        //文件共享：分享者本人，或群共享所在群的群主、管理员取消共享，共享从此对所有人失效
        case cmd_share_cancel:
            onShareCancel(body, conn);
            break;

        //文件共享：分享者追加、移除成员或修改成员的写权限
        case cmd_share_update_members:
            onShareUpdateMembers(body, conn);
            break;

        //文件共享：「文件共享」窗口与聊天窗按钮拉取「我的共享」或「共享给我的」
        case cmd_share_list:
            onShareList(body, conn);
            break;

        //文件共享：分享者客户端登录后、以及共享根可用性变化时报告各共享能否提供
        case cmd_share_serve_report:
            onShareServeReport(body, conn);
            break;

        //文件共享：成员打开共享（浏览窗口），服务端建访问会话并向两端下发中转参数
        case cmd_share_open:
            onShareOpen(body, conn);
            break;

        //文件共享：任一端结束访问会话（关闭浏览窗口、空闲回收、分享者停止提供）
        case cmd_share_close_access:
            onShareCloseAccess(body, conn);
            break;

        //文件共享：访问者申请一次下载或上传，服务端校验权限与名额后建传输子会话
        case cmd_share_request_transfer:
            onShareRequestTransfer(body, conn);
            break;

        //文件共享：任一端结束一次传输（完成、失败或取消）
        case cmd_share_transfer_close:
            onShareTransferClose(body, conn);
            break;

        //文件共享：分享者客户端批量上报每个请求的执行结果，写入审计表
        case cmd_share_audit_report:
            onShareAuditReport(body, conn);
            break;

        //备忘与提醒：登录后拉取本人可见的全部备忘，群聊窗「备忘」页签只拉本群的
        case cmd_memo_list:
            onMemoList(body, conn);
            break;

        //备忘与提醒：新建或修改个人备忘、群备忘
        case cmd_memo_save:
            onMemoSave(body, conn);
            break;

        //备忘与提醒：删除个人备忘、群备忘
        case cmd_memo_delete:
            onMemoDelete(body, conn);
            break;

        //P2P：客户端登录后报到协议版本，应答告诉它本企业是否开启、地址探测服务器在哪
        case cmd_p2p_register:
            onP2PCommand(cmd, body);
            break;

        //P2P：2302 只由服务端推送，客户端发来时交给信令处理按报文非法应答
        case cmd_p2p_session_notify:
            onP2PCommand(cmd, body);
            break;

        //P2P：交换候选地址与公钥，转给会话的另一端
        case cmd_p2p_signal:
            onP2PCommand(cmd, body);
            break;

        //P2P：上报建链结果（路径、NAT 判断），只核对成员身份并记日志
        case cmd_p2p_result:
            onP2PCommand(cmd, body);
            break;

        //P2P：一端结束会话（业务结束或建链失败），删除会话并通知另一端
        case cmd_p2p_close:
            onP2PCommand(cmd, body);
            break;

        //P2P：联调用的直接建会话，配置项 p2pdebugopen 为 1 时才受理
        case cmd_p2p_debug_open:
            onP2PCommand(cmd, body);
            break;

        //单聊密聊：FlamingoSecretChat.dll 报到协议版本与本机的防截图级别
        case cmd_secret_register:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：发起密聊，推邀请给对方全部已报到的连接
        case cmd_secret_invite:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：2603 只由服务端推送，客户端发来时交给密聊处理按报文非法应答
        case cmd_secret_invite_notify:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：被邀请人同意或忽略
        case cmd_secret_respond:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：2605 只由服务端推送，客户端发来时按报文非法应答
        case cmd_secret_state_notify:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：发起方在对方应答前撤回邀请
        case cmd_secret_cancel:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：任一方结束密聊
        case cmd_secret_close:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：转发一帧密文给对方绑定的那条连接，不落库
        case cmd_secret_message:
            onSecretCommand(cmd, body);
            break;

        //单聊密聊：2609 只由服务端推送，客户端发来时按报文非法应答
        case cmd_secret_message_notify:
            onSecretCommand(cmd, body);
            break;

        //帖子群：拉一页帖子
        case cmd_thread_list_posts:
            onThreadCommand(cmd, body);
            break;

        //帖子群：拉一条完整帖子
        case cmd_thread_get_post:
            onThreadCommand(cmd, body);
            break;

        //帖子群：发帖
        case cmd_thread_publish_post:
            onThreadCommand(cmd, body);
            break;

        //帖子群：作者修改帖子
        case cmd_thread_edit_post:
            onThreadCommand(cmd, body);
            break;

        //帖子群：删除帖子
        case cmd_thread_delete_post:
            onThreadCommand(cmd, body);
            break;

        //帖子群：置顶或取消置顶
        case cmd_thread_pin_post:
            onThreadCommand(cmd, body);
            break;

        //帖子群：拉一页留言
        case cmd_thread_list_comments:
            onThreadCommand(cmd, body);
            break;

        //帖子群：拉一页回复
        case cmd_thread_list_replies:
            onThreadCommand(cmd, body);
            break;

        //帖子群：留言或回复
        case cmd_thread_add_comment:
            onThreadCommand(cmd, body);
            break;

        //帖子群：删除留言或回复
        case cmd_thread_delete_comment:
            onThreadCommand(cmd, body);
            break;

        //帖子群：点赞、表情回应或取消
        case cmd_thread_react:
            onThreadCommand(cmd, body);
            break;

        //帖子群：拉互动名单
        case cmd_thread_get_reactors:
            onThreadCommand(cmd, body);
            break;

        //帖子群：登记或撤销「正在查看」
        case cmd_thread_watch:
            onThreadCommand(cmd, body);
            break;

        //帖子群：楼主追加补充
        case cmd_thread_add_addendum:
            onThreadCommand(cmd, body);
            break;

        //帖子群：删除补充
        case cmd_thread_delete_addendum:
            onThreadCommand(cmd, body);
            break;

        //帖子群：核对客户端本地存的卡片，返回已改写为已删除形态的（docs/帖子群方案.md 14.6）
        case cmd_thread_check_cards:
            onThreadCommand(cmd, body);
            break;

        //帖子群：2720 只由服务端推送，客户端发来时按报文非法应答
        case cmd_thread_changed_notify:
            onThreadCommand(cmd, body);
            break;

        //帖子群：2721 只由服务端推送，客户端发来时按报文非法应答
        case cmd_thread_cards_notify:
            onThreadCommand(cmd, body);
            break;

        default:
            //该 cmd 对应的业务尚未从 v1 迁移到 v2，暂忽略
            LOGI("cmd %u not implemented in v2 yet, ignored, userid: %d",
                 (unsigned)cmd, m_userinfo.userid);
            break;
        }
        break;
    }

    return true;
}

void ChatSession::onLogin(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    LoginRequest req;
    if (!LoginProtocol::parseLoginRequest(body, req))
    {
        LOGE("parse login request failed, body: %s, client: %s",
             body.c_str(), conn->peerAddress().toIpPort().c_str());
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_bad_request, "bad login request"));
        return;
    }

    //1. 客户端版本校验：低于最低版本要求则拒绝并提示升级。
    //最低版本以版本表里该平台最新发布记录的 f_min_version 为准，这样"拦住谁登录"与
    //"检查更新时算不算强制升级"由同一份配置驱动，不会两边打架；表里没有可用记录时
    //回落到编译期兜底常量。
    std::string minClientVersion = MIN_CLIENT_VERSION_FALLBACK;
    ClientVersionInfo latestForLogin;
    if (Singleton<ClientVersionManager>::Instance().getLatestVersion(req.platform, latestForLogin)
        && !latestForLogin.minVersion.empty())
    {
        minClientVersion = latestForLogin.minVersion;
    }

    if (!LoginProtocol::isVersionAtLeast(req.clientversion, minClientVersion))
    {
        LOGI("client version too old: %s, required at least: %s, client: %s",
             req.clientversion.c_str(), minClientVersion.c_str(),
             conn->peerAddress().toIpPort().c_str());
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_version_too_old, "client version too old"));
        return;
    }

    //2. 账号查询：账号与手机号都能登录（2026-10-06，docs/登录账号与手机号登录方案.md 第 3.4 节）。
    //   先按账号（不区分大小写），查不到且输入像手机号时再按手机号；都查不到返回未注册
    User cachedUser;
    cachedUser.userid = 0;
    bool matchedByPhone = false;
    if (!Singleton<UserManager>::Instance().findLoginUser(req.account, cachedUser, matchedByPhone)
        || cachedUser.userid == 0)
    {
        LOGI("login rejected: no account or phone matches '%s', client: %s",
             req.account.c_str(), conn->peerAddress().toIpPort().c_str());
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_not_registered, "account not registered"));
        return;
    }
    if (matchedByPhone)
    {
        LOGI("login matched by phone, userid: %d, account: %s", cachedUser.userid, cachedUser.username.c_str());
    }

    //3. 账号停用校验：被 admin 控制台停用的账号禁止登录
    if (cachedUser.status != 0)
    {
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_account_disabled, "account disabled"));
        return;
    }

    //3-1. 机器人与服务号账号不能登录：它们只由服务端代为发消息（docs/机器人方案.md §3.2）
    if (cachedUser.usertype != user_type_normal)
    {
        LOGW("login rejected for non-human account, userid: %d, usertype: %d", cachedUser.userid, cachedUser.usertype);
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_account_disabled, "account cannot login"));
        return;
    }

    //4. 口令校验：MD5(客户端SHA256哈希 + 盐值) 与库中存储哈希比对
    if (!PasswordHasher::verifyPassword(req.password, cachedUser.password, cachedUser.salt))
    {
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_wrong_password, "incorrect password"));
        return;
    }

    //5. 企业可用性校验：放在口令校验【之后】，避免未通过认证的人凭错误码探测
    //   某个账号所属企业的经营状态。企业版里每个员工都归属某个企业，故这里
    //   两种不可用情形都要挡：
    //     · 企业归属为 0 —— 企业已被平台管理员删除（deleteEnterprise 保留员工
    //       账号、只把 f_enterprise_id 清零），此时该账号已无组织可归，按
    //       「企业不存在」回绝；
    //     · 企业存在但状态不是「活跃」—— 待审核或已停用，按「企业不可用」回绝。
    //   与之配套，停用 / 删除企业时会把该企业在线会话立即踢下线（见
    //   ChatServer::kickEnterpriseSessions），两者合起来才是完整的「停服」。
    {
        const int64_t loginEntId =
            Singleton<DepartmentManager>::Instance().getUserEnterpriseId(cachedUser.userid);
        if (loginEntId == 0)
        {
            LOGI("login rejected: enterprise deleted, userid: %d, account: %s",
                 cachedUser.userid, req.account.c_str());
            send(cmd_login, m_seq,
                 LoginProtocol::buildLoginFailResponse(error_code_enterprise_not_exist,
                                                       "enterprise not exist"));
            return;
        }

        EnterpriseInfo entInfo;
        if (!Singleton<EnterpriseManager>::Instance().getEnterprise(loginEntId, entInfo))
        {
            //归属ID 有值却查不到企业：库里数据不一致（企业行没了但用户没被清）。
            //按企业不存在处理，并记 LOGE 以便运维发现这种脏数据。
            LOGE("login rejected: enterprise %lld not found, userid: %d",
                 (long long)loginEntId, cachedUser.userid);
            send(cmd_login, m_seq,
                 LoginProtocol::buildLoginFailResponse(error_code_enterprise_not_exist,
                                                       "enterprise not exist"));
            return;
        }

        if (entInfo.status != kEnterpriseStatusActive)
        {
            LOGI("login rejected: enterprise %lld not active (status: %d), userid: %d",
                 (long long)loginEntId, entInfo.status, cachedUser.userid);
            send(cmd_login, m_seq,
                 LoginProtocol::buildLoginFailResponse(error_code_enterprise_disabled,
                                                       "enterprise disabled"));
            return;
        }
    }

    //6. 同类型设备顶号：踢掉同 userid 且同 clienttype 的旧会话
    ChatServer& server = Singleton<ChatServer>::Instance();
    std::shared_ptr<ChatSession> oldSession;
    server.getSessionByUserIdAndClientType(oldSession, cachedUser.userid, req.clienttype);
    if (oldSession && oldSession.get() != this)
    {
        char kickoutBody[128];
        snprintf(kickoutBody, sizeof(kickoutBody),
                 "{\"reason\":%d,\"msg\":\"logged in on another device\"}",
                 kickout_reason_other_device);
        //被踢下线为服务器主动推送，seq 填 0
        oldSession->send(cmd_kickout, 0, std::string(kickoutBody));
        oldSession->makeSessionInvalid();
        LOGI("kick out old session, userid: %d, clienttype: %d", cachedUser.userid, req.clienttype);
    }

    //7. 记录本会话的登录用户信息
    m_userinfo.userid = cachedUser.userid;
    m_userinfo.username = cachedUser.username;
    m_userinfo.nickname = cachedUser.nickname;
    m_userinfo.password = cachedUser.password;
    m_userinfo.clienttype = req.clienttype;
    m_userinfo.status = req.presence;
    m_userinfo.deviceid = req.deviceid;
    //登录时间先于登录态写入：管理端口的推送线程看到 isLoggedIn() 为 true 时，登录时间已是本次的
    m_loginTimeMs.store(static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND);
    m_isLogin = true;
    //管理控制台的在线名单：本用户多了一台在线设备
    Singleton<AdminLinkServer>::Instance().onUserOnlineChanged(m_userinfo.userid);

    //8. 组装并下发成功应答
    LoginProfile profile;
    profile.usertype = cachedUser.usertype;
    profile.username = cachedUser.username;
    profile.nickname = cachedUser.nickname;
    profile.facetype = cachedUser.facetype;
    profile.customface = cachedUser.customface;
    profile.gender = cachedUser.gender;
    profile.birthday = cachedUser.birthday;
    profile.signature = cachedUser.signature;
    profile.address = cachedUser.address;
    profile.phonenumber = cachedUser.phonenumber;
    profile.landline    = cachedUser.landline;
    profile.post        = cachedUser.post;
    profile.mail = cachedUser.mail;
    profile.phonevisible    = cachedUser.phonevisible;
    profile.landlinevisible = cachedUser.landlinevisible;
    //企业归属不取自 UserManager 的用户缓存（那里没有加载 f_enterprise_id），
    //而是问 DepartmentManager —— 它按 t_department_member 维护着 userid 到
    //企业ID的映射。下面广播上线通知时还要用同一个值，故只查这一次。
    const int64_t loginEnterpriseId =
        Singleton<DepartmentManager>::Instance().getUserEnterpriseId(cachedUser.userid);
    profile.enterpriseid = loginEnterpriseId;

    int64_t servertime = static_cast<int64_t>(time(NULL)) * 1000;
    //登录应答带上本企业的功能开关（docs/文件共享方案.md 5.8），客户端据此决定是否加载文件共享模块
    send(cmd_login, m_seq,
         LoginProtocol::buildLoginSuccessResponse(cachedUser.userid, profile, servertime,
                                                  Singleton<FeatureSwitchManager>::Instance().featuresFor(loginEnterpriseId)));

    LOGI("user login ok, userid: %d, account: %s, clienttype: %d, client: %s",
         cachedUser.userid, req.account.c_str(), req.clienttype,
         conn->peerAddress().toIpPort().c_str());

    //向本账号其它在线设备推送本设备上线通知（cmd_device_changed_notify）
    server.broadcastDeviceChanged(m_userinfo.userid, device_change_online,
                                  m_userinfo.clienttype, m_id);

    //向同企业的在线同事广播本用户上线（在线状态变更 1109）：客户端联系人树是企业组织
    //架构，同事据此把本用户的状态点实时点亮（presence=登录状态）。排除发起者自己。
    //本调用未持有会话表锁，可安全调用 broadcastToEnterprise（其内部自行取会话快照）。
    //无企业归属（如平台超管）的账号不推关系链通知 —— 企业版里它没有联系人可推。
    if (loginEnterpriseId != 0)
    {
        std::string onlineBody = ProfileProtocol::buildPresenceChangedNotify(
            m_userinfo.userid, m_userinfo.status, m_userinfo.clienttype);
        server.broadcastToEnterprise(loginEnterpriseId, cmd_presence_changed_notify,
                                     onlineBody, m_userinfo.userid);
    }

    //通话：宽限期内重新登录，他参与的通话继续
    Singleton<CallManager>::Instance().markUserOnline(m_userinfo.userid);
    //远程桌面同理
    Singleton<RemoteDesktopManager>::Instance().markUserOnline(m_userinfo.userid);

    //屏幕共享：宽限期内重新登录的发起人恢复房间，新的中转参数发给这条连接（推送，seq 填 0）
    m_screenShareOfflineDone = false;
    std::vector<ScreenShareReply> shareReplies;
    ScreenShareService::onUserLogin(m_userinfo.userid, shareReplies);
    for (size_t i = 0; i < shareReplies.size(); ++i)
        send(shareReplies[i].m_cmd, 0, shareReplies[i].m_body);

    //群通话：宽限期内重新登录的已加入成员回到房间，新的中转参数发给这条连接（推送，seq 填 0）
    m_groupCallOfflineDone = false;
    {
        GroupCallServerEnv env;
        GroupCallService service(Singleton<GroupCallManager>::Instance(), env);
        std::vector<GroupCallReply> callReplies;
        service.onUserLogin(m_userinfo.userid, static_cast<int64_t>(time(NULL)) * 1000, callReplies);
        for (size_t i = 0; i < callReplies.size(); ++i)
            send(callReplies[i].m_cmd, 0, callReplies[i].m_body);
    }
}

void ChatSession::onHeartbeat(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    //心跳应答：原样回一个空包体心跳帧
    send(cmd_heartbeat, m_seq, std::string());
}

void ChatSession::onLogout(const std::shared_ptr<TcpConnection>& conn)
{
    LOGI("user logout, userid: %d, client: %s",
         m_userinfo.userid, conn->peerAddress().toIpPort().c_str());
    send(cmd_logout, m_seq, std::string("{\"code\":0,\"msg\":\"ok\"}"));
    m_isLogin = false;
    //管理控制台的在线名单：注销后的会话不再计入在线（连接可能还留着，等断开时还会再通知一次，无害）
    Singleton<AdminLinkServer>::Instance().onUserOnlineChanged(m_userinfo.userid);

    //本连接名下若还挂着一笔文件夹传输，此刻即作废：续传位点存在客户端内存里，退出登录
    //就丢了。此处主动清一次而不是等连接断开，是因为客户端可以只登出、不断开连接（换账号
    //登录走的就是这条路），那样对端会一直停在"传输中"直到中转通道超时才发现不对
    cleanupFolderTransferOnOffline();

    //文件共享同理：退出登录后这台电脑不再提供共享，成员要立刻看到「暂不可用」
    cleanupFileShareOnOffline();

    //屏幕共享同理：若这是本用户最后一个登录态的连接，观看者按离开处理、发起人进入宽限期
    cleanupScreenShareOnOffline();

    //通话同理：若这是本用户最后一个登录态的连接，他参与的通话进入掉线宽限期
    cleanupCallOnOffline();

    //群通话同理：振铃中的人从房间移除，已加入的人进入掉线宽限期
    cleanupGroupCallOnOffline();

    //远程桌面同理
    cleanupRemoteDesktopOnOffline();

    //P2P：退出登录后本连接不再参与 P2P，结束它的会话并通知对端；换账号重新登录时客户端会重新发 2301
    cleanupP2POnOffline();

    //密聊同理：退出登录后本连接不再参与密聊，进行中的密聊对方转为「只能查看」；重新登录时客户端会重新发 2601
    cleanupSecretOnOffline();

    //帖子群：退出登录后撤销本连接的「正在查看」登记
    cleanupThreadWatchOnOffline();

    //登出通常紧跟连接断开，设备下线通知由 ChatServer::onDisconnected 统一推送
}

void ChatSession::onGetDevices(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    //在线设备即本账号当前的全部在线会话；同一 clienttype 至多一个（同类型顶号）
    ChatServer& server = Singleton<ChatServer>::Instance();
    std::list<std::shared_ptr<ChatSession>> sessions;
    server.getSessions(sessions);

    std::vector<DeviceInfo> devices;
    for (auto& s : sessions)
    {
        if (!s || s->getUserId() != m_userinfo.userid)
            continue;

        DeviceInfo d;
        d.clienttype = s->getClientType();
        d.status     = s->getUserStatus();
        d.iscurrent  = (s->getSessionId() == m_id);
        devices.push_back(d);
    }

    send(cmd_get_devices, m_seq,
         LoginProtocol::buildGetDevicesResponse(0, "ok", devices));
    LOGI("get devices ok, userid: %d, count: %u",
         m_userinfo.userid, static_cast<unsigned>(devices.size()));
}

void ChatSession::onKickDevice(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int clienttype = 0;
    if (!LoginProtocol::parseKickDeviceRequest(body, clienttype))
    {
        send(cmd_kick_device, m_seq,
             LoginProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //不能踢当前设备：要下线本设备请用 cmd_logout
    if (clienttype == m_userinfo.clienttype)
    {
        send(cmd_kick_device, m_seq,
             LoginProtocol::buildSimpleResponse(error_code_bad_request, "cannot kick current device"));
        return;
    }

    //定位目标设备会话（userid + clienttype 唯一确定一个会话）
    ChatServer& server = Singleton<ChatServer>::Instance();
    std::shared_ptr<ChatSession> target;
    if (!server.getSessionByUserIdAndClientType(target, m_userinfo.userid, clienttype) || !target)
    {
        send(cmd_kick_device, m_seq,
             LoginProtocol::buildSimpleResponse(error_code_not_found, "device not online"));
        return;
    }

    int32_t kickedSessionId = target->getSessionId();

    //向被踢设备推 cmd_kickout 并使其会话失效
    char kickoutBody[128] = { 0 };
    snprintf(kickoutBody, sizeof(kickoutBody),
             "{\"reason\":%d,\"msg\":\"kicked by another device\"}",
             kickout_reason_kicked_by_user);
    target->send(cmd_kickout, 0, std::string(kickoutBody));
    target->makeSessionInvalid();

    send(cmd_kick_device, m_seq, LoginProtocol::buildSimpleResponse(0, "ok"));

    //向本账号其余设备（含发起方，便于其刷新设备列表）推设备下线通知，排除被踢设备本身
    server.broadcastDeviceChanged(m_userinfo.userid, device_change_offline,
                                  clienttype, kickedSessionId);

    LOGI("kick device ok, userid: %d, kicked clienttype: %d", m_userinfo.userid, clienttype);
}

//把注册失败的错误码翻成一句英文原因，随应答一起下发。
//客户端展示给用户的中文文案由客户端按错误码自己决定（服务端不下发中文），
//这里的字符串只用于抓包排查与服务端日志，所以保持简短的英文小写风格，
//与本文件其它应答（"bad request" 等）一致。
static const char* registerFailReason(int32_t code)
{
    switch (code)
    {
    case error_code_register_fail:
        return "create account failed";
    case error_code_already_registered:
        return "phone already registered";
    case error_code_enterprise_not_exist:
        return "company not found";
    case error_code_enterprise_disabled:
        return "company not active";
    case error_code_enterprise_no_root_dept:
        return "company has no root department";
    //企业关闭了允许用户自助注册，只能由管理员新建账号
    case error_code_register_closed:
        return "self registration closed by company";
    //昵称命中所加入企业的敏感词库，账号没有建出来
    case error_code_profile_sensitive_word:
        return "nickname contains sensitive word";
    default:
        return "register failed";
    }
}

/**
 *  按某企业的敏感词库检查一组文字，命中时写一条拦截记录、记一条日志并返回 true。
 *  ChatSession::hitSensitive（已登录的会话）与自助注册的数据源适配器（此时尚未登录、
 *  还没有账号）共用本函数，规则见 ChatSession::hitSensitive 的注释。
 *    enterpriseId： 提交者所属企业ID；不大于 0 时视为未命中
 *    userId：       提交者 userid；自助注册时为 0
 *    userName：     写进拦截记录的提交者名称
 *    source：       拦截来源，取值见 chatserver::sensitive_hit_source
 *    ctype / cid：  会话类型与会话ID；与会话无关时都传 0
 *    targetName：   写进拦截记录的会话名称；传空串时单聊按 cid 取对方昵称、群聊按 cid 取群名
 *    texts：        待检查的文字（UTF-8），空串跳过
 */
static bool checkSensitiveAndRecord(int64_t enterpriseId, int64_t userId, const std::string& userName,
                                    int source, int ctype, int64_t cid, const std::string& targetName,
                                    const std::vector<std::string>& texts)
{
    SensitiveWordManager& sensitiveMgr = Singleton<SensitiveWordManager>::Instance();
    SensitiveCheckResult result;
    if (!sensitiveMgr.checkTexts(enterpriseId, texts, &result))
        return false;

    SensitiveHitRecord record;
    record.m_enterpriseId = enterpriseId;
    record.m_userId = userId;
    record.m_userName = userName;
    record.m_source = source;
    record.m_ctype = ctype;
    record.m_cid = cid;
    record.m_targetName = targetName;
    //会话名只在命中时才取，未命中的请求不多做一次查询
    if (record.m_targetName.empty())
    {
        if (ctype == CHAT_TYPE_SINGLE)
        {
            User peer;
            peer.userid = 0;
            if (Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(cid), peer))
                record.m_targetName = peer.nickname;
        }
        else if (ctype == CHAT_TYPE_GROUP)
        {
            GroupRecord group;
            if (Singleton<GroupManager>::Instance().getGroup(cid, group))
                record.m_targetName = group.name;
        }
    }
    record.m_wordId = result.m_wordId;
    record.m_word = result.m_word;
    record.m_category = result.m_category;
    record.m_snippet = result.m_snippet;
    record.m_createTime = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;
    //写记录失败只影响管理员事后查询，本次请求照样拒绝；失败原因 SensitiveWordManager 已记日志
    sensitiveMgr.writeHit(record);

    LOGI("sensitive word hit, userid: %lld, enterpriseid: %lld, source: %d, ctype: %d, cid: %lld, "
         "wordid: %lld, category: %d, textindex: %u",
         (long long)userId, (long long)enterpriseId, source, ctype, (long long)cid,
         (long long)result.m_wordId, result.m_category, static_cast<unsigned>(result.m_textIndex));
    return true;
}

bool ChatSession::checkSensitiveForServer(int64_t enterpriseId, int64_t userId, const std::string& userName, int source,
                                          int ctype, int64_t cid, const std::vector<std::string>& texts)
{
    return checkSensitiveAndRecord(enterpriseId, userId, userName, source, ctype, cid, std::string(), texts);
}

//屏幕共享标题与描述的敏感词检查（第 15 期）：按提交者所属企业的词库检查，命中时记一条来源为
//sensitive_source_screenshare 的拦截记录。企业 id 在真正要检查时才查，不发起、不修改标题的屏幕共享请求不多做一次查询。
class ChatSessionShareSensitiveChecker : public IScreenShareSensitiveChecker
{
public:
    /**
     *  @param userId   提交者 userid
     *  @param userName 写进拦截记录的提交者名称
     */
    ChatSessionShareSensitiveChecker(int64_t userId, const std::string& userName)
        : m_userId(userId)
        , m_userName(userName)
    {
    }

    virtual bool hitShareInfo(int ctype, int64_t cid, const std::vector<std::string>& texts)
    {
        const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userId);
        return checkSensitiveAndRecord(enterpriseId, m_userId, m_userName, chatserver::sensitive_source_screenshare,
                                       ctype, cid, std::string(), texts);
    }

private:
    int64_t     m_userId;     //提交者 userid
    std::string m_userName;   //写进拦截记录的提交者名称：优先昵称，昵称为空时为登录账号
};

//云端录像标题的敏感词检查（第 26 期）：按提交者所属企业的词库检查，命中时记一条来源为 sensitive_source_cloudrecord 的拦截记录
class ChatSessionCloudRecordTitleChecker : public ICloudRecordTitleChecker
{
public:
    /**
     *  @param userId   提交者 userid
     *  @param userName 写进拦截记录的提交者名称
     */
    ChatSessionCloudRecordTitleChecker(int64_t userId, const std::string& userName)
        : m_userId(userId)
        , m_userName(userName)
    {
    }

    virtual bool hitTitle(const std::string& title) override
    {
        const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userId);
        std::vector<std::string> texts(1, title);
        return checkSensitiveAndRecord(enterpriseId, m_userId, m_userName, chatserver::sensitive_source_cloudrecord, 0, 0,
                                       std::string(), texts);
    }

private:
    int64_t     m_userId;     //提交者 userid
    std::string m_userName;   //写进拦截记录的提交者名称：优先昵称，昵称为空时为登录账号
};

//把 RegisterService 的数据访问接口接到本进程的几个管理器单例上。
//这层只做"转接 + 类型翻译"，不含任何业务判断——判断顺序与错误码全在
//RegisterService::doRegister 里，那部分才是被单元测试覆盖的对象。
class ChatSessionRegisterDataSource : public IRegisterDataSource
{
public:
    virtual bool findEnterpriseByName(const std::string& name, RegisterEnterprise& out)
    {
        EnterpriseInfo info;
        if (!Singleton<EnterpriseManager>::Instance().getEnterpriseByName(name, info))
            return false;

        out.enterpriseId = info.enterpriseId;
        out.name         = info.name;
        out.status       = info.status;
        out.rootDeptId   = info.rootDeptId;
        return true;
    }

    virtual bool isRegisterAllowed(int64_t enterpriseId)
    {
        return Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, kFeatureRegister);
    }

    virtual bool isPhoneTaken(const std::string& phone)
    {
        User existing;
        existing.userid = 0;
        return Singleton<UserManager>::Instance().getUserInfoByPhone(phone, existing) && existing.userid != 0;
    }

    virtual bool hitSensitiveNickname(int64_t enterpriseId, const RegisterRequest& req)
    {
        std::vector<std::string> texts;
        texts.push_back(req.nickname);
        //此时还没有账号：提交者 userid 记 0，名称记手机号，管理员据此找到是哪一次注册
        return checkSensitiveAndRecord(enterpriseId, 0, req.phone, chatserver::sensitive_source_register,
                                       0, 0, std::string(), texts);
    }

    virtual bool createUser(const RegisterRequest& req, int64_t enterpriseId,
                            int64_t& outUserId, std::string& outUsername)
    {
        //v2 口令存储为 MD5(客户端SHA256哈希 + 每用户随机盐)。
        User u;
        //登录账号按企业账号前缀抽一个随机号（没设前缀时用 EMP），与 admin 控制台建员工同规则
        //（docs/登录账号与手机号登录方案.md 第 3.3 节）。不再用连续的用户ID拼账号，免得暴露注册先后
        const std::string prefix = Singleton<EnterpriseManager>::Instance().getAccountPrefix(enterpriseId);
        u.username = Singleton<UserManager>::Instance().allocRandomAccount(prefix);
        if (u.username.empty())
            return false;
        u.nickname    = req.nickname;
        u.phonenumber = req.phone;
        u.usertype    = user_type_normal;
        u.salt        = PasswordHasher::generateSalt();
        u.password    = PasswordHasher::hashPassword(req.password, u.salt);
        if (!Singleton<UserManager>::Instance().addUser(u, enterpriseId))
            return false;

        outUserId   = u.userid;
        outUsername = u.username;
        return true;
    }

    virtual bool addUserToDepartment(int64_t deptId, int64_t userId)
    {
        //注册进来的都是普通员工：挂在公司根部门下、置为主部门、职位留空由管理员后补。
        //不设 operationtype：addMember 本身不看这个字段（它就是"添加"这一种操作），
        //admin 控制台的 onAdminUserCreate 同样没设，两处保持一致。
        dept::OperateDeptMemberReq memberReq;
        memberReq.deptid      = deptId;
        memberReq.userid      = userId;
        memberReq.isprimary   = 1;
        memberReq.memberorder = 0;

        //注意 DepartmentManager 用的是 AdminApiCodes.h 那套旧错误码，与本文件使用的 Protocol.h
        //新错误码数值并不通用，因此这里只把它折成成功/失败，绝不向上透传原始数值。
        int32_t code = Singleton<DepartmentManager>::Instance().addMember(memberReq);
        return code == 0;
    }
};

void ChatSession::onRegister(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    RegisterRequest req;
    if (!RegisterProtocol::parseRegisterRequest(body, req))
    {
        send(cmd_register, m_seq, RegisterProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //业务判断整体交给 RegisterService：手机号查重、按公司全称定位企业、企业
    //可用性、公司根部门是否就绪、建号（登录账号在这一步自动生成）、挂进公司根部门。
    //注册流程已不再校验短信验证码；SmsCodeManager 仍服务于找回密码（onResetPassword）。
    ChatSessionRegisterDataSource dataSource;
    RegisterOutcome outcome;
    int32_t code = RegisterService::doRegister(dataSource, req, outcome);
    if (code != error_code_ok)
    {
        send(cmd_register, m_seq, RegisterProtocol::buildSimpleResponse(code, registerFailReason(code)));
        //失败时还没有账号可打，用手机号与公司名定位是哪一次注册。
        LOGI("register failed, code: %d, phone: %s, company: %s, client: %s",
             code, req.phone.c_str(), req.company.c_str(), conn->peerAddress().toIpPort().c_str());
        return;
    }

    send(cmd_register, m_seq,
         RegisterProtocol::buildRegisterResponse(0, "ok", outcome.userid, outcome.username,
                                                 outcome.enterpriseId, outcome.enterpriseName));

    if (!outcome.departmentAttached)
    {
        //账号已经建出来且能登录，只是没进组织架构，登录后通讯录会是空的。
        //这属于必须人工介入的情况，用 LOGE 打出来，事后由运维把这类孤儿账号补挂回
        //公司根部门。
        LOGE("register: user created but not attached to root department, userid: %lld, enterpriseid: %lld",
             (long long)outcome.userid, (long long)outcome.enterpriseId);
    }
    else
    {
        //新人已进组织架构，通知该企业已在线的同事刷新通讯录。少了这一步，那些人的
        //通讯录还停在各自登录那一刻的快照上：看不到这位新同事，收到他的消息时也只能
        //显示成"用户<userid>"这种兜底名字，直到自己重新登录一次才好。
        //与管理台建员工那条路径（HttpSession::onAdminUserCreate）保持一致。
        //注意本连接此刻尚未登录（getUserId() 为 0），broadcastOrgStructureChanged
        //会把它跳过，所以刚注册的这个客户端不会收到自己触发的这条通知。
        Singleton<ChatServer>::Instance().broadcastOrgStructureChanged(
            outcome.enterpriseId, dept::org_change_member, outcome.rootDeptId);
    }

    LOGI("register ok, userid: %lld, username: %s, enterpriseid: %lld, enterprise: %s, client: %s",
         (long long)outcome.userid, outcome.username.c_str(), (long long)outcome.enterpriseId,
         outcome.enterpriseName.c_str(), conn->peerAddress().toIpPort().c_str());
}

void ChatSession::onSendSmsCode(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    std::string phone;
    int scene = 0;
    if (!RegisterProtocol::parseSendSmsCodeRequest(body, phone, scene))
    {
        send(cmd_send_sms_code, m_seq, RegisterProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //生成验证码并存内存；实际短信下发为桩——把验证码写日志，开发期从日志读取，
    //生产环境此处应改为调用短信网关下发
    std::string code = Singleton<SmsCodeManager>::Instance().generateCode(phone);
    send(cmd_send_sms_code, m_seq, RegisterProtocol::buildSimpleResponse(0, "ok"));
    LOGI("sms code requested, phone: %s, scene: %d, code: %s (TODO: send via real SMS gateway), client: %s",
         phone.c_str(), scene, code.c_str(), conn->peerAddress().toIpPort().c_str());
}

void ChatSession::onResetPassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    ResetPasswordRequest req;
    if (!RegisterProtocol::parseResetPasswordRequest(body, req))
    {
        send(cmd_reset_password, m_seq, RegisterProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //校验短信验证码
    if (!Singleton<SmsCodeManager>::Instance().verifyCode(req.phone, req.smscode))
    {
        send(cmd_reset_password, m_seq,
             RegisterProtocol::buildSimpleResponse(error_code_sms_code_error, "sms code error"));
        return;
    }

    //按手机号定位账号
    UserManager& userMgr = Singleton<UserManager>::Instance();
    User u;
    u.userid = 0;
    if (!userMgr.getUserInfoByPhone(req.phone, u) || u.userid == 0)
    {
        send(cmd_reset_password, m_seq,
             RegisterProtocol::buildSimpleResponse(error_code_not_registered, "phone not registered"));
        return;
    }

    //沿用原盐值，写入新口令的存储哈希
    std::string newStored = PasswordHasher::hashPassword(req.newpassword, u.salt);
    if (!userMgr.modifyUserPassword(u.userid, newStored))
    {
        send(cmd_reset_password, m_seq,
             RegisterProtocol::buildSimpleResponse(error_code_change_pwd_fail, "reset password failed"));
        return;
    }

    send(cmd_reset_password, m_seq, RegisterProtocol::buildSimpleResponse(0, "ok"));

    //踢掉该账号所有在线会话，要求用新密码重新登录
    char kickoutBody[128];
    snprintf(kickoutBody, sizeof(kickoutBody),
             "{\"reason\":%d,\"msg\":\"password reset\"}", kickout_reason_password_changed);
    std::list<std::shared_ptr<ChatSession>> allSessions;
    Singleton<ChatServer>::Instance().getSessions(allSessions);
    for (auto& s : allSessions)
    {
        if (s && s->getUserId() == u.userid)
        {
            s->send(cmd_kickout, 0, std::string(kickoutBody));
            s->makeSessionInvalid();
        }
    }

    LOGI("reset password ok, userid: %d, phone: %s, client: %s",
         u.userid, req.phone.c_str(), conn->peerAddress().toIpPort().c_str());
}

//把 UserManager 的 User 资料与实时在线状态填充成应答用的 ContactInfo
//  viewerUserId：本次请求通讯录的用户ID。联系人设为不公开的手机号、座机只发给本人
//           （docs/联系人资料字段扩充方案.md 第 9 节，见 ContactsProtocol::fillContactFromUser）。
//  remarks：本次请求发起人的"联系人 → 备注名"映射（见 ContactRemarkManager::getRemarks）。
//           备注名是单向的，同一个联系人在不同人的通讯录里备注不同，故要按
//           发起人的映射来填；不在映射里（没给这个人设过备注）则留空串。
//  tags：   本次请求发起人的"联系人 → 标签列表"映射（见 ContactTagManager::getTagsAll）。
//           标签同样是单向的，填法与备注名一致；没贴过标签则留空列表。
static void fillContactInfo(ContactInfo& c, const User& u, ChatServer& server, int64_t viewerUserId,
                            const std::map<int32_t, std::string>& remarks,
                            const std::map<int32_t, std::vector<std::string> >& tags)
{
    ContactsProtocol::fillContactFromUser(c, u, viewerUserId);
    //在线状态与设备类型由 ChatServer 会话表实时给出，离线时为 0
    c.presence    = server.getUserStatusByUserId(u.userid);
    c.clienttype  = server.getUserClientTypeByUserId(u.userid);

    //备注名：没给这个联系人设过备注就留空串，客户端据此退回显示 nickname。
    std::map<int32_t, std::string>::const_iterator it = remarks.find(u.userid);
    c.remark = (it != remarks.end()) ? it->second : std::string();

    //标签：没给这个联系人贴过标签就留空列表，客户端据此不渲染标签区。
    std::map<int32_t, std::vector<std::string> >::const_iterator itTag = tags.find(u.userid);
    if (itTag != tags.end())
        c.tags = itTag->second;
}

void ChatSession::onGetContacts(const std::shared_ptr<TcpConnection>& conn)
{
    ContactsData data;
    data.synctime = static_cast<int64_t>(time(NULL)) * 1000;

    UserManager&       userMgr = Singleton<UserManager>::Instance();
    ChatServer&        server  = Singleton<ChatServer>::Instance();
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();

    //1. 解析登录用户所属企业，并取企业名称
    int64_t enterpriseId = deptMgr.getUserEnterpriseId(m_userinfo.userid);
    data.enterpriseid = enterpriseId;
    if (enterpriseId != 0)
    {
        EnterpriseInfo entInfo;
        if (Singleton<EnterpriseManager>::Instance().getEnterprise(enterpriseId, entInfo))
            data.enterprisename = entInfo.name;
    }

    //2. 本人给各联系人起的备注名，一次取全。备注名是单向的（只影响"本人看对方"），
    //   所以它属于本次请求的发起人，而不是联系人自身的资料，须随每个人各自的
    //   通讯录下发。取不到（数据库不可用）就当没有任何备注，通讯录照发。
    std::map<int32_t, std::string> remarks;
    Singleton<ContactRemarkManager>::Instance().getRemarks(m_userinfo.userid, remarks);

    //2b. 本人给各联系人贴的标签，同样一次取全。标签与备注名性质相同（单向、属于
    //    发起人而不属于联系人本身），所以走同一个口径：一次查完按 target 归组，
    //    下面填联系人时逐个取用，避免每个联系人查一次把登录拖慢。
    //    取不到（数据库不可用）就当没有任何标签，通讯录照发。
    std::map<int32_t, std::vector<std::string> > contactTags;
    Singleton<ContactTagManager>::Instance().getTagsAll(m_userinfo.userid, contactTags);

    //3. 部门树 + 部门成员；同时把成员去重收进联系人资料池
    std::vector<dept::DepartmentInfo> orgDepts;
    if (enterpriseId != 0)
        deptMgr.getOrgStructure(enterpriseId, orgDepts);

    std::set<int32_t> contactIdSet;
    for (const dept::DepartmentInfo& od : orgDepts)
    {
        DepartmentInfo d;
        d.deptid       = od.deptid;
        d.name         = od.deptname;
        d.parentdeptid = od.parentdeptid;
        d.deptorder    = od.deptorder;
        d.leaderuserid = od.leaderuserid;

        for (const dept::DepartmentMember& om : od.members)
        {
            DeptMemberRef ref;
            ref.userid      = om.userid;
            ref.position    = om.position;
            ref.isprimary   = om.isprimary;
            ref.memberorder = om.memberorder;
            d.members.push_back(ref);

            //首次见到的成员才查资料、入池，避免跨部门成员重复
            if (contactIdSet.insert(static_cast<int32_t>(om.userid)).second)
            {
                User u;
                if (userMgr.getUserInfoByUserId(static_cast<int32_t>(om.userid), u))
                {
                    ContactInfo c;
                    fillContactInfo(c, u, server, m_userinfo.userid, remarks, contactTags);
                    data.contacts.push_back(c);
                }
            }
        }

        data.departments.push_back(d);
    }

    //4. 群组：从 t_group_member 查登录用户加入的群（WHERE f_userid 命中 idx_user 索引），
    //   JOIN t_group 取群资料；详见 GroupManager::getUserGroups
    std::vector<GroupRecord> myGroups;
    Singleton<GroupManager>::Instance().getUserGroups(m_userinfo.userid, myGroups);
    for (const GroupRecord& gr : myGroups)
    {
        GroupInfo gi;
        gi.groupid     = gr.groupid;
        gi.name        = gr.name;
        gi.facetype    = gr.facetype;
        gi.customface  = gr.customface;
        gi.ownerid     = gr.ownerid;
        gi.membercount = gr.membercount;
        data.groups.push_back(gi);
    }

    //5. favorites（我的收藏）：联系人与群混在一个列表里，按收藏时间倒序。
    //   拉不到时按"没有收藏"降级 —— 收藏只是通讯录顶上的一个分区，不该因为它
    //   查询失败就让整棵部门树也发不出去。serviceaccounts 留待后续，仍为空数组。
    Singleton<FavoriteManager>::Instance().getFavorites(m_userinfo.userid, data.favorites);

    //原第 6 项 starred（特别关注名单）已于 2026-10-03 删除：特别关注并入会话的消息提醒模式，
    //名单改由 cmd_sync_conversations 应答的 notifymodes 下发（docs/消息提醒模式与消息盒子方案.md §5.5）

    std::string resp = ContactsProtocol::buildContactsResponse(data);
    send(cmd_get_contacts, m_seq, resp);

    //tagged 是"有标签的联系人个数"而不是标签总条数：排查时关心的是标签有没有随
    //通讯录发出去，逐条数意义不大，也免得把用户贴的内容规模写进日志。
    LOGI("get contacts ok, userid: %d, departments: %u, contacts: %u, groups: %u, favorites: %u, tagged: %u, client: %s",
         m_userinfo.userid, static_cast<unsigned>(data.departments.size()),
         static_cast<unsigned>(data.contacts.size()),
         static_cast<unsigned>(data.groups.size()),
         static_cast<unsigned>(data.favorites.size()),
         static_cast<unsigned>(contactTags.size()),
         conn->peerAddress().toIpPort().c_str());
}

//把 UserManager 的 User 转换为账号资料应答用的 UserProfile（含实时在线状态）。
//得到的是号码完整的一份资料；发给谁之前须经 ProfileProtocol::applyViewer 或
//buildUserInfoChangedNotifies 按查看者处理（docs/联系人资料字段扩充方案.md 第 9 节）
static void fillUserProfile(UserProfile& p, const User& u)
{
    ChatServer& server = Singleton<ChatServer>::Instance();
    ProfileProtocol::fillProfileFromUser(p, u);
    p.enterpriseid = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(u.userid);
    p.presence     = server.getUserStatusByUserId(u.userid);
    p.clienttype   = server.getUserClientTypeByUserId(u.userid);
}

//向本人所在企业推送资料变更（cmd_user_info_changed_notify）。改资料与改头像共用。
//同事收到按公开范围隐藏了号码的一份；本人的全部在线会话收到号码完整、带公开范围标志的一份
//（PC 客户端通讯录树里本人那一行与本人的其它设备靠它同步）。
//  enterpriseId：本人所属企业ID，调用方保证不为 0
//  u：           本人更新后的用户记录
static void broadcastProfileChanged(int64_t enterpriseId, const User& u)
{
    UserProfile p;
    fillUserProfile(p, u);
    std::string ownerBody;
    std::string colleagueBody;
    ProfileProtocol::buildUserInfoChangedNotifies(p, ownerBody, colleagueBody);

    ChatServer& server = Singleton<ChatServer>::Instance();
    server.broadcastToEnterprise(enterpriseId, cmd_user_info_changed_notify, colleagueBody, u.userid);
    std::vector<int64_t> owner(1, static_cast<int64_t>(u.userid));
    server.pushToUsers(owner, cmd_user_info_changed_notify, ownerBody);
}

void ChatSession::onGetUserInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t targetid = 0;
    if (!ProfileProtocol::parseGetUserInfoRequest(body, targetid))
    {
        send(cmd_get_user_info, m_seq, ProfileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    User u;
    u.userid = 0;
    Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(targetid), u);
    if (u.userid == 0)
    {
        send(cmd_get_user_info, m_seq, ProfileProtocol::buildSimpleResponse(error_code_not_found, "user not found"));
        return;
    }

    UserProfile p;
    fillUserProfile(p, u);
    //取别人的资料时，对方不公开的号码不下发；取自己的资料时号码完整，并带上两个公开范围标志
    ProfileProtocol::applyViewer(p, m_userinfo.userid);
    send(cmd_get_user_info, m_seq, ProfileProtocol::buildGetUserInfoResponse(0, "ok", p));
}

void ChatSession::onUpdateProfile(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    UpdateProfileRequest req;
    if (!ProfileProtocol::parseUpdateProfileRequest(body, req))
    {
        send(cmd_update_profile, m_seq, ProfileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();
    User u;
    u.userid = 0;
    userMgr.getUserInfoByUserId(m_userinfo.userid, u);
    if (u.userid == 0)
    {
        send(cmd_update_profile, m_seq, ProfileProtocol::buildSimpleResponse(error_code_server_internal, "user not found"));
        return;
    }

    //原昵称，写库成功后据此判断昵称是否真的改变（改变时要重算所在的自动命名群）
    const std::string oldNickname = u.nickname;

    //企业级敏感词：只查本次提交、且与当前值不同的昵称、签名、所在地、邮箱、岗位（规则见
    //ProfileProtocol::collectChangedProfileTexts）。放在合并与写库之前，被拦下时资料保持原样
    UserProfile currentProfile;
    fillUserProfile(currentProfile, u);
    std::vector<std::string> sensitiveTexts;
    ProfileProtocol::collectChangedProfileTexts(req, currentProfile, sensitiveTexts);
    if (hitSensitive(chatserver::sensitive_source_profile, 0, 0, std::string(), sensitiveTexts))
    {
        send(cmd_update_profile, m_seq,
             ProfileProtocol::buildSimpleResponse(error_code_profile_sensitive_word, "profile contains sensitive word"));
        return;
    }

    //把本次提交的字段合并进当前资料（带了才改，手机号永不改动）；updateUserInfoInDb 会整体写回
    ProfileProtocol::applyProfileUpdate(req, u);

    if (!userMgr.updateUserInfoInDb(m_userinfo.userid, u))
    {
        send(cmd_update_profile, m_seq, ProfileProtocol::buildSimpleResponse(error_code_update_profile_fail, "update profile failed"));
        return;
    }

    send(cmd_update_profile, m_seq, ProfileProtocol::buildSimpleResponse(0, "ok"));

    //广播资料变更。客户端"联系人树"是企业组织架构，故按企业范围推送，使同事端实时刷新该联系人
    //昵称/签名等。发起者本人也推（2026-10-04 起）：PC 客户端的通讯录树里有本人那一行（带「（我）」），
    //靠这条推送同步；本人在线的其它设备也据此同步。与改头像(onChangeAvatar)同口径。
    //同事与本人收到的内容不同：不公开的号码只发给本人（2026-10-06 起，见 broadcastProfileChanged）。
    //无企业归属的账号不推 —— 企业版里它没有联系人可推。
    int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (enterpriseId != 0)
        broadcastProfileChanged(enterpriseId, u);

    //昵称改变后，本人所在的自动命名群的群名随之重算，群名改变的向全群推送资料变更
    if (u.nickname != oldNickname)
        Singleton<ChatServer>::Instance().refreshAutoGroupNamesOfMemberAndNotify(m_userinfo.userid);

    LOGI("update profile ok, userid: %d", m_userinfo.userid);
    if (req.has_phonevisible || req.has_landlinevisible)
        LOGI("update profile visibility, userid: %d, phonevisible: %d, landlinevisible: %d",
             m_userinfo.userid, u.phonevisible, u.landlinevisible);
}

void ChatSession::onChangePassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    ChangePasswordRequest req;
    if (!ProfileProtocol::parseChangePasswordRequest(body, req))
    {
        send(cmd_change_password, m_seq, ProfileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();
    User u;
    u.userid = 0;
    userMgr.getUserInfoByUserId(m_userinfo.userid, u);
    if (u.userid == 0)
    {
        send(cmd_change_password, m_seq, ProfileProtocol::buildSimpleResponse(error_code_server_internal, "user not found"));
        return;
    }

    //校验旧口令
    if (!PasswordHasher::verifyPassword(req.oldpassword, u.password, u.salt))
    {
        send(cmd_change_password, m_seq, ProfileProtocol::buildSimpleResponse(error_code_wrong_password, "incorrect old password"));
        return;
    }

    //沿用原盐值，算出新口令的存储哈希
    std::string newStored = PasswordHasher::hashPassword(req.newpassword, u.salt);
    if (!userMgr.modifyUserPassword(m_userinfo.userid, newStored))
    {
        send(cmd_change_password, m_seq, ProfileProtocol::buildSimpleResponse(error_code_change_pwd_fail, "change password failed"));
        return;
    }

    send(cmd_change_password, m_seq, ProfileProtocol::buildSimpleResponse(0, "ok"));

    //踢掉该账号其它在线会话，要求用新密码重新登录
    char kickoutBody[128];
    snprintf(kickoutBody, sizeof(kickoutBody),
             "{\"reason\":%d,\"msg\":\"password changed\"}", kickout_reason_password_changed);
    std::list<std::shared_ptr<ChatSession>> allSessions;
    Singleton<ChatServer>::Instance().getSessions(allSessions);
    for (auto& s : allSessions)
    {
        if (s && s.get() != this && s->getUserId() == m_userinfo.userid)
        {
            s->send(cmd_kickout, 0, std::string(kickoutBody));
            s->makeSessionInvalid();
        }
    }

    LOGI("change password ok, userid: %d", m_userinfo.userid);
}

void ChatSession::onChangeAvatar(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    ChangeAvatarRequest req;
    if (!ProfileProtocol::parseChangeAvatarRequest(body, req))
    {
        send(cmd_change_avatar, m_seq, ProfileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();
    User u;
    u.userid = 0;
    userMgr.getUserInfoByUserId(m_userinfo.userid, u);
    if (u.userid == 0)
    {
        send(cmd_change_avatar, m_seq, ProfileProtocol::buildSimpleResponse(error_code_server_internal, "user not found"));
        return;
    }

    u.facetype = req.facetype;
    u.customface = req.customface;
    if (!userMgr.updateUserInfoInDb(m_userinfo.userid, u))
    {
        send(cmd_change_avatar, m_seq, ProfileProtocol::buildSimpleResponse(error_code_update_profile_fail, "change avatar failed"));
        return;
    }

    send(cmd_change_avatar, m_seq, ProfileProtocol::buildSimpleResponse(0, "ok"));

    //同步刷新组织架构缓存里本用户的头像字段——cmd_get_contacts 的数据源是该缓存，
    //不刷新则同事端重登仍取到旧头像（组织架构内存缓存陈旧坑）。
    Singleton<DepartmentManager>::Instance().updateCachedMemberAvatar(
        m_userinfo.userid, req.facetype, req.customface);

    //广播资料变更。客户端"联系人树"是企业组织架构，故按企业范围推送，使同事端实时刷新该联系人
    //头像。发起者本人也推（2026-10-04 起），理由同改资料(onUpdateProfile)：本人那一行在通讯录树里，
    //本人的其它设备也要同步。无企业归属的账号不推 —— 企业版里它没有联系人可推。
    //推送内容同样按查看者区分：不公开的号码只发给本人（见 broadcastProfileChanged）。
    int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (enterpriseId != 0)
        broadcastProfileChanged(enterpriseId, u);

    LOGI("change avatar ok, userid: %d", m_userinfo.userid);

    //customface 是头像图片的 fileid：本人若是上传者，把该文件记录标记为已完成。
    //未设自定义头像时 customface 为空串，与其它非法取值一样由 parseFileIdText 过滤掉。
    int64_t avatarFileId = 0;
    if (MessageProtocol::parseFileIdText(req.customface, avatarFileId))
        markReferencedFileCompleted(avatarFileId, "avatar");
}

void ChatSession::onChangePresence(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    ChangePresenceRequest req;
    if (!ProfileProtocol::parseChangePresenceRequest(body, req))
    {
        send(cmd_change_presence, m_seq, ProfileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //在线状态是会话运行态，不入库
    m_userinfo.status = req.presence;
    send(cmd_change_presence, m_seq, ProfileProtocol::buildSimpleResponse(0, "ok"));
    //管理控制台的在线名单：本设备的在线状态变了
    Singleton<AdminLinkServer>::Instance().onUserOnlineChanged(m_userinfo.userid);

    //广播在线状态变更。客户端的"联系人树"是企业组织架构（同事），
    //故按<u>企业范围</u>推送：同企业的在线用户都能实时刷新该联系人的状态点。
    //排除发起者自己（其联系人树不含自己）。无企业归属（enterpriseid==0，如平台超管）
    //的账号不推 —— 企业版里它没有联系人可推。
    int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (enterpriseId != 0)
    {
        std::string notifyBody = ProfileProtocol::buildPresenceChangedNotify(
            m_userinfo.userid, req.presence, m_userinfo.clienttype);
        Singleton<ChatServer>::Instance().broadcastToEnterprise(
            enterpriseId, cmd_presence_changed_notify, notifyBody, m_userinfo.userid);
    }

    LOGI("change presence ok, userid: %d, presence: %d, enterpriseid: %lld",
         m_userinfo.userid, req.presence, (long long)enterpriseId);
}

void ChatSession::sendUserStatusChangeMsg(int32_t userid, int type, int status)
{
    //本会话客户端收到某联系人在线状态变更的推送：type 取值 1 上线 / 2 下线。
    //注意：下面的 getUserClientTypeByUserId 会加 ChatServer::m_sessionMutex，所以本方法
    //不能在持有该锁时调用，否则非递归锁重复加锁会终止进程。详见头文件里的调用约束说明。
    int presence = (type == 2) ? 0 : status;
    int clienttype = Singleton<ChatServer>::Instance().getUserClientTypeByUserId(userid);
    send(cmd_presence_changed_notify, 0,
         ProfileProtocol::buildPresenceChangedNotify(userid, presence, clienttype));
}

void ChatSession::sendToUser(int32_t userid, uint16_t cmd, const std::string& body)
{
    //遍历全部在线会话，把通知推给目标 userid 的每一个在线设备
    std::list<std::shared_ptr<ChatSession>> allSessions;
    Singleton<ChatServer>::Instance().getSessions(allSessions);
    for (auto& s : allSessions)
    {
        if (s && s->getUserId() == userid)
            s->send(cmd, 0, body);
    }
}

void ChatSession::onSearch(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string keyword;
    int type = 1;
    if (!FriendProtocol::parseSearchRequest(body, keyword, type))
    {
        send(cmd_search, m_seq, FriendProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();
    std::vector<SearchResultItem> items;

    //本期实现 type=1 用户、type=2 群；type=3 找企业预留，results 留空
    if (type == 1 || type == 2)
    {
        bool wantGroup = (type == 2);
        std::vector<User> matched;
        userMgr.searchUsersByPrefix(keyword, wantGroup, matched, SEARCH_MAX_RESULTS);
        for (const User& u : matched)
        {
            //用户查找结果中排除自己
            if (!wantGroup && u.userid == m_userinfo.userid)
                continue;

            SearchResultItem it;
            it.id         = u.userid;
            it.facetype   = u.facetype;
            it.customface = u.customface;
            it.name       = u.nickname;
            if (wantGroup)
            {
                it.ownerid = u.ownerid;
                //群成员数取 t_group_member 的真实计数（GroupManager::getGroup 用相关子查询
                //一并带出）。这里原先数的是群账号在好友关系表里占的行数 —— 那是老实现
                //"把群成员也写进 t_user_relationship"留下的副产物，好友关系表删掉后就永远是 0 了。
                GroupRecord gr;
                if (Singleton<GroupManager>::Instance().getGroup(u.userid, gr))
                    it.membercount = gr.membercount;
            }
            else
            {
                it.username  = u.username;
                it.gender    = u.gender;
                it.signature = u.signature;
                it.address   = u.address;
                it.usertype  = u.usertype;
            }
            items.push_back(it);
        }
    }

    send(cmd_search, m_seq, FriendProtocol::buildSearchResponse(0, "ok", type, items));
    LOGI("search ok, userid: %d, keyword: %s, type: %d, results: %u",
         m_userinfo.userid, keyword.c_str(), type, static_cast<unsigned>(items.size()));
}

void ChatSession::onGetAnnouncements(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GetAnnouncementsReqData req;
    if (!AnnouncementProtocol::parseGetAnnouncementsRequest(body, req))
    {
        send(cmd_get_announcements, m_seq,
             AnnouncementProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //受众匹配、平台内部公告的过滤、已读标记的回填都在 Manager 里，本处只做转发与组包。
    std::vector<AnnouncementPushItem> items;
    bool hasMore = false;
    Singleton<AnnouncementManager>::Instance().getAnnouncementsForUser(
        m_userinfo.userid, req.sinceId, req.limit, items, hasMore);

    send(cmd_get_announcements, m_seq,
         AnnouncementProtocol::buildGetAnnouncementsResponse(0, "ok", hasMore, items));
    LOGI("get announcements ok, userid: %d, sinceid: %lld, count: %u, hasmore: %d",
         m_userinfo.userid, (long long)req.sinceId,
         static_cast<unsigned>(items.size()), hasMore ? 1 : 0);
}

void ChatSession::onMarkAnnouncementRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t announcementId = 0;
    if (!AnnouncementProtocol::parseMarkReadRequest(body, announcementId))
    {
        send(cmd_mark_announcement_read, m_seq,
             AnnouncementProtocol::buildMarkReadResponse(error_code_bad_request, "bad request", 0));
        return;
    }

    int32_t code = Singleton<AnnouncementManager>::Instance().markAnnouncementRead(
        announcementId, m_userinfo.userid);

    //应答原样回带 announcementid，客户端据此把结果对上是哪一条上报。
    send(cmd_mark_announcement_read, m_seq,
         AnnouncementProtocol::buildMarkReadResponse(
             code, code == error_code_ok ? "ok" : "mark announcement read failed", announcementId));
    LOGI("mark announcement read, userid: %d, announcementid: %lld, code: %d",
         m_userinfo.userid, (long long)announcementId, code);
}

void ChatSession::onSetFriendRemark(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SetFriendRemarkRequestData req;
    if (!FriendProtocol::parseSetFriendRemarkRequest(body, req))
    {
        //解析都没过，req.userid 多半是 0；照样回带，客户端对不上号会直接丢弃，
        //不会误伤别的在途请求。
        send(cmd_set_friend_remark, m_seq,
             FriendProtocol::buildSetFriendRemarkResponse(error_code_bad_request,
                                                          "bad request", req.userid));
        return;
    }

    const int32_t targetId = static_cast<int32_t>(req.userid);
    UserManager& userMgr = Singleton<UserManager>::Instance();

    //企业版没有好友关系，通讯录里的人全部来自组织架构，所以这里<u>不</u>校验"是不是好友"
    //（那道校验在企业版里会让本功能 100% 不可用），只要求目标用户确实存在。
    User target;
    target.userid = 0;
    userMgr.getUserInfoByUserId(targetId, target);
    if (target.userid == 0)
    {
        send(cmd_set_friend_remark, m_seq,
             FriendProtocol::buildSetFriendRemarkResponse(error_code_not_found,
                                                          "target user not found", req.userid));
        return;
    }

    //一秒合多少毫秒。备注名的更新时间与本文件其它落库时间戳同一口径。
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    //备注名是单向的：只改"本人看对方"那一侧；remark 为空串表示清除备注。
    int errorCode = error_code_ok;
    if (!Singleton<ContactRemarkManager>::Instance().setRemark(
            m_userinfo.userid, targetId, req.remark, nowMs, errorCode))
    {
        LOGI("set contact remark failed, userid: %d, target: %d, code: %d",
             m_userinfo.userid, targetId, errorCode);

        send(cmd_set_friend_remark, m_seq,
             FriendProtocol::buildSetFriendRemarkResponse(errorCode,
                                                          "set remark failed", req.userid));
        return;
    }

    send(cmd_set_friend_remark, m_seq,
         FriendProtocol::buildSetFriendRemarkResponse(0, "ok", req.userid));
    LOGI("set contact remark ok, userid: %d, target: %d, remark: %s",
         m_userinfo.userid, targetId, req.remark.c_str());
}

//取"userid 当前给 targetUserId 贴的标签"的落库现状，供 cmd_set_contact_tags 的
//失败应答回带。写失败时客户端界面上还留着用户刚编辑的那一份，得把服务端真正
//存着的这一份发回去，它才知道该回滚成什么样子。
//  out：[出参] 进函数先清空；查不到（数据库不可用、或本来就没贴过）则保持为空。
static void loadCurrentContactTags(int64_t userid, int64_t targetUserId,
                                   std::vector<std::string>& out)
{
    out.clear();

    std::map<int32_t, std::vector<std::string> > all;
    if (!Singleton<ContactTagManager>::Instance().getTagsAll(userid, all))
        return;

    std::map<int32_t, std::vector<std::string> >::const_iterator it =
        all.find(static_cast<int32_t>(targetUserId));
    if (it != all.end())
        out = it->second;
}

void ChatSession::onSetContactTags(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;

    //失败应答里回带的标签列表：解析都没过时无从查起，留空数组即可。
    std::vector<std::string> respTags;

    SetContactTagsRequest req;
    if (!ContactTagProtocol::parseSetContactTagsRequest(body, req))
    {
        //解析都没过，req.userid 多半是 0；照样回带，客户端对不上号会直接丢弃，
        //不会误伤别的在途请求。
        send(cmd_set_contact_tags, m_seq,
             ContactTagProtocol::buildSetContactTagsResponse(error_code_bad_request,
                                                             "bad request", req.userid, respTags));
        return;
    }

    const int32_t targetId = static_cast<int32_t>(req.userid);

    //不能给自己贴标签：通讯录里本就不显示本人，贴了也没有任何地方会用到。
    if (targetId == m_userinfo.userid)
    {
        send(cmd_set_contact_tags, m_seq,
             ContactTagProtocol::buildSetContactTagsResponse(error_code_bad_request,
                                                             "cannot tag yourself",
                                                             req.userid, respTags));
        return;
    }

    //企业版没有好友关系，通讯录里的人全部来自组织架构，所以这里<u>不</u>校验"是不是好友"，
    //只要求目标用户确实存在。口径与 cmd_set_friend_remark 完全一致：标签与备注名
    //都是贴在"本人怎么看这个人"上的，够不着的人贴了也只是几行无人读取的数据。
    User target;
    target.userid = 0;
    Singleton<UserManager>::Instance().getUserInfoByUserId(targetId, target);
    if (target.userid == 0)
    {
        send(cmd_set_contact_tags, m_seq,
             ContactTagProtocol::buildSetContactTagsResponse(error_code_not_found,
                                                             "target user not found",
                                                             req.userid, respTags));
        return;
    }

    //一秒合多少毫秒。标签的更新时间与本文件其它落库时间戳同一口径。
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    //整份覆盖：请求里带的就是该联系人在本人视角下的完整标签列表，空数组表示清空。
    int errorCode = error_code_ok;
    if (!Singleton<ContactTagManager>::Instance().setTags(
            m_userinfo.userid, targetId, req.tags, nowMs, errorCode))
    {
        const char* msg = "set contact tags failed";
        if (errorCode == error_code_tag_limit)
            msg = "tag count limit reached";

        //一条都没写进去，客户端界面上却已经是用户刚编辑的样子，故回带服务端现存的那一份
        loadCurrentContactTags(m_userinfo.userid, targetId, respTags);

        LOGI("set contact tags failed, userid: %d, target: %d, submitted: %u, code: %d",
             m_userinfo.userid, targetId, static_cast<unsigned>(req.tags.size()), errorCode);

        send(cmd_set_contact_tags, m_seq,
             ContactTagProtocol::buildSetContactTagsResponse(errorCode, msg,
                                                             req.userid, respTags));
        return;
    }

    //应答回带服务端归一后的最终列表（去首尾空白、丢空串、忽略大小写去重之后的样子），
    //客户端据它把界面刷成与服务端一致。复用存储层的同一份归一逻辑，不必为拿这份
    //列表再查一次库 —— setTags 成功即意味着落库内容与它逐条相同。
    ContactTagManager::normalizeTags(req.tags, respTags);

    send(cmd_set_contact_tags, m_seq,
         ContactTagProtocol::buildSetContactTagsResponse(error_code_ok, "ok",
                                                         req.userid, respTags));

    //标签文本可能含姓名、项目代号一类敏感内容，日志只记条数，不记内容
    LOGI("set contact tags ok, userid: %d, target: %d, submitted: %u, stored: %u",
         m_userinfo.userid, targetId, static_cast<unsigned>(req.tags.size()),
         static_cast<unsigned>(respTags.size()));
}

int ChatSession::checkSingleChatNotifyTarget(int64_t peerId, int mode)
{
    //设回「允许提醒」不做归属校验：对方离职、被移出企业之后，用户更需要能把这一行清掉
    if (mode == notify_mode_allow)
        return error_code_ok;

    const int32_t targetId = static_cast<int32_t>(peerId);

    //不能对自己设置 —— 自己与自己没有会话
    if (targetId == m_userinfo.userid)
        return error_code_bad_request;

    //目标用户须存在
    User target;
    target.userid = 0;
    Singleton<UserManager>::Instance().getUserInfoByUserId(targetId, target);
    if (target.userid == 0)
        return error_code_not_found;

    //够得着的判定：同属一个企业即可，与收藏对联系人的校验同口径（沿用并入前特别关注的校验）。
    //跨企业的人在通讯录里根本看不见，与他也不会有消息往来，设了提醒模式没有意义
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    const int64_t myEnterpriseId     = deptMgr.getUserEnterpriseId(m_userinfo.userid);
    const int64_t targetEnterpriseId = deptMgr.getUserEnterpriseId(targetId);
    if (myEnterpriseId == 0 || myEnterpriseId != targetEnterpriseId)
        return error_code_not_friend;

    return error_code_ok;
}

void ChatSession::onSetNotifyModes(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::vector<SetNotifyModeEntry> entries;
    if (!MessageProtocol::parseSetNotifyModesRequest(body, entries))
    {
        LOGI("set notify modes rejected, bad request, userid: %d, body bytes: %u",
             m_userinfo.userid, static_cast<unsigned>(body.size()));
        send(cmd_set_notify_modes, m_seq,
             MessageProtocol::buildSetNotifyModesResponse(error_code_bad_request, "bad request",
                                                          std::vector<NotifyModeResult>()));
        return;
    }

    //本人加入的群一次取全，逐条判断成员身份时查集合，不必每条都查一次库
    std::vector<GroupRecord> myGroups;
    Singleton<GroupManager>::Instance().getUserGroups(m_userinfo.userid, myGroups);
    std::set<int64_t> myGroupIds;
    for (size_t i = 0; i < myGroups.size(); ++i)
        myGroupIds.insert(myGroups[i].groupid);

    //逐条预先校验：格式、取值、群成员身份、单聊对象。不通过的条目带上各自的错误码，
    //MessageManager::setNotifyModes 会跳过它们
    std::vector<NotifyModeItem> items;
    std::vector<NotifyModeResult> results;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const NotifyModeItem& item = entries[i].item;
        NotifyModeResult result;
        result.ctype = item.ctype;
        result.cid   = item.cid;
        result.code  = error_code_ok;

        if (!entries[i].wellformed || !MessageProtocol::isValidNotifyMode(item.ctype, item.mode))
            result.code = error_code_bad_request;
        else if (item.ctype == conversation_group && myGroupIds.count(item.cid) == 0)
            result.code = error_code_not_group_member;
        else if (item.ctype == conversation_single)
            result.code = checkSingleChatNotifyTarget(item.cid, item.mode);

        items.push_back(item);
        results.push_back(result);
    }

    //一秒合多少毫秒。更新时间与本文件其它落库时间戳同一口径
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    std::vector<NotifyModeItem> changed;
    const bool ok = Singleton<MessageManager>::Instance().setNotifyModes(
        m_userinfo.userid, items, nowMs, results, changed);

    if (!ok)
    {
        LOGE("set notify modes failed, userid: %d, items: %u", m_userinfo.userid,
             static_cast<unsigned>(items.size()));
        send(cmd_set_notify_modes, m_seq,
             MessageProtocol::buildSetNotifyModesResponse(error_code_server_internal,
                                                          "set notify modes failed", results));
        return;
    }

    send(cmd_set_notify_modes, m_seq,
         MessageProtocol::buildSetNotifyModesResponse(error_code_ok, "ok", results));

    //改成功的条目推给本人全部在线设备（含发起的这一台），各端据此刷新模式表
    if (!changed.empty())
    {
        sendToUser(m_userinfo.userid, cmd_notify_modes_changed_notify,
                   MessageProtocol::buildNotifyModesChangedNotify(changed));
    }

    LOGI("set notify modes ok, userid: %d, items: %u, changed: %u", m_userinfo.userid,
         static_cast<unsigned>(items.size()), static_cast<unsigned>(changed.size()));
}

void ChatSession::onSetFavorite(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    SetFavoriteRequest req;
    if (!FavoriteProtocol::parseSetFavoriteRequest(body, req))
    {
        //parseSetFavoriteRequest 已经把格式与取值域全查过了（ctype 只认 1/2、
        //cid 须为正、favorite 只认 0/1），失败一律回 bad_request，这里不必再分类。
        send(cmd_set_favorite, m_seq,
             FavoriteProtocol::buildSetFavoriteResponse(error_code_bad_request, "bad request",
                                                        req.ctype, req.cid, req.favorite));
        return;
    }

    //取消收藏不做归属校验：好友被删了、群退了之后，用户更需要能把这条死行清掉。
    //只有"加入收藏"才要求你确实够得着这个对象。
    if (req.favorite == 1)
    {
        if (req.ctype == conversation_single)
        {
            const int32_t targetId = static_cast<int32_t>(req.cid);

            //不能收藏自己 —— 收藏是"快速找到别人"的入口，收藏自己没有意义。
            if (targetId == m_userinfo.userid)
            {
                send(cmd_set_favorite, m_seq,
                     FavoriteProtocol::buildSetFavoriteResponse(error_code_bad_request,
                                                                "cannot favorite yourself",
                                                                req.ctype, req.cid, req.favorite));
                return;
            }

            //目标用户须存在
            User target;
            target.userid = 0;
            Singleton<UserManager>::Instance().getUserInfoByUserId(targetId, target);
            if (target.userid == 0)
            {
                send(cmd_set_favorite, m_seq,
                     FavoriteProtocol::buildSetFavoriteResponse(error_code_not_found,
                                                                "target user not found",
                                                                req.ctype, req.cid, req.favorite));
                return;
            }

            //够得着的判定：同属一个企业即可（企业通讯录里看得见的人都能收藏）。
            //服务号也走这条路 —— 它就是 usertype=1 的用户，同企业即可收藏。
            //原先还有"或者是好友"这半个条件，随好友关系子系统一并去掉了。
            DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
            const int64_t myEnterpriseId     = deptMgr.getUserEnterpriseId(m_userinfo.userid);
            const int64_t targetEnterpriseId = deptMgr.getUserEnterpriseId(targetId);
            const bool sameEnterprise = (myEnterpriseId != 0 && myEnterpriseId == targetEnterpriseId);

            if (!sameEnterprise)
            {
                send(cmd_set_favorite, m_seq,
                     FavoriteProtocol::buildSetFavoriteResponse(error_code_not_friend,
                                                                "not a colleague",
                                                                req.ctype, req.cid, req.favorite));
                return;
            }
        }
        else
        {
            //群：必须是本群成员才能收藏。getMemberRole 返回负值表示不在群里。
            const int role = Singleton<GroupManager>::Instance().getMemberRole(req.cid, m_userinfo.userid);
            if (role < 0)
            {
                send(cmd_set_favorite, m_seq,
                     FavoriteProtocol::buildSetFavoriteResponse(error_code_not_group_member,
                                                                "not a group member",
                                                                req.ctype, req.cid, req.favorite));
                return;
            }
        }
    }

    //一秒合多少毫秒。收藏时间与本文件其它落库时间戳同一口径。
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    int errorCode = error_code_ok;
    if (!Singleton<FavoriteManager>::Instance().setFavorite(
            m_userinfo.userid, req.ctype, req.cid, req.favorite, nowMs, errorCode))
    {
        const char* msg = "set favorite failed";
        if (errorCode == error_code_favorite_limit)
            msg = "favorite count limit reached";

        LOGI("set favorite failed, userid: %d, ctype: %d, cid: %lld, favorite: %d, code: %d",
             m_userinfo.userid, req.ctype, (long long)req.cid, req.favorite, errorCode);

        send(cmd_set_favorite, m_seq,
             FavoriteProtocol::buildSetFavoriteResponse(errorCode, msg,
                                                        req.ctype, req.cid, req.favorite));
        return;
    }

    send(cmd_set_favorite, m_seq,
         FavoriteProtocol::buildSetFavoriteResponse(error_code_ok, "ok",
                                                    req.ctype, req.cid, req.favorite));

    LOGI("set favorite ok, userid: %d, ctype: %d, cid: %lld, favorite: %d, client: %s",
         m_userinfo.userid, req.ctype, (long long)req.cid, req.favorite,
         conn->peerAddress().toIpPort().c_str());
}

void ChatSession::onSetBlacklist(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SetBlacklistRequestData req;
    if (!FriendProtocol::parseSetBlacklistRequest(body, req))
    {
        send(cmd_set_blacklist, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    int32_t targetId = static_cast<int32_t>(req.userid);

    //不能把自己加入黑名单
    if (targetId == m_userinfo.userid)
    {
        send(cmd_set_blacklist, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_bad_request, "cannot blacklist yourself"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();

    //目标用户须存在
    User target;
    target.userid = 0;
    userMgr.getUserInfoByUserId(targetId, target);
    if (target.userid == 0)
    {
        send(cmd_set_blacklist, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_not_found, "target user not found"));
        return;
    }

    //拉黑只写 t_blacklist 这一张表，不牵连任何别的关系；被拉黑方也不会因此被移出通讯录
    //（通讯录来自组织架构，拉黑只影响能不能给对方发消息）。
    bool ok = false;
    if (req.action == BLACKLIST_ACTION_ADD)
        ok = userMgr.addToBlacklist(m_userinfo.userid, targetId);
    else
        ok = userMgr.removeFromBlacklist(m_userinfo.userid, targetId);

    if (!ok)
    {
        send(cmd_set_blacklist, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_friend_op_fail, "set blacklist failed"));
        return;
    }

    send(cmd_set_blacklist, m_seq, FriendProtocol::buildSimpleResponse(0, "ok"));
    //拉黑不向被拉黑方推送任何通知——对方不应感知自己被拉黑
    LOGI("set blacklist ok, userid: %d, target: %d, action: %d",
         m_userinfo.userid, targetId, req.action);
}

//发送消息（cmd_send_message）与群发助手（cmd_mass_send）被敏感词拦下时，拦截记录的来源
//按消息类型区分：文件消息记为文件名，三种卡片记为卡片，其余记为文字
static int sensitiveSourceOfMsgType(int msgtype)
{
    switch (msgtype)
    {
    //文件消息：检查的是文件名
    case message_type_file:
        return chatserver::sensitive_source_file_name;

    //投票、接龙、待办卡片：检查的是卡片里的标题、选项、说明
    case message_type_poll:
    case message_type_relay:
    case message_type_todo:
        return chatserver::sensitive_source_card;

    //文字消息、合并记录及其它类型：检查的是正文与引用
    default:
        return chatserver::sensitive_source_text;
    }
}

void ChatSession::onSendMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SendMessageRequest req;
    if (!MessageProtocol::parseSendMessageRequest(body, req))
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //内容体积超限：不落库、不推送。放在单聊群聊分流之前，两种会话一并拦下。超大消息推送给
    //对方、或在同步时整页下发，都会超过客户端的单帧上限导致对方断线。
    //回执带上 clientmsgid，客户端据此知道是哪一条没有发出去。
    if (MessageProtocol::isMessageContentTooLarge(req.content, req.quote))
    {
        LOGW("send message rejected by content size, userid: %d, ctype: %d, cid: %lld, bytes: %u",
             m_userinfo.userid, req.ctype, static_cast<long long>(req.cid),
             static_cast<unsigned>(req.content.size() + req.quote.size()));
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSendMessageResponse(error_code_msg_too_large, "message content too large",
                                                       req.clientmsgid, 0, 0));
        return;
    }

    //共享卡片与系统提示只能由服务端生成：客户端自行发送等于伪造一张指向别人共享的卡片，或一行系统提示（理由见
    //isClientOperableMsgType 与 isClientSendableMsgType，bugs.md BUG-124）。放在单聊群聊分流之前，两种会话一并拦下
    if (!MessageProtocol::isClientSendableMsgType(req.msgtype))
    {
        LOGW("send message rejected by server-only msgtype, userid: %d, ctype: %d, cid: %lld, msgtype: %d",
             m_userinfo.userid, req.ctype, static_cast<long long>(req.cid), req.msgtype);
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSendMessageResponse(error_code_permission_denied,
                                                       "msgtype is generated by server only",
                                                       req.clientmsgid, 0, 0));
        return;
    }

    //按消息类型检查 content（目前只检查语音：文件走聊天文件的上传通道、上传时不限大小，在这里检查
    //字段与时长、大小、波形的取值范围，见 checkSendContent）。放在单聊群聊分流之前，两种会话一并拦下；
    //不合格的不落库、不推送，回执带上 clientmsgid
    const int contentCode = MessageProtocol::checkSendContent(req.msgtype, req.content);
    if (contentCode != error_code_ok)
    {
        LOGW("send message rejected by invalid content, userid: %d, ctype: %d, cid: %lld, msgtype: %d, bytes: %u",
             m_userinfo.userid, req.ctype, static_cast<long long>(req.cid), req.msgtype,
             static_cast<unsigned>(req.content.size()));
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSendMessageResponse(contentCode, "invalid message content",
                                                       req.clientmsgid, 0, 0));
        return;
    }

    //机器人标记只能由服务端产生：客户端带来的 bot 字段一律删掉，免得普通成员冒充机器人（docs/机器人方案.md §5.6）
    if (bot::StripBotMarker(req.content))
        LOGW("send message: bot marker stripped from client content, userid: %d", m_userinfo.userid);

    //接龙卡片：服务端规范化 content，只保留标题、说明与截止时间，客户端自带的接龙名单、
    //结束状态等字段一律丢弃。放在单聊群聊分流之前，两种会话共用；规范化后的内容替换掉
    //请求原文，此后的落库、回执与推送用的都是它
    if (req.msgtype == message_type_relay)
    {
        const int64_t kMillisPerSecond = 1000;
        const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
        std::string normalized;
        const int cardCode = CardProtocol::normalizeRelayContent(req.content, nowMs, normalized);
        if (cardCode != error_code_ok)
        {
            LOGI("send relay card rejected, code: %d, userid: %d, ctype: %d, cid: %lld",
                 cardCode, m_userinfo.userid, req.ctype, static_cast<long long>(req.cid));
            send(cmd_send_message, m_seq,
                 MessageProtocol::buildSendMessageResponse(cardCode, "invalid relay card",
                                                           req.clientmsgid, 0, 0));
            return;
        }
        req.content = normalized;
    }

    //群聊消息走专门的发送路径
    if (req.ctype == CHAT_TYPE_GROUP)
    {
        sendGroupMessage(req);
        return;
    }
    //其余非单聊类型暂不支持
    if (req.ctype != CHAT_TYPE_SINGLE)
    {
        replySendRejected(error_code_bad_request, "unsupported ctype", req.clientmsgid);
        return;
    }

    int32_t peerId = static_cast<int32_t>(req.cid);

    //不能给自己发消息
    if (peerId == m_userinfo.userid)
    {
        replySendRejected(error_code_bad_request, "cannot send to yourself", req.clientmsgid);
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();

    //对端用户须存在
    User peer;
    peer.userid = 0;
    userMgr.getUserInfoByUserId(peerId, peer);
    if (peer.userid == 0)
    {
        replySendRejected(error_code_not_found, "target user not found", req.clientmsgid);
        return;
    }

    //被对方拉黑则拒发：消息不落库、不推送
    if (userMgr.isInBlacklist(peerId, m_userinfo.userid))
    {
        replySendRejected(error_code_in_blacklist, "you are in the target's blacklist", req.clientmsgid);
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;

    //幂等去重：同一发送方重发相同 clientmsgid 时，直接回执既有消息ID，不再落库
    int64_t existedId = msgMgr.getMsgIdByClientId(m_userinfo.userid, req.clientmsgid);
    if (existedId > 0)
    {
        //既有消息的原始时间未知，回执 timestamp 取当前时间——客户端按 msgid 去重
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSendMessageResponse(0, "ok", req.clientmsgid, existedId, now));
        LOGI("send message deduplicated, from: %d, to: %d, clientmsgid: %s, msgid: %lld",
             m_userinfo.userid, peerId, req.clientmsgid.c_str(), (long long)existedId);
        return;
    }

    //企业级敏感词检查：放在幂等去重之后，客户端重发一条早已发送成功的消息时照常回执原来的
    //msgid，不因为期间新加了词而回 507（那条消息已经送达）；放在落库之前，被拦的消息不留痕迹
    std::vector<std::string> sensitiveTexts;
    MessageProtocol::collectSensitiveTexts(req.msgtype, req.content, req.quote, sensitiveTexts);
    if (hitSensitive(sensitiveSourceOfMsgType(req.msgtype), CHAT_TYPE_SINGLE, peerId, peer.nickname,
                     sensitiveTexts))
    {
        replySendRejected(error_code_sensitive_word, "message contains sensitive word", req.clientmsgid);
        return;
    }

    //落库：content/mentions/mentionall/quote 打包进 f_content
    std::string storedContent = MessageProtocol::buildStoredContent(
        req.content, req.mentions, req.mentionall, req.quote);
    int64_t msgId = msgMgr.saveMessage(req.clientmsgid, req.ctype, m_userinfo.userid,
                                       peerId, req.msgtype, storedContent, now);
    if (msgId == 0)
    {
        replySendRejected(error_code_send_msg_fail, "save message failed", req.clientmsgid);
        return;
    }

    //发送回执
    send(cmd_send_message, m_seq,
         MessageProtocol::buildSendMessageResponse(0, "ok", req.clientmsgid, msgId, now));

    //对端在线则推 cmd_message_notify；离线则消息留库，等其 cmd_sync_messages 拉取
    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = req.clientmsgid;
    rec.ctype       = req.ctype;
    rec.from        = m_userinfo.userid;
    rec.to          = peerId;
    rec.msgtype     = req.msgtype;
    rec.content     = req.content;
    rec.mentions    = req.mentions;
    rec.mentionall  = req.mentionall;
    rec.quote       = req.quote;
    rec.timestamp   = now;
    rec.recalled    = false;
    //收件人视角的会话ID = 发送方userid
    sendToUser(peerId, cmd_message_notify,
               MessageProtocol::buildMessageNotify(rec, m_userinfo.userid));

    LOGI("send message ok, from: %d, to: %d, msgid: %lld, msgtype: %d",
         m_userinfo.userid, peerId, (long long)msgId, req.msgtype);

    //机器人判定入口：收发任一方有个人机器人时投递给机器人线程（docs/机器人方案.md §5.2）
    BotSingleEvent botEv;
    botEv.m_from = m_userinfo.userid;
    botEv.m_to = peerId;
    botEv.m_msgid = msgId;
    botEv.m_msgtype = req.msgtype;
    botEv.m_content = req.content;
    botEv.m_timestamp = now;
    Singleton<BotEngine>::Instance().onSingleMessage(botEv);

    //图片 / 文件等消息：发送方若是上传者，把所引用的文件记录标记为已完成
    markMessageFileCompleted(req.msgtype, req.content);
}

void ChatSession::sendGroupMessage(const SendMessageRequest& req)
{
    int64_t groupid = req.cid;

    //群成员身份、群是否存在、禁言三项校验，与转发到群共用；不通过时已回过错误应答
    int myrole = group_role_member;
    GroupRecord group;
    if (!checkGroupSpeakAllowed(cmd_send_message, groupid, req.clientmsgid, myrole, group))
        return;

    //@全体：请求顶层的 mentionall 或 content.at.all（PC 客户端写在后者）任一为真都算，
    //落库与推送的 mentionall 也按这个结果写
    const bool mentionAll = req.mentionall || MessageProtocol::contentMentionsAll(req.content);

    //@全体校验：群设为"仅群主和管理员可@全体"时，普通成员发带 @全体 的消息被拒
    if (mentionAll && !GroupProtocol::canMentionAll(group.atallscope, myrole))
    {
        replySendRejected(error_code_at_all_denied, "only owner or admin can mention all", req.clientmsgid);
        LOGI("mention all denied, groupid: %lld, userid: %d, role: %d",
             (long long)groupid, m_userinfo.userid, myrole);
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;

    //幂等去重：同一发送方重发相同 clientmsgid 时直接回执既有消息ID
    int64_t existedId = msgMgr.getMsgIdByClientId(m_userinfo.userid, req.clientmsgid);
    if (existedId > 0)
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSendMessageResponse(0, "ok", req.clientmsgid, existedId, now));
        LOGI("send group message deduplicated, from: %d, group: %lld, msgid: %lld",
             m_userinfo.userid, (long long)groupid, (long long)existedId);
        return;
    }

    //企业级敏感词检查：位置的理由与单聊分支相同（去重之后、落库之前）。原先的群级敏感词
    //已于 2026-10-02 删除，现在单聊与群聊按发送者所属企业的同一份词库检查
    std::vector<std::string> sensitiveTexts;
    MessageProtocol::collectSensitiveTexts(req.msgtype, req.content, req.quote, sensitiveTexts);
    if (hitSensitive(sensitiveSourceOfMsgType(req.msgtype), CHAT_TYPE_GROUP, groupid, group.name,
                     sensitiveTexts))
    {
        replySendRejected(error_code_sensitive_word, "message contains sensitive word", req.clientmsgid);
        return;
    }

    //落库：ctype=2、f_to 取群id
    std::string storedContent = MessageProtocol::buildStoredContent(
        req.content, req.mentions, mentionAll, req.quote);
    int64_t msgId = msgMgr.saveMessage(req.clientmsgid, CHAT_TYPE_GROUP, m_userinfo.userid,
                                       static_cast<int32_t>(groupid), req.msgtype, storedContent, now);
    if (msgId == 0)
    {
        replySendRejected(error_code_send_msg_fail, "save message failed", req.clientmsgid);
        return;
    }

    //发送回执
    send(cmd_send_message, m_seq,
         MessageProtocol::buildSendMessageResponse(0, "ok", req.clientmsgid, msgId, now));

    //fan-out：向全体群成员（含发送方，兼顾多端）推 cmd_message_notify；cid 取群id
    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = req.clientmsgid;
    rec.ctype       = CHAT_TYPE_GROUP;
    rec.from        = m_userinfo.userid;
    rec.to          = groupid;
    rec.msgtype     = req.msgtype;
    rec.content     = req.content;
    rec.mentions    = req.mentions;
    rec.mentionall  = mentionAll;
    rec.quote       = req.quote;
    rec.timestamp   = now;
    rec.recalled    = false;
    notifyGroupMembers(groupid, cmd_message_notify,
                       MessageProtocol::buildMessageNotify(rec, groupid));

    LOGI("send group message ok, from: %d, group: %lld, msgid: %lld, msgtype: %d",
         m_userinfo.userid, (long long)groupid, (long long)msgId, req.msgtype);

    //机器人判定入口：本群可能有机器人时投递给机器人线程；群所属企业从内存缓存取，I/O 线程上不查库
    BotGroupEvent botEv;
    botEv.m_groupid = groupid;
    botEv.m_enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid);
    botEv.m_from = m_userinfo.userid;
    botEv.m_msgid = msgId;
    botEv.m_msgtype = req.msgtype;
    botEv.m_content = req.content;
    botEv.m_timestamp = now;
    Singleton<BotEngine>::Instance().onGroupMessage(botEv);

    //图片 / 文件等消息：发送方若是上传者，把所引用的文件记录标记为已完成
    markMessageFileCompleted(req.msgtype, req.content);
}

void ChatSession::markReferencedFileCompleted(int64_t fileid, const char* source)
{
    //是否真的改动了记录取决于本人是不是上传者，由 SQL 的 WHERE 条件决定，这里无从得知，
    //故日志只记"以上传者身份尝试标记"这一事实；数据库出错时 FileStoreManager 自己会记。
    if (Singleton<FileStoreManager>::Instance().markCompletedByUploader(fileid, m_userinfo.userid))
    {
        LOGI("mark referenced file completed, source: %s, fileid: %lld, userid: %d",
             source, (long long)fileid, m_userinfo.userid);
    }
}

void ChatSession::markMessageFileCompleted(int32_t msgtype, const std::string& content)
{
    int64_t fileid = 0;
    if (MessageProtocol::extractContentFileId(msgtype, content, fileid))
        markReferencedFileCompleted(fileid, "message");
}

void ChatSession::onReceivedAck(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t msgid = 0;
    if (!MessageProtocol::parseReceivedAck(body, msgid))
    {
        LOGE("bad received ack, userid: %d, body: %s", m_userinfo.userid, body.c_str());
        return;
    }

    //送达回执为客户端→服务器的单向上报，无需应答。
    //本阶段仅记日志确认送达；完整送达状态机（多端同步、回传发送方）留后续子阶段
    LOGI("message delivered ack, userid: %d, msgid: %lld",
         m_userinfo.userid, (long long)msgid);
}

void ChatSession::onSyncConversations(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    std::vector<ConversationBrief> briefs;
    msgMgr.getConversationBriefs(m_userinfo.userid, briefs);

    //追加群会话：取本人加入的群，再由 MessageManager 算各群的会话摘要
    std::vector<GroupRecord> myGroups;
    Singleton<GroupManager>::Instance().getUserGroups(m_userinfo.userid, myGroups);
    std::vector<int64_t> groupids;
    for (size_t i = 0; i < myGroups.size(); ++i)
        groupids.push_back(myGroups[i].groupid);
    std::vector<ConversationBrief> groupBriefs;
    msgMgr.getGroupConversationBriefs(m_userinfo.userid, groupids, groupBriefs);
    for (size_t i = 0; i < groupBriefs.size(); ++i)
        briefs.push_back(groupBriefs[i]);

    //本人全部非默认提醒模式的会话（含还没有消息、因而不在 briefs 里的群与联系人）。
    //取不到时按"全部是默认模式"降级，不让整个会话列表发不出去
    std::vector<NotifyModeItem> notifyModes;
    msgMgr.getNotifyModes(m_userinfo.userid, notifyModes);

    //本人在各群关注的成员（docs/消息提醒模式与消息盒子方案.md §12.4）；取不到时按没有名单降级
    std::vector<GroupFollowList> groupFollows;
    Singleton<GroupFollowManager>::Instance().getFollowsAll(m_userinfo.userid, groupFollows);

    //synctime 取服务器当前时间，供客户端作下次同步水位
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    send(cmd_sync_conversations, m_seq,
         MessageProtocol::buildSyncConversationsResponse(0, "ok", now, briefs, notifyModes, groupFollows));

    LOGI("sync conversations ok, userid: %d, count: %u, notify modes: %u, group follows: %u",
         m_userinfo.userid, static_cast<unsigned>(briefs.size()),
         static_cast<unsigned>(notifyModes.size()), static_cast<unsigned>(groupFollows.size()));
}

void ChatSession::onGetUserGroups(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    //拉取本人加入的全部群（含无消息的空群）。getUserGroups 以 t_group_member 为
    //驱动表，返回该用户所有成员行对应的群资料（群名 / 成员数等），与"有没有消息"
    //无关 —— 客户端"群"Tab 据此显示完整群列表。
    std::vector<GroupRecord> myGroups;
    Singleton<GroupManager>::Instance().getUserGroups(m_userinfo.userid, myGroups);

    //本人置顶的群。群会话要有消息才进会话列表，没有消息的空群置顶后，客户端只能从这里
    //得知它的置顶状态
    std::set<int64_t> pinnedGroupIds;
    Singleton<MessageManager>::Instance().getPinnedConversationIds(
        m_userinfo.userid, CHAT_TYPE_GROUP, pinnedGroupIds);

    send(cmd_get_user_groups, m_seq,
         GroupProtocol::buildUserGroupsResponse(0, "ok", myGroups, pinnedGroupIds));

    LOGI("get user groups ok, userid: %d, count: %u, pinned: %u",
         m_userinfo.userid, static_cast<unsigned>(myGroups.size()),
         static_cast<unsigned>(pinnedGroupIds.size()));
}

//两个时刻之间相隔的毫秒数（带小数），供 onSyncMessages 的分段计时使用
static double MillisBetween(const std::chrono::steady_clock::time_point& from,
                            const std::chrono::steady_clock::time_point& to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

void ChatSession::augmentSyncedRecord(MessageRecord& rec, int64_t nowMs, unsigned& cardAugments,
                                      unsigned& fileAckLookups)
{
    const int mt = rec.msgtype;
    if (mt == message_type_poll || mt == message_type_relay || mt == message_type_todo
        || mt == message_type_fileshare || mt == message_type_group_invite)
    {
        augmentCardRecord(rec, nowMs);
        ++cardAugments;
    }
    //自己发出的文件消息：补上对方的收取状态。发送方离线期间对方的接收 / 拒绝
    //就是靠这一步补看到的。只查本人发出的那些 —— 接收方自己知道自己收没收，
    //不必为它多查一次库。
    else if (mt == message_type_file && rec.from == m_userinfo.userid)
    {
        rec.fileackstate = Singleton<MessageManager>::Instance().getFileAckState(rec.msgid);
        ++fileAckLookups;
    }
}

void ChatSession::onSyncRewrittenMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SyncRewrittenRequest req;
    if (!MessageProtocol::parseSyncRewrittenRequest(body, req))
    {
        send(cmd_sync_rewritten_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //可见范围与全局搜索相同：本人参与的单聊，加本人当前所在的群（按各群的历史下界过滤）
    std::vector<GroupSearchScope> scopes;
    if (!Singleton<GroupManager>::Instance().getUserHistoryScopes(m_userinfo.userid, scopes))
    {
        send(cmd_sync_rewritten_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_server_internal, "db error"));
        return;
    }

    std::vector<RewrittenMessage> items;
    bool more = false;
    int64_t currev = 0;
    if (!Singleton<MessageManager>::Instance().getRewrittenMessages(m_userinfo.userid, scopes, req.since, req.limit,
                                                                     items, more, currev))
    {
        send(cmd_sync_rewritten_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_server_internal, "db error"));
        return;
    }

    //条目与 cmd_sync_messages 同样加工（卡片状态、文件收取状态），须在按体积截断之前
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    unsigned cardAugments = 0;
    unsigned fileAckLookups = 0;
    for (size_t i = 0; i < items.size(); ++i)
        augmentSyncedRecord(items[i].record, nowMs, cardAugments, fileAckLookups);

    size_t trimmed = 0;
    const std::string response = MessageProtocol::buildSyncRewrittenResponseWithinBudget(
        error_code_ok, "ok", m_userinfo.userid, req.since, items, more, currev, RESPONSE_MESSAGES_BUDGET_BYTES,
        trimmed);
    send(cmd_sync_rewritten_messages, m_seq, response);
    LOGI("sync rewritten messages, userid: %d, since: %lld, limit: %d, items: %u, trimmed: %u, more: %d, "
         "currev: %lld, card augments: %u, file ack lookups: %u",
         m_userinfo.userid, (long long)req.since, req.limit, static_cast<unsigned>(items.size()),
         static_cast<unsigned>(trimmed), more ? 1 : 0, (long long)currev, cardAugments, fileAckLookups);
}

void ChatSession::onSyncMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    //分段计时（2026-10-04）：打开聊天窗时拉第一页历史是用户等得最久的一步，处理结束时按阶段记一行日志，
    //查消息、补卡片与文件状态、查已读位点、按体积裁剪、组装应答、发送各花多久，能直接从日志看出来
    const std::chrono::steady_clock::time_point tStart = std::chrono::steady_clock::now();
    SyncMessagesRequest req;
    if (!MessageProtocol::parseSyncMessagesRequest(body, req))
    {
        send(cmd_sync_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //仅支持单聊与群聊
    if (req.ctype != CHAT_TYPE_SINGLE && req.ctype != CHAT_TYPE_GROUP)
    {
        send(cmd_sync_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "unsupported ctype"));
        return;
    }

    //群聊：须为群成员才能拉取群历史消息。同时算出本人能看到的历史范围 ——
    //群设为"新成员不可查看入群前的历史消息"时取入群时间，邀请时选了「同步最近 N 条」时取那条消息的 msgid。
    GroupHistoryBound bound;
    if (req.ctype == CHAT_TYPE_GROUP)
    {
        if (!Singleton<GroupManager>::Instance().isGroupMember(req.cid, m_userinfo.userid))
        {
            send(cmd_sync_messages, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
            return;
        }
        bound = groupHistoryBound(req.cid);
    }

    //limit 夹到合理区间：缺省 20、上限 100
    if (req.limit <= 0)
        req.limit = SYNC_MESSAGES_DEFAULT_LIMIT;
    if (req.limit > SYNC_MESSAGES_MAX_LIMIT)
        req.limit = SYNC_MESSAGES_MAX_LIMIT;

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    std::vector<MessageRecord> messages;
    bool hasMore = false;
    const std::chrono::steady_clock::time_point tPrepared = std::chrono::steady_clock::now();
    msgMgr.getConversationMessages(m_userinfo.userid, req, messages, hasMore, bound.m_minCreateTime,
                                   bound.m_minMsgId);
    const std::chrono::steady_clock::time_point tQueried = std::chrono::steady_clock::now();

    //卡片消息（投票/接龙/待办）：把当前互动状态合并进 content，使客户端打开会话即见最新；
    //文件共享卡片：按本人合并共享状态与权限。客户端用这里拿到的消息体覆盖本地库，共享卡片不合并时
    //本地的那份也就没有状态，已取消的共享在客户端上会一直显示「正在获取状态」（2026-10-03 用户发现）。
    //须在下面按体积截断之前做，合并后消息体会变大
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    unsigned cardAugments = 0;     //补了互动状态的卡片消息条数（计时日志用）
    unsigned fileAckLookups = 0;   //查了收取状态的文件消息条数（计时日志用）
    for (size_t i = 0; i < messages.size(); ++i)
        augmentSyncedRecord(messages[i], nowMs, cardAugments, fileAckLookups);
    const std::chrono::steady_clock::time_point tAugmented = std::chrono::steady_clock::now();

    //单聊：带上对方的已读位点（查对方那一行 t_conversation：userid=对端、cid=本人）。
    //实时的 cmd_message_read_notify 只推给在线的发送方；发送方重开聊天窗、或离线期间
    //对方读过的消息，都靠这个字段补上正确的「已读 / 未读」。查询失败时按 0 返回，
    //客户端只会把消息显示成「未读」，不会误显示「已读」。群聊不做已读回执，不查也不带。
    int64_t peerReadMsgid = MessageProtocol::kPeerReadMsgidOmitted;
    if (req.ctype == CHAT_TYPE_SINGLE)
    {
        msgMgr.getReadMsgid(static_cast<int32_t>(req.cid), CHAT_TYPE_SINGLE,
                            m_userinfo.userid, peerReadMsgid);
    }
    const std::chrono::steady_clock::time_point tPeerRead = std::chrono::steady_clock::now();

    //组装应答并按体积截断：一页里有几条长消息（或合并记录）就能超过客户端的单帧上限，客户端收到后
    //会断线重连。截掉的是远离锚点的那一端，并把 hasmore 置为真：
    //翻历史模式锚点在最新一端（升序数组的末尾），补齐模式锚点在开头。客户端据留下的消息
    //计算覆盖区间，截掉的消息在区间之外，不会被当作已删除。
    //先整页组装、超过预算才截断后重新组装，绝大多数页面每条消息只构造、序列化一遍。
    //传入请求本身，应答据此原样回带锚点（beforemsgid 或 aftermsgid），客户端用它计算本页
    //覆盖了哪段 msgid，见 docs/清理聊天记录方案.md §7.2。
    //组装与发送分开写，是为了分别计时：一页里有几条长消息时，组装 JSON 与发送（含压缩、加密）都不便宜
    size_t trimmed = 0;
    const std::string response = MessageProtocol::buildSyncMessagesResponseWithinBudget(
        0, "ok", req.ctype, req.cid, hasMore, messages, peerReadMsgid, &req,
        !req.isAfterMode, RESPONSE_MESSAGES_BUDGET_BYTES, trimmed);
    const std::chrono::steady_clock::time_point tBuilt = std::chrono::steady_clock::now();
    if (trimmed > 0)
    {
        LOGI("sync messages trimmed by size, userid: %d, ctype: %d, cid: %lld, kept: %u, trimmed: %u",
             m_userinfo.userid, req.ctype, (long long)req.cid,
             static_cast<unsigned>(messages.size()), static_cast<unsigned>(trimmed));
    }
    send(cmd_sync_messages, m_seq, response);
    const std::chrono::steady_clock::time_point tSent = std::chrono::steady_clock::now();

    LOGI("sync messages ok, userid: %d, cid: %lld, before: %lld, after: %lld, count: %u, "
         "hasmore: %d, peerreadmsgid: %lld",
         m_userinfo.userid, (long long)req.cid,
         req.isAfterMode ? 0LL : (long long)req.beforemsgid,
         req.isAfterMode ? (long long)req.aftermsgid : 0LL,
         static_cast<unsigned>(messages.size()), hasMore ? 1 : 0, (long long)peerReadMsgid);
    LOGI("sync messages timing, userid: %d, cid: %lld, count: %u: prepare %.1f ms, query %.1f ms, "
         "augment %.1f ms (cards %u, file acks %u), peer read %.1f ms, "
         "build %.1f ms (%u bytes, trimmed %u), send %.1f ms, total %.1f ms",
         m_userinfo.userid, (long long)req.cid, static_cast<unsigned>(messages.size()),
         MillisBetween(tStart, tPrepared), MillisBetween(tPrepared, tQueried),
         MillisBetween(tQueried, tAugmented), cardAugments, fileAckLookups,
         MillisBetween(tAugmented, tPeerRead),
         MillisBetween(tPeerRead, tBuilt), static_cast<unsigned>(response.size()),
         static_cast<unsigned>(trimmed),
         MillisBetween(tBuilt, tSent), MillisBetween(tStart, tSent));
}

void ChatSession::onMarkRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    MarkReadRequest req;
    if (!MessageProtocol::parseMarkReadRequest(body, req))
    {
        send(cmd_mark_read, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //仅支持单聊与群聊
    if (req.ctype != CHAT_TYPE_SINGLE && req.ctype != CHAT_TYPE_GROUP)
    {
        send(cmd_mark_read, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "unsupported ctype"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    //t_conversation 按 (userid,ctype,cid) 联合主键，单聊 / 群聊都用同一 markRead
    msgMgr.markRead(m_userinfo.userid, req.ctype, req.cid, req.readmsgid, now);

    send(cmd_mark_read, m_seq, MessageProtocol::buildSimpleResponse(0, "ok"));

    //单聊：通知对端其消息已被读到 readmsgid（cid 取本人userid，readcount 恒为 1）。
    //群聊不发已读回执——群已读回执是 N×M 重型特性，业界通常不做，仅维护本人位点。
    if (req.ctype == CHAT_TYPE_SINGLE)
    {
        int32_t peerId = static_cast<int32_t>(req.cid);
        sendToUser(peerId, cmd_message_read_notify,
                   MessageProtocol::buildMessageReadNotify(req.ctype, m_userinfo.userid,
                                                           req.readmsgid, SINGLE_CHAT_READ_COUNT));
    }

    LOGI("mark read ok, userid: %d, ctype: %d, cid: %lld, readmsgid: %lld",
         m_userinfo.userid, req.ctype, (long long)req.cid, (long long)req.readmsgid);
}

void ChatSession::onRecallMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    RecallMessageRequest req;
    if (!MessageProtocol::parseRecallMessageRequest(body, req))
    {
        send(cmd_recall_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessageRecord rec;
    if (!msgMgr.getMessageById(req.msgid, rec))
    {
        send(cmd_recall_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_message_not_exist, "message not exist"));
        return;
    }

    //共享卡片不能撤回：撤回与「取消共享」语义重叠，统一走 cmd_share_cancel
    if (!MessageProtocol::isClientOperableMsgType(rec.msgtype))
    {
        send(cmd_recall_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_permission_denied, "msgtype cannot be recalled"));
        LOGI("recall message rejected by server-only msgtype, userid: %d, msgid: %lld, msgtype: %d",
             m_userinfo.userid, (long long)req.msgid, rec.msgtype);
        return;
    }

    //已撤回则幂等返回，不重复落库与推送
    if (rec.recalled)
    {
        send(cmd_recall_message, m_seq, MessageProtocol::buildSimpleResponse(0, "ok"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;

    if (rec.ctype == CHAT_TYPE_GROUP)
    {
        //群消息撤回：发送者本人、或群主 / 管理员可撤回
        int64_t groupid = rec.to;
        int myrole = Singleton<GroupManager>::Instance().getMemberRole(groupid, m_userinfo.userid);
        if (myrole < 0)
        {
            send(cmd_recall_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
            return;
        }

        bool isSender = (rec.from == m_userinfo.userid);
        bool isAdmin  = (myrole >= group_role_admin);
        if (!isSender && !isAdmin)
        {
            send(cmd_recall_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_recall_not_allowed, "no permission to recall"));
            return;
        }
        //时限只约束"普通成员撤回本人消息"；群主 / 管理员撤回不受时限
        if (isSender && !isAdmin && now - rec.timestamp > RECALL_TIME_LIMIT_MS)
        {
            send(cmd_recall_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_recall_timeout, "recall time limit exceeded"));
            return;
        }

        if (!msgMgr.recallMessage(req.msgid, m_userinfo.userid))
        {
            send(cmd_recall_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_server_internal, "recall failed"));
            return;
        }
        //撤回的消息不该再出现在任何人的 Pin 分类里，顺手清掉它的 Pin 行。
        //这里<u>不</u>额外推 cmd_message_pinned_notify —— 客户端本地库的查询恒含
        //recalled=0，撤回推送一到这条自然就从 Pin 分类消失了，少一条推送就少一处竞态。
        Singleton<MessagePinManager>::Instance().removePinForMessage(req.msgid);
        send(cmd_recall_message, m_seq, MessageProtocol::buildSimpleResponse(0, "ok"));

        //向全体群成员推 cmd_message_recalled_notify（cid 取群id）
        notifyGroupMembers(groupid, cmd_message_recalled_notify,
                           MessageProtocol::buildMessageRecalledNotify(CHAT_TYPE_GROUP, groupid,
                                                                       req.msgid, m_userinfo.userid));
        LOGI("recall group message ok, userid: %d, group: %lld, msgid: %lld",
             m_userinfo.userid, (long long)groupid, (long long)req.msgid);
        return;
    }

    //以下为单聊消息撤回：只能撤回本人发送的消息
    if (rec.from != m_userinfo.userid)
    {
        send(cmd_recall_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_recall_not_allowed, "can only recall your own message"));
        return;
    }
    //超出可撤回时限则拒绝
    if (now - rec.timestamp > RECALL_TIME_LIMIT_MS)
    {
        send(cmd_recall_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_recall_timeout, "recall time limit exceeded"));
        return;
    }

    if (!msgMgr.recallMessage(req.msgid, m_userinfo.userid))
    {
        send(cmd_recall_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_server_internal, "recall failed"));
        return;
    }
    //理由同群聊分支：撤回后清掉该条的 Pin 行，不额外推 Pin 变更。
    Singleton<MessagePinManager>::Instance().removePinForMessage(req.msgid);
    send(cmd_recall_message, m_seq, MessageProtocol::buildSimpleResponse(0, "ok"));

    //向双方推 cmd_message_recalled_notify（cid 取收件方视角的对端，operator 为撤回者）
    int32_t peerId = static_cast<int32_t>(rec.to);
    sendToUser(peerId, cmd_message_recalled_notify,
               MessageProtocol::buildMessageRecalledNotify(CHAT_TYPE_SINGLE, m_userinfo.userid,
                                                           req.msgid, m_userinfo.userid));
    sendToUser(m_userinfo.userid, cmd_message_recalled_notify,
               MessageProtocol::buildMessageRecalledNotify(CHAT_TYPE_SINGLE, peerId,
                                                           req.msgid, m_userinfo.userid));

    LOGI("recall single message ok, userid: %d, peer: %d, msgid: %lld",
         m_userinfo.userid, peerId, (long long)req.msgid);
}

void ChatSession::onPinMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    PinMessageRequest req;
    if (!MessageProtocol::parsePinMessageRequest(body, req))
    {
        send(cmd_pin_message, m_seq,
             MessageProtocol::buildPinMessageResponse(error_code_bad_request, "bad request",
                                                      req.ctype, req.cid, req.msgid, req.pin));
        LOGE("pin message bad request, userid: %d, body: %s", m_userinfo.userid, body.c_str());
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessagePinManager& pinMgr = Singleton<MessagePinManager>::Instance();

    MessageRecord rec;
    if (!msgMgr.getMessageById(req.msgid, rec) || rec.recalled)
    {
        //已撤回的消息与不存在同等对待：钉一条谁都看不见的消息没有意义
        send(cmd_pin_message, m_seq,
             MessageProtocol::buildPinMessageResponse(error_code_message_not_exist,
                                                      "message not exist",
                                                      req.ctype, req.cid, req.msgid, req.pin));
        LOGI("pin message rejected, message not exist or recalled, userid: %d, msgid: %lld",
             m_userinfo.userid, (long long)req.msgid);
        return;
    }

    //共享卡片不能置顶：Pin 列表对会话内所有人可见，置顶会让可见范围之外的人看到它
    if (!MessageProtocol::isClientOperableMsgType(rec.msgtype))
    {
        send(cmd_pin_message, m_seq,
             MessageProtocol::buildPinMessageResponse(error_code_permission_denied,
                                                      "msgtype cannot be pinned",
                                                      req.ctype, req.cid, req.msgid, req.pin));
        LOGI("pin message rejected by server-only msgtype, userid: %d, msgid: %lld, msgtype: %d",
             m_userinfo.userid, (long long)req.msgid, rec.msgtype);
        return;
    }

    //以下一律以<u>消息本身记录的会话归属</u>为准，不听请求里的 ctype / cid ——
    //那两个只用于回带给客户端对号，被伪造也影响不到落库范围与推送范围。
    PinRecord exist;
    const bool alreadyPinned = pinMgr.getPinRecord(req.msgid, exist);

    if (rec.ctype == CHAT_TYPE_GROUP)
    {
        const int64_t groupid = rec.to;
        const int myrole = Singleton<GroupManager>::Instance().getMemberRole(groupid,
                                                                            m_userinfo.userid);
        if (myrole < 0)
        {
            send(cmd_pin_message, m_seq,
                 MessageProtocol::buildPinMessageResponse(error_code_not_group_member,
                                                          "not a group member",
                                                          req.ctype, req.cid, req.msgid, req.pin));
            return;
        }

        //群里"谁都能钉，但别人钉的只有他自己和管理员能取消"：放开钉是因为 Pin 本就是
        //轻量标记，卡死会没人用；收紧取消是因为共享状态被旁人随手取消，原本钉它的人会
        //莫名其妙。群主 / 管理员保留兜底权限。
        if (req.pin == 0 && alreadyPinned)
        {
            const bool isOperator = (exist.operatorId == static_cast<int64_t>(m_userinfo.userid));
            const bool isAdmin    = (myrole >= group_role_admin);
            if (!isOperator && !isAdmin)
            {
                send(cmd_pin_message, m_seq,
                     MessageProtocol::buildPinMessageResponse(error_code_pin_not_allowed,
                                                              "no permission to unpin",
                                                              req.ctype, req.cid, req.msgid, req.pin));
                LOGI("unpin rejected, not operator nor admin, userid: %d, group: %lld, "
                     "msgid: %lld, pin operator: %lld",
                     m_userinfo.userid, (long long)groupid, (long long)req.msgid,
                     (long long)exist.operatorId);
                return;
            }
        }
    }
    else
    {
        //单聊：双方任一都能钉、也都能取消。只有两个人，再按"谁钉的"收紧没有意义。
        if (rec.from != static_cast<int64_t>(m_userinfo.userid)
            && rec.to != static_cast<int64_t>(m_userinfo.userid))
        {
            send(cmd_pin_message, m_seq,
                 MessageProtocol::buildPinMessageResponse(error_code_permission_denied,
                                                          "not a participant",
                                                          req.ctype, req.cid, req.msgid, req.pin));
            LOGI("pin rejected, not a participant, userid: %d, msgid: %lld",
                 m_userinfo.userid, (long long)req.msgid);
            return;
        }
    }

    //幂等短路：状态本来就对，直接回成功，不重复落库、也不重复推送。
    //客户端连点两下不该收到莫名的报错，别人也不该收到两条一样的推送。
    if ((req.pin == 1) == alreadyPinned)
    {
        send(cmd_pin_message, m_seq,
             MessageProtocol::buildPinMessageResponse(0, "ok",
                                                      req.ctype, req.cid, req.msgid, req.pin));
        LOGI("pin message no-op, already in target state, userid: %d, msgid: %lld, pin: %d",
             m_userinfo.userid, (long long)req.msgid, req.pin);
        return;
    }

    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * 1000;
    //推送里要带的 Pin 时间：钉住时取本次时刻，取消时无意义、传 0
    int64_t pinTime = 0;
    bool ok = false;
    if (req.pin == 1)
    {
        pinTime = nowMs;
        ok = pinMgr.pinMessage(req.msgid, m_userinfo.userid, nowMs);
    }
    else
    {
        ok = pinMgr.unpinMessage(req.msgid);
    }

    if (!ok)
    {
        send(cmd_pin_message, m_seq,
             MessageProtocol::buildPinMessageResponse(error_code_server_internal, "pin failed",
                                                      req.ctype, req.cid, req.msgid, req.pin));
        LOGE("pin message db error, userid: %d, msgid: %lld, pin: %d",
             m_userinfo.userid, (long long)req.msgid, req.pin);
        return;
    }

    send(cmd_pin_message, m_seq,
         MessageProtocol::buildPinMessageResponse(0, "ok",
                                                  req.ctype, req.cid, req.msgid, req.pin));

    if (rec.ctype == CHAT_TYPE_GROUP)
    {
        //向全体群成员推 cmd_message_pinned_notify（cid 取群id），操作者本人也会收到
        const int64_t groupid = rec.to;
        notifyGroupMembers(groupid, cmd_message_pinned_notify,
                           MessageProtocol::buildMessagePinnedNotify(CHAT_TYPE_GROUP, groupid,
                                                                     req.msgid, req.pin,
                                                                     m_userinfo.userid, pinTime));
        LOGI("pin group message ok, userid: %d, group: %lld, msgid: %lld, pin: %d",
             m_userinfo.userid, (long long)groupid, (long long)req.msgid, req.pin);
        return;
    }

    //单聊：给双方各推一条，cid 按<u>收件人视角</u>分别构造 —— 给对端推时 cid 取我，
    //给自己推时 cid 取对端。不这么分开，收到推送的一方会对不上是哪个会话。
    const int32_t peerId = (rec.from == static_cast<int64_t>(m_userinfo.userid))
        ? static_cast<int32_t>(rec.to)
        : static_cast<int32_t>(rec.from);
    sendToUser(peerId, cmd_message_pinned_notify,
               MessageProtocol::buildMessagePinnedNotify(CHAT_TYPE_SINGLE, m_userinfo.userid,
                                                         req.msgid, req.pin,
                                                         m_userinfo.userid, pinTime));
    sendToUser(m_userinfo.userid, cmd_message_pinned_notify,
               MessageProtocol::buildMessagePinnedNotify(CHAT_TYPE_SINGLE, peerId,
                                                         req.msgid, req.pin,
                                                         m_userinfo.userid, pinTime));

    LOGI("pin single message ok, userid: %d, peer: %d, msgid: %lld, pin: %d",
         m_userinfo.userid, peerId, (long long)req.msgid, req.pin);
}

void ChatSession::onGetPinnedMessages(const std::string& body,
                                      const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::vector<MessageRecord> messages;
    std::vector<PinRecord> pins;

    GetPinnedMessagesRequest req;
    if (!MessageProtocol::parseGetPinnedMessagesRequest(body, req))
    {
        send(cmd_get_pinned_messages, m_seq,
             MessageProtocol::buildGetPinnedMessagesResponse(error_code_bad_request, "bad request",
                                                             req.ctype, req.cid, 0,
                                                             messages, pins));
        LOGE("get pinned messages bad request, userid: %d, body: %s",
             m_userinfo.userid, body.c_str());
        return;
    }

    //群会话先确认请求者确实在群里 —— 本应答带的是完整消息体，放行等于把别人群里的
    //聊天内容送出去。单聊不需要这一步：getPinnedMessages 的条件里已经含了"消息的
    //收发双方之一必须是我"，非参与方查出来恒为空。同理，本人看不到的入群前历史也不能
    //经 Pin 列表送出去（docs/邀请入群方案.md §5.3）
    GroupHistoryBound bound;
    if (req.ctype == CHAT_TYPE_GROUP)
    {
        if (Singleton<GroupManager>::Instance().getMemberRole(req.cid, m_userinfo.userid) < 0)
        {
            send(cmd_get_pinned_messages, m_seq,
                 MessageProtocol::buildGetPinnedMessagesResponse(error_code_not_group_member,
                                                                 "not a group member",
                                                                 req.ctype, req.cid, 0,
                                                                 messages, pins));
            return;
        }
        bound = groupHistoryBound(req.cid);
    }

    int64_t total = 0;
    if (!Singleton<MessagePinManager>::Instance().getPinnedMessages(
            req.ctype, req.cid, m_userinfo.userid, PIN_FETCH_LIMIT, messages, pins, total,
            bound.m_minCreateTime, bound.m_minMsgId))
    {
        send(cmd_get_pinned_messages, m_seq,
             MessageProtocol::buildGetPinnedMessagesResponse(error_code_server_internal,
                                                             "query failed",
                                                             req.ctype, req.cid, 0,
                                                             messages, pins));
        LOGE("get pinned messages db error, userid: %d, ctype: %d, cid: %lld",
             m_userinfo.userid, req.ctype, (long long)req.cid);
        return;
    }

    //卡片消息合并当前互动状态，理由同 onSyncMessages：客户端用这里拿到的消息体补齐本地库，
    //不带名单与状态的卡片会把本地已有的接龙名单覆盖掉。须在按体积截断之前做
    augmentCardRecords(messages);

    //按体积截断，理由同 onSyncMessages。列表按消息时间倒序，与 PIN_FETCH_LIMIT 的条数截断
    //一样保留开头（较新的）、截掉末尾（较老的）；pins 与 messages 一一对应，同步截短。
    //total 保持真实总数不变，客户端据 total 大于返回条数得知发生了截断。
    //先整页组装、超过预算才截断后重新组装，理由同 onSyncMessages
    send(cmd_get_pinned_messages, m_seq,
         MessageProtocol::buildGetPinnedMessagesResponseWithinBudget(0, "ok", req.ctype, req.cid, total,
                                                                     messages, pins,
                                                                     RESPONSE_MESSAGES_BUDGET_BYTES));

    //截断要单独打一条日志：线上出现"用户说钉了却看不到"时，第一眼就能判断是不是撞上
    //了单次返回上限，而不是去猜推送丢没丢。
    if (total > static_cast<int64_t>(messages.size()))
    {
        LOGI("get pinned messages truncated, userid: %d, ctype: %d, cid: %lld, "
             "returned: %d, total: %lld, limit: %d",
             m_userinfo.userid, req.ctype, (long long)req.cid,
             (int)messages.size(), (long long)total, PIN_FETCH_LIMIT);
    }

    LOGI("get pinned messages ok, userid: %d, ctype: %d, cid: %lld, returned: %d, total: %lld",
         m_userinfo.userid, req.ctype, (long long)req.cid, (int)messages.size(), (long long)total);
}

int64_t ChatSession::deliverConversationMessage(int ctype, int64_t cid, int32_t msgtype,
                                                const std::string& content,
                                                const std::string& clientmsgid,
                                                MessageRecord* outRec)
{
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;

    //转发 / 群发产生的消息不带 @ 与引用
    std::vector<int64_t> noMentions;
    std::string storedContent = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    int64_t msgId = msgMgr.saveMessage(clientmsgid, ctype, m_userinfo.userid,
                                       static_cast<int32_t>(cid), msgtype, storedContent, now);
    if (msgId == 0)
        return 0;

    //群发图片时发送方就是上传者，需要标记；转发别人的图片时不是上传者，不会改动记录
    markMessageFileCompleted(msgtype, content);

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientmsgid;
    rec.ctype       = ctype;
    rec.from        = m_userinfo.userid;
    rec.to          = cid;
    rec.msgtype     = msgtype;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = now;
    rec.recalled    = false;

    if (ctype == CHAT_TYPE_GROUP)
    {
        //群消息：cid 为群id，fan-out 给全体群成员
        notifyGroupMembers(cid, cmd_message_notify,
                           MessageProtocol::buildMessageNotify(rec, cid));
    }
    else
    {
        //单聊：推给对端（cid 取发送方userid）；离线则留库等其 sync
        sendToUser(static_cast<int32_t>(cid), cmd_message_notify,
                   MessageProtocol::buildMessageNotify(rec, m_userinfo.userid));
    }

    if (outRec != NULL)
        *outRec = rec;
    return msgId;
}

bool ChatSession::checkGroupSpeakAllowed(uint16_t cmd, int64_t groupid, const std::string& clientmsgid,
                                         int& myrole, GroupRecord& group)
{
    std::string reason;
    const int code = checkGroupSpeak(cmd, groupid, myrole, group, reason);
    if (code == error_code_ok)
        return true;
    //发送消息的回执要带回 clientmsgid，客户端才知道是哪一条被拒；转发按帧 seq 对应请求，回 { code, msg }
    if (cmd == cmd_send_message)
        replySendRejected(code, reason, clientmsgid);
    else
        send(cmd, m_seq, MessageProtocol::buildSimpleResponse(code, reason));
    return false;
}

void ChatSession::replySendRejected(int code, const std::string& msg, const std::string& clientmsgid)
{
    send(cmd_send_message, m_seq, MessageProtocol::buildSendMessageResponse(code, msg, clientmsgid, 0, 0));
}

bool ChatSession::hitSensitive(int source, int ctype, int64_t cid, const std::string& targetName,
                               const std::vector<std::string>& texts)
{
    const int64_t enterpriseId =
        Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    //拦截记录给管理员看，提交者名称优先记昵称；昵称为空时记登录账号
    const std::string userName = m_userinfo.nickname.empty() ? m_userinfo.username : m_userinfo.nickname;
    return checkSensitiveAndRecord(enterpriseId, m_userinfo.userid, userName, source, ctype, cid,
                                   targetName, texts);
}

int ChatSession::checkGroupSpeak(uint16_t cmd, int64_t groupid, int& myrole, GroupRecord& group,
                                 std::string& reason)
{
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();

    //发送方须为群成员；同时取回其角色与个人禁言状态。出参 mymuted 已由
    //GroupManager 按到期时间折算过 —— 定时禁言到点后这里直接是 0。
    myrole = group_role_member;
    int mymuted = 0;
    int64_t mymuteuntil = 0;
    int mymustread = 0;
    if (!groupMgr.getMemberRoleAndMuted(groupid, m_userinfo.userid, myrole, mymuted, &mymuteuntil, &mymustread))
    {
        reason = "not a group member";
        return error_code_not_group_member;
    }

    //定时禁言刚过期：库里 f_muted 还是 1，趁这条本来就要写库的路径顺手清掉，
    //并把状态变更推给全群，使各端界面上的"禁言中"标记同步消失。只读路径
    //（如拉成员列表）不写库，靠 isMuteExpired 在内存里折算。
    if (mymuted == 0 && mymuteuntil > 0)
    {
        if (groupMgr.clearExpiredMute(groupid, m_userinfo.userid))
        {
            std::vector<int64_t> expired;
            expired.push_back(m_userinfo.userid);
            notifyGroupMembers(groupid, cmd_group_member_changed_notify,
                               GroupProtocol::buildGroupMemberChangedNotify(
                                   groupid, group_member_change_muted, expired, m_userinfo.userid));
        }
    }

    if (!groupMgr.getGroup(groupid, group))
    {
        reason = "group not exist";
        return error_code_group_not_exist;
    }

    //帖子群没有普通聊天（docs/帖子群方案.md 6.2）：发消息、转发到群、群发助手一律拒收；发帖与留言用的是 2700 段命令，不受影响
    if (group.grouptype == group_type_thread
        && (cmd == cmd_send_message || cmd == cmd_forward_message || cmd == cmd_mass_send))
    {
        LOGI("group speak rejected, thread group has no chat, cmd: %u, groupid: %lld, userid: %d",
             static_cast<unsigned>(cmd), (long long)groupid, m_userinfo.userid);
        reason = "thread group has no chat";
        return error_code_thread_not_thread_group;
    }

    //禁言校验：本人被单独禁言、或全员禁言且本人非管理员 → 拒发
    if (chatserver::IsMutedForSpeaking(mymuted, group.muteall, myrole))
    {
        LOGI("group speak rejected by mute, cmd: %u, groupid: %lld, userid: %d, role: %d",
             static_cast<unsigned>(cmd), (long long)groupid, m_userinfo.userid, myrole);
        reason = "you are muted in this group";
        return error_code_member_muted;
    }

    //入群必读还没确认：确认前不能发言（docs/群公告接入方案.md §14）。只约束设定之后入群的人，
    //所以这里只看本人成员行上的标记，不看群有没有设必读
    if (mymustread != 0)
    {
        LOGI("group speak rejected by must-read, cmd: %u, groupid: %lld, userid: %d",
             static_cast<unsigned>(cmd), (long long)groupid, m_userinfo.userid);
        reason = "confirm the must-read announcement first";
        return error_code_must_read_pending;
    }
    return error_code_ok;
}

void ChatSession::onTypingNotify(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    //"正在输入"不可靠、不应答：解析失败或非单聊直接忽略
    TypingNotifyRequest req;
    if (!MessageProtocol::parseTypingNotifyRequest(body, req))
        return;
    if (req.ctype != CHAT_TYPE_SINGLE)
        return;

    //转发给对端；cid 取正在输入一方（本人）的userid，不落库、不应答
    int32_t peerId = static_cast<int32_t>(req.cid);
    sendToUser(peerId, cmd_typing_notify,
               MessageProtocol::buildTypingNotify(req.ctype, m_userinfo.userid, req.state));
}

void ChatSession::onFileTransferAck(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    //与"正在输入"一样不给应答：上报方不关心结果，失败了也没有可补救的动作。
    //但与它不同的是本状态<u>要落库</u> —— 发送方可能不在线，得等其下次拉历史时补看到。
    FileTransferAckRequest req;
    if (!MessageProtocol::parseFileTransferAckRequest(body, req))
    {
        LOGE("bad file transfer ack request, userid: %d", m_userinfo.userid);
        return;
    }
    //当前只有单聊文件消息（群聊的文件走群文件库，不是聊天消息）
    if (req.ctype != CHAT_TYPE_SINGLE)
        return;

    //校验这条消息确实存在、确实是文件消息、且确实是发给上报者本人的 ——
    //否则任何人都能对任意 msgid 上报状态，把别人的气泡改成"已拒绝"。
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessageRecord rec;
    if (!msgMgr.getMessageById(req.msgid, rec))
    {
        LOGE("file transfer ack on missing message, userid: %d, msgid: %lld",
             m_userinfo.userid, (long long)req.msgid);
        return;
    }
    if (rec.msgtype != message_type_file)
        return;
    if (rec.to != m_userinfo.userid)
    {
        LOGE("file transfer ack from non-receiver, userid: %d, msgid: %lld, msg.to: %lld",
             m_userinfo.userid, (long long)req.msgid, (long long)rec.to);
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    if (!msgMgr.saveFileAckState(req.msgid, req.state, now))
    {
        //落库失败仍继续推送：在线的发送方至少能即时看到，只是下次拉历史看不到
        LOGE("save file ack state failed, msgid: %lld, state: %d",
             (long long)req.msgid, req.state);
    }

    //转推发送方；cid 取上报方（接收方）的 userid，即发送方视角的会话ID。
    //发送方不在线时这一步自然无人接收，等它下次 cmd_sync_messages 从库里带出。
    int32_t senderId = static_cast<int32_t>(rec.from);
    sendToUser(senderId, cmd_file_transfer_ack,
               MessageProtocol::buildFileTransferAckNotify(CHAT_TYPE_SINGLE, m_userinfo.userid,
                                                           req.msgid, req.state));

    LOGI("file transfer ack ok, userid: %d, sender: %d, msgid: %lld, state: %d",
         m_userinfo.userid, senderId, (long long)req.msgid, req.state);
}

void ChatSession::onRemoteDesktopInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t peerId = 0;
    if (!RemoteDesktopProtocol::parseInviteRequest(body, peerId))
    {
        send(cmd_rd_invite, m_seq,
             RemoteDesktopProtocol::buildInviteResponse(error_code_bad_request, "bad request", ""));
        return;
    }

    //不能远程控制自己
    if (peerId == m_userinfo.userid)
    {
        send(cmd_rd_invite, m_seq,
             RemoteDesktopProtocol::buildInviteResponse(error_code_bad_request, "cannot control self", ""));
        return;
    }

    //校验被控端是否在线：其名下无任何在线会话即视为离线
    std::list<std::shared_ptr<ChatSession>> peerSessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(peerSessions, static_cast<int32_t>(peerId));
    if (peerSessions.empty())
    {
        send(cmd_rd_invite, m_seq,
             RemoteDesktopProtocol::buildInviteResponse(error_code_not_found, "peer offline", ""));
        LOGI("remote desktop invite failed, peer offline, from: %d, to: %lld",
             m_userinfo.userid, (long long)peerId);
        return;
    }

    //建会话：控制端=本人，被控端=peer；回控制端应答（带 sessionid），推送邀请给被控端
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::string sessionId = Singleton<RemoteDesktopManager>::Instance().createSession(
        m_userinfo.userid, peerId, now);

    send(cmd_rd_invite, m_seq,
         RemoteDesktopProtocol::buildInviteResponse(error_code_ok, "ok", sessionId));

    sendToUser(static_cast<int32_t>(peerId), cmd_rd_invite_notify,
               RemoteDesktopProtocol::buildInviteNotify(sessionId, m_userinfo.userid, m_userinfo.nickname));

    LOGI("remote desktop invite, from: %d, to: %lld, sessionid: %s",
         m_userinfo.userid, (long long)peerId, sessionId.c_str());
}

namespace {

//以下两个函数定义在本文件末尾的 P2P 信令一节，这里先声明，供远程桌面下发 1716 前建 P2P 会话时使用
int64_t p2pNowMs();
int64_t lookupUserEnterpriseForP2P(int64_t userId);

//远程桌面建 P2P 会话时的用途（2302 的 purpose，客户端 FlamingoMedia.dll 以此登记业务方，P2P 方案第 6 期）
const char* const kP2PPurposeRd = "rd";

//一对一通话建 P2P 会话时的用途（同上，响铃阶段就建，接通时链路多半已经打通）
const char* const kP2PPurposeCall = "call";

} // namespace

void ChatSession::onRemoteDesktopResponse(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    int accept = 0;
    if (!RemoteDesktopProtocol::parseResponseRequest(body, sessionId, accept))
        return;

    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    RemoteDesktopSessionInfo s;
    if (!mgr.getSession(sessionId, s))
    {
        //会话不存在（可能已被取消 / 超时清除）：忽略，不回错（应答不可靠）
        LOGI("remote desktop response on unknown session: %s, userid: %d",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //只有被邀请的被控端本人可应答
    if (m_userinfo.userid != s.controlledId)
    {
        LOGI("remote desktop response from non-controlled user, session: %s, userid: %d",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //拒绝：通知控制端、清会话
    if (accept == 0)
    {
        sendToUser(static_cast<int32_t>(s.controllerId), cmd_rd_state_notify,
                   RemoteDesktopProtocol::buildStateNotify(sessionId, rd_state_rejected, "rejected"));
        mgr.removeSession(sessionId);
        LOGI("remote desktop rejected, session: %s, by: %d", sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //接受：记为已接通（控制状态从未授权开始，控制端只能查看，见 onRemoteDesktopControl），
    //再给双方各签一枚 relay 令牌（同会话、相反角色），分别下发中转参数
    mgr.markConnected(sessionId);
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::string tokenController;
    std::string tokenControlled;
    mgr.signTokenFor(sessionId, RelayToken::role_controller, now, tokenController);
    mgr.signTokenFor(sessionId, RelayToken::role_controlled, now, tokenControlled);

    const std::string& relayIp = mgr.getRelayHost();
    int relayPort = mgr.getRelayPort();

    //P2P 方案第 6 期：先尝试为这次远程桌面在两端之间建 P2P 会话（控制端为发起方，会话号直接用作功能会话号）。
    //两端都报到了用途 rd、所在企业都开着 P2P 才会建成；建成时 1716 带上 p2psessionid，两端优先走 P2P 链路，
    //建链失败再改走 imgserver 中转；建不成时照旧只走中转
    std::string p2pSessionId;
    std::vector<P2POutFrame> p2pFrames;
    {
        P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
        P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
        const int p2pCode = service.createForFeature(kP2PPurposeRd, sessionId, s.controllerId, s.controlledId,
                                                     p2pNowMs(), p2pSessionId, p2pFrames);
        LOGI("remote desktop p2p session: session: %s, code: %d, p2p session: %s.", sessionId.c_str(), p2pCode,
             p2pSessionId.c_str());
    }

    sendToUser(static_cast<int32_t>(s.controllerId), cmd_rd_relay_info,
               RemoteDesktopProtocol::buildRelayInfo(sessionId, RelayToken::role_controller,
                                                     relayIp, relayPort, tokenController, p2pSessionId));
    sendToUser(static_cast<int32_t>(s.controlledId), cmd_rd_relay_info,
               RemoteDesktopProtocol::buildRelayInfo(sessionId, RelayToken::role_controlled,
                                                     relayIp, relayPort, tokenControlled, p2pSessionId));
    //2302 排在 1716 之后：客户端先知道这次远程桌面带着哪个 P2P 会话，再收到会话本身
    sendP2PFrames(p2pFrames);

    LOGI("remote desktop accepted, session: %s, controller: %lld, controlled: %lld",
         sessionId.c_str(), (long long)s.controllerId, (long long)s.controlledId);
}

void ChatSession::onRemoteDesktopCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    if (!RemoteDesktopProtocol::parseSessionRequest(body, sessionId))
        return;

    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    RemoteDesktopSessionInfo s;
    if (!mgr.getSession(sessionId, s))
        return;

    //只有发起方控制端可取消
    if (m_userinfo.userid != s.controllerId)
        return;

    //通知被控端已取消、清会话
    sendToUser(static_cast<int32_t>(s.controlledId), cmd_rd_state_notify,
               RemoteDesktopProtocol::buildStateNotify(sessionId, rd_state_canceled, "canceled"));
    mgr.removeSession(sessionId);
    LOGI("remote desktop canceled, session: %s, by: %d", sessionId.c_str(), m_userinfo.userid);
}

void ChatSession::onRemoteDesktopClose(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    if (!RemoteDesktopProtocol::parseSessionRequest(body, sessionId))
        return;

    RemoteDesktopManager& mgr = Singleton<RemoteDesktopManager>::Instance();
    RemoteDesktopSessionInfo s;
    if (!mgr.getSession(sessionId, s))
        return;

    //只有会话参与方（控制端或被控端）可结束
    if (m_userinfo.userid != s.controllerId && m_userinfo.userid != s.controlledId)
        return;

    //通知对端会话已结束、清会话
    int64_t peerId = (m_userinfo.userid == s.controllerId) ? s.controlledId : s.controllerId;
    sendToUser(static_cast<int32_t>(peerId), cmd_rd_state_notify,
               RemoteDesktopProtocol::buildStateNotify(sessionId, rd_state_disconnected, "disconnected"));
    mgr.removeSession(sessionId);
    LOGI("remote desktop closed, session: %s, by: %d", sessionId.c_str(), m_userinfo.userid);
}

void ChatSession::onRemoteDesktopControl(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    int action = 0;
    if (!RemoteDesktopProtocol::parseControlRequest(body, sessionId, action))
    {
        send(cmd_rd_control, m_seq,
             RemoteDesktopProtocol::buildControlResponse(error_code_bad_request, "bad request", sessionId, action,
                                                         rd_control_none));
        LOGI("remote desktop control: bad request, userid: %d", m_userinfo.userid);
        return;
    }

    //校验角色与状态、更新会话表里的控制状态，规则见 RemoteDesktopManager::applyControl
    RemoteDesktopControlResult result;
    const int code = Singleton<RemoteDesktopManager>::Instance().applyControl(sessionId, m_userinfo.userid, action,
                                                                             result);
    send(cmd_rd_control, m_seq,
         RemoteDesktopProtocol::buildControlResponse(code, code == error_code_ok ? "ok" : "rejected", sessionId,
                                                     action, result.m_control));
    if (code != error_code_ok)
    {
        LOGI("remote desktop control rejected, session: %s, userid: %d, action: %d, control: %d, code: %d",
             sessionId.c_str(), m_userinfo.userid, action, result.m_control, code);
        return;
    }

    //状态有变化时转给会话的另一方：申请、撤回、放弃给被控端，授予、拒绝、收回给控制端
    if (result.m_notifyUser > 0)
    {
        sendToUser(static_cast<int32_t>(result.m_notifyUser), cmd_rd_control_notify,
                   RemoteDesktopProtocol::buildControlNotify(sessionId, action, result.m_control, m_userinfo.userid,
                                                             m_userinfo.nickname));
    }
    LOGI("remote desktop control, session: %s, userid: %d, action: %d, control: %d, notify: %lld", sessionId.c_str(),
         m_userinfo.userid, action, result.m_control, (long long)result.m_notifyUser);
}

void ChatSession::onCallInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t peerId = 0;
    int media = call_media_audio;
    if (!CallProtocol::parseInviteRequest(body, peerId, media))
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_bad_request, "bad request", ""));
        return;
    }

    //不能呼叫自己
    if (peerId == m_userinfo.userid)
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_call_fail, "cannot call self", ""));
        return;
    }

    //校验被叫是否在线：其名下无任何在线会话即视为离线
    std::list<std::shared_ptr<ChatSession>> peerSessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(peerSessions, static_cast<int32_t>(peerId));
    if (peerSessions.empty())
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_peer_offline, "peer offline", ""));
        LOGI("call invite failed, peer offline, from: %d, to: %lld",
             m_userinfo.userid, (long long)peerId);
        return;
    }

    //与屏幕共享互斥：本人在共享房间里不能发起通话；被叫在共享房间里按「对方忙」回复
    if (ScreenShareService::isUserInShare(m_userinfo.userid))
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_screenshare_in_call, "in screen share", ""));
        LOGI("call invite failed, caller in screen share, from: %d, to: %lld",
             m_userinfo.userid, (long long)peerId);
        return;
    }
    if (ScreenShareService::isUserInShare(peerId))
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_peer_busy, "peer busy", ""));
        LOGI("call invite failed, peer in screen share, from: %d, to: %lld",
             m_userinfo.userid, (long long)peerId);
        return;
    }

    //与群通话互斥：本人在群通话里（含振铃中）不能发起一对一通话；被叫在群通话里按「对方忙」回复
    if (GroupCallService::isUserInGroupCall(m_userinfo.userid))
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_groupcall_busy, "in group call", ""));
        LOGI("call invite failed, caller in group call, from: %d, to: %lld",
             m_userinfo.userid, (long long)peerId);
        return;
    }
    if (GroupCallService::isUserInGroupCall(peerId))
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_peer_busy, "peer busy", ""));
        LOGI("call invite failed, peer in group call, from: %d, to: %lld",
             m_userinfo.userid, (long long)peerId);
        return;
    }

    CallManager& mgr = Singleton<CallManager>::Instance();

    //对方忙：被叫已在另一通话中
    if (mgr.isUserBusy(peerId))
    {
        send(cmd_call_invite, m_seq,
             CallProtocol::buildInviteResponse(error_code_peer_busy, "peer busy", ""));
        LOGI("call invite failed, peer busy, from: %d, to: %lld",
             m_userinfo.userid, (long long)peerId);
        return;
    }

    //建会话：主叫=本人，被叫=peer；回主叫应答（带 sessionid），推送邀请给被叫
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::string sessionId = mgr.createSession(m_userinfo.userid, peerId, media, now);

    //P2P 方案第 6 期：响铃阶段就为这通电话在两端之间建 P2P 会话（主叫为发起方，通话会话号直接用作功能会话号），
    //接通时链路多半已经打通。两端都报到了用途 call、所在企业都开着 P2P 才会建成；建成时邀请应答与来电推送都带
    //p2psessionid。通话没有接通（拒绝、取消、超时）时由客户端关闭链路，服务端收到 2305 即结束这个 P2P 会话；
    //客户端掉线时按原有的断开处理结束
    std::string p2pSessionId;
    std::vector<P2POutFrame> p2pFrames;
    {
        P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
        P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
        const int p2pCode = service.createForFeature(kP2PPurposeCall, sessionId, m_userinfo.userid, peerId,
                                                     p2pNowMs(), p2pSessionId, p2pFrames);
        LOGI("call p2p session: session: %s, code: %d, p2p session: %s.", sessionId.c_str(), p2pCode,
             p2pSessionId.c_str());
    }

    send(cmd_call_invite, m_seq,
         CallProtocol::buildInviteResponse(error_code_ok, "ok", sessionId, p2pSessionId));

    //邀请带上剩余振铃毫秒数（第 15 期），被叫的来电弹窗据此显示倒计时
    sendToUser(static_cast<int32_t>(peerId), cmd_call_invite_notify,
               CallProtocol::buildInviteNotify(sessionId, m_userinfo.userid, m_userinfo.nickname, media,
                                               mgr.ringRemainingMs(sessionId, now), p2pSessionId));
    //2302 排在应答与来电推送之后：客户端先知道这通电话带着哪个 P2P 会话，再收到会话本身
    sendP2PFrames(p2pFrames);

    //往两人的单聊写一张通话卡片（排在来电推送之后，不拖慢振铃），结束时改写成结果与时长
    CallCardWriter::writeStartCard(sessionId, m_userinfo.userid, m_userinfo.nickname, peerId, media, now);

    LOGI("call invite, from: %d, to: %lld, media: %d, sessionid: %s",
         m_userinfo.userid, (long long)peerId, media, sessionId.c_str());
}

void ChatSession::onCallResponse(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    int accept = 0;
    if (!CallProtocol::parseResponseRequest(body, sessionId, accept))
        return;

    CallManager& mgr = Singleton<CallManager>::Instance();
    CallSessionInfo s;
    if (!mgr.getSession(sessionId, s))
    {
        //会话不存在（可能已被取消 / 超时清除）：忽略，不回错
        LOGI("call response on unknown session: %s, userid: %d",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //只有被邀请的被叫本人可应答
    if (m_userinfo.userid != s.calleeId)
    {
        LOGI("call response from non-callee user, session: %s, userid: %d",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //拒绝：通知主叫、改写通话卡片、清会话
    if (accept == 0)
    {
        sendToUser(static_cast<int32_t>(s.callerId), cmd_call_state_notify,
                   CallProtocol::buildStateNotify(sessionId, call_state_rejected, "rejected"));
        CallCardWriter::finishCard(s, call_state_rejected, static_cast<int64_t>(time(NULL)) * 1000);
        mgr.removeSession(sessionId);
        LOGI("call rejected, session: %s, by: %d", sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //接听：会话置为已接通（接通后不再受响铃超时清理影响），记下接通时刻（通话卡片据此算时长）
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    mgr.markConnected(sessionId, now);

    //给双方各签一枚 relay 令牌（同会话、相反角色）
    std::string tokenCaller;
    std::string tokenCallee;
    mgr.signTokenFor(sessionId, RelayToken::role_caller, now, tokenCaller);
    mgr.signTokenFor(sessionId, RelayToken::role_callee, now, tokenCallee);

    const std::string& relayIp = mgr.getRelayHost();
    int relayPort = mgr.getRelayPort();

    //下发中转参数（cmd_call_signaling）：主叫 / 被叫各一份，据此连 imgserver 中转通道
    sendToUser(static_cast<int32_t>(s.callerId), cmd_call_signaling,
               CallProtocol::buildSignaling(sessionId, RelayToken::role_caller,
                                            relayIp, relayPort, tokenCaller));
    sendToUser(static_cast<int32_t>(s.calleeId), cmd_call_signaling,
               CallProtocol::buildSignaling(sessionId, RelayToken::role_callee,
                                            relayIp, relayPort, tokenCallee));

    //推接通状态（cmd_call_state_notify）给双方，驱动 UI 进入通话中
    sendToUser(static_cast<int32_t>(s.callerId), cmd_call_state_notify,
               CallProtocol::buildStateNotify(sessionId, call_state_connected, "connected"));
    sendToUser(static_cast<int32_t>(s.calleeId), cmd_call_state_notify,
               CallProtocol::buildStateNotify(sessionId, call_state_connected, "connected"));

    LOGI("call accepted, session: %s, caller: %lld, callee: %lld",
         sessionId.c_str(), (long long)s.callerId, (long long)s.calleeId);
}

void ChatSession::onCallCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    if (!CallProtocol::parseSessionRequest(body, sessionId))
        return;

    CallManager& mgr = Singleton<CallManager>::Instance();
    CallSessionInfo s;
    if (!mgr.getSession(sessionId, s))
        return;

    //只有发起方主叫可取消
    if (m_userinfo.userid != s.callerId)
        return;

    //通知被叫已取消、改写通话卡片、清会话
    sendToUser(static_cast<int32_t>(s.calleeId), cmd_call_state_notify,
               CallProtocol::buildStateNotify(sessionId, call_state_canceled, "canceled"));
    CallCardWriter::finishCard(s, call_state_canceled, static_cast<int64_t>(time(NULL)) * 1000);
    mgr.removeSession(sessionId);
    LOGI("call canceled, session: %s, by: %d", sessionId.c_str(), m_userinfo.userid);
}

void ChatSession::onCallHangup(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    if (!CallProtocol::parseSessionRequest(body, sessionId))
        return;

    CallManager& mgr = Singleton<CallManager>::Instance();
    CallSessionInfo s;
    if (!mgr.getSession(sessionId, s))
        return;

    //只有会话参与方（主叫或被叫）可挂断
    if (m_userinfo.userid != s.callerId && m_userinfo.userid != s.calleeId)
        return;

    //通知对端通话已结束、改写通话卡片（带时长）、清会话
    int64_t peerId = (m_userinfo.userid == s.callerId) ? s.calleeId : s.callerId;
    sendToUser(static_cast<int32_t>(peerId), cmd_call_state_notify,
               CallProtocol::buildStateNotify(sessionId, call_state_ended, "ended"));
    CallCardWriter::finishCard(s, call_state_ended, static_cast<int64_t>(time(NULL)) * 1000);
    mgr.removeSession(sessionId);
    LOGI("call hangup, session: %s, by: %d", sessionId.c_str(), m_userinfo.userid);
}

void ChatSession::onCallSwitchMedia(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    int media = 0;
    if (!CallProtocol::parseSwitchMediaRequest(body, sessionId, media))
    {
        send(cmd_call_switch_media, m_seq,
             CallProtocol::buildInviteResponse(error_code_bad_request, "bad request", ""));
        return;
    }

    //会话存在性、参与方身份、接通态三重校验集中在 CallManager 里，口径唯一
    CallManager& mgr = Singleton<CallManager>::Instance();
    bool changed = false;
    int code = mgr.switchMedia(sessionId, m_userinfo.userid, media, changed);
    if (code != 0)
    {
        send(cmd_call_switch_media, m_seq,
             CallProtocol::buildInviteResponse(code, "switch media failed", sessionId));
        LOGI("call switch media rejected, session: %s, by: %d, media: %d, code: %d",
             sessionId.c_str(), m_userinfo.userid, media, code);
        return;
    }

    send(cmd_call_switch_media, m_seq,
         CallProtocol::buildInviteResponse(0, "ok", sessionId));

    //幂等命中（目标类型与当前相同）时不推送，避免无意义的双向广播
    if (!changed)
    {
        LOGI("call switch media no-op (same media), session: %s, media: %d",
             sessionId.c_str(), media);
        return;
    }

    //向双方各推一份，含发起者本人 —— 客户端统一在收到推送时才改界面与引擎，
    //这样本端、对端看到的媒体类型必然一致
    CallSessionInfo s;
    if (!mgr.getSession(sessionId, s))
        return;

    std::string notify = CallProtocol::buildMediaChangedNotify(sessionId, media, m_userinfo.userid);
    sendToUser(static_cast<int32_t>(s.callerId), cmd_call_media_changed_notify, notify);
    sendToUser(static_cast<int32_t>(s.calleeId), cmd_call_media_changed_notify, notify);

    LOGI("call switch media ok, session: %s, by: %d, media: %d",
         sessionId.c_str(), m_userinfo.userid, media);
}

namespace {

//以下两个函数定义在本文件末尾的 P2P 信令一节，这里先声明，供文件夹传输与单文件直传建 P2P 会话时使用
int64_t p2pNowMs();
int64_t lookupUserEnterpriseForP2P(int64_t userId);

//单聊文件夹传输建 P2P 会话时的用途（2302 的 purpose，客户端据此找到文件夹传输的业务方）
const char* const kP2PPurposeFolder = "folder";

//单聊单文件直传建 P2P 会话时的用途（2026-10-04 起，客户端以此登记直传的业务方；旧版客户端不报到它）
const char* const kP2PPurposeDirectFile = "file";

//文件共享传输建 P2P 会话时的用途（P2P 方案第 6 期，客户端 FlamingoFileShare.dll 以此登记业务方）
const char* const kP2PPurposeFileShare = "fileshare";

} // namespace

void ChatSession::onFolderInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    FolderInviteRequest req;
    if (!FolderTransferProtocol::parseInviteRequest(body, req))
    {
        send(cmd_folder_invite, m_seq,
             FolderTransferProtocol::buildInviteResponse(error_code_bad_request, "bad request",
                                                         "", folder_state_ringing));
        LOGE("folder invite: bad request, userid: %d, body: %s", m_userinfo.userid, body.c_str());
        return;
    }

    //不能把文件夹发给自己：两端会被中转层配成同一条连接的一对，语义上也没有意义
    if (req.peerid == m_userinfo.userid)
    {
        send(cmd_folder_invite, m_seq,
             FolderTransferProtocol::buildInviteResponse(error_code_bad_request, "cannot send folder to self",
                                                         "", folder_state_ringing));
        LOGI("folder invite rejected, sending folder to self, userid: %d.", m_userinfo.userid);
        return;
    }

    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    //单文件直传（2026-10-04 起）：数据只走 P2P 链路，不占文件夹传输的名额，按 directfilemaxperuser 另行限额
    const bool directFile = (req.kind == folder_kind_file);

    //规模上限、双方各自的并发笔数、服务器活跃会话总数三项校验集中在 checkInvite 里，
    //本方法按它给出的错误码回话即可，不必自己逐项判，避免与单元测试里的口径分叉
    int code = directFile ? mgr.checkDirectFileInvite(m_userinfo.userid, req.peerid)
                          : mgr.checkInvite(m_userinfo.userid, req.peerid, req.totalfiles, req.totalsize);
    if (code != error_code_ok)
    {
        //规模超限的那两种失败要回带服务端配置的上限值：上限是配置项、随部署环境变化，
        //客户端一律用应答里带回的数值做提示，不得硬编码
        if (code == error_code_folder_too_many_files || code == error_code_folder_too_large)
        {
            send(cmd_folder_invite, m_seq,
                 FolderTransferProtocol::buildInviteLimitResponse(code, "folder scale exceeds limit",
                                                                   mgr.getMaxFiles(), mgr.getMaxSize()));
        }
        else
        {
            send(cmd_folder_invite, m_seq,
                 FolderTransferProtocol::buildInviteResponse(code, "folder invite rejected",
                                                             "", folder_state_ringing));
        }

        LOGI("folder invite rejected, from: %d, to: %lld, files: %lld, size: %lld, code: %d.",
             m_userinfo.userid, (long long)req.peerid,
             (long long)req.totalfiles, (long long)req.totalsize, code);
        return;
    }

    //校验接收端是否在线：其名下无任何在线会话即视为离线。判定口径与远程桌面
    //onRemoteDesktopInvite、音视频 onCallInvite 完全一致
    std::list<std::shared_ptr<ChatSession>> peerSessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(peerSessions, static_cast<int32_t>(req.peerid));
    if (peerSessions.empty())
    {
        send(cmd_folder_invite, m_seq,
             FolderTransferProtocol::buildInviteResponse(error_code_folder_peer_offline, "peer offline",
                                                         "", folder_state_ringing));
        LOGI("folder invite failed, peer offline, from: %d, to: %lld.",
             m_userinfo.userid, (long long)req.peerid);
        return;
    }

    //单文件直传先确认此刻能为双方建用途 file 的 P2P 会话（双方都报到了 file、所在企业开着 P2P、会话数没到上限），
    //建不成回 622、不建会话：发送端随即改为上传服务器发离线文件，接收端看不到这次邀请
    if (directFile)
    {
        P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
        P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
        const int p2pCode = service.checkForFeature(kP2PPurposeDirectFile, m_userinfo.userid, req.peerid);
        if (p2pCode != error_code_ok)
        {
            send(cmd_folder_invite, m_seq,
                 FolderTransferProtocol::buildInviteResponse(error_code_folder_direct_unavailable,
                                                             "direct file transfer unavailable",
                                                             "", folder_state_ringing));
            LOGI("direct file invite rejected, p2p unavailable, from: %d, to: %lld, p2p code: %d.",
                 m_userinfo.userid, (long long)req.peerid, p2pCode);
            return;
        }
    }

    //企业级敏感词：文件夹名会显示在对方的邀请提示与传输记录条里，要检查。放在各项校验之后、
    //建会话之前，被拦下时不建会话，对方收不到邀请
    //单文件直传时 foldername 是文件名，按文件名的来源检查（与申请上传 1801 同一来源）
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(req.foldername);
    if (hitSensitive(directFile ? chatserver::sensitive_source_file_name : chatserver::sensitive_source_folder_name,
                     CHAT_TYPE_SINGLE, req.peerid, std::string(), sensitiveTexts))
    {
        send(cmd_folder_invite, m_seq,
             FolderTransferProtocol::buildInviteResponse(error_code_file_sensitive_word,
                                                         directFile ? "file name contains sensitive word"
                                                                    : "folder name contains sensitive word",
                                                         "", folder_state_ringing));
        return;
    }

    //建会话：发送端=本人，接收端=peer，初始状态为响铃中
    FolderTransferScale scale;
    scale.folderName = req.foldername;
    scale.totalDirs  = req.totaldirs;
    scale.totalFiles = req.totalfiles;
    scale.totalSize  = req.totalsize;
    scale.kind       = req.kind;

    int64_t now = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;
    std::string sessionId = mgr.createSession(m_userinfo.userid, req.peerid, scale, now);

    //把会话 id 记在本连接上：本连接断开时据此判定"这笔传输的一端已离线"并通知对端
    m_folderTransferSessionId = sessionId;

    send(cmd_folder_invite, m_seq,
         FolderTransferProtocol::buildInviteResponse(error_code_ok, "ok", sessionId, folder_state_ringing));

    sendToUser(static_cast<int32_t>(req.peerid), cmd_folder_invite_notify,
               FolderTransferProtocol::buildInviteNotify(sessionId, m_userinfo.userid,
                                                          m_userinfo.nickname, req));

    LOGI("folder invite, from: %d, to: %lld, sessionid: %s, kind: %d, dirs: %lld, files: %lld, size: %lld, "
         "invite timeout: %lld ms.",
         m_userinfo.userid, (long long)req.peerid, sessionId.c_str(), req.kind,
         (long long)req.totaldirs, (long long)req.totalfiles, (long long)req.totalsize,
         (long long)(directFile ? mgr.getDirectFileInviteTimeoutMs() : mgr.getInviteTimeoutMs()));
}

void ChatSession::onFolderResponse(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    int accept = folder_response_reject;
    if (!FolderTransferProtocol::parseResponseRequest(body, sessionId, accept))
    {
        send(cmd_folder_response, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_bad_request, "bad request", ""));
        LOGE("folder response: bad request, userid: %d, body: %s", m_userinfo.userid, body.c_str());
        return;
    }

    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    FolderTransferSessionInfo s;
    if (!mgr.getSession(sessionId, s))
    {
        //会话不存在：发送端已撤回、邀请已超时被清理，或对端掉线时会话已被销毁
        send(cmd_folder_response, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_not_found, "folder session not found",
                                                          sessionId));
        LOGI("folder response on unknown session: %s, userid: %d.", sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //只有本次会话的接收端本人可以应答，别人拿着 sessionid 来应答一律拒绝
    if (m_userinfo.userid != s.receiverId)
    {
        send(cmd_folder_response, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_permission_denied,
                                                          "not the receiver of this folder session", sessionId));
        LOGW("folder response from non-receiver, session: %s, userid: %d, receiver: %lld.",
             sessionId.c_str(), m_userinfo.userid, (long long)s.receiverId);
        return;
    }

    //拒绝：向发送端推"已拒绝"，随后立即销毁会话。拒绝不进重试保留期，发送端只能重新发起
    if (accept == folder_response_reject)
    {
        send(cmd_folder_response, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_ok, "ok", sessionId));

        //终态收口：向发送端推"已拒绝"，并在双方会话里留下这条传输记录。
        //接收端不在推送名单里 —— 拒绝是它自己的动作，结果由上面那条应答告诉它；
        //它同样会经记录条的 cmd_message_notify 看到聊天记录里多出的那一条。
        //按协议约定只有"已失败"态才带有意义的失败原因，故这里传入的
        //error_code_folder_rejected 会被归一成 0；照常传是为了让调用点一眼看出这次
        //结束的原因，那条不变式集中由 buildStateNotify / buildRecordContent 保证
        std::vector<int64_t> notifySender;
        notifySender.push_back(s.senderId);
        finishFolderTransferSession(s, folder_state_rejected, error_code_folder_rejected,
                                    "rejected by receiver", notifySender);

        mgr.removeSession(sessionId);
        m_folderTransferSessionId.clear();

        LOGI("folder invite rejected by receiver, session: %s, receiver: %d, sender: %lld.",
             sessionId.c_str(), m_userinfo.userid, (long long)s.senderId);
        return;
    }

    //同意：会话转入传输中，并把会话 id 记在接收端这条连接上，供其掉线时通知发送端
    int64_t now = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;
    mgr.setState(sessionId, folder_state_transferring, now);
    m_folderTransferSessionId = sessionId;

    //先下发中转参数再回应答：签发失败时还来得及把失败码回给接收端，不至于让它以为
    //已经同意成功、却永远等不到 cmd_folder_relay_info
    const int relayCode = pushFolderRelayInfo(s, now);
    if (relayCode == error_code_folder_direct_unavailable)
    {
        send(cmd_folder_response, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_folder_direct_unavailable,
                                                          "direct file transfer unavailable", sessionId));

        //单文件直传建不起 P2P 会话：向双方推「已失败（622）」，发送端随即改为上传服务器，会话不进重试保留期
        std::vector<int64_t> notifyBoth;
        notifyBoth.push_back(s.senderId);
        notifyBoth.push_back(s.receiverId);
        finishFolderTransferSession(s, folder_state_failed, error_code_folder_direct_unavailable,
                                    "p2p session unavailable", notifyBoth);

        mgr.removeSession(sessionId);
        m_folderTransferSessionId.clear();
        LOGI("direct file accepted but p2p session unavailable, session: %s, receiver: %d.",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }
    if (relayCode != error_code_ok)
    {
        send(cmd_folder_response, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_server_internal,
                                                          "sign relay token failed", sessionId));

        //签不出令牌就建不起中转通道，这笔传输到此为止，也是一个终态：向发送端推"已失败"
        //并留记录。失败原因取"中转通道断开"—— 这组错误码里没有更贴切的一项，而从两端
        //用户的角度看，结果确实是通道没能建起来
        std::vector<int64_t> notifySender;
        notifySender.push_back(s.senderId);
        finishFolderTransferSession(s, folder_state_failed, error_code_folder_relay_broken,
                                    "sign relay token failed", notifySender);

        mgr.removeSession(sessionId);
        m_folderTransferSessionId.clear();
        LOGE("folder response accepted but relay token sign failed, session: %s, receiver: %d.",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    send(cmd_folder_response, m_seq,
         FolderTransferProtocol::buildSessionResponse(error_code_ok, "ok", sessionId));

    //按协议不再另推一帧"传输中"状态：cmd_folder_relay_info 送达本身已表明会话被接受，
    //两帧含义重复只会让客户端状态机多一条分支（见 docs/协议设计-v2.md 12.2.3）
    LOGI("folder invite accepted, session: %s, sender: %lld, receiver: %lld.",
         sessionId.c_str(), (long long)s.senderId, (long long)s.receiverId);
}

void ChatSession::onFolderCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    //tooffline（2026-10-04 起）：单文件直传的发送端改为上传服务器发离线文件，缺省为 0
    int tooffline = 0;
    if (!FolderTransferProtocol::parseCancelRequest(body, sessionId, tooffline))
    {
        send(cmd_folder_cancel, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_bad_request, "bad request", ""));
        LOGE("folder cancel: bad request, userid: %d, body: %s", m_userinfo.userid, body.c_str());
        return;
    }

    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    FolderTransferSessionInfo s;
    if (!mgr.getSession(sessionId, s))
    {
        send(cmd_folder_cancel, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_not_found, "folder session not found",
                                                          sessionId));
        LOGI("folder cancel on unknown session: %s, userid: %d.", sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //只有本次会话的发送端可以撤回自己发出的邀请
    if (m_userinfo.userid != s.senderId)
    {
        send(cmd_folder_cancel, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_permission_denied,
                                                          "not the sender of this folder session", sessionId));
        LOGW("folder cancel from non-sender, session: %s, userid: %d, sender: %lld.",
             sessionId.c_str(), m_userinfo.userid, (long long)s.senderId);
        return;
    }

    //只有还在响铃时才谈得上"撤回邀请"。传输已经开始之后再取消要走 cmd_folder_close 并把
    //errcode 填成 error_code_folder_canceled —— 那时已建了通道、可能已落盘一部分，
    //与"还没开始就撤回"是两种收尾（见 docs/协议设计-v2.md 12.2.4）
    if (s.state != folder_state_ringing)
    {
        send(cmd_folder_cancel, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_bad_request,
                                                          "folder session is not ringing", sessionId));
        LOGW("folder cancel rejected, session %s is in state %d, not ringing.",
             sessionId.c_str(), s.state);
        return;
    }

    //「改为离线发送」只属于单文件直传，文件夹传输没有离线的去处
    if (tooffline != 0 && s.kind != folder_kind_file)
    {
        send(cmd_folder_cancel, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_bad_request,
                                                          "tooffline is only for direct file transfer", sessionId));
        LOGW("folder cancel rejected, tooffline on a folder session %s, userid: %d.",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    send(cmd_folder_cancel, m_seq,
         FolderTransferProtocol::buildSessionResponse(error_code_ok, "ok", sessionId));

    //终态收口：通知接收端邀请已被撤回（或已改为离线发送），让它那侧的气泡收起；文件夹传输同时在双方会话里
    //留下记录，单文件直传这两种结局都不留（发送端随后那条离线文件消息代替它）。
    //发送端不在推送名单里 —— 撤回是它自己的动作，结果由上面那条应答告诉它
    std::vector<int64_t> notifyReceiver;
    notifyReceiver.push_back(s.receiverId);
    finishFolderTransferSession(s, tooffline != 0 ? folder_state_to_offline : folder_state_canceled,
                                error_code_ok, "", notifyReceiver);

    mgr.removeSession(sessionId);
    m_folderTransferSessionId.clear();

    LOGI("folder invite canceled by sender, session: %s, sender: %d, receiver: %lld.",
         sessionId.c_str(), m_userinfo.userid, (long long)s.receiverId);
}

void ChatSession::onFolderClose(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    FolderCloseRequest req;
    if (!FolderTransferProtocol::parseCloseRequest(body, req))
    {
        send(cmd_folder_close, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_bad_request, "bad request", ""));
        LOGE("folder close: bad request, userid: %d, body: %s", m_userinfo.userid, body.c_str());
        return;
    }

    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    FolderTransferSessionInfo s;
    if (!mgr.getSession(req.sessionid, s))
    {
        //正常收尾时双方会各发一帧 cmd_folder_close，服务端以先到的那一帧为准并销毁会话；
        //后到的这一帧按幂等处理、照常回成功，不视为错误（见 docs/协议设计-v2.md 12.2.5）
        send(cmd_folder_close, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_ok, "ok", req.sessionid));
        m_folderTransferSessionId.clear();
        LOGI("folder close on unknown session (treated as idempotent), session: %s, userid: %d, result: %d.",
             req.sessionid.c_str(), m_userinfo.userid, req.result);
        return;
    }

    //只有本次会话的两个参与方可以结束它
    if (m_userinfo.userid != s.senderId && m_userinfo.userid != s.receiverId)
    {
        send(cmd_folder_close, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_permission_denied,
                                                          "not a participant of this folder session",
                                                          req.sessionid));
        LOGW("folder close from non-participant, session: %s, userid: %d, sender: %lld, receiver: %lld.",
             req.sessionid.c_str(), m_userinfo.userid,
             (long long)s.senderId, (long long)s.receiverId);
        return;
    }

    int64_t peerId = (m_userinfo.userid == s.senderId) ? s.receiverId : s.senderId;

    send(cmd_folder_close, m_seq,
         FolderTransferProtocol::buildSessionResponse(error_code_ok, "ok", req.sessionid));

    //成功结束：向对端推"已完成"并留记录，会话立即销毁，不留重试余地。
    //正常收尾时双方各发一帧 cmd_folder_close，两帧多半落在两个不同的工作线程上：先到的
    //那一帧走完这里并销毁会话，后到的那一帧要么在上面的"会话已不存在"分支就返回，要么
    //也走到这里 —— 后者由 finishFolderTransferSession 里的终结闸门挡下，不会重复推送、
    //也不会重复留记录
    if (req.result == folder_close_success)
    {
        std::vector<int64_t> notifyPeer;
        notifyPeer.push_back(peerId);
        finishFolderTransferSession(s, folder_state_completed, error_code_ok, "", notifyPeer,
                                    req.linkpath, req.md5);

        mgr.removeSession(req.sessionid);
        m_folderTransferSessionId.clear();

        LOGI("folder transfer completed, session: %s, kind: %d, closed by: %d, peer: %lld, files: %lld, "
             "size: %lld, linkpath: %d.",
             req.sessionid.c_str(), s.kind, m_userinfo.userid, (long long)peerId,
             (long long)s.totalFiles, (long long)s.totalSize, req.linkpath);
        return;
    }

    //单文件直传从没走上 P2P 链路（622）：向对端推「已失败（622）」后立即销毁会话，不进重试保留期 ——
    //发送端随即改为上传服务器发离线文件，留着会话只会多占一个直传名额
    if (s.kind == folder_kind_file && req.errcode == error_code_folder_direct_unavailable)
    {
        std::vector<int64_t> notifyPeer;
        notifyPeer.push_back(peerId);
        finishFolderTransferSession(s, folder_state_failed, req.errcode, req.errmsg, notifyPeer);

        mgr.removeSession(req.sessionid);
        m_folderTransferSessionId.clear();
        LOGI("direct file transfer failed without a p2p link, session: %s, closed by: %d, peer: %lld.",
             req.sessionid.c_str(), m_userinfo.userid, (long long)peerId);
        return;
    }

    //失败结束：置为失败态但<u>不销毁会话</u> —— 会话要保留 folderretrywindow 供发送端点
    //「重试」做断点续传，期满由定时器扫描销毁。失败原因原样透传给对端，保证两端看到的
    //是同一个 errcode（中转通道还通时对端已从 ft_abort 收到过一份，本推送是兜底通路）。
    //
    //置失败态这一步<u>不在这里单独做</u>：它已经并进 finishFolderTransferSession 那道
    //终结闸门（最终落到 FolderTransferManager::finishAttempt），与"判定本次尝试是否已经
    //终结过"在同一个临界区内完成。在闸门之外先置一次的话，双方各发一帧 cmd_folder_close
    //时会话会被两个线程各置一遍终态（实测日志里表现为 state: 7 -> 7）
    //
    //终结闸门：向对端推"已失败"并留记录。<u>同一次尝试以最先到达的终态为准</u>——本笔若在
    //保留期内点「重试」，尝试序号会加一，那一次尝试的结局另留一条记录，两条并存
    //（见 docs/协议设计-v2.md 12.8）
    std::vector<int64_t> notifyPeer;
    notifyPeer.push_back(peerId);
    finishFolderTransferSession(s, folder_state_failed, req.errcode, req.errmsg, notifyPeer, req.linkpath);

    //本连接与这笔传输的绑定<u>不解除</u>：保留期内本端掉线仍要通知对端，
    //故此处不清 m_folderTransferSessionId
    LOGI("folder transfer failed, session: %s, closed by: %d, peer: %lld, errcode: %d, errmsg: %s, "
         "session kept %lld ms for retry.",
         req.sessionid.c_str(), m_userinfo.userid, (long long)peerId,
         req.errcode, req.errmsg.c_str(), (long long)mgr.getRetryWindowMs());
}

void ChatSession::onFolderRetry(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string sessionId;
    if (!FolderTransferProtocol::parseSessionRequest(body, sessionId))
    {
        send(cmd_folder_retry, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_bad_request, "bad request", ""));
        LOGE("folder retry: bad request, userid: %d, body: %s", m_userinfo.userid, body.c_str());
        return;
    }

    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    int64_t now = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;

    //会话是否还在、请求者是不是发送方、会话是否处于失败态、是否仍在保留期内，
    //四项判定连同各自的失败码都由 canRetry 一次给出
    int errCode = error_code_ok;
    if (!mgr.canRetry(sessionId, m_userinfo.userid, now, errCode))
    {
        send(cmd_folder_retry, m_seq,
             FolderTransferProtocol::buildSessionResponse(errCode, "folder retry rejected", sessionId));
        LOGI("folder retry rejected, session: %s, userid: %d, code: %d.",
             sessionId.c_str(), m_userinfo.userid, errCode);
        return;
    }

    //canRetry 通过即说明会话仍在表里，这里取出它是为了拿到双方 userid 与当前的尝试序号
    FolderTransferSessionInfo s;
    if (!mgr.getSession(sessionId, s))
    {
        send(cmd_folder_retry, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_folder_session_expired,
                                                          "folder session expired", sessionId));
        LOGW("folder retry: session %s vanished right after canRetry passed, userid: %d.",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //续传要双方重新建通道，接收端已退出登录则重试没有意义：告知发送端并销毁会话。
    //正常情况下接收端掉线时 cleanupFolderTransferOnOffline 已经把会话销毁了，
    //这一步是为它没来得及执行（例如接收端刚断、清理尚未跑到）时兜底
    std::list<std::shared_ptr<ChatSession>> peerSessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(peerSessions, static_cast<int32_t>(s.receiverId));
    if (peerSessions.empty())
    {
        send(cmd_folder_retry, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_folder_peer_offline, "peer offline",
                                                          sessionId));

        //终态收口：会话就此销毁。推送名单为空 —— 发起重试的发送端由上面那条应答得知结果，
        //接收端已经离线、推了也收不到。本笔在转入失败态时已经留过一条记录，故这次调用
        //只会命中幂等判定并记一条日志；照常走收口是为了不给"新增终态分支忘了留记录"留缺口
        std::vector<int64_t> noNotify;
        finishFolderTransferSession(s, folder_state_peer_offline, error_code_ok, "", noNotify);

        mgr.removeSession(sessionId);
        m_folderTransferSessionId.clear();
        LOGI("folder retry failed, receiver %lld is offline, session destroyed: %s.",
             (long long)s.receiverId, sessionId.c_str());
        return;
    }

    //会话回到传输中、尝试序号加一，随后重新签发两枚令牌并向双方各再推一帧中转参数。
    //接收端不再被询问是否同意，它复用上一次那个目录做断点续传。
    //序号加一必须赶在签发令牌之前：从这一刻起本次会话进入新一次尝试，此后无论成功还是
    //失败，留下的记录条都归入新序号，与上一次失败那条各自成立（"每次尝试各留一条记录"）
    int attemptSeq = 0;
    if (!mgr.beginRetryAttempt(sessionId, now, attemptSeq))
    {
        send(cmd_folder_retry, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_folder_session_expired,
                                                          "folder session expired", sessionId));
        LOGW("folder retry: session %s vanished before the retry attempt began, userid: %d.",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //本地这份会话快照是加一之前取的，同步成新序号，使下面失败分支里留的记录挂在本次尝试上
    s.attemptSeq = attemptSeq;

    const int relayCode = pushFolderRelayInfo(s, now);
    if (relayCode == error_code_folder_direct_unavailable)
    {
        send(cmd_folder_retry, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_folder_direct_unavailable,
                                                          "direct file transfer unavailable", sessionId));

        //理由同 onFolderResponse 的同名分支：单文件直传重试时建不起 P2P 会话，推给双方后销毁会话
        std::vector<int64_t> notifyBoth;
        notifyBoth.push_back(s.senderId);
        notifyBoth.push_back(s.receiverId);
        finishFolderTransferSession(s, folder_state_failed, error_code_folder_direct_unavailable,
                                    "p2p session unavailable", notifyBoth);

        mgr.removeSession(sessionId);
        m_folderTransferSessionId.clear();
        LOGI("direct file retry but p2p session unavailable, session: %s, sender: %d.",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }
    if (relayCode != error_code_ok)
    {
        send(cmd_folder_retry, m_seq,
             FolderTransferProtocol::buildSessionResponse(error_code_server_internal,
                                                          "sign relay token failed", sessionId));

        //终态收口：理由同 onFolderResponse 里的同名分支 —— 令牌签不出来就建不起通道。
        //只推给接收端，发起重试的发送端由上面那条应答得知结果
        std::vector<int64_t> notifyReceiver;
        notifyReceiver.push_back(s.receiverId);
        finishFolderTransferSession(s, folder_state_failed, error_code_folder_relay_broken,
                                    "sign relay token failed", notifyReceiver);

        mgr.removeSession(sessionId);
        m_folderTransferSessionId.clear();
        LOGE("folder retry accepted but relay token sign failed, session: %s, sender: %d.",
             sessionId.c_str(), m_userinfo.userid);
        return;
    }

    send(cmd_folder_retry, m_seq,
         FolderTransferProtocol::buildSessionResponse(error_code_ok, "ok", sessionId));

    LOGI("folder transfer retry accepted, session: %s, attempt: %d, sender: %lld, receiver: %lld.",
         sessionId.c_str(), attemptSeq, (long long)s.senderId, (long long)s.receiverId);
}

int ChatSession::pushFolderRelayInfo(const FolderTransferSessionInfo& s, int64_t now)
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    //两端各一枚令牌：sessionid 相同、role 相反、uid 与 peeruid 对调，
    //fileserver 的中转层据此校验配对关系
    std::string tokenSender;
    std::string tokenReceiver;
    if (!mgr.signTokenFor(s.sessionid, RelayToken::role_folder_sender, now, tokenSender)
        || !mgr.signTokenFor(s.sessionid, RelayToken::role_folder_receiver, now, tokenReceiver))
    {
        LOGE("folder relay info: sign relay token failed, session: %s.", s.sessionid.c_str());
        return error_code_server_internal;
    }

    //中转地址即 fileserver 对客户端公布的那个上传下载地址，本功能不新开端口
    const std::string& relayIp = mgr.getRelayHost();
    int relayPort = mgr.getRelayPort();
    //数据通道条数不含 lane 0 那条控制通道，客户端据此共建 lanes + 1 条连接
    int lanes = mgr.getDataLanes();

    //P2P 方案第 5 期：先尝试在两端之间建 P2P 会话（发送端为发起方）。两端都报到过 P2P 能力、所在企业都开着 P2P
    //才会建成；建成时 1816 带上 p2psessionid，客户端优先走 P2P 链路，建链失败再改走 fileserver 中转。
    //建不成（老客户端、手机端、企业关闭、会话数到上限）时照旧只下发中转参数。
    //单文件直传（2026-10-04 起）用用途 file，数据只走 P2P 链路：建不成就不下发 1816，由调用方按 622 收尾
    const bool directFile = (s.kind == folder_kind_file);
    std::string p2pSessionId;
    std::vector<P2POutFrame> p2pFrames;
    {
        P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
        P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
        const int p2pCode = service.createForFeature(directFile ? kP2PPurposeDirectFile : kP2PPurposeFolder,
                                                     s.sessionid, s.senderId, s.receiverId,
                                                     p2pNowMs(), p2pSessionId, p2pFrames);
        LOGI("folder transfer p2p session: session: %s, kind: %d, code: %d, p2p session: %s.", s.sessionid.c_str(),
             s.kind, p2pCode, p2pSessionId.c_str());
    }
    if (directFile && p2pSessionId.empty())
        return error_code_folder_direct_unavailable;

    sendToUser(static_cast<int32_t>(s.senderId), cmd_folder_relay_info,
               FolderTransferProtocol::buildRelayInfo(s.sessionid, RelayToken::role_folder_sender,
                                                      relayIp, relayPort, tokenSender, lanes, p2pSessionId));
    sendToUser(static_cast<int32_t>(s.receiverId), cmd_folder_relay_info,
               FolderTransferProtocol::buildRelayInfo(s.sessionid, RelayToken::role_folder_receiver,
                                                      relayIp, relayPort, tokenReceiver, lanes, p2pSessionId));
    //2302 排在 1816 之后：客户端先知道这次传输带着哪个 P2P 会话，再收到会话本身
    sendP2PFrames(p2pFrames);

    LOGI("folder relay info pushed, session: %s, sender: %lld, receiver: %lld, relay: %s:%d, lanes: %d, p2p: %s.",
         s.sessionid.c_str(), (long long)s.senderId, (long long)s.receiverId,
         relayIp.c_str(), relayPort, lanes, p2pSessionId.empty() ? "none" : p2pSessionId.c_str());

    return error_code_ok;
}

bool ChatSession::finishFolderTransferSession(const FolderTransferSessionInfo& s,
                                              int state, int errcode, const std::string& errmsg,
                                              const std::vector<int64_t>& notifyUsers,
                                              int linkPath, const std::string& md5)
{
    const int64_t now = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;

    //（1）过程态不该走到这里，出现即说明某条调用路径传错了参数：照常推送、但不留记录，
    //也不占用本次尝试的终结名额（此后这一次尝试真正走到终态时仍要留下自己那条记录），
    //并记一条告警使这个参数错误能被发现
    if (!FolderTransferProtocol::isTerminalState(state))
    {
        if (!notifyUsers.empty())
        {
            Singleton<ChatServer>::Instance().pushToUsers(
                notifyUsers, cmd_folder_state_notify,
                FolderTransferProtocol::buildStateNotify(s.sessionid, state, errcode, errmsg));
        }

        LOGW("folder transfer record skipped, state %d is not a terminal state, session: %s.",
             state, s.sessionid.c_str());
        return false;
    }

    //（2）终结闸门。「本次尝试是否已经终结过」的判断与「标记为已终结」由
    //FolderTransferManager 在同一个临界区内一次做完，只有第一个调用方拿到 true。
    //后面的推送与留记录都在闸门之内，故一次尝试只推一帧状态变更、只留一条记录。
    //会话状态也由它顺带置成终态，调用方不必再单独调一次 setState
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    if (!mgr.finishAttempt(s.sessionid, s.attemptSeq, state, now))
    {
        LOGI("folder transfer terminal decision ignored, session: %s, attempt: %d, state: %d, "
             "already finished by another path.",
             s.sessionid.c_str(), s.attemptSeq, state);
        return false;
    }

    //（3）推终态：名单为空表示本次不推（pushToUsers 对空名单本就直接返回，这里显式
    //判一次是为了让"不推"这条路径在代码里看得见）
    if (!notifyUsers.empty())
    {
        Singleton<ChatServer>::Instance().pushToUsers(
            notifyUsers, cmd_folder_state_notify,
            FolderTransferProtocol::buildStateNotify(s.sessionid, state, errcode, errmsg));
    }

    //（4）留记录：单文件直传只在完成与拒绝时留一条文件消息，其余结局之后发送端要么续传、要么改为离线文件，
    //随后的结局或离线文件消息代替它（见 FolderTransferProtocol::shouldWriteRecord）
    if (!FolderTransferProtocol::shouldWriteRecord(s.kind, state))
    {
        LOGI("folder transfer record not written for this result, session: %s, kind: %d, attempt: %d, state: %d.",
             s.sessionid.c_str(), s.kind, s.attemptSeq, state);
        return true;
    }

    //幂等键由会话 id 与本次尝试的序号一起派生，与上面那道闸门的去重粒度
    //完全一致；它是第二道防线，挡的是日后新增的、绕开闸门的调用路径与进程重启前后的
    //重复落库
    const std::string clientMsgId =
        FolderTransferProtocol::buildRecordClientMsgId(s.sessionid, s.attemptSeq);

    //记录挂在"发送方 → 接收方"这条单聊会话上：from 取文件夹发送方、to 取接收方，
    //两端渲染出来的左右位置因而与传输方向一致
    const int32_t fromId = static_cast<int32_t>(s.senderId);
    const int32_t toId   = static_cast<int32_t>(s.receiverId);

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();

    //落库前先按「发送方 userid + clientmsgid」查一次，已有记录就跳过 —— 去重键与
    //cmd_send_message 的重发去重是同一个（MessageManager::getMsgIdByClientId）
    const int64_t existedId = msgMgr.getMsgIdByClientId(fromId, clientMsgId);
    if (existedId > 0)
    {
        LOGI("folder transfer record already saved, session: %s, attempt: %d, msgid: %lld, "
             "state: %d, duplicate terminal decision ignored.",
             s.sessionid.c_str(), s.attemptSeq, (long long)existedId, state);
        return false;
    }

    FolderTransferRecord record;
    record.sessionid  = s.sessionid;
    record.foldername = s.folderName;
    record.totaldirs  = s.totalDirs;
    record.totalfiles = s.totalFiles;
    record.totalsize  = s.totalSize;
    record.result     = state;
    record.linkpath   = linkPath;

    //只有"已失败"这一种结局才带有意义的失败原因。buildRecordContent 内部同样会做这一步
    //归一（那条不变式由它统一保证），此处先归一一次是为了让下面那条日志打出来的 errcode
    //与真正落库的内容一致，排查时不会被误导
    record.errcode = 0;
    if (state == folder_state_failed)
        record.errcode = errcode;

    //文件夹传输：content 只有动作与结局，没有文件清单、路径与 md5，msgtype=18。
    //单文件直传：留一条普通文件消息（msgtype=3），content 带直传标记、会话 id、结局、链路路径与 MD5，没有 fileid
    const bool directFile = (s.kind == folder_kind_file);
    const int msgType = directFile ? message_type_file : message_type_folder;
    std::string content;
    if (directFile)
    {
        DirectFileRecord fileRecord;
        fileRecord.sessionid = s.sessionid;
        fileRecord.filename  = s.folderName;
        fileRecord.filesize  = s.totalSize;
        fileRecord.result    = state;
        //拒绝时一个字节都没传：不写 MD5 与链路路径
        if (state == folder_state_completed)
        {
            fileRecord.md5      = md5;
            fileRecord.linkpath = linkPath;
        }
        content = FolderTransferProtocol::buildDirectFileContent(fileRecord);
    }
    else
    {
        content = FolderTransferProtocol::buildRecordContent(record);
    }

    //落库前还要再套一层 buildStoredContent 的外壳（content/mentions/mentionall/quote），
    //与单聊 / 群聊 / 转发那几条落库路径口径一致。读回时 parseStoredContent 只认最外层的
    //content 字段，若把裸 content 直接存进去，重新登录同步会话列表时摘要会退化成
    //"[消息]"、翻历史时这条记录的正文也会是空的。本记录不带 @ 与引用
    std::vector<int64_t> noMentions;
    const std::string storedContent =
        MessageProtocol::buildStoredContent(content, noMentions, false, "");

    //落 t_chatmsg。会话列表的摘要与未读不另做处理：会话集合、最后一条消息与未读数都由
    //t_chatmsg 派生（见 MessageManager::getConversationBriefs），这条记录一入库，双方的
    //会话列表下次拉取时自然带上它，接收方的未读也照常计入
    const int64_t msgId = msgMgr.saveMessage(clientMsgId, CHAT_TYPE_SINGLE, fromId, toId,
                                             msgType, storedContent, now);
    if (msgId == 0)
    {
        LOGE("save folder transfer record failed, session: %s, attempt: %d, from: %d, to: %d, "
             "state: %d.",
             s.sessionid.c_str(), s.attemptSeq, fromId, toId, state);
        return false;
    }

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype       = CHAT_TYPE_SINGLE;
    rec.from        = s.senderId;
    rec.to          = s.receiverId;
    rec.msgtype     = msgType;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = now;
    rec.recalled    = false;

    //双方都要收到。服务端生成的消息没有"发送方的发送回执"这一说，故发送方也走
    //cmd_message_notify；两帧的差别只在会话 id ——各自视角下的会话 id 都是对端 userid，
    //推给接收方时取发送方、推给发送方时取接收方
    std::vector<int64_t> receiverOnly;
    receiverOnly.push_back(s.receiverId);
    Singleton<ChatServer>::Instance().pushToUsers(
        receiverOnly, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, s.senderId));

    std::vector<int64_t> senderOnly;
    senderOnly.push_back(s.senderId);
    Singleton<ChatServer>::Instance().pushToUsers(
        senderOnly, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, s.receiverId));

    LOGI("folder transfer record saved, session: %s, kind: %d, attempt: %d, msgid: %lld, from: %d, to: %d, "
         "name: %s, dirs: %lld, files: %lld, size: %lld, result: %d, errcode: %d, linkpath: %d.",
         s.sessionid.c_str(), s.kind, s.attemptSeq, (long long)msgId, fromId, toId,
         s.folderName.c_str(), (long long)s.totalDirs, (long long)s.totalFiles,
         (long long)s.totalSize, state, record.errcode, linkPath);

    return true;
}

void ChatSession::cleanupFolderTransferOnOffline()
{
    //先取出再清空：本方法可能被登出与连接断开各调用一次，清空使第二次成为空操作
    std::string recordedSessionId = m_folderTransferSessionId;
    m_folderTransferSessionId.clear();

    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    //本次待收尾的会话。一个用户现在可同时参与至多 foldermaxperuser 笔文件夹传输
    //（配置项默认 5 笔，作发送方与作接收方各算一笔），故这里是一个列表、要逐笔收尾。
    //只处理其中一笔的话，漏掉的那几笔的对端会一直干等到邀请超时（默认 30 分钟）
    //才知道这一方已经离线
    std::vector<FolderTransferSessionInfo> pending;

    //（1）本连接自己记着的那笔：本端发出过邀请（1810）或应答同意过（1812）
    if (!recordedSessionId.empty())
    {
        FolderTransferSessionInfo recorded;
        if (mgr.getSession(recordedSessionId, recorded))
        {
            pending.push_back(recorded);
        }
        else
        {
            //这一笔已正常收尾，或已被邀请超时 / 失败保留期的清理销毁，无人需要通知
            LOGI("folder transfer cleanup on offline: session %s already gone, userid: %d.",
                 recordedSessionId.c_str(), m_userinfo.userid);
        }
    }

    //（2）按 userid 反查本用户参与的全部会话。这一步有两重必要性：
    //     其一，尚未应答邀请的接收端手里没有会话 id（会话是发送端那条连接建的），
    //     它在响铃阶段掉线时只能靠 userid 定位；不做这层兜底的话，发送端只能一直等到
    //     邀请超时才知道对方已经离线，而协议要求这种情况下发送端收到的是"对方离线"
    //     （见 docs/协议设计-v2.md 12.2.6）。
    //     其二，m_folderTransferSessionId 只记得住最近的那一笔，本用户同时参与的其余
    //     几笔同样要收尾。
    //     未登录的连接（登录流程尚未走完就断开）没有 userid 可查，也不可能参与传输
    if (m_userinfo.userid != 0)
    {
        //多端登录（PC 与手机可同时在线）时，不能仅凭本连接断开就断定用户已离线：
        //反查是按 userid 命中的，那些会话可能属于本账号的另一端，或者邀请仍在另一端
        //响铃。本账号还有别的在线连接时不做反查收尾，留给真正参与的那条连接断开时
        //处理，或由邀请超时兜底。被顶号踢下线的旧连接 userid 已被置 0，不算作在线。
        //会话快照由 getSessions 在其内部加锁拷贝出来，返回时 ChatServer::m_sessionMutex
        //已经释放，故下面的推送与销毁不会与该锁构成嵌套
        std::list<std::shared_ptr<ChatSession>> allSessions;
        Singleton<ChatServer>::Instance().getSessions(allSessions);

        bool hasAnotherOnlineSession = false;
        for (std::list<std::shared_ptr<ChatSession>>::iterator it = allSessions.begin();
             it != allSessions.end(); ++it)
        {
            //本连接自身要排除在外：退出登录（1002）时它还挂在会话表里，
            //连接断开时它已被 ChatServer::onDisconnected 摘除
            if (!(*it) || (*it)->getSessionId() == m_id)
                continue;

            if ((*it)->getUserId() == m_userinfo.userid)
            {
                LOGI("folder transfer cleanup on offline skipped, userid %d still has another "
                     "online session %d.",
                     m_userinfo.userid, (*it)->getSessionId());
                hasAnotherOnlineSession = true;
                break;
            }
        }

        if (!hasAnotherOnlineSession)
        {
            //反查结果追加进一个独立的容器，再逐条并入待收尾列表：（1）里已经收进来的
            //那笔必然也在反查结果里，重复收尾会给对端推两帧状态变更
            std::vector<FolderTransferSessionInfo> byUser;
            mgr.findSessionsByUser(m_userinfo.userid, byUser);

            for (size_t i = 0; i < byUser.size(); ++i)
            {
                bool duplicated = false;
                for (size_t j = 0; j < pending.size(); ++j)
                {
                    if (pending[j].sessionid == byUser[i].sessionid)
                    {
                        duplicated = true;
                        break;
                    }
                }

                if (!duplicated)
                    pending.push_back(byUser[i]);
            }
        }
    }

    //（3）逐笔收尾：向对端推一帧状态变更，随后销毁会话。绝大多数连接的 pending 都是空的
    for (size_t i = 0; i < pending.size(); ++i)
    {
        const FolderTransferSessionInfo& s = pending[i];

        //本人须确实是这笔传输的参与方，避免因会话 id 记串而销毁了别人的会话。
        //不匹配时只跳过这一笔，其余几笔照常收尾
        if (m_userinfo.userid != s.senderId && m_userinfo.userid != s.receiverId)
        {
            LOGW("folder transfer cleanup on offline: userid %d is not a participant of session %s, "
                 "sender: %lld, receiver: %lld.",
                 m_userinfo.userid, s.sessionid.c_str(),
                 (long long)s.senderId, (long long)s.receiverId);
            continue;
        }

        int64_t peerId = (m_userinfo.userid == s.senderId) ? s.receiverId : s.senderId;

        //推给对端的状态按会话当时处于哪个阶段区分，取值见 docs/协议设计-v2.md 12.2.6 的推送时机表：
        //  还在响铃（邀请尚未被应答）—— 一个字节都还没传，推"对方离线"，errcode 恒为 0；
        //  已进入传输中或失败待重试 —— 已经建过通道、可能已落盘一部分，按整体失败处理，
        //                              推"已失败"并带 error_code_folder_peer_offline，
        //                              使对端与本端看到同一条失败原因。
        int notifyState = folder_state_failed;
        if (s.state == folder_state_ringing)
            notifyState = folder_state_peer_offline;

        //终态收口：推给对端的同时在双方会话里留下这条传输记录
        std::vector<int64_t> notifyPeer;
        notifyPeer.push_back(peerId);
        finishFolderTransferSession(s, notifyState, error_code_folder_peer_offline,
                                    "peer offline", notifyPeer);

        //续传位点存在双方客户端的内存与接收端的半成品文件里，服务端只记会话；本端连接一断
        //这些位点即作废，会话留着也无法再续传，故直接销毁而不是留进保留期
        //（见 docs/协议设计-v2.md 12.7 不变式 2）
        mgr.removeSession(s.sessionid);

        LOGI("folder transfer aborted because a participant went offline, session: %s, offline user: %d, "
             "notified peer: %lld, session state was: %d, notify state: %d.",
             s.sessionid.c_str(), m_userinfo.userid, (long long)peerId, s.state, notifyState);
    }
}

void ChatSession::onSetConversation(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SetConversationRequest req;
    if (!MessageProtocol::parseSetConversationRequest(body, req))
    {
        send(cmd_set_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //仅支持单聊与群聊
    if (req.ctype != CHAT_TYPE_SINGLE && req.ctype != CHAT_TYPE_GROUP)
    {
        send(cmd_set_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "unsupported ctype"));
        return;
    }

    //消息提醒模式：取值须在 0~5 之内，单聊不能设为屏蔽与「只提醒 @我和关注的人」
    if (req.has_notifymode && !MessageProtocol::isValidNotifyMode(req.ctype, req.notifymode))
    {
        LOGI("set conversation rejected, invalid notify mode, userid: %d, ctype: %d, cid: %lld, mode: %d",
             m_userinfo.userid, req.ctype, (long long)req.cid, req.notifymode);
        send(cmd_set_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "invalid notify mode"));
        return;
    }

    //单聊改提醒模式：对象须是够得着的同事（设回允许提醒除外）
    if (req.ctype == CHAT_TYPE_SINGLE && req.has_notifymode)
    {
        int targetCode = checkSingleChatNotifyTarget(req.cid, req.notifymode);
        if (targetCode != error_code_ok)
        {
            LOGI("set conversation rejected, notify target check failed, userid: %d, cid: %lld, mode: %d, code: %d",
                 m_userinfo.userid, (long long)req.cid, req.notifymode, targetCode);
            send(cmd_set_conversation, m_seq,
                 MessageProtocol::buildSimpleResponse(targetCode, "invalid notify target"));
            return;
        }
    }

    //群聊：须为群成员才能设置该群会话（置顶 / 消息提醒模式 / 删除）
    if (req.ctype == CHAT_TYPE_GROUP &&
        !Singleton<GroupManager>::Instance().isGroupMember(req.cid, m_userinfo.userid))
    {
        send(cmd_set_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    int errorCode = error_code_ok;
    if (!msgMgr.setConversation(m_userinfo.userid, req, now, errorCode))
    {
        const char* msg = "set conversation failed";
        if (errorCode == error_code_starred_limit)
            msg = "starred count limit reached";
        LOGI("set conversation failed, userid: %d, ctype: %d, cid: %lld, code: %d",
             m_userinfo.userid, req.ctype, (long long)req.cid, errorCode);
        send(cmd_set_conversation, m_seq, MessageProtocol::buildSimpleResponse(errorCode, msg));
        return;
    }

    send(cmd_set_conversation, m_seq, MessageProtocol::buildSimpleResponse(0, "ok"));

    //读回完整状态，向本人所有在线会话推 conversation_changed_notify 做多端同步
    bool pinned = false;
    int notifymode = notify_mode_allow;
    bool deleted = false;
    bool unreadmark = false;
    msgMgr.getConversationState(m_userinfo.userid, req.ctype, req.cid, pinned, notifymode, deleted, unreadmark);
    sendToUser(m_userinfo.userid, cmd_conversation_changed_notify,
               MessageProtocol::buildConversationChangedNotify(req.ctype, req.cid,
                                                               pinned, notifymode, deleted, unreadmark));

    LOGI("set conversation ok, userid: %d, cid: %lld", m_userinfo.userid, (long long)req.cid);
}

void ChatSession::onSearchMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SearchMessagesRequest req;
    if (!MessageProtocol::parseSearchMessagesRequest(body, req))
    {
        send(cmd_search_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //仅支持单聊与群聊
    if (req.ctype != CHAT_TYPE_SINGLE && req.ctype != CHAT_TYPE_GROUP)
    {
        send(cmd_search_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "unsupported ctype"));
        return;
    }

    //群聊：须为群成员才能搜本群消息；本人看不到的入群前历史，搜索同样不返回
    GroupHistoryBound bound;
    if (req.ctype == CHAT_TYPE_GROUP)
    {
        if (!Singleton<GroupManager>::Instance().isGroupMember(req.cid, m_userinfo.userid))
        {
            send(cmd_search_messages, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
            return;
        }
        bound = groupHistoryBound(req.cid);
    }

    //limit 夹到合理区间：复用 sync_messages 的缺省 20、上限 100
    if (req.limit <= 0)
        req.limit = SYNC_MESSAGES_DEFAULT_LIMIT;
    if (req.limit > SYNC_MESSAGES_MAX_LIMIT)
        req.limit = SYNC_MESSAGES_MAX_LIMIT;

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    std::vector<MessageRecord> messages;
    bool hasMore = false;
    msgMgr.searchMessages(m_userinfo.userid, req, messages, hasMore, bound.m_minCreateTime, bound.m_minMsgId);

    //卡片消息合并当前互动状态，理由同 onGetPinnedMessages。须在按体积截断之前做
    augmentCardRecords(messages);

    //按体积截断，理由同 onSyncMessages：搜索按 beforemsgid 往前翻页，锚点在最新一端
    //（升序数组的末尾），截掉更早的命中并把 hasmore 置为真，下一页从留下的最早一条接着搜。
    //search 应答与 sync_messages 同构，复用同一个组装函数（不带已读位点与锚点）
    size_t trimmed = 0;
    const std::string response = MessageProtocol::buildSyncMessagesResponseWithinBudget(
        0, "ok", req.ctype, req.cid, hasMore, messages, MessageProtocol::kPeerReadMsgidOmitted, NULL,
        true, RESPONSE_MESSAGES_BUDGET_BYTES, trimmed);
    if (trimmed > 0)
    {
        LOGI("search messages trimmed by size, userid: %d, cid: %lld, kept: %u, trimmed: %u",
             m_userinfo.userid, (long long)req.cid,
             static_cast<unsigned>(messages.size()), static_cast<unsigned>(trimmed));
    }
    send(cmd_search_messages, m_seq, response);

    LOGI("search messages ok, userid: %d, cid: %lld, count: %u",
         m_userinfo.userid, (long long)req.cid, static_cast<unsigned>(messages.size()));
}

void ChatSession::onGlobalSearch(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    const std::vector<GlobalSearchConversationHit> noConvs;
    const std::vector<GlobalSearchAnnouncementHit> noAnns;
    GlobalSearchRequest req;
    if (!GlobalSearchProtocol::parseRequest(body, req))
    {
        send(cmd_global_search, m_seq,
             GlobalSearchProtocol::buildResponse(error_code_bad_request, "bad request", "", noConvs, noAnns));
        return;
    }

    //本人所在的群，以及每个群里能看到的历史范围（规则与 groupHistoryBound 相同，见 getUserHistoryScopes）
    std::vector<GroupSearchScope> scopes;
    Singleton<GroupManager>::Instance().getUserHistoryScopes(m_userinfo.userid, scopes);
    std::vector<int64_t> groupIds;
    for (size_t i = 0; i < scopes.size(); ++i)
        groupIds.push_back(scopes[i].groupid);

    std::vector<GlobalSearchConversationHit> convs;
    if (!Singleton<MessageManager>::Instance().globalSearchConversations(
            m_userinfo.userid, req.keyword, scopes, req.limit, convs))
    {
        send(cmd_global_search, m_seq,
             GlobalSearchProtocol::buildResponse(error_code_server_internal, "db error", "", noConvs, noAnns));
        return;
    }

    std::vector<GlobalSearchAnnouncementHit> anns;
    if (req.announcementlimit > 0)
    {
        std::vector<GroupAnnouncementRecord> records;
        //群公告查失败只少这一段，聊天记录照常返回
        if (Singleton<GroupManager>::Instance().searchAnnouncements(groupIds, req.keyword, req.announcementlimit, records))
        {
            for (size_t i = 0; i < records.size(); ++i)
            {
                GlobalSearchAnnouncementHit hit;
                hit.groupid = records[i].groupid;
                hit.id = records[i].id;
                hit.title = records[i].title;
                hit.excerpt = chatserver::MakeSearchExcerpt(records[i].content, req.keyword, GLOBAL_SEARCH_EXCERPT_MAX_BYTES);
                hit.publishtime = records[i].publishtime;
                anns.push_back(hit);
            }
        }
    }

    send(cmd_global_search, m_seq, GlobalSearchProtocol::buildResponse(0, "ok", req.keyword, convs, anns));
    //关键字只记字节数，日志里不留用户搜过什么
    LOGI("global search ok, userid: %d, keyword bytes: %u, groups: %u, conversations: %u, announcements: %u",
         m_userinfo.userid, static_cast<unsigned>(req.keyword.size()), static_cast<unsigned>(scopes.size()),
         static_cast<unsigned>(convs.size()), static_cast<unsigned>(anns.size()));
}

GroupHistoryBound ChatSession::groupHistoryBound(int64_t groupid)
{
    //取本人的成员行（角色、入群时间、历史边界）与群设置。群主与管理员不受限 —— 这两项限制是拿来
    //防新人翻旧账的，管理者需要看全量记录才能处理纠纷（GroupProtocol::computeHistoryBound）
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupMemberRecord me;
    if (!groupMgr.getMember(groupid, m_userinfo.userid, me))
        return GroupHistoryBound();
    if (me.role >= group_role_admin)
        return GroupHistoryBound();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
        return GroupHistoryBound();
    return GroupProtocol::computeHistoryBound(me.role, group.newseehistory, me.jointime, me.historyfrom);
}

int64_t ChatSession::inviteHistoryFrom(const GroupRecord& group, int historyLimit)
{
    //群设为不可看历史：以群设置为准（时间下界会在查询时生效），不另设边界
    if (group.newseehistory == 0 || historyLimit == kInviteHistoryLimitAll)
        return 0;
    int64_t from = 0;
    if (!Singleton<MessageManager>::Instance().computeGroupHistoryFrom(group.groupid, historyLimit, from))
        return 0;
    return from;
}

void ChatSession::onForwardMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    ForwardMessageRequest req;
    if (!MessageProtocol::parseForwardMessageRequest(body, req))
    {
        send(cmd_forward_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //目标会话类型校验：支持转发到单聊或群聊
    if (req.toctype != CHAT_TYPE_SINGLE && req.toctype != CHAT_TYPE_GROUP)
    {
        send(cmd_forward_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "unsupported toctype"));
        return;
    }

    //目标会话校验：单聊查对端存在 + 黑名单；群聊与普通群发同一套校验（群成员身份、
    //群是否存在、禁言）
    GroupRecord targetGroup;
    if (req.toctype == CHAT_TYPE_SINGLE)
    {
        int32_t targetId = static_cast<int32_t>(req.tocid);
        if (targetId == m_userinfo.userid)
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_bad_request, "cannot forward to yourself"));
            return;
        }

        UserManager& userMgr = Singleton<UserManager>::Instance();
        User target;
        target.userid = 0;
        userMgr.getUserInfoByUserId(targetId, target);
        if (target.userid == 0)
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_not_found, "target user not found"));
            return;
        }
        if (userMgr.isInBlacklist(targetId, m_userinfo.userid))
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_in_blacklist, "you are in the target's blacklist"));
            return;
        }
    }
    else
    {
        //不通过时已回过错误应答（不是群成员 / 群不存在 / 禁言中）
        int myrole = group_role_member;
        //转发的错误应答是 { code, msg }，不需要 clientmsgid
        if (!checkGroupSpeakAllowed(cmd_forward_message, req.tocid, std::string(), myrole, targetGroup))
            return;
    }

    //取转发源消息；本人已删除的、不在本人可见范围内的在取的时候就已排除，这里再只保留本人
    //所在会话里（单聊须为收发方之一、群聊须为该群成员）未撤回、且不是共享卡片的。共享卡片只给
    //部分成员看，与已撤回的消息一样静默跳过；全部被跳过时按「没有可转发的消息」回复
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    std::vector<MessageRecord> sources;
    msgMgr.getMessagesByIds(m_userinfo.userid, req.msgids, sources);
    std::vector<MessageRecord> visible;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const MessageRecord& s = sources[i];
        if (!MessageProtocol::isForwardableSource(s))
            continue;

        bool canSee = false;
        if (s.ctype == CHAT_TYPE_GROUP)
            canSee = groupMgr.isGroupMember(s.to, m_userinfo.userid);
        else
            canSee = (s.from == m_userinfo.userid || s.to == m_userinfo.userid);
        if (canSee)
            visible.push_back(s);
    }
    if (visible.empty())
    {
        send(cmd_forward_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_message_not_exist, "no forwardable message"));
        return;
    }

    //企业级敏感词检查放在下面两个分支里、新内容组装完成之后：查的是对方实际会收到的内容
    //（逐条转发会去掉引用，合并转发会加上标题），转发到单聊与群聊都检查。原先只对转发到群
    //做的群级检查已于 2026-10-02 删除

    //本次新建的消息，按新建顺序排列；应答把它们的完整消息体回带给转发人
    std::vector<MessageRecord> created;
    if (req.merge)
    {
        //合并转发：所有源消息打包成一条 msgtype=15 消息。标题在解析阶段已经截断、
        //空串已换成缺省值（MessageProtocol::normalizeForwardTitle），这里直接使用。
        //每条子消息带上发送者此刻的昵称，使这份记录不依赖接收方的通讯录
        std::map<int64_t, std::string> fromNames;
        UserManager& userMgr = Singleton<UserManager>::Instance();
        for (size_t i = 0; i < visible.size(); ++i)
        {
            if (fromNames.find(visible[i].from) != fromNames.end())
                continue;
            User sender;
            sender.userid = 0;
            userMgr.getUserInfoByUserId(static_cast<int32_t>(visible[i].from), sender);
            //查不到的发送者记一个空串，只为不再重复查询；空串不会写进 fromname
            fromNames[visible[i].from] = (sender.userid != 0) ? sender.nickname : std::string();
        }

        std::string mergedContent = MessageProtocol::buildMergedContent(req.title, visible, fromNames);
        //体积超限的合并记录不落库：它会被推送、被同步，超过客户端单帧上限时对方会断线
        if (MessageProtocol::isMergedContentTooLarge(mergedContent))
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_msg_too_large,
                                                      "merged content too large"));
            LOGI("forward rejected by merged content size, userid: %d, sources: %u, bytes: %u",
                 m_userinfo.userid, static_cast<unsigned>(visible.size()),
                 static_cast<unsigned>(mergedContent.size()));
            return;
        }

        //敏感词：标题与全部子消息（含子消息上的引用、嵌套的合并记录）一并检查，命中即整体拒绝
        std::vector<std::string> sensitiveTexts;
        MessageProtocol::collectSensitiveTexts(MSGTYPE_MERGED_FORWARD, mergedContent, std::string(),
                                               sensitiveTexts);
        if (hitSensitive(chatserver::sensitive_source_forward, req.toctype, req.tocid, std::string(),
                         sensitiveTexts))
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_sensitive_word,
                                                      "message contains sensitive word"));
            return;
        }

        MessageRecord rec;
        if (deliverConversationMessage(req.toctype, req.tocid, MSGTYPE_MERGED_FORWARD,
                                       mergedContent, req.clientmsgid, &rec) != 0)
            created.push_back(rec);
    }
    else
    {
        //逐条转发：每条源消息在目标会话各新建一条，clientmsgid 按 "原ID-序号" 派生。
        //文字消息去掉 content 里的 at 与 quote，理由见 MessageProtocol::buildForwardedContent
        //
        //先把每条要新建的内容组装好并逐条检查体积，有一条超限就整体拒绝、一条都不发出，
        //避免目标会话里只出现其中一部分。会超限的只可能是单条上限生效之前存进库的旧消息，
        //它被推送或同步时同样会让对方断线。
        //
        //接龙卡片不能逐条转发（理由见 CardProtocol::canForwardSeparately），与已撤回的源消息
        //一样跳过、其余照常转发；客户端据应答里实际新建的消息提示用户哪些没有转发出去。
        //跳过之后一条不剩时按"没有可转发的消息"回复
        std::vector<MessageRecord> separate;
        separate.reserve(visible.size());
        for (size_t i = 0; i < visible.size(); ++i)
        {
            if (CardProtocol::canForwardSeparately(visible[i].msgtype))
                separate.push_back(visible[i]);
        }
        if (separate.size() != visible.size())
        {
            LOGI("forward skipped relay cards, userid: %d, skipped: %u, remaining: %u",
                 m_userinfo.userid, static_cast<unsigned>(visible.size() - separate.size()),
                 static_cast<unsigned>(separate.size()));
        }
        if (separate.empty())
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_message_not_exist, "no forwardable message"));
            return;
        }

        std::vector<std::string> contents;
        contents.reserve(separate.size());
        for (size_t i = 0; i < separate.size(); ++i)
        {
            contents.push_back(MessageProtocol::buildForwardedContent(separate[i].msgtype,
                                                                      separate[i].content));
            if (MessageProtocol::isMessageContentTooLarge(contents.back(), std::string()))
            {
                send(cmd_forward_message, m_seq,
                     MessageProtocol::buildSimpleResponse(error_code_msg_too_large,
                                                          "forwarded content too large"));
                LOGI("forward rejected by message content size, userid: %d, msgid: %lld, bytes: %u",
                     m_userinfo.userid, (long long)separate[i].msgid,
                     static_cast<unsigned>(contents.back().size()));
                return;
            }
        }

        //敏感词：逐条检查要新建的内容，有一条命中就整体拒绝、一条都不发出（理由同上面的体积检查）。
        //逐条转发的新消息不带顶层 quote，文字消息 content 里的 quote 也已被 buildForwardedContent
        //去掉，这些引用文字不会出现在目标会话；这里检查的是去掉之后、实际要新建的内容
        std::vector<std::string> sensitiveTexts;
        for (size_t i = 0; i < separate.size(); ++i)
            MessageProtocol::collectSensitiveTexts(separate[i].msgtype, contents[i], std::string(), sensitiveTexts);
        if (hitSensitive(chatserver::sensitive_source_forward, req.toctype, req.tocid, std::string(),
                         sensitiveTexts))
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_sensitive_word,
                                                      "message contains sensitive word"));
            return;
        }

        for (size_t i = 0; i < separate.size(); ++i)
        {
            std::string cmid = req.clientmsgid + "-" + std::to_string(i);
            MessageRecord rec;
            if (deliverConversationMessage(req.toctype, req.tocid,
                                           separate[i].msgtype, contents[i], cmid, &rec) != 0)
                created.push_back(rec);
        }
    }

    send(cmd_forward_message, m_seq,
         MessageProtocol::buildForwardMessageResponse(0, "ok", req.clientmsgid, created, req.tocid));
    LOGI("forward message ok, userid: %d, toctype: %d, tocid: %lld, merge: %d, sources: %u, created: %u",
         m_userinfo.userid, req.toctype, (long long)req.tocid, req.merge ? 1 : 0,
         static_cast<unsigned>(visible.size()), static_cast<unsigned>(created.size()));
}

void ChatSession::onDeleteMessage(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t msgid = 0;
    if (!MessageProtocol::parseDeleteMessageRequest(body, msgid))
    {
        send(cmd_delete_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessageRecord rec;
    if (!msgMgr.getMessageById(msgid, rec))
    {
        send(cmd_delete_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_message_not_exist, "message not exist"));
        return;
    }

    //只能删除本人参与的会话里的消息：单聊须为收发双方之一，群聊须为该群成员
    bool canDelete = false;
    if (rec.ctype == CHAT_TYPE_GROUP)
        canDelete = Singleton<GroupManager>::Instance().isGroupMember(rec.to, m_userinfo.userid);
    else
        canDelete = (rec.from == m_userinfo.userid || rec.to == m_userinfo.userid);
    if (!canDelete)
    {
        send(cmd_delete_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_message_not_exist, "message not in your conversation"));
        return;
    }

    if (!msgMgr.deleteMessageForUser(m_userinfo.userid, msgid))
    {
        send(cmd_delete_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_server_internal, "delete message failed"));
        return;
    }

    send(cmd_delete_message, m_seq, MessageProtocol::buildSimpleResponse(0, "ok"));

    //推给本人全部在线设备（含发起删除的这一台）：各设备据此删掉本地库里的这一行、移除已打开
    //窗口里的气泡。只靠"下次同步时自然被过滤"不够 —— 其他设备本地库与已打开窗口里的这一条
    //会一直留着。cid 取本人视角：群聊为群id，单聊为对方userid
    const int64_t selfId = static_cast<int64_t>(m_userinfo.userid);
    const int64_t cid = (rec.ctype == CHAT_TYPE_GROUP) ? rec.to
                      : ((rec.from == selfId) ? rec.to : rec.from);
    std::vector<int64_t> deletedIds;
    deletedIds.push_back(msgid);
    sendToUser(m_userinfo.userid, cmd_messages_deleted_notify,
               MessageProtocol::buildMessagesDeletedNotify(rec.ctype, cid, deletedIds));

    LOGI("delete message ok, userid: %d, ctype: %d, cid: %lld, msgid: %lld",
         m_userinfo.userid, rec.ctype, (long long)cid, (long long)msgid);
}

void ChatSession::onClearConversation(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    ClearConversationRequest req;
    if (!MessageProtocol::parseClearConversationRequest(body, req))
    {
        LOGE("clear conversation bad request, userid: %d", m_userinfo.userid);
        send(cmd_clear_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //群聊须是该群成员：不在群里的人本来就看不到这个群的记录，也不该凭空写一行会话状态
    if (req.ctype == conversation_group
        && Singleton<GroupManager>::Instance().getMemberRole(req.cid, m_userinfo.userid) < 0)
    {
        LOGE("clear conversation not a group member, userid: %d, groupid: %lld",
             m_userinfo.userid, (long long)req.cid);
        send(cmd_clear_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    //单聊不能以自己为对象
    if (req.ctype == conversation_single && req.cid == m_userinfo.userid)
    {
        send(cmd_clear_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "cannot clear conversation with yourself"));
        return;
    }

    //只写一个锚点，写入量与会话里的消息条数无关，不会长时间占住本 I/O 线程
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;
    int64_t anchor = 0;
    if (!Singleton<MessageManager>::Instance().clearConversation(m_userinfo.userid, req.ctype, req.cid,
                                                                  nowMs, anchor))
    {
        LOGE("clear conversation db error, userid: %d, ctype: %d, cid: %lld",
             m_userinfo.userid, req.ctype, (long long)req.cid);
        send(cmd_clear_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_server_internal, "clear conversation failed"));
        return;
    }

    send(cmd_clear_conversation, m_seq,
         MessageProtocol::buildClearConversationResponse(0, "ok", req.ctype, req.cid, anchor));

    //推给本人全部在线设备（含发起的这一台），各设备据此删掉本地库里该会话锚点之前的消息、刷新界面
    sendToUser(m_userinfo.userid, cmd_messages_deleted_notify,
               MessageProtocol::buildMessagesClearedNotify(req.ctype, req.cid, anchor));

    LOGI("clear conversation ok, userid: %d, ctype: %d, cid: %lld, anchor: %lld",
         m_userinfo.userid, req.ctype, (long long)req.cid, (long long)anchor);
}

void ChatSession::onDeleteMessagesByTime(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    DeleteMessagesByTimeRequest req;
    if (!MessageProtocol::parseDeleteMessagesByTimeRequest(body, req))
    {
        LOGE("delete messages by time bad request, userid: %d", m_userinfo.userid);
        send(cmd_delete_messages_by_time, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //区间终点截断到服务端当前这一秒开始之前，防止客户端时钟偏快时把之后到达的新消息
    //也算进区间（见 MessageDeleteRange.h 的 NormalizeDeleteRange）
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;
    msgdelete::DeleteRange range;
    if (!msgdelete::NormalizeDeleteRange(req.begintime, req.endtime, nowMs, range))
    {
        LOGE("delete messages by time invalid range, userid: %d, begin: %lld, end: %lld, now: %lld",
             m_userinfo.userid, (long long)req.begintime, (long long)req.endtime, (long long)nowMs);
        send(cmd_delete_messages_by_time, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "invalid time range"));
        return;
    }

    //只写一条区间（与已有区间合并），写入量与区间内的消息条数无关，不会长时间占住本 I/O 线程
    const std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    if (!msgMgr.addDeletedRange(m_userinfo.userid, range, nowMs))
    {
        LOGE("delete messages by time db error, userid: %d, begin: %lld, end: %lld",
             m_userinfo.userid, (long long)range.begintime, (long long)range.endtime);
        send(cmd_delete_messages_by_time, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_server_internal, "delete messages failed"));
        return;
    }
    const long long elapsedMs = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count());

    //应答回带截断后的实际区间：客户端要按它提示用户，而不是按自己请求时的值
    send(cmd_delete_messages_by_time, m_seq,
         MessageProtocol::buildDeleteMessagesByTimeResponse(0, "ok", range.begintime, range.endtime));

    //推给本人全部在线设备（含发起的这一台），各设备据此删掉本地库里该区间的消息、刷新界面
    sendToUser(m_userinfo.userid, cmd_messages_deleted_notify,
               MessageProtocol::buildMessagesDeletedRangeNotify(range.begintime, range.endtime));

    LOGI("delete messages by time ok, userid: %d, requested: [%lld, %lld], effective: [%lld, %lld], "
         "elapsed: %lld ms",
         m_userinfo.userid, (long long)req.begintime, (long long)req.endtime,
         (long long)range.begintime, (long long)range.endtime, elapsedMs);
}

void ChatSession::onMassSend(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    MassSendRequest req;
    if (!MessageProtocol::parseMassSendRequest(body, req))
    {
        send(cmd_mass_send, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //内容体积超限：一个目标都不发。与 cmd_send_message 同一上限，理由也相同：超大消息推送给对方时
    //会超过客户端的单帧上限，导致对方断线
    if (MessageProtocol::isMessageContentTooLarge(req.content, std::string()))
    {
        LOGW("mass send rejected by content size, userid: %d, targets: %u, bytes: %u",
             m_userinfo.userid, static_cast<unsigned>(req.targets.size()),
             static_cast<unsigned>(req.content.size()));
        send(cmd_mass_send, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_msg_too_large, "message content too large"));
        return;
    }

    //共享卡片与系统提示只能由服务端生成，客户端群发同样拒绝，一个目标都不发（理由见 isClientSendableMsgType）
    if (!MessageProtocol::isClientSendableMsgType(req.msgtype))
    {
        LOGW("mass send rejected by server-only msgtype, userid: %d, msgtype: %d",
             m_userinfo.userid, req.msgtype);
        send(cmd_mass_send, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_permission_denied, "msgtype is generated by server only"));
        return;
    }

    //按消息类型检查 content（目前只检查语音，见 checkSendContent）：不合格时一个目标都不发
    const int massContentCode = MessageProtocol::checkSendContent(req.msgtype, req.content);
    if (massContentCode != error_code_ok)
    {
        LOGW("mass send rejected by invalid content, userid: %d, msgtype: %d, targets: %u",
             m_userinfo.userid, req.msgtype, static_cast<unsigned>(req.targets.size()));
        send(cmd_mass_send, m_seq,
             MessageProtocol::buildSimpleResponse(massContentCode, "invalid message content"));
        return;
    }

    //机器人标记只能由服务端产生：客户端带来的 bot 字段一律删掉（docs/机器人方案.md §5.6）
    if (bot::StripBotMarker(req.content))
        LOGW("mass send: bot marker stripped from client content, userid: %d", m_userinfo.userid);
    //@ 的文字位置只对一个会话有意义，群发到多个会话时没有用处；也免得借群发绕过群的 @全体 权限
    if (MessageProtocol::stripContentMentions(req.content))
        LOGI("mass send: mentions stripped from client content, userid: %d", m_userinfo.userid);

    //企业级敏感词：所有目标收到的是同一份内容，投递之前检查一次，命中就整条拒绝、一个目标都不发。
    //群发请求不带引用。拦截记录不对应某一个会话，会话类型记 0，会话名写明目标个数。
    //会话名要写进库、显示在管理端，中文须用 u8"" 前缀（chatserver 工程未加 /utf-8，不带前缀时
    //MSVC 按 GBK 编码字符串常量）
    std::vector<std::string> sensitiveTexts;
    MessageProtocol::collectSensitiveTexts(req.msgtype, req.content, std::string(), sensitiveTexts);
    const std::string massSendTargetName =
        std::string(u8"群发助手（") + std::to_string(req.targets.size()) + u8" 个对象）";
    if (hitSensitive(sensitiveSourceOfMsgType(req.msgtype), 0, 0, massSendTargetName, sensitiveTexts))
    {
        send(cmd_mass_send, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_sensitive_word, "message contains sensitive word"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();
    std::vector<MassSendResult> results;
    for (size_t i = 0; i < req.targets.size(); ++i)
    {
        const MassSendTarget& t = req.targets[i];
        MassSendResult r;
        r.cid = t.cid;
        r.msgid = 0;

        //每条派生独立 clientmsgid，便于各目标分别去重
        std::string cmid = req.clientmsgid + "-" + std::to_string(i);
        //单聊：对端存在、非本人、未被对方拉黑才发；群聊：本人为群成员且未被禁言才发；
        //其余跳过（msgid 留 0）
        if (t.ctype == CHAT_TYPE_SINGLE)
        {
            int32_t targetId = static_cast<int32_t>(t.cid);
            if (targetId != m_userinfo.userid)
            {
                User peer;
                peer.userid = 0;
                userMgr.getUserInfoByUserId(targetId, peer);
                if (peer.userid != 0 && !userMgr.isInBlacklist(targetId, m_userinfo.userid))
                    r.msgid = deliverConversationMessage(CHAT_TYPE_SINGLE, t.cid,
                                                         req.msgtype, req.content, cmid);
            }
        }
        else if (t.ctype == CHAT_TYPE_GROUP)
        {
            //成员身份、群是否存在、禁言三项与普通群发同一套判定（2026-10-02 补上：原先只判
            //是否为群成员，被禁言的成员可以借群发往群里发消息）。不通过的群与其它被跳过的目标
            //一样把 msgid 留 0，不影响其余目标；被禁言时 checkGroupSpeak 已记日志
            int myrole = group_role_member;
            GroupRecord group;
            std::string reason;
            if (checkGroupSpeak(cmd_mass_send, t.cid, myrole, group, reason) == error_code_ok)
                r.msgid = deliverConversationMessage(CHAT_TYPE_GROUP, t.cid,
                                                     req.msgtype, req.content, cmid);
        }
        results.push_back(r);
    }

    send(cmd_mass_send, m_seq, MessageProtocol::buildMassSendResponse(0, "ok", results));
    LOGI("mass send ok, userid: %d, targets: %u",
         m_userinfo.userid, static_cast<unsigned>(req.targets.size()));
}

void ChatSession::onCreateGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    CreateGroupRequest req;
    if (!GroupProtocol::parseCreateGroupRequest(body, req))
    {
        send(cmd_create_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //企业级敏感词：查群名。群名为空时由服务端用成员昵称拼成自动群名，没有用户输入的文字；
    //空串由 checkTexts 跳过，不必单独判断。群还没有建出来，拦截记录的会话类型记 0
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(req.name);
    if (hitSensitive(chatserver::sensitive_source_group_info, 0, 0, std::string(), sensitiveTexts))
    {
        send(cmd_create_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_sensitive_word, "group name contains sensitive word"));
        return;
    }

    //帖子群要求创建者所属企业开着帖子群开关（docs/帖子群方案.md 4.7）；普通群不受影响
    if (req.grouptype == group_type_thread)
    {
        const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
        if (!Singleton<FeatureSwitchManager>::Instance().isEnabled(enterpriseId, kFeatureThreadGroup))
        {
            LOGI("create thread group rejected, feature off, userid: %d, enterpriseid: %lld", m_userinfo.userid,
                 (long long)enterpriseId);
            send(cmd_create_group, m_seq,
                 GroupProtocol::buildSimpleResponse(error_code_thread_disabled, "thread group disabled"));
            return;
        }
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();

    //分段计时（2026-10-08 用户反馈 166 人建群要等约 9 秒）：过滤成员、写库（含自动群名）、推送各花多久，
    //处理结束时记在 create group ok 那一行日志里，慢在哪一步能直接从日志看出来
    const std::chrono::steady_clock::time_point tStart = std::chrono::steady_clock::now();

    //过滤初始成员：去重、剔除创建者本人、剔除不存在的用户
    std::vector<int64_t> members;
    std::set<int64_t> seen;
    for (size_t i = 0; i < req.memberids.size(); ++i)
    {
        int64_t mid = req.memberids[i];
        if (mid == m_userinfo.userid || seen.count(mid) > 0)
            continue;

        User u;
        u.userid = 0;
        userMgr.getUserInfoByUserId(static_cast<int32_t>(mid), u);
        if (u.userid == 0)
            continue;

        seen.insert(mid);
        members.push_back(mid);
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const std::chrono::steady_clock::time_point tFiltered = std::chrono::steady_clock::now();
    int64_t groupid = groupMgr.createGroup(req.name, m_userinfo.userid, members, now, req.grouptype);
    const std::chrono::steady_clock::time_point tStored = std::chrono::steady_clock::now();
    if (groupid == 0)
    {
        LOGE("create group failed, owner: %d, members: %u, store took %lld ms", m_userinfo.userid,
             static_cast<unsigned>(members.size()),
             (long long)std::chrono::duration_cast<std::chrono::milliseconds>(tStored - tFiltered).count());
        send(cmd_create_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_create_group_fail, "create group failed"));
        return;
    }

    send(cmd_create_group, m_seq, GroupProtocol::buildCreateGroupResponse(0, "ok", groupid));

    //向群主与全部初始成员推 group_member_changed_notify（加入），各端据此刷新通讯录
    std::vector<int64_t> roster;
    roster.push_back(m_userinfo.userid);
    for (size_t i = 0; i < members.size(); ++i)
        roster.push_back(members[i]);
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        groupid, group_member_change_join, roster, m_userinfo.userid);
    for (size_t i = 0; i < roster.size(); ++i)
        sendToUser(static_cast<int32_t>(roster[i]), cmd_group_member_changed_notify, notify);
    const std::chrono::steady_clock::time_point tNotified = std::chrono::steady_clock::now();

    LOGI("create group ok, groupid: %lld, owner: %d, members: %u, grouptype: %d, filter %lld ms, store %lld ms, "
         "notify %lld ms",
         (long long)groupid, m_userinfo.userid, static_cast<unsigned>(members.size()), req.grouptype,
         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(tFiltered - tStart).count(),
         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(tStored - tFiltered).count(),
         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(tNotified - tStored).count());
}

void ChatSession::onDismissGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_dismiss_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_dismiss_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //仅群主可解散
    if (group.ownerid != m_userinfo.userid)
    {
        send(cmd_dismiss_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only group owner can dismiss"));
        return;
    }

    //删除前先取成员名单，用于解散后通知
    std::vector<GroupMemberRecord> members;
    groupMgr.getGroupMembers(groupid, members);

    if (!groupMgr.dismissGroup(groupid))
    {
        send(cmd_dismiss_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "dismiss group failed"));
        return;
    }

    //群没了，群里那些被钉住的消息也就无处可去，清掉它们的 Pin 行，免得 t_message_pin
    //里长期挂着一堆再也不会被查到的死行。<u>退群不做这件事</u> —— 人退了群，群里的 Pin
    //与他无关，但对留下的人仍然有效。
    Singleton<MessagePinManager>::Instance().removePinsForGroup(groupid);

    //该群的文件共享全部取消（原因「群已解散」），在发出解散通知之前完成：成员收到解散通知时，
    //卡片已经是「已失效」（docs/文件共享方案.md 6.1）
    handleShareGroupDismissed(groupid, m_userinfo.userid);

    //该群的群机器人软删除，企业机器人在该群的所选与关闭记录删除（docs/机器人方案.md §5.5）
    Singleton<BotManager>::Instance().onGroupDismissed(groupid, static_cast<int64_t>(time(NULL)) * 1000);

    send(cmd_dismiss_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //该群的群备忘全部删除，按解散前的成员名单推删除
    std::vector<MemoMemberRole> memoMembers;
    for (size_t i = 0; i < members.size(); ++i)
        memoMembers.push_back(MemoMemberRole(members[i].userid, members[i].role));
    handleMemoGroupDismissed(groupid, memoMembers);

    //帖子群：群没了，撤销全部「正在查看」登记（普通群里本来就没有登记）
    Singleton<ThreadWatchRegistry>::Instance().removeGroup(groupid);

    //向全体原成员推 group_info_changed_notify（群已解散）
    GroupRecord dismissed;
    dismissed.groupid = groupid;
    std::string notify = GroupProtocol::buildGroupInfoChangedNotify(
        dismissed, group_info_change_dismissed);
    for (size_t i = 0; i < members.size(); ++i)
        sendToUser(static_cast<int32_t>(members[i].userid), cmd_group_info_changed_notify, notify);

    LOGI("dismiss group ok, groupid: %lld, by: %d", (long long)groupid, m_userinfo.userid);
}

void ChatSession::onGetGroupInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_get_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //须为群成员才能查看群资料
    int myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_get_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    send(cmd_get_group_info, m_seq,
         GroupProtocol::buildGroupInfoResponse(0, "ok", group, myrole));
    LOGI("get group info ok, groupid: %lld, userid: %d", (long long)groupid, m_userinfo.userid);
}

void ChatSession::onGetGroupMembers(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_group_members, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();

    //须为群成员才能查看成员列表
    if (!groupMgr.isGroupMember(groupid, m_userinfo.userid))
    {
        send(cmd_get_group_members, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    std::vector<GroupMemberRecord> members;
    groupMgr.getGroupMembers(groupid, members);

    //从 UserManager 补全每个成员的昵称与头像
    UserManager& userMgr = Singleton<UserManager>::Instance();
    for (size_t i = 0; i < members.size(); ++i)
    {
        User u;
        u.userid = 0;
        userMgr.getUserInfoByUserId(static_cast<int32_t>(members[i].userid), u);
        if (u.userid != 0)
        {
            members[i].nickname   = u.nickname;
            members[i].facetype   = u.facetype;
            members[i].customface = u.customface;
        }
    }

    send(cmd_get_group_members, m_seq,
         GroupProtocol::buildGroupMembersResponse(0, "ok", groupid, members));
    LOGI("get group members ok, groupid: %lld, count: %u",
         (long long)groupid, static_cast<unsigned>(members.size()));
}

void ChatSession::onUpdateGroupInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    UpdateGroupInfoRequest req;
    if (!GroupProtocol::parseUpdateGroupInfoRequest(body, req))
    {
        send(cmd_update_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(req.groupid, group))
    {
        send(cmd_update_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //仅群主或管理员可改群资料
    int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_update_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can update"));
        return;
    }

    //企业级敏感词：只查本次提交、且与当前值不同的群名、简介、欢迎语、旧版公告字段（规则见
    //GroupProtocol::collectChangedGroupInfoTexts）。放在写库之前，被拦下时整次修改都不生效
    std::vector<std::string> sensitiveTexts;
    GroupProtocol::collectChangedGroupInfoTexts(req, group, sensitiveTexts);
    if (hitSensitive(chatserver::sensitive_source_group_info, CHAT_TYPE_GROUP, req.groupid, group.name,
                     sensitiveTexts))
    {
        send(cmd_update_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_sensitive_word, "group info contains sensitive word"));
        return;
    }

    if (!groupMgr.updateGroupInfo(req))
    {
        send(cmd_update_group_info, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "update group info failed"));
        return;
    }

    send(cmd_update_group_info, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //读回最新群资料，向全体成员推 group_info_changed_notify（资料变更）
    GroupRecord updated;
    if (groupMgr.getGroup(req.groupid, updated))
    {
        std::vector<GroupMemberRecord> members;
        groupMgr.getGroupMembers(req.groupid, members);
        std::string notify = GroupProtocol::buildGroupInfoChangedNotify(
            updated, group_info_change_updated);
        for (size_t i = 0; i < members.size(); ++i)
            sendToUser(static_cast<int32_t>(members[i].userid),
                       cmd_group_info_changed_notify, notify);
    }

    LOGI("update group info ok, groupid: %lld, by: %d",
         (long long)req.groupid, m_userinfo.userid);

    //本次改了群头像：customface 是头像图片的 fileid。修改者若是上传者，把该文件记录
    //标记为已完成。未设自定义头像时 customface 为空串，与其它非法取值一样由
    //parseFileIdText 过滤掉。
    int64_t avatarFileId = 0;
    if (req.has_customface && MessageProtocol::parseFileIdText(req.customface, avatarFileId))
        markReferencedFileCompleted(avatarFileId, "group avatar");
}

void ChatSession::notifyGroupMembers(int64_t groupid, uint16_t cmd, const std::string& body)
{
    std::vector<GroupMemberRecord> members;
    Singleton<GroupManager>::Instance().getGroupMembers(groupid, members);
    for (size_t i = 0; i < members.size(); ++i)
        sendToUser(static_cast<int32_t>(members[i].userid), cmd, body);
}

// ====================================================================
// 群公告
// ====================================================================

//把一条公告的发布人昵称补上。库里只存 f_publisher，而发布人可能早已退群，
//客户端的成员列表里未必找得到他 —— 昵称必须由服务端下发。
//取不到用户（已注销等）时留空，客户端会退回显示 userid。
static void fillAnnouncementPublisherName(GroupAnnouncementRecord& item)
{
    if (item.publisher <= 0)
        return;

    User u;
    if (Singleton<UserManager>::Instance().getUserInfoByUserId(
            static_cast<int32_t>(item.publisher), u))
    {
        item.publishername = u.nickname;
    }
}

void ChatSession::onGetGroupAnnouncements(const std::string& body,
                                          const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GetAnnouncementsRequest req;
    if (!GroupProtocol::parseGetAnnouncementsRequest(body, req))
    {
        send(cmd_get_group_announcements, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_get_group_announcements, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not group member"));
        return;
    }

    std::vector<GroupAnnouncementRecord> items;
    int total = 0;
    groupMgr.getAnnouncements(req.groupid, req.offset, req.count, items, &total);

    for (size_t i = 0; i < items.size(); ++i)
        fillAnnouncementPublisherName(items[i]);

    //offset / count 原样回显请求里的值（不是数据层夹过之后的），客户端才能拿它
    //与自己发出去的那次请求逐字比对、认出这是不是给自己的应答
    send(cmd_get_group_announcements, m_seq,
         GroupProtocol::buildAnnouncementsResponse(0, "ok", req.groupid, total,
                                                   req.offset, req.count, items));

    LOGI("get group announcements ok, groupid: %lld, userid: %d, count: %d, total: %d",
         (long long)req.groupid, m_userinfo.userid, static_cast<int>(items.size()), total);
}

void ChatSession::onPublishGroupAnnouncement(const std::string& body,
                                             const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    PublishAnnouncementRequest req;
    if (!GroupProtocol::parsePublishAnnouncementRequest(body, req))
    {
        send(cmd_publish_group_announcement, m_seq,
             GroupProtocol::buildPublishAnnouncementResponse(error_code_bad_request, "bad request", 0));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(req.groupid, group))
    {
        send(cmd_publish_group_announcement, m_seq,
             GroupProtocol::buildPublishAnnouncementResponse(error_code_group_not_exist, "group not exist", 0));
        return;
    }

    const int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_publish_group_announcement, m_seq,
             GroupProtocol::buildPublishAnnouncementResponse(
                 error_code_not_group_admin, "only owner or admin can publish announcement", 0));
        return;
    }

    //重新发布：按ID读回原件，把标题 / 正文 / 优先级原样再发一条。校验归属是必须的，
    //否则拿着别的群的公告ID就能把那条内容搬到本群来。
    const bool republish = (req.id > 0);
    if (republish)
    {
        GroupAnnouncementRecord origin;
        if (!groupMgr.getAnnouncement(req.id, origin) || origin.groupid != req.groupid)
        {
            send(cmd_publish_group_announcement, m_seq,
                 GroupProtocol::buildPublishAnnouncementResponse(
                     error_code_announcement_404, "announcement not exist", 0));
            return;
        }
        req.title    = origin.title;
        req.content  = origin.content;
        req.severity = origin.severity;
    }

    //企业级敏感词：查标题与正文。重新发布时查的是上面复制过来的最终内容：重新发布会生成一条
    //新公告、再次推送给全群，与原公告是否在加词之前发布无关
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(req.title);
    sensitiveTexts.push_back(req.content);
    if (hitSensitive(chatserver::sensitive_source_announcement, CHAT_TYPE_GROUP, req.groupid, group.name,
                     sensitiveTexts))
    {
        send(cmd_publish_group_announcement, m_seq,
             GroupProtocol::buildPublishAnnouncementResponse(
                 error_code_group_sensitive_word, "announcement contains sensitive word", 0));
        return;
    }

    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    const int64_t newId = groupMgr.publishAnnouncement(req.groupid, req.title, req.content,
                                                       req.severity, m_userinfo.userid, now);
    if (newId == 0)
    {
        send(cmd_publish_group_announcement, m_seq,
             GroupProtocol::buildPublishAnnouncementResponse(
                 error_code_server_internal, "publish announcement failed", 0));
        return;
    }

    send(cmd_publish_group_announcement, m_seq,
         GroupProtocol::buildPublishAnnouncementResponse(0, "ok", newId));

    //推送与系统消息用的是本端刚写进去的那份内容，不再回读一次 —— 字段都在手上，
    //回读只会多一次查询。
    GroupAnnouncementRecord item;
    item.id          = newId;
    item.groupid     = req.groupid;
    item.title       = req.title;
    item.content     = req.content;
    item.severity    = req.severity;
    item.publisher   = m_userinfo.userid;
    item.publishtime = now;
    fillAnnouncementPublisherName(item);

    notifyAnnouncementChanged(item, group_announcement_change_publish);
    const int64_t cardMsgId = sendAnnouncementCard(item, republish);

    LOGI("publish group announcement ok, groupid: %lld, id: %lld, by: %d, republish: %d, card msgid: %lld",
         (long long)req.groupid, (long long)newId, m_userinfo.userid, republish ? 1 : 0, (long long)cardMsgId);
}

void ChatSession::onDeleteGroupAnnouncement(const std::string& body,
                                            const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int64_t id = 0;
    if (!GroupProtocol::parseDeleteAnnouncementRequest(body, groupid, id))
    {
        send(cmd_delete_group_announcement, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_delete_group_announcement, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin,
                                                "only owner or admin can delete announcement"));
        return;
    }

    //必须先读回原件再删：既校验归属，也拿到推送要用的完整内容 —— 删掉之后就再也
    //读不出来了，而客户端要靠推送里的 id 把这一条从列表里摘掉。
    GroupAnnouncementRecord item;
    if (!groupMgr.getAnnouncement(id, item) || item.groupid != groupid)
    {
        send(cmd_delete_group_announcement, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_announcement_404, "announcement not exist"));
        return;
    }

    if (!groupMgr.deleteAnnouncement(groupid, id))
    {
        send(cmd_delete_group_announcement, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "delete announcement failed"));
        return;
    }

    send(cmd_delete_group_announcement, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //删的正是入群必读公告：按取消入群必读处理，待确认的人一并解除限制（docs/群公告接入方案.md §14.1）。
    //群资料变更随下面 notifyAnnouncementChanged 一起推出去
    GroupRecord groupNow;
    if (groupMgr.getGroup(groupid, groupNow) && groupNow.mustreadid == id)
    {
        std::vector<int64_t> cleared;
        if (groupMgr.setMustRead(groupid, 0, cleared) && !cleared.empty())
        {
            notifyGroupMembers(groupid, cmd_group_member_changed_notify,
                               GroupProtocol::buildGroupMemberChangedNotify(
                                   groupid, group_member_change_must_read, cleared, m_userinfo.userid));
        }
        LOGI("must-read announcement deleted, must-read cancelled, groupid: %lld, id: %lld, cleared: %u",
             (long long)groupid, (long long)id, static_cast<unsigned>(cleared.size()));
    }

    //公告卡片改写为已删除形态，改写结果随删除推送带给在线成员（docs/群公告接入方案.md §13.3）
    std::string cardContent;
    const int64_t cardMsgId = rewriteAnnouncementCardAsDeleted(item, cardContent);

    fillAnnouncementPublisherName(item);
    notifyAnnouncementChanged(item, group_announcement_change_delete, cardMsgId, cardContent);

    LOGI("delete group announcement ok, groupid: %lld, id: %lld, by: %d, card msgid: %lld",
         (long long)groupid, (long long)id, m_userinfo.userid, (long long)cardMsgId);
}

void ChatSession::notifyAnnouncementChanged(const GroupAnnouncementRecord& item, int changetype,
                                            int64_t cardMsgId, const std::string& cardContent)
{
    notifyGroupMembers(item.groupid, cmd_group_announcement_notify,
                       GroupProtocol::buildAnnouncementNotify(item, changetype, cardMsgId, cardContent));

    //公告快照四列刚被 refreshLatestAnnouncement 改过，读回最新群资料再推一条群资料
    //变更 —— 群资料对话框与群聊窗头部摘要行只认这条。
    GroupRecord updated;
    if (Singleton<GroupManager>::Instance().getGroup(item.groupid, updated))
    {
        notifyGroupMembers(item.groupid, cmd_group_info_changed_notify,
                           GroupProtocol::buildGroupInfoChangedNotify(updated, group_info_change_updated));
    }
}

int64_t ChatSession::sendAnnouncementCard(const GroupAnnouncementRecord& item, bool republish)
{
    const std::string content = GroupProtocol::buildAnnouncementCardContent(item, republish);

    //落库前套上 buildStoredContent 的外壳，与系统消息、共享卡片的落库口径一致；卡片不带 @ 与引用
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");

    //clientmsgid 由群 id 与公告 id 拼成：重新发布得到的是新的公告 id，各自成一张卡片；
    //删除公告时也靠它找回这张卡片
    const std::string clientMsgId = GroupProtocol::announcementCardClientMsgId(item.groupid, item.id);

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    const int64_t msgId = msgMgr.saveMessage(clientMsgId, CHAT_TYPE_GROUP,
                                             static_cast<int32_t>(item.publisher),
                                             static_cast<int32_t>(item.groupid),
                                             message_type_announcement, stored, item.publishtime);
    if (msgId == 0)
    {
        LOGE("save announcement card failed, groupid: %lld, announcement id: %lld",
             (long long)item.groupid, (long long)item.id);
        return 0;
    }

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype       = CHAT_TYPE_GROUP;
    rec.from        = item.publisher;
    rec.to          = item.groupid;
    rec.msgtype     = message_type_announcement;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = item.publishtime;
    rec.recalled    = false;
    //推给全体成员，包括发布人本人的各台设备：发布人自己的群聊窗里同样要出现这张卡片
    notifyGroupMembers(item.groupid, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, item.groupid));

    LOGI("announcement card sent, groupid: %lld, announcement id: %lld, msgid: %lld, republish: %d",
         (long long)item.groupid, (long long)item.id, (long long)msgId, republish ? 1 : 0);
    return msgId;
}

int64_t ChatSession::rewriteAnnouncementCardAsDeleted(const GroupAnnouncementRecord& item, std::string& outContent)
{
    outContent.clear();
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();

    //本功能上线前发布的公告没有卡片（当时插的是系统提示），查不到属正常
    const std::string clientMsgId = GroupProtocol::announcementCardClientMsgId(item.groupid, item.id);
    const int64_t cardMsgId = msgMgr.getMsgIdByClientId(static_cast<int32_t>(item.publisher), clientMsgId);
    if (cardMsgId <= 0)
    {
        LOGI("announcement card not found, nothing to rewrite, groupid: %lld, announcement id: %lld",
             (long long)item.groupid, (long long)item.id);
        return 0;
    }

    MessageRecord card;
    if (!msgMgr.getMessageById(cardMsgId, card) || card.msgtype != message_type_announcement)
    {
        LOGE("announcement card unreadable, groupid: %lld, announcement id: %lld, msgid: %lld",
             (long long)item.groupid, (long long)item.id, (long long)cardMsgId);
        return 0;
    }

    std::string deleted;
    if (!GroupProtocol::buildDeletedAnnouncementCardContent(card.content, deleted))
    {
        LOGE("announcement card content is not an object, msgid: %lld", (long long)cardMsgId);
        return 0;
    }

    //改写落库时同样套上外壳；updateMessageContent 会按新内容重算搜索文本，正文从此搜不到
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(deleted, noMentions, false, "");
    if (!msgMgr.updateMessageContent(cardMsgId, stored))
    {
        LOGE("rewrite announcement card failed, msgid: %lld", (long long)cardMsgId);
        return 0;
    }

    outContent = deleted;
    LOGI("announcement card rewritten as deleted, groupid: %lld, announcement id: %lld, msgid: %lld",
         (long long)item.groupid, (long long)item.id, (long long)cardMsgId);
    return cardMsgId;
}

int64_t ChatSession::sendGroupSystemMessage(int64_t groupid, const std::string& content,
                                            const std::string& clientMsgId)
{
    //系统消息的 content 与文本消息同构（带 text 字段），客户端的文本渲染路径可以直接复用。
    //另带的 tipkind / tipargs（多语言第 5 期）与接龙提示的 cardmsgid，不认识的客户端照常按 text 显示。

    //落库前还要再套一层 buildStoredContent 的外壳（content/mentions/mentionall/quote），
    //与单聊/群聊/转发那几条落库路径口径一致。读回时 parseStoredContent 只认最外层的
    //content 字段，若把裸 content 直接存进去，重新登录同步会话列表时摘要会退化成
    //"[消息]"、翻历史时这条系统提示的正文也会是空的。系统消息不带 @ 与引用，故
    //mentions 传空、mentionall 传 false、quote 传空串。
    std::vector<int64_t> noMentions;
    const std::string storedContent =
        MessageProtocol::buildStoredContent(content, noMentions, false, "");

    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    const int64_t msgId = msgMgr.saveMessage(clientMsgId, CHAT_TYPE_GROUP,
                                             kSystemMessageSenderId,
                                             static_cast<int32_t>(groupid),
                                             message_type_system, storedContent, now);
    if (msgId == 0)
    {
        LOGE("save group system message failed, groupid: %lld, clientmsgid: %s",
             (long long)groupid, clientMsgId.c_str());
        return 0;
    }

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype       = CHAT_TYPE_GROUP;
    rec.from        = kSystemMessageSenderId;
    rec.to          = groupid;
    rec.msgtype     = message_type_system;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = now;
    rec.recalled    = false;
    notifyGroupMembers(groupid, cmd_message_notify,
                       MessageProtocol::buildMessageNotify(rec, groupid));

    LOGI("group system message sent, groupid: %lld, msgid: %lld, clientmsgid: %s",
         (long long)groupid, (long long)msgId, clientMsgId.c_str());
    return msgId;
}

void ChatSession::notifyGroupAdmins(int64_t groupid, uint16_t cmd, const std::string& body)
{
    std::vector<GroupMemberRecord> members;
    Singleton<GroupManager>::Instance().getGroupMembers(groupid, members);
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].role >= group_role_admin)
            sendToUser(static_cast<int32_t>(members[i].userid), cmd, body);
    }
}

void ChatSession::sendGroupWelcomeMessage(const GroupRecord& group,
                                          const std::vector<int64_t>& newMembers)
{
    //群没开欢迎语、或压根没设正文、或本次一个人都没加进来 —— 什么都不做
    if (group.welcomeon == 0 || group.welcometext.empty() || newMembers.empty())
        return;

    //新成员的昵称拼在欢迎语前面，形如"张三、李四 欢迎加入本群"（GroupProtocol::buildWelcomeTip）。
    //取不到昵称的（用户已注销等）跳过，不让空名字污染这句话。
    UserManager& userMgr = Singleton<UserManager>::Instance();
    std::vector<std::string> names;
    for (size_t i = 0; i < newMembers.size(); ++i)
    {
        User u;
        if (!userMgr.getUserInfoByUserId(static_cast<int32_t>(newMembers[i]), u))
            continue;
        if (u.nickname.empty())
            continue;
        names.push_back(u.nickname);
    }
    const std::string content = GroupProtocol::buildWelcomeTipContent(names, group.welcometext);

    //占位的客户端消息ID带上群ID与时间戳：欢迎语没有可用的业务ID
    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    const std::string clientMsgId = "sys-welcome-" + std::to_string(group.groupid)
                                    + "-" + std::to_string(now);
    const int64_t msgId = sendGroupSystemMessage(group.groupid, content, clientMsgId);
    if (msgId != 0)
    {
        LOGI("group welcome message sent, groupid: %lld, msgid: %lld, newmembers: %d",
             (long long)group.groupid, (long long)msgId, static_cast<int>(newMembers.size()));
    }
}

void ChatSession::completeGroupJoin(const GroupRecord& group, const std::vector<int64_t>& newMembers,
                                    int64_t inviterId, int64_t operatorId)
{
    if (newMembers.empty())
        return;

    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    //1) 已读位点推到本群最新一条：新成员原先没有会话行，位点按 0 处理，入群前别人发的消息
    //   会全部算成未读（docs/邀请入群方案.md §2.5）。失败只影响未读数，不影响入群本身
    if (!Singleton<MessageManager>::Instance().initNewMembersReadPos(group.groupid, newMembers, now))
    {
        LOGE("init new members read position failed, groupid: %lld, newmembers: %u",
             (long long)group.groupid, static_cast<unsigned>(newMembers.size()));
    }

    //1-1) 本群设了入群必读：新成员标为待确认，确认前不能发言（docs/群公告接入方案.md §14）。须在推成员变更之前标好，
    //   各端收到推送后重拉成员列表时才看得到标记
    if (group.mustreadid > 0 && !Singleton<GroupManager>::Instance().markMustReadPending(group.groupid, newMembers))
    {
        LOGE("mark must-read pending failed, groupid: %lld, newmembers: %u",
             (long long)group.groupid, static_cast<unsigned>(newMembers.size()));
    }

    //2) 向全体成员（含新成员）推 member_changed_notify（加入）
    const std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        group.groupid, group_member_change_join, newMembers, operatorId);
    notifyGroupMembers(group.groupid, cmd_group_member_changed_notify, notify);

    //3) 自动命名的群随成员变化重算群名，群名改变时向全群推送资料变更
    Singleton<ChatServer>::Instance().refreshAutoGroupNameAndNotify(group.groupid);

    //4) 「有人入群」系统提示：用账号昵称（新成员此时还没有群名片），取不到昵称的只计人数
    UserManager& userMgr = Singleton<UserManager>::Instance();
    std::vector<JoinTipMember> tipMembers;
    for (size_t i = 0; i < newMembers.size(); ++i)
    {
        User u;
        u.userid = 0;
        std::string nickname;
        if (userMgr.getUserInfoByUserId(static_cast<int32_t>(newMembers[i]), u))
            nickname = u.nickname;
        tipMembers.push_back(JoinTipMember(newMembers[i], nickname));
    }
    std::string inviterNickname;
    if (inviterId != 0)
    {
        User inviter;
        inviter.userid = 0;
        if (userMgr.getUserInfoByUserId(static_cast<int32_t>(inviterId), inviter))
            inviterNickname = inviter.nickname;
    }
    //占位消息ID带上群、第一位新成员与时间戳：同一批人退了又进时各成一条
    const std::string clientMsgId = "sys-join-" + std::to_string(group.groupid) + "-"
                                    + std::to_string(newMembers[0]) + "-" + std::to_string(now);
    const int64_t tipMsgId = sendGroupSystemMessage(
        group.groupid, GroupProtocol::buildMemberJoinTipContent(inviterId, inviterNickname, tipMembers), clientMsgId);

    //5) 入群欢迎语
    sendGroupWelcomeMessage(group, newMembers);

    //6) 新成员能看到本群提醒全体成员的群备忘
    handleMemoMembersJoinedGroup(group.groupid, newMembers);

    //7) 入群必读卡片：每位新成员各一张，只有他本人看得到
    if (group.mustreadid > 0)
        sendMustReadCards(group, newMembers, now);

    //8) 机器人的入群规则（docs/机器人方案.md §3.5 T3）
    Singleton<BotEngine>::Instance().onMembersJoined(
        group.groupid, Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid), newMembers);

    LOGI("group join completed, groupid: %lld, newmembers: %u, inviter: %lld, operator: %lld, tipmsgid: %lld",
         (long long)group.groupid, static_cast<unsigned>(newMembers.size()), (long long)inviterId,
         (long long)operatorId, (long long)tipMsgId);
}

void ChatSession::sendMustReadCards(const GroupRecord& group, const std::vector<int64_t>& newMembers, int64_t nowMs)
{
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupAnnouncementRecord item;
    if (!groupMgr.getAnnouncement(group.mustreadid, item) || item.groupid != group.groupid)
    {
        LOGE("send must-read cards: announcement not found, groupid: %lld, id: %lld",
             (long long)group.groupid, (long long)group.mustreadid);
        return;
    }
    fillAnnouncementPublisherName(item);
    const std::string content = GroupProtocol::buildMustReadCardContent(item);
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    for (size_t i = 0; i < newMembers.size(); ++i)
    {
        //以系统的名义写入（发送者 0），可见范围只登记这位新成员：别人看不到，系统也没有设备
        std::vector<int32_t> audience(1, static_cast<int32_t>(newMembers[i]));
        const std::string clientMsgId = "mustread-" + std::to_string(group.groupid) + "-"
                                        + std::to_string(newMembers[i]) + "-" + std::to_string(nowMs);
        const int64_t msgId = msgMgr.saveMessage(clientMsgId, CHAT_TYPE_GROUP, kSystemMessageSenderId,
                                                 static_cast<int32_t>(group.groupid), message_type_announcement,
                                                 stored, nowMs, &audience);
        if (msgId == 0)
        {
            LOGE("save must-read card failed, groupid: %lld, userid: %lld",
                 (long long)group.groupid, (long long)newMembers[i]);
            continue;
        }
        MessageRecord rec;
        rec.msgid       = msgId;
        rec.clientmsgid = clientMsgId;
        rec.ctype       = CHAT_TYPE_GROUP;
        rec.from        = kSystemMessageSenderId;
        rec.to          = group.groupid;
        rec.msgtype     = message_type_announcement;
        rec.content     = content;
        rec.mentionall  = false;
        rec.timestamp   = nowMs;
        rec.recalled    = false;
        sendToUser(static_cast<int32_t>(newMembers[i]), cmd_message_notify,
                   MessageProtocol::buildMessageNotify(rec, group.groupid));
        LOGI("must-read card sent, groupid: %lld, userid: %lld, announcement: %lld, msgid: %lld",
             (long long)group.groupid, (long long)newMembers[i], (long long)item.id, (long long)msgId);
    }
}

void ChatSession::onSetMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int64_t announcementId = 0;
    if (!GroupProtocol::parseSetMustReadRequest(body, groupid, announcementId))
    {
        send(cmd_set_must_read, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_set_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    if (myrole < group_role_admin)
    {
        send(cmd_set_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can set must-read"));
        return;
    }

    //指定的公告须是本群的
    if (announcementId > 0)
    {
        GroupAnnouncementRecord item;
        if (!groupMgr.getAnnouncement(announcementId, item) || item.groupid != groupid)
        {
            send(cmd_set_must_read, m_seq,
                 GroupProtocol::buildSimpleResponse(error_code_announcement_404, "announcement not exist"));
            return;
        }
    }

    std::vector<int64_t> cleared;
    if (!groupMgr.setMustRead(groupid, announcementId, cleared))
    {
        send(cmd_set_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "set must-read failed"));
        return;
    }
    send(cmd_set_must_read, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //群资料带着新的 mustreadid 推给全群；取消时被解除限制的人另推成员变更，他们的输入区与成员标记随之恢复
    GroupRecord updated;
    if (groupMgr.getGroup(groupid, updated))
    {
        notifyGroupMembers(groupid, cmd_group_info_changed_notify,
                           GroupProtocol::buildGroupInfoChangedNotify(updated, group_info_change_updated));
    }
    if (!cleared.empty())
    {
        notifyGroupMembers(groupid, cmd_group_member_changed_notify,
                           GroupProtocol::buildGroupMemberChangedNotify(
                               groupid, group_member_change_must_read, cleared, m_userinfo.userid));
    }
    LOGI("set must-read ok, groupid: %lld, announcement: %lld, by: %d, cleared: %u",
         (long long)groupid, (long long)announcementId, m_userinfo.userid, static_cast<unsigned>(cleared.size()));
}

void ChatSession::onConfirmMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_confirm_must_read, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    if (!groupMgr.isGroupMember(groupid, m_userinfo.userid))
    {
        send(cmd_confirm_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    std::vector<int64_t> self(1, m_userinfo.userid);
    std::vector<int64_t> cleared;
    if (!groupMgr.clearMustReadPending(groupid, self, cleared))
    {
        send(cmd_confirm_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "confirm must-read failed"));
        return;
    }
    //本来就不是待确认的（重复点、别处已确认）也回成功，界面照样恢复
    send(cmd_confirm_must_read, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));
    if (!cleared.empty())
    {
        notifyGroupMembers(groupid, cmd_group_member_changed_notify,
                           GroupProtocol::buildGroupMemberChangedNotify(
                               groupid, group_member_change_must_read, cleared, m_userinfo.userid));
    }
    LOGI("confirm must-read ok, groupid: %lld, userid: %d, changed: %u",
         (long long)groupid, m_userinfo.userid, static_cast<unsigned>(cleared.size()));
}

void ChatSession::onReleaseMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GroupMembersOpRequest req;
    if (!GroupProtocol::parseGroupMembersOpRequest(body, req) || req.userids.empty())
    {
        send(cmd_release_must_read, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_release_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    if (myrole < group_role_admin)
    {
        send(cmd_release_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can release must-read"));
        return;
    }
    std::vector<int64_t> cleared;
    if (!groupMgr.clearMustReadPending(req.groupid, req.userids, cleared))
    {
        send(cmd_release_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "release must-read failed"));
        return;
    }
    send(cmd_release_must_read, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));
    if (!cleared.empty())
    {
        notifyGroupMembers(req.groupid, cmd_group_member_changed_notify,
                           GroupProtocol::buildGroupMemberChangedNotify(
                               req.groupid, group_member_change_must_read, cleared, m_userinfo.userid));
    }
    LOGI("release must-read ok, groupid: %lld, by: %d, requested: %u, changed: %u",
         (long long)req.groupid, m_userinfo.userid, static_cast<unsigned>(req.userids.size()),
         static_cast<unsigned>(cleared.size()));
}

void ChatSession::onGetMustRead(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_must_read, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    int role = group_role_member;
    int muted = 0;
    int mustRead = 0;
    if (!groupMgr.getMemberRoleAndMuted(groupid, m_userinfo.userid, role, muted, NULL, &mustRead))
    {
        send(cmd_get_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_get_must_read, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }
    GroupAnnouncementRecord item;
    const GroupAnnouncementRecord* pItem = NULL;
    if (group.mustreadid > 0 && groupMgr.getAnnouncement(group.mustreadid, item) && item.groupid == groupid)
    {
        fillAnnouncementPublisherName(item);
        pItem = &item;
    }
    send(cmd_get_must_read, m_seq,
         GroupProtocol::buildGetMustReadResponse(0, "ok", groupid, group.mustreadid, mustRead != 0, pItem));
    LOGI("get must-read ok, groupid: %lld, userid: %d, mustreadid: %lld, pending: %d",
         (long long)groupid, m_userinfo.userid, (long long)group.mustreadid, mustRead);
}

void ChatSession::onGetGroupQa(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_group_qa, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_get_group_qa, m_seq, GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }
    if (!groupMgr.isGroupMember(groupid, m_userinfo.userid))
    {
        send(cmd_get_group_qa, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    GroupQaManager& qaMgr = Singleton<GroupQaManager>::Instance();
    std::vector<GroupQaRecord> groupItems;
    std::vector<GroupQaRecord> entItems;
    qaMgr.listGroupQa(groupid, groupItems);
    //群所属企业按群主所属企业认定（t_group 没有企业列，docs/群问答方案.md §1）
    const int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(group.ownerid);
    qaMgr.listEnterpriseQaForGroup(enterpriseId, groupid, entItems);

    //本群问答的修改人显示昵称
    UserManager& userMgr = Singleton<UserManager>::Instance();
    for (size_t i = 0; i < groupItems.size(); ++i)
    {
        User u;
        u.userid = 0;
        if (userMgr.getUserInfoByUserId(static_cast<int32_t>(groupItems[i].editor), u))
            groupItems[i].editorname = u.nickname;
    }
    const std::vector<GroupQaRecord> merged = GroupQaProtocol::mergeForGroup(groupItems, entItems);
    send(cmd_get_group_qa, m_seq, GroupQaProtocol::buildGetResponse(0, "ok", groupid, merged));
    LOGI("get group qa ok, groupid: %lld, userid: %d, group items: %u, enterprise items: %u, merged: %u",
         (long long)groupid, m_userinfo.userid, static_cast<unsigned>(groupItems.size()),
         static_cast<unsigned>(entItems.size()), static_cast<unsigned>(merged.size()));
}

void ChatSession::onSaveGroupQa(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SaveGroupQaRequest req;
    if (!GroupQaProtocol::parseSaveRequest(body, req))
    {
        send(cmd_save_group_qa, m_seq, GroupQaProtocol::buildSaveResponse(error_code_bad_request, "bad request", 0, 0));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_save_group_qa, m_seq,
             GroupQaProtocol::buildSaveResponse(error_code_not_group_member, "not a group member", req.groupid, 0));
        return;
    }
    if (myrole < group_role_admin)
    {
        send(cmd_save_group_qa, m_seq,
             GroupQaProtocol::buildSaveResponse(error_code_not_group_admin, "only owner or admin can edit qa", req.groupid, 0));
        return;
    }
    if (GroupQaProtocol::checkContent(req.question, req.answer) != group_qa_check_ok)
    {
        send(cmd_save_group_qa, m_seq,
             GroupQaProtocol::buildSaveResponse(error_code_bad_request, "invalid question or answer", req.groupid, 0));
        return;
    }
    GroupQaManager& qaMgr = Singleton<GroupQaManager>::Instance();
    if (req.id > 0)
    {
        GroupQaRecord exist;
        if (!qaMgr.getGroupQa(req.id, exist) || exist.groupid != req.groupid)
        {
            send(cmd_save_group_qa, m_seq,
                 GroupQaProtocol::buildSaveResponse(error_code_group_qa_not_exist, "qa not exist", req.groupid, 0));
            return;
        }
    }
    else if (qaMgr.countGroupQa(req.groupid) >= kGroupQaPerGroupMax)
    {
        send(cmd_save_group_qa, m_seq,
             GroupQaProtocol::buildSaveResponse(error_code_group_qa_limit, "qa limit reached", req.groupid, 0));
        return;
    }

    //企业级敏感词：问题与答案都查
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(req.question);
    sensitiveTexts.push_back(req.answer);
    if (hitSensitive(chatserver::sensitive_source_group_qa, CHAT_TYPE_GROUP, req.groupid, std::string(), sensitiveTexts))
    {
        send(cmd_save_group_qa, m_seq,
             GroupQaProtocol::buildSaveResponse(error_code_group_sensitive_word, "qa contains sensitive word", req.groupid, 0));
        return;
    }

    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    int64_t savedId = req.id;
    bool ok = false;
    if (req.id > 0)
        ok = qaMgr.updateGroupQa(req.groupid, req.id, req.question, req.answer, m_userinfo.userid, now);
    else
        ok = qaMgr.addGroupQa(req.groupid, req.question, req.answer, m_userinfo.userid, now, savedId);
    if (!ok)
    {
        send(cmd_save_group_qa, m_seq,
             GroupQaProtocol::buildSaveResponse(error_code_server_internal, "save qa failed", req.groupid, 0));
        return;
    }
    send(cmd_save_group_qa, m_seq, GroupQaProtocol::buildSaveResponse(0, "ok", req.groupid, savedId));
    notifyGroupMembers(req.groupid, cmd_group_qa_changed_notify, GroupQaProtocol::buildChangedNotify(req.groupid));
    LOGI("save group qa ok, groupid: %lld, id: %lld, by: %d, new: %d",
         (long long)req.groupid, (long long)savedId, m_userinfo.userid, req.id > 0 ? 0 : 1);
}

void ChatSession::onDeleteGroupQa(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int64_t id = 0;
    if (!GroupQaProtocol::parseDeleteRequest(body, groupid, id))
    {
        send(cmd_delete_group_qa, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_delete_group_qa, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    if (myrole < group_role_admin)
    {
        send(cmd_delete_group_qa, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can delete qa"));
        return;
    }
    GroupQaManager& qaMgr = Singleton<GroupQaManager>::Instance();
    GroupQaRecord exist;
    if (!qaMgr.getGroupQa(id, exist) || exist.groupid != groupid)
    {
        send(cmd_delete_group_qa, m_seq, GroupProtocol::buildSimpleResponse(error_code_group_qa_not_exist, "qa not exist"));
        return;
    }
    if (!qaMgr.deleteGroupQa(groupid, id))
    {
        send(cmd_delete_group_qa, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "delete qa failed"));
        return;
    }
    send(cmd_delete_group_qa, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));
    notifyGroupMembers(groupid, cmd_group_qa_changed_notify, GroupQaProtocol::buildChangedNotify(groupid));
    LOGI("delete group qa ok, groupid: %lld, id: %lld, by: %d", (long long)groupid, (long long)id, m_userinfo.userid);
}

void ChatSession::onCreateGroupLink(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int days = kGroupLinkDaysSeven;
    if (!GroupLinkProtocol::parseCreateRequest(body, groupid, days))
    {
        send(cmd_create_group_link, m_seq,
             GroupLinkProtocol::buildCreateResponse(error_code_bad_request, "bad request", NULL));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_create_group_link, m_seq,
             GroupLinkProtocol::buildCreateResponse(error_code_group_not_exist, "group not exist", NULL));
        return;
    }
    if (!groupMgr.isGroupMember(groupid, m_userinfo.userid))
    {
        send(cmd_create_group_link, m_seq,
             GroupLinkProtocol::buildCreateResponse(error_code_not_group_member, "not a group member", NULL));
        return;
    }
    //禁止加入的群：链接点了也进不来，干脆不让生成
    if (group.joinmode == group_join_forbid)
    {
        send(cmd_create_group_link, m_seq,
             GroupLinkProtocol::buildCreateResponse(error_code_permission_denied, "group forbids joining", NULL));
        return;
    }
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    GroupLinkRecord rec;
    rec.groupid = groupid;
    rec.creator = m_userinfo.userid;
    rec.enterpriseid = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    rec.expiretime = GroupLinkProtocol::computeExpire(now, days);
    if (!Singleton<GroupLinkManager>::Instance().create(rec, now))
    {
        send(cmd_create_group_link, m_seq,
             GroupLinkProtocol::buildCreateResponse(error_code_server_internal, "create link failed", NULL));
        return;
    }
    rec.creatorname = m_userinfo.nickname;
    send(cmd_create_group_link, m_seq, GroupLinkProtocol::buildCreateResponse(0, "ok", &rec));
    LOGI("create group link ok, groupid: %lld, id: %lld, by: %d, days: %d",
         (long long)groupid, (long long)rec.id, m_userinfo.userid, days);
}

void ChatSession::onGetGroupLinks(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_group_links, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    const int myrole = Singleton<GroupManager>::Instance().getMemberRole(groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_get_group_links, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    //群主与管理员看全部，普通成员只看本人生成的
    const int64_t creatorFilter = (myrole >= group_role_admin) ? 0 : m_userinfo.userid;
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::vector<GroupLinkRecord> links;
    if (!Singleton<GroupLinkManager>::Instance().listUsable(groupid, creatorFilter, now, links))
    {
        send(cmd_get_group_links, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "list links failed"));
        return;
    }
    UserManager& userMgr = Singleton<UserManager>::Instance();
    for (size_t i = 0; i < links.size(); ++i)
    {
        User u;
        u.userid = 0;
        if (userMgr.getUserInfoByUserId(static_cast<int32_t>(links[i].creator), u))
            links[i].creatorname = u.nickname;
    }
    send(cmd_get_group_links, m_seq, GroupLinkProtocol::buildListResponse(0, "ok", groupid, links));
    LOGI("get group links ok, groupid: %lld, userid: %d, count: %u",
         (long long)groupid, m_userinfo.userid, static_cast<unsigned>(links.size()));
}

void ChatSession::onRevokeGroupLink(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int64_t id = 0;
    if (!GroupLinkProtocol::parseRevokeRequest(body, groupid, id))
    {
        send(cmd_revoke_group_link, m_seq, GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }
    const int myrole = Singleton<GroupManager>::Instance().getMemberRole(groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_revoke_group_link, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }
    GroupLinkManager& linkMgr = Singleton<GroupLinkManager>::Instance();
    GroupLinkRecord link;
    if (!linkMgr.getById(id, link) || link.groupid != groupid)
    {
        send(cmd_revoke_group_link, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_link_invalid, "link not exist"));
        return;
    }
    if (myrole < group_role_admin && link.creator != m_userinfo.userid)
    {
        send(cmd_revoke_group_link, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_permission_denied, "only owner, admin or creator can revoke"));
        return;
    }
    if (!linkMgr.revoke(groupid, id))
    {
        send(cmd_revoke_group_link, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "revoke link failed"));
        return;
    }
    send(cmd_revoke_group_link, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));
    LOGI("revoke group link ok, groupid: %lld, id: %lld, by: %d", (long long)groupid, (long long)id, m_userinfo.userid);
}

int ChatSession::resolveGroupLink(const std::string& code, GroupLinkRecord& link, GroupRecord& group)
{
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    if (!Singleton<GroupLinkManager>::Instance().getByCode(code, link) || !GroupLinkProtocol::isUsable(link, now))
        return error_code_group_link_invalid;
    if (!Singleton<GroupManager>::Instance().getGroup(link.groupid, group))
        return error_code_group_link_invalid;
    //只对生成者所在企业的同事有效
    const int64_t myEnterprise = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (link.enterpriseid != 0 && myEnterprise != link.enterpriseid)
        return error_code_group_link_enterprise;
    return error_code_ok;
}

void ChatSession::onGetGroupLinkInfo(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string code;
    if (!GroupLinkProtocol::parseCodeRequest(body, code))
    {
        send(cmd_get_group_link_info, m_seq,
             GroupLinkProtocol::buildInfoResponse(error_code_group_link_invalid, "invalid link", NULL));
        return;
    }
    GroupLinkRecord link;
    GroupRecord group;
    const int rc = resolveGroupLink(code, link, group);
    if (rc != error_code_ok)
    {
        LOGI("get group link info rejected, userid: %d, code: %d", m_userinfo.userid, rc);
        send(cmd_get_group_link_info, m_seq, GroupLinkProtocol::buildInfoResponse(rc, "link not usable", NULL));
        return;
    }
    GroupLinkInfo info;
    info.groupid     = group.groupid;
    info.name        = group.name;
    info.facetype    = group.facetype;
    info.customface  = group.customface;
    info.membercount = group.membercount;
    info.joinmode    = group.joinmode;
    info.member      = Singleton<GroupManager>::Instance().isGroupMember(group.groupid, m_userinfo.userid);
    send(cmd_get_group_link_info, m_seq, GroupLinkProtocol::buildInfoResponse(0, "ok", &info));
    LOGI("get group link info ok, userid: %d, groupid: %lld, member: %d",
         m_userinfo.userid, (long long)group.groupid, info.member ? 1 : 0);
}

void ChatSession::onJoinGroupByLink(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string code;
    if (!GroupLinkProtocol::parseCodeRequest(body, code))
    {
        send(cmd_join_group_by_link, m_seq,
             GroupLinkProtocol::buildJoinResponse(error_code_group_link_invalid, "invalid link", 0, false));
        return;
    }
    GroupLinkRecord link;
    GroupRecord group;
    const int rc = resolveGroupLink(code, link, group);
    if (rc != error_code_ok)
    {
        LOGI("join group by link rejected, userid: %d, code: %d", m_userinfo.userid, rc);
        send(cmd_join_group_by_link, m_seq, GroupLinkProtocol::buildJoinResponse(rc, "link not usable", 0, false));
        return;
    }
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    //已经在群里：当作入群成功，客户端直接打开群聊窗
    if (groupMgr.isGroupMember(group.groupid, m_userinfo.userid))
    {
        send(cmd_join_group_by_link, m_seq, GroupLinkProtocol::buildJoinResponse(0, "ok", group.groupid, true));
        return;
    }
    if (group.joinmode == group_join_forbid)
    {
        send(cmd_join_group_by_link, m_seq,
             GroupLinkProtocol::buildJoinResponse(error_code_permission_denied, "group forbids joining", group.groupid, false));
        return;
    }
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::vector<int64_t> self(1, m_userinfo.userid);
    if (group.joinmode == group_join_free)
    {
        if (!groupMgr.addMembers(group.groupid, self, now))
        {
            send(cmd_join_group_by_link, m_seq,
                 GroupLinkProtocol::buildJoinResponse(error_code_server_internal, "join failed", group.groupid, false));
            return;
        }
        send(cmd_join_group_by_link, m_seq, GroupLinkProtocol::buildJoinResponse(0, "ok", group.groupid, true));
        //入群提示里的邀请人记链接生成者（docs/邀请入群方案.md §12.1）
        completeGroupJoin(group, self, link.creator, m_userinfo.userid);
        LOGI("join group by link ok (free), groupid: %lld, userid: %d, link: %lld",
             (long long)group.groupid, m_userinfo.userid, (long long)link.id);
        return;
    }

    //需审批：落一条加群申请，来源记「扫码加群」，推荐人记链接生成者
    const int64_t requestId = groupMgr.addJoinRequest(group.groupid, m_userinfo.userid, std::string(), now,
                                                      group_join_source_qrcode, link.creator, kInviteHistoryLimitAll);
    if (requestId <= 0)
    {
        send(cmd_join_group_by_link, m_seq,
             GroupLinkProtocol::buildJoinResponse(error_code_server_internal, "join request failed", group.groupid, false));
        return;
    }
    send(cmd_join_group_by_link, m_seq, GroupLinkProtocol::buildJoinResponse(0, "ok", group.groupid, false));
    GroupJoinRequestRecord item;
    if (groupMgr.getJoinRequestById(requestId, item))
    {
        User self;
        self.userid = 0;
        Singleton<UserManager>::Instance().getUserInfoByUserId(m_userinfo.userid, self);
        item.nickname   = self.nickname;
        item.facetype   = self.facetype;
        item.customface = self.customface;
        notifyGroupAdmins(group.groupid, cmd_join_group_request_notify, GroupProtocol::buildJoinGroupRequestNotify(item));
    }
    LOGI("join group by link submitted as request, groupid: %lld, userid: %d, link: %lld, request: %lld",
         (long long)group.groupid, m_userinfo.userid, (long long)link.id, (long long)requestId);
}

void ChatSession::onInviteToGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GroupMembersOpRequest req;
    if (!GroupProtocol::parseGroupMembersOpRequest(body, req))
    {
        send(cmd_invite_to_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(req.groupid, group))
    {
        send(cmd_invite_to_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //邀请人须为群成员
    if (!groupMgr.isGroupMember(req.groupid, m_userinfo.userid))
    {
        send(cmd_invite_to_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();

    //过滤被邀请用户：去重、剔除已是成员、剔除不存在的用户
    std::vector<int64_t> toAdd;
    std::set<int64_t> seen;
    for (size_t i = 0; i < req.userids.size(); ++i)
    {
        int64_t uid = req.userids[i];
        if (seen.count(uid) > 0)
            continue;
        seen.insert(uid);

        if (groupMgr.isGroupMember(req.groupid, static_cast<int32_t>(uid)))
            continue;

        User u;
        u.userid = 0;
        userMgr.getUserInfoByUserId(static_cast<int32_t>(uid), u);
        if (u.userid == 0)
            continue;

        toAdd.push_back(uid);
    }

    //没有可邀请的有效用户：无人变动，直接回 ok（三项统计都为 0）
    if (toAdd.empty())
    {
        send(cmd_invite_to_group, m_seq, GroupProtocol::buildInviteToGroupResponse(0, "ok", 0, 0, 0));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;

    //「邀请入群需被邀请人同意」按被邀请人所属企业取值（docs/邀请入群方案.md §3.3、§3.5）：开启的被邀请人一律先收到
    //入群邀请卡片，等他点「加入群聊」时再按那一刻的情况入群或转为审批（先发卡片，后审批）；其余被邀请人按原来的逻辑处理
    int invited = 0;
    {
        std::vector<int64_t> rest;
        FeatureSwitchManager& switches = Singleton<FeatureSwitchManager>::Instance();
        EmployeeManager& employees = Singleton<EmployeeManager>::Instance();
        for (size_t i = 0; i < toAdd.size(); ++i)
        {
            const int64_t enterpriseId = employees.getEmployeeEnterpriseId(toAdd[i]);
            if (!switches.isEnabled(enterpriseId, kFeatureInviteConfirm))
            {
                rest.push_back(toAdd[i]);
                continue;
            }
            if (sendGroupInviteCard(group, toAdd[i], req.historylimit, now))
                ++invited;
        }
        toAdd.swap(rest);
    }
    if (toAdd.empty())
    {
        send(cmd_invite_to_group, m_seq, GroupProtocol::buildInviteToGroupResponse(0, "ok", 0, invited, 0));
        LOGI("invite to group sent as cards, groupid: %lld, by: %d, invited: %d, historylimit: %d",
             (long long)req.groupid, m_userinfo.userid, invited, req.historylimit);
        return;
    }

    //普通成员邀请是否免审批，由群设置 f_member_invite 决定：
    //  · 群主 / 管理员 —— 始终直接拉人；
    //  · 普通成员且 memberinvite 为 1 —— 直接拉人；
    //  · 普通成员且 memberinvite 为 0 —— 邀请转为待审批申请，等群主 / 管理员处理。
    //转申请这一路记下来源为"好友邀请"、推荐人为邀请者，审批界面据此显示
    //"来源：好友邀请（张三）"。
    const int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    const bool canInviteDirectly = (myrole >= group_role_admin) || (group.memberinvite != 0);
    if (!canInviteDirectly)
    {
        int converted = 0;
        for (size_t i = 0; i < toAdd.size(); ++i)
        {
            //同步条数随申请记下，审批通过时再按它算历史边界（docs/邀请入群方案.md §3.4）
            const int64_t requestId = groupMgr.addJoinRequest(
                req.groupid, static_cast<int32_t>(toAdd[i]), std::string(), now,
                group_join_source_friend, m_userinfo.userid, req.historylimit);
            if (requestId <= 0)
                continue;

            ++converted;

            //推给群主与管理员，让他们的审批入口立刻出现红点
            GroupJoinRequestRecord rec;
            if (groupMgr.getJoinRequestById(requestId, rec))
            {
                User applicant;
                applicant.userid = 0;
                userMgr.getUserInfoByUserId(static_cast<int32_t>(rec.userid), applicant);
                rec.nickname   = applicant.nickname;
                rec.facetype   = applicant.facetype;
                rec.customface = applicant.customface;
                notifyGroupAdmins(req.groupid, cmd_join_group_request_notify,
                                  GroupProtocol::buildJoinGroupRequestNotify(rec));
            }
        }

        //应答带上转为审批的人数，邀请人的客户端据此提示「已提交邀请，等待群主或管理员审批」
        send(cmd_invite_to_group, m_seq, GroupProtocol::buildInviteToGroupResponse(0, "ok", 0, invited, converted));
        LOGI("invite converted to join requests, groupid: %lld, by: %d, converted: %d, historylimit: %d",
             (long long)req.groupid, m_userinfo.userid, converted, req.historylimit);
        return;
    }

    //按邀请人选择的同步条数算这批新成员的历史边界，随成员行一起写入；须在插入入群提示之前算
    const int64_t historyFrom = inviteHistoryFrom(group, req.historylimit);
    if (!groupMgr.addMembers(req.groupid, toAdd, now, historyFrom))
    {
        send(cmd_invite_to_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "invite failed"));
        return;
    }

    send(cmd_invite_to_group, m_seq,
         GroupProtocol::buildInviteToGroupResponse(0, "ok", static_cast<int>(toAdd.size()), invited, 0));

    //已读位点、成员变动推送、自动群名、入群提示、欢迎语、群备忘：邀请人既是邀请者也是操作者
    completeGroupJoin(group, toAdd, m_userinfo.userid, m_userinfo.userid);

    LOGI("invite to group ok, groupid: %lld, by: %d, added: %u, historylimit: %d, historyfrom: %lld",
         (long long)req.groupid, m_userinfo.userid, static_cast<unsigned>(toAdd.size()), req.historylimit,
         (long long)historyFrom);
}

bool ChatSession::sendGroupInviteCard(const GroupRecord& group, int64_t invitee, int historyLimit, int64_t nowMs)
{
    //先落一行 t_group_invite 拿到邀请 id：卡片 content 里要带它，点「加入群聊」时据此找到这次邀请
    GroupInviteRecord invite;
    invite.m_groupid      = group.groupid;
    invite.m_inviter      = m_userinfo.userid;
    invite.m_invitee      = invitee;
    invite.m_historyLimit = historyLimit;
    invite.m_state        = group_invite_pending;
    invite.m_createTime   = nowMs;
    invite.m_expireTime   = nowMs + kGroupInviteExpireMs;
    GroupInviteManager& inviteMgr = Singleton<GroupInviteManager>::Instance();
    invite.m_id = inviteMgr.addInvite(invite);
    if (invite.m_id <= 0)
    {
        LOGE("send group invite card failed: add invite, groupid: %lld, inviter: %d, invitee: %lld",
             (long long)group.groupid, m_userinfo.userid, (long long)invitee);
        return false;
    }

    //卡片上的展示信息：发出那一刻的群名、群头像与双方昵称（下发时群名与头像会被群的当前资料覆盖）
    GroupInviteCardNames names;
    names.m_groupName   = group.name;
    names.m_facetype    = group.facetype;
    names.m_customface  = group.customface;
    names.m_inviterName = m_userinfo.nickname;
    User inviteeUser;
    inviteeUser.userid = 0;
    if (Singleton<UserManager>::Instance().getUserInfoByUserId(static_cast<int32_t>(invitee), inviteeUser))
        names.m_inviteeName = inviteeUser.nickname;
    const std::string content = GroupInviteProtocol::buildCardContent(invite, names);

    //卡片是邀请人与被邀请人之间的一条单聊消息，发送者为邀请人。落库前套一层 buildStoredContent 的外壳，
    //与其它落库路径口径一致；占位的客户端消息 id 带上邀请 id，保证去重键唯一
    std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");
    const std::string clientMsgId = "sys-ginv-" + std::to_string(invite.m_id);
    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, CHAT_TYPE_SINGLE, m_userinfo.userid, static_cast<int32_t>(invitee), message_type_group_invite,
        stored, nowMs);
    if (msgId == 0)
    {
        LOGE("send group invite card failed: save message, inviteid: %lld, groupid: %lld, invitee: %lld",
             (long long)invite.m_id, (long long)group.groupid, (long long)invitee);
        return false;
    }
    invite.m_msgid = msgId;
    inviteMgr.setMsgId(invite.m_id, msgId);

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype       = CHAT_TYPE_SINGLE;
    rec.from        = m_userinfo.userid;
    rec.to          = invitee;
    rec.msgtype     = message_type_group_invite;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = nowMs;
    rec.recalled    = false;
    //推送前合并状态与成员数，与同步时下发的口径一致
    augmentGroupInviteCardRecord(rec, nowMs);

    //双方都要收到（双方的其它在线设备也一样）。两帧的差别只在会话 id：各自视角下都是对方的 userid
    std::vector<int64_t> inviteeOnly(1, invitee);
    Singleton<ChatServer>::Instance().pushToUsers(
        inviteeOnly, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, m_userinfo.userid));
    std::vector<int64_t> inviterOnly(1, static_cast<int64_t>(m_userinfo.userid));
    Singleton<ChatServer>::Instance().pushToUsers(
        inviterOnly, cmd_message_notify, MessageProtocol::buildMessageNotify(rec, invitee));

    LOGI("group invite card sent, inviteid: %lld, msgid: %lld, groupid: %lld, inviter: %d, invitee: %lld, "
         "historylimit: %d",
         (long long)invite.m_id, (long long)msgId, (long long)group.groupid, m_userinfo.userid, (long long)invitee,
         historyLimit);
    return true;
}

void ChatSession::notifyGroupInviteChanged(const GroupInviteRecord& invite, int state)
{
    std::vector<int64_t> both;
    both.push_back(invite.m_inviter);
    both.push_back(invite.m_invitee);
    Singleton<ChatServer>::Instance().pushToUsers(both, cmd_group_invite_changed_notify,
                                                  GroupInviteProtocol::buildChangedNotify(invite, state));
    LOGI("group invite changed notify, inviteid: %lld, msgid: %lld, state: %d",
         (long long)invite.m_id, (long long)invite.m_msgid, state);
}

void ChatSession::augmentGroupInviteCardRecord(MessageRecord& message, int64_t nowMs)
{
    const int64_t inviteId = GroupInviteProtocol::parseCardInviteId(message.content);
    GroupInviteRecord invite;
    if (inviteId <= 0 || !Singleton<GroupInviteManager>::Instance().getInvite(inviteId, invite))
    {
        LOGE("augment group invite card skipped: invite not found, msgid: %lld, inviteid: %lld",
             (long long)message.msgid, (long long)inviteId);
        return;
    }

    //显示状态要看群是否还在、邀请人与被邀请人此刻是否在群里
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    const bool groupExists = groupMgr.getGroup(invite.m_groupid, group);
    const bool inviterIsMember =
        groupExists && groupMgr.isGroupMember(invite.m_groupid, static_cast<int32_t>(invite.m_inviter));
    const bool inviteeIsMember =
        groupExists && groupMgr.isGroupMember(invite.m_groupid, static_cast<int32_t>(invite.m_invitee));
    const int state = GroupInviteProtocol::computeDisplayState(invite.m_state, invite.m_expireTime, nowMs, groupExists,
                                                               inviterIsMember, inviteeIsMember);
    message.content = GroupInviteProtocol::augmentCardContent(message.content, state, groupExists, group.name,
                                                              group.facetype, group.customface, group.membercount);
}

void ChatSession::onAcceptGroupInvite(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t inviteId = 0;
    if (!GroupInviteProtocol::parseAcceptRequest(body, inviteId))
    {
        send(cmd_accept_group_invite, m_seq,
             GroupInviteProtocol::buildAcceptResponse(error_code_bad_request, "bad request", 0, 0, false));
        return;
    }

    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    GroupInviteManager& inviteMgr = Singleton<GroupInviteManager>::Instance();
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();

    //查好判定要用的事实：邀请本身、群是否还在、本人与邀请人此刻是否在群里、邀请人的角色、群的邀请设置
    GroupInviteAcceptFacts facts;
    facts.m_selfId = m_userinfo.userid;
    facts.m_nowMs  = now;
    facts.m_found  = inviteMgr.getInvite(inviteId, facts.m_invite);
    GroupRecord group;
    if (facts.m_found)
    {
        const int64_t gid = facts.m_invite.m_groupid;
        facts.m_groupExists = groupMgr.getGroup(gid, group);
        if (facts.m_groupExists)
        {
            facts.m_selfIsMember     = groupMgr.isGroupMember(gid, m_userinfo.userid);
            facts.m_inviterRole      = groupMgr.getMemberRole(gid, static_cast<int32_t>(facts.m_invite.m_inviter));
            facts.m_inviterIsMember  = (facts.m_inviterRole >= 0);
            facts.m_memberInviteFree = (group.memberinvite != 0);
        }
    }
    const GroupInviteAcceptDecision decision = GroupInviteProtocol::decideAccept(facts);
    const GroupInviteRecord& invite = facts.m_invite;
    const int64_t groupid = facts.m_found ? invite.m_groupid : 0;

    //拒绝：已在群中时顺带把卡片记为已加入，双方的卡片随之刷新
    if (decision.m_action == group_invite_accept_reject)
    {
        if (decision.m_errorCode == error_code_already_in_group)
        {
            inviteMgr.updateState(invite.m_id, group_invite_joined, now);
            notifyGroupInviteChanged(invite, group_invite_joined);
        }
        send(cmd_accept_group_invite, m_seq,
             GroupInviteProtocol::buildAcceptResponse(decision.m_errorCode, "cannot accept group invite", inviteId, groupid,
                                                      false));
        LOGI("accept group invite rejected, inviteid: %lld, by: %d, code: %d",
             (long long)inviteId, m_userinfo.userid, decision.m_errorCode);
        return;
    }

    //转为加群申请：来源好友邀请、推荐人为邀请人，带上同步条数与邀请 id，审批结果据此回写卡片（§3.4）
    if (decision.m_action == group_invite_accept_approval)
    {
        const int64_t requestId = groupMgr.addJoinRequest(groupid, m_userinfo.userid, std::string(), now,
                                                          group_join_source_friend, invite.m_inviter,
                                                          invite.m_historyLimit, invite.m_id);
        if (requestId <= 0)
        {
            send(cmd_accept_group_invite, m_seq,
                 GroupInviteProtocol::buildAcceptResponse(error_code_server_internal, "join request failed", inviteId, groupid,
                                                          false));
            return;
        }
        inviteMgr.updateState(invite.m_id, group_invite_approving, now);
        send(cmd_accept_group_invite, m_seq, GroupInviteProtocol::buildAcceptResponse(0, "ok", inviteId, groupid, false));

        //推给群主与管理员，让他们的审批入口立刻出现红点
        GroupJoinRequestRecord rec;
        if (groupMgr.getJoinRequestById(requestId, rec))
        {
            User applicant;
            applicant.userid = 0;
            Singleton<UserManager>::Instance().getUserInfoByUserId(m_userinfo.userid, applicant);
            rec.nickname   = applicant.nickname;
            rec.facetype   = applicant.facetype;
            rec.customface = applicant.customface;
            notifyGroupAdmins(groupid, cmd_join_group_request_notify, GroupProtocol::buildJoinGroupRequestNotify(rec));
        }
        notifyGroupInviteChanged(invite, group_invite_approving);
        LOGI("accept group invite converted to join request, inviteid: %lld, groupid: %lld, by: %d, requestid: %lld",
             (long long)inviteId, (long long)groupid, m_userinfo.userid, (long long)requestId);
        return;
    }

    //直接入群：按邀请时选择的同步条数算历史边界，入群提示里的邀请人取卡片的邀请人
    std::vector<int64_t> self(1, static_cast<int64_t>(m_userinfo.userid));
    const int64_t historyFrom = inviteHistoryFrom(group, invite.m_historyLimit);
    if (!groupMgr.addMembers(groupid, self, now, historyFrom))
    {
        send(cmd_accept_group_invite, m_seq,
             GroupInviteProtocol::buildAcceptResponse(error_code_server_internal, "join failed", inviteId, groupid, false));
        return;
    }
    inviteMgr.updateState(invite.m_id, group_invite_joined, now);
    send(cmd_accept_group_invite, m_seq, GroupInviteProtocol::buildAcceptResponse(0, "ok", inviteId, groupid, true));
    notifyGroupInviteChanged(invite, group_invite_joined);
    completeGroupJoin(group, self, invite.m_inviter, m_userinfo.userid);

    LOGI("accept group invite ok, inviteid: %lld, groupid: %lld, by: %d, inviter: %lld, historyfrom: %lld",
         (long long)inviteId, (long long)groupid, m_userinfo.userid, (long long)invite.m_inviter,
         (long long)historyFrom);
}

void ChatSession::onJoinGroupRequest(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    JoinGroupRequest req;
    if (!GroupProtocol::parseJoinGroupRequest(body, req))
    {
        send(cmd_join_group_request, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(req.groupid, group))
    {
        send(cmd_join_group_request, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //已是群成员
    if (groupMgr.isGroupMember(req.groupid, m_userinfo.userid))
    {
        send(cmd_join_group_request, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_already_in_group, "already in group"));
        return;
    }

    //加群方式为"禁止"则拒绝
    if (group.joinmode == group_join_forbid)
    {
        send(cmd_join_group_request, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_permission_denied, "group forbids joining"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;

    if (group.joinmode == group_join_free)
    {
        //自由加入：直接入群
        std::vector<int64_t> self;
        self.push_back(m_userinfo.userid);
        if (!groupMgr.addMembers(req.groupid, self, now))
        {
            send(cmd_join_group_request, m_seq,
                 GroupProtocol::buildSimpleResponse(error_code_server_internal, "join failed"));
            return;
        }

        send(cmd_join_group_request, m_seq, GroupProtocol::buildJoinGroupResponse(0, "ok", true));

        //自由加群没有邀请人，入群提示写成「某某加入了群聊」；操作者是本人
        completeGroupJoin(group, self, 0, m_userinfo.userid);

        LOGI("join group (free) ok, groupid: %lld, userid: %d",
             (long long)req.groupid, m_userinfo.userid);
        return;
    }

    //企业级敏感词：查验证消息。只在需审批时检查，因为验证消息只在这里落库、展示给群主与管理员；
    //上面自由加入的分支不保存也不展示它，不检查
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(req.verifymsg);
    if (hitSensitive(chatserver::sensitive_source_join_request, CHAT_TYPE_GROUP, req.groupid, group.name,
                     sensitiveTexts))
    {
        send(cmd_join_group_request, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_sensitive_word, "verify message contains sensitive word"));
        return;
    }

    //需审批：落一条加群申请
    int64_t requestId = groupMgr.addJoinRequest(req.groupid, m_userinfo.userid, req.verifymsg, now);
    if (requestId == 0)
    {
        send(cmd_join_group_request, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "join request failed"));
        return;
    }

    send(cmd_join_group_request, m_seq, GroupProtocol::buildJoinGroupResponse(0, "ok", false));

    //构造申请通知（补全申请人展示信息），推给全部群主 / 管理员
    GroupJoinRequestRecord item;
    item.id         = requestId;
    item.groupid    = req.groupid;
    item.userid     = m_userinfo.userid;
    item.verifymsg  = req.verifymsg;
    item.createtime = now;
    User self;
    self.userid = 0;
    Singleton<UserManager>::Instance().getUserInfoByUserId(m_userinfo.userid, self);
    item.nickname   = self.nickname;
    item.facetype   = self.facetype;
    item.customface = self.customface;
    notifyGroupAdmins(req.groupid, cmd_join_group_request_notify,
                      GroupProtocol::buildJoinGroupRequestNotify(item));

    LOGI("join group request ok, groupid: %lld, userid: %d, requestid: %lld",
         (long long)req.groupid, m_userinfo.userid, (long long)requestId);
}

void ChatSession::onHandleJoinGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    HandleJoinGroupRequest req;
    if (!GroupProtocol::parseHandleJoinGroupRequest(body, req))
    {
        send(cmd_handle_join_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupJoinRequestRecord rec;
    if (!groupMgr.getJoinRequestById(req.requestid, rec))
    {
        send(cmd_handle_join_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_join_req_not_exist, "join request not exist"));
        return;
    }

    //申请须仍处于待处理状态
    if (rec.status != 0)
    {
        send(cmd_handle_join_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_join_req_not_exist, "join request already handled"));
        return;
    }

    //处理人须为该群群主 / 管理员
    int myrole = groupMgr.getMemberRole(rec.groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_handle_join_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can handle"));
        return;
    }

    bool accept = (req.action == GROUP_JOIN_ACTION_ACCEPT);
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    //改状态带「仍是待处理」的条件：上面的检查与这里之间，另一位管理员可能已经处理了同一条申请（两个请求在不同的 IO 线程上
    //同时执行）。只有真正改到的请求继续入群与推送，没改到的按「已处理」回 308（docs/邀请入群方案.md 第 4 期，用例 JQ1）
    bool changed = false;
    if (!groupMgr.updateJoinRequestStatus(req.requestid, accept ? 1 : 2, now, m_userinfo.userid, changed))
    {
        send(cmd_handle_join_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "handle join request failed"));
        return;
    }
    if (!changed)
    {
        send(cmd_handle_join_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_join_req_not_exist, "join request already handled"));
        LOGI("handle join group skipped: request handled by someone else meanwhile, requestid: %lld, by: %d",
             (long long)req.requestid, m_userinfo.userid);
        return;
    }

    //同意时要用到群资料：算历史边界要看群设置，收尾要读欢迎语开关与正文。上面的流程里只用到了申请记录，
    //手头没有 GroupRecord，这里读一次；群恰好在这期间被解散时读不到，按不限处理边界、跳过收尾
    GroupRecord group;
    const bool groupFound = accept && groupMgr.getGroup(rec.groupid, group);
    std::vector<int64_t> applicant;
    applicant.push_back(rec.userid);
    if (accept)
    {
        //由邀请转来的申请记着邀请人选择的同步条数，本人主动申请的为全部（docs/邀请入群方案.md §3.4）
        const int64_t historyFrom = groupFound ? inviteHistoryFrom(group, rec.historylimit) : 0;
        groupMgr.addMembers(rec.groupid, applicant, now, historyFrom);
    }

    send(cmd_handle_join_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //同意时走入群收尾（成员变动推送、入群提示、欢迎语等）；
    //拒绝不向申请人单独推结果（申请人可重新申请）
    if (accept)
    {
        //由普通成员的邀请或加群链接转来的申请，入群提示里的邀请人取推荐人（邀请人 / 链接生成者）；
        //本人主动申请的没有邀请人（2026-10-07 之前链接来源不取，提示里没有生成者，与链接直接入群不一致）
        const int64_t inviterId = GroupProtocol::joinTipInviterForRequest(rec.source, rec.recommender);
        if (groupFound)
            completeGroupJoin(group, applicant, inviterId, m_userinfo.userid);
        else
            LOGE("handle join group: group vanished before completing join, groupid: %lld", (long long)rec.groupid);
    }

    //由入群邀请卡片转来的申请：审批结果回写卡片，邀请人与被邀请人的卡片随之变为已加入 / 未通过审批。
    //拒绝时这就是被邀请人得知结果的途径（docs/邀请入群方案.md §3.4）
    if (rec.inviteid > 0)
    {
        GroupInviteManager& inviteMgr = Singleton<GroupInviteManager>::Instance();
        GroupInviteRecord invite;
        if (inviteMgr.getInvite(rec.inviteid, invite))
        {
            const int state = accept ? group_invite_joined : group_invite_rejected;
            inviteMgr.updateState(invite.m_id, state, now);
            notifyGroupInviteChanged(invite, state);
        }
        else
        {
            LOGE("handle join group: invite of the request not found, requestid: %lld, inviteid: %lld",
                 (long long)req.requestid, (long long)rec.inviteid);
        }
    }

    //告诉该群全体群主与管理员（含本人的其他设备）这条申请已处理，他们的待审批列表据此去掉这一条，
    //避免再去处理一条已处理过的申请（docs/邀请入群方案.md 第 4 期）
    notifyGroupAdmins(rec.groupid, cmd_join_request_handled_notify,
                      GroupProtocol::buildJoinRequestHandledNotify(rec.groupid, req.requestid, accept ? 1 : 2,
                                                                   m_userinfo.userid, m_userinfo.nickname));

    LOGI("handle join group ok, requestid: %lld, action: %d, by: %d, inviteid: %lld",
         (long long)req.requestid, req.action, m_userinfo.userid, (long long)rec.inviteid);
}

void ChatSession::onGetGroupJoinRequests(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_group_join_requests, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();

    //须为群主 / 管理员
    int myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_get_group_join_requests, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can view"));
        return;
    }

    std::vector<GroupJoinRequestRecord> requests;
    groupMgr.getPendingJoinRequests(groupid, requests);

    //补全每个申请人的昵称与头像
    UserManager& userMgr = Singleton<UserManager>::Instance();
    for (size_t i = 0; i < requests.size(); ++i)
    {
        User u;
        u.userid = 0;
        userMgr.getUserInfoByUserId(static_cast<int32_t>(requests[i].userid), u);
        if (u.userid != 0)
        {
            requests[i].nickname   = u.nickname;
            requests[i].facetype   = u.facetype;
            requests[i].customface = u.customface;
        }
    }

    send(cmd_get_group_join_requests, m_seq,
         GroupProtocol::buildGetJoinRequestsResponse(0, "ok", groupid, requests));
    LOGI("get group join requests ok, groupid: %lld, count: %u",
         (long long)groupid, static_cast<unsigned>(requests.size()));
}

void ChatSession::onLeaveGroup(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_leave_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    int myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < 0)
    {
        send(cmd_leave_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    //群主不能直接退群，须先转让群主或解散群
    if (myrole == group_role_owner)
    {
        send(cmd_leave_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_permission_denied, "owner must transfer or dismiss the group"));
        return;
    }

    std::vector<int64_t> self;
    self.push_back(m_userinfo.userid);
    if (!groupMgr.removeMembers(groupid, self))
    {
        send(cmd_leave_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "leave group failed"));
        return;
    }

    send(cmd_leave_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //向余下成员推 member_changed_notify（退出），并同步推给退群者本人（多端）
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        groupid, group_member_change_leave, self, m_userinfo.userid);
    notifyGroupMembers(groupid, cmd_group_member_changed_notify, notify);
    sendToUser(m_userinfo.userid, cmd_group_member_changed_notify, notify);

    //自动命名的群随成员变化重算群名，群名改变时向全群推送资料变更
    Singleton<ChatServer>::Instance().refreshAutoGroupNameAndNotify(groupid);

    //2026-10-04 起不再插「「某人」退出了群聊」的系统提示（用户决定，docs/邀请入群方案.md 第 4 期）：留下的成员只收到上面的
    //成员变动推送，成员列表随之更新；此前已存在的退群提示留在聊天记录里，客户端仍按 member_leave 的模板显示

    //文件共享：他在本群的共享取消，他能看到的本群共享把他移出（docs/文件共享方案.md 6.1）
    handleShareMemberLeftGroup(groupid, m_userinfo.userid);

    //备忘：他原先能看到的本群群备忘推删除，并从各条群备忘的提醒对象里移除
    handleMemoMembersLeftGroup(groupid, std::vector<MemoMemberRole>(1, MemoMemberRole(m_userinfo.userid, myrole)));

    //帖子群：退群后不再收到本群的留言与互动推送
    Singleton<ThreadWatchRegistry>::Instance().removeUsers(groupid, std::vector<int64_t>(1, m_userinfo.userid));

    LOGI("leave group ok, groupid: %lld, userid: %d", (long long)groupid, m_userinfo.userid);
}

void ChatSession::onKickGroupMember(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GroupMembersOpRequest req;
    if (!GroupProtocol::parseGroupMembersOpRequest(body, req))
    {
        send(cmd_kick_group_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    const bool groupExists = groupMgr.getGroup(req.groupid, group);

    //群不存在时不必再查角色，查了也是落空；直接以"不在群"参与判定
    int myrole = groupExists
        ? groupMgr.getMemberRole(req.groupid, m_userinfo.userid) : -1;

    //前置权限：够不够格发起移出。规则摘在 GroupPermission 里，可被
    //tests/test_grouppermission.cpp 穷举断言
    const chatserver::kick_member_check checked =
        chatserver::CheckKickGroupMember(groupExists, myrole);
    if (checked != chatserver::kick_check_ok)
    {
        LOGI("kick group member denied, groupid: %lld, by: %d, reason: %d",
             (long long)req.groupid, m_userinfo.userid, (int)checked);
        send(cmd_kick_group_member, m_seq,
             GroupProtocol::buildSimpleResponse(
                 chatserver::KickGroupMemberCheckToErrorCode(checked),
                 chatserver::KickGroupMemberCheckToMessage(checked)));
        return;
    }

    //逐个筛选可踢对象。被筛掉的静默跳过而不是让整个请求失败 —— 一次踢多人时，
    //个别人刚好已退群不该拖垮其余人。
    std::vector<int64_t> toKick;
    //被移出的人与他们被移出前的角色，备忘按它判断原先能看到哪些
    std::vector<MemoMemberRole> kickedRoles;
    std::set<int64_t> seen;
    for (size_t i = 0; i < req.userids.size(); ++i)
    {
        int64_t uid = req.userids[i];
        if (uid == m_userinfo.userid || seen.count(uid) > 0)
            continue;
        seen.insert(uid);

        //自己已在上面挡掉，故 targetIsSelf 恒为 false；这样省掉为自己再查一次角色
        int trole = groupMgr.getMemberRole(req.groupid, static_cast<int32_t>(uid));
        if (!chatserver::CanKickTarget(myrole, trole, false))
            continue;

        toKick.push_back(uid);
        kickedRoles.push_back(MemoMemberRole(uid, trole));
    }

    //没有可踢的有效对象：无人变动，直接回 ok
    if (toKick.empty())
    {
        send(cmd_kick_group_member, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));
        return;
    }

    if (!groupMgr.removeMembers(req.groupid, toKick))
    {
        send(cmd_kick_group_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "kick failed"));
        return;
    }

    send(cmd_kick_group_member, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //向余下成员推 member_changed_notify（被踢），并推给被踢者本人
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        req.groupid, group_member_change_kick, toKick, m_userinfo.userid);
    notifyGroupMembers(req.groupid, cmd_group_member_changed_notify, notify);
    for (size_t i = 0; i < toKick.size(); ++i)
        sendToUser(static_cast<int32_t>(toKick[i]), cmd_group_member_changed_notify, notify);

    //自动命名的群随成员变化重算群名，群名改变时向全群推送资料变更
    Singleton<ChatServer>::Instance().refreshAutoGroupNameAndNotify(req.groupid);

    //文件共享：被移出的人在本群的共享取消，他能看到的本群共享把他移出（与退群相同，docs/文件共享方案.md 6.1）
    for (size_t i = 0; i < toKick.size(); ++i)
        handleShareMemberLeftGroup(req.groupid, toKick[i]);

    //备忘：与退群相同
    handleMemoMembersLeftGroup(req.groupid, kickedRoles);

    //帖子群：被移出的人不再收到本群的留言与互动推送
    {
        std::vector<int64_t> kickedIds;
        for (size_t i = 0; i < kickedRoles.size(); ++i)
            kickedIds.push_back(kickedRoles[i].userid);
        Singleton<ThreadWatchRegistry>::Instance().removeUsers(req.groupid, kickedIds);
    }

    LOGI("kick group member ok, groupid: %lld, by: %d, kicked: %u",
         (long long)req.groupid, m_userinfo.userid, static_cast<unsigned>(toKick.size()));
}

void ChatSession::onSetMemberRole(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GroupMemberValueRequest req;
    if (!GroupProtocol::parseGroupMemberValueRequest(body, "role", req))
    {
        send(cmd_set_member_role, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    const bool groupExists = groupMgr.getGroup(req.groupid, group);
    int32_t targetId = static_cast<int32_t>(req.userid);
    const bool targetIsSelf = (targetId == m_userinfo.userid);

    //群不存在时不必再查成员角色，两次查询都会落空；直接以"不在群"参与判定，
    //判定函数会先答"群不存在"，与逐条内联时的提示顺序一致。
    const int operatorRole = groupExists
        ? groupMgr.getMemberRole(req.groupid, m_userinfo.userid) : -1;
    const int targetRole = groupExists
        ? groupMgr.getMemberRole(req.groupid, targetId) : -1;

    //权限规则本身摘在 GroupPermission 里，可被 tests/test_grouppermission.cpp 穷举断言
    const chatserver::set_member_role_check checked = chatserver::CheckSetMemberRole(
        groupExists, operatorRole, targetRole, targetIsSelf, req.value);
    if (checked != chatserver::set_role_check_ok)
    {
        LOGI("set member role denied, groupid: %lld, target: %d, role: %d, by: %d, reason: %d",
             (long long)req.groupid, targetId, req.value, m_userinfo.userid, (int)checked);
        send(cmd_set_member_role, m_seq,
             GroupProtocol::buildSimpleResponse(
                 chatserver::SetMemberRoleCheckToErrorCode(checked),
                 chatserver::SetMemberRoleCheckToMessage(checked)));
        return;
    }

    if (!groupMgr.updateMemberRole(req.groupid, targetId, req.value))
    {
        send(cmd_set_member_role, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "set member role failed"));
        return;
    }

    send(cmd_set_member_role, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    std::vector<int64_t> changed;
    changed.push_back(req.userid);
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        req.groupid, group_member_change_role, changed, m_userinfo.userid);
    notifyGroupMembers(req.groupid, cmd_group_member_changed_notify, notify);

    //备忘：设为或取消管理员会改变他能看到的群备忘
    handleMemoRoleChanged(req.groupid, req.userid, targetRole, req.value);

    LOGI("set member role ok, groupid: %lld, target: %d, role: %d, by: %d",
         (long long)req.groupid, targetId, req.value, m_userinfo.userid);
}

void ChatSession::onMuteMember(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GroupMemberValueRequest req;
    if (!GroupProtocol::parseGroupMemberValueRequest(body, "muted", req))
    {
        send(cmd_mute_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(req.groupid, group))
    {
        send(cmd_mute_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //操作者须为群主 / 管理员
    int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_mute_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can mute"));
        return;
    }

    int32_t targetId = static_cast<int32_t>(req.userid);
    int trole = groupMgr.getMemberRole(req.groupid, targetId);
    if (trole < 0)
    {
        send(cmd_mute_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "target is not a group member"));
        return;
    }
    //不能禁言群主；管理员（非群主）不能禁言其他管理员
    if (trole == group_role_owner || (myrole == group_role_admin && trole >= group_role_admin))
    {
        send(cmd_mute_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_permission_denied, "no permission to mute this member"));
        return;
    }

    int muted = (req.value != 0) ? 1 : 0;

    //禁言时长换算成到期时刻：duration 为 0（含老客户端不带该字段的情形）表示永久
    //禁言，此时到期时刻记 0 —— GroupProtocol::isMuteExpired 把 0 当作"永不过期"。
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    const int64_t muteUntil = GroupProtocol::computeMuteUntil(muted, req.duration, nowMs);

    //企业级敏感词：查禁言原因（与禁言状态一起写库，会展示给被禁言的成员）。放在写库之前，
    //被拦下时对方的禁言状态不变
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(req.reason);
    if (hitSensitive(chatserver::sensitive_source_mute_reason, CHAT_TYPE_GROUP, req.groupid, group.name,
                     sensitiveTexts))
    {
        send(cmd_mute_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_sensitive_word, "mute reason contains sensitive word"));
        return;
    }

    if (!groupMgr.updateMemberMuted(req.groupid, targetId, muted, muteUntil, req.reason))
    {
        send(cmd_mute_member, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "mute member failed"));
        return;
    }

    send(cmd_mute_member, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    std::vector<int64_t> changed;
    changed.push_back(req.userid);
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        req.groupid, group_member_change_muted, changed, m_userinfo.userid);
    notifyGroupMembers(req.groupid, cmd_group_member_changed_notify, notify);

    LOGI("mute member ok, groupid: %lld, target: %d, muted: %d, by: %d",
         (long long)req.groupid, targetId, muted, m_userinfo.userid);
}

void ChatSession::onMuteAll(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int muteall = 0;
    if (!GroupProtocol::parseMuteAllRequest(body, groupid, muteall))
    {
        send(cmd_mute_all, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_mute_all, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //操作者须为群主 / 管理员
    if (groupMgr.getMemberRole(groupid, m_userinfo.userid) < group_role_admin)
    {
        send(cmd_mute_all, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin, "only owner or admin can mute all"));
        return;
    }

    //复用 updateGroupInfo 改 t_group.f_mute_all
    UpdateGroupInfoRequest ureq;
    ureq.groupid     = groupid;
    ureq.has_muteall = true;
    ureq.muteall     = (muteall != 0) ? 1 : 0;
    if (!groupMgr.updateGroupInfo(ureq))
    {
        send(cmd_mute_all, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "mute all failed"));
        return;
    }

    send(cmd_mute_all, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //读回最新群资料，向全体成员推 group_info_changed_notify（资料变更）
    GroupRecord updated;
    if (groupMgr.getGroup(groupid, updated))
    {
        std::string notify = GroupProtocol::buildGroupInfoChangedNotify(
            updated, group_info_change_updated);
        notifyGroupMembers(groupid, cmd_group_info_changed_notify, notify);
    }

    //开关真的变了才在群里写一条「开启 / 关闭了仅通知模式」的系统提示（docs/群管理接入方案.md §14）；
    //重复设置同一个值不写，免得群里出现一串相同的提示
    const bool changed = ((group.muteall != 0) != (ureq.muteall != 0));
    if (changed)
    {
        const int64_t kMillisPerSecond = 1000;
        const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
        const std::string clientMsgId = "sys-muteall-" + std::to_string(groupid) + "-" + std::to_string(now);
        sendGroupSystemMessage(groupid,
                               GroupProtocol::buildMuteAllTipContent(ureq.muteall, m_userinfo.nickname,
                                                                     static_cast<int64_t>(m_userinfo.userid)),
                               clientMsgId);
    }

    LOGI("mute all ok, groupid: %lld, muteall: %d, changed: %d, by: %d",
         (long long)groupid, ureq.muteall, changed ? 1 : 0, m_userinfo.userid);
}

void ChatSession::onSetGroupFollow(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GroupFollowList req;
    if (!GroupProtocol::parseSetGroupFollowRequest(body, req))
    {
        send(cmd_set_group_follow, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //本人须为群成员、不超过上限、不含自己、都是本群成员（细节见 GroupFollowManager::checkTargets）
    GroupFollowManager& followMgr = Singleton<GroupFollowManager>::Instance();
    const int checkCode = followMgr.checkTargets(m_userinfo.userid, req.groupid, req.targets);
    if (checkCode != error_code_ok)
    {
        send(cmd_set_group_follow, m_seq,
             GroupProtocol::buildSimpleResponse(checkCode, checkCode == error_code_not_group_member
                                                           ? "not a group member" : "invalid follow targets"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    if (!followMgr.setFollows(m_userinfo.userid, req.groupid, req.targets, now))
    {
        send(cmd_set_group_follow, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "set group follow failed"));
        return;
    }

    send(cmd_set_group_follow, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //读回完整名单，推给本人全部在线设备（含发起端），各端以推送为准
    GroupFollowList saved;
    saved.groupid = req.groupid;
    followMgr.getFollows(m_userinfo.userid, req.groupid, saved.targets);
    sendToUser(m_userinfo.userid, cmd_group_follow_changed_notify,
               GroupProtocol::buildGroupFollowChangedNotify(saved));

    LOGI("set group follow ok, userid: %d, groupid: %lld, count: %u",
         m_userinfo.userid, (long long)req.groupid, static_cast<unsigned>(saved.targets.size()));
}

void ChatSession::onSetMemberCard(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    std::string card;
    if (!GroupProtocol::parseSetMemberCardRequest(body, groupid, card))
    {
        send(cmd_set_member_card, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();

    //只能改自己的群名片，故须为该群成员
    if (!groupMgr.isGroupMember(groupid, m_userinfo.userid))
    {
        send(cmd_set_member_card, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    //企业级敏感词：查群名片；空串表示清除名片，由 checkTexts 跳过。会话名由 hitSensitive
    //在命中时按群ID取
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(card);
    if (hitSensitive(chatserver::sensitive_source_member_card, CHAT_TYPE_GROUP, groupid, std::string(),
                     sensitiveTexts))
    {
        send(cmd_set_member_card, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_sensitive_word, "member card contains sensitive word"));
        return;
    }

    if (!groupMgr.updateMemberCard(groupid, m_userinfo.userid, card))
    {
        send(cmd_set_member_card, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "set member card failed"));
        return;
    }

    send(cmd_set_member_card, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    std::vector<int64_t> changed;
    changed.push_back(m_userinfo.userid);
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        groupid, group_member_change_card, changed, m_userinfo.userid);
    notifyGroupMembers(groupid, cmd_group_member_changed_notify, notify);

    LOGI("set member card ok, groupid: %lld, userid: %d", (long long)groupid, m_userinfo.userid);
}

void ChatSession::onTransferOwner(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    int64_t newOwnerId = 0;
    if (!GroupProtocol::parseTransferOwnerRequest(body, groupid, newOwnerId))
    {
        send(cmd_transfer_owner, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_transfer_owner, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //仅群主可转让群主
    if (group.ownerid != m_userinfo.userid)
    {
        send(cmd_transfer_owner, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_permission_denied, "only group owner can transfer"));
        return;
    }

    int32_t targetId = static_cast<int32_t>(newOwnerId);
    //不能转让给自己
    if (targetId == m_userinfo.userid)
    {
        send(cmd_transfer_owner, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "cannot transfer to yourself"));
        return;
    }
    //新群主须为该群现有成员
    const int targetOldRole = groupMgr.getMemberRole(groupid, targetId);
    if (targetOldRole < 0)
    {
        send(cmd_transfer_owner, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_member, "new owner is not a group member"));
        return;
    }

    if (!groupMgr.transferOwner(groupid, m_userinfo.userid, targetId))
    {
        send(cmd_transfer_owner, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "transfer owner failed"));
        return;
    }

    send(cmd_transfer_owner, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //原群主与新群主角色均变更，向全体成员推 member_changed_notify（角色变更）
    std::vector<int64_t> changed;
    changed.push_back(m_userinfo.userid);
    changed.push_back(newOwnerId);
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        groupid, group_member_change_role, changed, m_userinfo.userid);
    notifyGroupMembers(groupid, cmd_group_member_changed_notify, notify);

    //自动群名不含群主，换了群主之后原群主进入群名、新群主移出群名
    Singleton<ChatServer>::Instance().refreshAutoGroupNameAndNotify(groupid);

    //备忘：原群主降为普通成员、新群主升为群主，两人能看到的群备忘都可能变化
    handleMemoRoleChanged(groupid, m_userinfo.userid, group_role_owner,
                          groupMgr.getMemberRole(groupid, m_userinfo.userid));
    handleMemoRoleChanged(groupid, newOwnerId, targetOldRole, group_role_owner);

    //插一条"「原群主」已将群主转让给「新群主」"的系统提示。新群主的昵称取不到时（资料缺失）
    //提示里省掉他的名字，不影响转让本身
    std::string newOwnerNickname;
    User newOwnerInfo;
    if (Singleton<UserManager>::Instance().getUserInfoByUserId(targetId, newOwnerInfo))
        newOwnerNickname = newOwnerInfo.nickname;
    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    const std::string clientMsgId = "sys-transfer-" + std::to_string(groupid) + "-" + std::to_string(now);
    sendGroupSystemMessage(groupid,
                           GroupProtocol::buildOwnerTransferTipContent(m_userinfo.nickname, newOwnerNickname),
                           clientMsgId);

    LOGI("transfer owner ok, groupid: %lld, from: %d, to: %d",
         (long long)groupid, m_userinfo.userid, targetId);
}

bool ChatSession::canAccessCard(const MessageRecord& card)
{
    //群卡片：须为该群成员；单聊卡片：须为收发双方之一
    if (card.ctype == CHAT_TYPE_GROUP)
        return Singleton<GroupManager>::Instance().isGroupMember(card.to, m_userinfo.userid);
    return (card.from == m_userinfo.userid || card.to == m_userinfo.userid);
}

void ChatSession::notifyCardConversation(const MessageRecord& card, uint16_t cmd,
                                         const std::string& body)
{
    if (card.ctype == CHAT_TYPE_GROUP)
    {
        notifyGroupMembers(card.to, cmd, body);
    }
    else
    {
        //单聊卡片：推给收发双方
        sendToUser(static_cast<int32_t>(card.from), cmd, body);
        sendToUser(static_cast<int32_t>(card.to), cmd, body);
    }
}

void ChatSession::onVotePoll(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    VotePollRequest req;
    if (!CardProtocol::parseVotePollRequest(body, req))
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessageRecord card;
    if (!msgMgr.getMessageById(req.msgid, card))
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(error_code_message_not_exist, "card not exist"));
        return;
    }
    //卡片已撤回、或类型不是投票，都不接受投票（2026-10-02 之前只判断类型，撤回后仍可投票）
    const int targetCode = CardProtocol::checkCardTarget(card.msgtype, card.recalled, message_type_poll);
    if (targetCode != error_code_ok)
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(targetCode,
                 targetCode == error_code_message_not_exist ? "card recalled" : "not a poll card"));
        LOGI("vote poll rejected, card unusable, code: %d, userid: %d, msgid: %lld",
             targetCode, m_userinfo.userid, (long long)req.msgid);
        return;
    }
    if (!canAccessCard(card))
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(error_code_permission_denied, "no access to this card"));
        return;
    }

    //取投票卡片元信息，校验未过期、选项合法
    int optioncount = 0;
    int multi = 0;
    int64_t expiretime = 0;
    if (!CardProtocol::getPollMeta(card.content, optioncount, multi, expiretime))
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "invalid poll card"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    if (expiretime > 0 && now >= expiretime)
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "poll expired"));
        return;
    }
    //单选投票只能投一项
    if (multi == 0 && req.options.size() != 1)
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "single-choice poll allows one option"));
        return;
    }
    //选项下标须在合法范围内
    for (size_t i = 0; i < req.options.size(); ++i)
    {
        if (req.options[i] < 0 || req.options[i] >= optioncount)
        {
            send(cmd_vote_poll, m_seq,
                 CardProtocol::buildSimpleResponse(error_code_bad_request, "invalid option index"));
            return;
        }
    }

    CardManager& cardMgr = Singleton<CardManager>::Instance();
    cardMgr.upsertInteraction(req.msgid, m_userinfo.userid,
                              CardProtocol::buildVoteData(req.options), now);

    send(cmd_vote_poll, m_seq, CardProtocol::buildSimpleResponse(0, "ok"));

    //广播 poll_changed_notify 给卡片所在会话全体
    std::vector<CardInteractionRecord> rows;
    cardMgr.getCardInteractions(req.msgid, rows);
    notifyCardConversation(card, cmd_poll_changed_notify,
                           CardProtocol::buildPollChangedNotify(req.msgid, rows));

    LOGI("vote poll ok, userid: %d, msgid: %lld", m_userinfo.userid, (long long)req.msgid);
}

void ChatSession::onJoinRelay(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    JoinRelayRequest req;
    if (!CardProtocol::parseJoinRelayRequest(body, req))
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //请求解析成功之后的应答，无论成败都带回请求里的 ctype / cid / msgid，
    //客户端据 msgid 把结果送到对应卡片的详情窗（2026-10-02 与用户确认：原样带回请求里的值）

    //接龙文本先规范化：去掉首尾空白，拒绝只有空白、中间含换行与超长的文本。这一步不访问
    //数据库，放在取卡片之前，非法请求不必再查库
    std::string text;
    const int textCode = CardProtocol::normalizeRelayText(req.text, text);
    if (textCode != error_code_ok)
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildRelayOpResponse(textCode,
                 textCode == error_code_msg_too_large ? "relay text too long" : "invalid relay text",
                 req.ctype, req.cid, req.msgid));
        LOGI("join relay rejected by text check, code: %d, userid: %d, msgid: %lld",
             textCode, m_userinfo.userid, (long long)req.msgid);
        return;
    }

    MessageRecord card;
    RelayStatus status;
    std::string reason;
    const int cardCode = loadRelayCardForOp(cmd_join_relay, req.msgid, true, card, status, reason);
    if (cardCode != error_code_ok)
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildRelayOpResponse(cardCode, reason, req.ctype, req.cid, req.msgid));
        return;
    }

    //人数上限：已在名单里的人是修改，不占新名额。数人数与写库之间不加锁，两人同时抢最后
    //一个名额时偶尔会多出一两条（2026-10-02 与用户确认可以接受）
    CardManager& cardMgr = Singleton<CardManager>::Instance();
    std::vector<CardInteractionRecord> rows;
    if (!cardMgr.getCardInteractions(req.msgid, rows))
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_server_internal, "load relay failed",
                                                req.ctype, req.cid, req.msgid));
        return;
    }
    const int joinKind = CardProtocol::classifyRelayJoin(rows, m_userinfo.userid, text);

    //内容与本人已提交的完全相同（如双击提交）：名单没有变化，直接回成功，
    //不写库、不推送、不发提示（2026-10-02 与用户确认）
    if (joinKind == relay_join_unchanged)
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_ok, "ok", req.ctype, req.cid, req.msgid));
        LOGI("join relay skipped, text unchanged, userid: %d, msgid: %lld",
             m_userinfo.userid, (long long)req.msgid);
        return;
    }

    //企业级敏感词：放在「内容未变」判断之后，重复提交一条早已成功的接龙照常回成功，不因为
    //期间新加了词而失败；放在写库之前，被拦的文字不进名单。拦截记录的会话取卡片所在会话：
    //群聊为群ID，单聊为对方 userid（卡片可能是本人发的，也可能是对方发的）
    std::vector<std::string> sensitiveTexts;
    sensitiveTexts.push_back(text);
    int64_t relayCid = card.to;
    if (card.ctype == CHAT_TYPE_SINGLE && card.to == m_userinfo.userid)
        relayCid = card.from;
    if (hitSensitive(chatserver::sensitive_source_card, card.ctype, relayCid, std::string(), sensitiveTexts))
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_sensitive_word, "relay text contains sensitive word",
                                                req.ctype, req.cid, req.msgid));
        return;
    }

    const bool alreadyJoined = (joinKind != relay_join_new);
    const int capacityCode = CardProtocol::checkRelayJoinCapacity(alreadyJoined, rows.size());
    if (capacityCode != error_code_ok)
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildRelayOpResponse(capacityCode, "relay is full",
                                                req.ctype, req.cid, req.msgid));
        LOGI("join relay rejected, full, userid: %d, msgid: %lld, count: %u",
             m_userinfo.userid, (long long)req.msgid, static_cast<unsigned>(rows.size()));
        return;
    }

    //互动时间以 Unix 毫秒落库
    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    //每人一条接龙，重复参与即更新本人的接龙文本，名次保持首次参与时的位置。
    //写库失败时如实回错（原先忽略返回值，一律回成功），CardManager 内部已记错误日志
    if (!cardMgr.upsertInteraction(req.msgid, m_userinfo.userid, text, now))
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_server_internal, "save relay failed",
                                                req.ctype, req.cid, req.msgid));
        return;
    }

    send(cmd_join_relay, m_seq,
         CardProtocol::buildRelayOpResponse(error_code_ok, "ok", req.ctype, req.cid, req.msgid));
    notifyRelayChanged(card);
    sendRelayTip(card, alreadyJoined ? relay_tip_modify : relay_tip_join, 0);

    LOGI("join relay ok, userid: %d, msgid: %lld, kind: %d",
         m_userinfo.userid, (long long)req.msgid, joinKind);
}

void ChatSession::onWithdrawRelay(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    RelayTargetRequest req;
    if (!CardProtocol::parseRelayTargetRequest(body, req))
    {
        send(cmd_withdraw_relay, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //与参与接龙同一套前置校验：卡片存在、未撤回、是接龙、本人在会话内、群卡片不在禁言中、
    //接龙仍在进行中。撤回本人那条也会改变群里可见的内容，被禁言时同样拒绝（2026-10-02 与用户确认）
    MessageRecord card;
    RelayStatus status;
    std::string reason;
    const int cardCode = loadRelayCardForOp(cmd_withdraw_relay, req.msgid, true, card, status, reason);
    if (cardCode != error_code_ok)
    {
        send(cmd_withdraw_relay, m_seq,
             CardProtocol::buildRelayOpResponse(cardCode, reason, req.ctype, req.cid, req.msgid));
        return;
    }

    CardManager& cardMgr = Singleton<CardManager>::Instance();
    CardInteractionRecord mine;
    bool found = false;
    if (!cardMgr.getInteraction(req.msgid, m_userinfo.userid, mine, found))
    {
        send(cmd_withdraw_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_server_internal, "load relay failed",
                                                req.ctype, req.cid, req.msgid));
        return;
    }

    //本人没参与过：直接回成功，名单没有变化，不推送
    if (!found)
    {
        send(cmd_withdraw_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_ok, "ok", req.ctype, req.cid, req.msgid));
        LOGI("withdraw relay skipped, not joined, userid: %d, msgid: %lld",
             m_userinfo.userid, (long long)req.msgid);
        return;
    }

    if (!cardMgr.removeInteraction(req.msgid, m_userinfo.userid))
    {
        send(cmd_withdraw_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_server_internal, "withdraw relay failed",
                                                req.ctype, req.cid, req.msgid));
        return;
    }

    send(cmd_withdraw_relay, m_seq,
         CardProtocol::buildRelayOpResponse(error_code_ok, "ok", req.ctype, req.cid, req.msgid));
    notifyRelayChanged(card);
    sendRelayTip(card, relay_tip_withdraw, 0);

    LOGI("withdraw relay ok, userid: %d, msgid: %lld", m_userinfo.userid, (long long)req.msgid);
}

void ChatSession::onCloseRelay(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    RelayTargetRequest req;
    if (!CardProtocol::parseRelayTargetRequest(body, req))
    {
        send(cmd_close_relay, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //前置校验与参与接龙相同，只是不查禁言：提前结束是管理动作，被禁言的发起人或管理员
    //仍可结束（docs/群接龙方案.md §3.6）。已结束或已截止时在这里回 error_code_card_closed
    MessageRecord card;
    RelayStatus status;
    std::string reason;
    const int cardCode = loadRelayCardForOp(cmd_close_relay, req.msgid, false, card, status, reason);
    if (cardCode != error_code_ok)
    {
        send(cmd_close_relay, m_seq,
             CardProtocol::buildRelayOpResponse(cardCode, reason, req.ctype, req.cid, req.msgid));
        return;
    }

    //权限：发起人总可以；群卡片另外允许群主与管理员。canAccessCard 已保证本人在群内
    const bool isGroupCard = (card.ctype == CHAT_TYPE_GROUP);
    int myrole = group_role_member;
    if (isGroupCard)
        myrole = Singleton<GroupManager>::Instance().getMemberRole(card.to, m_userinfo.userid);
    if (!CardProtocol::canCloseRelay(isGroupCard, card.from, m_userinfo.userid, myrole))
    {
        send(cmd_close_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_permission_denied,
                                                "only creator, owner or admin can close relay",
                                                req.ctype, req.cid, req.msgid));
        LOGI("close relay rejected, no permission, userid: %d, role: %d, msgid: %lld",
             m_userinfo.userid, myrole, (long long)req.msgid);
        return;
    }

    //写入结束记录。两人几乎同时结束时只有先写入的那一次生效，后到的一方按"已结束"回复
    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    bool inserted = false;
    if (!Singleton<CardManager>::Instance().closeCard(req.msgid, m_userinfo.userid, now, inserted))
    {
        send(cmd_close_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_server_internal, "close relay failed",
                                                req.ctype, req.cid, req.msgid));
        return;
    }
    if (!inserted)
    {
        send(cmd_close_relay, m_seq,
             CardProtocol::buildRelayOpResponse(error_code_card_closed, "relay already closed",
                                                req.ctype, req.cid, req.msgid));
        LOGI("close relay skipped, already closed, userid: %d, msgid: %lld",
             m_userinfo.userid, (long long)req.msgid);
        return;
    }

    send(cmd_close_relay, m_seq,
         CardProtocol::buildRelayOpResponse(error_code_ok, "ok", req.ctype, req.cid, req.msgid));
    notifyRelayChanged(card);

    //提示里带上结束时的参与人数；读名单失败时人数按 0 写，只影响提示文字
    std::vector<CardInteractionRecord> rows;
    Singleton<CardManager>::Instance().getCardInteractions(req.msgid, rows);
    sendRelayTip(card, relay_tip_close, rows.size());

    LOGI("close relay ok, userid: %d, role: %d, msgid: %lld, count: %u",
         m_userinfo.userid, myrole, (long long)req.msgid, static_cast<unsigned>(rows.size()));
}

void ChatSession::sendRelayTip(const MessageRecord& card, int kind, size_t count)
{
    //只对群卡片发：服务端没有单聊系统提示的发送路径，客户端也只在群聊里提供接龙入口
    if (card.ctype != CHAT_TYPE_GROUP)
        return;

    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    const std::string content = CardProtocol::buildRelayTipContent(kind, m_userinfo.nickname,
                                                                   CardProtocol::getRelayTitle(card.content),
                                                                   count, card.msgid);
    const std::string clientMsgId = CardProtocol::buildRelayTipClientMsgId(kind, card.msgid,
                                                                           m_userinfo.userid, nowMs);
    //落库失败时 sendGroupSystemMessage 已记错误日志；提示只是附带的通知，不影响操作本身的结果
    sendGroupSystemMessage(card.to, content, clientMsgId);
}

int ChatSession::loadRelayCardForOp(uint16_t cmd, int64_t msgid, bool checkSpeak, MessageRecord& card,
                                    RelayStatus& status, std::string& reason)
{
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    if (!msgMgr.getMessageById(msgid, card))
    {
        reason = "card not exist";
        return error_code_message_not_exist;
    }

    //卡片已撤回、或类型不是接龙，都不接受操作（2026-10-02 之前只判断类型，撤回后仍可参与）
    const int targetCode = CardProtocol::checkCardTarget(card.msgtype, card.recalled, message_type_relay);
    if (targetCode != error_code_ok)
    {
        reason = (targetCode == error_code_message_not_exist) ? "card recalled" : "not a relay card";
        LOGI("relay op rejected, card unusable, cmd: %u, code: %d, userid: %d, msgid: %lld",
             static_cast<unsigned>(cmd), targetCode, m_userinfo.userid, (long long)msgid);
        return targetCode;
    }

    if (!canAccessCard(card))
    {
        reason = "no access to this card";
        return error_code_permission_denied;
    }

    //群卡片：参与或撤回接龙都会改变群里可见的内容，按群内发言对待，被禁言时拒绝
    //（checkGroupSpeak 被禁言时已记日志）；提前结束是管理动作，由调用方传 checkSpeak=false 跳过。
    //接龙文字的敏感词检查不在这里做，由企业级敏感词统一处理（docs/敏感词方案.md §6.3「参与接龙」检查点）
    if (checkSpeak && card.ctype == CHAT_TYPE_GROUP)
    {
        int myrole = group_role_member;
        GroupRecord group;
        const int speakCode = checkGroupSpeak(cmd, card.to, myrole, group, reason);
        if (speakCode != error_code_ok)
            return speakCode;
    }

    //接龙已被提前结束或已过截止时间：名单冻结，参与、修改、撤回、再次结束一律拒绝。
    //读结束记录失败时不能确定状态，按服务端内部错误回复，不冒险放行
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    if (!loadRelayStatus(card, nowMs, status))
    {
        reason = "load relay state failed";
        return error_code_server_internal;
    }
    if (status.state != relay_state_open)
    {
        reason = (status.state == relay_state_closed) ? "relay closed" : "relay expired";
        LOGI("relay op rejected, not open, cmd: %u, state: %d, userid: %d, msgid: %lld",
             static_cast<unsigned>(cmd), status.state, m_userinfo.userid, (long long)msgid);
        return error_code_card_closed;
    }
    return error_code_ok;
}

bool ChatSession::loadRelayStatus(const MessageRecord& card, int64_t nowMs, RelayStatus& status)
{
    CardStateRecord row;
    bool found = false;
    const bool ok = Singleton<CardManager>::Instance().getCardState(card.msgid, row, found);
    //读库失败时 found 为 false，下面按"没有结束记录"计算，只看截止时间；调用方据返回值决定是否放行
    status = CardProtocol::computeRelayStatus(found ? row : CardStateRecord(),
                                              CardProtocol::getRelayDeadline(card.content), nowMs);
    return ok;
}

void ChatSession::notifyRelayChanged(const MessageRecord& card)
{
    std::vector<CardInteractionRecord> rows;
    Singleton<CardManager>::Instance().getCardInteractions(card.msgid, rows);

    //读结束记录失败时不推送：带着可能不对的状态推出去，客户端会把已结束的接龙显示成进行中。
    //这种情况下各端在下次同步时拿到正确结果
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    RelayStatus status;
    if (!loadRelayStatus(card, nowMs, status))
    {
        LOGE("relay changed notify skipped, load state failed, msgid: %lld", (long long)card.msgid);
        return;
    }

    if (card.ctype == CHAT_TYPE_GROUP)
    {
        //群卡片：会话ID就是群id，全体成员共用同一份推送
        notifyGroupMembers(card.to, cmd_relay_changed_notify,
                           CardProtocol::buildRelayChangedNotify(card.ctype, card.to, card.msgid, rows,
                                                                 status));
        return;
    }

    //单聊卡片：双方的会话ID互为对方，各发一份
    const int64_t peers[] = { card.from, card.to };
    for (size_t i = 0; i < sizeof(peers) / sizeof(peers[0]); ++i)
    {
        const int64_t cid = CardProtocol::cardConversationIdFor(card.ctype, card.from, card.to, peers[i]);
        sendToUser(static_cast<int32_t>(peers[i]), cmd_relay_changed_notify,
                   CardProtocol::buildRelayChangedNotify(card.ctype, cid, card.msgid, rows, status));
    }
}

void ChatSession::augmentCardRecord(MessageRecord& message, int64_t nowMs)
{
    //文件共享卡片：按本会话的登录用户合并共享状态与他的权限（docs/文件共享方案.md 5.6）。
    //放在群卡片的判断之前：共享卡片不是群投票 / 接龙 / 待办那一类，走自己的合并逻辑
    if (message.msgtype == message_type_fileshare)
    {
        augmentShareCardRecord(message);
        return;
    }

    //入群邀请卡片：合并显示状态与群的当前资料（docs/邀请入群方案.md §4.3）
    if (message.msgtype == message_type_group_invite)
    {
        augmentGroupInviteCardRecord(message, nowMs);
        return;
    }

    //非卡片消息、以及已撤回的卡片都不合并：后者的名单与投票结果不随它下发，也省掉两次查库
    if (!CardProtocol::shouldAugmentCard(message.msgtype, message.recalled))
        return;

    const int mt = message.msgtype;
    std::vector<CardInteractionRecord> rows;
    Singleton<CardManager>::Instance().getCardInteractions(message.msgid, rows);

    //投票、待办：合并互动结果
    if (mt != message_type_relay)
    {
        message.content = CardProtocol::augmentCardContent(mt, message.content, rows);
        return;
    }

    //接龙：合并名单与状态。读结束记录失败时 loadRelayStatus 已按截止时间算出状态并记了日志，
    //这里照样下发，客户端下次同步或收到推送时会更正
    RelayStatus status;
    if (!loadRelayStatus(message, nowMs, status))
        LOGE("augment relay card with deadline-only state, load state failed, msgid: %lld",
             (long long)message.msgid);
    message.content = CardProtocol::augmentRelayContent(message.content, rows, status);
}

void ChatSession::augmentCardRecords(std::vector<MessageRecord>& messages)
{
    const int64_t kMillisPerSecond = 1000;
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
    for (size_t i = 0; i < messages.size(); ++i)
        augmentCardRecord(messages[i], nowMs);
}

void ChatSession::onUpdateTodo(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    UpdateTodoRequest req;
    if (!CardProtocol::parseUpdateTodoRequest(body, req))
    {
        send(cmd_update_todo, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessageRecord card;
    if (!msgMgr.getMessageById(req.msgid, card))
    {
        send(cmd_update_todo, m_seq,
             CardProtocol::buildSimpleResponse(error_code_message_not_exist, "card not exist"));
        return;
    }
    //卡片已撤回、或类型不是待办，都不接受勾选（2026-10-02 之前只判断类型，撤回后仍可勾选）
    const int targetCode = CardProtocol::checkCardTarget(card.msgtype, card.recalled, message_type_todo);
    if (targetCode != error_code_ok)
    {
        send(cmd_update_todo, m_seq,
             CardProtocol::buildSimpleResponse(targetCode,
                 targetCode == error_code_message_not_exist ? "card recalled" : "not a todo card"));
        LOGI("update todo rejected, card unusable, code: %d, userid: %d, msgid: %lld",
             targetCode, m_userinfo.userid, (long long)req.msgid);
        return;
    }
    if (!canAccessCard(card))
    {
        send(cmd_update_todo, m_seq,
             CardProtocol::buildSimpleResponse(error_code_permission_denied, "no access to this card"));
        return;
    }
    //待办按"每个负责人各自勾选"：仅负责人可勾选自己那份
    if (!CardProtocol::isTodoAssignee(card.content, m_userinfo.userid))
    {
        send(cmd_update_todo, m_seq,
             CardProtocol::buildSimpleResponse(error_code_permission_denied, "not a todo assignee"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    CardManager& cardMgr = Singleton<CardManager>::Instance();
    //勾选完成写入一行，取消勾选则删除本人的那行
    if (req.done != 0)
        cardMgr.upsertInteraction(req.msgid, m_userinfo.userid, "1", now);
    else
        cardMgr.removeInteraction(req.msgid, m_userinfo.userid);

    send(cmd_update_todo, m_seq, CardProtocol::buildSimpleResponse(0, "ok"));

    std::vector<CardInteractionRecord> rows;
    cardMgr.getCardInteractions(req.msgid, rows);
    notifyCardConversation(card, cmd_todo_changed_notify,
                           CardProtocol::buildTodoChangedNotify(req.msgid, rows));

    LOGI("update todo ok, userid: %d, msgid: %lld, done: %d",
         m_userinfo.userid, (long long)req.msgid, req.done);
}

void ChatSession::onRequestUpload(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    RequestUploadRequest req;
    if (!FileProtocol::parseRequestUploadRequest(body, req))
    {
        send(cmd_request_upload, m_seq,
             FileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //四类场景全部开放（scene 已由 parseRequestUploadRequest 限定在 1~4）：
    //头像 / 图片消息走 imgserver，聊天文件 / 群文件走 fileserver。
    //按场景校验文件大小上限——头像、图片消息各有上限；聊天文件与群文件不限大小。
    //maxSize < 0 作哨兵表示"不限"，跳过下面的大小校验。
    int64_t maxSize = 0;
    int     errcode = error_code_file_too_large;
    switch (req.scene)
    {
    case file_scene_avatar:
        maxSize = FILE_MAX_SIZE_AVATAR;
        errcode = error_code_avatar_too_large;
        break;
    case file_scene_image_msg:
        maxSize = FILE_MAX_SIZE_IMAGE_MSG;
        break;
    default:
        //聊天文件（scene=1）与群文件（scene=2）均不限大小（产品决定取消原 200MB
        //上限）：-1 表示不校验大小。二者都走 fileserver，分片流式收发、落盘时不把
        //整文件读入内存，故文件多大都不受服务端单机内存约束。
        maxSize = -1;
        break;
    }
    if (maxSize >= 0 && req.size > maxSize)
    {
        send(cmd_request_upload, m_seq,
             FileProtocol::buildSimpleResponse(errcode, "file too large"));
        return;
    }

    //企业级敏感词：聊天文件与群文件的文件名会显示在聊天记录与群文件库里，要检查（场景划分见
    //FileProtocol::isNameCheckedScene）。放在秒传判断之前，否则秒传命中的文件会跳过检查。
    //申请上传时还不知道文件要发到哪个会话，拦截记录的会话类型记 0
    if (FileProtocol::isNameCheckedScene(req.scene))
    {
        std::vector<std::string> sensitiveTexts;
        sensitiveTexts.push_back(req.name);
        if (hitSensitive(chatserver::sensitive_source_file_name, 0, 0, std::string(), sensitiveTexts))
        {
            send(cmd_request_upload, m_seq,
                 FileProtocol::buildSimpleResponse(error_code_file_sensitive_word, "file name contains sensitive word"));
            return;
        }
    }

    FileStoreManager& fileMgr = Singleton<FileStoreManager>::Instance();

    //秒传判断：库中已有同 MD5、同场景的已完成文件，则直接返回其 fileid，客户端无需上传。
    //必须同场景：下载时令牌场景与目标服务器都取自记录本身，跨场景复用会让接收方
    //连错服务器或被群文件鉴权拒绝（见 FileStoreManager::findCompletedByMd5 的注释）。
    FileRecord hit;
    if (fileMgr.findCompletedByMd5(req.md5, req.scene, hit))
    {
        send(cmd_request_upload, m_seq,
             FileProtocol::buildRequestUploadResponse(hit.fileid, true, "", "", 0, false, 0));
        LOGI("request upload hit instant-transfer, userid: %d, md5: %s, fileid: %lld, scene: %d",
             m_userinfo.userid, req.md5.c_str(), (long long)hit.fileid, req.scene);
        return;
    }

    //新建一条待上传记录
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    int64_t fileid = fileMgr.createPendingFile(req.md5, req.name, req.ext,
                                               req.size, req.scene,
                                               m_userinfo.userid, now);
    if (fileid <= 0)
    {
        send(cmd_request_upload, m_seq,
             FileProtocol::buildSimpleResponse(error_code_upload_fail, "create file record failed"));
        return;
    }

    //签发上传令牌：fileserver / imgserver 据此自校验，无需查库
    FileTokenPayload payload;
    payload.type   = FileToken::token_type_upload;
    payload.scene  = req.scene;
    payload.md5    = req.md5;
    payload.size   = req.size;
    payload.uid    = m_userinfo.userid;
    payload.expire = now + FILE_TOKEN_TTL_MS;
    std::string token = FileToken::sign(payload, fileMgr.getTokenKey());

    //按场景指明目标服务器
    std::string host;
    int port = 0;
    bool tls = false;
    fileMgr.getServerForScene(req.scene, host, port, tls);

    send(cmd_request_upload, m_seq,
         FileProtocol::buildRequestUploadResponse(fileid, false, token, host, port, tls,
                                                  static_cast<int>(FILE_CHUNK_SIZE)));
    LOGI("request upload ok, userid: %d, md5: %s, fileid: %lld, scene: %d, size: %lld",
         m_userinfo.userid, req.md5.c_str(), (long long)fileid, req.scene,
         (long long)req.size);
}

void ChatSession::onRequestDownload(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    RequestDownloadRequest req;
    if (!FileProtocol::parseRequestDownloadRequest(body, req))
    {
        send(cmd_request_download, m_seq,
             FileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    FileStoreManager& fileMgr = Singleton<FileStoreManager>::Instance();
    FileRecord rec;
    if (!fileMgr.getFileById(req.fileid, rec))
    {
        send(cmd_request_download, m_seq,
             FileProtocol::buildSimpleResponse(error_code_file_not_exist, "file not exist"));
        return;
    }

    //群文件下载需校验群成员身份（决策：群文件按群成员鉴权，其余持令牌即可下载）。
    //checkDownloadAccess 一次查询同时判定「是否群文件」与「调用者是否成员」：
    //  - 非群文件：沿用 bearer，凭下载令牌即可（与头像 / 图片消息一致）；
    //  - 群文件且调用者非成员：拒绝；
    //  - 数据库出错：放行（不因 DB 抖动阻断头像 / 图片下载），仅记日志。
    GroupFileManager& gfMgr = Singleton<GroupFileManager>::Instance();
    bool isGroupFile = false;
    bool isMember    = false;
    if (gfMgr.checkDownloadAccess(req.fileid, m_userinfo.userid, isGroupFile, isMember))
    {
        if (isGroupFile && !isMember)
        {
            send(cmd_request_download, m_seq,
                 FileProtocol::buildSimpleResponse(error_code_permission_denied,
                                                   "not a member of the group holding this file"));
            LOGI("request download denied, not group member, userid: %d, fileid: %lld",
                 m_userinfo.userid, (long long)req.fileid);
            return;
        }
    }
    else
    {
        LOGW("checkDownloadAccess db error, fall back to bearer, userid: %d, fileid: %lld",
             m_userinfo.userid, (long long)req.fileid);
    }

    //签发下载令牌
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    FileTokenPayload payload;
    payload.type   = FileToken::token_type_download;
    payload.scene  = rec.scene;
    payload.md5    = rec.md5;
    payload.size   = rec.size;
    payload.uid    = m_userinfo.userid;
    payload.expire = now + FILE_TOKEN_TTL_MS;
    std::string token = FileToken::sign(payload, fileMgr.getTokenKey());

    std::string host;
    int port = 0;
    bool tls = false;
    fileMgr.getServerForScene(rec.scene, host, port, tls);

    send(cmd_request_download, m_seq,
         FileProtocol::buildRequestDownloadResponse(rec.fileid, token, rec.md5,
                                                    rec.size, host, port, tls));
    LOGI("request download ok, userid: %d, fileid: %lld, md5: %s",
         m_userinfo.userid, (long long)req.fileid, rec.md5.c_str());
}

void ChatSession::onCheckUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    CheckUpdateRequest req;
    if (!UpdateProtocol::parseCheckUpdateRequest(body, req))
    {
        LOGE("check update bad request, client: %s", conn->peerAddress().toIpPort().c_str());
        send(cmd_check_update, m_seq,
             UpdateProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //查该平台当前已发布的最新版本。没发过版与库不可用都按"无更新"回复：
    //调用方可能正因版本过低而登不上，这一步再报错就把它彻底堵死了。
    ClientVersionInfo info;
    if (!Singleton<ClientVersionManager>::Instance().getLatestVersion(req.platform, info))
    {
        send(cmd_check_update, m_seq, UpdateProtocol::buildNoUpdateResponse(std::string()));
        return;
    }

    //客户端版本已不低于最新发布版本即为无更新。高于最新版本的内部测试包同样走这里，
    //只回"已是最新"，不提示降级。
    if (LoginProtocol::isVersionAtLeast(req.version, info.version))
    {
        send(cmd_check_update, m_seq, UpdateProtocol::buildNoUpdateResponse(info.version));
        return;
    }

    //记录里没有可用的升级包（MD5 或大小缺失）时不下发：客户端拿到也校验不过，
    //不如让它维持现状，等发布侧把记录补齐。
    if (info.packageMd5.empty() || info.packageSize <= 0)
    {
        LOGE("check update: published version %s has no usable package, platform: %d",
             info.version.c_str(), (int)req.platform);
        send(cmd_check_update, m_seq, UpdateProtocol::buildNoUpdateResponse(info.version));
        return;
    }

    //强制升级：客户端低于最低可登录版本，登录也会被拒，故不给"稍后再说"的出口
    //强制升级有两条独立来源，任一命中即为强制：
    //  ① 发布时在控制台勾了"强制升级"（f_forced）—— 发布人主观认定这个版本必须装；
    //  ② 客户端版本低于最低可登录版本 —— 它连登录都会被拒，不强制也没有别的出路。
    const bool forced = (info.forced != 0)
                     || !LoginProtocol::isVersionAtLeast(req.version, info.minVersion);

    //直接签发下载令牌，省掉一次 cmd_request_download，也让未登录的客户端能下载。
    //未登录时 m_userinfo.userid 为 0，令牌里的 uid 随之为 0——这是允许的：升级包对所有人
    //是同一份公开内容，防伪由 HMAC 签名与过期时间保证，不依赖调用方身份。
    FileStoreManager& fileMgr = Singleton<FileStoreManager>::Instance();
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    FileTokenPayload payload;
    payload.type   = FileToken::token_type_download;
    payload.scene  = file_scene_update;
    payload.md5    = info.packageMd5;
    payload.size   = info.packageSize;
    payload.uid    = m_userinfo.userid;
    payload.expire = now + FILE_TOKEN_TTL_MS;
    std::string token = FileToken::sign(payload, fileMgr.getTokenKey());

    std::string host;
    int port = 0;
    bool tls = false;
    fileMgr.getServerForScene(file_scene_update, host, port, tls);

    send(cmd_check_update, m_seq,
         UpdateProtocol::buildHasUpdateResponse(info, forced, token, host, port, tls));
    LOGI("check update has update, platform: %d, from: %s, to: %s, forced: %d, md5: %s, client: %s",
         (int)req.platform, req.version.c_str(), info.version.c_str(), (int)forced,
         info.packageMd5.c_str(), conn->peerAddress().toIpPort().c_str());
}

void ChatSession::onGetGroupFiles(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    GetGroupFilesRequest req;
    if (!GroupFileProtocol::parseGetGroupFilesRequest(body, req))
    {
        send(cmd_get_group_files, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //须为群成员才能查看群文件列表
    if (!Singleton<GroupManager>::Instance().isGroupMember(req.groupid, m_userinfo.userid))
    {
        send(cmd_get_group_files, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    std::vector<GroupFileEntry> files;
    int total = 0;
    if (!Singleton<GroupFileManager>::Instance().getGroupFiles(
            req.groupid, req.filetype, req.offset, req.count, files, total))
    {
        send(cmd_get_group_files, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_server_internal, "query group files failed"));
        return;
    }

    //从 UserManager 补全每个条目登记者的昵称
    UserManager& userMgr = Singleton<UserManager>::Instance();
    for (size_t i = 0; i < files.size(); ++i)
    {
        User u;
        u.userid = 0;
        userMgr.getUserInfoByUserId(static_cast<int32_t>(files[i].uploaderid), u);
        if (u.userid != 0)
            files[i].uploadername = u.nickname;
    }

    send(cmd_get_group_files, m_seq,
         GroupFileProtocol::buildGetGroupFilesResponse(req.groupid, req.filetype,
                                                       req.offset, total, files));
    LOGI("get group files ok, userid: %d, groupid: %lld, filetype: %d, offset: %d, "
         "returned: %u, total: %d",
         m_userinfo.userid, (long long)req.groupid, req.filetype, req.offset,
         static_cast<unsigned>(files.size()), total);
}

void ChatSession::onSearchGroupFiles(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SearchGroupFilesRequest req;
    if (!GroupFileProtocol::parseSearchGroupFilesRequest(body, req))
    {
        send(cmd_search_group_files, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        LOGI("search group files rejected: bad request, userid: %d", m_userinfo.userid);
        return;
    }

    //成员范围由查询本身限定（只 JOIN 本人的成员行），这里不必再逐群校验
    std::vector<SearchedGroupFile> files;
    bool hasMore = false;
    if (!Singleton<GroupFileManager>::Instance().searchGroupFilesByName(
            m_userinfo.userid, req.keyword, req.limit, files, hasMore))
    {
        send(cmd_search_group_files, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_server_internal, "search group files failed"));
        return;
    }

    //从 UserManager 补全每个条目登记者的昵称（与 onGetGroupFiles 相同）
    UserManager& userMgr = Singleton<UserManager>::Instance();
    for (size_t i = 0; i < files.size(); ++i)
    {
        User u;
        u.userid = 0;
        userMgr.getUserInfoByUserId(static_cast<int32_t>(files[i].entry.uploaderid), u);
        if (u.userid != 0)
            files[i].entry.uploadername = u.nickname;
    }

    send(cmd_search_group_files, m_seq,
         GroupFileProtocol::buildSearchGroupFilesResponse(req.keyword, hasMore, files));
    //搜索关键字是用户输入的内容，日志只记字节数，不记原文
    LOGI("search group files ok, userid: %d, keyword bytes: %u, limit: %d, returned: %u, hasmore: %d",
         m_userinfo.userid, static_cast<unsigned>(req.keyword.size()), req.limit,
         static_cast<unsigned>(files.size()), hasMore ? 1 : 0);
}

void ChatSession::onAddGroupFile(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    AddGroupFileRequest req;
    if (!GroupFileProtocol::parseAddGroupFileRequest(body, req))
    {
        send(cmd_add_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    //须为群成员才能登记群文件
    if (!Singleton<GroupManager>::Instance().isGroupMember(req.groupid, m_userinfo.userid))
    {
        send(cmd_add_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    //文件须已在 t_file 登记（客户端应先 cmd_request_upload 再上传字节）
    FileStoreManager& fileMgr = Singleton<FileStoreManager>::Instance();
    FileRecord frec;
    if (!fileMgr.getFileById(req.fileid, frec))
    {
        send(cmd_add_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_file_not_exist, "file not exist"));
        return;
    }

    //文件首次被引用：标记为已完成，使后续同 MD5 上传可命中秒传
    fileMgr.markCompleted(req.fileid);

    //文件类别按扩展名算定，写入 t_group_file 便于群文件列表按类型筛选
    int filetype = GroupFileProtocol::classifyFileType(frec.ext);
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    GroupFileManager& gfMgr = Singleton<GroupFileManager>::Instance();
    int64_t entryid = gfMgr.addGroupFile(req.groupid, req.fileid, filetype,
                                         m_userinfo.userid, now);
    if (entryid <= 0)
    {
        send(cmd_add_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_server_internal, "add group file failed"));
        return;
    }

    //组装条目详情：文件元数据来自 t_file，登记者昵称取当前登录用户
    GroupFileEntry entry;
    entry.entryid      = entryid;
    entry.fileid       = frec.fileid;
    entry.name         = frec.name;
    entry.ext          = frec.ext;
    entry.size         = frec.size;
    entry.md5          = frec.md5;
    entry.filetype     = filetype;
    entry.uploaderid   = m_userinfo.userid;
    entry.uploadername = m_userinfo.nickname;
    entry.uploadtime   = now;

    send(cmd_add_group_file, m_seq, GroupFileProtocol::buildAddGroupFileResponse(entry));
    LOGI("add group file ok, userid: %d, groupid: %lld, fileid: %lld, entryid: %lld",
         m_userinfo.userid, (long long)req.groupid, (long long)req.fileid, (long long)entryid);

    //向全群推送 1806（新增）
    notifyGroupMembers(req.groupid, cmd_group_files_changed_notify,
                       GroupFileProtocol::buildFileAddedNotify(req.groupid, entry));
}

void ChatSession::onDeleteGroupFile(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    DeleteGroupFileRequest req;
    if (!GroupFileProtocol::parseDeleteGroupFileRequest(body, req))
    {
        send(cmd_delete_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupFileManager& gfMgr = Singleton<GroupFileManager>::Instance();

    //取条目，确认其存在且确实属于请求声明的群（防跨群误删）
    int64_t entryGroupid = 0;
    int64_t entryFileid  = 0;
    int64_t uploaderid   = 0;
    if (!gfMgr.getEntryById(req.entryid, entryGroupid, entryFileid, uploaderid))
    {
        send(cmd_delete_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_not_found, "group file not exist"));
        return;
    }
    if (entryGroupid != req.groupid)
    {
        send(cmd_delete_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_bad_request, "entry not in this group"));
        return;
    }

    //取调用者在该群的角色：getMemberRole 返回 -1 表示非成员
    int role = Singleton<GroupManager>::Instance().getMemberRole(req.groupid, m_userinfo.userid);
    if (role < 0)
    {
        send(cmd_delete_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    //权限：登记者本人，或群主 / 管理员
    bool allowed = (uploaderid == m_userinfo.userid) || (role >= group_role_admin);
    if (!allowed)
    {
        send(cmd_delete_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_permission_denied,
                                                    "only uploader / owner / admin can delete"));
        return;
    }

    //只删 t_group_file 登记行，不动 t_file 与磁盘文件
    if (!gfMgr.deleteGroupFile(req.entryid))
    {
        send(cmd_delete_group_file, m_seq,
             GroupFileProtocol::buildSimpleResponse(error_code_server_internal, "delete group file failed"));
        return;
    }

    send(cmd_delete_group_file, m_seq,
         GroupFileProtocol::buildDeleteGroupFileResponse(req.groupid, req.entryid));
    LOGI("delete group file ok, userid: %d, groupid: %lld, entryid: %lld, fileid: %lld",
         m_userinfo.userid, (long long)req.groupid, (long long)req.entryid, (long long)entryFileid);

    //向全群推送 1806（删除）
    notifyGroupMembers(req.groupid, cmd_group_files_changed_notify,
                       GroupFileProtocol::buildFileRemovedNotify(req.groupid, req.entryid));
}

void ChatSession::makeSessionInvalid()
{
    //管理控制台的在线名单：被顶号、被管理员踢、被本人其它设备踢的会话不再计入在线。
    //必须在清零之前取 userid；之后连接断开时 userid 已是 0，ChatServer::onDisconnected 不会再通知
    const int32_t userid = m_userinfo.userid;
    m_userinfo.userid = 0;
    Singleton<AdminLinkServer>::Instance().onUserOnlineChanged(userid);
}

bool ChatSession::isSessionValid()
{
    return m_userinfo.userid > 0;
}

void ChatSession::enableHearbeatCheck()
{
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    if (conn)
    {
        //每 15 秒检测一次是否掉线
        m_checkOnlineTimerId = conn->getLoop()->runEvery(
            HEARTBEAT_CHECK_INTERVAL_US, std::bind(&ChatSession::checkHeartbeat, this, conn));
    }
}

void ChatSession::disableHeartbeatCheck()
{
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    if (conn)
    {
        LOGI("remove check online timerId, userid: %d, clientType: %d, client: %s",
             m_userinfo.userid, m_userinfo.clienttype, conn->peerAddress().toIpPort().c_str());
        conn->getLoop()->cancel(m_checkOnlineTimerId, true);
    }
}

void ChatSession::checkHeartbeat(const std::shared_ptr<TcpConnection>& conn)
{
    if (!conn)
        return;

    //收包间隔在允许范围内，视为在线
    if (time(NULL) - m_lastPackageTime < MAX_NO_PACKAGE_INTERVAL)
        return;

    //超过允许的最大收包间隔仍无数据包，判定掉线，主动断开
    conn->forceClose();
}

//==================== 文件共享（cmd_share_* 2201~2210，docs/文件共享方案.md 6.1） ====================

namespace {

//审计记录里调整成员名单摘要的最大字节数：t_file_share_audit.f_path 为 varchar(1024)，摘要只含 ASCII，
//留出余量后截断，末尾补 "..."
const size_t kShareAuditSummaryMaxBytes = 1000;

//卡片 clientmsgid 的前缀。服务端生成的消息要用一个能区分来源、不会与客户端 UUID 相撞的串，
//saveMessage 的幂等去重键是「发送方 + clientmsgid」
const char* const kShareCardClientMsgIdPrefix = "share-card-";

//向若干人各推一帧内容各不相同的通知：只取一次在线会话快照，按 userid 找到各自的内容再发送。
//逐人调用 sendToUser 会为每个人复制一遍全部在线会话，群里人多时开销与「人数 × 在线会话数」成正比
void pushDistinctBodies(uint16_t cmd, const std::map<int64_t, std::string>& bodies)
{
    if (bodies.empty())
        return;

    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        if (!(*it) || (*it)->getUserId() == 0)
            continue;
        std::map<int64_t, std::string>::const_iterator found = bodies.find(static_cast<int64_t>((*it)->getUserId()));
        if (found != bodies.end())
            (*it)->send(cmd, 0, found->second);
    }
}

//把一组 userid 拼成逗号分隔的文本，写审计摘要用
std::string joinUserIds(const std::vector<int64_t>& ids)
{
    std::string text;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i > 0)
            text += ",";
        text += std::to_string(static_cast<long long>(ids[i]));
    }
    return text;
}

//成员列表里的 userid
std::vector<int64_t> userIdsOf(const std::vector<ShareMemberSpec>& members)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < members.size(); ++i)
        ids.push_back(members[i].m_userId);
    return ids;
}

//当前时间（Unix 毫秒）
int64_t shareNowMs()
{
    const int64_t kMillisPerSecond = 1000;
    return static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
}

} // namespace

ShareConnKey ChatSession::shareConnKey() const
{
    return static_cast<ShareConnKey>(reinterpret_cast<uintptr_t>(this));
}

void ChatSession::augmentShareCardRecord(MessageRecord& message)
{
    //合并不了状态时也要去掉真实路径：2026-10-04 之前入库的卡片正文带着分享者的路径，不能原样发给别人
    ShareCardContent card;
    if (!FileShareProtocol::parseCardContent(message.content, card))
    {
        message.content = FileShareProtocol::removeCardDisplayPath(message.content);
        LOGW("augment share card skipped, bad content, msgid: %lld", (long long)message.msgid);
        return;
    }

    ShareRecord share;
    if (!Singleton<FileShareManager>::Instance().getShare(card.m_shareId, share))
    {
        message.content = FileShareProtocol::removeCardDisplayPath(message.content);
        LOGW("augment share card skipped, share not found, msgid: %lld, shareid: %lld",
             (long long)message.msgid, (long long)card.m_shareId);
        return;
    }

    ShareViewerFacts facts;
    loadShareViewerFacts(share, m_userinfo.userid, facts);
    message.content = FileSharePolicy::augmentCardFor(message.content, share, facts);
}

void ChatSession::cleanupFileShareOnOffline()
{
    const ShareConnKey key = shareConnKey();

    //结束涉及本连接的访问会话：本连接是分享者一端时告诉访问者「分享者已不可用」，是访问者一端时
    //告诉分享者「中转通道断开」
    std::vector<ShareAccessInfo> closed = Singleton<FileShareSessionManager>::Instance().closeForConnection(key);
    for (size_t i = 0; i < closed.size(); ++i)
    {
        const int errCode = (closed[i].m_ownerConn == key) ? error_code_share_unavailable
                                                            : error_code_share_relay_broken;
        notifyShareAccessEnded(std::vector<ShareAccessInfo>(1, closed[i]), errCode);
    }

    //本连接提供的共享：清除在线提供状态后向成员与分享者推新状态
    std::vector<ShareOfflineNotice> notices = Singleton<FileShareManager>::Instance().takeOfflineNotices(key);
    for (size_t i = 0; i < notices.size(); ++i)
        pushShareStateToUsers(notices[i].m_share, notices[i].m_notifyUsers, key);

    if (!closed.empty() || !notices.empty())
        LOGI("file share offline cleanup, userid: %d, closed accesses: %u, unprovided shares: %u",
             m_userinfo.userid, static_cast<unsigned>(closed.size()), static_cast<unsigned>(notices.size()));
}

void ChatSession::loadShareViewerFactsForUsers(const ShareRecord& share, const std::vector<int64_t>& users,
                                               std::vector<ShareViewerFacts>& out, ShareConnKey excludeConn)
{
    out.clear();

    //与看的人无关的部分只查一次：分享者的在线情况与在线提供状态
    ShareViewerFacts base;
    //分享者所属企业的文件共享开关：关闭时该企业的共享对访问者一律「暂不可用，功能已被关闭」
    base.m_featureEnabled = Singleton<FeatureSwitchManager>::Instance().isEnabled(share.m_enterpriseId, kFeatureFileShare);
    std::list<std::shared_ptr<ChatSession>> ownerSessions;
    Singleton<ChatServer>::Instance().getSessionsByUserId(ownerSessions, static_cast<int32_t>(share.m_ownerId));
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = ownerSessions.begin(); it != ownerSessions.end(); ++it)
    {
        if (!(*it) || !(*it)->isSessionValid())
            continue;
        //正在下线的那条连接（退出登录时它还在在线会话表里）不算分享者在线
        if (excludeConn != 0 && (*it)->shareConnKey() == excludeConn)
            continue;
        base.m_ownerOnline = true;
        if (!share.m_deviceId.empty() && (*it)->getDeviceId() == share.m_deviceId)
            base.m_ownerOnBoundDevice = true;
    }
    ShareProvideState provide;
    if (Singleton<FileShareManager>::Instance().getProvideState(share.m_shareId, provide))
    {
        base.m_hasProvideState = true;
        base.m_provideAvailable = provide.m_available;
    }

    //成员表与群成员表各查一次
    std::vector<ShareMemberSpec> members;
    Singleton<FileShareManager>::Instance().getMembers(share.m_shareId, members);
    std::map<int64_t, int> permsOf;
    for (size_t i = 0; i < members.size(); ++i)
        permsOf[members[i].m_userId] = members[i].m_perms;

    std::set<int64_t> groupUsers;
    if (share.m_ctype == conversation_group)
    {
        std::vector<GroupMemberRecord> groupMembers;
        Singleton<GroupManager>::Instance().getGroupMembers(share.m_cid, groupMembers);
        for (size_t i = 0; i < groupMembers.size(); ++i)
            groupUsers.insert(groupMembers[i].userid);
    }

    for (size_t i = 0; i < users.size(); ++i)
    {
        ShareViewerFacts facts = base;
        facts.m_viewerId = users[i];
        std::map<int64_t, int>::const_iterator it = permsOf.find(users[i]);
        facts.m_isMember = (it != permsOf.end());
        facts.m_perms = facts.m_isMember ? it->second : 0;
        facts.m_inConversation = (share.m_ctype == conversation_group) ? (groupUsers.count(users[i]) > 0) : true;
        out.push_back(facts);
    }
}

void ChatSession::loadShareViewerFacts(const ShareRecord& share, int64_t viewerId, ShareViewerFacts& facts)
{
    std::vector<ShareViewerFacts> out;
    loadShareViewerFactsForUsers(share, std::vector<int64_t>(1, viewerId), out);
    facts = out.empty() ? ShareViewerFacts() : out[0];
}

void ChatSession::pushShareStateToUsers(const ShareRecord& share, const std::vector<int64_t>& users,
                                        ShareConnKey excludeConn)
{
    std::vector<ShareViewerFacts> facts;
    loadShareViewerFactsForUsers(share, users, facts, excludeConn);

    std::map<int64_t, std::string> bodies;
    for (size_t i = 0; i < facts.size(); ++i)
    {
        const ShareStateView view = FileSharePolicy::viewFor(share, facts[i]);
        bodies[facts[i].m_viewerId] = FileShareProtocol::buildStateNotify(share.m_shareId, view);
    }
    pushDistinctBodies(cmd_share_state_notify, bodies);
    LOGI("share state pushed, shareid: %lld, users: %u", (long long)share.m_shareId, static_cast<unsigned>(users.size()));
}

int64_t ChatSession::sendShareCard(const ShareRecord& share, const std::vector<int64_t>& audience, int64_t nowMs)
{
    ShareCardContent card;
    card.m_shareId = share.m_shareId;
    card.m_kind = share.m_kind;
    card.m_name = share.m_name;
    //真实路径不写进入库的正文（2026-10-04 起）：下面逐人合并时只给分享者本人补上
    card.m_ownerId = share.m_ownerId;
    const std::string content = FileShareProtocol::buildCardContent(card);
    const std::string stored = MessageProtocol::buildStoredContent(content, std::vector<int64_t>(), false, "");

    //clientmsgid 带上共享 id、时间与首个可见成员：同一共享在同一毫秒里先后为两批新成员发卡片也不会相撞
    const std::string clientMsgId = std::string(kShareCardClientMsgIdPrefix)
        + std::to_string(static_cast<long long>(share.m_shareId)) + "-" + std::to_string(static_cast<long long>(nowMs))
        + "-" + std::to_string(static_cast<long long>(audience.empty() ? 0 : audience[0]));

    std::vector<int32_t> audience32;
    for (size_t i = 0; i < audience.size(); ++i)
        audience32.push_back(static_cast<int32_t>(audience[i]));

    const int64_t msgId = Singleton<MessageManager>::Instance().saveMessage(
        clientMsgId, share.m_ctype, static_cast<int32_t>(share.m_ownerId), static_cast<int32_t>(share.m_cid),
        message_type_fileshare, stored, nowMs, &audience32);
    if (msgId == 0)
    {
        LOGE("save share card failed, shareid: %lld, audience: %u",
             (long long)share.m_shareId, static_cast<unsigned>(audience.size()));
        return 0;
    }

    MessageRecord rec;
    rec.msgid = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype = share.m_ctype;
    rec.from = share.m_ownerId;
    rec.to = share.m_cid;
    rec.msgtype = message_type_fileshare;
    rec.timestamp = nowMs;
    rec.recalled = false;

    //逐人合并状态：成员看到自己的权限，分享者本人看到的不带权限
    const std::vector<int64_t> targets = FileSharePolicy::pushTargets(audience, share.m_ownerId);
    std::vector<ShareViewerFacts> facts;
    loadShareViewerFactsForUsers(share, targets, facts);
    std::map<int64_t, std::string> bodies;
    for (size_t i = 0; i < facts.size(); ++i)
    {
        MessageRecord personal = rec;
        personal.content = FileSharePolicy::augmentCardFor(content, share, facts[i]);
        //收件方视角的会话 id：群聊为群 id；单聊里对方看到的是分享者，分享者自己的其他设备看到的是对方
        int64_t cid = share.m_cid;
        if (share.m_ctype == conversation_single && facts[i].m_viewerId != share.m_ownerId)
            cid = share.m_ownerId;
        bodies[facts[i].m_viewerId] = MessageProtocol::buildMessageNotify(personal, cid);
    }
    pushDistinctBodies(cmd_message_notify, bodies);

    LOGI("share card sent, shareid: %lld, msgid: %lld, audience: %u, pushed users: %u",
         (long long)share.m_shareId, (long long)msgId, static_cast<unsigned>(audience.size()),
         static_cast<unsigned>(targets.size()));
    return msgId;
}

void ChatSession::notifyShareAccessEnded(const std::vector<ShareAccessInfo>& closed, int errCode)
{
    for (size_t i = 0; i < closed.size(); ++i)
    {
        std::vector<int64_t> users;
        users.push_back(closed[i].m_ownerId);
        users.push_back(closed[i].m_visitorId);
        Singleton<ChatServer>::Instance().pushToUsers(users, cmd_share_access_state_notify,
            FileShareProtocol::buildAccessEndedNotify(closed[i].m_accessId, std::string(), errCode));
        LOGI("share access ended, accessid: %s, shareid: %lld, visitor: %lld, errcode: %d",
             closed[i].m_accessId.c_str(), (long long)closed[i].m_shareId, (long long)closed[i].m_visitorId, errCode);
    }
}

void ChatSession::addShareServerAudit(const ShareRecord& share, int64_t actorId, int action,
                                      const std::string& path, int64_t nowMs, const std::string& accessId)
{
    ShareAuditRecord rec;
    rec.m_shareId = share.m_shareId;
    rec.m_ownerId = share.m_ownerId;
    rec.m_actorId = actorId;
    rec.m_enterpriseId = share.m_enterpriseId;
    rec.m_action = action;
    rec.m_source = share_audit_source_server;
    rec.m_path = path;
    rec.m_accessId = accessId;
    rec.m_createTime = nowMs;
    if (!Singleton<FileShareManager>::Instance().addAudit(rec))
        LOGE("add share audit failed, shareid: %lld, actor: %lld, action: %d",
             (long long)share.m_shareId, (long long)actorId, action);
}

int ChatSession::cancelShareAndNotify(int64_t shareId, int reason, int64_t operatorId, int64_t nowMs)
{
    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
    const int code = shareMgr.cancelShare(shareId, reason, operatorId, nowMs);
    if (code != error_code_ok)
    {
        LOGI("cancel share skipped, shareid: %lld, reason: %d, operator: %lld, code: %d",
             (long long)shareId, reason, (long long)operatorId, code);
        return code;
    }

    ShareRecord share;
    if (!shareMgr.getShare(shareId, share))
    {
        //记录刚改过却读不回来，只可能是数据库故障；推送所需的成员与分享者无从得知，记日志后返回
        LOGE("cancel share: record not readable after cancel, shareid: %lld", (long long)shareId);
        return error_code_ok;
    }

    shareMgr.clearProvideState(shareId);
    notifyShareAccessEnded(Singleton<FileShareSessionManager>::Instance().closeAllForShare(shareId),
                           error_code_share_canceled);

    //已取消对所有人都是同一个状态，推同一份内容即可
    std::vector<ShareMemberSpec> members;
    shareMgr.getMembers(shareId, members);
    const std::vector<int64_t> users = FileSharePolicy::pushTargets(
        FileSharePolicy::cardAudience(members, share.m_ownerId), share.m_ownerId);
    ShareStateView view;
    view.m_state = share_state_invalid;
    view.m_reason = reason;
    Singleton<ChatServer>::Instance().pushToUsers(users, cmd_share_state_notify,
                                                  FileShareProtocol::buildStateNotify(shareId, view));

    addShareServerAudit(share, operatorId, share_audit_cancel_share, std::string(), nowMs);
    LOGI("share canceled, shareid: %lld, reason: %d, operator: %lld, notified users: %u",
         (long long)shareId, reason, (long long)operatorId, static_cast<unsigned>(users.size()));
    return error_code_ok;
}

void ChatSession::pushFeaturesNotify(int64_t enterpriseId)
{
    if (enterpriseId <= 0)
        return;
    //1013：推给本企业的全体在线用户，包体是该企业当前的全部功能开关
    const std::string features =
        FeatureSwitchManager::buildFeaturesJson(Singleton<FeatureSwitchManager>::Instance().featuresFor(enterpriseId));
    Singleton<ChatServer>::Instance().broadcastToEnterprise(enterpriseId, cmd_features_notify, features, 0);
    LOGI("features notify pushed, enterpriseid: %lld, features: %s", (long long)enterpriseId, features.c_str());
}

void ChatSession::applyFileShareSwitch(int64_t enterpriseId, bool enabledBefore, bool enabledAfter)
{
    if (enterpriseId <= 0)
        return;
    pushFeaturesNotify(enterpriseId);
    if (enabledBefore == enabledAfter)
    {
        LOGI("file share switch unchanged, enterpriseid: %lld, enabled: %d, features pushed", (long long)enterpriseId,
             enabledAfter ? 1 : 0);
        return;
    }

    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
    std::vector<ShareRecord> shares;
    if (!shareMgr.listActiveEnterpriseShares(enterpriseId, shares))
    {
        LOGE("file share switch: list enterprise shares failed, enterpriseid: %lld", (long long)enterpriseId);
        return;
    }
    size_t closedAccesses = 0;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        //关闭：结束这个共享上的全部访问会话与传输子会话，访问者收到 2223（800）
        if (!enabledAfter)
        {
            const std::vector<ShareAccessInfo> closed = Singleton<FileShareSessionManager>::Instance().closeAllForShare(shares[i].m_shareId);
            closedAccesses += closed.size();
            notifyShareAccessEnded(closed, error_code_share_disabled);
        }
        //按新开关计算的状态推给成员与分享者（关闭时为暂不可用、功能已被关闭；打开时按在线提供状态恢复）
        std::vector<ShareMemberSpec> members;
        shareMgr.getMembers(shares[i].m_shareId, members);
        pushShareStateToUsers(shares[i], FileSharePolicy::pushTargets(
            FileSharePolicy::cardAudience(members, shares[i].m_ownerId), shares[i].m_ownerId));
    }
    LOGI("file share switch applied, enterpriseid: %lld, enabled: %d -> %d, shares: %u, closed accesses: %u",
         (long long)enterpriseId, enabledBefore ? 1 : 0, enabledAfter ? 1 : 0, static_cast<unsigned>(shares.size()),
         static_cast<unsigned>(closedAccesses));
}

void ChatSession::onShareCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    const std::vector<int64_t> noInvalid;
    ShareCreateRequest req;
    if (!FileShareProtocol::parseCreateRequest(body, req))
    {
        send(cmd_share_create, m_seq,
             FileShareProtocol::buildCreateResponse(error_code_bad_request, "bad request", 0, std::string(), noInvalid));
        LOGW("share create bad request, userid: %d", m_userinfo.userid);
        return;
    }

    const int64_t me = m_userinfo.userid;
    const int64_t nowMs = shareNowMs();
    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();

    //应答超时后的重发：直接回第一次建出的 shareid，不再做名额等校验，也不再发卡片
    const int64_t existed = shareMgr.findShareIdByClientShareId(me, req.m_clientShareId);
    if (existed > 0)
    {
        send(cmd_share_create, m_seq,
             FileShareProtocol::buildCreateResponse(error_code_ok, "ok", existed, req.m_clientShareId, noInvalid));
        LOGI("share create deduplicated, userid: %lld, clientshareid: %s, shareid: %lld",
             (long long)me, req.m_clientShareId.c_str(), (long long)existed);
        return;
    }

    //共享只由发起时的那台电脑提供，连接没有上报设备标识就无从绑定
    if (m_userinfo.deviceid.empty())
    {
        send(cmd_share_create, m_seq,
             FileShareProtocol::buildCreateResponse(error_code_bad_request, "device id required", 0,
                                                    req.m_clientShareId, noInvalid));
        LOGW("share create rejected, no device id, userid: %lld", (long long)me);
        return;
    }

    //共享名称是分享者以外的人能看到的唯一称呼，按分享者所属企业的词库检查；命中时记一条来源为
    //sensitive_source_fileshare 的拦截记录，不发起（2026-10-04 起）
    if (hitSensitive(chatserver::sensitive_source_fileshare, req.m_ctype, req.m_cid, std::string(),
                     std::vector<std::string>(1, req.m_name)))
    {
        send(cmd_share_create, m_seq,
             FileShareProtocol::buildCreateResponse(error_code_share_name_sensitive, "share name contains sensitive word",
                                                    0, req.m_clientShareId, noInvalid));
        LOGI("share create rejected, name contains sensitive word, userid: %lld, ctype: %d, cid: %lld",
             (long long)me, req.m_ctype, (long long)req.m_cid);
        return;
    }

    //查好判定所需的事实
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    ShareCheckFacts facts;
    facts.m_ownerEnterpriseId = deptMgr.getUserEnterpriseId(me);
    facts.m_featureEnabled = Singleton<FeatureSwitchManager>::Instance().isEnabled(facts.m_ownerEnterpriseId, kFeatureFileShare);
    if (req.m_ctype == conversation_group)
    {
        GroupManager& groupMgr = Singleton<GroupManager>::Instance();
        if (!groupMgr.isGroupMember(req.m_cid, m_userinfo.userid))
        {
            send(cmd_share_create, m_seq,
                 FileShareProtocol::buildCreateResponse(error_code_not_group_member, "not a group member", 0,
                                                        req.m_clientShareId, noInvalid));
            LOGI("share create rejected, not group member, userid: %lld, groupid: %lld",
                 (long long)me, (long long)req.m_cid);
            return;
        }
        std::vector<GroupMemberRecord> groupMembers;
        groupMgr.getGroupMembers(req.m_cid, groupMembers);
        for (size_t i = 0; i < groupMembers.size(); ++i)
            facts.m_conversationUsers.insert(groupMembers[i].userid);
    }
    else
    {
        facts.m_conversationUsers.insert(req.m_cid);
    }
    for (size_t i = 0; i < req.m_members.size(); ++i)
        facts.m_enterpriseOf[req.m_members[i].m_userId] = deptMgr.getUserEnterpriseId(req.m_members[i].m_userId);
    facts.m_activeShareCount = shareMgr.countActiveShares(me);
    facts.m_maxSharesPerOwner = shareMgr.getMaxSharesPerOwner();
    if (facts.m_activeShareCount < 0)
    {
        send(cmd_share_create, m_seq,
             FileShareProtocol::buildCreateResponse(error_code_server_internal, "server internal error", 0,
                                                    req.m_clientShareId, noInvalid));
        LOGE("share create failed, count active shares error, userid: %lld", (long long)me);
        return;
    }

    std::vector<int64_t> invalid;
    int code = FileSharePolicy::checkCreate(req, me, facts, invalid);
    if (code != error_code_ok)
    {
        send(cmd_share_create, m_seq,
             FileShareProtocol::buildCreateResponse(code, "share create rejected", 0, req.m_clientShareId, invalid));
        LOGI("share create rejected, userid: %lld, ctype: %d, cid: %lld, code: %d, invalid members: %s",
             (long long)me, req.m_ctype, (long long)req.m_cid, code, joinUserIds(invalid).c_str());
        return;
    }

    ShareRecord rec;
    rec.m_clientShareId = req.m_clientShareId;
    rec.m_ownerId = me;
    rec.m_enterpriseId = facts.m_ownerEnterpriseId;
    rec.m_deviceId = m_userinfo.deviceid;
    rec.m_ctype = req.m_ctype;
    rec.m_cid = req.m_cid;
    rec.m_kind = req.m_kind;
    rec.m_name = req.m_name;
    rec.m_displayPath = req.m_displayPath;
    rec.m_includeHidden = req.m_includeHidden;
    rec.m_createTime = nowMs;

    int64_t shareId = 0;
    bool created = false;
    code = shareMgr.createShare(rec, req.m_members, shareId, created);
    if (code != error_code_ok)
    {
        send(cmd_share_create, m_seq,
             FileShareProtocol::buildCreateResponse(code, "save share failed", 0, req.m_clientShareId, noInvalid));
        return;
    }

    send(cmd_share_create, m_seq,
         FileShareProtocol::buildCreateResponse(error_code_ok, "ok", shareId, req.m_clientShareId, noInvalid));

    //并发重发时另一条请求刚刚建成了同一个共享：卡片由那一条负责发
    if (!created)
        return;

    rec.m_shareId = shareId;
    rec.m_state = FileShareManager::kShareRecordActive;
    sendShareCard(rec, FileSharePolicy::cardAudience(req.m_members, me), nowMs);
    addShareServerAudit(rec, me, share_audit_create_share, std::string(), nowMs);
    LOGI("share created, shareid: %lld, owner: %lld, ctype: %d, cid: %lld, kind: %d, members: %u, device: %s",
         (long long)shareId, (long long)me, req.m_ctype, (long long)req.m_cid, req.m_kind,
         static_cast<unsigned>(req.m_members.size()), m_userinfo.deviceid.c_str());
}

void ChatSession::onShareCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t shareId = 0;
    if (!FileShareProtocol::parseShareIdRequest(body, shareId))
    {
        send(cmd_share_cancel, m_seq, FileShareProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        LOGW("share cancel bad request, userid: %d", m_userinfo.userid);
        return;
    }

    ShareRecord share;
    if (!Singleton<FileShareManager>::Instance().getShare(shareId, share))
    {
        send(cmd_share_cancel, m_seq, FileShareProtocol::buildSimpleResponse(error_code_share_not_exist, "share not exist"));
        LOGI("share cancel rejected, share not exist, userid: %d, shareid: %lld", m_userinfo.userid, (long long)shareId);
        return;
    }

    //群共享：群主与管理员也能取消，需要发起者在群里的角色；单聊没有角色，传 -1
    int groupRole = -1;
    if (share.m_ctype == conversation_group)
        groupRole = Singleton<GroupManager>::Instance().getMemberRole(share.m_cid, m_userinfo.userid);

    int reason = 0;
    int code = FileSharePolicy::checkCancel(share, m_userinfo.userid, groupRole, reason);
    if (code == error_code_ok)
        code = cancelShareAndNotify(shareId, reason, m_userinfo.userid, shareNowMs());

    send(cmd_share_cancel, m_seq,
         FileShareProtocol::buildSimpleResponse(code, code == error_code_ok ? "ok" : "share cancel rejected"));
    if (code != error_code_ok)
        LOGI("share cancel rejected, userid: %d, shareid: %lld, role: %d, code: %d",
             m_userinfo.userid, (long long)shareId, groupRole, code);
}

void ChatSession::onShareUpdateMembers(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    ShareUpdateMembersRequest req;
    if (!FileShareProtocol::parseUpdateMembersRequest(body, req))
    {
        send(cmd_share_update_members, m_seq, FileShareProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        LOGW("share update members bad request, userid: %d", m_userinfo.userid);
        return;
    }

    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
    ShareRecord share;
    if (!shareMgr.getShare(req.m_shareId, share))
    {
        send(cmd_share_update_members, m_seq,
             FileShareProtocol::buildSimpleResponse(error_code_share_not_exist, "share not exist"));
        LOGI("share update members rejected, share not exist, userid: %d, shareid: %lld",
             m_userinfo.userid, (long long)req.m_shareId);
        return;
    }

    //只有新增成员需要查会话成员与企业归属；功能开关按分享者所属企业判定
    ShareCheckFacts facts;
    facts.m_featureEnabled = Singleton<FeatureSwitchManager>::Instance().isEnabled(share.m_enterpriseId, kFeatureFileShare);
    if (!req.m_add.empty())
    {
        DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
        facts.m_ownerEnterpriseId = deptMgr.getUserEnterpriseId(share.m_ownerId);
        if (share.m_ctype == conversation_group)
        {
            std::vector<GroupMemberRecord> groupMembers;
            Singleton<GroupManager>::Instance().getGroupMembers(share.m_cid, groupMembers);
            for (size_t i = 0; i < groupMembers.size(); ++i)
                facts.m_conversationUsers.insert(groupMembers[i].userid);
        }
        else
        {
            facts.m_conversationUsers.insert(share.m_cid);
        }
        for (size_t i = 0; i < req.m_add.size(); ++i)
            facts.m_enterpriseOf[req.m_add[i].m_userId] = deptMgr.getUserEnterpriseId(req.m_add[i].m_userId);
    }

    std::vector<int64_t> invalid;
    int code = FileSharePolicy::checkUpdateMembers(share, m_userinfo.userid, req, facts, invalid);
    if (code != error_code_ok)
    {
        send(cmd_share_update_members, m_seq,
             FileShareProtocol::buildUpdateMembersResponse(code, "share update members rejected", invalid));
        LOGI("share update members rejected, userid: %d, shareid: %lld, code: %d, invalid members: %s",
             m_userinfo.userid, (long long)req.m_shareId, code, joinUserIds(invalid).c_str());
        return;
    }

    const int64_t nowMs = shareNowMs();
    ShareMemberChanges changes;
    code = shareMgr.updateMembers(req, nowMs, changes);
    if (code != error_code_ok)
    {
        send(cmd_share_update_members, m_seq, FileShareProtocol::buildSimpleResponse(code, "save members failed"));
        return;
    }
    send(cmd_share_update_members, m_seq, FileShareProtocol::buildSimpleResponse(error_code_ok, "ok"));

    //新成员：另发一张只给他们看的卡片（原卡片可能早已淹没在历史里，见方案 5.6）
    if (!changes.m_added.empty())
        sendShareCard(share, FileSharePolicy::cardAudience(changes.m_added, share.m_ownerId), nowMs);

    //被移除的人：结束他们正在进行的访问，再推「已失效」
    if (!changes.m_removed.empty())
    {
        FileShareSessionManager& sessionMgr = Singleton<FileShareSessionManager>::Instance();
        for (size_t i = 0; i < changes.m_removed.size(); ++i)
            notifyShareAccessEnded(sessionMgr.closeForMember(share.m_shareId, changes.m_removed[i]),
                                   error_code_share_not_member);
        pushShareStateToUsers(share, changes.m_removed);
    }

    //权限有变化的人：推给本人与分享者（分享者的客户端据此更新正在进行的访问里的权限检查）
    for (size_t i = 0; i < changes.m_updated.size(); ++i)
    {
        std::vector<int64_t> users;
        users.push_back(changes.m_updated[i].m_userId);
        users.push_back(share.m_ownerId);
        Singleton<ChatServer>::Instance().pushToUsers(users, cmd_share_perms_notify,
            FileShareProtocol::buildPermsNotify(share.m_shareId, changes.m_updated[i].m_userId,
                                                changes.m_updated[i].m_perms));
    }

    //审计摘要：add=新成员;remove=被移除的人;update=权限有变化的人，超长截断
    std::string summary = "add=" + joinUserIds(userIdsOf(changes.m_added))
        + ";remove=" + joinUserIds(changes.m_removed)
        + ";update=" + joinUserIds(userIdsOf(changes.m_updated));
    if (summary.size() > kShareAuditSummaryMaxBytes)
        summary = summary.substr(0, kShareAuditSummaryMaxBytes) + "...";
    addShareServerAudit(share, m_userinfo.userid, share_audit_update_members, summary, nowMs);
    LOGI("share members updated, shareid: %lld, userid: %d, added: %u, removed: %u, updated: %u",
         (long long)share.m_shareId, m_userinfo.userid, static_cast<unsigned>(changes.m_added.size()),
         static_cast<unsigned>(changes.m_removed.size()), static_cast<unsigned>(changes.m_updated.size()));
}

void ChatSession::onShareList(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    const std::vector<ShareListItem> noItems;
    ShareListRequest req;
    if (!FileShareProtocol::parseListRequest(body, req))
    {
        send(cmd_share_list, m_seq,
             FileShareProtocol::buildListResponse(error_code_bad_request, "bad request", 0, noItems));
        LOGW("share list bad request, userid: %d", m_userinfo.userid);
        return;
    }

    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
    const bool mine = (req.m_scope == share_list_scope_mine);
    std::vector<ShareRecord> records;
    const bool ok = mine ? shareMgr.listOwnerShares(m_userinfo.userid, req.m_ctype, req.m_cid, records)
                         : shareMgr.listMemberShares(m_userinfo.userid, req.m_ctype, req.m_cid, records);
    if (!ok)
    {
        send(cmd_share_list, m_seq,
             FileShareProtocol::buildListResponse(error_code_server_internal, "server internal error", req.m_scope, noItems));
        LOGE("share list failed, userid: %d, scope: %d", m_userinfo.userid, req.m_scope);
        return;
    }

    std::vector<ShareListItem> items;
    for (size_t i = 0; i < records.size(); ++i)
    {
        const ShareRecord& rec = records[i];
        ShareViewerFacts facts;
        loadShareViewerFacts(rec, m_userinfo.userid, facts);
        const ShareStateView view = FileSharePolicy::viewFor(rec, facts);

        ShareListItem item;
        item.m_shareId = rec.m_shareId;
        item.m_ownerId = rec.m_ownerId;
        item.m_ctype = rec.m_ctype;
        item.m_cid = rec.m_cid;
        item.m_kind = rec.m_kind;
        item.m_name = rec.m_name;
        item.m_displayPath = rec.m_displayPath;
        item.m_includeHidden = rec.m_includeHidden;
        item.m_state = view.m_state;
        item.m_reason = view.m_reason;
        item.m_createTime = rec.m_createTime;
        if (mine)
        {
            item.m_onThisDevice = !rec.m_deviceId.empty() && rec.m_deviceId == m_userinfo.deviceid;
            shareMgr.getMembers(rec.m_shareId, item.m_members);
        }
        else
        {
            item.m_myPerms = facts.m_perms;
        }
        items.push_back(item);
    }

    send(cmd_share_list, m_seq, FileShareProtocol::buildListResponse(error_code_ok, "ok", req.m_scope, items));
    LOGI("share list ok, userid: %d, scope: %d, ctype: %d, cid: %lld, count: %u",
         m_userinfo.userid, req.m_scope, req.m_ctype, (long long)req.m_cid, static_cast<unsigned>(items.size()));
}

void ChatSession::onShareServeReport(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::vector<ShareServeReportItem> items;
    if (!FileShareProtocol::parseServeReport(body, items))
    {
        send(cmd_share_serve_report, m_seq, FileShareProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        LOGW("share serve report bad request, userid: %d", m_userinfo.userid);
        return;
    }

    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
    size_t accepted = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ShareServeReportItem& item = items[i];
        ShareRecord share;
        if (!shareMgr.getShare(item.m_shareId, share))
        {
            LOGW("share serve report ignored, share not exist, userid: %d, shareid: %lld",
                 m_userinfo.userid, (long long)item.m_shareId);
            continue;
        }
        if (!FileSharePolicy::acceptServeReport(share, m_userinfo.userid, m_userinfo.deviceid))
        {
            LOGW("share serve report ignored, userid: %d, shareid: %lld, owner: %lld, state: %d, device: %s, bound: %s",
                 m_userinfo.userid, (long long)item.m_shareId, (long long)share.m_ownerId, share.m_state,
                 m_userinfo.deviceid.c_str(), share.m_deviceId.c_str());
            continue;
        }

        ShareProvideState previous;
        const bool hadState = shareMgr.getProvideState(item.m_shareId, previous);
        const int reason = item.m_available ? 0 : item.m_reason;
        shareMgr.setProvideState(item.m_shareId, shareConnKey(), item.m_available, reason);
        ++accepted;

        //状态没有变化时不打扰成员
        if (hadState && previous.m_available == item.m_available && previous.m_reason == reason)
            continue;

        //取消在别的连接的线程上进行，可能就发生在上面读出共享之后：推送之前再读一次，已不再有效的不推，
        //否则成员会在「已失效」之后又收到一次「可用」（第 2 期验收 SAC8 实测）。客户端同样把「已取消」当作终态
        ShareRecord latest;
        if (!shareMgr.getShare(item.m_shareId, latest) || latest.m_state != FileShareManager::kShareRecordActive)
        {
            LOGI("share provide state not pushed, share no longer active, shareid: %lld", (long long)item.m_shareId);
            continue;
        }
        std::vector<ShareMemberSpec> members;
        shareMgr.getMembers(item.m_shareId, members);
        pushShareStateToUsers(latest, FileSharePolicy::pushTargets(
            FileSharePolicy::cardAudience(members, latest.m_ownerId), latest.m_ownerId));
        LOGI("share provide state changed, shareid: %lld, available: %d, reason: %d",
             (long long)item.m_shareId, item.m_available ? 1 : 0, reason);
    }

    send(cmd_share_serve_report, m_seq, FileShareProtocol::buildSimpleResponse(error_code_ok, "ok"));
    LOGI("share serve report done, userid: %d, items: %u, accepted: %u",
         m_userinfo.userid, static_cast<unsigned>(items.size()), static_cast<unsigned>(accepted));
}

void ChatSession::handleShareMemberLeftGroup(int64_t groupId, int64_t userId)
{
    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
    std::vector<ShareRecord> shares;
    if (!shareMgr.listActiveConversationShares(conversation_group, groupId, shares))
    {
        LOGE("share cleanup on leave group failed, list shares error, groupid: %lld, userid: %lld",
             (long long)groupId, (long long)userId);
        return;
    }
    if (shares.empty())
        return;

    std::map<int64_t, std::vector<ShareMemberSpec> > membersOf;
    std::map<int64_t, ShareRecord> shareOf;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        shareMgr.getMembers(shares[i].m_shareId, membersOf[shares[i].m_shareId]);
        shareOf[shares[i].m_shareId] = shares[i];
    }

    const int64_t nowMs = shareNowMs();
    const ShareLeavePlan plan = FileSharePolicy::planUserLeftGroup(shares, membersOf, groupId, userId);
    for (size_t i = 0; i < plan.m_cancelShareIds.size(); ++i)
        cancelShareAndNotify(plan.m_cancelShareIds[i], share_invalid_owner_left_group, userId, nowMs);

    FileShareSessionManager& sessionMgr = Singleton<FileShareSessionManager>::Instance();
    for (size_t i = 0; i < plan.m_removeFromShareIds.size(); ++i)
    {
        const int64_t shareId = plan.m_removeFromShareIds[i];
        ShareUpdateMembersRequest req;
        req.m_shareId = shareId;
        req.m_remove.push_back(userId);
        ShareMemberChanges changes;
        if (shareMgr.updateMembers(req, nowMs, changes) != error_code_ok)
        {
            LOGE("share cleanup on leave group: remove member failed, shareid: %lld, userid: %lld",
                 (long long)shareId, (long long)userId);
            continue;
        }
        notifyShareAccessEnded(sessionMgr.closeForMember(shareId, userId), error_code_share_not_member);
        //他已不在群里也不在成员表里，按他的身份算出来就是「已失效，你已退出该群」
        pushShareStateToUsers(shareOf[shareId], std::vector<int64_t>(1, userId));
    }

    LOGI("share cleanup on leave group, groupid: %lld, userid: %lld, canceled: %u, removed from: %u",
         (long long)groupId, (long long)userId, static_cast<unsigned>(plan.m_cancelShareIds.size()),
         static_cast<unsigned>(plan.m_removeFromShareIds.size()));
}

void ChatSession::handleShareGroupDismissed(int64_t groupId, int64_t operatorId)
{
    std::vector<ShareRecord> shares;
    if (!Singleton<FileShareManager>::Instance().listActiveConversationShares(conversation_group, groupId, shares))
    {
        LOGE("share cleanup on dismiss group failed, list shares error, groupid: %lld", (long long)groupId);
        return;
    }

    const int64_t nowMs = shareNowMs();
    for (size_t i = 0; i < shares.size(); ++i)
        cancelShareAndNotify(shares[i].m_shareId, share_invalid_group_dismissed, operatorId, nowMs);
    if (!shares.empty())
        LOGI("share cleanup on dismiss group, groupid: %lld, canceled: %u",
             (long long)groupId, static_cast<unsigned>(shares.size()));
}

std::shared_ptr<ChatSession> ChatSession::findSessionByShareConnKey(ShareConnKey key)
{
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        if ((*it) && (*it)->isSessionValid() && (*it)->shareConnKey() == key)
            return *it;
    }
    return std::shared_ptr<ChatSession>();
}

void ChatSession::onShareOpen(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t shareId = 0;
    if (!FileShareProtocol::parseShareIdRequest(body, shareId))
    {
        send(cmd_share_open, m_seq, FileShareProtocol::buildOpenResponse(error_code_bad_request, "bad request", std::string()));
        LOGW("share open bad request, userid: %d", m_userinfo.userid);
        return;
    }

    ShareRecord share;
    if (!Singleton<FileShareManager>::Instance().getShare(shareId, share))
    {
        send(cmd_share_open, m_seq,
             FileShareProtocol::buildOpenResponse(error_code_share_not_exist, "share not exist", std::string()));
        LOGI("share open rejected, share not exist, userid: %d, shareid: %lld", m_userinfo.userid, (long long)shareId);
        return;
    }

    const int64_t me = m_userinfo.userid;
    ShareViewerFacts facts;
    loadShareViewerFacts(share, me, facts);
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    ShareEnterprisePair enterprise;
    enterprise.m_ownerEnterpriseId = deptMgr.getUserEnterpriseId(share.m_ownerId);
    enterprise.m_viewerEnterpriseId = deptMgr.getUserEnterpriseId(me);

    int code = FileSharePolicy::checkOpen(share, facts, enterprise);

    //提供该共享的那条连接：访问会话的分享者一端必须是它，中转参数只推给它
    ShareProvideState provide;
    std::shared_ptr<ChatSession> ownerSession;
    if (code == error_code_ok)
    {
        if (Singleton<FileShareManager>::Instance().getProvideState(shareId, provide))
            ownerSession = findSessionByShareConnKey(provide.m_conn);
        //判定之后、取会话之前分享者恰好断开：按暂不可用处理
        if (!ownerSession)
            code = error_code_share_unavailable;
    }
    if (code != error_code_ok)
    {
        send(cmd_share_open, m_seq, FileShareProtocol::buildOpenResponse(code, "share open rejected", std::string()));
        LOGI("share open rejected, userid: %lld, shareid: %lld, code: %d", (long long)me, (long long)shareId, code);
        return;
    }

    const int64_t nowMs = shareNowMs();
    FileShareSessionManager& sessionMgr = Singleton<FileShareSessionManager>::Instance();
    std::string accessId;
    code = sessionMgr.openAccess(shareId, share.m_ownerId, provide.m_conn, me, shareConnKey(), nowMs, accessId);
    ShareAccessInfo access;
    if (code == error_code_ok && !sessionMgr.getAccess(accessId, access))
        code = error_code_server_internal;
    if (code != error_code_ok)
    {
        send(cmd_share_open, m_seq, FileShareProtocol::buildOpenResponse(code, "share open failed", std::string()));
        LOGI("share open failed, userid: %lld, shareid: %lld, code: %d, accesses: %u",
             (long long)me, (long long)shareId, code, static_cast<unsigned>(sessionMgr.getAccessCount()));
        return;
    }

    ShareRelayInfo toOwner;
    ShareRelayInfo toVisitor;
    FileSharePolicy::makeRelayInfos(sessionMgr, access, std::string(), 0, facts.m_perms, m_userinfo.nickname, nowMs,
                                    toOwner, toVisitor);
    //带上共享名称：访问者的浏览窗口用它作标题，分享者握手时不再给出取自真实路径的根名（2026-10-04 起）
    send(cmd_share_open, m_seq, FileShareProtocol::buildOpenResponse(error_code_ok, "ok", accessId, share.m_name));
    send(cmd_share_relay_info, 0, FileShareProtocol::buildRelayInfo(toVisitor));
    ownerSession->send(cmd_share_relay_info, 0, FileShareProtocol::buildRelayInfo(toOwner));

    addShareServerAudit(share, me, share_audit_open, std::string(), nowMs, accessId);
    LOGI("share opened, shareid: %lld, accessid: %s, owner: %lld, visitor: %lld, perms: %d",
         (long long)shareId, accessId.c_str(), (long long)share.m_ownerId, (long long)me, facts.m_perms);
}

void ChatSession::onShareCloseAccess(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string accessId;
    int errCode = 0;
    if (!FileShareProtocol::parseCloseAccessRequest(body, accessId, errCode))
    {
        send(cmd_share_close_access, m_seq, FileShareProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        LOGW("share close access bad request, userid: %d", m_userinfo.userid);
        return;
    }

    FileShareSessionManager& sessionMgr = Singleton<FileShareSessionManager>::Instance();
    ShareAccessInfo access;
    //已经结束（另一端先关了、或连接断开时已收尾）：结束是幂等的，照常回成功
    if (!sessionMgr.getAccess(accessId, access))
    {
        send(cmd_share_close_access, m_seq, FileShareProtocol::buildSimpleResponse(error_code_ok, "ok"));
        return;
    }

    const int64_t me = m_userinfo.userid;
    if (me != access.m_ownerId && me != access.m_visitorId)
    {
        send(cmd_share_close_access, m_seq,
             FileShareProtocol::buildSimpleResponse(error_code_permission_denied, "not a party of this access"));
        LOGW("share close access rejected, userid: %lld, accessid: %s", (long long)me, accessId.c_str());
        return;
    }

    ShareAccessInfo closed;
    if (sessionMgr.closeAccess(accessId, closed))
    {
        //只通知另一端：发起结束的这一端已经知道
        const int64_t other = (me == closed.m_ownerId) ? closed.m_visitorId : closed.m_ownerId;
        Singleton<ChatServer>::Instance().pushToUsers(std::vector<int64_t>(1, other), cmd_share_access_state_notify,
            FileShareProtocol::buildAccessEndedNotify(accessId, std::string(), errCode));
        LOGI("share access closed, accessid: %s, shareid: %lld, by: %lld, errcode: %d, with transfer: %s",
             accessId.c_str(), (long long)closed.m_shareId, (long long)me, errCode, closed.m_transferId.c_str());
    }
    send(cmd_share_close_access, m_seq, FileShareProtocol::buildSimpleResponse(error_code_ok, "ok"));
}

void ChatSession::onShareRequestTransfer(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    ShareTransferRequest req;
    if (!FileShareProtocol::parseTransferRequest(body, req))
    {
        send(cmd_share_request_transfer, m_seq,
             FileShareProtocol::buildTransferResponse(error_code_bad_request, "bad request", std::string()));
        LOGW("share request transfer bad request, userid: %d", m_userinfo.userid);
        return;
    }

    const int64_t me = m_userinfo.userid;
    FileShareSessionManager& sessionMgr = Singleton<FileShareSessionManager>::Instance();
    ShareAccessInfo access;
    ShareRecord share;
    int code = error_code_ok;
    if (!sessionMgr.getAccess(req.m_accessId, access))
        code = error_code_share_not_exist;
    else if (access.m_visitorId != me)
        code = error_code_permission_denied;
    else if (!Singleton<FileShareManager>::Instance().getShare(access.m_shareId, share))
        code = error_code_share_not_exist;
    if (code != error_code_ok)
    {
        send(cmd_share_request_transfer, m_seq,
             FileShareProtocol::buildTransferResponse(code, "share request transfer rejected", std::string()));
        LOGI("share request transfer rejected, userid: %lld, accessid: %s, code: %d",
             (long long)me, req.m_accessId.c_str(), code);
        return;
    }

    const int64_t nowMs = shareNowMs();
    ShareViewerFacts facts;
    loadShareViewerFacts(share, me, facts);
    DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
    ShareEnterprisePair enterprise;
    enterprise.m_ownerEnterpriseId = deptMgr.getUserEnterpriseId(share.m_ownerId);
    enterprise.m_viewerEnterpriseId = deptMgr.getUserEnterpriseId(me);
    code = FileSharePolicy::checkTransfer(share, facts, enterprise, req.m_direction);

    //分享者一端是访问会话建立时的那条提供连接
    std::shared_ptr<ChatSession> ownerSession;
    if (code == error_code_ok)
    {
        ownerSession = findSessionByShareConnKey(access.m_ownerConn);
        if (!ownerSession)
            code = error_code_share_unavailable;
    }

    std::string transferId;
    if (code == error_code_ok)
        code = sessionMgr.requestTransfer(req.m_accessId, me, req.m_direction, transferId);

    //申请无论成败都记审计：被拒的申请同样是需要留痕的访问行为
    ShareAuditRecord audit = FileSharePolicy::buildTransferAuditRecord(share, me, req, nowMs);
    audit.m_result = code;
    if (!Singleton<FileShareManager>::Instance().addAudit(audit))
        LOGE("add share transfer audit failed, shareid: %lld, visitor: %lld", (long long)share.m_shareId, (long long)me);

    if (code != error_code_ok)
    {
        send(cmd_share_request_transfer, m_seq,
             FileShareProtocol::buildTransferResponse(code, "share request transfer rejected", std::string()));
        LOGI("share request transfer rejected, userid: %lld, shareid: %lld, direction: %d, code: %d, transfers: %u",
             (long long)me, (long long)share.m_shareId, req.m_direction, code,
             static_cast<unsigned>(sessionMgr.getTransferCount()));
        return;
    }

    ShareRelayInfo toOwner;
    ShareRelayInfo toVisitor;
    FileSharePolicy::makeRelayInfos(sessionMgr, access, transferId, req.m_direction, facts.m_perms, m_userinfo.nickname, nowMs,
                                    toOwner, toVisitor);

    //P2P 方案第 6 期：先尝试为这次传输在访问者（本连接，发起方）与分享者提供该共享的那条连接之间建 P2P 会话。
    //两种连接键都是 ChatSession 对象地址，可以直接互换。两端都报到了用途 fileshare、所在企业都开着 P2P 才会建成；
    //建成时两帧 2222 带上 p2psessionid，两端优先走 P2P 链路，建链失败再改走 fileserver 中转；建不成时照旧只走中转
    std::vector<P2POutFrame> p2pFrames;
    {
        P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
        P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
        std::string p2pSessionId;
        const int p2pCode = service.createForConnections(kP2PPurposeFileShare, transferId, p2pConnKey(), me,
                                                         static_cast<P2PConnKey>(access.m_ownerConn), access.m_ownerId,
                                                         p2pNowMs(), p2pSessionId, p2pFrames);
        LOGI("share transfer p2p session: transferid: %s, code: %d, p2p session: %s.", transferId.c_str(), p2pCode,
             p2pSessionId.c_str());
        toOwner.m_p2pSessionId = p2pSessionId;
        toVisitor.m_p2pSessionId = p2pSessionId;
    }

    send(cmd_share_request_transfer, m_seq, FileShareProtocol::buildTransferResponse(error_code_ok, "ok", transferId));
    send(cmd_share_relay_info, 0, FileShareProtocol::buildRelayInfo(toVisitor));
    ownerSession->send(cmd_share_relay_info, 0, FileShareProtocol::buildRelayInfo(toOwner));
    //2302 排在 2222 之后：客户端先知道这次传输带着哪个 P2P 会话，再收到会话本身
    sendP2PFrames(p2pFrames);
    LOGI("share transfer started, shareid: %lld, accessid: %s, transferid: %s, direction: %d, items: %u, bytes: %lld",
         (long long)share.m_shareId, req.m_accessId.c_str(), transferId.c_str(), req.m_direction,
         static_cast<unsigned>(req.m_items.size()), (long long)req.m_totalBytes);
}

void ChatSession::onShareTransferClose(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::string transferId;
    int result = 0;
    int errCode = 0;
    if (!FileShareProtocol::parseTransferClose(body, transferId, result, errCode))
    {
        send(cmd_share_transfer_close, m_seq, FileShareProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        LOGW("share transfer close bad request, userid: %d", m_userinfo.userid);
        return;
    }

    //传输子会话 id 形如「<访问会话 id>-t<序号>」，取出访问会话核对请求者是不是这次访问的两端之一
    const std::string transferInfix = "-t";
    const size_t pos = transferId.rfind(transferInfix);
    FileShareSessionManager& sessionMgr = Singleton<FileShareSessionManager>::Instance();
    ShareAccessInfo access;
    const bool found = (pos != std::string::npos) && sessionMgr.getAccess(transferId.substr(0, pos), access)
                       && access.m_transferId == transferId;
    //已经结束：结束是幂等的，照常回成功
    if (!found)
    {
        send(cmd_share_transfer_close, m_seq, FileShareProtocol::buildSimpleResponse(error_code_ok, "ok"));
        return;
    }

    const int64_t me = m_userinfo.userid;
    if (me != access.m_ownerId && me != access.m_visitorId)
    {
        send(cmd_share_transfer_close, m_seq,
             FileShareProtocol::buildSimpleResponse(error_code_permission_denied, "not a party of this transfer"));
        LOGW("share transfer close rejected, userid: %lld, transferid: %s", (long long)me, transferId.c_str());
        return;
    }

    ShareAccessInfo closed;
    if (sessionMgr.closeTransfer(transferId, closed))
    {
        const int64_t other = (me == closed.m_ownerId) ? closed.m_visitorId : closed.m_ownerId;
        Singleton<ChatServer>::Instance().pushToUsers(std::vector<int64_t>(1, other), cmd_share_access_state_notify,
            FileShareProtocol::buildAccessEndedNotify(closed.m_accessId, transferId, errCode));
        LOGI("share transfer closed, transferid: %s, shareid: %lld, by: %lld, result: %d, errcode: %d",
             transferId.c_str(), (long long)closed.m_shareId, (long long)me, result, errCode);
    }
    send(cmd_share_transfer_close, m_seq, FileShareProtocol::buildSimpleResponse(error_code_ok, "ok"));
}

void ChatSession::onShareAuditReport(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    std::vector<ShareAuditItem> items;
    if (!FileShareProtocol::parseAuditReport(body, items))
    {
        send(cmd_share_audit_report, m_seq, FileShareProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        LOGW("share audit report bad request, userid: %d", m_userinfo.userid);
        return;
    }

    FileShareManager& shareMgr = Singleton<FileShareManager>::Instance();
    const int64_t nowMs = shareNowMs();
    //同一批里多条记录通常属于同一个共享，按 shareid 缓存一次查询结果；查不到的记为空记录
    std::map<int64_t, ShareRecord> shares;
    std::vector<ShareAuditRecord> accepted;
    size_t ignored = 0;
    int64_t firstIgnoredShareId = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ShareAuditItem& item = items[i];
        std::map<int64_t, ShareRecord>::iterator it = shares.find(item.m_shareId);
        if (it == shares.end())
        {
            ShareRecord share;
            if (!shareMgr.getShare(item.m_shareId, share))
                share.m_shareId = 0;
            it = shares.insert(std::make_pair(item.m_shareId, share)).first;
        }
        if (it->second.m_shareId == 0 || !FileSharePolicy::acceptAuditItem(it->second, m_userinfo.userid))
        {
            if (ignored == 0)
                firstIgnoredShareId = item.m_shareId;
            ++ignored;
            continue;
        }
        accepted.push_back(FileSharePolicy::buildClientAuditRecord(it->second, item, nowMs));
    }
    //一批在一个事务里写完：逐条提交时 200 条要几秒，这段时间本网络线程上的其他连接都得不到处理
    const bool saved = shareMgr.addAudits(accepted);

    send(cmd_share_audit_report, m_seq, FileShareProtocol::buildSimpleResponse(error_code_ok, "ok"));
    //被忽略的记录每批只记一条汇总（上报者不是分享者、共享不存在）
    if (ignored > 0)
        LOGW("share audit items ignored, reporter: %d, count: %u, first shareid: %lld",
             m_userinfo.userid, static_cast<unsigned>(ignored), (long long)firstIgnoredShareId);
    LOGI("share audit report done, reporter: %d, items: %u, saved: %u",
         m_userinfo.userid, static_cast<unsigned>(items.size()), static_cast<unsigned>(saved ? accepted.size() : 0));
}

// ====================================================================
// 屏幕共享
// ====================================================================

namespace {

//屏幕共享建 P2P 会话时的用途（2302 的 purpose，客户端 FlamingoMedia.dll 以此登记业务方，P2P 方案第 6 期）
const char* const kP2PPurposeScreenShare = "screenshare";

/**
 *  单聊共享有人加入时为观看者与发起人建 P2P 会话：建成时产生的 2302 先攒在本对象里，由 onScreenShare 在发完应答与
 *  1726 之后发出（客户端先知道这次加入带着哪个 P2P 会话，再收到会话本身）。
 */
class ChatSessionShareP2PLinker : public IScreenShareP2PLinker
{
public:
    virtual bool createViewerLink(const std::string& featureSessionId, int64_t hostId, int64_t viewerId,
                                  std::string& p2pSessionId)
    {
        P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
        P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
        const int code = service.createForFeature(kP2PPurposeScreenShare, featureSessionId, hostId, viewerId,
                                                  p2pNowMs(), p2pSessionId, m_frames);
        LOGI("screenshare p2p session: feature: %s, host: %lld, viewer: %lld, code: %d, p2p session: %s.",
             featureSessionId.c_str(), (long long)hostId, (long long)viewerId, code, p2pSessionId.c_str());
        return code == error_code_ok;
    }

    std::vector<P2POutFrame> m_frames;   //建会话产生的 2302，等应答与 1726 发完之后再发
};

} // namespace

void ChatSession::onScreenShare(uint16_t cmd, const std::string& body)
{
    //第一帧是应答，用本次请求的 seq；其后的中转参数是推送，seq 填 0
    std::vector<ScreenShareReply> replies;
    //共享标题与描述的敏感词检查（第 15 期）：拦截记录给管理员看，提交者名称优先记昵称、昵称为空时记登录账号
    ChatSessionShareSensitiveChecker checker(m_userinfo.userid,
                                             m_userinfo.nickname.empty() ? m_userinfo.username : m_userinfo.nickname);
    ChatSessionShareP2PLinker p2pLinker;
    ScreenShareService::handleRequest(cmd, m_userinfo.userid, m_userinfo.nickname, body, replies, &checker, &p2pLinker);
    for (size_t i = 0; i < replies.size(); ++i)
        send(replies[i].m_cmd, i == 0 ? m_seq : 0, replies[i].m_body);
    //2302 排在应答与 1726 之后；发起人那份排在已推送的 1725 之后
    sendP2PFrames(p2pLinker.m_frames);
}

void ChatSession::onCloudRecord(uint16_t cmd, const std::string& body)
{
    CloudRecordService* service = CloudRecordService::instance();
    if (service == NULL)
    {
        //没有配置云端录制（缺 imgserver 地址或中转端口）：两种请求都回「云端存储服务不可用」
        std::vector<CloudRecordRow> none;
        if (cmd == cmd_cloudrecord_list)
            send(cmd, m_seq, CloudRecordProtocol::buildListResponse(error_code_cloudrecord_unavailable,
                                                                    "cloud storage unavailable", none, false, false, 0, 0, 0));
        else
            send(cmd, m_seq, CloudRecordProtocol::buildOpResponse(error_code_cloudrecord_unavailable,
                                                                  "cloud storage unavailable", 0, 0, NULL, NULL));
        return;
    }
    if (cmd == cmd_cloudrecord_list)
    {
        send(cmd, m_seq, service->handleList(m_userinfo.userid, body));
        return;
    }
    ChatSessionCloudRecordTitleChecker checker(m_userinfo.userid,
                                               m_userinfo.nickname.empty() ? m_userinfo.username : m_userinfo.nickname);
    const int64_t nowMs = static_cast<int64_t>(time(NULL)) * MILLISECONDS_PER_SECOND;
    send(cmd, m_seq, service->handleOp(m_userinfo.userid, body, nowMs, &checker));
}

void ChatSession::cleanupCallOnOffline()
{
    if (m_userinfo.userid <= 0)
        return;
    CallManager& mgr = Singleton<CallManager>::Instance();
    if (!mgr.isUserBusy(m_userinfo.userid))
        return;

    //本用户还有其它登录态的连接（如手机端）时仍算在线，通话不进入宽限期
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        if (*it && it->get() != this && (*it)->getUserId() == m_userinfo.userid && (*it)->isLoggedIn())
            return;
    }
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    mgr.markUserOffline(m_userinfo.userid, now);
    LOGI("call participant went offline, user: %d, the call ends unless the user logs in again within %lld ms",
         m_userinfo.userid, (long long)mgr.offlineGraceMs());
}

void ChatSession::cleanupRemoteDesktopOnOffline()
{
    if (m_userinfo.userid <= 0)
        return;

    //本用户还有其它登录态的连接（如手机端）时仍算在线，会话不进入宽限期
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        if (*it && it->get() != this && (*it)->getUserId() == m_userinfo.userid && (*it)->isLoggedIn())
            return;
    }
    const int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    Singleton<RemoteDesktopManager>::Instance().markUserOffline(m_userinfo.userid, now);
}

void ChatSession::cleanupScreenShareOnOffline()
{
    if (m_userinfo.userid <= 0 || m_screenShareOfflineDone)
        return;
    m_screenShareOfflineDone = true;

    //本用户还有其它登录态的连接（如手机端）时仍算在线，房间里的身份不变
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        if (*it && it->get() != this && (*it)->getUserId() == m_userinfo.userid && (*it)->isLoggedIn())
            return;
    }
    ScreenShareService::onUserOffline(m_userinfo.userid);
}

void ChatSession::onGroupCall(uint16_t cmd, const std::string& body)
{
    //第一帧是应答，用本次请求的 seq；其后的中转参数是推送，seq 填 0
    GroupCallServerEnv env;
    GroupCallService service(Singleton<GroupCallManager>::Instance(), env);
    std::vector<GroupCallReply> replies;
    service.handleRequest(cmd, m_userinfo.userid, m_userinfo.nickname, body,
                          static_cast<int64_t>(time(NULL)) * 1000, replies);
    for (size_t i = 0; i < replies.size(); ++i)
        send(replies[i].m_cmd, i == 0 ? m_seq : 0, replies[i].m_body);
}

void ChatSession::cleanupGroupCallOnOffline()
{
    if (m_userinfo.userid <= 0 || m_groupCallOfflineDone)
        return;
    m_groupCallOfflineDone = true;
    if (!GroupCallService::isUserInGroupCall(m_userinfo.userid))
        return;

    //本用户还有其它登录态的连接（如另一台电脑）时仍算在线，房间里的状态不变
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        if (*it && it->get() != this && (*it)->getUserId() == m_userinfo.userid && (*it)->isLoggedIn())
            return;
    }
    GroupCallServerEnv env;
    GroupCallService service(Singleton<GroupCallManager>::Instance(), env);
    service.onUserOffline(m_userinfo.userid, static_cast<int64_t>(time(NULL)) * 1000);
}

//==================== P2P 信令（cmd_p2p_* 2301~2306，docs/协议设计-v2.md 第 18 节） ====================

namespace {

//P2P 信令用的当前时间（Unix 毫秒）：会话号与回落中转令牌的有效期都以它为准
int64_t p2pNowMs()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

//P2PFeatureGate 用的「用户 → 企业」查询；不属于任何企业时返回 0
int64_t lookupUserEnterpriseForP2P(int64_t userId)
{
    return Singleton<DepartmentManager>::Instance().getUserEnterpriseId(userId);
}

} // namespace

uint64_t ChatSession::p2pConnKey() const
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
}

std::shared_ptr<ChatSession> ChatSession::findSessionByP2PConnKey(uint64_t key)
{
    std::list<std::shared_ptr<ChatSession>> sessions;
    Singleton<ChatServer>::Instance().getSessions(sessions);
    for (std::list<std::shared_ptr<ChatSession>>::iterator it = sessions.begin(); it != sessions.end(); ++it)
    {
        if ((*it) && (*it)->isSessionValid() && (*it)->p2pConnKey() == key)
            return *it;
    }
    return std::shared_ptr<ChatSession>();
}

void ChatSession::onP2PCommand(uint16_t cmd, const std::string& body)
{
    P2PRequestContext ctx;
    ctx.m_conn = p2pConnKey();
    ctx.m_userId = m_userinfo.userid;
    ctx.m_deviceId = m_userinfo.deviceid;
    ctx.m_seq = static_cast<uint32_t>(m_seq);
    ctx.m_nowMs = p2pNowMs();

    //信令处理对象无状态，每次临时构造；会话状态都在 P2PSessionManager 单例里
    P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
    P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
    std::vector<P2POutFrame> frames;
    const int code = service.handle(cmd, ctx, body, frames);
    //报到包体很短（协议版本与已登记的用途名），整个记下，排查「为什么没有建 P2P 会话」时要看它报了哪些用途
    if (cmd == cmd_p2p_register)
        LOGI("p2p register, userid: %d, body: %s", m_userinfo.userid, body.c_str());
    LOGI("p2p cmd handled, userid: %d, cmd: %u, seq: %d, code: %d, frames: %u", m_userinfo.userid, (unsigned)cmd, m_seq,
         code, static_cast<unsigned>(frames.size()));
    sendP2PFrames(frames);
}

void ChatSession::sendP2PFrames(const std::vector<P2POutFrame>& frames)
{
    const uint64_t self = p2pConnKey();
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const P2POutFrame& f = frames[i];
        //发给本连接（应答）：直接发送，不必查会话表
        if (f.m_conn == self)
        {
            send(f.m_cmd, static_cast<int32_t>(f.m_seq), f.m_body);
            continue;
        }
        //发给对端（转发与推送）：TcpSession::send 会投递到对端连接自己的线程执行
        std::shared_ptr<ChatSession> peer = findSessionByP2PConnKey(f.m_conn);
        if (!peer)
        {
            LOGW("p2p frame dropped, peer connection gone, cmd: %u, from userid: %d", (unsigned)f.m_cmd, m_userinfo.userid);
            continue;
        }
        peer->send(f.m_cmd, static_cast<int32_t>(f.m_seq), f.m_body);
        LOGI("p2p frame sent to peer, cmd: %u, from userid: %d, to userid: %d", (unsigned)f.m_cmd, m_userinfo.userid,
             peer->getUserId());
    }
}

void ChatSession::cleanupP2POnOffline()
{
    //断线收尾不查企业开关，但 P2PSignalService 的构造需要一个开关查询对象
    P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), lookupUserEnterpriseForP2P);
    P2PSignalService service(Singleton<P2PSessionManager>::Instance(), gate);
    std::vector<P2POutFrame> frames;
    service.onDisconnect(p2pConnKey(), frames);
    if (frames.empty())
        return;
    LOGI("p2p cleanup on offline, userid: %d, sessions closed: %u", m_userinfo.userid, static_cast<unsigned>(frames.size()));
    sendP2PFrames(frames);
}
