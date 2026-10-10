/**
 *  帖子列表模型（docs/帖子群方案.md 4.2、4.6 左栏）：保存一个帖子群当前排序下已加载的帖子摘要与翻页状态，
 *  合并各页应答、本人操作的应答与 2720 推送，并维持显示顺序。纯逻辑，不触及界面与网络。
 *
 *  显示顺序：置顶的帖子总在最前（按置顶时间倒序）；其余按排序方式：
 *    - 最新动态：按最新动态时间倒序，相同时按帖子 ID 倒序（与服务端 listPosts 的 ORDER BY 一致）；
 *    - 最新发布：按帖子 ID 倒序。
 *  翻页：第一页替换全部；后续页追加，与已有的帖子重复时以新拉到的为准（推送可能已经把它插到了前面）。
 *  推送：修改与置顶的推送里互动汇总的 mine 是动作发起人视角，不可用，只取正文与置顶字段，本地的互动汇总保留。
 *
 *  典型用法：
 *      CThreadFeedModel feed;
 *      feed.Reset(thread_sort_activity);
 *      feed.BeginLoad();
 *      feed.ApplyPage(posts, next, more, true);
 *      feed.ApplyChange(change, myId);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ThreadTypes.h"

namespace threadgroup {

/**
 *  CThreadFeedModel —— 帖子列表模型。只在界面线程上使用。
 */
class CThreadFeedModel
{
public:
    CThreadFeedModel();

    /**
     *  清空列表并改用给定的排序（切换排序或重新打开窗口时）。
     *  @param sort 排序，取值见 ThreadSort
     */
    void Reset(int sort);

    //当前排序
    int Sort() const;

    /**
     *  标记开始加载一页；正在加载时返回 false（调用方不要重复发请求）。
     */
    bool BeginLoad();

    //加载失败：清掉「正在加载」，下次可以重试
    void LoadFailed();

    //是否正在加载
    bool IsLoading() const;

    //是否已经收到过第一页
    bool IsLoaded() const;

    //是否还有下一页
    bool HasMore() const;

    //下一页的游标；第一页为空
    const std::string& NextCursor() const;

    /**
     *  合并一页应答。
     *  @param posts      这一页的帖子（第一页另带全部置顶帖）
     *  @param nextCursor 下一页的游标
     *  @param hasMore    是否还有下一页
     *  @param firstPage  是否第一页：是则替换全部，否则追加
     */
    void ApplyPage(const std::vector<ThreadPost>& posts, const std::string& nextCursor, bool hasMore, bool firstPage);

    /**
     *  插入或更新一条帖子（本人发帖、修改、置顶的应答；mine 为本人视角，整条替换）。
     *  @param post 帖子；完整形态时只取摘要需要的字段（补充段数等），正文照样保存
     */
    void Upsert(const ThreadPost& post);

    /**
     *  删除一条帖子。
     *  @return 列表里有这条帖子并已删除返回 true
     */
    bool Remove(int64_t postId);

    /**
     *  本人对帖子点了互动后立即更新它的汇总（不等应答），返回点之前的汇总供失败时恢复。
     *  @return 列表里有这条帖子返回 true
     */
    bool ToggleReactionLocal(int64_t postId, int reaction, bool on, std::vector<ThreadReaction>& before);

    /**
     *  用 2711 应答（或失败时的恢复值）替换某条帖子的互动汇总。
     */
    void SetReactions(int64_t postId, const std::vector<ThreadReaction>& reactions);

    /**
     *  合并一条 2720 推送。
     *  @param change 推送内容
     *  @param myId   本人 userid（判断互动的发起人是不是本人）
     *  @return 列表有变化（需要重画）返回 true
     */
    bool ApplyChange(const ThreadChange& change, int64_t myId);

    //按显示顺序排列的帖子
    const std::vector<ThreadPost>& Posts() const;

    //按 ID 找帖子；没有返回 NULL
    const ThreadPost* Find(int64_t postId) const;

    //置顶帖的条数
    int PinnedCount() const;

private:
    //按显示顺序重新排列
    void Reorder();

    //按 ID 找帖子的下标；没有返回 -1
    int IndexOf(int64_t postId) const;

private:
    int                     m_sort;        //排序，取值见 ThreadSort
    std::vector<ThreadPost> m_posts;       //已加载的帖子，按显示顺序
    std::string             m_nextCursor;  //下一页的游标
    bool                    m_hasMore;     //是否还有下一页
    bool                    m_loading;     //是否正在加载一页
    bool                    m_loaded;      //是否已经收到过第一页
};

} // namespace threadgroup
