/**
 *  主程序一侧的单聊密聊接入：实现交给 FlamingoSecretChat.dll 的宿主接口 ISecretChatHost，负责按企业开关加载、初始化、
 *  通知与卸载这个 DLL，并把 DLL 的事件转给主面板（docs/密聊方案.md 6.2）。
 *
 *  生命周期（只在 UI 线程上调用）：
 *    - 登录成功（含断线重连）：SetFeatureEnabled 之后 OnLoginSucceeded —— 开关打开时，本进程首次调用加载 DLL 并 Init，
 *      之后每次通知模块 OnLoggedIn（模块发 2601 报到）。DLL 不存在、版本不符或 Init 失败时只记一条日志，本进程不再重试，
 *      客户端照常运行，只是没有「密聊」按钮；
 *    - 运行中收到 1013：OnFeaturesChanged —— 开关由关变开时补加载与报到；由开变关时只记下（已在进行的密聊由服务端的
 *      2605 结束，客户端不自行结束，避免两边状态不一致）；
 *    - 退出登录：OnLoggingOut —— 通知模块结束全部会话并清零密钥，不卸载 DLL；
 *    - 进程退出：Shutdown —— 先解除帧路由，再关闭模块、卸载 DLL。
 *
 *  线程：Log 与 SendSecretCommand 可能在 DLL 的任意线程上被调用，OnServerFrame 在网络接收线程上被调用，均线程安全；
 *  其余回调由 DLL 在界面线程上发出，本类原样转给 ISecretChatSink（主面板）。
 *
 *  典型用法：
 *      balloonsecret::CSecretChatHost& host = balloonsecret::CSecretChatHost::GetInstance();
 *      host.SetSink(pMainPanel);
 *      host.SetFeatureEnabled(userMgr.m_bSecretChatFeature);
 *      host.OnLoginSucceeded(userId);
 *      if (host.IsEntryAvailable()) host.Invite(peerId);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <mutex>
#include <string>
#include "../../App/PluginModuleLoader.h"
#include "../../../FlamingoSecretChat/SecretChatApi.h"
#include "SecretChatGate.h"   // SecretSessionBook / PendingSecretInvite

namespace balloonsecret {

/**
 *  接收密聊事件的一方（主面板实现），全部在界面线程上调用。参数含义同 secretchat::ISecretChatHost 的同名回调；
 *  DLL 只给了会话号的几个回调，宿主按登记簿补上了对方 userid（peerId），会话号不认识时为 0。
 *  字节块与文字只在本次调用期间有效，需要保留时自行拷贝。
 */
class ISecretChatSink
{
public:
    virtual void OnSecretRegistered(bool enabled) = 0;
    virtual void OnSecretInviteSent(uint64_t sessionId, int64_t peerId, uint32_t remainMs, int code) = 0;
    virtual void OnSecretInviteArrived(uint64_t sessionId, int64_t fromId, uint32_t remainMs, int peerProtect) = 0;
    virtual void OnSecretSessionState(uint64_t sessionId, int64_t peerId, int state, int reason, int peerProtect) = 0;
    virtual void OnSecretText(uint64_t sessionId, int64_t peerId, uint32_t msgId, const wchar_t* text,
                              int64_t serverTimeMs) = 0;
    virtual void OnSecretImageBegin(uint64_t sessionId, int64_t peerId, uint32_t imageId, uint32_t totalBytes,
                                    uint32_t width, uint32_t height, int64_t serverTimeMs) = 0;
    virtual void OnSecretImageProgress(uint64_t sessionId, int64_t peerId, uint32_t imageId,
                                       uint32_t receivedBytes) = 0;
    virtual void OnSecretImage(uint64_t sessionId, int64_t peerId, uint32_t imageId, const void* bytes, uint32_t len,
                               bool ok) = 0;
    virtual void OnSecretSendResult(uint64_t sessionId, int64_t peerId, uint32_t msgId, int code) = 0;
    virtual void OnSecretImageSendProgress(uint64_t sessionId, int64_t peerId, uint32_t imageId, uint32_t sentBytes,
                                           uint32_t totalBytes, int code) = 0;

protected:
    ~ISecretChatSink()
    {
    }
};

