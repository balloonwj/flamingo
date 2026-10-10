/**
 *  远程桌面会话管理器，RemoteDesktopManager.h
 *
 *  chatserver 端持有远程桌面域的运行配置（relay 令牌密钥、imgserver 中转对外
 *  地址）并维护"活跃远程桌面会话表"。一次远程桌面从控制端发起邀请开始，由一个
 *  sessionid 贯穿全程：邀请时建会话(createSession)，被控端应答 / 取消 / 结束时
 *  按 sessionid 查会话以定位对端、校验参与方、签发 relay 令牌，会话结束时清除。
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
    int         state;         //会话状态，取值见 enum rd_state
    int64_t     createTime;    //会话创建时间，Unix 毫秒（供超时清理）
    int64_t     controllerOfflineAt;   //控制端最后一个登录连接断开的时刻，Unix 毫秒；0 表示在线（掉线宽限用）
    int64_t     controlledOfflineAt;   //被控端最后一个登录连接断开的时刻，Unix 毫秒；0 表示在线（掉线宽限用）

    RemoteDesktopSessionInfo()
        : controllerId(0), controlledId(0), state(0), createTime(0), controllerOfflineAt(0), controlledOfflineAt(0)
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
     *  新建一次远程桌面会话：生成 sessionid、以 rd_state_ringing 登记会话表。
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
     *  更新会话状态。
     *  @param sessionid 会话 id
     *  @param state     新状态，取值见 enum rd_state
     *  @return 会话存在并更新返回 true，否则 false
     */
    bool setState(const std::string& sessionid, int state);

    /**
     *  删除会话（拒绝 / 取消 / 结束时调用）。
     *  @param sessionid 会话 id
     *  @return 删除了一条返回 true；本无该会话返回 false
     */
    bool removeSession(const std::string& sessionid);

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
