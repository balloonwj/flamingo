/**
 *  「消息盒子」窗口布局契约单元测试（Suite ANB），AnnounceBoxLayoutTests.cpp
 *
 *  用 BuildAnnounceBoxBody / BuildAnnounceListContent 直接构树，覆盖：
 *    ANB1  固定控件点名齐全（顶部信息 / 列表 / 右栏三项 / 底部按钮）；
 *    ANB2  顶部信息文案：有未读时带「N 条未读」，没有时不显示那一截；
 *    ANB3  行 id 按基址 + 下标生成，与 RowIndexFromCtrlId 互为逆运算；
 *    ANB4  RowIndexFromCtrlId 对越界 id 一律返回 -1（含容量边界）；
 *    ANB5  一条公告都没有时给出空状态，副文案为空则不建那个控件；
 *    ANB6  没有选中行时右栏标题与 meta 为空、正文位置显示占位提示；
 *    ANB7  选中某行时右栏显示该行的标题 / meta / 正文；
 *    ANB8  行的未读态与级别被正确注入 AnnounceRow；
 *    ANB9  「全部标为已读」按钮在没有未读时被禁用；
 *    ANB10 置顶公告的标题带「[置顶]」前缀。
 *
 *  框架 Catch2 v3。本套件只验"控件点名 + 数据注入"这层契约，不验像素级渲染。
 *  balloonwj@qq.com   2026-08-05
 */
//stdafx.h 必须最先引入：AnnounceBoxLayout.h 用到 CString 等 ATL/WTL 类型，
//按 balloonui 的头文件约定由调用方保证前置依赖（与 BalloonChatLayoutTests 同写法）。
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../BalloonAnnounce/AnnounceBoxLayout.h"
#include "../BalloonAnnounce/AnnounceRow.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

using namespace balloonannounce;
using balloonwjui::DuiControl;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;

namespace {

//造一条公告行视图，字段给出可辨认的值。
AnnounceRowView MakeRow(int64_t id, LPCTSTR title, bool unread = false,
                        int severity = 0, bool pinned = false)
{
    AnnounceRowView r;
    r.m_announcementId = id;
    r.m_title    = title;
    r.m_metaText = _T("平台运营 · 08-05 14:30");
    r.m_severity = severity;
    r.m_pinned   = pinned;
    r.m_unread   = unread;
    return r;
}

//造一份最小可用的整窗数据：两条公告，选中第一条。
AnnounceBoxViewData MakeData()
{
    AnnounceBoxViewData d;
    d.m_rows.push_back(MakeRow(1001, _T("系统维护通知"), true, 2));
    d.m_rows.push_back(MakeRow(1002, _T("新版本已发布")));
    d.m_totalCount   = 2;
    d.m_unreadCount  = 1;
    d.m_selectedIndex = 0;
    d.m_detailTitle  = _T("系统维护通知");
    d.m_detailMeta   = _T("平台运营 · 08-05 14:30");
    d.m_detailBody   = _T("本周日 02:00 至 04:00 停机维护。");
    d.m_emptyTitle   = _T("暂无公告");
    d.m_emptySubtitle= _T("平台发布公告后会出现在这里");
    return d;
}

//按 id 取标签文字；控件不存在或不是 DuiLabel 时返回空串。
CString LabelTextById(DuiControl* root, UINT id)
{
    DuiControl* c = root->FindCtrlById(id);
    if (c == NULL)
    {
        return CString(_T(""));
    }
    DuiLabel* lbl = dynamic_cast<DuiLabel*>(c);
    if (lbl == NULL)
    {
        return CString(_T(""));
    }
    return lbl->GetText();
}

} // namespace

