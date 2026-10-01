/**
 *  ForwardDispatcherTests.cpp
 *
 *  转发调度器（balloonmain::ForwardDispatcher）的测试（标签 [forward]），见
 *  docs/聊天消息转发方案.md §5.4、§7.2 的 FWD-C12。
 *
 *    FWD-C12a  一次转发多个目标：应答按帧 seq 对回目标，全部返回后才给出汇总；
 *              部分失败时成功与失败分开记录；未知 seq、重复应答一律忽略。
 *    FWD-C12b  没能发出的请求（seq 为 0）当场记为失败；全部没能发出时登记当场就完成。
 *    FWD-C12c  超时：发出后超过时限仍无应答的请求记为超时失败；超时之后才到的应答忽略；
 *              两次转发的请求互不串批。
 *    FWD-C12d  失败提示文字：错误码对应的原因、失败目标的名字、成功的条数。
 *
 *  用 Catch2 v3（amalgamated 形态），纯内存断言。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonMain/ForwardDispatcher.h"
#include "../net/Protocol.h"   // conversation_* / error_code_*

using namespace balloonmain;

namespace {

// 测试用的时间起点（毫秒）与超时时限（毫秒）
const int64_t kT0 = 1000000;
const int64_t kTimeoutMs = 15000;

// 造一个目标
ForwardTarget MakeTarget(int ctype, int64_t cid, const wchar_t* name)
{
    ForwardTarget t;
    t.m_ctype = ctype;
    t.m_cid = cid;
    t.m_name = name;
    return t;
}

// 三个目标：两个单聊、一个群
std::vector<ForwardTarget> ThreeTargets()
{
    std::vector<ForwardTarget> v;
    v.push_back(MakeTarget(conversation_single, 100002, L"张三"));
    v.push_back(MakeTarget(conversation_single, 100003, L"李四"));
    v.push_back(MakeTarget(conversation_group, 900001, L"项目组"));
    return v;
}

// 把一组 seq 拼成数组
std::vector<uint32_t> Seqs(uint32_t a, uint32_t b, uint32_t c)
{
    std::vector<uint32_t> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    return v;
}

} // anonymous namespace

TEST_CASE("FWD-C12a 多个目标全部返回后才汇总，成功与失败分开记录", "[forward]")
{
    ForwardDispatcher d;
    ForwardBatchOutcome outcome;
    bool done = true;
    d.StartBatch(NULL, true, ThreeTargets(), Seqs(11, 12, 13), kT0, &outcome, &done);
    CHECK_FALSE(done);
    CHECK(d.HasPending());

    // 第一个应答：对应李四，成功；批次尚未完成
    ForwardTarget target;
    CHECK(d.OnResponse(12, error_code_ok, &target, &outcome) == kForwardResponsePending);
    CHECK(target.m_cid == 100003);

    // 第二个：张三，被拉黑
    CHECK(d.OnResponse(11, error_code_in_blacklist, &target, &outcome) == kForwardResponsePending);
    CHECK(target.m_cid == 100002);

    // 最后一个：项目组，成功；批次完成并给出汇总
    CHECK(d.OnResponse(13, error_code_ok, &target, &outcome) == kForwardResponseBatchDone);
    CHECK(target.m_cid == 900001);
    CHECK(outcome.m_targetCount == 3);
    CHECK(outcome.m_merge);
    REQUIRE(outcome.m_succeeded.size() == 2);
    CHECK(outcome.m_succeeded[0].m_cid == 100003);
    CHECK(outcome.m_succeeded[1].m_cid == 900001);
    REQUIRE(outcome.m_failed.size() == 1);
    CHECK(outcome.m_failed[0].m_target.m_name == L"张三");
    CHECK(outcome.m_failed[0].m_code == error_code_in_blacklist);
    CHECK_FALSE(d.HasPending());

    // 未知 seq 与重复应答：忽略
    CHECK(d.OnResponse(99, error_code_ok, &target, &outcome) == kForwardResponseUnknown);
    CHECK(d.OnResponse(13, error_code_ok, &target, &outcome) == kForwardResponseUnknown);
}

TEST_CASE("FWD-C12b 没能发出的请求当场记为失败", "[forward]")
{
    // 部分没能发出：只等发出的那个
    ForwardDispatcher d;
    ForwardBatchOutcome outcome;
    bool done = true;
    d.StartBatch(NULL, false, ThreeTargets(), Seqs(0, 21, 0), kT0, &outcome, &done);
    CHECK_FALSE(done);

    ForwardTarget target;
    CHECK(d.OnResponse(21, error_code_ok, &target, &outcome) == kForwardResponseBatchDone);
    REQUIRE(outcome.m_succeeded.size() == 1);
    CHECK(outcome.m_succeeded[0].m_cid == 100003);
    REQUIRE(outcome.m_failed.size() == 2);
    CHECK(outcome.m_failed[0].m_code == kForwardCodeNotSent);
    CHECK(outcome.m_failed[1].m_code == kForwardCodeNotSent);

    // 全部没能发出：登记时当场完成
    ForwardDispatcher offline;
    ForwardBatchOutcome all;
    bool allDone = false;
    offline.StartBatch(NULL, false, ThreeTargets(), Seqs(0, 0, 0), kT0, &all, &allDone);
    CHECK(allDone);
    CHECK(all.m_succeeded.empty());
    CHECK(all.m_failed.size() == 3);
    CHECK_FALSE(offline.HasPending());
}

TEST_CASE("FWD-C12c 超时记为失败，迟到的应答忽略，两次转发互不串批", "[forward]")
{
    ForwardDispatcher d;
    ForwardBatchOutcome first;
    ForwardBatchOutcome second;
    bool done = false;

    std::vector<ForwardTarget> two;
    two.push_back(MakeTarget(conversation_single, 100002, L"张三"));
    two.push_back(MakeTarget(conversation_single, 100003, L"李四"));
    std::vector<uint32_t> seqs1;
    seqs1.push_back(31);
    seqs1.push_back(32);
    const int batch1 = d.StartBatch(NULL, false, two, seqs1, kT0, &first, &done);

    // 第二次转发晚 5 秒发出，只有一个目标
    std::vector<ForwardTarget> one;
    one.push_back(MakeTarget(conversation_group, 900001, L"项目组"));
    std::vector<uint32_t> seqs2;
    seqs2.push_back(41);
    const int batch2 = d.StartBatch(NULL, false, one, seqs2, kT0 + 5000, &second, &done);
    CHECK(batch1 != batch2);

    ForwardTarget target;
    CHECK(d.OnResponse(31, error_code_ok, &target, &first) == kForwardResponsePending);

    // 未到时限：没有批次因超时完成
    CHECK(d.CheckTimeouts(kT0 + 10000, kTimeoutMs).empty());

    // 第一批的 32 超时；第二批发出才 11 秒，还没到时限
    std::vector<ForwardBatchOutcome> timedOut = d.CheckTimeouts(kT0 + 16000, kTimeoutMs);
    REQUIRE(timedOut.size() == 1);
    CHECK(timedOut[0].m_batchId == batch1);
    REQUIRE(timedOut[0].m_succeeded.size() == 1);
    REQUIRE(timedOut[0].m_failed.size() == 1);
    CHECK(timedOut[0].m_failed[0].m_target.m_cid == 100003);
    CHECK(timedOut[0].m_failed[0].m_code == kForwardCodeTimeout);

    // 超时之后才到的应答：忽略
    CHECK(d.OnResponse(32, error_code_ok, &target, &first) == kForwardResponseUnknown);

    // 第二批照常完成
    CHECK(d.OnResponse(41, error_code_member_muted, &target, &second) == kForwardResponseBatchDone);
    CHECK(second.m_batchId == batch2);
    REQUIRE(second.m_failed.size() == 1);
    CHECK(second.m_failed[0].m_target.m_cid == 900001);
    CHECK_FALSE(d.HasPending());
}

TEST_CASE("FWD-C12d 失败提示文字", "[forward]")
{
    // 常见错误码都有具体原因
    CHECK(ForwardErrorText(error_code_member_muted).find(L"禁言") != std::wstring::npos);
    CHECK(ForwardErrorText(error_code_in_blacklist).find(L"拉黑") != std::wstring::npos);
    CHECK(ForwardErrorText(error_code_sensitive_word).find(L"敏感词") != std::wstring::npos);
    CHECK(ForwardErrorText(error_code_not_group_member).find(L"群成员") != std::wstring::npos);
    CHECK(ForwardErrorText(error_code_msg_too_large).find(L"过大") != std::wstring::npos);
    CHECK(ForwardErrorText(kForwardCodeNotSent).find(L"网络") != std::wstring::npos);
    CHECK(ForwardErrorText(kForwardCodeTimeout).find(L"超时") != std::wstring::npos);
    // 没有专门文案的错误码：带上数值，便于反馈问题
    CHECK(ForwardErrorText(12345).find(L"12345") != std::wstring::npos);

    // 汇总：列出失败目标的名字与原因，并说明成功了几个
    ForwardBatchOutcome o;
    o.m_targetCount = 3;
    o.m_succeeded.push_back(MakeTarget(conversation_single, 100003, L"李四"));
    ForwardFailure f1;
    f1.m_target = MakeTarget(conversation_single, 100002, L"张三");
    f1.m_code = error_code_in_blacklist;
    ForwardFailure f2;
    f2.m_target = MakeTarget(conversation_group, 900001, L"项目组");
    f2.m_code = error_code_member_muted;
    o.m_failed.push_back(f1);
    o.m_failed.push_back(f2);

    const std::wstring text = BuildForwardFailureText(o);
    CHECK(text.find(L"张三") != std::wstring::npos);
    CHECK(text.find(L"项目组") != std::wstring::npos);
    CHECK(text.find(L"拉黑") != std::wstring::npos);
    CHECK(text.find(L"禁言") != std::wstring::npos);
    CHECK(text.find(L"1 个") != std::wstring::npos);   // 其余 1 个已转发

    // 全部失败时不写「其余已转发」
    ForwardBatchOutcome allFail = o;
    allFail.m_succeeded.clear();
    allFail.m_targetCount = 2;
    CHECK(BuildForwardFailureText(allFail).find(L"已转发") == std::wstring::npos);
}
