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
#include <string>
#include <vector>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "HistoryView.h"   // HighlightRange
#include "../BalloonChatWnd/ChatMockData.h"   // balloonchatwnd::ChatMessage（聊天文件卡片的数据）
#include "../BalloonChatWnd/ChatMarkdownCard.h"   // balloonchatwnd::MarkdownCardInfo（Markdown 卡片的数据）
#include "../../Common/I18n/I18n.h"   // TR_NOOP：界面文字的多语言标记

namespace balloonchatwnd { class ChatVoiceStrip; }

namespace balloonhistory {

//历史窗里所有“需要被窗口代码引用”的控件的数字 id。固定 id 用 5xx；页码按钮与每行
//“定位”按钮数量不定，用基址 + 序号动态生成（kIdHistoryPageNumBase / LocateBase）。
enum HistoryCtrlId : UINT
{
    kIdHistoryTitle      = 500,   //标题区左侧的会话名（聊天记录管理器左栏切换会话时更新；名字为空时宽度为 0）
    kIdHistorySubtitle   = 501,   //副标题“共 N 条消息”
    kIdHistorySearch     = 502,   //搜索框（内容 / 文件名 / @某人）
    kIdHistoryWildcard   = 503,   //搜索框右侧的「通配符」复选框（默认不勾；勾选后 * ? 是通配符，docs/聊天记录管理器方案.md §11.4）
    kIdHistoryChipAll    = 510,   //类型胶囊：全部
    kIdHistoryChipText   = 511,   //类型胶囊：文字
    kIdHistoryChipImage  = 512,   //类型胶囊：图片
    kIdHistoryChipFile   = 513,   //类型胶囊：文件
    kIdHistoryChipVoice  = 514,   //类型胶囊：语音
    kIdHistoryChipLink   = 515,   //类型胶囊：链接
    kIdHistoryChipPin    = 516,   //类型胶囊：Pin（本会话被钉住的消息，不分内容类型）
    kIdHistoryTimeCombo  = 520,   //时间范围下拉（全部时间 / 近7天 / 近30天 / 近3个月 / 近半年 / 近一年）
    kIdHistorySenderCombo = 521,  //发送人下拉（搜索框右侧）：第 0 项「全部成员」，其余各项由窗口按本地库里发过言的人填入
    //522 原为「跳到日期」按钮，2026-10-01 与时间下拉里的「自定义」合并为「选日期」后删除，不再复用
    kIdHistoryDatePick       = 523,  //「选日期」按钮（时间下拉右侧）：打开「从 / 到」日期弹层，可跳到某天或只看某段时间
    kIdHistoryDateRangeChip  = 524,  //日期区间标签容器：「只看这段时间」生效后替下时间下拉显示，否则隐藏
    kIdHistoryDateRangeLabel = 525,  //日期区间标签里的日期文字（可点，重新打开「选日期」弹层修改）
    kIdHistoryDateRangeClear = 526,  //日期区间标签里的关闭叉号：清掉日期筛选，回到「全部时间」
    kIdHistoryFilterRow      = 527,  //筛选行容器（类型胶囊 + 时间区域）；切换时间下拉与日期区间标签后按它重排
    kIdHistoryHeaderRow      = 528,  //标题区那一行（「»」按钮 + 会话名 + 「共 N 条消息」）；会话名或「»」变化后按它重排
    kIdHistoryList       = 530,   //列表内容容器（DuiScrollView 内的 vbox，承载各行）
    kIdHistoryScroll     = 531,   //列表滚动视口（局部刷新时按 id 取回、SetContent 换列表）
    kIdHistoryFooterInfo = 540,   //底部左侧“共 N 条 · 第 x/y 页”
    kIdHistoryPageFirst  = 541,   //分页：首页
    kIdHistoryPagePrev   = 542,   //分页：上一页
    kIdHistoryPageNext   = 543,   //分页：下一页
    kIdHistoryPageLast   = 544,   //分页：末页
    kIdHistoryExport     = 545,   //导出按钮
    kIdHistoryPager      = 546,   //分页器容器（局部刷新时清空 + 重填页码按钮）
    kIdHistoryClearConversation = 547,   //「清空聊天记录」按钮（「导出」左边）：清空当前查看的会话。2026-10-03 起从
                                         //聊天窗头部移到这里；调试入口没有主面板，不显示（见 HistoryViewData）
    //560～566 原为自定义时间区间那一行（行容器与起止年月日六个下拉），2026-10-01 改由「选日期」
    //弹层设置起止后删除，不再复用

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
    kIdHistoryThumbBase   = 3800,  //每行图片缩略图基址：第 i 行缩略图 id = 基址 + i。只有图片行
                                   //有（文件行的类型色块不可点击，不给 id）；单击可下载原图
    kIdHistoryPinMarkBase = 4000,  //每行时间右侧图钉的基址：第 i 行图钉 id = 基址 + i。只有已 Pin
                                   //的行有图钉；单击即取消 Pin（与行内「取消」按钮效果相同）
    kIdHistoryCardBase    = 4200,  //每行"聊天记录卡片"标题链接的基址：第 i 行 id = 基址 + i。只有
                                   //合并转发的聊天记录行有；单击打开聊天记录查看窗
    kIdHistoryFileCardBase = 4400, //每行"聊天文件卡片"（ChatFileCardControl）的基址：第 i 行 id =
                                   //基址 + i。只有 m_chatFileCard 为 true 的行有（聊天记录查看窗的
                                   //文件行）；宿主按它找到卡片、就地更新下载进度
    kIdHistorySelectBase   = 4600, //多选模式下每行行首复选框的基址：第 i 行 id = 基址 + i。只有多选
                                   //模式下才建；勾选变化时发 DUIN_VALUECHANGED，窗口据 id 反算行下标
    kIdHistoryVoiceBase    = 5000, //每行语音条（ChatVoiceStrip）的基址：第 i 行 id = 基址 + i。只有语音行有；宿主按它找到
                                   //语音条、就地更新播放与下载状态（语音消息第 4 期）
    kIdHistoryGridBase     = 4800, //「图片」分类网格里第 i 张缩略图的 id = 基址 + i（i 为当前页记录下标）。
                                   //单击看大图（本地没有时先下载），右键弹与列表行相同的菜单

    kIdHistorySelectBar    = 590,  //多选操作栏容器（平时隐藏，进入多选后替换底部分页栏显示）
    kIdHistorySelectInfo   = 591,  //多选操作栏左侧「已选 N 条」
    kIdHistorySelectMerge  = 592,  //多选操作栏「合并转发」：选中的消息合并成一条聊天记录转发
    kIdHistorySelectEach   = 593,  //多选操作栏「逐条转发」：选中的消息逐条转发
    kIdHistorySelectDelete = 594,  //多选操作栏「删除」：确认后逐条删除选中的消息
    kIdHistorySelectCancel = 595,  //多选操作栏「取消」：退出多选模式，清空选择
    kIdHistoryBatchDownload = 596, //「文件」分类列表顶部的「全部下载」按钮：下载本页本地没有的文件
    kIdHistoryBatchInfo    = 597,  //上面那个按钮左侧的「本页有 N 个文件还没有下载」
    kIdHistoryBody         = 598,  //BuildHistoryWindowBody 造的整套右侧界面的根容器。聊天记录管理器把它放进左右分栏的
                                   //右半边，窗口据此找回它（提示条挂在它下面、显隐变化后按它重排）
    kIdHistoryExpandLeft   = 599,  //标题区最左边的「»」按钮：聊天记录管理器左栏折叠后出现，点它重新展开左栏

