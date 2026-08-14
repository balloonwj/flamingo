/**
 *  文件夹传输中转通道客户端，FolderRelayClient.h
 *
 *  管理一条到 fileserver 的独立 TCP 连接，用作单聊「发送文件夹」的中转通道：连上后
 *  先走三帧协议加密握手，再用 cmd_folder_relay_hello(1857) 携中转令牌与本连接的通道
 *  序号 lane 报到；fileserver 验签、验角色，并按 (sessionid, lane) 把发送端与接收端的
 *  同序号连接配成一对。报到成功后在该连接上双向收发 cmd_folder_relay_data(1858) 帧，
 *  其包体是 FolderRelayFraming 定义的「应用帧」，fileserver 不解析包体，只解密后用配
 *  对端的会话密钥重新加密转发，字节不落盘、不入库。
 *
 *  本类与远程桌面中转客户端 RemoteDesktop/RdRelayClient、音视频中转客户端
 *  Call/CallRelayClient 同构（非阻塞 connect、三帧加密握手、报到帧、接收线程、线程
 *  安全发送、Close 幂等），差异有三处：
 *    1. 中转跑在 fileserver <u>现有的监听端口</u>上，与文件上传下载共用同一个端口，
 *       服务端按帧 cmd 分流，不新开端口（见 docs/协议设计-v2.md 12.3）；
 *    2. 报到帧包体比远程桌面多一个 lane 序号。一次文件夹传输的本端要建 1 条控制通道
 *       （lane 0）与 N 条数据通道（lane 1..N，N 默认 4、硬上限 8），<u>本类只表示其中
 *       一条</u>：N+1 条通道由上层各建一个实例，共用同一枚令牌、各报自己的 lane；
 *    3. 应用帧的编解码不属于本类职责。发送时把调用方给的字节原样装进数据帧，接收时
 *       把解密后的包体原样交给上层；清单分段、文件分片、回执等一律由
 *       FolderRelayFraming 与上层状态机负责，本类不看包体内容。
 *
 *  典型用法（每条通道各一个实例，控制通道与数据通道用法相同）：
 *      foldertransfer::FolderRelayClient lane0;
 *      if (!lane0.Connect(ip, port, token, foldertransfer::kFolderControlLane, this))
 *          ...（建通道失败，按整个文件夹传输失败收场）
 *
 *      std::string frame;
 *      foldertransfer::relayframing::BuildManifestAckFrame(ack, frame);
 *      lane0.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size());
 *      ...
 *      lane0.Close();
 *
 *  socket 收发本身不在单元测试覆盖范围（与 RdRelayClient 一致，靠真机联调验证）。报到
 *  帧包体的构造与 lane 序号校验是纯逻辑，故抽成 folderrelay 命名空间下的内联函数，由
 *  Source/Tests/FolderRelayClientTests.cpp 覆盖；做成内联而不是放进 .cpp，是因为本文件
 *  的实现要用 CIUSocket 取服务端公钥，而测试工程不编译 net/IUSocket.cpp，.cpp 里的东西
 *  测试工程链接不到。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <winsock2.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

#include "FolderRelayFraming.h"   // kFolderRelayMaxBodyBytes（单帧包体上限的出处）
#include "../net/FrameCrypto.h"   // net::ClientHandshake / net::STREAM_OVERHEAD_BYTES

namespace foldertransfer {

//==================== 通道常量 ====================

//控制通道的 lane 序号，固定为 0：清单、目录就绪回执、中止与完成只走这一条通道。
//把控制单独拉出来是为了让"整个文件夹中止"这条消息不必排在若干兆字节的文件数据
//之后。出处：docs/单聊文件夹传输方案.md 4.2。
const int kFolderControlLane = 0;

//数据通道条数的硬上限：8 条。实际用几条由服务端配置项 folderlanes 决定（默认 4），
//经 cmd_folder_relay_info 的 lanes 字段下发，客户端不自行决定，只负责不越过这个硬
//上限；服务端对越界的 lane 一律拒绝报到。出处：docs/单聊文件夹传输方案.md 4.2、
//docs/协议设计-v2.md 12.3.1。
const int kFolderMaxDataLanes = 8;

//lane 序号的合法上界（含）。控制通道占 0，数据通道最多占 1..8，故上界即数据通道
//条数的硬上限。
const int kFolderMaxLaneIndex = kFolderMaxDataLanes;

//连接 + 加密握手 + 报到的默认总超时，单位：秒。取 5 秒，与 RdRelayClient、
//CallRelayClient 一致 —— 三者连的是同一批中转服务器，超时口径没有理由各写各的。
const int kFolderRelayConnectTimeoutSec = 5;

//单次 SendAppFrame 允许的应用帧字节数上限。
//中转单帧的<u>线上</u>包体不得超过 kFolderRelayMaxBodyBytes（2 MiB，即 fileserver 的
//MAX_BODY_FILESERVER，一致性由 FolderRelayFraming.cpp 里的 static_assert 锁定），而
//包体加密后会固定膨胀 net::STREAM_OVERHEAD_BYTES 字节，故明文应用帧的上限必须把这
//份膨胀让出来。不让出来的后果是压线的那一帧在 net::encodeFrame 处返回 false，表现为
//"偶尔发送失败"而看不出原因。实际发出的应用帧都远小于此值：文件分片按 512 KiB
//（kFolderRelayChunkBytes）切，清单按 512 KiB 预算分段。
const size_t kFolderRelayMaxAppFrameBytes = kFolderRelayMaxBodyBytes - net::STREAM_OVERHEAD_BYTES;

namespace folderrelay {

//JSON 字符串里可以原样出现的最小字符值：小于它的都是控制字符，须写成 \u00XX 转义。
const unsigned char kJsonMinLiteralChar = 0x20;
//从一个字节里取高低四位时用的位数与掩码（把控制字符写成两位十六进制要用）。
const int           kNibbleBits = 4;
const unsigned char kNibbleMask = 0x0F;

/**
 *  把一段文本按 JSON 字符串的转义规则追加进 out（<u>不含</u>两侧的双引号）。
 *
 *  JSON 规定必须转义的只有三类字符：双引号、反斜杠、以及 0x20 以下的控制字符；UTF-8
 *  的多字节序列可以在 JSON 字符串里原样出现，无须转成 \uXXXX。中转令牌本身只含十六
 *  进制数字、竖线、连字符与点号（见 flamingoserver/utils/RelayToken.h 对 payload 字段
 *  的约定），走不到任何一个转义分支；这里仍做完整转义是防御性的 —— 令牌格式日后若变，
 *  本函数不必跟着改。
 *
 *  @param text 待转义的文本，可以为空串
 *  @param out  [出参] 转义结果<u>追加</u>写入其尾部，函数内部不清空它
 */
