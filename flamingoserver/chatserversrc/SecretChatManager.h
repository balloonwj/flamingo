/**
 *  单聊密聊的服务端会话管理，SecretChatManager.h
 *
 *  密聊（docs/密聊方案.md 第 4、5 节）只存在于内存：服务端负责报到登记、邀请、应答、超时、转发与收尾，
 *  不写数据库。消息在客户端端到端加密，本类只按会话把密文从一条连接转给另一条连接，不解析密文。
 *
 *  本类负责：
 *    - 能力登记：哪些连接已用 cmd_secret_register(2601) 报到、报到的是哪个用户、本机的防截图级别；
 *    - 邀请（2602）：校验双方企业开关、黑名单、对方是否有已报到的连接、频率与数量上限、同一对用户是否已有密聊，
 *      建待应答会话，并推 2603 给被邀请人全部已报到的连接；
 *    - 应答（2604）、撤回（2606）、结束（2607）：转换会话状态，按 secret_state 给相关连接推 2605；
 *    - 转发（2608）：只在会话绑定的两条连接之间转发，检查单帧长度与每秒帧数；
 *    - 收尾：连接断开、邀请超时、企业关闭开关时结束相关会话并通知仍在线的一方。
 *
 *  企业开关、所属企业与黑名单经 ISecretChatGate 查询（会话层转给 FeatureSwitchManager、DepartmentManager、
 *  UserManager，测试里注入假的）。
 *
 *  纯逻辑：不触及网络、数据库与日志，时间由调用方传入，便于单元测试（tests/test_secretchat.cpp，Suite SC）。
 *  全部公开方法线程安全，内部一把互斥量；setLimits 只在服务启动阶段调用。
 *
 *  典型用法（会话层）：
 *      SecretRequestContext ctx;  ...（连接标识、userid、seq、当前毫秒）
 *      std::vector<SecretOutFrame> out;
 *      int code = Singleton<SecretChatManager>::Instance().handle(cmd, ctx, body, gate, out);
 *      for (...) 按 out[i].m_conn 找到会话对象，send(out[i].m_cmd, out[i].m_seq, out[i].m_body)
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

//密聊所属 chatserver 连接的不透明标识。会话层传会话对象的地址（与 P2PConnKey 同源），本类只用它做相等比较
typedef uint64_t SecretConnKey;

//本服务端支持的密聊协议版本：2601 报到的 version 必须等于它，否则回 error_code_secret_version_unsupported
const int kSecretProtocolVersion = 1;

//运行参数的默认值（docs/密聊方案.md 4.2、4.10、5.3），main.cpp 可按 etc/chatserver.conf 覆盖
const int64_t kSecretDefaultInviteTimeoutMs = 60 * 1000;   //邀请有效期：60 秒（配置项 secretinvitetimeout，单位秒）
const uint32_t kSecretDefaultMaxFrameBytes = 131072;       //单帧包体上限：128 KB，容得下 48 KB 图片块加密并 base64 之后的大小
const int kSecretDefaultMaxSessionsPerUser = 10;           //每人同时进行（含待应答）的密聊数上限
const int kSecretDefaultInvitesPerMinute = 10;             //每人每分钟最多发起的邀请数
const int kSecretDefaultFramesPerSecond = 64;              //每个会话每秒最多转发的帧数（两个方向合计）

//X25519 公钥（32 字节）的 base64 文本长度：ceil(32 / 3) * 4 = 44，末尾一个 '='
const size_t kSecretPublicKeyBase64Len = 44;

//一帧要发出的帧：应答发回请求者自己的连接（seq 为请求的 seq），推送发给其它连接（seq 为 0）
struct SecretOutFrame
{
    SecretConnKey m_conn;   //发往哪条连接
    uint16_t      m_cmd;    //命令号
    uint32_t      m_seq;    //帧序号
    std::string   m_body;   //包体（紧凑 JSON）

    SecretOutFrame()
        : m_conn(0)
        , m_cmd(0)
        , m_seq(0)
    {
    }
};

//一帧请求的上下文：来自哪条连接、哪个用户
struct SecretRequestContext
{
    SecretConnKey m_conn;     //请求者的连接
    int64_t       m_userId;   //请求者 userid
    uint32_t      m_seq;      //请求帧的 seq，应答原样带回
    int64_t       m_nowMs;    //当前时间（毫秒），用于邀请到期时间、频率限制与转发时间戳

    SecretRequestContext()
        : m_conn(0)
        , m_userId(0)
        , m_seq(0)
        , m_nowMs(0)
    {
    }
};

//运行参数
struct SecretChatLimits
{
    int64_t  m_inviteTimeoutMs;     //邀请有效期（毫秒）
    uint32_t m_maxFrameBytes;       //单帧包体上限（字节），对 2608 的整个包体检查
    int      m_maxSessionsPerUser;  //每人同时进行（含待应答）的密聊数上限
    int      m_invitesPerMinute;    //每人每分钟最多发起的邀请数
    int      m_framesPerSecond;     //每个会话每秒最多转发的帧数

    SecretChatLimits()
        : m_inviteTimeoutMs(kSecretDefaultInviteTimeoutMs)
        , m_maxFrameBytes(kSecretDefaultMaxFrameBytes)
        , m_maxSessionsPerUser(kSecretDefaultMaxSessionsPerUser)
        , m_invitesPerMinute(kSecretDefaultInvitesPerMinute)
        , m_framesPerSecond(kSecretDefaultFramesPerSecond)
    {
    }
};

/**
 *  企业开关、所属企业与黑名单的查询。由会话层实现，测试里注入假的。
 *  析构函数受保护：SecretChatManager 只借用，不负责销毁。
 */
