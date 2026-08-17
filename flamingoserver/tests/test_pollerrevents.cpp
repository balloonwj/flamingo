/**
 *  Poller 事件位口径的单元测试（Suite PRV），test_pollerrevents.cpp
 *
 *  被测的是 net/Channel 的 m_revents（本轮真正发生了哪些事件）由谁写、怎么写这件事。
 *  框架里三个 poller 的写法本应一致：EpollPoller 与 PollPoller 用 set_revents 整体覆盖，
 *  而 Windows 用的 SelectPoller 曾经用的是 add_revents（按位或累加），Channel 又从不把
 *  m_revents 清零。两者叠加的后果是：一条连接只要发生过一次可读事件，可读位就永久留在
 *  m_revents 里，此后任何一次可写事件都会连着它一起派发，TcpConnection::handleRead
 *  在无数据可读时从 readFd 拿到 -1，走 handleError -> handleClose，把一条健康的连接判死。
 *  该缺陷于 2026-08-16 修复，表现是客户端读得慢时服务端把连接拆掉，本文件即当时的守卫用例。
 *
 *  两条用例分别从两个高度钉这件事，都是真的起 EventLoop 线程、真的建一条 127.0.0.1 上的
 *  TCP 连接，不使用任何假对象：
 *
 *    PRV1：贴着 Channel 观察。先让服务端一侧真的发生一次可读事件并把数据取走，再打开写关注，
 *          断言随后的那一轮<u>只</u>派发写回调、不再派发读回调。这一条不依赖缓冲区容量，
 *          也不依赖并发时序，是确定性复现。
 *    PRV2：端到端复现真实故障场景。客户端把接收缓冲区调小并故意先不读，服务端持续发数据，
 *          直到内核发送缓冲与 TcpConnection 的输出缓冲都被灌满、触发 enableWriting；
 *          断言服务端不把这条连接判死，且客户端最终收全每一个字节。
 *
 *  balloonwj@qq.com   2026-08-16
 */
#include "doctest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "../base/Platform.h"
#include "../base/Timestamp.h"
#include "../net/ByteBuffer.h"
#include "../net/Channel.h"
#include "../net/EventLoop.h"
#include "../net/EventLoopThread.h"
#include "../net/InetAddress.h"
#include "../net/Sockets.h"
#include "../net/TcpConnection.h"

#ifdef WIN32
//Windows 下用 socket 之前必须先 WSAStartup。三个服务的 main.cpp 各自定义了一个同样的
//全局对象，测试程序没有 main.cpp，故在本文件里补一个 —— 全局对象的构造早于 main，
//也就早于任何用例执行。WSAStartup / WSACleanup 是引用计数的，别的测试文件里再有一个
//同样的对象也不冲突
static NetworkInitializer s_prvNetworkInitializer;
#endif

