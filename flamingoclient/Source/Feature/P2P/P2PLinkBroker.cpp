/**
 *  业务方共用的 P2P 链路层的实现，见 P2PLinkBroker.h。
 *  第 6 期从 FolderP2PTransport.cpp 抽出：链路槽、业务方与帧通道的逻辑原样搬来，只把文件夹专用的名字换成通用的。
 *  balloonwj@qq.com   2026-10-04
 */
//本文件不使用预编译头：主程序、文件共享 DLL 与媒体 DLL 都直接编译它，三者的预编译头各不相同
#include "P2PLinkBroker.h"

#include <chrono>

#include "../../Common/Log/IULog.h"
#include "../../net/Protocol.h"   //p2p_close_business_end

namespace balloonp2p {

namespace {

//一个字节的位数，长度字段按网络字节序拼装
const int kBitsPerByte = 8;

//取一个字节的掩码
const uint32_t kByteMask = 0xFF;

//等可写通知时每次等待的时长（毫秒）：期间检查关闭与断开
const int kSendWaitSliceMs = 100;

//按网络字节序写 / 读 4 字节长度
void PutLength(uint32_t len, std::string& out)
{
    out.push_back((char)((len >> (3 * kBitsPerByte)) & kByteMask));
    out.push_back((char)((len >> (2 * kBitsPerByte)) & kByteMask));
    out.push_back((char)((len >> kBitsPerByte) & kByteMask));
    out.push_back((char)(len & kByteMask));
}

uint32_t GetLength(const unsigned char* p)
{
    return ((uint32_t)p[0] << (3 * kBitsPerByte)) | ((uint32_t)p[1] << (2 * kBitsPerByte))
         | ((uint32_t)p[2] << kBitsPerByte) | (uint32_t)p[3];
}

} // namespace

int64_t P2PSteadyNowMs()
{
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ======================== P2PStreamChannel ========================

P2PStreamChannel::P2PStreamChannel(const std::shared_ptr<P2PLinkSlot>& slot, size_t maxFrameBytes, int sendWaitMs)
    : m_slot(slot)
    , m_maxFrameBytes(maxFrameBytes)
    , m_sendWaitMs(sendWaitMs)
    , m_channelId(0)
    , m_registered(false)
    , m_handler(NULL)
    , m_open(false)
    , m_closing(false)
    , m_broken(false)
    , m_writableSeq(0)
    , m_detached(false)
{
}

P2PStreamChannel::~P2PStreamChannel()
{
    Close();
}

bool P2PStreamChannel::Open(uint16_t channelId, IP2PStreamHandler* handler)
{
    if (handler == NULL || m_registered || m_closing)
        return false;
    m_channelId = channelId;
    m_handler = handler;
    if (!m_slot->AddChannel(this, channelId, p2p::p2p_channel_reliable))
    {
        LOG_WARNING("P2P stream: open channel failed, session=%s, channel=%u.", m_slot->FeatureSessionId().c_str(),
                    (unsigned)channelId);
        return false;
    }
    m_registered = true;
    m_open = true;
    m_thread = std::thread(&P2PStreamChannel::DeliverProc, this);
    LOG_INFO("P2P stream ready, session=%s, channel=%u.", m_slot->FeatureSessionId().c_str(), (unsigned)channelId);
    return true;
}

bool P2PStreamChannel::SendFrame(const unsigned char* data, size_t len)
{
    if (!m_open || m_broken || (data == NULL && len > 0) || len > m_maxFrameBytes)
        return false;
    p2p::IP2PLink* link = m_slot->Link();
    if (link == NULL)
        return false;
    std::string buf;
    buf.reserve(kP2PStreamFrameHeaderBytes + len);
    PutLength((uint32_t)len, buf);
    if (len > 0)
        buf.append(reinterpret_cast<const char*>(data), len);
    //同一通道上可能有多个线程发帧（如工作线程发数据、别的线程发中止），一帧要整体交出，不能与别的帧交错
    std::lock_guard<std::mutex> sendGuard(m_sendMutex);
    const int64_t deadline = P2PSteadyNowMs() + m_sendWaitMs;
    while (true)
    {
        const uint64_t seqBefore = m_writableSeq;
        const int r = link->Send(m_channelId, reinterpret_cast<const uint8_t*>(buf.data()), (uint32_t)buf.size());
        if (r == p2p::p2p_send_ok)
            return true;
        if (r != p2p::p2p_send_would_block)
        {
            LOG_WARNING("P2P stream: send failed, session=%s, channel=%u, result=%d.",
                        m_slot->FeatureSessionId().c_str(), (unsigned)m_channelId, r);
            return false;
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_writableSeq == seqBefore && !m_closing && !m_broken)
        {
            if (P2PSteadyNowMs() >= deadline)
            {
                LOG_WARNING("P2P stream: no writable notice in time, session=%s, channel=%u.",
                            m_slot->FeatureSessionId().c_str(), (unsigned)m_channelId);
                return false;
            }
            m_cv.wait_for(lock, std::chrono::milliseconds(kSendWaitSliceMs));
        }
        if (m_closing || m_broken)
            return false;
    }
}

void P2PStreamChannel::Close()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_closing)
            return;
        m_closing = true;
        m_open = false;
    }
    m_cv.notify_all();
    if (m_registered)
        m_slot->RemoveChannel(m_channelId);
    if (m_thread.joinable())
    {
        //在转交线程自己（业务的回调里）关闭时不能等自己：分离即可，线程退出时也不再回调业务
        if (std::this_thread::get_id() == m_thread.get_id())
        {
            m_detached = true;
            m_thread.detach();
        }
        else
        {
            m_thread.join();
        }
    }
}

