/**
 *  主面板搜索下拉群文件请求状态的实现。整体说明见 GroupFileSearchState.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "GroupFileSearchState.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonsearch {

GroupFileSearchState::GroupFileSearchState()
    : m_status(kGroupFileIdle)
{
}

void GroupFileSearchState::Reset()
{
    m_keyword.clear();
    m_status = kGroupFileIdle;
}

void GroupFileSearchState::OnRequestSent(const std::string& keywordUtf8)
{
    m_keyword = keywordUtf8;
    m_status  = kGroupFilePending;
}

void GroupFileSearchState::OnOffline(const std::string& keywordUtf8)
{
    m_keyword = keywordUtf8;
    m_status  = kGroupFileOffline;
}

bool GroupFileSearchState::OnResponse(const std::string& echoedKeywordUtf8, bool success)
{
    //只采纳最近一次请求的应答：关键字不同说明是更早那次请求的应答迟到了；不在等应答
    //（已收到过、已收起下拉、或根本没发出）时到来的应答同样作废。
    if (m_status != kGroupFilePending || m_keyword.empty() || echoedKeywordUtf8 != m_keyword)
    {
        return false;
    }
    m_status = success ? kGroupFileDone : kGroupFileFailed;
    return true;
}

GroupFileSearchStatus GroupFileSearchState::StatusFor(const std::string& keywordUtf8) const
{
    if (keywordUtf8.empty() || keywordUtf8 != m_keyword)
    {
        return kGroupFileIdle;
    }
    return m_status;
}

std::wstring GroupFileStatusHint(GroupFileSearchStatus status)
{
    switch (status)
    {
    //请求已发出、应答未到
    case kGroupFilePending:
        return TR(L"正在搜索群文件…");
    //没连上服务器，请求没有发出：本地的聊天文件照常显示，只是群文件这一段搜不了
    case kGroupFileOffline:
        return TR(L"群文件需联网搜索");
    //服务端返回失败
    case kGroupFileFailed:
        return TR(L"群文件搜索失败");
    //还没发请求（延迟查询未到点）或已有结果：不显示提示
    default:
        return std::wstring();
    }
}

std::wstring ServerSearchStatusHint(GroupFileSearchStatus status)
{
    switch (status)
    {
    //请求已发出、应答未到
    case kGroupFilePending:
        return TR(L"正在搜索服务器…");
    //没连上服务器，请求没有发出：本机的聊天记录照常显示
    case kGroupFileOffline:
        return TR(L"搜索服务器上的聊天记录需要联网");
    //服务端返回失败
    case kGroupFileFailed:
        return TR(L"搜索服务器失败，请稍后重试");
    //还没发请求或已有结果：不显示提示
    default:
        return std::wstring();
    }
}

} // namespace balloonsearch