    kIdHistoryEmptyTitle   = 580,  //空状态主文案（列表为空时才存在，供契约测试与窗口取回）
    kIdHistoryEmptySub     = 581,  //空状态副文案；副文案为空串时该控件不建

    kIdHistoryPageJump     = 570,  //“跳至第 N 页”微调框（DuiSpinBox 本体，▲▼ 点击发通知）
    kIdHistoryPageJumpEdit = 571,  //上面那个微调框内部的输入框子控件。手输入的提交要在它
                                   //失焦时自己做（见 DuiSpinBox 头注释），故单独给个 id
                                   //才认得出是谁失焦了
    kIdHistoryPageJumpLabel = 572, //微调框左边的「跳至」文字：窗口窄到放不下时与微调框、「页」整组隐藏
    kIdHistoryPageJumpUnit  = 573, //微调框右边的「页」文字，同上

    //==== 时间轴模式（docs/聊天记录管理器方案.md §11.7），编号接在文件 / 图片页（kIdLib*，到 7399）之后 ====
    kIdHistoryViewList     = 7400, //搜索行最右边的「列表」：普通列表，有关键字时只显示命中的消息（默认）
    kIdHistoryViewTimeline = 7401, //「时间轴」：列表改成时间轴样式、右侧出现导航条，关键字只高亮不筛选
    kIdHistoryListHost     = 7402, //列表滚动视口与导航条所在的那一行；导航条显隐后按它重排
    kIdHistoryNav          = 7403, //列表右侧的时间轴导航条（平时隐藏）
    kIdHistoryNavScroll    = 7404, //导航条的滚动视口
    kIdHistoryNavList      = 7405, //导航条滚动视口里的内容容器（各年、各月）
    kIdHistoryHitBar       = 7410, //筛选行下面的命中条（时间轴模式且有关键字时显示）
    kIdHistoryHitInfo      = 7411, //命中条左侧「共 N 处命中…」
    kIdHistoryHitPos       = 7412, //命中条右侧「第 x / N 处」
    kIdHistoryHitPrev      = 7413, //命中条「▲ 上一处」
    kIdHistoryHitNext      = 7414, //命中条「▼ 下一处」
    kIdHistoryNavMonthBase = 7500, //导航条第 i 个月份行的编号 = 基址 + i（i 按导航数据里各年各月依次编号）
    kIdHistoryNavYearBase  = 8100, //导航条第 i 年标题行的编号 = 基址 + i
    kIdHistoryCodeCardBase = 8200, //每行代码片段卡片（ChatCodeCardControl）的编号 = 基址 + i（i 为行下标）。只有代码片段行有
                                   //（docs/代码片段方案.md 第 3.4 节）；卡片自己处理点击，编号用于按行找回卡片
    kIdHistoryMarkdownCardBase = 11200, //每行 Markdown 卡片（ChatMarkdownCardControl）的编号 = 基址 + i（i 为行下标）。只有
                                        //Markdown 消息行有（docs/Markdown消息方案.md 第 12 节）；卡片自己处理点击，编号用于按行
                                        //找回卡片。取在导出窗会话勾选段（9000 起，容量 2000）之后，不与已有的任何一段相交
    kIdHistoryCardThumbBase = 11400, //帖子卡片与补充卡片下的缩略图（docs/帖子群方案.md 14.5）：第 i 行第 k 张的编号 =
                                     //基址 + i × kHistoryCardMaxThumbs + k。只有带图片的帖子卡片行有；单击看图（本机没有时先下载），
                                     //接在 Markdown 卡片段（到 11399）之后
};

//时间轴导航条两段编号的容量：月份行最多 50 年，年标题行最多 100 年
constexpr UINT kIdHistoryNavMonthCapacity = 600;
constexpr UINT kIdHistoryNavYearCapacity  = 100;

//上面三段各自的容量。改基址时务必连同这里一起看：三段区间不得重叠。
//
//页码段给到 1000：省略式分页器一次最多只渲染十来个按钮（见 BuildPagerModel），
//这个容量纯粹是兜底，保证再大的会话也不会溢出本段。
constexpr UINT kIdHistoryPageNumCapacity = 2000;
//行按钮段给到 200：远大于每页条数（kHistoryPageSize，当前 30），日后把每页条数调大也够用。
constexpr UINT kIdHistoryLocateCapacity  = 200;
constexpr UINT kIdHistoryPinCapacity     = 200;
constexpr UINT kIdHistoryRowCapacity     = 200;
constexpr UINT kIdHistoryExpandCapacity  = 200;
constexpr UINT kIdHistoryThumbCapacity   = 200;
constexpr UINT kIdHistoryPinMarkCapacity = 200;
constexpr UINT kIdHistoryCardCapacity    = 200;
constexpr UINT kIdHistoryFileCardCapacity = 200;
constexpr UINT kIdHistorySelectCapacity   = 200;
constexpr UINT kIdHistoryGridCapacity     = 200;
constexpr UINT kIdHistoryVoiceCapacity    = 200;
constexpr UINT kIdHistoryCodeCardCapacity = 200;
constexpr UINT kIdHistoryMarkdownCardCapacity = 200;
//帖子卡片缩略图段：200 行、每行最多 kHistoryCardMaxThumbs 张
constexpr UINT kIdHistoryCardThumbRowCapacity = 200;
constexpr UINT kIdHistoryCardThumbCapacity = kIdHistoryCardThumbRowCapacity * (UINT)kHistoryCardMaxThumbs;

//帖子卡片缩略图的边长上限（像素）：按原图宽高等比缩放进这个方框，小图不放大；宽高未知时取这个边长的正方形
constexpr int kHistoryCardThumbPx = 72;
//帖子卡片缩略图之间的距离（像素）
constexpr int kHistoryCardThumbGap = 6;

/**
 *  算出帖子卡片下一张缩略图的显示尺寸：按原图宽高等比缩放进 kHistoryCardThumbPx 见方的方框，小图不放大；
 *  宽高任一不为正时取方框本身。
 *    realW / realH：原图的像素宽高（取自卡片的 images[]）。
 *    outW / outH：  [出参] 缩略图的宽高（像素，恒 >= 1）。
 */
void ComputeHistoryCardThumbSize(int realW, int realH, int& outW, int& outH);

/**
 *  把帖子卡片缩略图的点击解析成「当前页第几行、第几张」。
 *    id：      DUIN_CLICK 通知里的 ctrlId。
 *    rowOut：  [出参] 行下标；返回 false 时不修改。
 *    thumbOut：[出参] 这一行的第几张（0 起）；返回 false 时不修改。
 *  @return id 落在 kIdHistoryCardThumbBase 段内返回 true；其它 id 返回 false，调用方据此继续判断别的分支
 */
bool ResolveHistoryCardThumbClick(UINT id, int& rowOut, int& thumbOut);

//发送人下拉第 0 项的文字（不按发送人筛选）。布局函数建下拉时放进去，窗口重填名单时也用它。
//只做标记（TR_NOOP），使用处用 TR 取译文
const TCHAR* const kHistorySenderAllText = TR_NOOP(L"全部成员");

//消息记录窗每页显示的消息条数。原先是 8 条，列表本身能滚动、一页却很短，200 条记录要分成
//25 页、基本靠翻页在看；改为 30 条（2026-10-01）。行内各类按钮的 id 段按行下标编号，每页
//条数不得超过各段容量（见上方各 kIdHistoryXxxCapacity 与下面的编译期检查）。
constexpr int kHistoryPageSize = 30;
static_assert(kHistoryPageSize <= (int)kIdHistoryLocateCapacity
              && kHistoryPageSize <= (int)kIdHistoryPinCapacity
              && kHistoryPageSize <= (int)kIdHistoryRowCapacity
              && kHistoryPageSize <= (int)kIdHistoryExpandCapacity
              && kHistoryPageSize <= (int)kIdHistoryThumbCapacity
              && kHistoryPageSize <= (int)kIdHistoryPinMarkCapacity
              && kHistoryPageSize <= (int)kIdHistoryCardCapacity
              && kHistoryPageSize <= (int)kIdHistoryFileCardCapacity
              && kHistoryPageSize <= (int)kIdHistorySelectCapacity
              && kHistoryPageSize <= (int)kIdHistoryGridCapacity
              && kHistoryPageSize <= (int)kIdHistoryVoiceCapacity
              && kHistoryPageSize <= (int)kIdHistoryCodeCardCapacity
              && kHistoryPageSize <= (int)kIdHistoryCardThumbRowCapacity,
              "kHistoryPageSize exceeds a per-row control id segment");

/**
 *  把行内与 Pin 有关的点击解析成「对当前页第几行、改成什么状态」。
 *
 *  有两个入口会改一条消息的 Pin 状态：
 *    · 行内「Pin / 取消」按钮（kIdHistoryPinBase 段）：按该行当前状态取反；
 *    · 时间右侧的图钉（kIdHistoryPinMarkBase 段）：图钉只出现在已 Pin 的行上，目标状态
 *      恒为「取消」。不按当前状态取反，是为了防止模型刚被推送刷新成未 Pin、而旧界面上的
 *      图钉还没来得及消失时，这次点击被解析成「钉住」。
 *  两段各按自己的容量判上界（见上方各段 id 的说明）。
 *
 *    id：          DUIN_CLICK 通知里的 ctrlId。
 *    pagePinned：  当前页各行是否已 Pin，下标即行下标（引用，不转移所有权）。
 *    rowOut：      [出参] 行下标；返回 false 时不修改。
 *    targetPinOut：[出参] 目标状态，true 钉住、false 取消；返回 false 时不修改。
 *  @return id 属于上述两段、且行下标在当前页范围内时返回 true；其它 id 返回 false，
 *          调用方据此继续判断别的分支。
 */
bool ResolveHistoryPinClick(UINT id, const std::vector<bool>& pagePinned,
                            int& rowOut, bool& targetPinOut);

//图片消息在列表里的预览图尺寸上限（像素）。按原图宽高等比缩放到这个框里，小图不放大。
//取得比聊天窗的图小一档：消息记录一页要放好几条，预览图太大一屏只剩两三条。
constexpr int kHistoryImageMaxW = 200;
constexpr int kHistoryImageMaxH = 150;
//消息里没有原图宽高（老消息、字段缺失）时预览图的尺寸（像素），与聊天窗同口径。
constexpr int kHistoryImageDefaultW = 160;
constexpr int kHistoryImageDefaultH = 120;

/**
 *  算出图片消息在列表里的预览图尺寸：按原图宽高等比缩放到 kHistoryImageMaxW ×
 *  kHistoryImageMaxH 以内，小图不放大；宽高任一不为正时取默认尺寸。
 *    realW / realH：原图的像素宽高（取自消息内容）。
 *    outW / outH：  [出参] 预览图的宽高（像素，恒 >= 1）。
 */
void ComputeHistoryImageSize(int realW, int realH, int& outW, int& outH);

/**
 *  找出某个控件所在的是列表里的第几行消息。
 *
 *  用途：双击落在行内的昵称、时间、空白处或图片预览上时，收到双击的是那个最深层的
 *  子控件而不是行本身，窗口拿它的 ctrlId 认不出是哪一行。本函数从该控件沿父链往上
 *  找到所在的行容器（HistoryRow），按行容器的 ctrlId（kIdHistoryRowBase + 行下标）
 *  换算出行下标。
 *    ctrl：任意控件（引用，不转移所有权）；可为空。
 *  @return 当前页的行下标（从 0 起）；ctrl 为空、不在任何消息行里（如日期分组头、
 *          筛选栏）时返回 -1
 */
int HistoryRowIndexOfControl(const balloonwjui::DuiControl* ctrl);

//帖子卡片下一张缩略图的渲染数据（docs/帖子群方案.md 14.5）
struct HistoryCardThumbView
{
    CString m_path;      //原图在本机缓存里的路径（按内容 MD5 算出）；算不出时为空
    int     m_state;     //显示状态，取值见 ImagePlaceholder.h 的 balloonchatwnd::HistoryThumbState
    int     m_width;     //原图像素宽；未知为 0
    int     m_height;    //原图像素高；未知为 0
    CString m_tooltip;   //悬停提示；空串表示不挂提示

