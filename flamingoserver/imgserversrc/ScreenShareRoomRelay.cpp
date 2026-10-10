/**
 *  屏幕共享房间中转的调度，ScreenShareRoomRelay.cpp
 *  职责与加锁顺序见 ScreenShareRoomRelay.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ScreenShareRoomRelay.h"

#include <chrono>
#include <vector>
#include "../base/AsyncLog.h"
#include "../chatserversrc/Protocol.h"
#include "CloudRecorder.h"
#include "RelaySession.h"

namespace {

//系统时间，Unix 毫秒
int64_t systemNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

ScreenShareRoomRelay::ScreenShareRoomRelay()
    : m_recorder(NULL)
    , m_bufferCapBytes(RoomForwardPolicy::kDefaultViewerBufferCapBytes)
{
}

ScreenShareRoomRelay::~ScreenShareRoomRelay()
{
}

void ScreenShareRoomRelay::setViewerBufferCapBytes(size_t bytes)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_bufferCapBytes = (bytes == 0) ? RoomForwardPolicy::kDefaultViewerBufferCapBytes : bytes;
}

size_t ScreenShareRoomRelay::getViewerBufferCapBytes() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_bufferCapBytes;
}

void ScreenShareRoomRelay::setCloudRecorder(CloudRecorder* recorder)
{
    m_recorder = recorder;
}

int ScreenShareRoomRelay::startCloudRecord(uint64_t recordId, const std::string& roomId)
{
    if (m_recorder == NULL)
        return cloudrec_start_bad_arg;

    //座位归属写在录像开头。取座位与开始录制之间若恰好有人进房间，他的座位记录会缺一条，合成时那一路画面只是不显示名字
    std::vector<std::pair<int, int64_t> > seats;
    m_hub.listSeats(roomId, seats);
    const int result = m_recorder->start(recordId, roomId, recordNowMs(), false, seats);
    if (result != cloudrec_start_ok)
    {
        LOGE("screenshare relay: start cloud record failed, record: %llu, room: %s, result: %d",
             static_cast<unsigned long long>(recordId), roomId.c_str(), result);
        return result;
    }

    //录像要从关键帧开始才能解码：与新成员加入时一样，向每路已知视频的发送者请求一次关键帧
    std::vector<RoomVideoStream> requests;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<std::string, std::shared_ptr<RoomForwardPolicy>>::iterator it = m_policies.find(roomId);
        if (it != m_policies.end())
            it->second->onMemberJoined(nowMs(), requests);
    }
    for (size_t i = 0; i < requests.size(); ++i)
        requestKeyFrame(roomId, requests[i].m_senderSeat, requests[i].m_track);
    LOGI("screenshare relay: cloud record started, record: %llu, room: %s, seats: %d, key frame requests: %d",
         static_cast<unsigned long long>(recordId), roomId.c_str(), (int)seats.size(), (int)requests.size());
    return result;
}

int64_t ScreenShareRoomRelay::recordNowMs()
{
    //第一次调用时记下系统时间与单调时钟之差，之后一律按单调时钟推算（C++11 起函数内静态变量的初始化是线程安全的）
    static const int64_t kOffsetMs = systemNowMs() - nowMs();
    return nowMs() + kOffsetMs;
}

bool ScreenShareRoomRelay::join(const std::string& roomId, int seat, int role,
                                const std::shared_ptr<RelaySession>& session, int64_t userId)
{
    if (!m_hub.join(roomId, seat, role, session, userId))
        return false;
    //房间正在录时写一条座位记录（没在录时录制器什么也不做）
    if (m_recorder != NULL)
        m_recorder->onSeat(roomId, seat, userId, recordNowMs());

    std::vector<RoomVideoStream> requests;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        policyLocked(roomId)->onMemberJoined(nowMs(), requests);
    }
    for (size_t i = 0; i < requests.size(); ++i)
        requestKeyFrame(roomId, requests[i].m_senderSeat, requests[i].m_track);
    return true;
}

void ScreenShareRoomRelay::forward(const std::string& roomId, const std::shared_ptr<net::TcpConnection>& self,
                                   const RoomRelayFrameHeader& header, uint16_t cmd, uint32_t seq, uint8_t srcFlags,
                                   uint32_t originlen, const std::string& plainBody)
{
    std::vector<RoomHub::MemberRef> others;
    int64_t senderUserId = 0;
    if (!m_hub.getOthers(roomId, self, others, &senderUserId))
        return;
    //房间正在录时另交一份给录制器；录制器只入队，写盘在它自己的线程上
    if (m_recorder != NULL)
        m_recorder->onFrame(roomId, header.m_track, plainBody, recordNowMs());

    //在锁内为每个接收者做决定，在锁外发送
    std::vector<std::shared_ptr<RelaySession>> targets;
    bool needKeyFrame = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::shared_ptr<RoomForwardPolicy> policy = policyLocked(roomId);
        //不论有没有接收者都要登记：发起人独自在房间时发的画面也算已知视频流，第一个观看者加入时才会请求关键帧
        policy->noteFrame(header);
        const int64_t now = nowMs();
        for (size_t i = 0; i < others.size(); ++i)
        {
            //只推流的连接（第 22 期）不接收房间里的转发，只接收发给它的控制帧（关键帧请求、键鼠输入走 forwardTo）；
            //发送者本人的另一条连接（共享者自己的观看连接）也不回送
            if (!RoomRelayFrame::shouldForwardTo(others[i].m_role, others[i].m_userId, senderUserId))
                continue;
            bool request = false;
            const int decision = policy->decide(others[i].m_seat, header, others[i].m_session->pendingBytes(), now,
                                                request);
            if (decision == room_forward_send)
                targets.push_back(others[i].m_session);
            if (request)
                needKeyFrame = true;
        }
    }

    for (size_t i = 0; i < targets.size(); ++i)
        targets[i]->forwardFrame(cmd, seq, srcFlags, originlen, plainBody);

    if (needKeyFrame)
    {
        LOGI("screenshare relay: viewer backlog over limit, request key frame, room: %s, sender seat: %d, track: %d",
             roomId.c_str(), (int)header.m_senderSeat, (int)header.m_track);
        requestKeyFrame(roomId, header.m_senderSeat, header.m_track);
    }
}

bool ScreenShareRoomRelay::leave(const std::shared_ptr<net::TcpConnection>& conn, std::string* outRoomId,
                                 int* outSeat)
{
    std::string roomId;
    int seat = -1;
    if (!m_hub.leaveByConn(conn, &roomId, &seat))
        return false;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<std::string, std::shared_ptr<RoomForwardPolicy>>::iterator it = m_policies.find(roomId);
        if (it != m_policies.end())
        {
            it->second->onMemberLeft(seat);
            //房间摘空：连同策略一起删除
            if (m_hub.memberCount(roomId) == 0)
                m_policies.erase(it);
        }
    }

    if (outRoomId != NULL)
        *outRoomId = roomId;
    if (outSeat != NULL)
        *outSeat = seat;
    return true;
}

void ScreenShareRoomRelay::setSubscription(const std::string& roomId, int receiverSeat, const std::vector<int>& seats)
{
    std::vector<RoomVideoStream> requests;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        policyLocked(roomId)->setSubscription(receiverSeat, seats, nowMs(), requests);
    }
    for (size_t i = 0; i < requests.size(); ++i)
        requestKeyFrame(roomId, requests[i].m_senderSeat, requests[i].m_track);
}

bool ScreenShareRoomRelay::forwardTo(const std::string& roomId, int senderSeat, int targetSeat, uint16_t cmd,
                                     uint32_t seq, uint8_t srcFlags, uint32_t originlen, const std::string& plainBody)
{
    if (targetSeat == senderSeat)
        return false;
    std::shared_ptr<RelaySession> target = m_hub.getMember(roomId, targetSeat);
    if (!target)
        return false;
    target->forwardFrame(cmd, seq, srcFlags, originlen, plainBody);
    return true;
}

int64_t ScreenShareRoomRelay::nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::shared_ptr<RoomForwardPolicy> ScreenShareRoomRelay::policyLocked(const std::string& roomId)
{
    std::shared_ptr<RoomForwardPolicy>& policy = m_policies[roomId];
    if (!policy)
        policy.reset(new RoomForwardPolicy(m_bufferCapBytes));
    return policy;
}

void ScreenShareRoomRelay::requestKeyFrame(const std::string& roomId, int senderSeat, int videoTrack)
{
    std::shared_ptr<RelaySession> sender = m_hub.getMember(roomId, senderSeat);
    if (!sender)
        return;
    //控制帧由服务端构造，不压缩；originlen 即包体长度
    const std::string body = RoomRelayFrame::buildKeyFrameRequest(videoTrack);
    sender->forwardFrame(cmd_screenshare_relay_data, 0, 0, static_cast<uint32_t>(body.size()), body);
}
