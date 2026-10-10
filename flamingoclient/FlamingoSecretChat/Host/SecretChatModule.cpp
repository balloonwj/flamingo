/**
 *  密聊模块对象 CSecretChatModule 的实现，见 SecretChatModule.h。
 *
 *  回调宿主时注意重入：宿主可能在回调里调用本模块（例如在 OnSessionState 里调 Close），因此回调一律放在
 *  对会话表的修改完成之后，回调返回后不再沿用回调之前取得的迭代器与引用，需要时按会话号重新查找。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "SecretChatModule.h"

#include <string.h>
#include <time.h>
#include "SecretLog.h"
#include "../Core/CaptureProtect.h"
#include "../Core/SecretProtocol.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"

//本 DLL 的模块句柄（链接器提供），注册窗口类时用
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace secretchat {

namespace {

//消息窗口的窗口类名
const wchar_t* const kMsgWndClass = L"FlamingoSecretChatMsgWnd";

//消息窗口的两个定时器
const UINT_PTR kTimerProtectCheck = 1;   //防截图核对
const UINT_PTR kTimerPump = 2;           //发送节奏

//安全码文本的最少缓冲字符数：4 组 5 位 + 3 个空格 + 结尾 0
const uint32_t kSafetyCodeMinChars = 24;

//秒换算成毫秒
const int64_t kMillisPerSecond = 1000;

//网络线程投递到消息窗口的一帧
struct FramePacket
{
    uint16_t    cmd;    //命令号
    uint32_t    seq;    //帧序号
    std::string body;   //包体

    FramePacket()
        : cmd(0)
        , seq(0)
    {
    }
};

//把一段明文的内存清零（用完即清，不留在堆上）
void WipeString(std::string& s)
{
    if (!s.empty())
        memset(&s[0], 0, s.size());
    s.clear();
}

} // namespace

CSecretChatModule& CSecretChatModule::Instance()
{
    static CSecretChatModule s_instance;
    return s_instance;
}

CSecretChatModule::CSecretChatModule()
    : m_host(NULL)
    , m_headless(false)
    , m_hwnd(NULL)
    , m_postTarget(NULL)
    , m_loggedIn(false)
    , m_registered(false)
    , m_enabled(false)
    , m_reportedProtect(kProtectNone)
    , m_pumpRunning(false)
{
}

CSecretChatModule::~CSecretChatModule()
{
}

void CSecretChatModule::SetHeadlessForTest(bool headless)
{
    m_headless = headless;
}

size_t CSecretChatModule::SessionCount() const
{
    return m_sessions.size();
}

//======================== 生命周期 ========================

bool CSecretChatModule::Init(ISecretChatHost* host, const SecretInitParams* params)
{
    if (host == NULL || params == NULL || params->m_cbSize != sizeof(SecretInitParams))
        return false;
    m_host = host;
    SecretLogSetHost(host);
    if (!SecretCryptoInit())
    {
        LOG_ERROR("secret chat init failed: sodium_init failed.");
        SecretLogSetHost(NULL);
        m_host = NULL;
        return false;
    }
    if (!m_headless && !CreateMessageWindow())
    {
        LOG_ERROR("secret chat init failed: message window not created, error=%lu.", ::GetLastError());
        SecretLogSetHost(NULL);
        m_host = NULL;
        return false;
    }
    m_reportedProtect = ProbeCaptureProtectLevel();
    LOG_INFO("secret chat module initialized, api version %u, capture protect level %d.",
             static_cast<unsigned>(kApiVersion), m_reportedProtect);
    return true;
}

void CSecretChatModule::Shutdown()
{
    if (m_host == NULL)
        return;
    OnLoggingOut();
    for (std::map<HWND, int>::iterator it = m_protected.begin(); it != m_protected.end(); ++it)
        RemoveCaptureProtect(it->first);
    m_protected.clear();
    DestroyMessageWindow();
    LOG_INFO("secret chat module shut down.");
    SecretLogSetHost(NULL);
    m_host = NULL;
}

uint32_t CSecretChatModule::GetApiVersion() const
{
    return kApiVersion;
}

void CSecretChatModule::OnLoggedIn()
{
    m_loggedIn = true;
    SendRegister();
}

void CSecretChatModule::OnLoggingOut()
{
    //尽量通知服务端：进行中的结束、发起中的撤回、收到的邀请当作忽略（服务端对 2607 按阶段处理）
    std::vector<uint64_t> ids;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
        ids.push_back(it->first);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        Send(cmd_secret_close, SecretProtocol::BuildSessionOnly(ids[i]));
        EndSession(ids[i], false);
    }
    for (std::map<uint32_t, PendingInvite>::iterator it = m_pendingInvites.begin(); it != m_pendingInvites.end(); ++it)
        it->second.cipher->Wipe();
    m_pendingInvites.clear();
    m_loggedIn = false;
    m_registered = false;
    m_enabled = false;
    if (!ids.empty())
        LOG_INFO("secret chat logging out, sessions ended: %u.", static_cast<unsigned>(ids.size()));
}

void CSecretChatModule::OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len)
{
    FramePacket* packet = new FramePacket();
    packet->cmd = cmd;
    packet->seq = seq;
    if (body != NULL && len > 0)
        packet->body.assign(body, len);
    HWND target = NULL;
    {
        std::lock_guard<std::mutex> guard(m_wndMutex);
        target = m_postTarget;
    }
    if (target == NULL || !::PostMessageW(target, kMsgSecretFrameArrived, 0, reinterpret_cast<LPARAM>(packet)))
    {
        LOG_WARNING("secret frame dropped, cmd=%u, module not ready.", static_cast<unsigned>(cmd));
        delete packet;
    }
}

int CSecretChatModule::ProbeProtect()
{
    return ProbeCaptureProtectLevel();
}

//======================== 本人发起的操作 ========================

bool CSecretChatModule::Invite(int64_t peerId)
{
    if (m_host == NULL || !m_loggedIn || peerId <= 0)
        return false;
    for (SessionMap::const_iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second->peerId == peerId)
        {
            LOG_WARNING("secret invite rejected locally: a session with peer %lld exists.",
                        static_cast<long long>(peerId));
            return false;
        }
    }
    std::shared_ptr<CSecretCipher> cipher(new CSecretCipher());
    if (!cipher->Generate())
    {
        LOG_ERROR("secret invite failed: key pair not generated.");
        return false;
    }
    const uint32_t seq = Send(cmd_secret_invite, SecretProtocol::BuildInvite(peerId, cipher->PublicKeyBase64()));
    if (seq == 0)
        return false;
    PendingInvite pending;
    pending.peerId = peerId;
    pending.cipher = cipher;
    m_pendingInvites[seq] = pending;
    LOG_INFO("secret invite sent, peer %lld, seq %u.", static_cast<long long>(peerId), seq);
    return true;
}

bool CSecretChatModule::Respond(uint64_t sessionId, bool accept)
{
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second->phase != phase_incoming)
        return false;
    Session& s = *it->second;
    if (!accept)
    {
        Send(cmd_secret_respond, SecretProtocol::BuildRespond(sessionId, false, std::string()));
        LOG_INFO("secret invite ignored, session %llu.", static_cast<unsigned long long>(sessionId));
        EndSession(sessionId, false);
        return true;
    }
    if (!s.cipher->Generate())
    {
        LOG_ERROR("secret accept failed: key pair not generated, session %llu.",
                  static_cast<unsigned long long>(sessionId));
        return false;
    }
    //公钥要在派生之前取：派生后私钥清零，但公钥保留
    const std::string myPubkey = s.cipher->PublicKeyBase64();
    if (!s.cipher->DeriveAsInvitee(s.peerPubkey))
    {
        LOG_ERROR("secret accept failed: key agreement failed, session %llu.",
                  static_cast<unsigned long long>(sessionId));
        s.cipher->Wipe();
        return false;
    }
    const uint32_t seq = Send(cmd_secret_respond, SecretProtocol::BuildRespond(sessionId, true, myPubkey));
    if (seq == 0)
    {
        s.cipher->Wipe();
        return false;
    }
    s.phase = phase_accepting;
    LOG_INFO("secret invite accepted, session %llu.", static_cast<unsigned long long>(sessionId));
    return true;
}

bool CSecretChatModule::Cancel(uint64_t sessionId)
{
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second->phase != phase_outgoing)
        return false;
    Send(cmd_secret_cancel, SecretProtocol::BuildSessionOnly(sessionId));
    LOG_INFO("secret invite canceled, session %llu.", static_cast<unsigned long long>(sessionId));
    EndSession(sessionId, false);
    return true;
}

bool CSecretChatModule::Close(uint64_t sessionId)
{
    if (m_sessions.find(sessionId) == m_sessions.end())
        return false;
    Send(cmd_secret_close, SecretProtocol::BuildSessionOnly(sessionId));
    LOG_INFO("secret chat closed by self, session %llu.", static_cast<unsigned long long>(sessionId));
    EndSession(sessionId, false);
    return true;
}

bool CSecretChatModule::SendText(uint64_t sessionId, const wchar_t* text, uint32_t* outMsgId)
{
    if (outMsgId != NULL)
        *outMsgId = 0;
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second->phase != phase_active || text == NULL)
        return false;
    const size_t chars = wcslen(text);
    if (chars == 0 || chars > kMaxTextChars)
        return false;
    Session& s = *it->second;
    SecretEnvelope env;
    env.type = envelope_text;
    env.id = s.nextMsgId++;
    env.text = ToUtf8(text);
    env.ts = static_cast<int64_t>(::time(NULL)) * kMillisPerSecond;
    std::string plain;
    const bool encoded = EncodeEnvelope(env, plain);
    WipeString(env.text);
    if (!encoded)
        return false;
    const bool sent = SendEnvelope(s, plain, envelope_text, env.id, 0, 0);
    WipeString(plain);
    if (!sent)
        return false;
    if (outMsgId != NULL)
        *outMsgId = env.id;
    return true;
}

bool CSecretChatModule::SendImage(uint64_t sessionId, const void* bytes, uint32_t len, uint32_t width, uint32_t height,
                                  uint32_t* outImageId)
{
    if (outImageId != NULL)
        *outImageId = 0;
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second->phase != phase_active || bytes == NULL || len == 0
        || len > kMaxImageBytes)
        return false;
    Session& s = *it->second;
    const uint32_t imageId = s.nextImageId++;
    std::vector<SecretEnvelope> chunks;
    SplitImage(imageId, std::string(static_cast<const char*>(bytes), len), width, height, chunks);
    for (size_t i = 0; i < chunks.size(); ++i)
        s.queue.push_back(chunks[i]);
    OutImage& out = s.outImages[imageId];
    out.totalBytes = len;
    out.sentBytes = 0;
    LOG_INFO("secret image queued, session %llu, image %u, bytes %u, chunks %u.",
             static_cast<unsigned long long>(sessionId), imageId, len, static_cast<unsigned>(chunks.size()));
    if (outImageId != NULL)
        *outImageId = imageId;
    PumpSend(NowMs());
    EnsurePumpTimer();
    return true;
}

int CSecretChatModule::ProtectWindow(HWND hwnd, bool on)
{
    if (hwnd == NULL)
        return kProtectNone;
    if (!on)
    {
        RemoveCaptureProtect(hwnd);
        m_protected.erase(hwnd);
        if (m_protected.empty() && m_hwnd != NULL)
            ::KillTimer(m_hwnd, kTimerProtectCheck);
        LOG_INFO("secret capture protect off.");
        return kProtectNone;
    }
    const int level = ApplyCaptureProtect(hwnd);
    m_protected[hwnd] = level;
    if (m_hwnd != NULL)
        ::SetTimer(m_hwnd, kTimerProtectCheck, kProtectCheckIntervalMs, NULL);
    //实际达到的级别比报到时差（例如期间关闭了桌面合成）：重新报到，服务端转告进行中会话的对方
    if (level < m_reportedProtect)
    {
        LOG_WARNING("secret capture protect level dropped %d -> %d, registering again.", m_reportedProtect, level);
        m_reportedProtect = level;
        SendRegister();
    }
    LOG_INFO("secret capture protect on, level %d.", level);
    return level;
}

bool CSecretChatModule::GetSafetyCode(uint64_t sessionId, wchar_t* buf, uint32_t chars)
{
    if (buf == NULL || chars < kSafetyCodeMinChars)
        return false;
    buf[0] = L'\0';
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || it->second->phase != phase_active)
        return false;
    std::string code;
    if (!it->second->cipher->SafetyCode(code))
        return false;
    const std::wstring wide = FromUtf8(code);
    if (wide.size() + 1 > chars)
        return false;
    wcscpy_s(buf, chars, wide.c_str());
    return true;
}

//======================== 收帧 ========================

void CSecretChatModule::ProcessFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    switch (cmd)
    {
    //报到的应答
    case cmd_secret_register:
        OnRegisterResponse(body);
        break;

    //发起邀请的应答
    case cmd_secret_invite:
        OnInviteResponse(seq, body);
        break;

    //收到别人的邀请
    case cmd_secret_invite_notify:
        OnInviteNotify(body);
        break;

    //同意或忽略的应答
    case cmd_secret_respond:
        OnRespondResponse(body);
        break;

    //会话状态变化
    case cmd_secret_state_notify:
        OnStateNotify(body);
        break;

    //撤回的应答：本机在发出时已经清理，只记日志
    case cmd_secret_cancel:
    {
        int code = 0;
        SecretProtocol::ParseCode(body, code);
        LOG_INFO("secret cancel answered, code %d.", code);
        break;
    }

    //结束的应答：本机在发出时已经清理，只记日志
    case cmd_secret_close:
    {
        int code = 0;
        SecretProtocol::ParseCode(body, code);
        LOG_INFO("secret close answered, code %d.", code);
        break;
    }

    //发出的一帧密文的应答
    case cmd_secret_message:
        OnMessageResponse(body);
        break;

    //收到一帧密文
    case cmd_secret_message_notify:
        OnMessageNotify(body);
        break;

    //号段内的其余编号：本版本不认识，忽略
    default:
        LOG_WARNING("secret frame ignored, unknown cmd %u.", static_cast<unsigned>(cmd));
        break;
    }
}

void CSecretChatModule::OnRegisterResponse(const std::string& body)
{
    int code = -1;
    bool enabled = false;
    if (!SecretProtocol::ParseRegisterResponse(body, code, enabled))
    {
        LOG_WARNING("secret register response unparsable.");
        return;
    }
    m_registered = (code == 0);
    m_enabled = m_registered && enabled;
    LOG_INFO("secret register answered, code %d, enabled %d.", code, m_enabled ? 1 : 0);
    if (m_host != NULL)
        m_host->OnRegistered(m_enabled);
}

void CSecretChatModule::OnInviteResponse(uint32_t seq, const std::string& body)
{
    std::map<uint32_t, PendingInvite>::iterator pit = m_pendingInvites.find(seq);
    if (pit == m_pendingInvites.end())
    {
        LOG_WARNING("secret invite response for unknown seq %u.", seq);
        return;
    }
    PendingInvite pending = pit->second;
    m_pendingInvites.erase(pit);

    int code = -1;
    uint64_t sessionId = 0;
    uint32_t expireMs = 0;
    if (!SecretProtocol::ParseInviteResponse(body, code, sessionId, expireMs) || code != 0)
    {
        if (code == 0)
            code = error_code_bad_request;
        pending.cipher->Wipe();
        LOG_INFO("secret invite failed, peer %lld, code %d.", static_cast<long long>(pending.peerId), code);
        if (m_host != NULL)
            m_host->OnInviteSent(0, pending.peerId, 0, code);
        return;
    }
    std::shared_ptr<Session> s(new Session());
    s->id = sessionId;
    s->idText = SecretProtocol::SessionIdText(sessionId);
    s->peerId = pending.peerId;
    s->phase = phase_outgoing;
    s->cipher = pending.cipher;   //沿用发邀请时生成的那一对：对方会用这把公钥
    m_sessions[sessionId] = s;
    LOG_INFO("secret invite accepted by server, session %llu, peer %lld, expire %u ms.",
             static_cast<unsigned long long>(sessionId), static_cast<long long>(pending.peerId), expireMs);
    if (m_host != NULL)
        m_host->OnInviteSent(sessionId, pending.peerId, expireMs, 0);
}

void CSecretChatModule::OnInviteNotify(const std::string& body)
{
    InviteNotify n;
    if (!SecretProtocol::ParseInviteNotify(body, n))
    {
        LOG_WARNING("secret invite notify unparsable.");
        return;
    }
    if (m_sessions.find(n.sessionId) != m_sessions.end())
        return;
    std::shared_ptr<Session> s(new Session());
    s->id = n.sessionId;
    s->idText = SecretProtocol::SessionIdText(n.sessionId);
    s->peerId = n.fromId;
    s->phase = phase_incoming;
    s->peerPubkey = n.pubkey;
    s->peerProtect = n.peerProtect;
    m_sessions[n.sessionId] = s;
    LOG_INFO("secret invite arrived, session %llu, from %lld, expire %u ms, peer protect %d.",
             static_cast<unsigned long long>(n.sessionId), static_cast<long long>(n.fromId), n.expireMs, n.peerProtect);
    if (m_host != NULL)
        m_host->OnInviteArrived(n.sessionId, n.fromId, n.expireMs, n.peerProtect);
}

void CSecretChatModule::OnRespondResponse(const std::string& body)
{
    int code = 0;
    SecretProtocol::ParseCode(body, code);
    if (code == 0)
        return;
    //同意失败（邀请已过期、被撤回、企业关闭了开关）：找出唯一在「同意中」的会话报给宿主
    uint64_t failedId = 0;
    int64_t peerId = 0;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second->phase == phase_accepting)
        {
            failedId = it->first;
            peerId = it->second->peerId;
            break;
        }
    }
    LOG_INFO("secret respond failed, code %d, session %llu.", code, static_cast<unsigned long long>(failedId));
    if (failedId == 0)
        return;
    EndSession(failedId, true);
    if (m_host != NULL)
        m_host->OnSessionState(failedId, peerId, session_state_canceled, 0, kProtectNone);
}

void CSecretChatModule::OnStateNotify(const std::string& body)
{
    StateNotify n;
    if (!SecretProtocol::ParseStateNotify(body, n))
    {
        LOG_WARNING("secret state notify unparsable.");
        return;
    }
    SessionMap::iterator it = m_sessions.find(n.sessionId);
    if (it == m_sessions.end())
    {
        LOG_INFO("secret state %d for unknown session %llu ignored.", n.state, static_cast<unsigned long long>(n.sessionId));
        return;
    }
    Session& s = *it->second;
    const int64_t peerId = s.peerId;
    LOG_INFO("secret state notify, session %llu, state %d, reason %d, peer protect %d.",
             static_cast<unsigned long long>(n.sessionId), n.state, n.reason, n.peerProtect);

    if (n.state == secret_state_active)
    {
        s.peerProtect = n.peerProtect;
        //发起方：用对方公钥派生；被邀请方：同意时已经派生过
        if (s.phase == phase_outgoing && !s.cipher->DeriveAsInviter(n.pubkey))
        {
            LOG_ERROR("secret key agreement failed, session %llu.", static_cast<unsigned long long>(n.sessionId));
            Send(cmd_secret_close, SecretProtocol::BuildSessionOnly(n.sessionId));
            EndSession(n.sessionId, true);
            if (m_host != NULL)
                m_host->OnSessionState(n.sessionId, peerId, session_state_local_error, 0, n.peerProtect);
            return;
        }
        s.phase = phase_active;
        s.txSeq = 0;
        s.rxSeq = 0;
        if (m_host != NULL)
            m_host->OnSessionState(n.sessionId, peerId, session_state_active, 0, n.peerProtect);
        return;
    }
    if (n.state == secret_state_peer_protect)
    {
        s.peerProtect = n.peerProtect;
        if (m_host != NULL)
            m_host->OnSessionState(n.sessionId, peerId, session_state_peer_protect, 0, n.peerProtect);
        return;
    }
    //未接受、已取消、已结束、已在其他设备处理：本机结束会话（宿主据此转为「只能查看」或移除卡片）
    EndSession(n.sessionId, true);
    if (m_host != NULL)
        m_host->OnSessionState(n.sessionId, peerId, n.state, n.reason, n.peerProtect);
}

void CSecretChatModule::OnMessageResponse(const std::string& body)
{
    int code = -1;
    uint32_t seq = 0;
    if (!SecretProtocol::ParseMessageResponse(body, code, seq) || seq == 0)
    {
        LOG_WARNING("secret message response unparsable.");
        return;
    }
    //找出发出这一帧的会话
    std::shared_ptr<Session> owner;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second->inflight.find(seq) != it->second->inflight.end())
        {
            owner = it->second;
            break;
        }
    }
    if (!owner)
        return;
    Session& s = *owner;
    std::map<uint32_t, InFlight>::iterator fit = s.inflight.find(seq);
    InFlight f = fit->second;

    //转发过快：到点后换新序号重发
    if (code == error_code_secret_too_many && f.retries < kMaxFrameRetries)
    {
        fit->second.retryAtMs = NowMs() + kRetryDelayMs;
        EnsurePumpTimer();
        return;
    }
    s.inflight.erase(fit);
    if (code != 0)
    {
        LOG_WARNING("secret frame rejected, session %llu, seq %u, code %d.", static_cast<unsigned long long>(s.id), seq,
                    code);
        FailFrame(s, f, code);
        WipeString(f.plain);
        return;
    }
    const uint64_t sessionId = s.id;
    if (f.kind == envelope_text)
    {
        WipeString(f.plain);
        if (m_host != NULL)
            m_host->OnSendResult(sessionId, f.itemId, 0);
        return;
    }
    //图片块：累计进度
    std::map<uint32_t, OutImage>::iterator oit = s.outImages.find(f.itemId);
    WipeString(f.plain);
    if (oit == s.outImages.end() || oit->second.failed)
        return;
    oit->second.sentBytes += f.bytes;
    const uint32_t sent = oit->second.sentBytes;
    const uint32_t total = oit->second.totalBytes;
    if (sent >= total)
        s.outImages.erase(oit);
    if (m_host != NULL)
        m_host->OnImageSendProgress(sessionId, f.itemId, sent, total, 0);
}

void CSecretChatModule::OnMessageNotify(const std::string& body)
{
    MessageNotify n;
    if (!SecretProtocol::ParseMessageNotify(body, n))
    {
        LOG_WARNING("secret message notify unparsable.");
        return;
    }
    SessionMap::iterator it = m_sessions.find(n.sessionId);
    if (it == m_sessions.end() || it->second->phase != phase_active)
    {
        LOG_WARNING("secret message for inactive session %llu dropped.", static_cast<unsigned long long>(n.sessionId));
        return;
    }
    std::shared_ptr<Session> keep = it->second;
    Session& s = *keep;
    //序号必须递增：重放与倒序注入的帧丢弃
    if (n.seq <= s.rxSeq)
    {
        LOG_WARNING("secret message replay dropped, session %llu, seq %u <= %u.",
                    static_cast<unsigned long long>(n.sessionId), n.seq, s.rxSeq);
        return;
    }
    std::string plain;
    if (!s.cipher->Decrypt(s.idText, n.seq, n.nonce, n.data, plain))
    {
        LOG_WARNING("secret message undecryptable, session %llu, seq %u.", static_cast<unsigned long long>(n.sessionId),
                    n.seq);
        return;
    }
    s.rxSeq = n.seq;
    SecretEnvelope env;
    const bool decoded = DecodeEnvelope(plain, env);
    WipeString(plain);
    if (!decoded)
    {
        LOG_WARNING("secret envelope undecodable, session %llu, seq %u.", static_cast<unsigned long long>(n.sessionId),
                    n.seq);
        return;
    }
    const uint64_t sessionId = s.id;
    const int64_t peerId = s.peerId;
    if (env.type == envelope_text)
    {
        std::wstring text = FromUtf8(env.text);
        WipeString(env.text);
        if (m_host != NULL)
            m_host->OnSecretText(sessionId, peerId, env.id, text.c_str(), n.serverTime);
        //宿主已自行拷贝，本地这份解密后的正文清零
        if (!text.empty())
            ::SecureZeroMemory(&text[0], text.size() * sizeof(wchar_t));
        return;
    }
    //图片块
    uint32_t received = 0;
    const int r = s.assembler.AddChunk(env, received);
    WipeString(env.payload);
    switch (r)
    {
    //这张图片的第一块：先报开始，再报进度（只有一块时紧接着就会是收齐）
    case CImageAssembler::add_first_chunk:
        if (m_host != NULL)
        {
            m_host->OnSecretImageBegin(sessionId, peerId, env.id, env.totalBytes, env.width, env.height, n.serverTime);
            m_host->OnSecretImageProgress(sessionId, env.id, received);
        }
        break;

    //又收到一块
    case CImageAssembler::add_progress:
        if (m_host != NULL)
            m_host->OnSecretImageProgress(sessionId, env.id, received);
        break;

    //收齐且摘要正确：交出整图（只有一块的图片在这里才第一次出现，先补报开始）
    case CImageAssembler::add_complete:
    {
        std::string bytes;
        uint32_t width = 0;
        uint32_t height = 0;
        if (!s.assembler.Take(env.id, bytes, width, height))
            break;
        if (m_host != NULL)
        {
            if (env.chunkCount == 1)
                m_host->OnSecretImageBegin(sessionId, peerId, env.id, env.totalBytes, width, height, n.serverTime);
            m_host->OnSecretImage(sessionId, env.id, bytes.data(), static_cast<uint32_t>(bytes.size()), true);
        }
        WipeString(bytes);
        break;
    }

    //元数据矛盾或摘要不对：这张图片作废
    case CImageAssembler::add_failed:
        LOG_WARNING("secret image failed, session %llu, image %u.", static_cast<unsigned long long>(sessionId), env.id);
        if (m_host != NULL)
            m_host->OnSecretImage(sessionId, env.id, NULL, 0, false);
        break;

    //重复的块：忽略
    default:
        break;
    }
}

//======================== 发送 ========================

bool CSecretChatModule::SendEnvelope(Session& s, const std::string& plain, int kind, uint32_t itemId, uint32_t bytes,
                                     int retries)
{
    const uint32_t seq = ++s.txSeq;
    std::string nonce;
    std::string data;
    if (!s.cipher->Encrypt(s.idText, seq, plain, nonce, data))
        return false;
    if (Send(cmd_secret_message, SecretProtocol::BuildMessage(s.id, seq, nonce, data)) == 0)
        return false;
    InFlight f;
    f.kind = kind;
    f.itemId = itemId;
    f.plain = plain;
    f.bytes = bytes;
    f.retries = retries;
    f.retryAtMs = 0;
    s.inflight[seq] = f;
    return true;
}

bool CSecretChatModule::PumpSend(int64_t nowMs)
{
    bool pending = false;
    std::vector<std::shared_ptr<Session> > sessions;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
        sessions.push_back(it->second);
    for (size_t i = 0; i < sessions.size(); ++i)
    {
        Session& s = *sessions[i];
        if (s.phase != phase_active)
            continue;

        //到点的重发：换新序号重新加密
        std::vector<uint32_t> due;
        for (std::map<uint32_t, InFlight>::iterator it = s.inflight.begin(); it != s.inflight.end(); ++it)
        {
            if (it->second.retryAtMs > 0)
            {
                pending = true;
                if (nowMs >= it->second.retryAtMs)
                    due.push_back(it->first);
            }
        }
        for (size_t d = 0; d < due.size(); ++d)
        {
            InFlight f = s.inflight[due[d]];
            s.inflight.erase(due[d]);
            if (!SendEnvelope(s, f.plain, f.kind, f.itemId, f.bytes, f.retries + 1))
                FailFrame(s, f, error_code_server_internal);
            WipeString(f.plain);
        }

        //排队的图片块：在途不超过 kMaxInflightImageFrames，相邻两帧间隔不小于 kImageFrameIntervalMs
        size_t imageInflight = 0;
        for (std::map<uint32_t, InFlight>::iterator it = s.inflight.begin(); it != s.inflight.end(); ++it)
        {
            if (it->second.kind == envelope_image_chunk)
                ++imageInflight;
        }
        while (!s.queue.empty() && imageInflight < kMaxInflightImageFrames
               && nowMs - s.lastImageSendMs >= kImageFrameIntervalMs)
        {
            SecretEnvelope env = s.queue.front();
            s.queue.pop_front();
            std::map<uint32_t, OutImage>::iterator oit = s.outImages.find(env.id);
            if (oit == s.outImages.end() || oit->second.failed)
                continue;
            std::string plain;
            const uint32_t bytes = static_cast<uint32_t>(env.payload.size());
            if (!EncodeEnvelope(env, plain) || !SendEnvelope(s, plain, envelope_image_chunk, env.id, bytes, 0))
            {
                InFlight f;
                f.kind = envelope_image_chunk;
                f.itemId = env.id;
                FailFrame(s, f, error_code_server_internal);
            }
            WipeString(plain);
            WipeString(env.payload);
            s.lastImageSendMs = nowMs;
            ++imageInflight;
        }
        if (!s.queue.empty())
            pending = true;
    }
    return pending;
}

void CSecretChatModule::FailFrame(Session& s, const InFlight& f, int code)
{
    const uint64_t sessionId = s.id;
    if (f.kind == envelope_text)
    {
        if (m_host != NULL)
            m_host->OnSendResult(sessionId, f.itemId, code);
        return;
    }
    std::map<uint32_t, OutImage>::iterator oit = s.outImages.find(f.itemId);
    if (oit == s.outImages.end() || oit->second.failed)
        return;
    oit->second.failed = true;
    const uint32_t sent = oit->second.sentBytes;
    const uint32_t total = oit->second.totalBytes;
    //丢弃这张图片还在排队的块
    for (std::deque<SecretEnvelope>::iterator qit = s.queue.begin(); qit != s.queue.end();)
    {
        if (qit->id == f.itemId)
            qit = s.queue.erase(qit);
        else
            ++qit;
    }
    if (m_host != NULL)
        m_host->OnImageSendProgress(sessionId, f.itemId, sent, total, code);
}

void CSecretChatModule::EndSession(uint64_t sessionId, bool notify)
{
    SessionMap::iterator it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return;
    std::shared_ptr<Session> keep = it->second;
    m_sessions.erase(it);
    Session& s = *keep;
    const std::vector<uint32_t> incoming = s.assembler.PendingIds();
    std::vector<std::pair<uint32_t, OutImage> > outgoing;
    for (std::map<uint32_t, OutImage>::iterator oit = s.outImages.begin(); oit != s.outImages.end(); ++oit)
    {
        if (!oit->second.failed)
            outgoing.push_back(*oit);
    }
    std::vector<uint32_t> texts;
    for (std::map<uint32_t, InFlight>::iterator fit = s.inflight.begin(); fit != s.inflight.end(); ++fit)
    {
        if (fit->second.kind == envelope_text)
            texts.push_back(fit->second.itemId);
        WipeString(fit->second.plain);
    }
    s.inflight.clear();
    for (std::deque<SecretEnvelope>::iterator qit = s.queue.begin(); qit != s.queue.end(); ++qit)
        WipeString(qit->payload);
    s.queue.clear();
    s.assembler.Clear();
    s.cipher->Wipe();
    WipeString(s.peerPubkey);
    if (!notify || m_host == NULL)
        return;
    for (size_t i = 0; i < incoming.size(); ++i)
        m_host->OnSecretImage(sessionId, incoming[i], NULL, 0, false);
    for (size_t i = 0; i < outgoing.size(); ++i)
        m_host->OnImageSendProgress(sessionId, outgoing[i].first, outgoing[i].second.sentBytes,
                                    outgoing[i].second.totalBytes, error_code_secret_not_exist);
    for (size_t i = 0; i < texts.size(); ++i)
        m_host->OnSendResult(sessionId, texts[i], error_code_secret_not_exist);
}

void CSecretChatModule::SendRegister()
{
    if (!m_loggedIn)
        return;
    Send(cmd_secret_register, SecretProtocol::BuildRegister(kProtocolVersion, m_reportedProtect));
}

uint32_t CSecretChatModule::Send(uint16_t cmd, const std::string& body)
{
    if (m_host == NULL)
        return 0;
    return m_host->SendSecretCommand(cmd, body.data(), static_cast<uint32_t>(body.size()));
}

void CSecretChatModule::EnsurePumpTimer()
{
    if (m_hwnd == NULL || m_pumpRunning)
        return;
    ::SetTimer(m_hwnd, kTimerPump, kPumpIntervalMs, NULL);
    m_pumpRunning = true;
}

void CSecretChatModule::CheckProtectedWindows()
{
    for (std::map<HWND, int>::iterator it = m_protected.begin(); it != m_protected.end();)
    {
        if (!::IsWindow(it->first))
        {
            m_protected.erase(it++);
            continue;
        }
        if (!IsCaptureProtectIntact(it->first, it->second))
        {
            LOG_WARNING("secret capture protect was changed by someone, applying again.");
            it->second = ApplyCaptureProtect(it->first);
        }
        ++it;
    }
    if (m_protected.empty() && m_hwnd != NULL)
        ::KillTimer(m_hwnd, kTimerProtectCheck);
}

//======================== 消息窗口 ========================

bool CSecretChatModule::CreateMessageWindow()
{
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CSecretChatModule::MessageWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kMsgWndClass;
    //重复注册（DLL 卸载后又加载）返回失败但类仍可用，故不检查返回值
    ::RegisterClassExW(&wc);
    m_hwnd = ::CreateWindowExW(0, kMsgWndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, this);
    if (m_hwnd == NULL)
        return false;
    ::SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    std::lock_guard<std::mutex> guard(m_wndMutex);
    m_postTarget = m_hwnd;
    return true;
}

void CSecretChatModule::DestroyMessageWindow()
{
    {
        std::lock_guard<std::mutex> guard(m_wndMutex);
        m_postTarget = NULL;
    }
    if (m_hwnd == NULL)
        return;
    //丢弃还在队列里的帧，释放它们的包体
    MSG msg;
    while (::PeekMessageW(&msg, m_hwnd, kMsgSecretFrameArrived, kMsgSecretFrameArrived, PM_REMOVE))
        delete reinterpret_cast<FramePacket*>(msg.lParam);
    ::DestroyWindow(m_hwnd);
    m_hwnd = NULL;
    m_pumpRunning = false;
}

LRESULT CALLBACK CSecretChatModule::MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CSecretChatModule* self = reinterpret_cast<CSecretChatModule*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != NULL && msg == kMsgSecretFrameArrived)
    {
        FramePacket* packet = reinterpret_cast<FramePacket*>(lParam);
        if (packet != NULL)
        {
            self->ProcessFrame(packet->cmd, packet->seq, packet->body);
            WipeString(packet->body);
            delete packet;
        }
        return 0;
    }
    if (self != NULL && msg == WM_TIMER)
    {
        if (wParam == kTimerProtectCheck)
        {
            self->CheckProtectedWindows();
        }
        else if (wParam == kTimerPump)
        {
            if (!self->PumpSend(NowMs()))
            {
                ::KillTimer(hwnd, kTimerPump);
                self->m_pumpRunning = false;
            }
        }
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

//======================== 工具 ========================

std::string CSecretChatModule::ToUtf8(const wchar_t* text)
{
    if (text == NULL || text[0] == L'\0')
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (len <= 1)
        return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], len, NULL, NULL);
    out.resize(static_cast<size_t>(len - 1));
    return out;
}

std::wstring CSecretChatModule::FromUtf8(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], len);
    return out;
}

int64_t CSecretChatModule::NowMs()
{
    return static_cast<int64_t>(::GetTickCount64());
}

} // namespace secretchat