    HistoryCardThumbView()
        : m_state(0)
        , m_width(0)
        , m_height(0)
    {
    }
};

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

    //多选模式：为 true 时行首多一个复选框（id = kIdHistorySelectBase + 行下标），正文列相应
    //变窄。m_selected 是复选框的勾选状态，取自窗口的选择集合（HistorySelection），翻页往返不丢
    bool      m_selectMode;
    bool      m_selected;

    //本行之前要不要插一条日期分组头，非空时即为组头文字（「今天」/「昨天」/「2026-08-02」）。
    //由 FormatDateGroupLabel 算出。分组<u>在页内做</u>、不跨页合并：翻到下一页时若首行
    //仍是同一天，那一页会再出现一次同样的组头 —— 这是分页 + 分组的固有折中，比"跨页
    //记住上一页最后一天"简单得多，也不会因翻页顺序不同而出现组头忽有忽无。
    CString   m_groupLabel;

    //本行预览文字里要高亮的字符区间（搜索命中处）。无关键字时为空。
    std::vector<HighlightRange> m_highlights;

    //昵称下方的图片预览 / 文件卡片。m_hasImageThumb 为 false 且 m_thumbFileExt 为空时两者
    //都不画，显示正文（文本等消息）。
    CString m_thumbImagePath;  //图片消息的本地缓存路径（按内容 MD5 算出）；取不到时为空
    CString m_thumbFileExt;    //文件消息的小写扩展名，如 "xlsx"；决定类型色块的颜色与字

