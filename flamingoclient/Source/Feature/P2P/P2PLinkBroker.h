/**
 *  业务方共用的 P2P 链路层（docs/P2P通信方案.md 第 6 期），P2PLinkBroker.h
 *
 *  服务端为一次业务（文件夹传输、文件共享、远程桌面……）建了 P2P 会话时，业务信令（1816 / 2222 / 1716）带上
 *  p2psessionid，随后两端各收到一条 2302，由 FlamingoP2P.dll 建链并按用途交给登记了该用途的业务方。业务信令与 2302
 *  走不同的线程（前者经 UI 线程，后者在网络线程上直接交给 DLL），谁先处理完没有保证，链路可能先于业务就绪。本文件把
 *  这部分与具体业务无关的逻辑集中起来：
 *    - P2PLinkBroker      一个用途的业务方：向 P2P 服务登记用途，把 DLL 交来的链路结果放进「链路槽」；业务按功能
 *                         会话号认领链路槽（ClaimSlot）。链路先到时槽先存着等认领，业务先到时认领一个空槽等结果；
 *    - P2PLinkSlot        一次业务的链路槽：等链路就绪 / 失败，链路断开时通知各条流通道；最后一个使用者释放时关闭链路；
 *    - P2PStreamChannel   链路上的一条帧通道：可靠通道是字节流，每帧前加 4 字节长度（网络字节序）；收到的帧由本通道
 *                         自己的转交线程交给业务（业务的回调可以阻塞，DLL 的 IO 线程不能阻塞）。单帧上限由业务指定；
 *    - P2PDatagramChannel 链路上的一条数据报通道（一对一通话的音视频，第 6 期）：一次 Send 的数据由 DLL 按路径分片，
 *                         接收端收齐后整条交出，任何一片丢失整条丢弃，不重传；收到的消息同样由自己的转交线程交给业务，
 *                         排队的消息数有上限，满了丢最旧的（实时媒体宁可丢旧的也不要越积越晚）。
 *  同一业务重试时功能会话号可能不变：新一轮认领只替换已被认领过的旧槽，没被认领的槽（新链路先到了）留给这一轮。
 *
 *  本文件不带 winsock，主程序、FlamingoFileShare.dll 与 FlamingoMedia.dll 都直接编译它。文件夹传输与文件共享的通道
 *  工厂建在它上面（FolderP2PTransport.h），远程桌面的传输也是。
 *
 *  典型用法：
 *      P2PLinkBroker broker("rd");
 *      broker.Register(service);                                    //P2P 模块可用之后
 *      std::shared_ptr<P2PLinkSlot> slot = broker.ClaimSlot(sessionId);   //收到带 p2psessionid 的业务信令时
 *      if (slot->Wait(timeoutMs) == P2PLinkSlot::slot_ready)
 *      {
 *          P2PStreamChannel channel(slot, maxFrameBytes, sendWaitMs);
 *          channel.Open(channelId, handler);
 *          channel.SendFrame(data, len);
 *      }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../../FlamingoP2P/FlamingoP2PApi.h"

namespace balloonp2p {

//帧前的长度字段（字节）
const size_t kP2PStreamFrameHeaderBytes = 4;

//没被任何业务认领的链路最多保留多久（毫秒）：先到的链路正常几十毫秒内就会被认领
const int64_t kP2PUnclaimedLinkTtlMs = 60000;

/**
 *  登记在链路槽上的一条通道：帧通道与数据报通道都实现它。三个方法都由链路槽在 DLL 的 IO 线程上、持有链路槽的互斥量时
 *  调用，实现不得阻塞，也不得回调链路槽。
 *  析构函数受保护：链路槽只借用。
 */
class IP2PSlotChannel
{
public:
    /**
     *  收到本通道的一块数据（可靠通道为一段字节流，数据报通道为一条完整消息）。
     *  @param data 数据，只在本次调用期间有效
     *  @param len  长度（字节）
     */
    virtual void OnChannelData(const uint8_t* data, uint32_t len) = 0;

