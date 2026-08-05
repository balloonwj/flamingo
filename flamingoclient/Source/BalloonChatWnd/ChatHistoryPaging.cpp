/**
 *  ChatHistoryPaging.cpp
 *
 *  聊天窗历史分页的纯逻辑实现。说明见 ChatHistoryPaging.h。
 *
 *  balloonwj@qq.com   2026-08-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 GroupMentionModel.cpp 同例。
#include "ChatHistoryPaging.h"

namespace balloonchatwnd {

bool ShouldLoadOlderHistory(const HistoryPagingState& st,
                            int scrollY, int viewHeight, int contentHeight)
{
    if (!st.m_hasMore)
    {
        return false;   // 已经到会话开头，再拉也是空
    }
    if (st.m_loading)
    {
        return false;   // 有请求在途，等它回来再说，否则会连发好几页
    }
    if (st.m_oldestMsgid == 0)
    {
        return false;   // 首屏还没到位，没有可用的 before 锚点
    }
    if (contentHeight <= viewHeight)
    {
        // 内容还不满一屏，连滚动条都没有 —— 此时 scrollY 恒为 0，若不挡这一道，
        // 会在"消息很少"的会话里一进窗口就无条件拉一页。
        return false;
    }
    return scrollY <= kHistoryLoadTriggerY;
}

void OnFirstPageLoaded(HistoryPagingState& st, long long oldestMsgid, bool hasMore)
{
    st.m_oldestMsgid = oldestMsgid;
    st.m_hasMore     = hasMore;
    st.m_loading     = false;
}

void OnOlderPageLoaded(HistoryPagingState& st, long long oldestMsgid, bool hasMore)
{
    // 本批为空（oldestMsgid 为 0）时锚点原样保留 —— 清成 0 会让下一次翻页的
    // before 变成 0，那是"从最新开始"的意思，等于把最新一页当成更早的插到头部。
    if (oldestMsgid != 0)
    {
        st.m_oldestMsgid = oldestMsgid;
    }
    st.m_hasMore = hasMore;
    st.m_loading = false;
}

} // namespace balloonchatwnd