    //本行是否是图片消息、要画图片缩略图。为 true 时即便 m_thumbImagePath 为空（调试入口
    //没有登录态、或消息缺少 MD5）也画一块缩略图，按 m_thumbState 显示对应的占位。
    bool    m_hasImageThumb;
    //图片缩略图的显示状态，取值见 ImagePlaceholder.h 的 balloonchatwnd::HistoryThumbState；
    //由窗口按 ResolveHistoryThumb 算出。仅 m_hasImageThumb 为 true 时有意义。
    int     m_thumbState;
    //单击图片缩略图是否发起下载（需要下载，或下载失败后重试）。
    bool    m_thumbClickable;
    //鼠标悬停在图片缩略图上时的提示文字；空串表示不挂提示。
    CString m_thumbTooltip;

    //图片原图的像素宽 / 高（取自消息内容），用来等比算出昵称下方那张预览图的尺寸；
    //0 表示未知，按默认尺寸显示。仅 m_hasImageThumb 为 true 时有意义。
    int     m_imageWidth;
    int     m_imageHeight;

    //文件消息在昵称下方那张卡片上显示的文件名与大小文字（如 "182.8 KB"）。仅
    //m_thumbFileExt 非空时有意义；大小未知时 m_fileSizeText 为空串，卡片只显示文件名。
    CString m_fileName;
    CString m_fileSizeText;

    //本行是不是合并转发的聊天记录。为 true 时昵称下方画一张"聊天记录卡片"（色块 + 可点击的
    //标题 + "聊天记录（共 N 条）"），单击标题打开查看窗；图片 / 文件行不会同时置它。
    bool    m_isMergedCard;
    CString m_mergedTitle;    //卡片标题；空串时显示"聊天记录"
    int     m_mergedCount;    //记录里的子消息条数；仅 m_isMergedCard 为 true 时有意义

    //本行是否用聊天窗的文件卡片显示文件（状态文字、进度条、下载 / 打开等操作链接，见
    //ChatFileCardControl）。为 true 时昵称下方画 m_fileCard 描述的那张卡片，代替消息记录窗
    //那种只有色块、文件名与大小的静态卡片。聊天记录查看窗的文件行用它；消息记录窗不用。
    bool    m_chatFileCard;
    //卡片显示的文件消息（kind 为 kMsgFile）；仅 m_chatFileCard 为 true 时有意义
    balloonchatwnd::ChatMessage m_fileCard;

    //本行是不是语音消息（语音消息第 4 期）。为 true 时昵称下方画一条与聊天窗同款的语音条（ChatVoiceStrip），
    //单击播放 / 停止；本机没有文件时单击先下载
    bool    m_isVoice;
    //语音条显示的语音消息（kind 为 kMsgVoice）：时长、波形、播放中、已听、本机文件状态；仅 m_isVoice 为 true 时有意义
    balloonchatwnd::ChatMessage m_voice;
    //是否按已听状态画未听红点：消息记录窗为 true；合并记录查看窗里的语音没有已听状态，为 false
    bool    m_voiceShowDot;

    //本行是不是代码片段（docs/代码片段方案.md 第 3.4 节）。为 true 时昵称下方画一张与聊天窗相同的代码卡片
    //（ChatCodeCardControl），代码取 m_fullText；超过折叠行数时只显示前面几行，点卡片打开代码查看窗
    bool    m_isCode;
    //代码片段的语言，取值见 codesnippet::CodeLanguage；仅 m_isCode 为 true 时有意义
    int     m_codeLang;
    //代码片段的批注（docs/代码片段方案.md 第 10.5 节），行号指 m_fullText 的行：卡片标出被批注的行，
    //打开代码查看窗时一并带上；仅 m_isCode 为 true 时有意义，没有批注时为空
    std::vector<codesnippet::CodeNote> m_codeNotes;

    //本行是不是 Markdown 消息（docs/Markdown消息方案.md 第 12 节）。为 true 时昵称下方画一张与聊天窗相同的 Markdown 卡片
    //（ChatMarkdownCardControl），点卡片打开 Markdown 预览窗口。只在 FlamingoMarkdown.dll 可用且解析成功时置位
    //（MakeMarkdownCardInfo），否则这一行仍按普通文字显示源文本
    bool    m_isMarkdown;
    //Markdown 卡片的内容：标题、摘要、源文本，以及打开预览窗口用的发送者、是否本人发的、时间与消息标识；
    //仅 m_isMarkdown 为 true 时有意义
    balloonchatwnd::MarkdownCardInfo m_markdownCard;

    //帖子卡片与补充卡片带的图片（docs/帖子群方案.md 14.5）：正文下面排一行缩略图，最多 kHistoryCardMaxThumbs 张；
    //不是这两种卡片、没有图片或卡片已删除时为空
    std::vector<HistoryCardThumbView> m_cardThumbs;
    //没排出来的图片张数；大于 0 时在最后一张缩略图上写「+N」
    int     m_cardThumbExtra;

    //---- 时间轴模式（docs/聊天记录管理器方案.md §11.7）----
    //是否按时间轴样式画这一行：行首多出时间列（m_timeText，「HH:MM」）与竖线节点列，名字后面不再写时间；
    //日期分组头画成竖线上的大节点，右侧写 m_groupLabel 与 m_groupDetail
    bool    m_timeline;
    //这一行是否命中关键字（时间轴模式下竖线上的节点画成黄色）
    bool    m_timelineHit;
    //时间轴模式下日期大节点右侧的灰色说明，如「星期四 · 42 条」；只在 m_groupLabel 非空时有意义
    CString m_groupDetail;

    HistoryRowView()
        : m_avatarColor(RGB(0xF2, 0x6B, 0x3E))
        //初始化顺序须与声明顺序一致：m_expanded 声明在 m_msgid 之前。
        , m_expanded(false)
        , m_msgid(0)
        , m_pinned(false)
        , m_compact(false)
        , m_selectMode(false)
        , m_selected(false)
        , m_hasImageThumb(false)
        , m_thumbState(0)
        , m_thumbClickable(false)
        , m_imageWidth(0)
        , m_imageHeight(0)
        , m_isMergedCard(false)
        , m_mergedCount(0)
        , m_chatFileCard(false)
        , m_isVoice(false)
        , m_voiceShowDot(false)
        , m_isCode(false)
        , m_codeLang(0)
        , m_isMarkdown(false)
        , m_cardThumbExtra(0)
        , m_timeline(false)
        , m_timelineHit(false)
    {
    }
};

//「图片」分类网格里一格对应的内容（docs/帖子群方案.md 14.5）
struct HistoryGridCellRef
{
    int m_row;     //当前页的记录下标
    int m_image;   //帖子卡片里的第几张图片（0 起）；图片消息本身为 -1

    HistoryGridCellRef()
        : m_row(-1)
        , m_image(-1)
    {
    }
};