    //本通道的发送队列降下来了（只有可靠通道会收到）
    virtual void OnChannelWritable() = 0;

    //链路断开
    virtual void OnLinkBroken() = 0;

protected:
    ~IP2PSlotChannel()
    {
    }
};

/**
 *  一次业务的链路槽。它同时是这条链路上全部通道的接收端（IP2PChannelSink）：按通道号转给登记着的流通道，没登记
 *  （或已注销）的通道上的数据丢弃。流通道关闭时先注销，正在转发给它的那一次完成之后注销才返回，此后它可以安全
 *  销毁；链路在本对象析构时关闭，DLL 保证 Close 返回之后不再回调。
 *  线程：Set* 与 OnData / OnWritable 在 DLL 的 IO 线程上调用；Wait 与通道登记在业务的线程上调用；全部加锁。
 */
class P2PLinkSlot : public p2p::IP2PChannelSink
{
public:
    //链路槽的状态
    enum slot_state
    {
        slot_waiting = 0,   //还没有结果：DLL 还在建链，或者业务先认领了槽、2302 还没处理到
        slot_ready   = 1,   //链路就绪，可以打开流通道
        slot_failed  = 2,   //建链失败（打洞与 DLL 自己的中转都没成功，或会话被服务端拒绝）
        slot_closed  = 3,   //就绪后断开
    };

    /**
     *  @param featureSessionId 功能会话号（业务信令里的会话号，与 2302 的 featuresessionid 相同）
     *  @param createdMs        创建时刻（单调时钟毫秒）
     */
    P2PLinkSlot(const std::string& featureSessionId, int64_t createdMs);

    //析构：持有链路时关闭它（业务结束，CloseLink 已关闭过的不再关闭）并释放引用
    ~P2PLinkSlot();

    /**
     *  业务结束：立即关闭链路（通知对方并结束 P2P 会话），不必等最后一个持有者释放本对象。之后状态为 slot_closed，
     *  Link 返回 NULL；链路的引用保留到析构，别的线程手里已经取到的链路指针仍然有效（其 Send 返回 p2p_send_closed）。
     *  幂等；不得在持有本对象互斥量的回调里调用。
     */
    void CloseLink();

    //链路就绪（IO 线程）：接管一个引用
    void SetReady(p2p::IP2PLink* link);

    //建链失败（IO 线程）
    void SetFailed(int code);

    //就绪后断开（IO 线程）：通知已登记的各条流通道
    void SetClosed(int reason);

    /**
     *  等链路结果。
     *  @param timeoutMs 最长等待（毫秒），0 表示不等、直接返回当前状态
     *  @return 当前状态，取值见 slot_state；超时时为 slot_waiting
     */
    int Wait(int timeoutMs);

    //当前状态，取值见 slot_state
    int State() const;

    //就绪的链路（不转移引用）；没有时返回 NULL
    p2p::IP2PLink* Link() const;

    /**
     *  链路实际路径（2026-10-04 起，供「P2P」徽章判断数据是否直连）。可在任意线程上调用。
     *  @return 链路就绪时返回链路统计里的路径（p2p_link_path：1 局域网直连、2 打洞直连、3 经中转）；
     *          还在等、建链失败或已关闭时返回 0
     */
    int LinkPath() const;

    /**
     *  登记一条通道并在链路上打开同号、指定类型的通道。
     *  @param channel   通道对象（借用，关闭时注销）
     *  @param channelId 通道号，两端约定
     *  @param kind      通道类型，取 p2p::P2PChannelKind（帧通道用可靠通道，数据报通道用数据报通道）
     *  @return 打开成功返回 true；链路未就绪、通道号已登记或 DLL 拒绝打开时返回 false
     */
    bool AddChannel(IP2PSlotChannel* channel, uint16_t channelId, int kind);

