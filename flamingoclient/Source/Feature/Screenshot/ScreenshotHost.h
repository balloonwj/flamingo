/**
 *  主程序一侧的截图宿主：加载 FlamingoScreenshot.dll、实现宿主接口（日志、翻译、界面语言、图片文字识别），
 *  并把截图请求转给模块。方案见 docs/截图独立模块方案.md 第 3.2、3.3 节。
 *
 *  程序启动时加载（未登录时也要能截图），退出时卸载。加载失败不重试，记一条日志；之后 IsModuleReady 返回 false，
 *  截图入口据此隐藏（方案第 5 节 S6 起接线）。
 *
 *  典型用法：
 *      CScreenshotHost::GetInstance().Load(dllPath, appDir);      // CAppShell::Startup
 *      if (CScreenshotHost::GetInstance().IsModuleReady()) 显示截图入口;
 *      CScreenshotHost::GetInstance().Run(request, &sink);        // 模态运行，返回时截图已结束
 *      CScreenshotHost::GetInstance().Shutdown();                 // CAppShell::Shutdown
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>
#include "../../App/PluginModuleLoader.h"
#include "../../../FlamingoScreenshot/ScreenshotApi.h"

namespace balloonscreenshot {

/**
 *  截图宿主（单例）。全部方法在主界面线程上调用；IScreenshotHost 的方法由 DLL 在调用 Run 的线程（同样是主界面
 *  线程）上回调。
 */
class CScreenshotHost : public flamingoshot::IScreenshotHost
{
public:
    /**
     *  取进程内唯一的宿主对象。
     */
    static CScreenshotHost& GetInstance();

    CScreenshotHost();
    virtual ~CScreenshotHost();

    CScreenshotHost(const CScreenshotHost&) = delete;
    CScreenshotHost& operator=(const CScreenshotHost&) = delete;

    /**
     *  加载并初始化截图模块。已加载时直接返回 true。
     *  @param dllPath FlamingoScreenshot.dll 的完整路径，不可为 NULL
     *  @param appDir  程序所在目录（不带结尾反斜杠），不可为 NULL
     *  @return 成功返回 true；DLL 不存在、版本不符或 Init 失败时返回 false（并记日志），截图入口随之隐藏
     */
    bool Load(const wchar_t* dllPath, const wchar_t* appDir);

    /**
     *  模块是否已加载并初始化。
     */
    bool IsModuleReady() const;

    /**
     *  本进程当前是否正有一次截图在进行。模块未就绪时返回 false。
     */
    bool IsRunning() const;

    /**
     *  模态发起一次截图，参数与返回值同 flamingoshot::IScreenshotModule::Run。
     *  @param request 本次截图的请求（m_cbSize 由调用方填好）
     *  @param sink    结果接收方（借用），可为 NULL（不绑定会话）
     *  @return 用户选了「发送」或「发送到对话框」返回 true；模块未就绪时直接返回 false（已记日志）
     */
    bool Run(const flamingoshot::ScreenshotRequest& request, flamingoshot::IScreenshotSink* sink);

    /**
     *  关闭并卸载模块。重复调用安全。
     */
    void Shutdown();

    // ---- flamingoshot::IScreenshotHost ----
    void Log(int level, const char* utf8) override;
    const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override;
    bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override;
    bool IsOcrAvailable() override;
    bool LaunchOcrForImage(HWND owner, const uint8_t* bgra, uint32_t width, uint32_t height) override;

private:
    balloonapp::CPluginModuleLoader<flamingoshot::IScreenshotModule> m_loader;   // DLL 加载器
    bool m_ready;   // 模块是否就绪：Load 成功后为 true，Shutdown 后为 false
};

} // namespace balloonscreenshot
