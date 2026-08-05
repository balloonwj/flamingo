/**
 *  单聊「收发文件」串行队列的取消簿记实现，ChatTransferCancelRegistry.cpp
 *  balloonwj@qq.com   2026-08-03
 */
#include "ChatTransferCancelRegistry.h"

namespace net {

ChatTransferCancelRegistry::ChatTransferCancelRegistry()
{
}

std::shared_ptr<std::atomic<bool> > ChatTransferCancelRegistry::RegisterSend(
    const std::string& clientMsgId)
{
    //空 ID 无法与任何气泡对回，登记了也取消不了，直接拒绝
    if (clientMsgId.empty())
        return std::shared_ptr<std::atomic<bool> >();

    //已在册：沿用原标志。若此前已被置位（先取消、后又被重复入队），保持置位状态，
    //让 worker 取出时仍能判定为已取消。
    std::map<std::string, std::shared_ptr<std::atomic<bool> > >::iterator it =
        m_sends.find(clientMsgId);
    if (it != m_sends.end())
        return it->second;

    std::shared_ptr<std::atomic<bool> > flag(new std::atomic<bool>(false));
    m_sends[clientMsgId] = flag;
    return flag;
}

std::shared_ptr<std::atomic<bool> > ChatTransferCancelRegistry::RegisterDownload(int64_t msgid)
{
    //msgid 是服务器分配的正整数，非正值无法与气泡对回，直接拒绝
    if (msgid <= 0)
        return std::shared_ptr<std::atomic<bool> >();

    std::map<int64_t, std::shared_ptr<std::atomic<bool> > >::iterator it =
        m_downloads.find(msgid);
    if (it != m_downloads.end())
        return it->second;

    std::shared_ptr<std::atomic<bool> > flag(new std::atomic<bool>(false));
    m_downloads[msgid] = flag;
    return flag;
}

bool ChatTransferCancelRegistry::CancelSend(const std::string& clientMsgId)
{
    std::map<std::string, std::shared_ptr<std::atomic<bool> > >::iterator it =
        m_sends.find(clientMsgId);
    if (it == m_sends.end())
        return false;

    //重复取消是幂等的：再置一次 true 无副作用
    it->second->store(true);
    return true;
}

bool ChatTransferCancelRegistry::CancelDownload(int64_t msgid)
{
    std::map<int64_t, std::shared_ptr<std::atomic<bool> > >::iterator it =
        m_downloads.find(msgid);
    if (it == m_downloads.end())
        return false;

    it->second->store(true);
    return true;
}

bool ChatTransferCancelRegistry::IsSendCancelled(const std::string& clientMsgId) const
{
    std::map<std::string, std::shared_ptr<std::atomic<bool> > >::const_iterator it =
        m_sends.find(clientMsgId);
    if (it == m_sends.end())
        return false;

    return it->second->load();
}

bool ChatTransferCancelRegistry::IsDownloadCancelled(int64_t msgid) const
{
    std::map<int64_t, std::shared_ptr<std::atomic<bool> > >::const_iterator it =
        m_downloads.find(msgid);
    if (it == m_downloads.end())
        return false;

    return it->second->load();
}

void ChatTransferCancelRegistry::CompleteSend(const std::string& clientMsgId)
{
    //只从表里摘掉；worker 手上那份 shared_ptr 让标志对象继续存活，读取仍安全
    m_sends.erase(clientMsgId);
}

void ChatTransferCancelRegistry::CompleteDownload(int64_t msgid)
{
    m_downloads.erase(msgid);
}

int ChatTransferCancelRegistry::SendCount() const
{
    return static_cast<int>(m_sends.size());
}

int ChatTransferCancelRegistry::DownloadCount() const
{
    return static_cast<int>(m_downloads.size());
}

} // namespace net
