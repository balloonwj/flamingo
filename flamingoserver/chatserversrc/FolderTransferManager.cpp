/**
 *  单聊文件夹传输会话管理器实现，FolderTransferManager.cpp
 *  balloonwj@qq.com   2026-08-13
 */
#include "FolderTransferManager.h"
#include "FolderTransferProtocol.h"   //enum folder_transfer_kind（单文件直传会话，2026-10-04 起）
#include "Protocol.h"                   // enum folder_transfer_state / enum error_code
#include "../utils/RelayToken.h"
#include "../base/AsyncLog.h"

//初始化列表的书写顺序必须与头文件里成员的声明顺序一致，
//否则 GCC 会报 -Wreorder：实际的初始化顺序由声明顺序决定，与这里写的顺序无关
FolderTransferManager::FolderTransferManager()
    : m_seqCounter(0)
    , m_relayPort(0)
    , m_dataLanes(kDefaultDataLanes)
    , m_maxFiles(kDefaultMaxFiles)
    , m_maxSize(kDefaultMaxSize)
    , m_maxSessions(kDefaultMaxSessions)
    , m_maxSessionsPerUser(kDefaultMaxSessionsPerUser)
    , m_inviteTimeoutMs(kDefaultInviteTimeoutMs)
    , m_retryWindowMs(kDefaultRetryWindowMs)
    , m_directFileInviteTimeoutMs(kDefaultDirectFileInviteTimeoutMs)
    , m_directFileMaxPerUser(kDefaultDirectFileMaxPerUser)
{
}

FolderTransferManager::~FolderTransferManager()
{
}

void FolderTransferManager::configure(const std::string& relayTokenKey,
                                      const std::string& relayHost, int relayPort,
                                      int dataLanes,
                                      int64_t maxFiles, int64_t maxSize,
                                      int maxSessions, int maxSessionsPerUser,
                                      int64_t inviteTimeoutMs, int64_t retryWindowMs)
{
    m_relayTokenKey = relayTokenKey;
    m_relayHost     = relayHost;
    m_relayPort     = relayPort;

    //数据通道条数是唯一有硬上限的配置项：一次传输在服务端占 2 × (N + 1) 条连接，
    //放开了会把 Windows 的 select 连接数吃光，故越界一律按硬上限截断并留下日志，
    //让运维一眼看出实际生效的值与配置文件里写的不一样
    if (dataLanes > kMaxDataLanes)
    {
        LOGW("folder transfer: folderlanes %d exceeds hard limit %d, clamped to %d.",
             dataLanes, kMaxDataLanes, kMaxDataLanes);
        m_dataLanes = kMaxDataLanes;
    }
    else if (dataLanes < 1)
    {
        LOGW("folder transfer: folderlanes %d is invalid, fall back to default %d.",
             dataLanes, kDefaultDataLanes);
        m_dataLanes = kDefaultDataLanes;
    }
    else
    {
        m_dataLanes = dataLanes;
    }

    //其余配置项没有硬上限，只挡住"配置项缺失或写成 0 / 负数"这一种明显笔误：
    //此时按默认值走，否则上限为 0 会把所有传输一律判超限
    m_maxFiles           = (maxFiles          > 0) ? maxFiles          : kDefaultMaxFiles;
    m_maxSize            = (maxSize           > 0) ? maxSize           : kDefaultMaxSize;
    m_maxSessions        = (maxSessions       > 0) ? maxSessions       : kDefaultMaxSessions;
    m_maxSessionsPerUser = (maxSessionsPerUser > 0) ? maxSessionsPerUser : kDefaultMaxSessionsPerUser;
    m_inviteTimeoutMs    = (inviteTimeoutMs   > 0) ? inviteTimeoutMs   : kDefaultInviteTimeoutMs;
    m_retryWindowMs      = (retryWindowMs     > 0) ? retryWindowMs     : kDefaultRetryWindowMs;

    LOGI("folder transfer configured, relay: %s:%d, lanes: %d, maxfiles: %lld, maxsize: %lld, "
         "maxsessions: %d, maxperuser: %d, invitetimeout: %lld ms, retrywindow: %lld ms.",
         m_relayHost.c_str(), m_relayPort, m_dataLanes,
         (long long)m_maxFiles, (long long)m_maxSize, m_maxSessions, m_maxSessionsPerUser,
         (long long)m_inviteTimeoutMs, (long long)m_retryWindowMs);
}

