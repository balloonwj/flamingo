/**
 *  通话中共享屏幕的单元测试（Suite SSIC，标签 [incallshare]），ShareCallPolicyTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareCallPolicy.h（docs/屏幕共享与录制方案.md 第 8 期）：
 *    - SSIC1 只有「单聊、共享的另一方是已接通通话的对端」才算通话中共享；
 *    - SSIC2 通话窗「共享屏幕」按钮的动作：发起、结束、正在别处共享、正在观看；
 *    - SSIC3 收到开始共享推送时，只有发起人是通话对端且本端空闲才直接加入；
 *    - SSIC4 按钮文字随是否正在共享变化；
 *    - SSIC5 观看中的共享结束：通话中正常结束不弹提示框，网络中断仍提示，通话外一律提示。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../../FlamingoMedia/Share/ShareCallPolicy.h"

using namespace screenshare;

namespace {

//会话类型：单聊、群聊（与 conversation_type 取值一致）
const int kSsicSingle = 1;
const int kSsicGroup = 2;

//通话对端、另一个联系人、一个群
const int64_t kSsicPeer = 5308;
const int64_t kSsicOther = 5010;
const int64_t kSsicGroupId = 268435463;

} // namespace

TEST_CASE("SSIC1 只有与通话对端的单聊共享才算通话中共享", "[incallshare]")
{
    CHECK(IsInCallShare(kSsicPeer, kSsicSingle, kSsicPeer));
    //没有通话
    CHECK_FALSE(IsInCallShare(0, kSsicSingle, kSsicPeer));
    //单聊对象不是通话对端
    CHECK_FALSE(IsInCallShare(kSsicPeer, kSsicSingle, kSsicOther));
    //群聊共享即使群号恰好等于对端 id 也不算
    CHECK_FALSE(IsInCallShare(kSsicPeer, kSsicGroup, kSsicPeer));
    CHECK_FALSE(IsInCallShare(kSsicPeer, kSsicGroup, kSsicGroupId));
}

TEST_CASE("SSIC2 共享屏幕按钮：发起、结束、正在别处共享、正在观看", "[incallshare]")
{
    CHECK(DecideInCallShareClick(false, false, false) == in_call_share_start);
    CHECK(DecideInCallShareClick(true, false, false) == in_call_share_stop);
    //正在与对端共享时优先结束，hostingOther 不起作用
    CHECK(DecideInCallShareClick(true, true, false) == in_call_share_stop);
    CHECK(DecideInCallShareClick(false, true, false) == in_call_share_busy_hosting);
    CHECK(DecideInCallShareClick(false, false, true) == in_call_share_busy_viewing);
}

TEST_CASE("SSIC3 开始共享推送：发起人是通话对端且本端空闲才直接加入", "[incallshare]")
{
    CHECK(ShouldAutoJoinInCall(kSsicSingle, kSsicPeer, kSsicPeer, true));
    //本端正在共享或观看：照原有规则处理
    CHECK_FALSE(ShouldAutoJoinInCall(kSsicSingle, kSsicPeer, kSsicPeer, false));
    //没有通话、发起人不是通话对端、群聊：都弹提示而不是直接加入
    CHECK_FALSE(ShouldAutoJoinInCall(kSsicSingle, kSsicPeer, 0, true));
    CHECK_FALSE(ShouldAutoJoinInCall(kSsicSingle, kSsicOther, kSsicPeer, true));
    CHECK_FALSE(ShouldAutoJoinInCall(kSsicGroup, kSsicPeer, kSsicPeer, true));
}

TEST_CASE("SSIC4 按钮文字随是否正在共享变化", "[incallshare]")
{
    CHECK(std::wstring(InCallShareButtonText(false)) == L"共享屏幕");
    CHECK(std::wstring(InCallShareButtonText(true)) == L"停止共享");
}

TEST_CASE("SSIC5 共享结束时是否弹提示框", "[incallshare]")
{
    CHECK_FALSE(ShouldPromptShareEnded(true, false));
    CHECK(ShouldPromptShareEnded(true, true));
    CHECK(ShouldPromptShareEnded(false, false));
    CHECK(ShouldPromptShareEnded(false, true));
}