namespace {

//==================== 测试参数 ====================

//轮询等待的间隔（毫秒）
const int PRV_POLL_INTERVAL_MS = 1;
//等待某个计数或标志达到预期的最长毫秒数。回环上正常情况远达不到这个数，
//设它是为了让失败表现为用例失败，而不是整个测试程序挂住
const int PRV_FLAG_WAIT_MS = 10000;
//PRV1 里观察到写事件之后额外静置的毫秒数。缺陷发作时读回调与写回调是在同一轮里
//先后派发的，等到写计数变化时读计数其实已经多了一次，这段静置只是留出余量，
//让可能滞后一轮的事件也落地再看计数
const int PRV_SETTLE_MS = 100;

//客户端 socket 的接收超时（毫秒）。取 5 秒：本用例全部收发都在本机回环上，
//单次 recv 正常情况在毫秒级，5 秒足够宽松；缺陷未修复时用例正是靠它结束等待
const int PRV_RECV_TIMEOUT_MS = 5000;

//PRV1 的读回调每次从 socket 上取走的字节数上限。这一条用例只发一个字节，取小值即可
const size_t PRV_PROBE_READ_BYTES = 64;
//客户端用来触发「服务端发生一次可读事件」的那个字节，取值本身没有含义
const char PRV_TRIGGER_BYTE = 'g';

//PRV2 里客户端 socket 的接收缓冲区字节数（SO_RCVBUF），取 8 KiB。
//要的就是「小」：客户端不读的时候，服务端只写出这么点数据就会把链路灌满
const int PRV_CLIENT_RECVBUF_BYTES = 8 * 1024;
//PRV2 里服务端 socket 的发送缓冲区字节数（SO_SNDBUF），同样取 8 KiB。
//与上面那个一起把「服务端转为缓冲输出并 enableWriting」这一刻提前到发出几十 KiB 之内
const int PRV_SERVER_SENDBUF_BYTES = 8 * 1024;
//PRV2 里服务端总共发出的字节数，取 4 MiB。它比链路上能容纳的字节数（两端缓冲区
//合计不过几十 KiB）大两个数量级，因此「输出必须转入 TcpConnection 自己的缓冲区」
//这件事是必然发生的，不依赖任何时序巧合
const size_t PRV_PAYLOAD_BYTES = 4 * 1024 * 1024;
//PRV2 里服务端每次 send 的字节数。取 64 KiB 是为了让投递到 loop 的任务数量保持在
//几十个的量级，既不至于一次拷贝过大，也不至于任务过多把时间耗在队列上
const size_t PRV_SEND_CHUNK_BYTES = 64 * 1024;
//PRV2 里客户端每次 recv 的缓冲区字节数
const size_t PRV_RECV_CHUNK_BYTES = 8192;
//PRV2 里客户端在开始读之前故意等待的毫秒数。这段时间用来让服务端把链路灌满并
//转入缓冲输出；缺陷正是在链路重新可写的那一刻发作
const int PRV_FILL_WAIT_MS = 300;
//PRV2 的校验字节序列的周期。用 26 个小写字母循环，既能查出丢字节，也能查出错序
const size_t PRV_PATTERN_PERIOD = 26;

//传给建链辅助函数、表示「该项缓冲区保持系统默认、不去设置」的取值
const int PRV_KEEP_DEFAULT_BUF = 0;

//==================== 小工具 ====================

//轮询等待某个计数达到 expected；超时返回 false
bool waitForCount(std::atomic<int>& counter, int expected, int timeoutMs)
{
    for (int waited = 0; waited < timeoutMs; waited += PRV_POLL_INTERVAL_MS)
    {
        if (counter.load() >= expected)
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(PRV_POLL_INTERVAL_MS));
    }
    return counter.load() >= expected;
}

//轮询等待某个标志置位；超时返回 false
bool waitForFlag(std::atomic<bool>& flag, int timeoutMs)
{
    for (int waited = 0; waited < timeoutMs; waited += PRV_POLL_INTERVAL_MS)
    {
        if (flag.load())
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(PRV_POLL_INTERVAL_MS));
    }
    return flag.load();
}

//取当前线程最近一次 socket 调用的系统错误码
int lastSocketError()
{
#ifdef WIN32
    return (int)WSAGetLastError();
#else
    return errno;
#endif
}

/**
 *  在回环地址上建出一对已经连上的 socket。监听 socket 绑到 127.0.0.1 的 0 号端口，
 *  由系统分配一个空闲端口，故用例可以重复跑、也可以与别的程序并存；accept 之后
 *  监听 socket 立即关闭，不残留监听端口。
 *
 *  @param clientRecvBufBytes 客户端 SO_RCVBUF 的字节数；取 PRV_KEEP_DEFAULT_BUF 表示不设置。
 *                            必须在 connect 之前设置才生效（窗口大小在三次握手时协商）
 *  @param serverSendBufBytes 服务端 SO_SNDBUF 的字节数；取 PRV_KEEP_DEFAULT_BUF 表示不设置
 *  @param serverfdOut        [出参] 服务端 accept 出来的那一端，已置为非阻塞。
 *                            调用方负责关闭（交给 TcpConnection 时则由它的 Socket 关闭）
 *  @param clientfdOut        [出参] 客户端主动连上来的那一端，阻塞式，已设接收超时。调用方负责关闭
 *  @param localAddrOut       [出参] 服务端一侧的本地地址，用于构造 TcpConnection
 *  @param peerAddrOut        [出参] 服务端一侧看到的对端地址，用于构造 TcpConnection
 *  @param errOut             [出参] 失败原因（英文）
 *  @return 成功返回 true；失败时两个出参 socket 均为 INVALID_SOCKET
 */
