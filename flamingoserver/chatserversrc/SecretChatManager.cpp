/**
 *  单聊密聊的服务端会话管理 SecretChatManager 的实现，见 SecretChatManager.h 与 docs/密聊方案.md 第 4、5 节。
 *  balloonwj@qq.com   2026-10-07
 */
#include "SecretChatManager.h"

#include <stdio.h>
#include <stdlib.h>
#include <random>
#include "Protocol.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//64 位整数的十进制文本最多 20 个字符（含负号），留余量
const int kInt64TextChars = 32;

//密聊号段的上下界（2600~2699）
const uint16_t kSecretCmdFirst = 2600;
const uint16_t kSecretCmdLast = 2699;

//频率限制的窗口长度（毫秒）：邀请按分钟计，转发按秒计
const int64_t kInviteRateWindowMs = 60 * 1000;
const int64_t kFrameRateWindowMs = 1000;

//2608 的 nonce（XChaCha20 的 24 字节随机数）base64 后的长度：ceil(24 / 3) * 4 = 32
const size_t kNonceBase64Len = 32;

//2608 的 seq 与 data 的上限：seq 为每个方向从 1 起递增的序号，data 不超过整帧上限（由帧长度检查兜住）
const int64_t kMaxSeq = 0x7FFFFFFF;

//把 Json::Value 序列化为紧凑（无缩进、无换行、冒号两侧无空格）的 JSON 文本
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或顶层不是对象返回 false
bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 64 位 ID 写成十进制字符串（v2 惯例，避免客户端按双精度解析丢精度）
std::string idToString(int64_t id)
{
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(id));
    return std::string(buf);
}

//从一个 JSON 值里取 64 位 ID：十进制字符串与整数两种写法都接受；其余类型、空串、非数字返回 0
int64_t valueToId(const Json::Value& v)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty())
            return 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9')
                return 0;
        }
        return static_cast<int64_t>(atoll(text.c_str()));
    }
    if (v.isInt64())
        return v.asInt64();
    return 0;
}

//取出会话号：只接受非空的十进制数字串
bool getSessionId(const Json::Value& root, std::string& out)
{
    out.clear();
    if (!root.isMember("sessionid") || !root["sessionid"].isString())
        return false;
    out = root["sessionid"].asString();
    if (out.empty() || out.size() >= static_cast<size_t>(kInt64TextChars))
        return false;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] < '0' || out[i] > '9')
            return false;
    }
    return true;
}

//判断一个字符是否属于标准 base64 字母表（不含填充符）
bool isBase64Char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
}

//判断文本是否为长度恰好 len、末尾按需带 '=' 的标准 base64
bool isBase64OfLength(const std::string& text, size_t len)
{
    if (text.size() != len)
        return false;
    size_t i = 0;
    for (; i < text.size(); ++i)
    {
        if (!isBase64Char(text[i]))
            break;
    }
    //剩下的只能是填充符，且最多两个
    size_t pad = text.size() - i;
    if (pad > 2)
        return false;
    for (; i < text.size(); ++i)
    {
        if (text[i] != '=')
            return false;
    }
    return true;
}

//判断文本是否为非空的标准 base64（长度为 4 的倍数）
bool isBase64(const std::string& text)
{
    if (text.empty() || (text.size() % 4) != 0)
        return false;
    return isBase64OfLength(text, text.size());
}

//取出 X25519 公钥：必须是 44 个字符的 base64
bool getPublicKey(const Json::Value& root, std::string& out)
{
    out.clear();
    if (!root.isMember("pubkey") || !root["pubkey"].isString())
        return false;
    out = root["pubkey"].asString();
    return isBase64OfLength(out, kSecretPublicKeyBase64Len);
}

//只带 code 与 msg 的应答
std::string buildSimpleResponse(int code, const char* msg)
{
    Json::Value root(Json::objectValue);
    root["code"] = code;
    root["msg"] = msg;
    return writeCompact(root);
}

//防截图级别是否在合法取值范围内
bool isValidProtect(int protect)
{
    return protect >= secret_protect_none && protect <= secret_protect_exclude;
}

} // namespace

