/**
 *  屏幕共享在分辨率变化后的两处修复（标签 [rdres]），ShareCaptureRetryTests.cpp
 *  背景：2026-10-12 前屏幕共享的显示器矩形只在开始时取一次，采集丢失后只重建一次（docs/远程桌面控制权限与观看工具条方案.md
 *  第 3.2 节）。与用户确认的用例：
 *    - RES6 屏幕共享采集器重建后，报告的源矩形是显示器当前的矩形（用一个故意写错的矩形建采集器，Start 之后应被纠正）；
 *    - RES7 采集重建失败后的重试策略：持续重试，间隔 500 毫秒起每次失败翻倍、最长 2 秒，重建成功或 Reset 后回到 500
 *      （2026-10-14 起，原为固定 500 毫秒）；失败期间向观看端报告「画面暂停」，成功后恢复。
 *  RES6 会枚举本机显示器并建立 DXGI 输出复制（隐藏桌面上复制可能失败，矩形在复制之前已读取）；RES7 是纯逻辑。框架 Catch2 v3。
 *  RES6 建立复制期间把本线程临时切成按显示器感知 DPI（2026-10-13）：FlamingoUnit.exe 不感知 DPI，以不感知 DPI 的身份做桌面复制时，
 *  Windows 的兼容性助手会在 HKCU\...\AppCompatFlags\Layers 下给它加上 HIGHDPIAWARE，此后每次运行它都按系统 DPI 感知启动，
 *  依赖 96 DPI 的 NMR、RRV2、HLL9 随之失败（二分全部用例查实，只跑 RES6 一条就会加上这个标志）。正式客户端启动时已声明按显示器感知，
 *  不受影响。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <vector>
#include "../../FlamingoMedia/Share/ShareCaptureRetry.h"
#include "../../FlamingoMedia/Share/ShareCapturer.h"
#include "../../FlamingoMedia/Share/ShareSources.h"

using namespace screenshare;

namespace {

//DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的哨兵值（与 FlamingoMedia/RemoteDesktop/RdControlledStreamer.cpp 取值一致）
const HANDLE kRes6Pmv2Context = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));

//SetThreadDpiAwarenessContext 的函数指针类型（Win10 1607+）
typedef HANDLE (WINAPI* Res6SetThreadCtxFn)(HANDLE);

//作用域内把本线程切成按显示器感知 DPI，离开时恢复；老系统没有这个 API 时什么也不做
class Res6ThreadDpiScope
{
public:
    Res6ThreadDpiScope()
        : m_set(NULL)
        , m_old(NULL)
    {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        if (user32 != NULL)
            m_set = reinterpret_cast<Res6SetThreadCtxFn>(::GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (m_set != NULL)
            m_old = m_set(kRes6Pmv2Context);
    }

    ~Res6ThreadDpiScope()
    {
        if (m_set != NULL && m_old != NULL)
            m_set(m_old);
    }

private:
    Res6SetThreadCtxFn m_set;   //SetThreadDpiAwarenessContext；老系统上为 NULL
    HANDLE             m_old;   //切换前的线程 DPI 上下文，析构时恢复
};

} // namespace

TEST_CASE("RES6 the monitor capturer re-reads the monitor rectangle when it starts", "[rdres]")
{
    //整个用例以按显示器感知 DPI 的身份运行（枚举显示器与建立桌面复制在同一个 DPI 上下文里），见文件头说明
    Res6ThreadDpiScope dpiScope;
    std::vector<ShareMonitor> monitors;
    REQUIRE(EnumerateShareMonitors(monitors));
    const ShareMonitor* target = NULL;
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        if (monitors[i].m_adapter >= 0 && monitors[i].m_output >= 0)
        {
            target = &monitors[i];
            break;
        }
    }
    if (target == NULL)
    {
        WARN("no DXGI output on this machine, RES6 skipped");
        return;
    }
    //故意给一个错的矩形，模拟开始共享之后分辨率变了
    const RECT stale = { 0, 0, 1, 1 };
    std::unique_ptr<IShareCapturer> cap(CreateMonitorCapturer(target->m_adapter, target->m_output, stale));
    REQUIRE(cap);
    cap->Start();
    const RECT now = cap->SourceRect();
    CHECK(now.left == target->m_rect.left);
    CHECK(now.top == target->m_rect.top);
    CHECK(now.right == target->m_rect.right);
    CHECK(now.bottom == target->m_rect.bottom);
    cap->Stop();
}

TEST_CASE("RES7 capture keeps retrying with growing intervals and reports paused until it recovers", "[rdres]")
{
    //默认的重试间隔：500 毫秒起，每次失败翻倍，最长 2 秒（docs/远程桌面控制权限与观看工具条方案.md 8.2 第 4 条）
    const int64_t first = 500;
    const int64_t second = 1000;
    const int64_t longest = 2000;
    ShareCaptureRetry r;
    CHECK_FALSE(r.Active());
    CHECK_FALSE(r.Due(0));
    //丢失：等第一个间隔（500）之后才第一次重试
    int64_t t = 1000;
    r.OnLost(t);
    CHECK(r.Active());
    CHECK_FALSE(r.Due(t + first - 1));
    CHECK(r.Due(t + first));
    //第一次失败：报告画面暂停，下一次等 1000
    t += first;
    CHECK(r.OnAttempt(false, t) == share_retry_report_paused);
    CHECK_FALSE(r.Due(t + second - 1));
    CHECK(r.Due(t + second));
    //第二次失败：不重复报告，下一次等 2000
    t += second;
    CHECK(r.OnAttempt(false, t) == share_retry_report_none);
    CHECK_FALSE(r.Due(t + longest - 1));
    CHECK(r.Due(t + longest));
    //第三次失败：间隔停在 2000，不再翻倍
    t += longest;
    CHECK(r.OnAttempt(false, t) == share_retry_report_none);
    CHECK_FALSE(r.Due(t + longest - 1));
    CHECK(r.Due(t + longest));
    CHECK(r.Attempts() == 3);
    //成功：报告恢复，不再重试
    t += longest;
    CHECK(r.OnAttempt(true, t) == share_retry_report_resumed);
    CHECK_FALSE(r.Active());
    CHECK_FALSE(r.Due(t + longest * 10));

    //重建成功之后再次丢失：间隔从 500 重新开始
    t = 20000;
    r.OnLost(t);
    CHECK_FALSE(r.Due(t + first - 1));
    CHECK(r.Due(t + first));
    t += first;
    CHECK(r.OnAttempt(false, t) == share_retry_report_paused);
    CHECK(r.Due(t + second));
    CHECK_FALSE(r.Due(t + second - 1));

    //换了共享内容（Reset）：不再重试；之后再次丢失同样从 500 开始，且第一次失败重新报告暂停
    r.Reset();
    CHECK_FALSE(r.Active());
    CHECK_FALSE(r.Due(t + longest * 10));
    t = 30000;
    r.OnLost(t);
    CHECK_FALSE(r.Due(t + first - 1));
    CHECK(r.Due(t + first));
    t += first;
    CHECK(r.OnAttempt(false, t) == share_retry_report_paused);
    CHECK_FALSE(r.Due(t + second - 1));

    //短暂的丢失第一次重试就恢复：不报告暂停，也不报告恢复
    r.Reset();
    r.OnLost(40000);
    CHECK(r.OnAttempt(true, 40000 + first) == share_retry_report_none);
}