bool P2PStreamChannel::IsOpen() const
{
    return m_open && !m_broken;
}

void P2PStreamChannel::OnChannelData(const uint8_t* data, uint32_t len)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_closing || m_broken)
        return;
    m_inBuf.append(reinterpret_cast<const char*>(data), len);
    size_t offset = 0;
    while (m_inBuf.size() - offset >= kP2PStreamFrameHeaderBytes)
    {
        const uint32_t frameLen = GetLength(reinterpret_cast<const unsigned char*>(m_inBuf.data()) + offset);
        //长度不合理：字节流已经错位，只能按通道断开处理
        if (frameLen > m_maxFrameBytes)
        {
            LOG_ERROR("P2P stream: bad frame length %u, session=%s, channel=%u.", frameLen,
                      m_slot->FeatureSessionId().c_str(), (unsigned)m_channelId);
            m_broken = true;
            break;
        }
        if (m_inBuf.size() - offset - kP2PStreamFrameHeaderBytes < frameLen)
            break;
        m_frames.push_back(m_inBuf.substr(offset + kP2PStreamFrameHeaderBytes, frameLen));
        offset += kP2PStreamFrameHeaderBytes + frameLen;
    }
    m_inBuf.erase(0, offset);
    m_cv.notify_all();
}

void P2PStreamChannel::OnChannelWritable()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_writableSeq;
    }
    m_cv.notify_all();
}

void P2PStreamChannel::OnLinkBroken()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_broken = true;
    }
    m_cv.notify_all();
}

void P2PStreamChannel::DeliverProc()
{
    m_handler->OnStreamThreadStart();
    DeliverLoop();
    if (!m_detached)
        m_handler->OnStreamThreadExit();
}

void P2PStreamChannel::DeliverLoop()
{
    while (true)
    {
        std::string frame;
        bool reportClosed = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (m_frames.empty() && !m_closing && !m_broken)
                m_cv.wait(lock);
            if (m_closing)
                return;
            if (!m_frames.empty())
            {
                frame.swap(m_frames.front());
                m_frames.pop_front();
            }
            else
            {
                reportClosed = true;
            }
        }
        if (reportClosed)
        {
            LOG_WARNING("P2P stream: link broken, session=%s, channel=%u.", m_slot->FeatureSessionId().c_str(),
                        (unsigned)m_channelId);
            m_handler->OnStreamClosed();
            return;
        }
        m_handler->OnStreamFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size());
    }
}

// ======================== P2PDatagramChannel ========================

P2PDatagramChannel::P2PDatagramChannel(const std::shared_ptr<P2PLinkSlot>& slot, size_t maxQueuedMessages)
    : m_slot(slot)
    , m_maxQueued(maxQueuedMessages > 0 ? maxQueuedMessages : 1)
    , m_channelId(0)
    , m_registered(false)
    , m_handler(NULL)
    , m_open(false)
    , m_closing(false)
    , m_broken(false)
    , m_dropped(0)
{
}

P2PDatagramChannel::~P2PDatagramChannel()
{
    Close();
}

