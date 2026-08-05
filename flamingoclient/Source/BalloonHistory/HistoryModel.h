/**
 *  v2 历史消息本地库——浏览状态 / 分页模型，HistoryModel.h
 *
 *  HistoryModel 持有“历史消息记录窗口”一次浏览的过滤与分页状态（会话、类型胶囊、
 *  时间档、关键字、页号、每页条数），把对 CV2MsgStore 的统计与分页查询编排起来，
 *  并对外暴露总数、页数、当前页记录，供窗口 UI 绑定。状态机 / 分页算法是纯逻辑，
 *  便于单测（见 flamingoclient/Source/Tests/HistoryModelTests.cpp）；真正的取数走
 *  注入的 CV2MsgStore。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <set>
#include <string>
#include <vector>
#include "V2MsgQuery.h"

namespace balloonhistory {

class CV2MsgStore;

//历史消息浏览模型。不持有 store（仅引用）。切换任一过滤条件都会把页号重置回第1页。
class HistoryModel
{
public:
    HistoryModel();

    //注入本地库（不持有，生命周期由调用方保证覆盖本模型）。
    //@param store  v2 本地消息库指针；可传 NULL 解绑
    void SetStore(CV2MsgStore* store);

    //绑定要浏览的会话；重置到第1页。
    //@param ctype  会话类型（1单聊 2群聊）
    //@param cid    会话ID（我方视角）：单聊=对端userid，群聊=群id
    void SetConversation(int ctype, int64_t cid);

    //设置类型筛选胶囊；重置到第1页。
    void SetTypeFilter(HistoryTypeFilter type);

    //设置时间范围档；重置到第1页。
    void SetTimeRange(HistoryTimeRange range);

    //设置自定义时间区间（[minMs,maxMs] 闭区间，Unix毫秒），并把时间档切到自定义；
    //重置到第1页。
    //@param minMs  起（含）
    //@param maxMs  止（含）
    void SetCustomRange(int64_t minMs, int64_t maxMs);

    //设置关键字（UTF-8，空串=不过滤）；重置到第1页。
    void SetKeyword(const std::string& keyword);

    //设置每页条数（须>0，否则忽略）；重置到第1页。
    void SetPageSize(int pageSize);

    //跳转到指定页（夹取到 [1, PageCount()]，按上次 Refresh 得到的总数）。
    //@param page  目标页号（从1起；越界自动夹取）
    void GoToPage(int page);

    //按当前过滤 / 分页查本地库：更新总数与当前页记录。先按新总数夹取页号（如过滤
    //后变少、当前页越界则回弹到末页），再取该页。
    //@param nowMs  当前时间（Unix毫秒），供“近N天”换算
    //@return 查询成功返回 true；未注入 store 或出错返回 false
    bool Refresh(int64_t nowMs);

    //当前过滤 / 分页状态（含 ctype/cid/type/range/keyword/page/pageSize）。
    const HistoryFilter& Filter() const;

    //上次 Refresh 得到的匹配总条数。
    int64_t Total() const;

    //总页数：max(1, ceil(total/pageSize))。空结果按 1 页计。
    int PageCount() const;

    //当前页号（从1起）。
    int CurrentPage() const;

    //当前页记录（最新在前）。
    const std::vector<HistoryRecord>& Records() const;

    //按当前过滤条件取**全部**匹配记录（不分页，时间倒序，与 QueryPage 同序），供导出用。
    //内部复用 QueryPage 逐页累加，不改当前页 / 过滤状态。
    //@param nowMs  当前时间（Unix毫秒）
    //@param out    [出参] 全部匹配记录（最新在前）；调用前会被清空
    //@return 查询成功返回 true；未注入 store 或出错返回 false
    bool QueryAll(int64_t nowMs, std::vector<HistoryRecord>& out);

    //纯函数：按总数与每页条数算页数。pageSize<=0 或 total<=0 时返回 1。
    //@param total     匹配总条数
    //@param pageSize  每页条数
    //@return 页数（>=1）
    static int ComputePageCount(int64_t total, int pageSize);

    // ---- 长消息的「展开 / 收起」状态 ----
    //
    // 消息记录窗里超过 5 行的文本消息默认折叠，点「展开」看全（见
    // docs/消息记录内容交互方案.md §5 F1）。展开态记在模型里而不是记在控件上，
    // 因为控件每次局部刷新都会重建 —— 记在控件上的话，一改筛选条件、一翻页，
    // 用户刚展开的那条又缩回去了。
    //
    // 按 <u>msgid</u> 记而不是按行下标：行下标随翻页与筛选变化，同一个下标在不同
    // 页上是不同的消息，用它记会张冠李戴。

    //某条消息当前是否处于展开态。
    //@param msgid  消息的服务器 msgid；<=0 一律返回 false（还没分配 ID 的消息不记）
    //@return 展开返回 true；默认（从未展开过）为 false
    bool IsExpanded(int64_t msgid) const;

    //设置某条消息的展开 / 收起态。
    //@param msgid     消息的服务器 msgid；<=0 直接忽略
    //@param expanded  true 展开、false 收起
    void SetExpanded(int64_t msgid, bool expanded);

    //翻转某条消息的展开态，返回翻转后的新状态。供「展开 / 收起」链接一次点击直接调用。
    //@param msgid  消息的服务器 msgid；<=0 直接忽略并返回 false
    //@return 翻转后是否为展开态
    bool ToggleExpanded(int64_t msgid);

    //清空所有展开态。换会话时调用 —— 那已经是另一批消息了，留着上一个会话的
    //展开记录既没意义，也会让集合无限长大。
    //
    //<u>改筛选条件 / 翻页时不清</u>：那还是同一批消息，用户翻回来时理应保持原样。
    void ClearExpanded();

private:
    void ResetToFirstPage();   //把页号置 1

    CV2MsgStore*               m_store;    //本地库（注入，不持有）；NULL=未注入
    HistoryFilter              m_filter;   //当前过滤与分页状态
    int64_t                    m_total;    //上次 Refresh 的匹配总数
    std::vector<HistoryRecord> m_records;  //当前页记录（最新在前）
    //当前处于展开态的消息 msgid 集合。只增不减地累积同一个会话内被展开过的消息，
    //换会话时由 SetConversation 清空。窗口级、不落盘 —— 重开窗口一律回到折叠态。
    std::set<int64_t>          m_expandedMsgids;
};

} // namespace balloonhistory