bool makeLoopbackPair(int clientRecvBufBytes, int serverSendBufBytes,
                      SOCKET& serverfdOut, SOCKET& clientfdOut,
                      struct sockaddr_in& localAddrOut, struct sockaddr_in& peerAddrOut,
                      std::string& errOut)
{
    serverfdOut = INVALID_SOCKET;
    clientfdOut = INVALID_SOCKET;
    memset(&localAddrOut, 0, sizeof(localAddrOut));
    memset(&peerAddrOut, 0, sizeof(peerAddrOut));

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

    socklen_t listenLen = sizeof(listenAddr);
    if (::getsockname(listenfd, (struct sockaddr*)&listenAddr, &listenLen) != 0)
    {
        closesocket(listenfd);
        errOut = "getsockname() on the listening socket failed";
        return false;
    }

    SOCKET clientfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == INVALID_SOCKET)
    {
        closesocket(listenfd);
        errOut = "client socket() failed";
        return false;
    }

    if (clientRecvBufBytes != PRV_KEEP_DEFAULT_BUF)
    {
        ::setsockopt(clientfd, SOL_SOCKET, SO_RCVBUF,
                     (const char*)&clientRecvBufBytes, sizeof(clientRecvBufBytes));
    }

    if (::connect(clientfd, (const struct sockaddr*)&listenAddr, sizeof(listenAddr)) != 0)
    {
        closesocket(clientfd);
        closesocket(listenfd);
        errOut = "client connect() failed";
        return false;
    }

    //收不到数据时不要一直等下去，让用例以失败告终而不是整个测试程序挂住
#ifdef WIN32
    DWORD timeout = static_cast<DWORD>(PRV_RECV_TIMEOUT_MS);
    ::setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec  = PRV_RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (PRV_RECV_TIMEOUT_MS % 1000) * 1000;
    ::setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    socklen_t peerLen = sizeof(peerAddrOut);
    SOCKET serverfd = ::accept(listenfd, (struct sockaddr*)&peerAddrOut, &peerLen);
    closesocket(listenfd);
    if (serverfd == INVALID_SOCKET)
    {
        closesocket(clientfd);
        errOut = "accept() failed";
        return false;
    }

    if (serverSendBufBytes != PRV_KEEP_DEFAULT_BUF)
    {
        ::setsockopt(serverfd, SOL_SOCKET, SO_SNDBUF,
                     (const char*)&serverSendBufBytes, sizeof(serverSendBufBytes));
    }

    //框架里的连接一律是非阻塞的（正常路径上由 Acceptor 保证），这里手工补上。
    //本缺陷的表现也只有在非阻塞 socket 上才成立：无数据可读时 recv 返回 -1 而不是阻塞
    net::sockets::setNonBlockAndCloseOnExec(serverfd);

    socklen_t localLen = sizeof(localAddrOut);
    ::getsockname(serverfd, (struct sockaddr*)&localAddrOut, &localLen);

    serverfdOut = serverfd;
    clientfdOut = clientfd;
    return true;
}

//==================== PRV1：贴着 Channel 观察事件派发 ====================

/**
 *  一个 Channel 的事件派发观察点：把读 / 写回调各被调用了多少次记下来，
 *  供用例断言「上一轮的可读事件没有残留到下一轮」。
 */
