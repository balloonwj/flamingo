/**
 *  GroupManageLayoutTests.cpp
 *
 *  群管理窗各页控件树的「控件点名契约」测试 —— 与 GroupChatWindowLayoutTests
 *  同一套做法：不创建任何窗口，纯在内存里 BuildPage / BuildNav 出控件树，
 *  按既定 ctrl id 取回控件、Layout 一次、断言它们真的占到了尺寸。
 *
 *  为什么值得单独测：布局 hint 里 DuiLayout::Hint::Fixed(main, cross) 的第一个
 *  参数是<u>主轴</u>尺寸，而主轴是横是竖取决于父容器是 HBox 还是 VBox。写反了
 *  编译期毫无提示、运行期控件被压成零尺寸直接消失 —— 历史公告条目右侧那两个
 *  按钮就这么隐身过一轮（Fixed(0, 26) 在 VBox 里是"高 0、宽 26"）。这类问题
 *  只有把树排一遍、量一量矩形才抓得住。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonGroupManage/GroupManageLayout.h"
#include "../BalloonGroupManage/GroupManageModel.h"

using namespace balloonwjui;
using namespace balloongroupmanage;

namespace {

// 群管理窗右侧内容区在真实窗口里的大致尺寸（窗口 960 宽、左导航 188）。
// 用它排版，量出来的矩形才有参考意义。
const int kPageWidth  = 700;
const int kPageHeight = 560;

// 造一个"有 N 条公告"的模型，其余字段取默认。
GroupManageModel MakeModelWithAnnouncements(int count)
{
    GroupManageModel model;
    model.m_myRole = kRoleOwner;
    model.m_announcementsLoaded = true;
    model.m_announcementTotal = count;
    model.m_announcementPage = 0;

    for (int i = 0; i < count; ++i)
    {
        GroupAnnouncement a;
        a.id = 100 + i;
        a.title.Format(_T("公告 %d"), i + 1);
        a.content = _T("正文");
        a.severity = kSeverityNormal;
        a.publisher = 100023;
        a.publisherName = _T("林知夏");
        a.timeText = _T("今天 14:32");
        model.m_announcements.push_back(a);
    }
    return model;
}

// 把某页排一遍版，返回排好的树（调用方持有）。
std::unique_ptr<DuiControl> LayoutPage(int pageId, const GroupManageModel& model)
{
    std::unique_ptr<DuiControl> page = BuildPage(pageId, model);
    if (page)
    {
        page->Layout(RECT{ 0, 0, kPageWidth, kPageHeight });
    }
    return page;
}

int RectHeight(const RECT& rc)
{
    return rc.bottom - rc.top;
}

int RectWidth(const RECT& rc)
{
    return rc.right - rc.left;
}

} // anonymous namespace

// =====================================================================
// 群公告页：历史公告条目上的两个按钮
// =====================================================================

TEST_CASE("GL1 历史公告每条都有「重新发布」与「删除」两个按钮", "[groupmanage][layout][announcement]")
{
    GroupManageModel model = MakeModelWithAnnouncements(3);
    std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
    REQUIRE(page != nullptr);

    //逐条按钮的 id = 基址 + 本页内下标
    for (int i = 0; i < 3; ++i)
    {
        INFO("announcement row index = " << i);
        CHECK(page->FindCtrlById(kIdAnnounceRepublishBase + i) != nullptr);
        CHECK(page->FindCtrlById(kIdAnnounceDeleteBase + i) != nullptr);
    }
}

TEST_CASE("GL2 历史公告的按钮排版后必须占到真实尺寸", "[groupmanage][layout][announcement]")
{
    //这条用例锁的就是那次"按钮隐身"：Hint::Fixed 的第一个参数是主轴，按钮列
    //是 DuiVBox（主轴竖直），写成 Fixed(0, 26) 会让高度变成 0 —— 控件还在树里、
    //FindCtrlById 照样找得到，但界面上一个像素都看不见。所以只查 id 存在不够，
    //必须量矩形。
    GroupManageModel model = MakeModelWithAnnouncements(1);
    std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
    REQUIRE(page != nullptr);

    DuiControl* republish = page->FindCtrlById(kIdAnnounceRepublishBase);
    DuiControl* del       = page->FindCtrlById(kIdAnnounceDeleteBase);
    REQUIRE(republish != nullptr);
    REQUIRE(del != nullptr);

    const RECT rcRepublish = republish->GetRect();
    const RECT rcDelete    = del->GetRect();

    //高度：能放下一行 12px 的字加上下留白，取 20 作下限
    CHECK(RectHeight(rcRepublish) >= 20);
    CHECK(RectHeight(rcDelete) >= 20);

    //宽度：至少放得下"重新发布"四个字
    CHECK(RectWidth(rcRepublish) >= 40);
    CHECK(RectWidth(rcDelete) >= 40);

    //删除在重新发布下方，两者不重叠
    CHECK(rcDelete.top >= rcRepublish.bottom);
}

TEST_CASE("GL3 公告页的发布表单控件齐备且可见", "[groupmanage][layout][announcement]")
{
    GroupManageModel model = MakeModelWithAnnouncements(0);
    std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
    REQUIRE(page != nullptr);

    //标题 / 正文输入框
    DuiControl* title = page->FindCtrlById(kIdEditAnnounceTitle);
    DuiControl* body  = page->FindCtrlById(kIdEditAnnounceBody);
    REQUIRE(title != nullptr);
    REQUIRE(body != nullptr);
    CHECK(RectHeight(title->GetRect()) >= 20);
    CHECK(RectHeight(body->GetRect()) >= 40);

    //优先级三选一 + 清空 + 发布
    const UINT ids[] =
    {
        kIdChipSeverityNormal, kIdChipSeverityImportant, kIdChipSeverityUrgent,
        kIdBtnAnnounceClear, kIdBtnAnnouncePublish,
    };
    const int count = (int)(sizeof(ids) / sizeof(ids[0]));
    for (int i = 0; i < count; ++i)
    {
        INFO("form control id = " << ids[i]);
        DuiControl* c = page->FindCtrlById(ids[i]);
        REQUIRE(c != nullptr);
        CHECK(RectHeight(c->GetRect()) >= 16);
        CHECK(RectWidth(c->GetRect()) >= 16);
    }
}

TEST_CASE("GL4 公告一条都没有时不建条目、也不建分页栏", "[groupmanage][layout][announcement]")
{
    GroupManageModel model = MakeModelWithAnnouncements(0);
    std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
    REQUIRE(page != nullptr);

    //空列表时显示的是空状态提示，没有条目按钮，也没有翻页按钮
    CHECK(page->FindCtrlById(kIdAnnounceRepublishBase) == nullptr);
    CHECK(page->FindCtrlById(kIdAnnounceDeleteBase) == nullptr);
    CHECK(page->FindCtrlById(kIdAnnouncePagePrev) == nullptr);
    CHECK(page->FindCtrlById(kIdAnnouncePageNext) == nullptr);
}

// =====================================================================
// 群公告页：分页栏
// =====================================================================

TEST_CASE("GL5 只有一页时两个翻页按钮都不出现", "[groupmanage][layout][announcement]")
{
    //点了没反应的按钮不如不给
    GroupManageModel model = MakeModelWithAnnouncements(kAnnouncePageSize);
    std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
    REQUIRE(page != nullptr);

    CHECK(page->FindCtrlById(kIdAnnouncePagePrev) == nullptr);
    CHECK(page->FindCtrlById(kIdAnnouncePageNext) == nullptr);
}

TEST_CASE("GL6 第一页只出现「下一页」，末页只出现「上一页」", "[groupmanage][layout][announcement]")
{
    //总共 3 页（每页 kAnnouncePageSize 条，凑够 2 页多一点）
    GroupManageModel model = MakeModelWithAnnouncements(kAnnouncePageSize);
    model.m_announcementTotal = kAnnouncePageSize * 2 + 1;
    REQUIRE(model.AnnouncementPageCount() == 3);

    //第一页
    model.m_announcementPage = 0;
    {
        std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdAnnouncePagePrev) == nullptr);
        DuiControl* next = page->FindCtrlById(kIdAnnouncePageNext);
        REQUIRE(next != nullptr);
        CHECK(RectHeight(next->GetRect()) >= 16);
    }

    //中间页：两个都在
    model.m_announcementPage = 1;
    {
        std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdAnnouncePagePrev) != nullptr);
        CHECK(page->FindCtrlById(kIdAnnouncePageNext) != nullptr);
    }

    //末页
    model.m_announcementPage = 2;
    {
        std::unique_ptr<DuiControl> page = LayoutPage(kPageAnnouncement, model);
        REQUIRE(page != nullptr);
        DuiControl* prev = page->FindCtrlById(kIdAnnouncePagePrev);
        REQUIRE(prev != nullptr);
        CHECK(RectHeight(prev->GetRect()) >= 16);
        CHECK(page->FindCtrlById(kIdAnnouncePageNext) == nullptr);
    }
}

// =====================================================================
// 左侧导航：群公告分组的角色门控
// =====================================================================

TEST_CASE("GL7 导航里的「群公告」项仅群主与管理员可见", "[groupmanage][layout][announcement]")
{
    GroupManageModel model = MakeModelWithAnnouncements(2);

    model.m_myRole = kRoleOwner;
    {
        std::unique_ptr<DuiControl> nav = BuildNav(model);
        REQUIRE(nav != nullptr);
        CHECK(nav->FindCtrlById(kPageAnnouncement) != nullptr);
    }

    model.m_myRole = kRoleAdmin;
    {
        std::unique_ptr<DuiControl> nav = BuildNav(model);
        REQUIRE(nav != nullptr);
        CHECK(nav->FindCtrlById(kPageAnnouncement) != nullptr);
    }

    //普通成员看公告走群聊窗边栏，群管理窗里这一项整条不建
    model.m_myRole = kRoleMember;
    {
        std::unique_ptr<DuiControl> nav = BuildNav(model);
        REQUIRE(nav != nullptr);
        CHECK(nav->FindCtrlById(kPageAnnouncement) == nullptr);
    }
}

// =====================================================================
// 基本信息页：公告改为只读摘要之后
// =====================================================================

TEST_CASE("GL8 基本信息页不再有公告输入框，改为「去管理」跳转", "[groupmanage][layout][announcement]")
{
    //公告统一由「群公告」页发布与管理，基本信息页只留一行只读摘要。
    //若哪天有人把输入框加回来，这条会立刻失败 —— 两条写入路径正是要避免的。
    GroupManageModel model = MakeModelWithAnnouncements(1);
    model.m_myRole = kRoleOwner;
    std::unique_ptr<DuiControl> page = LayoutPage(kPageBasicInfo, model);
    REQUIRE(page != nullptr);

    //群名称与群简介的输入框仍在
    CHECK(page->FindCtrlById(kIdEditGroupName) != nullptr);
    CHECK(page->FindCtrlById(kIdEditIntro) != nullptr);

    //跳转按钮在，且占到了尺寸
    DuiControl* goTo = page->FindCtrlById(kIdBtnAnnounceGoto);
    REQUIRE(goTo != nullptr);
    CHECK(RectHeight(goTo->GetRect()) >= 16);
    CHECK(RectWidth(goTo->GetRect()) >= 30);
}

TEST_CASE("GL9 普通成员在基本信息页看不到「去管理」按钮", "[groupmanage][layout][announcement]")
{
    //他们进不了「群公告」页，给一个点了会落空的按钮没有意义
    GroupManageModel model = MakeModelWithAnnouncements(1);
    model.m_myRole = kRoleMember;
    std::unique_ptr<DuiControl> page = LayoutPage(kPageBasicInfo, model);
    REQUIRE(page != nullptr);

    CHECK(page->FindCtrlById(kIdBtnAnnounceGoto) == nullptr);
}
