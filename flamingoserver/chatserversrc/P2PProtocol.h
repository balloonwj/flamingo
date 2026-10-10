/**
 *  v2 P2P 连接信令编解码（服务端侧），P2PProtocol.h
 *
 *  两个在线客户端之间的点对点链路（方案见 docs/P2P通信方案.md，协议见 docs/协议设计-v2.md 第 18 节）。
 *  信令走 chatserver 主连接上的 cmd_p2p_* 2301~2306，链路数据走客户端之间的 UDP 直连或 imgserver 中转。
 *  本模块负责：
 *    - 解析客户端发来的报到（2301）、信令（2303）、结果上报（2304）、结束（2305）与联调建会话（2306）；
 *    - 构造回给客户端的应答，以及会话推送（2302）、信令转发（2303）与结束推送（2305）。
 *
 *  字段编码沿用仓库既有惯例：userid 这类 64 位 ID 以十进制字符串编码传输（客户端若按 IEEE754 双精度解析，
 *  超过 2^53 的整数会丢精度），读取时字符串与整数两种写法都接受；端口、超时、耗时这类数量以 JSON 整数编码。
 *  2303 的 data 对服务端不透明：只校验它是 JSON 对象且不超长，转发时原样带上。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp 与标准库，不触及网络、数据库与日志，便于单元测试，见
 *  tests/test_p2pprotocol.cpp（Suite P2PP）。会话表、按企业开关与令牌签发在 P2PSessionManager（第 2b 步），
 *  鉴权与推送在会话层（第 2c 步）。
 *
 *  典型用法（ChatSession）：
 *      P2PSignalRequest req;
 *      int code = P2PProtocol::parseSignalRequest(body, req);
 *      if (code == error_code_ok)
 *          sendToConnection(peer, cmd_p2p_signal, P2PProtocol::buildSignalForward(req.m_sessionId, myUid, req.m_data));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "Protocol.h"   //p2p_role / p2p_path / p2p_close_reason 与 error_code_p2p_*

//P2P 协议版本号：客户端在 2301 里报到时带上，服务端只接受这一个值，不一致时应答 906
const int kP2PProtocolVersion = 1;

//P2P 会话号的最大字节数。会话号形如 p2p-<uid>-<uid>-<毫秒>-<序号>，两个 64 位 userid 各至多 20 位、
//毫秒时间戳 13 位、序号至多 10 位，加上前缀与分隔符约 70 字节，取 96 留余量
const size_t kP2PSessionIdMaxBytes = 96;

//2303 的 data 序列化为紧凑 JSON 后的最大字节数，超过时应答 905。data 里是一把 X25519 公钥（Base64 44 字节）
//与本机的候选地址（每个约 80 字节，多网卡的机器至多十几个），正常不到 2 KB，取 4 KB 留出余量
const size_t kP2PSignalDataMaxBytes = 4096;

//2301 里 purposes（本端已登记业务方的用途名）最多几个、每个用途名最多几个字节（第 5 期起）。用途名的长度上限与
//客户端 FlamingoP2PApi.h 的 kMaxPurposeChars 相同；个数按将来可能接入的业务（文件夹、文件共享、通话、远程桌面、
//屏幕共享等）留出余量
const size_t kP2PMaxPurposes = 16;
const size_t kP2PPurposeMaxBytes = 31;

//2304 里 pair（选定的地址对，只供日志）的最大字节数
const size_t kP2PPairTextMaxBytes = 128;

//2304 里 nat（NAT 判断结果，只供日志）序列化后的最大字节数
const size_t kP2PNatTextMaxBytes = 512;

//地址探测服务器的一个地址，2301 应答的 probe 数组元素
struct P2PProbeAddress
{
    std::string m_ip;     //IP 地址（点分十进制）
    int         m_port;   //UDP 端口

    P2PProbeAddress()
        : m_port(0)
    {
    }
};

//cmd_p2p_session_notify(2302) 推送的内容
struct P2PSessionNotify
{
    std::string m_sessionId;          //P2P 会话号，非空、不超过 kP2PSessionIdMaxBytes 字节
    int         m_role;               //本方角色，取值见 p2p_role
    std::string m_purpose;            //用途：folder / fileshare / call / rd / screenshare / test
    std::string m_featureSessionId;   //业务会话号（如文件夹传输的 sessionid），客户端据此交给对应的业务方
    int64_t     m_peerUid;            //对端 userid；JSON 中以十进制字符串编码
    std::string m_peerDeviceId;       //对端设备标识
    std::string m_relayIp;            //回落中转（imgserver）的地址
    int         m_relayPort;          //回落中转的端口
    std::string m_token;              //回落中转的 RelayToken，角色 11（发起方）或 12（应答方）
    int         m_connectTimeoutMs;   //打洞超时（毫秒），超时后改走中转
    int         m_keepaliveMs;        //链路空闲时的保活间隔（毫秒）
    int         m_linkTimeoutMs;      //链路断开判定时间（毫秒）

    P2PSessionNotify()
        : m_role(0)
        , m_peerUid(0)
        , m_relayPort(0)
        , m_connectTimeoutMs(0)
        , m_keepaliveMs(0)
        , m_linkTimeoutMs(0)
    {
    }
};

//cmd_p2p_signal(2303) 客户端发来的请求
struct P2PSignalRequest
{
    std::string m_sessionId;   //P2P 会话号
    std::string m_data;        //data 对象序列化后的紧凑 JSON 文本；服务端不解析其内容，转发时原样带上
};

//cmd_p2p_result(2304) 客户端上报的建链结果；服务端只记日志与统计
struct P2PResultReport
{
    std::string m_sessionId;   //P2P 会话号
    bool        m_ok;          //是否建链成功（直连或回落中转成功都算）
    int         m_path;        //成功时选定的路径，取值见 p2p_path；失败时为 0
    int         m_elapsedMs;   //建链耗时（毫秒），缺省为 0
    std::string m_pair;        //选定的地址对（只供日志），缺省为空串
    std::string m_nat;         //NAT 判断结果对象序列化后的文本（只供日志），缺省为空串

    P2PResultReport()
        : m_ok(false)
        , m_path(0)
        , m_elapsedMs(0)
    {
    }
};

//cmd_p2p_close(2305)：客户端发来与服务端推送共用同一结构
struct P2PCloseMessage
{
    std::string m_sessionId;   //P2P 会话号
    int         m_reason;      //结束原因，取值见 p2p_close_reason
    int         m_errCode;     //附带的错误码（error_code），缺省为 0

    P2PCloseMessage()
        : m_reason(0)
        , m_errCode(0)
    {
    }
};

/**
 *  P2P 连接信令编解码，全部为静态方法。任何不合契约的输入都以返回失败的方式安全处理，不抛异常。
 */
