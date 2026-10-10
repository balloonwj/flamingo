/**
 *  主程序侧 P2P 宿主单元测试（Suite P2PH，标签 [p2phost]），P2PHostTests.cpp
 *  覆盖（对应 docs/P2P通信方案.md 第 1 期第 1c 步，2026-10-03 与用户确认的用例）：
 *    - P2PH1 宿主加载：DLL 不存在时 Load 失败、宿主照常运行，之后登录、退出、收帧、关闭都安全；
 *            DLL 存在时加载并初始化成功，DLL 的初始化日志经宿主写出、带 "[p2p]" 前缀，服务接口可取；
 *    - P2PH2 生命周期：收帧经路由送到模块；登录与退出登录的通知送到模块；宿主代发命令转给网络层、
 *            包体为空指针而长度不为 0 时不发；关闭后模块已关闭、DLL 已卸载、帧不再转交、
 *            重复关闭与关闭后的各种调用都安全。
 *
 *  测试不经过 CIUSocket：宿主经 IP2PNetwork 发命令、取公钥，这里注入一个假的网络层；
 *  DLL 经 IP2PHost::Log 写出的日志由继承 CP2PHost 的 RecordingHost 记下。用的是测试程序旁边那份
 *  真实的 FlamingoP2P.dll（FlamingoUnit 构建后由 CopyFlamingoP2PDll 复制过来）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <mutex>
#include <string>
#include <vector>

#include "../net/Protocol.h"
#include "../Feature/P2P/P2PHost.h"

using balloonp2p::CP2PHost;
using balloonp2p::IP2PNetwork;

namespace {

//DLL 初始化、关闭、收帧、登录、退出时写出的日志标志（见 FlamingoP2P/Host/P2PModule.cpp）
const char* const kInitLogMarker = "FlamingoP2P initialized";
const char* const kShutdownLogMarker = "FlamingoP2P shutting down";
const char* const kFrameLogMarker = "p2p frame received";
const char* const kLoggedInMarker = "notified: logged in";
const char* const kLoggingOutMarker = "notified: logging out";
//每行日志开头的前缀
const char* const kP2PPrefix = "[p2p] ";

//不存在的 DLL 文件名
const wchar_t* const kMissingDllName = L"FlamingoP2P_missing.dll";

//假网络层每次发送返回的 seq
const uint32_t kFakeSeq = 9001;

//一帧合法的 2302 推送包体与 2301 报到请求包体
const char* const kNotifyBody = "{\"p2psessionid\":\"p2p-1-2-3-4\",\"role\":1,\"purpose\":\"test\"}";
const char* const kRegisterBody = "{\"version\":1}";

//包体为空指针而长度不为 0 的非法组合里使用的长度
const uint32_t kBogusLength = 3;

//假的 32 字节服务器公钥
const unsigned char kFakeServerKey[32] = { 0x11, 0x22, 0x33 };

//假网络层：记录宿主请求发出的命令。DLL 的 IO 线程也可能经宿主发命令，记录与读取都加锁
class FakeNetwork : public IP2PNetwork
{
public:
    virtual uint32_t SendP2PCommand(uint16_t cmd, const std::string& body) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_cmds.push_back(cmd);
        m_bodies.push_back(body);
        return kFakeSeq;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return kFakeServerKey;
    }

    //请求发出的命令总数
    size_t CmdCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_cmds.size();
    }

    //第 index 条命令的命令号与包体；越界时返回 0 与空串
    uint16_t CmdAt(size_t index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return (index < m_cmds.size()) ? m_cmds[index] : 0;
    }

    std::string BodyAt(size_t index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return (index < m_bodies.size()) ? m_bodies[index] : std::string();
    }

private:
    mutable std::mutex       m_mutex;    //保护下面两项
    std::vector<uint16_t>    m_cmds;     //请求发出的命令号
    std::vector<std::string> m_bodies;   //请求发出的包体
};

//记录型宿主：DLL 经 IP2PHost::Log 写出的每一行都记下来，再交给 CP2PHost 照常写进日志文件。
//DLL 初始化时就启动了引擎的 IO 线程，该线程一启动就写日志（见 FlamingoP2P/Io/P2PEngine.cpp 的 ThreadMain），
//与 UI 线程写的初始化日志、测试线程的读取同时发生，所以记录与读取都加锁
class RecordingHost : public CP2PHost
{
public:
    virtual void Log(int level, const char* text) override
    {
        if (text != NULL)
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_lines.push_back(text);
        }
        CP2PHost::Log(level, text);
    }

    //已记下的日志行数
    size_t LineCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_lines.size();
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

    //第一条含有 marker 的日志行；没有时返回空串
    std::string FirstLineContaining(const char* marker) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
                return m_lines[i];
        }
        return std::string();
    }

private:
    mutable std::mutex       m_mutex;   //保护 m_lines
    std::vector<std::string> m_lines;   //DLL 写出的日志行（UTF-8）
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

} // namespace

TEST_CASE("P2PH1 宿主加载：DLL 不存在时照常运行，存在时加载并初始化成功", "[p2phost]")
{
    const std::wstring appDir = ExeDir();
    FakeNetwork network;

    SECTION("DLL 不存在：Load 失败，之后的各种调用都安全")
    {
        RecordingHost host;
        const std::wstring missing = appDir + L"\\" + kMissingDllName;
        CHECK_FALSE(host.Load(missing.c_str(), appDir.c_str(), &network));
        CHECK_FALSE(host.IsModuleReady());
        CHECK(host.GetService() == NULL);

        host.OnLoginSucceeded();
        host.OnLoggingOut();
        host.OnServerFrame(cmd_p2p_session_notify, 0, kNotifyBody);
        CHECK(host.GetDroppedFrameCount() == 1);
        host.Shutdown();
        host.Shutdown();
        CHECK(host.LineCount() == 0);
        CHECK(network.CmdCount() == 0);
    }

    SECTION("DLL 存在：加载并初始化，DLL 的初始化日志经宿主写出")
    {
        RecordingHost host;
        const std::wstring path = appDir + L"\\" + FLAMINGO_P2P_DLL_NAME;
        REQUIRE(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
        REQUIRE(host.Load(path.c_str(), appDir.c_str(), &network));
        CHECK(host.IsModuleReady());
        CHECK(host.GetService() != NULL);
        CHECK(::GetModuleHandleW(FLAMINGO_P2P_DLL_NAME) != NULL);

        REQUIRE(host.CountLinesContaining(kInitLogMarker) == 1);
        const std::string line = host.FirstLineContaining(kInitLogMarker);
        CHECK(line.compare(0, strlen(kP2PPrefix), kP2PPrefix) == 0);

        //已加载时再次 Load：直接返回成功，不重复初始化
        CHECK(host.Load(path.c_str(), appDir.c_str(), &network));
        CHECK(host.CountLinesContaining(kInitLogMarker) == 1);

        host.Shutdown();
        CHECK_FALSE(host.IsModuleReady());
        CHECK(::GetModuleHandleW(FLAMINGO_P2P_DLL_NAME) == NULL);
    }
}

TEST_CASE("P2PH2 生命周期：收帧、登录与退出通知、代发命令，关闭后的状态", "[p2phost]")
{
    const std::wstring appDir = ExeDir();
    const std::wstring path = appDir + L"\\" + FLAMINGO_P2P_DLL_NAME;
    REQUIRE(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);

    FakeNetwork network;
    RecordingHost host;
    REQUIRE(host.Load(path.c_str(), appDir.c_str(), &network));

    //收帧：2300 段的帧经路由送到模块（模块记一行日志）；号段外的帧不转交、不计丢弃
    host.OnServerFrame(cmd_p2p_session_notify, 0, kNotifyBody);
    CHECK(host.CountLinesContaining(kFrameLogMarker) == 1);
    host.OnServerFrame(cmd_share_create, 0, "{}");
    CHECK(host.CountLinesContaining(kFrameLogMarker) == 1);
    CHECK(host.GetDroppedFrameCount() == 0);

    //登录与退出登录的通知送到模块
    host.OnLoginSucceeded();
    CHECK(host.CountLinesContaining(kLoggedInMarker) == 1);
    host.OnLoggingOut();
    CHECK(host.CountLinesContaining(kLoggingOutMarker) == 1);

    //代发命令：原样转给网络层并返回其 seq；包体为空指针而长度不为 0 时不发
    const uint32_t registerLen = static_cast<uint32_t>(strlen(kRegisterBody));
    CHECK(host.SendP2PCommand(cmd_p2p_register, kRegisterBody, registerLen) == kFakeSeq);
    REQUIRE(network.CmdCount() == 1);
    CHECK(network.CmdAt(0) == cmd_p2p_register);
    CHECK(network.BodyAt(0) == kRegisterBody);
    CHECK(host.SendP2PCommand(cmd_p2p_register, NULL, kBogusLength) == 0);
    CHECK(network.CmdCount() == 1);
    CHECK(host.GetServerPublicKey() == kFakeServerKey);

    //关闭：模块收到关闭、DLL 被卸载
    host.Shutdown();
    CHECK(host.CountLinesContaining(kShutdownLogMarker) == 1);
    CHECK_FALSE(host.IsModuleReady());
    CHECK(host.GetService() == NULL);
    CHECK(::GetModuleHandleW(FLAMINGO_P2P_DLL_NAME) == NULL);

    //关闭之后：帧丢弃并计数，其余调用都安全，DLL 不再写日志
    const size_t linesAfterShutdown = host.LineCount();
    host.OnServerFrame(cmd_p2p_session_notify, 0, kNotifyBody);
    CHECK(host.GetDroppedFrameCount() == 1);
    host.OnLoginSucceeded();
    host.OnLoggingOut();
    host.Shutdown();
    CHECK(host.LineCount() == linesAfterShutdown);
}