/**
 *  CSecretChatHost —— 密聊模块的宿主（进程内唯一）。
 */
class CSecretChatHost : public secretchat::ISecretChatHost
{
public:
    /**
     *  取进程内唯一的宿主对象。
     */
    static CSecretChatHost& GetInstance();

    /**
     *  登记接收事件的主面板。登录成功处（OnLoginSucceeded 之前）调用；传 NULL 表示解除（进程退出流程里）。
     *  @param sink 主面板（借用，不拥有）
     */
    void SetSink(ISecretChatSink* sink);

    /**
     *  记下登录结果里的密聊开关（登录成功处、OnLoginSucceeded 之前调用）。不加载、不通知模块。
     *  @param enabled features.secretchat 是否为 1（老服务端没有这个字段时按关闭）
     */
    void SetFeatureEnabled(bool enabled);

    /**
     *  登录成功（含断线重连成功）时调用：开关打开时确保 DLL 已加载，并通知模块报到。
     *  @param myUserId 本人 userid
     */
    void OnLoginSucceeded(int64_t myUserId);

    /**
     *  运行中收到 1013 时调用。
     *  @param enabled features.secretchat 是否为 1
     *  @return 入口是否可用（IsEntryAvailable）因此改变
     */
    bool OnFeaturesChanged(bool enabled);

    /**
     *  即将退出登录时调用：模块结束全部会话并清零密钥；DLL 保持加载。
     */
    void OnLoggingOut();

    /**
     *  进程退出时调用：解除帧路由、关闭模块、卸载 DLL。未加载时与重复调用都安全。
     */
    void Shutdown();

    /**
     *  「密聊」按钮是否可用：DLL 就绪、企业开关打开、服务端确认开启（对方是否本人由调用方另判）。
     */
    bool IsEntryAvailable() const;

    /**
     *  取模块接口；DLL 未就绪时返回 NULL。只在界面线程上使用。
     *  发起、应答、撤回、退出请改用下面几个同名包装方法，它们会同步维护会话登记簿。
     */
    secretchat::ISecretChatModule* Module() const;

    /**
     *  发起密聊（转给模块的 Invite），结果经 ISecretChatSink::OnSecretInviteSent 回来。
     *  @param peerId 对方 userid
     *  @return 已发出返回 true；DLL 未就绪、未登录、已有与此人的密聊时返回 false
     */
    bool Invite(int64_t peerId);

    /**
     *  应答别人的邀请（转给模块的 Respond），并删掉这条待应答记录；忽略时连同会话登记一并删除。
     *  @param sessionId 会话号
     *  @param accept    true 同意，false 忽略
     *  @return 模块受理返回 true；会话已不在（过期、被撤回）返回 false
     */
    bool Respond(uint64_t sessionId, bool accept);

    /**
     *  发起方撤回待应答的邀请（转给模块的 Cancel），并删除会话登记。
     *  @return 模块受理返回 true
     */
    bool Cancel(uint64_t sessionId);

    /**
     *  本人退出密聊（转给模块的 Close），并删除会话登记。会话已在服务端结束（只能查看）时模块返回 false，登记照样删除。
     *  @return 模块受理返回 true
     */
    bool Close(uint64_t sessionId);

    /**
     *  本机能做到的防截图级别（secretchat::CaptureProtectLevel）；DLL 未就绪时返回 kProtectNone。
     */
    int ProbeProtect();

    /**
     *  对一个窗口打开或关闭防截图（转给模块的 ProtectWindow，模块同时开始或停止定时核对）。
     *  进程退出时 DLL 先于主面板卸载，此时调用安全返回。
     *  @param hwnd 顶层窗口
     *  @param on   true 打开，false 关闭
     *  @return 打开时为实际达到的级别；关闭或 DLL 未就绪时返回 kProtectNone
     */
    int ProtectWindow(HWND hwnd, bool on);

