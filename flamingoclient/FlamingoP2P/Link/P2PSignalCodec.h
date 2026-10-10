/**
 *  P2P 信令在客户端一侧的编解码（docs/协议设计-v2.md 第 18 节，第 3 期 3d）：构造 2301 / 2303 / 2304 / 2305 / 2306
 *  的请求包体，解析 2301 / 2306 的应答与 2302 / 2303 / 2305 的推送。字段名与服务端 chatserversrc/P2PProtocol 一致。
 *
 *  - 64 位 userid 在 JSON 里是十进制字符串（v2 惯例），解析时也接受整数；
 *  - 2301 应答的 probe[].ip 与 2302 的 relayip 取自服务端配置，可能是域名，这里只保留原文，由引擎解析成地址；
 *  - 2303 的 data 由两端客户端约定：{ "pubkey": "<X25519 公钥 Base64>", "candidates": [ … ] }，服务端原样转发。
 *
 *  只依赖标准库、jsoncpp 与 Ice/P2PCandidate。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../Ice/P2PCandidate.h"

namespace p2p {

//本客户端实现的 P2P 协议版本（2301 的 version）
const int kP2PClientProtocolVersion = 1;

//一个服务器地址的原文（地址可能是域名）
struct P2PServerEndpoint
{
    std::string m_host;   //地址或域名
    uint16_t    m_port;   //端口

    P2PServerEndpoint()
        : m_port(0)
    {
    }
};

//2301 的应答
struct P2PRegisterResult
{
    int                            m_code;      //错误码，0 为成功
    bool                           m_enabled;   //本企业是否开启了 P2P
    std::vector<P2PServerEndpoint> m_probes;    //地址探测服务器

    P2PRegisterResult()
        : m_code(-1)
        , m_enabled(false)
    {
    }
};

//2302 推送的会话信息
struct P2PSessionInfo
{
    std::string m_sessionId;          //P2P 会话号
    int         m_role;               //本方角色，取 p2p_role（1 发起方，2 应答方）
    std::string m_purpose;            //用途：folder / fileshare / call / rd / screenshare / test
    std::string m_featureSessionId;   //业务会话号
    int64_t     m_peerUid;            //对端 userid
    std::string m_peerDeviceId;       //对端设备标识
    P2PServerEndpoint m_relay;        //回落中转（imgserver）的地址
    std::string m_token;              //回落中转的令牌
    int64_t     m_connectTimeoutMs;   //打洞超时（毫秒）
    int64_t     m_keepaliveMs;        //保活间隔（毫秒）
    int64_t     m_linkTimeoutMs;      //链路断开判定时间（毫秒）

    P2PSessionInfo()
        : m_role(0)
        , m_peerUid(0)
        , m_connectTimeoutMs(0)
        , m_keepaliveMs(0)
        , m_linkTimeoutMs(0)
    {
    }

    //本方是否为发起方
    bool IsControlling() const;
};

//2303 转发来的对方信令
struct P2PPeerSignal
{
    std::string               m_sessionId;    //P2P 会话号
    int64_t                   m_fromUid;      //发送者 userid
    std::string               m_publicKey;    //对方公钥的 Base64 文本
    std::vector<P2PCandidate> m_candidates;   //对方的候选地址

    P2PPeerSignal()
        : m_fromUid(0)
    {
    }
};

//2305 推送的关闭消息
struct P2PCloseInfo
{
    std::string m_sessionId;   //P2P 会话号
    int         m_reason;      //关闭原因，取 p2p_close_reason
    int         m_errCode;     //附带的错误码

    P2PCloseInfo()
        : m_reason(0)
        , m_errCode(0)
    {
    }
};

/**
 *  客户端一侧的 P2P 信令编解码。只有静态函数。
 */
class P2PSignalCodec
{
public:
    /**
     *  构造 2301 报到：{ "version": kP2PClientProtocolVersion, "purposes": [ 用途名, ... ] }。
     *  @param purposes 本端已登记业务方的用途名（第 5 期起）；服务端只为两端都报到了的用途建会话，可为空
     *  @return 包体（紧凑 JSON）
     */
    static std::string BuildRegister(const std::vector<std::string>& purposes);

    /**
     *  解析 2301 的应答。
     *  @param body 包体（UTF-8 JSON）
     *  @param out  [出参] 结果；code 非 0 时 enabled 为假、probes 为空
     *  @return 包体是对象且带整数 code 时返回 true
     */
    static bool ParseRegisterResponse(const std::string& body, P2PRegisterResult& out);

    /**
     *  解析 2302 推送。
     *  @return 会话号非空、角色为 1 或 2、三个时间都为正数时返回 true
     */
    static bool ParseSessionNotify(const std::string& body, P2PSessionInfo& out);

    /**
     *  构造 2303 请求：{ "p2psessionid", "data": { "pubkey", "candidates" } }。
     *  @param sessionId  P2P 会话号
     *  @param publicKey  本方公钥的 Base64 文本
     *  @param candidates 本方的候选地址
     */
    static std::string BuildSignal(const std::string& sessionId, const std::string& publicKey,
                                   const std::vector<P2PCandidate>& candidates);

    /**
     *  解析 2303 转发：{ "p2psessionid", "fromuid", "data": { "pubkey", "candidates" } }。
     *  @return 会话号与公钥文本都非空时返回 true（候选地址可以为空）
     */
    static bool ParseSignalForward(const std::string& body, P2PPeerSignal& out);

    /**
     *  构造 2304 请求。
     *  @param sessionId  P2P 会话号
     *  @param ok         是否建链成功
     *  @param path       成功时的路径，取 P2PLinkPath；失败时忽略（不写 path 字段）
     *  @param elapsedMs  从收到 2302 到出结果的耗时（毫秒）
     *  @param pair       选定的地址对文本，如 "192.168.1.10:54429 -> 203.0.113.7:16725"；失败时可为空
     *  @param natMapping NAT 映射的判断，取 p2p_nat_mapping；未知时不写 nat 字段
     */
    static std::string BuildResult(const std::string& sessionId, bool ok, int path, int64_t elapsedMs,
                                   const std::string& pair, int natMapping);

    //2305：{ "p2psessionid", "reason", "errcode" }
    static std::string BuildClose(const std::string& sessionId, int reason, int errCode);

    /**
     *  解析 2305 推送。
     *  @return 会话号非空且 reason 为整数时返回 true（errcode 缺省为 0）
     */
    static bool ParseClose(const std::string& body, P2PCloseInfo& out);

    //2306（联调）：{ "peeruid": "<十进制>" }
    static std::string BuildDebugOpen(int64_t peerUid);
};

} // namespace p2p
