/**
 *  远程桌面会话管理器，RemoteDesktopManager.h
 *
 *  chatserver 端持有远程桌面域的运行配置（relay 令牌密钥、imgserver 中转对外
 *  地址）并维护"活跃远程桌面会话表"。一次远程桌面从控制端发起邀请开始，由一个
 *  sessionid 贯穿全程：邀请时建会话(createSession)，被控端应答 / 取消 / 结束时
 *  按 sessionid 查会话以定位对端、校验参与方、签发 relay 令牌，会话结束时清除。
 *
 *  被控端接受后会话记为已接通，控制状态从「未授权」开始：控制端只能查看，经 cmd_rd_control（1718）
 *  申请、被控端同意（或被控端直接授予）之后才能控制，规则见 applyControl（2026-10-12）。
 *
 *  授权通过后给控制端、被控端各签一枚 RelayToken（同 sessionid、相反 role、
 *  uid 与 peeruid 对调），双方据此连 imgserver 中转通道、按字节双向转发屏幕帧
 *  与控制命令（中转层见 [RelaySession]，本类不参与中转数据）。
 *
 *  全局单例，经 Singleton<RemoteDesktopManager> 访问；会话表受内部互斥量保护，
 *  可被多个 EventLoop 工作线程并发访问。本类为纯逻辑（不触网、不查库），便于
 *  单元测试，见 tests/test_remotedesktopprotocol.cpp。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <map>
#include <mutex>
#include <vector>

//一条活跃远程桌面会话，对应一次"控制端 ↔ 被控端"的远程桌面
struct RemoteDesktopSessionInfo
{
    std::string sessionid;     //会话 id（UUID，不含令牌分隔符 '.' / '|'）
    int64_t     controllerId;  //控制端 userid（发起邀请、观看并控制对方的一方）
    int64_t     controlledId;  //被控端 userid（被观看与控制的一方）
    int64_t     createTime;    //会话创建时间，Unix 毫秒（供超时清理）
    int64_t     controllerOfflineAt;   //控制端最后一个登录连接断开的时刻，Unix 毫秒；0 表示在线（掉线宽限用）
    int64_t     controlledOfflineAt;   //被控端最后一个登录连接断开的时刻，Unix 毫秒；0 表示在线（掉线宽限用）
    bool        connected;     //被控端是否已接受邀请（markConnected 置位）；接受之前不允许任何控制动作
    int         controlState;  //控制状态，取值见 enum rd_control_state；建会话与接通时都是未授权

    RemoteDesktopSessionInfo()
        : controllerId(0), controlledId(0), createTime(0), controllerOfflineAt(0), controlledOfflineAt(0),
          connected(false), controlState(0)
    {
    }
};

//一次控制动作（RemoteDesktopManager::applyControl）的结果，供 ChatSession 回应答与推 1719
struct RemoteDesktopControlResult
{
    int     m_control;      //动作之后的控制状态，取值见 enum rd_control_state；动作被拒时为当前状态
    int64_t m_notifyUser;   //要推 1719 的人（会话的另一方）；0 表示状态没有变化、不必通知

    RemoteDesktopControlResult()
        : m_control(0), m_notifyUser(0)
    {
    }
};

//远程桌面会话管理器；全局单例，经 Singleton<RemoteDesktopManager> 访问
class RemoteDesktopManager final
{
public:
    RemoteDesktopManager();
    ~RemoteDesktopManager();

    RemoteDesktopManager(const RemoteDesktopManager& rhs) = delete;
    RemoteDesktopManager& operator=(const RemoteDesktopManager& rhs) = delete;

    //relay 令牌有效期（毫秒）：仅覆盖"拿令牌去连 imgserver 建立中转"这一步，
    //连上后中转层不再校验令牌；5 分钟足够覆盖授权到建连的间隔
    static const int64_t kRelayTokenTtlMs = 5 * 60 * 1000;

    /**
     *  配置远程桌面域运行参数；main.cpp 读取配置文件后调用。
     *  @param relayTokenKey relay 令牌 HMAC 密钥，与 imgserver 共享（relaytokenkey 配置项）
     *  @param relayHost     imgserver 中转通道对客户端公布的主机地址
     *  @param relayPort     imgserver 中转通道端口
     */
    void configure(const std::string& relayTokenKey, const std::string& relayHost, int relayPort);

    //取 imgserver 中转对外地址 / 端口，供构造 cmd_rd_relay_info
    const std::string& getRelayHost() const;
    int getRelayPort() const;

    /**
     *  新建一次远程桌面会话：生成 sessionid、登记会话表（未接通，控制状态为未授权）。
     *  @param controllerId 控制端 userid（发起方）
     *  @param controlledId 被控端 userid（被叫方）
     *  @param now          当前时间，Unix 毫秒（作 createTime）
     *  @return 新会话的 sessionid（UUID）
     */
    std::string createSession(int64_t controllerId, int64_t controlledId, int64_t now);

    /**
     *  按 sessionid 查会话（拷贝出）。
     *  @param sessionid 会话 id
     *  @param out       [出参] 命中时回填
     *  @return 命中返回 true，否则 false
     */
    bool getSession(const std::string& sessionid, RemoteDesktopSessionInfo& out);

    /**
     *  删除会话（拒绝 / 取消 / 结束时调用）。
     *  @param sessionid 会话 id
     *  @return 删除了一条返回 true；本无该会话返回 false
     */
    bool removeSession(const std::string& sessionid);

    /**
     *  被控端接受了邀请：把会话记为已接通，控制状态置为未授权（只能查看）。
     *  @param sessionid 会话 id
     *  @return 会话存在返回 true，否则 false
     */
    bool markConnected(const std::string& sessionid);

    /**
     *  执行一次控制动作（cmd_rd_control=1718）并更新会话的控制状态。规则：
     *    - 申请、撤回、放弃只能由控制端做；授予、拒绝、收回只能由被控端做；
     *    - 会话接通之前一律不允许；
     *    - 申请：未授权 → 申请中，通知被控端；申请中或已授权时再申请按成功处理、不通知；
     *    - 撤回：申请中 → 未授权，通知被控端；
     *    - 授予：未授权或申请中 → 已授权，通知控制端；已授权时再授予按成功处理、不通知；
     *    - 拒绝：申请中 → 未授权，通知控制端；
     *    - 收回（被控端）、放弃（控制端）：已授权 → 未授权，通知对方。
     *  @param sessionid 会话 id
     *  @param actorId   做动作的人
     *  @param action    控制动作，见 enum rd_control_action
     *  @param out       [出参] 动作之后的控制状态与要通知的人
     *  @return error_code_ok 成功；error_code_rd_not_exist 会话不存在；error_code_rd_not_participant 不是参与方或角色不对；
     *          error_code_rd_control_state 当前状态不允许；error_code_bad_request 动作取值越界
     */
    int applyControl(const std::string& sessionid, int64_t actorId, int action, RemoteDesktopControlResult& out);

    /**
     *  给会话的某一角色签发 relay 令牌。role==RelayToken::role_controller 时
     *  uid=控制端、peeruid=被控端；role==role_controlled 时反之。
     *  @param sessionid 会话 id
     *  @param role      角色，取值见 RelayToken::role_*
     *  @param now       当前时间，Unix 毫秒（用于计算过期 = now + kRelayTokenTtlMs）
     *  @param outToken  [出参] 命中时回填签发出的令牌
     *  @return 会话存在并签发返回 true，否则 false
     */
    bool signTokenFor(const std::string& sessionid, int role, int64_t now, std::string& outToken);

    //参与方掉线后保留会话的默认宽限期（毫秒）；可由 chatserver.conf 的 rdofflinegracems 覆盖
    static const int64_t kDefaultOfflineGraceMs = 30 * 1000;

    /**
     *  设置掉线宽限期。
     *  @param graceMs 宽限期，单位毫秒；不大于 0 时按 kDefaultOfflineGraceMs 处理
     */
    void setOfflineGraceMs(int64_t graceMs);

    //当前的掉线宽限期，单位毫秒
    int64_t offlineGraceMs() const;

    /**
     *  用户最后一个登录连接断开：给他参与的每个远程桌面会话记下掉线时刻。已记过的不改（以第一次为准）。
     *  @param userId 掉线的用户
     *  @param now    当前时间，Unix 毫秒
     */
    void markUserOffline(int64_t userId, int64_t now);

    /**
     *  用户重新登录：清掉他参与的会话的掉线时刻。
     *  @param userId 登录的用户
     */
    void markUserOnline(int64_t userId);

    /**
     *  收集并移除「有参与方掉线已达宽限期」的会话（响铃中与已接受的都算）。
     *  @param now 当前时间，Unix 毫秒
     *  @param out [出参] 追加被移除的会话（含双方掉线时刻）；调用前不清空由调用方负责
     */
    void reapOfflineSessions(int64_t now, std::vector<RemoteDesktopSessionInfo>& out);

private:
    mutable std::mutex                              m_mutex;        //保护会话表
    std::map<std::string, RemoteDesktopSessionInfo> m_sessions;     //sessionid → 会话
    uint64_t                                        m_seqCounter;   //会话序号自增器，与时间 / 双方 id 拼成唯一 sessionid
    std::string                                     m_relayTokenKey;//relay 令牌 HMAC 密钥
    std::string                                     m_relayHost;    //imgserver 中转对外主机地址
    int                                             m_relayPort;    //imgserver 中转端口
    int64_t                                         m_offlineGraceMs;   //掉线宽限期（毫秒），由 setOfflineGraceMs 设置
};
