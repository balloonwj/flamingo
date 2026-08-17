/**
 *  会话发送路径的线程归属单元测试（Suite SSA），test_sendthreadaffinity.cpp
 *
 *  被测的是 chatserversrc/TcpSession::send 这条发送漏斗：协议加密器（FrameCipher）
 *  是<u>每条连接一份的流式对象</u>，同一方向的帧必须按同一个顺序加密与写出 socket，
 *  接收端才解得开。框架的线程模型本就是「一条连接的读写都归它自己的 EventLoop 线程管」，
 *  但组帧与加密这一段此前发生在<u>调用方线程</u>，只有最后写 socket 那一步才被投递回
 *  连接所属的 loop 线程。于是「给别人推消息」这类跨线程发送会出现「加密的先后」与
 *  「写到线上的先后」不一致，接收端解密必然失败。
 *
 *  两条用例都是端到端复现，不是纯函数测试：真的起一个 EventLoop 线程、真的建一条
 *  127.0.0.1 上的 TCP 连接、真的走完三帧协议加密握手，再从多个线程并发发送，
 *  最后在客户端一侧按线上顺序逐帧解密，断言每一帧都解得开且内容一一对应。
 *
 *    SSA1：全部发送都发生在 loop 线程之外，模拟 ChatSession::sendToUser 那样
 *          由别的会话所在线程给本连接推消息的场景。
 *    SSA2：一路发送在 loop 线程<u>之内</u>、另外几路在 loop 线程之外，模拟
 *          「本连接自己回应答」与「配对端线程转发数据帧」同时发生的场景
 *          （fileserver 的文件夹中转、imgserver 的远程桌面中转都是这个形态）。
 *          这一条对「只加一把发送锁」的修法同样是红的：锁只能保证单帧的
 *          「加密 + 投递」不被打断，保证不了投递到队列里的那一帧会先于
 *          loop 线程自己直接写出去的那一帧到达线上。
 *
 *  balloonwj@qq.com   2026-08-16
 */
#include "doctest.h"

#include <sodium.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../base/Platform.h"
#include "../chatserversrc/TcpSession.h"
#include "../jsoncpp1.9.0/json.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThread.h"
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/InetAddress.h"
#include "../net/Sockets.h"
#include "../net/TcpConnection.h"

#ifdef WIN32
//Windows 下用 socket 之前必须先 WSAStartup。三个服务的 main.cpp 各自定义了一个同样的
//全局对象，测试程序没有 main.cpp，故在本文件里补一个 —— 全局对象的构造早于 main，
//也就早于任何用例执行
static NetworkInitializer s_ssaNetworkInitializer;
#endif

namespace {

//==================== 测试参数 ====================
//这些数值不是随手取的：并发复现有概率性，取值要保证「改动前必红」。实测见文件末尾注释。

//SSA1 里在 loop 线程之外并发发送的线程数。取 4 是为了在常见的多核开发机上真正并行
const int SSA_SENDER_THREAD_COUNT = 4;
//SSA1 里每个线程发送的帧数；合计 4 x 250 = 1000 帧
const int SSA_FRAMES_PER_THREAD = 250;
//SSA2 里在 loop 线程之内发送的帧数
const int SSA_INLOOP_FRAME_COUNT = 100;
//SSA2 里 loop 线程之外的发送线程数
const int SSA_OFFLOOP_THREAD_COUNT = 3;
//SSA2 里每个 loop 线程外的线程发送的帧数；合计 100 + 3 x 100 = 400 帧
const int SSA_OFFLOOP_FRAME_COUNT = 100;

//每帧包体的字节数。取 1 KiB 有两个考虑：一是加密之后还要做一次整段拷贝，包体越大
//「加密完成」到「投递进 loop 队列」之间的时间窗越宽，乱序越容易发生；二是 1000 帧
//合计也才 1 MiB 上下，不至于把发送缓冲撑得太大
const size_t SSA_BODY_BYTES = 1024;
//包体开头这么多字节是帧编号的十进制文本（7 位数字 + 1 个空格），收到后据此核对是哪一帧
const size_t SSA_INDEX_TEXT_BYTES = 8;

//客户端 socket 的接收超时（毫秒）。用例里所有收发都在本机回环上，正常情况远达不到这个数；
//设它是为了让「服务端不再发帧」这种失败表现为用例失败，而不是整个测试程序挂住
const int SSA_RECV_TIMEOUT_MS = 10000;
//SSA2 里 loop 线程内的发送任务等待「loop 线程外已经发出至少一帧」的最长毫秒数。
//超时后照常往下发，用例仍然会因为收不齐帧而失败，不会挂住
const int SSA_INLOOP_WAIT_MS = 3000;
//等待某个后台标志置位的最长毫秒数
const int SSA_FLAG_WAIT_MS = 10000;
//轮询等待的间隔（毫秒）
const int SSA_POLL_INTERVAL_MS = 1;
//客户端每次 recv 的缓冲区字节数
const size_t SSA_RECV_CHUNK_BYTES = 8192;

//握手完成后服务端主动推的那一帧的包体。客户端解开它，即证明双方的加密流都已就绪
const char* const SSA_READY_MARKER = "ssa-handshake-ready";
//本套用例专用的服务端身份密钥种子文件名，放在当前工作目录下，用完删除
const char* const SSA_KEY_FILE = "flamingo_crypto_ssa_test.key";
//服务端身份密钥的公钥文件后缀，与 net/FrameCrypto.cpp 里 loadOrCreate 写出的一致
const char* const SSA_PUB_FILE_SUFFIX = ".pub";

//==================== 小工具 ====================

//把二进制转成 base64 文本（标准变体，带填充），与服务端握手里用的变体一致
std::string toBase64Text(const unsigned char* data, size_t len)
{
    const size_t bufLen = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);
    std::vector<char> buf(bufLen, 0);
    sodium_bin2base64(&buf[0], bufLen, data, len, sodium_base64_VARIANT_ORIGINAL);
    return std::string(&buf[0]);
}

