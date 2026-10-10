/**
 *  群通话的媒体会话，GroupCallMediaSession.h
 *
 *  一名已加入的成员持有一个：一条到 imgserver 的房间中转连接（复用屏幕共享的 ShareRelayClient 与 1859 / 1860，令牌角色
 *  为群通话成员 13），其上收发本人与其他人的麦克风声音（ShareAudioSession：采集、回声消除、Opus 编码、多路解码混音、
 *  说话人检测）与摄像头画面（ShareCameraSender / ShareCameraReceiver，640×360、每个座位一个解码器、最多 9 路）。
 *  收到的包体按轨道分发：麦克风 → 房间语音，摄像头 → 摄像头接收端，服务端的摄像头关键帧请求 → 本人摄像头发送端。
 *  第 5 期起还发订阅控制帧（只收宫格里看得见的画面），并把 Opus DTX 打开（不说话时几乎不发包）。
 *  设计见 docs/群音视频通话方案.md 6.1。
 *
 *  线程：Start / Stop / Set* / Subscribe / GetCameraFrame / UpdateSpeaking 在 UI 线程调用；连中转在本类自己的连接线程上，
 *  连上后中转的接收线程调用 OnRelayBody；事件（连上、连不上、断开）经 PostMessage 投给 Start 时给的窗口。
 *  无设备模式（单元测试与联调用例）不打开麦克风、扬声器与摄像头。
 *
 *  典型用法（群通话控制器里）：
 *      m_media.Start(relay, host->GetServerPublicKey(), true, false, m_hWnd, ++m_mediaToken, gc_devices_all);
 *      ...收到 kMsgGroupCallMediaEvent 且 wParam 为 gc_media_connected 后 m_media.Subscribe(seats);
 *      m_media.Stop();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "../Share/ShareRelayClient.h"   //带进 winsock2.h，须排在 windows.h 之前
#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../Share/ShareAudioSession.h"
#include "../Share/ShareAudioTransport.h"
#include "../Share/ShareCameraReceiver.h"
#include "../Share/ShareCameraSender.h"
#include "../Share/ShareFraming.h"
#include "GroupCallProtocolClient.h"

namespace groupcall {

//媒体会话打开哪些设备。正常使用只有 gc_devices_all；另两种给单元测试与联调用（Debug 版由环境变量
//FLAMINGO_GROUPCALL_NO_DEVICES 选择，见 CGroupCallController::deviceMode）
enum gc_device_mode
{
    gc_devices_all      = 0,   //正常：打开麦克风、扬声器与摄像头
    gc_devices_none     = 1,   //什么设备都不开（单元测试、无设备联调）：声音由调用方喂入，摄像头不出画面
    gc_devices_no_audio = 2,   //不开声音设备、摄像头照常：同一台机器上开多个客户端联调时，避免扬声器的声音又被麦克风收进去
};

//连房间中转的超时，单位秒（连接、加密握手与报到合计）
const int kGroupCallRelayConnectTimeoutSec = 5;

//群通话的媒体会话
class GroupCallMediaSession : public screenshare::ShareAudioTransport, private screenshare::ShareRelayClientHandler
{
public:
    GroupCallMediaSession();
    ~GroupCallMediaSession();

    GroupCallMediaSession(const GroupCallMediaSession&) = delete;
    GroupCallMediaSession& operator=(const GroupCallMediaSession&) = delete;

    /**
     *  开始：打开扬声器（与麦克风，micOn 时）、启动摄像头接收端（与本人摄像头，cameraOn 时），在连接线程上连房间中转。
     *  已在运行时先 Stop 再重新开始。
     *  @param relay          1746 下发的中转参数
     *  @param serverPubKey   服务端 32 字节公钥；为 NULL 时不加密（内部复制）
     *  @param micOn          麦克风一开始是否打开
     *  @param cameraOn       摄像头一开始是否打开；打不开时摄像头保持关闭，原因见 CameraFailReason
     *  @param notifyWnd      事件的接收窗口（kMsgGroupCallMediaEvent），不可为 NULL
     *  @param token          本次会话的代号，随事件回传（lParam），接收方据此丢掉旧会话的事件
     *  @param deviceMode     打开哪些设备，取值见 enum gc_device_mode；正常使用传 gc_devices_all
     *  @return 已起连接线程返回 true；参数非法返回 false
     */
    bool Start(const GroupCallRelayInfo& relay, const unsigned char* serverPubKey, bool micOn, bool cameraOn,
               HWND notifyWnd, LPARAM token, int deviceMode);

    //断开中转、停止声音与摄像头、等各线程结束；可重复调用。不可在回调里调用
    void Stop();

    //是否在运行（Start 之后、Stop 之前）
    bool IsRunning() const;

    //房间中转是否已连上并报到成功
    bool IsConnected() const;

    /**
     *  打开或关闭本人麦克风（关闭时真正停掉麦克风设备）。
     *  @return 打开失败（设备不可用）返回 false，麦克风保持关闭
     */
    bool SetMicEnabled(bool on);

    //麦克风是否打开
    bool IsMicEnabled() const;

    /**
     *  打开或关闭本人摄像头。
     *  @return 打开失败（没有摄像头或被占用）返回 false，摄像头保持关闭，原因见 CameraFailReason
     */
    bool SetCameraEnabled(bool on);

    //摄像头是否打开（正在发送）
    bool IsCameraEnabled() const;

    //摄像头最近一次失败的原因（call::CameraFailReason）；没有失败过为 0
    int CameraFailReason() const;

    /**
     *  设置本人摄像头这一路的目标码率（第 5 期码率自适应，取值见 GroupCallCameraBitrateFor）。摄像头关着时记下，打开后生效。
     *  @param bitrateBps 目标码率，单位 bit/s
     */
    void SetCameraBitrate(int bitrateBps);

    /**
     *  取某一路摄像头最近一帧（BGRA，自上而下，行跨度 = 宽 × 4）。
     *  @param seat   座位号（self 为 true 时忽略）
     *  @param self   是否取本人摄像头的预览
     *  @param serial [出参] 帧序号，调用方据此判断有没有新画面
     *  @return 已有画面返回 true
     */
    bool GetCameraFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial);

    /**
     *  按各路最近的电平更新发言判定（UI 定时器上每 300 毫秒左右调一次）。
     *  @param nowMs 当前时间（毫秒，单调递增）
     *  @return 当前在说话的座位号（升序；本人在说话时含本人座位）
     */
    std::vector<int> UpdateSpeaking(int64_t nowMs);

    /**
     *  成员离开：删掉他的声音路与摄像头解码器。
     *  @param seat 座位号
     */
    void RemoveSeat(int seat);

    /**
     *  订阅（第 5 期）：只要这些座位的摄像头画面。与上一次相同时不重发；中转还没连上时先记下，连上后补发。
     *  @param seats 座位号（宫格里开着摄像头的其他人）；为空表示一路画面都不要
     */
    void Subscribe(const std::vector<int>& seats);

    //本人座位号（Start 时记下）
    int SelfSeat() const;

    /**
     *  构造订阅控制帧的包体（1860 的包体，未加密）：轨道为控制帧、首尾分片位都置上、时间戳 0，
     *  载荷 [screenshare_control_subscribe][座位数][各座位号]，座位最多 254 个。纯函数，单测用。
     *  @param selfSeat 本人座位号（服务端会改写为登记的座位）
     *  @param seats    要看的座位号
     */
    static std::string BuildSubscribeBody(int selfSeat, const std::vector<int>& seats);

    //ShareAudioTransport：经房间中转连接发出一个包体；没连上时返回 false。可在任意线程调用
    virtual bool SendRoomBody(const std::string& body) override;

