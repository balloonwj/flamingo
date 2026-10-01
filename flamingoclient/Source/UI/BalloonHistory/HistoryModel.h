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

//「跳到某一天」的结果
enum HistoryDateJumpResult
{
    kHistoryDateJumpExact      = 0,  //那天有消息：已翻到那天最晚的一条所在的页
    kHistoryDateJumpEarlier    = 1,  //那天没有消息：已翻到那天之前最近的一条所在的页
    kHistoryDateJumpNoneBefore = 2,  //那天及以前都没有消息（在当前筛选条件下）：已翻到末页
    kHistoryDateJumpFailed     = 3   //未注入本地库或查询出错：页号不变
};

//「跳到某一天」的结果与翻到的那一条
struct HistoryDateJump
{
    int     m_result;  //取值见 HistoryDateJumpResult
    int64_t m_msgid;   //翻到的那一条消息的 msgid（窗口据此高亮那一行）；之前没有或失败时为 0

    HistoryDateJump()
        : m_result(kHistoryDateJumpFailed)
        , m_msgid(0)
    {
    }
};

//算出本地时间某一天的起止：当天 0 点与 23:59:59.999 对应的 Unix 毫秒。
//@param year / month / day  日期（month 1~12，day 1~31）
//@param outStartMs  [出参] 当天 0 点
//@param outEndMs    [出参] 当天最后一毫秒（次日 0 点减 1）
//@return 日期合法返回 true；不存在的日期（如 2 月 30 日、13 月）返回 false，出参不可用
bool LocalDayBoundsMs(int year, int month, int day, int64_t& outStartMs, int64_t& outEndMs);

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

    //设置发送人筛选：只看这个 userid 发的消息；重置到第1页。
    //@param fromid  发送人 userid；0 或负数表示不限
    void SetSenderFilter(int64_t fromid);

    //按当前筛选条件，算出时间与 msgid 为给定值的那条消息落在第几页（排序同 QueryPage）。
    //@param nowMs      当前时间（Unix毫秒），供"近N天"换算
    //@param timestamp  那条消息的时间（Unix毫秒）
    //@param msgid      那条消息的 msgid
    //@return 页号（从1起）；未注入 store 或查询出错返回 0
    int PageOfPosition(int64_t nowMs, int64_t timestamp, int64_t msgid);

    //查看上下文：清掉关键字、类型、时间与发送人筛选（会话、每页条数与显示下界不变），翻到
    //rec 在全量列表里所在的那一页并重新统计、取页。
    //@param nowMs  当前时间（Unix毫秒）
    //@param rec    要查看上下文的那条消息（取它的时间与 msgid）
    //@return 刷新成功且当前页里确实包含这条消息时返回 true；否则返回 false（例如它在覆盖区间
    //        以外，全量列表里看不到它），此时筛选条件已经清掉、停在第1页
    bool ShowContextOf(int64_t nowMs, const HistoryRecord& rec);

    //跳到某一天：在当前筛选结果里，翻到那天最晚的一条消息所在的页（那天没有就取之前最近的
    //一条；之前也没有就翻到末页），并取好那一页。筛选条件不变。
    //@param nowMs       当前时间（Unix毫秒），供"近N天"换算
    //@param dayStartMs  那天 0 点（Unix毫秒，见 LocalDayBoundsMs）
    //@param dayEndMs    那天最后一毫秒
    //@return 结果与翻到的那一条，见 HistoryDateJump
    HistoryDateJump JumpToDate(int64_t nowMs, int64_t dayStartMs, int64_t dayEndMs);

    //取当前会话里发过言的人，供「发送人」下拉框使用。按当前的显示下界（覆盖区间）统计，
    //与不加筛选时列表里看得到的消息一致；类型、时间、关键字、发送人筛选都不影响它。
    //@param out  [出参] 发送人 userid，最近发过言的在前；调用前会被清空
    //@return 查询成功返回 true（即使一个人都没有）；未注入 store 或出错返回 false
    bool QuerySenders(std::vector<int64_t>& out) const;

    //把搜索条件恢复为初始状态：关键字与发送人清空，时间范围回到「全部时间」（自定义起止一并
    //清零），并回到第1页。会话、类型胶囊、每页条数、显示下界都不变。
    //消息记录窗重新打开、或打开着时换到别的会话时调用（何时调用见 ShouldResetHistorySearch）。
    //窗口关闭后本模型仍然保留，不在这些时机清空，新打开的窗口搜索框是空的、列表却仍按
    //上次的关键字筛选。
    void ResetSearchFilters();

    //设置每页条数（须>0，否则忽略）；重置到第1页。
    void SetPageSize(int pageSize);

    //设置显示下界 msgid：只数、只查 msgid 不小于它的消息；0 表示不限制。由消息记录窗按
    //该会话的覆盖区间算出（HistoryCoverage.h 的 CoverageMinVisibleMsgid），Pin 分类不受限制。
    //<u>不</u>重置页号：区间往更早的方向扩展时只是在列表末尾多出旧记录，用户停留的那一页
    //内容不变，不该被拉回第 1 页。
    //@param minMsgid  显示下界；负数按 0 处理
    void SetMinMsgid(int64_t minMsgid);

    //跳转到指定页（夹取到 [1, PageCount()]，按上次 Refresh 得到的总数）。
    //@param page  目标页号（从1起；越界自动夹取）
    void GoToPage(int page);

    //按当前过滤 / 分页查本地库：更新总数与当前页记录。先按新总数夹取页号（如过滤
    //后变少、当前页越界则回弹到末页），再取该页。
    //@param nowMs  当前时间（Unix毫秒），供“近N天”换算
    //@return 查询成功返回 true；未注入 store 或出错返回 false
    bool Refresh(int64_t nowMs);

    //只换页时用的刷新：筛选条件（除页号、每页条数以外的全部字段）与上次统计时相同，就沿用
    //上次统计出的总数，只取当前页；筛选条件变了则与 Refresh 相同，重新统计。
    //统计条数在消息多的会话上很贵（10 万条时约 100 毫秒，见 V2MsgStoreTests 的 HS2），而翻页
    //并不改变总数。<u>数据可能变化时</u>（新消息、删除、往前拉取、Pin 变化）必须用 Refresh ——
    //本函数不会察觉库里的变化。页号仍按（沿用的）总数夹取。
    //@param nowMs  当前时间（Unix毫秒），供"近N天"换算
    //@return 查询成功返回 true；未注入 store 或出错返回 false
    bool RefreshPage(int64_t nowMs);

    //自创建以来执行过多少次统计查询（Count）。供单元测试核对翻页时确实没有重新统计。
    int64_t CountQueryTimes() const;

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

    //按 m_total 把页号夹到 [1, PageCount()]。Refresh 与 RefreshPage 共用。
    void ClampPageToTotal();

    //两组筛选条件统计出的总数是否必然相同：只比较影响总数的字段（页号、每页条数除外）。
    static bool SameCountFilter(const HistoryFilter& a, const HistoryFilter& b);

    CV2MsgStore*               m_store;    //本地库（注入，不持有）；NULL=未注入
    HistoryFilter              m_filter;   //当前过滤与分页状态
    int64_t                    m_total;    //上次 Refresh 的匹配总数
    std::vector<HistoryRecord> m_records;  //当前页记录（最新在前）
    //上次统计总数时的筛选条件；RefreshPage 据它判断筛选条件是否变过（只比较影响总数的字段）
    HistoryFilter              m_countedFilter;
    //m_total 是否来自一次成功的统计。刚创建、换了库时为 false，RefreshPage 此时必须重新统计
    bool                       m_countValid;
    //执行过的统计查询次数，只增不减，见 CountQueryTimes
    int64_t                    m_countQueries;
    //当前处于展开态的消息 msgid 集合。只增不减地累积同一个会话内被展开过的消息，
    //换会话时由 SetConversation 清空。窗口级、不落盘 —— 重开窗口一律回到折叠态。
    std::set<int64_t>          m_expandedMsgids;
};

