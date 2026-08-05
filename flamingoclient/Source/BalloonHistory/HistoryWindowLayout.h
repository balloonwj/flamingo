/**
 *  历史消息记录窗口——balloonui 客户区布局种子，HistoryWindowLayout.h
 *
 *  把“历史消息记录窗口长什么样”从窗口类里抽出成一个可独立编译、可被单元测试
 *  直接调用的纯构造模块——CBalloonHistoryWindow 与 FlamingoUnit 测试工程共用本
 *  文件。整窗按设计稿（design/历史消息记录窗口1-3.png）布局：标题区、搜索行、
 *  类型筛选胶囊 + 时间下拉、消息列表、底部分页 + 导出。列表每一行按用户决定用
 *  balloonui 现成控件组合（DuiHBox + DuiAvatar + DuiLabel + DuiButton），不自绘。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证，FlamingoUnit 侧由其 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <memory>
#include <vector>
#include "../../balloonui/balloonui/DuiControl.h"
#include "HistoryView.h"   // HighlightRange

namespace balloonhistory {

//历史窗里所有“需要被窗口代码引用”的控件的数字 id。固定 id 用 5xx；页码按钮与每行
//“定位”按钮数量不定，用基址 + 序号动态生成（kIdHistoryPageNumBase / LocateBase）。
enum HistoryCtrlId : UINT
{
    kIdHistoryTitle      = 500,   //标题“消息记录 · 对话名”
    kIdHistorySubtitle   = 501,   //副标题“共 N 条消息”
    kIdHistorySearch     = 502,   //搜索框（内容 / 文件名 / @某人）
    kIdHistoryChipAll    = 510,   //类型胶囊：全部
    kIdHistoryChipText   = 511,   //类型胶囊：文字
    kIdHistoryChipImage  = 512,   //类型胶囊：图片
    kIdHistoryChipFile   = 513,   //类型胶囊：文件
    kIdHistoryChipVoice  = 514,   //类型胶囊：语音
    kIdHistoryChipLink   = 515,   //类型胶囊：链接
    kIdHistoryChipPin    = 516,   //类型胶囊：Pin（本会话被钉住的消息，不分内容类型）
    kIdHistoryTimeCombo  = 520,   //时间范围下拉（全部时间 / 近7天 / 近30天 / 自定义）
    kIdHistoryList       = 530,   //列表内容容器（DuiScrollView 内的 vbox，承载各行）
    kIdHistoryScroll     = 531,   //列表滚动视口（局部刷新时按 id 取回、SetContent 换列表）
    kIdHistoryFooterInfo = 540,   //底部左侧“共 N 条 · 第 x/y 页”
    kIdHistoryPageFirst  = 541,   //分页：首页
    kIdHistoryPagePrev   = 542,   //分页：上一页
    kIdHistoryPageNext   = 543,   //分页：下一页
    kIdHistoryPageLast   = 544,   //分页：末页
    kIdHistoryExport     = 545,   //导出按钮
    kIdHistoryPager      = 546,   //分页器容器（局部刷新时清空 + 重填页码按钮）
    kIdHistoryCustomRow    = 560, //自定义时间区间行容器（时间下拉选“自定义”时显示，否则隐藏）
    kIdHistoryCustomStartY = 561, //自定义起始 年下拉
    kIdHistoryCustomStartM = 562, //自定义起始 月下拉
    kIdHistoryCustomStartD = 563, //自定义起始 日下拉
    kIdHistoryCustomEndY   = 564, //自定义结束 年下拉
    kIdHistoryCustomEndM   = 565, //自定义结束 月下拉
    kIdHistoryCustomEndD   = 566, //自定义结束 日下拉

    //==== 以下三段是"数量不定"的按钮，各占一段连续 id ====
    //
    //三段之间留足余量，且**各自的容量单独写成常量**（见下方 kIdHistoryXxxCapacity）。
    //分发时必须按"基址 <= id < 基址 + 本段容量"判定，<u>不得</u>拿下一段的基址当上界 ——
    //历史上正是那个写法出过事：页码段原本只有 600~699 共 100 个位置，而分发处判的是
    //`id < kIdHistoryLocateBase(700)`，于是第 101 页的按钮 id 恰好是 700，被后面的
    //"定位"分支接走，点第 101 页变成"定位到本页第 0 行"。每页 8 条，800 条消息的
    //会话就能触到。
    kIdHistoryPageNumBase = 1000,  //页码按钮基址：第 p 页按钮 id = 基址 + (p-1)
    kIdHistoryLocateBase  = 3000,  //每行“定位”按钮基址：第 i 行按钮 id = 基址 + i
    kIdHistoryPinBase     = 3200,  //每行“Pin / 取消”按钮基址：第 i 行按钮 id = 基址 + i
    kIdHistoryRowBase     = 3400,  //每行行容器基址：第 i 行 id = 基址 + i（双击定位靠它认行）
    kIdHistoryExpandBase  = 3600,  //每行“展开 / 收起”链接基址：第 i 行链接 id = 基址 + i。
                                   //只有正文超过折叠行数上限的行才建这个控件，故同一页里
                                   //<u>不是每一行都有</u>——分发时按 id 反算行下标即可，
                                   //不要假设"第 i 个链接就是第 i 行"

    kIdHistoryEmptyTitle   = 580,  //空状态主文案（列表为空时才存在，供契约测试与窗口取回）
    kIdHistoryEmptySub     = 581,  //空状态副文案；副文案为空串时该控件不建

    kIdHistoryPageJump     = 570,  //“跳至第 N 页”微调框（DuiSpinBox 本体，▲▼ 点击发通知）
    kIdHistoryPageJumpEdit = 571,  //上面那个微调框内部的真 EDIT。手输入的提交要在它
                                   //失焦时自己做（见 DuiSpinBox 头注释），故单独给个 id
                                   //才认得出是谁失焦了
};

//上面三段各自的容量。改基址时务必连同这里一起看：三段区间不得重叠。
//
//页码段给到 1000：省略式分页器一次最多只渲染十来个按钮（见 BuildPagerModel），
//这个容量纯粹是兜底，保证再大的会话也不会溢出本段。
constexpr UINT kIdHistoryPageNumCapacity = 2000;
//行按钮段给到 200：远大于每页条数（当前 8，见 BalloonHistoryWindow.cpp 的 kHisPageSize），
//日后把每页条数调大也够用。
constexpr UINT kIdHistoryLocateCapacity  = 200;
constexpr UINT kIdHistoryPinCapacity     = 200;
constexpr UINT kIdHistoryRowCapacity     = 200;
constexpr UINT kIdHistoryExpandCapacity  = 200;

//列表里一行的渲染视图数据。
struct HistoryRowView
{
    CString   m_senderName;   //发送者昵称（兼作头像字形来源）
    COLORREF  m_avatarColor;  //头像底色（群聊里各发送者不同色）
    CString   m_timeText;     //时间文本（如“06-04 11:01”）
    CString   m_previewText;  //内容预览（文本 / “[图片] xxx” / “[文件] xxx”）
    //本条消息的<u>完整正文</u>，文本消息里含 /f["<id>"] 内联表情标记（由
    //FullTextFromContent 取出，未经 DigestFaceTokens 净化）。行内容块据它把表情
    //画成图、把长文折行；非文本消息与 m_previewText 相同（都是“[图片]”这类占位）。
    CString   m_fullText;
    //本条当前是否处于“展开”态（长文默认折叠、最多 5 行）。取自 HistoryModel 的
    //展开态集合，按 msgid 记 —— 局部刷新与翻页往返都不会把它冲掉。
    bool      m_expanded;
    long long m_msgid;        //服务器消息ID（“定位”跳转与 Pin / 取消 Pin 都按它定位）
    bool      m_pinned;       //本条当前是否被 Pin（决定行内那枚按钮的文案取「Pin」还是「取消」）
    //是否"折叠"：省掉头像与名字，只留预览，行高也矮一截。
    //
    //<u>当前恒为 false</u>。折叠原本是为了提高密度，但它把消息之间的边界抹掉了 ——
    //同一人连发三条看上去就是一条带换行的长消息，用户会奇怪"一条消息怎么有三个 Pin"。
    //消息记录是"按条查找"的界面，边界比密度重要。渲染分支与 ComputeCompactFlags
    //（见 HistoryView.h）都留着：折叠本身没错，日后若做"紧凑模式"开关可直接复用。
    bool      m_compact;

    //本行之前要不要插一条日期分组头，非空时即为组头文字（「今天」/「昨天」/「2026-08-02」）。
    //由 FormatDateGroupLabel 算出。分组<u>在页内做</u>、不跨页合并：翻到下一页时若首行
    //仍是同一天，那一页会再出现一次同样的组头 —— 这是分页 + 分组的固有折中，比"跨页
    //记住上一页最后一天"简单得多，也不会因翻页顺序不同而出现组头忽有忽无。
    CString   m_groupLabel;

    //本行预览文字里要高亮的字符区间（搜索命中处）。无关键字时为空。
    std::vector<HighlightRange> m_highlights;

    //行内左侧那一小块内容预览。两者都为空时不画那一块（文本等消息）。
    CString m_thumbImagePath;  //图片消息的本地缓存路径；文件不存在时画占位块，不下载
    CString m_thumbFileExt;    //文件消息的小写扩展名，如 "xlsx"；决定类型色块的颜色与字

    HistoryRowView()
        : m_avatarColor(RGB(0xF2, 0x6B, 0x3E))
        //初始化顺序须与声明顺序一致：m_expanded 声明在 m_msgid 之前。
        , m_expanded(false)
        , m_msgid(0)
        , m_pinned(false)
        , m_compact(false)
    {
    }
};

//整窗的渲染视图数据。窗口把 HistoryModel 的当前状态翻译成本结构再交给布局。
struct HistoryViewData
{
    CString m_title;        //标题“消息记录 · 苏沐辰”
    int     m_totalCount;   //匹配总条数（标题副行与底部都用）
    int     m_currentPage;  //当前页（从1起）
    int     m_pageCount;    //总页数（>=1）
    int     m_activeChip;   //当前选中的类型胶囊（取值见 HistoryTypeFilter）
    int     m_activeTimeIndex;  //时间下拉当前选中项下标（0全部 1近7天 2近30天 3近3个月
                                //4近半年 5近一年 6自定义），与 HistoryTimeRange 的枚举值一一对应
    std::vector<HistoryRowView> m_rows;   //当前页各行（最新在前）

    //列表为空时显示的两行文案（由 BuildEmptyStateText 按当前筛选条件算出）。
    //m_rows 非空时这两项不被使用。
    CString m_emptyTitle;     //主文案
    CString m_emptySubtitle;  //副文案；空串表示不显示副行

    HistoryViewData()
        : m_totalCount(0)
        , m_currentPage(1)
        , m_pageCount(1)
        , m_activeChip(0)
        , m_activeTimeIndex(0)
    {
    }
};

//构造历史窗客户区控件树（不含窗口边框，边框由 DuiFrameWindow 负责）。
//@param data  整窗渲染数据（标题 / 总数 / 当前页 / 各行 / 选中胶囊等）
//@return 客户区根控件（一个 vbox），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryWindowBody(const HistoryViewData& data);

//构造列表内容容器（DuiScrollView 的 content，id=kIdHistoryList，承载各行）。窗口在
//改筛选 / 翻页时用它重建列表、经 DuiScrollView::SetContent 局部换上，避免整窗重建。
//
//rows 为空时产出的<u>不是</u>一个空容器，而是一块居中的空状态（主 + 副两行文案）——
//筛选或搜索到 0 条时若只留一片白，看着更像卡住了而不是"没有结果"。
//
//@param rows          当前页各行
//@param emptyTitle    列表为空时的主文案（rows 非空时忽略）
//@param emptySubtitle 列表为空时的副文案；空串表示不显示副行
//@param listWidth     列表（滚动视口内容）的可用宽度（像素）。<u>行高按正文在这个宽度
//                     下折出几行来算</u>，故传错了行高就会偏。窗口侧传滚动视口的实际
//                     宽度；<=0 时用一个中等窗口的典型宽度兜底（整窗首次构建、以及
//                     单元测试直接调用时用得上）
//@return 列表内容 vbox，调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildListContent(const std::vector<HistoryRowView>& rows,
                                                          LPCTSTR emptyTitle,
                                                          LPCTSTR emptySubtitle,
                                                          int listWidth = 0);

//把分页按钮（首页 < 1 2 … > 末页）填进给定的分页器容器。窗口在翻页 / 改筛选导致页数
//变化时，先清空分页器容器再调本函数重填，避免整窗重建。
//@param pager        分页器容器（一个 hbox，调用方负责先清空其旧子控件）
//@param currentPage  当前页（从1起，用于高亮）
//@param pageCount    总页数（>=1）
void PopulatePagerButtons(balloonwjui::DuiControl* pager, int currentPage, int pageCount);

//用一组假数据填充 view（供 --history 调试入口与布局契约测试，不连真实数据）。
//@param data  [出参] 被填入标题、若干行与分页信息的渲染数据
void FillMockHistory(HistoryViewData& data);

} // namespace balloonhistory
