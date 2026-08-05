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
#include "../net/TcpConnection.h"
#include "../net/EventLoop.h"
#include "../net/FrameCodec.h"
#include "../zlib1.2.11/ZlibUtil.h"
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
#include "ProfileProtocol.h"
#include "RegisterProtocol.h"
#include "RegisterService.h"
#include "SmsCodeManager.h"
#include "FriendProtocol.h"
//平台公告的投递（消息盒子）：报文编解码 + 受众匹配与已读落库。
#include "AnnouncementProtocol.h"
#include "AnnouncementManager.h"
#include "MessageProtocol.h"
#include "MessageManager.h"
#include "MessagePinManager.h"
#include "GroupProtocol.h"
#include "GroupPermission.h"
#include "GroupManager.h"
#include "CardProtocol.h"
#include "CardManager.h"
#include "FileProtocol.h"
#include "FileStoreManager.h"
#include "RemoteDesktopProtocol.h"
#include "RemoteDesktopManager.h"
#include "CallProtocol.h"
#include "CallManager.h"
#include "../utils/RelayToken.h"
#include "GroupFileProtocol.h"
#include "GroupFileManager.h"
#include "../utils/FileToken.h"
#include "DepartmentManager.h"
#include "EnterpriseManager.h"

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
//服务器要求的客户端最低版本号；低于此版本拒绝登录
constexpr const char* MIN_CLIENT_VERSION = "3.0.0";
//cmd_search 单次最多返回的结果条数
constexpr int SEARCH_MAX_RESULTS = 50;
//cmd_handle_friend_request 的 action 值：1=同意 0=拒绝
constexpr int FRIEND_REQUEST_ACTION_ACCEPT = 1;
//cmd_friend_relation_changed_notify 的 changetype 值：1=成为好友 2=好友被删除
constexpr int FRIEND_RELATION_BECOME_FRIEND = 1;
//cmd_friend_relation_changed_notify 的 changetype 值：好友被删除
constexpr int FRIEND_RELATION_FRIEND_DELETED = 2;
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
//合并转发未指定标题时使用的默认标题（u8"" 确保以 UTF-8 写入 JSON）
constexpr const char* DEFAULT_MERGED_TITLE = u8"聊天记录";
//cmd_handle_join_group 的 action 值：1=同意加群申请 0=拒绝
constexpr int GROUP_JOIN_ACTION_ACCEPT = 1;

ChatSession::ChatSession(const std::shared_ptr<TcpConnection>& conn, int sessionid) :
    TcpSession(conn),
    m_id(sessionid),
    m_seq(0),
    m_isLogin(false)
{
    m_userinfo.userid = 0;
    m_userinfo.clienttype = 0;
    m_userinfo.status = 0;
    m_lastPackageTime = time(NULL);

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

        //包体可能被发送方 zlib 压缩：客户端在包体 >= COMPRESS_THRESHOLD 时压缩，
        //并置 frame_flag_compressed、originlen 填未压缩前的原始长度。FrameCodec
        //只透传不解压，须在此由会话层按 originlen 解压还原明文，再交 process 解析
        //JSON。originlen 已被 decodeFrame 校验不超过包体上限，分配安全。
        if ((header.flags & frame_flag_compressed) != 0)
        {
            std::string plainBody;
            if (!ZlibUtil::uncompressBuf(body, plainBody, header.originlen))
            {
                LOGE("uncompress v2 frame body failed, cmd: %d, bodylen: %u, originlen: %u, "
                     "close connection, client: %s",
                     (int)header.cmd, (unsigned)body.size(), header.originlen,
                     conn->peerAddress().toIpPort().c_str());
                conn->forceClose();
                return;
            }
            body.swap(plainBody);
        }

        if (!process(conn, header.cmd, header.seq, body))
        {
            LOGE("process v2 frame error, cmd: %d, close connection, client: %s",
                 header.cmd, conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }
    }
}