class P2PProtocol
{
public:
    /**
     *  解析 cmd_p2p_register(2301) 请求。
     *  @param json     请求包体（UTF-8 JSON）
     *  @param version  [出参] 客户端报到的协议版本
     *  @param purposes [出参] 客户端已登记业务方的用途名（第 5 期起）；字段缺省时为空，表示不支持任何业务用途
     *  @return version 字段存在且为整数，purposes 缺省、或是至多 kP2PMaxPurposes 个非空且不超过 kP2PPurposeMaxBytes
     *          字节的字符串组成的数组时返回 true；是否为受支持的版本由调用方判断
     */
    static bool parseRegisterRequest(const std::string& json, int& version, std::vector<std::string>& purposes);

    /**
     *  构造 cmd_p2p_register(2301) 应答。
     *  @param code    结果码（error_code）
     *  @param msg     附带说明，可为空串
     *  @param enabled 本企业是否开启了 P2P
     *  @param probes  地址探测服务器地址；enabled 为 false 时传空数组
     *  @return 应答包体（紧凑 JSON）
     */
    static std::string buildRegisterResponse(int code, const std::string& msg, bool enabled,
                                             const std::vector<P2PProbeAddress>& probes);

    /**
     *  构造 cmd_p2p_session_notify(2302) 推送。
     *  @param notify 推送内容
     *  @return 推送包体（紧凑 JSON），十二个字段，peeruid 以十进制字符串写出
     */
    static std::string buildSessionNotify(const P2PSessionNotify& notify);

    /**
     *  解析 cmd_p2p_signal(2303) 请求。
     *  @param json 请求包体（UTF-8 JSON）
     *  @param req  [出参] 会话号与 data 的紧凑 JSON 文本
     *  @return error_code_ok 表示成功；报文格式不对（会话号缺失或超长、data 缺失或不是对象）返回
     *          error_code_bad_request；data 序列化后超过 kP2PSignalDataMaxBytes 返回 error_code_p2p_signal_too_large
     */
    static int parseSignalRequest(const std::string& json, P2PSignalRequest& req);

    /**
     *  构造转发给对端的 cmd_p2p_signal(2303) 包体。
     *  @param sessionId P2P 会话号
     *  @param fromUid   发送者 userid，以十进制字符串写出
     *  @param dataJson  data 的紧凑 JSON 文本（来自 parseSignalRequest），原样放进 data 字段
     *  @return 转发包体（紧凑 JSON）；dataJson 不是 JSON 对象时 data 写为空对象
     */
    static std::string buildSignalForward(const std::string& sessionId, int64_t fromUid, const std::string& dataJson);

    /**
     *  解析 cmd_p2p_result(2304) 上报。ok 必须是布尔值；ok 为真时 path 必须在 1~3，为假时不得带 path。
     *  @param json   上报包体（UTF-8 JSON）
     *  @param report [出参] 上报内容
     *  @return 合法返回 true
     */
    static bool parseResultReport(const std::string& json, P2PResultReport& report);

    /**
     *  解析 cmd_p2p_close(2305)。reason 必须在 1~5，errcode 可省略。
     *  @param json 包体（UTF-8 JSON）
     *  @param msg  [出参] 结束消息
     *  @return 合法返回 true
     */
    static bool parseClose(const std::string& json, P2PCloseMessage& msg);

    /**
     *  构造 cmd_p2p_close(2305) 推送（一方掉线时推给另一方）。
     *  @param msg 结束消息
     *  @return 包体（紧凑 JSON）
     */
    static std::string buildClose(const P2PCloseMessage& msg);

    /**
     *  解析 cmd_p2p_debug_open(2306) 请求。
     *  @param json    请求包体（UTF-8 JSON）
     *  @param peerUid [出参] 对端 userid（字符串与整数两种写法都接受）
     *  @return peeruid 存在且大于 0 时返回 true
     */
    static bool parseDebugOpenRequest(const std::string& json, int64_t& peerUid);

    /**
     *  构造 cmd_p2p_debug_open(2306) 应答。
     *  @param code      结果码（error_code）
     *  @param msg       附带说明，可为空串
     *  @param sessionId 成功时的 P2P 会话号；为空串时不写这个字段
     *  @return 应答包体（紧凑 JSON）
     */
    static std::string buildDebugOpenResponse(int code, const std::string& msg, const std::string& sessionId);

    /**
     *  构造只有结果码与说明的应答（2303、2304、2305 的应答）。
     *  @param code 结果码（error_code）
     *  @param msg  附带说明，可为空串
     *  @return 应答包体（紧凑 JSON）
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

private:
    P2PProtocol();
};