SecretChatManager::SecretChatManager()
    : m_idCounter(0)
{
    //会话号计数器取随机起点：会话号不作为凭证（每次都核对连接），随机起点只为不同进程实例之间不易重号
    std::random_device rd;
    m_idCounter = (static_cast<uint64_t>(rd()) << 20) & 0x0000FFFFFFF00000ULL;
}

void SecretChatManager::setLimits(const SecretChatLimits& limits)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (limits.m_inviteTimeoutMs > 0)
        m_limits.m_inviteTimeoutMs = limits.m_inviteTimeoutMs;
    if (limits.m_maxFrameBytes > 0)
        m_limits.m_maxFrameBytes = limits.m_maxFrameBytes;
    if (limits.m_maxSessionsPerUser > 0)
        m_limits.m_maxSessionsPerUser = limits.m_maxSessionsPerUser;
    if (limits.m_invitesPerMinute > 0)
        m_limits.m_invitesPerMinute = limits.m_invitesPerMinute;
    if (limits.m_framesPerSecond > 0)
        m_limits.m_framesPerSecond = limits.m_framesPerSecond;
}

SecretChatLimits SecretChatManager::getLimits() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_limits;
}

bool SecretChatManager::isSecretCommand(uint16_t cmd)
{
    return cmd >= kSecretCmdFirst && cmd <= kSecretCmdLast;
}

int SecretChatManager::handle(uint16_t cmd, const SecretRequestContext& ctx, const std::string& body,
                              ISecretChatGate& gate, std::vector<SecretOutFrame>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    switch (cmd)
    {
    //报到：登记连接与防截图级别
    case cmd_secret_register:
        return handleRegister(ctx, body, gate, out);

    //发起密聊
    case cmd_secret_invite:
        return handleInvite(ctx, body, gate, out);

    //被邀请人同意或忽略
    case cmd_secret_respond:
        return handleRespond(ctx, body, gate, out);

    //发起方撤回邀请
    case cmd_secret_cancel:
        return handleCancel(ctx, body, out);

    //任一方结束密聊
    case cmd_secret_close:
        return handleClose(ctx, body, out);

    //转发一帧密文
    case cmd_secret_message:
        return handleMessage(ctx, body, out);

    //2603 / 2605 / 2609 只由服务端推送，号段内的其余编号未定义：一律按报文非法应答
    default:
        append(out, ctx.m_conn, cmd, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "unsupported secret cmd"));
        return error_code_bad_request;
    }
}

int SecretChatManager::handleRegister(const SecretRequestContext& ctx, const std::string& body, ISecretChatGate& gate,
                                      std::vector<SecretOutFrame>& out)
{
    Json::Value root;
    if (!parseObject(body, root) || !root.isMember("version") || !root["version"].isInt()
        || !root.isMember("protect") || !root["protect"].isInt() || !isValidProtect(root["protect"].asInt()))
    {
        append(out, ctx.m_conn, cmd_secret_register, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "bad request"));
        return error_code_bad_request;
    }
    if (root["version"].asInt() != kSecretProtocolVersion)
    {
        append(out, ctx.m_conn, cmd_secret_register, ctx.m_seq,
               buildSimpleResponse(error_code_secret_version_unsupported, "secret chat version unsupported"));
        return error_code_secret_version_unsupported;
    }

    const int protect = root["protect"].asInt();
    std::map<SecretConnKey, ConnInfo>::iterator found = m_conns.find(ctx.m_conn);
    const bool changed = found != m_conns.end() && found->second.m_protect != protect;
    ConnInfo& info = m_conns[ctx.m_conn];
    info.m_userId = ctx.m_userId;
    info.m_protect = protect;

    Json::Value resp(Json::objectValue);
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["enabled"] = gate.isSecretChatEnabledForUser(ctx.m_userId) ? 1 : 0;
    append(out, ctx.m_conn, cmd_secret_register, ctx.m_seq, writeCompact(resp));

    //密聊进行中重新报到且级别变了：转告这条连接参与的每个进行中会话的另一方（状态 6）
    if (changed)
    {
        for (SessionMap::const_iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
        {
            const Session& s = it->second;
            if (s.m_phase != kPhaseActive)
                continue;
            std::vector<SecretConnKey> target;
            int64_t peerOfTarget = 0;
            if (s.m_inviterConn == ctx.m_conn)
            {
                target.push_back(s.m_inviteeConn);
                peerOfTarget = s.m_inviterId;
            }
            else if (s.m_inviteeConn == ctx.m_conn)
            {
                target.push_back(s.m_inviterConn);
                peerOfTarget = s.m_inviteeId;
            }
            if (!target.empty())
                appendState(out, target, s, peerOfTarget, secret_state_peer_protect, 0);
        }
    }
    return error_code_ok;
}