bool ChatSession::process(const std::shared_ptr<TcpConnection>& conn,
                          uint16_t cmd, uint32_t seq, const std::string& body)
{
    //记录请求序列号，应答时回填，使客户端能将应答与请求关联
    m_seq = static_cast<int32_t>(seq);

    //心跳过于频繁，不打印日志
    if (cmd != cmd_heartbeat)
    {
        LOGI("recv v2 frame: userid=%d, cmd=%u, seq=%u, bodylen=%u, body=%s, client=%s",
             m_userinfo.userid, (unsigned)cmd, seq, (unsigned)body.size(),
             body.c_str(), conn->peerAddress().toIpPort().c_str());
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

        case cmd_add_friend_request:
            onAddFriendRequest(body, conn);
            break;

        case cmd_handle_friend_request:
            onHandleFriendRequest(body, conn);
            break;

        case cmd_get_notifications:
            onGetNotifications(conn);
            break;

        case cmd_get_announcements:
            onGetAnnouncements(body, conn);
            break;

        case cmd_mark_announcement_read:
            onMarkAnnouncementRead(body, conn);
            break;

        case cmd_delete_friend:
            onDeleteFriend(body, conn);
            break;

        case cmd_set_friend_remark:
            onSetFriendRemark(body, conn);
            break;

        case cmd_set_blacklist:
            onSetBlacklist(body, conn);
            break;

        case cmd_set_favorite:
            onSetFavorite(body, conn);
            break;

        case cmd_set_friend_tags:
            onSetFriendTags(body, conn);
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

        case cmd_set_conversation:
            onSetConversation(body, conn);
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

        case cmd_join_group_request:
            onJoinGroupRequest(body, conn);
            break;

        case cmd_handle_join_group:
            onHandleJoinGroup(body, conn);
            break;

        case cmd_get_group_join_requests:
            onGetGroupJoinRequests(body, conn);
            break;

        case cmd_get_sensitive_words:
            onGetSensitiveWords(body, conn);
            break;

        case cmd_operate_sensitive_word:
            onOperateSensitiveWord(body, conn);
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

        case cmd_transfer_owner:
            onTransferOwner(body, conn);
            break;

        case cmd_vote_poll:
            onVotePoll(body, conn);
            break;

        case cmd_join_relay:
            onJoinRelay(body, conn);
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

        case cmd_add_group_file:
            onAddGroupFile(body, conn);
            break;

        case cmd_delete_group_file:
            onDeleteGroupFile(body, conn);
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

    //1. 客户端版本校验：低于最低版本要求则拒绝并提示升级
    if (!LoginProtocol::isVersionAtLeast(req.clientversion, MIN_CLIENT_VERSION))
    {
        LOGI("client version too old: %s, client: %s",
             req.clientversion.c_str(), conn->peerAddress().toIpPort().c_str());
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_version_too_old, "client version too old"));
        return;
    }

    //2. 账号查询：账号不存在返回未注册
    User cachedUser;
    cachedUser.userid = 0;
    Singleton<UserManager>::Instance().getUserInfoByUsername(req.account, cachedUser);
    if (cachedUser.userid == 0)
    {
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_not_registered, "account not registered"));
        return;
    }

    //3. 账号停用校验：被 admin 控制台停用的账号禁止登录
    if (cachedUser.status != 0)
    {
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_account_disabled, "account disabled"));
        return;
    }

    //4. 口令校验：MD5(客户端SHA256哈希 + 盐值) 与库中存储哈希比对
    if (!PasswordHasher::verifyPassword(req.password, cachedUser.password, cachedUser.salt))
    {
        send(cmd_login, m_seq,
             LoginProtocol::buildLoginFailResponse(error_code_wrong_password, "incorrect password"));
        return;
    }

    //5. 同类型设备顶号：踢掉同 userid 且同 clienttype 的旧会话
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

    //6. 记录本会话的登录用户信息
    m_userinfo.userid = cachedUser.userid;
    m_userinfo.username = cachedUser.username;
    m_userinfo.nickname = cachedUser.nickname;
    m_userinfo.password = cachedUser.password;
    m_userinfo.clienttype = req.clienttype;
    m_userinfo.status = req.presence;
    m_isLogin = true;

    //7. 组装并下发成功应答
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
    profile.mail = cachedUser.mail;
    //企业归属不取自 UserManager 的用户缓存（那里没有加载 f_enterprise_id），
    //而是问 DepartmentManager —— 它按 t_department_member 维护着 userid 到
    //企业ID的映射。下面广播上线通知时还要用同一个值，故只查这一次。
    const int64_t loginEnterpriseId =
        Singleton<DepartmentManager>::Instance().getUserEnterpriseId(cachedUser.userid);
    profile.enterpriseid = loginEnterpriseId;

    int64_t servertime = static_cast<int64_t>(time(NULL)) * 1000;
    send(cmd_login, m_seq,
         LoginProtocol::buildLoginSuccessResponse(cachedUser.userid, profile, servertime));

    LOGI("user login ok, userid: %d, account: %s, clienttype: %d, client: %s",
         cachedUser.userid, req.account.c_str(), req.clienttype,
         conn->peerAddress().toIpPort().c_str());

    //向本账号其它在线设备推送本设备上线通知（cmd_device_changed_notify）
    server.broadcastDeviceChanged(m_userinfo.userid, device_change_online,
                                  m_userinfo.clienttype, m_id);

    //向同企业的在线同事广播本用户上线（在线状态变更 1109）：客户端联系人树是企业组织
    //架构，同事据此把本用户的状态点实时点亮（presence=登录状态）。无企业归属（如平台
    //超管）时退回"仅好友"广播。排除发起者自己。本调用未持有会话表锁，可安全调用
    //broadcastToEnterprise（其内部自行取会话快照）。
    std::string onlineBody = ProfileProtocol::buildPresenceChangedNotify(
        m_userinfo.userid, m_userinfo.status, m_userinfo.clienttype);
    if (loginEnterpriseId != 0)
    {
        server.broadcastToEnterprise(loginEnterpriseId, cmd_presence_changed_notify,
                                     onlineBody, m_userinfo.userid);
    }
    else
    {
        notifyRelations(cmd_presence_changed_notify, onlineBody);
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
    default:
        return "register failed";
    }
}

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

    virtual bool isPhoneTaken(const std::string& phone)
    {
        User existing;
        existing.userid = 0;
        return Singleton<UserManager>::Instance().getUserInfoByPhone(phone, existing) && existing.userid != 0;
    }

    virtual bool createUser(const RegisterRequest& req, int64_t enterpriseId,
                            int64_t& outUserId, std::string& outUsername)
    {
        //v2 口令存储为 MD5(客户端SHA256哈希 + 每用户随机盐)。
        User u;
        //username 刻意留空：交给 addUser 按新分配的用户ID自动生成登录账号
        //（见 AccountName.h 的 MakeEmployeeAccount），与 admin 控制台建员工同规则。
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

        //注意 DepartmentManager 用的是 Msg.h 那套旧错误码，与本文件使用的 Protocol.h
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
        //公司根部门（参照 migrations/v3_add_enterprise_root_dept.py 的补挂逻辑）。
        LOGE("register: user created but not attached to root department, userid: %lld, enterpriseid: %lld",
             (long long)outcome.userid, (long long)outcome.enterpriseId);
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
//  marknames：本次请求发起人的"好友 → 备注名"映射（见 UserManager::getFriendMarknames）。
//             备注名是单向的，同一个联系人在不同人的通讯录里备注不同，故要按
//             发起人的映射来填；不在映射里（不是好友，或就是本人）则留空串。
static void fillContactInfo(ContactInfo& c, const User& u, ChatServer& server,
                            const std::map<int32_t, std::string>& marknames)
{
    c.userid      = u.userid;
    c.username    = u.username;
    c.nickname    = u.nickname;
    c.usertype    = u.usertype;
    c.facetype    = u.facetype;
    c.customface  = u.customface;
    c.gender      = u.gender;
    c.birthday    = u.birthday;
    c.signature   = u.signature;
    c.address     = u.address;
    c.phonenumber = u.phonenumber;
    c.mail        = u.mail;
    //在线状态与设备类型由 ChatServer 会话表实时给出，离线时为 0
    c.presence    = server.getUserStatusByUserId(u.userid);
    c.clienttype  = server.getUserClientTypeByUserId(u.userid);

    //备注名：通讯录里的同事不一定是好友，非好友查不到，一律留空串。
    std::map<int32_t, std::string>::const_iterator it = marknames.find(u.userid);
    c.remark = (it != marknames.end()) ? it->second : std::string();
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

    //2. 本人给各好友起的备注名，一次取全。备注名是单向的（只影响"本人看好友"），
    //   所以它属于本次请求的发起人，而不是联系人自身的资料，须随每个人各自的
    //   通讯录下发。取不到（本人不在缓存里）就当没有任何备注，通讯录照发。
    std::map<int32_t, std::string> marknames;
    userMgr.getFriendMarknames(m_userinfo.userid, marknames);

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
                    fillContactInfo(c, u, server, marknames);
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

    std::string resp = ContactsProtocol::buildContactsResponse(data);
    send(cmd_get_contacts, m_seq, resp);

    LOGI("get contacts ok, userid: %d, departments: %u, contacts: %u, groups: %u, favorites: %u, client: %s",
         m_userinfo.userid, static_cast<unsigned>(data.departments.size()),
         static_cast<unsigned>(data.contacts.size()),
         static_cast<unsigned>(data.groups.size()),
         static_cast<unsigned>(data.favorites.size()),
         conn->peerAddress().toIpPort().c_str());
}

//把 UserManager 的 User 转换为账号资料应答用的 UserProfile（含实时在线状态）
static void fillUserProfile(UserProfile& p, const User& u)
{
    ChatServer& server = Singleton<ChatServer>::Instance();
    p.userid       = u.userid;
    p.usertype     = u.usertype;
    p.username     = u.username;
    p.nickname     = u.nickname;
    p.facetype     = u.facetype;
    p.customface   = u.customface;
    p.gender       = u.gender;
    p.birthday     = u.birthday;
    p.signature    = u.signature;
    p.address      = u.address;
    p.phonenumber  = u.phonenumber;
    p.mail         = u.mail;
    p.enterpriseid = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(u.userid);
    p.presence     = server.getUserStatusByUserId(u.userid);
    p.clienttype   = server.getUserClientTypeByUserId(u.userid);
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

    //把本次提交的字段合并进当前资料；updateUserInfoInDb 会整体写回
    if (req.has_nickname)
        u.nickname = req.nickname;
    if (req.has_signature)
        u.signature = req.signature;
    if (req.has_gender)
        u.gender = req.gender;
    if (req.has_birthday)
        u.birthday = req.birthday;
    if (req.has_address)
        u.address = req.address;
    if (req.has_mail)
        u.mail = req.mail;

    if (!userMgr.updateUserInfoInDb(m_userinfo.userid, u))
    {
        send(cmd_update_profile, m_seq, ProfileProtocol::buildSimpleResponse(error_code_update_profile_fail, "update profile failed"));
        return;
    }

    send(cmd_update_profile, m_seq, ProfileProtocol::buildSimpleResponse(0, "ok"));

    //广播资料变更。客户端"联系人树"是企业组织架构（同事），故按企业范围推送，使同事端实时刷新
    //该联系人昵称/签名等；无企业归属时退回"仅好友"。排除发起者自己。与改头像(onChangeAvatar)、
    //在线状态(onChangePresence)同口径——此前本处只发好友，导致同企业非好友同事收不到资料变更。
    UserProfile p;
    fillUserProfile(p, u);
    std::string notifyBody = ProfileProtocol::buildUserInfoChangedNotify(p);
    int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (enterpriseId != 0)
    {
        Singleton<ChatServer>::Instance().broadcastToEnterprise(
            enterpriseId, cmd_user_info_changed_notify, notifyBody, m_userinfo.userid);
    }
    else
    {
        notifyRelations(cmd_user_info_changed_notify, notifyBody);
    }

    LOGI("update profile ok, userid: %d", m_userinfo.userid);
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

    //广播资料变更。客户端"联系人树"是企业组织架构（同事），故按企业范围推送，使同事端
    //实时刷新该联系人头像；无企业归属时退回"仅好友"。排除发起者自己。与在线状态变更同口径。
    UserProfile p;
    fillUserProfile(p, u);
    std::string notifyBody = ProfileProtocol::buildUserInfoChangedNotify(p);
    int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (enterpriseId != 0)
    {
        Singleton<ChatServer>::Instance().broadcastToEnterprise(
            enterpriseId, cmd_user_info_changed_notify, notifyBody, m_userinfo.userid);
    }
    else
    {
        notifyRelations(cmd_user_info_changed_notify, notifyBody);
    }

    LOGI("change avatar ok, userid: %d", m_userinfo.userid);
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

    //广播在线状态变更。客户端的"联系人树"是企业组织架构（同事），而非好友列表，
    //故按<u>企业范围</u>推送：同企业的在线用户都能实时刷新该联系人的状态点。无企业归属
    //（enterpriseid==0，如平台超管）时退回"仅好友"广播。排除发起者自己（其联系人树不含自己）。
    std::string notifyBody = ProfileProtocol::buildPresenceChangedNotify(
        m_userinfo.userid, req.presence, m_userinfo.clienttype);
    int64_t enterpriseId = Singleton<DepartmentManager>::Instance().getUserEnterpriseId(m_userinfo.userid);
    if (enterpriseId != 0)
    {
        Singleton<ChatServer>::Instance().broadcastToEnterprise(
            enterpriseId, cmd_presence_changed_notify, notifyBody, m_userinfo.userid);
    }
    else
    {
        notifyRelations(cmd_presence_changed_notify, notifyBody);
    }

    LOGI("change presence ok, userid: %d, presence: %d, enterpriseid: %lld",
         m_userinfo.userid, req.presence, (long long)enterpriseId);
}

void ChatSession::notifyRelations(uint16_t cmd, const std::string& body)
{
    //取本用户的关系链（好友 + 群；群无会话，遍历时自然跳过）
    std::list<User> relations;
    Singleton<UserManager>::Instance().getFriendInfoByUserId(m_userinfo.userid, relations);
    if (relations.empty())
        return;

    std::set<int32_t> relationIds;
    for (const User& r : relations)
        relationIds.insert(r.userid);

    //遍历全部在线会话，命中关系链的逐一推送（同账号多设备会各收到一份）
    std::list<std::shared_ptr<ChatSession>> allSessions;
    Singleton<ChatServer>::Instance().getSessions(allSessions);
    for (auto& s : allSessions)
    {
        if (s && relationIds.count(s->getUserId()) > 0)
            s->send(cmd, 0, body);
    }
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
                //群成员数 = 群账号关系链的条数
                std::list<User> members;
                userMgr.getFriendInfoByUserId(u.userid, members);
                it.membercount = static_cast<int>(members.size());
            }
            else
            {
                it.username  = u.username;
                it.gender    = u.gender;
                it.signature = u.signature;
                it.address   = u.address;
                it.usertype  = u.usertype;
                it.isfriend  = userMgr.isFriend(m_userinfo.userid, u.userid);
            }
            items.push_back(it);
        }
    }

    send(cmd_search, m_seq, FriendProtocol::buildSearchResponse(0, "ok", type, items));
    LOGI("search ok, userid: %d, keyword: %s, type: %d, results: %u",
         m_userinfo.userid, keyword.c_str(), type, static_cast<unsigned>(items.size()));
}

