/**
 *  聊天记录管理器时间轴模式的界面部分实现，说明见 HistoryTimelineLayout.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
//本文件被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用、不走预编译头，自己引入 balloonui 头文件约定要求的 ATL/WTL
#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>
#include <gdiplus.h>
#include "HistoryTimelineLayout.h"

#include "HistoryLayoutKit.h"        // layoutkit：与聊天记录页同一套小工具
#include "HistoryWindowLayout.h"     // kIdHistory* 控件编号
#include "../../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonhistory {

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiProgressBar;

namespace {

typedef DuiLayout::Hint Hint;

// ---- 导航条尺寸（像素）----
const int kNavPadH        = 8;    //内容左右内边距
const int kNavPadV        = 8;    //内容上下内边距
const int kNavYearH       = 26;   //年标题行高
const int kNavYearGapTop  = 6;    //第二年起，年标题行上方多留的间距
const int kNavMonthH      = 26;   //月份行高
const int kNavMonthPadH   = 4;    //月份行左右内边距
const int kNavMonthGap    = 6;    //月份行里各列之间的间距
const int kNavMonthLabelW = 34;   //「10月」那一列的宽
const int kNavHitW        = 24;   //命中条数那一列的宽
const int kNavBarH        = 8;    //消息量条的高
const int kNavYearPt      = 9;    //年份的字号（磅），粗体
const int kNavLegendLineH = 18;   //图例每一行的高
const int kNavLegendGapTop = 10;  //图例上方的间距
const int kNavCountPadW   = 4;    //年标题行右侧条数在文字宽度之外的余量
const int kPercentFull    = 100;  //消息量条的满值

// ---- 颜色（布局图 tmp/histenhance/layout.html 图 4 的 .nav 一组）----
const COLORREF kColorNavBg        = RGB(0xF7, 0xF8, 0xFA);   //导航条底色
const COLORREF kColorNavCurrentBg = RGB(0xE6, 0xEE, 0xFC);   //当前月份那一行的底色
const COLORREF kColorNavCurrentFg = RGB(0x2D, 0x6C, 0xDF);   //当前月份那一行的文字
const COLORREF kColorNavMonth     = RGB(0x55, 0x55, 0x55);   //其它月份的文字
const COLORREF kColorNavBarBg     = RGB(0xE3, 0xE7, 0xEE);   //消息量条的底
const COLORREF kColorNavBarFill   = RGB(0x9D, 0xB5, 0xE8);   //消息量条的填充
const COLORREF kColorNavHit       = RGB(0xE0, 0xA8, 0x00);   //命中条数
const COLORREF kColorNavYear      = RGB(0x33, 0x33, 0x33);   //年份

//年标题行最前面的展开 / 收起标记
const wchar_t* const kNavExpandedMark  = L"\x25BE ";   //「▾ 」
const wchar_t* const kNavCollapsedMark = L"\x25B8 ";   //「▸ 」

//一年的标题行：「▾ 2026」+ 右侧「1016 条」
std::unique_ptr<DuiControl> BuildYearRow(const HistoryTimelineYear& year, int yearIndex, bool expanded)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetCtrlId((UINT)(kIdHistoryNavYearBase + yearIndex));
    row->SetGap(0);
    CString yearText;
    yearText.Format(_T("%s%d"), expanded ? kNavExpandedMark : kNavCollapsedMark, year.m_year);
    std::unique_ptr<DuiControl> yearLabel = layoutkit::Label(yearText, kColorNavYear,
                                                             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DuiLabel* label = dynamic_cast<DuiLabel*>(yearLabel.get());
    if (label != NULL)
    {
        label->SetTextPointSize(kNavYearPt, true);
    }
    row->AddChild(std::move(yearLabel), Hint().Weight(1));
    const CString countText = TRF(L"{0} 条", (long long)year.m_count).c_str();
    row->AddChild(layoutkit::Label(countText, layoutkit::SubtitleColor(), DT_RIGHT | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(layoutkit::TextWidth(countText) + kNavCountPadW));
    return std::unique_ptr<DuiControl>(row.release());
}

//一个月份行：「10月」+ 消息量条 + 命中条数
std::unique_ptr<DuiControl> BuildMonthRow(const HistoryTimelineMonth& month, int monthIndex, int64_t maxCount,
                                          bool current)
{
    //外层是竖向容器：只有 DuiVBox 能画底色，当前月份那一行靠它画高亮底色；编号也给在它身上
    std::unique_ptr<DuiVBox> outer(new DuiVBox());
    outer->SetCtrlId((UINT)(kIdHistoryNavMonthBase + monthIndex));
    outer->SetGap(0);
    if (current)
    {
        outer->SetBgColor(kColorNavCurrentBg);
    }
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetPadding(kNavMonthPadH, 0, kNavMonthPadH, 0);
    row->SetGap(kNavMonthGap);
    const CString monthText = TRF(L"{0}月", month.m_month).c_str();
    row->AddChild(layoutkit::Label(monthText, current ? kColorNavCurrentFg : kColorNavMonth,
                                   DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(layoutkit::GrowWidth(monthText, kNavMonthLabelW, kNavCountPadW)));

    //消息量条：DuiProgressBar 只画底与填充，不写百分比；边框与底同色
    std::unique_ptr<DuiProgressBar> bar(new DuiProgressBar());
    bar->SetRange(0, kPercentFull);
    bar->SetPos(TimelineBarPercent(month.m_count, maxCount), false);
    bar->SetShowPercent(false);
    bar->SetBgColor(kColorNavBarBg);
    bar->SetBorderColor(kColorNavBarBg);
    bar->SetFillColor(kColorNavBarFill);
    Hint barHint;
    barHint.fixedCross = kNavBarH;
    barHint.alignCross = DuiLayout::AlignCenter;
    row->AddChild(std::unique_ptr<DuiControl>(bar.release()), barHint);

    //命中条数：没有命中时留空
    CString hitText;
    if (month.m_hits > 0)
    {
        hitText.Format(_T("%lld"), (long long)month.m_hits);
    }
    std::unique_ptr<DuiControl> hitLabel = layoutkit::Label(hitText, kColorNavHit,
                                                            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DuiLabel* hit = dynamic_cast<DuiLabel*>(hitLabel.get());
    if (hit != NULL)
    {
        hit->SetTextPointSize(kNavYearPt, true);
    }
    row->AddChild(std::move(hitLabel), Hint().Fixed(kNavHitW));
    outer->AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(outer.release());
}

//在 root（或其祖先）里按编号找控件；root 本身就是时也返回它
DuiControl* FindIn(DuiControl* root, UINT id)
{
    if (root == NULL)
    {
        return NULL;
    }
    if (root->GetCtrlId() == id)
    {
        return root;
    }
    return root->FindCtrlById(id);
}

} // namespace

std::unique_ptr<DuiControl> BuildTimelineNavContent(const std::vector<HistoryTimelineYear>& years,
                                                    int64_t currentMonthKey, const std::set<int>& expandedYears)
{
    std::unique_ptr<DuiVBox> box(new DuiVBox());
    box->SetCtrlId(kIdHistoryNavList);
    box->SetPadding(kNavPadH, kNavPadV, kNavPadH, kNavPadV);
    box->SetGap(0);
    box->SetBgColor(kColorNavBg);

    const int64_t maxCount = TimelineMaxMonthCount(years);
    const int64_t kMonthKeyYearFactor = 100;
    int monthIndex = 0;
    for (size_t y = 0; y < years.size() && y < kIdHistoryNavYearCapacity; ++y)
    {
        if (y > 0)
        {
            box->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kNavYearGapTop));
        }
        const bool expanded = expandedYears.count(years[y].m_year) > 0;
        box->AddChild(BuildYearRow(years[y], (int)y, expanded), Hint().Fixed(kNavYearH));
        for (size_t m = 0; m < years[y].m_months.size(); ++m, ++monthIndex)
        {
            //收起的年份不建月份行，编号照样往下数，与 TimelineNavMonthOfId 的换算一致
            if (!expanded || monthIndex >= (int)kIdHistoryNavMonthCapacity)
            {
                continue;
            }
            const HistoryTimelineMonth& month = years[y].m_months[m];
            const bool current = (currentMonthKey == (int64_t)month.m_year * kMonthKeyYearFactor + month.m_month);
            box->AddChild(BuildMonthRow(month, monthIndex, maxCount, current), Hint().Fixed(kNavMonthH));
        }
    }

    //图例
    if (!years.empty())
    {
        box->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kNavLegendGapTop));
        //每一行不超过 8 个字：导航条内容只有约 130 像素宽，125% 缩放下 9 个字就放不下了（2026-10-06 实机核对）
        const wchar_t* const legends[] = { TR_NOOP(L"条形：当月消息量"), TR_NOOP(L"黄色数字：命中数"),
                                           TR_NOOP(L"点月份跳到该月") };
        for (size_t i = 0; i < sizeof(legends) / sizeof(legends[0]); ++i)
        {
            box->AddChild(layoutkit::Label(TR(legends[i]), layoutkit::SubtitleColor(),
                                           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                          Hint().Fixed(kNavLegendLineH));
        }
    }
    return std::unique_ptr<DuiControl>(box.release());
}

bool TimelineNavMonthOfId(const std::vector<HistoryTimelineYear>& years, UINT id, int& year, int& month)
{
    if (id < (UINT)kIdHistoryNavMonthBase || id >= (UINT)kIdHistoryNavMonthBase + kIdHistoryNavMonthCapacity)
    {
        return false;
    }
    int index = (int)(id - (UINT)kIdHistoryNavMonthBase);
    for (size_t y = 0; y < years.size(); ++y)
    {
        if (index < (int)years[y].m_months.size())
        {
            year = years[y].m_months[(size_t)index].m_year;
            month = years[y].m_months[(size_t)index].m_month;
            return true;
        }
        index -= (int)years[y].m_months.size();
    }
    return false;
}

bool TimelineNavYearOfId(const std::vector<HistoryTimelineYear>& years, UINT id, int& year)
{
    if (id < (UINT)kIdHistoryNavYearBase || id >= (UINT)kIdHistoryNavYearBase + kIdHistoryNavYearCapacity)
    {
        return false;
    }
    const size_t index = (size_t)(id - (UINT)kIdHistoryNavYearBase);
    if (index >= years.size())
    {
        return false;
    }
    year = years[index].m_year;
    return true;
}

void ApplyHistoryTimelineMode(DuiControl* root, bool timeline)
{
    if (root == NULL)
    {
        return;
    }
    DuiButton* listChip = dynamic_cast<DuiButton*>(FindIn(root, kIdHistoryViewList));
    DuiButton* timelineChip = dynamic_cast<DuiButton*>(FindIn(root, kIdHistoryViewTimeline));
    if (listChip != NULL && timelineChip != NULL)
    {
        listChip->SetVariant(timeline ? DuiButton::Variant::Default : DuiButton::Variant::Primary);
        timelineChip->SetVariant(timeline ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
        listChip->Invalidate();
        timelineChip->Invalidate();
    }
    if (!timeline)
    {
        ApplyHistoryHitBar(root, false, CString(), CString(), false);
    }
    DuiControl* nav = FindIn(root, kIdHistoryNav);
    DuiControl* host = FindIn(root, kIdHistoryListHost);
    if (nav != NULL)
    {
        nav->SetVisible(timeline);
    }
    //SetVisible 不会重排：导航条显隐改变的是列表那一行的横向分配（开发约定文档 UI 约定第 5 条）
    if (host != NULL)
    {
        host->ForceLayout(host->GetRect());
        host->Invalidate();
    }
}

void ApplyHistoryHitBar(DuiControl* root, bool show, const CString& info, const CString& position, bool canNavigate)
{
    DuiControl* bar = FindIn(root, kIdHistoryHitBar);
    if (bar == NULL)
    {
        return;
    }
    DuiLabel* infoLabel = dynamic_cast<DuiLabel*>(bar->FindCtrlById(kIdHistoryHitInfo));
    DuiLabel* posLabel = dynamic_cast<DuiLabel*>(bar->FindCtrlById(kIdHistoryHitPos));
    DuiControl* prev = bar->FindCtrlById(kIdHistoryHitPrev);
    DuiControl* next = bar->FindCtrlById(kIdHistoryHitNext);
    if (infoLabel != NULL)
    {
        infoLabel->SetText(info);
    }
    if (posLabel != NULL)
    {
        posLabel->SetText(position);
    }
    if (prev != NULL)
    {
        prev->SetEnabled(canNavigate);
    }
    if (next != NULL)
    {
        next->SetEnabled(canNavigate);
    }
    const bool changed = (bar->IsVisible() != show);
    bar->SetVisible(show);
    bar->Invalidate();
    //命中条是聊天记录页根容器的直接子控件：显隐变化后按根容器当前矩形重排（开发约定文档 UI 约定第 5 条）
    DuiControl* body = bar->GetParent();
    if (changed && body != NULL)
    {
        body->ForceLayout(body->GetRect());
        body->Invalidate();
    }
}

bool IsHistoryHitBarVisible(DuiControl* root)
{
    DuiControl* bar = FindIn(root, kIdHistoryHitBar);
    return bar != NULL && bar->IsVisible();
}

} // namespace balloonhistory