inline void AppendJsonEscaped(const std::string& text, std::string& out)
{
    //把控制字符写成 \u00XX 时用的十六进制数字表
    static const char kHexDigits[] = "0123456789abcdef";

    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        //双引号：不转义会让字符串提前结束
        if (ch == '"')
        {
            out += "\\\"";
        }
        //反斜杠：不转义会把它后面那个字符吞成一个转义序列
        else if (ch == '\\')
        {
            out += "\\\\";
        }
        //控制字符：JSON 不允许它们直接出现在字符串里，一律写成 \u00XX
        else if (ch < kJsonMinLiteralChar)
        {
            out += "\\u00";
            out.push_back(kHexDigits[(ch >> kNibbleBits) & kNibbleMask]);
            out.push_back(kHexDigits[ch & kNibbleMask]);
        }
        //其余字节（含 UTF-8 多字节序列的每一个字节）原样写出
        else
        {
            out.push_back(static_cast<char>(ch));
        }
    }
}

/**
 *  构造中转报到帧 cmd_folder_relay_hello(1857) 的包体（紧凑 UTF-8 JSON）。
 *
 *  包体形如 {"token":"...","lane":0}，字段名与 docs/协议设计-v2.md 12.3.1 一致。
 *  本函数只负责格式化，<u>不校验</u> lane 取值是否合法，合法性由 IsValidLaneIndex 判定
 *  ——把"怎么写"与"能不能写"分开，测试才好分别覆盖。
 *
 *  @param token 中转令牌（由 cmd_folder_relay_info 下发，正常非空）；本端 N+1 条连接
 *               共用同一枚，令牌里不含 lane
 *  @param lane  本连接的通道序号：0 为控制通道，1..N 为数据通道
 *  @return 紧凑格式的报到 JSON 文本
 */
inline std::string BuildRelayHelloBody(const std::string& token, int lane)
{
    std::string body;
    body += "{\"token\":\"";
    AppendJsonEscaped(token, body);
    body += "\",\"lane\":";
    body += std::to_string(lane);
    body += "}";
    return body;
}

/**
 *  判断一个 lane 序号是否落在协议允许的范围内。
 *
 *  只判硬上限这一层：真正可用的数据通道条数 N 由服务端在 cmd_folder_relay_info 里下发，
 *  上层建连接时不应超过 N，本函数拦的是"连硬上限都越过了"这种明显错误，避免把注定被
 *  服务端拒绝的连接发出去。
 *
 *  @param lane 待判定的通道序号
 *  @return 落在 [kFolderControlLane, kFolderMaxLaneIndex] 之内返回 true，否则返回 false
 */