class ISecretChatGate
{
public:
    /**
     *  查询某个用户所在企业是否开启了单聊密聊。
     *  @param userId 用户 userid
     *  @return 开启返回 true；不属于任何企业返回 false
     */
    virtual bool isSecretChatEnabledForUser(int64_t userId) = 0;

    /**
     *  查询某个用户所属的企业。
     *  @param userId 用户 userid
     *  @return 企业 id；不属于任何企业返回 0
     */
    virtual int64_t enterpriseOfUser(int64_t userId) = 0;

    /**
     *  查询 ownerId 的黑名单里是否有 targetId。
     *  @param ownerId  黑名单的主人
     *  @param targetId 被查的用户
     *  @return ownerId 拉黑了 targetId 时返回 true
     */
    virtual bool isBlockedBy(int64_t ownerId, int64_t targetId) = 0;

protected:
    ~ISecretChatGate()
    {
    }
};

/**
 *  单聊密聊的会话管理。chatserver 内经 Singleton<SecretChatManager>::Instance() 使用，测试里直接构造。
 */
class SecretChatManager
{
public:
    SecretChatManager();

    /**
     *  设置运行参数，只在服务启动阶段调用一次。
     *  @param limits 运行参数；各项不大于 0 的保留默认值
     */
    void setLimits(const SecretChatLimits& limits);

    /**
     *  取当前的运行参数。
     *  @return 运行参数的副本
     */
    SecretChatLimits getLimits() const;

    /**
     *  判断一个命令号是否属于密聊号段（2600~2699）。
     *  @param cmd 命令号
     *  @return 属于时返回 true；会话层据此决定日志里不打印包体
     */
    static bool isSecretCommand(uint16_t cmd);

    /**
     *  处理一帧 2600 段请求。任何情况下都至少产出一帧回给请求者的应答。
     *  @param cmd  命令号（2601 / 2602 / 2604 / 2606 / 2607 / 2608；其余 2600 段的命令按报文非法应答）
     *  @param ctx  请求上下文
     *  @param body 请求包体（UTF-8 JSON）
     *  @param gate 企业开关、所属企业与黑名单的查询（借用，不持有）
     *  @param out  [出参] 追加要发出的帧
     *  @return 本次请求的结果码（error_code），供会话层记日志
     */
    int handle(uint16_t cmd, const SecretRequestContext& ctx, const std::string& body, ISecretChatGate& gate,
               std::vector<SecretOutFrame>& out);

    /**
     *  连接断开或退出登录：移除这条连接的登记，结束它参与的全部会话。进行中的会话给另一方推 2605（已结束、对方离线）；
     *  它发起的待应答邀请给被邀请人推 2605（已取消）；它是被邀请人唯一已报到的连接时，给发起方推 2605（未接受）。
     *  @param conn 断开的连接
     *  @param out  [出参] 追加要发出的帧；没有相关会话时不追加
     */
    void onDisconnect(SecretConnKey conn, std::vector<SecretOutFrame>& out);

    /**
     *  结束已到期的待应答邀请：给发起方推 2605（未接受），给被邀请人全部已报到的连接推 2605（未接受），
     *  以便双方的邀请卡片消失。由 main.cpp 的周期任务每秒调用一次。
     *  @param nowMs 当前时间（毫秒）
     *  @param out   [出参] 追加要发出的帧
     *  @return 本次结束的会话数
     */
    size_t reapExpired(int64_t nowMs, std::vector<SecretOutFrame>& out);

    /**
     *  企业关闭了密聊开关：结束该企业成员参与的全部会话（含待应答），给每条相关连接推 2605（已结束、管理员已关闭）。
     *  @param enterpriseId 企业 id
     *  @param gate         查询成员所属企业（借用，不持有）
     *  @param out          [出参] 追加要发出的帧
     *  @return 本次结束的会话数
     */
    size_t onFeatureDisabled(int64_t enterpriseId, ISecretChatGate& gate, std::vector<SecretOutFrame>& out);

