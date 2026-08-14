/**
 *  LoginProgressStateTests.cpp
 *
 *  登录窗「登录中」状态机的单元测试。
 *
 *  这组用例真正要钉住的是<u>迟到结果的作废</u>：登录请求发在后台线程上，用户点了
 *  取消它也停不下来，结果终究会投回来。若不作废，表现就是「点了取消，界面几秒后
 *  自己跳出一个登录失败提示」，或者第二次登录进行到一半被第一次的结果打断。这类
 *  时序问题在真机上难以稳定复现，只能靠断言钉住。
 *
 *  balloonwj@qq.com   2026-08-13
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonAuth/LoginProgressState.h"

using namespace auth;

TEST_CASE("LPS1 初始为空闲态，允许发起登录", "[loginprogress]")
{
    LoginProgressState st;

    CHECK(st.phase() == login_phase_idle);
    CHECK(st.canStartLogin());
    // 空闲时没有正在进行的一轮
    CHECK(st.currentGeneration() == kInvalidLoginGeneration);
}

TEST_CASE("LPS2 发起后转入登录中，轮次号递增", "[loginprogress]")
{
    LoginProgressState st;

    const uint32_t gen1 = st.startLogin();
    CHECK(gen1 != kInvalidLoginGeneration);
    CHECK(st.phase() == login_phase_logining);
    CHECK(st.currentGeneration() == gen1);

    // 失败一轮后再发起，轮次号必须是新的——否则上一轮的迟到结果会被误认为本轮的
    REQUIRE(st.acceptFailure(gen1));
    const uint32_t gen2 = st.startLogin();
    CHECK(gen2 != kInvalidLoginGeneration);
    CHECK(gen2 != gen1);
}

TEST_CASE("LPS3 登录中再点登录不生效，不会起第二个线程", "[loginprogress]")
{
    LoginProgressState st;

    const uint32_t gen1 = st.startLogin();
    REQUIRE(gen1 != kInvalidLoginGeneration);

    CHECK_FALSE(st.canStartLogin());

    // 重复发起须返回无效号，调用方据此不起线程；且不能把当前这一轮的号冲掉
    const uint32_t gen2 = st.startLogin();
    CHECK(gen2 == kInvalidLoginGeneration);
    CHECK(st.currentGeneration() == gen1);
    CHECK(st.phase() == login_phase_logining);
}

TEST_CASE("LPS4 结果轮次号与当前一致时采纳", "[loginprogress]")
{
    LoginProgressState st;
    const uint32_t gen = st.startLogin();
    REQUIRE(gen != kInvalidLoginGeneration);

    CHECK(st.shouldAcceptResult(gen));

    SECTION("采纳成功结果后转入已登录")
    {
        CHECK(st.acceptSuccess(gen));
        CHECK(st.phase() == login_phase_done);
    }

    SECTION("采纳失败结果后回到空闲，可以重试")
    {
        CHECK(st.acceptFailure(gen));
        CHECK(st.phase() == login_phase_idle);
        CHECK(st.canStartLogin());
    }
}

TEST_CASE("LPS5 取消之后回来的旧结果被丢弃", "[loginprogress]")
{
    LoginProgressState st;
    const uint32_t gen = st.startLogin();
    REQUIRE(gen != kInvalidLoginGeneration);

    REQUIRE(st.cancelLogin());

    // 后台线程稍后才回来。它带的还是取消前那一轮的号，必须被丢弃，
    // 否则界面会在用户取消之后突然自己弹出登录失败。
    CHECK_FALSE(st.shouldAcceptResult(gen));
    CHECK_FALSE(st.acceptFailure(gen));
    CHECK_FALSE(st.acceptSuccess(gen));

    // 丢弃迟到结果不该改变当前状态
    CHECK(st.phase() == login_phase_idle);
}

TEST_CASE("LPS6 取消后回到空闲，允许重新发起", "[loginprogress]")
{
    LoginProgressState st;
    REQUIRE(st.startLogin() != kInvalidLoginGeneration);

    CHECK(st.cancelLogin());
    CHECK(st.phase() == login_phase_idle);
    CHECK(st.canStartLogin());
    CHECK(st.currentGeneration() == kInvalidLoginGeneration);

    // 不在登录中时取消是无事可做，返回 false 而不是把状态搅乱
    CHECK_FALSE(st.cancelLogin());
    CHECK(st.phase() == login_phase_idle);
}

TEST_CASE("LPS7 取消后立刻重登，第一轮的结果仍被丢弃", "[loginprogress]")
{
    LoginProgressState st;

    const uint32_t gen1 = st.startLogin();
    REQUIRE(gen1 != kInvalidLoginGeneration);
    REQUIRE(st.cancelLogin());

    const uint32_t gen2 = st.startLogin();
    REQUIRE(gen2 != kInvalidLoginGeneration);
    REQUIRE(gen2 != gen1);

    // 第一轮的结果这时才回来：它既不能被当成第二轮的结果采纳，
    // 也不能把正在进行的第二轮打断
    CHECK_FALSE(st.shouldAcceptResult(gen1));
    CHECK_FALSE(st.acceptFailure(gen1));
    CHECK(st.phase() == login_phase_logining);
    CHECK(st.currentGeneration() == gen2);

    // 第二轮自己的结果照常采纳
    CHECK(st.shouldAcceptResult(gen2));
    CHECK(st.acceptFailure(gen2));
    CHECK(st.phase() == login_phase_idle);
}

TEST_CASE("LPS8 各错误码映射到对应文案，未知码有兜底", "[loginprogress]")
{
    // 文案不能为空——空串会让登录窗上那行错误提示什么都不显示，
    // 用户只看到登录没成功却不知道为什么
    CHECK(LoginErrorText(LOGIN_FAILED) != NULL);
    CHECK(LoginErrorText(LOGIN_UNREGISTERED) != NULL);
    CHECK(LoginErrorText(LOGIN_PASSWORD_ERROR) != NULL);
    CHECK(LoginErrorText(LOGIN_SERVER_REFUSED) != NULL);
    CHECK(LoginErrorText(LOGIN_VERSION_TOO_OLD) != NULL);

    CHECK(wcslen(LoginErrorText(LOGIN_FAILED)) > 0);
    CHECK(wcslen(LoginErrorText(LOGIN_PASSWORD_ERROR)) > 0);

    // 几个错误码的文案必须互不相同，否则用户分不清是密码错了还是网络不通
    CHECK(wcscmp(LoginErrorText(LOGIN_FAILED), LoginErrorText(LOGIN_PASSWORD_ERROR)) != 0);
    CHECK(wcscmp(LoginErrorText(LOGIN_UNREGISTERED), LoginErrorText(LOGIN_PASSWORD_ERROR)) != 0);

    // 未知错误码要有兜底文案，不能返回空指针让调用方崩掉
    const long kNotAnErrorCode = 9999;
    CHECK(LoginErrorText(kNotAnErrorCode) != NULL);
    CHECK(wcslen(LoginErrorText(kNotAnErrorCode)) > 0);
}

TEST_CASE("LPS9 成功被采纳后，迟到的结果一律丢弃", "[loginprogress]")
{
    LoginProgressState st;
    const uint32_t gen = st.startLogin();
    REQUIRE(gen != kInvalidLoginGeneration);
    REQUIRE(st.acceptSuccess(gen));
    REQUIRE(st.phase() == login_phase_done);

    // 已登录成功之后窗口就要关了，此后任何结果都不该再改动状态
    CHECK_FALSE(st.shouldAcceptResult(gen));
    CHECK_FALSE(st.acceptFailure(gen));
    CHECK(st.phase() == login_phase_done);

    // 也不允许在这个阶段再发起登录
    CHECK_FALSE(st.canStartLogin());
    CHECK(st.startLogin() == kInvalidLoginGeneration);

    // 取消同样无效
    CHECK_FALSE(st.cancelLogin());
    CHECK(st.phase() == login_phase_done);
}

TEST_CASE("LPS10 复位后回到空闲，可以重新发起登录", "[loginprogress]")
{
    // 自动登录那条路会在窗口尚未创建时直接推进状态机，失败时又没人把它推回来。
    // 登录窗每次摆出来都会 reset，否则用户点第一下「登录」会被当成「取消登录」。
    LoginProgressState st;
    const uint32_t gen = st.startLogin();
    REQUIRE(gen != kInvalidLoginGeneration);
    REQUIRE(st.phase() == login_phase_logining);

    st.reset();
    CHECK(st.phase() == login_phase_idle);
    CHECK(st.currentGeneration() == kInvalidLoginGeneration);
    CHECK(st.canStartLogin());

    const uint32_t gen2 = st.startLogin();
    CHECK(gen2 != kInvalidLoginGeneration);
    // 复位不复位轮次号计数器 —— 新一轮必须拿到与旧轮不同的号
    CHECK(gen2 != gen);
}

TEST_CASE("LPS11 复位之前那一轮的迟到结果仍被丢弃", "[loginprogress]")
{
    // 复位若把轮次号计数器一并归零，旧线程带回来的号就会与新一轮撞上，
    // 迟到的失败结果会打断正在进行的新登录。这条用例钉住"号只增不减"。
    LoginProgressState st;
    const uint32_t genOld = st.startLogin();
    REQUIRE(genOld != kInvalidLoginGeneration);

    st.reset();
    const uint32_t genNew = st.startLogin();
    REQUIRE(genNew != kInvalidLoginGeneration);

    CHECK_FALSE(st.shouldAcceptResult(genOld));
    CHECK_FALSE(st.acceptFailure(genOld));
    CHECK(st.phase() == login_phase_logining);   // 新一轮不受影响

    CHECK(st.shouldAcceptResult(genNew));
}

TEST_CASE("LPS12 已登录成功的状态也能被复位", "[loginprogress]")
{
    // 登录成功后窗口关闭；同一个 CMainDlg 里的登录窗对象会被复用（例如用户登出
    // 后重新登录），那时状态机停在 done，不复位就再也发不起登录。
    LoginProgressState st;
    const uint32_t gen = st.startLogin();
    REQUIRE(st.acceptSuccess(gen));
    REQUIRE(st.phase() == login_phase_done);
    REQUIRE_FALSE(st.canStartLogin());

    st.reset();
    CHECK(st.phase() == login_phase_idle);
    CHECK(st.canStartLogin());
    CHECK(st.startLogin() != kInvalidLoginGeneration);
}
