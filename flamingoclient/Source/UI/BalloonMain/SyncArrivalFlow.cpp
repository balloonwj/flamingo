/**
 *  一页同步应答到达后各项处理的先后顺序，说明见 SyncArrivalFlow.h。
 *  balloonwj@qq.com   2026-10-01
 */

#include "SyncArrivalFlow.h"

namespace balloonmain {

bool RunSyncArrival(ISyncArrivalSteps& steps, const SyncArrivalContext& ctx)
{
    //聊天窗只接收它自己请求的页；消息记录窗往前拉的更早页不能转过去
    const bool bToChatWindow = !ctx.m_historyOwnFetch && steps.HasOpenChatWindow();

    if (bToChatWindow)
        steps.RenderToChatWindow();

    //已读位点与这一页是谁拉的无关，单聊应答每次都要应用
    if (ctx.m_isSingleChat)
        steps.ApplyPeerRead();

    if (bToChatWindow)
        steps.PaintChatWindowNow();

    const bool bStored = steps.StoreBatch();
    steps.ReconcileCoverage();
    steps.RefreshHistoryWindow();
    return bStored;
}

} // namespace balloonmain