const std::string& FolderTransferManager::getRelayHost() const
{
    return m_relayHost;
}

int FolderTransferManager::getRelayPort() const
{
    return m_relayPort;
}

int FolderTransferManager::getDataLanes() const
{
    return m_dataLanes;
}

int64_t FolderTransferManager::getMaxFiles() const
{
    return m_maxFiles;
}

int64_t FolderTransferManager::getMaxSize() const
{
    return m_maxSize;
}

int FolderTransferManager::getMaxSessions() const
{
    return m_maxSessions;
}

int FolderTransferManager::getMaxSessionsPerUser() const
{
    return m_maxSessionsPerUser;
}

int64_t FolderTransferManager::getInviteTimeoutMs() const
{
    return m_inviteTimeoutMs;
}

int64_t FolderTransferManager::getRetryWindowMs() const
{
    return m_retryWindowMs;
}

int FolderTransferManager::checkScale(int64_t totalFiles, int64_t totalSize) const
{
    //正好等于上限放行，超出一个即拒绝
    if (totalFiles > m_maxFiles)
        return error_code_folder_too_many_files;

    if (totalSize > m_maxSize)
        return error_code_folder_too_large;

    return error_code_ok;
}

int FolderTransferManager::collectUserSessionsLocked(int64_t userId, int kindFilter,
                                                     std::vector<FolderTransferSessionInfo>* out) const
{
    int hit = 0;

    //会话表以 sessionid 为键有序存放，故顺序遍历下来追加进 out 的顺序天然是字典序
    for (std::map<std::string, FolderTransferSessionInfo>::const_iterator it = m_sessions.begin();
         it != m_sessions.end(); ++it)
    {
        //会话表里除了进行中的传输，还留着失败待重试的会话；后者同样占着这个人的
        //每用户名额（重试要接着往同一个目录续传），故一并算作命中
        if (it->second.senderId != userId && it->second.receiverId != userId)
            continue;

        //只数指定种类：文件夹传输与单文件直传各有各的名额（2026-10-04 起）
        if (kindFilter != kAnyKind && it->second.kind != kindFilter)
            continue;

        ++hit;

        //只要笔数的调用方（并发限制的名额判定）传 NULL，省掉整条会话的拷贝
        if (out != NULL)
            out->push_back(it->second);
    }

    return hit;
}

int FolderTransferManager::getUserSessionCount(int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return collectUserSessionsLocked(userId, folder_kind_folder, NULL);
}

int FolderTransferManager::countFolderSessionsLocked() const
{
    int count = 0;
    for (std::map<std::string, FolderTransferSessionInfo>::const_iterator it = m_sessions.begin();
         it != m_sessions.end(); ++it)
    {
        if (it->second.kind == folder_kind_folder)
            ++count;
    }
    return count;
}

int FolderTransferManager::findSessionsByUser(int64_t userId,
                                              std::vector<FolderTransferSessionInfo>& out)
{
    //先记下调用方原有的元素个数，便于只对本次新追加的那几条记日志
    size_t before = out.size();

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        collectUserSessionsLocked(userId, kAnyKind, &out);
    }

    //本方法只在连接断开 / 退出登录的收尾路径上被调用，命中即意味着有传输要被作废，
    //属于关键路径，逐笔记一条日志便于仅凭日志复原当时各会话的状态
    for (size_t i = before; i < out.size(); ++i)
    {
        LOGI("folder session located by user, userid: %lld, sessionid: %s, sender: %lld, "
             "receiver: %lld, state: %d.",
             (long long)userId, out[i].sessionid.c_str(),
             (long long)out[i].senderId, (long long)out[i].receiverId, out[i].state);
    }

    return (int)(out.size() - before);
}

bool FolderTransferManager::isServerBusy()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //这道闸门防的是 fileserver 中转连接被占满，单文件直传不用中转，不计入
    return countFolderSessionsLocked() >= m_maxSessions;
}