    //注销一条通道：之后不再转发给它（正在转发的那一次完成之后才返回）
    void RemoveChannel(uint16_t channelId);

    //以下为 IP2PChannelSink 的实现，在 DLL 的 IO 线程上被调用

    /**
     *  收到某条通道上的一块字节流：转给登记着的流通道。
     *  @param channelId 通道号
     *  @param data      数据，只在本次调用期间有效
     *  @param len       长度（字节）
     */
    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) override;

    /**
     *  某条通道的发送队列降下来了：转给登记着的流通道。
     *  @param channelId 通道号
     */
    virtual void OnWritable(uint16_t channelId) override;

    //是否已被某次业务认领
    bool IsClaimed() const;

    //标记为已认领（P2PLinkBroker::ClaimSlot 调用）
    void MarkClaimed();

    //创建时刻（单调时钟毫秒）
    int64_t CreatedMs() const { return m_createdMs; }

    //功能会话号
    const std::string& FeatureSessionId() const { return m_featureSessionId; }

private:
    P2PLinkSlot(const P2PLinkSlot&);
    P2PLinkSlot& operator=(const P2PLinkSlot&);

private:
    std::string                              m_featureSessionId;   //功能会话号
    int64_t                                  m_createdMs;          //创建时刻（单调时钟毫秒）
    mutable std::mutex                       m_mutex;      //保护以下成员；转发数据给流通道时也持有它
    std::condition_variable                  m_cv;         //结果到达时唤醒 Wait
    int                                      m_state;      //状态，取值见 slot_state
    p2p::IP2PLink*                           m_link;       //就绪的链路（持有一个引用）；没有时为 NULL
    bool                                     m_claimed;    //是否已被某次业务认领
    bool                                     m_linkClosed; //链路是否已被业务关闭（CloseLink），析构时不再关闭
    std::map<uint16_t, IP2PSlotChannel*>     m_channels;   //已登记的通道：通道号 → 通道对象（借用）
};

/**
 *  业务此刻用的链路实际路径（2026-10-04 起，供「P2P」徽章）：业务正走 P2P 链路时报链路槽的路径，走中转或没连上时报 0。
 *  远程桌面、通话、屏幕共享都按自己的路径状态调用它，判断口径只有这一份。可在任意线程上调用，不等待。
 *  @param usingP2P 业务当前是否走 P2P 链路（例如 RdTransport::Path() == rd_path_p2p）
 *  @param slot     业务认领的链路槽；为空表示没有
 *  @return usingP2P 为 true 且链路槽就绪时返回 P2PLinkSlot::LinkPath()；其余返回 0
 */
int LinkPathWhenUsing(bool usingP2P, const std::shared_ptr<P2PLinkSlot>& slot);

/**
 *  流通道的回调：由业务实现。两个方法都在流通道的转交线程上调用（不是 DLL 的 IO 线程），可以阻塞，但阻塞期间
 *  后面的帧排队等待。
 *  析构函数受保护：流通道只借用。
 */
class IP2PStreamHandler
{
public:
    /**
     *  收到一帧。
     *  @param data 帧数据，只在本次调用期间有效
     *  @param len  帧长度（字节），可以为 0
     */
    virtual void OnStreamFrame(const unsigned char* data, size_t len) = 0;

    //链路断开或字节流错位（已收到的帧先交完）：只回调一次；本端调用 Close 关闭时不回调
    virtual void OnStreamClosed() = 0;

    //转交线程刚启动、还没交任何帧时调用一次（如在本线程上初始化 COM 与 Media Foundation，好在本线程解码）；默认什么也不做
    virtual void OnStreamThreadStart()
    {
    }

    //转交线程退出前调用一次（与 OnStreamThreadStart 配对释放）；在回调里调用 Close 而使线程分离时不调用。默认什么也不做
    virtual void OnStreamThreadExit()
    {
    }

protected:
    ~IP2PStreamHandler()
    {
    }
};