struct ChannelProbe
{
    //被观察的 Channel，不拥有，生命周期由用例函数的栈帧管理
    net::Channel*    m_channel;
    //服务端一侧的 socket，读回调里用它把数据取走。不拥有
    SOCKET           m_fd;
    //读回调被调用的次数
    std::atomic<int> m_readCount;
    //写回调被调用的次数
    std::atomic<int> m_writeCount;
    //读回调里一个字节也没取到的次数。它正是本缺陷的直接表现：
    //没有数据可读却被当作可读事件派发了一次
    std::atomic<int> m_emptyReadCount;

    ChannelProbe()
        : m_channel(NULL),
          m_fd(INVALID_SOCKET),
          m_readCount(0),
          m_writeCount(0),
          m_emptyReadCount(0)
    {
    }
};

//可读事件的回调，运行在 loop 线程里
void probeOnReadable(ChannelProbe* probe, Timestamp receiveTime)
{
    (void)receiveTime;

    char buf[PRV_PROBE_READ_BYTES] = { 0 };
    const int n = static_cast<int>(::recv(probe->m_fd, buf, sizeof(buf), 0));
    if (n <= 0)
        probe->m_emptyReadCount.fetch_add(1);

    probe->m_readCount.fetch_add(1);
}

//可写事件的回调，运行在 loop 线程里
void probeOnWritable(ChannelProbe* probe)
{
    probe->m_writeCount.fetch_add(1);

    //可写事件是水平触发的：只要发送缓冲还有空间，不撤掉写关注就会每一轮都触发，
    //既拖慢用例，也让观察到的次数失去意义。收到第一次就撤掉
    probe->m_channel->disableWriting();
}

//下面三个都必须在 Channel 所属的 loop 线程里执行，故由用例经 runInLoop 投进去

void probeEnableReading(ChannelProbe* probe)
{
    probe->m_channel->enableReading();
}

void probeEnableWriting(ChannelProbe* probe)
{
    probe->m_channel->enableWriting();
}

void probeTeardown(ChannelProbe* probe)
{
    probe->m_channel->disableAll();
    probe->m_channel->remove();
}

//==================== PRV2：端到端的慢读客户端场景 ====================

/**
 *  服务端一侧的观察点：记录网络层有没有主动把连接关掉，以及有没有收到客户端
 *  发来的那个触发字节。
 */
struct SlowReaderProbe
{
    //网络层是否走过关闭回调。本用例的核心断言就是它必须始终为 false ——
    //一条健康的连接不该在「需要缓冲输出」的那一刻被判死
    std::atomic<bool> m_closed;
    //是否已经收到客户端发来的触发字节。收到它即表示这条连接上确实发生过可读事件，
    //也就具备了本缺陷发作的前提
    std::atomic<bool> m_triggered;

    SlowReaderProbe()
        : m_closed(false),
          m_triggered(false)
    {
    }
};

//连接建立 / 断开时的回调。用例不关心，但 TcpConnection 会无条件调用它，不能为空
void onSlowReaderConnection(const std::shared_ptr<net::TcpConnection>& conn)
{
    (void)conn;
}

//网络层决定关闭连接时的回调
void onSlowReaderClose(SlowReaderProbe* probe, const std::shared_ptr<net::TcpConnection>& conn)
{
    (void)conn;
    probe->m_closed.store(true);
}

//有数据可读时的回调。本用例的服务端只需要「收到过一次数据」这件事，内容不作要求
void onSlowReaderMessage(SlowReaderProbe* probe,
                         const std::shared_ptr<net::TcpConnection>& conn,
                         net::ByteBuffer* pBuffer, Timestamp receiveTime)
{
    (void)conn;
    (void)receiveTime;

    pBuffer->retrieveAll();
    probe->m_triggered.store(true);
}

//第 offset 个字节应当是什么
char payloadByteAt(size_t offset)
{
    return static_cast<char>('a' + (offset % PRV_PATTERN_PERIOD));
}

//按上面的序列填出 [offset, offset + len) 这一段
void makePayloadChunk(size_t offset, size_t len, std::string& out)
{
    out.resize(len);
    for (size_t i = 0; i < len; ++i)
        out[i] = payloadByteAt(offset + i);
}

} // namespace

