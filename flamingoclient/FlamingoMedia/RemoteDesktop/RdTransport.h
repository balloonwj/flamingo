/**
 *  远程桌面一端的传输：P2P 链路优先、imgserver 中转兜底（docs/P2P通信方案.md 第 6 期），RdTransport.h
 *
 *  服务端为一次远程桌面建了 P2P 会话时，1716 带 p2psessionid，随后两端各收到用途为 rd、功能会话号为远程桌面会话号
 *  的 2302，由 FlamingoP2P.dll 建链。远程桌面控制器为本端认领该会话号的链路槽（P2PLinkBroker::ClaimSlot），连同
 *  1716 的中转参数一起交给本类：
 *    - 有链路槽：先等链路结果（最多 kRdP2PLinkWaitMs），就绪就在链路上打开远程桌面的帧通道（通道号 kRdP2PChannelId）；
 *      链路失败、超时或通道打不开时退回 imgserver 中转。两端各自决定，链路的结果对两端对称；
 *    - 没有链路槽：直接连 imgserver 中转，与第 6 期之前相同。
 *  两条路径对上层是同一个接口：发送引擎帧、经 RdRelayClientHandler 收引擎帧与断开通知。P2P 帧通道每帧整帧发出
 *  （可靠通道没有单次发送上限），中转仍按 RdRelayFraming 分片。
 *
 *  Connect 是阻塞的（等链路最多 20 秒、连中转最多 5 秒），调用方在自己的工作线程上调用：被控端在推流线程上、
 *  控制端在观看窗的连接线程上。
 *
 *  典型用法（RdControlledStreamer::PushProc）：
 *      if (!m_transport.Connect(m_params, this, &m_stopping))
 *          return;
 *      m_transport.SendEngineFrame(frame.data(), frame.size());
 *      ...
 *      m_transport.Close();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <winsock2.h>
#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <memory>
#include <string>

//RdRelayClient.h 带进 <winsock2.h>，必须排在带进 <windows.h> 的 P2PLinkBroker.h 之前
#include "RdRelayClient.h"
#include "../../Source/Feature/P2P/P2PLinkBroker.h"

namespace remotedesktop {

//远程桌面登记的 P2P 用途：与服务端 ChatSession::onRemoteDesktopResponse 建会话时的 purpose（kP2PPurposeRd）一致
const char* const kRdP2PPurpose = "rd";

//远程桌面在 P2P 链路上使用的通道号：两端约定，一次会话只用这一条
const uint16_t kRdP2PChannelId = 1;

//P2P 帧通道的单帧上限（字节）：关键帧是 16 位色的整屏位图，8K 屏约 63 MiB，再大的帧按字节流错位处理
const size_t kRdP2PMaxFrameBytes = 64 * 1024 * 1024;

//等链路结果的最长时间（毫秒）：打洞超时（服务端下发，默认 3 秒）+ 地址探测预留 1.5 秒 + DLL 回落中转最多 10 秒，再留余量
const int kRdP2PLinkWaitMs = 20000;

//等链路期间检查取消标志的间隔（毫秒）：用户在这段时间里结束会话时，最多这么久之后放弃等待
const int kRdP2PWaitSliceMs = 100;

//P2P 帧通道发送队列满时等可写通知的最长时间（毫秒）：超过即判通道异常，推流随之结束
const int kRdP2PSendWaitMs = 30000;

//连 imgserver 中转（含握手）的超时（秒），与第 6 期之前 RdRelayClient::Connect 的默认值相同
const int kRdRelayConnectTimeoutSec = 5;

/**
 *  一次连接的参数：中转参数必有，P2P 链路槽可选。由远程桌面控制器在 UI 线程上填好，交给工作线程使用，因此服务端
 *  公钥存一份副本，不引用宿主的缓冲。
 */
struct RdTransportParams
{
    std::string   m_sessionId;   //远程桌面会话号（日志用）
    std::string   m_relayIp;     //imgserver 中转地址（1716 的 relayip）
    int           m_relayPort;   //中转端口（1716 的 relayport）
    std::string   m_token;       //中转令牌（1716 的 token）
    unsigned char m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];   //服务端公钥的副本；m_hasServerPubKey 为 false 时无意义
    bool          m_hasServerPubKey;   //是否有服务端公钥；false 表示中转全程明文
    std::shared_ptr<balloonp2p::P2PLinkSlot> m_slot;   //P2P 链路槽；为空表示这次会话不走 P2P

    RdTransportParams();

    /**
     *  填中转参数。
     *  @param sessionId    远程桌面会话号
     *  @param relayIp      中转地址
     *  @param relayPort    中转端口
     *  @param token        中转令牌
     *  @param serverPubKey 服务端公钥（net::IDENTITY_PUBLICKEY_BYTES 字节，只在本次调用期间使用）；NULL 表示明文
     */
    void SetRelay(const std::string& sessionId, const std::string& relayIp, int relayPort, const std::string& token,
                  const unsigned char* serverPubKey);
};

