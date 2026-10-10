/**
 *  访问者状态机 CShareVisitor 的实现，见 ShareVisitor.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareVisitor.h"

#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"

namespace fileshare {

CShareVisitor::CShareVisitor(int64_t shareId, IShareVisitorTransport* transport, IShareVisitorObserver* observer)
    : m_shareId(shareId)
    , m_transport(transport)
    , m_observer(observer)
    , m_state(share_visitor_idle)
    , m_errCode(0)
    , m_openSeq(0)
    , m_abandonedSeq(0)
    , m_deadline(0)
    , m_openTimeoutMs(kShareVisitorDefaultOpenTimeoutMs)
    , m_idleTimeoutMs(kShareVisitorDefaultIdleTimeoutMs)
    , m_lastActivityMs(0)
    , m_kind(0)
    , m_includeHidden(false)
    , m_perms(0)
    , m_nextReqId(1)
    , m_transferCount(0)
    , m_listPending(false)
    , m_listSent(false)
    , m_listReqId(0)
    , m_listDeadline(0)
    , m_searchReqId(0)
    , m_searchSent(false)
    , m_searchDeadline(0)
    , m_treeReqId(0)
    , m_treeSent(false)
    , m_treeMax(0)
    , m_treeDeadline(0)
{
}

void CShareVisitor::List(const std::string& path, int64_t nowMs)
{
    //新的请求作废旧的：换一个请求号，迟到的旧应答按请求号对不上而忽略
    m_listPending = true;
    m_listSent = false;
    m_listPath = path;
    m_listReqId = m_nextReqId++;
    m_listEntries.clear();
    m_lastActivityMs = nowMs;

    if (m_state == share_visitor_ready)
    {
        SendQueuedList(nowMs);
        return;
    }
    //未打开、空闲回收或失败之后：重新打开；正在打开时排队即可
    if (m_state == share_visitor_idle || m_state == share_visitor_failed)
        Open(nowMs);
}

int64_t CShareVisitor::MakeDirectory(const std::string& path, int64_t nowMs)
{
    ShareControlMessage req;
    req.m_type = sf_mkdir;
    req.m_path = path;
    return RequestOp(req, nowMs);
}

int64_t CShareVisitor::Rename(const std::string& path, const std::string& newName, int64_t nowMs)
{
    ShareControlMessage req;
    req.m_type = sf_rename;
    req.m_path = path;
    req.m_newName = newName;
    return RequestOp(req, nowMs);
}

int64_t CShareVisitor::Delete(const std::vector<std::string>& paths, int64_t nowMs)
{
    ShareControlMessage req;
    req.m_type = sf_delete;
    req.m_paths = paths;
    return RequestOp(req, nowMs);
}

int64_t CShareVisitor::Move(const std::vector<std::string>& paths, const std::string& toDir, int64_t nowMs)
{
    ShareControlMessage req;
    req.m_type = sf_move;
    req.m_paths = paths;
    req.m_toDir = toDir;
    return RequestOp(req, nowMs);
}

size_t CShareVisitor::GetPendingOpCount() const
{
    return m_ops.size();
}

int64_t CShareVisitor::Search(const std::string& path, const std::string& keyword, int64_t nowMs)
{
    //新的搜索取代旧的：旧的已发出就告诉分享者停下，不再回报
    CancelSearch();
    m_searchReqId = m_nextReqId++;
    m_searchSent = false;
    m_searchPath = path;
    m_searchKeyword = keyword;
    m_lastActivityMs = nowMs;
    LOG_INFO("share visitor search requested, shareid: %lld, reqid: %lld.", static_cast<long long>(m_shareId),
             static_cast<long long>(m_searchReqId));
    if (m_state == share_visitor_ready)
        SendQueuedSearch(nowMs);
    //未打开、空闲回收或失败之后：重新打开；正在打开时排队即可
    else if (m_state == share_visitor_idle || m_state == share_visitor_failed)
        Open(nowMs);
    return m_searchReqId;
}

void CShareVisitor::CancelSearch()
{
    if (m_searchReqId == 0)
        return;
    if (m_searchSent && m_state == share_visitor_ready)
        SendCancel(m_searchReqId);
    m_searchReqId = 0;
    m_searchSent = false;
}

int64_t CShareVisitor::GetSearchReqId() const
{
    return m_searchReqId;
}

int64_t CShareVisitor::RequestThumb(const std::string& path, int maxEdge, int64_t nowMs)
{
    PendingOp op;
    op.m_req.m_type = sf_thumb;
    op.m_req.m_reqId = m_nextReqId++;
    op.m_req.m_path = path;
    op.m_req.m_maxEdge = maxEdge;
    m_thumbs[op.m_req.m_reqId] = op;
    m_lastActivityMs = nowMs;
    if (m_state == share_visitor_ready)
        SendQueuedThumbs(nowMs);
    //未打开、空闲回收或失败之后：重新打开；正在打开时排队即可
    else if (m_state == share_visitor_idle || m_state == share_visitor_failed)
        Open(nowMs);
    return op.m_req.m_reqId;
}

void CShareVisitor::CancelThumb(int64_t reqId)
{
    std::map<int64_t, PendingOp>::iterator it = m_thumbs.find(reqId);
    if (it == m_thumbs.end())
        return;
    if (it->second.m_sent && m_state == share_visitor_ready)
        SendCancel(reqId);
    m_thumbs.erase(it);
}

size_t CShareVisitor::GetPendingThumbCount() const
{
    return m_thumbs.size();
}

int64_t CShareVisitor::ListTree(const std::string& path, int maxItems, int64_t nowMs)
{
    //新的取代旧的：旧的已发出就告诉分享者停下，不再交出
    if (m_treeReqId != 0 && m_treeSent && m_state == share_visitor_ready)
        SendCancel(m_treeReqId);
    m_treeReqId = m_nextReqId++;
    m_treeSent = false;
    m_treePath = path;
    m_treeMax = (maxItems > 0) ? maxItems : kShareTreeMaxItems + 1;
    m_treeEntries.clear();
    m_lastActivityMs = nowMs;
    if (m_state == share_visitor_ready)
        SendQueuedTree(nowMs);
    //未打开、空闲回收或失败之后：重新打开；正在打开时排队即可
    else if (m_state == share_visitor_idle || m_state == share_visitor_failed)
        Open(nowMs);
    return m_treeReqId;
}

int64_t CShareVisitor::GetTreeReqId() const
{
    return m_treeReqId;
}

void CShareVisitor::SendQueuedTree(int64_t nowMs)
{
    if (m_treeReqId == 0 || m_treeSent)
        return;
    ShareControlMessage req;
    req.m_type = sf_search;
    req.m_reqId = m_treeReqId;
    req.m_path = m_treePath;
    req.m_matchAll = true;
    req.m_maxItems = m_treeMax;
    std::string frame;
    if (!ShareControlFraming::BuildFrame(req, frame) || !m_transport->SendFrame(frame))
    {
        FinishTree(error_code_share_relay_broken, false);
        return;
    }
    m_treeSent = true;
    m_treeDeadline = nowMs + kShareVisitorRequestTimeoutMs;
}

void CShareVisitor::FinishTree(int code, bool truncated)
{
    if (m_treeReqId == 0)
        return;
    const int64_t reqId = m_treeReqId;
    const std::string path = m_treePath;
    std::vector<ShareEntry> entries;
    if (code == error_code_ok)
        entries.swap(m_treeEntries);
    m_treeEntries.clear();
    m_treeReqId = 0;
    m_treeSent = false;
    if (m_observer != NULL)
        m_observer->OnVisitorTreeResult(reqId, path, entries, truncated, code);
}

void CShareVisitor::SendQueuedThumbs(int64_t nowMs)
{
    //先收集再发：发送失败时直接按失败回报，回报可能让观察者再发新的请求
    std::vector<int64_t> ids;
    for (std::map<int64_t, PendingOp>::iterator it = m_thumbs.begin(); it != m_thumbs.end(); ++it)
    {
        if (!it->second.m_sent)
            ids.push_back(it->first);
    }
    for (size_t i = 0; i < ids.size(); ++i)
    {
        std::map<int64_t, PendingOp>::iterator it = m_thumbs.find(ids[i]);
        if (it == m_thumbs.end())
            continue;
        std::string frame;
        if (!ShareControlFraming::BuildFrame(it->second.m_req, frame) || !m_transport->SendFrame(frame))
        {
            const std::string path = it->second.m_req.m_path;
            m_thumbs.erase(it);
            if (m_observer != NULL)
                m_observer->OnVisitorThumbResult(ids[i], path, error_code_share_relay_broken, 0, 0, std::string());
            continue;
        }
        it->second.m_sent = true;
        it->second.m_deadline = nowMs + kShareVisitorRequestTimeoutMs;
    }
}

void CShareVisitor::FailPendingThumbs(int errCode)
{
    std::map<int64_t, PendingOp> thumbs;
    thumbs.swap(m_thumbs);
    if (m_observer == NULL)
        return;
    for (std::map<int64_t, PendingOp>::iterator it = thumbs.begin(); it != thumbs.end(); ++it)
        m_observer->OnVisitorThumbResult(it->first, it->second.m_req.m_path, errCode, 0, 0, std::string());
}

void CShareVisitor::SendQueuedSearch(int64_t nowMs)
{
    if (m_searchReqId == 0 || m_searchSent)
        return;
    ShareControlMessage req;
    req.m_type = sf_search;
    req.m_reqId = m_searchReqId;
    req.m_path = m_searchPath;
    req.m_keyword = m_searchKeyword;
    std::string frame;
    if (!ShareControlFraming::BuildFrame(req, frame) || !m_transport->SendFrame(frame))
    {
        FinishSearch(error_code_share_relay_broken);
        return;
    }
    m_searchSent = true;
    m_searchDeadline = nowMs + kShareVisitorRequestTimeoutMs;
}

void CShareVisitor::SendCancel(int64_t reqId)
{
    ShareControlMessage req;
    req.m_type = sf_cancel;
    req.m_reqId = reqId;
    std::string frame;
    if (ShareControlFraming::BuildFrame(req, frame))
        m_transport->SendFrame(frame);
}

void CShareVisitor::FinishSearch(int code)
{
    if (m_searchReqId == 0)
        return;
    const int64_t reqId = m_searchReqId;
    m_searchReqId = 0;
    m_searchSent = false;
    if (m_observer != NULL)
        m_observer->OnVisitorSearchResult(reqId, std::vector<ShareEntry>(), true, false, code);
}

int64_t CShareVisitor::RequestOp(ShareControlMessage& req, int64_t nowMs)
{
    req.m_reqId = m_nextReqId++;
    PendingOp op;
    op.m_req = req;
    m_ops[req.m_reqId] = op;
    m_lastActivityMs = nowMs;
    LOG_INFO("share visitor write op requested, shareid: %lld, type: %d, reqid: %lld.", static_cast<long long>(m_shareId),
             req.m_type, static_cast<long long>(req.m_reqId));

    if (m_state == share_visitor_ready)
        SendQueuedOps(nowMs);
    //未打开、空闲回收或失败之后：重新打开；正在打开时排队即可
    else if (m_state == share_visitor_idle || m_state == share_visitor_failed)
        Open(nowMs);
    return req.m_reqId;
}

void CShareVisitor::SendQueuedOps(int64_t nowMs)
{
    //先收集再发：发送失败会让会话失败，进而清空 m_ops
    std::vector<int64_t> ids;
    for (std::map<int64_t, PendingOp>::iterator it = m_ops.begin(); it != m_ops.end(); ++it)
    {
        if (!it->second.m_sent)
            ids.push_back(it->first);
    }
    for (size_t i = 0; i < ids.size(); ++i)
    {
        std::map<int64_t, PendingOp>::iterator it = m_ops.find(ids[i]);
        if (it == m_ops.end())
            continue;
        std::string frame;
        if (!ShareControlFraming::BuildFrame(it->second.m_req, frame) || !m_transport->SendFrame(frame))
        {
            const int type = it->second.m_req.m_type;
            m_ops.erase(it);
            if (m_observer != NULL)
                m_observer->OnVisitorOpResult(ids[i], type, error_code_share_relay_broken, std::vector<ShareOpItem>());
            continue;
        }
        it->second.m_sent = true;
        it->second.m_deadline = nowMs + kShareVisitorRequestTimeoutMs;
    }
}

void CShareVisitor::FailPendingOps(int errCode)
{
    std::map<int64_t, PendingOp> ops;
    ops.swap(m_ops);
    if (m_observer == NULL)
        return;
    for (std::map<int64_t, PendingOp>::iterator it = ops.begin(); it != ops.end(); ++it)
        m_observer->OnVisitorOpResult(it->first, it->second.m_req.m_type, errCode, std::vector<ShareOpItem>());
}

void CShareVisitor::Close()
{
    const bool relayUp = (m_state == share_visitor_waiting_hello || m_state == share_visitor_ready);
    const bool opened = (m_state != share_visitor_idle && m_state != share_visitor_failed && m_state != share_visitor_opening);
    if (relayUp)
    {
        ShareControlMessage bye;
        bye.m_type = sf_bye;
        std::string frame;
        if (ShareControlFraming::BuildFrame(bye, frame))
            m_transport->SendFrame(frame);
    }
    if (opened && !m_accessId.empty())
        m_transport->SendCommand(cmd_share_close_access, ShareProtocolCodec::BuildCloseAccessRequest(m_accessId, 0));
    m_transport->CloseRelay();
    m_listPending = false;
    m_listEntries.clear();
    //界面关闭时观察者随即离开：未完成的写操作与搜索不再回报（写操作的结果以分享者一端实际执行的为准）
    m_ops.clear();
    m_thumbs.clear();
    m_searchReqId = 0;
    m_searchSent = false;
    m_treeReqId = 0;
    m_treeSent = false;
    m_treeEntries.clear();
    if (m_openSeq != 0)
        m_abandonedSeq = m_openSeq;
    m_openSeq = 0;
    m_accessId.clear();
    SetState(share_visitor_idle, 0);
}

void CShareVisitor::EnsureOpen(int64_t nowMs)
{
    if (m_state != share_visitor_idle && m_state != share_visitor_failed)
        return;
    m_lastActivityMs = nowMs;
    LOG_INFO("share visitor reopening for queued transfers, shareid: %lld, state: %d.", static_cast<long long>(m_shareId),
             m_state);
    Open(nowMs);
}

bool CShareVisitor::OnOpenResponse(uint32_t seq, int code, const std::string& accessId, int64_t nowMs)
{
    //已放弃等待的那次打开：服务端若建成了访问会话，立即结束它
    if (seq != 0 && seq == m_abandonedSeq)
    {
        m_abandonedSeq = 0;
        if (code == error_code_ok && !accessId.empty())
            m_transport->SendCommand(cmd_share_close_access, ShareProtocolCodec::BuildCloseAccessRequest(accessId, 0));
        return true;
    }
    if (seq == 0 || seq != m_openSeq)
        return false;
    m_openSeq = 0;
    if (m_state != share_visitor_opening)
        return true;

    if (code != error_code_ok || accessId.empty())
    {
        LOG_WARNING("share open failed, shareid: %lld, code: %d.", static_cast<long long>(m_shareId), code);
        Fail(code != error_code_ok ? code : error_code_server_internal, false);
        return true;
    }
    m_accessId = accessId;
    m_deadline = nowMs + kShareVisitorSignalTimeoutMs;
    SetState(share_visitor_awaiting_relay, 0);
    return true;
}

bool CShareVisitor::OnRelayInfo(const ShareRelayParams& relay, int64_t nowMs)
{
    if (relay.m_forOwner || !relay.m_transferId.empty() || relay.m_accessId.empty() || relay.m_accessId != m_accessId)
        return false;
    if (m_state != share_visitor_awaiting_relay)
        return true;

    m_openTimeoutMs = (relay.m_openTimeoutMs > 0) ? relay.m_openTimeoutMs : kShareVisitorDefaultOpenTimeoutMs;
    m_idleTimeoutMs = (relay.m_idleTimeoutMs > 0) ? relay.m_idleTimeoutMs : kShareVisitorDefaultIdleTimeoutMs;
    if (!m_transport->StartRelay(relay))
    {
        Fail(error_code_share_relay_broken, true);
        return true;
    }
    //连接本身有自己的超时（约 5 秒）；这里再给一个兜底的截止时间，取等 sf_hello 的时长
    m_deadline = nowMs + m_openTimeoutMs;
    SetState(share_visitor_connecting, 0);
    return true;
}

void CShareVisitor::OnRelayConnected(bool ok, int64_t nowMs)
{
    if (m_state != share_visitor_connecting)
        return;
    if (!ok)
    {
        Fail(error_code_share_relay_broken, true);
        return;
    }
    m_deadline = nowMs + m_openTimeoutMs;
    SetState(share_visitor_waiting_hello, 0);
}

void CShareVisitor::OnFrame(const ShareControlMessage& msg, int64_t nowMs)
{
    if (m_state != share_visitor_waiting_hello && m_state != share_visitor_ready)
        return;

    switch (msg.m_type)
    {
    //握手首帧：只认第一个，分享者在收到本端第一帧之前会重发
    case sf_hello:
        if (m_state == share_visitor_waiting_hello)
        {
            m_kind = msg.m_kind;
            m_rootName = msg.m_rootName;
            m_includeHidden = msg.m_includeHidden;
            m_perms = msg.m_perms;
            m_lastActivityMs = nowMs;
            SetState(share_visitor_ready, 0);
            if (m_listPending && !m_listSent)
                SendQueuedList(nowMs);
            if (m_state == share_visitor_ready)
                SendQueuedOps(nowMs);
            if (m_state == share_visitor_ready)
                SendQueuedSearch(nowMs);
            if (m_state == share_visitor_ready)
                SendQueuedThumbs(nowMs);
            if (m_state == share_visitor_ready)
                SendQueuedTree(nowMs);
        }
        break;

    //列目录结果：请求号对得上才处理；more 为真时带游标续取
    case sf_list_result:
    {
        if (!m_listPending || !m_listSent || msg.m_reqId != m_listReqId)
            break;
        m_lastActivityMs = nowMs;
        if (msg.m_code != error_code_ok)
        {
            const std::string path = m_listPath;
            m_listPending = false;
            m_listEntries.clear();
            if (m_observer != NULL)
                m_observer->OnVisitorListed(path, msg.m_code, std::vector<ShareEntry>());
            break;
        }
        m_listEntries.insert(m_listEntries.end(), msg.m_entries.begin(), msg.m_entries.end());
        if (msg.m_more)
        {
            m_listDeadline = nowMs + kShareVisitorRequestTimeoutMs;
            if (!SendListFrame(msg.m_cursor))
                FailPendingList(error_code_share_relay_broken);
            break;
        }
        const std::string path = m_listPath;
        std::vector<ShareEntry> entries;
        entries.swap(m_listEntries);
        m_listPending = false;
        if (m_observer != NULL)
            m_observer->OnVisitorListed(path, error_code_ok, entries);
        break;
    }

    //单文件共享「列目录」的结果：查共享根本身得到的那一项，按列表交出
    case sf_stat_result:
    {
        if (!m_listPending || !m_listSent || msg.m_reqId != m_listReqId)
            break;
        m_lastActivityMs = nowMs;
        const std::string path = m_listPath;
        m_listPending = false;
        m_listEntries.clear();
        if (m_observer != NULL)
            m_observer->OnVisitorListed(path, msg.m_code, msg.m_code == error_code_ok ? msg.m_entries : std::vector<ShareEntry>());
        break;
    }

    //写操作结果：请求号对得上才处理
    case sf_op_result:
    {
        std::map<int64_t, PendingOp>::iterator it = m_ops.find(msg.m_reqId);
        if (it == m_ops.end() || !it->second.m_sent)
            break;
        m_lastActivityMs = nowMs;
        const int type = it->second.m_req.m_type;
        m_ops.erase(it);
        LOG_INFO("share visitor write op result, shareid: %lld, type: %d, reqid: %lld, code: %d.",
                 static_cast<long long>(m_shareId), type, static_cast<long long>(msg.m_reqId), msg.m_code);
        if (m_observer != NULL)
            m_observer->OnVisitorOpResult(msg.m_reqId, type, msg.m_code, msg.m_items);
        break;
    }

    //缩略图结果：请求号对得上才处理
    case sf_thumb_result:
    {
        std::map<int64_t, PendingOp>::iterator it = m_thumbs.find(msg.m_reqId);
        if (it == m_thumbs.end() || !it->second.m_sent)
            break;
        m_lastActivityMs = nowMs;
        const std::string path = it->second.m_req.m_path;
        m_thumbs.erase(it);
        if (m_observer != NULL)
            m_observer->OnVisitorThumbResult(msg.m_reqId, path, msg.m_code, msg.m_width, msg.m_height, msg.m_binary);
        break;
    }

    //搜索结果：先看是不是目录树请求的；请求号对得上才处理；done 为真时结束
    case sf_search_result:
    {
        if (m_treeReqId != 0 && m_treeSent && msg.m_reqId == m_treeReqId)
        {
            m_lastActivityMs = nowMs;
            m_treeDeadline = nowMs + kShareVisitorRequestTimeoutMs;
            if (msg.m_code != error_code_ok)
            {
                FinishTree(msg.m_code, false);
                break;
            }
            m_treeEntries.insert(m_treeEntries.end(), msg.m_entries.begin(), msg.m_entries.end());
            if (msg.m_done)
                FinishTree(error_code_ok, msg.m_truncated);
            break;
        }
        if (m_searchReqId == 0 || !m_searchSent || msg.m_reqId != m_searchReqId)
            break;
        m_lastActivityMs = nowMs;
        m_searchDeadline = nowMs + kShareVisitorRequestTimeoutMs;
        const int64_t reqId = m_searchReqId;
        if (msg.m_done)
        {
            m_searchReqId = 0;
            m_searchSent = false;
            LOG_INFO("share visitor search done, shareid: %lld, reqid: %lld, truncated: %d, code: %d.",
                     static_cast<long long>(m_shareId), static_cast<long long>(reqId), msg.m_truncated ? 1 : 0, msg.m_code);
        }
        if (m_observer != NULL)
            m_observer->OnVisitorSearchResult(reqId, msg.m_entries, msg.m_done, msg.m_truncated, msg.m_code);
        break;
    }

    //会话中途权限变化
    case sf_perms:
        m_perms = msg.m_perms;
        if (m_observer != NULL)
            m_observer->OnVisitorPermsChanged(m_perms);
        break;

    //分享者主动结束：原因为 0 时按「分享者已不在线」处理
    case sf_bye:
        LOG_INFO("share visitor: owner said bye, shareid: %lld, accessid: %s, errcode: %d.",
                 static_cast<long long>(m_shareId), m_accessId.c_str(), msg.m_errCode);
        Fail(msg.m_errCode != error_code_ok ? msg.m_errCode : error_code_share_unavailable, true);
        break;

    //其余帧（第 1 期不发出的请求的结果、方向不对的帧）：忽略
    default:
        break;
    }
}

void CShareVisitor::OnRelayClosed(int64_t nowMs)
{
    (void)nowMs;
    if (m_state != share_visitor_waiting_hello && m_state != share_visitor_ready && m_state != share_visitor_connecting)
        return;
    Fail(error_code_share_relay_broken, true);
}

bool CShareVisitor::OnAccessEnded(const std::string& accessId, int errCode)
{
    if (accessId.empty() || accessId != m_accessId)
        return false;
    if (m_state == share_visitor_idle || m_state == share_visitor_failed)
        return true;
    //服务端已经结束了会话，不必再发 2221；原因为 0（对端正常关闭）时按「分享者已不在线」处理
    Fail(errCode != error_code_ok ? errCode : error_code_share_unavailable, false);
    return true;
}

void CShareVisitor::OnPermsNotify(int perms)
{
    m_perms = perms;
    if (m_observer != NULL)
        m_observer->OnVisitorPermsChanged(perms);
}

void CShareVisitor::OnShareStateNotify(int state, int reason)
{
    if (m_observer != NULL)
        m_observer->OnVisitorShareState(state, reason);
    if (state != share_state_invalid || m_state == share_visitor_idle || m_state == share_visitor_failed)
        return;
    //被移出成员或已退群按「不是成员」，其余（取消、解散等）按「共享已被取消」
    const bool notMember = (reason == share_invalid_member_removed || reason == share_invalid_member_left_group);
    Fail(notMember ? error_code_share_not_member : error_code_share_canceled, false);
}

void CShareVisitor::OnTick(int64_t nowMs)
{
    switch (m_state)
    {
    //等 2220 应答或等 2222：服务端没有回应
    case share_visitor_opening:
    case share_visitor_awaiting_relay:
        if (nowMs >= m_deadline)
            Fail(error_code_server_internal, m_state == share_visitor_awaiting_relay);
        break;

    //连接中转或等 sf_hello：分享者在规定时间内没有响应
    case share_visitor_connecting:
    case share_visitor_waiting_hello:
        if (nowMs >= m_deadline)
            Fail(error_code_share_owner_timeout, true);
        break;

    //就绪：列目录或写操作的结果超时，或空闲回收
    case share_visitor_ready:
    {
        //各类请求的超时在同一轮里都检查一遍，互不影响
        if (m_listPending && m_listSent && nowMs >= m_listDeadline)
            FailPendingList(error_code_share_owner_timeout);
        std::vector<int64_t> expired;
        for (std::map<int64_t, PendingOp>::iterator it = m_ops.begin(); it != m_ops.end(); ++it)
        {
            if (it->second.m_sent && nowMs >= it->second.m_deadline)
                expired.push_back(it->first);
        }
        for (size_t i = 0; i < expired.size(); ++i)
        {
            std::map<int64_t, PendingOp>::iterator it = m_ops.find(expired[i]);
            const int type = it->second.m_req.m_type;
            m_ops.erase(it);
            LOG_WARNING("share visitor write op timed out, shareid: %lld, reqid: %lld.", static_cast<long long>(m_shareId),
                        static_cast<long long>(expired[i]));
            if (m_observer != NULL)
                m_observer->OnVisitorOpResult(expired[i], type, error_code_share_owner_timeout, std::vector<ShareOpItem>());
        }
        //搜索太久没有动静：告诉分享者停下，按 816 结束
        if (m_searchReqId != 0 && m_searchSent && nowMs >= m_searchDeadline)
        {
            SendCancel(m_searchReqId);
            LOG_WARNING("share visitor search timed out, shareid: %lld, reqid: %lld.", static_cast<long long>(m_shareId),
                        static_cast<long long>(m_searchReqId));
            FinishSearch(error_code_share_owner_timeout);
        }
        //目录树请求太久没有动静：告诉分享者停下，按 816 结束
        if (m_treeReqId != 0 && m_treeSent && nowMs >= m_treeDeadline)
        {
            SendCancel(m_treeReqId);
            FinishTree(error_code_share_owner_timeout, false);
        }
        //缩略图请求超时：按 816 回报
        {
            std::vector<int64_t> expiredThumbs;
            for (std::map<int64_t, PendingOp>::iterator it = m_thumbs.begin(); it != m_thumbs.end(); ++it)
            {
                if (it->second.m_sent && nowMs >= it->second.m_deadline)
                    expiredThumbs.push_back(it->first);
            }
            for (size_t i = 0; i < expiredThumbs.size(); ++i)
            {
                std::map<int64_t, PendingOp>::iterator it = m_thumbs.find(expiredThumbs[i]);
                const std::string path = it->second.m_req.m_path;
                m_thumbs.erase(it);
                if (m_observer != NULL)
                    m_observer->OnVisitorThumbResult(expiredThumbs[i], path, error_code_share_owner_timeout, 0, 0,
                                                     std::string());
            }
        }
        if (!m_listPending && m_ops.empty() && m_thumbs.empty() && m_searchReqId == 0 && m_treeReqId == 0
            && m_transferCount == 0
            && nowMs - m_lastActivityMs >= m_idleTimeoutMs)
        {
            LOG_INFO("share visitor idle, closing access, shareid: %lld, accessid: %s.",
                     static_cast<long long>(m_shareId), m_accessId.c_str());
            Close();
        }
        break;
    }

    //空闲或失败：没有要检查的
    default:
        break;
    }
}

void CShareVisitor::SetTransferCount(int count)
{
    m_transferCount = count;
}

void CShareVisitor::SetObserver(IShareVisitorObserver* observer)
{
    m_observer = observer;
}

int64_t CShareVisitor::GetShareId() const
{
    return m_shareId;
}

int CShareVisitor::GetState() const
{
    return m_state;
}

int CShareVisitor::GetErrorCode() const
{
    return m_errCode;
}

const std::string& CShareVisitor::GetAccessId() const
{
    return m_accessId;
}

int CShareVisitor::GetKind() const
{
    return m_kind;
}

const std::string& CShareVisitor::GetRootName() const
{
    return m_rootName;
}

void CShareVisitor::SetShareName(const std::string& name)
{
    m_shareName = name;
}

const std::string& CShareVisitor::GetShareName() const
{
    return m_shareName;
}

int CShareVisitor::GetPerms() const
{
    return m_perms;
}

void CShareVisitor::Open(int64_t nowMs)
{
    m_accessId.clear();
    m_openSeq = m_transport->SendCommand(cmd_share_open, ShareProtocolCodec::BuildShareIdRequest(m_shareId));
    if (m_openSeq == 0)
    {
        //主连接没连上：直接失败，界面提示后由用户重试
        m_state = share_visitor_opening;
        Fail(error_code_share_relay_broken, false);
        return;
    }
    m_deadline = nowMs + kShareVisitorSignalTimeoutMs;
    LOG_INFO("share visitor opening, shareid: %lld, seq: %u.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(m_openSeq));
    SetState(share_visitor_opening, 0);
}

void CShareVisitor::SendQueuedList(int64_t nowMs)
{
    m_listDeadline = nowMs + kShareVisitorRequestTimeoutMs;
    m_listSent = true;
    if (!SendListFrame(std::string()))
        FailPendingList(error_code_share_relay_broken);
}

bool CShareVisitor::SendListFrame(const std::string& cursor)
{
    ShareControlMessage req;
    //单文件共享没有目录可列：改为查共享根本身，结果按只有这一项的列表交出（带上文件的大小与修改时间）
    req.m_type = (m_kind == share_kind_file && cursor.empty()) ? sf_stat : sf_list;
    req.m_reqId = m_listReqId;
    req.m_path = m_listPath;
    req.m_cursor = cursor;
    std::string frame;
    if (!ShareControlFraming::BuildFrame(req, frame))
        return false;
    return m_transport->SendFrame(frame);
}

void CShareVisitor::Fail(int errCode, bool sendCloseAccess)
{
    if (sendCloseAccess && !m_accessId.empty())
        m_transport->SendCommand(cmd_share_close_access, ShareProtocolCodec::BuildCloseAccessRequest(m_accessId, errCode));
    m_transport->CloseRelay();
    if (m_openSeq != 0)
        m_abandonedSeq = m_openSeq;
    m_openSeq = 0;
    LOG_WARNING("share visitor failed, shareid: %lld, accessid: %s, state: %d, errcode: %d.",
                static_cast<long long>(m_shareId), m_accessId.c_str(), m_state, errCode);
    SetState(share_visitor_failed, errCode);
    FailPendingList(errCode);
    FailPendingOps(errCode);
    FailPendingThumbs(errCode);
    FinishSearch(errCode);
    FinishTree(errCode, false);
}

void CShareVisitor::SetState(int state, int errCode)
{
    m_state = state;
    m_errCode = (state == share_visitor_failed) ? errCode : 0;
    if (m_observer != NULL)
        m_observer->OnVisitorStateChanged(m_state, m_errCode);
}

void CShareVisitor::FailPendingList(int errCode)
{
    if (!m_listPending)
        return;
    const std::string path = m_listPath;
    m_listPending = false;
    m_listSent = false;
    m_listEntries.clear();
    if (m_observer != NULL)
        m_observer->OnVisitorListed(path, errCode, std::vector<ShareEntry>());
}

} // namespace fileshare
