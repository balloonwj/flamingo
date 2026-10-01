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
    kIdHistoryTimeCombo  = 520,   //时间范围下拉（全部时间 / 近7天 / 近30天 / 近3个月 / 近半年 / 近一年）
    kIdHistorySenderCombo = 521,  //发送人下拉（搜索框右侧）：第 0 项「全部成员」，其余各项由窗口按本地库里发过言的人填入
    //522 原为「跳到日期」按钮，2026-10-01 与时间下拉里的「自定义」合并为「选日期」后删除，不再复用
    kIdHistoryDatePick       = 523,  //「选日期」按钮（时间下拉右侧）：打开「从 / 到」日期弹层，可跳到某天或只看某段时间
    kIdHistoryDateRangeChip  = 524,  //日期区间标签容器：「只看这段时间」生效后替下时间下拉显示，否则隐藏
    kIdHistoryDateRangeLabel = 525,  //日期区间标签里的日期文字（可点，重新打开「选日期」弹层修改）
    kIdHistoryDateRangeClear = 526,  //日期区间标签里的关闭叉号：清掉日期筛选，回到「全部时间」
    kIdHistoryFilterRow      = 527,  //筛选行容器（类型胶囊 + 时间区域）；切换时间下拉与日期区间标签后按它重排
    kIdHistoryList       = 530,   //列表内容容器（DuiScrollView 内的 vbox，承载各行）
    kIdHistoryScroll     = 531,   //列表滚动视口（局部刷新时按 id 取回、SetContent 换列表）
    kIdHistoryFooterInfo = 540,   //底部左侧“共 N 条 · 第 x/y 页”
    kIdHistoryPageFirst  = 541,   //分页：首页
    kIdHistoryPagePrev   = 542,   //分页：上一页
    kIdHistoryPageNext   = 543,   //分页：下一页
    kIdHistoryPageLast   = 544,   //分页：末页
    kIdHistoryExport     = 545,   //导出按钮
    kIdHistoryPager      = 546,   //分页器容器（局部刷新时清空 + 重填页码按钮）
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

    kIdHistoryEmptyTitle   = 580,  //空状态主文案（列表为空时才存在，供契约测试与窗口取回）
    kIdHistoryEmptySub     = 581,  //空状态副文案；副文案为空串时该控件不建

    kIdHistoryPageJump     = 570,  //“跳至第 N 页”微调框（DuiSpinBox 本体，▲▼ 点击发通知）
    kIdHistoryPageJumpEdit = 571,  //上面那个微调框内部的输入框子控件。手输入的提交要在它
                                   //失焦时自己做（见 DuiSpinBox 头注释），故单独给个 id
                                   //才认得出是谁失焦了
};

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

//发送人下拉第 0 项的文字（不按发送人筛选）。布局函数建下拉时放进去，窗口重填名单时也用它
const TCHAR* const kHistorySenderAllText = _T("全部成员");

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
              && kHistoryPageSize <= (int)kIdHistoryGridCapacity,
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
                                //4近半年 5近一年），与 HistoryTimeRange 的枚举值一一对应
    //日期区间标签的文字（如「9月1日 至 9月30日」，由 FormatHistoryDateRangeLabel 算出）。
    //非空表示「只看这段时间」正在生效：筛选行显示该标签、隐藏时间下拉；空串时反之
    std::wstring m_dateRangeLabel;
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

//构建「图片」分类的缩略图网格，作为滚动视口的内容（替代 BuildListContent 的逐行列表）。
//按日期分组：每组先一行日期标题，再按可用宽度能放几张就一行几张地排正方形缩略图（列数见
//HistoryImageGrid.h 的 GridColumnCount）。缩略图 id = kIdHistoryGridBase + 记录下标，一律可点击，
//悬停提示「发送人 · 时间」。
//@param rows          当前页各行（只用图片相关字段与日期分组头）
//@param emptyTitle    列表为空时的主文案；emptySubtitle：副文案。为空时与 BuildListContent 显示相同
//@param listWidth     滚动视口内容的可用宽度（像素）；<=0 时按中等窗口宽度兜底
//@return 网格内容 vbox（id = kIdHistoryList），调用方拿到所有权
std::unique_ptr<balloonwjui::DuiControl> BuildImageGridContent(const std::vector<HistoryRowView>& rows,
                                                               LPCTSTR emptyTitle,
                                                               LPCTSTR emptySubtitle,
                                                               int listWidth);

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

} // namespace balloonhistory