/**
 *  把当前页的行展开成「图片」分类网格的格子：图片消息一条一格；带图片的帖子卡片与补充卡片每张图片一格（取 m_cardThumbs，
 *  网格模式下窗口给它填了全部图片），日期分组头只留在这条记录的第一格上。超出 capacity 格的部分不显示（格子编号段的容量）。
 *    rows：    当前页的行数据。
 *    capacity：最多几格（kIdHistoryGridCapacity）。
 *    refsOut： [出参] 每一格对应的记录与图片，与返回值一一对应。
 *  @return 网格的格子，直接交给 BuildImageGridContent；第 i 格的编号为 kIdHistoryGridBase + i
 */
std::vector<HistoryRowView> ExpandHistoryGridCells(const std::vector<HistoryRowView>& rows, size_t capacity,
                                                   std::vector<HistoryGridCellRef>& refsOut);

/**
 *  帖子卡片正文下面那一行缩略图的高度：显示出来的几张里最高的那张（各张按 ComputeHistoryCardThumbSize 算）。
 *  行高计算与建行共用，两处不会算得不一样。
 *    row：行数据；没有缩略图时返回 0。
 *  @return 缩略图那一行的高度（像素）
 */
int HistoryCardThumbStripHeight(const HistoryRowView& row);

//整窗的渲染视图数据。窗口把 HistoryModel 的当前状态翻译成本结构再交给布局。
struct HistoryViewData
{
    CString m_title;        //标题“消息记录 · 苏沐辰”
    int     m_totalCount;   //匹配总条数（标题副行与底部都用）
    int     m_currentPage;  //当前页（从1起）
    int     m_pageCount;    //总页数（>=1）
    int     m_activeChip;   //当前选中的类型胶囊（取值见 HistoryTypeFilter）
    int     m_activeTimeIndex;  //时间下拉当前选中项下标（0全部 1近7天 2近30天 3近3个月
                                //4近半年 5近一年），与 HistoryTimeRange 的枚举值一一对应
    //日期区间标签的文字（如「9月1日 至 9月30日」，由 FormatHistoryDateRangeLabel 算出）。
    //非空表示「只看这段时间」正在生效：筛选行显示该标签、隐藏时间下拉；空串时反之
    std::wstring m_dateRangeLabel;
    std::vector<HistoryRowView> m_rows;   //当前页各行（最新在前）

    //列表为空时显示的两行文案（由 BuildEmptyStateText 按当前筛选条件算出）。
    //m_rows 非空时这两项不被使用。
    CString m_emptyTitle;     //主文案
    CString m_emptySubtitle;  //副文案；空串表示不显示副行

    //底部一行是否放「清空聊天记录」按钮。清空要经主面板发请求，调试入口没有主面板时为 false
    bool    m_showClearConversation;

    //标题区左侧显示的会话名（kIdHistoryTitle）；空串表示不显示，该标签宽度为 0
    CString m_conversationName;
    //标题区最左边是否显示「»」按钮（聊天记录管理器左栏折叠时为 true）
    bool    m_showExpandLeft;
    //搜索框右侧「通配符」复选框（kIdHistoryWildcard）建出来时是否勾选，取模型当前的 m_wildcard。
    //局部刷新不重建搜索行，之后的勾选状态由复选框自己保持
    bool    m_wildcard;
    //是否处于时间轴模式：搜索行最右边「列表 | 时间轴」的选中项、列表右侧导航条的显隐（之后的切换由
    //ApplyHistoryTimelineMode 完成）
    bool    m_timeline;

    HistoryViewData()
        : m_totalCount(0)
        , m_currentPage(1)
        , m_pageCount(1)
        , m_activeChip(0)
        , m_activeTimeIndex(0)
        , m_showClearConversation(true)
        , m_showExpandLeft(false)
        , m_wildcard(false)
        , m_timeline(false)
    {
    }
};

//构造历史窗客户区控件树（不含窗口边框，边框由 DuiFrameWindow 负责）。
//@param data              整窗渲染数据（标题 / 总数 / 当前页 / 各行 / 选中胶囊等）
//@param listRightInsetPx  列表滚动视口右侧要让出的宽度（物理像素，>= 0；负值按 0 处理）。
//                         DuiFrameWindow 把窗口最外侧 GetBorderPx() 个逻辑像素判为缩放
//                         边框，列表滚动条若贴着窗口右缘，鼠标移到滑块上只会变成左右双向
//                         箭头、无法拖动。窗口按 balloonmain::ComputeResizeBorderInsetPx
//                         算出该宽度传入，使滚动条完整落在可点击的客户区内。默认 0 表示
//                         不让出（单元测试等没有窗口的场景）。
//@return 客户区根控件（一个 vbox），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryWindowBody(const HistoryViewData& data,
                                                                 int listRightInsetPx = 0);

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
//@param pendingDownloadCount 本页还没下载的文件数；大于 0 时列表最前面多一条「本页有 N 个文件还没有
//                     下载 [全部下载]」（「文件」分类用，按钮 id = kIdHistoryBatchDownload），其余情况传 0
//@param reserveRowActions 是否在每行右侧建「Pin / 定位」两个按钮槽。消息记录窗用默认值 true；
//                     聊天记录查看窗是只读的、不会出现这两个按钮，传 false：不建按钮槽，
//                     正文列用满这部分宽度，行高也按加宽后的正文宽度测量
//@return 列表内容 vbox，调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildListContent(const std::vector<HistoryRowView>& rows,
                                                          LPCTSTR emptyTitle,
                                                          LPCTSTR emptySubtitle,
                                                          int listWidth = 0,
                                                          bool reserveRowActions = true,
                                                          int pendingDownloadCount = 0);

//在列表控件树里按 msgid 找语音条：语音条编号为 kIdHistoryVoiceBase + 行下标，在一页的行数（kHistoryPageSize）
//之内逐个比对其消息的 msgid。供消息记录窗使用。
//@param root  列表内容或整个窗口客户区的根控件（不持有），可以为 NULL
//@param msgid 服务器消息 ID；为 0 时返回 NULL
//@return 找到的语音条（不持有，随控件树销毁）；没有时返回 NULL
balloonchatwnd::ChatVoiceStrip* FindHistoryVoiceStrip(balloonwjui::DuiControl* root, long long msgid);

//别的窗口里听过一条语音后，把记录窗当前页里同一条语音的未听红点去掉（语音消息第 6a 步）。
//@param root      列表内容或整个窗口客户区的根控件（不持有），可以为 NULL
//@param viewCtype 记录窗当前显示的会话类型（1 单聊 2 群聊）
//@param viewCid   记录窗当前显示的会话 ID
//@param ctype     听过的那条语音所在会话的类型
//@param cid       听过的那条语音所在会话的 ID
//@param msgid     听过的那条语音的服务器消息 ID
//@return 会话相符、当前页里有这条语音且原来未听、已改为已听（语音条重绘）时返回 true；其余情况返回 false 且不改
bool MarkHistoryVoicePlayed(balloonwjui::DuiControl* root, int viewCtype, int64_t viewCid,
                            int ctype, int64_t cid, long long msgid);

