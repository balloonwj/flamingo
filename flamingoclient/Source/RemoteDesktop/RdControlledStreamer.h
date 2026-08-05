/**
 *  远程桌面被控端推流器，RdControlledStreamer.h
 *
 *  被控端在收到中转参数(cmd_rd_relay_info, role=被控端)后用本类推流：连上 imgserver
 *  中转通道，起一条抓屏线程，先发一次屏幕 BITMAPINFO 帧与首屏全量帧让控制端解码器
 *  就绪，之后按固定周期抓增量帧推给控制端。抓屏线程内把本线程切到 Per-Monitor-V2
 *  DPI 上下文，使 GetSystemMetrics / BitBlt 工作在物理像素（Flamingo 是 DPI-unaware
 *  进程，否则抓到的是被 DWM 拉伸的逻辑像素、画面模糊放大）。
 *
 *  本类同时作为中转连接的事件接收者：控制端发来的鼠标键盘命令在 ⑤-c 阶段注入，
 *  本期暂忽略；中转断开时标记停止、由协调器据 state_notify 收尾会话。一次被控会话
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
#include "RdRelayClient.h"

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
     *  启动推流。阻塞完成中转连接与握手后，起抓屏线程异步推流。
     *  @param ip    中转通道地址（cmd_rd_relay_info 下发）
     *  @param port  中转通道端口（一期 20003）
     *  @param token relay 令牌（cmd_rd_relay_info 下发，非空）
     *  @return 中转连接 / 握手成功并已起推流线程返回 true；连接失败返回 false
     */
    bool Start(const std::string& ip, int port, const std::string& token);

    //停止推流：停抓屏线程、关中转连接。幂等。
    void Stop();

    //是否处于推流运行态。
    bool IsRunning() const;

    // ==== RdRelayClientHandler ====

    //控制端发来的鼠标键盘命令（⑤-c 阶段注入到本机；本期暂忽略）。
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override;

    //中转连接断开（对端关闭 / 网络错误）：标记停止，UI 侧收尾由协调器据信令处理。
    virtual void OnRelayClosed() override;

private:
    //抓屏线程主体：切 PMv2 → 建抓屏引擎 → 发 BITMAPINFO+首屏 → 周期推增量帧。
    void PushProc();

private:
    RdRelayClient                m_relay;        //中转连接（被控端这条用于发屏幕帧、收控制命令）
    std::unique_ptr<std::thread> m_pushThread;   //抓屏推流线程
    std::atomic<bool>            m_running;       //是否处于推流运行态
    std::atomic<bool>            m_stopping;      //是否正在停止（推流线程据此退出循环）
    std::atomic<bool>            m_keyframeRequested;  //控制端请求重发关键帧的标志（接收线程置，推流线程取并清）
};

} // namespace remotedesktop
