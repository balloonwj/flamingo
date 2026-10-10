/**
 *  主程序一侧的 P2P 宿主：加载 FlamingoP2P.dll、实现 p2p::IP2PHost 交给它，并在登录、退出登录、收帧、退出程序
 *  这几个时机通知它（docs/P2P通信方案.md 4.2、8.4）。
 *
 *  与文件共享的 CFileShareHost 不同，P2P 模块在<u>启动时</u>加载（它的初始化不依赖登录，另外两个 DLL 以后还要从
 *  宿主取它的服务接口），在<u>退出时最后</u>卸载。DLL 不存在或加载失败时只记日志，客户端照常运行，各业务继续走
 *  原来的中转。
 *
 *  发命令、取服务器公钥经 IP2PNetwork 注入：exe 里是转给 CIUSocket 的 CP2PSocketNetwork（P2PSocketNetwork.h），
 *  单元测试里注入假的网络层。所以本类不包含 IUSocket.h，可以直接编进 FlamingoUnit、用真实的 DLL 测试。
 *
 *  线程：Load / OnLoginSucceeded / OnLoggingOut / Shutdown / GetService 在 UI 线程上调用；OnServerFrame 在网络
 *  接收线程上调用；IP2PHost 的三个方法可能在 DLL 的任意线程上被调用。
 *
 *  典型用法（exe）：
 *      CP2PHost::GetInstance().Load(dllPath, appDir, &CP2PSocketNetwork::GetInstance());   // CAppShell 启动时
 *      CP2PHost::GetInstance().OnLoginSucceeded();                                          // 登录成功后
 *      CP2PHost::GetInstance().OnServerFrame(cmd, seq, body);                               // CIUSocket 收到 2300 段帧
 *      CP2PHost::GetInstance().Shutdown();                                                  // CAppShell 退出时最后调用
 *
 *  单元测试见 Source/Tests/P2PHostTests.cpp（标签 [p2phost]）。
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <string>

#include "../../App/PluginModuleLoader.h"
#include "../../../FlamingoP2P/FlamingoP2PApi.h"
#include "P2PChannel.h"

namespace balloonp2p {

/**
 *  宿主经它向主连接发命令、取服务器公钥。exe 里由 CP2PSocketNetwork 转给 CIUSocket，测试里注入假的实现。
 *  析构函数受保护：宿主只借用，不负责销毁。
 */
class IP2PNetwork
{
public:
    /**
     *  经主连接发送一条 P2P 信令。实现方按 P2PChannelPolicy 校验（号段、包体非空、已连接）。
     *  可能在任意线程上调用，实现方须线程安全。
     *  @param cmd  命令号
     *  @param body 包体（UTF-8 JSON）
     *  @return 已放入发送队列时返回所用的帧 seq（大于 0）；不放行时返回 0
     */
    virtual uint32_t SendP2PCommand(uint16_t cmd, const std::string& body) = 0;

    /**
     *  取 chatserver 的公钥。可能在任意线程上调用。
     *  @return 指向 32 字节公钥的指针，在进程生命周期内有效；尚未取得时返回 NULL
     */
    virtual const unsigned char* GetServerPublicKey() = 0;

protected:
    ~IP2PNetwork()
    {
    }
};

/**
 *  CP2PHost —— 主程序一侧的 P2P 宿主。exe 里经 GetInstance 使用进程内唯一的对象；构造函数公开，单元测试自建实例。
 */
class CP2PHost : public p2p::IP2PHost
{
public:
    /**
     *  取 exe 里进程内唯一的宿主对象。
     *  @return 宿主对象（静态存储）
     */
    static CP2PHost& GetInstance();

    CP2PHost();
    virtual ~CP2PHost();

    CP2PHost(const CP2PHost&) = delete;
    CP2PHost& operator=(const CP2PHost&) = delete;

