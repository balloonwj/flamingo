/**
 *  转发调度器：记录用户每次点「发送」发出的一组 cmd_forward_message 请求，按帧 seq 把
 *  应答对回到目标，全部目标都有结果（成功、失败、没能发出或超时）后给出一份汇总，
 *  供主面板提示用户。纯逻辑，不碰网络与界面，可被 FlamingoUnit 直接测试。
 *
 *  为什么按帧 seq 对回：服务端的失败应答只有 code 与 msg，不带 clientmsgid，也不带目标会话，
 *  能把它与请求对上号的只有帧 seq（CIUSocket::ForwardMessage 的返回值）。
 *
 *  典型用法（主面板）：
 *      std::vector<uint32_t> seqs;   //对每个目标调用 CIUSocket::ForwardMessage 得到的 seq
 *      m_forwardDispatcher.StartBatch(srcWnd, merge, targets, seqs, now, &outcome, &done);
 *      //收到 FMG_MSG_V2_FORWARD_RESULT：
 *      if (m_forwardDispatcher.OnResponse(seq, code, &target, &outcome) == kForwardResponseBatchDone)
 *          提示结果;
 *      //定时器里：
 *      std::vector<ForwardBatchOutcome> late = m_forwardDispatcher.CheckTimeouts(now, kForwardResponseTimeoutMs);
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "ForwardTargetModel.h"   // ForwardTarget

namespace balloonmain {

//失败原因里的两个客户端自定义取值，与服务端 error_code 的正数取值不冲突
const int kForwardCodeNotSent = -2;   // 请求没能发出（离线或未连接），CIUSocket::ForwardMessage 返回了 0
const int kForwardCodeTimeout = -3;   // 请求已发出，但超过时限仍未收到应答

//等待转发应答的时限（毫秒）。逐条转发 100 条消息时服务端要逐条落库，正常也在一两秒内
//返回；超过这个时限，多半是请求发出后连接断了、应答再也不会来
const int64_t kForwardResponseTimeoutMs = 15000;

//主面板上的转发超时检查定时器：标识与检查间隔（毫秒）。标识避开主面板及其协作类已用的
//1、2（托盘）、5（自动重连）、1002（自动离开）、0xB001（悬浮滚动条动画）
const UINT_PTR kForwardTimeoutTimerId = 0xF0A1;
const UINT     kForwardTimeoutCheckMs = 2000;

/**
 *  一个目标的失败记录。
 */
struct ForwardFailure
{
    ForwardTarget m_target;   // 失败的目标
    int           m_code;     // 原因：服务端 error_code，或 kForwardCodeNotSent / kForwardCodeTimeout

    ForwardFailure()
        : m_code(0)
    {
    }
};

/**
 *  一次转发（用户点一次「发送」）的汇总结果。
 */
struct ForwardBatchOutcome
{
    int                         m_batchId;       // 批次号，StartBatch 的返回值
    HWND                        m_srcWnd;        // 发起转发的聊天窗或消息记录窗；可能已关闭，使用前须 IsWindow
    bool                        m_merge;         // 是否合并转发
    size_t                      m_targetCount;   // 本批目标总数
    std::vector<ForwardTarget>  m_succeeded;     // 成功的目标，按应答先后排列
    std::vector<ForwardFailure> m_failed;        // 失败的目标，按判定先后排列

    ForwardBatchOutcome()
        : m_batchId(0)
        , m_srcWnd(NULL)
        , m_merge(false)
        , m_targetCount(0)
    {
    }
};

/**
 *  ForwardDispatcher::OnResponse 的结果。
 */
enum ForwardResponseKind
{
    kForwardResponseUnknown   = 0,   // 不认识这个 seq：不是转发请求、已超时、或重复的应答，调用方忽略
    kForwardResponsePending   = 1,   // 认识，已记下结果；本批还有目标没有结果
    kForwardResponseBatchDone = 2    // 认识，已记下结果；本批全部目标都有了结果，outcome 有效
};

