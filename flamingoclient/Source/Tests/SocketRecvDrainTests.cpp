/**
 *  SocketRecvDrainTests.cpp
 *
 *  主连接收包循环 net::DrainSocketRecv 的测试（标签 [socketdrain]）。
 *
 *  覆盖：
 *    NET1 对端一次写入 48 KB，收包函数把数据全部读出；十轮读取的耗时中位数低于 5 毫秒
 *         （每读一块就 Sleep(1) 的旧实现，每轮至少要多等一个系统时钟周期，约 16 毫秒）
 *    NET2 对端没有数据时立即返回"正常结束"，读到 0 字节，输出缓冲里原有的内容不动
 *    NET3 对端关闭连接时返回"对端关闭"；关闭前发来的数据仍然全部读出
 *    NET4 对端分三次写入（其中一次大于单次 recv 缓冲），读出的内容与写入的逐字节一致
 *
 *  全部用本机回环 socket，不需要服务端。收包一侧设为非阻塞，与 CIUSocket 的主连接一致。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

#include "../net/SocketRecvDrain.h"
#include "../Common/Util/PerfStopWatch.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

//NET1 每轮写入的字节数。小于 64 KB 的单次 recv 缓冲，好让新实现一次 recv 就读完；
//又明显大于旧实现的 10 KB 缓冲，贴近一页历史消息应答的量级
const int kNet1PayloadBytes = 48 * 1024;
//NET1 重复的轮数；取中位数，避免个别轮次恰好被系统调度打断而误判
const int kNet1Rounds = 10;
//NET1 每轮读取耗时中位数的上限，单位毫秒。去掉等待之后实测为微秒量级；
//旧实现每轮至少一次 Sleep(1)，约 16 毫秒，此上限可以把两者清楚地区分开
const double kNet1MaxMedianMs = 5.0;

//回环 socket 的收发缓冲大小，单位字节。调大是为了让测试里的阻塞 send 能一次写完，
//不必等收包一侧先读走，单线程的用例因此不会卡住
const int kLoopbackBufferBytes = 1024 * 1024;

//等待数据到达的上限，单位毫秒。回环连接上数据通常在微秒内到达，这里只是防止用例失败时卡死
const int kWaitTimeoutMs = 2000;

//NET3 关闭前发送的字节数
const int kNet3PayloadBytes = 1000;

//NET4 三次写入的字节数；第二次大于单次 recv 缓冲，必须分多次 recv 才能读完
const int kNet4FirstBytes = 100;
const int kNet4SecondBytes = net::kRecvChunkSize + 6000;
const int kNet4ThirdBytes = 5;

//进程内初始化 / 清理 Winsock。WSAStartup 按调用次数计数，每个用例各配一对即可
struct ScopedWinsock
{
    ScopedWinsock()
    {
        WSADATA wsaData;
        ::WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~ScopedWinsock()
    {
        ::WSACleanup();
    }
};

//一对已连接的回环 socket：m_hReceiver 是非阻塞的收包一侧（对应 CIUSocket 的主连接），
//m_hSender 是阻塞的发送一侧（对应服务端）。析构时关闭两者
struct LoopbackPair
{
    //收包一侧，非阻塞
    SOCKET m_hReceiver;
    //发送一侧，阻塞
    SOCKET m_hSender;

    LoopbackPair() : m_hReceiver(INVALID_SOCKET), m_hSender(INVALID_SOCKET)
    {
        SOCKET hListener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (hListener == INVALID_SOCKET)
            return;

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
        addr.sin_port = 0;
        int nAddrLen = sizeof(addr);
        if (::bind(hListener, (sockaddr*)&addr, sizeof(addr)) != 0 ||
            ::listen(hListener, 1) != 0 ||
            ::getsockname(hListener, (sockaddr*)&addr, &nAddrLen) != 0)
        {
            ::closesocket(hListener);
            return;
        }

        m_hReceiver = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int nBufBytes = kLoopbackBufferBytes;
        ::setsockopt(m_hReceiver, SOL_SOCKET, SO_RCVBUF, (const char*)&nBufBytes, sizeof(nBufBytes));
        if (::connect(m_hReceiver, (sockaddr*)&addr, sizeof(addr)) != 0)
        {
            ::closesocket(hListener);
            return;
        }

        m_hSender = ::accept(hListener, NULL, NULL);
        ::closesocket(hListener);
        ::setsockopt(m_hSender, SOL_SOCKET, SO_SNDBUF, (const char*)&nBufBytes, sizeof(nBufBytes));

        u_long nNonBlocking = 1;
        ::ioctlsocket(m_hReceiver, FIONBIO, &nNonBlocking);
    }

    ~LoopbackPair()
    {
        if (m_hReceiver != INVALID_SOCKET)
            ::closesocket(m_hReceiver);
        if (m_hSender != INVALID_SOCKET)
            ::closesocket(m_hSender);
    }

    bool IsValid() const
    {
        return m_hReceiver != INVALID_SOCKET && m_hSender != INVALID_SOCKET;
    }
};

//生成指定长度、内容随下标变化的测试数据，便于发现错位或丢字节
std::string MakePayload(int nBytes, int nSeed)
{
    std::string s;
    s.resize((size_t)nBytes);
    for (int i = 0; i < nBytes; ++i)
        s[(size_t)i] = (char)((i * 31 + nSeed) & 0xFF);
    return s;
}

//在阻塞 socket 上把 data 全部写出
bool SendAll(SOCKET hSocket, const std::string& data)
{
    size_t nSent = 0;
    while (nSent < data.size())
    {
        int nRet = ::send(hSocket, data.data() + nSent, (int)(data.size() - nSent), 0);
        if (nRet <= 0)
            return false;
        nSent += (size_t)nRet;
    }
    return true;
}

//NET4 的发送线程入口：依次写出三段数据，每段之间让出一次时间片，使三段更可能分批到达
void SendPiecesThreadProc(SOCKET hSocket, const std::vector<std::string>* pPieces)
{
    for (size_t i = 0; i < pPieces->size(); ++i)
    {
        SendAll(hSocket, (*pPieces)[i]);
        ::Sleep(0);
    }
}

//等待 socket 可读（有数据或对端关闭），超时返回 false
bool WaitReadable(SOCKET hSocket, int nTimeoutMs)
{
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(hSocket, &readset);
    timeval tv;
    tv.tv_sec = nTimeoutMs / 1000;
    tv.tv_usec = (nTimeoutMs % 1000) * 1000;
    return ::select(0, &readset, NULL, NULL, &tv) == 1;
}

//与收包线程相同的用法：等可读、调一次 DrainSocketRecv，直到读满 nExpectedBytes、
//对端关闭、出错或超时。pDrainMs 累加所有 DrainSocketRecv 调用本身的耗时（不含等待可读）
net::SocketDrainResult ReceiveUntil(SOCKET hSocket, size_t nExpectedBytes, std::string& strOut,
                                    double* pDrainMs)
{
    std::vector<char> chunk((size_t)net::kRecvChunkSize);
    PerfStopWatch total;
    net::SocketDrainResult result = net::kSocketDrainOk;
    while (strOut.size() < nExpectedBytes && total.ElapsedMs() < kWaitTimeoutMs)
    {
        if (!WaitReadable(hSocket, kWaitTimeoutMs))
            break;

        PerfStopWatch drain;
        net::SocketDrainStats stats;
        result = net::DrainSocketRecv(hSocket, strOut, &chunk[0], (int)chunk.size(), &stats);
        if (pDrainMs != NULL)
            *pDrainMs += drain.ElapsedMs();

        if (result != net::kSocketDrainOk)
            break;
    }
    return result;
}

} // namespace

TEST_CASE("NET1 对端一次写入 48 KB：全部读出，且每轮读取耗时中位数低于 5 毫秒", "[socketdrain]")
{
    ScopedWinsock winsock;
    LoopbackPair pair;
    REQUIRE(pair.IsValid());

    std::vector<double> drainMs;
    for (int round = 0; round < kNet1Rounds; ++round)
    {
        const std::string payload = MakePayload(kNet1PayloadBytes, round);
        REQUIRE(SendAll(pair.m_hSender, payload));

        std::string received;
        double dMs = 0.0;
        const net::SocketDrainResult r = ReceiveUntil(pair.m_hReceiver, payload.size(), received, &dMs);
        REQUIRE(r == net::kSocketDrainOk);
        REQUIRE(received == payload);
        drainMs.push_back(dMs);
    }

    std::sort(drainMs.begin(), drainMs.end());
    const double median = drainMs[drainMs.size() / 2];
    //把实测数字打到标准输出，通过时也能看到，便于对照改动前后
    printf("[NET1] drain time per %d-byte round: median %.3f ms, min %.3f ms, max %.3f ms\n",
           kNet1PayloadBytes, median, drainMs.front(), drainMs.back());
    CHECK(median < kNet1MaxMedianMs);
}

TEST_CASE("NET2 对端没有数据：立即正常返回，读到 0 字节，输出缓冲原有内容不动", "[socketdrain]")
{
    ScopedWinsock winsock;
    LoopbackPair pair;
    REQUIRE(pair.IsValid());

    std::vector<char> chunk((size_t)net::kRecvChunkSize);
    std::string out("abc");
    net::SocketDrainStats stats;
    stats.m_nChunks = -1;
    PerfStopWatch sw;
    const net::SocketDrainResult r =
        net::DrainSocketRecv(pair.m_hReceiver, out, &chunk[0], (int)chunk.size(), &stats);
    const double ms = sw.ElapsedMs();

    CHECK(r == net::kSocketDrainOk);
    CHECK(out == "abc");
    CHECK(stats.m_nChunks == 0);
    CHECK(stats.m_nBytes == 0);
    CHECK(stats.m_nLastError == 0);
    CHECK(ms < kNet1MaxMedianMs);
}

TEST_CASE("NET3 对端关闭连接：返回对端关闭，关闭前发来的数据全部读出", "[socketdrain]")
{
    ScopedWinsock winsock;
    LoopbackPair pair;
    REQUIRE(pair.IsValid());

    const std::string payload = MakePayload(kNet3PayloadBytes, 3);
    REQUIRE(SendAll(pair.m_hSender, payload));
    ::shutdown(pair.m_hSender, SD_SEND);

    //数据与关闭通知可能分两次可读事件到达，故一直读到出现"对端关闭"为止
    std::string received;
    const net::SocketDrainResult r = ReceiveUntil(pair.m_hReceiver, (size_t)-1, received, NULL);

    CHECK(r == net::kSocketDrainClosed);
    CHECK(received == payload);
}

TEST_CASE("NET4 对端分三次写入：读出的内容与写入的逐字节一致", "[socketdrain]")
{
    ScopedWinsock winsock;
    LoopbackPair pair;
    REQUIRE(pair.IsValid());

    std::vector<std::string> pieces;
    pieces.push_back(MakePayload(kNet4FirstBytes, 1));
    pieces.push_back(MakePayload(kNet4SecondBytes, 2));
    pieces.push_back(MakePayload(kNet4ThirdBytes, 4));
    const std::string expected = pieces[0] + pieces[1] + pieces[2];

    //由另一个线程发送，收包一侧与之并发读取，贴近真实的收包线程
    std::thread sender(SendPiecesThreadProc, pair.m_hSender, &pieces);

    std::string received;
    const net::SocketDrainResult r = ReceiveUntil(pair.m_hReceiver, expected.size(), received, NULL);
    sender.join();

    CHECK(r == net::kSocketDrainOk);
    CHECK(received.size() == expected.size());
    CHECK(received == expected);
}
