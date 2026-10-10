/**
 *  聊天记录管理器「本机存储占用」页的右侧界面实现，说明见 HistoryStorageLayout.h。
 *
 *  balloonwj@qq.com   2026-10-06
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
#include "HistoryStorageLayout.h"

#include "HistoryLayoutKit.h"        // layoutkit：与聊天记录页、文件页同一套小工具
#include "../BalloonMain/MainPanelScrollView.h"   // 与聊天记录页列表同款的悬浮细滚动条
#include "../../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonhistory {

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;

namespace {

typedef DuiLayout::Hint Hint;

// ---- 尺寸（像素），与文件页（HistoryLibraryLayout.cpp）的同名常量取值一致 ----
const int kPadH          = 20;   //左右内边距
const int kHeaderH       = 32;   //标题区高
const int kHeaderPadTop  = 8;    //标题区上内边距
const int kHeaderPadBot  = 6;    //标题区下内边距
const int kHeaderLineH   = 18;   //标题区那一行的高
const int kHeaderGap     = 8;    //标题区一行里各控件之间的间距
const int kExpandLeftW   = 24;   //「»」按钮宽
const int kTitlePt       = 10;   //标题字号（磅），粗体
const int kTitleSlackW   = 6;    //标题量宽之外再留的余量
//标题是粗体、字号比默认字体大一磅，按默认字体量出的宽度要放大到这个百分比才放得下（与文件页标题同一算法）
const int kBoldWidthPercent = 125;
const int kPercentBase      = 100;
const int kSummaryRowH   = 36;   //汇总行高
const int kButtonRowH    = 44;   //按钮行高（含上下内边距）
const int kRowGap8       = 8;    //按钮行里各控件之间的间距
const int kButtonPadW    = 24;   //按钮在文字宽度之外的留白
const int kListPadH      = 8;    //列表内容的左右内边距
const int kListHeadH     = 28;   //列表表头行高
const int kRowH          = 40;   //会话行高
const int kRowPad        = 8;    //会话行左右内边距
const int kColGap        = 10;   //各列之间的间距
const int kSizeColW      = 84;   //图片、文件、语音、合计四列各自的宽度
const int kCleanBtnH     = 26;   //「清理缓存」按钮高
const int kEmptyH        = 60;   //提示文字所占的高

const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const wchar_t* const kExpandLeftText = L"\x00BB";   //「»」

//按钮宽度：文字宽度加留白
int ButtonWidth(LPCTSTR text)
{
    return layoutkit::TextWidth(text) + kButtonPadW;
}

//「清理缓存」那一列的宽度
int CleanColumnWidth()
{
    return ButtonWidth(TR(L"清理缓存"));
}

//表头或会话行：名字（弹性）+ 四列大小 +（会话行）「清理缓存」
std::unique_ptr<DuiControl> BuildRow(const CString& name, const CString& image, const CString& file,
                                     const CString& voice, const CString& total, bool head, int index)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    if (!head)
    {
        row->SetCtrlId((UINT)(kIdStorRowBase + index));
    }
    row->SetPadding(kRowPad, 0, kRowPad, 0);
    row->SetGap(kColGap);
    const COLORREF nameColor = head ? layoutkit::SubtitleColor() : layoutkit::NameColor();
    const COLORREF sizeColor = head ? layoutkit::SubtitleColor() : layoutkit::TimeColor();
    const DWORD align = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    row->AddChild(layoutkit::Label(name, nameColor, align), Hint().Weight(1));
    row->AddChild(layoutkit::Label(image, sizeColor, align), Hint().Fixed(kSizeColW));
    row->AddChild(layoutkit::Label(file, sizeColor, align), Hint().Fixed(kSizeColW));
    row->AddChild(layoutkit::Label(voice, sizeColor, align), Hint().Fixed(kSizeColW));
    row->AddChild(layoutkit::Label(total, head ? sizeColor : layoutkit::NameColor(), align), Hint().Fixed(kSizeColW));
    if (head)
    {
        row->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(CleanColumnWidth()));
    }
    else
    {
        row->AddChild(layoutkit::Button(TR(L"清理缓存"), (UINT)(kIdStorCleanBase + index), DuiButton::Variant::Default),
                      Hint().Fixed(CleanColumnWidth(), kCleanBtnH).AlignC(DuiLayout::AlignCenter));
    }
    return std::unique_ptr<DuiControl>(row.release());
}

//在 root（或其祖先）里找本页根容器
DuiControl* StorageRoot(DuiControl* root)
{
    if (root == NULL)
    {
        return NULL;
    }
    if (root->GetCtrlId() == (UINT)kIdStorBody)
    {
        return root;
    }
    return root->FindCtrlById(kIdStorBody);
}

} // namespace

std::unique_ptr<DuiControl> BuildHistoryStorageList(const HistoryStorageViewData& data)
{
    std::unique_ptr<DuiVBox> list(new DuiVBox());
    list->SetCtrlId(kIdStorList);
    list->SetPadding(kListPadH, 0, kListPadH, 0);
    list->SetGap(0);
    //忙或没有会话：一行提示
    if (data.m_busy || data.m_rows.empty())
    {
        list->AddChild(layoutkit::Label(data.m_emptyText, layoutkit::EmptyTitleColor(),
                                        DT_CENTER | DT_VCENTER | DT_SINGLELINE, kIdStorEmpty),
                       Hint().Fixed(kEmptyH));
        return std::unique_ptr<DuiControl>(list.release());
    }
    list->AddChild(BuildRow(TR(L"会话"), TR(L"图片"), TR(L"文件"), TR(L"语音"), TR(L"合计"), true, 0),
                   Hint().Fixed(kListHeadH));
    list->AddChild(layoutkit::Separator(), Hint().Fixed(1));
    for (size_t i = 0; i < data.m_rows.size() && (int)i < kIdStorRowCapacity; ++i)
    {
        const HistoryStorageRowView& r = data.m_rows[i];
        list->AddChild(BuildRow(r.m_name, r.m_imageText, r.m_fileText, r.m_voiceText, r.m_totalText, false, (int)i),
                       Hint().Fixed(kRowH));
    }
    return std::unique_ptr<DuiControl>(list.release());
}

std::unique_ptr<DuiControl> BuildHistoryStorageBody(const HistoryStorageViewData& data, int listRightInsetPx)
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetCtrlId(kIdStorBody);
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // 1. 标题区：「»」+ 标题 + 灰色说明
    std::unique_ptr<DuiVBox> header(new DuiVBox());
    header->SetPadding(kPadH, kHeaderPadTop, kPadH, kHeaderPadBot);
    header->SetGap(0);
    std::unique_ptr<DuiHBox> headerRow(new DuiHBox());
    headerRow->SetCtrlId(kIdStorHeaderRow);
    headerRow->SetGap(kHeaderGap);
    std::unique_ptr<DuiControl> expand = layoutkit::Button(kExpandLeftText, kIdStorExpandLeft,
                                                           DuiButton::Variant::Ghost);
    expand->SetVisible(data.m_showExpandLeft);
    headerRow->AddChild(std::move(expand), Hint().Fixed(kExpandLeftW));
    const LPCTSTR titleText = TR(L"本机存储占用");
    std::unique_ptr<DuiControl> title = layoutkit::Label(titleText, layoutkit::TitleColor(),
                                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                                         kIdStorTitle);
    DuiLabel* titleLabel = dynamic_cast<DuiLabel*>(title.get());
    if (titleLabel != NULL)
    {
        titleLabel->SetTextPointSize(kTitlePt, true);
    }
    headerRow->AddChild(std::move(title),
                        Hint().Fixed(layoutkit::TextWidth(titleText) * kBoldWidthPercent / kPercentBase + kTitleSlackW));
    headerRow->AddChild(layoutkit::Label(data.m_subtitle, layoutkit::SubtitleColor(),
                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, kIdStorSubtitle),
                        Hint().Weight(1));
    header->AddChild(std::move(headerRow), Hint().Fixed(kHeaderLineH));
    root->AddChild(std::move(header), Hint().Fixed(kHeaderH));
    root->AddChild(layoutkit::Separator(), Hint().Fixed(1));

    // 2. 汇总行
    std::unique_ptr<DuiHBox> summaryRow(new DuiHBox());
    summaryRow->SetPadding(kPadH, 0, kPadH, 0);
    summaryRow->AddChild(layoutkit::Label(data.m_summary, layoutkit::NameColor(),
                                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, kIdStorSummary),
                         Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(summaryRow.release()), Hint().Fixed(kSummaryRowH));

    // 3. 按钮行：重新统计、清理全部会话的缓存（靠左排）
    std::unique_ptr<DuiHBox> buttonRow(new DuiHBox());
    buttonRow->SetCtrlId(kIdStorButtonRow);
    buttonRow->SetPadding(kPadH, kRowGap8, kPadH, kRowGap8);
    buttonRow->SetGap(kRowGap8);
    const LPCTSTR refreshText = TR(L"重新统计");
    std::unique_ptr<DuiControl> refresh = layoutkit::Button(refreshText, kIdStorRefresh, DuiButton::Variant::Default);
    refresh->SetEnabled(!data.m_busy);
    buttonRow->AddChild(std::move(refresh), Hint().Fixed(ButtonWidth(refreshText)));
    const LPCTSTR cleanAllText = TR(L"清理全部会话的缓存");
    std::unique_ptr<DuiControl> cleanAll = layoutkit::Button(cleanAllText, kIdStorCleanAll,
                                                             DuiButton::Variant::Outlined);
    cleanAll->SetEnabled(data.m_canCleanAll && !data.m_busy);
    buttonRow->AddChild(std::move(cleanAll), Hint().Fixed(ButtonWidth(cleanAllText)));
    buttonRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(buttonRow.release()), Hint().Fixed(kButtonRowH));
    root->AddChild(layoutkit::Separator(), Hint().Fixed(1));

    // 4. 列表：与聊天记录页同款的滚动视口
    std::unique_ptr<balloonmain::MainPanelScrollView> scroll(new balloonmain::MainPanelScrollView());
    scroll->SetCtrlId(kIdStorScroll);
    scroll->SetAutoContentHeight(true);
    scroll->SetContent(BuildHistoryStorageList(data));
    const int rightInset = (listRightInsetPx > 0) ? listRightInsetPx : 0;
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1).Margin(0, 0, rightInset, 0));
    return std::unique_ptr<DuiControl>(root.release());
}

void ApplyHistoryStorageData(DuiControl* root, const HistoryStorageViewData& data)
{
    DuiControl* page = StorageRoot(root);
    if (page == NULL)
    {
        return;
    }
    DuiControl* expand = page->FindCtrlById(kIdStorExpandLeft);
    DuiControl* headerRow = page->FindCtrlById(kIdStorHeaderRow);
    if (expand != NULL && headerRow != NULL)
    {
        expand->SetVisible(data.m_showExpandLeft);
        //「»」显隐变化不会自己重排（开发约定文档 UI 约定第 5 条）
        headerRow->ForceLayout(headerRow->GetRect());
        headerRow->Invalidate();
    }
    DuiLabel* subtitle = dynamic_cast<DuiLabel*>(page->FindCtrlById(kIdStorSubtitle));
    if (subtitle != NULL)
    {
        subtitle->SetText(data.m_subtitle);
        subtitle->Invalidate();
    }
    DuiLabel* summary = dynamic_cast<DuiLabel*>(page->FindCtrlById(kIdStorSummary));
    if (summary != NULL)
    {
        summary->SetText(data.m_summary);
        summary->Invalidate();
    }
    DuiControl* refresh = page->FindCtrlById(kIdStorRefresh);
    if (refresh != NULL)
    {
        refresh->SetEnabled(!data.m_busy);
        refresh->Invalidate();
    }
    DuiControl* cleanAll = page->FindCtrlById(kIdStorCleanAll);
    if (cleanAll != NULL)
    {
        cleanAll->SetEnabled(data.m_canCleanAll && !data.m_busy);
        cleanAll->Invalidate();
    }
    balloonmain::MainPanelScrollView* scroll =
        dynamic_cast<balloonmain::MainPanelScrollView*>(page->FindCtrlById(kIdStorScroll));
    if (scroll != NULL)
    {
        scroll->SetContent(BuildHistoryStorageList(data));
        scroll->ForceLayout(scroll->GetRect());
        scroll->Invalidate();
    }
}

} // namespace balloonhistory