void ChatSession::onAddFriendRequest(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    AddFriendRequestData req;
    if (!FriendProtocol::parseAddFriendRequest(body, req))
    {
        send(cmd_add_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    int32_t targetId = static_cast<int32_t>(req.userid);
    UserManager& userMgr = Singleton<UserManager>::Instance();

    //不能加自己
    if (targetId == m_userinfo.userid)
    {
        send(cmd_add_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_cannot_add_self, "cannot add yourself"));
        return;
    }

    //对方必须存在
    User target;
    target.userid = 0;
    userMgr.getUserInfoByUserId(targetId, target);
    if (target.userid == 0)
    {
        send(cmd_add_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_not_found, "target user not found"));
        return;
    }

    //已是好友
    if (userMgr.isFriend(m_userinfo.userid, targetId))
    {
        send(cmd_add_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_already_friend, "already friend"));
        return;
    }

    //对方已把我拉黑：不允许发起申请（对方无感知，仅申请方收到该错误码）
    if (userMgr.isInBlacklist(targetId, m_userinfo.userid))
    {
        send(cmd_add_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_in_blacklist, "you are in the target's blacklist"));
        return;
    }

    //落库：按约定每次申请新建一条记录
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    int64_t requestId = userMgr.addFriendRequest(m_userinfo.userid, targetId,
                                                 req.verifymsg, req.source, now);
    if (requestId == 0)
    {
        send(cmd_add_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_friend_op_fail, "add friend request failed"));
        return;
    }

    send(cmd_add_friend_request, m_seq, FriendProtocol::buildAddFriendResponse(0, "ok", requestId));

    //目标在线则推送申请通知；离线则等其登录后经 cmd_get_notifications 取
    User self;
    self.userid = 0;
    userMgr.getUserInfoByUserId(m_userinfo.userid, self);
    FriendRequestItem item;
    item.requestid      = requestId;
    item.fromuserid     = m_userinfo.userid;
    item.fromusername   = self.username;
    item.fromnickname   = self.nickname;
    item.fromfacetype   = self.facetype;
    item.fromcustomface = self.customface;
    item.verifymsg      = req.verifymsg;
    item.timestamp      = now;
    sendToUser(targetId, cmd_friend_request_notify, FriendProtocol::buildFriendRequestNotify(item));

    LOGI("add friend request ok, from: %d, to: %d, requestid: %lld",
         m_userinfo.userid, targetId, (long long)requestId);
}

void ChatSession::onHandleFriendRequest(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    HandleFriendRequestData req;
    if (!FriendProtocol::parseHandleFriendRequest(body, req))
    {
        send(cmd_handle_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();
    FriendRequestRecord rec;
    if (!userMgr.getFriendRequestById(req.requestid, rec))
    {
        send(cmd_handle_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_request_not_exist, "request not exist"));
        return;
    }

    //只能由被申请方处理，且该申请须仍处于待处理状态
    if (rec.touserid != m_userinfo.userid || rec.status != 0)
    {
        send(cmd_handle_friend_request, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_request_not_exist, "request not handleable"));
        return;
    }

    bool accept = (req.action == FRIEND_REQUEST_ACTION_ACCEPT);
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    userMgr.updateFriendRequestStatus(req.requestid, accept ? 1 : 2, now);

    if (accept)
    {
        //建立双向好友关系：DB 写 t_user_relationship（小id在前），内存同步两侧好友列表
        int32_t a = rec.fromuserid;
        int32_t b = rec.touserid;
        int32_t smallId = (a < b) ? a : b;
        int32_t greatId = (a < b) ? b : a;
        userMgr.makeFriendRelationshipInDB(smallId, greatId);
        userMgr.updateUserRelationshipInMemory(a, b, FRIEND_OPERATION_ADD);
    }

    send(cmd_handle_friend_request, m_seq, FriendProtocol::buildSimpleResponse(0, "ok"));

    //把处理结果推给原申请方
    sendToUser(rec.fromuserid, cmd_friend_request_result_notify,
               FriendProtocol::buildFriendRequestResultNotify(req.requestid, rec.fromuserid, req.action));

    //同意时双方各收到一条好友关系变更通知（携带对端 userid）
    if (accept)
    {
        sendToUser(rec.fromuserid, cmd_friend_relation_changed_notify,
                   FriendProtocol::buildFriendRelationChangedNotify(rec.touserid, FRIEND_RELATION_BECOME_FRIEND));
        sendToUser(rec.touserid, cmd_friend_relation_changed_notify,
                   FriendProtocol::buildFriendRelationChangedNotify(rec.fromuserid, FRIEND_RELATION_BECOME_FRIEND));
    }

    LOGI("handle friend request ok, requestid: %lld, action: %d, by userid: %d",
         (long long)req.requestid, req.action, m_userinfo.userid);
}

