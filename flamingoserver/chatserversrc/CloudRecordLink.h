/**
 *  chatserver 到 imgserver 的云端录制控制连接，CloudRecordLink.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 第 12.6.2 节）：云端录制在 imgserver 上进行，开始、暂停、继续、停止与删除文件都由
 *  chatserver 决定，因此 chatserver 主动连 imgserver 的中转端口（relayport），在这条长连接上收发 JSON 控制消息
 *  （格式见 utils/CloudRecordCtlMsg.h）。连接过程：
 *      TCP 连接 → 协议加密三帧握手（本端为客户端侧，net::PeerClientHandshake，验 imgserver 的身份签名）
 *      → cmd_cloudrec_ctl_hello 报到（令牌角色 role_server_control，有效期 60 秒，每次重连重签）→ 收到 code 0 的应答
 *      → 之后双方以 cmd_cloudrec_ctl_data 互发消息。
 *  本类用一条专用线程与非阻塞套接字（配合 select 等待）实现，不经 muduo 的 TcpClient（它在 Windows 下有缺陷，见方案 12.6.2）：
 *    - 断开或连不上时按 1、2、5、10、30 秒退避重连，一直重试到 stop；
 *    - 每 kPingIntervalMs 发一次 ping，超过 kIdleTimeoutMs 收不到任何消息视为连接已失效，断开重连；
 *    - 所有回调都在连接线程上调用，实现方（CloudRecordManager）自行保证线程安全，不要在回调里调用 stop。
 *  send 可从任意线程调用：加密与写套接字在同一把锁内完成，保证「加密的先后」等于「写到线上的先后」。
 *
 *  典型用法（chatserver 的 main.cpp）：
 *      CloudRecordLinkConfig cfg;
 *      cfg.m_host = imgserverhost;  cfg.m_port = relayport;  cfg.m_tokenKey = relaytokenkey;
 *      cfg.m_encrypt = net::cryptoEncryptionEnabled();
 *      Singleton<CloudRecordLink>::Instance().setListener(&Singleton<CloudRecordManager>::Instance());
 *      Singleton<CloudRecordLink>::Instance().start(cfg);
 *      ...退出时：Singleton<CloudRecordLink>::Instance().stop();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "../base/Platform.h"

namespace net {
class PeerClientHandshake;
}

//控制连接的事件回报；全部在连接线程上调用
class ICloudRecordLinkListener
{
public:
    virtual ~ICloudRecordLinkListener() {}

    //控制连接已建立（握手与报到都成功），此后 send 可用
    virtual void onCloudRecordLinkUp() = 0;

    //控制连接断开（之前已建立过）；此后 send 返回 false，直到下一次 onCloudRecordLinkUp
    virtual void onCloudRecordLinkDown() = 0;

    /**
     *  收到 imgserver 的一条控制消息。
     *  @param body JSON 文本，格式见 utils/CloudRecordCtlMsg.h
     */
    virtual void onCloudRecordLinkMessage(const std::string& body) = 0;
};

//控制连接的配置
struct CloudRecordLinkConfig
{
    std::string m_host;         //imgserver 的地址（主机名或 IP，IPv4 / IPv6 均可）
    int         m_port;         //imgserver 的中转端口（relaylistenport）
    std::string m_tokenKey;     //relay 令牌的 HMAC 密钥（relaytokenkey），与 imgserver 一致
    bool        m_encrypt;      //是否做协议加密握手；须与 imgserver 的 encryption 一致
    std::string m_peerPubKey;   //imgserver 的身份公钥（32 字节原始字节）；为空时用本进程的身份公钥（两台服务共用 server_key）

    CloudRecordLinkConfig();
};

//chatserver 到 imgserver 的云端录制控制连接；全局单例经 Singleton<CloudRecordLink> 访问，测试里可直接构造
class CloudRecordLink final
{
public:
    //发 ping 的间隔（毫秒）
    static const int kPingIntervalMs = 30000;
    //超过这么久收不到任何消息即视为连接失效（毫秒）
    static const int kIdleTimeoutMs = 90000;
    //TCP 连接、握手与报到各步等待应答的上限（毫秒）
    static const int kStepTimeoutMs = 10000;
    //等待网络事件时每次 select 的时长（毫秒）：也是 stop 生效的最长延迟
    static const int kPollSliceMs = 200;

    CloudRecordLink();
    ~CloudRecordLink();

    CloudRecordLink(const CloudRecordLink&) = delete;
    CloudRecordLink& operator=(const CloudRecordLink&) = delete;

    //登记事件回报（不拥有）；须在 start 之前调用
    void setListener(ICloudRecordLinkListener* listener);

    /**
     *  启动连接线程。地址或端口为空时不启动（云端录制不可用），返回 false。
     *  @param config 配置
     *  @return 线程已启动返回 true；重复调用返回 false
     */
    bool start(const CloudRecordLinkConfig& config);

    //停止连接线程并断开（最多等 kPollSliceMs 量级）；可重复调用
    void stop();

    //控制连接当前是否可用
    bool isUp() const;

    /**
     *  发一条控制消息给 imgserver（任意线程）。
     *  @param body JSON 文本
     *  @return 连接可用且已写进套接字返回 true；连接不可用或写失败返回 false（调用方决定是否稍后重发）
     */
    bool send(const std::string& body);

    /**
     *  重连退避的等待时长（纯函数，便于测试）。
     *  @param failures 连续失败的次数，从 1 开始
     *  @return 毫秒：1 次 1000，2 次 2000，3 次 5000，4 次 10000，之后一律 30000
     */
    static int reconnectDelayMs(int failures);

private:
    void threadProc();
    //连接、握手、报到；成功时 m_sock 与 m_hs 就绪
    bool establish(std::string& err);
    //连接建立后的收发循环，返回即表示连接已断开或要求停止
    void serve();
    //关闭套接字并清空握手状态（持 m_sendMutex）
    void closeSocket();
    //按当前加密状态组帧并写出；调用方已持 m_sendMutex
    bool sendFrameLocked(uint16_t cmd, uint32_t seq, const std::string& body, bool forcePlain);
    //写出全部字节，期间按 kPollSliceMs 等待可写；超过 kStepTimeoutMs 或要求停止时返回 false
    bool sendAll(const std::string& data);
    //读到一帧为止（解出包体，含解密与解压）；timeoutMs 内没有完整的帧返回 false 且 timedOut 为 true
    bool readFrame(uint16_t& cmd, std::string& body, int timeoutMs, bool& timedOut, std::string& err);
    //可中断的等待
    void sleepMs(int ms);

    ICloudRecordLinkListener*                 m_listener;    //事件回报（不拥有）
    CloudRecordLinkConfig                     m_config;      //配置（start 时保存，之后只读）
    std::unique_ptr<std::thread>              m_thread;      //连接线程
    std::atomic<bool>                         m_stop;        //要求停止
    std::atomic<bool>                         m_up;          //连接是否可用（报到成功后为 true）
    mutable std::mutex                        m_sendMutex;   //保护下面两项与发送顺序
    SOCKET                                    m_sock;        //当前套接字；没有连接时为 INVALID_SOCKET
    std::unique_ptr<net::PeerClientHandshake> m_hs;          //当前连接的握手与加解密状态；没有连接时为空
    std::string                               m_inbuf;       //收到、还没解出完整帧的字节（只在连接线程上读写）
    std::mutex                                m_sleepMutex;  //配合 m_sleepCv 实现可中断的等待
    std::condition_variable                   m_sleepCv;     //stop 时唤醒退避等待
};