/**
 *  链路上的一条帧通道：通道号由两端业务约定的可靠通道。收到的字节流按长度字段拆成帧，由本通道的转交线程交给
 *  业务；发送时在帧前加长度字段，发送队列满时等可写通知。
 *  线程：Open / Close 在业务的线程上调用；SendFrame 可以在多个线程上调用（内部串行化，一帧整体交出）；
 *  OnChannel* 与 OnLinkBroken 由链路槽在 DLL 的 IO 线程上调用。
 */
class P2PStreamChannel : public IP2PSlotChannel
{
public:
    /**
     *  @param slot          链路槽（共享所有权：本对象存续期间链路不会被关闭）
     *  @param maxFrameBytes 单帧上限（字节）：发送超过它的帧被拒绝；收到的长度字段超过它时按字节流错位处理
     *  @param sendWaitMs    发送队列满时等可写通知的最长时间（毫秒），超过即判通道异常
     */
    P2PStreamChannel(const std::shared_ptr<P2PLinkSlot>& slot, size_t maxFrameBytes, int sendWaitMs);

    //析构：等同 Close
    ~P2PStreamChannel();

    /**
     *  打开：在链路上打开同号的可靠通道，起转交线程。只能调用一次。
     *  @param channelId 通道号，两端约定
     *  @param handler   回调（借用，须在 Close 返回之前一直有效），不可为 NULL
     *  @return 打开成功返回 true；链路未就绪或通道号已被占用时返回 false
     */
    bool Open(uint16_t channelId, IP2PStreamHandler* handler);

    /**
     *  发一帧：加长度字段后在可靠通道上发出；发送队列满时等可写通知，最多 sendWaitMs。
     *  @param data 帧数据（len 为 0 时可以为 NULL）
     *  @param len  帧长度（字节），不超过构造时给的单帧上限
     *  @return 已交给链路返回 true；未打开、已断开、超长、等可写超时或链路拒绝时返回 false
     */
    bool SendFrame(const unsigned char* data, size_t len);

    //注销通道、停转交线程（幂等）；之后不再回调业务。可以在回调里调用（此时转交线程分离，不等自己）
    void Close();

    //是否打开着且链路没有断开
    bool IsOpen() const;

    //通道号；Open 之前为 0
    uint16_t ChannelId() const { return m_channelId; }

    //功能会话号（日志用）
    const std::string& FeatureSessionId() const { return m_slot->FeatureSessionId(); }

    //以下为 IP2PSlotChannel 的实现，由链路槽在 DLL 的 IO 线程上、持有它的互斥量时调用

    /**
     *  收到本通道的一块字节流：拆出完整的帧排进队列。
     *  @param data 数据
     *  @param len  长度（字节）
     */
    virtual void OnChannelData(const uint8_t* data, uint32_t len) override;

    //本通道的发送队列降下来了：唤醒等可写的发送方
    virtual void OnChannelWritable() override;

    //链路断开：转交线程交完已收到的帧后向业务报告断开
    virtual void OnLinkBroken() override;

private:
    P2PStreamChannel(const P2PStreamChannel&);
    P2PStreamChannel& operator=(const P2PStreamChannel&);

    //转交线程：先调用业务的线程开始钩子，再转交，退出前调用线程退出钩子
    void DeliverProc();

