/**
 *  聊天记录管理器「文件」「图片」两页的右侧界面实现，说明见 HistoryLibraryLayout.h。
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
#include "HistoryLibraryLayout.h"

#include <time.h>
#include "HistoryLayoutKit.h"        // layoutkit：与聊天记录页同一套小工具
#include "HistoryLibraryQuery.h"     // HistoryLibraryKind 等取值
#include "HistoryHighlightLabel.h"   // 文件名的搜索命中高亮
#include "HistoryRow.h"              // HistoryRow（悬停按钮）、HistoryRowThumb（类型色块）
#include "HistoryTipSearchBox.h"     // 悬停时显示搜索语法说明的搜索框
#include "../BalloonMain/MainPanelScrollView.h"   // 与聊天记录页列表同款的悬浮细滚动条
#include "../../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonhistory {

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiComboBox;
using balloonwjui::DuiSearchBox;

namespace {

typedef DuiLayout::Hint Hint;

// ---- 尺寸（像素），与聊天记录页右侧界面（HistoryWindowLayout.cpp）的同名常量取值一致 ----
const int kPadH          = 20;   //左右内边距
const int kHeaderH       = 32;   //标题区高
const int kHeaderPadTop  = 8;    //标题区上内边距
const int kHeaderPadBot  = 6;    //标题区下内边距
const int kHeaderLineH   = 18;   //标题区那一行的高
const int kHeaderGap     = 8;    //标题区一行里各控件之间的间距
const int kExpandLeftW   = 24;   //「»」按钮宽
const int kTitlePt       = 10;   //标题字号（磅），粗体
const int kTitleMaxW     = 320;  //标题最多占多宽，再长以省略号截断
const int kTitleSlackW   = 6;    //标题量宽之外再留的余量
//标题是粗体、字号比默认字体大一磅，按默认字体量出的宽度要放大到这个百分比才放得下
const int kBoldWidthPercent = 125;
const int kPercentBase      = 100;
const int kSearchRowH    = 44;   //搜索行高（含上下内边距）
const int kFilterRowH    = 44;   //筛选行高
const int kRowGap8       = 8;    //搜索行、筛选行里各控件之间的间距
const int kChipW         = 56;   //方向胶囊的设计宽度（两个汉字）；放不下文字时按文字加宽
const int kComboMinW     = 110;  //来源、类型下拉的最小宽度
const int kFooterH       = 52;   //底部行高
const int kFooterPadV    = 8;    //底部上下内边距
const int kPagerGap      = 4;    //分页器按钮之间的间距
const int kPagerBtnW     = 48;   //分页「首页 / 末页」按钮宽
const int kPagerCellW    = 34;   //分页箭头 / 页码按钮宽
const int kBatchBtnPadW  = 24;   //「下载本页全部」按钮在文字宽度之外的留白
const int kListPadH      = 8;    //列表内容的左右内边距
const int kEmptyTitleH   = 26;   //空状态主文案行高
const int kEmptySubH     = 22;   //空状态副文案行高
// ---- 文件页的一行 ----
const int kRowH          = 56;   //行高
const int kRowPad        = 8;    //行的四周内边距
const int kRowGap        = 10;   //行内各列之间的间距
const int kBlockSize     = 32;   //类型色块边长
const int kNameLineH     = 20;   //文件名那一行的高
const int kSourceLineH   = 18;   //来源那一行的高
const int kSizeW         = 72;   //大小列宽
const int kTimeW         = 92;   //时间列宽
const int kTagGap        = 6;    //来源行里标签与文字之间的间距
const int kTagPadW       = 4;    //标签在文字宽度之外的余量
const int kActionH       = 26;   //行内操作按钮的高
const int kActionGap     = 4;    //行内操作按钮之间的间距
const int kActionPadW    = 16;   //行内操作按钮在文字宽度之外的留白
const int kActionMinW    = 40;   //行内操作按钮的最小宽度
const int kMoreW         = 28;   //「⋯」按钮宽
const int kPercentMax    = 100;  //下载进度的满值
// ---- 链接页、与我有关页的一行（docs/聊天记录管理器方案.md §12.4、§12.5）----
const int kDomainMaxW    = 220;  //域名最多占多宽，再长以省略号截断
const int kDomainPt      = 9;    //域名的字号（磅），粗体；与列表正文同一字号
const int kDomainGap     = 8;    //域名与完整链接之间的间距
const int kRelTagPadW    = 12;   //与我有关页行首标签在文字宽度之外的留白

// ---- 配色 ----
const COLORREF kColorWhite     = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorGroupTag  = RGB(0x2F, 0x5F, 0xB8);   //「群文件」标签文字
const COLORREF kColorMineTag   = RGB(0x2D, 0x7A, 0x45);   //「我发送的 / 我上传的」标签文字
const COLORREF kColorFileName  = RGB(0x2A, 0x2A, 0x2A);   //文件名
const COLORREF kColorLinkUrl   = RGB(0x2F, 0x6F, 0xD0);   //链接页的完整链接
const COLORREF kColorMentionTag = RGB(0xD9, 0x6C, 0x1E);  //与我有关页「@我」标签
const COLORREF kColorReplyTag  = RGB(0x2F, 0x5F, 0xB8);   //与我有关页「回复我」标签
//链接页行首色块里写的字（色块按「扩展名」配色，这个词不在任何类别里，取默认底色）
const wchar_t* const kLinkBlockText = L"URL";

// ---- 文字 ----
const wchar_t* const kExpandLeftText = L"\x00BB";   //「»」
const wchar_t* const kMoreText       = L"\x22EF";   //「⋯」
//方向胶囊，下标与 HistoryLibraryDirection 一一对应
const wchar_t* const kDirTexts[] = { TR_NOOP(L"全部"), TR_NOOP(L"我发送的"), TR_NOOP(L"我接收的") };
const UINT kDirIds[] = { kIdLibDirAll, kIdLibDirSent, kIdLibDirReceived };
const int kDirCount = 3;
//与我有关页的关系胶囊，下标与 HistoryLibraryRelation 一一对应
const wchar_t* const kRelTexts[] = { TR_NOOP(L"全部"), TR_NOOP(L"@我的"), TR_NOOP(L"回复我的"), TR_NOOP(L"我发的") };
const UINT kRelIds[] = { kIdLibRelAll, kIdLibRelMention, kIdLibRelReply, kIdLibRelMine };
const int kRelCount = 4;
//与我有关页行首标签，下标与 HistoryLibraryRelation 一一对应（「全部」不会出现在行上）
const wchar_t* const kRelTagTexts[] = { L"", TR_NOOP(L"@我"), TR_NOOP(L"回复我"), TR_NOOP(L"我发的") };
//来源下拉，下标与 HistoryLibrarySource 一一对应
const wchar_t* const kSourceTexts[] =
{
    TR_NOOP(L"聊天文件和群文件"), TR_NOOP(L"只看聊天文件"), TR_NOOP(L"只看群文件"),
};
//类型下拉，下标与 HistoryFileCategory 一一对应
const wchar_t* const kCategoryTexts[] =
{
    TR_NOOP(L"全部类型"), TR_NOOP(L"文档"), TR_NOOP(L"图片"), TR_NOOP(L"视频"),
    TR_NOOP(L"音频"), TR_NOOP(L"压缩包"), TR_NOOP(L"其它"), TR_NOOP(L"文件夹"),
};

//清空一个容器的全部子控件
void RemoveAllChildrenOf(DuiControl* box)
{
    if (box == NULL)
    {
        return;
    }
    std::vector<DuiControl*> kids;
    for (size_t i = 0; i < box->Children().size(); ++i)
    {
        kids.push_back(box->Children()[i].get());
    }
    for (size_t i = 0; i < kids.size(); ++i)
    {
        box->RemoveChild(kids[i]);
    }
}

//行内操作按钮的宽度：文字宽度加留白，不小于最小宽度
int ActionWidth(LPCTSTR text)
{
    const int w = layoutkit::TextWidth(text) + kActionPadW;
    return (w > kActionMinW) ? w : kActionMinW;
}

//行内操作按钮那一列要预留的宽度：按最多的那一组（打开 + 文件夹 + 定位 + ⋯）算，悬停时按钮只切可见性、不让位
int ActionsColumnWidth()
{
    return ActionWidth(TR(L"打开")) + ActionWidth(TR(L"文件夹")) + ActionWidth(TR(L"定位")) + kMoreW
         + kActionGap * 3;
}

//造一个下拉框：按各项文字实测宽度，不小于 minW
std::unique_ptr<DuiControl> MakeCombo(UINT ctrlId, const wchar_t* const* items, int count, int sel, int minW,
                                      int& outWidth)
{
    std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
    combo->SetCtrlId(ctrlId);
    outWidth = minW;
    for (int i = 0; i < count; ++i)
    {
        const LPCTSTR text = TR(items[i]);
        combo->AddString(text);
        const int need = layoutkit::TextWidth(text) + layoutkit::ComboChromeWidth();
        if (need > outWidth)
        {
            outWidth = need;
        }
    }
    combo->SetCurSel((sel >= 0 && sel < count) ? sel : 0, false);
    return std::unique_ptr<DuiControl>(combo.release());
}

//标题按粗体实测的宽度，限制在 kTitleMaxW 以内
int TitleWidth(const CString& title)
{
    if (title.IsEmpty())
    {
        return 0;
    }
    const int w = layoutkit::TextWidth(title) * kBoldWidthPercent / kPercentBase + kTitleSlackW;
    return (w < kTitleMaxW) ? w : kTitleMaxW;
}

//一行文件的状态文字：已下载、未下载、下载中 N%、无法下载
CString StateText(const HistoryLibraryFileRowView& view)
{
    switch (view.m_state)
    {
    //本机有完整文件
    case kHistoryLibFileLocal:
        return TR(L"已下载");
    //本窗口正在下载
    case kHistoryLibFileDownloading:
    {
        const int pct = (view.m_progress < 0) ? 0 : ((view.m_progress > kPercentMax) ? kPercentMax : view.m_progress);
        return TRF(L"下载中 {0}%", pct).c_str();
    }
    //在线直传、或文件夹在本机没有落点：没法从服务器取回
    case kHistoryLibFileUnavailable:
        return view.m_isFolder ? TR(L"本机没有这个文件夹") : TR(L"服务器上没有副本");
    //本机没有，可以下载
    case kHistoryLibFileRemote:
    default:
        return TR(L"未下载");
    }
}

//第二行来源说明的完整文字：来源 · 状态
CString SourceLineText(const HistoryLibraryFileRowView& view)
{
    CString text = view.m_sourceText;
    if (!text.IsEmpty())
    {
        text += L" \x00B7 ";
    }
    text += StateText(view);
    return text;
}

//造一个按文字宽度定宽的彩色小标签，追加到行里
void AddTag(DuiHBox* line, const CString& text, COLORREF color)
{
    if (text.IsEmpty())
    {
        return;
    }
    line->AddChild(layoutkit::Label(text, color, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                   Hint().Fixed(layoutkit::TextWidth(text) + kTagPadW));
}

//造一个行内操作按钮，登记为悬停才显示
void AddActionButton(balloonhistory::HistoryRow* row, DuiHBox* actions, LPCTSTR text, UINT ctrlId, int width)
{
    std::unique_ptr<DuiControl> btn = layoutkit::Button(text, ctrlId, DuiButton::Variant::Default);
    DuiControl* raw = btn.get();
    actions->AddChild(std::move(btn), Hint().Fixed(width, kActionH).AlignC(DuiLayout::AlignCenter));
    row->AddHoverOnlyControl(raw);
}

//造文件页的一行
std::unique_ptr<DuiControl> BuildFileRow(const HistoryLibraryFileRowView& view, int index)
{
    std::unique_ptr<HistoryRow> row(new HistoryRow());
    row->SetCtrlId((UINT)(kIdLibRowBase + index));
    row->SetPadding(kRowPad, kRowPad, kRowPad, kRowPad);
    row->SetGap(kRowGap);

    //类型色块：文件夹不写扩展名
    std::unique_ptr<HistoryRowThumb> block(new HistoryRowThumb());
    block->SetFile(view.m_isFolder ? CString() : view.m_ext);
    row->AddChild(std::unique_ptr<DuiControl>(block.release()),
                  Hint().Fixed(kBlockSize, kBlockSize).AlignC(DuiLayout::AlignCenter));

    //文件名 + 来源两行
    std::unique_ptr<DuiVBox> mid(new DuiVBox());
    mid->SetGap(0);
    std::unique_ptr<HistoryHighlightLabel> name(new HistoryHighlightLabel());
    name->SetText(view.m_isFolder ? (view.m_name + _T(" (") + TR(L"文件夹") + _T(")")) : view.m_name);
    name->SetTextColor(kColorFileName);
    name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    name->SetHighlightRanges(view.m_nameHighlights);
    mid->AddChild(std::unique_ptr<DuiControl>(name.release()), Hint().Fixed(kNameLineH));
    std::unique_ptr<DuiHBox> source(new DuiHBox());
    source->SetGap(kTagGap);
    if (view.m_isGroupFile)
    {
        AddTag(source.get(), TR(L"群文件"), kColorGroupTag);
    }
    AddTag(source.get(), view.m_mineTag, kColorMineTag);
    source->AddChild(layoutkit::Label(SourceLineText(view), layoutkit::SubtitleColor(),
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                     Hint().Weight(1));
    mid->AddChild(std::unique_ptr<DuiControl>(source.release()), Hint().Fixed(kSourceLineH));
    row->AddChild(std::unique_ptr<DuiControl>(mid.release()), Hint().Weight(1));

    row->AddChild(layoutkit::Label(view.m_sizeText, layoutkit::TimeColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kSizeW));
    row->AddChild(layoutkit::Label(view.m_timeText, layoutkit::TimeColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kTimeW));

    //悬停才显示的操作按钮。整列宽度固定预留，鼠标进来时文件名不被挤短
    std::unique_ptr<DuiHBox> actions(new DuiHBox());
    actions->SetGap(kActionGap);
    HistoryRow* rowRaw = row.get();
    if (view.m_state == kHistoryLibFileLocal)
    {
        AddActionButton(rowRaw, actions.get(), TR(L"打开"), (UINT)(kIdLibOpenBase + index), ActionWidth(TR(L"打开")));
        AddActionButton(rowRaw, actions.get(), TR(L"文件夹"), (UINT)(kIdLibFolderBase + index),
                        ActionWidth(TR(L"文件夹")));
    }
    else if (view.m_state == kHistoryLibFileRemote || view.m_state == kHistoryLibFileDownloading)
    {
        LPCTSTR text = (view.m_state == kHistoryLibFileDownloading) ? TR(L"取消") : TR(L"下载");
        AddActionButton(rowRaw, actions.get(), text, (UINT)(kIdLibDownloadBase + index), ActionWidth(text));
    }
    if (view.m_canLocate)
    {
        AddActionButton(rowRaw, actions.get(), TR(L"定位"), (UINT)(kIdLibLocateBase + index), ActionWidth(TR(L"定位")));
    }
    AddActionButton(rowRaw, actions.get(), kMoreText, (UINT)(kIdLibMoreBase + index), kMoreW);
    //按钮靠右排：前面补一个弹性占位
    std::unique_ptr<DuiHBox> actionsHost(new DuiHBox());
    actionsHost->SetGap(0);
    actionsHost->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    actionsHost->AddChild(std::unique_ptr<DuiControl>(actions.release()), Hint().Auto());
    row->AddChild(std::unique_ptr<DuiControl>(actionsHost.release()), Hint().Fixed(ActionsColumnWidth()));
    return std::unique_ptr<DuiControl>(row.release());
}

//与我有关页行首标签的文字；关系取值越界时为空串
LPCTSTR RelationTagText(int relation)
{
    if (relation <= kHistoryLibraryRelAll || relation >= kRelCount)
    {
        return _T("");
    }
    return TR(kRelTagTexts[relation]);
}

//与我有关页行首标签的颜色
COLORREF RelationTagColor(int relation)
{
    switch (relation)
    {
    //回复我的
    case kHistoryLibraryRelReply:
        return kColorReplyTag;
    //我发的
    case kHistoryLibraryRelMine:
        return kColorMineTag;
    //@我的
    case kHistoryLibraryRelMention:
    default:
        return kColorMentionTag;
    }
}

//与我有关页行首标签那一列的宽度：按最宽的标签文字算，各行对齐
int RelationTagColumnWidth()
{
    int w = 0;
    for (int i = kHistoryLibraryRelMention; i < kRelCount; ++i)
    {
        const int need = layoutkit::TextWidth(TR(kRelTagTexts[i])) + kRelTagPadW;
        if (need > w)
        {
            w = need;
        }
    }
    return w;
}

//链接页、与我有关页行内操作按钮那一列要预留的宽度
int TextActionsColumnWidth(int kind)
{
    if (kind == kHistoryLibraryLinks)
    {
        return ActionWidth(TR(L"打开")) + ActionWidth(TR(L"复制链接")) + ActionWidth(TR(L"定位")) + kActionGap * 2;
    }
    return ActionWidth(TR(L"复制")) + ActionWidth(TR(L"定位")) + kActionGap;
}

//造链接页或与我有关页的一行：行首（链接色块或关系标签）| 两行文字 | 时间 | 悬停时的操作按钮
std::unique_ptr<DuiControl> BuildTextRow(const HistoryLibraryTextRowView& view, int index, int kind)
{
    const bool links = (kind == kHistoryLibraryLinks);
    std::unique_ptr<HistoryRow> row(new HistoryRow());
    row->SetCtrlId((UINT)(kIdLibRowBase + index));
    row->SetPadding(kRowPad, kRowPad, kRowPad, kRowPad);
    row->SetGap(kRowGap);

    if (links)
    {
        std::unique_ptr<HistoryRowThumb> block(new HistoryRowThumb());
        block->SetFile(kLinkBlockText);
        row->AddChild(std::unique_ptr<DuiControl>(block.release()),
                      Hint().Fixed(kBlockSize, kBlockSize).AlignC(DuiLayout::AlignCenter));
    }
    else
    {
        row->AddChild(layoutkit::Label(RelationTagText(view.m_relation), RelationTagColor(view.m_relation),
                                       DT_CENTER | DT_VCENTER | DT_SINGLELINE),
                      Hint().Fixed(RelationTagColumnWidth()));
    }

    //第一行：链接页是「域名（粗体）+ 完整链接」，与我有关页是正文预览；第二行是来源
    std::unique_ptr<DuiVBox> mid(new DuiVBox());
    mid->SetGap(0);
    std::unique_ptr<DuiHBox> first(new DuiHBox());
    first->SetGap(kDomainGap);
    if (!view.m_title.IsEmpty())
    {
        std::unique_ptr<DuiControl> domain = layoutkit::Label(view.m_title, kColorFileName,
                                                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DuiLabel* domainLabel = dynamic_cast<DuiLabel*>(domain.get());
        if (domainLabel != NULL)
        {
            domainLabel->SetTextPointSize(kDomainPt, true);
        }
        const int domainW = layoutkit::TextWidth(view.m_title) * kBoldWidthPercent / kPercentBase + kTitleSlackW;
        first->AddChild(std::move(domain), Hint().Fixed((domainW < kDomainMaxW) ? domainW : kDomainMaxW));
    }
    std::unique_ptr<HistoryHighlightLabel> text(new HistoryHighlightLabel());
    text->SetText(view.m_text);
    text->SetTextColor(links ? kColorLinkUrl : kColorFileName);
    text->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    text->SetHighlightRanges(view.m_textHighlights);
    first->AddChild(std::unique_ptr<DuiControl>(text.release()), Hint().Weight(1));
    mid->AddChild(std::unique_ptr<DuiControl>(first.release()), Hint().Fixed(kNameLineH));
    std::unique_ptr<DuiHBox> source(new DuiHBox());
    source->SetGap(kTagGap);
    AddTag(source.get(), view.m_mineTag, kColorMineTag);
    source->AddChild(layoutkit::Label(view.m_sourceText, layoutkit::SubtitleColor(),
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS),
                     Hint().Weight(1));
    mid->AddChild(std::unique_ptr<DuiControl>(source.release()), Hint().Fixed(kSourceLineH));
    row->AddChild(std::unique_ptr<DuiControl>(mid.release()), Hint().Weight(1));

    row->AddChild(layoutkit::Label(view.m_timeText, layoutkit::TimeColor(), DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                  Hint().Fixed(kTimeW));

    //悬停才显示的操作按钮，整列宽度固定预留
    std::unique_ptr<DuiHBox> actions(new DuiHBox());
    actions->SetGap(kActionGap);
    HistoryRow* rowRaw = row.get();
    if (links)
    {
        if (view.m_canOpen)
        {
            AddActionButton(rowRaw, actions.get(), TR(L"打开"), (UINT)(kIdLibOpenBase + index),
                            ActionWidth(TR(L"打开")));
        }
        AddActionButton(rowRaw, actions.get(), TR(L"复制链接"), (UINT)(kIdLibCopyBase + index),
                        ActionWidth(TR(L"复制链接")));
    }
    else
    {
        AddActionButton(rowRaw, actions.get(), TR(L"复制"), (UINT)(kIdLibCopyBase + index), ActionWidth(TR(L"复制")));
    }
    AddActionButton(rowRaw, actions.get(), TR(L"定位"), (UINT)(kIdLibLocateBase + index), ActionWidth(TR(L"定位")));
    std::unique_ptr<DuiHBox> actionsHost(new DuiHBox());
    actionsHost->SetGap(0);
    actionsHost->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    actionsHost->AddChild(std::unique_ptr<DuiControl>(actions.release()), Hint().Auto());
    row->AddChild(std::unique_ptr<DuiControl>(actionsHost.release()), Hint().Fixed(TextActionsColumnWidth(kind)));
    return std::unique_ptr<DuiControl>(row.release());
}

//列表为空时的内容：主文案与副文案上下居中
std::unique_ptr<DuiControl> BuildEmptyList(const HistoryLibraryViewData& data)
{
    std::unique_ptr<DuiVBox> box(new DuiVBox());
    box->SetCtrlId(kIdLibList);
    box->SetPadding(kListPadH, 0, kListPadH, 0);
    box->SetGap(0);
    box->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    box->AddChild(layoutkit::Label(data.m_emptyTitle, layoutkit::EmptyTitleColor(),
                                   DT_CENTER | DT_VCENTER | DT_SINGLELINE, kIdLibEmptyTitle),
                  Hint().Fixed(kEmptyTitleH));
    if (!data.m_emptySub.IsEmpty())
    {
        box->AddChild(layoutkit::Label(data.m_emptySub, layoutkit::SubtitleColor(),
                                       DT_CENTER | DT_VCENTER | DT_SINGLELINE, kIdLibEmptySub),
                      Hint().Fixed(kEmptySubH));
    }
    box->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    return std::unique_ptr<DuiControl>(box.release());
}

//底部信息文字：文件页「共 N 个 · 第 x/y 页」，图片页「共 N 张 · 第 x/y 页」，链接页与与我有关页「共 N 条 · 第 x/y 页」
CString FooterInfoText(const HistoryLibraryViewData& data)
{
    if (data.m_kind == kHistoryLibraryImages)
    {
        return TRF(L"共 {0} 张 · 第 {1}/{2} 页", (long long)data.m_total, data.m_currentPage,
                   data.m_pageCount).c_str();
    }
    //链接页、与我有关页按消息计数
    if (data.m_kind == kHistoryLibraryLinks || data.m_kind == kHistoryLibraryMentions)
    {
        return TRF(L"共 {0} 条 · 第 {1}/{2} 页", (long long)data.m_total, data.m_currentPage,
                   data.m_pageCount).c_str();
    }
    return TRF(L"共 {0} 个 · 第 {1}/{2} 页", (long long)data.m_total, data.m_currentPage,
               data.m_pageCount).c_str();
}

//把分页按钮（首页 < 页码… > 末页）填进分页器容器
void PopulateLibraryPager(DuiControl* pager, int currentPage, int pageCount)
{
    DuiLayout* box = dynamic_cast<DuiLayout*>(pager);
    if (box == NULL)
    {
        return;
    }
    box->AddChild(layoutkit::Button(TR(L"首页"), kIdLibPageFirst, DuiButton::Variant::Default),
                  Hint().Fixed(layoutkit::GrowWidth(TR(L"首页"), kPagerBtnW, layoutkit::ChipPadWidth())));
    box->AddChild(layoutkit::Button(_T("<"), kIdLibPagePrev, DuiButton::Variant::Default), Hint().Fixed(kPagerCellW));
    const std::vector<HistoryPagerCell> cells = BuildPagerModel(currentPage, pageCount);
    for (size_t i = 0; i < cells.size(); ++i)
    {
        //省略号是不可点的标签，不给编号
        if (cells[i].m_isEllipsis)
        {
            box->AddChild(layoutkit::Label(_T("\x2026"), layoutkit::SubtitleColor(),
                                           DT_CENTER | DT_VCENTER | DT_SINGLELINE),
                          Hint().Fixed(kPagerCellW));
            continue;
        }
        CString num;
        num.Format(_T("%d"), cells[i].m_page);
        box->AddChild(layoutkit::Button(num, (UINT)(kIdLibPageCellBase + i),
                                        cells[i].m_isCurrent ? DuiButton::Variant::Primary
                                                             : DuiButton::Variant::Default),
                      Hint().Fixed(kPagerCellW));
    }
    box->AddChild(layoutkit::Button(_T(">"), kIdLibPageNext, DuiButton::Variant::Default), Hint().Fixed(kPagerCellW));
    box->AddChild(layoutkit::Button(TR(L"末页"), kIdLibPageLast, DuiButton::Variant::Default),
                  Hint().Fixed(layoutkit::GrowWidth(TR(L"末页"), kPagerBtnW, layoutkit::ChipPadWidth())));
}

//在 root（或其祖先）里找文件 / 图片页的根容器
DuiControl* LibraryRoot(DuiControl* root)
{
    if (root == NULL)
    {
        return NULL;
    }
    if (root->GetCtrlId() == (UINT)kIdLibBody)
    {
        return root;
    }
    return root->FindCtrlById(kIdLibBody);
}

} // namespace

std::unique_ptr<DuiControl> BuildHistoryLibraryBody(const HistoryLibraryViewData& data, int listWidth,
                                                    int listRightInsetPx)
{
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetCtrlId(kIdLibBody);
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    // 1. 标题区：「»」+ 标题 + 灰色说明
    std::unique_ptr<DuiVBox> header(new DuiVBox());
    header->SetPadding(kPadH, kHeaderPadTop, kPadH, kHeaderPadBot);
    header->SetGap(0);
    std::unique_ptr<DuiHBox> headerRow(new DuiHBox());
    headerRow->SetCtrlId(kIdLibHeaderRow);
    headerRow->SetGap(kHeaderGap);
    std::unique_ptr<DuiControl> expand = layoutkit::Button(kExpandLeftText, kIdLibExpandLeft, DuiButton::Variant::Ghost);
    expand->SetVisible(data.m_showExpandLeft);
    headerRow->AddChild(std::move(expand), Hint().Fixed(kExpandLeftW));
    std::unique_ptr<DuiControl> title = layoutkit::Label(data.m_title, layoutkit::TitleColor(),
                                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                                         kIdLibTitle);
    DuiLabel* titleLabel = dynamic_cast<DuiLabel*>(title.get());
    if (titleLabel != NULL)
    {
        titleLabel->SetTextPointSize(kTitlePt, true);
    }
    headerRow->AddChild(std::move(title), Hint().Fixed(TitleWidth(data.m_title)));
    headerRow->AddChild(layoutkit::Label(data.m_subtitle, layoutkit::SubtitleColor(),
                                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, kIdLibSubtitle),
                        Hint().Weight(1));
    header->AddChild(std::move(headerRow), Hint().Fixed(kHeaderLineH));
    root->AddChild(std::move(header), Hint().Fixed(kHeaderH));
    root->AddChild(layoutkit::Separator(), Hint().Fixed(1));

    // 2. 搜索行（只有文件页有）：文件名搜索框 +「通配符」
    std::unique_ptr<DuiHBox> searchRow(new DuiHBox());
    searchRow->SetCtrlId(kIdLibSearchRow);
    searchRow->SetPadding(kPadH, kRowGap8, kPadH, kHeaderPadBot);
    searchRow->SetGap(kRowGap8);
    //悬停时显示搜索语法说明，文字随页面在 ApplyHistoryLibraryKind 里更换
    std::unique_ptr<HistoryTipSearchBox> search(new HistoryTipSearchBox(HistoryFileNameSearchTip()));
    search->SetCtrlId(kIdLibSearch);
    search->SetPlaceholder(TR(L"搜索文件名"));
    searchRow->AddChild(std::unique_ptr<DuiControl>(search.release()), Hint().Weight(1));
    int wildcardW = 0;
    std::unique_ptr<DuiControl> wildcard = layoutkit::WildcardCheckbox(kIdLibWildcard, data.m_wildcard, wildcardW);
    searchRow->AddChild(std::move(wildcard), Hint().Fixed(wildcardW));
    root->AddChild(std::move(searchRow), Hint().Fixed(kSearchRowH));

    // 3. 筛选行：方向胶囊（左）+ 来源、类型（只有文件页有）、时间下拉（右）
    std::unique_ptr<DuiHBox> filterRow(new DuiHBox());
    filterRow->SetCtrlId(kIdLibFilterRow);
    filterRow->SetPadding(kPadH, kRowGap8, kPadH, kRowGap8);
    filterRow->SetGap(kRowGap8);
    for (int i = 0; i < kDirCount; ++i)
    {
        const LPCTSTR text = TR(kDirTexts[i]);
        filterRow->AddChild(layoutkit::Chip(text, kDirIds[i], data.m_direction == i),
                            Hint().Fixed(layoutkit::GrowWidth(text, kChipW, layoutkit::ChipPadWidth())));
    }
    //与我有关页的关系胶囊：与方向胶囊二者只显示其一（见 ApplyHistoryLibraryKind）
    for (int i = 0; i < kRelCount; ++i)
    {
        const LPCTSTR text = TR(kRelTexts[i]);
        filterRow->AddChild(layoutkit::Chip(text, kRelIds[i], data.m_relation == i),
                            Hint().Fixed(layoutkit::GrowWidth(text, kChipW, layoutkit::ChipPadWidth())));
    }
    filterRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    int comboW = 0;
    std::unique_ptr<DuiControl> sourceCombo = MakeCombo(kIdLibSourceCombo, kSourceTexts,
                                                        (int)(sizeof(kSourceTexts) / sizeof(kSourceTexts[0])),
                                                        data.m_source, kComboMinW, comboW);
    filterRow->AddChild(std::move(sourceCombo), Hint().Fixed(comboW));
    std::unique_ptr<DuiControl> categoryCombo = MakeCombo(kIdLibCategoryCombo, kCategoryTexts,
                                                          (int)(sizeof(kCategoryTexts) / sizeof(kCategoryTexts[0])),
                                                          data.m_category, kComboMinW, comboW);
    filterRow->AddChild(std::move(categoryCombo), Hint().Fixed(comboW));
    std::unique_ptr<DuiControl> timeCombo = layoutkit::TimeCombo(kIdLibTimeCombo, data.m_timeIndex, comboW);
    filterRow->AddChild(std::move(timeCombo), Hint().Fixed(comboW));
    root->AddChild(std::move(filterRow), Hint().Fixed(kFilterRowH));

    // 4. 列表：与聊天记录页同款的滚动视口（悬浮细滚动条）
    std::unique_ptr<balloonmain::MainPanelScrollView> scroll(new balloonmain::MainPanelScrollView());
    scroll->SetCtrlId(kIdLibScroll);
    scroll->SetAutoContentHeight(true);
    scroll->SetContent(BuildHistoryLibraryList(data, listWidth));
    //右侧给窗口缩放边框让出位置，理由同聊天记录页（BuildHistoryWindowBody）：滚动条贴着窗口右缘时拖不动
    const int rightInset = (listRightInsetPx > 0) ? listRightInsetPx : 0;
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1).Margin(0, 0, rightInset, 0));
    root->AddChild(layoutkit::Separator(), Hint().Fixed(1));

    // 5. 底部：信息 + 分页器 +「下载本页全部」
    std::unique_ptr<DuiHBox> footer(new DuiHBox());
    footer->SetCtrlId(kIdLibFooter);
    footer->SetPadding(kPadH, kFooterPadV, kPadH, kFooterPadV);
    footer->SetGap(kRowGap8);
    footer->AddChild(layoutkit::Label(FooterInfoText(data), layoutkit::SubtitleColor(),
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, kIdLibFooterInfo),
                     Hint().Weight(1));
    std::unique_ptr<DuiHBox> pager(new DuiHBox());
    pager->SetCtrlId(kIdLibPager);
    pager->SetGap(kPagerGap);
    PopulateLibraryPager(pager.get(), data.m_currentPage, data.m_pageCount);
    footer->AddChild(std::unique_ptr<DuiControl>(pager.release()), Hint().Auto());
    const LPCTSTR batchText = TR(L"下载本页全部");
    std::unique_ptr<DuiControl> batch = layoutkit::Button(batchText, kIdLibBatchDownload, DuiButton::Variant::Outlined);
    batch->SetEnabled(data.m_pendingDownloads > 0);
    footer->AddChild(std::move(batch), Hint().Fixed(layoutkit::TextWidth(batchText) + kBatchBtnPadW));
    root->AddChild(std::unique_ptr<DuiControl>(footer.release()), Hint().Fixed(kFooterH));

    ApplyHistoryLibraryKind(root.get(), data.m_kind);
    return std::unique_ptr<DuiControl>(root.release());
}

std::unique_ptr<DuiControl> BuildHistoryLibraryList(const HistoryLibraryViewData& data, int listWidth)
{
    const bool images = (data.m_kind == kHistoryLibraryImages);
    const bool textRows = (data.m_kind == kHistoryLibraryLinks || data.m_kind == kHistoryLibraryMentions);
    const bool empty = images ? data.m_imageCells.empty() : (textRows ? data.m_textRows.empty() : data.m_fileRows.empty());
    if (empty)
    {
        return BuildEmptyList(data);
    }
    if (images)
    {
        return BuildImageGridContent(data.m_imageCells, data.m_emptyTitle, data.m_emptySub, listWidth,
                                     kIdLibList, kIdLibGridBase);
    }
    std::unique_ptr<DuiVBox> list(new DuiVBox());
    list->SetCtrlId(kIdLibList);
    list->SetPadding(kListPadH, 0, kListPadH, 0);
    list->SetGap(0);
    if (textRows)
    {
        for (size_t i = 0; i < data.m_textRows.size() && (int)i < kIdLibRowCapacity; ++i)
        {
            list->AddChild(BuildTextRow(data.m_textRows[i], (int)i, data.m_kind), Hint().Fixed(kRowH));
        }
        return std::unique_ptr<DuiControl>(list.release());
    }
    for (size_t i = 0; i < data.m_fileRows.size() && (int)i < kIdLibRowCapacity; ++i)
    {
        list->AddChild(BuildFileRow(data.m_fileRows[i], (int)i), Hint().Fixed(kRowH));
    }
    return std::unique_ptr<DuiControl>(list.release());
}

void ApplyHistoryLibraryChrome(DuiControl* root, const HistoryLibraryViewData& data)
{
    DuiControl* lib = LibraryRoot(root);
    if (lib == NULL)
    {
        return;
    }
    //标题区：标题宽度随文字变化，「»」显隐也可能变了，整行重排
    DuiLabel* title = dynamic_cast<DuiLabel*>(lib->FindCtrlById(kIdLibTitle));
    DuiLabel* subtitle = dynamic_cast<DuiLabel*>(lib->FindCtrlById(kIdLibSubtitle));
    DuiControl* expand = lib->FindCtrlById(kIdLibExpandLeft);
    DuiLayout* headerRow = dynamic_cast<DuiLayout*>(lib->FindCtrlById(kIdLibHeaderRow));
    if (title != NULL && subtitle != NULL && expand != NULL && headerRow != NULL)
    {
        title->SetText(data.m_title);
        subtitle->SetText(data.m_subtitle);
        expand->SetVisible(data.m_showExpandLeft);
        headerRow->SetHint(title, Hint().Fixed(TitleWidth(data.m_title)));
        headerRow->ForceLayout(headerRow->GetRect());
        headerRow->Invalidate();
    }

    //方向胶囊与关系胶囊
    for (int i = 0; i < kDirCount; ++i)
    {
        DuiButton* chip = dynamic_cast<DuiButton*>(lib->FindCtrlById(kDirIds[i]));
        if (chip != NULL)
        {
            chip->SetVariant((data.m_direction == i) ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
            chip->Invalidate();
        }
    }
    for (int i = 0; i < kRelCount; ++i)
    {
        DuiButton* chip = dynamic_cast<DuiButton*>(lib->FindCtrlById(kRelIds[i]));
        if (chip != NULL)
        {
            chip->SetVariant((data.m_relation == i) ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
            chip->Invalidate();
        }
    }

    //底部：信息、分页器、「下载本页全部」；分页器宽度随页码格数变化，整行重排
    DuiLabel* info = dynamic_cast<DuiLabel*>(lib->FindCtrlById(kIdLibFooterInfo));
    if (info != NULL)
    {
        info->SetText(FooterInfoText(data));
    }
    DuiControl* pager = lib->FindCtrlById(kIdLibPager);
    if (pager != NULL)
    {
        RemoveAllChildrenOf(pager);
        PopulateLibraryPager(pager, data.m_currentPage, data.m_pageCount);
    }
    DuiControl* batch = lib->FindCtrlById(kIdLibBatchDownload);
    if (batch != NULL)
    {
        batch->SetEnabled(data.m_pendingDownloads > 0);
    }
    DuiControl* footer = lib->FindCtrlById(kIdLibFooter);
    if (footer != NULL)
    {
        footer->ForceLayout(footer->GetRect());
        footer->Invalidate();
    }
    //ForceLayout 只强制它作用的那一层（开发约定文档 UI 约定第 5 条）：页数不变时分页器的矩形与上次相同，底部一行给它
    //排版时 SetRect 直接返回，刚重填的按钮矩形为空、画不出来。对分页器自身再强制排一次
    if (pager != NULL)
    {
        pager->ForceLayout(pager->GetRect());
        pager->Invalidate();
    }
}

void ApplyHistoryLibraryKind(DuiControl* root, int kind)
{
    DuiControl* lib = LibraryRoot(root);
    if (lib == NULL)
    {
        return;
    }
    const bool files = (kind == kHistoryLibraryFiles);
    const bool mentions = (kind == kHistoryLibraryMentions);
    DuiControl* searchRow = lib->FindCtrlById(kIdLibSearchRow);
    DuiControl* source = lib->FindCtrlById(kIdLibSourceCombo);
    DuiControl* category = lib->FindCtrlById(kIdLibCategoryCombo);
    DuiControl* filterRow = lib->FindCtrlById(kIdLibFilterRow);
    DuiControl* batch = lib->FindCtrlById(kIdLibBatchDownload);
    //搜索行：图片页没有；文件页搜文件名，链接页与与我有关页搜消息内容
    if (searchRow != NULL)
    {
        searchRow->SetVisible(kind != kHistoryLibraryImages);
    }
    DuiSearchBox* search = dynamic_cast<DuiSearchBox*>(lib->FindCtrlById(kIdLibSearch));
    if (search != NULL)
    {
        search->SetPlaceholder(files ? TR(L"搜索文件名") : TR(L"搜索消息内容，支持 from: type: after: 等语法"));
    }
    HistoryTipSearchBox* tipSearch = dynamic_cast<HistoryTipSearchBox*>(search);
    if (tipSearch != NULL)
    {
        tipSearch->SetTipText(files ? HistoryFileNameSearchTip() : HistorySearchSyntaxTip());
    }
    if (source != NULL)
    {
        source->SetVisible(files);
    }
    if (category != NULL)
    {
        category->SetVisible(files);
    }
    //方向胶囊与关系胶囊：与我有关页只显示关系胶囊，其它页只显示方向胶囊
    for (int i = 0; i < kDirCount; ++i)
    {
        DuiControl* chip = lib->FindCtrlById(kDirIds[i]);
        if (chip != NULL)
        {
            chip->SetVisible(!mentions);
        }
    }
    for (int i = 0; i < kRelCount; ++i)
    {
        DuiControl* chip = lib->FindCtrlById(kRelIds[i]);
        if (chip != NULL)
        {
            chip->SetVisible(mentions);
        }
    }
    //「下载本页全部」只对文件页、图片页有意义
    if (batch != NULL)
    {
        batch->SetVisible(files || kind == kHistoryLibraryImages);
    }
    DuiControl* footer = lib->FindCtrlById(kIdLibFooter);
    if (footer != NULL)
    {
        footer->ForceLayout(footer->GetRect());
    }
    //SetVisible 不会重排：搜索行显隐改变的是根容器的竖向分配，来源、类型改变的是筛选行的横向分配
    //（开发约定文档 UI 约定第 5 条）
    lib->ForceLayout(lib->GetRect());
    if (filterRow != NULL)
    {
        filterRow->ForceLayout(filterRow->GetRect());
    }
    lib->Invalidate();
}

bool ApplyHistoryLibraryRowState(DuiControl* root, int row, const HistoryLibraryFileRowView& view)
{
    DuiControl* lib = LibraryRoot(root);
    if (lib == NULL || row < 0 || row >= kIdLibRowCapacity)
    {
        return false;
    }
    DuiControl* line = lib->FindCtrlById((UINT)(kIdLibRowBase + row));
    if (line == NULL || line->Children().size() < 2)
    {
        return false;
    }
    //第二列是「文件名 + 来源」两行，来源那一行的最后一个子控件是来源说明（含状态）
    DuiControl* mid = line->Children()[1].get();
    if (mid == NULL || mid->Children().size() < 2)
    {
        return false;
    }
    DuiControl* source = mid->Children()[1].get();
    if (source == NULL || source->Children().empty())
    {
        return false;
    }
    DuiLabel* label = dynamic_cast<DuiLabel*>(source->Children().back().get());
    if (label == NULL)
    {
        return false;
    }
    label->SetText(SourceLineText(view));
    label->Invalidate();
    return true;
}

int HistoryLibraryPageOfCell(int cellIndex, int currentPage, int pageCount)
{
    const std::vector<HistoryPagerCell> cells = BuildPagerModel(currentPage, pageCount);
    if (cellIndex < 0 || cellIndex >= (int)cells.size() || cells[(size_t)cellIndex].m_isEllipsis)
    {
        return 0;
    }
    return cells[(size_t)cellIndex].m_page;
}

std::string FormatLibraryTime(int64_t timestampMs, int64_t nowMs)
{
    const int64_t kMsPerSecond = 1000;
    const int kTmYearBase = 1900;
    if (timestampMs <= 0)
    {
        return std::string();
    }
    __time64_t t = (__time64_t)(timestampMs / kMsPerSecond);
    __time64_t n = (__time64_t)(nowMs / kMsPerSecond);
    struct tm tmv;
    struct tm tmNow;
    if (_localtime64_s(&tmv, &t) != 0 || _localtime64_s(&tmNow, &n) != 0)
    {
        return std::string();
    }
    char buf[32] = { 0 };
    if (tmv.tm_year == tmNow.tm_year)
    {
        sprintf_s(buf, "%02d-%02d %02d:%02d", tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    }
    else
    {
        sprintf_s(buf, "%04d-%02d-%02d", tmv.tm_year + kTmYearBase, tmv.tm_mon + 1, tmv.tm_mday);
    }
    return std::string(buf);
}

} // namespace balloonhistory