size_t FolderTransferManager::getActiveSessionCount()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_sessions.size();
}

int FolderTransferManager::checkInvite(int64_t senderId, int64_t receiverId,
                                       int64_t totalFiles, int64_t totalSize)
{
    //规模校验不碰会话表，放在加锁之前做
    int scaleCode = checkScale(totalFiles, totalSize);
    if (scaleCode != error_code_ok)
    {
        LOGI("folder invite rejected by scale limit, sender: %lld, receiver: %lld, "
             "files: %lld, size: %lld, code: %d.",
             (long long)senderId, (long long)receiverId,
             (long long)totalFiles, (long long)totalSize, scaleCode);
        return scaleCode;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    //每个用户同时可参与的笔数有上限，作发送方与作接收方各算一笔。
    //两道闸门（每用户上限、服务器总数上限）同时到顶时先报每用户上限：等自己那几笔里
    //有一笔传完就能继续，比"服务器繁忙"更有指导性
    int senderCount = collectUserSessionsLocked(senderId, folder_kind_folder, NULL);
    if (senderCount >= m_maxSessionsPerUser)
    {
        LOGI("folder invite rejected, sender %lld already has %d folder transfers, limit %d.",
             (long long)senderId, senderCount, m_maxSessionsPerUser);
        return error_code_folder_busy;
    }

    int receiverCount = collectUserSessionsLocked(receiverId, folder_kind_folder, NULL);
    if (receiverCount >= m_maxSessionsPerUser)
    {
        LOGI("folder invite rejected, receiver %lld already has %d folder transfers, limit %d.",
             (long long)receiverId, receiverCount, m_maxSessionsPerUser);
        return error_code_folder_busy;
    }

    const int folderSessions = countFolderSessionsLocked();
    if (folderSessions >= m_maxSessions)
    {
        LOGW("folder invite rejected, active folder sessions %d reached limit %d.", folderSessions, m_maxSessions);
        return error_code_folder_server_busy;
    }

    return error_code_ok;
}

std::string FolderTransferManager::createSession(int64_t senderId, int64_t receiverId,
                                                 const FolderTransferScale& scale, int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    //sessionid 由 发送端-接收端-时间-自增序号 拼成：序号保证同一毫秒内也唯一，
    //全部为数字与连字符，不含 RelayToken 的分隔符 '.' / '|'，可安全放入令牌 payload
    uint64_t seq = ++m_seqCounter;
    std::string sid = std::to_string(senderId) + "-" + std::to_string(receiverId)
                    + "-" + std::to_string(now) + "-" + std::to_string(seq);

    FolderTransferSessionInfo info;
    info.sessionid  = sid;
    info.senderId   = senderId;
    info.receiverId = receiverId;
    info.folderName = scale.folderName;
    info.totalDirs  = scale.totalDirs;
    info.totalFiles = scale.totalFiles;
    info.totalSize  = scale.totalSize;
    info.kind       = scale.kind;
    info.state      = folder_state_ringing;
    info.createTime = now;
    info.stateTime  = now;
    //第一次尝试。此后每受理一次重试加一，聊天记录里的传输记录条据此各留一条
    info.attemptSeq = kFirstAttemptSeq;

    m_sessions[sid] = info;

    LOGI("folder session created, sessionid: %s, kind: %d, sender: %lld, receiver: %lld, "
         "dirs: %lld, files: %lld, size: %lld, active sessions: %d.",
         sid.c_str(), scale.kind, (long long)senderId, (long long)receiverId,
         (long long)scale.totalDirs, (long long)scale.totalFiles, (long long)scale.totalSize,
         (int)m_sessions.size());

    return sid;
}

bool FolderTransferManager::getSession(const std::string& sessionid, FolderTransferSessionInfo& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, FolderTransferSessionInfo>::const_iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return false;

    out = it->second;
    return true;
}

bool FolderTransferManager::setState(const std::string& sessionid, int state, int64_t now)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, FolderTransferSessionInfo>::iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return false;

    int oldState = it->second.state;
    it->second.state     = state;
    it->second.stateTime = now;

    LOGI("folder session state changed, sessionid: %s, state: %d -> %d.",
         sessionid.c_str(), oldState, state);

    return true;
}

