/**
 *  FlamingoP2P.dll 的模块对象（IP2PModule 的实现），进程内唯一。
 *
 *  Init 登记宿主、初始化 DLL 内的 libsodium、打开 P2P 服务并启动 P2P 引擎（IO 线程，第 3d 步起）；Shutdown 先停止
 *  引擎（关闭全部会话），再关闭服务、解除宿主。OnServerFrame 做号段与包体检查后交给引擎；OnLoggedIn 让引擎发 2301
 *  报到，OnLoggingOut 让引擎关闭全部会话；业务方登记或注销用途之后（模块是 P2P 服务的观察者）让引擎重新报到。
 *
 *  典型用法：经导出函数取得（见 DllMain.cpp 的 FlamingoP2P_Create），exe 只经 IP2PModule 接口调用。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <mutex>
#include <string>

#include "../FlamingoP2PApi.h"
#include "../Io/P2PEngine.h"
#include "P2PService.h"

namespace p2p {

/**
 *  模块对象：IP2PModule 的实现。单例，由 Instance 返回；对象在 DLL 卸载时随静态存储一起销毁。
 */
class CP2PModule : public IP2PModule, public IP2PServiceObserver
{
public:
    /**
     *  取进程内唯一的模块对象。
     *  @return 模块对象（静态存储，归 DLL 所有）
     */
    static CP2PModule& Instance();

    //以下为 IP2PModule 的实现

    /**
     *  取模块实现的接口版本号，见 IP2PModule::GetApiVersion。
     *  @return kApiVersion
     */
    virtual uint32_t GetApiVersion() const override;

    /**
     *  初始化模块，见 IP2PModule::Init：登记宿主、调用 sodium_init、打开 P2P 服务。重复调用直接返回 true。
     *  @param host   宿主接口（不转移所有权），不可为 NULL
     *  @param params 初始化参数，m_cbSize 须等于 sizeof(P2PInitParams)，m_appDir 不可为 NULL
     *  @return 成功返回 true；参数非法或 libsodium 初始化失败返回 false（失败时不登记宿主、不写日志）
     */
    virtual bool Init(IP2PHost* host, const P2PInitParams* params) override;

    /**
     *  关闭模块，见 IP2PModule::Shutdown：关闭服务（清空业务方登记）、解除宿主。未初始化与重复调用都安全。
     */
    virtual void Shutdown() override;

    /**
     *  登录成功的通知，见 IP2PModule::OnLoggedIn：交给引擎发 2301 报到。未初始化时无效果。
     */
    virtual void OnLoggedIn() override;

    /**
     *  即将退出登录的通知，见 IP2PModule::OnLoggingOut：交给引擎关闭全部会话。未初始化时无效果。
     */
    virtual void OnLoggingOut() override;

    /**
     *  接收主连接上的一帧 P2P 信令，见 IP2PModule::OnServerFrame。在 exe 的网络接收线程上调用。
     *  @param cmd  命令号，须属于 [kP2PCmdFirst, kP2PCmdLast]
     *  @param seq  帧序号
     *  @param body 包体（UTF-8 JSON），不可为 NULL
     *  @param len  包体字节数，须大于 0
     *  @return 接收（复制后投递给引擎的 IO 线程）返回 true；号段外、包体为空或模块未初始化时返回 false
     */
    virtual bool OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) override;

    /**
     *  取 P2P 服务接口，见 IP2PModule::GetService。可在任意线程上调用。
     *  @return 服务对象（归模块所有）；未初始化时也返回它，此时其上的登记一律失败
     */
    virtual IP2PService* GetService() override;

    //以下为 IP2PServiceObserver 的实现

    /**
     *  业务方登记或注销了用途：交给引擎，已登录时重新发 2301。在调用 RegisterHandler / UnregisterHandler 的线程上调用；
     *  未初始化时无效果。
     */
    virtual void OnHandlersChanged() override;

private:
    /**
     *  引擎的宿主：发信令经 exe 的 IP2PHost，找业务方经 P2P 服务。
     *  宿主指针在 Init 时登记、在引擎停止之后才清空，引擎运行期间一直有效。
     */
    class EngineHost : public IP2PEngineHost
    {
    public:
        EngineHost();

        //登记 exe 的宿主接口（不持有所有权）与 P2P 服务（借用）；传 NULL 解除
        void Attach(IP2PHost* host, CP2PService* service);

        /**
         *  经 exe 的主连接发一个 2300 段命令（IP2PEngineHost 的实现）。exe 一侧线程安全。
         *  @param cmd  命令号
         *  @param body 包体（UTF-8 JSON）
         */
        virtual void SendCommand(uint16_t cmd, const std::string& body) override;

        /**
         *  按用途找业务方（IP2PEngineHost 的实现）。
         *  @param purpose 用途名
         *  @return 已登记的业务方；没有登记或未登记服务时返回 NULL
         */
        virtual IP2PLinkHandler* FindHandler(const std::string& purpose) override;

        /**
         *  取服务端身份公钥（IP2PEngineHost 的实现），转调 exe 的 IP2PHost::GetServerPublicKey。
         *  @return 公钥（归 exe 所有）；未登记宿主或服务端没有配置公钥时返回 NULL
         */
        virtual const unsigned char* GetServerPublicKey() override;

        /**
         *  列出已登记业务方的用途名（IP2PEngineHost 的实现），转调 P2P 服务。
         *  @param purposes [出参] 用途名；未登记服务时为空
         */
        virtual void ListPurposes(std::vector<std::string>& purposes) override;

    private:
        IP2PHost*    m_host;      //exe 的宿主接口（不持有所有权）
        CP2PService* m_service;   //P2P 服务（借用，模块的成员）
    };

    CP2PModule();

    //单例，禁止拷贝
    CP2PModule(const CP2PModule&);
    CP2PModule& operator=(const CP2PModule&);

private:
    std::mutex   m_mutex;         //保护 m_initialized 与 m_host：OnServerFrame 在网络接收线程上调用，其余在 UI 线程
    bool         m_initialized;   //是否已初始化：Init 成功后为 true，Shutdown 后为 false
    IP2PHost*    m_host;          //宿主接口（不持有所有权）；Init 时登记，Shutdown 时清空
    std::wstring m_appDir;        //exe 所在目录（不带结尾反斜杠），Init 时从参数拷贝，供以后定位配置等文件
    CP2PService  m_service;       //P2P 服务：业务方按用途登记的地方，模块初始化期间打开
    EngineHost   m_engineHost;    //引擎的宿主
    //P2P 引擎：Init 时创建并启动，Shutdown 时停止并销毁（受 m_mutex 保护）。用裸指针而不放进析构函数自动销毁，是因为
    //停止引擎要等 IO 线程结束，若在 DLL 卸载时的静态析构里做（持有加载器锁）会死锁；exe 保证卸载前先调用 Shutdown
    P2PEngine*   m_engine;
};

} // namespace p2p