private:
    //ShareRelayClientHandler：接收线程上收到一个 1860 包体
    virtual void OnRelayBody(const char* body, size_t len) override;
    //ShareRelayClientHandler：中转连接异常断开
    virtual void OnRelayClosed() override;

    //连接线程：连房间中转并报到，结果经 kMsgGroupCallMediaEvent 告知；连上后补发订阅
    void ConnectProc();

    //向事件窗口投一个事件
    void PostEvent(int event);

    //把记下的订阅发出去（已连上时）
    void SendPendingSubscribe();

    screenshare::ShareRelayClient       m_relay;           //房间中转连接
    screenshare::ShareFrameReassembler  m_reassembler;     //分片重组（只在接收线程上使用）
    screenshare::ShareAudioSession      m_audio;           //房间语音
    screenshare::ShareCameraSender      m_cameraSender;    //本人摄像头的发送端
    screenshare::ShareCameraReceiver    m_cameraReceiver;  //其他人摄像头的解码
    GroupCallRelayInfo                  m_relayInfo;       //本次的中转参数（Start 时拷贝，连接线程使用）
    std::vector<unsigned char>          m_serverPubKey;    //服务端公钥的副本；为空表示不加密
    HWND                                m_notifyWnd;       //事件的接收窗口（不拥有）
    LPARAM                              m_token;           //本次会话的代号
    int                                 m_deviceMode;      //打开哪些设备（Start 时记下），取值见 enum gc_device_mode
    std::atomic<bool>                   m_running;         //是否在运行
    std::atomic<bool>                   m_stopping;        //是否正在停止：连接线程据此不再投事件
    std::thread                         m_connectThread;   //连接线程；Stop 时回收
    std::mutex                          m_subscribeMutex;  //保护以下三项
    std::vector<int>                    m_subscribeSeats;  //最近一次要求的订阅
    bool                                m_hasSubscribe;    //是否要求过订阅（从没要求过时不发，服务端按收全部处理）
    bool                                m_subscribeSent;   //最近一次要求的订阅是否已经发到中转上
};

} // namespace groupcall
