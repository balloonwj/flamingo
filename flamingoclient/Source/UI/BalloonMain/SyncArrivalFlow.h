/**
 *  一页历史 / 同步消息应答到达后，主面板各项处理的先后顺序。
 *
 *  从 CBalloonMainPanel::OnSyncMessagesArrived 中抽出，只负责"先做什么、后做什么、哪些情况跳过"，
 *  每一项的具体动作由调用方实现 ISyncArrivalSteps 提供。这样顺序本身可以脱离窗口单独测试
 *  （SyncArrivalFlowTests.cpp）。
 *
 *  顺序的要点：先把消息交给聊天窗并让它立即画出来，再写本地消息库。写库一页 50 条约 20 毫秒，
 *  放在前面的话聊天窗要等它写完才显示内容；聊天窗的渲染不读本地库，调换顺序不影响结果。
 *
 *  典型用法（CBalloonMainPanel::OnSyncMessagesArrived 内）：
 *      SyncArrivalContext ctx;
 *      ctx.m_historyOwnFetch = ...;   // 必须在任何一步执行之前算好，见该字段的注释
 *      ctx.m_isSingleChat = (result.m_ctype != 2);
 *      RunSyncArrival(steps, ctx);
 *
 *  <u>本文件被 FlamingoUnit 测试工程直接编译</u>，故 .cpp 不走主工程的预编译头。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

namespace balloonmain {

/**
 *  一页同步应答到达后的各项处理。由 CBalloonMainPanel 实现；单元测试里用记录调用顺序的替身实现。
 *  各方法只做自己那一项，不关心先后，先后由 RunSyncArrival 决定。
 */
class ISyncArrivalSteps
{
public:
    virtual ~ISyncArrivalSteps() {}

    //这一页所属会话的聊天窗是否仍然打开着
    virtual bool HasOpenChatWindow() = 0;

    //把这一页交给聊天窗渲染（聊天窗的 OnHistoryMessages）
    virtual void RenderToChatWindow() = 0;

    //单聊：应用这一页带回的对方已读位点
    virtual void ApplyPeerRead() = 0;

    //让聊天窗立即完成一次绘制，不等消息队列空闲时才处理 WM_PAINT
    virtual void PaintChatWindowNow() = 0;

    /**
     *  把这一页写进本地消息库。
     *  @return 写入成功返回 true；失败时由实现方记录日志，后续各项照常执行
     */
    virtual bool StoreBatch() = 0;

    //按这一页对账本地库并更新覆盖区间（读的是刚写进库的数据，须在 StoreBatch 之后）
    virtual void ReconcileCoverage() = 0;

    //消息记录窗正显示这个会话时刷新它（须在 ReconcileCoverage 之后，才能看到对完账的结果）
    virtual void RefreshHistoryWindow() = 0;
};

//一页同步应答的处理条件
struct SyncArrivalContext
{
    //这一页是不是消息记录窗为"往前拉更早的消息"发出的。是的话不交给聊天窗：聊天窗只靠自己的
    //翻页标志区分首屏与翻页，一页更早的消息转过去会被当成首屏，把聊天窗清空、换成一页旧消息。
    //调用方必须在执行任何一项之前算好它：刷新消息记录窗会改变它的拉取锚点，之后再算就认不出了
    bool m_historyOwnFetch;
    //是否为单聊（单聊应答带回对方已读位点，群聊没有）
    bool m_isSingleChat;

    SyncArrivalContext()
        : m_historyOwnFetch(false)
        , m_isSingleChat(false)
    {
    }
};

/**
 *  按规定顺序执行一页同步应答的各项处理：
 *    1. 交给聊天窗渲染（消息记录窗自己拉的页、或聊天窗没开时跳过）；
 *    2. 单聊时应用对方已读位点（任何情况下都执行）；
 *    3. 让聊天窗立即绘制（与第 1 步同条件）；
 *    4. 写本地消息库；
 *    5. 对账覆盖区间；
 *    6. 刷新消息记录窗。
 *  已读位点排在绘制之前，这样第一次画出来的就已经带着"已读"标记，不会先显示未读再变成已读。
 *
 *  @param steps 各项处理的实现，由调用方持有
 *  @param ctx   处理条件
 *  @return 第 4 步写库是否成功
 */
bool RunSyncArrival(ISyncArrivalSteps& steps, const SyncArrivalContext& ctx);

} // namespace balloonmain
