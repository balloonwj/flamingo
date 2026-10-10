/**
 *  FlamingoP2P.dll 与宿主 Flamingo.exe 之间的接口约定。
 *
 *  FlamingoP2P.dll 在两个在线客户端之间建立点对点链路：先在局域网内直连或跨 NAT 打洞，打不通时自动改走
 *  imgserver 的一对一中转，业务方看到的接口不变（方案见 docs/P2P通信方案.md）。exe 在启动时用 LoadLibrary
 *  动态加载它，经唯一的导出函数 FlamingoP2P_Create 取得 IP2PModule；exe 自己实现 IP2PHost 交给 DLL，DLL 经它
 *  写日志、经主连接收发 2300 段信令。文件夹传输、文件共享、通话等业务方经 IP2PModule::GetService 取得
 *  IP2PService，按「用途」登记自己，接收建好的链路。DLL 不存在或加载失败时 exe 照常运行，各业务继续走原来的中转。
 *
 *  接口形态与 FlamingoFileShare.dll（FlamingoFileShare/FileShareApi.h）保持一致：工厂函数只接收接口版本号、
 *  返回归 DLL 所有的模块对象，模块再经 Init 接收宿主接口。宿主侧用通用加载器 Source/App/PluginModuleLoader.h 加载。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此本文件中的接口只使用纯虚类与基本类型：
 *    - 字符串一律由调用方持有，只在本次调用期间有效，被调方需要保留时自行拷贝；
 *    - 不传递 STL 对象，不在一侧分配、另一侧释放内存；
 *    - 异常不越过模块边界，每个接口实现的最外层自行处理；
 *    - IP2PModule 与 IP2PService 的实现对象由 DLL 自己持有（进程内唯一），exe 不负责释放。
 *
 *  修改规则：任何一侧增删接口方法、改动参数或结构体布局，都必须把 kApiVersion 加 1。
 *  加载时双方版本号必须完全相同（见 IsApiVersionCompatible），不做兼容旧版本的处理。
 *
 *  典型用法（exe 侧）：
 *
 *      balloonapp::CPluginModuleLoader<p2p::IP2PModule> loader;
 *      if (loader.Load(path, FLAMINGO_P2P_FACTORY_NAME, p2p::kApiVersion) == balloonapp::plugin_load_ok)
 *          loader.GetModule()->Init(&host, &params);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace p2p {

//接口版本号。exe 与 DLL 编译时都取这一个值；加载时 exe 把它传给 FlamingoP2P_Create，
//DLL 发现与自己的值不同就拒绝创建模块。
//  1：第 1b 步，模块、宿主、服务与业务方回调；
//  2：第 3d 步，给出 IP2PLink、IP2PChannelSink 与 P2PLinkStats 的完整定义。
const uint32_t kApiVersion = 2;

//P2P 信令的号段（Protocol.h 的 cmd_p2p_* 2301~2306 都落在其中）。主程序据此把收到的帧
//原样转交本模块，本模块也只接受这一段的命令。
const uint16_t kP2PCmdFirst = 2300;
const uint16_t kP2PCmdLast  = 2399;

//业务方登记用途时，用途名的最大字符数（不含结尾 '\0'）。用途名是 cmd_p2p_session_notify 里 purpose
//字段的取值（folder、fileshare、call、rd、screenshare、test），取 31 留足余量
const uint32_t kMaxPurposeChars = 31;

//DLL 文件名，与 exe 放在同一目录。
#define FLAMINGO_P2P_DLL_NAME       L"FlamingoP2P.dll"

//导出的工厂函数名（GetProcAddress 使用 ANSI 名称）。
#define FLAMINGO_P2P_FACTORY_NAME   "FlamingoP2P_Create"

//DLL 经 IP2PHost::Log 写日志时的级别。
enum P2PLogLevel
{
    p2p_log_info    = 1,   //普通信息：模块初始化、会话建立与结束、选定路径等关键路径
    p2p_log_warning = 2,   //警告：可以继续运行的异常情况，如收到无法解析的信令
    p2p_log_error   = 3,   //错误：某项操作无法继续，如建链失败、回落中转也失败
};

//IP2PModule::Init 的参数。exe 填好后传入，DLL 只在 Init 期间读取。
struct P2PInitParams
{
    uint32_t       m_cbSize;   //本结构体的字节数，取 sizeof(P2PInitParams)；DLL 据此拒绝布局不符的调用方
    const wchar_t* m_appDir;   //exe 所在目录（不带结尾反斜杠）；不可为 NULL
};

/**
 *  判断一个命令号是否属于 P2P 信令的号段。
 *  @param cmd 命令号
 *  @return 落在 [kP2PCmdFirst, kP2PCmdLast] 内返回 true
 */