TEST_CASE("ANB1 消息盒子固定控件点名齐全", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //左栏。
    CHECK(root->FindCtrlById(kIdAnnSummary) != nullptr);
    CHECK(root->FindCtrlById(kIdAnnScroll)  != nullptr);
    CHECK(root->FindCtrlById(kIdAnnList)    != nullptr);
    //右栏三项。
    CHECK(root->FindCtrlById(kIdAnnDetailTitle)  != nullptr);
    CHECK(root->FindCtrlById(kIdAnnDetailMeta)   != nullptr);
    CHECK(root->FindCtrlById(kIdAnnDetailBody)   != nullptr);
    CHECK(root->FindCtrlById(kIdAnnDetailScroll) != nullptr);
    //底部按钮。
    CHECK(root->FindCtrlById(kIdAnnMarkAllRead) != nullptr);
}

TEST_CASE("ANB2 顶部信息按未读数变文案", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();

    //有未读：带「N 条未读」。
    {
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        CHECK(LabelTextById(root.get(), kIdAnnSummary) == CString(_T("共 2 条 · 1 条未读")));
    }

    //一条未读都没有：不显示「0 条未读」——那是句废话，反而让人以为出了错。
    {
        d.m_unreadCount = 0;
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        CHECK(LabelTextById(root.get(), kIdAnnSummary) == CString(_T("共 2 条")));
    }
}

TEST_CASE("ANB3 行 id 按基址加下标生成且可逆解", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //两行分别是 基址+0、基址+1。
    DuiControl* row0 = root->FindCtrlById(kIdAnnRowBase + 0);
    DuiControl* row1 = root->FindCtrlById(kIdAnnRowBase + 1);
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);
    //行必须是 AnnounceRow —— 窗口靠 dynamic_cast 认行做命中测试。
    CHECK(dynamic_cast<AnnounceRow*>(row0) != nullptr);
    CHECK(dynamic_cast<AnnounceRow*>(row1) != nullptr);

    //反解回下标。
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + 0) == 0);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + 1) == 1);
}

TEST_CASE("ANB4 RowIndexFromCtrlId 对非行 id 返回 -1", "[announce][layout]")
{
    //基址以下的固定控件 id 一律不是行。
    CHECK(RowIndexFromCtrlId(kIdAnnSummary) == -1);
    CHECK(RowIndexFromCtrlId(kIdAnnMarkAllRead) == -1);
    CHECK(RowIndexFromCtrlId(0) == -1);

    //边界：基址本身是第 0 行；基址 + 容量 - 1 是最后一行；再往上就不是行了。
    //这条边界正是历史窗那边出过事的地方（拿下一段基址当上界，导致第 101 页
    //的按钮被"定位"分支接走），本用例把它钉住。
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase) == 0);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + kIdAnnRowCapacity - 1) == (int)kIdAnnRowCapacity - 1);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + kIdAnnRowCapacity) == -1);
    CHECK(RowIndexFromCtrlId(kIdAnnRowBase + kIdAnnRowCapacity + 5) == -1);
}

TEST_CASE("ANB5 没有公告时给出空状态", "[announce][layout]")
{
    std::vector<AnnounceRowView> none;

    //主 + 副两行文案都在。
    {
        std::unique_ptr<DuiControl> list =
            BuildAnnounceListContent(none, -1, _T("暂无公告"), _T("平台发布公告后会出现在这里"));
        REQUIRE(list != nullptr);
        CHECK(LabelTextById(list.get(), kIdAnnEmptyTitle) == CString(_T("暂无公告")));
        CHECK(LabelTextById(list.get(), kIdAnnEmptySub)
              == CString(_T("平台发布公告后会出现在这里")));
        //空状态下不该有任何行。
        CHECK(list->FindCtrlById(kIdAnnRowBase) == nullptr);
    }

    //副文案为空串时不建那个控件——免得留一条看不见却占位的空行。
    {
        std::unique_ptr<DuiControl> list =
            BuildAnnounceListContent(none, -1, _T("暂无公告"), _T(""));
        REQUIRE(list != nullptr);
        CHECK(list->FindCtrlById(kIdAnnEmptyTitle) != nullptr);
        CHECK(list->FindCtrlById(kIdAnnEmptySub) == nullptr);
    }
}

