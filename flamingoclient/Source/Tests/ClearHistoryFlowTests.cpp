/**
 *  ClearHistoryFlowTests.cpp
 *
 *  「清空聊天记录」流程里的纯逻辑（UI/BalloonMain/ClearHistoryFlow.h）的单元测试，标签 [clearconv]：
 *
 *    CHF1  单聊确认框：带联系人名，说明只清空本人一方、其他设备同步、对方不受影响、无法恢复；
 *          名字为空时不出现空的书名号
 *    CHF2  群聊确认框：带群名，不受影响的是"其他群成员"而不是"对方"
 *    CHF3  按错误码给出的失败提示：成功无提示，不是群成员有专门说法，其它错误为通用提示
 *    CHF4  等待应答的登记：按 seq 对上请求，对上后删除；没有登记过的 seq 不认领；seq 为 0 不登记
 *    CHF5  登记里记下发起清空的窗口；失败提示弹在它上面，它已关闭或就是主面板时沿用原来的规则
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonMain/ClearHistoryFlow.h"
#include "../net/Protocol.h"   // error_code_xxx、conversation_single / conversation_group

using namespace balloonmain;

namespace {

// 判断 text 里是否含有子串 part
bool Contains(const std::wstring& text, const wchar_t* part)
{
    return text.find(part) != std::wstring::npos;
}

} // namespace

TEST_CASE("CHF1 单聊确认框：带联系人名并说明影响范围，名字为空时不出现空的书名号", "[clearconv]")
{
    const std::wstring tip = BuildClearHistoryConfirmText(conversation_single, L"韩晓燕");
    CHECK(Contains(tip, L"与「韩晓燕」的聊天记录"));
    CHECK(Contains(tip, L"只清空你这一方"));
    CHECK(Contains(tip, L"其他设备会同步清空"));
    CHECK(Contains(tip, L"对方的聊天记录不受影响"));
    CHECK(Contains(tip, L"无法恢复"));
    CHECK_FALSE(Contains(tip, L"群"));

    const std::wstring noName = BuildClearHistoryConfirmText(conversation_single, L"");
    CHECK(Contains(noName, L"与该联系人的聊天记录"));
    CHECK_FALSE(Contains(noName, L"「」"));
}

TEST_CASE("CHF2 群聊确认框：带群名，不受影响的是其他群成员", "[clearconv]")
{
    const std::wstring tip = BuildClearHistoryConfirmText(conversation_group, L"研发部");
    CHECK(Contains(tip, L"群聊「研发部」的聊天记录"));
    CHECK(Contains(tip, L"其他群成员的聊天记录不受影响"));
    CHECK(Contains(tip, L"无法恢复"));
    CHECK_FALSE(Contains(tip, L"对方"));

    const std::wstring noName = BuildClearHistoryConfirmText(conversation_group, L"");
    CHECK(Contains(noName, L"该群的聊天记录"));
    CHECK_FALSE(Contains(noName, L"「」"));
}

TEST_CASE("CHF3 失败提示：成功无提示，不是群成员有专门说法，其它错误为通用提示", "[clearconv]")
{
    CHECK(BuildClearHistoryErrorText(error_code_ok, conversation_group, L"研发部").empty());

    const std::wstring notMember = BuildClearHistoryErrorText(error_code_not_group_member,
                                                              conversation_group, L"研发部");
    CHECK(Contains(notMember, L"你已不在该群中"));
    CHECK(Contains(notMember, L"「研发部」"));

    const std::wstring internal = BuildClearHistoryErrorText(error_code_server_internal,
                                                             conversation_single, L"韩晓燕");
    CHECK(Contains(internal, L"与「韩晓燕」的聊天记录失败"));
    CHECK(Contains(internal, L"请稍后重试"));

    //报文格式错误与没有专门说法的码同样走通用提示，名字为空时不出现空的书名号
    const std::wstring badRequest = BuildClearHistoryErrorText(error_code_bad_request,
                                                               conversation_single, L"");
    CHECK(Contains(badRequest, L"请稍后重试"));
    CHECK_FALSE(Contains(badRequest, L"「」"));
}

TEST_CASE("CHF4 等待应答的登记：按 seq 对上请求，对上后删除，没登记过的不认领", "[clearconv]")
{
    //请求帧序号，取值无特殊含义，只要互不相同
    const uint32_t kSeqA = 101;
    const uint32_t kSeqB = 102;
    const uint32_t kSeqUnknown = 999;
    //会话ID，取值无特殊含义
    const int64_t kPeerId = 5308;
    const int64_t kGroupId = 268435456;

    ClearHistoryTracker tracker;
    PendingClearHistory single;
    single.m_ctype = conversation_single;
    single.m_cid = kPeerId;
    single.m_name = L"韩晓燕";
    PendingClearHistory group;
    group.m_ctype = conversation_group;
    group.m_cid = kGroupId;
    group.m_name = L"研发部";

    tracker.Begin(kSeqA, single);
    tracker.Begin(kSeqB, group);
    //seq 为 0 表示没有发出，不登记
    tracker.Begin(0, single);
    CHECK(tracker.PendingCount() == 2);

    //应答不必按发出顺序到达：先回 B（失败），再回 A（成功）
    const ClearHistoryOutcome rb = tracker.OnResult(kSeqB, error_code_not_group_member);
    CHECK(rb.m_matched);
    CHECK_FALSE(rb.m_succeeded);
    CHECK(rb.m_op.m_ctype == conversation_group);
    CHECK(rb.m_op.m_cid == kGroupId);
    CHECK(Contains(rb.m_errorText, L"你已不在该群中"));

    const ClearHistoryOutcome ra = tracker.OnResult(kSeqA, error_code_ok);
    CHECK(ra.m_matched);
    CHECK(ra.m_succeeded);
    CHECK(ra.m_op.m_cid == kPeerId);
    CHECK(ra.m_errorText.empty());
    CHECK(tracker.PendingCount() == 0);

    //同一 seq 再来一次、或从未登记过的 seq：不认领
    CHECK_FALSE(tracker.OnResult(kSeqA, error_code_ok).m_matched);
    const ClearHistoryOutcome unknown = tracker.OnResult(kSeqUnknown, error_code_server_internal);
    CHECK_FALSE(unknown.m_matched);
    CHECK(unknown.m_errorText.empty());
}

TEST_CASE("CHF5 登记里记下发起清空的窗口：失败提示弹在它上面，它已关闭或就是主面板时沿用原来的规则", "[clearconv]")
{
    //窗口句柄只当作不透明的值比较，取值无特殊含义
    const HWND kHistoryWnd = (HWND)(UINT_PTR)0x1001;
    const HWND kMainPanel  = (HWND)(UINT_PTR)0x1002;
    const HWND kChatWnd    = (HWND)(UINT_PTR)0x1003;
    const uint32_t kSeq = 201;
    const int64_t kGroupId = 268435456;

    //发起的窗口随登记带到应答
    ClearHistoryTracker tracker;
    PendingClearHistory pending;
    pending.m_ctype = conversation_group;
    pending.m_cid = kGroupId;
    pending.m_ownerHwnd = kHistoryWnd;
    tracker.Begin(kSeq, pending);
    const ClearHistoryOutcome r = tracker.OnResult(kSeq, error_code_server_internal);
    REQUIRE(r.m_matched);
    CHECK(r.m_op.m_ownerHwnd == kHistoryWnd);

    //消息记录窗发起、窗口还开着：弹在消息记录窗上
    CHECK(ChooseClearFailureOwner(kHistoryWnd, true, kMainPanel, kChatWnd) == kHistoryWnd);
    //消息记录窗已经关了：沿用原来的规则（该会话的聊天窗，没有时由调用方给主面板）
    CHECK(ChooseClearFailureOwner(kHistoryWnd, false, kMainPanel, kChatWnd) == kChatWnd);
    //主面板右键菜单发起（传的是主面板自己）：同样沿用原来的规则，提示仍弹在聊天窗上
    CHECK(ChooseClearFailureOwner(kMainPanel, true, kMainPanel, kChatWnd) == kChatWnd);
    //没有记下发起窗口
    CHECK(ChooseClearFailureOwner(NULL, false, kMainPanel, kChatWnd) == kChatWnd);
}
