/**
 *  MemoBook.h
 *
 *  「备忘与提醒」在客户端内存里的备忘簿：登录后 cmd_memo_list 的结果整体放进来，之后按 cmd_memo_notify
 *  增删改。备忘窗口的月历与当天列表、群聊窗「备忘」页签、提醒调度都从这里取数据，不各自缓存。
 *  纯逻辑，不碰窗口与网络，便于 FlamingoUnit 直接断言（Tests/MemoBookTests.cpp）。
 *
 *  群「备忘」页签的角标：本次登录之后经推送新到的、自己能看到的群备忘，在点开该群的页签之前计数；
 *  自己创建或修改的不计（docs/备忘与提醒方案.md 1.2）。
 *
 *  群主、管理员能看到本群全部群备忘（群页签里列出），但只有提醒到本人的那些（提醒全体成员，或被指定）
 *  才进本人的日历与提醒：按天列事件、有事件的日子、调度输入都只取个人备忘与提醒到本人的群备忘。
 *
 *  典型用法：
 *      memo::CMemoBook book;                       //主面板成员，生命周期跟随登录会话
 *      book.ReplaceAll(listResult.m_memos);        //2401 应答
 *      book.ApplyUpsert(notify.m_memo, selfId);    //2404 changetype=1
 *      book.ListDayEntries(day, entries);          //备忘窗口右侧当天列表
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <map>
#include <set>
#include <vector>
#include "MemoRule.h"
#include "MemoScheduler.h"
#include "../../net/MemoProtocolClient.h"

namespace memo {

//月历一屏最多显示的天数：6 行 × 7 列
const int kMemoCalendarCells = 42;

/**
 *  当天列表里的一项：哪条备忘、这一天的哪次事件。
 */
struct MemoDayEntry
{
    int64_t     m_memoid;   //备忘 ID
    LocalMinute m_event;    //这一天里的事件时间

    MemoDayEntry() : m_memoid(0), m_event(0) {}
};

/**
 *  把服务端备忘里与提醒有关的字段取出来。
 */
MemoRuleSpec RuleOfItem(const net::MemoItem& item);

/**
 *  客户端备忘簿。
 */
class CMemoBook
{
public:
    CMemoBook();

    /**
     *  登记本人 userid（登录时调用），据此判断一条群备忘是否提醒到本人。Clear 时清零。
     */
    void SetSelfId(int64_t selfId);

    /**
     *  这条备忘是否提醒到本人：个人备忘一律是；群备忘在提醒全体成员、或本人被指定时是。
     */
    bool IsRemindedToSelf(const net::MemoItem& item) const;

    /**
     *  用全量拉取的结果整体替换（登录或重连后的 2401 应答）。替换之后角标清零：全量拉回来的不算「新到」。
     *    items：本人可见的全部备忘。
     */
    void ReplaceAll(const std::vector<net::MemoItem>& items);

    /**
     *  新增或修改一条（2402 应答、2404 changetype=1）。
     *    item：  完整备忘；
     *    selfId：本人 userid。经推送新到、本人能看到、且最后修改人不是本人的群备忘计入该群的角标。
     *  @return 本来就有这条（即这是一次修改）返回 true
     */
    bool ApplyUpsert(const net::MemoItem& item, int64_t selfId);

    /**
     *  删除一条（2403 应答、2404 changetype=2）。没有这条时什么都不做。
     *  @return 确实删掉了一条返回 true
     */
    bool ApplyDelete(int64_t memoid);

    /**
     *  清空（注销、切换账号时调用）。
     */
    void Clear();

    /**
     *  是否已经拿到过一次全量结果。没拿到之前备忘窗口显示「正在加载」。
     */
    bool IsLoaded() const;

    /**
     *  按 ID 取一条；没有返回 NULL。返回的指针在下一次修改备忘簿之前有效。
     */
    const net::MemoItem* Find(int64_t memoid) const;

    /**
     *  全部备忘，按开始时间、ID 升序。
     */
    const std::vector<net::MemoItem>& Items() const;

    /**
     *  某个群的全部群备忘，按开始时间、ID 升序。
     *    out：[出参] 先清空再填入。
     */
    void ListGroup(int64_t groupid, std::vector<net::MemoItem>& out) const;

    /**
     *  某一天（从 1970-01-01 起的天数）里的全部事件，按时间、备忘 ID 升序。
     *    out：[出参] 先清空再填入。
     */
    void ListDayEntries(int64_t day, std::vector<MemoDayEntry>& out) const;   //只列提醒到本人的

    /**
     *  [firstDay, endDay) 里哪些天有事件，月历标圆点用。
     *    out：[出参] 先清空，再填入有事件的天数（从 1970-01-01 起）。
     */
    void CollectEventDays(int64_t firstDay, int64_t endDay, std::set<int64_t>& out) const;

    /**
     *  组装提醒调度的输入（只取提醒到本人的）。
     *    toLocalMinute：把 Unix 毫秒换算成本地分钟序号的函数（界面侧传 MemoClock 的实现，测试传假实现）；
     *    out：[出参] 先清空再填入，与 Items() 顺序一致。
     */
    void BuildScheduleInputs(LocalMinute (*toLocalMinute)(int64_t unixMs), std::vector<MemoScheduleInput>& out) const;

    /**
     *  某群「备忘」页签的角标数。
     */
    int UnseenCount(int64_t groupid) const;

    /**
     *  点开某群的「备忘」页签后清零它的角标。
     */
    void MarkGroupSeen(int64_t groupid);

private:
    //把 m_items 按开始时间、ID 排好
    void SortItems();

    std::vector<net::MemoItem> m_items;                //全部备忘，按开始时间、ID 升序
    std::map<int64_t, std::set<int64_t> > m_unseen;    //群 id → 本次登录后新到、尚未点开页签的群备忘 ID
    bool m_loaded;                                     //是否已经拿到过一次全量结果
    int64_t m_selfId;                                  //本人 userid，判断群备忘是否提醒到本人；0 表示未登记
};

} // namespace memo