int SecretChatManager::handleInvite(const SecretRequestContext& ctx, const std::string& body, ISecretChatGate& gate,
                                    std::vector<SecretOutFrame>& out)
{
    Json::Value root;
    std::string pubkey;
    int64_t peerId = 0;
    if (parseObject(body, root) && root.isMember("peerid"))
        peerId = valueToId(root["peerid"]);
    if (peerId <= 0 || !getPublicKey(root, pubkey) || peerId == ctx.m_userId)
    {
        append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "bad request"));
        return error_code_bad_request;
    }
    //发起方这条连接必须先报到（否则说明客户端没有密聊模块，或是按老协议乱发）
    if (m_conns.find(ctx.m_conn) == m_conns.end())
    {
        append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq,
               buildSimpleResponse(error_code_secret_not_member, "connection not registered"));
        return error_code_secret_not_member;
    }
    //双方所属企业都要开着密聊
    if (!gate.isSecretChatEnabledForUser(ctx.m_userId) || !gate.isSecretChatEnabledForUser(peerId))
    {
        append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq,
               buildSimpleResponse(error_code_secret_disabled, "secret chat disabled"));
        return error_code_secret_disabled;
    }
    //被对方拉黑时不能发起
    if (gate.isBlockedBy(peerId, ctx.m_userId))
    {
        append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq,
               buildSimpleResponse(error_code_secret_blocked, "blocked by peer"));
        return error_code_secret_blocked;
    }
    //对方必须有已报到的连接
    std::vector<SecretConnKey> peerConns = connsOfUser(peerId, 0);
    if (peerConns.empty())
    {
        append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq,
               buildSimpleResponse(error_code_secret_peer_unavailable, "peer unavailable"));
        return error_code_secret_peer_unavailable;
    }
    //同一对用户之间已有待应答或进行中的密聊
    if (hasSessionBetween(ctx.m_userId, peerId))
    {
        append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq,
               buildSimpleResponse(error_code_secret_busy, "secret chat already exists"));
        return error_code_secret_busy;
    }
    //数量与频率上限：数量先查，未超时才记一次频率，避免被拒的请求也占用频率额度
    if (sessionCountOfUser(ctx.m_userId) >= m_limits.m_maxSessionsPerUser
        || sessionCountOfUser(peerId) >= m_limits.m_maxSessionsPerUser
        || !allowInvite(ctx.m_userId, ctx.m_nowMs))
    {
        append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq,
               buildSimpleResponse(error_code_secret_too_many, "too many secret chats"));
        return error_code_secret_too_many;
    }

    Session s;
    s.m_id = newSessionId(ctx.m_nowMs);
    s.m_phase = kPhasePending;
    s.m_inviterId = ctx.m_userId;
    s.m_inviterConn = ctx.m_conn;
    s.m_inviteeId = peerId;
    s.m_inviteeConn = 0;
    s.m_expireMs = ctx.m_nowMs + m_limits.m_inviteTimeoutMs;
    m_sessions[s.m_id] = s;

    Json::Value resp(Json::objectValue);
    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    resp["sessionid"] = s.m_id;
    resp["expirems"] = static_cast<Json::Int64>(m_limits.m_inviteTimeoutMs);
    append(out, ctx.m_conn, cmd_secret_invite, ctx.m_seq, writeCompact(resp));

    Json::Value notify(Json::objectValue);
    notify["sessionid"] = s.m_id;
    notify["fromid"] = idToString(ctx.m_userId);
    notify["pubkey"] = pubkey;
    notify["expirems"] = static_cast<Json::Int64>(m_limits.m_inviteTimeoutMs);
    notify["peerprotect"] = protectOf(ctx.m_conn);
    const std::string notifyBody = writeCompact(notify);
    for (size_t i = 0; i < peerConns.size(); ++i)
        append(out, peerConns[i], cmd_secret_invite_notify, 0, notifyBody);
    return error_code_ok;
}