inline bool IsP2PCommand(uint32_t cmd)
{
    return cmd >= kP2PCmdFirst && cmd <= kP2PCmdLast;
}

/**
 *  判断宿主与 DLL 的接口版本是否兼容。目前要求完全相同。
 *  @param hostVersion   宿主编译时的 kApiVersion
 *  @param moduleVersion DLL 编译时的 kApiVersion
 *  @return 兼容返回 true
 */
inline bool IsApiVersionCompatible(uint32_t hostVersion, uint32_t moduleVersion)
{
    return hostVersion == moduleVersion;
}

/**
 *  宿主接口：由 exe 实现，DLL 调用。
 *
 *  所有方法都可能在 DLL 的任意线程上被调用（日志尤其如此），实现方须保证线程安全。
 *  析构函数受保护：DLL 只使用该对象，不负责销毁它。
 */
class IP2PHost
{
public:
    /**
     *  写一行日志到 exe 的日志文件。
     *  @param level 日志级别，取值见 enum P2PLogLevel；不认识的值按普通信息处理
     *  @param text  日志正文（UTF-8，按代码规范为英文），以 '\0' 结尾；为 NULL 时忽略
     */
    virtual void Log(int level, const char* text) = 0;

    /**
     *  经主连接（chatserver）发送一条 P2P 信令。
     *  @param cmd  命令号，必须属于 [kP2PCmdFirst, kP2PCmdLast]
     *  @param json 包体（UTF-8 JSON），不要求以 '\0' 结尾
     *  @param len  包体字节数
     *  @return 已放入发送队列时返回本帧的 seq（大于 0），应答帧带回同一个 seq 供配对；
     *          未连接、命令号不在号段内或参数非法时返回 0
     */
    virtual uint32_t SendP2PCommand(uint16_t cmd, const char* json, uint32_t len) = 0;

    /**
     *  取 chatserver 的公钥，DLL 回落中转时与 imgserver 握手要用它做逐跳加密。
     *  @return 指向 32 字节公钥的指针，在进程生命周期内有效；尚未取得公钥时返回 NULL
     */
    virtual const unsigned char* GetServerPublicKey() = 0;

protected:
    ~IP2PHost()
    {
    }
};

//通道的类型（IP2PLink::OpenChannel 的 kind）
enum P2PChannelKind
{
    p2p_channel_reliable = 1,   //可靠有序的字节流：文件传输、控制信令。UDP 路径上经 KCP（BBR 类拥塞控制），中转路径上直接透传；
                                //发送队列超过 4 MiB 时 Send 返回 p2p_send_would_block
    p2p_channel_datagram = 2,   //数据报：音视频。不重传，丢了就丢了；一次 Send 的数据由 DLL 分片，接收端收齐后整块交出，任何
                                //一片丢失整块丢弃；UDP 路径一次最多约 1.1 MB
};

//IP2PLink::Send 与 OpenChannel 的返回值
enum P2PSendResult
{
    p2p_send_ok          = 0,   //成功：已交给链路发送（OpenChannel：通道已打开）
    p2p_send_would_block = 1,   //发送队列已满，稍后重试；队列降下来后回调 IP2PChannelSink::OnWritable
    p2p_send_too_large   = 2,   //数据超过该通道单次发送的上限
    p2p_send_closed      = 3,   //链路已断开或已关闭
    p2p_send_bad_channel = 4,   //通道没有打开、类型不支持，或通道号已被占用（OpenChannel）
};

//链路的路径类型（P2PLinkStats::m_path），与 Protocol.h 的 p2p_path 取值一致
enum P2PLinkPath
{
    p2p_link_path_lan     = 1,   //经本机网卡地址直连（同一局域网或同一 VPN 内）
    p2p_link_path_punched = 2,   //经公网映射地址打洞直连
    p2p_link_path_relay   = 3,   //经 imgserver 的一对一中转
};

//链路的统计（IP2PLink::GetStats 的出参）
struct P2PLinkStats
{
    uint32_t m_cbSize;          //结构体大小，调用方填 sizeof(P2PLinkStats)，便于以后在末尾追加字段
    int      m_path;            //路径类型，取值见 P2PLinkPath
    uint32_t m_rttMs;           //最近的往返时延（毫秒）；还没有样本时为 0
    uint32_t m_lossPermille;    //近期的丢包率（千分比）；还没有统计时为 0
    uint64_t m_queuedBytes;     //发送队列里积压的字节数
    uint64_t m_bytesSent;       //累计发出的业务字节数
    uint64_t m_bytesReceived;   //累计收到的业务字节数
};

