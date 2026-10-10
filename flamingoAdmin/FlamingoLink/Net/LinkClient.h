/**
 *  FlamingoLink.dll —— 长连接的工作线程：建连、握手、鉴权、心跳、断线检测、按退避间隔重连，
 *  以及按页面的需要订阅 / 退订在线名单。
 *
 *  界面线程与工作线程之间只有三样东西：
 *    - 启停（Start / Stop）；
 *    - 想订阅的企业（SetDesiredEnterprise，原子量）：工作线程每一轮循环比较它与已发出的订阅，
 *      不一致就发 2502 / 2503。断线重连后已发出的订阅清零，于是自动按想订阅的企业重新订阅；
 *    - 事件队列（加锁）：工作线程把连接状态、订阅结果、名单推送、会话过期放进队列，再向登记的
 *      窗口 PostMessage 一条通知；界面线程收到后用 DrainEvents 一次取走全部事件。
 *
 *  典型用法（界面线程）：
 *      client.SetNotifyTarget(hwnd, kMsgLinkEvent);
 *      client.Start(cfg);
 *      client.SetDesiredEnterprise(123);
 *      ...收到 kMsgLinkEvent... client.DrainEvents(events);
 *      client.Stop();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <random>

#include "FlamingoLink/Core/LinkProtocol.h"

namespace flamingolink {

class LinkConnection;

//服务端身份公钥的字节数（Ed25519），与 net::IDENTITY_PUBLICKEY_BYTES 相同
const size_t kLinkPubkeyBytes = 32;

/**
 *  一次长连接会话的参数，Start 时整体复制进工作线程。
 */
struct LinkConfig
{
    LinkConfig() : m_port(0), m_encrypt(false)
    {
        memset(m_pubkey, 0, sizeof(m_pubkey));
    }

    std::wstring  m_host;                      //服务器主机名或 IP
    int           m_port;                      //管理端口
    bool          m_encrypt;                   //是否走加密握手（服务端下发了公钥时为 true）
    unsigned char m_pubkey[kLinkPubkeyBytes];  //服务端身份公钥，m_encrypt 为 true 时有效
    std::string   m_token;                     //HTTP 登录得到的会话 token
    std::string   m_clientVersion;             //控制台版本号，只用于服务端日志
};

//事件类型，LinkEvent::m_kind 的取值
enum LinkEventKind
{
    link_event_state     = 0,   //连接状态变化：m_state / m_attempt / m_delayMs 有效
    link_event_subscribe = 1,   //2502 应答：m_enterpriseId（请求订阅的企业）/ m_code 有效
    link_event_online    = 2,   //2504 推送：m_notify 有效
    link_event_expired   = 3,   //鉴权时 token 已失效，工作线程已结束
    link_event_unsubscribed = 4, //连接线程确实发出了 2503 退订。只在这时清零「已订阅企业」：页面快速
                                 //失活又激活时线程可能两次都看到同一个期望企业，什么都不发，服务端仍在订阅
};

/**
 *  工作线程交给界面线程的一个事件。
 */
struct LinkEvent
{
    LinkEvent() : m_kind(0), m_state(0), m_attempt(0), m_delayMs(0), m_enterpriseId(0), m_code(0) {}

    int          m_kind;          //事件类型，取值见 enum LinkEventKind
    int          m_state;         //连接状态，取值见 FlamingoLinkApi.h 的 enum LinkState
    int          m_attempt;       //等待重连时是第几次重连
    int          m_delayMs;       //等待重连时这次要等多少毫秒
    int64_t      m_enterpriseId;  //订阅应答对应的企业
    int          m_code;          //订阅应答的错误码
    OnlineNotify m_notify;        //名单推送
};

/**
 *  长连接工作线程。对象由 LinkModule 持有，只在界面线程上调用它的公开方法。
 */
class LinkClient
{
public:
    LinkClient();

    //析构时停止工作线程
    ~LinkClient();

    LinkClient(const LinkClient&) = delete;
    LinkClient& operator=(const LinkClient&) = delete;

    /**
     *  登记事件通知的目标窗口：工作线程每次放入事件（且上一次通知尚未被取走）时
     *  PostMessage(hwnd, msg, 0, 0)。须在 Start 之前调用。
     *  @param hwnd 接收通知的窗口（DLL 自己的消息窗口）
     *  @param msg  通知的消息号
     */
    void SetNotifyTarget(HWND hwnd, UINT msg);