bool P2PDatagramChannel::Open(uint16_t channelId, IP2PDatagramHandler* handler)
{
    if (handler == NULL || m_registered || m_closing)
        return false;
    m_channelId = channelId;
    m_handler = handler;
    if (!m_slot->AddChannel(this, channelId, p2p::p2p_channel_datagram))
    {
        LOG_WARNING("P2P datagram: open channel failed, session=%s, channel=%u.", m_slot->FeatureSessionId().c_str(),
                    (unsigned)channelId);
        return false;
    }
    m_registered = true;
    m_open = true;
    m_thread = std::thread(&P2PDatagramChannel::DeliverProc, this);
    LOG_INFO("P2P datagram ready, session=%s, channel=%u.", m_slot->FeatureSessionId().c_str(), (unsigned)channelId);
    return true;
}

int P2PDatagramChannel::Send(const unsigned char* data, size_t len)
{
    if (!m_open || m_broken || (data == NULL && len > 0))
        return p2p::p2p_send_closed;
    p2p::IP2PLink* link = m_slot->Link();
    if (link == NULL)
        return p2p::p2p_send_closed;
    return link->Send(m_channelId, reinterpret_cast<const uint8_t*>(data), (uint32_t)len);
}

void P2PDatagramChannel::Close()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_closing)
            return;
        m_closing = true;
        m_open = false;
    }
    m_cv.notify_all();
    if (m_registered)
        m_slot->RemoveChannel(m_channelId);
    if (m_thread.joinable())
    {
        //在转交线程自己（业务的回调里）关闭时不能等自己：分离即可
        if (std::this_thread::get_id() == m_thread.get_id())
            m_thread.detach();
        else
            m_thread.join();
    }
}

bool P2PDatagramChannel::IsOpen() const
{
    return m_open && !m_broken;
}

void P2PDatagramChannel::OnChannelData(const uint8_t* data, uint32_t len)
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_closing || m_broken)
            return;
        //实时媒体宁可丢旧的：转交线程跟不上时先丢最旧的一条
        while (m_messages.size() >= m_maxQueued)
        {
            m_messages.pop_front();
            ++m_dropped;
        }
        m_messages.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }
    m_cv.notify_all();
}

void P2PDatagramChannel::OnChannelWritable()
{
}

void P2PDatagramChannel::OnLinkBroken()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_broken = true;
    }
    m_cv.notify_all();
}

void P2PDatagramChannel::DeliverProc()
{
    while (true)
    {
        std::string message;
        bool reportClosed = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (m_messages.empty() && !m_closing && !m_broken)
                m_cv.wait(lock);
            if (m_closing)
                return;
            if (!m_messages.empty())
            {
                message.swap(m_messages.front());
                m_messages.pop_front();
            }
            else
            {
                reportClosed = true;
            }
        }
        if (reportClosed)
        {
            LOG_WARNING("P2P datagram: link broken, session=%s, channel=%u, dropped=%llu.",
                        m_slot->FeatureSessionId().c_str(), (unsigned)m_channelId,
                        (unsigned long long)m_dropped.load());
            m_handler->OnDatagramClosed();
            return;
        }
        m_handler->OnDatagram(reinterpret_cast<const unsigned char*>(message.data()), message.size());
    }
}

// ======================== P2PLinkSlot ========================

P2PLinkSlot::P2PLinkSlot(const std::string& featureSessionId, int64_t createdMs)
    : m_featureSessionId(featureSessionId)
    , m_createdMs(createdMs)
    , m_state(slot_waiting)
    , m_link(NULL)
    , m_claimed(false)
    , m_linkClosed(false)
{
}

P2PLinkSlot::~P2PLinkSlot()
{
    p2p::IP2PLink* link = NULL;
    bool closed = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        link = m_link;
        m_link = NULL;
        closed = m_linkClosed;
    }
    if (link != NULL)
    {
        //业务结束：通知对方并结束 P2P 会话；Close 返回之后 DLL 不再回调本对象
        if (!closed)
        {
            LOG_INFO("P2P link closed by business, session=%s.", m_featureSessionId.c_str());
            link->Close(p2p_close_business_end);
        }
        link->Release();
    }
}

void P2PLinkSlot::CloseLink()
{
    p2p::IP2PLink* link = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_link == NULL || m_linkClosed)
            return;
        m_linkClosed = true;
        m_state = slot_closed;
        link = m_link;
    }
    //在锁外关闭：Close 要等 IO 线程上正在进行的回调结束，而回调转发数据时持有本对象的互斥量
    LOG_INFO("P2P link closed by business, session=%s.", m_featureSessionId.c_str());
    link->Close(p2p_close_business_end);
    m_cv.notify_all();
}