/**
 *  一条通道的接收端：业务方实现，经 IP2PLink::OpenChannel 交给链路。
 *  回调都发生在 DLL 的 IO 线程上：不得阻塞，需要做耗时工作时自行投递到别的线程。回调时 DLL 不持有链路内部的锁，
 *  回调里可以调用 IP2PLink 的任何方法（第 4a 步起）。
 *  析构函数受保护：DLL 只使用该对象，不负责销毁它。
 */
class IP2PChannelSink
{
public:
    /**
     *  收到一块数据。
     *  @param channelId 通道号
     *  @param data      数据，只在本次调用期间有效
     *  @param len       数据长度（字节）
     */
    virtual void OnData(uint16_t channelId, const uint8_t* data, uint32_t len) = 0;

    /**
     *  发送队列已降到上限以下（此前 Send 返回过 p2p_send_would_block），可以继续发送。
     *  @param channelId 通道号
     */
    virtual void OnWritable(uint16_t channelId) = 0;

protected:
    ~IP2PChannelSink()
    {
    }
};

/**
 *  一条建好的 P2P 链路：直连或回落中转，业务方看到的接口相同。
 *  引用计数管理：OnLinkReady 交出的指针在回调期间有效，业务方需要之后继续使用时先 AddRef，用完 Release；
 *  链路断开后对象仍然有效（Send 返回 p2p_send_closed），直到最后一个引用释放。
 *  所有方法都可以在任意线程上调用。
 */
class IP2PLink
{
public:
    /**
     *  打开一条通道。通道号由双方业务约定，两端各自打开同一个通道号后即可收发。
     *  @param channelId 通道号
     *  @param kind      通道类型，取值见 P2PChannelKind
     *  @param sink      接收端（借用，须在 Close 或链路断开之前一直有效），不可为 NULL
     *  @return P2PSendResult：p2p_send_ok 表示已打开
     */
    virtual int OpenChannel(uint16_t channelId, int kind, IP2PChannelSink* sink) = 0;

    /**
     *  在一条已打开的通道上发送一块数据。
     *  @param channelId 通道号
     *  @param data      数据，只在本次调用期间有效（链路自行拷贝）
     *  @param len       数据长度（字节）
     *  @return P2PSendResult
     */
    virtual int Send(uint16_t channelId, const uint8_t* data, uint32_t len) = 0;

    /**
     *  取链路的统计。
     *  @param out [出参] 调用方先把 m_cbSize 填为 sizeof(P2PLinkStats)
     */
    virtual void GetStats(P2PLinkStats* out) = 0;

    /**
     *  关闭链路：通知对方并结束 P2P 会话（经 chatserver 发 cmd_p2p_close）。之后不再回调任何通道。
     *  在回调之外的线程上调用时，要等 DLL 的 IO 线程上正在进行的回调结束才返回，返回之后即可销毁各通道的接收端；
     *  因此不要在持有某把锁时调用它，而回调里又要取同一把锁。在回调里调用时立即返回（不等自己）。
     *  @param reason 关闭原因，取 Protocol.h 的 p2p_close_reason（通常为 p2p_close_business_end）
     */
    virtual void Close(int reason) = 0;

    //增加一个引用
    virtual void AddRef() = 0;

    //释放一个引用；最后一个引用释放后对象销毁
    virtual void Release() = 0;

protected:
    ~IP2PLink()
    {
    }
};

/**
 *  业务方实现的回调接口：经 IP2PService::RegisterHandler 按用途登记，接收该用途的链路。
 *
 *  回调都发生在 DLL 的 IO 线程上，实现方不得在回调里阻塞，需要做耗时工作时自行投递到别的线程。
 *  析构函数受保护：DLL 只使用该对象，不负责销毁它。
 */
class IP2PLinkHandler
{
public:
    /**
     *  一条链路已经建好（直连或回落中转成功）。
     *  @param featureSessionId 业务会话号（cmd_p2p_session_notify 的 featuresessionid，UTF-8），只在本次调用期间有效
     *  @param link             链路对象；业务方需要在回调之后继续使用时，按 IP2PLink 的约定增加引用
     */
    virtual void OnLinkReady(const char* featureSessionId, IP2PLink* link) = 0;

    /**
     *  建链失败：打洞超时，回落中转也没有成功。业务方据此改走原来的中转或报告失败。
     *  @param featureSessionId 业务会话号（UTF-8），只在本次调用期间有效
     *  @param code             失败原因，取 Protocol.h 的 error_code（900 段或通用错误码）
     */
    virtual void OnLinkFailed(const char* featureSessionId, int code) = 0;