    /**
     *  按 cfg 启动工作线程；已在运行时先停掉旧线程。
     *  @param cfg 连接参数，函数内部复制
     */
    void Start(const LinkConfig& cfg);

    /**
     *  停止工作线程并等待它结束（最长约为一次连接检查间隔加一次握手超时），清空未取走的事件。
     *  未启动时调用是安全的。
     */
    void Stop();

    //工作线程是否在运行
    bool IsRunning() const { return m_thread.joinable(); }

    /**
     *  设置想订阅的企业。工作线程在下一轮循环（最迟约 200 毫秒）里据此发 2502 / 2503。任意线程可调。
     *  @param enterpriseId 企业ID；0 表示不订阅
     */
    void SetDesiredEnterprise(int64_t enterpriseId);

    /**
     *  要求连接线程在下一轮循环里重新订阅当前想订阅的企业（即使与已发出的订阅相同）。界面线程换企业
     *  时调用：它清空了名单，需要服务端重发一份完整名单。任意线程可调。
     */
    void RequestResubscribe();

    /**
     *  取走全部未处理的事件。只在界面线程上调用。
     *  @param out [出参] 先清空再填充，按发生顺序排列
     */
    void DrainEvents(std::vector<LinkEvent>& out);

private:
    //一次连接尝试的结局
    enum RunResult
    {
        run_failed  = 0,   //连接、握手、鉴权或之后的收发出错，或心跳超时：按退避间隔重连
        run_stopped = 1,   //被要求停止
        run_expired = 2,   //鉴权时 token 已失效：不再重连
    };

    //工作线程主函数：循环做连接尝试，失败后按退避间隔等待
    void ThreadMain(LinkConfig cfg);

    /**
     *  做一次连接尝试，直到断开、被要求停止或会话过期。
     *  @param cfg        连接参数
     *  @param outAuthed  [出参] 这次尝试是否鉴权成功过（成功过就把重连次数清零）
     */
    RunResult RunConnection(const LinkConfig& cfg, bool& outAuthed);

    /**
     *  等待 2501 应答。
     *  @param conn    连接
     *  @param seq     2501 请求的序列号
     *  @param out     [出参] 应答内容
     *  @return 收到应答返回 true；超时、出错或被要求停止返回 false
     */
    bool WaitAuthReply(LinkConnection& conn, uint32_t seq, AuthResult& out);

    /**
     *  处理一个收到的帧（心跳、订阅应答、名单推送）。
     *  @param cmd / seq / body 帧内容
     *  @param pendingSubscribes 尚未收到应答的订阅请求：序列号 → 企业ID
     */
    void HandleFrame(uint16_t cmd, uint32_t seq, const std::string& body,
                     std::map<uint32_t, int64_t>& pendingSubscribes);

    /**
     *  按退避间隔等待；期间被要求停止时提前返回。
     *  @return 等满返回 true；被要求停止返回 false
     */
    bool WaitRetry(int delayMs);

    //放入一个事件并通知界面线程
    void PushEvent(const LinkEvent& ev);

    //放入一个连接状态事件
    void PushState(int state, int attempt, int delayMs);

    std::thread             m_thread;          //工作线程；未启动时不可 join
    std::atomic<bool>       m_stop;            //停止标志：Stop 置位，工作线程每轮检查
    std::mutex              m_waitMutex;       //配合 m_waitCv，让退避等待可以被 Stop 打断
    std::condition_variable m_waitCv;          //退避等待用的条件变量
    std::atomic<int64_t>    m_desired;         //想订阅的企业ID，0 表示不订阅
    std::atomic<uint32_t>   m_generation;      //订阅代次：RequestResubscribe 每调用一次加一，连接线程据此判断要不要重订
    std::mutex              m_eventMutex;      //保护 m_events 与 m_notifyPending
    std::vector<LinkEvent>  m_events;          //尚未被界面线程取走的事件
    bool                    m_notifyPending;   //已发出通知、界面线程尚未取走事件；为 true 时不再重复 PostMessage
    HWND                    m_notifyHwnd;      //事件通知的目标窗口，归 LinkModule 所有
    UINT                    m_notifyMsg;       //事件通知的消息号
    std::mt19937            m_random;          //重连间隔的随机浮动，只在工作线程上使用
};

} // namespace flamingolink
