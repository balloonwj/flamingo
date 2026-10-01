/**
 *  「清理聊天记录」对话框流程状态的单元测试（Suite CF，标签 [chatclean]），ChatCleanFlowTests.cpp
 *
 *  覆盖（docs/清理聊天记录方案.md §11.3，用例已与用户确认）：
 *    CF1  未连接服务器时「清理」置灰，连上以后恢复可用。
 *    CF2  点「清理」：区间合法时进入"清理中"（按钮"清理中…"并置灰，再点无效）；区间不合法时不发请求并给出原因。
 *    CF3  清理中收到应答：成功进入"完成"；失败恢复可用，原因按错误码区分。
 *    CF4  清理中 15 秒没有应答：恢复可用并提示；之后应答才到，照常处理。
 *    CF5  清理中连接断开：按钮置灰，提示连接已断开。
 *    CF6  没有在途请求时收到应答：忽略。
 *    CF7  程序填入期间的下拉框变化不切换档位；用户手动修改任意一个时切到「自定义」。
 *
 *  框架 Catch2 v3。纯逻辑，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonSettings/ChatCleanFlow.h"
#include "../UI/BalloonSettings/ChatCleanRange.h"
#include "../net/Protocol.h"

#include <string>

using namespace balloonsettings;

namespace {

// 构造一个已连接、停在默认档位、等待操作的流程。
ChatCleanFlow ConnectedFlow()
{
    ChatCleanFlow flow;
    flow.Reset(kCleanBefore30Days);
    flow.SetConnected(true);
    return flow;
}

// 构造一个已连接、已发出请求、正在等应答的流程。
ChatCleanFlow RunningFlow()
{
    ChatCleanFlow flow = ConnectedFlow();
    REQUIRE(flow.CanStartClean(kCleanRangeOk));
    flow.OnRequestSent();
    REQUIRE(flow.Stage() == kCleanStageRunning);
    return flow;
}

} // anonymous namespace

TEST_CASE("CF1 未连接服务器时清理按钮置灰，连上后恢复可用", "[chatclean]")
{
    ChatCleanFlow flow;
    flow.Reset(kCleanBefore30Days);
    flow.SetConnected(false);

    CHECK_FALSE(flow.IsCleanEnabled());
    CHECK_FALSE(flow.Hint().empty());
    CHECK_FALSE(flow.CanStartClean(kCleanRangeOk));
    CHECK(flow.Stage() == kCleanStageIdle);

    flow.SetConnected(true);
    CHECK(flow.IsCleanEnabled());
    CHECK(flow.Hint().empty());
    CHECK(std::wstring(flow.CleanButtonText()) == L"清理");
}

TEST_CASE("CF2 点清理：合法时进入清理中，不合法时不发请求并给出原因", "[chatclean]")
{
    // 区间不合法：不进入清理中，提示原因，按钮仍可用（用户改好时间后可以再点）
    {
        ChatCleanFlow flow = ConnectedFlow();
        CHECK_FALSE(flow.CanStartClean(kCleanRangeEndInFuture));
        CHECK(flow.Stage() == kCleanStageIdle);
        CHECK(flow.Hint() == ChatCleanRangeErrorText(kCleanRangeEndInFuture));
        CHECK(flow.IsCleanEnabled());

        CHECK_FALSE(flow.CanStartClean(kCleanRangeStartAfterEnd));
        CHECK(flow.Hint() == ChatCleanRangeErrorText(kCleanRangeStartAfterEnd));
    }

    // 区间合法：确认后发出请求，进入清理中
    {
        ChatCleanFlow flow = ConnectedFlow();
        REQUIRE(flow.CanStartClean(kCleanRangeOk));
        flow.OnRequestSent();
        CHECK(flow.Stage() == kCleanStageRunning);
        CHECK_FALSE(flow.IsCleanEnabled());
        CHECK(std::wstring(flow.CleanButtonText()) == L"清理中…");
        CHECK(flow.Hint().empty());

        // 清理中再点无效
        CHECK_FALSE(flow.CanStartClean(kCleanRangeOk));
        CHECK(flow.Stage() == kCleanStageRunning);
    }
}

TEST_CASE("CF3 清理中收到应答：成功进入完成，失败恢复可用并按错误码说明原因", "[chatclean]")
{
    // 成功
    {
        ChatCleanFlow flow = RunningFlow();
        CHECK(flow.OnResult(0));
        CHECK(flow.Stage() == kCleanStageDone);
        CHECK_FALSE(flow.IsCleanEnabled());
        CHECK_FALSE(flow.CanStartClean(kCleanRangeOk));
    }

    // 参数不合法、服务端出错、其他错误：各有各的原因，按钮恢复可用
    const int codes[] = { error_code_bad_request, error_code_server_internal, error_code_not_login };
    std::wstring hints[3];
    for (int i = 0; i < 3; ++i)
    {
        INFO("code " << codes[i]);
        ChatCleanFlow flow = RunningFlow();
        CHECK(flow.OnResult(codes[i]));
        CHECK(flow.Stage() == kCleanStageIdle);
        CHECK(flow.IsCleanEnabled());
        CHECK(std::wstring(flow.CleanButtonText()) == L"清理");
        CHECK_FALSE(flow.Hint().empty());
        hints[i] = flow.Hint();
    }
    CHECK(hints[0] != hints[1]);
    CHECK(hints[1] != hints[2]);
    CHECK(hints[0] != hints[2]);

    // 其他错误的原因里带上错误码，便于反馈问题
    CHECK(hints[2].find(L"2") != std::wstring::npos);
}

TEST_CASE("CF4 清理中超时：恢复可用并提示，之后应答才到照常处理", "[chatclean]")
{
    // 超时后应答成功才到：照常进入完成
    {
        ChatCleanFlow flow = RunningFlow();
        flow.OnTimeout();
        CHECK(flow.Stage() == kCleanStageIdle);
        CHECK(flow.IsCleanEnabled());
        CHECK(flow.Hint() == L"服务器没有响应，请稍后重试");

        CHECK(flow.OnResult(0));
        CHECK(flow.Stage() == kCleanStageDone);
    }

    // 超时后应答失败才到：照常给出失败原因
    {
        ChatCleanFlow flow = RunningFlow();
        flow.OnTimeout();
        CHECK(flow.OnResult(error_code_server_internal));
        CHECK(flow.Stage() == kCleanStageIdle);
        CHECK(flow.Hint() != L"服务器没有响应，请稍后重试");
    }

    // 不在清理中时的超时忽略
    {
        ChatCleanFlow flow = ConnectedFlow();
        flow.OnTimeout();
        CHECK(flow.Stage() == kCleanStageIdle);
        CHECK(flow.Hint().empty());
    }
}

TEST_CASE("CF5 清理中连接断开：按钮置灰并提示连接已断开", "[chatclean]")
{
    ChatCleanFlow flow = RunningFlow();
    flow.SetConnected(false);
    CHECK(flow.Stage() == kCleanStageIdle);
    CHECK_FALSE(flow.IsCleanEnabled());
    CHECK(flow.Hint() == L"与服务器的连接已断开，请重连后重试");

    // 这次请求的应答不会再到达；万一到了也不再处理
    CHECK_FALSE(flow.OnResult(0));
    CHECK(flow.Stage() == kCleanStageIdle);

    // 重连后恢复可用
    flow.SetConnected(true);
    CHECK(flow.IsCleanEnabled());
    CHECK(flow.Hint().empty());
}

TEST_CASE("CF6 没有在途请求时收到的应答被忽略", "[chatclean]")
{
    ChatCleanFlow flow = ConnectedFlow();
    CHECK_FALSE(flow.OnResult(0));
    CHECK(flow.Stage() == kCleanStageIdle);
    CHECK_FALSE(flow.OnResult(error_code_server_internal));
    CHECK(flow.Hint().empty());

    // 完成之后对话框重新打开（Reset），上一次的应答再到也忽略
    ChatCleanFlow done = RunningFlow();
    REQUIRE(done.OnResult(0));
    done.Reset(kCleanAll);
    CHECK(done.Stage() == kCleanStageIdle);
    CHECK_FALSE(done.OnResult(0));
    CHECK(done.Stage() == kCleanStageIdle);
}

TEST_CASE("CF7 程序填入不切换档位，用户手动修改切到自定义", "[chatclean]")
{
    ChatCleanFlow flow = ConnectedFlow();
    REQUIRE(flow.Preset() == kCleanBefore30Days);

    // 用户选了一个档位，随后程序把两组下拉框填好：填入期间的变化通知不切换档位
    CHECK(flow.OnPresetSelected(kCleanLast7Days));
    flow.BeginProgramFill();
    for (int i = 0; i < 10; ++i)
    {
        CHECK_FALSE(flow.OnTimeFieldChanged());
    }
    // 程序给「时间范围」下拉框赋值时发出的通知同样不算用户选择
    CHECK_FALSE(flow.OnPresetSelected(kCleanAll));
    flow.EndProgramFill();
    CHECK(flow.Preset() == kCleanLast7Days);

    // 嵌套填入：以最外层的结束为准
    flow.BeginProgramFill();
    flow.BeginProgramFill();
    flow.EndProgramFill();
    CHECK_FALSE(flow.OnTimeFieldChanged());
    flow.EndProgramFill();
    CHECK(flow.Preset() == kCleanLast7Days);

    // 用户手动改了任意一个时间：切到自定义
    CHECK(flow.OnTimeFieldChanged());
    CHECK(flow.Preset() == kCleanCustom);

    // 已经是自定义时再改，不再报告"切换"
    CHECK_FALSE(flow.OnTimeFieldChanged());
    CHECK(flow.Preset() == kCleanCustom);

    // 越界档位忽略
    CHECK_FALSE(flow.OnPresetSelected(kCleanPresetCount));
    CHECK(flow.Preset() == kCleanCustom);

    // 用户改时间后，之前的不合法提示清掉
    CHECK_FALSE(flow.CanStartClean(kCleanRangeStartAfterEnd));
    REQUIRE_FALSE(flow.Hint().empty());
    CHECK(flow.OnPresetSelected(kCleanAll));
    CHECK(flow.Hint().empty());
}
