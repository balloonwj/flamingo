/**
 *  MemoCardList.h
 *
 *  群聊窗「备忘」页签与本人备忘窗口共用的备忘卡片列表（2026-10-03 与用户确认的四行卡片）：
 *  一个 DuiScrollView 里竖排若干张卡片，每张卡片依次是标题、备注（有才显示）、提醒时间、规则、
 *  创建人（只有群备忘有）。能修改的卡片在鼠标悬停时于标题右侧显示「修改」「删除」两个按钮。
 *
 *  控件全部是 balloonui 现成控件的组合（开发约定文档 UI 约定第 2 条）：卡片外层复用消息记录窗的
 *  balloonhistory::HistoryRow（悬停时显示登记过的按钮、双击发 DUIN_DBLCLK），里面一个 DuiVBox 作圆角
 *  浅底，各行是 DuiLabel，两个按钮是 DuiButton。不新增自绘控件。
 *
 *  滚动视口是 DuiScrollView 的子类 CMemoCardScrollView。滚动条用 balloonui 统一的悬浮式细滑块（与聊天窗同一外观：
 *  半透明灰色圆角滑块、没有轨道、可按住拖动，滚动时淡入、停手后淡出，2026-10-04 起）；内容超出时卡片右侧让出与
 *  滚动条命中带同宽的窄条，滑块画在窄条里，不压在卡片上（2026-10-04 用户要求）。
 *
 *  两个约定：
 *    · 悬停由窗口按鼠标位置驱动：窗口旁听 WM_MOUSEMOVE / WM_MOUSELEAVE 后调 UpdateMemoCardHover。
 *      理由见 HistoryRow::SetHovered —— DuiHost 只把 enter / leave 发给最深的那个控件，卡片外层收不到。
 *    · 卡片内每个控件的 ctrlId 都落在本卡片的 id 段内（见 kMemoCardIdBase），窗口用 MemoCardFromCtrlId
 *      反推是第几张卡片的哪一部分。双击、右键落在卡片里任何一行文字上都能认出是哪张卡片。
 *
 *  典型用法：
 *      //建控件树时
 *      col->AddChild(CreateMemoCardScroll(kIdList), Hint().Weight(1));
 *      //数据变化时
 *      SetMemoCards(scroll, cards, kPanelBg);
 *      //OnDuiNotify 里
 *      int index = 0;
 *      int part = 0;
 *      if (pNotify->code == DUIN_CLICK && MemoCardFromCtrlId(pNotify->ctrlId, index, part)
 *          && part == kMemoCardPartEdit) { ... }
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <memory>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView / DuiScrollBar
#include "MemoViewModel.h"   // MemoCardText

namespace balloonmemo {

//内容超出时卡片右侧让出的窄条宽（像素）：等于滚动条命中带宽 —— 滑块 5、离右缘 2，余下 4 是滑块与卡片之间的
//空隙，滑块不会贴着卡片
const int kMemoScrollGutter = balloonwjui::DuiScrollBar::kOverlayBandPx;

/**
 *  装备忘卡片的滚动视口。DuiScrollView 的子类，滚动、滚轮、滚动条（悬浮式细滑块，自动隐藏）与内容排版沿用基类，
 *  只覆写两处：
 *    · 排版：内容超出视口时给卡片列表加 kMemoScrollGutter 的右内边距，卡片排窄、让出滚动条那一条，滑块不压在
 *      卡片上；内容放得下时卡片用满整宽。
 *    · 绘制：底色画成卡片外面的颜色（SetOuterBg），不用基类的浅灰底，没有卡片时与面板融为一体。
 */
class CMemoCardScrollView : public balloonwjui::DuiScrollView
{
public:
    CMemoCardScrollView();

    /**
     *  设置底色（卡片外面的颜色），默认白色。值变化时重绘。
     */
    void SetOuterBg(COLORREF color);

    /**
     *  当前画出来的细滑块矩形（宿主窗口客户区坐标），即基类滚动条的 ComputePaintThumbRect；内容没有超出
     *  （滚动条不出现）时为空矩形。供布局单测核对滑块位置。
     */
    RECT ThumbRect() const;

    //DuiScrollView 覆写：先按内容是否超出视口给卡片列表设右内边距（超出时让出 kMemoScrollGutter），再交给基类排版。
    //  rcAvail：分到的矩形（宿主窗口客户区坐标）
    void Layout(const RECT& rcAvail) override;

    //DuiControl 覆写：铺底色，把卡片裁在视口里画，最后画基类的滚动条（浮在右侧窄条之上）
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    COLORREF m_outerBg;          //底色：卡片外面的颜色
};

