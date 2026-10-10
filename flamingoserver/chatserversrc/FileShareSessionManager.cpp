/**
 *  文件共享的访问会话与传输子会话管理（服务端侧）实现，FileShareSessionManager.cpp
 *
 *  所有公开方法都在 m_mutex 保护下读写会话表；会话数量受 sharemaxaccess 限制（几十个量级），
 *  按共享、按用户、按连接的反查直接遍历 m_accesses，不另建索引。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "FileShareSessionManager.h"

#include <stdio.h>
#include <set>
#include "FileShareProtocol.h"
#include "Protocol.h"
#include "../utils/RelayToken.h"
#include "../utils/RelayRoleCheck.h"   //kShareRelaySessionIdPrefix：与 fileserver 报到校验共用同一个前缀

namespace {

//生成会话 id 时数字转文本的缓冲区字节数（两个 64 位整数加前缀，留余量）
const int kSessionIdTextChars = 96;


//传输子会话 id 在访问会话 id 之后追加的分隔串
const char* const kTransferIdInfix = "-t";

//把小于 1 的取值换成默认值
int positiveOrDefault(int value, int defaultValue)
{
    if (value < 1)
        return defaultValue;
    return value;
}

} // namespace

//static const 整型成员被按引用使用（如测试里的 CHECK）时需要类外定义，否则 GCC 下链接失败
const int FileShareSessionManager::kShareMaxLanes;
const int FileShareSessionManager::kDefaultLanes;
const int FileShareSessionManager::kDefaultMaxAccess;
const int FileShareSessionManager::kDefaultMaxTransfers;
const int FileShareSessionManager::kDefaultMaxTransferPerShare;
const int FileShareSessionManager::kDefaultOpenTimeoutMs;
const int FileShareSessionManager::kDefaultIdleTimeoutMs;
const int FileShareSessionManager::kDefaultSearchMaxResults;
const int FileShareSessionManager::kDefaultSearchTimeoutMs;
const int64_t FileShareSessionManager::kRelayTokenTtlMs;

FileShareSessionConfig::FileShareSessionConfig()
    : m_relayPort(0)
    , m_lanes(FileShareSessionManager::kDefaultLanes)
    , m_maxAccess(FileShareSessionManager::kDefaultMaxAccess)
    , m_maxTransfers(FileShareSessionManager::kDefaultMaxTransfers)
    , m_maxTransferPerShare(FileShareSessionManager::kDefaultMaxTransferPerShare)
    , m_openTimeoutMs(FileShareSessionManager::kDefaultOpenTimeoutMs)
    , m_idleTimeoutMs(FileShareSessionManager::kDefaultIdleTimeoutMs)
    , m_searchMaxResults(FileShareSessionManager::kDefaultSearchMaxResults)
    , m_searchTimeoutMs(FileShareSessionManager::kDefaultSearchTimeoutMs)
{
}

FileShareSessionManager::FileShareSessionManager()
    : m_accessSeq(0)
    , m_transferSeq(0)
{
}

FileShareSessionManager::~FileShareSessionManager()
{
}

void FileShareSessionManager::configure(const FileShareSessionConfig& config)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_config = config;

    if (m_config.m_lanes < 1)
        m_config.m_lanes = kDefaultLanes;
    else if (m_config.m_lanes > kShareMaxLanes)
        m_config.m_lanes = kShareMaxLanes;

    m_config.m_maxAccess = positiveOrDefault(m_config.m_maxAccess, kDefaultMaxAccess);
    m_config.m_maxTransfers = positiveOrDefault(m_config.m_maxTransfers, kDefaultMaxTransfers);
    m_config.m_maxTransferPerShare = positiveOrDefault(m_config.m_maxTransferPerShare, kDefaultMaxTransferPerShare);
    m_config.m_openTimeoutMs = positiveOrDefault(m_config.m_openTimeoutMs, kDefaultOpenTimeoutMs);
    m_config.m_idleTimeoutMs = positiveOrDefault(m_config.m_idleTimeoutMs, kDefaultIdleTimeoutMs);
    m_config.m_searchMaxResults = positiveOrDefault(m_config.m_searchMaxResults, kDefaultSearchMaxResults);
    m_config.m_searchTimeoutMs = positiveOrDefault(m_config.m_searchTimeoutMs, kDefaultSearchTimeoutMs);
}

const FileShareSessionConfig& FileShareSessionManager::getConfig() const
{
    return m_config;
}

int FileShareSessionManager::openAccess(int64_t shareId, int64_t ownerId, ShareConnKey ownerConn,
                                        int64_t visitorId, ShareConnKey visitorConn, int64_t nowMs,
                                        std::string& accessId)
{
    accessId.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_accesses.size() >= (size_t)m_config.m_maxAccess)
        return error_code_share_server_busy;

    ++m_accessSeq;
    char buf[kSessionIdTextChars];
    snprintf(buf, sizeof(buf), "%s%llu-%lld", kShareRelaySessionIdPrefix,
             (unsigned long long)m_accessSeq, (long long)nowMs);

    ShareAccessInfo info;
    info.m_accessId = buf;
    info.m_shareId = shareId;
    info.m_ownerId = ownerId;
    info.m_visitorId = visitorId;
    info.m_ownerConn = ownerConn;
    info.m_visitorConn = visitorConn;
    info.m_createTimeMs = nowMs;
    m_accesses[info.m_accessId] = info;

    accessId = info.m_accessId;
    return error_code_ok;
}

bool FileShareSessionManager::getAccess(const std::string& accessId, ShareAccessInfo& out) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ShareAccessInfo>::const_iterator it = m_accesses.find(accessId);
    if (it == m_accesses.end())
        return false;

    out = it->second;
    return true;
}

int FileShareSessionManager::requestTransfer(const std::string& accessId, int64_t visitorId, int direction,
                                             std::string& transferId)
{
    transferId.clear();
    if (direction != share_transfer_download && direction != share_transfer_upload)
        return error_code_bad_request;

    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ShareAccessInfo>::iterator it = m_accesses.find(accessId);
    if (it == m_accesses.end())
        return error_code_share_not_exist;

    ShareAccessInfo& access = it->second;
    if (access.m_visitorId != visitorId)
        return error_code_permission_denied;

    //同一访问会话名下同时只能有一笔传输
    if (!access.m_transferId.empty())
        return error_code_share_transfer_busy;

    //每个共享的人数闸门先于整台服务器的闸门判定：同时到顶时报 813，用户换个时间或换个共享就能继续，
    //比笼统的「服务器繁忙」更有指导性
    if (countTransferringVisitorsLocked(access.m_shareId) >= (size_t)m_config.m_maxTransferPerShare)
        return error_code_share_transfer_busy;

    if (m_transferToAccess.size() >= (size_t)m_config.m_maxTransfers)
        return error_code_share_server_busy;

    ++m_transferSeq;
    char buf[kSessionIdTextChars];
    snprintf(buf, sizeof(buf), "%s%llu", kTransferIdInfix, (unsigned long long)m_transferSeq);

    access.m_transferId = access.m_accessId + buf;
    access.m_transferDirection = direction;
    m_transferToAccess[access.m_transferId] = access.m_accessId;

    transferId = access.m_transferId;
    return error_code_ok;
}

bool FileShareSessionManager::closeTransfer(const std::string& transferId, ShareAccessInfo& access)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::string>::iterator tit = m_transferToAccess.find(transferId);
    if (tit == m_transferToAccess.end())
        return false;

    std::map<std::string, ShareAccessInfo>::iterator ait = m_accesses.find(tit->second);
    m_transferToAccess.erase(tit);
    //两张表在同一把锁下同步维护，按理不会出现传输还在、访问会话已不在的情况；出现时只清理传输表
    if (ait == m_accesses.end())
        return false;

    //先取快照再清空，快照里保留被结束的传输 id 与方向，供调用方通知两端
    access = ait->second;
    ait->second.m_transferId.clear();
    ait->second.m_transferDirection = 0;
    return true;
}

bool FileShareSessionManager::closeAccess(const std::string& accessId, ShareAccessInfo& closed)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ShareAccessInfo>::iterator it = m_accesses.find(accessId);
    if (it == m_accesses.end())
        return false;

    closed = it->second;
    if (!closed.m_transferId.empty())
        m_transferToAccess.erase(closed.m_transferId);
    m_accesses.erase(it);
    return true;
}

std::vector<ShareAccessInfo> FileShareSessionManager::closeAllForShare(int64_t shareId)
{
    std::vector<ShareAccessInfo> closed;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ShareAccessInfo>::iterator it = m_accesses.begin();
    while (it != m_accesses.end())
    {
        if (it->second.m_shareId != shareId)
        {
            ++it;
            continue;
        }

        closed.push_back(it->second);
        if (!it->second.m_transferId.empty())
            m_transferToAccess.erase(it->second.m_transferId);
        m_accesses.erase(it++);
    }
    return closed;
}

std::vector<ShareAccessInfo> FileShareSessionManager::closeForMember(int64_t shareId, int64_t visitorId)
{
    std::vector<ShareAccessInfo> closed;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ShareAccessInfo>::iterator it = m_accesses.begin();
    while (it != m_accesses.end())
    {
        if (it->second.m_shareId != shareId || it->second.m_visitorId != visitorId)
        {
            ++it;
            continue;
        }

        closed.push_back(it->second);
        if (!it->second.m_transferId.empty())
            m_transferToAccess.erase(it->second.m_transferId);
        m_accesses.erase(it++);
    }
    return closed;
}

std::vector<ShareAccessInfo> FileShareSessionManager::closeForConnection(ShareConnKey conn)
{
    std::vector<ShareAccessInfo> closed;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ShareAccessInfo>::iterator it = m_accesses.begin();
    while (it != m_accesses.end())
    {
        if (it->second.m_ownerConn != conn && it->second.m_visitorConn != conn)
        {
            ++it;
            continue;
        }

        closed.push_back(it->second);
        if (!it->second.m_transferId.empty())
            m_transferToAccess.erase(it->second.m_transferId);
        m_accesses.erase(it++);
    }
    return closed;
}

std::vector<ShareAccessInfo> FileShareSessionManager::findByUser(int64_t userId) const
{
    std::vector<ShareAccessInfo> found;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, ShareAccessInfo>::const_iterator it = m_accesses.begin(); it != m_accesses.end(); ++it)
    {
        if (it->second.m_ownerId == userId || it->second.m_visitorId == userId)
            found.push_back(it->second);
    }
    return found;
}

std::vector<ShareAccessInfo> FileShareSessionManager::findByShare(int64_t shareId) const
{
    std::vector<ShareAccessInfo> found;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, ShareAccessInfo>::const_iterator it = m_accesses.begin(); it != m_accesses.end(); ++it)
    {
        if (it->second.m_shareId == shareId)
            found.push_back(it->second);
    }
    return found;
}

size_t FileShareSessionManager::getAccessCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_accesses.size();
}

size_t FileShareSessionManager::getTransferCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_transferToAccess.size();
}

void FileShareSessionManager::signTokens(const std::string& relaySessionId, int64_t ownerId, int64_t visitorId,
                                         int64_t nowMs, std::string& ownerToken, std::string& visitorToken) const
{
    //密钥在 configure 之后不再变化，这里只读 m_config，不必持锁
    RelayTokenPayload owner;
    owner.sessionid = relaySessionId;
    owner.role = RelayToken::role_share_owner;
    owner.uid = ownerId;
    owner.peeruid = visitorId;
    owner.expire = nowMs + kRelayTokenTtlMs;
    ownerToken = RelayToken::sign(owner, m_config.m_relayTokenKey);

    RelayTokenPayload visitor = owner;
    visitor.role = RelayToken::role_share_visitor;
    visitor.uid = visitorId;
    visitor.peeruid = ownerId;
    visitorToken = RelayToken::sign(visitor, m_config.m_relayTokenKey);
}

size_t FileShareSessionManager::countTransferringVisitorsLocked(int64_t shareId) const
{
    //按访问者去重：同一人即便在同一共享上开了两个访问会话，也只算一个人
    std::set<int64_t> visitors;
    for (std::map<std::string, ShareAccessInfo>::const_iterator it = m_accesses.begin(); it != m_accesses.end(); ++it)
    {
        if (it->second.m_shareId == shareId && !it->second.m_transferId.empty())
            visitors.insert(it->second.m_visitorId);
    }
    return visitors.size();
}