//==================== 用例 ====================

TEST_CASE("PRV1 上一轮的可读事件不得残留到下一轮的可写事件里")
{
    SOCKET             serverfd = INVALID_SOCKET;
    SOCKET             clientfd = INVALID_SOCKET;
    struct sockaddr_in localAddr;
    struct sockaddr_in peerAddr;
    std::string        err;

    //这一条不依赖缓冲区容量，两端都用系统默认值
    REQUIRE_MESSAGE(makeLoopbackPair(PRV_KEEP_DEFAULT_BUF, PRV_KEEP_DEFAULT_BUF,
                                     serverfd, clientfd, localAddr, peerAddr, err), err);

    net::EventLoopThread loopThread;
    net::EventLoop*      loop = loopThread.startLoop();
    REQUIRE(loop != NULL);

    ChannelProbe  probe;
    net::Channel  channel(loop, serverfd);
    probe.m_channel = &channel;
    probe.m_fd      = serverfd;
    channel.setReadCallback(std::bind(&probeOnReadable, &probe, std::placeholders::_1));
    channel.setWriteCallback(std::bind(&probeOnWritable, &probe));

    //关注可读事件。Channel 的启用必须在它自己的 loop 线程里做
    loop->runInLoop(std::bind(&probeEnableReading, &probe));

    //客户端发一个字节，让服务端一侧真的发生一次可读事件 —— 缺陷的前提正是「发生过可读事件」
    const char trigger = PRV_TRIGGER_BYTE;
    REQUIRE(static_cast<int>(::send(clientfd, &trigger, 1, 0)) == 1);
    REQUIRE(waitForCount(probe.m_readCount, 1, PRV_FLAG_WAIT_MS));

    //到这里读事件已经派发完，那一个字节也已经被取走，socket 上再没有任何可读数据。
    //此时才打开写关注：下一轮 select / epoll_wait 只应报可写
    loop->runInLoop(std::bind(&probeEnableWriting, &probe));
    REQUIRE(waitForCount(probe.m_writeCount, 1, PRV_FLAG_WAIT_MS));

    std::this_thread::sleep_for(std::chrono::milliseconds(PRV_SETTLE_MS));

    //核心断言：可写事件那一轮不得再派发读回调。缺陷未修复时读计数会变成 2
    CHECK(probe.m_readCount.load() == 1);
    //读回调只要被多派发一次，就必然是一次「没有数据可读」的空读
    CHECK(probe.m_emptyReadCount.load() == 0);
    CHECK(probe.m_writeCount.load() == 1);

    //收尾：摘掉 Channel 必须在它自己的 loop 线程里做。这条任务在 quit 之前入队，
    //loop 退出前的最后一轮会把它执行掉
    loop->runInLoop(std::bind(&probeTeardown, &probe));
    loopThread.stopLoop();

    closesocket(clientfd);
    closesocket(serverfd);
}

