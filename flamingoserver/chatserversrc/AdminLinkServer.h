/**
 *  管理控制台长连接服务，AdminLinkServer.h
 *
 *  在 chatserver 的管理端口（adminlinklistenport，默认 20006）上监听 flamingoAdmin 的
 *  FlamingoLink.dll，向订阅了某企业的控制台实时推送该企业的在线名单（2504）。
 *  方案见 docs/FlamingoLink方案.md，协议见 docs/协议设计-v2.md 第 22 节。
 *
 *  线程模型：
 *    - 管理连接的收发在本服务自己的 I/O 线程上（AdminLinkSession::onRead）；
 *    - 一条专用推送线程负责：订阅表的增删、完整名单与变化推送的计算和发送、空闲与未鉴权超时
 *      检查、token 复查。订阅表只在这条线程上访问，不加锁；名单计算与发送都在这条线程上，
 *      同一连接上不会出现新状态先到、旧状态后到；
 *    - 员工会话的五个变化点（登录、改状态、注销、置为失效、断开）在各自线程上调用
 *      onUserOnlineChanged，它只把 userid 放进加锁的待推送集合，不做别的事。
 *
 *  本头文件不包含 Protocol.h（HttpSession.cpp 要用 getLoginLinkInfo）。
 *
 *  典型用法（main.cpp）：
 *      Singleton<AdminLinkServer>::Instance().init(ip, port, &g_mainLoop, ioThreads);
 *      ...
 *      Singleton<AdminLinkServer>::Instance().uninit();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <list>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include "../net/TcpServer.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThread.h"

using namespace net;

class AdminLinkSession;
struct AdminLinkSessionFact;
struct AdminLinkUser;

/**
 *  管理端口服务（服务器级单例，经 Singleton<AdminLinkServer> 使用）。
 */
class AdminLinkServer final
{
public:
    AdminLinkServer();
    ~AdminLinkServer() = default;

    AdminLinkServer(const AdminLinkServer& rhs) = delete;
    AdminLinkServer& operator =(const AdminLinkServer& rhs) = delete;

    //管理端口网络 I/O 工作线程数的默认值，单位：条线程。同时在线的管理员很少，一条足够。
    //配置项 adminlinkiothreadnum 缺失或取值非法时回落到这里，见 utils/ServerConfigLoader.h
    static const int kDefaultIoThreadCount = 1;

    /**
     *  启动管理端口的侦听与推送线程。
     *  @param ip             监听地址，取自配置项 adminlinklistenip
     *  @param port           监听端口，取自配置项 adminlinklistenport；调用方已保证大于 0
     *  @param loop           主事件循环（accept 在它上面），本类只使用不接管其生命周期
     *  @param ioThreadCount  网络 I/O 工作线程数，语义同 ChatServer::init
     *  @return 恒返回 true
     *  副作用：创建一条推送线程并在其上注册三个周期任务（攒批推送、超时检查、token 复查）。
     */
    bool init(const char* ip, short port, EventLoop* loop, int ioThreadCount);

    /**
     *  停止侦听并结束推送线程。未 init 时调用是安全的。
     */
    void uninit();

    //管理端口是否在监听。任意线程可调
    bool isListening() const { return m_listening.load(); }

    /**
     *  取 login.do 应答里的 linkport 与 linkpubkey。任意线程可调。
     *  @param outPort    [出参] 管理端口；未监听时为 0
     *  @param outPubkey  [出参] 服务端身份公钥的 base64；未监听或未开启加密时为空串
     */
    void getLoginLinkInfo(int& outPort, std::string& outPubkey);

    /**
     *  某员工的在线情况可能变了（登录、改状态、注销、会话置为失效、连接断开）。任意线程可调，
     *  只把 userid 放进待推送集合，由推送线程在下一次攒批时为订阅了其企业的控制台重算并推送。
     *  未监听时直接返回。
     *  @param userid  员工ID；不大于 0 时忽略
     */
    void onUserOnlineChanged(int64_t userid);