    //转交循环：按顺序把帧交给业务；链路断开（且不是本端关闭）时报告一次断开
    void DeliverLoop();

private:
    std::shared_ptr<P2PLinkSlot> m_slot;            //链路槽（共享所有权）
    const size_t                 m_maxFrameBytes;   //单帧上限（字节）
    const int                    m_sendWaitMs;      //发送队列满时等可写通知的最长时间（毫秒）
    uint16_t                     m_channelId;       //通道号；Open 之前为 0
    bool                         m_registered;      //是否已在链路槽上登记（Close 时据此注销）
    IP2PStreamHandler*           m_handler;         //业务的回调（借用）
    std::atomic<bool>            m_open;            //是否已打开（Open 成功、尚未 Close）
    std::mutex                   m_mutex;           //保护下面的接收缓冲、帧队列与标志
    std::condition_variable      m_cv;              //帧到达、可写、关闭、断开时唤醒
    bool                         m_closing;         //本端是否已关闭
    std::atomic<bool>            m_broken;          //链路是否已断开（或字节流错位）
    uint64_t                     m_writableSeq;     //收到可写通知的次数（发送方据此判断是否有新的可写通知）
    std::string                  m_inBuf;           //还没拼成完整帧的字节
    std::deque<std::string>      m_frames;          //待转交的帧
    std::mutex                   m_sendMutex;       //一帧整体交出，不与别的帧交错
    std::thread                  m_thread;          //转交线程
    std::atomic<bool>            m_detached;        //本端在回调里关闭、转交线程已分离：线程退出时不再调用 OnStreamThreadExit
};

/**
 *  数据报通道的回调：由业务实现。两个方法都在数据报通道的转交线程上调用（不是 DLL 的 IO 线程），可以做解码等工作，
 *  但阻塞期间后面的消息排队，排满了丢最旧的。
 *  析构函数受保护：数据报通道只借用。
 */
class IP2PDatagramHandler
{
public:
    /**
     *  收到一条完整的消息。
     *  @param data 消息数据，只在本次调用期间有效
     *  @param len  消息长度（字节）
     */
    virtual void OnDatagram(const unsigned char* data, size_t len) = 0;

    //链路断开（已收到的消息先交完）：只回调一次；本端调用 Close 关闭时不回调
    virtual void OnDatagramClosed() = 0;

protected:
    ~IP2PDatagramHandler()
    {
    }
};

/**
 *  链路上的一条数据报通道：通道号由两端业务约定。一次 Send 的数据整条交给链路，DLL 按路径分片（UDP 路径一次最多约
 *  1.1 MB），接收端收齐后整条交出，任何一片丢失整条丢弃，不重传。收到的消息由本通道的转交线程按到达顺序交给业务；
 *  排队的消息超过上限时丢最旧的，并计入丢弃数。
 *  线程：Open / Close 在业务的线程上调用；Send 可以在任意线程上调用；IP2PSlotChannel 的方法由链路槽在 IO 线程上调用。
 */
class P2PDatagramChannel : public IP2PSlotChannel
{
public:
    /**
     *  @param slot             链路槽（共享所有权：本对象存续期间链路不会被关闭）
     *  @param maxQueuedMessages 排队等待转交的消息数上限（不小于 1）；超过时丢最旧的
     */
    P2PDatagramChannel(const std::shared_ptr<P2PLinkSlot>& slot, size_t maxQueuedMessages);

    //析构：等同 Close
    ~P2PDatagramChannel();

    /**
     *  打开：在链路上打开同号的数据报通道，起转交线程。只能调用一次。
     *  @param channelId 通道号，两端约定
     *  @param handler   回调（借用，须在 Close 返回之前一直有效），不可为 NULL
     *  @return 打开成功返回 true；链路未就绪或通道号已被占用时返回 false
     */
    bool Open(uint16_t channelId, IP2PDatagramHandler* handler);

    /**
     *  发一条消息，不等待。
     *  @param data 消息数据（len 为 0 时可以为 NULL）
     *  @param len  消息长度（字节）
     *  @return p2p::P2PSendResult：p2p_send_ok 已交给链路；p2p_send_too_large 超过当前路径一次发送的上限；
     *          未打开、已关闭或链路已断开时为 p2p_send_closed
     */
    int Send(const unsigned char* data, size_t len);

    //注销通道、停转交线程（幂等）；之后不再回调业务。可以在回调里调用（此时转交线程分离，不等自己）
    void Close();