int SecretChatManager::handleRespond(const SecretRequestContext& ctx, const std::string& body, ISecretChatGate& gate,
                                     std::vector<SecretOutFrame>& out)
{
    Json::Value root;
    std::string sessionId;
    if (!parseObject(body, root) || !getSessionId(root, sessionId) || !root.isMember("accept")
        || !root["accept"].isInt())
    {
        append(out, ctx.m_conn, cmd_secret_respond, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "bad request"));
        return error_code_bad_request;
    }
    const bool accept = root["accept"].asInt() != 0;
    std::string pubkey;
    if (accept && !getPublicKey(root, pubkey))
    {
        append(out, ctx.m_conn, cmd_secret_respond, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "bad pubkey"));
        return error_code_bad_request;
    }

    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second.m_phase != kPhasePending || ctx.m_nowMs >= it->second.m_expireMs)
    {
        append(out, ctx.m_conn, cmd_secret_respond, ctx.m_seq,
               buildSimpleResponse(error_code_secret_not_exist, "secret chat not exist"));
        return error_code_secret_not_exist;
    }
    Session& s = it->second;
    //只有被邀请人已报到的连接能应答
    if (s.m_inviteeId != ctx.m_userId || m_conns.find(ctx.m_conn) == m_conns.end())
    {
        append(out, ctx.m_conn, cmd_secret_respond, ctx.m_seq,
               buildSimpleResponse(error_code_secret_not_member, "not the invitee"));
        return error_code_secret_not_member;
    }
    //被邀请人其余已报到的连接：都要收到「已在其他设备处理」，以便它们的邀请卡片消失
    std::vector<SecretConnKey> others = connsOfUser(s.m_inviteeId, ctx.m_conn);

    if (!accept)
    {
        const Session ended = s;
        m_sessions.erase(it);
        append(out, ctx.m_conn, cmd_secret_respond, ctx.m_seq, buildSimpleResponse(error_code_ok, "ok"));
        std::vector<SecretConnKey> inviter(1, ended.m_inviterConn);
        appendState(out, inviter, ended, ended.m_inviteeId, secret_state_not_accepted, 0);
        appendState(out, others, ended, ended.m_inviterId, secret_state_handled_elsewhere, 0);
        return error_code_ok;
    }

    //同意时再查一次双方企业开关：邀请发出之后管理员可能关掉了它
    if (!gate.isSecretChatEnabledForUser(s.m_inviterId) || !gate.isSecretChatEnabledForUser(s.m_inviteeId))
    {
        append(out, ctx.m_conn, cmd_secret_respond, ctx.m_seq,
               buildSimpleResponse(error_code_secret_disabled, "secret chat disabled"));
        return error_code_secret_disabled;
    }

    s.m_phase = kPhaseActive;
    s.m_inviteeConn = ctx.m_conn;
    s.m_rateWindowMs = ctx.m_nowMs;
    s.m_rateCount = 0;
    append(out, ctx.m_conn, cmd_secret_respond, ctx.m_seq, buildSimpleResponse(error_code_ok, "ok"));

    //发起方：带被邀请方的公钥与防截图级别
    Json::Value toInviter(Json::objectValue);
    toInviter["sessionid"] = s.m_id;
    toInviter["peerid"] = idToString(s.m_inviteeId);
    toInviter["state"] = secret_state_active;
    toInviter["reason"] = 0;
    toInviter["pubkey"] = pubkey;
    toInviter["peerprotect"] = protectOf(s.m_inviteeConn);
    append(out, s.m_inviterConn, cmd_secret_state_notify, 0, writeCompact(toInviter));

    //被邀请方点「同意」的那条连接：带发起方此刻的防截图级别（2603 之后可能变过）
    std::vector<SecretConnKey> invitee(1, s.m_inviteeConn);
    appendState(out, invitee, s, s.m_inviterId, secret_state_active, 0);
    appendState(out, others, s, s.m_inviterId, secret_state_handled_elsewhere, 0);
    return error_code_ok;
}