void P2PLinkSlot::SetReady(p2p::IP2PLink* link)
{
    p2p::IP2PLink* old = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        link->AddRef();
        old = m_link;
        m_link = link;
        m_state = slot_ready;
    }
    if (old != NULL)
        old->Release();
    m_cv.notify_all();
}

void P2PLinkSlot::SetFailed(int code)
{
    (void)code;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_state == slot_waiting)
            m_state = slot_failed;
    }
    m_cv.notify_all();
}

void P2PLinkSlot::SetClosed(int reason)
{
    (void)reason;
    std::lock_guard<std::mutex> guard(m_mutex);
    m_state = slot_closed;
    for (std::map<uint16_t, IP2PSlotChannel*>::iterator it = m_channels.begin(); it != m_channels.end(); ++it)
        it->second->OnLinkBroken();
    m_cv.notify_all();
}

int P2PLinkSlot::Wait(int timeoutMs)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_state == slot_waiting)
    {
        if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }
    return m_state;
}

int P2PLinkSlot::State() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_state;
}

int LinkPathWhenUsing(bool usingP2P, const std::shared_ptr<P2PLinkSlot>& slot)
{
    //业务走着中转时链路即便建好了也不算：数据实际没走它
    if (!usingP2P || !slot)
        return 0;
    return slot->LinkPath();
}

int P2PLinkSlot::LinkPath() const
{
    //持锁只取出链路并加一个引用，读统计放到锁外：GetStats 会进 DLL，不在本槽的锁里调用别的模块
    p2p::IP2PLink* link = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_state != slot_ready || m_link == NULL)
            return 0;
        link = m_link;
        link->AddRef();
    }

    p2p::P2PLinkStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    link->GetStats(&stats);
    link->Release();
    return stats.m_path;
}

p2p::IP2PLink* P2PLinkSlot::Link() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return (m_state == slot_ready) ? m_link : NULL;
}

bool P2PLinkSlot::AddChannel(IP2PSlotChannel* channel, uint16_t channelId, int kind)
{
    p2p::IP2PLink* link = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_state != slot_ready || m_link == NULL || m_channels.count(channelId) != 0)
            return false;
        m_channels[channelId] = channel;
        link = m_link;
    }
    //在锁外打开通道：OpenChannel 要取 DLL 会话的互斥量，IO 线程在回调本对象时也持有本对象的互斥量
    if (link->OpenChannel(channelId, kind, this) != p2p::p2p_send_ok)
    {
        RemoveChannel(channelId);
        return false;
    }
    return true;
}

void P2PLinkSlot::RemoveChannel(uint16_t channelId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_channels.erase(channelId);
}

void P2PLinkSlot::OnData(uint16_t channelId, const uint8_t* data, uint32_t len)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<uint16_t, IP2PSlotChannel*>::iterator it = m_channels.find(channelId);
    if (it != m_channels.end())
        it->second->OnChannelData(data, len);
}

void P2PLinkSlot::OnWritable(uint16_t channelId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<uint16_t, IP2PSlotChannel*>::iterator it = m_channels.find(channelId);
    if (it != m_channels.end())
        it->second->OnChannelWritable();
}

bool P2PLinkSlot::IsClaimed() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_claimed;
}

void P2PLinkSlot::MarkClaimed()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_claimed = true;
}

// ======================== P2PLinkBroker ========================

P2PLinkBroker::P2PLinkBroker(const std::string& purpose)
    : m_purpose(purpose)
    , m_service(NULL)
{
}

P2PLinkBroker::~P2PLinkBroker()
{
}

bool P2PLinkBroker::Register(p2p::IP2PService* service)
{
    if (service == NULL)
        return false;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_service != NULL)
        return true;
    if (!service->RegisterHandler(m_purpose.c_str(), this))
    {
        LOG_WARNING("P2P link broker: register handler failed, purpose: %s.", m_purpose.c_str());
        return false;
    }
    m_service = service;
    LOG_INFO("P2P link broker registered, purpose: %s.", m_purpose.c_str());
    return true;
}

void P2PLinkBroker::Unregister()
{
    std::map<std::string, std::shared_ptr<P2PLinkSlot> > unclaimed;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_service == NULL)
            return;
        m_service->UnregisterHandler(m_purpose.c_str());
        m_service = NULL;
        unclaimed.swap(m_unclaimed);
        m_claimed.clear();
        m_abandoned.clear();
    }
    //未被认领的槽在锁外析构（会关闭链路）
    unclaimed.clear();
    LOG_INFO("P2P link broker unregistered, purpose: %s.", m_purpose.c_str());
}