/**
 *  远程桌面一端的传输。实现 IP2PStreamHandler 是为了接 P2P 帧通道的回调，转给上层的 RdRelayClientHandler。
 *  线程：Connect 在调用方的工作线程上；SendEngineFrame 可以在任意线程上调用（Connect 返回之前调用返回 false）；
 *  Close 可以在任意线程上调用，但调用方须保证 Close 时没有线程停在 Connect 里（先置取消标志、等连接线程结束）。
 */
class RdTransport : public balloonp2p::IP2PStreamHandler
{
public:
    //当前走的路径
    enum rd_transport_path
    {
        rd_path_none  = 0,   //还没连上，或已关闭
        rd_path_p2p   = 1,   //P2P 链路上的帧通道
        rd_path_relay = 2,   //imgserver 中转
    };

    RdTransport();

    //析构：等同 Close
    ~RdTransport();

    RdTransport(const RdTransport&) = delete;
    RdTransport& operator=(const RdTransport&) = delete;

    /**
     *  连接：有链路槽时先等链路、就绪走 P2P，否则（或 P2P 失败）连 imgserver 中转。阻塞。
     *  @param params  连接参数（只在本次调用期间使用；链路槽的引用由本对象另持一份，直到下次 Connect 或析构）
     *  @param handler 收帧与断开的回调（借用，须在 Close 返回之前一直有效），不可为 NULL；回调线程见 RdRelayClientHandler
     *  @param cancel  取消标志（借用，可为 NULL）：等链路期间置为 true 时放弃等待并返回 false，不再连中转
     *  @return 连上返回 true（经 Path 可知走的哪条路）；两条路都失败或被取消返回 false
     */
    bool Connect(const RdTransportParams& params, RdRelayClientHandler* handler, const std::atomic<bool>* cancel);

    /**
     *  发送一个引擎帧。线程安全。
     *  @param data 引擎帧（首字节为 rd_frame_type）
     *  @param len  长度（字节）
     *  @return 已交给链路或中转连接返回 true；未连接、已断开或发送失败返回 false
     */
    bool SendEngineFrame(const unsigned char* data, size_t len);

    //关闭当前连接（幂等）：之后不再回调 handler
    void Close();

    //是否连着（Connect 成功且没有断开、没有 Close）
    bool IsConnected() const;

    //当前路径，取值见 rd_transport_path
    int Path() const { return m_path; }

    /**
     *  取当前 P2P 链路的统计，供观看窗、推流器写诊断日志。可在任意线程上调用。
     *  @param out 输出：链路统计（本函数先清零并填好 m_cbSize）
     *  @return 当前走 P2P 且链路还在时返回 true；走中转、未连接或链路已断开时返回 false，out 保持清零
     */
    bool GetP2PStats(p2p::P2PLinkStats& out) const;

    /**
     *  链路实际路径（2026-10-04 起，供观看窗工具条与受控提示条上的「P2P」徽章）。可在任意线程上调用，不等待。
     *  @return 走 P2P 且链路还在时返回链路统计里的路径（1 局域网直连、2 打洞直连、3 经中转）；走中转、未连接或已关闭时返回 0
     */
    int LinkPath() const;

    /**
     *  判定推流器在某条路径上是否需要定时重发关键帧（BITMAPINFO + 首屏全量）。中转路径上，控制端晚连上中转时
     *  之前的帧会被中转丢掉，要靠周期关键帧重新对齐；P2P 帧通道可靠有序，未打开前的数据由 DLL 暂存补交，控制端
     *  连上后也会主动请求一次关键帧，定时重发只会白占带宽（16 位色 1080p 的首屏约 4 MB，见 docs/P2P通信方案.md 第 14.26 节）。
     *  @param path 取值见 rd_transport_path
     *  @return 走中转或还没连上时返回 true；走 P2P 时返回 false
     */
    static bool NeedsPeriodicKeyframes(int path);

    //以下为 IP2PStreamHandler 的实现，在 P2P 帧通道的转交线程上被调用

    /**
     *  收到一个引擎帧：转给上层。
     *  @param data 帧数据，只在本次调用期间有效
     *  @param len  帧长度（字节）
     */
    virtual void OnStreamFrame(const unsigned char* data, size_t len) override;

    //P2P 链路断开：转给上层（与中转断开同样处理）
    virtual void OnStreamClosed() override;

private:
    /**
     *  等链路结果并打开 P2P 帧通道。
     *  @return 打开成功返回 true；链路失败、超时、被取消或通道打不开返回 false（*cancelled 区分是否被取消）
     */
    bool ConnectP2P(const RdTransportParams& params, const std::atomic<bool>* cancel, bool* cancelled);

private:
    RdRelayClient                                  m_relay;     //imgserver 中转连接（走中转时使用）
    std::shared_ptr<balloonp2p::P2PLinkSlot>       m_slot;      //本次连接的链路槽（走 P2P 时由帧通道共同持有）
    std::unique_ptr<balloonp2p::P2PStreamChannel>  m_channel;   //P2P 帧通道；只在 Connect 里重建，Close 只关闭不销毁，
                                                                //避免别的线程正在发送时对象被销毁
    RdRelayClientHandler*                          m_handler;   //上层回调（借用）；Connect 之前为 NULL
    std::atomic<int>                               m_path;      //当前路径，取值见 rd_transport_path
};

} // namespace remotedesktop