int SecretChatManager::handleCancel(const SecretRequestContext& ctx, const std::string& body,
                                    std::vector<SecretOutFrame>& out)
{
    Json::Value root;
    std::string sessionId;
    if (!parseObject(body, root) || !getSessionId(root, sessionId))
    {
        append(out, ctx.m_conn, cmd_secret_cancel, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "bad request"));
        return error_code_bad_request;
    }
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second.m_phase != kPhasePending)
    {
        append(out, ctx.m_conn, cmd_secret_cancel, ctx.m_seq,
               buildSimpleResponse(error_code_secret_not_exist, "secret chat not exist"));
        return error_code_secret_not_exist;
    }
    if (it->second.m_inviterConn != ctx.m_conn)
    {
        append(out, ctx.m_conn, cmd_secret_cancel, ctx.m_seq,
               buildSimpleResponse(error_code_secret_not_member, "not the inviter"));
        return error_code_secret_not_member;
    }
    const Session ended = it->second;
    m_sessions.erase(it);
    append(out, ctx.m_conn, cmd_secret_cancel, ctx.m_seq, buildSimpleResponse(error_code_ok, "ok"));
    appendState(out, connsOfUser(ended.m_inviteeId, 0), ended, ended.m_inviterId, secret_state_canceled, 0);
    return error_code_ok;
}

int SecretChatManager::handleClose(const SecretRequestContext& ctx, const std::string& body,
                                   std::vector<SecretOutFrame>& out)
{
    Json::Value root;
    std::string sessionId;
    if (!parseObject(body, root) || !getSessionId(root, sessionId))
    {
        append(out, ctx.m_conn, cmd_secret_close, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "bad request"));
        return error_code_bad_request;
    }
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
    {
        //会话可能已因对方退出、超时而结束：结束是幂等的，回成功，客户端照常清理本机
        append(out, ctx.m_conn, cmd_secret_close, ctx.m_seq, buildSimpleResponse(error_code_ok, "already ended"));
        return error_code_ok;
    }
    const Session ended = it->second;

    //待应答阶段：发起方结束等同撤回；被邀请人结束等同忽略
    if (ended.m_phase == kPhasePending)
    {
        if (ended.m_inviterConn == ctx.m_conn)
        {
            m_sessions.erase(it);
            append(out, ctx.m_conn, cmd_secret_close, ctx.m_seq, buildSimpleResponse(error_code_ok, "ok"));
            appendState(out, connsOfUser(ended.m_inviteeId, 0), ended, ended.m_inviterId, secret_state_canceled, 0);
            return error_code_ok;
        }
        if (ended.m_inviteeId == ctx.m_userId && m_conns.find(ctx.m_conn) != m_conns.end())
        {
            m_sessions.erase(it);
            append(out, ctx.m_conn, cmd_secret_close, ctx.m_seq, buildSimpleResponse(error_code_ok, "ok"));
            std::vector<SecretConnKey> inviter(1, ended.m_inviterConn);
            appendState(out, inviter, ended, ended.m_inviteeId, secret_state_not_accepted, 0);
            appendState(out, connsOfUser(ended.m_inviteeId, ctx.m_conn), ended, ended.m_inviterId,
                        secret_state_handled_elsewhere, 0);
            return error_code_ok;
        }
        append(out, ctx.m_conn, cmd_secret_close, ctx.m_seq,
               buildSimpleResponse(error_code_secret_not_member, "not a member"));
        return error_code_secret_not_member;
    }

    //进行中：只有绑定的两条连接能结束，另一方收到「已结束、对方退出」
    std::vector<SecretConnKey> other;
    int64_t peerOfOther = 0;
    if (ended.m_inviterConn == ctx.m_conn)
    {
        other.push_back(ended.m_inviteeConn);
        peerOfOther = ended.m_inviterId;
    }
    else if (ended.m_inviteeConn == ctx.m_conn)
    {
        other.push_back(ended.m_inviterConn);
        peerOfOther = ended.m_inviteeId;
    }
    else
    {
        append(out, ctx.m_conn, cmd_secret_close, ctx.m_seq,
               buildSimpleResponse(error_code_secret_not_member, "not a member"));
        return error_code_secret_not_member;
    }
    m_sessions.erase(it);
    append(out, ctx.m_conn, cmd_secret_close, ctx.m_seq, buildSimpleResponse(error_code_ok, "ok"));
    appendState(out, other, ended, peerOfOther, secret_state_ended, secret_end_peer_left);
    return error_code_ok;
}