inline bool IsValidLaneIndex(int lane)
{
    return lane >= kFolderControlLane && lane <= kFolderMaxLaneIndex;
}

} // namespace folderrelay

//中转通道事件回调接口。
//注意：两个回调都在 FolderRelayClient 的<u>接收线程</u>上触发，实现方须自行保证线程
//安全（典型做法是把数据拷出后 PostMessage 转投 UI 线程，或投进本通道的工作队列），
//不可在回调里做耗时阻塞操作，否则这条通道的收字节会一直被堵着。
//一次文件夹传输的本端有 N+1 条通道、也就有 N+1 条接收线程，同一个 handler 若被多条
//通道共用，其实现必须自己加锁。
class FolderRelayClientHandler
{
public:
    virtual ~FolderRelayClientHandler() {}

    /**
     *  收到一个应用帧（cmd_folder_relay_data 帧的包体，解密后的明文）。
     *  帧内容由 relayframing::DecodeFrame / DecodeFileChunk 解析，本类不看。
     *  @param data 应用帧字节起始地址；<u>仅在本次回调内有效</u>，需留存须自行拷贝
     *  @param len  应用帧字节数
     */
    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len) = 0;

    /**
     *  连接断开（对端关闭 / 网络错误 / 拆帧或解密失败）。
     *  本端主动调用 Close() 关闭时<u>不</u>回调本方法，仅异常断开时回调一次。
     *  按方案约定，任一条通道断开即整个文件夹传输失败，且不做自动重连，故上层收到本
     *  回调后应当停掉本次传输的全部通道并走失败路径。
     */
    virtual void OnFolderRelayClosed() = 0;
};

//文件夹传输中转通道客户端：一条到 fileserver 监听端口的独立连接，加密握手与报到成功
//后在其上双向收发应用帧。一个实例只对应一条通道（一个 lane），不可跨通道复用。
class FolderRelayClient
{
public:
    FolderRelayClient();
    ~FolderRelayClient();

    FolderRelayClient(const FolderRelayClient&) = delete;
    FolderRelayClient& operator=(const FolderRelayClient&) = delete;

    /**
     *  连接 fileserver 的中转端口，走完协议加密握手并完成报到。
     *  <u>阻塞</u>直到成功 / 失败 / 超时，故不应在 UI 线程上直接调用。
     *
     *  @param ip         中转地址（cmd_folder_relay_info 的 relayip 字段，点分十进制
     *                    或主机名）
     *  @param port       中转端口（cmd_folder_relay_info 的 relayport 字段，就是
     *                    fileserver 上传下载用的那个监听端口，不是新端口）
     *  @param token      中转令牌（cmd_folder_relay_info 的 token 字段，非空），作报到
     *                    帧包体提交服务端校验；本端 N+1 条通道共用同一枚
     *  @param lane       本连接的通道序号：0 为控制通道，1..N 为数据通道；越过硬上限
     *                    kFolderMaxLaneIndex 时本函数直接判失败，不发起连接
     *  @param handler    事件回调（非空，<u>不接管所有权</u>；其生命周期须长于本连接，
     *                    至少持续到 Close() 返回）
     *  @param timeoutSec 连接 + 加密握手 + 报到的总超时，单位秒
     *  @return 报到成功（服务端回 code==0）返回 true；参数非法 / 连接失败 / 加密握手
     *          失败 / 令牌非法 / 超时返回 false。返回 false 时 socket 已被关闭，调用方
     *          可以直接丢弃本对象，也可以换参数再调一次
     *  副作用：成功时启动一条接收线程，此后 handler 可能随时被回调。
     */
    bool Connect(const std::string& ip, int port, const std::string& token, int lane,
                 FolderRelayClientHandler* handler,
                 int timeoutSec = kFolderRelayConnectTimeoutSec);

    /**
     *  发送一个应用帧（封成 cmd_folder_relay_data 帧发出）。
     *  <u>线程安全</u>：内部用一把发送锁把"加密 + 写 socket"串行化，多条线程同时调用
     *  不会让密文乱序。
     *
     *  @param data 应用帧字节起始地址（由 relayframing::Build* 系列产出）；len 为 0 时
     *              可为 NULL，函数不接管其所有权，返回后调用方即可释放
     *  @param len  应用帧字节数；超过 kFolderRelayMaxAppFrameBytes 一律<u>判失败</u>，
     *              绝不截断后发出去 —— 截断会让对端解出一个残缺帧，比发送失败难查得多
     *  @return 全部发出返回 true；未连接 / 超出单帧上限 / 加密失败 / socket 错误返回 false
     */
    bool SendAppFrame(const unsigned char* data, size_t len);