//打开消息记录窗时，是否要把搜索条件（关键字、时间范围）恢复为初始状态。
//@param windowOpen        打开之前窗口是否已经开着
//@param sameConversation  要打开的会话与模型里当前的会话是否相同
//@return 窗口原本没开，或开着但换了会话时返回 true；开着且是同一会话时返回 false ——
//        此时搜索框里看得到当前关键字，界面与筛选条件一致，保留用户正在进行的搜索
bool ShouldResetHistorySearch(bool windowOpen, bool sameConversation);

//消息记录窗换完列表后，要不要把列表滚回顶部（DuiScrollView 换内容时保留原滚动位置）。
//@param shown  上一次显示列表时的筛选条件（含页号）
//@param now    这一次的筛选条件
//@return 页号、每页条数，或会话、类型、时间范围（含自定义起止）、关键字、发送人任一项变了时返回
//        true —— 新列表是另一批消息，应从头看起。完全相同（同一页因图片下载完成等原因重画），
//        或只有显示下界（覆盖区间，往前拉取到更早消息时由程序改动）变了时返回 false，保持用户
//        当前的滚动位置
bool ShouldResetListScroll(const HistoryFilter& shown, const HistoryFilter& now);

//右键菜单里要不要出现「查看上下文」。
//@param filter  当前的筛选条件
//@param msgid   这一行的服务器 msgid；0 表示尚未分配
//@return 当前有筛选条件（关键字、非「全部」的类型、时间范围、发送人任一项）、msgid 已分配、
//        且这条消息在覆盖区间以内（全量列表里看得到它）时返回 true。没有筛选条件时这一行
//        本来就在上下文里，不需要这一项
bool ShouldOfferViewContext(const HistoryFilter& filter, int64_t msgid);

} // namespace balloonhistory
