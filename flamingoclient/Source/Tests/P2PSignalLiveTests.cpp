/**
 *  P2P 信令的端到端联调用例（Suite P2PL，标签 [.live][p2plive]），P2PSignalLiveTests.cpp
 *
 *  docs/P2P通信方案.md 第 2 期 2c-2c：两个账号用 LiveChatClient 直接登录隔离的测试服务端，收发 2301~2306，
 *  核对 chatserver 的分发、转发、断线与退出登录的收尾，以及按企业开关的效果。客户端的 FlamingoP2P.dll 要到第 3 期
 *  才处理这些信令，所以这里由测试代码扮演两端。
 *    - P2PL1 两人登记：2301 应答 code 0、enabled 为真、带两个探测地址（测试服务端配置的 29904 / 29905）；
 *    - P2PL2 联调建会话：2306 应答带会话号，双方各收到一份 2302，逐项核对角色、用途、对端、中转参数、令牌与三个时间；
 *    - P2PL3 信令转发：一端发 2303，自己收到 code 0 的应答，对端收到带 fromuid 的转发、data 原样；反方向同样核对；
 *    - P2PL4 结果与结束：2304 应答 0；2305（原因 1）推给对端；结束后再发 2303 返回 901；
 *    - P2PL5 对端断开连接：仍在线的一方收到 2305，原因 3、错误码 903；
 *    - P2PL6 对端只退出登录、不断开连接：同样收到 2305，原因 3；
 *    - P2PL7 企业开关：用后台接口关闭两人所在企业的 p2p.enabled，在线的一方收到 1013（p2p 为 0、fileshare 仍为 1），
 *            2301 应答 enabled 为假且不带探测地址，2306 返回 900；最后恢复开关（用例中途失败也会恢复）。
 *            服务端日志与管理员操作日志的核对由运行脚本 tmp/p2ptest/run_live.ps1 完成。
 *
 *  默认不参与常规测试：用例带 [.live] 标签，Catch2 会跳过。跑之前用 tmp/p2ptest/setup_p2ptest.py 建环境、
 *  start_server.ps1 起测试服务端（chatserver 29900，HTTP 29945）。
 *
 *      FlamingoUnit.exe "[p2plive]"
 *
 *  账号：MY0127（5127，作 A）、MY0308（5308，作 B），密码都是 perf1234，同属企业 1；
 *  后台账号是平台管理员 li@flamingo.com，密码只在隔离库副本里重设过（见 setup_p2ptest.py）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdlib.h>
#include <string.h>
#include <string>
#include "LiveChatClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;

namespace {

//tmp/p2ptest 测试服务端的 chatserver 端口与 HTTP 端口
const unsigned short kP2PTestServerPort = 29900;
const unsigned short kP2PTestHttpPort = 29945;

//两个人的账号与密码，以及他们所在的企业
const char* kAccountA = "MY0127";
const char* kAccountB = "MY0308";
const char* kPassword = "perf1234";
const int64_t kEnterprise = 1;

//后台账号（平台管理员）
const char* kAdminEmail = "li@flamingo.com";
const char* kAdminPassword = "p2ptest1234";

//测试服务端 chatserver.conf 里的 P2P 配置（setup_p2ptest.py 写入），2301 / 2302 应原样下发
const char* kRelayHost = "127.0.0.1";
const int kRelayPort = 29903;
const int kProbePort1 = 29904;
const int kProbePort2 = 29905;
const int kConnectTimeoutMs = 2500;
const int kKeepaliveMs = 4000;
const int kLinkTimeoutMs = 20000;

//本客户端报到的 P2P 协议版本
const int kP2PVersion = 1;

//联调建会话的用途
const char* kPurposeTest = "test";

//LiveChatClient 登录时上报的设备标识前缀（见 LiveChatClient::ConnectAndLogin）
const char* kDeviceIdPrefix = "livetest-";

//等一条应答或推送的最长时间，单位毫秒
const int kWaitMs = 5000;

//error_code 里用到的取值：成功
const int kCodeOk = 0;

//一次 recv 的缓冲大小（字节）：后台接口的应答只有几百字节
const int kHttpRecvBufSize = 4096;

//HTTP 报文头与包体之间的分隔
const char* kHttpHeaderEnd = "\r\n\r\n";
const size_t kHttpHeaderEndLen = 4;

bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

std::string compactJson(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

void login(LiveChatClient& c, const char* account)
{
    std::string reason;
    const bool ok = c.ConnectAndLogin(kP2PTestServerPort, kTestServerPubKey, account, kPassword, reason);
    INFO("login " << account << " failed: " << reason);
    REQUIRE(ok);
}

//发一条请求并等它的应答，解析成 JSON
Json::Value request(LiveChatClient& c, uint16_t cmd, const Json::Value& body)
{
    REQUIRE(c.Send(cmd, compactJson(body)));
    std::string resp;
    REQUIRE(c.WaitFor(cmd, resp, kWaitMs));
    Json::Value root;
    INFO("response of cmd " << cmd << ": " << resp);
    REQUIRE(parseJson(resp, root));
    return root;
}

//等一条推送并解析成 JSON
Json::Value waitPush(LiveChatClient& c, uint16_t cmd)
{
    std::string body;
    INFO("waiting for push cmd " << cmd);
    REQUIRE(c.WaitFor(cmd, body, kWaitMs));
    Json::Value root;
    INFO("push body: " << body);
    REQUIRE(parseJson(body, root));
    return root;
}

//发 2301 报到并返回应答
Json::Value registerP2P(LiveChatClient& c)
{
    Json::Value req;
    req["version"] = kP2PVersion;
    return request(c, cmd_p2p_register, req);
}

//联调建会话的结果：会话号与双方收到的 2302
struct OpenedSession
{
    std::string m_sessionId;   //2306 应答里的会话号
    Json::Value m_notifyA;     //发起方 A 收到的 2302
    Json::Value m_notifyB;     //对端 B 收到的 2302
};

//双方报到后由 A 对 B 发 2306，等应答与双方的 2302
OpenedSession openSession(LiveChatClient& a, LiveChatClient& b)
{
    REQUIRE(registerP2P(a)["code"].asInt() == kCodeOk);
    REQUIRE(registerP2P(b)["code"].asInt() == kCodeOk);
    Json::Value req;
    req["peeruid"] = std::to_string(b.UserId());
    const Json::Value resp = request(a, cmd_p2p_debug_open, req);
    REQUIRE(resp["code"].asInt() == kCodeOk);
    OpenedSession s;
    s.m_sessionId = resp["p2psessionid"].asString();
    REQUIRE_FALSE(s.m_sessionId.empty());
    s.m_notifyA = waitPush(a, cmd_p2p_session_notify);
    s.m_notifyB = waitPush(b, cmd_p2p_session_notify);
    return s;
}

//一段 2303 的 data：只有测试两端自己约定内容，服务端应原样转发
Json::Value signalData(const char* pubkey, const char* ip, int port)
{
    const int kHostCandidate = 1;
    const int kPriority = 2130706431;
    Json::Value cand;
    cand["type"] = kHostCandidate;
    cand["ip"] = ip;
    cand["port"] = port;
    cand["priority"] = kPriority;
    Json::Value data;
    data["pubkey"] = pubkey;
    data["candidates"].append(cand);
    return data;
}

//收到的 HTTP 应答是否完整：报文头已收齐，且包体长度达到 Content-Length（没有这个头时等连接关闭）
bool httpResponseComplete(const std::string& resp)
{
    const size_t headerEnd = resp.find(kHttpHeaderEnd);
    if (headerEnd == std::string::npos)
        return false;
    const std::string header = resp.substr(0, headerEnd);
    const char* kLengthName = "Content-Length:";
    size_t pos = header.find(kLengthName);
    if (pos == std::string::npos)
        return false;
    const size_t length = (size_t)strtoul(header.c_str() + pos + strlen(kLengthName), NULL, 10);
    return resp.size() >= headerEnd + kHttpHeaderEndLen + length;
}

//向测试服务端的 HTTP 端口发一条 JSON POST，读回应答包体（后台接口是短连接）
bool postJson(const std::string& path, const Json::Value& body, Json::Value& out)
{
    EnsureWinsockReady();
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
        return false;
    DWORD timeout = (DWORD)kIoTimeoutMs;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(kP2PTestHttpPort);
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    if (::connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        ::closesocket(s);
        return false;
    }

    const std::string json = compactJson(body);
    const std::string req = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
        "Content-Length: " + std::to_string(json.size()) + "\r\nConnection: close\r\n\r\n" + json;
    size_t sent = 0;
    while (sent < req.size())
    {
        const int n = ::send(s, req.data() + sent, (int)(req.size() - sent), 0);
        if (n <= 0)
        {
            ::closesocket(s);
            return false;
        }
        sent += (size_t)n;
    }

    std::string resp;
    char buf[kHttpRecvBufSize];
    while (!httpResponseComplete(resp))
    {
        const int n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        resp.append(buf, (size_t)n);
    }
    ::closesocket(s);
    const size_t headerEnd = resp.find(kHttpHeaderEnd);
    if (headerEnd == std::string::npos)
        return false;
    return parseJson(resp.substr(headerEnd + kHttpHeaderEndLen), out);
}

//登录后台，返回令牌
std::string adminLogin()
{
    Json::Value req;
    req["email"] = kAdminEmail;
    req["password"] = kAdminPassword;
    Json::Value resp;
    REQUIRE(postJson("/admin/login.do", req, resp));
    INFO("admin login response: " << compactJson(resp));
    REQUIRE(resp["code"].asInt() == kCodeOk);
    const std::string token = resp["token"].asString();
    REQUIRE_FALSE(token.empty());
    return token;
}

//经后台接口设置企业的 p2p.enabled；成功返回 true
bool setP2PSwitch(const std::string& token, const char* value)
{
    Json::Value req;
    req["token"] = token;
    req["enterpriseid"] = (Json::Int64)kEnterprise;
    req["key"] = "p2p.enabled";
    req["value"] = value;
    Json::Value resp;
    return postJson("/admin/setting/set.do", req, resp) && resp["code"].asInt() == kCodeOk;
}

//在 kWaitMs 内等一条 1013，直到其中 p2p 等于 expected；等到返回该推送，没等到返回空对象
Json::Value waitFeatures(LiveChatClient& c, int expected)
{
    const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)kWaitMs;
    while (::GetTickCount64() < deadline)
    {
        const int left = (int)(deadline - ::GetTickCount64());
        std::string body;
        if (!c.WaitFor(cmd_features_notify, body, left > 0 ? left : 1))
            break;
        Json::Value root;
        if (parseJson(body, root) && root["p2p"].asInt() == expected)
            return root;
    }
    return Json::Value();
}

//P2PL7 结束时（包括断言失败提前退出）把企业的 p2p 开关恢复为开启，避免影响之后的用例与别的联调
struct P2PSwitchRestorer
{
    std::string m_token;   //后台令牌；为空时不恢复

    ~P2PSwitchRestorer()
    {
        if (!m_token.empty())
            setP2PSwitch(m_token, "1");
    }
};

} // anonymous namespace

TEST_CASE("P2PL1 两人登记 P2P 能力", "[.live][p2plive]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);

    const Json::Value ra = registerP2P(a);
    CHECK(ra["code"].asInt() == kCodeOk);
    CHECK(ra["enabled"].asBool());
    REQUIRE(ra["probe"].isArray());
    REQUIRE(ra["probe"].size() == 2);
    CHECK(ra["probe"][0]["ip"].asString() == kRelayHost);
    CHECK(ra["probe"][0]["port"].asInt() == kProbePort1);
    CHECK(ra["probe"][1]["ip"].asString() == kRelayHost);
    CHECK(ra["probe"][1]["port"].asInt() == kProbePort2);

    const Json::Value rb = registerP2P(b);
    CHECK(rb["code"].asInt() == kCodeOk);
    CHECK(rb["enabled"].asBool());
}

TEST_CASE("P2PL2 联调建会话：双方各收到一份 2302", "[.live][p2plive]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    const OpenedSession s = openSession(a, b);
    const Json::Value& na = s.m_notifyA;
    const Json::Value& nb = s.m_notifyB;
    INFO("notify A: " << compactJson(na) << "\nnotify B: " << compactJson(nb));

    //同一个会话；A 是发起方、B 是应答方；用途为 test
    CHECK(na["p2psessionid"].asString() == s.m_sessionId);
    CHECK(nb["p2psessionid"].asString() == s.m_sessionId);
    CHECK(na["role"].asInt() == p2p_role_controlling);
    CHECK(nb["role"].asInt() == p2p_role_controlled);
    CHECK(na["purpose"].asString() == kPurposeTest);
    CHECK(nb["purpose"].asString() == kPurposeTest);
    //对端互为对方，设备标识是对方登录时上报的
    CHECK(na["peeruid"].asString() == std::to_string(b.UserId()));
    CHECK(nb["peeruid"].asString() == std::to_string(a.UserId()));
    CHECK(na["peerdeviceid"].asString() == std::string(kDeviceIdPrefix) + kAccountB);
    CHECK(nb["peerdeviceid"].asString() == std::string(kDeviceIdPrefix) + kAccountA);
    //回落中转参数与配置一致，两份令牌都有且不相同（角色不同）
    CHECK(na["relayip"].asString() == kRelayHost);
    CHECK(na["relayport"].asInt() == kRelayPort);
    CHECK(nb["relayip"].asString() == kRelayHost);
    CHECK(nb["relayport"].asInt() == kRelayPort);
    CHECK_FALSE(na["token"].asString().empty());
    CHECK_FALSE(nb["token"].asString().empty());
    CHECK(na["token"].asString() != nb["token"].asString());
    //三个时间与配置一致
    CHECK(na["connecttimeoutms"].asInt() == kConnectTimeoutMs);
    CHECK(na["keepalivems"].asInt() == kKeepaliveMs);
    CHECK(na["linktimeoutms"].asInt() == kLinkTimeoutMs);
    CHECK(nb["connecttimeoutms"].asInt() == kConnectTimeoutMs);
    CHECK(nb["keepalivems"].asInt() == kKeepaliveMs);
    CHECK(nb["linktimeoutms"].asInt() == kLinkTimeoutMs);
}

TEST_CASE("P2PL3 信令转发：data 原样到达对端", "[.live][p2plive]")
{
    const int kPortA = 54429;
    const int kPortB = 16725;
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    const OpenedSession s = openSession(a, b);

    //A → B
    Json::Value fromA;
    fromA["p2psessionid"] = s.m_sessionId;
    fromA["data"] = signalData("pubkey-of-a", "192.168.10.2", kPortA);
    CHECK(request(a, cmd_p2p_signal, fromA)["code"].asInt() == kCodeOk);
    const Json::Value atB = waitPush(b, cmd_p2p_signal);
    CHECK(atB["p2psessionid"].asString() == s.m_sessionId);
    CHECK(atB["fromuid"].asString() == std::to_string(a.UserId()));
    CHECK(atB["data"] == fromA["data"]);

    //B → A
    Json::Value fromB;
    fromB["p2psessionid"] = s.m_sessionId;
    fromB["data"] = signalData("pubkey-of-b", "183.192.103.49", kPortB);
    CHECK(request(b, cmd_p2p_signal, fromB)["code"].asInt() == kCodeOk);
    const Json::Value atA = waitPush(a, cmd_p2p_signal);
    CHECK(atA["p2psessionid"].asString() == s.m_sessionId);
    CHECK(atA["fromuid"].asString() == std::to_string(b.UserId()));
    CHECK(atA["data"] == fromB["data"]);
}

TEST_CASE("P2PL4 上报结果与结束会话", "[.live][p2plive]")
{
    const int kElapsedMs = 412;
    const int kLatePort = 54430;
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    const OpenedSession s = openSession(a, b);

    //2304：服务端只核对成员身份并记日志
    Json::Value result;
    result["p2psessionid"] = s.m_sessionId;
    result["ok"] = true;
    result["path"] = p2p_path_relay;
    result["elapsedms"] = kElapsedMs;
    result["pair"] = "127.0.0.1:50000 -> 127.0.0.1:29903";
    CHECK(request(a, cmd_p2p_result, result)["code"].asInt() == kCodeOk);

    //2305：A 结束会话，B 收到同样的原因与错误码
    Json::Value close;
    close["p2psessionid"] = s.m_sessionId;
    close["reason"] = p2p_close_business_end;
    close["errcode"] = kCodeOk;
    CHECK(request(a, cmd_p2p_close, close)["code"].asInt() == kCodeOk);
    const Json::Value atB = waitPush(b, cmd_p2p_close);
    CHECK(atB["p2psessionid"].asString() == s.m_sessionId);
    CHECK(atB["reason"].asInt() == p2p_close_business_end);
    CHECK(atB["errcode"].asInt() == kCodeOk);

    //会话已结束：再发 2303 返回 901
    Json::Value late;
    late["p2psessionid"] = s.m_sessionId;
    late["data"] = signalData("late", "192.168.10.2", kLatePort);
    CHECK(request(a, cmd_p2p_signal, late)["code"].asInt() == error_code_p2p_session_not_exist);
}

TEST_CASE("P2PL5 对端断开连接：另一方收到 2305", "[.live][p2plive]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    const OpenedSession s = openSession(a, b);

    b.Close();
    const Json::Value atA = waitPush(a, cmd_p2p_close);
    CHECK(atA["p2psessionid"].asString() == s.m_sessionId);
    CHECK(atA["reason"].asInt() == p2p_close_peer_offline);
    CHECK(atA["errcode"].asInt() == error_code_p2p_peer_offline);
}

TEST_CASE("P2PL6 对端只退出登录、不断开连接：另一方同样收到 2305", "[.live][p2plive]")
{
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    const OpenedSession s = openSession(a, b);

    REQUIRE(b.Send(cmd_logout, "{}"));
    const Json::Value atA = waitPush(a, cmd_p2p_close);
    CHECK(atA["p2psessionid"].asString() == s.m_sessionId);
    CHECK(atA["reason"].asInt() == p2p_close_peer_offline);
    CHECK(atA["errcode"].asInt() == error_code_p2p_peer_offline);
}

TEST_CASE("P2PL7 企业关闭 P2P：1013、2301 与 2306 的表现", "[.live][p2plive]")
{
    //先确保开关处于开启状态（上一次运行若中途退出可能留下关闭），再让两人登录
    P2PSwitchRestorer restorer;
    restorer.m_token = adminLogin();
    REQUIRE(setP2PSwitch(restorer.m_token, "1"));
    LiveChatClient a;
    LiveChatClient b;
    login(a, kAccountA);
    login(b, kAccountB);
    a.ClearBacklog();

    //关闭：在线的 A 收到 1013，p2p 为 0、文件共享不受影响
    REQUIRE(setP2PSwitch(restorer.m_token, "0"));
    const Json::Value off = waitFeatures(a, 0);
    INFO("features after switching off: " << compactJson(off));
    REQUIRE(off.isObject());
    CHECK(off["fileshare"].asInt() == 1);

    //2301：照样登记，但 enabled 为假、不带探测地址
    const Json::Value ra = registerP2P(a);
    CHECK(ra["code"].asInt() == kCodeOk);
    CHECK_FALSE(ra["enabled"].asBool());
    CHECK((!ra.isMember("probe") || ra["probe"].empty()));
    REQUIRE(registerP2P(b)["code"].asInt() == kCodeOk);

    //2306：双方都已登记，因企业关闭而返回 900
    Json::Value req;
    req["peeruid"] = std::to_string(b.UserId());
    CHECK(request(a, cmd_p2p_debug_open, req)["code"].asInt() == error_code_p2p_disabled);

    //恢复：A 收到 p2p 为 1 的 1013，再次报到时 enabled 为真
    REQUIRE(setP2PSwitch(restorer.m_token, "1"));
    REQUIRE(waitFeatures(a, 1).isObject());
    CHECK(registerP2P(a)["enabled"].asBool());
}