    /**
     *  取一个进行中会话的安全码（转给模块的 GetSafetyCode）。
     *  @param sessionId 会话号
     *  @param out       [出参] 安全码，如 "12345 67890 13579 24680"
     *  @return 会话进行中且取到返回 true
     */
    bool GetSafetyCode(uint64_t sessionId, std::wstring& out);

    /**
     *  取别人发给本人、还在倒计时内的邀请（单聊窗打开时据此补出邀请卡片）。
     *  @param fromId 发起方 userid
     *  @param out    [出参] 邀请
     *  @return 有且未到期返回 true
     */
    bool GetIncomingInvite(int64_t fromId, PendingSecretInvite& out);

    /**
     *  会话号属于哪个对方；不认识的会话号返回 0。
     */
    int64_t PeerOfSession(uint64_t sessionId) const;

    /**
     *  收到一帧 2600 段的服务端帧（CIUSocket::HandleV2Frame 在网络接收线程上调用），原样转给模块；
     *  模块未加载时记一条日志后丢弃。线程安全。
     */
    void OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    //======== secretchat::ISecretChatHost ========
    virtual void Log(int level, const char* text) override;
    virtual uint32_t SendSecretCommand(uint16_t cmd, const char* json, uint32_t len) override;
    virtual int64_t GetMyUserId() override;
    virtual void OnRegistered(bool enabled) override;
    virtual void OnInviteSent(uint64_t sessionId, int64_t peerId, uint32_t remainMs, int code) override;
    virtual void OnInviteArrived(uint64_t sessionId, int64_t fromId, uint32_t remainMs, int peerProtect) override;
    virtual void OnSessionState(uint64_t sessionId, int64_t peerId, int state, int reason, int peerProtect) override;
    virtual void OnSecretText(uint64_t sessionId, int64_t peerId, uint32_t msgId, const wchar_t* text,
                              int64_t serverTimeMs) override;
    virtual void OnSecretImageBegin(uint64_t sessionId, int64_t peerId, uint32_t imageId, uint32_t totalBytes,
                                    uint32_t width, uint32_t height, int64_t serverTimeMs) override;
    virtual void OnSecretImageProgress(uint64_t sessionId, uint32_t imageId, uint32_t receivedBytes) override;
    virtual void OnSecretImage(uint64_t sessionId, uint32_t imageId, const void* bytes, uint32_t len, bool ok) override;
    virtual void OnSendResult(uint64_t sessionId, uint32_t msgId, int code) override;
    virtual void OnImageSendProgress(uint64_t sessionId, uint32_t imageId, uint32_t sentBytes, uint32_t totalBytes,
                                     int code) override;

private:
    CSecretChatHost();
    ~CSecretChatHost();
    CSecretChatHost(const CSecretChatHost&);
    CSecretChatHost& operator=(const CSecretChatHost&);

    //首次需要时加载并初始化 DLL；本进程只尝试一次。返回是否就绪
    bool EnsureModuleReady();

private:
    balloonapp::CPluginModuleLoader<secretchat::ISecretChatModule> m_loader;   //DLL 加载器
    bool                         m_loadAttempted;   //本进程是否已经尝试过加载
    bool                         m_ready;           //DLL 已加载且 Init 成功
    bool                         m_featureEnabled;  //本人所属企业的密聊开关
    bool                         m_serverEnabled;   //服务端在 2601 应答里确认开启
    bool                         m_loggedIn;        //是否已登录
    int64_t                      m_myUserId;        //本人 userid；未登录时为 0
    ISecretChatSink*             m_sink;            //主面板（借用）
    SecretSessionBook            m_book;            //本机参与的会话登记簿：会话号 → 对方、别人发来的待应答邀请（界面线程）
    mutable std::mutex           m_routeMutex;      //保护 m_routeTarget：网络线程读，界面线程写
    secretchat::ISecretChatModule* m_routeTarget;   //网络线程转发帧的目标；未就绪或已卸载时为 NULL
};

} // namespace balloonsecret