/**
 *  转发调度器。只在 UI 线程使用，不是线程安全的。
 */
class ForwardDispatcher
{
public:
    ForwardDispatcher();

    /**
     *  登记一次转发。
     *    srcWnd：  发起转发的聊天窗，原样放进汇总结果，本类不使用它。
     *    merge：   是否合并转发，原样放进汇总结果。
     *    targets： 本次的目标。
     *    seqs：    与 targets 一一对应的请求帧 seq；0 表示该目标的请求没能发出，当场记为失败。
     *              长度与 targets 不一致时，缺的按 0 处理。
     *    nowMs：   当前时刻（毫秒，单调递增的时钟即可），用于超时判定。
     *    outcome： [出参] *done 为 true 时是本批的汇总结果。
     *    done：    [出参] 本批是否在登记时就已全部有结果（例如全部没能发出）。
     *  @return 批次号（正数）
     */
    int StartBatch(HWND srcWnd, bool merge, const std::vector<ForwardTarget>& targets,
                   const std::vector<uint32_t>& seqs, int64_t nowMs,
                   ForwardBatchOutcome* outcome, bool* done);

    /**
     *  转发应答到达。
     *    seq：     应答的帧 seq。
     *    code：    应答的结果码，0 为成功。
     *    target：  [出参，可为 NULL] 认识这个 seq 时写入对应的目标，成功时调用方据它在本端显示新消息。
     *    outcome： [出参，可为 NULL] 返回 kForwardResponseBatchDone 时写入本批的汇总结果。
     *  @return 见 ForwardResponseKind
     */
    ForwardResponseKind OnResponse(uint32_t seq, int code, ForwardTarget* target,
                                   ForwardBatchOutcome* outcome);

    /**
     *  把发出超过 timeoutMs 仍无应答的请求记为超时失败。
     *    nowMs：     当前时刻（与 StartBatch 用同一个时钟）。
     *    timeoutMs： 时限；已等待的时长严格大于它才算超时。
     *  @return 因此全部有了结果的批次的汇总；没有时为空
     */
    std::vector<ForwardBatchOutcome> CheckTimeouts(int64_t nowMs, int64_t timeoutMs);

    /**
     *  是否还有请求在等应答。主面板据此决定超时检查定时器开不开。
     */
    bool HasPending() const;

private:
    //一个在等应答的请求
    struct PendingRequest
    {
        int           m_batchId;   // 所属批次
        ForwardTarget m_target;    // 目标
        int64_t       m_sentMs;    // 发出时刻，用于超时判定
    };

    //一个还没全部有结果的批次
    struct Batch
    {
        ForwardBatchOutcome m_outcome;   // 已记下的结果
        size_t              m_pending;   // 还在等应答的请求数
    };

    //把一个目标的结果记进批次；批次因此完成时返回 true 并从 m_batches 移除，汇总写入 outcome
    bool RecordResult(int batchId, const ForwardTarget& target, int code,
                      ForwardBatchOutcome* outcome);

    int                               m_nextBatchId;   // 下一个批次号，从 1 起递增
    std::map<uint32_t, PendingRequest> m_pending;      // 帧 seq → 在等应答的请求
    std::map<int, Batch>              m_batches;       // 批次号 → 还没全部有结果的批次
};

/**
 *  转发失败原因的提示文字。
 *    code：服务端 error_code，或 kForwardCodeNotSent / kForwardCodeTimeout。
 *  @return 原因（不含目标名字）；没有专门文案的错误码返回带错误码数值的通用说法
 */
std::wstring ForwardErrorText(int code);

/**
 *  一次转发有失败目标时，给用户看的提示文字：逐行列出失败目标的名字与原因，
 *  有成功的目标时再说明其余几个已转发。
 *    outcome：本批的汇总结果，调用方保证 m_failed 非空。
 *  @return 提示文字（多行）
 */
std::wstring BuildForwardFailureText(const ForwardBatchOutcome& outcome);

} // namespace balloonmain