bool FolderTransferManager::beginRetryAttempt(const std::string& sessionid, int64_t now,
                                              int& outAttemptSeq)
{
    outAttemptSeq = 0;

    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, FolderTransferSessionInfo>::iterator it = m_sessions.find(sessionid);
    if (it == m_sessions.end())
        return false;

    int oldState = it->second.state;

    //状态变更与序号加一必须在同一个临界区内完成，理由见头文件里本方法的注释
    it->second.state     = folder_state_transferring;
    it->second.stateTime = now;
    ++it->second.attemptSeq;

    outAttemptSeq = it->second.attemptSeq;

    LOGI("folder session retry attempt started, sessionid: %s, state: %d -> %d, attempt: %d.",
         sessionid.c_str(), oldState, folder_state_transferring, outAttemptSeq);

    return true;
}

bool FolderTransferManager::finishAttempt(const std::string& sessionid, int attemptSeq,
                                          int terminalState, int64_t now)
{
    //终态之外的取值一律是调用方参数写错：真按它登记的话，这一次尝试此后再走到真正的
    //终态时会被当成重复而丢掉整条记录，故此处既不登记也不改状态，只记一条告警。
    //终态的范围与协议层同口径（3~9，9 是 2026-10-04 起单文件直传的「已改为离线发送」）
    if (!FolderTransferProtocol::isTerminalState(terminalState))
    {
        LOGW("folder finish attempt rejected, state %d is not a terminal state, sessionid: %s, "
             "attempt: %d.",
             terminalState, sessionid.c_str(), attemptSeq);
        return false;
    }

    //以下三件事 ——「查本次尝试是否已经终结过」「登记为已终结」「把会话置为终态」——
    //必须在同一个临界区内一次做完。分成两次加锁（先查、再写）就等于把闸门打开：两个
    //工作线程各拿一帧 cmd_folder_close 进来，都在对方登记之前查到"还没终结过"，于是
    //各留一条一模一样的记录条、各推一帧状态变更，正是本方法要消灭的那个缺陷
    int oldState = 0;
    bool sessionAlive = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        //序号不大于已终结序号的一律是重复的：同一次尝试只终结一次，而序号只增不减
        std::map<std::string, FinishedAttempt>::iterator it = m_finishedAttempts.find(sessionid);
        if (it != m_finishedAttempts.end() && it->second.attemptSeq >= attemptSeq)
        {
            LOGI("folder terminal decision deduplicated, sessionid: %s, attempt: %d, state: %d, "
                 "already finished up to attempt %d.",
                 sessionid.c_str(), attemptSeq, terminalState, it->second.attemptSeq);
            return false;
        }

        if (it != m_finishedAttempts.end())
        {
            //同一个会话的后一次尝试：覆盖成新序号，此后按新序号判重
            it->second.attemptSeq = attemptSeq;
            it->second.finishTime = now;
        }
        else
        {
            FinishedAttempt mark;
            mark.attemptSeq = attemptSeq;
            mark.finishTime = now;
            m_finishedAttempts[sessionid] = mark;
        }

        //会话已被销毁时只登记、不改状态：邀请超时那条路径上 tick 已经把会话移出会话表，
        //调用方手里只有一份快照
        std::map<std::string, FolderTransferSessionInfo>::iterator sit = m_sessions.find(sessionid);
        if (sit != m_sessions.end())
        {
            sessionAlive = true;
            oldState = sit->second.state;
            sit->second.state     = terminalState;
            sit->second.stateTime = now;
        }
    }

    LOGI("folder attempt finished, sessionid: %s, attempt: %d, terminal state: %d, "
         "session alive: %d, session state: %d -> %d.",
         sessionid.c_str(), attemptSeq, terminalState, sessionAlive ? 1 : 0,
         oldState, terminalState);

    return true;
}

bool FolderTransferManager::removeSession(const std::string& sessionid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_sessions.erase(sessionid) == 0)
        return false;

    LOGI("folder session removed, sessionid: %s, active sessions: %d.",
         sessionid.c_str(), (int)m_sessions.size());

    return true;
}