    /**
     *  加载并初始化 P2P 模块。已加载时直接返回 true；上一次失败后可以再次调用重试。UI 线程调用。
     *  @param dllPath FlamingoP2P.dll 的完整路径，以 L'\0' 结尾
     *  @param appDir  exe 所在目录（不带结尾反斜杠），交给模块的 Init
     *  @param network 网络层（借用，不持有；须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @return 加载并初始化成功返回 true；DLL 不存在、版本不符、Init 失败或参数非法时返回 false（并记日志）
     */
    bool Load(const wchar_t* dllPath, const wchar_t* appDir, IP2PNetwork* network);

    /**
     *  模块是否已加载并初始化。
     *  @return 已就绪返回 true
     */
    bool IsModuleReady() const;

    /**
     *  登录成功（含断线重连成功）后调用，转告模块。模块未就绪时无效果。UI 线程调用。
     */
    void OnLoginSucceeded();

    /**
     *  即将退出登录（含切换账号、被踢下线）时调用，转告模块。模块未就绪时无效果。UI 线程调用。
     */
    void OnLoggingOut();

    /**
     *  关闭并卸载模块：先解除帧路由的挂接，再关闭模块，最后卸载 DLL。未加载与重复调用都安全。UI 线程调用。
     *  exe 退出时须在另外两个 DLL 卸载之后才调用，因为它们以后会持有本模块的服务接口。
     */
    void Shutdown();

    /**
     *  把主连接收到的一帧 2300 段信令交给模块。在网络接收线程上调用；模块未就绪时丢弃并记日志。
     *  @param cmd  命令号
     *  @param seq  帧序号
     *  @param body 包体（UTF-8 JSON）
     */
    void OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    /**
     *  取 P2P 服务接口，供业务方按用途登记。UI 线程调用。
     *  @return 模块就绪时返回服务接口（归 DLL 所有，在 Shutdown 之前有效）；未就绪时返回 NULL
     */
    p2p::IP2PService* GetService();

    /**
     *  取「号段内、但模块未就绪」而被丢弃的帧数，供诊断与单元测试使用。
     *  @return 丢弃的帧数
     */
    uint32_t GetDroppedFrameCount() const;

    //以下为 p2p::IP2PHost 的实现

    /**
     *  把 DLL 写出的一行日志写进 exe 的日志文件（经 CIULog）。可在任意线程上调用。
     *  @param level 日志级别，取值见 p2p::P2PLogLevel；不认识的值按普通信息处理
     *  @param text  日志正文（UTF-8），以 '\0' 结尾；为 NULL 时忽略
     */
    virtual void Log(int level, const char* text) override;

    /**
     *  代 DLL 经主连接发送一条 P2P 信令（转给 IP2PNetwork）。可在任意线程上调用。
     *  @param cmd  命令号，须属于 2300 段
     *  @param json 包体（UTF-8 JSON），不要求以 '\0' 结尾；为 NULL 而 len 不为 0 时不发
     *  @param len  包体字节数
     *  @return 已放入发送队列时返回帧 seq（大于 0）；未加载网络层、参数非法或不放行时返回 0
     */
    virtual uint32_t SendP2PCommand(uint16_t cmd, const char* json, uint32_t len) override;

    /**
     *  取 chatserver 的公钥（转给 IP2PNetwork）。可在任意线程上调用。
     *  @return 指向 32 字节公钥的指针；尚未取得或网络层未登记时返回 NULL
     */
    virtual const unsigned char* GetServerPublicKey() override;

private:
    balloonapp::CPluginModuleLoader<p2p::IP2PModule> m_loader;    //DLL 加载器：持有 DLL 句柄与模块指针，Shutdown 时卸载
    CP2PFrameRouter                                  m_router;    //帧路由：模块就绪后挂接，Shutdown 时先解除
    std::atomic<IP2PNetwork*>                        m_network;   //网络层（借用）；Load 时登记，Shutdown 后清空。DLL 的线程会读它，故用原子量
    bool                                             m_ready;     //模块是否已加载并初始化；只在 UI 线程上读写
};

} // namespace balloonp2p
