/**
 *  转发调度器的实现，见 ForwardDispatcher.h。
 *  balloonwj@qq.com   2026-10-01
 */
#include "ForwardDispatcher.h"

#include <stdio.h>   // _snwprintf_s

#include "../../net/Protocol.h"   // error_code_*

namespace balloonmain {

namespace {

//拼提示文字时格式化一个整数用的缓冲区长度，足够放下任意 int 与前后文字
const size_t kFormatBufLen = 64;

} // anonymous namespace

ForwardDispatcher::ForwardDispatcher()
    : m_nextBatchId(1)
{
}

int ForwardDispatcher::StartBatch(HWND srcWnd, bool merge, const std::vector<ForwardTarget>& targets,
                                  const std::vector<uint32_t>& seqs, int64_t nowMs,
                                  ForwardBatchOutcome* outcome, bool* done)
{
    const int batchId = m_nextBatchId++;
    if (done != NULL)
    {
        *done = false;
    }

    Batch batch;
    batch.m_outcome.m_batchId = batchId;
    batch.m_outcome.m_srcWnd = srcWnd;
    batch.m_outcome.m_merge = merge;
    batch.m_outcome.m_targetCount = targets.size();
    batch.m_pending = 0;

    //先把能发出的请求登记为待应答，没能发出的当场记为失败
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const uint32_t seq = (i < seqs.size()) ? seqs[i] : 0;
        if (seq == 0)
        {
            ForwardFailure f;
            f.m_target = targets[i];
            f.m_code = kForwardCodeNotSent;
            batch.m_outcome.m_failed.push_back(f);
            continue;
        }
        PendingRequest req;
        req.m_batchId = batchId;
        req.m_target = targets[i];
        req.m_sentMs = nowMs;
        m_pending[seq] = req;
        ++batch.m_pending;
    }

    if (batch.m_pending == 0)
    {
        //一个都没发出（或没有目标）：登记即完成，不进 m_batches
        if (outcome != NULL)
        {
            *outcome = batch.m_outcome;
        }
        if (done != NULL)
        {
            *done = true;
        }
        return batchId;
    }

    m_batches[batchId] = batch;
    return batchId;
}

ForwardResponseKind ForwardDispatcher::OnResponse(uint32_t seq, int code, ForwardTarget* target,
                                                  ForwardBatchOutcome* outcome)
{
    std::map<uint32_t, PendingRequest>::iterator it = m_pending.find(seq);
    if (it == m_pending.end())
    {
        return kForwardResponseUnknown;
    }
    const PendingRequest req = it->second;
    m_pending.erase(it);

    if (target != NULL)
    {
        *target = req.m_target;
    }
    return RecordResult(req.m_batchId, req.m_target, code, outcome)
           ? kForwardResponseBatchDone : kForwardResponsePending;
}

std::vector<ForwardBatchOutcome> ForwardDispatcher::CheckTimeouts(int64_t nowMs, int64_t timeoutMs)
{
    std::vector<ForwardBatchOutcome> finished;

    //先收集超时的 seq，再逐个处理：处理过程中会改动 m_pending，不能边遍历边删
    std::vector<uint32_t> expired;
    for (std::map<uint32_t, PendingRequest>::const_iterator it = m_pending.begin();
         it != m_pending.end(); ++it)
    {
        if (nowMs - it->second.m_sentMs > timeoutMs)
        {
            expired.push_back(it->first);
        }
    }

    for (size_t i = 0; i < expired.size(); ++i)
    {
        std::map<uint32_t, PendingRequest>::iterator it = m_pending.find(expired[i]);
        if (it == m_pending.end())
        {
            continue;
        }
        const PendingRequest req = it->second;
        m_pending.erase(it);

        ForwardBatchOutcome outcome;
        if (RecordResult(req.m_batchId, req.m_target, kForwardCodeTimeout, &outcome))
        {
            finished.push_back(outcome);
        }
    }
    return finished;
}

bool ForwardDispatcher::HasPending() const
{
    return !m_pending.empty();
}

bool ForwardDispatcher::RecordResult(int batchId, const ForwardTarget& target, int code,
                                     ForwardBatchOutcome* outcome)
{
    std::map<int, Batch>::iterator it = m_batches.find(batchId);
    if (it == m_batches.end())
    {
        return false;
    }
    Batch& batch = it->second;
    if (code == error_code_ok)
    {
        batch.m_outcome.m_succeeded.push_back(target);
    }
    else
    {
        ForwardFailure f;
        f.m_target = target;
        f.m_code = code;
        batch.m_outcome.m_failed.push_back(f);
    }

    if (batch.m_pending > 0)
    {
        --batch.m_pending;
    }
    if (batch.m_pending > 0)
    {
        return false;
    }

    if (outcome != NULL)
    {
        *outcome = batch.m_outcome;
    }
    m_batches.erase(it);
    return true;
}

std::wstring ForwardErrorText(int code)
{
    switch (code)
    {
    // 转发给单聊：对方把本人拉黑了
    case error_code_in_blacklist:
        return L"已被对方拉黑";
    // 转发给单聊：对方账号不存在
    case error_code_not_found:
        return L"对方账号不存在";
    // 转发到群：本人已不在该群
    case error_code_not_group_member:
        return L"你已不是该群成员";
    // 转发到群：群已解散
    case error_code_group_not_exist:
        return L"群已解散";
    // 转发到群：本人被单独禁言，或群开启了全员禁言
    case error_code_member_muted:
        return L"你在该群被禁言";
    // 转发到群：内容命中该群的敏感词
    case error_code_sensitive_word:
        return L"内容包含该群设置的敏感词";
    // 源消息全部被跳过：已撤回、已删除或本人看不到
    case error_code_message_not_exist:
        return L"消息已被撤回或删除";
    // 合并转发的聊天记录超过服务端的体积上限
    case error_code_msg_too_large:
        return L"聊天记录过大，请减少条数后重试";
    // 请求没能发出
    case kForwardCodeNotSent:
        return L"网络未连接";
    // 请求发出后等不到应答
    case kForwardCodeTimeout:
        return L"等待服务器应答超时";
    // 其余错误码：没有专门文案，带上数值便于反馈问题
    default:
        {
            wchar_t buf[kFormatBufLen] = { 0 };
            _snwprintf_s(buf, kFormatBufLen, _TRUNCATE, L"转发失败（错误码 %d）", code);
            return buf;
        }
    }
}

std::wstring BuildForwardFailureText(const ForwardBatchOutcome& outcome)
{
    wchar_t buf[kFormatBufLen] = { 0 };
    _snwprintf_s(buf, kFormatBufLen, _TRUNCATE, L"以下 %u 个聊天转发失败：",
                 (unsigned)outcome.m_failed.size());
    std::wstring text = buf;

    for (size_t i = 0; i < outcome.m_failed.size(); ++i)
    {
        text += L"\n";
        text += outcome.m_failed[i].m_target.m_name;
        text += L"：";
        text += ForwardErrorText(outcome.m_failed[i].m_code);
    }

    if (!outcome.m_succeeded.empty())
    {
        _snwprintf_s(buf, kFormatBufLen, _TRUNCATE, L"\n\n其余 %u 个已转发。",
                     (unsigned)outcome.m_succeeded.size());
        text += buf;
    }
    return text;
}

} // namespace balloonmain
