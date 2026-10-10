/**
 *  FlamingoScreenshot.dll 的模块对象：实现 IScreenshotModule，登记日志与翻译的宿主出口，发起截图。
 *  进程内唯一，由工厂函数 FlamingoScreenshot_Create（经 CreateScreenshotModule）返回。
 *
 *  本文件与 ScreenshotModule.cpp 同时编进单元测试（用例 SG2 直接验证工厂的版本判断与 Init 的参数校验），
 *  所以不依赖 DllMain.cpp 里的任何东西。
 *
 *  Run 校验参数后交给截图界面（Capture/ScreenCaptureOverlay，2026-10-07 方案第 5 节 S6 由 exe 迁入）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include "../ScreenshotApi.h"

namespace flamingoshot {

/**
 *  截图模块（单例）。Init、Shutdown、Run 在宿主的界面线程上调用；IsRunning、GetApiVersion 任意线程可调。
 */
class CScreenshotModule : public IScreenshotModule
{
public:
    /**
     *  取进程内唯一的模块对象。
     */
    static CScreenshotModule& Instance();

    bool Init(IScreenshotHost* host, const ScreenshotInitParams* params) override;
    void Shutdown() override;
    uint32_t GetApiVersion() const override;
    bool IsRunning() const override;
    bool Run(const ScreenshotRequest* request, IScreenshotSink* sink) override;

    /**
     *  是否已初始化（Init 成功、尚未 Shutdown）。供单元测试断言。
     */
    bool IsInitialized() const;

private:
    CScreenshotModule();
    ~CScreenshotModule();

    CScreenshotModule(const CScreenshotModule&) = delete;
    CScreenshotModule& operator=(const CScreenshotModule&) = delete;

private:
    IScreenshotHost* m_host;          // 宿主接口（借用）；Init 前与 Shutdown 后为 NULL
    bool             m_initialized;   // 是否已初始化
};

/**
 *  工厂的实现：版本号一致时返回进程内唯一的模块对象。导出函数 FlamingoScreenshot_Create 直接转调本函数。
 *  @param apiVersion 宿主编译时的 kApiVersion
 *  @return 版本一致时返回模块对象（归 DLL 所有，宿主不能释放）；不一致返回 NULL
 */
IScreenshotModule* CreateScreenshotModule(uint32_t apiVersion);

} // namespace flamingoshot