bool P2PLinkBroker::IsRegistered() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_service != NULL;
}

std::shared_ptr<P2PLinkSlot> P2PLinkBroker::ClaimSlot(const std::string& featureSessionId)
{
    std::vector<std::shared_ptr<P2PLinkSlot> > expired;   //声明在守卫之前：在互斥量释放之后才析构
    std::shared_ptr<P2PLinkSlot> slot;
    std::shared_ptr<P2PLinkSlot> replaced;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        PurgeLocked(P2PSteadyNowMs(), expired);
        //重新认领：不再算放弃（同一功能会话号重试时）
        m_abandoned.erase(featureSessionId);
        //链路先到：认领那个还没被认领的槽
        std::map<std::string, std::shared_ptr<P2PLinkSlot> >::iterator it = m_unclaimed.find(featureSessionId);
        if (it != m_unclaimed.end())
        {
            slot = it->second;
            m_unclaimed.erase(it);
        }
        else
        {
            //上一轮（重试之前）认领过的旧槽被替换：它的链路由上一轮的使用者释放时关闭
            std::map<std::string, std::weak_ptr<P2PLinkSlot> >::iterator old = m_claimed.find(featureSessionId);
            if (old != m_claimed.end())
                replaced = old->second.lock();
            slot.reset(new P2PLinkSlot(featureSessionId, P2PSteadyNowMs()));
        }
        slot->MarkClaimed();
        m_claimed[featureSessionId] = slot;
    }
    LOG_INFO("P2P link slot claimed, purpose=%s, session=%s, link already ready=%d, replaced old slot=%d.",
             m_purpose.c_str(), featureSessionId.c_str(), slot->State() == P2PLinkSlot::slot_ready ? 1 : 0,
             replaced ? 1 : 0);
    return slot;
}

void P2PLinkBroker::Abandon(const std::string& featureSessionId)
{
    std::vector<std::shared_ptr<P2PLinkSlot> > expired;   //声明在守卫之前：在互斥量释放之后才析构
    std::shared_ptr<P2PLinkSlot> claimed;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        PurgeLocked(P2PSteadyNowMs(), expired);
        m_abandoned[featureSessionId] = P2PSteadyNowMs();
        //没被认领的槽（链路先到了）随之释放，析构时关闭链路
        std::map<std::string, std::shared_ptr<P2PLinkSlot> >::iterator it = m_unclaimed.find(featureSessionId);
        if (it != m_unclaimed.end())
        {
            expired.push_back(it->second);
            m_unclaimed.erase(it);
        }
        std::map<std::string, std::weak_ptr<P2PLinkSlot> >::iterator c = m_claimed.find(featureSessionId);
        if (c != m_claimed.end())
        {
            claimed = c->second.lock();
            m_claimed.erase(c);
        }
    }
    //已认领的槽还被业务持有：链路就绪的话立即关闭
    if (claimed)
        claimed->CloseLink();
    LOG_INFO("P2P link abandoned by business, purpose=%s, session=%s.", m_purpose.c_str(), featureSessionId.c_str());
}

void P2PLinkBroker::OnLinkReady(const char* featureSessionId, p2p::IP2PLink* link)
{
    std::vector<std::shared_ptr<P2PLinkSlot> > expired;
    std::shared_ptr<P2PLinkSlot> slot;
    bool abandoned = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        const std::string id = (featureSessionId != NULL) ? featureSessionId : "";
        std::map<std::string, int64_t>::iterator a = m_abandoned.find(id);
        if (a != m_abandoned.end())
        {
            m_abandoned.erase(a);
            abandoned = true;
        }
        else
        {
            slot = SlotLocked(id, P2PLinkSlot::slot_waiting, true, expired);
        }
    }
    //业务已放弃这个会话号：链路直接关闭，不进链路槽（在回调里调用 Close 立即返回）
    if (abandoned)
    {
        LOG_INFO("P2P link ready after the business abandoned it, closing, purpose=%s, session=%s.", m_purpose.c_str(),
                 featureSessionId);
        link->Close(p2p_close_business_end);
        return;
    }
    LOG_INFO("P2P link ready, purpose=%s, session=%s, claimed=%d.", m_purpose.c_str(), slot->FeatureSessionId().c_str(),
             slot->IsClaimed() ? 1 : 0);
    slot->SetReady(link);
}

