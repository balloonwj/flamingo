/**
 *  P2P 信令处理（服务端侧），P2PSignalService.h
 *
 *  把一帧 2300 段信令「解析请求 → 调用会话管理器 → 查企业开关 → 决定给哪些连接发什么帧」的全过程放在这里，
 *  会话层（ChatSession）只负责把收到的帧交进来、把产出的帧按连接发出去、按返回的结果码记日志。这样分发逻辑
 *  可以脱离网络做单元测试（tests/test_p2psignalservice.cpp，Suite P2PSV），做法与文件共享的 FileSharePolicy 相同。
 *
 *  企业开关经 IP2PEnterpriseGate 查询：会话层的实现转给 FeatureSwitchManager（第 2d 步），测试里注入假的。
 *
 *  典型用法（ChatSession）：
 *      P2PSignalService svc(Singleton<P2PSessionManager>::Instance(), gate);
 *      std::vector<P2POutFrame> out;
 *      int code = svc.handle(cmd, ctx, body, out);
 *      for (...) 按 out[i].m_conn 找到会话对象，send(out[i].m_cmd, out[i].m_seq, out[i].m_body)
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "P2PSessionManager.h"

//一帧要发出的信令：应答发回请求者自己的连接（seq 为请求的 seq），推送发给对端连接（seq 为 0）
struct P2POutFrame
{
    P2PConnKey  m_conn;   //发往哪条连接
    uint16_t    m_cmd;    //命令号
    uint32_t    m_seq;    //帧序号
    std::string m_body;   //包体（紧凑 JSON）

    P2POutFrame()
        : m_conn(0)
        , m_cmd(0)
        , m_seq(0)
    {
    }
};

//一帧请求的上下文：来自哪条连接、哪个用户
struct P2PRequestContext
{
    P2PConnKey  m_conn;       //请求者的连接
    int64_t     m_userId;     //请求者 userid
    std::string m_deviceId;   //请求者的设备标识（2301 登记时使用）
    uint32_t    m_seq;        //请求帧的 seq，应答原样带回
    int64_t     m_nowMs;      //当前时间（毫秒），建会话时用于会话号与令牌有效期

    P2PRequestContext()
        : m_conn(0)
        , m_userId(0)
        , m_seq(0)
        , m_nowMs(0)
    {
    }
};

/**
 *  企业开关查询：某个用户所在企业是否开启了 P2P。由会话层实现（转给 FeatureSwitchManager），测试里注入假的。
 *  析构函数受保护：P2PSignalService 只借用，不负责销毁。
 */
class IP2PEnterpriseGate
{
public:
    /**
     *  查询某个用户所在企业是否开启了 P2P。
     *  @param userId 用户 userid
     *  @return 开启返回 true
     */
    virtual bool isP2PEnabledForUser(int64_t userId) = 0;

protected:
    ~IP2PEnterpriseGate()
    {
    }
};

/**
 *  P2P 信令处理：一帧请求进来，产出要发出的若干帧。无状态，可每次处理时临时构造。
 */
class P2PSignalService
{
public:
    /**
     *  @param manager 会话管理器（借用，不持有）
     *  @param gate    企业开关查询（借用，不持有）
     */
    P2PSignalService(P2PSessionManager& manager, IP2PEnterpriseGate& gate);

    /**
     *  处理一帧 2300 段请求。任何情况下都至少产出一帧回给请求者的应答。
     *  @param cmd  命令号（cmd_p2p_register / signal / result / close / debug_open；其余 2300 段的命令按报文非法应答）
     *  @param ctx  请求上下文
     *  @param body 请求包体（UTF-8 JSON）
     *  @param out  [出参] 追加要发出的帧
     *  @return 本次请求的结果码（error_code），供会话层记日志
     */
    int handle(uint16_t cmd, const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out);