void ChatSession::onGetNotifications(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    UserManager& userMgr = Singleton<UserManager>::Instance();
    std::vector<FriendRequestRecord> records;
    userMgr.getPendingFriendRequests(m_userinfo.userid, records);

    std::vector<FriendRequestItem> items;
    for (const FriendRequestRecord& rec : records)
    {
        FriendRequestItem item;
        item.requestid  = rec.id;
        item.fromuserid = rec.fromuserid;
        item.verifymsg  = rec.verifymsg;
        item.status     = rec.status;
        item.timestamp  = rec.createtime;

        //补全发起方的展示信息
        User from;
        from.userid = 0;
        userMgr.getUserInfoByUserId(rec.fromuserid, from);
        item.fromusername   = from.username;
        item.fromnickname   = from.nickname;
        item.fromfacetype   = from.facetype;
        item.fromcustomface = from.customface;

        items.push_back(item);
    }

    send(cmd_get_notifications, m_seq,
         FriendProtocol::buildGetNotificationsResponse(0, "ok", items));
    LOGI("get notifications ok, userid: %d, count: %u",
         m_userinfo.userid, static_cast<unsigned>(items.size()));
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

void ChatSession::onDeleteFriend(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    DeleteFriendRequestData req;
    if (!FriendProtocol::parseDeleteFriendRequest(body, req))
    {
        send(cmd_delete_friend, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    int32_t friendId = static_cast<int32_t>(req.userid);
    UserManager& userMgr = Singleton<UserManager>::Instance();

    //必须确实是好友才能删除
    if (!userMgr.isFriend(m_userinfo.userid, friendId))
    {
        send(cmd_delete_friend, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_not_friend, "not a friend"));
        return;
    }

    //解除双向好友关系：t_user_relationship 用小id在前的规则定位整行，
    //删行的同时备注名(f_userX_markname)随行一并删除
    int32_t smallId = (m_userinfo.userid < friendId) ? m_userinfo.userid : friendId;
    int32_t greatId = (m_userinfo.userid < friendId) ? friendId : m_userinfo.userid;
    if (!userMgr.releaseFriendRelationshipInDBAndMemory(smallId, greatId))
    {
        send(cmd_delete_friend, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_friend_op_fail, "delete friend failed"));
        return;
    }
    //releaseFriendRelationshipInDBAndMemory 内部的 deleteFriendToUser 按值拷贝迭代缓存(v1 既有缺陷)，
    //并未真正改动内存好友列表；此处再调一次按引用迭代的 updateUserRelationshipInMemory 补齐两侧缓存
    userMgr.updateUserRelationshipInMemory(m_userinfo.userid, friendId, FRIEND_OPERATION_DELETE);

    send(cmd_delete_friend, m_seq, FriendProtocol::buildSimpleResponse(0, "ok"));

    //双方各收到一条好友关系变更通知(changetype=2)，兼顾被删方与操作方的多端同步
    sendToUser(friendId, cmd_friend_relation_changed_notify,
               FriendProtocol::buildFriendRelationChangedNotify(m_userinfo.userid, FRIEND_RELATION_FRIEND_DELETED));
    sendToUser(m_userinfo.userid, cmd_friend_relation_changed_notify,
               FriendProtocol::buildFriendRelationChangedNotify(friendId, FRIEND_RELATION_FRIEND_DELETED));

    LOGI("delete friend ok, userid: %d, friendid: %d", m_userinfo.userid, friendId);
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

    int32_t friendId = static_cast<int32_t>(req.userid);
    UserManager& userMgr = Singleton<UserManager>::Instance();

    //只能给好友设置备注名
    if (!userMgr.isFriend(m_userinfo.userid, friendId))
    {
        send(cmd_set_friend_remark, m_seq,
             FriendProtocol::buildSetFriendRemarkResponse(error_code_not_friend,
                                                          "not a friend", req.userid));
        return;
    }

    //备注名是单向的：只改"本人看好友"那一侧；remark 为空串表示清除备注
    if (!userMgr.updateMarknameInDb(m_userinfo.userid, friendId, req.remark))
    {
        send(cmd_set_friend_remark, m_seq,
             FriendProtocol::buildSetFriendRemarkResponse(error_code_friend_op_fail,
                                                          "set remark failed", req.userid));
        return;
    }

    send(cmd_set_friend_remark, m_seq,
         FriendProtocol::buildSetFriendRemarkResponse(0, "ok", req.userid));
    LOGI("set friend remark ok, userid: %d, friendid: %d, remark: %s",
         m_userinfo.userid, friendId, req.remark.c_str());
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

            //够得着的判定：是好友，或者同属一个企业（企业通讯录里看得见的人都能收藏）。
            //服务号也走这条路 —— 它就是 usertype=1 的用户，同企业即可收藏。
            DepartmentManager& deptMgr = Singleton<DepartmentManager>::Instance();
            const int64_t myEnterpriseId     = deptMgr.getUserEnterpriseId(m_userinfo.userid);
            const int64_t targetEnterpriseId = deptMgr.getUserEnterpriseId(targetId);
            const bool sameEnterprise = (myEnterpriseId != 0 && myEnterpriseId == targetEnterpriseId);

            if (!sameEnterprise
                && !Singleton<UserManager>::Instance().isFriend(m_userinfo.userid, targetId))
            {
                send(cmd_set_favorite, m_seq,
                     FavoriteProtocol::buildSetFavoriteResponse(error_code_not_friend,
                                                                "not a friend or colleague",
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

    //拉黑独立于好友关系：加入/移出黑名单均不触动 t_user_relationship
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

void ChatSession::onSetFriendTags(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SetFriendTagsRequest req;
    if (!FriendProtocol::parseSetFriendTagsRequest(body, req))
    {
        send(cmd_set_friend_tags, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    int32_t friendId = static_cast<int32_t>(req.userid);
    UserManager& userMgr = Singleton<UserManager>::Instance();

    //只能给好友设置标签
    if (!userMgr.isFriend(m_userinfo.userid, friendId))
    {
        send(cmd_set_friend_tags, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_not_friend, "not a friend"));
        return;
    }

    //标签是单向的：只改"本人对该好友"的标签；空数组表示清除
    if (!userMgr.updateFriendTagsInDb(m_userinfo.userid, friendId, req.tagsJson))
    {
        send(cmd_set_friend_tags, m_seq,
             FriendProtocol::buildSimpleResponse(error_code_friend_op_fail, "set friend tags failed"));
        return;
    }

    send(cmd_set_friend_tags, m_seq, FriendProtocol::buildSimpleResponse(0, "ok"));
    LOGI("set friend tags ok, userid: %d, friendid: %d", m_userinfo.userid, friendId);
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

    //群聊消息走专门的发送路径
    if (req.ctype == CHAT_TYPE_GROUP)
    {
        sendGroupMessage(req);
        return;
    }
    //其余非单聊类型暂不支持
    if (req.ctype != CHAT_TYPE_SINGLE)
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "unsupported ctype"));
        return;
    }

    int32_t peerId = static_cast<int32_t>(req.cid);

    //不能给自己发消息
    if (peerId == m_userinfo.userid)
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_bad_request, "cannot send to yourself"));
        return;
    }

    UserManager& userMgr = Singleton<UserManager>::Instance();

    //对端用户须存在
    User peer;
    peer.userid = 0;
    userMgr.getUserInfoByUserId(peerId, peer);
    if (peer.userid == 0)
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_not_found, "target user not found"));
        return;
    }

    //被对方拉黑则拒发：消息不落库、不推送
    if (userMgr.isInBlacklist(peerId, m_userinfo.userid))
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_in_blacklist, "you are in the target's blacklist"));
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

    //落库：content/mentions/mentionall/quote 打包进 f_content
    std::string storedContent = MessageProtocol::buildStoredContent(
        req.content, req.mentions, req.mentionall, req.quote);
    int64_t msgId = msgMgr.saveMessage(req.clientmsgid, req.ctype, m_userinfo.userid,
                                       peerId, req.msgtype, storedContent, now);
    if (msgId == 0)
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_send_msg_fail, "save message failed"));
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
}