TEST_CASE("PRV2 客户端读得慢时服务端不得把连接判死，全部字节最终必须收全")
{
    SOCKET             serverfd = INVALID_SOCKET;
    SOCKET             clientfd = INVALID_SOCKET;
    struct sockaddr_in localAddr;
    struct sockaddr_in peerAddr;
    std::string        err;

    REQUIRE_MESSAGE(makeLoopbackPair(PRV_CLIENT_RECVBUF_BYTES, PRV_SERVER_SENDBUF_BYTES,
                                     serverfd, clientfd, localAddr, peerAddr, err), err);

    net::EventLoopThread loopThread;
    net::EventLoop*      loop = loopThread.startLoop();
    REQUIRE(loop != NULL);

    SlowReaderProbe probe;
    std::shared_ptr<net::TcpConnection> conn(
        new net::TcpConnection(loop, "prv-slow-reader", serverfd,
                               net::InetAddress(localAddr), net::InetAddress(peerAddr)));

    conn->setConnectionCallback(&onSlowReaderConnection);
    conn->setCloseCallback(std::bind(&onSlowReaderClose, &probe, std::placeholders::_1));
    conn->setMessageCallback(std::bind(&onSlowReaderMessage, &probe,
                                       std::placeholders::_1, std::placeholders::_2,
                                       std::placeholders::_3));

    //连接的启用必须在它自己的 loop 线程里做
    loop->runInLoop(std::bind(&net::TcpConnection::connectEstablished, conn));

    //先让服务端一侧真的发生一次可读事件，把「残留的可读位」这个前提造出来
    const char trigger = PRV_TRIGGER_BYTE;
    REQUIRE(static_cast<int>(::send(clientfd, &trigger, 1, 0)) == 1);
    REQUIRE(waitForFlag(probe.m_triggered, PRV_FLAG_WAIT_MS));

    //服务端连续发出 PRV_PAYLOAD_BYTES 个字节。这些调用发生在 loop 线程之外，
    //TcpConnection::send 会把每一段拷一份投进 loop 线程去发
    std::string chunk;
    size_t      offset = 0;
    while (offset < PRV_PAYLOAD_BYTES)
    {
        size_t thisLen = PRV_SEND_CHUNK_BYTES;
        if (offset + thisLen > PRV_PAYLOAD_BYTES)
            thisLen = PRV_PAYLOAD_BYTES - offset;

        makePayloadChunk(offset, thisLen, chunk);
        conn->send(chunk);
        offset += thisLen;
    }

    //客户端故意先不读：这段时间用来让内核发送缓冲与 TcpConnection 的输出缓冲都被灌满，
    //服务端因此转入缓冲输出并 enableWriting。缺陷正是在链路重新可写的那一刻发作
    std::this_thread::sleep_for(std::chrono::milliseconds(PRV_FILL_WAIT_MS));

    //开始读，并逐字节核对内容
    size_t      received   = 0;
    bool        contentOk  = true;
    std::string failDetail;
    while (received < PRV_PAYLOAD_BYTES)
    {
        char      buf[PRV_RECV_CHUNK_BYTES] = { 0 };
        const int n = static_cast<int>(::recv(clientfd, buf, static_cast<int>(sizeof(buf)), 0));
        if (n <= 0)
        {
            //把 recv 的返回值与系统错误码都带上：0 是对端关闭、负数才是出错或超时，
            //两者的排查方向完全不同。是否走过关闭回调同样要带上 —— 本缺陷的表现正是
            //服务端把连接判死（走了关闭回调）却没有关闭 fd，客户端只能等到接收超时
            char detail[224] = { 0 };
            snprintf(detail, sizeof(detail),
                     "client recv() returned %d, system error %d, received %u of %u bytes, "
                     "server closed the connection: %d",
                     n, lastSocketError(), (unsigned)received, (unsigned)PRV_PAYLOAD_BYTES,
                     probe.m_closed.load() ? 1 : 0);
            failDetail = detail;
            break;
        }

        for (int i = 0; i < n && contentOk; ++i)
        {
            if (buf[i] != payloadByteAt(received + static_cast<size_t>(i)))
                contentOk = false;
        }
        received += static_cast<size_t>(n);
    }

    //核心断言：一个字节都不能少
    CHECK_MESSAGE(received == PRV_PAYLOAD_BYTES, failDetail);
    //内容还必须原样到达，既不能错序也不能损坏
    CHECK(contentOk);
    //服务端自始至终不得把这条健康的连接判死
    CHECK_FALSE(probe.m_closed.load());
    CHECK(conn->connected());

    //收尾。先断开客户端，再把连接的收尾任务投进它自己的 loop 线程（内部要摘 Channel）；
    //serverfd 由 TcpConnection 内部的 Socket 在析构时关闭，这里不能重复关
    closesocket(clientfd);
    loop->runInLoop(std::bind(&net::TcpConnection::connectDestroyed, conn));
    loopThread.stopLoop();
    conn.reset();
}
