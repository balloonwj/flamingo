/**
 *  录制管理窗口的控件树与尺寸，见 RecordManagerLayout.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "RecordManagerLayout.h"

#include <string>
#include "RecordManagerList.h"
#include "CloudRecordManagerModel.h"   // 云端页状态文字（算状态列宽度）、用量文字（算最小宽度）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTab.h"
#include "../../Source/Common/I18n/I18n.h"         // TR / TRF：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：按钮按译文字宽算宽度

namespace screenrecord {

namespace {

//按钮的最小宽度（像素）
const int kMinButtonW = 64;
//按钮宽度在文字宽度之外留出的左右空白之和（像素）
const int kButtonTextExtra = 28;
//「共 N 个」按这个条数的文字宽度留位置，四位数以内不会被截断
const int kCountWidthSample = 9999;
//最小整窗宽不小于默认宽度的几分之几：分子、分母
const int kMinWidthNum = 2;
const int kMinWidthDen = 3;
//没有录像时的提示文字颜色
const COLORREF kEmptyTextColor = RGB(140, 140, 146);
//「共 N 个」的文字颜色
const COLORREF kCountTextColor = RGB(110, 110, 116);
//云端页：状态列的最小宽度与文字两侧的空白（像素）
const int kTrailingMinW = 96;
const int kTrailingTextExtra = 16;
//云端页：用量文字的宽度按这组数值的文字算（已用 999.9 GB、配额 9999 GB、保留 3650 天，覆盖常见取值；更长的文字以省略号收尾），
//用量条与文字之间的间距
const int64_t kUsageSampleUsed = 999LL * 1024 * 1024 * 1024 + 900LL * 1024 * 1024;
const int64_t kUsageSampleQuota = 9999LL * 1024 * 1024 * 1024;
const int64_t kUsageSampleDays = 3650;
const int kUsageBarGap = 8;
//云端页：用量条的颜色（底色、填充）
const COLORREF kUsageBarBgColor = RGB(230, 232, 236);
const COLORREF kUsageBarFillColor = RGB(45, 108, 223);
//用量条的满值（千分比）
const int kUsageBarMax = 1000;

//文字宽度（像素，按 balloonui 默认字体）
int textWidth(const std::wstring& text)
{
    return i18n::MeasureUiTextWidth(text.c_str(), i18n::DefaultFontPixelHeight(), false);
}

//按钮宽度：文字宽度加左右空白，不小于 kMinButtonW
int buttonWidth(const std::wstring& text)
{
    const int need = textWidth(text) + kButtonTextExtra;
    return need > kMinButtonW ? need : kMinButtonW;
}

//「删除」按钮的宽度：要同时放得下「删除」与「从列表移除」两种文字
int deleteButtonWidth()
{
    const int a = buttonWidth(TR(L"删除"));
    const int b = buttonWidth(TR(L"从列表移除"));
    return a > b ? a : b;
}

//「共 N 个」的宽度
int countWidth()
{
    return textWidth(RecordManagerCountText(kCountWidthSample)) + kRecordMgrButtonGap;
}

//云端页用量文字的宽度
int usageWidth()
{
    return textWidth(CloudUsageText(true, kUsageSampleUsed, kUsageSampleQuota, kUsageSampleDays)) + kRecordMgrButtonGap;
}

//没有录像时的提示
std::unique_ptr<balloonwjui::DuiLabel> makeEmptyLabel(UINT id, const wchar_t* text)
{
    std::unique_ptr<balloonwjui::DuiLabel> empty(new balloonwjui::DuiLabel());
    empty->SetCtrlId(id);
    empty->SetText(text);
    empty->SetTextColor(kEmptyTextColor);
    empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    empty->SetVisible(false);
    return empty;
}

//建一个工具栏按钮并加进工具栏
balloonwjui::DuiButton* addToolButton(balloonwjui::DuiHBox* bar, const wchar_t* text, UINT id, int width)
{
    std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
    b->SetText(text);
    b->SetVariant(balloonwjui::DuiButton::Variant::Default);
    b->SetCtrlId(id);
    b->SetEnabled(false);
    balloonwjui::DuiButton* raw = b.get();
    bar->AddChild(std::move(b), balloonwjui::DuiLayout::Hint().Fixed(width));
    return raw;
}

} // namespace

RecordManagerBody::RecordManagerBody()
    : m_list(NULL)
    , m_empty(NULL)
    , m_count(NULL)
    , m_play(NULL)
    , m_openFolder(NULL)
    , m_send(NULL)
    , m_rename(NULL)
    , m_delete(NULL)
    , m_caption(NULL)
    , m_tabs(NULL)
    , m_localPage(NULL)
    , m_cloudPage(NULL)
    , m_cloudList(NULL)
    , m_cloudEmpty(NULL)
    , m_cloudUsage(NULL)
    , m_cloudUsageBar(NULL)
    , m_cloudPlay(NULL)
    , m_cloudDownload(NULL)
    , m_cloudSend(NULL)
    , m_cloudRename(NULL)
    , m_cloudDelete(NULL)
    , m_cloudMore(NULL)
{
}

std::wstring RecordManagerCountText(int count)
{
    //{0} 为录像条数
    return TRF(L"共 {0} 个", count);
}

int RecordManagerMinWidth()
{
    //工具栏有八个子控件：六个按钮（含语音字幕的「编辑字幕」，按它显示时算）、弹性占位、「共 N 个」，相邻两个之间都有一个
    //kRecordMgrButtonGap（共七个，弹性占位缩到 0 宽时它两侧的两个间距仍在），再加左右内边距
    const int kButtonCount = 6;
    const int kToolbarGapCount = kButtonCount + 1;
    const int buttons = buttonWidth(TR(L"播放")) + buttonWidth(TR(L"打开所在文件夹")) +
                        buttonWidth(TR(L"发送到会话…")) + buttonWidth(TR(L"重命名")) + deleteButtonWidth() +
                        buttonWidth(TR(L"编辑字幕"));
    const int localNeed = 2 * kRecordMgrPadding + buttons + kToolbarGapCount * kRecordMgrButtonGap + countWidth();
    //云端页工具栏：五个按钮、弹性占位、用量条、用量文字，相邻两个之间各一个间距（共七个）
    const int kCloudGapCount = 7;
    const int cloudButtons = buttonWidth(TR(L"播放")) + buttonWidth(TR(L"下载…")) + buttonWidth(TR(L"发送到会话…")) +
                             buttonWidth(TR(L"重命名")) + buttonWidth(TR(L"删除"));
    const int cloudNeed = 2 * kRecordMgrPadding + cloudButtons + kCloudGapCount * kRecordMgrButtonGap +
                          kRecordMgrUsageBarW + usageWidth();
    const int need = localNeed > cloudNeed ? localNeed : cloudNeed;
    const int floor = kRecordMgrDefaultW * kMinWidthNum / kMinWidthDen;
    return need > floor ? need : floor;
}

int RecordManagerTrailingWidth()
{
    //各种状态里最长的文字：进度取三位数，到期天数取一位数
    const std::wstring samples[] = {
        CloudRowStateText(cloud_row_uploading, 100, -1), CloudRowStateText(cloud_row_composing, 100, -1),
        CloudRowStateText(cloud_row_ready, -1, kCloudExpireWarnDays), CloudRowStateText(cloud_row_ready, -1, 0),
        CloudRowStateText(cloud_row_ready, -1, -1), CloudRowStateText(cloud_row_pending, -1, -1),
        CloudRowStateText(cloud_row_compose_failed, -1, -1), CloudRowStateText(cloud_row_record_failed, -1, -1),
        CloudRowStateText(cloud_row_upload_stalled, -1, -1), CloudRowStateText(cloud_row_recording, -1, -1)
    };
    int width = kTrailingMinW;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        const int need = textWidth(samples[i]) + kTrailingTextExtra;
        if (need > width)
            width = need;
    }
    return width;
}

RecordManagerBody BuildRecordManagerBody()
{
    RecordManagerBody body;
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRecordMgrPadding);
    root->SetGap(kRecordMgrGap);

    //「本机 / 云端」页签（第 27 期 27d）
    std::unique_ptr<balloonwjui::DuiTab> tabs(new balloonwjui::DuiTab());
    tabs->AddTab(TR(L"本机"));
    tabs->AddTab(TR(L"云端"));
    tabs->SetCtrlId(kRecordMgrIdTabs);
    body.m_tabs = tabs.get();
    root->AddChild(std::move(tabs), balloonwjui::DuiLayout::Hint().Fixed(kRecordMgrTabH));

    //本机页：工具栏、列表、没有录像时的提示
    std::unique_ptr<balloonwjui::DuiVBox> localPage(new balloonwjui::DuiVBox());
    localPage->SetGap(kRecordMgrGap);
    body.m_localPage = localPage.get();

    //工具栏：五个按钮靠左，「共 N 个」靠右，中间是弹性占位
    std::unique_ptr<balloonwjui::DuiHBox> bar(new balloonwjui::DuiHBox());
    bar->SetGap(kRecordMgrButtonGap);
    body.m_play = addToolButton(bar.get(), TR(L"播放"), kRecordMgrIdPlay, buttonWidth(TR(L"播放")));
    body.m_openFolder = addToolButton(bar.get(), TR(L"打开所在文件夹"), kRecordMgrIdOpenFolder,
                                      buttonWidth(TR(L"打开所在文件夹")));
    body.m_send = addToolButton(bar.get(), TR(L"发送到会话…"), kRecordMgrIdSend, buttonWidth(TR(L"发送到会话…")));
    body.m_rename = addToolButton(bar.get(), TR(L"重命名"), kRecordMgrIdRename, buttonWidth(TR(L"重命名")));
    body.m_delete = addToolButton(bar.get(), TR(L"删除"), kRecordMgrIdDelete, deleteButtonWidth());
    //语音字幕：默认隐藏，窗口按字幕是否可用显示
    body.m_caption = addToolButton(bar.get(), TR(L"编辑字幕"), kRecordMgrIdCaption, buttonWidth(TR(L"编辑字幕")));
    body.m_caption->SetVisible(false);
    bar->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiLabel> count(new balloonwjui::DuiLabel());
    count->SetCtrlId(kRecordMgrIdCount);
    count->SetText(RecordManagerCountText(0).c_str());
    count->SetTextColor(kCountTextColor);
    count->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    body.m_count = count.get();
    bar->AddChild(std::move(count), balloonwjui::DuiLayout::Hint().Fixed(countWidth()));
    localPage->AddChild(std::move(bar), balloonwjui::DuiLayout::Hint().Fixed(kRecordMgrButtonH));

    //录像列表：最新的在最上面，每行左侧是图标，主文字为文件名，副文字为时间、时长与大小
    std::unique_ptr<CRecordManagerList> list(new CRecordManagerList());
    list->SetCtrlId(kRecordMgrIdList);
    list->SetItemHeight(kRecordMgrRowH);
    list->SetIconSize(kRecordMgrIconPx);
    body.m_list = list.get();
    localPage->AddChild(std::move(list), balloonwjui::DuiLayout::Hint().Weight(1));

    //没有录像时代替列表显示的提示
    std::unique_ptr<balloonwjui::DuiLabel> empty = makeEmptyLabel(kRecordMgrIdEmpty,
                                                                   TR(L"还没有录像。录制完成后，录像会出现在这里。"));
    body.m_empty = empty.get();
    localPage->AddChild(std::move(empty), balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(std::move(localPage), balloonwjui::DuiLayout::Hint().Weight(1));

    //云端页（默认隐藏）：工具栏（五个按钮靠左，用量条与用量文字靠右）、列表、提示、「加载更多」
    std::unique_ptr<balloonwjui::DuiVBox> cloudPage(new balloonwjui::DuiVBox());
    cloudPage->SetGap(kRecordMgrGap);
    cloudPage->SetVisible(false);
    body.m_cloudPage = cloudPage.get();
    std::unique_ptr<balloonwjui::DuiHBox> cloudBar(new balloonwjui::DuiHBox());
    cloudBar->SetGap(kRecordMgrButtonGap);
    body.m_cloudPlay = addToolButton(cloudBar.get(), TR(L"播放"), kRecordMgrIdCloudPlay, buttonWidth(TR(L"播放")));
    body.m_cloudDownload = addToolButton(cloudBar.get(), TR(L"下载…"), kRecordMgrIdCloudDownload,
                                         buttonWidth(TR(L"下载…")));
    body.m_cloudSend = addToolButton(cloudBar.get(), TR(L"发送到会话…"), kRecordMgrIdCloudSend,
                                     buttonWidth(TR(L"发送到会话…")));
    body.m_cloudRename = addToolButton(cloudBar.get(), TR(L"重命名"), kRecordMgrIdCloudRename,
                                       buttonWidth(TR(L"重命名")));
    body.m_cloudDelete = addToolButton(cloudBar.get(), TR(L"删除"), kRecordMgrIdCloudDelete, buttonWidth(TR(L"删除")));
    cloudBar->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                       balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiProgressBar> usageBar(new balloonwjui::DuiProgressBar());
    usageBar->SetCtrlId(kRecordMgrIdCloudUsageBar);
    usageBar->SetRange(0, kUsageBarMax);
    usageBar->SetPos(0, false);
    usageBar->SetShowPercent(false);
    usageBar->SetBgColor(kUsageBarBgColor);
    usageBar->SetBorderColor(kUsageBarBgColor);
    usageBar->SetFillColor(kUsageBarFillColor);
    body.m_cloudUsageBar = usageBar.get();
    //用量条比这一行矮：上下各留一半差值，垂直居中
    const int barMargin = (kRecordMgrButtonH - kRecordMgrUsageBarH) / 2;
    cloudBar->AddChild(std::move(usageBar),
                       balloonwjui::DuiLayout::Hint().Fixed(kRecordMgrUsageBarW).Margin(0, barMargin, 0, barMargin));
    std::unique_ptr<balloonwjui::DuiLabel> usage(new balloonwjui::DuiLabel());
    usage->SetCtrlId(kRecordMgrIdCloudUsage);
    usage->SetTextColor(kCountTextColor);
    usage->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    body.m_cloudUsage = usage.get();
    cloudBar->AddChild(std::move(usage), balloonwjui::DuiLayout::Hint().Fixed(usageWidth()));
    cloudPage->AddChild(std::move(cloudBar), balloonwjui::DuiLayout::Hint().Fixed(kRecordMgrButtonH));

    std::unique_ptr<CRecordManagerList> cloudList(new CRecordManagerList());
    cloudList->SetCtrlId(kRecordMgrIdCloudList);
    cloudList->SetItemHeight(kRecordMgrRowH);
    cloudList->SetIconSize(kRecordMgrIconPx);
    cloudList->SetTrailingWidth(RecordManagerTrailingWidth());
    body.m_cloudList = cloudList.get();
    cloudPage->AddChild(std::move(cloudList), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiLabel> cloudEmpty = makeEmptyLabel(kRecordMgrIdCloudEmpty, TR(L"还没有云端录像"));
    body.m_cloudEmpty = cloudEmpty.get();
    cloudPage->AddChild(std::move(cloudEmpty), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> more(new balloonwjui::DuiButton());
    more->SetText(TR(L"加载更多"));
    more->SetVariant(balloonwjui::DuiButton::Variant::Text);
    more->SetCtrlId(kRecordMgrIdCloudMore);
    more->SetVisible(false);
    body.m_cloudMore = more.get();
    cloudPage->AddChild(std::move(more), balloonwjui::DuiLayout::Hint().Fixed(kRecordMgrMoreH));
    root->AddChild(std::move(cloudPage), balloonwjui::DuiLayout::Hint().Weight(1));

    body.m_root = std::move(root);
    return body;
}

} // namespace screenrecord