int SecretChatManager::handleMessage(const SecretRequestContext& ctx, const std::string& body,
                                     std::vector<SecretOutFrame>& out)
{
    //整帧长度先查：超长的包体不值得解析
    if (body.size() > m_limits.m_maxFrameBytes)
    {
        Json::Value resp(Json::objectValue);
        resp["code"] = error_code_secret_too_large;
        resp["msg"] = "frame too large";
        append(out, ctx.m_conn, cmd_secret_message, ctx.m_seq, writeCompact(resp));
        return error_code_secret_too_large;
    }

    Json::Value root;
    std::string sessionId;
    int64_t seq = 0;
    std::string nonce;
    std::string data;
    if (parseObject(body, root) && root.isMember("seq") && root["seq"].isInt64())
        seq = root["seq"].asInt64();
    if (root.isMember("nonce") && root["nonce"].isString())
        nonce = root["nonce"].asString();
    if (root.isMember("data") && root["data"].isString())
        data = root["data"].asString();
    if (!getSessionId(root, sessionId) || seq <= 0 || seq > kMaxSeq || !isBase64OfLength(nonce, kNonceBase64Len)
        || !isBase64(data))
    {
        append(out, ctx.m_conn, cmd_secret_message, ctx.m_seq, buildSimpleResponse(error_code_bad_request, "bad request"));
        return error_code_bad_request;
    }

    Json::Value resp(Json::objectValue);
    resp["seq"] = static_cast<Json::Int64>(seq);

    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second.m_phase != kPhaseActive)
    {
        resp["code"] = error_code_secret_not_exist;
        resp["msg"] = "secret chat not exist";
        append(out, ctx.m_conn, cmd_secret_message, ctx.m_seq, writeCompact(resp));
        return error_code_secret_not_exist;
    }
    Session& s = it->second;
    SecretConnKey target = 0;
    if (s.m_inviterConn == ctx.m_conn)
        target = s.m_inviteeConn;
    else if (s.m_inviteeConn == ctx.m_conn)
        target = s.m_inviterConn;
    if (target == 0)
    {
        resp["code"] = error_code_secret_not_member;
        resp["msg"] = "not a member";
        append(out, ctx.m_conn, cmd_secret_message, ctx.m_seq, writeCompact(resp));
        return error_code_secret_not_member;
    }

    //每个会话每秒的帧数上限（两个方向合计）
    if (ctx.m_nowMs - s.m_rateWindowMs >= kFrameRateWindowMs || ctx.m_nowMs < s.m_rateWindowMs)
    {
        s.m_rateWindowMs = ctx.m_nowMs;
        s.m_rateCount = 0;
    }
    if (s.m_rateCount >= m_limits.m_framesPerSecond)
    {
        resp["code"] = error_code_secret_too_many;
        resp["msg"] = "too many frames";
        append(out, ctx.m_conn, cmd_secret_message, ctx.m_seq, writeCompact(resp));
        return error_code_secret_too_many;
    }
    ++s.m_rateCount;

    Json::Value notify(Json::objectValue);
    notify["sessionid"] = s.m_id;
    notify["seq"] = static_cast<Json::Int64>(seq);
    notify["nonce"] = nonce;
    notify["data"] = data;
    notify["servertime"] = static_cast<Json::Int64>(ctx.m_nowMs);
    append(out, target, cmd_secret_message_notify, 0, writeCompact(notify));

    resp["code"] = error_code_ok;
    resp["msg"] = "ok";
    append(out, ctx.m_conn, cmd_secret_message, ctx.m_seq, writeCompact(resp));
    return error_code_ok;
}

