/**
 *  P2P 会话管理（服务端侧），P2PSessionManager.h
 *
 *  两个在线客户端之间的点对点链路由服务端在业务会话建立之后创建（方案见 docs/P2P通信方案.md 4.3，协议见
 *  docs/协议设计-v2.md 第 18 节）。P2P 会话只存在于内存：随任一端主动结束（2305）或 chatserver 连接断开而结束。
 *
 *  本类负责：
 *    - 能力登记：哪些连接已用 cmd_p2p_register(2301) 报到、报到的是哪个用户与设备；
 *    - 建会话（tryCreate / debugOpen）：检查双方都已报到、双方企业都开启、每人会话数上限，生成会话号
 *      "p2p-<uidA>-<uidB>-<毫秒>-<序号>"，签发两枚回落中转令牌（发起方 role_p2p_controlling(11)、
 *      应答方 role_p2p_controlled(12)），并给出推给双方的 cmd_p2p_session_notify(2302) 内容；
 *    - 信令路由：2303 只在会话双方绑定的那两条连接之间转发；2304 只核对成员身份；
 *    - 结束与收尾：2305 结束一个会话；连接断开时结束它参与的全部会话，把对端连接交还调用方推送 2305。
 *
 *  企业开关由调用方在调用时从 FeatureSwitchManager 查好、作为参数传入（本类不依赖它，开关在运行中改变时
 *  也总是用调用当时的值）；业务路径上企业关闭时调用方不建会话、业务照旧走原来的中转。
 *
 *  纯逻辑：不触及网络、数据库与日志，时间由调用方传入，便于单元测试（tests/test_p2psessionmanager.cpp，
 *  Suite P2PS）。全部公开方法线程安全，内部一把互斥量；configure 只在服务启动阶段调用。
 *
 *  典型用法（会话层）：
 *      P2PSessionManager& mgr = Singleton<P2PSessionManager>::Instance();
 *      P2PCreateResult r;
 *      if (mgr.tryCreate("folder", folderSessionId, senderConn, receiverConn, senderOn, receiverOn, nowMs, r) == error_code_ok)
 *      {
 *          ...（向 r.m_connA 推 buildSessionNotify(r.m_notifyA)，向 r.m_connB 推 buildSessionNotify(r.m_notifyB)）
 *      }
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "P2PProtocol.h"   //P2PSessionNotify、P2PProbeAddress、kP2PProtocolVersion

//会话所属 chatserver 连接的不透明标识。会话层传一个能唯一标识连接的整数（如会话对象地址），
//本类只用它做相等比较，不解引用
typedef uint64_t P2PConnKey;

//运行参数，main.cpp 读取 etc/chatserver.conf 后经 configure 设置一次（配置项见方案 9.3）
struct P2PSessionConfig
{
    std::string                  m_relayTokenKey;      //回落中转令牌的 HMAC 密钥，与 imgserver 的 relaytokenkey 一致
    std::string                  m_relayHost;          //imgserver 中转对客户端公布的地址
    int                          m_relayPort;          //imgserver 中转端口
    std::vector<P2PProbeAddress> m_probes;             //地址探测服务器地址（2301 应答里下发），第 2e 步起有值
    int                          m_connectTimeoutMs;   //打洞超时（毫秒），经 2302 下发
    int                          m_keepaliveMs;        //链路空闲时的保活间隔（毫秒），经 2302 下发
    int                          m_linkTimeoutMs;      //链路断开判定时间（毫秒），经 2302 下发
    int                          m_maxPerUser;         //每人同时进行的 P2P 会话上限，超过时不建会话
    bool                         m_debugOpen;          //是否受理联调用的 cmd_p2p_debug_open(2306)

    P2PSessionConfig();
};

//建会话的结果：两条连接与推给它们的 2302 内容。A 为发起方（role 1），B 为应答方（role 2）
struct P2PCreateResult
{
    std::string      m_sessionId;   //新会话的会话号
    P2PConnKey       m_connA;       //发起方连接
    P2PConnKey       m_connB;       //应答方连接
    P2PSessionNotify m_notifyA;     //推给发起方的 2302 内容
    P2PSessionNotify m_notifyB;     //推给应答方的 2302 内容

    P2PCreateResult()
        : m_connA(0)
        , m_connB(0)
    {
    }
};

//信令路由的结果：转发给哪条连接、发送者是谁
struct P2PRoute
{
    P2PConnKey m_targetConn;   //对端连接
    int64_t    m_fromUserId;   //发送者 userid，写进转发包体的 fromuid

    P2PRoute()
        : m_targetConn(0)
        , m_fromUserId(0)
    {
    }
};

//一个已结束的会话：调用方据此给仍在线的对端推送 2305
struct P2PEndedSession
{
    std::string m_sessionId;    //会话号
    P2PConnKey  m_peerConn;     //对端连接（主动结束时为另一方，连接断开时为未断开的那一方）
    int64_t     m_peerUserId;   //对端 userid

    P2PEndedSession()
        : m_peerConn(0)
        , m_peerUserId(0)
    {
    }
};

/**
 *  P2P 会话管理：能力登记、建会话、信令路由与结束收尾。exe 内经 Singleton<P2PSessionManager>::Instance() 使用。
 */
