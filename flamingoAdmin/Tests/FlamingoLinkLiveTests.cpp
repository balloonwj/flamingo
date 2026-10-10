/**
 *  flamingoAdmin —— FlamingoLink.dll 的黑盒测试（FLK8）：加载测试程序同目录下编好的 FlamingoLink.dll，
 *  让它连本文件里的一个本地假服务端（明文，不走加密握手），检查连接线程的完整行为：
 *    - 鉴权成功后进入「已连接」；
 *    - 页面活动且设置了企业后订阅，收到完整名单；之后的变化推送更新名单；
 *    - 服务端断开连接后按退避间隔重连，重连成功后自动重新订阅；
 *    - 服务端不再应答心跳时，按服务端下发的空闲时间判定断线并重连；
 *    - 页面不活动时退订，重新活动时重新订阅；页面快速失活又激活时「已订阅企业」不丢；
 *      企业快速换走又换回时重新订阅并拿回完整名单；
 *    - 鉴权返回 1100（token 失效）时只通知一次宿主会话已过期，之后不再重连；
 *    - 页面窗口能在隐藏的父窗口下建出、刷新与销毁。
 *  假服务端在 2501 应答里把心跳设为 1 秒、空闲时间设为 3 秒，整组用例约十几秒跑完。
 *
 *  帧编解码借用 PC 客户端的 Source/net/FrameCodec.cpp（只依赖协议头，不涉及 jsoncpp），
 *  JSON 用控制台自己那份 jsoncpp。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <winsock2.h>
#include <ws2tcpip.h>

#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "FlamingoLink/FlamingoLinkApi.h"
#include "json.h"
#include "../../flamingoclient/Source/net/FrameCodec.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

namespace flamingoadmin { namespace test {

namespace {

// 假服务端在 2501 应答里下发的心跳间隔与空闲时间（秒），让断线判定在几秒内发生
const int kFakeHeartbeatSec = 1;
const int kFakeTimeoutSec = 3;

// 等待条件成立的各类超时（毫秒）
const DWORD kWaitShortMs = 3000;
const DWORD kWaitReconnectMs = 6000;
const DWORD kWaitIdleMs = 9000;
const DWORD kQuietWindowMs = 2500;

// 消息泵每轮之间的休眠（毫秒）
const DWORD kPumpSliceMs = 10;

// 假服务端的 select 等待（毫秒）与一次 recv 的缓冲大小
const int kServerSliceMs = 20;
const int kServerRecvBytes = 64 * 1024;

// 测试用的企业 ID
const int64_t kEnt = 123;

// 一毫秒折合的微秒数
const long kUsPerMs = 1000;

// 假服务端的鉴权模式
enum AuthMode
{
    kAuthOk = 0,       // 回成功
    kAuthExpired = 1,  // 回 1100 token 无效
};

// 宿主：收集日志、记录会话过期回调的次数
class TestHost : public flamingolink::ILinkHost
{
public:
    TestHost() : m_expired(0) {}

    void Log(int level, const char* text) override
    {
        (void)level;
        std::lock_guard<std::mutex> guard(m_mutex);
        m_lines.push_back(text != NULL ? text : "");
    }

    void OnSessionExpired() override
    {
        ++m_expired;
    }

    // 把收集到的日志打印出来，失败时便于排查
    void Dump()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_lines.size(); ++i)
            PrintLine(CString(_T("    [link] ")) + CString(m_lines[i].c_str()));
    }

    std::atomic<int>         m_expired;   // OnSessionExpired 被调用的次数
    std::mutex               m_mutex;     // 保护 m_lines
    std::vector<std::string> m_lines;     // 收到的日志
};

// 本地假服务端：一次只服务一条连接，按控制标志应答
class FakeLinkServer
{
public:
    FakeLinkServer()
        : m_listen(INVALID_SOCKET), m_port(0), m_stop(false), m_authMode(kAuthOk), m_echoHeartbeat(true)
        , m_closeCurrent(false), m_connections(0), m_auths(0), m_subscribes(0), m_unsubscribes(0)
        , m_lastSubscribed(0)
    {
    }

    ~FakeLinkServer()
    {
        Stop();
    }

    // 在 127.0.0.1 的随机端口上监听并起服务线程
    bool Start()
    {
        m_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listen == INVALID_SOCKET)
            return false;
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(m_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
            return false;
        if (::listen(m_listen, SOMAXCONN) != 0)
            return false;
        int len = sizeof(addr);
        ::getsockname(m_listen, reinterpret_cast<sockaddr*>(&addr), &len);
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread(std::bind(&FakeLinkServer::ThreadMain, this));
        return true;
    }

    void Stop()
    {
        m_stop.store(true);
        if (m_thread.joinable())
            m_thread.join();
        if (m_listen != INVALID_SOCKET)
        {
            ::closesocket(m_listen);
            m_listen = INVALID_SOCKET;
        }
    }

    int Port() const { return m_port; }

    // 排队一条 2504 推送，由服务线程在当前连接上发出
    void QueuePush(const std::string& body)
    {
        std::lock_guard<std::mutex> guard(m_pushMutex);
        m_pushes.push_back(body);
    }

    std::atomic<int>     m_authMode;       // 鉴权模式，取值见 AuthMode
    std::atomic<bool>    m_echoHeartbeat;  // 是否应答心跳
    std::atomic<bool>    m_closeCurrent;   // 置位后服务线程关掉当前连接（随即自动复位）
    std::atomic<int>     m_connections;    // 累计接受的连接数
    std::atomic<int>     m_auths;          // 累计收到的 2501 数
    std::atomic<int>     m_subscribes;     // 累计收到的 2502 数
    std::atomic<int>     m_unsubscribes;   // 累计收到的 2503 数
    std::atomic<int64_t> m_lastSubscribed; // 最近一次 2502 请求的企业

private:
    // 发一帧（明文）
    static bool SendFrame(SOCKET s, uint16_t cmd, uint32_t seq, const std::string& body)
    {
        std::string frame;
        if (!net::encodeFrame(cmd, seq, 0, body.data(), static_cast<uint32_t>(body.size()),
                              static_cast<uint32_t>(body.size()), frame))
            return false;
        size_t sent = 0;
        while (sent < frame.size())
        {
            const int n = ::send(s, frame.data() + sent, static_cast<int>(frame.size() - sent), 0);
            if (n <= 0)
                return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    // 完整名单：A 两台设备、B 一台
    static std::string SnapshotBody()
    {
        return "{\"enterpriseid\":\"123\",\"reset\":1,\"users\":["
               "{\"userid\":\"10001\",\"account\":\"a\",\"nickname\":\"A\",\"depts\":[\"D1\"],\"devices\":["
               "{\"clienttype\":1,\"presence\":1,\"logintime\":1759550000000,\"ip\":\"10.0.0.1\"},"
               "{\"clienttype\":2,\"presence\":2,\"logintime\":1759550000000,\"ip\":\"10.0.0.2\"}]},"
               "{\"userid\":\"10002\",\"account\":\"b\",\"nickname\":\"B\",\"depts\":[],\"devices\":["
               "{\"clienttype\":1,\"presence\":3,\"logintime\":1759550000000,\"ip\":\"10.0.0.3\"}]}]}";
    }

    // 从 JSON 包体里取字符串字段
    static std::string JsonString(const std::string& body, const char* key)
    {
        Json::Value v;
        Json::CharReaderBuilder b;
        std::string errs;
        std::istringstream in(body);
        if (!Json::parseFromStream(b, in, &v, &errs) || !v.isObject() || !v[key].isString())
            return std::string();
        return v[key].asString();
    }

    // 处理一帧
    bool HandleFrame(SOCKET s, uint16_t cmd, uint32_t seq, const std::string& body)
    {
        switch (cmd)
        {
        // 鉴权
        case cmd_adminlink_auth:
            ++m_auths;
            if (m_authMode.load() == kAuthExpired)
                return SendFrame(s, cmd, seq, "{\"code\":1100,\"msg\":\"token invalid\"}");
            {
                std::ostringstream ss;
                ss << "{\"code\":0,\"msg\":\"ok\",\"adminid\":\"7\",\"enterpriseid\":\"0\",\"heartbeat\":"
                   << kFakeHeartbeatSec << ",\"timeout\":" << kFakeTimeoutSec << "}";
                return SendFrame(s, cmd, seq, ss.str());
            }

        // 心跳：按开关决定是否应答
        case cmd_heartbeat:
            if (m_echoHeartbeat.load())
                return SendFrame(s, cmd, seq, std::string());
            return true;

        // 订阅：回成功，再发完整名单
        case cmd_adminlink_subscribe_online:
            ++m_subscribes;
            m_lastSubscribed.store(_atoi64(JsonString(body, "enterpriseid").c_str()));
            if (!SendFrame(s, cmd, seq, "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":\"123\"}"))
                return false;
            return SendFrame(s, cmd_adminlink_online_notify, 0, SnapshotBody());

        // 退订
        case cmd_adminlink_unsubscribe_online:
            ++m_unsubscribes;
            return SendFrame(s, cmd, seq, "{\"code\":0,\"msg\":\"ok\"}");

        // 其它命令：忽略
        default:
            return true;
        }
    }

    // 服务一条连接，直到它断开、被要求关闭或服务端停止
    void ServeConnection(SOCKET s)
    {
        std::string inbuf;
        std::vector<char> chunk(kServerRecvBytes);
        while (!m_stop.load())
        {
            if (m_closeCurrent.exchange(false))
                break;

            std::vector<std::string> pushes;
            {
                std::lock_guard<std::mutex> guard(m_pushMutex);
                pushes.swap(m_pushes);
            }
            for (size_t i = 0; i < pushes.size(); ++i)
                SendFrame(s, cmd_adminlink_online_notify, 0, pushes[i]);

            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(s, &rs);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = kServerSliceMs * kUsPerMs;
            const int n = ::select(0, &rs, NULL, NULL, &tv);
            if (n < 0)
                break;
            if (n == 0)
                continue;

            const int got = ::recv(s, &chunk[0], kServerRecvBytes, 0);
            if (got <= 0)
                break;
            inbuf.append(&chunk[0], static_cast<size_t>(got));

            bool ok = true;
            while (ok)
            {
                frame_header h;
                std::string body;
                size_t consumed = 0;
                const net::frame_decode_result r = net::decodeFrame(inbuf.data(), inbuf.size(), h, body, consumed);
                if (r == net::frame_decode_need_more)
                    break;
                if (r == net::frame_decode_error)
                {
                    ok = false;
                    break;
                }
                inbuf.erase(0, consumed);
                ok = HandleFrame(s, h.cmd, h.seq, body);
            }
            if (!ok)
                break;
        }
        ::closesocket(s);
    }

    void ThreadMain()
    {
        while (!m_stop.load())
        {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(m_listen, &rs);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = kServerSliceMs * kUsPerMs;
            if (::select(0, &rs, NULL, NULL, &tv) <= 0)
                continue;
            SOCKET s = ::accept(m_listen, NULL, NULL);
            if (s == INVALID_SOCKET)
                continue;
            ++m_connections;
            ServeConnection(s);
        }
    }

    SOCKET                   m_listen;     // 监听 socket
    int                      m_port;       // 监听端口
    std::atomic<bool>        m_stop;       // 停止标志
    std::thread              m_thread;     // 服务线程
    std::mutex               m_pushMutex;  // 保护 m_pushes
    std::vector<std::string> m_pushes;     // 待发出的 2504 包体
};

// 一个等待条件
typedef bool (*Predicate)(flamingolink::ILinkModule* m, FakeLinkServer* s, void* ctx);

// 一边泵消息一边等条件成立；超时返回 false
bool PumpUntil(flamingolink::ILinkModule* m, FakeLinkServer* s, Predicate pred, void* ctx, DWORD timeoutMs)
{
    const ULONGLONG start = ::GetTickCount64();
    while (::GetTickCount64() - start < timeoutMs)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        if (pred(m, s, ctx))
            return true;
        ::Sleep(kPumpSliceMs);
    }
    return pred(m, s, ctx);
}

// 只泵消息、不判断条件，持续 ms 毫秒
void PumpFor(DWORD ms)
{
    const ULONGLONG start = ::GetTickCount64();
    while (::GetTickCount64() - start < ms)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        ::Sleep(kPumpSliceMs);
    }
}

// 取模块状态
flamingolink::LinkStatus StatusOf(flamingolink::ILinkModule* m)
{
    flamingolink::LinkStatus st;
    memset(&st, 0, sizeof(st));
    m->GetStatus(&st);
    return st;
}

// 以下是各个等待条件；ctx 指向需要比较的整数

bool IsConnected(flamingolink::ILinkModule* m, FakeLinkServer*, void*)
{
    return StatusOf(m).m_state == flamingolink::link_state_connected;
}

bool IsWaiting(flamingolink::ILinkModule* m, FakeLinkServer*, void*)
{
    return StatusOf(m).m_state == flamingolink::link_state_waiting;
}

bool HasSnapshot(flamingolink::ILinkModule* m, FakeLinkServer*, void*)
{
    const flamingolink::LinkStatus st = StatusOf(m);
    return st.m_subscribedEnterprise == kEnt && st.m_onlineUsers == 2;
}

bool IsNotSubscribed(flamingolink::ILinkModule* m, FakeLinkServer*, void*)
{
    return StatusOf(m).m_subscribedEnterprise == 0;
}

bool UsersEqual(flamingolink::ILinkModule* m, FakeLinkServer*, void* ctx)
{
    return StatusOf(m).m_onlineUsers == *static_cast<int*>(ctx);
}

bool ConnectionsAtLeast(flamingolink::ILinkModule* m, FakeLinkServer* s, void* ctx)
{
    return s->m_connections.load() >= *static_cast<int*>(ctx) && IsConnected(m, s, NULL);
}

bool ResubscribedAfter(flamingolink::ILinkModule* m, FakeLinkServer* s, void* ctx)
{
    return s->m_subscribes.load() >= *static_cast<int*>(ctx) && HasSnapshot(m, s, NULL);
}

bool UnsubscribedAtLeast(flamingolink::ILinkModule*, FakeLinkServer* s, void* ctx)
{
    return s->m_unsubscribes.load() >= *static_cast<int*>(ctx);
}

bool ExpiredReported(flamingolink::ILinkModule* m, FakeLinkServer*, void* ctx)
{
    return static_cast<TestHost*>(ctx)->m_expired.load() >= 1
        && StatusOf(m).m_state == flamingolink::link_state_expired;
}

// 取测试程序所在目录
CString ExeDir()
{
    wchar_t path[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, path, MAX_PATH);
    CString dir(path);
    const int pos = dir.ReverseFind(L'\\');
    return (pos >= 0) ? dir.Left(pos) : dir;
}

// FLK8：黑盒测试
void TestLiveLink()
{
    BeginCase("FLK8 连接、订阅、推送、断线重连、心跳超时、退订与会话过期（加载 FlamingoLink.dll 对本地假服务端）");

    WSADATA wsa;
    FA_EXPECT_TRUE(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

    const CString dllPath = ExeDir() + _T("\\") + FLAMINGO_LINK_DLL_NAME;
    HMODULE dll = ::LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    FA_EXPECT_NOTNULL(dll);
    if (dll == NULL)
    {
        PrintLine(CString(_T("    FlamingoLink.dll not found next to the test exe: ")) + dllPath);
        ::WSACleanup();
        return;
    }

    FlamingoLink_CreateFn create = reinterpret_cast<FlamingoLink_CreateFn>(
        ::GetProcAddress(dll, FLAMINGO_LINK_FACTORY_NAME));
    FA_EXPECT_NOTNULL(create);
    flamingolink::ILinkModule* m = (create != NULL) ? create(flamingolink::kApiVersion) : NULL;
    FA_EXPECT_NOTNULL(m);
    // 版本不符时工厂函数拒绝
    FA_EXPECT_NULL(create != NULL ? create(flamingolink::kApiVersion + 1) : NULL);
    if (m == NULL)
    {
        ::FreeLibrary(dll);
        ::WSACleanup();
        return;
    }

    // 本用例开始前的失败数：本用例有失败时打印 DLL 的日志，便于排查
    const int failedBefore = Stats().m_failed;

    TestHost host;
    FA_EXPECT_TRUE(m->Init(&host));

    // 页面窗口建在一个隐藏的父窗口下：只验证建出、刷新与销毁不出错
    HWND parent = ::CreateWindowExW(0, L"STATIC", L"FlamingoLinkTestParent", WS_POPUP, 0, 0, 800, 600,
                                    NULL, NULL, ::GetModuleHandle(NULL), NULL);
    FA_EXPECT_NOTNULL(parent);
    HWND page = m->CreatePage(parent);
    FA_EXPECT_NOTNULL(page);
    RECT rc = { 0, 0, 800, 600 };
    m->SetPageBounds(&rc);

    FakeLinkServer srv;
    FA_EXPECT_TRUE(srv.Start());

    flamingolink::LinkStartParams p;
    p.m_host = L"127.0.0.1";
    p.m_port = srv.Port();
    p.m_pubkeyBase64 = "";
    p.m_token = "tok-flk8";
    p.m_clientVersion = "test";
    m->Start(&p);

    // 1. 鉴权成功进入已连接
    FA_EXPECT_TRUE(PumpUntil(m, &srv, IsConnected, NULL, kWaitShortMs));
    FA_EXPECT_EQ(srv.m_auths.load(), 1);

    // 2. 订阅并收到完整名单：2 人、3 台设备
    m->SetEnterprise(L"123", L"Test Enterprise");
    m->SetPageActive(true);
    FA_EXPECT_TRUE(PumpUntil(m, &srv, HasSnapshot, NULL, kWaitShortMs));
    FA_EXPECT_EQ(StatusOf(m).m_onlineDevices, 3);
    FA_EXPECT_EQ(srv.m_lastSubscribed.load(), kEnt);

    // 3. 变化推送：B 下线
    srv.QueuePush("{\"enterpriseid\":\"123\",\"users\":[{\"userid\":\"10002\",\"devices\":[]}]}");
    int oneUser = 1;
    FA_EXPECT_TRUE(PumpUntil(m, &srv, UsersEqual, &oneUser, kWaitShortMs));
    FA_EXPECT_EQ(StatusOf(m).m_onlineDevices, 2);

    // 4. 服务端断开：进入等待重连，第 1 次；随后重连成功并自动重新订阅
    srv.m_closeCurrent.store(true);
    FA_EXPECT_TRUE(PumpUntil(m, &srv, IsWaiting, NULL, kWaitShortMs));
    FA_EXPECT_EQ(StatusOf(m).m_retryAttempt, 1);
    int twoConnections = 2;
    FA_EXPECT_TRUE(PumpUntil(m, &srv, ConnectionsAtLeast, &twoConnections, kWaitReconnectMs));
    int twoSubscribes = 2;
    FA_EXPECT_TRUE(PumpUntil(m, &srv, ResubscribedAfter, &twoSubscribes, kWaitShortMs));
    FA_EXPECT_EQ(srv.m_auths.load(), 2);

    // 5. 服务端不再应答心跳：按 3 秒空闲判定断线，随后重连（重连前恢复应答）
    srv.m_echoHeartbeat.store(false);
    FA_EXPECT_TRUE(PumpUntil(m, &srv, IsWaiting, NULL, kWaitIdleMs));
    srv.m_echoHeartbeat.store(true);
    int threeConnections = 3;
    FA_EXPECT_TRUE(PumpUntil(m, &srv, ConnectionsAtLeast, &threeConnections, kWaitReconnectMs));

    // 6. 页面不活动：退订
    int oneUnsubscribe = 1;
    FA_EXPECT_TRUE(PumpUntil(m, &srv, HasSnapshot, NULL, kWaitShortMs));
    m->SetPageActive(false);
    FA_EXPECT_TRUE(PumpUntil(m, &srv, UnsubscribedAtLeast, &oneUnsubscribe, kWaitShortMs));
    FA_EXPECT_TRUE(PumpUntil(m, &srv, IsNotSubscribed, NULL, kWaitShortMs));

    // 6b. 重新活动：重新订阅并收到完整名单
    m->SetPageActive(true);
    FA_EXPECT_TRUE(PumpUntil(m, &srv, HasSnapshot, NULL, kWaitShortMs));

    // 6c. 页面快速失活又激活（中间不处理消息）：连接线程可能什么都不发，但「已订阅企业」不能丢
    m->SetPageActive(false);
    m->SetPageActive(true);
    PumpFor(kQuietWindowMs);
    FA_EXPECT_EQ(StatusOf(m).m_subscribedEnterprise, kEnt);
    FA_EXPECT_EQ(StatusOf(m).m_onlineUsers, 2);

    // 6d. 企业快速换走又换回（中间不处理消息）：名单被清空，必须重新订阅拿回完整名单
    const int subscribesBefore = srv.m_subscribes.load();
    m->SetEnterprise(L"456", L"Other");
    m->SetEnterprise(L"123", L"Test Enterprise");
    int moreSubscribes = subscribesBefore + 1;
    FA_EXPECT_TRUE(PumpUntil(m, &srv, ResubscribedAfter, &moreSubscribes, kWaitShortMs));

    // 7. token 失效：服务端断开后下一次鉴权回 1100，只通知一次会话过期，之后不再重连
    srv.m_authMode.store(kAuthExpired);
    srv.m_closeCurrent.store(true);
    FA_EXPECT_TRUE(PumpUntil(m, &srv, ExpiredReported, &host, kWaitReconnectMs));
    const int connectionsAtExpiry = srv.m_connections.load();
    PumpFor(kQuietWindowMs);
    FA_EXPECT_EQ(host.m_expired.load(), 1);
    FA_EXPECT_EQ(srv.m_connections.load(), connectionsAtExpiry);
    FA_EXPECT_EQ(StatusOf(m).m_state, static_cast<int>(flamingolink::link_state_expired));

    // 8. 停止：名单清空，状态回到未启动
    m->Stop();
    FA_EXPECT_EQ(StatusOf(m).m_state, static_cast<int>(flamingolink::link_state_stopped));
    FA_EXPECT_EQ(StatusOf(m).m_onlineUsers, 0);

    // 9. 服务器未开启管理端口：不连接，状态为 disabled
    flamingolink::LinkStartParams off = p;
    off.m_port = 0;
    m->Start(&off);
    FA_EXPECT_EQ(StatusOf(m).m_state, static_cast<int>(flamingolink::link_state_disabled));
    m->Stop();

    if (Stats().m_failed > failedBefore)
        host.Dump();

    m->DestroyPage();
    m->Shutdown();
    srv.Stop();
    if (parent != NULL)
        ::DestroyWindow(parent);
    ::FreeLibrary(dll);
    ::WSACleanup();
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllFlamingoLinkLiveTests()
{
    TestLiveLink();
}

}} // namespace flamingoadmin::test
