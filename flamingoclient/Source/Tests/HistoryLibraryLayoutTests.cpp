/**
 *  聊天记录管理器「文件」「图片」两页的布局测试（Suite HLL），HistoryLibraryLayoutTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryLibraryLayout.h 与 HistoryWindowLayout.h 里为两页新增的部分
 *  （docs/聊天记录管理器方案.md §11.5、§11.6）：
 *    HLL1 文件页：控件都能按编号找到，方向胶囊、三个下拉、通配符、「»」、「下载本页全部」的初始状态；
 *    HLL2 图片页隐藏搜索行与来源、类型下拉；切回文件页后搜索行重新排进来，列表随之下移（UI 约定第 5 条）；
 *    HLL3 文件行：按本机状态出现的行内按钮、按钮平时隐藏、文件夹名与来源行文字、两种标签；
 *    HLL4 ApplyHistoryLibraryRowState 只改来源行末尾的状态文字；行号越界返回 false；
 *    HLL5 ApplyHistoryLibraryChrome：标题、说明、「»」出现后标题右移、方向胶囊、分页器重建、下载按钮可用状态；
 *    HLL6 HistoryLibraryPageOfCell 与 BuildPagerModel 一致，省略号与越界返回 0；
 *    HLL7 FormatLibraryTime：今年与往年的两种写法，时间不大于 0 时为空串；
 *    HLL8 列表为空时显示主副两行文案；图片页列表是缩略图网格、编号从 kIdLibGridBase 起；
 *    HLL9 右侧取最小宽度时筛选行、搜索行、底部一行都排得下、互不重叠；
 *    HLL10 左栏标签切换：选中态、范围行显隐、右侧容器只显示一页；窗口在另一页时改过尺寸，切回来按新尺寸排版；
 *    HLL11 整个管理器（含两页）的控件编号不重复，各编号段互不重叠；
 *    HLL12 按窗口的顺序切到文件页并连续两次局部更新，分页器按钮都排进底部一行；
 *    HLL13 链接页与与我有关页的筛选区（docs/聊天记录管理器方案.md §12.4、§12.5）：方向胶囊与关系胶囊二选一，
 *          来源、类型与「下载本页全部」隐藏，切回文件页后重新排版；
 *    HLL14 链接行与与我有关行：行首、按钮编号、按钮平时隐藏、最小宽度下各列不重叠；
 *    HLL15 左栏四个标签在最小宽度下排得下；与我有关时「聊天」选中、范围行在、右侧是汇总页；
 *    HLL16 三个搜索框的悬停说明随标签与页面更换，控件销毁时取消登记。
 *  不创建窗口，纯内存构建 + 排版 + 断言。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonHistory/HistoryLibraryLayout.h"
#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../UI/BalloonHistory/HistoryConversationList.h"
#include "../UI/BalloonHistory/HistoryRow.h"
#include "../UI/BalloonHistory/HistoryTipSearchBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

#include <time.h>
#include <algorithm>
#include <memory>
#include <vector>

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//排版用的右侧区域高度（像素）
const int kTestHeight = 700;
//排版用的右侧区域宽度（像素），与原来消息记录窗的默认宽度相同
const int kTestWidth = 898;

//递归按 id 查控件；命中返回指针，否则 nullptr
DuiControl* FindById(DuiControl* node, UINT id)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->GetCtrlId() == id)
    {
        return node;
    }
    for (const auto& child : node->Children())
    {
        DuiControl* hit = FindById(child.get(), id);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

int Width(const RECT& rc)
{
    return rc.right - rc.left;
}

//两个矩形是否有重叠部分
bool RectsOverlap(const RECT& a, const RECT& b)
{
    RECT x;
    return ::IntersectRect(&x, &a, &b) != FALSE;
}

//矩形 inner 是否完整落在 outer 里
bool RectInside(const RECT& inner, const RECT& outer)
{
    return inner.left >= outer.left && inner.right <= outer.right
        && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

//一行文件的显示数据
HistoryLibraryFileRowView FileRow(const CString& name, int state)
{
    HistoryLibraryFileRowView v;
    v.m_ext = _T("docx");
    v.m_name = name;
    v.m_sourceText = _T("张三 · 产品研发群");
    v.m_sizeText = _T("2.1 MB");
    v.m_timeText = _T("10-01 18:12");
    v.m_state = state;
    return v;
}

//文件页的整页数据：三行（本机有、可下载、无法下载），共 3 页
HistoryLibraryViewData FilesData()
{
    HistoryLibraryViewData d;
    d.m_kind = kHistoryLibraryFiles;
    d.m_title = _T("文件 · 全部会话");
    d.m_subtitle = _T("共 120 个文件");
    d.m_currentPage = 1;
    d.m_pageCount = 3;
    d.m_total = 120;
    d.m_fileRows.push_back(FileRow(_T("季度报告.docx"), kHistoryLibFileLocal));
    d.m_fileRows.push_back(FileRow(_T("设计稿.zip"), kHistoryLibFileRemote));
    d.m_fileRows.push_back(FileRow(_T("录屏.mp4"), kHistoryLibFileUnavailable));
    d.m_pendingDownloads = 1;
    return d;
}

//建好并按给定尺寸排好版的文件 / 图片页
std::unique_ptr<DuiControl> BuildLibraryAt(const HistoryLibraryViewData& data, int width, int height)
{
    std::unique_ptr<DuiControl> root = BuildHistoryLibraryBody(data, width);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, width, height });
    return root;
}

//某个控件是否存在且可见
bool Visible(DuiControl* root, UINT id)
{
    DuiControl* c = FindById(root, id);
    return c != nullptr && c->IsVisible();
}

//本地时间 → Unix 毫秒
int64_t LocalMs(int year, int month, int day, int hour, int minute)
{
    const int kTmYearBase = 1900;
    const int64_t kMsPerSecond = 1000;
    struct tm t = { 0 };
    t.tm_year = year - kTmYearBase;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_isdst = -1;
    return (int64_t)_mktime64(&t) * kMsPerSecond;
}

//建一个带两页的管理器根容器，并按给定尺寸排版
std::unique_ptr<DuiControl> BuildManagerAt(int width, int height)
{
    HistoryViewData chat;
    FillMockHistory(chat);
    std::unique_ptr<DuiControl> root = BuildHistoryManagerRoot(BuildHistoryManagerPane(kManagerTypeAll, 0),
                                                               BuildHistoryWindowBody(chat),
                                                               BuildHistoryLibraryBody(FilesData(), 0),
                                                               kMgrPaneDefaultW, false);
    REQUIRE(root != nullptr);
    root->ForceLayout(RECT{ 0, 0, width, height });
    return root;
}

} // anonymous namespace

TEST_CASE("HLL1 文件页：控件都能按编号找到，各筛选控件与按钮的初始状态", "[histfiles]")
{
    HistoryLibraryViewData data = FilesData();
    data.m_direction = kHistoryLibraryDirReceived;
    data.m_source = kHistoryLibrarySourceGroup;
    data.m_category = kHistoryFileCatArchive;
    data.m_timeIndex = 2;
    data.m_wildcard = true;
    std::unique_ptr<DuiControl> root = BuildLibraryAt(data, kTestWidth, kTestHeight);
    CHECK(root->GetCtrlId() == (UINT)kIdLibBody);

    const UINT ids[] =
    {
        kIdLibHeaderRow, kIdLibExpandLeft, kIdLibTitle, kIdLibSubtitle, kIdLibSearchRow, kIdLibSearch,
        kIdLibWildcard, kIdLibFilterRow, kIdLibDirAll, kIdLibDirSent, kIdLibDirReceived, kIdLibSourceCombo,
        kIdLibCategoryCombo, kIdLibTimeCombo, kIdLibScroll, kIdLibList, kIdLibFooter, kIdLibFooterInfo,
        kIdLibPager, kIdLibPageFirst, kIdLibPagePrev, kIdLibPageNext, kIdLibPageLast, kIdLibBatchDownload,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id = " << ids[i]);
        CHECK(FindById(root.get(), ids[i]) != nullptr);
    }

    //标题与说明
    DuiLabel* title = dynamic_cast<DuiLabel*>(FindById(root.get(), kIdLibTitle));
    REQUIRE(title != nullptr);
    CHECK(title->GetText() == CString(L"文件 · 全部会话"));
    CHECK(Width(title->GetRect()) > 0);
    CHECK_FALSE(Visible(root.get(), kIdLibExpandLeft));

    //方向胶囊：选中「我接收的」
    DuiButton* dirAll = dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibDirAll));
    DuiButton* dirRecv = dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibDirReceived));
    REQUIRE(dirAll != nullptr);
    REQUIRE(dirRecv != nullptr);
    CHECK(dirAll->GetVariant() == DuiButton::Variant::Default);
    CHECK(dirRecv->GetVariant() == DuiButton::Variant::Primary);

    //三个下拉：项数与选中项
    DuiComboBox* source = dynamic_cast<DuiComboBox*>(FindById(root.get(), kIdLibSourceCombo));
    DuiComboBox* category = dynamic_cast<DuiComboBox*>(FindById(root.get(), kIdLibCategoryCombo));
    DuiComboBox* timeCombo = dynamic_cast<DuiComboBox*>(FindById(root.get(), kIdLibTimeCombo));
    REQUIRE(source != nullptr);
    REQUIRE(category != nullptr);
    REQUIRE(timeCombo != nullptr);
    CHECK(source->GetCount() == 3);
    CHECK(source->GetCurSel() == kHistoryLibrarySourceGroup);
    CHECK(category->GetCount() == 8);
    CHECK(category->GetCurSel() == kHistoryFileCatArchive);
    CHECK(timeCombo->GetCurSel() == 2);

    //通配符复选框按要求勾选
    DuiButton* wildcard = dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibWildcard));
    REQUIRE(wildcard != nullptr);
    CHECK(wildcard->GetButtonType() == DuiButton::StyleCheckbox);
    CHECK(wildcard->IsChecked());

    //底部信息与「下载本页全部」
    DuiLabel* info = dynamic_cast<DuiLabel*>(FindById(root.get(), kIdLibFooterInfo));
    REQUIRE(info != nullptr);
    CHECK(info->GetText() == CString(L"共 120 个 · 第 1/3 页"));
    CHECK(FindById(root.get(), kIdLibBatchDownload)->IsEnabled());
    data.m_pendingDownloads = 0;
    std::unique_ptr<DuiControl> none = BuildLibraryAt(data, kTestWidth, kTestHeight);
    CHECK_FALSE(FindById(none.get(), kIdLibBatchDownload)->IsEnabled());

    //分页器：1 2 3 三格，当前页是第 1 格
    DuiButton* cell0 = dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibPageCellBase + 0));
    DuiButton* cell2 = dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibPageCellBase + 2));
    REQUIRE(cell0 != nullptr);
    REQUIRE(cell2 != nullptr);
    CHECK(cell0->GetVariant() == DuiButton::Variant::Primary);
    CHECK(cell2->GetText() == CString(L"3"));
    CHECK(FindById(root.get(), kIdLibPageCellBase + 3) == nullptr);

    //列表三行，行编号从 kIdLibRowBase 起
    DuiControl* list = FindById(root.get(), kIdLibList);
    REQUIRE(list != nullptr);
    CHECK(list->Children().size() == 3);
    CHECK(FindById(root.get(), kIdLibRowBase + 2) != nullptr);
}

TEST_CASE("HLL2 图片页隐藏搜索行与来源、类型下拉；切回文件页后搜索行重新排进来、列表下移", "[histfiles],[histimages]")
{
    HistoryLibraryViewData data = FilesData();
    data.m_kind = kHistoryLibraryImages;
    data.m_fileRows.clear();
    std::unique_ptr<DuiControl> root = BuildLibraryAt(data, kTestWidth, kTestHeight);

    CHECK_FALSE(Visible(root.get(), kIdLibSearchRow));
    CHECK_FALSE(Visible(root.get(), kIdLibSourceCombo));
    CHECK_FALSE(Visible(root.get(), kIdLibCategoryCombo));
    CHECK(Visible(root.get(), kIdLibTimeCombo));
    CHECK(Visible(root.get(), kIdLibDirSent));
    DuiLabel* info = dynamic_cast<DuiLabel*>(FindById(root.get(), kIdLibFooterInfo));
    REQUIRE(info != nullptr);
    CHECK(info->GetText() == CString(L"共 120 张 · 第 1/3 页"));

    //图片页：筛选行紧接在标题区下面，时间下拉靠右
    DuiControl* filterRow = FindById(root.get(), kIdLibFilterRow);
    DuiControl* timeCombo = FindById(root.get(), kIdLibTimeCombo);
    DuiControl* scroll = FindById(root.get(), kIdLibScroll);
    REQUIRE(filterRow != nullptr);
    REQUIRE(timeCombo != nullptr);
    REQUIRE(scroll != nullptr);
    const int imagesScrollTop = scroll->GetRect().top;
    CHECK(RectInside(timeCombo->GetRect(), filterRow->GetRect()));
    CHECK(timeCombo->GetRect().right > kTestWidth / 2);

    //在图片页时窗口改过宽度，再切回文件页：搜索行出现并按新宽度排版，列表下移一个搜索行的高度
    const int kWiderW = kTestWidth + 120;
    root->ForceLayout(RECT{ 0, 0, kWiderW, kTestHeight });
    ApplyHistoryLibraryKind(root.get(), kHistoryLibraryFiles);
    CHECK(Visible(root.get(), kIdLibSearchRow));
    CHECK(Visible(root.get(), kIdLibSourceCombo));
    CHECK(Visible(root.get(), kIdLibCategoryCombo));
    DuiControl* searchRow = FindById(root.get(), kIdLibSearchRow);
    DuiControl* search = FindById(root.get(), kIdLibSearch);
    DuiControl* wildcard = FindById(root.get(), kIdLibWildcard);
    REQUIRE(searchRow != nullptr);
    REQUIRE(search != nullptr);
    REQUIRE(wildcard != nullptr);
    CHECK(Width(searchRow->GetRect()) == kWiderW);
    CHECK(Width(search->GetRect()) > 0);
    CHECK(wildcard->GetRect().left >= search->GetRect().right);
    CHECK(wildcard->GetRect().right <= kWiderW);
    CHECK(scroll->GetRect().top > imagesScrollTop);
    //来源、类型下拉排进了筛选行，与时间下拉、方向胶囊都不重叠
    DuiControl* source = FindById(root.get(), kIdLibSourceCombo);
    DuiControl* category = FindById(root.get(), kIdLibCategoryCombo);
    DuiControl* dirReceived = FindById(root.get(), kIdLibDirReceived);
    CHECK(Width(source->GetRect()) > 0);
    CHECK(Width(category->GetRect()) > 0);
    CHECK(source->GetRect().left >= dirReceived->GetRect().right);
    CHECK(category->GetRect().left >= source->GetRect().right);
    CHECK(timeCombo->GetRect().left >= category->GetRect().right);

    //再切到图片页：搜索行隐藏，列表回到原来的位置
    ApplyHistoryLibraryKind(root.get(), kHistoryLibraryImages);
    CHECK_FALSE(Visible(root.get(), kIdLibSearchRow));
    CHECK(scroll->GetRect().top == imagesScrollTop);
}

TEST_CASE("HLL3 文件行：按本机状态出现的行内按钮，按钮平时隐藏，文件夹名、来源行文字与标签", "[histfiles]")
{
    HistoryLibraryViewData data;
    data.m_kind = kHistoryLibraryFiles;
    //0 本机有；1 可下载；2 下载中；3 无法下载；4 本机有但不能定位；5 群文件文件夹
    data.m_fileRows.push_back(FileRow(_T("a.docx"), kHistoryLibFileLocal));
    data.m_fileRows.push_back(FileRow(_T("b.docx"), kHistoryLibFileRemote));
    HistoryLibraryFileRowView downloading = FileRow(_T("c.docx"), kHistoryLibFileDownloading);
    downloading.m_progress = 37;
    data.m_fileRows.push_back(downloading);
    data.m_fileRows.push_back(FileRow(_T("d.docx"), kHistoryLibFileUnavailable));
    HistoryLibraryFileRowView noLocate = FileRow(_T("e.docx"), kHistoryLibFileLocal);
    noLocate.m_canLocate = false;
    data.m_fileRows.push_back(noLocate);
    HistoryLibraryFileRowView folder = FileRow(_T("资料夹"), kHistoryLibFileUnavailable);
    folder.m_isFolder = true;
    folder.m_isGroupFile = true;
    folder.m_mineTag = _T("我上传的");
    data.m_fileRows.push_back(folder);
    std::unique_ptr<DuiControl> root = BuildLibraryAt(data, kTestWidth, kTestHeight);

    //各行出现哪些按钮：{打开, 文件夹, 下载 / 取消, 定位, ⋯}
    struct Expect
    {
        bool m_open;
        bool m_folder;
        bool m_download;
        bool m_locate;
    };
    const Expect expects[] =
    {
        { true,  true,  false, true  },
        { false, false, true,  true  },
        { false, false, true,  true  },
        { false, false, false, true  },
        { true,  true,  false, false },
        { false, false, false, true  },
    };
    for (int i = 0; i < (int)(sizeof(expects) / sizeof(expects[0])); ++i)
    {
        INFO("row = " << i);
        CHECK((FindById(root.get(), kIdLibOpenBase + i) != nullptr) == expects[i].m_open);
        CHECK((FindById(root.get(), kIdLibFolderBase + i) != nullptr) == expects[i].m_folder);
        CHECK((FindById(root.get(), kIdLibDownloadBase + i) != nullptr) == expects[i].m_download);
        CHECK((FindById(root.get(), kIdLibLocateBase + i) != nullptr) == expects[i].m_locate);
        DuiControl* more = FindById(root.get(), kIdLibMoreBase + i);
        REQUIRE(more != nullptr);
        //行内按钮都是悬停才显示
        CHECK_FALSE(more->IsVisible());
        //行容器是 HistoryRow（悬停底色与按钮显隐由它负责）
        CHECK(dynamic_cast<HistoryRow*>(FindById(root.get(), kIdLibRowBase + i)) != nullptr);
    }

    //下载按钮的文字：可下载时是「下载」，下载中是「取消」
    DuiButton* download = dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibDownloadBase + 1));
    DuiButton* cancel = dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibDownloadBase + 2));
    REQUIRE(download != nullptr);
    REQUIRE(cancel != nullptr);
    CHECK(download->GetText() == CString(L"下载"));
    CHECK(cancel->GetText() == CString(L"取消"));

    //来源行：最后一个标签是「来源 · 状态」
    DuiControl* row2 = FindById(root.get(), kIdLibRowBase + 2);
    REQUIRE(row2->Children().size() >= 2);
    DuiControl* mid = row2->Children()[1].get();
    REQUIRE(mid->Children().size() == 2);
    DuiLabel* source2 = dynamic_cast<DuiLabel*>(mid->Children()[1]->Children().back().get());
    REQUIRE(source2 != nullptr);
    CHECK(source2->GetText() == CString(L"张三 · 产品研发群 · 下载中 37%"));

    //文件夹：名字后注明「(文件夹)」；来源行前面依次是「群文件」「我上传的」两个标签；状态是本机没有这个文件夹
    DuiControl* row5 = FindById(root.get(), kIdLibRowBase + 5);
    DuiControl* mid5 = row5->Children()[1].get();
    DuiLabel* name5 = dynamic_cast<DuiLabel*>(mid5->Children()[0].get());
    REQUIRE(name5 != nullptr);
    CHECK(name5->GetText() == CString(L"资料夹 (文件夹)"));
    DuiControl* line5 = mid5->Children()[1].get();
    REQUIRE(line5->Children().size() == 3);
    DuiLabel* groupTag = dynamic_cast<DuiLabel*>(line5->Children()[0].get());
    DuiLabel* mineTag = dynamic_cast<DuiLabel*>(line5->Children()[1].get());
    DuiLabel* status5 = dynamic_cast<DuiLabel*>(line5->Children()[2].get());
    REQUIRE(groupTag != nullptr);
    REQUIRE(mineTag != nullptr);
    REQUIRE(status5 != nullptr);
    CHECK(groupTag->GetText() == CString(L"群文件"));
    CHECK(mineTag->GetText() == CString(L"我上传的"));
    CHECK(status5->GetText() == CString(L"张三 · 产品研发群 · 本机没有这个文件夹"));
    //没有标签的行，来源行只有一个标签
    DuiControl* row0 = FindById(root.get(), kIdLibRowBase + 0);
    CHECK(row0->Children()[1]->Children()[1]->Children().size() == 1);

    //文件名一列不被按钮列挤掉：按钮列整列预留，文件名列在色块与大小列之间、宽度为正
    CHECK(Width(mid->GetRect()) > 0);
    CHECK(mid->GetRect().right <= row2->Children()[2]->GetRect().left);
}

TEST_CASE("HLL4 只更新一行的状态文字；行号越界或根容器为空时返回 false", "[histfiles]")
{
    HistoryLibraryViewData data = FilesData();
    std::unique_ptr<DuiControl> root = BuildLibraryAt(data, kTestWidth, kTestHeight);

    HistoryLibraryFileRowView v = data.m_fileRows[1];
    v.m_state = kHistoryLibFileDownloading;
    v.m_progress = 150;   //超过 100 时按 100 显示
    CHECK(ApplyHistoryLibraryRowState(root.get(), 1, v));
    DuiControl* row1 = FindById(root.get(), kIdLibRowBase + 1);
    DuiLabel* status = dynamic_cast<DuiLabel*>(row1->Children()[1]->Children()[1]->Children().back().get());
    REQUIRE(status != nullptr);
    CHECK(status->GetText() == CString(L"张三 · 产品研发群 · 下载中 100%"));
    v.m_state = kHistoryLibFileLocal;
    CHECK(ApplyHistoryLibraryRowState(root.get(), 1, v));
    CHECK(status->GetText() == CString(L"张三 · 产品研发群 · 已下载"));

    //没有来源说明时只显示状态
    v.m_sourceText.Empty();
    v.m_state = kHistoryLibFileRemote;
    CHECK(ApplyHistoryLibraryRowState(root.get(), 1, v));
    CHECK(status->GetText() == CString(L"未下载"));

    CHECK_FALSE(ApplyHistoryLibraryRowState(root.get(), 3, v));
    CHECK_FALSE(ApplyHistoryLibraryRowState(root.get(), -1, v));
    CHECK_FALSE(ApplyHistoryLibraryRowState(root.get(), kIdLibRowCapacity, v));
    CHECK_FALSE(ApplyHistoryLibraryRowState(NULL, 0, v));
}

TEST_CASE("HLL5 局部更新：标题、说明、「»」出现后标题右移、方向胶囊、分页器重建与下载按钮可用状态", "[histfiles]")
{
    HistoryLibraryViewData data = FilesData();
    std::unique_ptr<DuiControl> root = BuildLibraryAt(data, kTestWidth, kTestHeight);
    DuiLabel* title = dynamic_cast<DuiLabel*>(FindById(root.get(), kIdLibTitle));
    REQUIRE(title != nullptr);
    const int titleLeft = title->GetRect().left;

    data.m_title = _T("文件 · 产品研发群");
    data.m_subtitle = _T("共 3 个文件");
    data.m_showExpandLeft = true;
    data.m_direction = kHistoryLibraryDirSent;
    data.m_currentPage = 9;
    data.m_pageCount = 20;
    data.m_total = 1000;
    data.m_pendingDownloads = 0;
    ApplyHistoryLibraryChrome(root.get(), data);

    CHECK(title->GetText() == CString(L"文件 · 产品研发群"));
    CHECK(dynamic_cast<DuiLabel*>(FindById(root.get(), kIdLibSubtitle))->GetText() == CString(L"共 3 个文件"));
    CHECK(Visible(root.get(), kIdLibExpandLeft));
    CHECK(title->GetRect().left > titleLeft);
    CHECK(FindById(root.get(), kIdLibExpandLeft)->GetRect().right <= title->GetRect().left);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibDirSent))->GetVariant() == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdLibDirAll))->GetVariant() == DuiButton::Variant::Default);
    CHECK(dynamic_cast<DuiLabel*>(FindById(root.get(), kIdLibFooterInfo))->GetText()
          == CString(L"共 1000 个 · 第 9/20 页"));
    CHECK_FALSE(FindById(root.get(), kIdLibBatchDownload)->IsEnabled());

    //分页器按新页码重建：当前页那一格是主色按钮，各格都排进了底部一行
    const std::vector<HistoryPagerCell> cells = BuildPagerModel(9, 20);
    DuiControl* footer = FindById(root.get(), kIdLibFooter);
    REQUIRE(footer != nullptr);
    int buttons = 0;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        DuiButton* cell = dynamic_cast<DuiButton*>(FindById(root.get(), (UINT)(kIdLibPageCellBase + i)));
        if (cells[i].m_isEllipsis)
        {
            CHECK(cell == nullptr);
            continue;
        }
        REQUIRE(cell != nullptr);
        ++buttons;
        CHECK((cell->GetVariant() == DuiButton::Variant::Primary) == cells[i].m_isCurrent);
        CHECK(Width(cell->GetRect()) > 0);
        CHECK(RectInside(cell->GetRect(), footer->GetRect()));
    }
    CHECK(buttons >= 3);
    //首页、末页按钮只各有一个（重建前的旧按钮已清掉）
    DuiControl* pager = FindById(root.get(), kIdLibPager);
    int firstCount = 0;
    for (const auto& child : pager->Children())
    {
        if (child->GetCtrlId() == (UINT)kIdLibPageFirst)
        {
            ++firstCount;
        }
    }
    CHECK(firstCount == 1);

    //根容器为空时什么都不做
    ApplyHistoryLibraryChrome(NULL, data);
}

TEST_CASE("HLL6 分页格子对应的页号与 BuildPagerModel 一致，省略号与越界返回 0", "[histfiles],[histimages]")
{
    const int kPageCounts[] = { 1, 3, 7, 20 };
    for (size_t p = 0; p < sizeof(kPageCounts) / sizeof(kPageCounts[0]); ++p)
    {
        const int pageCount = kPageCounts[p];
        for (int current = 1; current <= pageCount; ++current)
        {
            const std::vector<HistoryPagerCell> cells = BuildPagerModel(current, pageCount);
            for (size_t i = 0; i < cells.size(); ++i)
            {
                INFO("pageCount = " << pageCount << ", current = " << current << ", cell = " << i);
                const int expect = cells[i].m_isEllipsis ? 0 : cells[i].m_page;
                CHECK(HistoryLibraryPageOfCell((int)i, current, pageCount) == expect);
            }
            CHECK(HistoryLibraryPageOfCell((int)cells.size(), current, pageCount) == 0);
            CHECK(HistoryLibraryPageOfCell(-1, current, pageCount) == 0);
        }
    }
}

TEST_CASE("HLL7 列表时间：今年写月日时分，往年写年月日，时间不大于 0 时为空串", "[histfiles],[histimages]")
{
    const int64_t now = LocalMs(2026, 10, 5, 12, 0);
    CHECK(FormatLibraryTime(LocalMs(2026, 10, 1, 18, 12), now) == "10-01 18:12");
    CHECK(FormatLibraryTime(LocalMs(2026, 1, 2, 3, 4), now) == "01-02 03:04");
    CHECK(FormatLibraryTime(LocalMs(2025, 12, 31, 23, 59), now) == "2025-12-31");
    CHECK(FormatLibraryTime(LocalMs(2019, 3, 8, 9, 0), now) == "2019-03-08");
    CHECK(FormatLibraryTime(0, now).empty());
    CHECK(FormatLibraryTime(-5, now).empty());
}

TEST_CASE("HLL8 空列表显示主副两行文案；图片页是缩略图网格、编号从 kIdLibGridBase 起", "[histfiles],[histimages]")
{
    HistoryLibraryViewData data;
    data.m_kind = kHistoryLibraryFiles;
    data.m_emptyTitle = _T("没有找到文件");
    data.m_emptySub = _T("换个关键字试试");
    std::unique_ptr<DuiControl> list = BuildHistoryLibraryList(data, kTestWidth);
    REQUIRE(list != nullptr);
    CHECK(list->GetCtrlId() == (UINT)kIdLibList);
    DuiLabel* emptyTitle = dynamic_cast<DuiLabel*>(FindById(list.get(), kIdLibEmptyTitle));
    DuiLabel* emptySub = dynamic_cast<DuiLabel*>(FindById(list.get(), kIdLibEmptySub));
    REQUIRE(emptyTitle != nullptr);
    REQUIRE(emptySub != nullptr);
    CHECK(emptyTitle->GetText() == CString(L"没有找到文件"));
    CHECK(emptySub->GetText() == CString(L"换个关键字试试"));
    //没有副文案时不建那一行
    data.m_emptySub.Empty();
    list = BuildHistoryLibraryList(data, kTestWidth);
    CHECK(FindById(list.get(), kIdLibEmptySub) == nullptr);

    //图片页：三张缩略图
    HistoryLibraryViewData images;
    images.m_kind = kHistoryLibraryImages;
    for (int i = 0; i < 3; ++i)
    {
        HistoryRowView cell;
        cell.m_hasImageThumb = true;
        cell.m_senderName = _T("张三");
        cell.m_timeText = _T("10-01 18:12");
        //只有第一张前面有日期分组头
        if (i == 0)
        {
            cell.m_groupLabel = _T("2026-10-01");
        }
        images.m_imageCells.push_back(cell);
    }
    list = BuildHistoryLibraryList(images, kTestWidth);
    REQUIRE(list != nullptr);
    CHECK(list->GetCtrlId() == (UINT)kIdLibList);
    for (int i = 0; i < 3; ++i)
    {
        INFO("cell = " << i);
        CHECK(FindById(list.get(), kIdLibGridBase + i) != nullptr);
    }
    CHECK(FindById(list.get(), kIdLibGridBase + 3) == nullptr);
    //图片页没有文件行
    CHECK(FindById(list.get(), kIdLibRowBase) == nullptr);
}

TEST_CASE("HLL9 右侧取最小宽度时筛选行、搜索行、底部一行都排得下、互不重叠", "[histfiles]")
{
    HistoryLibraryViewData data = FilesData();
    //分页器最宽的情形：当前页在中间，两边都有省略号
    data.m_currentPage = 9;
    data.m_pageCount = 20;
    std::unique_ptr<DuiControl> root = BuildLibraryAt(data, kMgrBodyMinW, kTestHeight);

    //筛选行：三个胶囊、三个下拉依次排开，不重叠、都在这一行里
    const UINT filterIds[] =
    {
        kIdLibDirAll, kIdLibDirSent, kIdLibDirReceived, kIdLibSourceCombo, kIdLibCategoryCombo, kIdLibTimeCombo,
    };
    DuiControl* filterRow = FindById(root.get(), kIdLibFilterRow);
    REQUIRE(filterRow != nullptr);
    for (size_t i = 0; i < sizeof(filterIds) / sizeof(filterIds[0]); ++i)
    {
        INFO("id = " << filterIds[i]);
        DuiControl* c = FindById(root.get(), filterIds[i]);
        REQUIRE(c != nullptr);
        CHECK(Width(c->GetRect()) > 0);
        CHECK(RectInside(c->GetRect(), filterRow->GetRect()));
        if (i > 0)
        {
            CHECK(c->GetRect().left >= FindById(root.get(), filterIds[i - 1])->GetRect().right);
        }
    }

    //搜索行：搜索框不窄于 200 像素，复选框在它右边
    const int kSearchMinW = 200;
    DuiControl* search = FindById(root.get(), kIdLibSearch);
    DuiControl* wildcard = FindById(root.get(), kIdLibWildcard);
    CHECK(Width(search->GetRect()) >= kSearchMinW);
    CHECK(wildcard->GetRect().left >= search->GetRect().right);
    CHECK(wildcard->GetRect().right <= kMgrBodyMinW);

    //底部：信息文字、分页器、下载按钮从左到右排开，不越出这一行
    DuiControl* footer = FindById(root.get(), kIdLibFooter);
    DuiControl* info = FindById(root.get(), kIdLibFooterInfo);
    DuiControl* pager = FindById(root.get(), kIdLibPager);
    DuiControl* batch = FindById(root.get(), kIdLibBatchDownload);
    REQUIRE(footer != nullptr);
    CHECK(Width(info->GetRect()) > 0);
    CHECK(pager->GetRect().left >= info->GetRect().right);
    CHECK(batch->GetRect().left >= pager->GetRect().right);
    CHECK(RectInside(batch->GetRect(), footer->GetRect()));
    CHECK_FALSE(RectsOverlap(FindById(root.get(), kIdLibPageLast)->GetRect(), batch->GetRect()));

    //文件行：文件名一列在最小宽度下仍不窄于 120 像素
    const int kNameMinW = 120;
    DuiControl* row0 = FindById(root.get(), kIdLibRowBase);
    REQUIRE(row0 != nullptr);
    CHECK(Width(row0->Children()[1]->GetRect()) >= kNameMinW);
}

TEST_CASE("HLL10 左栏标签切换：选中态、范围行显隐、右侧只显示一页；在另一页时改过尺寸，切回来按新尺寸排版", "[historymgr],[histfiles],[histimages]")
{
    const int kWidth = 1200;
    std::unique_ptr<DuiControl> root = BuildManagerAt(kWidth, kTestHeight);
    DuiControl* host = FindById(root.get(), kIdMgrRightHost);
    DuiControl* body = FindById(root.get(), kIdHistoryBody);
    DuiControl* library = FindById(root.get(), kIdLibBody);
    REQUIRE(host != nullptr);
    REQUIRE(body != nullptr);
    REQUIRE(library != nullptr);
    REQUIRE(FindById(root.get(), kIdMgrTabRow) != nullptr);

    //建出来时是聊天记录页
    CHECK(body->IsVisible());
    CHECK_FALSE(library->IsVisible());
    CHECK(Visible(root.get(), kIdMgrScopeRow));
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrTabChat))->GetVariant() == DuiButton::Variant::Primary);

    //文件页：范围行隐藏，类型胶囊行上移；右侧只有文件页，占满右侧容器
    DuiControl* typeAll = FindById(root.get(), kIdMgrTypeAll);
    REQUIRE(typeAll != nullptr);
    const int typeTopChat = typeAll->GetRect().top;
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabFiles);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrTabFiles))->GetVariant() == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrTabChat))->GetVariant() == DuiButton::Variant::Default);
    CHECK_FALSE(Visible(root.get(), kIdMgrScopeRow));
    CHECK(typeAll->GetRect().top < typeTopChat);
    CHECK_FALSE(body->IsVisible());
    CHECK(library->IsVisible());
    CHECK(EqualRect(&library->GetRect(), &host->GetRect()));
    DuiControl* search = FindById(root.get(), kIdLibSearch);
    REQUIRE(search != nullptr);
    CHECK(Width(search->GetRect()) > 0);
    CHECK(search->GetRect().left >= host->GetRect().left);

    //停在文件页时窗口变窄，再切回聊天记录页：聊天记录页按新宽度排版，范围行回来
    const int kNarrowW = 1000;
    root->ForceLayout(RECT{ 0, 0, kNarrowW, kTestHeight });
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabChat);
    CHECK(body->IsVisible());
    CHECK_FALSE(library->IsVisible());
    CHECK(Visible(root.get(), kIdMgrScopeRow));
    CHECK(typeAll->GetRect().top == typeTopChat);
    CHECK(body->GetRect().right == kNarrowW);
    DuiControl* chatSearch = FindById(root.get(), kIdHistorySearch);
    REQUIRE(chatSearch != nullptr);
    CHECK(chatSearch->GetRect().right <= kNarrowW);

    //图片页；越界的取值按聊天记录页处理
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabImages);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrTabImages))->GetVariant() == DuiButton::Variant::Primary);
    CHECK(library->IsVisible());
    CHECK(library->GetRect().right == kNarrowW);
    ApplyHistoryManagerTab(root.get(), 7);
    CHECK(body->IsVisible());
    CHECK_FALSE(library->IsVisible());

    //左栏折叠时文件页标题区的「»」跟着出现
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabFiles);
    ApplyHistoryManagerCollapsed(root.get(), true, kMgrPaneDefaultW, _T(""));
    CHECK(Visible(root.get(), kIdLibExpandLeft));
    ApplyHistoryManagerCollapsed(root.get(), false, kMgrPaneDefaultW, _T(""));
    CHECK_FALSE(Visible(root.get(), kIdLibExpandLeft));
}

TEST_CASE("HLL12 按窗口的顺序切到文件页并局部更新（只有 1 页，连续两次）：分页器各按钮都排进了底部一行、宽度为正", "[histfiles]")
{
    const int kWidth = 1163;
    const int kHeight = 737;
    std::unique_ptr<DuiControl> root = BuildManagerAt(kWidth, kHeight);
    //与窗口的 SwitchManagerTab 同一顺序：切标签、按页别调整筛选区，再局部更新
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabFiles);
    ApplyHistoryLibraryKind(root.get(), kHistoryLibraryFiles);
    HistoryLibraryViewData data = FilesData();
    data.m_currentPage = 1;
    data.m_pageCount = 1;
    data.m_total = 5;
    data.m_pendingDownloads = 0;
    ApplyHistoryLibraryChrome(root.get(), data);
    //再以同样的页数局部更新一次：分页器宽度不变，重填的按钮必须照样排过版（2026-10-06 实机核对时发现分页器
    //整个不显示：底部一行重排时分页器矩形没变，SetRect 直接返回，新建的按钮矩形为空）
    ApplyHistoryLibraryChrome(root.get(), data);

    DuiControl* footer = FindById(root.get(), kIdLibFooter);
    DuiControl* pager = FindById(root.get(), kIdLibPager);
    DuiControl* info = FindById(root.get(), kIdLibFooterInfo);
    REQUIRE(footer != nullptr);
    REQUIRE(pager != nullptr);
    INFO("footer " << footer->GetRect().left << "," << footer->GetRect().right << " pager " << pager->GetRect().left
         << "," << pager->GetRect().right << " info " << info->GetRect().left << "," << info->GetRect().right);
    CHECK(Width(pager->GetRect()) > 0);
    const UINT ids[] = { kIdLibPageFirst, kIdLibPagePrev, kIdLibPageCellBase, kIdLibPageNext, kIdLibPageLast };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("id = " << ids[i]);
        DuiControl* c = FindById(root.get(), ids[i]);
        REQUIRE(c != nullptr);
        CHECK(c->IsVisible());
        CHECK(Width(c->GetRect()) > 0);
        CHECK(RectInside(c->GetRect(), footer->GetRect()));
    }
}

TEST_CASE("HLL11 整个管理器（含文件 / 图片页）的控件编号不重复", "[historymgr],[histfiles]")
{
    std::unique_ptr<DuiControl> root = BuildManagerAt(1200, kTestHeight);
    std::vector<UINT> ids;
    std::vector<DuiControl*> stack;
    stack.push_back(root.get());
    while (!stack.empty())
    {
        DuiControl* node = stack.back();
        stack.pop_back();
        if (node->GetCtrlId() != 0)
        {
            ids.push_back(node->GetCtrlId());
        }
        for (const auto& child : node->Children())
        {
            stack.push_back(child.get());
        }
    }
    std::sort(ids.begin(), ids.end());
    std::vector<UINT>::iterator dup = std::adjacent_find(ids.begin(), ids.end());
    INFO("duplicated id = " << ((dup == ids.end()) ? 0 : *dup));
    CHECK(dup == ids.end());

    //两页的编号段在左栏编号之后、彼此按行容量错开
    CHECK((UINT)kIdLibBody > (UINT)kIdMgrRightHost);
    CHECK((UINT)kIdLibPageCellBase + kHistoryPagerMaxCells <= (UINT)kIdLibRowBase);
    CHECK((UINT)kIdLibRowBase + kIdLibRowCapacity <= (UINT)kIdLibOpenBase);
    CHECK((UINT)kIdLibMoreBase + kIdLibRowCapacity <= (UINT)kIdLibGridBase);
    CHECK(kIdLibRowCapacity >= kHistoryLibraryModelImagePageSize);
    CHECK(kIdLibRowCapacity >= kHistoryLibraryModelFilePageSize);
    //关系胶囊在分页格子之后、行编号之前；「复制」按钮段接在时间轴导航条的编号之后
    CHECK((UINT)kIdLibPageCellBase + kHistoryPagerMaxCells <= (UINT)kIdLibRelAll);
    CHECK((UINT)kIdLibRelMine < (UINT)kIdLibRowBase);
    CHECK((UINT)kIdLibGridBase + kIdLibRowCapacity <= (UINT)kIdHistoryViewList);
    CHECK((UINT)kIdHistoryNavYearBase + kIdHistoryNavYearCapacity <= (UINT)kIdLibCopyBase);
    //左栏四个标签编号连续，标签行另有编号
    CHECK((UINT)kIdMgrTabLinks == (UINT)kIdMgrTabChat + kHistoryManagerTabLinks);
    CHECK((UINT)kIdMgrTabRow != (UINT)kIdMgrTabLinks);
}

namespace {

//链接页、与我有关页的一行
HistoryLibraryTextRowView TextRow(const CString& title, const CString& text, int relation)
{
    HistoryLibraryTextRowView v;
    v.m_title = title;
    v.m_text = text;
    v.m_relation = relation;
    v.m_sourceText = _T("张三 · 产品研发群");
    v.m_timeText = _T("10-01 18:12");
    return v;
}

//链接页或与我有关页的整页数据：两行
HistoryLibraryViewData TextData(int kind)
{
    HistoryLibraryViewData d;
    d.m_kind = kind;
    d.m_title = (kind == kHistoryLibraryLinks) ? _T("链接 · 全部会话") : _T("与我有关");
    d.m_subtitle = _T("共 2 条");
    d.m_currentPage = 1;
    d.m_pageCount = 1;
    d.m_total = 2;
    if (kind == kHistoryLibraryLinks)
    {
        HistoryLibraryTextRowView a = TextRow(_T("example.com"), _T("https://www.example.com/a"), 0);
        a.m_canOpen = true;
        d.m_textRows.push_back(a);
        d.m_textRows.push_back(TextRow(_T(""), _T("http://"), 0));
    }
    else
    {
        d.m_relation = kHistoryLibraryRelMention;
        d.m_textRows.push_back(TextRow(_T(""), _T("@我 看一下"), kHistoryLibraryRelMention));
        d.m_textRows.push_back(TextRow(_T(""), _T("好的收到"), kHistoryLibraryRelReply));
    }
    return d;
}

//一行里所有可见文字标签的文字拼在一起（查行首标签用）
CString LabelTexts(DuiControl* node)
{
    CString all;
    if (node == nullptr)
    {
        return all;
    }
    DuiLabel* label = dynamic_cast<DuiLabel*>(node);
    if (label != nullptr)
    {
        all += label->GetText();
        all += _T("|");
    }
    for (const auto& child : node->Children())
    {
        all += LabelTexts(child.get());
    }
    return all;
}

} // anonymous namespace

TEST_CASE("HLL13 链接页与与我有关页的筛选区：搜索行、方向胶囊与关系胶囊二选一、来源类型与下载按钮隐藏", "[histlinks],[histmentions]")
{
    //链接页：搜索行在，方向胶囊在，关系胶囊、来源、类型、「下载本页全部」不在
    std::unique_ptr<DuiControl> links = BuildLibraryAt(TextData(kHistoryLibraryLinks), kMgrBodyMinW, kTestHeight);
    CHECK(Visible(links.get(), kIdLibSearchRow));
    CHECK(Visible(links.get(), kIdLibDirAll));
    CHECK_FALSE(Visible(links.get(), kIdLibRelAll));
    CHECK_FALSE(Visible(links.get(), kIdLibSourceCombo));
    CHECK_FALSE(Visible(links.get(), kIdLibCategoryCombo));
    CHECK(Visible(links.get(), kIdLibTimeCombo));
    CHECK_FALSE(Visible(links.get(), kIdLibBatchDownload));
    DuiLabel* info = dynamic_cast<DuiLabel*>(FindById(links.get(), kIdLibFooterInfo));
    REQUIRE(info != nullptr);
    CHECK(info->GetText() == CString(L"共 2 条 · 第 1/1 页"));

    //与我有关页：关系胶囊在、方向胶囊不在；选中的关系胶囊是 Primary
    std::unique_ptr<DuiControl> mentions = BuildLibraryAt(TextData(kHistoryLibraryMentions), kMgrBodyMinW, kTestHeight);
    CHECK(Visible(mentions.get(), kIdLibSearchRow));
    CHECK_FALSE(Visible(mentions.get(), kIdLibDirAll));
    for (UINT id = kIdLibRelAll; id <= (UINT)kIdLibRelMine; ++id)
    {
        INFO("chip = " << id);
        CHECK(Visible(mentions.get(), id));
        DuiControl* chip = FindById(mentions.get(), id);
        CHECK(Width(chip->GetRect()) > 0);
    }
    CHECK(dynamic_cast<DuiButton*>(FindById(mentions.get(), kIdLibRelMention))->GetVariant()
          == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(mentions.get(), kIdLibRelAll))->GetVariant()
          == DuiButton::Variant::Default);

    //局部更新换选中的关系胶囊
    HistoryLibraryViewData d = TextData(kHistoryLibraryMentions);
    d.m_relation = kHistoryLibraryRelMine;
    ApplyHistoryLibraryChrome(mentions.get(), d);
    CHECK(dynamic_cast<DuiButton*>(FindById(mentions.get(), kIdLibRelMine))->GetVariant()
          == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(mentions.get(), kIdLibRelMention))->GetVariant()
          == DuiButton::Variant::Default);

    //切回文件页：方向胶囊、来源、类型、「下载本页全部」回来，关系胶囊隐藏，筛选行重新排过版
    ApplyHistoryLibraryKind(mentions.get(), kHistoryLibraryFiles);
    CHECK(Visible(mentions.get(), kIdLibDirAll));
    CHECK_FALSE(Visible(mentions.get(), kIdLibRelAll));
    CHECK(Visible(mentions.get(), kIdLibSourceCombo));
    CHECK(Visible(mentions.get(), kIdLibBatchDownload));
    CHECK(Width(FindById(mentions.get(), kIdLibDirAll)->GetRect()) > 0);
    CHECK(Width(FindById(mentions.get(), kIdLibSourceCombo)->GetRect()) > 0);
    CHECK(Width(FindById(mentions.get(), kIdLibBatchDownload)->GetRect()) > 0);
}

TEST_CASE("HLL14 链接行与与我有关行：行首、按钮编号、按钮平时隐藏、各列在行内不重叠", "[histlinks],[histmentions]")
{
    //链接页：第 0 行能打开（打开、复制链接、定位），第 1 行没有可打开的链接（没有「打开」）
    std::unique_ptr<DuiControl> links = BuildLibraryAt(TextData(kHistoryLibraryLinks), kMgrBodyMinW, kTestHeight);
    DuiControl* row0 = FindById(links.get(), kIdLibRowBase);
    REQUIRE(row0 != nullptr);
    REQUIRE(FindById(links.get(), kIdLibOpenBase) != nullptr);
    REQUIRE(FindById(links.get(), kIdLibCopyBase) != nullptr);
    REQUIRE(FindById(links.get(), kIdLibLocateBase) != nullptr);
    CHECK(FindById(links.get(), kIdLibOpenBase + 1) == nullptr);
    CHECK(FindById(links.get(), kIdLibCopyBase + 1) != nullptr);
    //文件页才有的按钮不出现
    CHECK(FindById(links.get(), kIdLibFolderBase) == nullptr);
    CHECK(FindById(links.get(), kIdLibMoreBase) == nullptr);
    //行内按钮平时隐藏
    CHECK_FALSE(FindById(links.get(), kIdLibCopyBase)->IsVisible());
    //域名与链接文字都在行内
    const CString texts = LabelTexts(row0);
    CHECK(texts.Find(_T("example.com|")) >= 0);
    CHECK(texts.Find(_T("张三 · 产品研发群|")) >= 0);

    //与我有关页：行首标签按关系显示，没有「打开」
    std::unique_ptr<DuiControl> mentions = BuildLibraryAt(TextData(kHistoryLibraryMentions), kMgrBodyMinW, kTestHeight);
    DuiControl* m0 = FindById(mentions.get(), kIdLibRowBase);
    DuiControl* m1 = FindById(mentions.get(), kIdLibRowBase + 1);
    REQUIRE(m0 != nullptr);
    REQUIRE(m1 != nullptr);
    CHECK(LabelTexts(m0).Find(_T("@我|")) == 0);
    CHECK(LabelTexts(m1).Find(_T("回复我|")) == 0);
    CHECK(FindById(mentions.get(), kIdLibOpenBase) == nullptr);
    REQUIRE(FindById(mentions.get(), kIdLibCopyBase) != nullptr);
    REQUIRE(FindById(mentions.get(), kIdLibLocateBase + 1) != nullptr);

    //右侧取最小宽度时一行的各列互不重叠、都在行内
    std::vector<DuiControl*> cols;
    for (const auto& child : m0->Children())
    {
        cols.push_back(child.get());
    }
    REQUIRE(cols.size() == 4);
    for (size_t i = 0; i < cols.size(); ++i)
    {
        INFO("col = " << i);
        CHECK(Width(cols[i]->GetRect()) > 0);
        CHECK(RectInside(cols[i]->GetRect(), m0->GetRect()));
        if (i > 0)
        {
            CHECK_FALSE(RectsOverlap(cols[i - 1]->GetRect(), cols[i]->GetRect()));
        }
    }
}

TEST_CASE("HLL15 左栏四个标签：最小宽度下都排得下；与我有关时「聊天」选中、范围行在、右侧是汇总页", "[historymgr],[histmentions],[histlinks]")
{
    //左栏取最小宽度：四个标签都在标签行里、互不重叠、宽度为正
    const int minWidth = kMgrPaneMinW + kMgrSplitBarW + kMgrBodyMinW;
    HistoryViewData chat;
    FillMockHistory(chat);
    std::unique_ptr<DuiControl> root = BuildHistoryManagerRoot(BuildHistoryManagerPane(kManagerTypeAll, 0),
                                                               BuildHistoryWindowBody(chat),
                                                               BuildHistoryLibraryBody(FilesData(), 0),
                                                               kMgrPaneMinW, false);
    root->ForceLayout(RECT{ 0, 0, minWidth, kTestHeight });
    DuiControl* tabRow = FindById(root.get(), kIdMgrTabRow);
    REQUIRE(tabRow != nullptr);
    for (int i = 0; i < kHistoryManagerVisibleTabCount; ++i)
    {
        INFO("tab = " << i);
        DuiControl* tab = FindById(root.get(), (UINT)(kIdMgrTabChat + i));
        REQUIRE(tab != nullptr);
        CHECK(Width(tab->GetRect()) > 0);
        CHECK(RectInside(tab->GetRect(), tabRow->GetRect()));
        if (i > 0)
        {
            CHECK_FALSE(RectsOverlap(FindById(root.get(), (UINT)(kIdMgrTabChat + i - 1))->GetRect(), tab->GetRect()));
        }
    }
    DuiButton* chatTab = dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrTabChat));
    REQUIRE(chatTab != nullptr);
    CHECK(chatTab->GetText() == CString(L"聊天"));

    //链接标签
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabLinks);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrTabLinks))->GetVariant() == DuiButton::Variant::Primary);
    CHECK_FALSE(Visible(root.get(), kIdMgrScopeRow));
    CHECK(Visible(root.get(), kIdLibBody));
    CHECK_FALSE(Visible(root.get(), kIdHistoryBody));

    //与我有关：「聊天」选中，范围行在（左栏与聊天标签相同），右侧是汇总页
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabMentions);
    CHECK(chatTab->GetVariant() == DuiButton::Variant::Primary);
    CHECK(dynamic_cast<DuiButton*>(FindById(root.get(), kIdMgrTabLinks))->GetVariant() == DuiButton::Variant::Default);
    CHECK(Visible(root.get(), kIdMgrScopeRow));
    CHECK(Visible(root.get(), kIdLibBody));
    CHECK_FALSE(Visible(root.get(), kIdHistoryBody));
}

TEST_CASE("HLL16 搜索框悬停说明：会话内与左栏是完整语法，文件页是文件名说明，链接页换回完整语法", "[historymgr],[searchsyntax]")
{
    std::unique_ptr<DuiControl> root = BuildManagerAt(1200, kTestHeight);
    DuiControl* chatSearch = FindById(root.get(), kIdHistorySearch);
    DuiControl* mgrSearch = FindById(root.get(), kIdMgrSearch);
    DuiControl* libSearch = FindById(root.get(), kIdLibSearch);
    REQUIRE(chatSearch != nullptr);
    REQUIRE(mgrSearch != nullptr);
    REQUIRE(libSearch != nullptr);
    const CString syntax = HistorySearchSyntaxTip();
    CHECK(syntax.Find(_T("from:")) >= 0);
    CHECK(syntax.Find(_T('\n')) > 0);
    CHECK(DuiToolTipMgr::Inst().GetText(chatSearch) == syntax);
    CHECK(DuiToolTipMgr::Inst().GetText(mgrSearch) == syntax);
    CHECK(DuiToolTipMgr::Inst().GetText(libSearch) == CString(HistoryFileNameSearchTip()));

    //文件标签：左栏只按名字找会话，不显示语法说明
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabFiles);
    CHECK(DuiToolTipMgr::Inst().GetText(mgrSearch).IsEmpty());
    //链接页：右侧搜索框按完整语法
    ApplyHistoryLibraryKind(root.get(), kHistoryLibraryLinks);
    CHECK(DuiToolTipMgr::Inst().GetText(libSearch) == syntax);
    //回到聊天标签：左栏说明回来
    ApplyHistoryManagerTab(root.get(), kHistoryManagerTabChat);
    CHECK(DuiToolTipMgr::Inst().GetText(mgrSearch) == syntax);

    //控件销毁后不再登记（析构时取消）
    DuiControl* const destroyed = chatSearch;
    root.reset();
    CHECK(DuiToolTipMgr::Inst().GetText(destroyed).IsEmpty());
}
