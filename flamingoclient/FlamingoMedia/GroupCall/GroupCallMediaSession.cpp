/**
 *  群通话的媒体会话，GroupCallMediaSession.cpp
 *  流程与线程约定见 GroupCallMediaSession.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupCallMediaSession.h"
#include "GroupCallMessages.h"

#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"

namespace groupcall {

namespace {

//服务端公钥的字节数
const size_t kServerPubKeyBytes = 32;

//订阅帧一次最多带的座位数（座位数字段只有 1 字节）
const size_t kMaxSubscribeSeats = 254;

} // namespace

GroupCallMediaSession::GroupCallMediaSession()
    : m_notifyWnd(NULL)
    , m_token(0)
    , m_deviceMode(gc_devices_all)
    , m_running(false)
    , m_stopping(false)
    , m_hasSubscribe(false)
    , m_subscribeSent(false)
{
}

GroupCallMediaSession::~GroupCallMediaSession()
{
    Stop();
}

bool GroupCallMediaSession::Start(const GroupCallRelayInfo& relay, const unsigned char* serverPubKey, bool micOn,
                                  bool cameraOn, HWND notifyWnd, LPARAM token, int deviceMode)
{
    if (notifyWnd == NULL || relay.m_token.empty() || relay.m_relayPort <= 0)
        return false;
    Stop();

    m_relayInfo = relay;
    m_serverPubKey.clear();
    if (serverPubKey != NULL)
        m_serverPubKey.assign(serverPubKey, serverPubKey + kServerPubKeyBytes);
    m_notifyWnd = notifyWnd;
    m_token = token;
    m_deviceMode = deviceMode;
    m_stopping.store(false);
    //上一次会话留下的未完成分片不能带进这一次（座位可能已换了人）
    m_reassembler = screenshare::ShareFrameReassembler();
    {
        std::lock_guard<std::mutex> guard(m_subscribeMutex);
        m_subscribeSeats.clear();
        m_hasSubscribe = false;
        m_subscribeSent = false;
    }

    //声音与摄像头先起来：中转没连上之前 SendRoomBody 返回 false，编出的帧直接丢掉。
    //麦克风轨打开 DTX：多数人多数时候不说话，静音时不发帧，省掉上行与服务端向其余成员的转发
    m_audio.SetDtx(true);
    if (!m_audio.Start(this, relay.m_seat, micOn, deviceMode != gc_devices_all))
        LOG_WARNING("groupcall media: audio devices unavailable, room=%s.", relay.m_roomId.c_str());
    m_cameraReceiver.Start();
    if (cameraOn)
    {
        const bool ok = (deviceMode == gc_devices_none) ? m_cameraSender.StartWithoutCamera(this, relay.m_seat)
                                                        : m_cameraSender.Start(this, relay.m_seat);
        if (!ok)
            LOG_WARNING("groupcall media: camera unavailable, reason=%d.", m_cameraSender.FailReason());
    }

    m_running.store(true);
    m_connectThread = std::thread(&GroupCallMediaSession::ConnectProc, this);
    LOG_INFO("groupcall media started, room=%s, seat=%d, mic=%d, camera=%d, relay=%s:%d.", relay.m_roomId.c_str(),
             relay.m_seat, micOn ? 1 : 0, cameraOn ? 1 : 0, relay.m_relayIp.c_str(), relay.m_relayPort);
    return true;
}

void GroupCallMediaSession::Stop()
{
    if (!m_running.load() && !m_connectThread.joinable())
        return;
    m_stopping.store(true);
    //先等连接线程：它可能还在连中转，连上之后会看到 m_stopping 不再投事件
    if (m_connectThread.joinable())
        m_connectThread.join();
    //再关中转（等接收线程结束），之后不会再有帧交给声音与摄像头
    m_relay.Close();
    m_cameraSender.Stop();
    m_cameraReceiver.Stop();
    m_audio.Stop();
    m_running.store(false);
    LOG_INFO("groupcall media stopped, room=%s.", m_relayInfo.m_roomId.c_str());
}

bool GroupCallMediaSession::IsRunning() const
{
    return m_running.load();
}

bool GroupCallMediaSession::IsConnected() const
{
    return m_relay.IsConnected();
}

bool GroupCallMediaSession::SetMicEnabled(bool on)
{
    return m_audio.SetMicEnabled(on);
}

bool GroupCallMediaSession::IsMicEnabled() const
{
    return m_audio.IsMicEnabled();
}

bool GroupCallMediaSession::SetCameraEnabled(bool on)
{
    if (!on)
    {
        m_cameraSender.Stop();
        return true;
    }
    if (m_cameraSender.IsRunning())
        return true;
    const bool ok = (m_deviceMode == gc_devices_none) ? m_cameraSender.StartWithoutCamera(this, m_relayInfo.m_seat)
                                                      : m_cameraSender.Start(this, m_relayInfo.m_seat);
    if (!ok)
        LOG_WARNING("groupcall media: camera open failed, reason=%d.", m_cameraSender.FailReason());
    return ok;
}

bool GroupCallMediaSession::IsCameraEnabled() const
{
    return m_cameraSender.IsRunning();
}

void GroupCallMediaSession::SetCameraBitrate(int bitrateBps)
{
    m_cameraSender.SetTargetBitrate(bitrateBps);
}

int GroupCallMediaSession::CameraFailReason() const
{
    return m_cameraSender.FailReason();
}

bool GroupCallMediaSession::GetCameraFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height,
                                           uint64_t& serial)
{
    if (self)
        return m_cameraSender.GetPreview(bgra, width, height, serial);
    return m_cameraReceiver.GetFrame(seat, bgra, width, height, serial);
}

std::vector<int> GroupCallMediaSession::UpdateSpeaking(int64_t nowMs)
{
    return m_audio.UpdateSpeaking(nowMs);
}

void GroupCallMediaSession::RemoveSeat(int seat)
{
    m_audio.RemoveSeat(seat);
    m_cameraReceiver.RemoveSeat(seat);
}

void GroupCallMediaSession::Subscribe(const std::vector<int>& seats)
{
    {
        std::lock_guard<std::mutex> guard(m_subscribeMutex);
        if (m_hasSubscribe && m_subscribeSent && m_subscribeSeats == seats)
            return;
        m_subscribeSeats = seats;
        m_hasSubscribe = true;
        m_subscribeSent = false;
    }
    SendPendingSubscribe();
}

int GroupCallMediaSession::SelfSeat() const
{
    return m_relayInfo.m_seat;
}

std::string GroupCallMediaSession::BuildSubscribeBody(int selfSeat, const std::vector<int>& seats)
{
    screenshare::ShareFrameHeader header;
    header.m_track = screenshare_track_control;
    header.m_senderSeat = selfSeat;
    header.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    header.m_timestamp = 0;
    const size_t count = seats.size() < kMaxSubscribeSeats ? seats.size() : kMaxSubscribeSeats;
    std::string payload;
    payload.push_back(static_cast<char>(screenshare_control_subscribe));
    payload.push_back(static_cast<char>(count));
    for (size_t i = 0; i < count; ++i)
        payload.push_back(static_cast<char>(seats[i]));
    return screenshare::ShareFraming::encodeBody(header, payload.data(), payload.size());
}

bool GroupCallMediaSession::SendRoomBody(const std::string& body)
{
    if (!m_relay.IsConnected())
        return false;
    return m_relay.SendBody(body);
}

void GroupCallMediaSession::OnRelayBody(const char* body, size_t len)
{
    screenshare::ShareMediaFrame frame;
    if (!m_reassembler.push(body, len, frame))
        return;
    //其他人的声音：交给房间语音
    if (frame.m_track == screenshare_track_mic)
    {
        m_audio.OnRemoteFrame(frame);
        return;
    }
    //其他人的摄像头：交给摄像头接收端
    if (frame.m_track == screenshare_track_camera)
    {
        m_cameraReceiver.OnRemoteFrame(frame);
        return;
    }
    //服务端要本人摄像头发关键帧（新成员加入、慢接收端丢帧、新订阅）
    int videoTrack = 0;
    if (screenshare::ShareFraming::parseKeyFrameRequest(frame, videoTrack) && videoTrack == screenshare_track_camera)
        m_cameraSender.RequestKeyFrame();
}

void GroupCallMediaSession::OnRelayClosed()
{
    LOG_WARNING("groupcall media: relay connection lost, room=%s.", m_relayInfo.m_roomId.c_str());
    PostEvent(gc_media_relay_lost);
}

void GroupCallMediaSession::ConnectProc()
{
    const unsigned char* key = m_serverPubKey.empty() ? NULL : &m_serverPubKey[0];
    const bool ok = m_relay.Connect(m_relayInfo.m_relayIp, m_relayInfo.m_relayPort, m_relayInfo.m_token, key, this,
                                    kGroupCallRelayConnectTimeoutSec);
    if (m_stopping.load())
        return;
    if (!ok)
    {
        LOG_ERROR("groupcall media: relay connect failed, room=%s, hello code=%d.", m_relayInfo.m_roomId.c_str(),
                  m_relay.LastHelloCode());
        PostEvent(gc_media_relay_failed);
        return;
    }
    LOG_INFO("groupcall media: relay connected, room=%s, seat=%d.", m_relayInfo.m_roomId.c_str(), m_relayInfo.m_seat);
    SendPendingSubscribe();
    PostEvent(gc_media_connected);
}

void GroupCallMediaSession::PostEvent(int event)
{
    if (m_stopping.load() || m_notifyWnd == NULL || !::IsWindow(m_notifyWnd))
        return;
    ::PostMessage(m_notifyWnd, kMsgGroupCallMediaEvent, static_cast<WPARAM>(event), m_token);
}

void GroupCallMediaSession::SendPendingSubscribe()
{
    std::vector<int> seats;
    {
        std::lock_guard<std::mutex> guard(m_subscribeMutex);
        if (!m_hasSubscribe || m_subscribeSent || !m_relay.IsConnected())
            return;
        seats = m_subscribeSeats;
        m_subscribeSent = true;
    }
    if (!SendRoomBody(BuildSubscribeBody(m_relayInfo.m_seat, seats)))
    {
        std::lock_guard<std::mutex> guard(m_subscribeMutex);
        m_subscribeSent = false;
        return;
    }
    LOG_INFO("groupcall media: subscribe sent, room=%s, seats=%u.", m_relayInfo.m_roomId.c_str(),
             static_cast<unsigned>(seats.size()));
}

} // namespace groupcall