//base64 解码，并要求解出来的长度恰好等于 expectLen
bool fromBase64Exact(const std::string& text, size_t expectLen, unsigned char* out)
{
    size_t realLen = 0;
    if (sodium_base642bin(out, expectLen, text.c_str(), text.size(),
                          NULL, &realLen, NULL, sodium_base64_VARIANT_ORIGINAL) != 0)
    {
        return false;
    }
    return realLen == expectLen;
}

//把 Json::Value 序列化成紧凑文本。本工程把 jsoncpp 的弃用告警当错误处理，只能用新接口
std::string writeCompactJson(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析 JSON 文本，同样只能用新版 CharReader 接口
bool parseJsonText(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs);
    delete reader;
    return ok && out.isObject();
}

//构造第 index 帧的包体：开头是编号文本，其余用与编号相关的字符填满到 SSA_BODY_BYTES
std::string makeFrameBody(int index)
{
    char head[SSA_INDEX_TEXT_BYTES + 1] = { 0 };
    snprintf(head, sizeof(head), "%07d ", index);

    std::string body(head, SSA_INDEX_TEXT_BYTES);
    body.append(SSA_BODY_BYTES - SSA_INDEX_TEXT_BYTES, static_cast<char>('a' + (index % 26)));
    return body;
}

//从包体里取回帧编号；不是本套用例发出的包体时返回 -1
int parseFrameIndex(const std::string& body)
{
    if (body.size() != SSA_BODY_BYTES)
        return -1;

    return atoi(body.substr(0, SSA_INDEX_TEXT_BYTES).c_str());
}

//轮询等待某个标志置位；超时返回 false
bool waitForFlag(std::atomic<bool>& flag, int timeoutMs)
{
    for (int waited = 0; waited < timeoutMs; waited += SSA_POLL_INTERVAL_MS)
    {
        if (flag.load())
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(SSA_POLL_INTERVAL_MS));
    }
    return flag.load();
}

//==================== 服务端一侧：最小会话 ====================

/**
 *  测试用的最小会话。只补一个收包入口，用来把三帧协议加密握手走完；握手之后
 *  本类不再处理任何业务帧，发送方向完全由用例直接调基类的 send 驱动。
 *
 *  之所以继承真正的 TcpSession 而不是自己造一个假的：本套用例要钉的正是
 *  TcpSession::send 内部「在哪个线程加密」这件事，换成假对象就测不到重点了。
 */
class HandshakeOnlySession : public TcpSession
{
public:
    /**
     *  @param conn 本会话所属连接。基类只保存弱引用，不获取其所有权
     */
    explicit HandshakeOnlySession(const std::shared_ptr<TcpConnection>& conn)
        : TcpSession(conn)
    {
    }

    /**
     *  有数据可读时由网络层回调，运行在本连接所属的 EventLoop 线程里。
     *  @param conn        当前连接
     *  @param pBuffer     连接的接收缓冲区，本函数负责从中取走已完整的帧
     *  @param receiveTime 收到数据的时刻，本用例不使用
     */
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receiveTime);

private:
    //把一帧握手帧交给握手状态机，并把应答（若有）发回去
    void handleHandshakeFrame(uint32_t seq, const std::string& body);
};