    /**
     *  当前会话数（含待应答），供监控与测试使用。
     *  @return 会话数
     */
    size_t sessionCount() const;

    /**
     *  当前已报到的连接数，供监控与测试使用。
     *  @return 连接数
     */
    size_t registeredCount() const;

private:
    //一条已报到的连接
    struct ConnInfo
    {
        int64_t m_userId;    //连接所属用户
        int     m_protect;   //该连接报到的防截图级别（secret_protect_level）

        ConnInfo()
            : m_userId(0)
            , m_protect(0)
        {
        }
    };

    //会话所处的阶段
    enum SessionPhase
    {
        kPhasePending = 0,   //待应答：邀请已发出，被邀请人还没有同意或忽略
        kPhaseActive  = 1,   //进行中：双方已绑定连接，可以转发
    };

    //一个密聊会话
    struct Session
    {
        std::string   m_id;              //会话号（十进制数字串）
        int           m_phase;           //所处阶段，取值见 SessionPhase
        int64_t       m_inviterId;       //发起方 userid
        SecretConnKey m_inviterConn;     //发起方连接（发出邀请的那条）
        int64_t       m_inviteeId;       //被邀请方 userid
        SecretConnKey m_inviteeConn;     //被邀请方连接：进行中为点「同意」的那条，待应答时为 0
        int64_t       m_expireMs;        //待应答邀请的到期时间（毫秒）；进行中不再使用
        int64_t       m_rateWindowMs;    //转发限速当前窗口的起点（毫秒）
        int           m_rateCount;       //当前窗口内已转发的帧数

        Session()
            : m_phase(kPhasePending)
            , m_inviterId(0)
            , m_inviterConn(0)
            , m_inviteeId(0)
            , m_inviteeConn(0)
            , m_expireMs(0)
            , m_rateWindowMs(0)
            , m_rateCount(0)
        {
        }
    };

    typedef std::map<std::string, Session> SessionMap;

private:
    //以下各处理函数在持锁状态下调用，参数与 handle 相同，返回本次请求的结果码
    int handleRegister(const SecretRequestContext& ctx, const std::string& body, ISecretChatGate& gate,
                       std::vector<SecretOutFrame>& out);
    int handleInvite(const SecretRequestContext& ctx, const std::string& body, ISecretChatGate& gate,
                     std::vector<SecretOutFrame>& out);
    int handleRespond(const SecretRequestContext& ctx, const std::string& body, ISecretChatGate& gate,
                      std::vector<SecretOutFrame>& out);
    int handleCancel(const SecretRequestContext& ctx, const std::string& body, std::vector<SecretOutFrame>& out);
    int handleClose(const SecretRequestContext& ctx, const std::string& body, std::vector<SecretOutFrame>& out);
    int handleMessage(const SecretRequestContext& ctx, const std::string& body, std::vector<SecretOutFrame>& out);

    //某个用户全部已报到的连接；exclude 非 0 时排除这一条
    std::vector<SecretConnKey> connsOfUser(int64_t userId, SecretConnKey exclude) const;

    //某条连接报到的防截图级别；没有报到时返回 secret_protect_none
    int protectOf(SecretConnKey conn) const;

    //某个用户同时参与（含待应答）的会话数
    int sessionCountOfUser(int64_t userId) const;

    //同一对用户之间是否已有待应答或进行中的会话（不分谁发起）
    bool hasSessionBetween(int64_t userA, int64_t userB) const;

    //检查并记录一次邀请的频率：最近一分钟内未超过上限时记下本次并返回 true
    bool allowInvite(int64_t userId, int64_t nowMs);

    //生成新的会话号
    std::string newSessionId(int64_t nowMs);

    //给一组连接各追加一帧 2605
    void appendState(std::vector<SecretOutFrame>& out, const std::vector<SecretConnKey>& conns, const Session& s,
                     int64_t peerId, int state, int reason) const;

    //追加一帧
    static void append(std::vector<SecretOutFrame>& out, SecretConnKey conn, uint16_t cmd, uint32_t seq,
                       const std::string& body);

private:
    mutable std::mutex                          m_mutex;        //保护以下全部成员
    SecretChatLimits                            m_limits;       //运行参数
    std::map<SecretConnKey, ConnInfo>           m_conns;        //已报到的连接
    SessionMap                                  m_sessions;     //全部会话，键为会话号
    std::map<int64_t, std::deque<int64_t> >     m_inviteTimes;  //每个用户最近一分钟内发起邀请的时间（毫秒），用于限频
    uint64_t                                    m_idCounter;    //会话号计数器，构造时取随机起点
};
