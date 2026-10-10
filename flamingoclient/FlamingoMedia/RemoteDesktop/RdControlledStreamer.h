/**
 *  远程桌面被控端推流器，RdControlledStreamer.h
 *
 *  被控端在收到中转参数(cmd_rd_relay_info, role=被控端)后用本类推流：起一条抓屏线程，
 *  它先建立传输（RdTransport：服务端建了 P2P 会话时优先走 P2P 链路，否则 / 失败时连
 *  imgserver 中转；P2P 方案第 6 期起连接挪到本线程里做，等链路最多 20 秒不能占着 UI 线程），
 *  连上后先发一次屏幕 BITMAPINFO 帧与首屏全量帧让控制端解码器就绪，之后按固定周期抓
 *  增量帧推给控制端。连接失败时向 Start 给的通知窗口投 kMsgRdStreamerConnectFailed。抓屏线程内把本线程切到 Per-Monitor-V2
 *  DPI 上下文，使 GetSystemMetrics / BitBlt 工作在物理像素（Flamingo 是 DPI-unaware
 *  进程，否则抓到的是被 DWM 拉伸的逻辑像素、画面模糊放大）。
 *
 *  本类同时作为传输的事件接收者：控制端发来的鼠标键盘命令注入本机；连接断开时标记
 *  停止、由协调器据 state_notify 收尾会话。一次被控会话
 *  持有一个本类实例。
 *
 *  一期固定 16 位色 + 差异(DIFF)算法、抓主显示器、5 帧/秒。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <winsock2.h>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "RdTransport.h"   //含 RdRelayClient.h（winsock2.h）与 P2PLinkBroker.h

namespace remotedesktop {

//被控端推流器：连中转后周期抓屏推流，并接收控制端命令。
class RdControlledStreamer : public RdRelayClientHandler
{
public:
    RdControlledStreamer();
    ~RdControlledStreamer();

    RdControlledStreamer(const RdControlledStreamer&) = delete;
    RdControlledStreamer& operator=(const RdControlledStreamer&) = delete;

    /**
     *  启动推流：起抓屏线程，由它先建立传输、连上后推流。不阻塞。
     *  @param params    传输参数（拷贝保存）：中转参数与可选的 P2P 链路槽
     *  @param notifyWnd 连接失败时接收 kMsgRdStreamerConnectFailed 的窗口（远程桌面控制器的消息窗口，借用）；
     *                   为 NULL 时不通知
     *  @return 已起推流线程返回 true；已在运行时直接返回 true
     */
    bool Start(const RdTransportParams& params, HWND notifyWnd);

    //停止推流：停抓屏线程、关传输。幂等。线程还在连接中转时最多等连接超时（5 秒）才返回。
    void Stop();

    //是否处于推流运行态（推流线程已起、尚未结束或断开）。
    bool IsRunning() const;

    //最近一次 Start 的序号：连接失败通知的 wParam 带着它，接收方据此丢弃上一次会话迟到的通知
    unsigned int StartSeq() const { return m_startSeq; }

    //当前传输走的路径（RdTransport::rd_transport_path），日志与测试用
    int TransportPath() const { return m_transport.Path(); }

    //传输的链路实际路径（2026-10-04 起，供受控提示条上的「P2P」徽章）；取值同 RdTransport::LinkPath，可在任意线程调用
    int LinkPath() const { return m_transport.LinkPath(); }

    // ==== RdRelayClientHandler（P2P 帧通道的转交线程或中转的接收线程上触发）====

    //控制端发来的帧：请求关键帧，或鼠标键盘命令（注入本机）。
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override;

    //连接断开（对端关闭 / 网络错误）：标记停止，UI 侧收尾由协调器据信令处理。
    virtual void OnRelayClosed() override;

private:
    //抓屏线程主体：先建立传输，连上后进入 PushLoop；连接失败时通知 m_notifyWnd。
    void PushProc();

    //推流循环（传输已连上之后）：切 PMv2 → 建抓屏引擎 → 发 BITMAPINFO+首屏 → 周期推增量帧。
    void PushLoop();

private:
    RdTransport                  m_transport;    //传输（被控端这条用于发屏幕帧、收控制命令）
    RdTransportParams            m_params;       //本次会话的传输参数（Start 时拷贝，推流线程使用）
    HWND                         m_notifyWnd;    //连接失败的通知窗口（借用）；可为 NULL
    std::unique_ptr<std::thread> m_pushThread;   //抓屏推流线程
    std::atomic<bool>            m_running;       //是否处于推流运行态
    std::atomic<bool>            m_stopping;      //是否正在停止（推流线程据此放弃连接、退出循环）
    std::atomic<bool>            m_keyframeRequested;  //控制端请求重发关键帧的标志（接收线程置，推流线程取并清）
    std::mutex                   m_connectMutex;  //保护 m_transportReady：Stop 据它决定能否在推流线程运行时关闭传输
    bool                         m_transportReady; //传输是否已连上（推流线程在 Connect 成功后置位）；连接期间不能关闭传输
    std::atomic<unsigned int>    m_startSeq;      //Start 的序号，从 1 起每次加 1
};

} // namespace remotedesktop