void HandshakeOnlySession::onRead(const std::shared_ptr<TcpConnection>& conn,
                                  ByteBuffer* pBuffer, Timestamp receiveTime)
{
    (void)conn;
    (void)receiveTime;

    while (true)
    {
        frame_header header;
        std::string       body;
        size_t            consumed = 0;
        const net::frame_decode_result ret = net::decodeFrame(pBuffer->peek(),
                                                              pBuffer->readableBytes(),
                                                              header, body, consumed);
        //不足一个完整帧：等更多字节
        if (ret == net::frame_decode_need_more)
            return;

        //协议错误：测试里客户端只发格式正确的帧，走到这里说明用例本身有问题，
        //丢弃缓冲区里的全部字节，用例会因为收不到预期的帧而失败
        if (ret == net::frame_decode_error)
        {
            pBuffer->retrieveAll();
            return;
        }

        pBuffer->retrieve(consumed);

        //本会话只受理握手帧，其余一律忽略
        if (header.cmd == cmd_handshake)
            handleHandshakeFrame(header.seq, body);
    }
}

void HandshakeOnlySession::handleHandshakeFrame(uint32_t seq, const std::string& body)
{
    std::string response;
    std::string err;
    if (!m_handshake.onHandshakeFrame(body, net::cryptoServerIdentity(), response, err))
        return;

    //前两帧有应答（ServerHello），第三帧没有 —— 没有应答即表示握手已经走完
    if (!response.empty())
    {
        send(cmd_handshake, static_cast<int32_t>(seq), response);
        return;
    }

    //握手完成，主动推一帧「就绪标记」。它是本连接上第一帧密文，客户端解开它
    //即可确认双方的加密流都已就绪，可以开始并发发送
    send(cmd_message_notify, 0, std::string(SSA_READY_MARKER));
}

//连接建立 / 断开时的回调。用例不关心，但 TcpConnection 会无条件调用它，不能为空
void onTestConnection(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
}

//网络层是否主动关掉过连接。收帧收不齐时要靠它分辨「服务端把连接关了」与
//「服务端还开着却不再发数据」，两者的排查方向完全不同
std::atomic<bool> g_connectionClosedByServer(false);

//连接关闭时的回调
void onTestClose(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    g_connectionClosedByServer.store(true);
}

//==================== 客户端一侧：一条裸 socket ====================

/**
 *  测试用的最小客户端：一条阻塞式 socket，自己完成三帧协议加密握手，
 *  并按<u>线上顺序</u>逐帧解密服务端发来的帧。
 *
 *  它只实现「解密服务端发来的帧」这半边（对应服务端的发送方向），因为本套用例
 *  要验的就是这半边；客户端自己的发送流只在握手时初始化一次，之后不再发业务帧。
 */
class TestClient
{
public:
    TestClient();
    ~TestClient();

    /**
     *  连到本机 127.0.0.1 的指定端口。
     *  @param port   目标端口
     *  @param errOut [出参] 失败原因（英文）
     *  @return 成功返回 true
     */
    bool connectTo(uint16_t port, std::string& errOut);

    /**
     *  完成三帧协议加密握手。返回后本对象的接收流已就绪，可以解密服务端发来的帧。
     *  @param errOut [出参] 失败原因（英文）
     *  @return 成功返回 true
     */
    bool handshake(std::string& errOut);

    /**
     *  读取一个完整帧。
     *  @param header [出参] 帧头
     *  @param body   [出参] 线上包体（可能仍是密文）
     *  @param errOut [出参] 失败原因（英文）
     *  @return 成功返回 true；socket 出错或超时返回 false
     */
    bool readFrame(frame_header& header, std::string& body, std::string& errOut);

    /**
     *  按接收流的当前状态解密一帧包体。<u>必须按帧到达的先后调用</u>，
     *  这正是本套用例要验的性质。
     *  @param header   本帧帧头，用于重建附加认证数据
     *  @param wire     线上包体（密文）
     *  @param plainOut [出参] 解出的明文
     *  @return 解密成功返回 true
     */
    bool decryptBody(const frame_header& header, const std::string& wire, std::string& plainOut);

    //关闭 socket；重复调用安全。<u>不能叫 close</u>：POSIX 下 base/Platform.h 把
    //closesocket 定义成了 close 宏，同名成员函数会与那个宏互相干扰
    void closeSocket();

private:
    TestClient(const TestClient&);
    TestClient& operator=(const TestClient&);

    //把一段字节全部写出去
    bool sendAll(const std::string& data, std::string& errOut);

private:
    //本端 socket；INVALID_SOCKET 表示尚未连接或已关闭
    SOCKET      m_fd;
    //已收到但尚未拆出完整帧的字节
    std::string m_inbuf;
    //本次连接的一次性公私钥，只在握手期间使用
    unsigned char m_pk[net::KX_PUBLICKEY_BYTES];
    unsigned char m_sk[net::KX_SECRETKEY_BYTES];
    //握手随机数，纳入握手记录
    unsigned char m_nonce[net::HANDSHAKE_NONCE_BYTES];
    //接收方向（服务端 -> 客户端）的流状态，握手完成后按帧推进
    crypto_secretstream_xchacha20poly1305_state m_pullState;
};