    /**
     *  一条已经建好的链路断开了。
     *  @param featureSessionId 业务会话号（UTF-8），只在本次调用期间有效
     *  @param reason           断开原因，取 Protocol.h 的 p2p_close_reason
     */
    virtual void OnLinkClosed(const char* featureSessionId, int reason) = 0;

protected:
    ~IP2PLinkHandler()
    {
    }
};

/**
 *  P2P 服务接口：由 DLL 实现，业务方（exe 内的文件夹传输，以及经宿主转交的另外两个 DLL）调用。
 *
 *  所有方法都可以在任意线程上调用，实现方负责线程安全。
 *  析构函数受保护：对象归 DLL 所有，调用方不能删除它。
 */
class IP2PService
{
public:
    /**
     *  按用途登记业务方。同一用途同时只能有一个业务方。登记或注销之后 DLL 会重新向服务端报到已登记的用途：服务端只为
     *  两端都登记了的用途建会话，所以业务方应在登录之前登记，且只在确实能接住该用途的链路时登记。
     *  @param purpose 用途名（ASCII，如 "folder"），以 '\0' 结尾，不超过 kMaxPurposeChars 个字符，不可为空串
     *  @param handler 回调接口（不转移所有权；须在 UnregisterHandler 返回之前一直有效），不可为 NULL
     *  @return 登记成功返回 true；参数非法、该用途已有业务方登记或模块未初始化时返回 false
     */
    virtual bool RegisterHandler(const char* purpose, IP2PLinkHandler* handler) = 0;

    /**
     *  注销一个用途的业务方。返回之后 DLL 不会再回调该业务方。
     *  @param purpose 用途名，规则同 RegisterHandler
     *  @return 注销成功返回 true；该用途没有登记或参数非法时返回 false
     */
    virtual bool UnregisterHandler(const char* purpose) = 0;

protected:
    ~IP2PService()
    {
    }
};

/**
 *  模块接口：由 DLL 实现，exe 调用。
 *
 *  除特别说明外，所有方法都只能在 exe 的 UI 线程上调用。
 *  析构函数受保护：对象归 DLL 所有，exe 不能删除它。
 */
class IP2PModule
{
public:
    /**
     *  取模块实现的接口版本号。
     *  @return 编译 DLL 时的 kApiVersion
     */
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  初始化模块：登记宿主、初始化 DLL 内的 libsodium。重复调用时直接返回 true，不会重复初始化。
     *  @param host   宿主接口（不转移所有权；须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @param params 初始化参数，只在本次调用期间读取，不可为 NULL，m_cbSize 须等于 sizeof(P2PInitParams)
     *  @return 成功返回 true；参数非法或 libsodium 初始化失败返回 false
     */
    virtual bool Init(IP2PHost* host, const P2PInitParams* params) = 0;

    /**
     *  关闭模块：结束全部链路、清空业务方登记，之后不再调用宿主接口。
     *  未初始化时调用、重复调用都是安全的。卸载 DLL 之前必须先调用本方法。
     */
    virtual void Shutdown() = 0;

    /**
     *  通知模块：登录成功（首次登录与断线重连成功都会调用）。模块据此向服务端报到（cmd_p2p_register）。
     *  未初始化时调用无效果。
     */
    virtual void OnLoggedIn() = 0;

    /**
     *  通知模块：即将退出登录（含切换账号）。模块据此结束全部链路。
     *  未初始化时调用无效果。
     */
    virtual void OnLoggingOut() = 0;

    /**
     *  把主连接收到的一帧 P2P 信令交给模块。<u>在 exe 的网络接收线程上调用</u>：
     *  模块只拷贝包体并交给自己的线程处理，立即返回，不在调用线程上做任何耗时工作。
     *  @param cmd  命令号
     *  @param seq  帧序号；应答帧与对应请求的 seq 相同，推送帧为 0
     *  @param body 包体（UTF-8 JSON），不要求以 '\0' 结尾，只在本次调用期间有效
     *  @param len  包体字节数；P2P 信令都带 JSON 包体，为 0 视为非法
     *  @return 模块接收了这一帧返回 true；命令号不在号段内、包体为空、模块未初始化时返回 false
     */
    virtual bool OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) = 0;

    /**
     *  取 P2P 服务接口，供业务方按用途登记。可在任意线程上调用。
     *  @return 服务接口（归 DLL 所有，在 DLL 卸载之前一直有效）；模块未初始化时也返回同一个对象，
     *          此时其上的登记一律失败
     */
    virtual IP2PService* GetService() = 0;

protected:
    ~IP2PModule()
    {
    }
};

} // namespace p2p