bool FolderTransferManager::signTokenFor(const std::string& sessionid, int role,
                                         int64_t now, std::string& outToken)
{
    //先在锁内取出会话的双方 userid，再在锁外签名（签名为纯计算，不必持锁）
    int64_t senderId   = 0;
    int64_t receiverId = 0;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<std::string, FolderTransferSessionInfo>::const_iterator it = m_sessions.find(sessionid);
        if (it == m_sessions.end())
            return false;

        senderId   = it->second.senderId;
        receiverId = it->second.receiverId;
    }

    RelayTokenPayload payload;
    payload.sessionid = sessionid;
    payload.role      = role;
    //发送端令牌：uid=发送端、peeruid=接收端；接收端令牌：两者对调
    if (role == RelayToken::role_folder_sender)
    {
        payload.uid     = senderId;
        payload.peeruid = receiverId;
    }
    else
    {
        payload.uid     = receiverId;
        payload.peeruid = senderId;
    }
    payload.expire = now + kRelayTokenTtlMs;

    outToken = RelayToken::sign(payload, m_relayTokenKey);
    return true;
}

void FolderTransferManager::tick(int64_t now,
                                 std::vector<FolderTransferSessionInfo>& outTimedOut,
                                 std::vector<FolderTransferSessionInfo>& outExpired)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, FolderTransferSessionInfo>::iterator it = m_sessions.begin();
    while (it != m_sessions.end())
    {
        //（1）邀请超时：仍在响铃、且响铃时长已达阈值。回填给调用方的那份状态改写成
        //超时态，调用方照它推 cmd_folder_state_notify 即可，不必自己再改一遍
        //单文件直传的邀请超时另行配置（默认 5 分钟，超时后发送端改为上传服务器的离线文件）
        const int64_t inviteTimeoutMs = (it->second.kind == folder_kind_file) ? m_directFileInviteTimeoutMs : m_inviteTimeoutMs;
        if (it->second.state == folder_state_ringing
            && now - it->second.createTime >= inviteTimeoutMs)
        {
            it->second.state     = folder_state_timeout;
            it->second.stateTime = now;
            outTimedOut.push_back(it->second);

            LOGI("folder session invite timed out, sessionid: %s, kind: %d, sender: %lld, receiver: %lld.",
                 it->second.sessionid.c_str(), it->second.kind,
                 (long long)it->second.senderId, (long long)it->second.receiverId);

            //erase 返回下一个迭代器，避免失效
            it = m_sessions.erase(it);
            continue;
        }

        //（2）保留期届满：失败态且失败至今已达保留期，此后不再允许重试续传，彻底销毁
        if (it->second.state == folder_state_failed
            && now - it->second.stateTime >= m_retryWindowMs)
        {
            outExpired.push_back(it->second);

            LOGI("folder session retry window expired, sessionid: %s, sender: %lld, receiver: %lld.",
                 it->second.sessionid.c_str(),
                 (long long)it->second.senderId, (long long)it->second.receiverId);

            it = m_sessions.erase(it);
            continue;
        }

        ++it;
    }

    //（3）顺带丢弃过期的"已终结尝试"登记项。判定分两步：所属会话还在表里的一律留着
    //（失败会话在整个保留期内都可能再收到一帧重复的 cmd_folder_close），会话已经销毁的
    //再多留 kFinishedAttemptRetentionMs，接住那些赶在销毁之后才到达的重复终结判定
    std::map<std::string, FinishedAttempt>::iterator mit = m_finishedAttempts.begin();
    while (mit != m_finishedAttempts.end())
    {
        if (m_sessions.find(mit->first) != m_sessions.end()
            || now - mit->second.finishTime < kFinishedAttemptRetentionMs)
        {
            ++mit;
            continue;
        }

        mit = m_finishedAttempts.erase(mit);
    }
}