TestClient::TestClient()
    : m_fd(INVALID_SOCKET)
{
    memset(m_pk, 0, sizeof(m_pk));
    memset(m_sk, 0, sizeof(m_sk));
    memset(m_nonce, 0, sizeof(m_nonce));
    memset(&m_pullState, 0, sizeof(m_pullState));
}

TestClient::~TestClient()
{
    closeSocket();
}

void TestClient::closeSocket()
{
    if (m_fd != INVALID_SOCKET)
    {
        closesocket(m_fd);
        m_fd = INVALID_SOCKET;
    }
}

bool TestClient::connectTo(uint16_t port, std::string& errOut)
{
    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd == INVALID_SOCKET)
    {
        errOut = "client socket() failed";
        return false;
    }

    //接收缓冲区一律用系统默认值。此处原先把 SO_RCVBUF 开到 8 MiB，是为了绕开一个当时
    //尚未修复的框架缺陷：Windows 的 SelectPoller 按位或累加事件位，残留的可读位会连着
    //可写事件一起派发，把连接判死（守卫用例见 tests/test_pollerrevents.cpp，Suite PRV）。
    //该缺陷已于 2026-08-16 修复，此处不再需要绕行；用默认缓冲区反而更好 ——
    //本套用例的客户端在收帧之前不读任何数据，
    //链路必然被灌满，服务端的发送路径因此会真的走一遍「转入输出缓冲并 enableWriting」，
    //顺带守住那条路径不再回退
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port);

    if (::connect(m_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        errOut = "client connect() failed";
        return false;
    }

    //收不到数据时不要一直等下去，让用例以失败告终而不是整个测试程序挂住
#ifdef WIN32
    DWORD timeout = static_cast<DWORD>(SSA_RECV_TIMEOUT_MS);
    ::setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec  = SSA_RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (SSA_RECV_TIMEOUT_MS % 1000) * 1000;
    ::setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    return true;
}