void ChatSession::sendGroupMessage(const SendMessageRequest& req)
{
    int64_t groupid = req.cid;
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();

    //发送方须为群成员；同时取回其角色与个人禁言状态。出参 mymuted 已由
    //GroupManager 按到期时间折算过 —— 定时禁言到点后这里直接是 0。
    int myrole = group_role_member;
    int mymuted = 0;
    int64_t mymuteuntil = 0;
    if (!groupMgr.getMemberRoleAndMuted(groupid, m_userinfo.userid, myrole, mymuted, &mymuteuntil))
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
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

    GroupRecord group;
    if (!groupMgr.getGroup(groupid, group))
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_group_not_exist, "group not exist"));
        return;
    }

    //禁言校验：本人被单独禁言、或全员禁言且本人非管理员 → 拒发
    if (mymuted != 0 || (group.muteall != 0 && myrole < group_role_admin))
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_member_muted, "you are muted in this group"));
        return;
    }

    //@全体校验：群设为"仅群主和管理员可@全体"时，普通成员发带 mentionall 的消息被拒
    if (req.mentionall && group.atallscope == group_at_all_owner_admin
        && myrole < group_role_admin)
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_at_all_denied,
                                                  "only owner or admin can mention all"));
        LOGI("mention all denied, groupid: %lld, userid: %d, role: %d",
             (long long)groupid, m_userinfo.userid, myrole);
        return;
    }

    //敏感词过滤：仅对开启了过滤的群、且仅对文本消息生效。非文本消息（图片 /
    //文件 / 卡片等）的 content 是结构化 JSON，拿去做子串匹配没有意义。
    if (group.sensitiveon != 0 && req.msgtype == message_type_text)
    {
        std::string hitWord;
        if (groupMgr.hitSensitiveWord(groupid, MessageProtocol::extractText(req.content), &hitWord))
        {
            send(cmd_send_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_sensitive_word,
                                                      "message contains sensitive word"));
            LOGI("message rejected by sensitive word, groupid: %lld, userid: %d, word: %s",
                 (long long)groupid, m_userinfo.userid, hitWord.c_str());
            return;
        }
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

    //落库：ctype=2、f_to 取群id
    std::string storedContent = MessageProtocol::buildStoredContent(
        req.content, req.mentions, req.mentionall, req.quote);
    int64_t msgId = msgMgr.saveMessage(req.clientmsgid, CHAT_TYPE_GROUP, m_userinfo.userid,
                                       static_cast<int32_t>(groupid), req.msgtype, storedContent, now);
    if (msgId == 0)
    {
        send(cmd_send_message, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_send_msg_fail, "save message failed"));
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
    rec.mentionall  = req.mentionall;
    rec.quote       = req.quote;
    rec.timestamp   = now;
    rec.recalled    = false;
    notifyGroupMembers(groupid, cmd_message_notify,
                       MessageProtocol::buildMessageNotify(rec, groupid));

    LOGI("send group message ok, from: %d, group: %lld, msgid: %lld, msgtype: %d",
         m_userinfo.userid, (long long)groupid, (long long)msgId, req.msgtype);
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

    //synctime 取服务器当前时间，供客户端作下次同步水位
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    send(cmd_sync_conversations, m_seq,
         MessageProtocol::buildSyncConversationsResponse(0, "ok", now, briefs));

    LOGI("sync conversations ok, userid: %d, count: %u",
         m_userinfo.userid, static_cast<unsigned>(briefs.size()));
}

void ChatSession::onGetUserGroups(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    //拉取本人加入的全部群（含无消息的空群）。getUserGroups 以 t_group_member 为
    //驱动表，返回该用户所有成员行对应的群资料（群名 / 成员数等），与"有没有消息"
    //无关 —— 客户端"群"Tab 据此显示完整群列表。
    std::vector<GroupRecord> myGroups;
    Singleton<GroupManager>::Instance().getUserGroups(m_userinfo.userid, myGroups);

    send(cmd_get_user_groups, m_seq,
         GroupProtocol::buildUserGroupsResponse(0, "ok", myGroups));

    LOGI("get user groups ok, userid: %d, count: %u",
         m_userinfo.userid, static_cast<unsigned>(myGroups.size()));
}