    /**
     *  连接断开：移除登记、结束它参与的全部会话，给仍在线的每个对端推 2305（原因 p2p_close_peer_offline，
     *  错误码 error_code_p2p_peer_offline）。
     *  @param conn 断开的连接
     *  @param out  [出参] 追加要发出的帧；没有会话时不追加
     */
    void onDisconnect(P2PConnKey conn, std::vector<P2POutFrame>& out);

    /**
     *  为一个业务会话在两个用户之间建 P2P 会话（第 5 期起，业务在下发自己的中转参数之前调用）：两端各取最近一次
     *  报到的连接，查两端所在企业的开关，成功时追加推给双方的 2302。
     *  @param purpose          用途（folder / fileshare / call / rd / screenshare）
     *  @param featureSessionId 业务会话号，原样写进 2302
     *  @param userA            发起方 userid（负责选定地址对）
     *  @param userB            应答方 userid
     *  @param nowMs            当前时间（毫秒）
     *  @param sessionIdOut     [出参] 成功时的 P2P 会话号；失败时清空
     *  @param out              [出参] 成功时追加推给双方的 2302；失败时不追加
     *  @return error_code_ok；任一方没有已报到的连接（老客户端、手机端或不在线）返回 error_code_p2p_peer_offline；
     *          其余同 P2PSessionManager::tryCreate。失败时调用方照旧只走业务原有的中转
     */
    int createForFeature(const std::string& purpose, const std::string& featureSessionId, int64_t userA, int64_t userB,
                         int64_t nowMs, std::string& sessionIdOut, std::vector<P2POutFrame>& out);

    /**
     *  预检两个用户之间此刻能否为某个用途建 P2P 会话（2026-10-04 起）：与 createForFeature 取同样的连接、查同样的
     *  企业开关与用途，但不建会话、不推任何帧。单聊单文件直传在发起邀请时调用，建不了就回 622 让发送端改为上传服务器。
     *  @param purpose 用途（如 file）
     *  @param userA   发起方 userid
     *  @param userB   应答方 userid
     *  @return 与 createForFeature 相同的返回码；error_code_ok 表示此刻能建
     */
    int checkForFeature(const std::string& purpose, int64_t userA, int64_t userB);

    /**
     *  为一个业务会话在两条指定的连接之间建 P2P 会话（第 6 期起）：业务自己记着参与的是哪条连接时用它，例如文件共享的
     *  分享者一端是提供该共享的那条连接，不一定是该用户最近一次报到的连接。成功时追加推给双方的 2302。
     *  @param purpose          用途（如 fileshare）
     *  @param featureSessionId 业务会话号，原样写进 2302
     *  @param connA            发起方连接（负责选定地址对）
     *  @param userA            发起方 userid（查企业开关）
     *  @param connB            应答方连接
     *  @param userB            应答方 userid
     *  @param nowMs            当前时间（毫秒）
     *  @param sessionIdOut     [出参] 成功时的 P2P 会话号；失败时清空
     *  @param out              [出参] 成功时追加推给双方的 2302；失败时不追加
     *  @return error_code_ok；任一条连接没有报到过返回 error_code_p2p_peer_offline；其余同 P2PSessionManager::tryCreate。
     *          失败时调用方照旧只走业务原有的中转
     */
    int createForConnections(const std::string& purpose, const std::string& featureSessionId, P2PConnKey connA,
                             int64_t userA, P2PConnKey connB, int64_t userB, int64_t nowMs, std::string& sessionIdOut,
                             std::vector<P2POutFrame>& out);

private:
    //以下各处理函数的参数与 handle 相同，返回本次请求的结果码
    int handleRegister(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out);
    int handleSignal(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out);
    int handleResult(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out);
    int handleClose(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out);
    int handleDebugOpen(const P2PRequestContext& ctx, const std::string& body, std::vector<P2POutFrame>& out);

    //追加一帧
    static void append(std::vector<P2POutFrame>& out, P2PConnKey conn, uint16_t cmd, uint32_t seq, const std::string& body);

private:
    P2PSessionManager&  m_manager;   //会话管理器（借用）
    IP2PEnterpriseGate& m_gate;      //企业开关查询（借用）
};
