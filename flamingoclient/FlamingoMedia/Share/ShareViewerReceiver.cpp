/**
 *  屏幕共享观看者一侧的收流管线，ShareViewerReceiver.cpp
 *  流程与线程约定见 ShareViewerReceiver.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareViewerReceiver.h"
#include "ShareAudioSession.h"
#include "ShareCameraSender.h"
#include "ShareCameraReceiver.h"
#include "ShareAnnotation.h"
#include "../Caption/ShareCaptionFrame.h"   // ParseShareCaptionFrame、ShareCaptionSink（语音字幕）

#include <objbase.h>
#include <mfapi.h>
#include <string.h>
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"

#pragma comment(lib, "mfplat.lib")

namespace screenshare {

namespace {

//连接房间中转的超时，单位秒
const int kRelayConnectTimeoutSec = 5;

} // namespace

std::string ShareP2PFeatureId(const std::string& roomId, int seat)
{
    return roomId + kShareP2PFeatureSeparator + std::to_string(seat);
}

ShareViewerReceiver::ShareViewerReceiver()
    : m_sink(NULL)
    , m_sourceState(screenshare_source_sharing)
    , m_audio(NULL)
    , m_cameraSender(NULL)
    , m_cameraReceiver(NULL)
    , m_annotSink(NULL)
    , m_captionSink(NULL)
    , m_path(share_viewer_path_none)
    , m_stopping(false)
{
}

ShareViewerReceiver::~ShareViewerReceiver()
{
    Stop();
}

bool ShareViewerReceiver::Start(const ShareRelayInfo& relay, const unsigned char* serverPubKey, ShareViewerSink* sink,
                                const std::shared_ptr<balloonp2p::P2PLinkSlot>& p2pSlot)
{
    if (sink == NULL)
        return false;
    //上一次的工作线程与 P2P 通道先收尾：此后没有线程再用旧通道，可以销毁
    Stop();
    m_stopping.store(false);
    m_channel.reset();
    m_sink = sink;
    m_screens.SetListener(this);
    m_sourceState.store(screenshare_source_sharing);
    m_relayInfo = relay;
    if (serverPubKey != NULL)
        m_serverPubKey.assign(serverPubKey, serverPubKey + net::IDENTITY_PUBLICKEY_BYTES);
    else
        m_serverPubKey.clear();
    m_p2pSlot = p2pSlot;
    m_path.store(share_viewer_path_none);

    //有链路槽：在连接线程上先等 P2P 链路，不成再连中转，不占着 UI 线程
    if (m_p2pSlot)
    {
        LOG_INFO("screenshare viewer: room %s seat %d, waiting for the p2p link first.", relay.m_roomId.c_str(),
                 relay.m_seat);
        std::lock_guard<std::mutex> guard(m_workerMutex);
        m_connectThread = std::thread(&ShareViewerReceiver::ConnectProc, this);
        return true;
    }
    return ConnectRelay(share_viewer_relay_failed);
}

int ShareViewerReceiver::Path() const
{
    return m_path.load();
}

int ShareViewerReceiver::LinkPath() const
{
    return balloonp2p::LinkPathWhenUsing(m_path.load() == share_viewer_path_p2p, m_p2pSlot);
}

bool ShareViewerReceiver::ConnectRelay(int failEvent)
{
    const unsigned char* serverPubKey = m_serverPubKey.empty() ? NULL : &m_serverPubKey[0];
    if (!m_relayClient.Connect(m_relayInfo.m_relayIp, m_relayInfo.m_relayPort, m_relayInfo.m_token, serverPubKey, this,
                               kRelayConnectTimeoutSec))
    {
        LOG_ERROR("screenshare viewer: relay connect failed, room=%s, code=%d.", m_relayInfo.m_roomId.c_str(),
                  m_relayClient.LastHelloCode());
        if (!m_stopping.load())
            m_sink->OnViewerEvent(failEvent, 0);
        return false;
    }
    m_path.store(share_viewer_path_relay);
    LOG_INFO("screenshare viewer: joined room %s as seat %d.", m_relayInfo.m_roomId.c_str(), m_relayInfo.m_seat);
    return true;
}

void ShareViewerReceiver::ConnectProc()
{
    //分段等链路结果，每段之间检查是否已停止
    const unsigned long long start = ::GetTickCount64();
    int state = balloonp2p::P2PLinkSlot::slot_waiting;
    while (true)
    {
        if (m_stopping.load())
            return;
        state = m_p2pSlot->Wait(kShareP2PWaitSliceMs);
        if (state != balloonp2p::P2PLinkSlot::slot_waiting)
            break;
        if (::GetTickCount64() - start >= (unsigned long long)kShareP2PLinkWaitMs)
            break;
    }
    const unsigned long long waited = ::GetTickCount64() - start;
    if (state == balloonp2p::P2PLinkSlot::slot_ready)
    {
        m_channel.reset(new balloonp2p::P2PStreamChannel(m_p2pSlot, kShareP2PMaxBodyBytes, kShareP2PSendWaitMs));
        if (m_channel->Open(kShareP2PChannelId, this))
        {
            m_path.store(share_viewer_path_p2p);
            LOG_INFO("screenshare viewer: path decided, room=%s, seat=%d, path=p2p, waited=%llu ms.",
                     m_relayInfo.m_roomId.c_str(), m_relayInfo.m_seat, waited);
            return;
        }
        LOG_WARNING("screenshare viewer: open p2p channel failed, room=%s, using relay.", m_relayInfo.m_roomId.c_str());
        m_channel->Close();
    }
    else
    {
        LOG_INFO("screenshare viewer: p2p link not available, room=%s, slot state=%d, waited=%llu ms, using relay.",
                 m_relayInfo.m_roomId.c_str(), state, waited);
    }
    if (!m_stopping.load())
        ConnectRelay(share_viewer_relay_failed);
}

void ShareViewerReceiver::OnStreamClosed()
{
    //转交线程上下文：P2P 链路中途断开。在回退线程上等本线程结束后改连中转
    LOG_WARNING("screenshare viewer: p2p link lost, room=%s, falling back to the relay.", m_relayInfo.m_roomId.c_str());
    m_path.store(share_viewer_path_none);
    std::lock_guard<std::mutex> guard(m_workerMutex);
    if (m_stopping.load() || m_fallbackThread.joinable())
        return;
    m_fallbackThread = std::thread(&ShareViewerReceiver::FallbackProc, this);
}

void ShareViewerReceiver::FallbackProc()
{
    //等 P2P 帧通道的转交线程结束（它在退出前关闭解码器），中转的接收线程才能重新建解码器
    if (m_channel)
        m_channel->Close();
    if (m_p2pSlot)
        m_p2pSlot->CloseLink();
    if (m_stopping.load())
        return;
    ConnectRelay(share_viewer_relay_lost);
}

void ShareViewerReceiver::JoinWorkers()
{
    std::thread connect;
    std::thread fallback;
    {
        std::lock_guard<std::mutex> guard(m_workerMutex);
        connect.swap(m_connectThread);
        fallback.swap(m_fallbackThread);
    }
    if (connect.joinable())
        connect.join();
    if (fallback.joinable())
        fallback.join();
}

void ShareViewerReceiver::Stop()
{
    //先让工作线程放弃（等链路的连接线程 100 毫秒内退出；正在连中转时最多等连接超时），再关通道与中转
    m_stopping.store(true);
    JoinWorkers();
    m_path.store(share_viewer_path_none);
    if (m_channel)
        m_channel->Close();
    if (m_p2pSlot)
        m_p2pSlot->CloseLink();
    m_relayClient.Close();
}

void ShareViewerReceiver::OnStreamFrame(const unsigned char* data, size_t len)
{
    OnRelayBody(reinterpret_cast<const char*>(data), len);
}

void ShareViewerReceiver::OnStreamThreadStart()
{
    OnRelayThreadStart();
}

void ShareViewerReceiver::OnStreamThreadExit()
{
    OnRelayThreadExit();
}

bool ShareViewerReceiver::GetLatestFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const
{
    //只有一块画面时就是它；多人共享时取座位最小的一路（通常是主持人），录制与只看一块画面的调用方用它
    const std::vector<int> seats = m_screens.Seats();
    if (seats.empty())
        return false;
    return m_screens.GetLatestFrame(seats[0], bgra, width, height, serial);
}

uint64_t ShareViewerReceiver::DecodedFrames() const
{
    return m_screens.DecodedCount();
}

bool ShareViewerReceiver::GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                         uint64_t& serial) const
{
    return m_screens.GetLatestFrame(seat, bgra, width, height, serial);
}

std::vector<int> ShareViewerReceiver::ScreenSeats() const
{
    return m_screens.Seats();
}

int ShareViewerReceiver::ScreenSourceState(int seat) const
{
    return m_screens.SourceState(seat);
}

int ShareViewerReceiver::SourceState() const
{
    return m_sourceState.load();
}

void ShareViewerReceiver::OnRelayThreadStart()
{
    //解码器在接收线程上使用，本线程要初始化 COM 与 MF
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
}

void ShareViewerReceiver::OnRelayThreadExit()
{
    //解码器在本线程上建、在本线程上关（MF 对象要在 MFShutdown 之前释放）
    m_screens.Clear();
    ::MFShutdown();
    ::CoUninitialize();
}

void ShareViewerReceiver::SetAudioSession(ShareAudioSession* audio)
{
    m_audio.store(audio);
}

void ShareViewerReceiver::SetCameraEndpoints(ShareCameraSender* sender, ShareCameraReceiver* receiver)
{
    m_cameraSender.store(sender);
    m_cameraReceiver.store(receiver);
}

void ShareViewerReceiver::SetAnnotationSink(ShareAnnotationSink* sink)
{
    m_annotSink.store(sink);
}

void ShareViewerReceiver::SetCaptionSink(ShareCaptionSink* sink)
{
    m_captionSink.store(sink);
}

bool ShareViewerReceiver::SendRoomBody(const std::string& body)
{
    //P2P：一个包体一帧；通道对象只在 Start 里重建，这里读到 p2p 时它一定存在
    if (m_path.load() == share_viewer_path_p2p)
        return m_channel->SendFrame(reinterpret_cast<const unsigned char*>(body.data()), body.size());
    if (!m_relayClient.IsConnected())
        return false;
    return m_relayClient.SendBody(body);
}

void ShareViewerReceiver::OnRelayBody(const char* body, size_t len)
{
    ShareMediaFrame frame;
    if (!m_reassembler.push(body, len, frame))
        return;
    //发起人与其他观看者的声音：交给房间语音会话
    if (frame.m_track == screenshare_track_mic || frame.m_track == screenshare_track_system_audio)
    {
        ShareAudioSession* audio = m_audio.load();
        if (audio != NULL)
            audio->OnRemoteFrame(frame);
        return;
    }
    //其他成员的摄像头：交给摄像头接收端
    if (frame.m_track == screenshare_track_camera)
    {
        ShareCameraReceiver* cameras = m_cameraReceiver.load();
        if (cameras != NULL)
            cameras->OnRemoteFrame(frame);
        return;
    }
    //发起人与其他观看者的标注：交给标注接收方
    if (frame.m_track == screenshare_track_annotation)
    {
        ShareAnnotationSink* annot = m_annotSink.load();
        if (annot != NULL)
            annot->OnAnnotationFrame(frame.m_senderSeat, frame.m_data);
        return;
    }
    //发起人与其他观看者的实时字幕（语音字幕）：交给字幕接收方
    ShareCaptionText caption;
    if (ParseShareCaptionFrame(frame, caption))
    {
        ShareCaptionSink* sink = m_captionSink.load();
        if (sink != NULL)
            sink->OnCaptionFrame(frame.m_senderSeat, caption);
        return;
    }
    //本人摄像头轨的关键帧请求（有新成员加入）
    int videoTrack = 0;
    if (ShareFraming::parseKeyFrameRequest(frame, videoTrack))
    {
        ShareCameraSender* camera = m_cameraSender.load();
        if (videoTrack == screenshare_track_camera && camera != NULL)
            camera->RequestKeyFrame();
        return;
    }

    int state = 0;
    if (ShareFraming::parseSourceState(frame, state))
    {
        //多人共享（第 22 期）：按画面座位各记一份；整体的状态与事件沿用（只有一块画面时就是它的）
        m_screens.SetSourceState(frame.m_senderSeat, state);
        if (m_sourceState.exchange(state) != state && m_sink != NULL)
            m_sink->OnViewerEvent(share_viewer_source_state, state);
        return;
    }
    //屏幕画面：按发送者座位交给那一路的解码器（第 22 期多人共享）
    if (frame.m_track == screenshare_track_screen)
        m_screens.OnScreenFrame(frame);
}

void ShareViewerReceiver::OnScreenDecoded(int seat)
{
    (void)seat;
    if (m_sink != NULL)
        m_sink->OnViewerFrame();
}

void ShareViewerReceiver::OnRelayClosed()
{
    LOG_WARNING("screenshare viewer: relay connection lost.");
    if (m_sink != NULL)
        m_sink->OnViewerEvent(share_viewer_relay_lost, 0);
}

} // namespace screenshare
