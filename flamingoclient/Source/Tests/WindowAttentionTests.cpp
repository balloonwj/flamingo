/**
 *  WindowAttentionTests.cpp
 *
 *  RaiseWindowForAttention 的单元测试（标签 [shakeraise]）。
 *
 *  背景：收到窗口抖动时聊天窗要调到用户眼前，但用户正在操作别的程序时，Windows 会
 *  拒绝后台进程的 SetForegroundWindow，窗口留在原处（2026-09-30 用户反馈"某些情况下
 *  对方的聊天窗口不会前置"）。被拒绝时改为只调整层级并闪烁任务栏按钮。
 *
 *  "被拒绝"在测试进程里无法稳定复现（是否放行取决于当时哪个进程是前台、谁收到了最近
 *  一次输入），所以系统调用经 IWindowRaiseOps 接口替换为假实现：它按用例的设定返回
 *  结果，并把每次调用按顺序记成一条字符串，用例据此核对调用顺序。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/WindowAttention.h"

#include <string>
#include <vector>
#include <sstream>

using namespace balloonchatwnd;

namespace {

// 被调到前台的聊天窗（假句柄，只用来比较，不会传给真正的系统函数）。
const HWND kChatHwnd = (HWND)(ULONG_PTR)0x1234;

// 用户此刻正在使用的另一个程序的窗口（假句柄）。
const HWND kOtherHwnd = (HWND)(ULONG_PTR)0x5678;

/**
 *  FakeRaiseOps —— 记录调用顺序的 IWindowRaiseOps 假实现。
 *
 *  GetForeground 在 SetForeground 调用之前返回 m_foregroundBefore，之后返回
 *  m_foregroundAfter，模拟"设前台成功 / 被拒绝"两种结果。
 */
class FakeRaiseOps : public IWindowRaiseOps
{
public:
    FakeRaiseOps()
        : m_minimized(false)
        , m_setForegroundResult(true)
        , m_foregroundBefore(kOtherHwnd)
        , m_foregroundAfter(kChatHwnd)
        , m_setForegroundCalled(false)
    {
    }

    bool IsMinimized(HWND) override
    {
        m_calls.push_back("IsMinimized");
        return m_minimized;
    }

    void Show(HWND, int nCmdShow) override
    {
        std::ostringstream os;
        os << "Show:" << (nCmdShow == SW_RESTORE ? "RESTORE" : (nCmdShow == SW_SHOW ? "SHOW" : "OTHER"));
        m_calls.push_back(os.str());
    }

    void BringToTop(HWND) override
    {
        m_calls.push_back("BringToTop");
    }

    bool SetForeground(HWND) override
    {
        m_calls.push_back("SetForeground");
        m_setForegroundCalled = true;
        return m_setForegroundResult;
    }

    HWND GetForeground() override
    {
        m_calls.push_back("GetForeground");
        return m_setForegroundCalled ? m_foregroundAfter : m_foregroundBefore;
    }

    void SetZOrderNoActivate(HWND, HWND insertAfter) override
    {
        if (insertAfter == HWND_TOPMOST)
            m_calls.push_back("ZOrder:TOPMOST");
        else if (insertAfter == HWND_NOTOPMOST)
            m_calls.push_back("ZOrder:NOTOPMOST");
        else
            m_calls.push_back("ZOrder:OTHER");
    }

    void FlashUntilForeground(HWND) override
    {
        m_calls.push_back("Flash");
    }

    // 该调用在记录里第一次出现的位置；没出现过返回 -1。
    int IndexOf(const std::string& call) const
    {
        for (size_t i = 0; i < m_calls.size(); ++i)
        {
            if (m_calls[i] == call)
                return (int)i;
        }
        return -1;
    }

    // 该调用在记录里出现的次数。
    int CountOf(const std::string& call) const
    {
        int n = 0;
        for (size_t i = 0; i < m_calls.size(); ++i)
        {
            if (m_calls[i] == call)
                ++n;
        }
        return n;
    }

    // 全部调用拼成一行，失败时打印出来便于对照。
    std::string Joined() const
    {
        std::string s;
        for (size_t i = 0; i < m_calls.size(); ++i)
        {
            if (i > 0)
                s += ", ";
            s += m_calls[i];
        }
        return s;
    }

    bool m_minimized;             // IsMinimized 的返回值
    bool m_setForegroundResult;   // SetForeground 的返回值
    HWND m_foregroundBefore;      // SetForeground 调用之前 GetForeground 的返回值
    HWND m_foregroundAfter;       // SetForeground 调用之后 GetForeground 的返回值
    bool m_setForegroundCalled;   // SetForeground 是否已被调用过
    std::vector<std::string> m_calls;   // 按先后顺序记录的调用
};

} // anonymous namespace

TEST_CASE("WA1 抢到了前台时不做置顶切换、也不闪任务栏", "[shakeraise]")
{
    FakeRaiseOps ops;
    ops.m_setForegroundResult = true;
    ops.m_foregroundAfter = kChatHwnd;

    const WindowRaiseResult r = RaiseWindowForAttention(kChatHwnd, ops);
    INFO("calls: " << ops.Joined());

    CHECK(r.m_setForegroundReturned);
    CHECK(r.m_isForeground);
    CHECK_FALSE(r.m_raisedWithoutFocus);
    CHECK_FALSE(r.m_flashed);
    CHECK(ops.CountOf("ZOrder:TOPMOST") == 0);
    CHECK(ops.CountOf("ZOrder:NOTOPMOST") == 0);
    CHECK(ops.CountOf("Flash") == 0);
}