bool TestClient::sendAll(const std::string& data, std::string& errOut)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        const int n = static_cast<int>(::send(m_fd, data.data() + sent,
                                              static_cast<int>(data.size() - sent), 0));
        if (n <= 0)
        {
            errOut = "client send() failed";
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TestClient::readFrame(frame_header& header, std::string& body, std::string& errOut)
{
    //每轮先试着从已有字节里拆一个完整帧，拆不出来再去 socket 上取
    while (true)
    {
        size_t consumed = 0;
        const net::frame_decode_result ret = net::decodeFrame(m_inbuf.data(), m_inbuf.size(),
                                                              header, body, consumed);
        if (ret == net::frame_decode_ok)
        {
            m_inbuf.erase(0, consumed);
            return true;
        }

        if (ret == net::frame_decode_error)
        {
            errOut = "client decodeFrame reported a protocol error";
            return false;
        }

        char buf[SSA_RECV_CHUNK_BYTES] = { 0 };
        const int n = static_cast<int>(::recv(m_fd, buf, sizeof(buf), 0));
        if (n <= 0)
        {
            //把 recv 的返回值与系统错误码都带上：0 是对端关闭、负数才是出错或超时，
            //两者的排查方向完全不同，光说「收不到」得再加一次日志才能分辨
#ifdef WIN32
            const int sysErr = (int)WSAGetLastError();
#else
            const int sysErr = errno;
#endif
            char detail[160] = { 0 };
            snprintf(detail, sizeof(detail),
                     "client recv() returned %d, system error %d, %u bytes still buffered, "
                     "server closed the connection: %d",
                     n, sysErr, (unsigned)m_inbuf.size(),
                     g_connectionClosedByServer.load() ? 1 : 0);
            errOut = detail;
            return false;
        }
        m_inbuf.append(buf, static_cast<size_t>(n));
    }
}

bool TestClient::handshake(std::string& errOut)
{
    //---- 第 1 帧 ClientHello：送出本次连接的一次性公钥与随机数 ----
    crypto_kx_keypair(m_pk, m_sk);
    randombytes_buf(m_nonce, sizeof(m_nonce));

    Json::Value hello;
    hello["ver"]    = static_cast<int>(net::HANDSHAKE_VERSION);
    hello["cpk"]    = toBase64Text(m_pk, sizeof(m_pk));
    hello["cnonce"] = toBase64Text(m_nonce, sizeof(m_nonce));

    std::string frame;
    if (!net::encodeFrame(cmd_handshake, 1, writeCompactJson(hello), frame))
    {
        errOut = "client failed to encode ClientHello frame";
        return false;
    }
    if (!sendAll(frame, errOut))
        return false;

    //---- 第 2 帧 ServerHello：取回服务端的一次性公钥与发送流流头 ----
    frame_header header;
    std::string       body;
    if (!readFrame(header, body, errOut))
        return false;

    if (header.cmd != cmd_handshake)
    {
        errOut = "client expected a handshake frame but got another cmd";
        return false;
    }

    Json::Value resp;
    if (!parseJsonText(body, resp))
    {
        errOut = "ServerHello is not valid JSON";
        return false;
    }

    unsigned char spk[net::KX_PUBLICKEY_BYTES]   = { 0 };
    unsigned char shdr[net::STREAM_HEADER_BYTES] = { 0 };
    if (!fromBase64Exact(resp["spk"].asString(), sizeof(spk), spk)
        || !fromBase64Exact(resp["shdr"].asString(), sizeof(shdr), shdr))
    {
        errOut = "ServerHello field 'spk' or 'shdr' is missing or malformed";
        return false;
    }

    //导出两把会话密钥：客户端的 rx 与服务端的 tx 是同一把
    unsigned char rxKey[net::KX_SESSIONKEY_BYTES] = { 0 };
    unsigned char txKey[net::KX_SESSIONKEY_BYTES] = { 0 };
    if (crypto_kx_client_session_keys(rxKey, txKey, m_pk, m_sk, spk) != 0)
    {
        errOut = "crypto_kx_client_session_keys failed";
        return false;
    }

    if (crypto_secretstream_xchacha20poly1305_init_pull(&m_pullState, shdr, rxKey) != 0)
    {
        errOut = "failed to init client receiving stream";
        return false;
    }

    //---- 第 3 帧：把本端发送流的流头交给服务端，握手到此结束 ----
    //本套用例的客户端不再发业务帧，这一步只是为了让服务端的握手状态机走完
    unsigned char chdr[net::STREAM_HEADER_BYTES] = { 0 };
    crypto_secretstream_xchacha20poly1305_state pushState;
    if (crypto_secretstream_xchacha20poly1305_init_push(&pushState, chdr, txKey) != 0)
    {
        errOut = "failed to init client sending stream";
        return false;
    }

    Json::Value third;
    third["chdr"] = toBase64Text(chdr, sizeof(chdr));

    frame.clear();
    if (!net::encodeFrame(cmd_handshake, 2, writeCompactJson(third), frame))
    {
        errOut = "client failed to encode stream header frame";
        return false;
    }
    return sendAll(frame, errOut);
}

bool TestClient::decryptBody(const frame_header& header, const std::string& wire,
                             std::string& plainOut)
{
    plainOut.clear();

    if (wire.size() < net::STREAM_OVERHEAD_BYTES)
        return false;

    //附加认证数据按本帧帧头字段重新拼一遍，拼法与服务端必须逐字节一致
    unsigned char ad[net::FRAME_AD_BYTES] = { 0 };
    net::buildFrameAd(header.cmd, header.seq, header.flags,
                      static_cast<uint32_t>(wire.size()), header.originlen, ad);

    plainOut.resize(wire.size() - net::STREAM_OVERHEAD_BYTES);
    unsigned long long plainLen = 0;
    unsigned char      tag = 0;
    if (crypto_secretstream_xchacha20poly1305_pull(
            &m_pullState,
            plainOut.empty() ? NULL : (unsigned char*)&plainOut[0], &plainLen, &tag,
            (const unsigned char*)wire.data(), wire.size(),
            ad, sizeof(ad)) != 0)
    {
        plainOut.clear();
        return false;
    }

    plainOut.resize(static_cast<size_t>(plainLen));
    return true;
}

//==================== 用例脚手架 ====================

/**
 *  一次端到端复现所需的全套环境：一个 EventLoop 线程、一条真实的回环 TCP 连接、
 *  连接上的一个会话对象，以及已完成握手的客户端。
 *
 *  构造后必须调 start() 才可用；析构时按「先收尾连接、再停 loop 线程」的顺序拆除，
 *  用例可以重复跑，不残留监听端口。
 */
class SendAffinityFixture
{
public:
    SendAffinityFixture();
    ~SendAffinityFixture();

    /**
     *  建好整套环境并完成协议加密握手。
     *  @param errOut [出参] 失败原因（英文）
     *  @return 成功返回 true；失败时用例应直接判失败，不要继续往下跑
     */
    bool start(std::string& errOut);

    //被测的会话对象（发送漏斗在它的基类里）
    const std::shared_ptr<HandshakeOnlySession>& session() const { return m_session; }
    //连接所属的 EventLoop；用例用它把发送任务投进 loop 线程
    net::EventLoop* loop() const { return m_loop; }
    //客户端一侧
    TestClient& client() { return m_client; }

private:
    SendAffinityFixture(const SendAffinityFixture&);
    SendAffinityFixture& operator=(const SendAffinityFixture&);

private:
    //承载连接的 EventLoop 线程
    net::EventLoopThread m_loopThread;
    //上面那个线程里的 EventLoop，不拥有
    net::EventLoop*      m_loop;
    //服务端一侧的连接对象
    std::shared_ptr<TcpConnection>        m_conn;
    //服务端一侧的会话对象
    std::shared_ptr<HandshakeOnlySession> m_session;
    //客户端一侧
    TestClient           m_client;
};

SendAffinityFixture::SendAffinityFixture()
    : m_loop(NULL)
{
}

SendAffinityFixture::~SendAffinityFixture()
{
    //先断开客户端，让服务端一侧不再有新的收包
    m_client.closeSocket();

    if (m_loop != NULL && m_conn)
    {
        //连接的收尾必须在它自己的 loop 线程里做（内部要摘 Channel）。
        //这条任务在 quit 之前入队，loop 退出前的最后一轮会把它执行掉
        m_loop->runInLoop(std::bind(&TcpConnection::connectDestroyed, m_conn));
        m_loopThread.stopLoop();
        m_loop = NULL;
    }

    m_session.reset();
    m_conn.reset();
}

bool SendAffinityFixture::start(std::string& errOut)
{
    //---- 监听 socket 绑到 127.0.0.1 的 0 号端口，由系统分配一个空闲端口 ----
    //这样用例可以重复跑、也可以与别的程序并存，不必挑一个「看着空着」的端口
    SOCKET listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == INVALID_SOCKET)
    {
        errOut = "listen socket() failed";
        return false;
    }

    struct sockaddr_in listenAddr;
    memset(&listenAddr, 0, sizeof(listenAddr));
    listenAddr.sin_family      = AF_INET;
    listenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listenAddr.sin_port        = 0;

    if (::bind(listenfd, (const struct sockaddr*)&listenAddr, sizeof(listenAddr)) != 0
        || ::listen(listenfd, 1) != 0)
    {
        closesocket(listenfd);
        errOut = "bind() or listen() failed";
        return false;
    }

    socklen_t addrLen = sizeof(listenAddr);
    if (::getsockname(listenfd, (struct sockaddr*)&listenAddr, &addrLen) != 0)
    {
        closesocket(listenfd);
        errOut = "getsockname() failed";
        return false;
    }
    const uint16_t port = ntohs(listenAddr.sin_port);

    //---- 客户端连上来，服务端 accept 出对应的那一端 ----
    if (!m_client.connectTo(port, errOut))
    {
        closesocket(listenfd);
        return false;
    }

    struct sockaddr_in peerAddr;
    memset(&peerAddr, 0, sizeof(peerAddr));
    socklen_t peerLen = sizeof(peerAddr);
    SOCKET serverfd = ::accept(listenfd, (struct sockaddr*)&peerAddr, &peerLen);
    closesocket(listenfd);
    if (serverfd == INVALID_SOCKET)
    {
        errOut = "accept() failed";
        return false;
    }

    //框架里的连接一律是非阻塞的（正常路径上由 Acceptor 保证），这里手工补上
    net::sockets::setNonBlockAndCloseOnExec(serverfd);

    //---- 起 EventLoop 线程，把 accept 出来的这一端交给它 ----
    m_loop = m_loopThread.startLoop();
    if (m_loop == NULL)
    {
        closesocket(serverfd);
        errOut = "startLoop() returned NULL";
        return false;
    }

    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    socklen_t localLen = sizeof(localAddr);
    ::getsockname(serverfd, (struct sockaddr*)&localAddr, &localLen);

    m_conn.reset(new TcpConnection(m_loop, "ssa-test-conn", serverfd,
                                   net::InetAddress(localAddr), net::InetAddress(peerAddr)));
    m_session.reset(new HandshakeOnlySession(m_conn));

    m_conn->setConnectionCallback(&onTestConnection);
    m_conn->setCloseCallback(&onTestClose);
    m_conn->setMessageCallback(std::bind(&HandshakeOnlySession::onRead, m_session.get(),
                                         std::placeholders::_1, std::placeholders::_2,
                                         std::placeholders::_3));

    //连接的启用必须在它自己的 loop 线程里做
    m_loop->runInLoop(std::bind(&TcpConnection::connectEstablished, m_conn));

    //---- 走完三帧握手，并等服务端推来的那一帧「就绪标记」 ----
    if (!m_client.handshake(errOut))
        return false;

    frame_header header;
    std::string       wire;
    if (!m_client.readFrame(header, wire, errOut))
        return false;

    if ((header.flags & frame_flag_encrypted) == 0)
    {
        errOut = "the frame after handshake is not encrypted";
        return false;
    }

    std::string marker;
    if (!m_client.decryptBody(header, wire, marker))
    {
        errOut = "failed to decrypt the handshake-ready frame";
        return false;
    }
    if (marker != SSA_READY_MARKER)
    {
        errOut = "the frame after handshake carries an unexpected body";
        return false;
    }

    return true;
}

