/**
 *  主程序一侧的语音字幕接入（docs/语音字幕方案.md 第 4 节）：实现交给 FlamingoCaption.dll 的宿主接口 ICaptionHost，
 *  负责按需加载、初始化与卸载这个 DLL，保存按账号的字幕设置，并在字幕可用状态变化时通知 FlamingoMedia.dll。
 *
 *  生命周期（只在 UI 线程上调用）：
 *    - 启动：CAppShell 加载 FlamingoMedia 后调 SetMediaModule、SetMediaHost，登记通知对象与界面能力的来源；
 *    - 登录成功：OnLoginSucceeded 记下企业开关与个人目录、读入字幕设置；不加载 DLL；
 *    - 第一次用到字幕（FlamingoMedia 经 IMediaHost::GetCaptionService 取模块）：AcquireModule 加载并初始化 DLL，
 *      失败时记一条日志，本进程不再重试（第 2 节 D6）；
 *    - 运行中收到 1013：OnFeaturesChanged；
 *    - 退出登录：OnLoggingOut；DLL 保持加载；
 *    - 进程退出：Shutdown，须在卸载 FlamingoMedia 之后（FlamingoMedia 持有本模块的识别流）。
 *  以上任一步使「字幕能否使用」变化时，经 IMediaModule::OnCaptionAvailabilityChanged 通知 FlamingoMedia。
 *
 *  ICaptionHost 的 Log 可能在 DLL 的任意线程上调用，为线程安全；其余方法只在 UI 线程上调用。
 *
 *  典型用法：
 *      ballooncaption::CCaptionHost::GetInstance().SetMediaModule(module);           // CAppShell::LoadMediaModule
 *      ballooncaption::CCaptionHost::GetInstance().OnLoginSucceeded(true, personalDir);
 *      caption::ICaptionModule* m = ballooncaption::CCaptionHost::GetInstance().AcquireModule();
 *      ballooncaption::CCaptionHost::GetInstance().Shutdown();                       // CAppShell::Shutdown
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

#include <string>

#include "../../App/PluginModuleLoader.h"
#include "../../../FlamingoCaption/CaptionApi.h"
#include "CaptionHostPolicy.h"

namespace flamingomedia {
class IMediaHost;
class IMediaModule;
}

namespace ballooncaption {

/**
 *  CCaptionHost —— 语音字幕模块的宿主（进程内唯一）。
 */
class CCaptionHost : public caption::ICaptionHost
{
public:
    /**
     *  取进程内唯一的宿主对象。
     *  @return 宿主对象（静态存储）
     */
    static CCaptionHost& GetInstance();

    /**
     *  登记要通知的 FlamingoMedia 模块（加载成功后登记，卸载之前传 NULL 解除）。登记时立即按当前状态通知一次。
     *  @param module 媒体模块（借用，不拥有），可为 NULL
     */
    void SetMediaModule(flamingomedia::IMediaModule* module);

    /**
     *  登记界面能力的来源：取主窗口、翻译界面文字、选择会话发送文件都转调媒体宿主适配器的同名实现，
     *  与 FlamingoMedia 用的是同一份。须在 Shutdown 之前一直有效。
     *  @param host 媒体宿主适配器（借用，不拥有），可为 NULL
     */
    void SetMediaHost(flamingomedia::IMediaHost* host);

    /**
     *  登录成功（含断线重连成功）时调用：记下企业开关与个人目录，读入该账号的字幕设置（已加载 DLL 时交给它）。
     *  @param featureEnabled 登录应答的 features.caption 是否为 1（老服务端没有这个字段时按关闭）
     *  @param personalDir    当前账号的个人目录（CUserMgr::GetPersonalFolder 的结果）
     */
    void OnLoginSucceeded(bool featureEnabled, const std::wstring& personalDir);

    /**
     *  运行中收到 1013（本企业的功能开关有变化）时调用。
     *  @param enabled features.caption 是否为 1
     *  @return 字幕能否使用是否因此改变
     */
    bool OnFeaturesChanged(bool enabled);

    /**
     *  即将回到登录界面（主动退出登录、被踢下线、切换账号）时调用：此后字幕不可用，直到再次登录成功。DLL 不卸载。
     */
    void OnLoggingOut();

    /**
     *  进程退出时调用：关闭字幕模块并卸载 DLL。未加载时调用、重复调用都是安全的。须在卸载 FlamingoMedia 之后、日志收尾之前调用。
     */
    void Shutdown();

    /**
     *  字幕当前能否使用（不触发加载），见 captionhost::IsCaptionAvailable。
     *  @return 可以使用返回 true
     */
    bool IsAvailable() const;

    /**
     *  取字幕模块，必要时加载并初始化 DLL。
     *  @return 字幕模块（归 DLL 所有，Shutdown 之前有效）；不能使用或加载失败时返回 NULL
     *  副作用：首次加载失败时记日志、把状态记为加载失败并通知 FlamingoMedia 字幕不可用
     */
    caption::ICaptionModule* AcquireModule();

    //以下为 ICaptionHost 的实现，语义见 FlamingoCaption/CaptionApi.h
    virtual void Log(int level, const char* text) override;
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override;
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override;
    virtual void SaveSettings(const char* settingsJson) override;
    virtual bool PickConversationAndSendFile(const wchar_t* path) override;
    virtual HWND GetMainWindow() override;

private:
    CCaptionHost();
    ~CCaptionHost();
    CCaptionHost(const CCaptionHost&) = delete;
    CCaptionHost& operator=(const CCaptionHost&) = delete;

    //按当前状态与上次通知的值比较，有变化时通知 FlamingoMedia；force 为 true 时不比较、一律通知
    void NotifyAvailability(bool force);

private:
    balloonapp::CPluginModuleLoader<caption::ICaptionModule> m_loader;   //FlamingoCaption.dll 的装卸；AcquireModule 时加载，Shutdown 时卸载
    captionhost::CaptionGateState  m_state;          //登录、企业开关与加载失败三项状态
    bool                           m_initialized;    //DLL 已加载且 Init 成功；Shutdown 时置 false
    bool                           m_notifiedValue;  //上次通知给 FlamingoMedia 的可用状态，用来避免重复通知
    std::wstring                   m_settingsPath;   //当前账号的字幕设置文件路径；未登录时为空
    std::string                    m_settingsJson;   //当前账号的字幕设置（UTF-8 JSON）；登录时读入，DLL 保存设置时更新
    flamingomedia::IMediaModule*   m_mediaModule;    //要通知的媒体模块（借用）；未加载 FlamingoMedia 时为 NULL
    flamingomedia::IMediaHost*     m_mediaHost;      //界面能力的来源（借用）；未登记时这几项能力返回空结果
};

} // namespace ballooncaption
