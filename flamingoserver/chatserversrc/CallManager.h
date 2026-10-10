/**
 *  音视频通话会话管理器，CallManager.h
 *
 *  chatserver 端持有音视频通话域的运行配置（relay 令牌密钥、imgserver 中转对外
 *  地址）并维护“活跃通话会话表”。一次通话从主叫发起邀请开始，由一个 sessionid
 *  贯穿全程：邀请时建会话(createSession)，被叫接听 / 拒绝 / 取消 / 挂断时按
 *  sessionid 查会话以定位对端、签发 relay 令牌，会话结束时清除。
 *
 *  接听后给主叫、被叫各签一枚 RelayToken（同 sessionid、相反 role：role_caller /
 *  role_callee，uid 与 peeruid 对调），双方据此连 imgserver 中转通道、按字节双向
 *  转发音视频帧（中转层复用远程桌面的 RelaySession / RelayHub，本类不参与中转
 *  数据）。相比远程桌面会话管理，本类额外提供两项：isUserBusy（“对方忙”判断，
 *  防止给正在通话的用户再发起呼叫）与 reapTimedOutSessions（响铃超时清理，呼叫
 *  无人应答时自动结束）。另有掉线宽限：参与方最后一个登录连接断开时
 *  markUserOffline 记下时刻，宽限期内重新登录由 markUserOnline 清掉，超过宽限期仍未回来的
 *  通话由 reapOfflineSessions 收集并移除，调用方据此通知仍在线的一方。
 *
 *  全局单例，经 Singleton<CallManager> 访问；会话表受内部互斥量保护，可被多个
 *  EventLoop 工作线程并发访问。本类为纯逻辑（不触网、不查库），便于单元测试，
 *  见 tests/test_callmanager.cpp。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <stdint.h>
#include <string>
#include <map>
#include <mutex>
#include <vector>

//一条活跃通话会话，对应一次“主叫 ↔ 被叫”的音视频通话
struct CallSessionInfo
{
    std::string sessionid;   //会话 id（UUID 风格，只含数字与连字符，不含令牌分隔符 '.' / '|'）
    int64_t     callerId;    //主叫 userid（发起呼叫的一方）
    int64_t     calleeId;    //被叫 userid（被呼叫的一方）
    int         media;       //媒体类型，取值见 enum call_media（纯语音 / 音视频）
    int         state;       //会话状态，取值见 enum call_state
    int64_t     createTime;  //会话创建时间，Unix 毫秒（供响铃超时清理）
    int64_t     callerOfflineAt;   //主叫最后一个登录连接断开的时刻，Unix 毫秒；0 表示在线（掉线宽限用）
    int64_t     calleeOfflineAt;   //被叫最后一个登录连接断开的时刻，Unix 毫秒；0 表示在线（掉线宽限用）
    int64_t     connectTime;       //接通时刻，Unix 毫秒；0 表示还没接通（通话卡片据此算时长，markConnected 设置）
    int64_t     cardMsgId;         //这通电话写在两人单聊里的通话卡片的 msgid；0 表示没有写成（结束时不改写，setCard 设置）
    std::string cardContent;       //通话卡片当前的 content（紧凑 JSON），结束时在它上面改写（setCard 设置）

    CallSessionInfo()
        : callerId(0), calleeId(0), media(0), state(0), createTime(0), callerOfflineAt(0), calleeOfflineAt(0),
          connectTime(0), cardMsgId(0)
    {
    }
};

//音视频通话会话管理器；全局单例，经 Singleton<CallManager> 访问
class CallManager final
{
public:
    CallManager();
    ~CallManager();

    CallManager(const CallManager& rhs) = delete;
    CallManager& operator=(const CallManager& rhs) = delete;

    //响铃超时时限（毫秒）：呼叫发出后被叫在此时限内不应答即自动结束、推送超时
    static const int64_t kRingingTimeoutMs = 45 * 1000;
    //relay 令牌有效期（毫秒）：仅覆盖“拿令牌去连 imgserver 建立中转”这一步，
    //连上后中转层不再校验令牌；5 分钟足够覆盖接听到建连的间隔
    static const int64_t kRelayTokenTtlMs = 5 * 60 * 1000;

    //参与方掉线后保留通话的默认宽限期（毫秒）：期内重新登录则通话继续，超过则由服务端结束。
    //可由 chatserver.conf 的 callofflinegracems 覆盖
    static const int64_t kDefaultOfflineGraceMs = 30 * 1000;

    /**
     *  配置音视频通话域运行参数；main.cpp 读取配置文件后调用。
     *  @param relayTokenKey relay 令牌 HMAC 密钥，与 imgserver 共享（relaytokenkey 配置项）
     *  @param relayHost     imgserver 中转通道对客户端公布的主机地址
     *  @param relayPort     imgserver 中转通道端口
     */
    void configure(const std::string& relayTokenKey, const std::string& relayHost, int relayPort);

    //取 imgserver 中转对外地址 / 端口，供构造 cmd_call_signaling
    const std::string& getRelayHost() const;
    int getRelayPort() const;

    /**
     *  新建一次通话会话：生成 sessionid、以 call_state_ringing 登记会话表。
     *  @param callerId 主叫 userid（发起方）
     *  @param calleeId 被叫 userid
     *  @param media    媒体类型，取值见 enum call_media
     *  @param now      当前时间，Unix 毫秒（作 createTime）
     *  @return 新会话的 sessionid
     */
    std::string createSession(int64_t callerId, int64_t calleeId, int media, int64_t now);

    /**
     *  按 sessionid 查会话（拷贝出）。
     *  @param sessionid 会话 id
     *  @param out       [出参] 命中时回填
     *  @return 命中返回 true，否则 false
     */
    bool getSession(const std::string& sessionid, CallSessionInfo& out);

    /**
     *  更新会话状态。
     *  @param sessionid 会话 id
     *  @param state     新状态，取值见 enum call_state
     *  @return 会话存在并更新返回 true，否则 false
     */
    bool setState(const std::string& sessionid, int state);

    /**
     *  记下接通：状态置为 call_state_connected，并记下接通时刻（通话卡片据此算时长）。
     *  @param sessionid 会话 id
     *  @param now       接通时刻，Unix 毫秒
     *  @return 会话存在并更新返回 true，否则 false
     */
    bool markConnected(const std::string& sessionid, int64_t now);

    /**
     *  记下这通电话写在两人单聊里的通话卡片（呼叫时写入后调用），结束时据此改写。
     *  @param sessionid 会话 id
     *  @param msgId     卡片消息的 msgid
     *  @param content   卡片的 content（紧凑 JSON）
     *  @return 会话存在并更新返回 true，否则 false
     */
    bool setCard(const std::string& sessionid, int64_t msgId, const std::string& content);

    /**
     *  响铃中的会话还剩多少毫秒超时（第 15 期，随来电邀请推送下发，来电弹窗据此显示倒计时）。
     *  @param sessionid 会话 id
     *  @param now       当前时间，Unix 毫秒
     *  @return 剩余毫秒数，已到或已过超时为 0；会话不存在或不在响铃中返回 -1
     */
    int64_t ringRemainingMs(const std::string& sessionid, int64_t now);

    /**
     *  删除会话（拒绝 / 取消 / 挂断 / 超时时调用）。
     *  @param sessionid 会话 id
     *  @return 删除了一条返回 true；本无该会话返回 false
     */
    bool removeSession(const std::string& sessionid);

    /**
     *  切换一次通话的媒体类型（语音 <-> 视频）。全部校验集中在本方法内，调用方只需
     *  按返回码回话，不必自己判状态，避免各处口径不一致。
     *
     *  校验顺序与失败码（见 enum error_code）：
     *    1. 会话不存在                    -> error_code_call_not_exist(705)
     *    2. 请求者不是本次通话双方之一    -> error_code_call_not_member(706)
     *    3. 会话未处于已接通态            -> error_code_call_state_invalid(707)
     *  以上都通过则更新会话的 media 并返回 0。
     *
     *  幂等：目标类型与当前相同时也返回 0（成功），但 outChanged 置 false，调用方据此
     *  跳过推送，避免无意义的双向广播。
     *
     *  @param sessionid   会话 id
     *  @param requesterId 发起切换的用户 userid
     *  @param media       目标媒体类型，取值见 enum call_media（调用方须已校验合法）
     *  @param outChanged  [出参] 本次是否真的发生了变更；false 表示与原类型相同
     *  @return 0 表示成功；非 0 为上述失败码
     */
    int switchMedia(const std::string& sessionid, int64_t requesterId, int media,
                    bool& outChanged);

    /**
     *  给会话的某一角色签发 relay 令牌。role==RelayToken::role_caller 时 uid=主叫、
     *  peeruid=被叫；role==role_callee 时反之。
     *  @param sessionid 会话 id
     *  @param role      角色，取值 RelayToken::role_caller / role_callee
     *  @param now       当前时间，Unix 毫秒（用于计算过期 = now + kRelayTokenTtlMs）
     *  @param outToken  [出参] 命中时回填签发出的令牌
     *  @return 会话存在并签发返回 true，否则 false
     */
    bool signTokenFor(const std::string& sessionid, int role, int64_t now, std::string& outToken);

    /**
     *  查询某用户当前是否正处于活跃通话中（作主叫或被叫均算），用于“对方忙”判断：
     *  给已在通话中的用户再发起呼叫时应直接回 error_code_peer_busy。
     *  @param userId 待查询用户 userid
     *  @return 该用户存在于任一活跃会话返回 true，否则 false
     */
    bool isUserBusy(int64_t userId);

    /**
     *  查出某用户当前所在的那通通话（作主叫或被叫均算）。用于「通话中共享屏幕」时判断
     *  共享对象是不是通话对端、通话是否已接通（见 ScreenShareRules::checkCallConflict）。
     *  @param userId 待查询用户 userid
     *  @param out    [出参] 查到时填入会话信息的拷贝；查不到时不修改
     *  @return 该用户在某个活跃会话里返回 true，否则 false
     */
    bool getUserCall(int64_t userId, CallSessionInfo& out);

    /**
     *  收集并移除所有“响铃超时”的会话（仍处于 call_state_ringing 且
     *  now - createTime >= kRingingTimeoutMs）。已接通的会话不受超时影响。
     *  供 chatserver 的周期定时器调用：据返回的会话信息向双方推送 call_state_timeout。
     *  @param now 当前时间，Unix 毫秒
     *  @param out [出参] 追加被移除的超时会话信息（含双方 userid）；调用前不清空由调用方负责
     */
    void reapTimedOutSessions(int64_t now, std::vector<CallSessionInfo>& out);

    /**
     *  设置掉线宽限期。
     *  @param graceMs 宽限期，单位毫秒；不大于 0 时按 kDefaultOfflineGraceMs 处理
     */
    void setOfflineGraceMs(int64_t graceMs);

    //当前的掉线宽限期，单位毫秒
    int64_t offlineGraceMs() const;

    /**
     *  用户最后一个登录连接断开：给他参与的每通通话记下掉线时刻。已记过的不改（以第一次为准）。
     *  @param userId 掉线的用户
     *  @param now    当前时间，Unix 毫秒
     */
    void markUserOffline(int64_t userId, int64_t now);

    /**
     *  用户重新登录：清掉他参与的通话的掉线时刻，通话继续。
     *  @param userId 登录的用户
     */
    void markUserOnline(int64_t userId);

    /**
     *  收集并移除「有参与方掉线已达宽限期」的通话（响铃中与已接通的都算）。
     *  @param now 当前时间，Unix 毫秒
     *  @param out [出参] 追加被移除的会话（含双方掉线时刻）；调用前不清空由调用方负责
     */
    void reapOfflineSessions(int64_t now, std::vector<CallSessionInfo>& out);

private:
    mutable std::mutex                       m_mutex;        //保护会话表
    std::map<std::string, CallSessionInfo>   m_sessions;     //sessionid → 会话
    uint64_t                                 m_seqCounter;   //会话序号自增器，与时间 / 双方 id 拼成唯一 sessionid
    std::string                              m_relayTokenKey;//relay 令牌 HMAC 密钥
    std::string                              m_relayHost;    //imgserver 中转对外主机地址
    int                                      m_relayPort;    //imgserver 中转端口
    int64_t                                  m_offlineGraceMs;   //掉线宽限期（毫秒），由 setOfflineGraceMs 设置
};