    //关闭连接并停止接收线程。<u>幂等</u>，重复调用安全；Close() 之后不再回调 handler。
    //析构函数会自动调用它，故上层忘记调用也不会漏掉线程回收。
    void Close();

    //是否处于已连接（报到成功且未断开）状态。可在任意线程调用。
    bool IsConnected() const;

    //本连接的通道序号。仅在 Connect() 成功之后才有意义；未连接时返回上一次传入的值，
    //初始为 kFolderControlLane。可在任意线程调用。
    int GetLane() const;

private:
    //接收线程主体：循环阻塞收字节、拆帧、解密，把应用帧交给 handler。
    void RecvProc();

    //非阻塞 connect + select 等待（仿 CIUSocket::ConnectToImgServer），成功置 m_sock
    //并切回阻塞模式、设好收发超时。
    bool DoConnect(const std::string& ip, int port, int timeoutSec);

    //在已连接的 socket 上把整段字节全部发出（自行处理"只发出去一部分"）；失败 / 断开
    //返回 false。
    bool SendAll(const char* data, size_t len);

    /**
     *  在刚建好的中转连接上同步走完三帧协议加密握手。
     *
     *  <u>必须排在报到帧（cmd_folder_relay_hello）之前</u>：报到帧里带的是中转令牌，明文
     *  发出去等于把凭据白送，抓到它的人可以抢先冒充一端接进本次会话。也<u>必须排在接收
     *  线程启动之前</u>，否则会与它抢同一份数据。
     *
     *  未配置服务端公钥时直接返回成功且不做任何收发，此时本连接全程明文。
     *
     *  @param timeoutSec 每次收发的超时时间，单位秒
     *  @return 成功返回 true；失败时调用方须放弃这条连接
     *  副作用：在中转连接上收发三帧；改写 m_handshake 的状态。
     */
    bool PerformHandshake(int timeoutSec);

    /**
     *  发出报到帧并阻塞等待其应答。
     *  @param token      中转令牌，写进报到帧包体
     *  @param lane       本连接的通道序号，写进报到帧包体
     *  @param ip         中转地址，仅用于日志
     *  @param port       中转端口，仅用于日志
     *  @param timeoutSec 等待应答的超时，单位秒
     *  @return 服务端回 code==0 返回 true；其余（发送失败、解密失败、超时、被拒）
     *          返回 false
     *  副作用：在中转连接上收发；把应答之后多收的字节留在 m_recvBuf 里交给接收线程。
     */
    bool SendHelloAndWaitAck(const std::string& token, int lane,
                             const std::string& ip, int port, int timeoutSec);

private:
    //握手期间的字节收发适配器，定义在 .cpp 里。做成嵌套类是因为它要用本类的私有
    //SendAll 与 socket。
    class HandshakeChannel;

    SOCKET                       m_sock;        //中转连接套接字；INVALID_SOCKET 表示未连接
    std::atomic<int>             m_lane;        //本连接的通道序号（0 控制 / 1..N 数据）；接收线程只读它来打日志，故用原子量
    std::atomic<bool>            m_connected;   //是否已报到成功且未断开（收发与 IsConnected 的判据）
    std::atomic<bool>            m_stopping;    //是否正在主动关闭（接收线程据此区分主动关闭与异常断开；主动关闭不回调 OnFolderRelayClosed）
    FolderRelayClientHandler*    m_handler;     //事件回调（不拥有；生命周期由上层保证）
    std::unique_ptr<std::thread> m_recvThread;  //接收线程
    std::mutex                   m_sendMtx;     //串行化发送，避免多条线程交错写同一 socket
    std::string                  m_recvBuf;     //接收累积缓冲（报到成功后归接收线程私有，处理粘包与半包）

    //本连接的协议加密状态。<u>每条通道各一个</u>——一次传输本端就有 N+1 条连接，共用
    //会让若干条流互相踩踏。
    //<u>发送方向的加密必须在 m_sendMtx 之内做</u>：流加密按调用顺序推进内部计数器，若在
    //锁外加密、锁内发送，两条业务线程产出的密文上线顺序会与加密顺序不一致，服务端按流
    //顺序解密必然失败并断开连接。详见 SendAppFrame 的注释。
    net::ClientHandshake         m_handshake;
};

} // namespace foldertransfer