//构建「图片」分类的缩略图网格，作为滚动视口的内容（替代 BuildListContent 的逐行列表）。
//按日期分组：每组先一行日期标题，再按可用宽度能放几张就一行几张地排正方形缩略图（列数见
//HistoryImageGrid.h 的 GridColumnCount）。缩略图 id = kIdHistoryGridBase + 记录下标，一律可点击，
//悬停提示「发送人 · 时间」。
//@param rows          当前页各行（只用图片相关字段与日期分组头）
//@param emptyTitle    列表为空时的主文案；emptySubtitle：副文案。为空时与 BuildListContent 显示相同
//@param listWidth     滚动视口内容的可用宽度（像素）；<=0 时按中等窗口宽度兜底
//@param listCtrlId    网格内容 vbox 的编号；聊天记录管理器的图片页用自己的编号段，与聊天记录页的不重复
//@param cellIdBase    第 i 张缩略图的编号 = 基址 + i
//@return 网格内容 vbox（id = listCtrlId），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildImageGridContent(const std::vector<HistoryRowView>& rows,
                                                               LPCTSTR emptyTitle,
                                                               LPCTSTR emptySubtitle,
                                                               int listWidth,
                                                               UINT listCtrlId = kIdHistoryList,
                                                               UINT cellIdBase = kIdHistoryGridBase);

//构建多选操作栏：左侧「已选 N 条」（撑满），右侧「合并转发」「逐条转发」「删除」「取消」四个按钮。
//平时隐藏，由窗口在进入多选时显示、并隐藏底部分页栏（两者同高，互相替换）。
//@return 操作栏（一个 hbox，id = kIdHistorySelectBar），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildMultiSelectBar();

//把分页按钮（首页 < 1 2 … > 末页）填进给定的分页器容器。窗口在翻页 / 改筛选导致页数
//变化时，先清空分页器容器再调本函数重填，避免整窗重建。
//@param pager        分页器容器（一个 hbox，调用方负责先清空其旧子控件）
//@param currentPage  当前页（从1起，用于高亮）
//@param pageCount    总页数（>=1）
void PopulatePagerButtons(balloonwjui::DuiControl* pager, int currentPage, int pageCount);

//切换筛行时间区域的显示：label 非空时隐藏时间下拉、显示日期区间标签（文字即 label，宽度按
//文字实测），为空时反过来；随后按筛选行当前矩形强制重排 —— SetVisible 只置标志、不会重排，
//不强制的话此前隐藏的那个控件会停在过时的矩形上（开发约定文档 UI 约定第 5 条）。
//建窗口时由 BuildHistoryWindowBody 按 HistoryViewData::m_dateRangeLabel 调用一次；
//「只看这段时间」生效或被清除时由窗口调用。
//@param root   客户区根控件（BuildHistoryWindowBody 的返回值）；为空或找不到筛选行时什么都不做
//@param label  日期区间标签文字；空串表示没有日期筛选
void ApplyHistoryDateRangeChip(balloonwjui::DuiControl* root, const std::wstring& label);

// =====================================================================
// 「选日期」弹层（CHistoryDateJumpPopup）的内容与动作判定
//
// 弹层是独立的浮层窗口，单元测试建不出来；把内容树的构建与"哪个通知对应哪个动作"放在这里，
// 弹层窗口只负责打开、读取日期、回调宿主，这两部分就能直接测（DR9）。
// =====================================================================

//「选日期」弹层内容树里的控件编号。弹层自成一个 DUI 宿主，编号只需在弹层内唯一
enum HistoryDatePopupId
{
    kIdDatePopupFromYear  = 1,    //「从」年微调框
    kIdDatePopupFromMonth = 2,    //「从」月微调框
    kIdDatePopupFromDay   = 3,    //「从」日微调框
    kIdDatePopupToYear    = 4,    //「到」年微调框
    kIdDatePopupToMonth   = 5,    //「到」月微调框
    kIdDatePopupToDay     = 6,    //「到」日微调框
    kIdDatePopupJump      = 7,    //「跳到这一天」按钮
    kIdDatePopupFilter    = 8,    //「只看这段时间」按钮
    //各微调框内部输入框的编号 = 微调框编号 + kIdDatePopupEditOffset。回车通知由输入框发出，
    //带的是输入框自己的编号
    kIdDatePopupEditOffset = 10,
};

//弹层里用户触发的动作
enum HistoryDatePopupAction
{
    kHistoryDatePopupNone   = 0,   //不是弹层要处理的通知
    kHistoryDatePopupJump   = 1,   //「跳到这一天」，或在任一输入框里按回车：只用「从」那个日期
    kHistoryDatePopupFilter = 2,   //「只看这段时间」：用「从」「到」两个日期筛选
};

//弹层的宽与高（像素）
const int kHistoryDatePopupW = 330;
const int kHistoryDatePopupH = 128;

//构造「选日期」弹层的内容树：「从 [年][月][日]」「到 [年][月][日]」两行，下面一行靠右放
//「跳到这一天」「只看这段时间」两个按钮。年份微调框的范围由弹层打开时重设。
//@return 内容树根控件（一个 vbox），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryDatePopupContent();

//把弹层内容树发来的一条 DUI 通知翻译成动作。
//@param code    通知码（DUIN_CLICK、DuiEdit::DUIN_EDIT_ENTER 等）
//@param ctrlId  发出通知的控件编号
//@return 动作，见 HistoryDatePopupAction；与弹层无关的通知返回 kHistoryDatePopupNone
int HistoryDatePopupActionFromNotify(UINT code, UINT ctrlId);

//用一组假数据填充 view（供 --history 调试入口与布局契约测试，不连真实数据）。
//@param data  [出参] 被填入标题、若干行与分页信息的渲染数据
void FillMockHistory(HistoryViewData& data);

//把标题区的会话名与「»」按钮改成给定的状态，并按标题区那一行的当前矩形重排（会话名宽度按文字
//实测；「»」显隐变化后不重排的话，此前隐藏的控件会停在过时的矩形上，开发约定文档 UI 约定第 5 条）。
//聊天记录管理器切换会话、折叠或展开左栏时调用。
//@param root            右侧界面的根（BuildHistoryWindowBody 的返回值），或包含它的任意上层控件；
//                       为空或找不到标题区时什么都不做
//@param conversationName 会话名；空串表示不显示
//@param showExpandLeft  是否显示「»」按钮
void ApplyHistoryHeaderTitle(balloonwjui::DuiControl* root, const CString& conversationName,
                             bool showExpandLeft);

// =====================================================================
// 聊天记录管理器的左栏（docs/聊天记录管理器方案.md §3）
//
// 客户区根容器是一个竖向分隔的 DuiSplitter：pane 0 是左栏（搜索框、范围按钮、类型筛选、
// 会话列表），pane 1 是 BuildHistoryWindowBody 造的整套右侧界面，原样放进去。列表的行由窗口
// 填入（名字与头像要经主面板解析），这里只造控件骨架、提供几项改状态并重排的函数。
// =====================================================================