/**
 *  收帧并按线上顺序逐帧解密，直到收满 expectCount 帧或出错。
 *  @param client      客户端一侧
 *  @param expectCount 期望收到的帧数
 *  @param indexesOut  [出参] 每一帧解出的编号，按线上顺序排列
 *  @param errOut      [出参] 失败原因（英文），成功时清空
 *  @return 全部帧都收到且都解密成功时返回 true
 */
bool receiveAndDecryptAll(TestClient& client, int expectCount,
                          std::vector<int>& indexesOut, std::string& errOut)
{
    indexesOut.clear();
    errOut.clear();

    for (int i = 0; i < expectCount; ++i)
    {
        frame_header header;
        std::string       wire;
        if (!client.readFrame(header, wire, errOut))
        {
            char detail[128] = { 0 };
            snprintf(detail, sizeof(detail), " (after %d of %d frames)", i, expectCount);
            errOut += detail;
            return false;
        }

        std::string plain;
        if (!client.decryptBody(header, wire, plain))
        {
            char detail[160] = { 0 };
            snprintf(detail, sizeof(detail),
                     "failed to decrypt frame #%d of %d (seq %u): "
                     "the on-wire order does not match the encryption order",
                     i, expectCount, static_cast<unsigned>(header.seq));
            errOut = detail;
            return false;
        }

        indexesOut.push_back(parseFrameIndex(plain));
    }

    return true;
}