void SecretChatManager::onDisconnect(SecretConnKey conn, std::vector<SecretOutFrame>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<SecretConnKey, ConnInfo>::iterator ci = m_conns.find(conn);
    if (ci == m_conns.end())
        return;
    const int64_t userId = ci->second.m_userId;
    m_conns.erase(ci);

    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end();)
    {
        const Session s = it->second;
        bool remove = false;
        if (s.m_phase == kPhaseActive)
        {
            //进行中：另一方转为「只能查看」
            if (s.m_inviterConn == conn)
            {
                std::vector<SecretConnKey> other(1, s.m_inviteeConn);
                appendState(out, other, s, s.m_inviterId, secret_state_ended, secret_end_peer_offline);
                remove = true;
            }
            else if (s.m_inviteeConn == conn)
            {
                std::vector<SecretConnKey> other(1, s.m_inviterConn);
                appendState(out, other, s, s.m_inviteeId, secret_state_ended, secret_end_peer_offline);
                remove = true;
            }
        }
        else if (s.m_inviterConn == conn)
        {
            //发起方断开：邀请作废，被邀请人的卡片消失
            appendState(out, connsOfUser(s.m_inviteeId, 0), s, s.m_inviterId, secret_state_canceled, 0);
            remove = true;
        }
        else if (s.m_inviteeId == userId && connsOfUser(userId, 0).empty())
        {
            //被邀请人最后一条已报到的连接断开：没人能再应答，按未接受结束
            std::vector<SecretConnKey> inviter(1, s.m_inviterConn);
            appendState(out, inviter, s, s.m_inviteeId, secret_state_not_accepted, 0);
            remove = true;
        }

        if (remove)
            m_sessions.erase(it++);
        else
            ++it;
    }
}

size_t SecretChatManager::reapExpired(int64_t nowMs, std::vector<SecretOutFrame>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    size_t count = 0;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end();)
    {
        const Session s = it->second;
        if (s.m_phase == kPhasePending && nowMs >= s.m_expireMs)
        {
            std::vector<SecretConnKey> inviter(1, s.m_inviterConn);
            appendState(out, inviter, s, s.m_inviteeId, secret_state_not_accepted, 0);
            appendState(out, connsOfUser(s.m_inviteeId, 0), s, s.m_inviterId, secret_state_not_accepted, 0);
            m_sessions.erase(it++);
            ++count;
        }
        else
        {
            ++it;
        }
    }

    //顺带清掉一分钟以前的邀请时间记录，避免长期运行时这张表只增不减
    for (std::map<int64_t, std::deque<int64_t> >::iterator it = m_inviteTimes.begin(); it != m_inviteTimes.end();)
    {
        std::deque<int64_t>& times = it->second;
        while (!times.empty() && nowMs - times.front() >= kInviteRateWindowMs)
            times.pop_front();
        if (times.empty())
            m_inviteTimes.erase(it++);
        else
            ++it;
    }
    return count;
}

