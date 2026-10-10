/**
 *  P2P 引擎（docs/P2P通信方案.md 8.2 节，第 3 期 3d）：一个 IO 线程驱动全部 P2P 会话。
 *
 *  - 登录后发 2301 报到（带上宿主已登记业务方的用途，第 5 期起），记下本企业是否开启与地址探测服务器（解析成地址）；
 *    已登录期间业务方登记或注销了用途，由宿主调用 OnPurposesChanged 重新报到；
 *  - 收到 2302：给会话新开一个 UDP socket（随机端口），枚举网卡得到本机候选，交给 P2PLinkSession 开始建链；
 *    2303 转给对应会话；2305 推送交给对应会话收尾；各应答只记日志；
 *  - 会话就绪时按用途找业务方，交出 IP2PLink；失败、断开时回调业务方；没有业务方登记的用途（如联调的 test）照样建链；
 *  - 会话打洞失败请求回落中转时（第 3e 步起），给它建一条 P2PRelayConn（连接线程负责连接、握手与报到），连接结果、
 *    中转上收到的报文与连接断开都由 IO 线程交给会话；中转 socket 与 UDP socket 一起 select；
 *  - 联调用的强制中转：环境变量 FLAMINGO_P2P_FORCE_RELAY 为 1（Start 时读取）或调用 SetForceRelay，之后新建的会话
 *    不打洞、直接走中转；
 *  - IO 线程：select 等待各会话的 socket、一个本机回环的唤醒 socket 与最近的定时器（最长等 kP2PEngineMaxWaitMs），
 *    醒来后依次处理投递来的任务、收包、驱动定时器、清理已结束的会话；
 *  - 退出登录时关闭全部会话（经 chatserver 发 2305，原因为本方退出）；停止时同样先关闭全部会话再结束线程。
 *  业务方经 IP2PLink 的调用可以在任意线程上进行（会话内部加锁）。
 *
 *  不是单例：模块对象持有一个（进程内唯一），单测里可以在同一进程开两个引擎，在本机用真实 UDP 互连（[p2pio]）。
 *  头文件不包含 winsock 的头文件。
 *
 *  典型用法（P2PModule）：
 *      m_engine.reset(new P2PEngine(m_engineHost));
 *      m_engine->Start();
 *      m_engine->OnLoggedIn();                                 //登录成功后
 *      m_engine->OnServerFrame(cmd, seq, body);                //收到 2300 段的帧
 *      m_engine->Stop();                                       //模块关闭时
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "../FlamingoP2PApi.h"

namespace p2p {

//IO 线程一次最长等待的时间（毫秒）：业务方 Close 之后最迟这么久会被清理
const int64_t kP2PEngineMaxWaitMs = 100;

/**
 *  引擎的宿主：模块用宿主接口与服务实现，单测里用假的。
 *  析构函数受保护：引擎只借用。
 */
class IP2PEngineHost
{
public:
    /**
     *  经 chatserver 发一个 2300 段命令。可能在 IO 线程或业务方的线程上调用，实现须线程安全。
     *  @param cmd  命令号
     *  @param body 包体（UTF-8 JSON）
     */
    virtual void SendCommand(uint16_t cmd, const std::string& body) = 0;

    /**
     *  按用途找业务方。在 IO 线程上调用。
     *  @param purpose 用途名
     *  @return 已登记的业务方；没有登记时返回 NULL
     */
    virtual IP2PLinkHandler* FindHandler(const std::string& purpose) = 0;

    /**
     *  取服务端身份公钥（连中转时做协议加密握手用）。在 IO 线程上调用。
     *  @return net::IDENTITY_PUBLICKEY_BYTES 字节的公钥（只需在本次调用之后短时间内有效，引擎立即复制）；
     *          服务端没有配置公钥时返回 NULL，中转连接不握手、全程明文
     */
    virtual const unsigned char* GetServerPublicKey() = 0;

    /**
     *  列出已登记业务方的用途名，2301 报到时带给服务端（第 5 期起）：服务端只为两端都报到了的用途建会话，避免一端
     *  有业务方走 P2P、另一端没有而走中转，两端对不上。在 IO 线程上调用。
     *  @param purposes [出参] 用途名；调用前已为空
     */
    virtual void ListPurposes(std::vector<std::string>& purposes) = 0;

protected:
    ~IP2PEngineHost()
    {
    }
};

/**
 *  P2P 引擎。除特别说明外，公开方法都可以在任意线程上调用（投递给 IO 线程执行）。
 */
class P2PEngine
{
public:
    /**
     *  @param host 宿主（借用，须比本对象活得久）
     */
    explicit P2PEngine(IP2PEngineHost& host);
    ~P2PEngine();

    /**
     *  启动：初始化 winsock、建唤醒 socket、启动 IO 线程。已启动时直接返回 true。
     *  @return 成功返回 true
     */
    bool Start();

    //停止：关闭全部会话（发 2305），结束 IO 线程。可重复调用。不得在 IO 线程上调用（如业务方回调里）
    void Stop();

    //登录成功：发 2301 报到
    void OnLoggedIn();

    //即将退出登录：关闭全部会话（发 2305，原因为本方退出），忘掉报到结果
    void OnLoggingOut();

    //业务方登记或注销了用途之后调用：已登录时重新发 2301（带上新的用途列表），未登录时无效果
    void OnPurposesChanged();

    /**
     *  收到一个 2300 段的帧。
     *  @param cmd  命令号
     *  @param seq  帧序号（应答为请求的 seq，推送为 0）
     *  @param body 包体（复制后投递）
     */
    void OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    /**
     *  联调：请服务端直接与指定用户建一个用途为 test 的会话（发 2306，服务端 p2pdebugopen=1 时才受理）。
     *  @param peerUid 对方 userid
     */
    void DebugOpen(int64_t peerUid);

    /**
     *  联调用：之后新建的会话是否不打洞、直接走中转（会话的 2302 须带中转参数）。
     *  @param force 是否强制中转
     */
    void SetForceRelay(bool force);

    //是否已收到 2301 的应答且本企业开启了 P2P
    bool IsEnabled() const;

    //当前的会话个数（含正在建链与已就绪的）
    size_t SessionCount() const;

private:
    P2PEngine(const P2PEngine&);
    P2PEngine& operator=(const P2PEngine&);

    struct Impl;
    std::unique_ptr<Impl> m_impl;   //实现（含全部 winsock 相关的状态）
};

} // namespace p2p