//==================== 发送线程 ====================

/**
 *  一路发送任务的参数。用结构体而不是 lambda 捕获，与仓库既有写法保持一致。
 */
struct SenderTask
{
    //被测会话；用 shared_ptr 持有，保证任务执行期间对象一定还在
    std::shared_ptr<HandshakeOnlySession> m_session;
    //本路发送的首帧编号
    int m_firstIndex;
    //本路发送的帧数
    int m_frameCount;
    //本路已经发出的帧数，供别的线程观察进度；不拥有
    std::atomic<int>* m_issued;
};

//一路发送：连续发 m_frameCount 帧，编号从 m_firstIndex 起连续递增
void runSenderTask(SenderTask task)
{
    for (int i = 0; i < task.m_frameCount; ++i)
    {
        const int index = task.m_firstIndex + i;
        task.m_session->send(cmd_message_notify, index, makeFrameBody(index));

        if (task.m_issued != NULL)
            task.m_issued->fetch_add(1);
    }
}

/**
 *  在 loop 线程之内跑的那一路发送（SSA2 用）。它先等 loop 线程之外的发送方
 *  至少发出一帧，再开始发自己的，以此稳定地造出「队列里已经躺着一帧、
 *  本线程又直接写出去一帧」的局面。
 *
 *  @param task       本路发送的参数
 *  @param offIssued  loop 线程之外已发出的帧数；不拥有
 *  @param doneFlag   [出参] 本路发送结束的标志；不拥有
 */
void runInLoopSenderTask(SenderTask task, std::atomic<int>* offIssued, std::atomic<bool>* doneFlag)
{
    for (int waited = 0; waited < SSA_INLOOP_WAIT_MS; waited += SSA_POLL_INTERVAL_MS)
    {
        if (offIssued->load() > 0)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(SSA_POLL_INTERVAL_MS));
    }

    runSenderTask(task);
    doneFlag->store(true);
}

} // namespace

//==================== 用例 ====================

TEST_CASE("SSA1 多个非 loop 线程并发发送时，每一帧都必须能按线上顺序解开")
{
    REQUIRE(net::cryptoInit());

    std::string cryptoErr;
    REQUIRE_MESSAGE(net::cryptoConfigInit(SSA_KEY_FILE, true, cryptoErr), cryptoErr);

    SendAffinityFixture fixture;
    std::string err;
    REQUIRE_MESSAGE(fixture.start(err), err);

    const int totalFrames = SSA_SENDER_THREAD_COUNT * SSA_FRAMES_PER_THREAD;

    std::vector<std::thread> senders;
    for (int t = 0; t < SSA_SENDER_THREAD_COUNT; ++t)
    {
        SenderTask task;
        task.m_session    = fixture.session();
        task.m_firstIndex = t * SSA_FRAMES_PER_THREAD;
        task.m_frameCount = SSA_FRAMES_PER_THREAD;
        task.m_issued     = NULL;
        senders.push_back(std::thread(&runSenderTask, task));
    }

    for (size_t i = 0; i < senders.size(); ++i)
        senders[i].join();

    std::vector<int> indexes;
    std::string      recvErr;
    const bool allDecrypted = receiveAndDecryptAll(fixture.client(), totalFrames, indexes, recvErr);
    CHECK_MESSAGE(allDecrypted, recvErr);

    //每一帧都要原样到达，既不能丢、也不能重复
    if (allDecrypted)
    {
        std::set<int> got(indexes.begin(), indexes.end());
        CHECK(static_cast<int>(got.size()) == totalFrames);
        CHECK(*got.begin() == 0);
        CHECK(*got.rbegin() == totalFrames - 1);
    }
}