bool FolderTransferManager::canRetry(const std::string& sessionid, int64_t uid,
                                     int64_t now, int& outErrCode)
{
    outErrCode = error_code_ok;

    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, FolderTransferSessionInfo>::const_iterator it = m_sessions.find(sessionid);

    //会话已被保留期清理销毁（或 sessionid 根本不对）：对用户就是"已超时，请重新发送"
    if (it == m_sessions.end())
    {
        LOGI("folder retry rejected, session not found, sessionid: %s, uid: %lld.",
             sessionid.c_str(), (long long)uid);
        outErrCode = error_code_folder_session_expired;
        return false;
    }

    const FolderTransferSessionInfo& s = it->second;

    //重试入口只在发送方的失败气泡上；接收方与第三方即便拿到 sessionid 也无权发起。
    //先判参与方再判状态，避免向非参与方泄露会话当前状态
    if (uid != s.senderId)
    {
        LOGW("folder retry rejected, uid %lld is not the sender of session %s.",
             (long long)uid, sessionid.c_str());
        outErrCode = error_code_permission_denied;
        return false;
    }

    //只有失败的会话才谈得上重试续传；响铃中 / 传输中 / 已完成的会话收到重试请求，
    //说明客户端状态与服务端不一致，按非法请求处理
    if (s.state != folder_state_failed)
    {
        LOGW("folder retry rejected, session %s is in state %d, not failed.",
             sessionid.c_str(), s.state);
        outErrCode = error_code_bad_request;
        return false;
    }

    //保留期以进入失败态的时刻起算；正好到点即视为过期，与 tick 的清理口径一致。
    //会话尚在表里只是因为定时器还没扫到它
    if (now - s.stateTime >= m_retryWindowMs)
    {
        LOGI("folder retry rejected, session %s expired, failed at %lld, now %lld, window %lld ms.",
             sessionid.c_str(), (long long)s.stateTime, (long long)now,
             (long long)m_retryWindowMs);
        outErrCode = error_code_folder_session_expired;
        return false;
    }

    LOGI("folder retry accepted, sessionid: %s, sender: %lld, receiver: %lld.",
         sessionid.c_str(), (long long)s.senderId, (long long)s.receiverId);

    return true;
}

//类内初始化的静态常量的类外定义：单元测试按引用使用它们（doctest 的 CHECK）构成 ODR 使用，g++ 链接时需要定义
const int64_t FolderTransferManager::kDefaultDirectFileInviteTimeoutMs;
const int FolderTransferManager::kDefaultDirectFileMaxPerUser;

//==== 2026-10-04 单文件直传 ====

void FolderTransferManager::configureDirectFile(int64_t inviteTimeoutMs, int maxPerUser)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //配置项缺失或写成 0 / 负数时按默认值走
    m_directFileInviteTimeoutMs = (inviteTimeoutMs > 0) ? inviteTimeoutMs : kDefaultDirectFileInviteTimeoutMs;
    m_directFileMaxPerUser      = (maxPerUser > 0) ? maxPerUser : kDefaultDirectFileMaxPerUser;
    LOGI("direct file transfer configured, invitetimeout: %lld ms, maxperuser: %d.",
         (long long)m_directFileInviteTimeoutMs, m_directFileMaxPerUser);
}

int64_t FolderTransferManager::getDirectFileInviteTimeoutMs() const
{
    return m_directFileInviteTimeoutMs;
}

int FolderTransferManager::getDirectFileMaxPerUser() const
{
    return m_directFileMaxPerUser;
}

int FolderTransferManager::checkDirectFileInvite(int64_t senderId, int64_t receiverId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //只数单文件直传会话；先判发送方再判接收方，与 checkInvite 的口径一致
    const int senderCount = collectUserSessionsLocked(senderId, folder_kind_file, NULL);
    if (senderCount >= m_directFileMaxPerUser)
    {
        LOGI("direct file invite rejected, sender %lld already has %d direct transfers, limit %d.",
             (long long)senderId, senderCount, m_directFileMaxPerUser);
        return error_code_folder_busy;
    }
    const int receiverCount = collectUserSessionsLocked(receiverId, folder_kind_file, NULL);
    if (receiverCount >= m_directFileMaxPerUser)
    {
        LOGI("direct file invite rejected, receiver %lld already has %d direct transfers, limit %d.",
             (long long)receiverId, receiverCount, m_directFileMaxPerUser);
        return error_code_folder_busy;
    }
    return error_code_ok;
}
