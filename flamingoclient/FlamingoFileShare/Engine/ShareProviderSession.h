/**
 *  分享者一端的一个访问会话：一条到 fileserver 的中转连接（lane 0）加一个请求处理器，ShareProviderSession.h
 *
 *  ShareService 收到 2222（推给分享者的那一帧）后为它建一个本类对象并调用 StartConnect：后台线程连接 fileserver、
 *  完成加密握手与报到（FolderRelayClient::Connect 是阻塞的，不能放在 UI 线程上），成功后立即由 CShareProvider
 *  发出 sf_hello；此后中转连接的接收线程把访问者送来的每个应用帧交给 CShareProvider 处理。
 *  fileserver 在中转两端都报到之前会丢弃数据帧，分享者又可能比访问者先报到（第 12 步联调实测），所以连接线程在发出
 *  首个 sf_hello 之后并不立即退出，而是每隔 kShareHelloResendMs 重发一次，直到收到访问者的第一帧、会话被关闭、
 *  或超过 2222 给出的 opentimeoutms 为止；访问者只处理第一个 sf_hello。
 *
 *  本类产生的事件（连接成功、连接失败、访问者发来 sf_bye、通道异常断开）都在后台线程上发生，一律交给
 *  IShareSessionEventSink（ShareService 实现），由它排队后转到 UI 线程处理；本类自己从不在后台线程上销毁自己。
 *
 *  关闭分两种情况：
 *    - 连接线程已经结束：Close 当场发 sf_bye、关闭中转连接、回收线程，返回 true；
 *    - 连接线程仍在 Connect 里阻塞（最长 kFolderRelayConnectTimeoutSec 秒）：Close 只做标记、立即返回 false，
 *      不阻塞 UI 线程；连接线程结束时看到标记，不再发 sf_hello，照常投递连接结果事件，ShareService 收到事件后
 *      再调用一次 Close 完成关闭。进程退出时 ShareService 改调 WaitConnectThread 等待这些线程结束。
 *
 *  典型用法（ShareService 内部）：
 *      std::unique_ptr<CShareProviderSession> s(new CShareProviderSession(ctx, relay, pubKey, this));
 *      s->StartConnect();
 *      ...（UI 线程收到事件）
 *      if (s->Close(error_code_ok)) ...（已关闭，可以销毁）
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ShareProvider.h"
#include "ShareProtocolCodec.h"   //ShareRelayParams
#include "../../Source/Feature/FolderTransfer/FolderRelayClient.h"

namespace fileshare {

//中转会话（分享者一端的提供会话、访问者一端的访问通道）在后台线程上产生的事件类型
enum share_session_event_type
{
    share_provider_event_connected      = 1,   //分享者：lane 0 报到成功（未被关闭时已发出 sf_hello）
    share_provider_event_connect_failed = 2,   //分享者：连接、加密握手或报到失败，或参数非法而未发起连接
    share_provider_event_visitor_bye    = 3,   //分享者：访问者发来 sf_bye，主动结束访问会话
    share_provider_event_channel_closed = 4,   //分享者：中转连接异常断开（对端断开、网络错误、拆帧或解密失败）
    share_provider_event_write_done     = 5,   //分享者：访问者的一次写操作执行完毕，审计记录在 m_audits
    share_visitor_event_connected       = 11,  //访问者：lane 0 报到成功
    share_visitor_event_connect_failed  = 12,  //访问者：连接、加密握手或报到失败
    share_visitor_event_frame           = 13,  //访问者：收到一个应用帧，内容在 m_data
    share_visitor_event_channel_closed  = 14,  //访问者：中转连接异常断开
    share_transfer_event_progress       = 21,  //传输子会话：进度（字节与文件数在下面四个字段）
    share_transfer_event_completed      = 22,  //传输子会话：整体完成
    share_transfer_event_failed         = 23,  //传输子会话：整体失败或被取消，原因在 m_errCode
};

//一个中转会话事件
struct ShareSessionEvent
{
    int         m_type;        //事件类型，share_session_event_type
    std::string m_accessId;    //所属访问会话 id；传输子会话的事件为传输子会话 id
    int         m_errCode;     //visitor_bye 时为访问者给出的原因，传输失败时为错误码，其余为 0
    std::string m_data;        //share_visitor_event_frame 时为应用帧的副本，其余为空
    int64_t     m_bytesDone;   //传输进度：已完成的字节数
    int64_t     m_bytesTotal;  //传输进度：总字节数
    int64_t     m_filesDone;   //传输进度：已完成的文件数
    int64_t     m_filesTotal;  //传输进度：文件总数
    std::vector<ShareAuditEntry> m_audits;   //share_provider_event_write_done 时为该次写操作的审计记录，其余为空

    ShareSessionEvent()
        : m_type(0)
        , m_errCode(0)
        , m_bytesDone(0)
        , m_bytesTotal(0)
        , m_filesDone(0)
        , m_filesTotal(0)
    {
    }
};

/**
 *  中转会话事件的接收方，由 ShareService 实现。PostSessionEvent 会在连接线程与中转接收线程上被调用，
 *  实现方须线程安全，且只能做排队一类的轻量工作。
 */
class IShareSessionEventSink
{
public:
    virtual ~IShareSessionEventSink()
    {
    }

