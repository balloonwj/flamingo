/**
 *  聊天记录管理器「导出」选项浮层的内容实现，说明见 HistoryExportLayout.h。
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
#include "HistoryExportLayout.h"

#include "HistoryLayoutKit.h"        // layoutkit：与消息记录窗同一套小工具
#include "../BalloonMain/MainPanelScrollView.h"   // 会话勾选列表的滚动视口
#include "../../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonhistory {

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiComboBox;

namespace {

typedef DuiLayout::Hint Hint;

const int kPad        = 16;   //四周内边距
const int kGap        = 8;    //各行之间、行内各控件之间的间距
const int kTitleH     = 28;   //标题行高
const int kTitlePt    = 11;   //标题字号（磅），粗体
const int kRowH       = 32;   //范围、时间、格式各行的高
const int kCheckRowH  = 28;   //复选框各行的高
const int kButtonRowH = 34;   //底部按钮行高
const int kInfoH      = 22;   //「已选 N 个会话」一行的高
const int kLabelW     = 40;   //行首「范围」「时间」「格式」的宽度；放不下译文时按文字加宽
const int kChipW      = 72;   //胶囊的设计宽度；放不下文字时按文字加宽
const int kComboMinW  = 160;  //时间下拉的最小宽度
const int kButtonW    = 72;   //「取消」「导出」的设计宽度
const int kCheckChromeW = 30; //复选框在文字宽度之外要占的宽度（方框与间距）

const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);

//时间下拉各项，下标与 HistoryExportOptions::m_range 一一对应（0～5 同 HistoryTimeRange）
const wchar_t* const kRangeTexts[] =
{
    TR_NOOP(L"全部时间"), TR_NOOP(L"近 7 天"), TR_NOOP(L"近 30 天"), TR_NOOP(L"近 3 个月"),
    TR_NOOP(L"近半年"), TR_NOOP(L"近一年"), TR_NOOP(L"按当前筛选条件"),
};

//行首的小标签
std::unique_ptr<DuiControl> RowLabel(LPCTSTR text)
{
    return layoutkit::Label(text, layoutkit::SubtitleColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

//行首标签的宽度
int RowLabelWidth()
{
    int w = kLabelW;
    const LPCTSTR texts[] = { TR(L"范围"), TR(L"时间"), TR(L"格式") };
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i)
    {
        const int need = layoutkit::TextWidth(texts[i]) + kGap;
        if (need > w)
        {
            w = need;
        }
    }
    return w;
}

//复选框（透明底，与「通配符」复选框同一外观）
std::unique_ptr<DuiControl> CheckBox(LPCTSTR text, UINT id, bool checked, int& outWidth)
{
    std::unique_ptr<DuiButton> box(new DuiButton());
    box->SetButtonType(DuiButton::StyleCheckbox);
    box->SetVariant(DuiButton::Variant::Ghost);
    box->SetText(text);
    box->SetCtrlId(id);
    box->SetCheck(checked, false);
    outWidth = layoutkit::TextWidth(text) + kCheckChromeW;
    return std::unique_ptr<DuiControl>(box.release());
}

//一行复选框
std::unique_ptr<DuiControl> CheckRow(LPCTSTR text, UINT id, bool checked)
{
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    row->SetGap(0);
    int w = 0;
    std::unique_ptr<DuiControl> box = CheckBox(text, id, checked, w);
    row->AddChild(std::move(box), Hint().Fixed(w));
    row->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(row.release());
}

//设置一枚胶囊的选中态
void SetChipActive(DuiControl* root, UINT id, bool active)
{
    DuiButton* chip = dynamic_cast<DuiButton*>(root->FindCtrlById(id));
    if (chip != NULL)
    {
        chip->SetVariant(active ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
        chip->Invalidate();
    }
}

} // namespace

std::unique_ptr<DuiControl> BuildHistoryExportPopupContent(const HistoryExportOptions& opts)
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetCtrlId(kIdExpRoot);
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPad, kPad, kPad, kPad);
    root->SetGap(kGap);

    //标题
    std::unique_ptr<DuiControl> title = layoutkit::Label(TR(L"导出聊天记录"), layoutkit::TitleColor(),
                                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DuiLabel* titleLabel = dynamic_cast<DuiLabel*>(title.get());
    if (titleLabel != NULL)
    {
        titleLabel->SetTextPointSize(kTitlePt, true);
    }
    root->AddChild(std::move(title), Hint().Fixed(kTitleH));

    const int labelW = RowLabelWidth();

    //范围：当前会话 / 选择会话
    std::unique_ptr<DuiHBox> scopeRow(new DuiHBox());
    scopeRow->SetCtrlId(kIdExpScopeRow);
    scopeRow->SetGap(kGap);
    scopeRow->AddChild(RowLabel(TR(L"范围")), Hint().Fixed(labelW));
    const LPCTSTR currentText = TR(L"当前会话");
    const LPCTSTR selectedText = TR(L"选择会话");
    scopeRow->AddChild(layoutkit::Chip(currentText, kIdExpScopeCurrent, opts.m_scope == kHistoryExportScopeCurrent),
                       Hint().Fixed(layoutkit::GrowWidth(currentText, kChipW, layoutkit::ChipPadWidth())));
    scopeRow->AddChild(layoutkit::Chip(selectedText, kIdExpScopeSelected, opts.m_scope == kHistoryExportScopeSelected),
                       Hint().Fixed(layoutkit::GrowWidth(selectedText, kChipW, layoutkit::ChipPadWidth())));
    scopeRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(scopeRow.release()), Hint().Fixed(kRowH));

    //会话勾选区（选择会话时）与说明（当前会话时），占据中间的弹性空间
    std::unique_ptr<DuiVBox> convBox(new DuiVBox());
    convBox->SetCtrlId(kIdExpConvBox);
    convBox->SetGap(0);
    std::unique_ptr<balloonmain::MainPanelScrollView> scroll(new balloonmain::MainPanelScrollView());
    scroll->SetCtrlId(kIdExpConvScroll);
    scroll->SetAutoContentHeight(true);
    scroll->SetContent(std::unique_ptr<DuiControl>(new DuiVBox()));
    convBox->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));
    convBox->AddChild(layoutkit::Label(_T(""), layoutkit::SubtitleColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE,
                                       kIdExpConvInfo),
                      Hint().Fixed(kInfoH));
    root->AddChild(std::unique_ptr<DuiControl>(convBox.release()), Hint().Weight(1));
    std::unique_ptr<DuiControl> hint = layoutkit::Label(TR(L"导出右侧正在查看的会话"), layoutkit::SubtitleColor(),
                                                        DT_LEFT | DT_TOP | DT_WORDBREAK, kIdExpCurrentHint);
    root->AddChild(std::move(hint), Hint().Weight(1));

    //时间
    std::unique_ptr<DuiHBox> rangeRow(new DuiHBox());
    rangeRow->SetGap(kGap);
    rangeRow->AddChild(RowLabel(TR(L"时间")), Hint().Fixed(labelW));
    std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
    combo->SetCtrlId(kIdExpRangeCombo);
    int comboW = kComboMinW;
    for (int i = 0; i < kHistoryExportRangeCount; ++i)
    {
        const LPCTSTR text = TR(kRangeTexts[i]);
        combo->AddString(text);
        const int need = layoutkit::TextWidth(text) + layoutkit::ComboChromeWidth();
        if (need > comboW)
        {
            comboW = need;
        }
    }
    combo->SetCurSel((opts.m_range >= 0 && opts.m_range < kHistoryExportRangeCount) ? opts.m_range : 0, false);
    rangeRow->AddChild(std::unique_ptr<DuiControl>(combo.release()), Hint().Fixed(comboW));
    rangeRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(rangeRow.release()), Hint().Fixed(kRowH));

    //格式
    std::unique_ptr<DuiHBox> fmtRow(new DuiHBox());
    fmtRow->SetGap(kGap);
    fmtRow->AddChild(RowLabel(TR(L"格式")), Hint().Fixed(labelW));
    const LPCTSTR htmlText = TR(L"网页");
    const LPCTSTR plainText = TR(L"纯文本");
    fmtRow->AddChild(layoutkit::Chip(htmlText, kIdExpFmtHtml, opts.m_html),
                     Hint().Fixed(layoutkit::GrowWidth(htmlText, kChipW, layoutkit::ChipPadWidth())));
    fmtRow->AddChild(layoutkit::Chip(plainText, kIdExpFmtText, !opts.m_html),
                     Hint().Fixed(layoutkit::GrowWidth(plainText, kChipW, layoutkit::ChipPadWidth())));
    fmtRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(fmtRow.release()), Hint().Fixed(kRowH));

    //附件与打包
    root->AddChild(CheckRow(TR(L"包含图片"), kIdExpImages, opts.m_includeImages), Hint().Fixed(kCheckRowH));
    root->AddChild(CheckRow(TR(L"包含本机已有的文件"), kIdExpFiles, opts.m_includeFiles), Hint().Fixed(kCheckRowH));
    root->AddChild(CheckRow(TR(L"打包为 zip"), kIdExpZip, opts.m_zip), Hint().Fixed(kCheckRowH));

    //按钮：靠右，取消在左、导出在右
    std::unique_ptr<DuiHBox> buttonRow(new DuiHBox());
    buttonRow->SetGap(kGap);
    buttonRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    const LPCTSTR cancelText = TR(L"取消");
    const LPCTSTR okText = TR(L"导出");
    buttonRow->AddChild(layoutkit::Button(cancelText, kIdExpCancel, DuiButton::Variant::Default),
                        Hint().Fixed(layoutkit::GrowWidth(cancelText, kButtonW, layoutkit::ChipPadWidth())));
    buttonRow->AddChild(layoutkit::Button(okText, kIdExpOk, DuiButton::Variant::Primary),
                        Hint().Fixed(layoutkit::GrowWidth(okText, kButtonW, layoutkit::ChipPadWidth())));
    root->AddChild(std::unique_ptr<DuiControl>(buttonRow.release()), Hint().Fixed(kButtonRowH));

    ApplyHistoryExportPopupState(root.get(), opts);
    return std::unique_ptr<DuiControl>(root.release());
}

void ApplyHistoryExportPopupState(DuiControl* root, const HistoryExportOptions& opts)
{
    if (root == NULL)
    {
        return;
    }
    const bool selected = (opts.m_scope == kHistoryExportScopeSelected);
    SetChipActive(root, kIdExpScopeCurrent, !selected);
    SetChipActive(root, kIdExpScopeSelected, selected);
    SetChipActive(root, kIdExpFmtHtml, opts.m_html);
    SetChipActive(root, kIdExpFmtText, !opts.m_html);
    DuiControl* convBox = root->FindCtrlById(kIdExpConvBox);
    DuiControl* hint = root->FindCtrlById(kIdExpCurrentHint);
    if (convBox != NULL)
    {
        convBox->SetVisible(selected);
    }
    if (hint != NULL)
    {
        hint->SetVisible(!selected);
    }
    //显隐变化不会自己重排；勾选区是根容器的直接子控件，对根容器强制重排一次即可，勾选区自己再排一次让列表就位
    root->ForceLayout(root->GetRect());
    if (convBox != NULL && selected)
    {
        convBox->ForceLayout(convBox->GetRect());
    }
    root->Invalidate();
}

} // namespace balloonhistory
