/**
 *  主程序一侧的锁屏宿主：加载 FlamingoLock.dll、实现宿主接口（日志、翻译、读写上次选的锁屏画面、锁屏状态通知），
 *  并提供全进程的「是否锁屏中」查询。方案见 docs/锁屏方案.md 第 4.1、5.2 节。
 *
 *  锁屏状态变化在 DLL 的锁屏线程上回调 OnLockStateChanged：这里只记下状态，再给登记的通知窗口（主面板）投递
 *  kMsgLockStateChanged（wParam 为 1 表示开始锁屏、0 表示已解锁），其余处理都回到主界面线程上进行。
 *
 *  典型用法：
 *      CLockHost::GetInstance().Load(dllPath, appDir, iniPath);   // CAppShell::Startup
 *      CLockHost::GetInstance().SetNotifyWindow(panel.m_hWnd);    // 主面板建好后
 *      if (CLockHost::GetInstance().IsModuleReady()) 托盘菜单显示「锁屏」;
 *      CLockHost::GetInstance().RequestLock();                    // 托盘「锁屏」
 *      if (balloonlock::IsScreenLocked()) 不弹窗;                 // 任意线程
 *      CLockHost::GetInstance().Shutdown();                       // CAppShell::Shutdown
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <atomic>
#include <string>

#include "../../App/PluginModuleLoader.h"
#include "../../../FlamingoLock/FlamingoLockApi.h"

namespace balloonlock {

//投给主面板的私有消息：锁屏开始或结束。wParam：1 开始锁屏，0 已解锁；lParam 不用。
//偏移登记在开发约定文档的 WM_APP 占用表（0x100~0x103 为 FlamingoLock.dll 自用）
const UINT kMsgLockStateChanged = WM_APP + 0x104;

/**
 *  锁屏宿主（单例）。Load、SetNotifyWindow、RequestLock、Shutdown 在主界面线程上调用；
 *  IsLocked、IsModuleReady 任意线程可调；ILockHost 的方法由 DLL 在锁屏线程上调用。
 */
class CLockHost : public flamingolock::ILockHost
{
public:
    /**
     *  取进程内唯一的宿主对象。
     */
    static CLockHost& GetInstance();

    CLockHost();
    virtual ~CLockHost();

    CLockHost(const CLockHost&) = delete;
    CLockHost& operator=(const CLockHost&) = delete;

    /**
     *  加载并初始化锁屏模块。已加载时直接返回 true。
     *  @param dllPath FlamingoLock.dll 的完整路径
     *  @param appDir  程序所在目录（不带结尾反斜杠）
     *  @param iniPath config\flamingo.ini 的完整路径（读写 [lock] animation）
     *  @return 成功返回 true；DLL 不存在、版本不符或 Init 失败时返回 false（并记日志），托盘菜单随之不显示「锁屏」
     */
    bool Load(const wchar_t* dllPath, const wchar_t* appDir, const wchar_t* iniPath);

    /**
     *  登记接收 kMsgLockStateChanged 的窗口（主面板）。
     *  @param hwnd 窗口句柄；传 NULL 表示取消登记
     */
    void SetNotifyWindow(HWND hwnd);

    /**
     *  模块是否已加载并初始化。
     */
    bool IsModuleReady() const;

    /**
     *  请求锁屏（托盘菜单「锁屏」）。模块未就绪时返回 false。
     *  @return 已开始弹出设置窗口返回 true
     */
    bool RequestLock();

    /**
     *  当前是否处于锁屏中。任意线程可调。
     */
    bool IsLocked() const;

    /**
     *  关闭并卸载模块（锁屏中也会结束锁屏）。重复调用安全。
     */
    void Shutdown();

    // ---- flamingolock::ILockHost ----
    void Log(int level, const char* utf8) override;
    const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override;
    bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override;
    bool ReadAnimationChoice(char* buf, uint32_t chars) override;
    void WriteAnimationChoice(const char* token) override;
    void OnLockStateChanged(bool locked) override;

private:
    balloonapp::CPluginModuleLoader<flamingolock::ILockModule> m_loader;   // DLL 加载器
    bool                m_ready;       // 模块是否就绪
    std::atomic<bool>   m_locked;      // 是否锁屏中（DLL 回调时更新）
    std::atomic<HWND>   m_notify;      // 接收状态通知的窗口（主面板）
    std::wstring        m_iniPath;     // config\flamingo.ini 的完整路径
};

/**
 *  当前是否处于锁屏中（CLockHost::GetInstance().IsLocked() 的简写）。任意线程可调。
 */
bool IsScreenLocked();

} // namespace balloonlock