    /**
     *  请推送线程登记一个订阅：回 2502 成功应答，再发该企业的完整名单。由 AdminLinkSession
     *  在 I/O 线程上校验完权限后调用。同一会话再次订阅即替换原订阅。
     *  @param session       发起订阅的会话
     *  @param enterpriseId  企业ID（已校验存在且有权查看）
     *  @param seq           请求序列号，应答原样回带
     */
    void requestSubscribe(const std::shared_ptr<AdminLinkSession>& session, int64_t enterpriseId, uint32_t seq);

    /**
     *  请推送线程退订，并回 2503 应答。
     *  @param session  发起退订的会话
     *  @param seq      请求序列号
     */
    void requestUnsubscribe(const std::shared_ptr<AdminLinkSession>& session, uint32_t seq);

private:
    //新连接到来或连接断开（由 conn->connected() 区分），在主 loop 或 I/O 线程上回调
    void onConnected(std::shared_ptr<TcpConnection> conn);
    //连接断开：从会话表摘除，并请推送线程清掉它的订阅
    void onDisconnected(const std::shared_ptr<TcpConnection>& conn);

    //以下 do* 与 *Tick 只在推送线程上运行

    //登记订阅、回应答、发完整名单
    void doSubscribe(std::shared_ptr<AdminLinkSession> session, int64_t enterpriseId, uint32_t seq);
    //删除订阅并回应答
    void doUnsubscribe(std::shared_ptr<AdminLinkSession> session, uint32_t seq);
    //连接断开后删除订阅
    void doRemoveSession(std::shared_ptr<AdminLinkSession> session);

    //攒批推送：取出待推送用户，按企业组帧推给订阅了该企业的会话
    void flushTick();
    //空闲与未鉴权超时检查
    void timeoutCheckTick();
    //复查已鉴权会话的 token，失效即断开
    void tokenRecheckTick();

    /**
     *  从员工会话表取一份事实快照，并给出每条事实所属的企业。
     *  @param outFacts        [出参] 处于登录态的会话事实
     *  @param outEnterprises  [出参] 与 outFacts 一一对应的企业ID
     */
    void collectFacts(std::vector<AdminLinkSessionFact>& outFacts, std::vector<int64_t>& outEnterprises);

    /**
     *  为某企业的一组用户组名单条目：资料取组织架构缓存，组织架构里没有的人取用户缓存。
     *  @param enterpriseId  企业ID
     *  @param userids       用户
     *  @param facts         会话事实
     *  @param outUsers      [出参] 名单条目
     */
    void buildEnterpriseUsers(int64_t enterpriseId, const std::vector<int64_t>& userids,
                              const std::vector<AdminLinkSessionFact>& facts,
                              std::vector<AdminLinkUser>& outUsers);

    //把一组 2504 包体依次发给一个会话
    void sendBodies(const std::shared_ptr<AdminLinkSession>& session, const std::vector<std::string>& bodies);

    //取会话表快照
    void getSessions(std::list<std::shared_ptr<AdminLinkSession> >& out);

private:
    std::unique_ptr<TcpServer>                                 m_server;          //管理端口的侦听，init 时创建
    std::list<std::shared_ptr<AdminLinkSession> >              m_sessions;        //全部管理连接；由 m_sessionMutex 保护
    std::mutex                                                 m_sessionMutex;    //保护 m_sessions
    std::unique_ptr<EventLoopThread>                           m_pushThread;      //专用推送线程，init 时启动、uninit 时结束
    EventLoop*                                                 m_pushLoop;        //推送线程上的事件循环，归 m_pushThread 所有；未 init 时为 NULL
    std::map<std::shared_ptr<AdminLinkSession>, int64_t>       m_subscriptions;   //会话 → 订阅的企业ID；只在推送线程上访问
    //员工ID → 推送线程最近一次看到他在线时所属的企业，只在推送线程上访问。员工被删除或调离企业后，
    //按当前归属已查不到原企业，变化推送时据此仍给原企业的订阅者推一条「已离线」，否则那几行会一直留着
    std::map<int64_t, int64_t>                                 m_lastEnterpriseOfUser;
    std::set<int64_t>                                          m_dirtyUsers;      //待推送的员工ID；由 m_dirtyMutex 保护
    std::mutex                                                 m_dirtyMutex;      //保护 m_dirtyUsers
    std::atomic<bool>                                          m_listening;       //是否在监听；onUserOnlineChanged 据此决定是否记录
    int                                                        m_port;            //监听端口；init 之后不再改变
};