    //是否打开着且链路没有断开
    bool IsOpen() const;

    //因排队超过上限而丢弃的消息数（累计）
    uint64_t DroppedCount() const { return m_dropped; }

    //以下为 IP2PSlotChannel 的实现，由链路槽在 DLL 的 IO 线程上、持有它的互斥量时调用

    /**
     *  收到一条完整的消息：排进队列，满了先丢最旧的。
     *  @param data 消息数据
     *  @param len  长度（字节）
     */
    virtual void OnChannelData(const uint8_t* data, uint32_t len) override;

    //数据报通道没有发送队列，不会收到可写通知
    virtual void OnChannelWritable() override;

    //链路断开：转交线程交完已收到的消息后向业务报告断开
    virtual void OnLinkBroken() override;

private:
    P2PDatagramChannel(const P2PDatagramChannel&);
    P2PDatagramChannel& operator=(const P2PDatagramChannel&);

    //转交线程：按顺序把消息交给业务；链路断开（且不是本端关闭）时报告一次断开
    void DeliverProc();

private:
    std::shared_ptr<P2PLinkSlot> m_slot;              //链路槽（共享所有权）
    const size_t                 m_maxQueued;         //排队消息数上限
    uint16_t                     m_channelId;         //通道号；Open 之前为 0
    bool                         m_registered;        //是否已在链路槽上登记（Close 时据此注销）
    IP2PDatagramHandler*         m_handler;           //业务的回调（借用）
    std::atomic<bool>            m_open;              //是否已打开（Open 成功、尚未 Close）
    std::mutex                   m_mutex;             //保护下面的队列与标志
    std::condition_variable      m_cv;                //消息到达、关闭、断开时唤醒
    bool                         m_closing;           //本端是否已关闭
    std::atomic<bool>            m_broken;            //链路是否已断开
    std::deque<std::string>      m_messages;          //待转交的消息
    std::atomic<uint64_t>        m_dropped;           //因排队超过上限而丢弃的消息数
    std::thread                  m_thread;            //转交线程
};

/**
 *  一个用途的业务方：把 DLL 交来的链路结果放进链路槽，等业务认领。每个用途一个实例（主程序的文件夹传输、文件共享
 *  DLL、媒体 DLL 各自持有自己的实例）。
 *  线程：Register / Unregister / ClaimSlot 在业务的线程上调用；IP2PLinkHandler 的回调在 DLL 的 IO 线程上；全部加锁。
 */
class P2PLinkBroker : public p2p::IP2PLinkHandler
{
public:
    /**
     *  @param purpose 用途名（ASCII，1~p2p::kMaxPurposeChars 个字符），与服务端建 P2P 会话时的 purpose 一致
     */
    explicit P2PLinkBroker(const std::string& purpose);

    //析构：不自动注销，持有者须在 P2P 模块关闭之前调用 Unregister
    virtual ~P2PLinkBroker();

    //登记的用途
    const std::string& Purpose() const { return m_purpose; }

    /**
     *  向 P2P 服务登记本实例的用途（P2P 模块加载成功之后调用）。已登记时直接返回 true。
     *  @param service P2P 服务（借用，须在 Unregister 之前一直有效）
     *  @return 登记成功返回 true；service 为 NULL 或 DLL 拒绝（用途名非法、已被别的业务方占用）时返回 false
     */
    bool Register(p2p::IP2PService* service);

    //注销（P2P 模块关闭之前调用）；未登记时无效果。未被认领的槽随之释放（关闭其链路）
    void Unregister();

    //是否已登记（P2P 可用）
    bool IsRegistered() const;

    /**
     *  为一次业务认领链路槽：该会话号上有还没被认领的槽（链路先到）就认领它，否则新建一个等结果；已被认领过的旧槽
     *  （上一轮的链路）被替换，它的链路由上一轮的使用者释放时关闭。
     *  @param featureSessionId 功能会话号
     *  @return 链路槽（调用方持有；最后一个持有者释放时关闭链路）
     */
    std::shared_ptr<P2PLinkSlot> ClaimSlot(const std::string& featureSessionId);

