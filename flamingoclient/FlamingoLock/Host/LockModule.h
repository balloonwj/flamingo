/**
 *  FlamingoLock.dll 的模块对象：实现 ILockModule，登记日志与翻译的宿主出口，把锁屏请求转给锁屏线程。
 *  进程内唯一，由工厂函数 FlamingoLock_Create 返回。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../FlamingoLockApi.h"
#include "../Session/LockThread.h"

namespace flamingolock {

/**
 *  锁屏模块（单例）。Init、Shutdown、RequestLock 在宿主的界面线程上调用；IsLocked 任意线程可调。
 */
class CLockModule : public ILockModule
{
public:
    /**
     *  取进程内唯一的模块对象。
     */
    static CLockModule& Instance();

    bool Init(ILockHost* host, const LockInitParams* params) override;
    void Shutdown() override;
    uint32_t GetApiVersion() const override;
    bool RequestLock() override;
    bool IsLocked() const override;

private:
    CLockModule();
    ~CLockModule();

    CLockModule(const CLockModule&) = delete;
    CLockModule& operator=(const CLockModule&) = delete;

private:
    ILockHost*  m_host;          // 宿主接口（借用）；Init 前与 Shutdown 后为 NULL
    bool        m_initialized;   // 是否已初始化
    CLockThread m_thread;        // 锁屏线程
};

} // namespace flamingolock