TEST_CASE("WA2 抢不到前台时先置顶再取消置顶，然后闪任务栏", "[shakeraise]")
{
    FakeRaiseOps ops;
    ops.m_setForegroundResult = false;
    ops.m_foregroundAfter = kOtherHwnd;   // 用户仍在用别的程序

    const WindowRaiseResult r = RaiseWindowForAttention(kChatHwnd, ops);
    INFO("calls: " << ops.Joined());

    CHECK_FALSE(r.m_setForegroundReturned);
    CHECK_FALSE(r.m_isForeground);
    CHECK(r.m_raisedWithoutFocus);
    CHECK(r.m_flashed);

    // 各调用一次，顺序为：设前台（被拒绝）→ 置顶 → 取消置顶 → 闪任务栏
    CHECK(ops.CountOf("ZOrder:TOPMOST") == 1);
    CHECK(ops.CountOf("ZOrder:NOTOPMOST") == 1);
    CHECK(ops.CountOf("Flash") == 1);
    const int iSfw     = ops.IndexOf("SetForeground");
    const int iTop     = ops.IndexOf("ZOrder:TOPMOST");
    const int iNoTop   = ops.IndexOf("ZOrder:NOTOPMOST");
    const int iFlash   = ops.IndexOf("Flash");
    CHECK(iSfw >= 0);
    CHECK(iSfw < iTop);
    CHECK(iTop < iNoTop);
    CHECK(iNoTop < iFlash);
}

TEST_CASE("WA3 设前台报告成功但前台仍是别的窗口时，按抢不到处理", "[shakeraise]")
{
    // 微软文档：即使满足放行条件，进程也可能被拒绝设前台。以调用之后的实际前台窗口为准。
    FakeRaiseOps ops;
    ops.m_setForegroundResult = true;
    ops.m_foregroundAfter = kOtherHwnd;

    const WindowRaiseResult r = RaiseWindowForAttention(kChatHwnd, ops);
    INFO("calls: " << ops.Joined());

    CHECK(r.m_setForegroundReturned);
    CHECK_FALSE(r.m_isForeground);
    CHECK(r.m_raisedWithoutFocus);
    CHECK(r.m_flashed);
    CHECK(ops.CountOf("ZOrder:TOPMOST") == 1);
    CHECK(ops.CountOf("ZOrder:NOTOPMOST") == 1);
    CHECK(ops.CountOf("Flash") == 1);
}

TEST_CASE("WA4 最小化的窗口先还原，之后才做前台与层级操作", "[shakeraise]")
{
    SECTION("最小化：第一步是还原")
    {
        FakeRaiseOps ops;
        ops.m_minimized = true;
        ops.m_setForegroundResult = false;
        ops.m_foregroundAfter = kOtherHwnd;

        const WindowRaiseResult r = RaiseWindowForAttention(kChatHwnd, ops);
        INFO("calls: " << ops.Joined());

        CHECK(r.m_wasMinimized);
        const int iRestore = ops.IndexOf("Show:RESTORE");
        REQUIRE(iRestore >= 0);
        CHECK(ops.CountOf("Show:SHOW") == 0);
        CHECK(iRestore < ops.IndexOf("BringToTop"));
        CHECK(iRestore < ops.IndexOf("SetForeground"));
        CHECK(iRestore < ops.IndexOf("ZOrder:TOPMOST"));
    }
    SECTION("未最小化：照现有做法显示窗口")
    {
        FakeRaiseOps ops;
        ops.m_minimized = false;

        const WindowRaiseResult r = RaiseWindowForAttention(kChatHwnd, ops);
        INFO("calls: " << ops.Joined());

        CHECK_FALSE(r.m_wasMinimized);
        CHECK(ops.CountOf("Show:SHOW") == 1);
        CHECK(ops.CountOf("Show:RESTORE") == 0);
        CHECK(ops.IndexOf("Show:SHOW") < ops.IndexOf("SetForeground"));
    }
}

TEST_CASE("WA5 窗口句柄为空时不调用任何系统函数", "[shakeraise]")
{
    FakeRaiseOps ops;
    const WindowRaiseResult r = RaiseWindowForAttention(NULL, ops);
    INFO("calls: " << ops.Joined());

    CHECK(ops.m_calls.empty());
    CHECK(r.m_foregroundBefore == NULL);
    CHECK_FALSE(r.m_isForeground);
    CHECK_FALSE(r.m_raisedWithoutFocus);
    CHECK_FALSE(r.m_flashed);
}

TEST_CASE("WA6 结果里记下调用前的前台窗口", "[shakeraise]")
{
    // 诊断日志据此写出当时前台窗口属于哪个进程，判断设前台被拒绝是不是因为前台在别的程序手里。
    FakeRaiseOps ops;
    ops.m_foregroundBefore = kOtherHwnd;
    ops.m_setForegroundResult = false;
    ops.m_foregroundAfter = kOtherHwnd;

    const WindowRaiseResult r = RaiseWindowForAttention(kChatHwnd, ops);
    INFO("calls: " << ops.Joined());

    CHECK(r.m_foregroundBefore == kOtherHwnd);
    // 前台窗口要在任何可能改变前台的调用之前读取
    const int iFirstGet = ops.IndexOf("GetForeground");
    REQUIRE(iFirstGet >= 0);
    CHECK(iFirstGet < ops.IndexOf("SetForeground"));
    const int iShow = (ops.IndexOf("Show:SHOW") >= 0) ? ops.IndexOf("Show:SHOW") : ops.IndexOf("Show:RESTORE");
    CHECK(iFirstGet < iShow);
}