    /**
     *  投递一个事件（拷贝保存）。
     *  @param ev 事件
     */
    virtual void PostSessionEvent(const ShareSessionEvent& ev) = 0;
};

/**
 *  CShareProviderSession —— 分享者一端的一个访问会话。不可拷贝；只能在 UI 线程上创建、关闭与销毁。
 */
class CShareProviderSession : public foldertransfer::FolderRelayClientHandler,
                              public IShareChannel,
                              public IShareProviderListener
{
public:
    /**
     *  @param ctx          访问会话的上下文（拷贝保存）
     *  @param relay        2222 推给分享者的中转参数（拷贝保存）
     *  @param serverPubKey 服务器公钥（32 字节，拷贝保存）；NULL 表示服务端未配置公钥，连接全程明文
     *  @param sink         事件接收方（不拥有，须在本对象销毁之前一直有效），不可为 NULL
     */
    CShareProviderSession(const ShareProviderContext& ctx, const ShareRelayParams& relay,
                          const unsigned char* serverPubKey, IShareSessionEventSink* sink);

    //销毁前须已经 Close 成功或 WaitConnectThread 返回；析构时仍会兜底关闭连接、回收线程
    virtual ~CShareProviderSession();

    CShareProviderSession(const CShareProviderSession&) = delete;
    CShareProviderSession& operator=(const CShareProviderSession&) = delete;

    /**
     *  启动后台连接线程。只能调用一次。
     */
    void StartConnect();

    /**
     *  关闭会话（UI 线程）。
     *  @param byeErrCode 连接已建立时发出的 sf_bye 的结束原因；传 kShareNoBye 表示不发 sf_bye
     *                   （访问会话已在服务端结束、或访问者已先发了 sf_bye 时不必再发）
     *  @return 已经关闭（可以销毁本对象）返回 true；连接线程仍在进行时返回 false，待其事件到达后再调用一次
     */
    bool Close(int byeErrCode);

    /**
     *  等待连接线程结束（进程退出时使用，最长阻塞约 kFolderRelayConnectTimeoutSec 秒），之后 Close 必然返回 true。
     */
    void WaitConnectThread();

    /**
     *  会话中途权限变化（UI 线程）：转给请求处理器，连接已建立时发 sf_perms。
     *  @param perms 新的写权限位
     */
    void SetPerms(int perms);

    //访问会话 id
    const std::string& GetAccessId() const;

    //共享 id
    int64_t GetShareId() const;

    //访问者 userid
    int64_t GetVisitorId() const;

    //访问者显示名（服务端在 2222 里给的；「文件共享」窗口的「正在访问的人」用）
    const std::string& GetVisitorName() const;

    //Close 的参数取这个值时不发 sf_bye
    static const int kShareNoBye = -1;

private:
    //连接线程主体：连接、握手、报到，成功且未被关闭时发 sf_hello，投递连接结果事件，之后重发 sf_hello 直到收到
    //访问者的第一帧（见 ResendHelloUntilHeard）
    void ConnectProc();

    //重发 sf_hello，直到收到访问者的第一帧、会话被关闭或超过 opentimeoutms；在连接线程上调用
    void ResendHelloUntilHeard();

    //FolderRelayClientHandler：中转接收线程上收到一个应用帧，交给请求处理器
    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len) override;

    //FolderRelayClientHandler：中转连接异常断开
    virtual void OnFolderRelayClosed() override;

    //IShareChannel：经 lane 0 发出一个应用帧
    virtual bool SendFrame(const std::string& frame) override;

    //IShareProviderListener：访问者发来 sf_bye
    virtual void OnVisitorBye(const std::string& accessId, int errCode) override;

    //IShareProviderListener：访问者的一次写操作执行完毕，投递到 UI 线程上报审计
    virtual void OnWriteOpDone(const std::string& accessId, const std::vector<ShareAuditEntry>& audits) override;

    //投递一个本会话的事件
    void Post(int type, int errCode);

private:
    ShareRelayParams                  m_relay;           //中转参数；构造后只读
    unsigned char                     m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];   //服务器公钥的副本
    bool                              m_hasServerPubKey; //是否带了服务器公钥；为假时连接全程明文
    IShareSessionEventSink*          m_sink;            //事件接收方（不拥有）
    foldertransfer::FolderRelayClient m_client;          //lane 0 中转连接；连接线程里 Connect，UI 线程上 Close
    CShareProvider                    m_provider;        //请求处理器：通道为本对象，监听者也为本对象

    std::mutex                        m_mutex;           //保护下面三项的读写顺序：连接线程与 UI 线程都会访问
    bool                              m_closing;         //UI 线程是否已要求关闭；连接线程据此决定是否发 sf_hello
    bool                              m_connectDone;     //连接阶段是否已结束（无论成败）；之后连接线程可能还在重发 sf_hello，但关闭时会被立即唤醒
    bool                              m_connected;       //是否已报到成功；只有已连接时 Close 才发 sf_bye
    std::condition_variable           m_cv;              //关闭会话或收到访问者的帧时通知连接线程，让它停止重发 sf_hello
    std::thread                       m_connectThread;   //连接线程；Close 或 WaitConnectThread 时回收
};

} // namespace fileshare