//左栏控件的编号。与右侧界面同在一棵控件树里，取 5100 起，避开右侧各固定 id（5xx）与各段
//动态 id（1000～4999，最后一段到 kIdHistoryGridBase + kIdHistoryGridCapacity 为止）
enum HistoryManagerCtrlId : UINT
{
    kIdMgrSplitter    = 5100,  //客户区根：左右分栏的 DuiSplitter；拖完分隔条时发 DUIN_VALUECHANGED，extra 为左栏宽度
    kIdMgrPane        = 5101,  //左栏根容器（分隔容器的 pane 0）
    kIdMgrSearch      = 5102,  //左栏搜索框：按名字（含拼音）筛会话，同时跨会话搜聊天内容
    kIdMgrCollapse    = 5103,  //左栏右上角的「«」按钮：折叠左栏
    kIdMgrScopeRow    = 5104,  //范围按钮所在的那一行；「×」显隐变化后按它重排
    kIdMgrScope       = 5105,  //范围按钮「范围：全部会话 / 已选 N 个会话」：点开勾选搜索范围
    kIdMgrScopeClear  = 5106,  //范围按钮右侧的「×」：把范围恢复为全部会话；不限范围时隐藏
    kIdMgrWildcard    = 5107,  //范围行最右边的「通配符」复选框：左栏跨会话搜索的关键字是否按通配符解释（默认不勾）
    //左栏最上面的标签行（聊天 / 文件 / 图片 / 链接）。2026-10-06 加「链接」标签时由 5133 改为 5134，把 5133 让给
    //「链接」，四个标签的编号保持连续
    kIdMgrTabRow      = 5134,
    kIdMgrTabChat     = 5130,  //标签：聊天（编号连续，5130..5133 与 HistoryManagerTab 的 0～3 一一对应）
    kIdMgrTabFiles    = 5131,  //标签：文件（docs/聊天记录管理器方案.md §11.5）
    kIdMgrTabImages   = 5132,  //标签：图片（§11.6）
    kIdMgrTabLinks    = 5133,  //标签：链接（§12.4）
    kIdMgrRightHost   = 5140,  //分栏右侧的容器：里面是聊天记录页（kIdHistoryBody）与文件 / 图片页（kIdLibBody），只显示其一
    kIdMgrStorageRow  = 5145,  //左栏最下面一行（各标签都显示）：「本机存储占用」入口所在的容器
    kIdMgrStorage     = 5146,  //「本机存储占用」：点它右侧显示本机存储占用页（docs/聊天记录管理器方案.md §12.6）
    kIdMgrTypeAll     = 5110,  //类型筛选：全部
    kIdMgrTypeSingle  = 5111,  //类型筛选：单聊
    kIdMgrTypeGroup   = 5112,  //类型筛选：群聊
    kIdMgrScroll      = 5120,  //列表滚动视口（MainPanelScrollView）
    kIdMgrListBox     = 5121,  //滚动视口里承载两段列表的 vbox
    kIdMgrConvHeader  = 5122,  //搜索时「会话」一段的标题；平时隐藏
    kIdMgrConvTree    = 5123,  //会话列表（MainPanelTreeView）：平时是全部会话，搜索时是名字匹配的会话
    kIdMgrHitHeader   = 5124,  //搜索时「聊天记录」一段的标题（注明只搜本机）；平时隐藏
    kIdMgrHitTree     = 5125,  //搜索时「聊天记录」一段的列表（MainPanelTreeView）；平时隐藏
    kIdMgrEmpty       = 5126,  //列表为空时的提示文字；不为空时隐藏
};

//左栏默认宽度（像素）。没有保存过宽度时用它
constexpr int kMgrPaneDefaultW = 260;
//左栏最小宽度（像素）：再窄会话名只剩两三个字，拖动分隔条时到此为止。2026-10-06 加「链接」标签时由 200 提到 210，
//四个标签在最小宽度里排得下（docs/聊天记录管理器方案.md §12.2）
constexpr int kMgrPaneMinW = 210;
//右侧界面的最小宽度（像素）：与消息记录窗原来的最小窗口宽度相同，底部一行的取舍按这个宽度验证过
//（HistoryWindowLayoutTests 的 V2L20 起）
constexpr int kMgrBodyMinW = 640;
//分隔条宽度（像素）：够宽才好拖，DuiSplitter 在 4 像素以上时画出抓手圆点
constexpr int kMgrSplitBarW = 5;
//左栏折叠后分隔条的宽度（像素）：DuiSplitter 不接受 0，取 1，与窗口左边框连成一条细线
constexpr int kMgrSplitBarCollapsedW = 1;
//左栏列表的行高与头像边长（像素），与主面板会话列表相同
constexpr int kMgrRowHeight = 48;
constexpr int kMgrAvatarSize = 36;
//左栏列表里两段标题（「会话」「聊天记录」）与空提示的行高（像素）
constexpr int kMgrSectionHeaderH = 28;
//左栏列表内容 vbox 的上下内边距之和（像素），算滚动内容高度时要加上
constexpr int kMgrListPadV = 8;

//构造左栏控件树：搜索框 +「«」、范围按钮 +「×」、类型筛选、列表（滚动视口里依次是「会话」标题、
//会话树、「聊天记录」标题、命中树、空提示，后四项平时隐藏）。
//@param typeFilter      初始选中的类型筛选，取值见 HistoryConversationList.h 的 ManagerTypeFilter
//@param scopeCount      搜索范围里勾选的会话个数；0 表示不限
//@param wildcard        范围行最右边的「通配符」复选框（kIdMgrWildcard）是否勾选
//@param tab             建出来时选中哪个标签，取值见 HistoryManagerTab（ApplyHistoryManagerTab 的规则同样套用）
//@return 左栏根容器（id = kIdMgrPane），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryManagerPane(int typeFilter, size_t scopeCount,
                                                                 bool wildcard = false, int tab = 0);

//把左栏与右侧界面装进左右分栏的根容器。
//@param pane        左栏（BuildHistoryManagerPane 的返回值），所有权转入
//@param body        右侧界面（BuildHistoryWindowBody 的返回值），所有权转入
//@param paneWidth   左栏宽度（像素）；不大于 0 时取 kMgrPaneDefaultW
//@param collapsed   左栏是否折叠
//@return 根容器（DuiSplitter，id = kIdMgrSplitter），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryManagerRoot(std::unique_ptr<balloonwjui::DuiControl> pane,
                                                                 std::unique_ptr<balloonwjui::DuiControl> body,
                                                                 int paneWidth, bool collapsed);

//同上，右侧再放一套文件 / 图片页（docs/聊天记录管理器方案.md §11.5、§11.6）：分栏右侧是一个容器
//（kIdMgrRightHost），聊天记录页与文件 / 图片页都在里面，建出来时只显示聊天记录页，由 ApplyHistoryManagerTab 切换。
//@param library    文件 / 图片页（BuildHistoryLibraryBody 的返回值），所有权转入；为空时与上一个函数相同
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryManagerRoot(std::unique_ptr<balloonwjui::DuiControl> pane,
                                                                 std::unique_ptr<balloonwjui::DuiControl> body,
                                                                 std::unique_ptr<balloonwjui::DuiControl> library,
                                                                 int paneWidth, bool collapsed);