class P2PSessionManager
{
public:
    //默认的打洞超时、保活间隔与断开判定时间（毫秒），取自方案 6.5 的暂定值，第 0 期实测后可经配置调整
    static const int kDefaultConnectTimeoutMs = 3000;
    static const int kDefaultKeepaliveMs = 5000;
    static const int kDefaultLinkTimeoutMs = 15000;
    //默认的每人同时进行的 P2P 会话上限（方案 6.5 暂定值）
    static const int kDefaultMaxPerUser = 16;
    //回落中转令牌的有效期（毫秒）：与其它中转令牌一样为 5 分钟，令牌只在报到时校验一次
    static const int64_t kRelayTokenTtlMs = 5 * 60 * 1000;

    P2PSessionManager();

    /**
     *  设置运行参数。只在服务启动阶段调用一次。
     *  @param config 运行参数
     */
    void configure(const P2PSessionConfig& config);

    /**
     *  取当前运行参数的副本（2301 应答里的地址探测服务器地址取自这里）。
     *  @return 运行参数
     */
    P2PSessionConfig getConfig() const;

    /**
     *  登记一条连接的 P2P 能力（处理 2301）。同一连接再次报到时以最后一次为准（客户端登记或注销业务方后会重新报到）。
     *  @param conn     连接标识
     *  @param userId   该连接登录的 userid
     *  @param deviceId 该连接上报的设备标识
     *  @param version  客户端报到的协议版本
     *  @param purposes 该连接已登记业务方的用途名（第 5 期起）；为空表示不支持任何业务用途，只能建联调会话
     *  @return error_code_ok；版本不受支持时返回 error_code_p2p_version_unsupported，且不登记
     */
    int registerConnection(P2PConnKey conn, int64_t userId, const std::string& deviceId, int version,
                           const std::vector<std::string>& purposes);

    /**
     *  连接是否已报到。
     *  @param conn 连接标识
     *  @return 已报到返回 true
     */
    bool isRegistered(P2PConnKey conn) const;

    /**
     *  连接断开：移除登记，并结束它参与的全部会话。
     *  @param conn 连接标识
     *  @return 被结束的会话，每项带对端连接，供调用方推 2305（reason 为 p2p_close_peer_offline）；没有时为空
     */
    std::vector<P2PEndedSession> unregisterConnection(P2PConnKey conn);

    /**
     *  在两条连接之间建一个 P2P 会话（业务会话建立后由业务管理器调用）。
     *  @param purpose          用途（folder / fileshare / call / rd / screenshare / test）
     *  @param featureSessionId 业务会话号，原样写进 2302
     *  @param connA            发起方连接（负责选定地址对）
     *  @param connB            应答方连接
     *  @param enabledA         发起方所在企业是否开启了 P2P（调用方从 FeatureSwitchManager 查得）
     *  @param enabledB         应答方所在企业是否开启了 P2P
     *  @param nowMs            当前时间（毫秒），用于会话号与令牌有效期
     *  @param out              [出参] 成功时的会话号与推给双方的内容
     *  @return error_code_ok；任一方没报到返回 error_code_p2p_peer_offline；任一方企业关闭返回 error_code_p2p_disabled；
     *          任一方没有报到这个用途返回 error_code_p2p_purpose_unsupported（否则一端走 P2P、另一端走中转，两端对不上）；
     *          两端是同一条连接返回 error_code_bad_request；任一方会话数已达上限返回 error_code_p2p_too_many。失败时不建会话
     */
    int tryCreate(const std::string& purpose, const std::string& featureSessionId, P2PConnKey connA, P2PConnKey connB,
                  bool enabledA, bool enabledB, int64_t nowMs, P2PCreateResult& out);

    /**
     *  按 tryCreate 的全部规则检查两条连接之间能否建这个用途的会话，但不建会话、不占名额（2026-10-04 起）。
     *  单聊单文件直传在发起邀请时用它预检：建不了就让发送端立即改为上传服务器，而不是等对方点了接收才发现。
     *  @param purpose  用途（如 file）
     *  @param connA    发起方连接
     *  @param connB    应答方连接
     *  @param enabledA 发起方所在企业是否开启了 P2P
     *  @param enabledB 应答方所在企业是否开启了 P2P
     *  @return 与 tryCreate 相同的返回码；error_code_ok 表示此刻能建
     */
    int checkCreate(const std::string& purpose, P2PConnKey connA, P2PConnKey connB, bool enabledA, bool enabledB) const;

    /**
     *  取某个用户最近一次报到的连接（第 5 期起）：业务会话按用户记录参与者，为它建 P2P 会话时据此找到两端的连接。
     *  同一用户多端在线时取最近一次报到的那条（与 debugOpen 找对端的规则相同）。
     *  @param userId 用户 userid
     *  @return 连接标识；该用户没有已报到的连接时返回 0
     */
    P2PConnKey latestConnectionOf(int64_t userId) const;

