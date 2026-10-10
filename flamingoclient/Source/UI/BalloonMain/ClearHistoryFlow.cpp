/**
 *  ClearHistoryFlow.cpp
 *
 *  「清空聊天记录」流程中纯逻辑部分的实现，说明见 ClearHistoryFlow.h。
 *  不碰任何窗口，故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在
 *  Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（提示文字）。两个工程均已开启 /utf-8 编译选项，无需 BOM。
 *
 *  balloonwj@qq.com   2026-10-02
 */

#include "ClearHistoryFlow.h"
#include "../../net/Protocol.h"   // error_code_xxx、conversation_group
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// 会话的称呼：单聊"与「X」"、群聊"群聊「X」"；名字为空时分别为"与该联系人" / "该群"。
// 结果作为整句模板的参数代入
std::wstring ConversationPhrase(int ctype, const std::wstring& name)
{
    if (ctype == conversation_group)
    {
        return name.empty() ? std::wstring(TR(L"该群")) : TRF(L"群聊「{0}」", name);
    }
    return name.empty() ? std::wstring(TR(L"与该联系人")) : TRF(L"与「{0}」", name);
}

} // namespace

std::wstring BuildClearHistoryConfirmText(int ctype, const std::wstring& name)
{
    // 群聊说"其他群成员"，单聊说"对方"：两种会话里不受影响的人不同，各用一句完整的模板
    if (ctype == conversation_group)
    {
        return TRF(L"确定清空{0}的聊天记录吗？\n\n只清空你这一方的记录，你登录的其他设备会同步清空，"
                   L"其他群成员的聊天记录不受影响。清空后无法恢复。",
                   ConversationPhrase(ctype, name));
    }
    return TRF(L"确定清空{0}的聊天记录吗？\n\n只清空你这一方的记录，你登录的其他设备会同步清空，"
               L"对方的聊天记录不受影响。清空后无法恢复。",
               ConversationPhrase(ctype, name));
}

std::wstring BuildClearHistoryErrorText(int code, int ctype, const std::wstring& name)
{
    switch (code)
    {
    //成功：没有提示
    case error_code_ok:
        return std::wstring();

    //不是群成员：本人已被移出或群已解散，界面上的群还没来得及移除
    case error_code_not_group_member:
        return TRF(L"你已不在该群中，无法清空{0}的聊天记录。", ConversationPhrase(ctype, name));

    //其它错误（服务器内部错误、报文格式错误等）：通用提示，用户可以重试
    default:
        return TRF(L"清空{0}的聊天记录失败，请稍后重试。", ConversationPhrase(ctype, name));
    }
}

void ClearHistoryTracker::Begin(uint32_t seq, const PendingClearHistory& pending)
{
    // seq 为 0 表示请求没有发出（参数非法或未连接），不会有应答，登记了也永远对不上
    if (seq == 0)
    {
        return;
    }
    m_pending[seq] = pending;
}

ClearHistoryOutcome ClearHistoryTracker::OnResult(uint32_t seq, int code)
{
    ClearHistoryOutcome outcome;
    std::map<uint32_t, PendingClearHistory>::iterator it = m_pending.find(seq);
    if (it == m_pending.end())
    {
        return outcome;
    }

    outcome.m_matched = true;
    outcome.m_op = it->second;
    m_pending.erase(it);
    outcome.m_succeeded = (code == error_code_ok);
    if (!outcome.m_succeeded)
    {
        outcome.m_errorText = BuildClearHistoryErrorText(code, outcome.m_op.m_ctype, outcome.m_op.m_name);
    }
    return outcome;
}

size_t ClearHistoryTracker::PendingCount() const
{
    return m_pending.size();
}

HWND ChooseClearFailureOwner(HWND requester, bool requesterIsWindow, HWND mainPanel, HWND conversationOwner)
{
    //主面板右键菜单发起时 requester 就是主面板：这时仍按会话找聊天窗，与改动之前一致
    if (requester != NULL && requesterIsWindow && requester != mainPanel)
    {
        return requester;
    }
    return conversationOwner;
}

} // namespace balloonmain