void ChatSession::onSyncMessages(const std::string& body, const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
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

    //群聊：须为群成员才能拉取群历史消息。同时算出历史消息的时间下界 ——
    //群设为"新成员不可查看入群前的历史消息"时，下界取本人的入群时间。
    int64_t minCreateTime = 0;
    if (req.ctype == CHAT_TYPE_GROUP)
    {
        GroupManager& groupMgr = Singleton<GroupManager>::Instance();
        if (!groupMgr.isGroupMember(req.cid, m_userinfo.userid))
        {
            send(cmd_sync_messages, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
            return;
        }

        GroupRecord group;
        if (groupMgr.getGroup(req.cid, group) && group.newseehistory == 0)
        {
            //取本人成员行的入群时间。群主与管理员不受此限 —— 这项设置是拿来
            //防新人翻旧账的，管理者需要看全量记录才能处理纠纷。
            std::vector<GroupMemberRecord> members;
            groupMgr.getGroupMembers(req.cid, members);
            for (size_t i = 0; i < members.size(); ++i)
            {
                if (members[i].userid != m_userinfo.userid)
                    continue;
                if (members[i].role < group_role_admin)
                    minCreateTime = members[i].jointime;
                break;
            }
        }
    }

    //limit 夹到合理区间：缺省 20、上限 100
    if (req.limit <= 0)
        req.limit = SYNC_MESSAGES_DEFAULT_LIMIT;
    if (req.limit > SYNC_MESSAGES_MAX_LIMIT)
        req.limit = SYNC_MESSAGES_MAX_LIMIT;

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    std::vector<MessageRecord> messages;
    bool hasMore = false;
    msgMgr.getConversationMessages(m_userinfo.userid, req, messages, hasMore, minCreateTime);

    //卡片消息（投票/接龙/待办）：把当前互动状态合并进 content，使客户端打开会话即见最新
    CardManager& cardMgr = Singleton<CardManager>::Instance();
    for (size_t i = 0; i < messages.size(); ++i)
    {
        int mt = messages[i].msgtype;
        if (mt == message_type_poll || mt == message_type_relay || mt == message_type_todo)
        {
            std::vector<CardInteractionRecord> rows;
            cardMgr.getCardInteractions(messages[i].msgid, rows);
            messages[i].content = CardProtocol::augmentCardContent(mt, messages[i].content, rows);
        }
        //自己发出的文件消息：补上对方的收取状态。发送方离线期间对方的接收 / 拒绝
        //就是靠这一步补看到的。只查本人发出的那些 —— 接收方自己知道自己收没收，
        //不必为它多查一次库。
        else if (mt == message_type_file && messages[i].from == m_userinfo.userid)
        {
            messages[i].fileackstate = msgMgr.getFileAckState(messages[i].msgid);
        }
    }

    send(cmd_sync_messages, m_seq,
         MessageProtocol::buildSyncMessagesResponse(0, "ok", req.ctype, req.cid, hasMore, messages));

    LOGI("sync messages ok, userid: %d, cid: %lld, count: %u, hasmore: %d",
         m_userinfo.userid, (long long)req.cid,
         static_cast<unsigned>(messages.size()), hasMore ? 1 : 0);
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

        if (!msgMgr.recallMessage(req.msgid))
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

    if (!msgMgr.recallMessage(req.msgid))
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
    //收发双方之一必须是我"，非参与方查出来恒为空。
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
    }

    int64_t total = 0;
    if (!Singleton<MessagePinManager>::Instance().getPinnedMessages(
            req.ctype, req.cid, m_userinfo.userid, PIN_FETCH_LIMIT, messages, pins, total))
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

    send(cmd_get_pinned_messages, m_seq,
         MessageProtocol::buildGetPinnedMessagesResponse(0, "ok", req.ctype, req.cid, total,
                                                         messages, pins));

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
                                                const std::string& clientmsgid)
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
    return msgId;
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

    //接受：给双方各签一枚 relay 令牌（同会话、相反角色），分别下发中转参数
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    std::string tokenController;
    std::string tokenControlled;
    mgr.signTokenFor(sessionId, RelayToken::role_controller, now, tokenController);
    mgr.signTokenFor(sessionId, RelayToken::role_controlled, now, tokenControlled);

    const std::string& relayIp = mgr.getRelayHost();
    int relayPort = mgr.getRelayPort();

    sendToUser(static_cast<int32_t>(s.controllerId), cmd_rd_relay_info,
               RemoteDesktopProtocol::buildRelayInfo(sessionId, RelayToken::role_controller,
                                                     relayIp, relayPort, tokenController));
    sendToUser(static_cast<int32_t>(s.controlledId), cmd_rd_relay_info,
               RemoteDesktopProtocol::buildRelayInfo(sessionId, RelayToken::role_controlled,
                                                     relayIp, relayPort, tokenControlled));

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

    send(cmd_call_invite, m_seq,
         CallProtocol::buildInviteResponse(error_code_ok, "ok", sessionId));

    sendToUser(static_cast<int32_t>(peerId), cmd_call_invite_notify,
               CallProtocol::buildInviteNotify(sessionId, m_userinfo.userid, m_userinfo.nickname, media));

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

    //拒绝：通知主叫、清会话
    if (accept == 0)
    {
        sendToUser(static_cast<int32_t>(s.callerId), cmd_call_state_notify,
                   CallProtocol::buildStateNotify(sessionId, call_state_rejected, "rejected"));
        mgr.removeSession(sessionId);
        LOGI("call rejected, session: %s, by: %d", sessionId.c_str(), m_userinfo.userid);
        return;
    }

    //接听：会话置为已接通（接通后不再受响铃超时清理影响）
    mgr.setState(sessionId, call_state_connected);

    //给双方各签一枚 relay 令牌（同会话、相反角色）
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
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

    //通知被叫已取消、清会话
    sendToUser(static_cast<int32_t>(s.calleeId), cmd_call_state_notify,
               CallProtocol::buildStateNotify(sessionId, call_state_canceled, "canceled"));
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

    //通知对端通话已结束、清会话
    int64_t peerId = (m_userinfo.userid == s.callerId) ? s.calleeId : s.callerId;
    sendToUser(static_cast<int32_t>(peerId), cmd_call_state_notify,
               CallProtocol::buildStateNotify(sessionId, call_state_ended, "ended"));
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

    //群聊：须为群成员才能设置该群会话（置顶 / 免打扰 / 删除）
    if (req.ctype == CHAT_TYPE_GROUP &&
        !Singleton<GroupManager>::Instance().isGroupMember(req.cid, m_userinfo.userid))
    {
        send(cmd_set_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    if (!msgMgr.setConversation(m_userinfo.userid, req, now))
    {
        send(cmd_set_conversation, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_server_internal, "set conversation failed"));
        return;
    }

    send(cmd_set_conversation, m_seq, MessageProtocol::buildSimpleResponse(0, "ok"));

    //读回完整状态，向本人所有在线会话推 conversation_changed_notify 做多端同步
    bool pinned = false;
    bool mute = false;
    bool deleted = false;
    msgMgr.getConversationState(m_userinfo.userid, req.ctype, req.cid, pinned, mute, deleted);
    sendToUser(m_userinfo.userid, cmd_conversation_changed_notify,
               MessageProtocol::buildConversationChangedNotify(req.ctype, req.cid,
                                                               pinned, mute, deleted));

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

    //群聊：须为群成员才能搜本群消息
    if (req.ctype == CHAT_TYPE_GROUP &&
        !Singleton<GroupManager>::Instance().isGroupMember(req.cid, m_userinfo.userid))
    {
        send(cmd_search_messages, m_seq,
             MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a group member"));
        return;
    }

    //limit 夹到合理区间：复用 sync_messages 的缺省 20、上限 100
    if (req.limit <= 0)
        req.limit = SYNC_MESSAGES_DEFAULT_LIMIT;
    if (req.limit > SYNC_MESSAGES_MAX_LIMIT)
        req.limit = SYNC_MESSAGES_MAX_LIMIT;

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    std::vector<MessageRecord> messages;
    bool hasMore = false;
    msgMgr.searchMessages(m_userinfo.userid, req, messages, hasMore);

    //search 应答与 sync_messages 同构，复用 buildSyncMessagesResponse
    send(cmd_search_messages, m_seq,
         MessageProtocol::buildSyncMessagesResponse(0, "ok", req.ctype, req.cid, hasMore, messages));

    LOGI("search messages ok, userid: %d, cid: %lld, count: %u",
         m_userinfo.userid, (long long)req.cid, static_cast<unsigned>(messages.size()));
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

    //目标会话校验：单聊查对端存在 + 黑名单；群聊查本人是否为该群成员
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
        if (!Singleton<GroupManager>::Instance().isGroupMember(req.tocid, m_userinfo.userid))
        {
            send(cmd_forward_message, m_seq,
                 MessageProtocol::buildSimpleResponse(error_code_not_group_member, "not a member of the target group"));
            return;
        }
    }

    //取转发源消息；只保留本人可见且未撤回的（单聊须为收发方之一、群聊须为该群成员）
    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    std::vector<MessageRecord> sources;
    msgMgr.getMessagesByIds(req.msgids, sources);
    std::vector<MessageRecord> visible;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const MessageRecord& s = sources[i];
        if (s.recalled)
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

    std::vector<int64_t> newMsgIds;
    if (req.merge)
    {
        //合并转发：所有源消息打包成一条 msgtype=15 消息
        std::string title = req.title.empty() ? std::string(DEFAULT_MERGED_TITLE) : req.title;
        std::string mergedContent = MessageProtocol::buildMergedContent(title, visible);
        int64_t mid = deliverConversationMessage(req.toctype, req.tocid, MSGTYPE_MERGED_FORWARD,
                                                 mergedContent, req.clientmsgid);
        if (mid != 0)
            newMsgIds.push_back(mid);
    }
    else
    {
        //逐条转发：每条源消息在目标会话各新建一条，clientmsgid 按 "原ID-序号" 派生
        for (size_t i = 0; i < visible.size(); ++i)
        {
            std::string cmid = req.clientmsgid + "-" + std::to_string(i);
            int64_t mid = deliverConversationMessage(req.toctype, req.tocid,
                                                     visible[i].msgtype, visible[i].content, cmid);
            if (mid != 0)
                newMsgIds.push_back(mid);
        }
    }

    send(cmd_forward_message, m_seq,
         MessageProtocol::buildForwardMessageResponse(0, "ok", newMsgIds));
    LOGI("forward message ok, userid: %d, toctype: %d, tocid: %lld, merge: %d, count: %u",
         m_userinfo.userid, req.toctype, (long long)req.tocid, req.merge ? 1 : 0,
         static_cast<unsigned>(newMsgIds.size()));
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
    //本端删除为每用户软删除：本人其他设备下次 sync_messages 时该消息已被过滤，无需额外推送
    LOGI("delete message ok, userid: %d, msgid: %lld", m_userinfo.userid, (long long)msgid);
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

    UserManager& userMgr = Singleton<UserManager>::Instance();
    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    std::vector<MassSendResult> results;
    for (size_t i = 0; i < req.targets.size(); ++i)
    {
        const MassSendTarget& t = req.targets[i];
        MassSendResult r;
        r.cid = t.cid;
        r.msgid = 0;

        //每条派生独立 clientmsgid，便于各目标分别去重
        std::string cmid = req.clientmsgid + "-" + std::to_string(i);
        //单聊：对端存在、非本人、未被对方拉黑才发；群聊：本人为群成员才发；其余跳过（msgid 留 0）
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
            if (groupMgr.isGroupMember(t.cid, m_userinfo.userid))
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

    UserManager& userMgr = Singleton<UserManager>::Instance();

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
    int64_t groupid = groupMgr.createGroup(req.name, m_userinfo.userid, members, now);
    if (groupid == 0)
    {
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

    LOGI("create group ok, groupid: %lld, owner: %d, members: %u",
         (long long)groupid, m_userinfo.userid, static_cast<unsigned>(members.size()));
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

    send(cmd_dismiss_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

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
}

void ChatSession::notifyGroupMembers(int64_t groupid, uint16_t cmd, const std::string& body)
{
    std::vector<GroupMemberRecord> members;
    Singleton<GroupManager>::Instance().getGroupMembers(groupid, members);
    for (size_t i = 0; i < members.size(); ++i)
        sendToUser(static_cast<int32_t>(members[i].userid), cmd, body);
}

void ChatSession::onGetSensitiveWords(const std::string& body,
                                      const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    int64_t groupid = 0;
    if (!GroupProtocol::parseGroupIdRequest(body, groupid))
    {
        send(cmd_get_sensitive_words, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_get_sensitive_words, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin,
                                                "only owner or admin can view sensitive words"));
        return;
    }

    std::vector<GroupSensitiveWordRecord> words;
    groupMgr.getSensitiveWords(groupid, words);
    send(cmd_get_sensitive_words, m_seq,
         GroupProtocol::buildSensitiveWordsResponse(0, "ok", groupid, words));

    LOGI("get sensitive words ok, groupid: %lld, userid: %d, count: %d",
         (long long)groupid, m_userinfo.userid, static_cast<int>(words.size()));
}

void ChatSession::onOperateSensitiveWord(const std::string& body,
                                         const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    SensitiveWordOpRequest req;
    if (!GroupProtocol::parseSensitiveWordOpRequest(body, req))
    {
        send(cmd_operate_sensitive_word, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_bad_request, "bad request"));
        return;
    }

    GroupManager& groupMgr = Singleton<GroupManager>::Instance();
    const int myrole = groupMgr.getMemberRole(req.groupid, m_userinfo.userid);
    if (myrole < group_role_admin)
    {
        send(cmd_operate_sensitive_word, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_not_group_admin,
                                                "only owner or admin can edit sensitive words"));
        return;
    }

    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    if (req.operation == sensitive_word_add)
    {
        //重复的词返回 0，但对调用方而言"这个词现在在库里"这一目标已经达成，
        //故仍回 ok —— 让界面上重复添加同一个词表现为无害的幂等操作。
        groupMgr.addSensitiveWord(req.groupid, req.word, now);
    }
    else
    {
        groupMgr.deleteSensitiveWord(req.groupid, req.id);
    }

    send(cmd_operate_sensitive_word, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    LOGI("operate sensitive word ok, groupid: %lld, userid: %d, operation: %d",
         (long long)req.groupid, m_userinfo.userid, req.operation);
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
    if (req.id > 0)
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
    sendAnnouncementSystemMessage(item);

    LOGI("publish group announcement ok, groupid: %lld, id: %lld, by: %d",
         (long long)req.groupid, (long long)newId, m_userinfo.userid);
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

    fillAnnouncementPublisherName(item);
    notifyAnnouncementChanged(item, group_announcement_change_delete);

    LOGI("delete group announcement ok, groupid: %lld, id: %lld, by: %d",
         (long long)groupid, (long long)id, m_userinfo.userid);
}

void ChatSession::notifyAnnouncementChanged(const GroupAnnouncementRecord& item, int changetype)
{
    notifyGroupMembers(item.groupid, cmd_group_announcement_notify,
                       GroupProtocol::buildAnnouncementNotify(item, changetype));

    //公告快照四列刚被 refreshLatestAnnouncement 改过，读回最新群资料再推一条群资料
    //变更 —— 群资料对话框与群聊窗头部摘要行只认这条。
    GroupRecord updated;
    if (Singleton<GroupManager>::Instance().getGroup(item.groupid, updated))
    {
        notifyGroupMembers(item.groupid, cmd_group_info_changed_notify,
                           GroupProtocol::buildGroupInfoChangedNotify(updated, group_info_change_updated));
    }
}

void ChatSession::sendAnnouncementSystemMessage(const GroupAnnouncementRecord& item)
{
    //发布人昵称取不到时退而用"有人"，不让提示语里出现一个空的书名号
    std::string who = item.publishername;
    if (who.empty())
        who = u8"有人";

    const std::string text = u8"「" + who + u8"」发布了群公告：" + item.title;

    //系统消息的 content 与文本消息同构（只有一个 text 字段），客户端的文本渲染
    //路径可以直接复用。
    const std::string content = MessageProtocol::buildTextContent(text);

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

    //系统消息由服务器生成，没有客户端消息ID可用；拼一个带公告ID的串占位，保证
    //saveMessage 的幂等去重键不会与真实客户端消息撞上。
    const std::string clientMsgId = "sys-announce-" + std::to_string(item.groupid)
                                    + "-" + std::to_string(item.id);

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    const int64_t msgId = msgMgr.saveMessage(clientMsgId, CHAT_TYPE_GROUP,
                                             kSystemMessageSenderId,
                                             static_cast<int32_t>(item.groupid),
                                             message_type_system, storedContent, now);
    if (msgId == 0)
    {
        LOGE("save announcement system message failed, groupid: %lld, id: %lld",
             (long long)item.groupid, (long long)item.id);
        return;
    }

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype       = CHAT_TYPE_GROUP;
    rec.from        = kSystemMessageSenderId;
    rec.to          = item.groupid;
    rec.msgtype     = message_type_system;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = now;
    rec.recalled    = false;
    notifyGroupMembers(item.groupid, cmd_message_notify,
                       MessageProtocol::buildMessageNotify(rec, item.groupid));

    LOGI("group announcement system message sent, groupid: %lld, announceid: %lld, msgid: %lld",
         (long long)item.groupid, (long long)item.id, (long long)msgId);
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

    //把新成员的昵称拼在欢迎语前面，形如"张三、李四 欢迎加入本群"。
    //取不到昵称的（用户已注销等）跳过，不让空名字污染这句话。
    UserManager& userMgr = Singleton<UserManager>::Instance();
    std::string names;
    for (size_t i = 0; i < newMembers.size(); ++i)
    {
        User u;
        if (!userMgr.getUserInfoByUserId(static_cast<int32_t>(newMembers[i]), u))
            continue;
        if (u.nickname.empty())
            continue;

        if (!names.empty())
            names += u8"、";
        names += u.nickname;
    }

    std::string text = group.welcometext;
    if (!names.empty())
        text = names + u8" " + group.welcometext;

    //系统消息的 content 与文本消息同构（只有一个 text 字段），客户端的
    //文本渲染路径可以直接复用。
    const std::string content = MessageProtocol::buildTextContent(text);

    //落库前还要再套一层 buildStoredContent 的外壳，理由同
    //sendAnnouncementSystemMessage：读回时 parseStoredContent 只认最外层的 content
    //字段，裸 content 会让重新登录后的会话摘要退化成"[消息]"、历史里正文为空。
    std::vector<int64_t> noMentions;
    const std::string storedContent =
        MessageProtocol::buildStoredContent(content, noMentions, false, "");

    const int64_t kMillisPerSecond = 1000;
    const int64_t now = static_cast<int64_t>(time(NULL)) * kMillisPerSecond;

    //系统消息由服务器生成，没有客户端消息ID可用；拼一个带群ID与时间戳的串
    //占位，保证 saveMessage 的幂等去重键不会与真实客户端消息撞上。
    const std::string clientMsgId = "sys-welcome-" + std::to_string(group.groupid)
                                    + "-" + std::to_string(now);

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    const int64_t msgId = msgMgr.saveMessage(clientMsgId, CHAT_TYPE_GROUP,
                                             kSystemMessageSenderId,
                                             static_cast<int32_t>(group.groupid),
                                             message_type_system, storedContent, now);
    if (msgId == 0)
    {
        LOGE("save group welcome message failed, groupid: %lld", (long long)group.groupid);
        return;
    }

    MessageRecord rec;
    rec.msgid       = msgId;
    rec.clientmsgid = clientMsgId;
    rec.ctype       = CHAT_TYPE_GROUP;
    rec.from        = kSystemMessageSenderId;
    rec.to          = group.groupid;
    rec.msgtype     = message_type_system;
    rec.content     = content;
    rec.mentionall  = false;
    rec.timestamp   = now;
    rec.recalled    = false;
    notifyGroupMembers(group.groupid, cmd_message_notify,
                       MessageProtocol::buildMessageNotify(rec, group.groupid));

    LOGI("group welcome message sent, groupid: %lld, msgid: %lld, newmembers: %d",
         (long long)group.groupid, (long long)msgId, static_cast<int>(newMembers.size()));
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

    //没有可邀请的有效用户：无人变动，直接回 ok
    if (toAdd.empty())
    {
        send(cmd_invite_to_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;

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
            const int64_t requestId = groupMgr.addJoinRequest(
                req.groupid, static_cast<int32_t>(toAdd[i]), std::string(), now,
                group_join_source_friend, m_userinfo.userid);
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

        send(cmd_invite_to_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));
        LOGI("invite converted to join requests, groupid: %lld, by: %d, converted: %d",
             (long long)req.groupid, m_userinfo.userid, converted);
        return;
    }

    if (!groupMgr.addMembers(req.groupid, toAdd, now))
    {
        send(cmd_invite_to_group, m_seq,
             GroupProtocol::buildSimpleResponse(error_code_server_internal, "invite failed"));
        return;
    }

    send(cmd_invite_to_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //向全体成员（含新成员）推 member_changed_notify（加入）
    std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
        req.groupid, group_member_change_join, toAdd, m_userinfo.userid);
    notifyGroupMembers(req.groupid, cmd_group_member_changed_notify, notify);

    //入群欢迎语须在成员变动通知<u>之后</u>发：客户端要先把新成员并进成员列表，
    //才能把欢迎语里提到的人对应上。
    sendGroupWelcomeMessage(group, toAdd);

    LOGI("invite to group ok, groupid: %lld, by: %d, added: %u",
         (long long)req.groupid, m_userinfo.userid, static_cast<unsigned>(toAdd.size()));
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

        std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
            req.groupid, group_member_change_join, self, m_userinfo.userid);
        notifyGroupMembers(req.groupid, cmd_group_member_changed_notify, notify);

        sendGroupWelcomeMessage(group, self);

        LOGI("join group (free) ok, groupid: %lld, userid: %d",
             (long long)req.groupid, m_userinfo.userid);
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
    groupMgr.updateJoinRequestStatus(req.requestid, accept ? 1 : 2, now, m_userinfo.userid);

    if (accept)
    {
        std::vector<int64_t> applicant;
        applicant.push_back(rec.userid);
        groupMgr.addMembers(rec.groupid, applicant, now);
    }

    send(cmd_handle_join_group, m_seq, GroupProtocol::buildSimpleResponse(0, "ok"));

    //同意时向全体成员（含新成员）推 member_changed_notify（加入）；
    //拒绝不向申请人单独推结果（申请人可重新申请）
    if (accept)
    {
        std::vector<int64_t> applicant;
        applicant.push_back(rec.userid);
        std::string notify = GroupProtocol::buildGroupMemberChangedNotify(
            rec.groupid, group_member_change_join, applicant, m_userinfo.userid);
        notifyGroupMembers(rec.groupid, cmd_group_member_changed_notify, notify);

        //审批通过也算入群，同样要发欢迎语。这里重新读一次群资料 —— 上面的
        //流程里只用到了申请记录，手头没有 GroupRecord。
        GroupRecord group;
        if (groupMgr.getGroup(rec.groupid, group))
            sendGroupWelcomeMessage(group, applicant);
    }

    LOGI("handle join group ok, requestid: %lld, action: %d, by: %d",
         (long long)req.requestid, req.action, m_userinfo.userid);
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

    LOGI("mute all ok, groupid: %lld, muteall: %d, by: %d",
         (long long)groupid, ureq.muteall, m_userinfo.userid);
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
    if (groupMgr.getMemberRole(groupid, targetId) < 0)
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
    if (card.msgtype != message_type_poll)
    {
        send(cmd_vote_poll, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "not a poll card"));
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

    MessageManager& msgMgr = Singleton<MessageManager>::Instance();
    MessageRecord card;
    if (!msgMgr.getMessageById(req.msgid, card))
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildSimpleResponse(error_code_message_not_exist, "card not exist"));
        return;
    }
    if (card.msgtype != message_type_relay)
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "not a relay card"));
        return;
    }
    if (!canAccessCard(card))
    {
        send(cmd_join_relay, m_seq,
             CardProtocol::buildSimpleResponse(error_code_permission_denied, "no access to this card"));
        return;
    }

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    CardManager& cardMgr = Singleton<CardManager>::Instance();
    //每人一条接龙，重复参与即更新本人的接龙文本
    cardMgr.upsertInteraction(req.msgid, m_userinfo.userid, req.text, now);

    send(cmd_join_relay, m_seq, CardProtocol::buildSimpleResponse(0, "ok"));

    std::vector<CardInteractionRecord> rows;
    cardMgr.getCardInteractions(req.msgid, rows);
    notifyCardConversation(card, cmd_relay_changed_notify,
                           CardProtocol::buildRelayChangedNotify(req.msgid, rows));

    LOGI("join relay ok, userid: %d, msgid: %lld", m_userinfo.userid, (long long)req.msgid);
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
    if (card.msgtype != message_type_todo)
    {
        send(cmd_update_todo, m_seq,
             CardProtocol::buildSimpleResponse(error_code_bad_request, "not a todo card"));
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

    FileStoreManager& fileMgr = Singleton<FileStoreManager>::Instance();

    //秒传判断：库中已有同 MD5 的已完成文件，则直接返回其 fileid，客户端无需上传
    FileRecord hit;
    if (fileMgr.findCompletedByMd5(req.md5, hit))
    {
        send(cmd_request_upload, m_seq,
             FileProtocol::buildRequestUploadResponse(hit.fileid, true, "", "", 0, false, 0));
        LOGI("request upload hit instant-transfer, userid: %d, md5: %s, fileid: %lld",
             m_userinfo.userid, req.md5.c_str(), (long long)hit.fileid);
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
    m_userinfo.userid = 0;
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
