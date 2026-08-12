/**
 *  「消息盒子」窗口——balloonui 客户区布局种子，AnnounceBoxLayout.h
 *
 *  把"消息盒子长什么样"从窗口类里抽出成一个可独立编译、可被单元测试直接调用的纯
 *  构造模块——CBalloonAnnounceBoxWindow 与 FlamingoUnit 测试工程共用本文件（与
 *  HistoryWindowLayout / BalloonMainLayout 同约定）。
 *
 *  整窗为左右双栏（与 flamingoAdmin 公告中心的形制呼应，2026-08-05 与用户确认）：
 *    ┌──────────────────┬───────────────────────────┐
 *    │ 共 N 条 · N 未读 │  <公告标题>               │
 *    │ ┌──────────────┐ │  发布者 · 时间            │
 *    │ │● 标题        │ │  ─────────────            │
 *    │ │  发布者·时间 │ │  正文……                   │
 *    │ ├──────────────┤ │                           │
 *    │ │  ……          │ │                           │
 *    │ └──────────────┘ │                           │
 *    ├──────────────────┴───────────────────────────┤
 *    │                            [全部标为已读]     │
 *    └───────────────────────────────────────────────┘
 *
 *  列表行用 AnnounceRow（DuiHBox 子类，只补悬停 / 选中态与未读点），左右两栏的滚动
 *  视口用 balloonmain::MainPanelScrollView（DuiScrollView 子类，滚动条悬浮、滚动时
 *  淡入、空闲后淡出），其余一律用 balloonui 现成控件（DuiVBox / DuiHBox / DuiLabel /
 *  DuiButton / DuiSeparator）组合，不自绘。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证，FlamingoUnit 侧由其 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <memory>
#include <vector>
#include <stdint.h>

#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonwjui { class DuiScrollView; }
namespace balloonhistory { class HistoryContentView; }

namespace balloonannounce {

/**
 *  消息盒子窗里所有"需要被窗口代码引用"的控件的数字 id。
 *
 *  固定控件用 7xx；列表行数量不定，用基址 + 序号动态生成（kIdAnnRowBase）。
 *  与历史窗的 5xx / 6xx / 1000+ / 3000+ 各段错开，两窗的 id 互不干扰。
 */
enum AnnounceBoxCtrlId : UINT
{
    kIdAnnSummary      = 700,   //左栏顶部「共 N 条 · N 条未读」
    kIdAnnScroll       = 701,   //左栏列表滚动视口（局部刷新时按 id 取回、SetContent 换列表）
    kIdAnnList         = 702,   //左栏列表内容容器（DuiScrollView 内的 vbox，承载各行）

    kIdAnnDetailTitle  = 710,   //右栏：公告标题
    kIdAnnDetailMeta   = 711,   //右栏：发布者 · 时间
    kIdAnnDetailBody   = 712,   //右栏：正文
    kIdAnnDetailScroll = 713,   //右栏：正文滚动视口

    kIdAnnMarkAllRead  = 720,   //底部「全部标为已读」按钮

    kIdAnnEmptyTitle   = 730,   //左栏空状态主文案（一条公告都没有时才存在）
    kIdAnnEmptySub     = 731,   //左栏空状态副文案；副文案为空串时该控件不建

    //列表行容器基址：第 i 行（从 0 起）的 id = 基址 + i。窗口按 id 认出点的是哪一行。
    kIdAnnRowBase      = 800,
};

//行 id 段的容量。判定"是不是行"时必须按 `基址 <= id < 基址 + 容量`，
//<u>不得</u>拿下一段的基址当上界 —— 历史窗那边正是这个写法出过事（见 HistoryCtrlId 注释）。
//消息盒子一次全量渲染所有公告（不分页），给到 2000 作兜底。
constexpr UINT kIdAnnRowCapacity = 2000;