    /**
     *  联调建会话（处理 2306）：发起者为发起方，对端用户最近一次报到的那条连接为应答方，用途为 test。
     *  @param fromConn    发起者连接，须已报到
     *  @param peerUserId  对端 userid
     *  @param selfEnabled 发起者所在企业是否开启了 P2P
     *  @param peerEnabled 对端所在企业是否开启了 P2P
     *  @param nowMs       当前时间（毫秒）
     *  @param out         [出参] 成功时的会话号与推给双方的内容
     *  @return error_code_ok；配置未开启 2306 返回 error_code_p2p_debug_disabled；发起者没报到返回 error_code_bad_request；
     *          对端没有已报到的连接返回 error_code_p2p_peer_offline；其余同 tryCreate
     */
    int debugOpen(P2PConnKey fromConn, int64_t peerUserId, bool selfEnabled, bool peerEnabled, int64_t nowMs,
                  P2PCreateResult& out);

    /**
     *  为一条 2303 找转发目标。
     *  @param sessionId 会话号
     *  @param fromConn  发送者连接
     *  @param out       [出参] 对端连接与发送者 userid
     *  @return error_code_ok；会话不存在返回 error_code_p2p_session_not_exist；发送者不是该会话绑定的连接返回 error_code_p2p_not_member
     */
    int routeSignal(const std::string& sessionId, P2PConnKey fromConn, P2PRoute& out) const;

    /**
     *  核对发送者是不是会话成员（处理 2304 时使用，不改变会话状态）。
     *  @param sessionId 会话号
     *  @param fromConn  发送者连接
     *  @return error_code_ok；会话不存在或不是成员时返回相应的 900 段错误码
     */
    int checkMember(const std::string& sessionId, P2PConnKey fromConn) const;

    /**
     *  结束一个会话（处理客户端发来的 2305）。
     *  @param sessionId 会话号
     *  @param fromConn  发送者连接，须为会话成员
     *  @param out       [出参] 被结束的会话与对端连接，供调用方把 2305 转给对端
     *  @return error_code_ok；会话不存在或不是成员时返回相应的 900 段错误码，会话不变
     */
    int close(const std::string& sessionId, P2PConnKey fromConn, P2PEndedSession& out);

    /**
     *  当前会话总数。
     *  @return 会话数
     */
    size_t sessionCount() const;

    /**
     *  某个用户当前参与的会话数（作为任一方都算）。
     *  @param userId 用户 userid
     *  @return 会话数
     */
    size_t sessionCountOfUser(int64_t userId) const;

private:
    //一条已报到的连接
    struct Registration
    {
        int64_t     m_userId;     //该连接登录的 userid
        std::string m_deviceId;   //该连接上报的设备标识
        uint64_t    m_order;      //报到顺序号：同一用户有多条连接时，debugOpen 取最近报到的那条
        std::set<std::string> m_purposes;   //该连接报到的用途名（客户端已登记业务方的那些），建业务会话时两端都要有

        Registration()
            : m_userId(0)
            , m_order(0)
        {
        }
    };

    //一个 P2P 会话：双方各自绑定的连接与用户
    struct Session
    {
        P2PConnKey m_connA;   //发起方连接
        P2PConnKey m_connB;   //应答方连接
        int64_t    m_userA;   //发起方 userid
        int64_t    m_userB;   //应答方 userid

        Session()
            : m_connA(0)
            , m_connB(0)
            , m_userA(0)
            , m_userB(0)
        {
        }
    };

    //建会话的内部实现，调用方已持锁。参数与返回值同 tryCreate
    int createLocked(const std::string& purpose, const std::string& featureSessionId, P2PConnKey connA, P2PConnKey connB,
                     bool enabledA, bool enabledB, int64_t nowMs, P2PCreateResult& out);

    //某个用户参与的会话数，调用方已持锁
    size_t countOfUserLocked(int64_t userId) const;

    //建会话前的全部检查（tryCreate 与 checkCreate 共用，规则只写一处），调用者须已持有 m_mutex；返回码同 tryCreate
    int checkCreateLocked(const std::string& purpose, P2PConnKey connA, P2PConnKey connB, bool enabledA,
                          bool enabledB) const;

    //某用户最近一次报到的连接，跳过 excludeConn；没有时返回 0（调用方已持有 m_mutex）
    P2PConnKey latestConnectionLocked(int64_t userId, P2PConnKey excludeConn) const;

    //按会话号查会话并核对成员身份，调用方已持锁；成功时 *sessionOut 指向会话
    int findMemberLocked(const std::string& sessionId, P2PConnKey fromConn, const Session** sessionOut) const;

private:
    mutable std::mutex                       m_mutex;           //保护以下全部成员
    P2PSessionConfig                         m_config;          //运行参数，configure 设置
    std::map<P2PConnKey, Registration>       m_registrations;   //已报到的连接 → 登记信息；连接断开时移除
    std::map<std::string, Session>           m_sessions;        //会话号 → 会话；结束或任一方断开时移除
    uint64_t                                 m_sessionSeq;      //会话号里的序号，进程内单调递增
    uint64_t                                 m_registerSeq;     //报到顺序号，进程内单调递增
};