size_t SecretChatManager::onFeatureDisabled(int64_t enterpriseId, ISecretChatGate& gate,
                                            std::vector<SecretOutFrame>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    size_t count = 0;
    if (enterpriseId <= 0)
        return 0;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end();)
    {
        const Session s = it->second;
        if (gate.enterpriseOfUser(s.m_inviterId) != enterpriseId && gate.enterpriseOfUser(s.m_inviteeId) != enterpriseId)
        {
            ++it;
            continue;
        }
        std::vector<SecretConnKey> inviter(1, s.m_inviterConn);
        appendState(out, inviter, s, s.m_inviteeId, secret_state_ended, secret_end_disabled);
        if (s.m_phase == kPhaseActive)
        {
            std::vector<SecretConnKey> invitee(1, s.m_inviteeConn);
            appendState(out, invitee, s, s.m_inviterId, secret_state_ended, secret_end_disabled);
        }
        else
        {
            appendState(out, connsOfUser(s.m_inviteeId, 0), s, s.m_inviterId, secret_state_ended, secret_end_disabled);
        }
        m_sessions.erase(it++);
        ++count;
    }
    return count;
}

size_t SecretChatManager::sessionCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_sessions.size();
}

size_t SecretChatManager::registeredCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_conns.size();
}

std::vector<SecretConnKey> SecretChatManager::connsOfUser(int64_t userId, SecretConnKey exclude) const
{
    std::vector<SecretConnKey> result;
    for (std::map<SecretConnKey, ConnInfo>::const_iterator it = m_conns.begin(); it != m_conns.end(); ++it)
    {
        if (it->second.m_userId == userId && it->first != exclude)
            result.push_back(it->first);
    }
    return result;
}

int SecretChatManager::protectOf(SecretConnKey conn) const
{
    std::map<SecretConnKey, ConnInfo>::const_iterator it = m_conns.find(conn);
    if (it == m_conns.end())
        return secret_protect_none;
    return it->second.m_protect;
}

int SecretChatManager::sessionCountOfUser(int64_t userId) const
{
    int count = 0;
    for (SessionMap::const_iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second.m_inviterId == userId || it->second.m_inviteeId == userId)
            ++count;
    }
    return count;
}

bool SecretChatManager::hasSessionBetween(int64_t userA, int64_t userB) const
{
    for (SessionMap::const_iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        const Session& s = it->second;
        if ((s.m_inviterId == userA && s.m_inviteeId == userB) || (s.m_inviterId == userB && s.m_inviteeId == userA))
            return true;
    }
    return false;
}

bool SecretChatManager::allowInvite(int64_t userId, int64_t nowMs)
{
    std::deque<int64_t>& times = m_inviteTimes[userId];
    while (!times.empty() && nowMs - times.front() >= kInviteRateWindowMs)
        times.pop_front();
    if (static_cast<int>(times.size()) >= m_limits.m_invitesPerMinute)
        return false;
    times.push_back(nowMs);
    return true;
}

std::string SecretChatManager::newSessionId(int64_t nowMs)
{
    (void)nowMs;
    ++m_idCounter;
    char buf[kInt64TextChars];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(m_idCounter));
    return std::string(buf);
}

void SecretChatManager::appendState(std::vector<SecretOutFrame>& out, const std::vector<SecretConnKey>& conns,
                                    const Session& s, int64_t peerId, int state, int reason) const
{
    if (conns.empty())
        return;
    for (size_t i = 0; i < conns.size(); ++i)
    {
        if (conns[i] == 0)
            continue;
        //peerprotect 取「对方」那条连接的级别：对方是发起方时取发起方连接，否则取被邀请方绑定的连接
        SecretConnKey peerConn = (peerId == s.m_inviterId) ? s.m_inviterConn : s.m_inviteeConn;
        Json::Value v(Json::objectValue);
        v["sessionid"] = s.m_id;
        v["peerid"] = idToString(peerId);
        v["state"] = state;
        v["reason"] = reason;
        v["peerprotect"] = protectOf(peerConn);
        append(out, conns[i], cmd_secret_state_notify, 0, writeCompact(v));
    }
}

void SecretChatManager::append(std::vector<SecretOutFrame>& out, SecretConnKey conn, uint16_t cmd, uint32_t seq,
                               const std::string& body)
{
    SecretOutFrame f;
    f.m_conn = conn;
    f.m_cmd = cmd;
    f.m_seq = seq;
    f.m_body = body;
    out.push_back(f);
}