TEST_CASE("SSA2 loop 线程内外同时发送时，每一帧都必须能按线上顺序解开")
{
    REQUIRE(net::cryptoInit());

    std::string cryptoErr;
    REQUIRE_MESSAGE(net::cryptoConfigInit(SSA_KEY_FILE, true, cryptoErr), cryptoErr);

    SendAffinityFixture fixture;
    std::string err;
    REQUIRE_MESSAGE(fixture.start(err), err);

    const int offLoopTotal = SSA_OFFLOOP_THREAD_COUNT * SSA_OFFLOOP_FRAME_COUNT;
    const int totalFrames  = offLoopTotal + SSA_INLOOP_FRAME_COUNT;

    //loop 线程之外已经发出的帧数，供 loop 线程内那一路观察
    std::atomic<int>  offIssued(0);
    //loop 线程内那一路是否已经发完
    std::atomic<bool> inLoopDone(false);

    //先把「loop 线程内发送」这一路投进去：它会先等 loop 线程外发出第一帧再开始发
    SenderTask inLoopTask;
    inLoopTask.m_session    = fixture.session();
    inLoopTask.m_firstIndex = offLoopTotal;
    inLoopTask.m_frameCount = SSA_INLOOP_FRAME_COUNT;
    inLoopTask.m_issued     = NULL;
    fixture.loop()->runInLoop(std::bind(&runInLoopSenderTask, inLoopTask, &offIssued, &inLoopDone));

    std::vector<std::thread> senders;
    for (int t = 0; t < SSA_OFFLOOP_THREAD_COUNT; ++t)
    {
        SenderTask task;
        task.m_session    = fixture.session();
        task.m_firstIndex = t * SSA_OFFLOOP_FRAME_COUNT;
        task.m_frameCount = SSA_OFFLOOP_FRAME_COUNT;
        task.m_issued     = &offIssued;
        senders.push_back(std::thread(&runSenderTask, task));
    }

    for (size_t i = 0; i < senders.size(); ++i)
        senders[i].join();

    //loop 线程内那一路是投给 loop 线程执行的，与本线程并行推进，
    //等它也发完再收帧，否则会少收一批
    REQUIRE(waitForFlag(inLoopDone, SSA_FLAG_WAIT_MS));

    std::vector<int> indexes;
    std::string      recvErr;
    const bool allDecrypted = receiveAndDecryptAll(fixture.client(), totalFrames, indexes, recvErr);
    CHECK_MESSAGE(allDecrypted, recvErr);

    if (allDecrypted)
    {
        std::set<int> got(indexes.begin(), indexes.end());
        CHECK(static_cast<int>(got.size()) == totalFrames);
        CHECK(*got.begin() == 0);
        CHECK(*got.rbegin() == totalFrames - 1);
    }

    //本套用例专用的身份密钥文件用完就删，不留在工作目录里
    remove(SSA_KEY_FILE);
    remove((std::string(SSA_KEY_FILE) + SSA_PUB_FILE_SUFFIX).c_str());
}

/*
 *  取值依据（2026-08-16 在 Windows 10 / Debug|Win32 上实测，本机 CPU 12 逻辑核）：
 *
 *    - 把 TcpSession::send 改回「在调用方线程里加密」的旧行为后，连跑 10 次，
 *      SSA1 与 SSA2 <u>10 次全红</u>。SSA1 通常在收到第 3 ~ 210 帧之间解不开，
 *      SSA2 则每次都恰好在第 0 帧就解不开 —— 它是刻意造出来的确定性场景：
 *      loop 线程内那一路会先等 loop 线程外发出一帧，那一帧已经加密、却还排在
 *      loop 的任务队列里，而 loop 线程自己发的帧是直接写出去的，两者的先后必然相反。
 *    - 改动之后，SSA1 与 SSA2 连跑 20 次全绿，全量用例连跑 10 次全绿。
 *
 *  因此 SSA1 的 4 x 250 帧是「留足余量」的取值，而不是刚好卡在复现边缘；SSA2 不依赖
 *  概率，帧数取小一些只是为了省时间。
 */