//列表里一条公告的渲染视图数据。
struct AnnounceRowView
{
    int64_t m_announcementId;  //公告ID（窗口据它回查正文、发已读上报）
    CString m_title;           //公告标题
    CString m_metaText;        //「发布者 · 时间」合成的一行，如「平台运营 · 08-05 14:30」
    int     m_severity;        //级别，取值见 net/Protocol.h 的 announcement_severity
    bool    m_pinned;          //是否置顶（置顶的排在最前，标题前加标记）
    bool    m_unread;          //是否未读（画标题左侧的小圆点）

    AnnounceRowView()
        : m_announcementId(0)
        , m_severity(0)
        , m_pinned(false)
        , m_unread(false)
    {
    }
};

//整窗的渲染视图数据。窗口把本地公告库的查询结果翻译成本结构再交给布局。
struct AnnounceBoxViewData
{
    int m_totalCount;    //公告总条数（左栏顶部信息用）
    int m_unreadCount;   //未读条数（左栏顶部信息用；为 0 时不显示「N 条未读」那一截）

    std::vector<AnnounceRowView> m_rows;   //全部公告，已按「置顶优先、时间倒序」排好

    //当前选中行的下标（0 起）；-1 表示没有选中任何一行（右栏显示占位提示）。
    int m_selectedIndex;

    //右栏三项。m_selectedIndex 为 -1 时前两项被忽略，正文位置改显示占位提示。
    CString m_detailTitle;   //公告标题
    CString m_detailMeta;    //「发布者 · 时间」
    CString m_detailBody;    //公告正文

    //左栏一条公告都没有时显示的两行文案。m_rows 非空时这两项不被使用。
    CString m_emptyTitle;     //主文案
    CString m_emptySubtitle;  //副文案；空串表示不显示副行

    AnnounceBoxViewData()
        : m_totalCount(0)
        , m_unreadCount(0)
        , m_selectedIndex(-1)
    {
    }
};

/**
 *  构造消息盒子窗客户区控件树（不含窗口边框——边框由 DuiFrameWindow 负责）。
 *    data：整窗渲染数据。
 *  @return 客户区根控件（一个 vbox），调用方拿到所有权。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildAnnounceBoxBody(const AnnounceBoxViewData& data);

/**
 *  构造左栏列表内容容器（DuiScrollView 的 content，id=kIdAnnList，承载各行）。
 *  窗口在收到新公告 / 撤回 / 选中变化时用它重建列表、经 DuiScrollView::SetContent
 *  局部换上，避免整窗重建。
 *
 *  rows 为空时产出的<u>不是</u>一个空容器，而是一块居中的空状态（主 + 副两行文案）
 *  ——一条公告都没有时若只留一片白，看着更像卡住了而不是"确实没有公告"。
 *
 *    rows          全部公告行。
 *    selectedIndex 当前选中行下标；越界或 -1 表示无选中。
 *    emptyTitle    列表为空时的主文案（rows 非空时忽略）。
 *    emptySubtitle 列表为空时的副文案；空串表示不显示副行。
 *  @return 列表内容 vbox，调用方拿到所有权。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildAnnounceListContent(
    const std::vector<AnnounceRowView>& rows,
    int selectedIndex,
    LPCTSTR emptyTitle,
    LPCTSTR emptySubtitle);

/**
 *  把「哪一行选中 / 哪些行未读 / 顶部计数 / 底部按钮可用性」落到<u>既有</u>控件上。
 *
 *  点选一条公告、或把公告标为已读时用它代替整棵控件树重建：这些场景下行的<u>集合</u>
 *  没有变，变的只是每行的状态与两处文案，重建整棵树既费时（要重查本地库、重新排版、
 *  重新测正文高度），又会把列表的滚动位置清零 —— 用户点了靠下的一行，列表却跳回顶部。
 *
 *  行的集合真的变了（收到新公告 / 公告被撤回）时本函数不适用，那种情况仍须重建列表内容
 *  （见 BuildAnnounceListContent），因为要新增 / 删除行控件。
 *
 *    root：客户区根控件（BuildAnnounceBoxBody 的产物）；为空时直接返回。
 *    data：当前整窗渲染数据；其中只有 m_rows 的未读位、m_selectedIndex、
 *          m_totalCount、m_unreadCount 会被用到。
 *  副作用：状态确有变化的控件会被标脏重绘。不碰任何一栏的滚动位置。
 */
