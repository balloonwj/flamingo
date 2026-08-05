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

//一条活跃远程桌面会话，对应一次"控制端 ↔ 被控端"的远程桌面
struct RemoteDesktopSessionInfo
{
    std::string sessionid;     //会话 id（UUID，不含令牌分隔符 '.' / '|'）
    int64_t     controllerId;  //控制端 userid（发起邀请、观看并控制对方的一方）
    int64_t     controlledId;  //被控端 userid（被观看与控制的一方）
    int         state;         //会话状态，取值见 enum rd_state
    int64_t     createTime;    //会话创建时间，Unix 毫秒（供超时清理）

    RemoteDesktopSessionInfo()
        : controllerId(0), controlledId(0), state(0), createTime(0)
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

private:
    mutable std::mutex                              m_mutex;        //保护会话表
    std::map<std::string, RemoteDesktopSessionInfo> m_sessions;     //sessionid → 会话
    uint64_t                                        m_seqCounter;   //会话序号自增器，与时间 / 双方 id 拼成唯一 sessionid
    std::string                                     m_relayTokenKey;//relay 令牌 HMAC 密钥
    std::string                                     m_relayHost;    //imgserver 中转对外主机地址
    int                                             m_relayPort;    //imgserver 中转端口
};