TEST_CASE("ANB6 未选中任何行时右栏显示占位提示", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    d.m_selectedIndex = -1;

    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //标题与 meta 留空，不显示上一次选中的残留内容。
    CHECK(LabelTextById(root.get(), kIdAnnDetailTitle).IsEmpty());
    CHECK(LabelTextById(root.get(), kIdAnnDetailMeta).IsEmpty());
    //正文位置给一句引导，而不是一片空白。
    CHECK(LabelTextById(root.get(), kIdAnnDetailBody)
          == CString(_T("从左侧选择一条公告查看详情。")));
}

TEST_CASE("ANB7 选中某行时右栏显示该行内容", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    CHECK(LabelTextById(root.get(), kIdAnnDetailTitle) == CString(_T("系统维护通知")));
    CHECK(LabelTextById(root.get(), kIdAnnDetailMeta)
          == CString(_T("平台运营 · 08-05 14:30")));
    CHECK(LabelTextById(root.get(), kIdAnnDetailBody)
          == CString(_T("本周日 02:00 至 04:00 停机维护。")));

    //选中的那一行处于选中态，其余不是。
    AnnounceRow* row0 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 0));
    AnnounceRow* row1 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 1));
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);
    CHECK(row0->IsSelected() == true);
    CHECK(row1->IsSelected() == false);
}

TEST_CASE("ANB8 行的未读态与级别被注入到 AnnounceRow", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();
    std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
    REQUIRE(root != nullptr);

    //第 0 条是未读的警告级公告，第 1 条是已读的普通公告。
    AnnounceRow* row0 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 0));
    AnnounceRow* row1 = dynamic_cast<AnnounceRow*>(root->FindCtrlById(kIdAnnRowBase + 1));
    REQUIRE(row0 != nullptr);
    REQUIRE(row1 != nullptr);

    //未读态决定要不要画标题左边那个小圆点；窗口也靠它判断点开时要不要发已读上报。
    CHECK(row0->IsUnread() == true);
    CHECK(row1->IsUnread() == false);
}

TEST_CASE("ANB9 没有未读时全部标为已读按钮被禁用", "[announce][layout]")
{
    AnnounceBoxViewData d = MakeData();

    //有未读：可点。
    {
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        DuiButton* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdAnnMarkAllRead));
        REQUIRE(btn != nullptr);
        CHECK(btn->IsEnabled() == true);
    }

    //一条未读都没有：禁用。点了也没有任何效果，留着可点会让人以为没生效。
    {
        d.m_unreadCount = 0;
        std::unique_ptr<DuiControl> root = BuildAnnounceBoxBody(d);
        REQUIRE(root != nullptr);
        DuiButton* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdAnnMarkAllRead));
        REQUIRE(btn != nullptr);
        CHECK(btn->IsEnabled() == false);
    }
}

TEST_CASE("ANB10 置顶公告的标题带前缀", "[announce][layout]")
{
    std::vector<AnnounceRowView> rows;
    rows.push_back(MakeRow(2001, _T("置顶的那条"), false, 0, true));
    rows.push_back(MakeRow(2002, _T("普通的那条")));

    std::unique_ptr<DuiControl> list = BuildAnnounceListContent(rows, -1, _T(""), _T(""));
    REQUIRE(list != nullptr);

    //置顶那条的标题带「[置顶] 」前缀：它本就排在最前，但翻到列表中段时
    //看不出边界在哪，加个标记才认得出。
    DuiControl* row0 = list->FindCtrlById(kIdAnnRowBase + 0);
    REQUIRE(row0 != nullptr);
    //标题 Label 是行内 vbox 的第一个子控件，逐层取下来。
    REQUIRE(row0->Children().size() >= 1);
    DuiControl* texts = row0->Children()[0].get();
    REQUIRE(texts != nullptr);
    REQUIRE(texts->Children().size() >= 1);
    DuiLabel* titleLbl = dynamic_cast<DuiLabel*>(texts->Children()[0].get());
    REQUIRE(titleLbl != nullptr);
    CHECK(titleLbl->GetText() == CString(_T("[置顶] 置顶的那条")));
}