void ApplyAnnounceListState(balloonwjui::DuiControl* root, const AnnounceBoxViewData& data);

/**
 *  把右栏三项（标题 / 发布者·时间 / 正文）换成当前选中的那一条，重算正文滚动范围，
 *  并把正文滚回顶部。同样不重建控件树。
 *
 *  正文滚回顶部是有意为之：上一条公告看到一半的位置对新选中的这条没有意义，
 *  留在那儿只会让人以为开头的内容丢了。
 *
 *    root：客户区根控件；为空时直接返回。
 *    data：当前整窗渲染数据；用到 m_selectedIndex 与 m_detailTitle / m_detailMeta /
 *          m_detailBody。m_selectedIndex 为 -1 时标题与 meta 置空、正文显示占位提示。
 *  须在布局落定之后调用（要按视口实际宽度算正文高度）。
 */
void ApplyAnnounceDetail(balloonwjui::DuiControl* root, const AnnounceBoxViewData& data);

/**
 *  按右栏滚动视口的实际宽度算出正文折行后的高度，喂给视口并让它重新排一次。
 *
 *  正文是一个多行内联文本控件，它折行后有多高只有拿到<u>实际宽度</u>才问得出
 *  （HistoryContentView::MeasureHeight）。滚动视口的内容高度就是这个值——不设的话
 *  视口以为内容只有一屏高，长公告滚不下去。
 *
 *  设完高度还必须让视口重排一次：DuiScrollView::SetContentHeight 只更新滚动范围，
 *  <u>不</u>重算内容控件的名义矩形，而那个矩形是上一次布局按当时的内容高（建窗时
 *  还是 0）算的，只有一个视口那么高。不补这一下，正文滚过一屏后整块内容矩形就跑到
 *  视口上方，DuiScrollView::OnPaint 里那句 IntersectRect 不成立，正文会整块不画
 *  ——表现为"往下滚着滚着正文突然没了"。
 *
 *  须在布局完成之后调用（此时控件的 GetRect 才有效）：建窗后、每次局部刷新后、
 *  以及窗口尺寸变化后各调一次。
 *
 *    scroll：右栏正文滚动视口；为空时静默返回。
 *    body：  右栏正文控件；为空时静默返回。
 *  @return 算得并已设进视口的内容总高（像素）；控件为空或布局尚未落定（视口矩形
 *          为空）时返回 0，且不改动任何状态。
 */
int SyncAnnounceDetailBodyHeight(balloonwjui::DuiScrollView* scroll,
                                 balloonhistory::HistoryContentView* body);

/**
 *  把某行的 id 反解成行下标。
 *    ctrlId：被点击控件的 id。
 *  @return 落在行 id 段内时返回下标（0 起）；不是行 id 时返回 -1。
 */
int RowIndexFromCtrlId(UINT ctrlId);

/**
 *  把公告的发布时间（Unix 毫秒）格式化成展示文本。
 *
 *  今年的公告显示「MM-DD HH:MM」，往年的带上年份「YYYY-MM-DD HH:MM」——
 *  跨年之后光看月日会分不清是哪一年的。
 *
 *  放在本模块而不是各窗口自己写一份：消息盒子列表与新公告提示窗都要显示时间，
 *  两处口径必须一致，否则同一条公告在两个地方显示的时间不一样。
 *
 *    timestampMs：Unix 毫秒；<= 0 或无法转换时返回空串。
 *  @return 展示文本（UTF-16）。
 */
CString FormatAnnounceTime(int64_t timestampMs);

} // namespace balloonannounce