//聊天记录管理器左栏顶部的标签（docs/聊天记录管理器方案.md §11.5、§12.2）
enum HistoryManagerTab
{
    kHistoryManagerTabChat     = 0,   //聊天：左栏是会话列表，右侧是某个会话的聊天记录（原来的界面）
    kHistoryManagerTabFiles    = 1,   //文件：右侧汇总聊天文件与群文件
    kHistoryManagerTabImages   = 2,   //图片：右侧汇总聊天里的图片
    kHistoryManagerTabLinks    = 3,   //链接：右侧汇总正文含链接的消息
    //与我有关：没有自己的标签，是「聊天」标签左栏会话列表的第一行。左栏与「聊天」标签相同（「聊天」标签显示为选中，
    //第一行「与我有关」选中），右侧显示与我有关页（文件页那一套界面）
    kHistoryManagerTabMentions = 4,
};

//左栏可点的标签个数（kHistoryManagerTabChat～kHistoryManagerTabLinks）
const int kHistoryManagerVisibleTabCount = 4;

//切换左栏标签：标签选中态（与我有关时「聊天」标签选中）；文件、图片、链接三页隐藏范围行（搜索框只按名字找会话，
//不跨会话搜内容），搜索框的提示文字随之改变；右侧容器里显示对应的那一页（聊天显示聊天记录页，其余显示文件页那一套
//界面）。显隐变化后重排左栏与右侧容器（开发约定文档 UI 约定第 5 条）。
//@param root  客户区根容器或左栏（不持有），可为 NULL
//@param tab   取值见 HistoryManagerTab；越界按聊天处理
void ApplyHistoryManagerTab(balloonwjui::DuiControl* root, int tab);

//显示或离开「本机存储占用」页（docs/聊天记录管理器方案.md §12.6）。active 为 true 时右侧容器只显示存储页
//（kIdStorBody，须已由窗口加进右侧容器；没有时什么都不做），左栏最下面的入口显示为选中；为 false 时与
//ApplyHistoryManagerTab(root, tab) 相同（存储页隐藏、入口恢复普通外观、右侧按 tab 显示）。显隐变化后重排右侧容器。
//@param root    客户区根容器（不持有），可为 NULL
//@param active  true 进入存储页，false 离开
//@param tab     离开时回到哪个标签，取值见 HistoryManagerTab
void ApplyHistoryManagerStorage(balloonwjui::DuiControl* root, bool active, int tab);

//折叠或展开左栏：改分隔条的位置、宽度与左栏最小宽度，切换左栏与右侧「»」按钮的显隐，再按根容器
//当前矩形重排（左栏宽度变了，右侧整套界面都要重新排）。
//@param root       根容器（BuildHistoryManagerRoot 的返回值）；为空或不是分隔容器时什么都不做
//@param collapsed  true 折叠，false 展开
//@param paneWidth  展开时左栏的宽度（像素）；不大于 0 时取 kMgrPaneDefaultW
//@param conversationName 右侧标题区当前的会话名（ApplyHistoryHeaderTitle 要用）
void ApplyHistoryManagerCollapsed(balloonwjui::DuiControl* root, bool collapsed, int paneWidth,
                                  const CString& conversationName);

//把左栏类型筛选的选中态改成 typeFilter（选中的用 Primary，其余 Default）。
void ApplyHistoryManagerTypeChips(balloonwjui::DuiControl* root, int typeFilter);

//把范围按钮的文字改成 BuildManagerScopeButtonText(scopeCount)，不限范围时隐藏「×」，再重排那一行。
void ApplyHistoryManagerScope(balloonwjui::DuiControl* root, size_t scopeCount);

//左栏列表的显示状态
struct HistoryManagerListView
{
    bool    m_searching;     //是否处于搜索结果模式（显示两段标题）
    int     m_convRows;      //会话树的行数
    int     m_hitRows;       //命中树的行数；非搜索模式下忽略
    CString m_hitHeader;     //「聊天记录」一段的标题文字
    CString m_emptyText;     //两段都没有行时显示的提示；空串表示不显示

    HistoryManagerListView()
        : m_searching(false)
        , m_convRows(0)
        , m_hitRows(0)
    {
    }
};

//按 view 设置左栏列表各部分的显隐与高度：两棵树按行数定高（行数 × kMgrRowHeight），搜索模式下
//显示两段标题（某段没有行时连标题一起隐藏），两段都没有行时显示空提示；随后设滚动内容高度、重排。
//树里的行由调用方事先填好。
//@param root  左栏或包含左栏的任意上层控件；找不到相关控件时什么都不做
//@param view  显示状态
void ApplyHistoryManagerListView(balloonwjui::DuiControl* root, const HistoryManagerListView& view);

// =====================================================================
// 聊天记录管理器的搜索范围弹层（CHistoryScopePopup）的内容
// =====================================================================

//范围弹层内容树里的控件编号。弹层自成一个 DUI 宿主，编号只需在弹层内唯一
enum HistoryScopePopupId : UINT
{
    kIdScopePopupSearch    = 1,     //顶部搜索框：按名字（含拼音）筛选列表
    kIdScopePopupScroll    = 2,     //列表滚动视口
    kIdScopePopupInfo      = 3,     //底部左侧「已选 N 个」
    kIdScopePopupAll       = 4,     //「全部会话」按钮：以空集合提交（不限范围）
    kIdScopePopupOk        = 5,     //「确定」按钮：以勾选的会话提交
    kIdScopePopupCheckBase = 100,   //列表第 i 行复选框的编号 = 基址 + i（i 为当前显示的行下标）
};

//范围弹层的宽与高（像素）
const int kHistoryScopePopupW = 300;
const int kHistoryScopePopupH = 420;
//范围弹层列表的行高与头像边长（像素）
const int kHistoryScopePopupRowH = 40;
const int kHistoryScopePopupAvatarSize = 28;

//构造范围弹层的内容树：搜索框、列表滚动视口（内容由弹层按会话填入）、底部「已选 N 个」+「全部会话」+「确定」。
//@return 内容树根控件（一个 vbox），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryScopePopupContent();

//构造范围弹层列表的一行：复选框 + 头像 + 名字。复选框点击时不抢搜索框的焦点。
//@param checkId    复选框编号（kIdScopePopupCheckBase + 行下标）
//@param checked    复选框初始是否勾选
//@param name       会话名
//@param avatar     头像位图，借用不持有；为 NULL 时按名字首字与 cid 取色画首字头像
//@param cid        会话 ID，没有位图时据它稳定取头像底色
//@return 一行（hbox），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildHistoryScopePopupRow(UINT checkId, bool checked,
                                                                   const CString& name, HBITMAP avatar,
                                                                   int64_t cid);

} // namespace balloonhistory
