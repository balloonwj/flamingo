/**
 *  P2P 信令在主程序里的转发通道：发送放行规则与服务端帧的路由（docs/P2P通信方案.md 8.4「exe 侧改动」）。
 *  主程序不解析 2300 段信令的内容：DLL 交来的命令经 CIUSocket 原样发出，收到的 2300 段帧在网络接收线程上
 *  原样交给 DLL。本文件放这条通道上两处与业务无关、可以单独测试的逻辑，做法与文件共享的 ShareChannel 相同：
 *    - P2PChannelPolicy：代 DLL 发送命令前的放行判定；
 *    - CP2PFrameRouter：把网络线程上收到的帧交给已挂接的 DLL 模块。UI 线程在加载成功后挂接、在卸载 DLL 之前
 *      解除挂接；解除挂接时会等网络线程上正在进行的转交结束，此后再来的帧一律丢弃，进程退出时不会调到已经
 *      卸载的 DLL。
 *  典型用法（CP2PHost）：
 *      m_router.Attach(module);                       // DLL 初始化成功后，UI 线程
 *      m_router.Route(cmd, seq, body, len);           // CIUSocket::HandleV2Frame，网络接收线程
 *      m_router.Detach();                             // 卸载 DLL 之前，UI 线程
 *  单元测试见 Source/Tests/P2PChannelTests.cpp（标签 [p2phost]）。
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <mutex>

#include "../../../FlamingoP2P/FlamingoP2PApi.h"

namespace balloonp2p {

/**
 *  P2PChannelPolicy —— 主程序代 DLL 发送 P2P 信令的放行规则；只有静态方法。
 */
class P2PChannelPolicy
{
public:
    /**
     *  判断能否代 DLL 发出一条命令。只放行 2300 段（p2p::IsP2PCommand），DLL 不能借这个接口发别的命令；
     *  包体为空的不发；主连接未连上时不发（调用方据返回 0 得知没有发出）。
     *  @param cmd       命令号
     *  @param bodyLen   包体字节数
     *  @param connected 主连接此刻是否已连上
     *  @return 可以发送返回 true
     */
    static bool CanSendP2PCommand(uint16_t cmd, uint32_t bodyLen, bool connected);

private:
    P2PChannelPolicy() = delete;
};

/**
 *  CP2PFrameRouter —— 把网络接收线程上收到的 2300 段帧交给已挂接的 DLL 模块。
 *  Attach / Detach 只在 UI 线程上调用；Route 可在任意线程上调用。三者由同一把互斥量串行化：
 *  Detach 返回之后，没有任何线程还在模块的 OnServerFrame 里。
 */
class CP2PFrameRouter
{
public:
    CP2PFrameRouter();

    CP2PFrameRouter(const CP2PFrameRouter&) = delete;
    CP2PFrameRouter& operator=(const CP2PFrameRouter&) = delete;

    /**
     *  挂接模块，此后的 2300 段帧都交给它。
     *  @param module 已初始化的模块（借用，不持有）；须在 Detach 之后才能被关闭、卸载
     */
    void Attach(p2p::IP2PModule* module);

    /**
     *  解除挂接。返回时没有线程还在调用模块；此后的帧一律丢弃。未挂接时调用无效果。
     */
    void Detach();

    /**
     *  转交一帧。号段外的帧不转交、不计入丢弃数；号段内但没有挂接模块的帧丢弃并计数。
     *  模块的 OnServerFrame 只拷贝包体后立即返回，持锁调用它不会长时间阻塞 UI 线程的 Detach。
     *  @param cmd  命令号
     *  @param seq  帧序号（应答帧为对应请求的 seq，推送为 0）
     *  @param body 包体（借用，调用期间有效）
     *  @param len  包体字节数
     *  @return 模块接收了这一帧返回 true；号段外、没有挂接模块、模块拒收时返回 false
     */
    bool Route(uint16_t cmd, uint32_t seq, const char* body, uint32_t len);

    /**
     *  取「号段内、但没有挂接模块」而被丢弃的帧数（DLL 未加载或已卸载时到达的帧）。
     *  @return 丢弃的帧数
     */
    uint32_t GetDroppedCount() const;

private:
    mutable std::mutex   m_mutex;     //串行化 Attach / Detach / Route
    p2p::IP2PModule*     m_module;    //已挂接的模块（借用）；未挂接时为 NULL
    uint32_t             m_dropped;   //号段内但没有挂接模块而被丢弃的帧数
};

} // namespace balloonp2p
