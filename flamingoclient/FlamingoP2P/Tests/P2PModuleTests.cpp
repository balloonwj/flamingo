/**
 *  FlamingoP2P.dll 加载与模块生命周期单元测试（Suite P2PD，标签 [p2pdll]），P2PModuleTests.cpp
 *  覆盖（对应 docs/P2P通信方案.md 第 1 期第 1b 步，2026-10-03 与用户确认的用例）：
 *    - P2PD1 DLL 只导出 FlamingoP2P_Create 一个函数；用错误的接口版本号加载返回「版本不一致」并卸载；
 *            经通用加载器正常加载、取到模块、卸载，连做两轮；
 *    - P2PD2 Init 成功，宿主收到一行带 "[p2p]" 前缀与调用处的初始化日志，日志里报告 DLL 内的
 *            libsodium 已初始化（DLL 静态链接的 libsodium 须由 DLL 自己调用 sodium_init，exe 里的调用只初始化 exe 那一份）；
 *    - P2PD3 重复 Init 只初始化一次；宿主为空、参数为空、结构体大小不符、目录为空时 Init 失败；
 *    - P2PD4 未初始化就 Shutdown、重复 Shutdown 都安全；Shutdown 之后不再回调宿主；
 *    - P2PD5 服务端帧：只接收 2300~2399 段的命令；号段外的命令、包体为空、未初始化、已关闭时一律拒收；
 *    - P2PD6 业务方按用途登记：登记成功、同一用途重复登记被拒、注销后可以重新登记，非法参数被拒，
 *            未初始化时登记失败，Shutdown 清空全部登记；
 *    - P2PD7（第 3d 步）模块接上引擎：登录后经宿主发出 2301；收到 2302 后建会话并发出 2303（没有探测服务器时
 *            直接发）；Shutdown 关闭这个会话，经宿主发出 2305（原因为本方退出）；
 *    - P2PD8（第 5 期 5d）报到带已登记的用途：登录前登记不发 2301；登录后的 2301 带上已登记的用途；登录期间登记
 *            或注销业务方都重新发 2301、用途随之变化；退出登录之后再登记不发。
 *
 *  第 3d 步起模块带有 IO 线程，宿主的 Log 与 SendP2PCommand 会在 IO 线程上被调用，桩宿主因此加锁。
 *
 *  本文件由 FlamingoUnit 编译；FlamingoUnit 构建后把同配置的 FlamingoP2P.dll 复制到测试程序
 *  所在目录（见 FlamingoUnit.vcxproj 末尾的 CopyFlamingoP2PDll）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../Source/App/PluginModuleLoader.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "../FlamingoP2PApi.h"

using balloonapp::CPluginModuleLoader;
using balloonapp::plugin_load_ok;
using balloonapp::plugin_load_version_mismatch;
using p2p::IP2PHost;
using p2p::IP2PLink;
using p2p::IP2PLinkHandler;
using p2p::IP2PModule;
using p2p::IP2PService;
using p2p::P2PInitParams;

namespace {

//初始化日志里的标志文字（P2PModule.cpp 的 Init 写出）
const char* const kInitLogMarker = "FlamingoP2P initialized";
//初始化日志里报告 libsodium 已初始化的文字
const char* const kSodiumReadyMarker = "libsodium ready";
//初始化日志的调用处：日志行带有调用处的函数签名，其中含有类名与方法名
const char* const kInitLogCallSite = "CP2PModule::Init";
//关闭日志里的标志文字
const char* const kShutdownLogMarker = "FlamingoP2P shutting down";
//每行日志开头的前缀
const char* const kP2PPrefix = "[p2p] ";

//号段首尾与段外紧邻的两个命令号
const uint16_t kP2PFirstCmd = p2p::kP2PCmdFirst;
const uint16_t kP2PLastCmd = p2p::kP2PCmdLast;
const uint16_t kBelowP2PCmd = p2p::kP2PCmdFirst - 1;
const uint16_t kAboveP2PCmd = p2p::kP2PCmdLast + 1;

//包体为空指针而长度不为 0 的非法组合里使用的长度
const uint32_t kBogusLength = 5;

//一帧合法的 2302 推送包体
const char* const kSessionNotifyBody = "{\"p2psessionid\":\"p2p-1-2-3-4\",\"role\":1,\"purpose\":\"test\"}";

//P2PD7 用的完整 2302 推送包体（带三个时间，引擎据此建会话）
const char* const kFullSessionNotifyBody =
    "{\"p2psessionid\":\"p2p-d7\",\"role\":1,\"purpose\":\"test\",\"featuresessionid\":\"\","
    "\"peeruid\":\"2\",\"peerdeviceid\":\"d7-peer\",\"relayip\":\"127.0.0.1\",\"relayport\":29903,"
    "\"token\":\"t\",\"connecttimeoutms\":3000,\"keepalivems\":1000,\"linktimeoutms\":5000}";

//P2PD7 用的 2301 应答：本企业开启，没有地址探测服务器
const char* const kRegisterResponseBody = "{\"code\":0,\"enabled\":true}";

//P2PD7 里 2303 / 2305 包体应带的会话号，以及 2305 应带的关闭原因（本方退出，p2p_close_self_logout）
const char* const kD7SessionIdField = "\"p2psessionid\":\"p2p-d7\"";
const char* const kSelfLogoutReasonField = "\"reason\":5";

//等待 IO 线程发出信令的最长时间（毫秒）与轮询间隔（毫秒）
const int kWaitSignalMs = 3000;
const int kPollMs = 10;

//断言「没有再发信令」之前等待的时长（毫秒）：投递给 IO 线程的任务在这段时间内早已处理完
const int kNoSignalWaitMs = 300;

//超过 kMaxPurposeChars 一个字符的用途名
const std::string kTooLongPurpose(p2p::kMaxPurposeChars + 1, 'a');

//桩宿主：记录日志与请求发出的信令，不做任何网络操作。各方法可能在模块的 IO 线程上被调用，一律加锁
class StubHost : public IP2PHost
{
public:
    virtual void Log(int level, const char* text) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (text != NULL)
        {
            m_levels.push_back(level);
            m_lines.push_back(text);
        }
    }

    virtual uint32_t SendP2PCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_sentCmds.push_back(cmd);
        m_sentBodies.push_back(std::string(json != NULL ? json : "", len));
        return 0;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return NULL;
    }

    //含有 marker 的日志行数
    size_t CountLinesContaining(const char* marker) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        size_t n = 0;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
                ++n;
        }
        return n;
    }

    //第一条含有 marker 的日志行的下标；没有时返回 -1
    int IndexOfFirstLineContaining(const char* marker) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
                return static_cast<int>(i);
        }
        return -1;
    }

    //第 index 行日志与它的级别（下标须有效）
    void LineAt(int index, std::string& line, int& level) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        line = m_lines[index];
        level = m_levels[index];
    }

    //日志行数
    size_t LineCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_lines.size();
    }

    //请求发出的信令条数
    size_t SentCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_sentCmds.size();
    }

    //第一条命令号为 cmd 的信令的包体；没有时返回 false
    bool FindSent(uint16_t cmd, std::string& body) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_sentCmds.size(); ++i)
        {
            if (m_sentCmds[i] == cmd)
            {
                body = m_sentBodies[i];
                return true;
            }
        }
        return false;
    }

    //命令号为 cmd 的信令条数
    size_t CountSent(uint16_t cmd) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        size_t n = 0;
        for (size_t i = 0; i < m_sentCmds.size(); ++i)
        {
            if (m_sentCmds[i] == cmd)
                ++n;
        }
        return n;
    }

    //最后一条命令号为 cmd 的信令的包体；没有时返回 false
    bool LastSent(uint16_t cmd, std::string& body) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = m_sentCmds.size(); i > 0; --i)
        {
            if (m_sentCmds[i - 1] == cmd)
            {
                body = m_sentBodies[i - 1];
                return true;
            }
        }
        return false;
    }

    //最多等 timeoutMs 毫秒，直到命令号为 cmd 的信令发出过 count 条
    bool WaitSentCount(uint16_t cmd, size_t count, int timeoutMs) const
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            if (CountSent(cmd) >= count)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

    //最多等 timeoutMs 毫秒，直到发出过命令号为 cmd 的信令；等到时返回 true 并给出包体
    bool WaitSent(uint16_t cmd, int timeoutMs, std::string& body) const
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            if (FindSent(cmd, body))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

private:
    mutable std::mutex       m_mutex;        //保护以下四个成员
    std::vector<int>         m_levels;       //每行日志的级别，与 m_lines 一一对应
    std::vector<std::string> m_lines;        //收到的日志行（UTF-8）
    std::vector<uint16_t>    m_sentCmds;     //请求发出的信令命令号
    std::vector<std::string> m_sentBodies;   //请求发出的信令包体
};

//桩业务方：只记录收到的回调次数（本步还不会有回调，用来占住登记）
class StubHandler : public IP2PLinkHandler
{
public:
    StubHandler()
        : m_ready(0)
        , m_failed(0)
        , m_closed(0)
    {
    }

    virtual void OnLinkReady(const char* featureSessionId, IP2PLink* link) override
    {
        (void)featureSessionId;
        (void)link;
        ++m_ready;
    }

    virtual void OnLinkFailed(const char* featureSessionId, int code) override
    {
        (void)featureSessionId;
        (void)code;
        ++m_failed;
    }

    virtual void OnLinkClosed(const char* featureSessionId, int reason) override
    {
        (void)featureSessionId;
        (void)reason;
        ++m_closed;
    }

    int m_ready;    //OnLinkReady 被调用的次数
    int m_failed;   //OnLinkFailed 被调用的次数
    int m_closed;   //OnLinkClosed 被调用的次数
};

//测试程序所在目录（不带结尾反斜杠）
std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s(buf);
    size_t pos = s.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? s : s.substr(0, pos);
}

//测试程序旁边那份 FlamingoP2P.dll 的完整路径
std::wstring P2PDllPath()
{
    return ExeDir() + L"\\" + FLAMINGO_P2P_DLL_NAME;
}

//合法的初始化参数（调用方须保证 appDir 在 Init 期间有效）
P2PInitParams MakeParams(const std::wstring& appDir)
{
    P2PInitParams p;
    p.m_cbSize = sizeof(P2PInitParams);
    p.m_appDir = appDir.c_str();
    return p;
}

//加载 DLL 并取得模块；失败时直接判本用例失败
IP2PModule* LoadModule(CPluginModuleLoader<IP2PModule>& loader)
{
    std::wstring path = P2PDllPath();
    REQUIRE(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
    REQUIRE(loader.Load(path.c_str(), FLAMINGO_P2P_FACTORY_NAME, p2p::kApiVersion) == plugin_load_ok);
    REQUIRE(loader.GetModule() != NULL);
    return loader.GetModule();
}

//读出一个已加载模块的导出名清单（按 PE 导出目录逐个取名字）
std::vector<std::string> ListExportNames(HMODULE module)
{
    std::vector<std::string> names;
    const unsigned char* base = reinterpret_cast<const unsigned char*>(module);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return names;
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return names;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0)
        return names;
    const IMAGE_EXPORT_DIRECTORY* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const DWORD* nameRvas = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i)
        names.push_back(reinterpret_cast<const char*>(base + nameRvas[i]));
    return names;
}

} // namespace

TEST_CASE("P2PD1 只导出工厂函数；版本不一致被拒；正常加载与卸载连做两轮", "[p2pdll][pluginloader]")
{
    std::wstring path = P2PDllPath();
    REQUIRE(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);

    //版本不一致：加载器返回「版本不一致」，DLL 被卸载
    {
        CPluginModuleLoader<IP2PModule> loader;
        CHECK(loader.Load(path.c_str(), FLAMINGO_P2P_FACTORY_NAME, p2p::kApiVersion + 1) == plugin_load_version_mismatch);
        CHECK_FALSE(loader.IsLoaded());
        CHECK(loader.GetModule() == NULL);
        CHECK(::GetModuleHandleW(FLAMINGO_P2P_DLL_NAME) == NULL);
    }

    for (int round = 0; round < 2; ++round)
    {
        CPluginModuleLoader<IP2PModule> loader;
        IP2PModule* module = LoadModule(loader);
        CHECK(loader.IsLoaded());
        CHECK(module->GetApiVersion() == p2p::kApiVersion);

        HMODULE dll = ::GetModuleHandleW(FLAMINGO_P2P_DLL_NAME);
        REQUIRE(dll != NULL);
        //导出表里只有工厂函数一个名字（导出名由 FlamingoP2P.def 固定，32 位与 64 位相同）
        std::vector<std::string> names = ListExportNames(dll);
        REQUIRE(names.size() == 1);
        CHECK(names[0] == FLAMINGO_P2P_FACTORY_NAME);

        loader.Unload();
        CHECK_FALSE(loader.IsLoaded());
        CHECK(::GetModuleHandleW(FLAMINGO_P2P_DLL_NAME) == NULL);
    }
}

TEST_CASE("P2PD2 Init 成功并经宿主写出带前缀与调用处的初始化日志，libsodium 已初始化", "[p2pdll]")
{
    CPluginModuleLoader<IP2PModule> loader;
    IP2PModule* module = LoadModule(loader);

    StubHost host;
    std::wstring appDir = ExeDir();
    P2PInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    REQUIRE(host.CountLinesContaining(kInitLogMarker) == 1);
    int index = host.IndexOfFirstLineContaining(kInitLogMarker);
    std::string line;
    int level = 0;
    host.LineAt(index, line, level);
    CHECK(line.compare(0, strlen(kP2PPrefix), kP2PPrefix) == 0);
    CHECK(line.find(kInitLogCallSite) != std::string::npos);
    CHECK(line.find(kSodiumReadyMarker) != std::string::npos);
    CHECK(level == p2p::p2p_log_info);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("P2PD3 重复 Init 只初始化一次，非法参数 Init 失败", "[p2pdll]")
{
    CPluginModuleLoader<IP2PModule> loader;
    IP2PModule* module = LoadModule(loader);

    StubHost host;
    std::wstring appDir = ExeDir();
    P2PInitParams params = MakeParams(appDir);

    //非法参数：宿主为空、参数为空、结构体大小不符、目录为空，都失败且不写日志
    CHECK_FALSE(module->Init(NULL, &params));
    CHECK_FALSE(module->Init(&host, NULL));
    P2PInitParams badSize = params;
    badSize.m_cbSize = sizeof(P2PInitParams) + 1;
    CHECK_FALSE(module->Init(&host, &badSize));
    P2PInitParams noDir = params;
    noDir.m_appDir = NULL;
    CHECK_FALSE(module->Init(&host, &noDir));
    CHECK(host.LineCount() == 0);

    //合法参数初始化两次：都返回成功，但只初始化一次
    CHECK(module->Init(&host, &params));
    CHECK(module->Init(&host, &params));
    CHECK(host.CountLinesContaining(kInitLogMarker) == 1);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("P2PD4 未初始化就关闭、重复关闭都安全，关闭后不再回调宿主", "[p2pdll]")
{
    CPluginModuleLoader<IP2PModule> loader;
    IP2PModule* module = LoadModule(loader);

    //未初始化就关闭
    module->Shutdown();

    StubHost host;
    std::wstring appDir = ExeDir();
    P2PInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    module->Shutdown();
    CHECK(host.CountLinesContaining(kShutdownLogMarker) == 1);

    //关闭之后：任何调用都不再写日志、不再请求发信令
    const size_t linesAfterShutdown = host.LineCount();
    StubHandler handler;
    module->Shutdown();
    module->OnLoggedIn();
    module->OnLoggingOut();
    CHECK_FALSE(module->OnServerFrame(cmd_p2p_session_notify, 0, kSessionNotifyBody,
                                      static_cast<uint32_t>(strlen(kSessionNotifyBody))));
    CHECK_FALSE(module->GetService()->RegisterHandler("folder", &handler));
    CHECK(host.LineCount() == linesAfterShutdown);
    CHECK(host.SentCount() == 0);

    loader.Unload();
}

TEST_CASE("P2PD5 服务端帧只接收 2300 段，号段外、空包体、未初始化与已关闭时拒收", "[p2pdll]")
{
    CPluginModuleLoader<IP2PModule> loader;
    IP2PModule* module = LoadModule(loader);
    const uint32_t notifyLen = static_cast<uint32_t>(strlen(kSessionNotifyBody));

    //初始化之前：一律拒收
    CHECK_FALSE(module->OnServerFrame(cmd_p2p_session_notify, 0, kSessionNotifyBody, notifyLen));

    StubHost host;
    std::wstring appDir = ExeDir();
    P2PInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    //号段外的命令：拒收（含紧邻号段的两个值与文件共享的命令）
    CHECK_FALSE(module->OnServerFrame(kBelowP2PCmd, 0, "{}", 2));
    CHECK_FALSE(module->OnServerFrame(kAboveP2PCmd, 0, "{}", 2));
    CHECK_FALSE(module->OnServerFrame(cmd_share_create, 0, "{}", 2));

    //包体为空：空指针带长度、空指针不带长度、非空指针但长度为 0，都拒收（P2P 信令都带 JSON 包体）
    CHECK_FALSE(module->OnServerFrame(cmd_p2p_session_notify, 0, NULL, kBogusLength));
    CHECK_FALSE(module->OnServerFrame(cmd_p2p_session_notify, 0, NULL, 0));
    CHECK_FALSE(module->OnServerFrame(cmd_p2p_session_notify, 0, kSessionNotifyBody, 0));

    //号段首尾与段内的帧：接收
    CHECK(module->OnServerFrame(kP2PFirstCmd, 0, "{}", 2));
    CHECK(module->OnServerFrame(kP2PLastCmd, 0, "{}", 2));
    CHECK(module->OnServerFrame(cmd_p2p_session_notify, 0, kSessionNotifyBody, notifyLen));

    //关闭之后：拒收
    module->Shutdown();
    CHECK_FALSE(module->OnServerFrame(cmd_p2p_session_notify, 0, kSessionNotifyBody, notifyLen));

    loader.Unload();
}

TEST_CASE("P2PD6 业务方按用途登记：成功、重复被拒、注销后可重新登记，非法参数与未初始化被拒", "[p2pdll]")
{
    CPluginModuleLoader<IP2PModule> loader;
    IP2PModule* module = LoadModule(loader);
    IP2PService* service = module->GetService();
    REQUIRE(service != NULL);

    StubHandler folderHandler;
    StubHandler otherHandler;

    //未初始化：登记失败
    CHECK_FALSE(service->RegisterHandler("folder", &folderHandler));

    StubHost host;
    std::wstring appDir = ExeDir();
    P2PInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    //初始化前后取到的是同一个服务对象
    CHECK(module->GetService() == service);

    //登记成功；同一用途再登记（无论是不是同一个业务方）被拒；别的用途照常登记
    CHECK(service->RegisterHandler("folder", &folderHandler));
    CHECK_FALSE(service->RegisterHandler("folder", &otherHandler));
    CHECK_FALSE(service->RegisterHandler("folder", &folderHandler));
    CHECK(service->RegisterHandler("fileshare", &otherHandler));

    //注销：成功一次，重复注销与注销没登记过的用途都失败；注销后可以重新登记
    CHECK(service->UnregisterHandler("folder"));
    CHECK_FALSE(service->UnregisterHandler("folder"));
    CHECK_FALSE(service->UnregisterHandler("call"));
    CHECK(service->RegisterHandler("folder", &otherHandler));

    //非法参数：用途为空指针、空串、超长，业务方为空指针
    CHECK_FALSE(service->RegisterHandler(NULL, &folderHandler));
    CHECK_FALSE(service->RegisterHandler("", &folderHandler));
    CHECK_FALSE(service->RegisterHandler(kTooLongPurpose.c_str(), &folderHandler));
    CHECK_FALSE(service->RegisterHandler("call", NULL));
    CHECK_FALSE(service->UnregisterHandler(NULL));
    CHECK_FALSE(service->UnregisterHandler(""));

    //Shutdown 清空全部登记：重新初始化后，原来的用途可以直接登记
    module->Shutdown();
    REQUIRE(module->Init(&host, &params));
    CHECK(service->RegisterHandler("folder", &folderHandler));
    CHECK(service->RegisterHandler("fileshare", &folderHandler));

    //本步还没有建链逻辑，任何回调都不应发生
    CHECK(folderHandler.m_ready == 0);
    CHECK(folderHandler.m_failed == 0);
    CHECK(folderHandler.m_closed == 0);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("P2PD7 模块接上引擎：登录后发 2301，收到 2302 建会话并发 2303，Shutdown 发 2305", "[p2pdll]")
{
    CPluginModuleLoader<IP2PModule> loader;
    IP2PModule* module = LoadModule(loader);

    StubHost host;
    std::wstring appDir = ExeDir();
    P2PInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    //登录成功：IO 线程经宿主发出 2301，包体带协议版本
    module->OnLoggedIn();
    std::string body;
    REQUIRE(host.WaitSent(cmd_p2p_register, kWaitSignalMs, body));
    CHECK(body.find("\"version\"") != std::string::npos);

    //2301 应答（开启、没有探测服务器）之后收到 2302：建会话，不探测，直接发出本方的 2303
    REQUIRE(module->OnServerFrame(cmd_p2p_register, 1, kRegisterResponseBody,
                                  static_cast<uint32_t>(strlen(kRegisterResponseBody))));
    REQUIRE(module->OnServerFrame(cmd_p2p_session_notify, 0, kFullSessionNotifyBody,
                                  static_cast<uint32_t>(strlen(kFullSessionNotifyBody))));
    REQUIRE(host.WaitSent(cmd_p2p_signal, kWaitSignalMs, body));
    CHECK(body.find(kD7SessionIdField) != std::string::npos);
    CHECK(body.find("\"pubkey\"") != std::string::npos);
    std::string closeBody;
    CHECK_FALSE(host.FindSent(cmd_p2p_close, closeBody));

    //Shutdown：引擎关闭仍在建链的会话，经宿主发出 2305，原因为本方退出
    module->Shutdown();
    REQUIRE(host.FindSent(cmd_p2p_close, closeBody));
    CHECK(closeBody.find(kD7SessionIdField) != std::string::npos);
    CHECK(closeBody.find(kSelfLogoutReasonField) != std::string::npos);

    loader.Unload();
}

namespace {

//最后一条 2301 的用途列表（以逗号连接）；包体不是对象或没有 purposes 数组时返回 "<bad>"
std::string LastRegisterPurposes(const StubHost& host)
{
    std::string body;
    if (!host.LastSent(cmd_p2p_register, body))
        return "<none>";
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();
    Json::Value root;
    JSONCPP_STRING errs;
    const bool ok = reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs);
    delete reader;
    if (!ok || !root.isObject() || !root["purposes"].isArray())
        return "<bad>";
    std::string joined;
    for (Json::ArrayIndex i = 0; i < root["purposes"].size(); ++i)
    {
        if (i > 0)
            joined += ",";
        joined += root["purposes"][i].asString();
    }
    return joined;
}

} // namespace

TEST_CASE("P2PD8 报到带已登记的用途：登录后的 2301 带上，登录期间登记或注销业务方后重新报到", "[p2pdll]")
{
    CPluginModuleLoader<IP2PModule> loader;
    IP2PModule* module = LoadModule(loader);
    StubHost host;
    std::wstring appDir = ExeDir();
    P2PInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    IP2PService* service = module->GetService();
    StubHandler folderHandler;
    StubHandler callHandler;

    //登录之前登记：还没登录，不发 2301
    REQUIRE(service->RegisterHandler("folder", &folderHandler));
    std::this_thread::sleep_for(std::chrono::milliseconds(kNoSignalWaitMs));
    CHECK(host.CountSent(cmd_p2p_register) == 0);

    //登录：2301 带上已登记的 folder
    module->OnLoggedIn();
    REQUIRE(host.WaitSentCount(cmd_p2p_register, 1, kWaitSignalMs));
    CHECK(LastRegisterPurposes(host) == "folder");

    //登录期间再登记 call：重新报到，两个用途按名字排序
    REQUIRE(service->RegisterHandler("call", &callHandler));
    REQUIRE(host.WaitSentCount(cmd_p2p_register, 2, kWaitSignalMs));
    CHECK(LastRegisterPurposes(host) == "call,folder");

    //注销 folder：重新报到，只剩 call
    REQUIRE(service->UnregisterHandler("folder"));
    REQUIRE(host.WaitSentCount(cmd_p2p_register, 3, kWaitSignalMs));
    CHECK(LastRegisterPurposes(host) == "call");

    //重复登记被拒：登记表没变，不重新报到
    CHECK_FALSE(service->RegisterHandler("call", &folderHandler));

    //退出登录之后再登记：不发 2301
    module->OnLoggingOut();
    REQUIRE(service->RegisterHandler("folder", &folderHandler));
    std::this_thread::sleep_for(std::chrono::milliseconds(kNoSignalWaitMs));
    CHECK(host.CountSent(cmd_p2p_register) == 3);

    module->Shutdown();
    loader.Unload();
}