//卡片内各控件 ctrlId 的分段：第 i 张卡片（从 0 起）占 [kMemoCardIdBase + i × kMemoCardIdStride,
//kMemoCardIdBase + (i + 1) × kMemoCardIdStride)。20000 起的号段在群聊窗与备忘窗口里都没有别的控件使用
const UINT kMemoCardIdBase   = 20000;
const UINT kMemoCardIdStride = 16;

//最多建这么多张卡片（占用 id 20000～27999）；超出的不显示。群备忘与一天之内的备忘都远少于这个数
const int kMemoCardMaxCount = 500;

/**
 *  卡片里的各个部分，ctrlId = kMemoCardIdBase + 序号 × kMemoCardIdStride + 部分。
 */
enum MemoCardPart
{
    kMemoCardPartRow      = 0,   // 卡片外层（balloonhistory::HistoryRow）：悬停判定与双击
    kMemoCardPartBody     = 1,   // 卡片底板（DuiVBox）：圆角浅底，悬停时颜色加深
    kMemoCardPartTitleRow = 2,   // 标题一行（DuiHBox）：标题，以及悬停时出现的「修改」「删除」
    kMemoCardPartTitle    = 3,   // 标题（DuiLabel）
    kMemoCardPartEdit     = 4,   // 「修改」（DuiButton）：只有能修改的卡片才有，悬停时显示
    kMemoCardPartDelete   = 5,   // 「删除」（DuiButton）：同上
    kMemoCardPartNote     = 6,   // 备注（DuiLabel）：没有备注时不建
    kMemoCardPartWhen     = 7,   // 提醒时间或「已结束」（DuiLabel）
    kMemoCardPartRule     = 8,   // 规则一行（DuiLabel）
    kMemoCardPartCreator  = 9,   // 创建人一行（DuiLabel）：个人备忘不建
};

/**
 *  建一个装卡片的滚动视口（CMemoCardScrollView）：内容高度自动测量，滚动条与聊天消息区同一种细滑块。
 *    ctrlId：视口本身的 ctrlId。
 *  @return 视口控件，所有权交给调用方；里面还没有卡片，由 SetMemoCards 填入
 */
std::unique_ptr<balloonwjui::DuiScrollView> CreateMemoCardScroll(UINT ctrlId);

/**
 *  用 cards 整体替换视口里的卡片，保留原来的滚动位置。旧卡片全部销毁，所有悬停态随之清除，
 *  调用方随后可按当前光标位置调一次 UpdateMemoCardHover。
 *  不要在卡片里某个控件发来的通知处理过程中调用：那个控件会被销毁。按钮命令先 PostMessage 给自己再处理。
 *    scroll：CreateMemoCardScroll 建的视口，借用；为 NULL 时什么都不做；
 *    cards：各卡片的内容，按显示顺序；超过 kMemoCardMaxCount 的部分不显示；
 *    outerBg：卡片外面的底色。HistoryRow 悬停时铺满整个外层矩形，取这个颜色使它与背景一致、只显出圆角卡片；
 *             视口是 CMemoCardScrollView 时同时设为它的底色。
 */
void SetMemoCards(balloonwjui::DuiScrollView* scroll, const std::vector<MemoCardText>& cards, COLORREF outerBg);

/**
 *  从通知的 ctrlId 反推卡片序号与部分。
 *    ctrlId：DuiNotify 里的 ctrlId；index：[出参] 第几张卡片，从 0 起；part：[出参] MemoCardPart 的取值。
 *  @return ctrlId 落在卡片 id 段内返回 true；否则返回 false，出参不变
 */
bool MemoCardFromCtrlId(UINT ctrlId, int& index, int& part);

/**
 *  按鼠标位置更新各卡片的悬停态：光标所在的那张卡片显示「修改」「删除」并加深底色，其余的撤掉。
 *  光标必须同时落在视口内：滚出视口的卡片矩形仍在，只看卡片矩形会让视口外的卡片也亮起来。
 *    scroll：卡片视口，借用；为 NULL 或不可见时什么都不做；
 *    pt：光标位置，宿主窗口客户区坐标；传 (-1, -1) 撤掉全部悬停（鼠标离开窗口时）。
 */
void UpdateMemoCardHover(balloonwjui::DuiScrollView* scroll, POINT pt);

/**
 *  一张卡片的高度（逻辑像素）：随有没有备注、有没有创建人一行变化。布局单测与 SetMemoCards 共用。
 */
int MemoCardHeight(const MemoCardText& card);

} // namespace balloonmemo