    /**
     *  业务不再需要某个会话号的链路（一对一通话在响铃阶段就建链，没接通就结束时调用）：该会话号上已就绪的链路
     *  立即关闭；还没建好的，之后 DLL 交来时直接关闭、不再进链路槽。记录保留 kP2PUnclaimedLinkTtlMs。
     *  @param featureSessionId 功能会话号
     */
    void Abandon(const std::string& featureSessionId);

    //以下为 IP2PLinkHandler 的实现，都在 DLL 的 IO 线程上被调用

    /**
     *  链路就绪：放进该会话号的链路槽（没有槽时新建一个等认领）。
     *  @param featureSessionId 功能会话号
     *  @param link             链路（回调期间有效，槽会增加一个引用）
     */
    virtual void OnLinkReady(const char* featureSessionId, p2p::IP2PLink* link) override;

    /**
     *  建链失败：告诉等待中的业务，由业务决定退回自己的中转。
     *  @param featureSessionId 功能会话号
     *  @param code             失败原因
     */
    virtual void OnLinkFailed(const char* featureSessionId, int code) override;

    /**
     *  就绪后断开：告诉使用这条链路的各条流通道。
     *  @param featureSessionId 功能会话号
     *  @param reason           断开原因
     */
    virtual void OnLinkClosed(const char* featureSessionId, int reason) override;

private:
    P2PLinkBroker(const P2PLinkBroker&);
    P2PLinkBroker& operator=(const P2PLinkBroker&);

    /**
     *  取该会话号上处在 wantState 状态的链路槽（先找已认领的，再找未认领的）。调用方持有 m_mutex。
     *  @param featureSessionId 会话号
     *  @param wantState        要找的槽状态（P2PLinkSlot::slot_state）
     *  @param create           找不到时是否新建一个未认领的槽（替换同号的旧未认领槽）
     *  @param expired          [出参] 新建时清理出的过期槽，见 PurgeLocked
     *  @return 槽；找不到且不新建时为空
     */
    std::shared_ptr<P2PLinkSlot> SlotLocked(const std::string& featureSessionId, int wantState, bool create,
                                            std::vector<std::shared_ptr<P2PLinkSlot> >& expired);

    /**
     *  清理过期未认领的槽与已失效的弱引用。调用方持有 m_mutex。
     *  @param nowMs   当前时间（单调时钟毫秒）
     *  @param expired [出参] 过期的槽移到这里，由调用方在释放 m_mutex 之后释放：槽析构时关闭链路，而 IP2PLink::Close
     *                 要等 IO 线程上正在进行的回调结束，那个回调可能正等着 m_mutex
     */
    void PurgeLocked(int64_t nowMs, std::vector<std::shared_ptr<P2PLinkSlot> >& expired);

private:
    const std::string                                     m_purpose;     //登记的用途，构造时确定
    mutable std::mutex                                    m_mutex;       //保护以下成员
    p2p::IP2PService*                                     m_service;     //已登记的 P2P 服务（借用）；未登记时为 NULL
    std::map<std::string, std::shared_ptr<P2PLinkSlot> >  m_unclaimed;   //未被认领的槽（链路先到）：会话号 → 槽
    std::map<std::string, std::weak_ptr<P2PLinkSlot> >    m_claimed;     //已被认领的槽：会话号 → 槽（业务持有强引用）
    std::map<std::string, int64_t>                        m_abandoned;   //业务已放弃的会话号 → 放弃时刻（单调时钟毫秒）
};

/**
 *  单调时钟的当前时间（毫秒），链路槽的创建时刻与各业务的等待计时共用。
 *  @return 毫秒
 */
int64_t P2PSteadyNowMs();

} // namespace balloonp2p