void P2PLinkBroker::OnLinkFailed(const char* featureSessionId, int code)
{
    std::vector<std::shared_ptr<P2PLinkSlot> > expired;
    std::shared_ptr<P2PLinkSlot> slot;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        const std::string id = (featureSessionId != NULL) ? featureSessionId : "";
        //业务已放弃这个会话号：结果没人要了，只清掉记录
        if (m_abandoned.erase(id) > 0)
            return;
        slot = SlotLocked(id, P2PLinkSlot::slot_waiting, true, expired);
    }
    LOG_INFO("P2P link failed, purpose=%s, session=%s, code=%d.", m_purpose.c_str(), slot->FeatureSessionId().c_str(),
             code);
    slot->SetFailed(code);
}

void P2PLinkBroker::OnLinkClosed(const char* featureSessionId, int reason)
{
    const std::string id = (featureSessionId != NULL) ? featureSessionId : "";
    std::vector<std::shared_ptr<P2PLinkSlot> > expired;
    std::shared_ptr<P2PLinkSlot> slot;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        slot = SlotLocked(id, P2PLinkSlot::slot_ready, false, expired);
    }
    LOG_INFO("P2P link closed, purpose=%s, session=%s, reason=%d.", m_purpose.c_str(), id.c_str(), reason);
    if (slot)
        slot->SetClosed(reason);
}

std::shared_ptr<P2PLinkSlot> P2PLinkBroker::SlotLocked(const std::string& featureSessionId, int wantState, bool create,
                                                       std::vector<std::shared_ptr<P2PLinkSlot> >& expired)
{
    //只交给处在 wantState 的槽：建链结果交给还在等的槽，断开交给已就绪的槽。同一业务重试时功能会话号可能不变，
    //上一轮的槽可能还没释放，它已经处在终态，不能把新链路的结果交给它
    std::map<std::string, std::weak_ptr<P2PLinkSlot> >::iterator claimed = m_claimed.find(featureSessionId);
    if (claimed != m_claimed.end())
    {
        std::shared_ptr<P2PLinkSlot> slot = claimed->second.lock();
        if (slot && slot->State() == wantState)
            return slot;
        if (!slot)
            m_claimed.erase(claimed);
    }
    std::map<std::string, std::shared_ptr<P2PLinkSlot> >::iterator it = m_unclaimed.find(featureSessionId);
    if (it != m_unclaimed.end() && it->second->State() == wantState)
        return it->second;
    if (!create)
        return std::shared_ptr<P2PLinkSlot>();
    //没有合适的槽：新建一个未认领的（同号的旧未认领槽被替换，同样交给调用方在锁外释放）
    PurgeLocked(P2PSteadyNowMs(), expired);
    std::shared_ptr<P2PLinkSlot> slot(new P2PLinkSlot(featureSessionId, P2PSteadyNowMs()));
    std::map<std::string, std::shared_ptr<P2PLinkSlot> >::iterator old = m_unclaimed.find(featureSessionId);
    if (old != m_unclaimed.end())
        expired.push_back(old->second);
    m_unclaimed[featureSessionId] = slot;
    return slot;
}

void P2PLinkBroker::PurgeLocked(int64_t nowMs, std::vector<std::shared_ptr<P2PLinkSlot> >& expired)
{
    for (std::map<std::string, std::shared_ptr<P2PLinkSlot> >::iterator it = m_unclaimed.begin();
         it != m_unclaimed.end();)
    {
        if (nowMs - it->second->CreatedMs() >= kP2PUnclaimedLinkTtlMs)
        {
            LOG_INFO("P2P unclaimed link expired, purpose=%s, session=%s.", m_purpose.c_str(), it->first.c_str());
            expired.push_back(it->second);
            it = m_unclaimed.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (std::map<std::string, std::weak_ptr<P2PLinkSlot> >::iterator it = m_claimed.begin(); it != m_claimed.end();)
    {
        if (it->second.expired())
            it = m_claimed.erase(it);
        else
            ++it;
    }
    for (std::map<std::string, int64_t>::iterator it = m_abandoned.begin(); it != m_abandoned.end();)
    {
        if (nowMs - it->second >= kP2PUnclaimedLinkTtlMs)
            it = m_abandoned.erase(it);
        else
            ++it;
    }
}

} // namespace balloonp2p
