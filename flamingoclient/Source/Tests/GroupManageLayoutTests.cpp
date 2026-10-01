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
 *  DP 组（[dpi]）验证整窗按 DPI 等比放大：字号与尺寸都按 96 DPI 设计，经
 *  GmScale 换算成当前 DPI 下的像素。测试用 GmSetDpiOverride 指定 DPI，
 *  不依赖跑测试这台机器的系统缩放。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

#include "../UI/BalloonGroupManage/GroupManageLayout.h"
#include "../UI/BalloonGroupManage/GroupManageModel.h"
#include "../UI/BalloonGroupManage/GroupManageMapping.h" // LL 组：ApplyGroupContext 造"刚开窗"的模型
#include "../UI/BalloonGroupManage/GroupManagePaint.h"   // DP 组：GmScale / GmSetDpiOverride
#include "../UI/BalloonGroupManage/GroupManageNavItem.h" // NV 组：离屏绘制导航项
#include "../UI/BalloonGroupManage/GroupManageIcons.h"   // NV 组：图标种类
#include "../UI/BalloonGroupManage/GroupManageButton.h"  // DZ 组：危险按钮样式
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DP2：排除输入框内部滚动条
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"       // HD3：顶栏角色标签
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"     // NV1：导航容器底色
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"        // FC 组：输入框
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"      // FC 组：开关
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"    // FC 组：下拉框
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"   // BI 组：行间分隔线
#include "../net/Protocol.h"                               // JM 组：group_join_mode 的取值

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
    //基本信息页要等群资料到达才显示表单（GL8 / GL9 查的是表单里的控件）
    model.m_groupInfoState = kLoadDone;
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

// =====================================================================
// DP 组：整窗按 DPI 等比放大
//
//   群管理窗的字号与尺寸都按 96 DPI（100% 缩放）设计，经 GmScale 换算成当前
//   DPI 下的物理像素，与窗口里 balloonui 控件的字体（随系统 DPI 放大）处在同一
//   尺度上。这里用 GmSetDpiOverride 指定 DPI，结果不随跑测试的机器变化。
// =====================================================================

namespace {

// 标准 DPI（100% 缩放）与本组用到的几档高 DPI。
const int kDpi100 = 96;
const int kDpi125 = 120;
const int kDpi150 = 144;

// DP2 比较时允许的误差（像素）。每个尺寸常量换算时各有不超过 0.5px 的取整误差，
// 一个控件的宽高或它与相邻控件的间隔通常由两三个常量相加而成，3px 足够覆盖。
const int kScaleTolerancePx = 3;

// DP2 最多报告的不符合条数。漏换算的常量往往一处牵连一串控件，全列出来反而
// 看不清，列前若干条足够定位。
const size_t kMaxReportedMismatches = 20;

// 各棵树在 96 DPI 下的排版尺寸（像素），取真实窗口里的大致尺寸：
// 窗口客户区 960×660，顶栏高 46，左导航宽 188。
const int kHeaderWidth  = 960;
const int kHeaderHeight = 46;
const int kNavWidth     = 188;
const int kNavHeight    = 614;

/**
 *  在作用域内把群管理窗的 DPI 固定为指定值，离开作用域恢复为取系统 DPI。
 */
class ScopedGmDpi
{
public:
    // 指定本作用域内 GmDpi() 的返回值。
    explicit ScopedGmDpi(int dpi)
    {
        GmSetDpiOverride(dpi);
    }

    // 恢复为取系统 DPI，不影响后续用例。
    ~ScopedGmDpi()
    {
        GmSetDpiOverride(0);
    }
};

// 被比较的树：顶栏、左导航，或某个页面（取值为页面 id）。
const int kTreeHeader = -1;
const int kTreeNav    = -2;

/**
 *  按指定 DPI 构建一棵树并排版。
 *    tree：  kTreeHeader / kTreeNav / 页面 id。
 *    dpi：   构建与排版时使用的 DPI。
 *    model： 数据模型。
 *  @return 排好版的树，所有权归调用方。
 */
std::unique_ptr<DuiControl> BuildTreeAtDpi(int tree, int dpi, const GroupManageModel& model)
{
    // 构建与排版都要在指定 DPI 下进行：自绘控件在 Layout 里也可能用到尺寸常量
    ScopedGmDpi guard(dpi);

    std::unique_ptr<DuiControl> root;
    int width  = kPageWidth;
    int height = kPageHeight;
    if (tree == kTreeHeader)
    {
        root   = BuildHeader(model);
        width  = kHeaderWidth;
        height = kHeaderHeight;
    }
    else if (tree == kTreeNav)
    {
        root   = BuildNav(model);
        width  = kNavWidth;
        height = kNavHeight;
    }
    else
    {
        root = BuildPage(tree, model);
    }

    if (root)
    {
        root->Layout(RECT{ 0, 0, GmScale(width), GmScale(height) });
    }
    return root;
}

/**
 *  核对一个量在高 DPI 下是否是 96 DPI 下的等比放大值，不是就记一条。
 *    base / scaled：96 DPI 与高 DPI 下量到的值（像素）。
 *    dpi：          高 DPI。
 *    what：         出错时报告里写的名称。
 *    errors：       [出参] 不符合的条目追加到这里。
 */
void ExpectScaled(int base, int scaled, int dpi, const std::string& what,
                  std::vector<std::string>& errors)
{
    const int expected = ::MulDiv(base, dpi, kDpi100);
    if (std::abs(scaled - expected) > kScaleTolerancePx)
    {
        std::ostringstream os;
        os << what << ": 96dpi=" << base << ", expected=" << expected << ", got=" << scaled;
        errors.push_back(os.str());
    }
}

// 生成控件在报告里的名字：类型名 + ctrlId。
std::string DescribeCtrl(const DuiControl& ctrl)
{
    std::ostringstream os;
    os << typeid(ctrl).name() << "#" << ctrl.GetCtrlId();
    return os.str();
}

/**
 *  逐节点比较两棵结构相同的树：高 DPI 那棵的每个控件，宽高与位置都应是 96 DPI
 *  那棵的等比放大。
 *
 *  位置比的是<u>相对偏移</u>而不是绝对坐标：第一个子控件相对父控件左上角的偏移，
 *  其余子控件相对前一个兄弟控件左上角的偏移。一列若干行逐行堆叠时，每行各有一点
 *  取整误差，绝对坐标会越往下偏得越多，比相对偏移则不会累积；而漏换算的尺寸、
 *  内边距、间距照样会在对应的那一处偏移上暴露出来。
 *
 *  有两类节点不比：
 *    · balloonui 输入框内部自带的 DuiScrollBar（连同其子树）。它的尺寸由 balloonui
 *      自己决定，不经群管理窗的换算，不属于本组要守护的范围。
 *    · 纯占位控件（类型恰好是 DuiControl、没有子控件）的宽高。它以 Weight 吃掉剩余
 *      空间，前面各项的取整误差全累积在它身上；它前面若有尺寸漏换算，照样会在它
 *      后面那个兄弟控件的偏移上暴露出来。它自己的偏移仍然照比。
 *    base / scaled：96 DPI 与高 DPI 下的同一个节点。
 *    dpi：          高 DPI。
 *    path：         本节点在树里的路径，写进报告便于定位。
 *    errors：       [出参] 不符合的条目追加到这里。
 */
void CompareScaledTree(const DuiControl& base, const DuiControl& scaled, int dpi,
                       const std::string& path, std::vector<std::string>& errors)
{
    const RECT rb = base.GetRect();
    const RECT rs = scaled.GetRect();

    const bool isSpacer = (typeid(base) == typeid(DuiControl)) && base.Children().empty();
    if (!isSpacer)
    {
        ExpectScaled(RectWidth(rb), RectWidth(rs), dpi, path + " width", errors);
        ExpectScaled(RectHeight(rb), RectHeight(rs), dpi, path + " height", errors);
    }

    const std::vector<std::unique_ptr<DuiControl> >& kidsB = base.Children();
    const std::vector<std::unique_ptr<DuiControl> >& kidsS = scaled.Children();
    if (kidsB.size() != kidsS.size())
    {
        errors.push_back(path + ": child count differs between the two DPIs");
        return;
    }

    for (size_t i = 0; i < kidsB.size(); ++i)
    {
        if (dynamic_cast<const DuiScrollBar*>(kidsB[i].get()) != NULL)
        {
            continue;
        }
        const RECT cb = kidsB[i]->GetRect();
        const RECT cs = kidsS[i]->GetRect();
        const RECT anchorB = (i == 0) ? rb : kidsB[i - 1]->GetRect();
        const RECT anchorS = (i == 0) ? rs : kidsS[i - 1]->GetRect();

        std::ostringstream os;
        os << path << "/" << i << ":" << DescribeCtrl(*kidsB[i]);
        const std::string childPath = os.str();

        ExpectScaled(cb.left - anchorB.left, cs.left - anchorS.left, dpi,
                     childPath + " dx", errors);
        ExpectScaled(cb.top - anchorB.top, cs.top - anchorS.top, dpi,
                     childPath + " dy", errors);
        CompareScaledTree(*kidsB[i], *kidsS[i], dpi, childPath, errors);
    }
}

// 造一个各页都有内容可排的模型：mock 数据，且公告与敏感词都标记为已加载。
GroupManageModel MakeFullMockModel()
{
    GroupManageModel model;
    FillMockData(model);
    model.m_myRole = kRoleOwner;
    model.m_announcementsLoaded  = true;
    model.m_sensitiveWordsLoaded = true;
    return model;
}

} // anonymous namespace

TEST_CASE("DP1 尺寸换算：96 DPI 原样，高 DPI 按比例四舍五入", "[groupmanage][dpi]")
{
    {
        ScopedGmDpi guard(kDpi100);
        CHECK(GmDpi() == kDpi100);
        CHECK(GmScale(188) == 188);
        CHECK(GmScale(46) == 46);
        CHECK(GmScale(0) == 0);
    }
    {
        ScopedGmDpi guard(kDpi125);
        CHECK(GmDpi() == kDpi125);
        CHECK(GmScale(188) == 235);
        CHECK(GmScale(46) == 58);    // 57.5 四舍五入
        CHECK(GmScale(13) == 16);    // 16.25
    }
    {
        ScopedGmDpi guard(kDpi150);
        CHECK(GmScale(188) == 282);
        CHECK(GmScale(13) == 20);    // 19.5 四舍五入
    }

    //覆盖值撤掉之后回到系统 DPI，而系统 DPI 至少是 96
    CHECK(GmDpi() >= kDpi100);
}

TEST_CASE("DP2 顶栏、导航与每个页面在 125% 下都是 100% 的等比放大", "[groupmanage][dpi]")
{
    const GroupManageModel model = MakeFullMockModel();

    const int trees[] = { kTreeHeader, kTreeNav,
                          kPageMembers, kPageApproval, kPageMute, kPageBasicInfo,
                          kPagePerms, kPageDanger, kPageSensitive, kPageAnnouncement };
    for (size_t t = 0; t < sizeof(trees) / sizeof(trees[0]); ++t)
    {
        INFO("tree=" << trees[t]);
        std::unique_ptr<DuiControl> base   = BuildTreeAtDpi(trees[t], kDpi100, model);
        std::unique_ptr<DuiControl> scaled = BuildTreeAtDpi(trees[t], kDpi125, model);
        REQUIRE(base != nullptr);
        REQUIRE(scaled != nullptr);

        std::vector<std::string> errors;
        CompareScaledTree(*base, *scaled, kDpi125, DescribeCtrl(*base), errors);

        std::ostringstream report;
        for (size_t i = 0; i < errors.size() && i < kMaxReportedMismatches; ++i)
        {
            report << errors[i] << "\n";
        }
        INFO("mismatches=" << errors.size() << "\n" << report.str());
        CHECK(errors.empty());
    }
}

TEST_CASE("DP3 自绘文字的字号随 DPI 等比放大", "[groupmanage][dpi]")
{
    {
        ScopedGmDpi guard(kDpi100);
        CHECK(GmFontPixelHeight(13) == 13);
        CHECK(GmFontPixelHeight(16) == 16);
    }
    {
        ScopedGmDpi guard(kDpi125);
        CHECK(GmFontPixelHeight(13) == 16);
        CHECK(GmFontPixelHeight(16) == 20);
    }
    {
        ScopedGmDpi guard(kDpi150);
        CHECK(GmFontPixelHeight(12) == 18);
    }
}

TEST_CASE("DP4 窗口初始大小随 DPI 放大，且不超出屏幕工作区、不小于最小尺寸", "[groupmanage][dpi]")
{
    // 足够大的工作区：不需要缩
    const RECT bigArea = { 0, 0, 2560, 1400 };

    SECTION("100% 缩放取设计尺寸")
    {
        const SIZE sz = ComputeInitialClientSize(kDpi100, bigArea);
        CHECK(sz.cx == 960);
        CHECK(sz.cy == 660);
    }

    SECTION("125% 缩放等比放大")
    {
        const SIZE sz = ComputeInitialClientSize(kDpi125, bigArea);
        CHECK(sz.cx == 1200);
        CHECK(sz.cy == 825);
    }

    SECTION("工作区放不下时缩到工作区以内并留出边距")
    {
        //125% 下边距为 25px（20px 按 DPI 换算）；工作区高 860，最多只能给 810
        const RECT area = { 0, 0, 1600, 860 };
        const SIZE sz = ComputeInitialClientSize(kDpi125, area);
        CHECK(sz.cx == 1200);
        CHECK(sz.cy == 810);
    }

    SECTION("缩小后不能小于同样按 DPI 换算过的最小尺寸")
    {
        //125% 下最小高度为 700（560 换算），工作区只够给 670，以最小尺寸为准
        const RECT area = { 0, 0, 1280, 720 };
        const SIZE sz = ComputeInitialClientSize(kDpi125, area);
        CHECK(sz.cx == 1200);
        CHECK(sz.cy == 700);
    }
}

// =====================================================================
// TS 组：文字样式表（字号表）
//
//   群管理窗所有自绘文字只能取自这张表（MakeText / MakeButton 只收样式、不收像素
//   值，由编译器保证）。取值来自对 design/群管理1/6/7/8.png 的实测：同一段文字用
//   微软雅黑渲染量宽度反推设计稿字号，再按窗口宽度比例 960/1125 折算到 96 DPI。
// =====================================================================

namespace {

// 表里全部档位，TS3 逐档检查用。
const int kAllTextStyles[] = { kGmTextPageTitle, kGmTextCardTitle, kGmTextItemTitle,
                               kGmTextBody, kGmTextCaption };

// 任何一档在 96 DPI 下都不应小于这个字号（像素）。
const int kMinTextPx = 12;

} // anonymous namespace

TEST_CASE("TS1 字号表各档的字号与字重符合约定", "[groupmanage][textstyle]")
{
    CHECK(GmTextStyleSpec(kGmTextPageTitle).m_px == 15);
    CHECK(GmTextStyleSpec(kGmTextPageTitle).m_weight == kGmFontBold);

    CHECK(GmTextStyleSpec(kGmTextCardTitle).m_px == 14);
    CHECK(GmTextStyleSpec(kGmTextCardTitle).m_weight == kGmFontBold);

    CHECK(GmTextStyleSpec(kGmTextItemTitle).m_px == 14);
    CHECK(GmTextStyleSpec(kGmTextItemTitle).m_weight == kGmFontNormal);

    CHECK(GmTextStyleSpec(kGmTextBody).m_px == 13);
    CHECK(GmTextStyleSpec(kGmTextBody).m_weight == kGmFontNormal);

    CHECK(GmTextStyleSpec(kGmTextCaption).m_px == 12);
    CHECK(GmTextStyleSpec(kGmTextCaption).m_weight == kGmFontNormal);
}

TEST_CASE("TS2 各档层级递减，不会出现下一级比上一级更醒目", "[groupmanage][textstyle]")
{
    const GmTextSpec page    = GmTextStyleSpec(kGmTextPageTitle);
    const GmTextSpec card    = GmTextStyleSpec(kGmTextCardTitle);
    const GmTextSpec item    = GmTextStyleSpec(kGmTextItemTitle);
    const GmTextSpec body    = GmTextStyleSpec(kGmTextBody);
    const GmTextSpec caption = GmTextStyleSpec(kGmTextCaption);

    CHECK(page.m_px > card.m_px);

    //卡片标题与条目标题字号相同，靠字重拉开
    CHECK(card.m_px >= item.m_px);
    CHECK(card.m_weight > item.m_weight);

    CHECK(item.m_px > body.m_px);
    CHECK(body.m_px > caption.m_px);
}

TEST_CASE("TS3 任何一档在 100% 缩放下都不小于 12px", "[groupmanage][textstyle]")
{
    for (size_t i = 0; i < sizeof(kAllTextStyles) / sizeof(kAllTextStyles[0]); ++i)
    {
        INFO("style=" << kAllTextStyles[i]);
        CHECK(GmTextStyleSpec(kAllTextStyles[i]).m_px >= kMinTextPx);
    }
}

// =====================================================================
// HD 组：顶栏（群头像 + 群名 + 本人角色标签 + 成员人数）
//
//   群名按文字实际宽度撑开、放不下时才在末尾省略；角色标签与人数紧跟在群名后面。
//   此前群名被写死成 130px 宽，窗口再宽也只显示几个字。
//   本组固定在 96 DPI 下排版，结果不随跑测试的机器变化。
// =====================================================================

namespace {

// 两段间距的上限（96 DPI 下的像素）。HD4 用它确认群名、标签、人数是挨着排的，
// 不再有一大段空白。
const int kMaxHeaderGapPx = 12;

/**
 *  按 96 DPI 构建并排版顶栏。
 *    name：群名；role：本人角色。
 *  @return 排好版的顶栏，所有权归调用方。
 */
std::unique_ptr<DuiControl> LayoutHeaderAt96(LPCTSTR name, GroupRole role)
{
    ScopedGmDpi guard(kDpi100);
    GroupManageModel model;
    FillMockData(model);
    model.m_groupName = name;
    model.m_myRole    = role;

    std::unique_ptr<DuiControl> header = BuildHeader(model);
    if (header)
    {
        header->Layout(RECT{ 0, 0, kHeaderWidth, kHeaderHeight });
    }
    return header;
}

} // anonymous namespace

TEST_CASE("HD1 常见长度的群名在顶栏里完整显示", "[groupmanage][header]")
{
    const LPCTSTR names[] = { _T("设计组·周会"), _T("莫梅、黄涛波、江怡、向瑶娜") };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        std::unique_ptr<DuiControl> header = LayoutHeaderAt96(names[i], kRoleOwner);
        REQUIRE(header != nullptr);
        DuiControl* name = header->FindCtrlById(kIdHeaderName);
        REQUIRE(name != nullptr);

        int textW = 0;
        {
            ScopedGmDpi guard(kDpi100);
            textW = GmMeasureTextWidth(names[i], kGmTextPageTitle);
        }
        INFO("name index=" << i << ", textW=" << textW);
        REQUIRE(textW > 0);
        CHECK(RectWidth(name->GetRect()) >= textW);
    }
}

TEST_CASE("HD2 群名特别长时只省略群名，角色标签与人数仍完整留在顶栏里", "[groupmanage][header]")
{
    CString longName;
    const int kLongNameRepeat = 8;
    for (int i = 0; i < kLongNameRepeat; ++i)
    {
        longName += _T("非常非常长的群名");
    }

    std::unique_ptr<DuiControl> header = LayoutHeaderAt96(longName, kRoleAdmin);
    REQUIRE(header != nullptr);
    DuiControl* name = header->FindCtrlById(kIdHeaderName);
    DuiControl* role = header->FindCtrlById(kIdHeaderRole);
    DuiControl* meta = header->FindCtrlById(kIdHeaderMeta);
    REQUIRE(name != nullptr);
    REQUIRE(role != nullptr);
    REQUIRE(meta != nullptr);

    int nameTextW = 0;
    {
        ScopedGmDpi guard(kDpi100);
        nameTextW = GmMeasureTextWidth(longName, kGmTextPageTitle);
    }

    //群名被收窄了（末尾显示省略号），而不是把后面两项挤出去
    CHECK(RectWidth(name->GetRect()) < nameTextW);
    CHECK(RectWidth(role->GetRect()) > 0);
    CHECK(RectWidth(meta->GetRect()) > 0);
    CHECK(role->GetRect().right <= header->GetRect().right);
    CHECK(meta->GetRect().right <= header->GetRect().right);
    CHECK(role->GetRect().left >= name->GetRect().right);
}

TEST_CASE("HD3 角色标签按本人角色显示群主 / 管理员 / 成员", "[groupmanage][header]")
{
    const GroupRole roles[]  = { kRoleOwner, kRoleAdmin, kRoleMember };
    const LPCTSTR   labels[] = { _T("群主"), _T("管理员"), _T("成员") };
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
    {
        std::unique_ptr<DuiControl> header = LayoutHeaderAt96(_T("设计组·周会"), roles[i]);
        REQUIRE(header != nullptr);
        DuiBadge* badge = dynamic_cast<DuiBadge*>(header->FindCtrlById(kIdHeaderRole));
        INFO("role=" << roles[i]);
        REQUIRE(badge != nullptr);
        CHECK(badge->GetText() == labels[i]);
        CHECK(RectWidth(badge->GetRect()) > 0);
    }
}

TEST_CASE("HD4 群名、角色标签、人数依次紧挨着排列", "[groupmanage][header]")
{
    std::unique_ptr<DuiControl> header = LayoutHeaderAt96(_T("设计组·周会"), kRoleOwner);
    REQUIRE(header != nullptr);
    DuiControl* name = header->FindCtrlById(kIdHeaderName);
    DuiControl* role = header->FindCtrlById(kIdHeaderRole);
    DuiControl* meta = header->FindCtrlById(kIdHeaderMeta);
    REQUIRE(name != nullptr);
    REQUIRE(role != nullptr);
    REQUIRE(meta != nullptr);

    const int gapNameRole = role->GetRect().left - name->GetRect().right;
    const int gapRoleMeta = meta->GetRect().left - role->GetRect().right;
    INFO("gapNameRole=" << gapNameRole << ", gapRoleMeta=" << gapRoleMeta);
    CHECK(gapNameRole == gapRoleMeta);
    CHECK(gapNameRole > 0);
    CHECK(gapNameRole <= kMaxHeaderGapPx);
}

// =====================================================================
// NV 组：左侧导航的底色与选中样式
//
//   取色来自 design/群管理8.png（普通项）与 群管理1/6.png（危险项）：导航浅灰底，
//   选中项是白色圆角卡片加浅描边，普通项选中时只有图标变橙、文字深色加粗；危险项
//   无论选中与否图标与文字都是红色。做法是把导航项画到一张先铺好导航底色的离屏
//   位图上再数像素。图标描边经过抗锯齿，取"与底色差异最大的那个像素"比颜色并留
//   容差；文字是 ClearType 渲染，笔画中心的像素就是文字色本身，可以精确比对。
//   "没有橙色"只在图标区检查，原因见 AnyNearInColumns 的说明。
// =====================================================================

namespace {

// 设计稿取色。
const COLORREF kNavBgExpected   = RGB(0xFA, 0xFA, 0xF9);   // 导航底色
const COLORREF kNavCardWhite    = RGB(0xFF, 0xFF, 0xFF);   // 选中卡片底色
const COLORREF kNavAccentOrange = RGB(0xFF, 0x7A, 0x45);   // 选中普通项的图标色
const COLORREF kNavActiveText   = RGB(0x1A, 0x1A, 0x1A);   // 选中普通项的文字色
const COLORREF kNavIdleText     = RGB(0x6B, 0x6B, 0x6B);   // 未选项的文字色
const COLORREF kNavIdleIcon     = RGB(0x9A, 0x9A, 0x9A);   // 未选项的图标色
const COLORREF kNavDangerRed    = RGB(0xEF, 0x44, 0x44);   // 危险项的图标与文字色

// 导航项在 96 DPI 下的尺寸（像素）：导航宽 188 减左右内边距各 12，行高 36。
const int kNavItemWidth  = 164;
const int kNavItemHeight = 36;

// 图标与文字所在的横向范围（像素，相对导航项左缘），与 GroupManageNavItem 的
// kIconX / kIconSize / kLabelX 一致。文字区只取前 60px，避开右侧计数徽章。
const int kNavIconLeft  = 16;
const int kNavIconRight = 32;
const int kNavTextLeft  = 42;
const int kNavTextRight = 102;

// 图标颜色比对的每通道容差：抗锯齿后覆盖最充分的像素离目标色仍可能差几十。
const int kIconColorTolerance = 40;

// "图标区没有橙色"这项检查用的每通道容差，比上面的小。红色向白色或浅灰抗锯齿混色时，
// 绿、蓝两通道始终相等（实测 #F26767）；而品牌橙 #FF7A45 的绿、蓝两通道相差 0x35 = 53。
// 容差不超过 26 时，红色的任何混色都不可能同时落进橙色的绿、蓝两个通道范围，不会
// 误判；真用了橙色的图标，覆盖最充分的像素仍在这个容差以内。
const int kNoOrangeTolerance = 20;

// 选中卡片至少应占到的白色像素数：卡片几乎铺满整项，取整项面积的一半作下限。
const int kMinCardWhitePixels = kNavItemWidth * kNavItemHeight / 2;

// 描边判定：卡片上沿最深的那一行应比导航底色与卡片白底都明显深，至少深到这个值。
const int kMaxBorderBrightness = 0xF2;

/**
 *  在作用域内启动 GDI+，离开时关闭。导航项的圆角底与图标都用 GDI+ 绘制。
 */
class ScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    ScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~ScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

/**
 *  一次离屏绘制的结果：按行存放的 32 位像素（0x00RRGGBB）。
 */
struct NavPaintResult
{
    std::vector<unsigned int> m_pixels;   // 像素，行优先
    int                       m_width;    // 宽（像素）
    int                       m_height;   // 高（像素）
};

// 0x00RRGGBB 与 COLORREF 互转。
unsigned int ToRgb(COLORREF c)
{
    return ((unsigned int)GetRValue(c) << 16) | ((unsigned int)GetGValue(c) << 8)
         | (unsigned int)GetBValue(c);
}

COLORREF FromRgb(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/**
 *  把一个控件画到铺好底色的离屏位图上，取回像素。调用方须已把控件的矩形设为
 *  (0, 0, width, height)，并已处在想要的 DPI 下。
 *    ctrl：  要绘制的控件（借用）；
 *    width / height：位图尺寸（像素）；
 *    bgColor：先铺的底色。
 *  @return 绘制结果；位图建不出来时 m_pixels 为空。
 */
NavPaintResult PaintToPixels(DuiControl& ctrl, int width, int height, COLORREF bgColor)
{
    NavPaintResult result;
    result.m_width  = width;
    result.m_height = height;
    const RECT rc = { 0, 0, width, height };

    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = -height;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (memDC == NULL || bmp == NULL || bits == NULL)
    {
        if (bmp != NULL)
        {
            ::DeleteObject(bmp);
        }
        if (memDC != NULL)
        {
            ::DeleteDC(memDC);
        }
        return result;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);

    HBRUSH bg = ::CreateSolidBrush(bgColor);
    ::FillRect(memDC, &rc, bg);
    ::DeleteObject(bg);

    ctrl.OnPaint(memDC, rc);
    ::GdiFlush();

    const unsigned int* px = (const unsigned int*)bits;
    for (int i = 0; i < width * height; ++i)
    {
        result.m_pixels.push_back(px[i] & 0x00FFFFFFu);
    }

    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return result;
}

/**
 *  在 96 DPI 下把一个导航项画到铺好导航底色的离屏位图上。
 *    active：是否选中；danger：是否危险项；iconKind：图标种类（GmIconKind）。
 *  @return 绘制结果；位图建不出来时 m_pixels 为空。
 */
NavPaintResult PaintNavItem(bool active, bool danger, int iconKind)
{
    ScopedGmDpi guard(kDpi100);
    GroupManageNavItem item;
    item.SetNavInfo(iconKind, _T("成员管理"), danger);
    item.SetActive(active);
    item.SetRect(RECT{ 0, 0, kNavItemWidth, kNavItemHeight });
    return PaintToPixels(item, kNavItemWidth, kNavItemHeight, kNavBgExpected);
}

// 统计与 c 完全相同的像素个数。
int CountExact(const NavPaintResult& r, COLORREF c)
{
    const unsigned int want = ToRgb(c);
    int n = 0;
    for (size_t i = 0; i < r.m_pixels.size(); ++i)
    {
        if (r.m_pixels[i] == want)
        {
            ++n;
        }
    }
    return n;
}

// 三通道各自的差都不超过 tol 视为相近。
bool ColorNear(COLORREF a, COLORREF b, int tol)
{
    return std::abs((int)GetRValue(a) - (int)GetRValue(b)) <= tol
        && std::abs((int)GetGValue(a) - (int)GetGValue(b)) <= tol
        && std::abs((int)GetBValue(a) - (int)GetBValue(b)) <= tol;
}

// 在 [left, right) 这一竖条里找与 base 差异最大的像素（墨迹覆盖最充分的一点）。
COLORREF MostInked(const NavPaintResult& r, int left, int right, COLORREF base)
{
    unsigned int best = ToRgb(base);
    int bestDiff = -1;
    for (int y = 0; y < r.m_height; ++y)
    {
        for (int x = left; x < right; ++x)
        {
            const COLORREF c = FromRgb(r.m_pixels[(size_t)(y * r.m_width + x)]);
            const int diff = std::abs((int)GetRValue(c) - (int)GetRValue(base))
                           + std::abs((int)GetGValue(c) - (int)GetGValue(base))
                           + std::abs((int)GetBValue(c) - (int)GetBValue(base));
            if (diff > bestDiff)
            {
                bestDiff = diff;
                best = ToRgb(c);
            }
        }
    }
    return FromRgb(best);
}

/**
 *  在 [left, right) 这一竖条里找有没有与 c 相近的像素。
 *
 *  只用于图标区，不用于文字区：文字是 ClearType 渲染，红字边缘会出现各通道覆盖
 *  程度不同的彩色像素（实测危险项文字边缘有 #F76744），恰好落进橙色的容差里，
 *  并不是真用橙色画的。图标用 GDI+ 灰度抗锯齿绘制，没有这种彩边；而导航项若误用
 *  了品牌橙，也只会出现在图标上（文字色另有精确比对）。
 *    where：[出参，可为 NULL] 找到时写入第一个命中像素的位置与颜色，便于失败时定位。
 */
bool AnyNearInColumns(const NavPaintResult& r, int left, int right, COLORREF c, int tol,
                      std::string* where)
{
    for (int y = 0; y < r.m_height; ++y)
    {
        for (int x = left; x < right; ++x)
        {
            const unsigned int rgb = r.m_pixels[(size_t)(y * r.m_width + x)];
            if (ColorNear(FromRgb(rgb), c, tol))
            {
                if (where != NULL)
                {
                    std::ostringstream os;
                    os << "x=" << x << " y=" << y << " rgb=" << std::hex << rgb;
                    *where = os.str();
                }
                return true;
            }
        }
    }
    return false;
}

// 卡片上沿（前几行）正中那一列最深的亮度（三通道平均）。
int TopEdgeDarkest(const NavPaintResult& r)
{
    const int kEdgeRows = 6;
    const int x = r.m_width / 2;
    int darkest = 0xFF;
    for (int y = 0; y < kEdgeRows && y < r.m_height; ++y)
    {
        const COLORREF c = FromRgb(r.m_pixels[(size_t)(y * r.m_width + x)]);
        const int lum = ((int)GetRValue(c) + (int)GetGValue(c) + (int)GetBValue(c)) / 3;
        if (lum < darkest)
        {
            darkest = lum;
        }
    }
    return darkest;
}

// 用某种颜色的矩形作底时，文字区最深的像素（即文字色本身）。
COLORREF TextColorOf(const NavPaintResult& r, COLORREF base)
{
    return MostInked(r, kNavTextLeft, kNavTextRight, base);
}

} // anonymous namespace

TEST_CASE("NV1 导航栏底色为设计稿的浅灰", "[groupmanage][nav]")
{
    GroupManageModel model;
    FillMockData(model);
    std::unique_ptr<DuiControl> nav = BuildNav(model);
    DuiVBox* box = dynamic_cast<DuiVBox*>(nav.get());
    REQUIRE(box != nullptr);
    CHECK(box->GetBgColor() == kNavBgExpected);
}

TEST_CASE("NV2 选中的普通项：白卡片加描边，图标橙色，文字深色", "[groupmanage][nav]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    const NavPaintResult r = PaintNavItem(true, false, kGmIconMembers);
    REQUIRE(!r.m_pixels.empty());

    CHECK(CountExact(r, kNavCardWhite) >= kMinCardWhitePixels);
    CHECK(TopEdgeDarkest(r) <= kMaxBorderBrightness);

    const COLORREF icon = MostInked(r, kNavIconLeft, kNavIconRight, kNavCardWhite);
    INFO("icon=" << std::hex << (unsigned)icon);
    CHECK(ColorNear(icon, kNavAccentOrange, kIconColorTolerance));

    CHECK(TextColorOf(r, kNavCardWhite) == kNavActiveText);
}

TEST_CASE("NV3 未选中的普通项：没有卡片、没有橙色，图标与文字为灰色", "[groupmanage][nav]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());
    const NavPaintResult r = PaintNavItem(false, false, kGmIconMembers);
    REQUIRE(!r.m_pixels.empty());

    CHECK(CountExact(r, kNavCardWhite) == 0);
    std::string hit;
    const bool orange = AnyNearInColumns(r, kNavIconLeft, kNavIconRight,
                                         kNavAccentOrange, kNoOrangeTolerance, &hit);
    INFO("orange-like pixel: " << hit);
    CHECK_FALSE(orange);

    const COLORREF icon = MostInked(r, kNavIconLeft, kNavIconRight, kNavBgExpected);
    INFO("icon=" << std::hex << (unsigned)icon);
    CHECK(ColorNear(icon, kNavIdleIcon, kIconColorTolerance));

    CHECK(TextColorOf(r, kNavBgExpected) == kNavIdleText);
}

TEST_CASE("NV4 危险项无论选中与否图标与文字都是红色，选中时有白卡片", "[groupmanage][nav]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());

    SECTION("选中")
    {
        const NavPaintResult r = PaintNavItem(true, true, kGmIconDanger);
        REQUIRE(!r.m_pixels.empty());
        CHECK(CountExact(r, kNavCardWhite) >= kMinCardWhitePixels);
        std::string hit;
        const bool orange = AnyNearInColumns(r, kNavIconLeft, kNavIconRight,
                                             kNavAccentOrange, kNoOrangeTolerance, &hit);
        INFO("orange-like pixel: " << hit);
        CHECK_FALSE(orange);
        const COLORREF icon = MostInked(r, kNavIconLeft, kNavIconRight, kNavCardWhite);
        INFO("icon=" << std::hex << (unsigned)icon);
        CHECK(ColorNear(icon, kNavDangerRed, kIconColorTolerance));
        CHECK(TextColorOf(r, kNavCardWhite) == kNavDangerRed);
    }

    SECTION("未选中")
    {
        const NavPaintResult r = PaintNavItem(false, true, kGmIconDanger);
        REQUIRE(!r.m_pixels.empty());
        CHECK(CountExact(r, kNavCardWhite) == 0);
        std::string hit;
        const bool orange = AnyNearInColumns(r, kNavIconLeft, kNavIconRight,
                                             kNavAccentOrange, kNoOrangeTolerance, &hit);
        INFO("orange-like pixel: " << hit);
        CHECK_FALSE(orange);
        const COLORREF icon = MostInked(r, kNavIconLeft, kNavIconRight, kNavBgExpected);
        INFO("icon=" << std::hex << (unsigned)icon);
        CHECK(ColorNear(icon, kNavDangerRed, kIconColorTolerance));
        CHECK(TextColorOf(r, kNavBgExpected) == kNavDangerRed);
    }
}

// =====================================================================
// PG 组：内容区最大宽度与页面标题块
//
//   设计稿里基本信息、群权限、危险区域三页的内容限宽，其余页铺满（管理词库是群权限
//   的子页，按群权限处理）。限宽值按设计稿实测（约 600~650 设计稿像素）折算为 560。
//   各页标题与副标题紧挨着排成一块，块下与正文之间留出明显的距离。
//   本组固定在 96 DPI 下排版。
// =====================================================================

namespace {

// 限宽页的内容最大宽度与页面左右内边距（96 DPI 下的像素）。
const int kExpectedMaxContentWidth = 560;
const int kExpectedPagePadding     = 24;

// PG1 用的宽内容区与 PG2 用的窄内容区（96 DPI 下的像素）。
const int kWidePageWidth   = 1000;
const int kNarrowPageWidth = 600;

// 铺满页最右侧控件离右内边距允许差的像素（最后一列成员卡等可能有 1~2px 取整）。
const int kFullWidthSlackPx = 2;

// 标题与副标题之间最多的间距、副标题与下方正文之间最少的间距（96 DPI 下的像素）。
const int kMaxTitleSubtitleGap  = 6;
const int kMinSubtitleContentGap = 12;

// 限宽页与铺满页。
const int kWidthLimitedPages[] = { kPageBasicInfo, kPagePerms, kPageDanger, kPageSensitive };
const int kFullWidthPages[]    = { kPageMembers, kPageApproval, kPageMute, kPageAnnouncement };

// 有副标题的页面。
const int kPagesWithSubtitle[] = { kPageApproval, kPageMute, kPagePerms, kPageDanger,
                                   kPageAnnouncement, kPageSensitive };

// 按 96 DPI 构建某页并排版在指定宽度的内容区里。
std::unique_ptr<DuiControl> LayoutPageAt96(int pageId, int width)
{
    ScopedGmDpi guard(kDpi100);
    const GroupManageModel model = MakeFullMockModel();
    std::unique_ptr<DuiControl> page = BuildPage(pageId, model);
    if (page)
    {
        page->Layout(RECT{ 0, 0, width, kPageHeight });
    }
    return page;
}

// 子孙控件（不含 node 自身）里可见且有面积者的右边界最大值；没有子孙时返回 0。
// 不算 node 自身：页面根容器的矩形包含右内边距，拿它比较就查不出内容是否越界。
int MaxDescendantRight(const DuiControl& node)
{
    int best = 0;
    const std::vector<std::unique_ptr<DuiControl> >& kids = node.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const RECT rc = kids[i]->GetRect();
        if (!kids[i]->IsVisible() || rc.right <= rc.left || rc.bottom <= rc.top)
        {
            continue;
        }
        if (rc.right > best)
        {
            best = rc.right;
        }
        const int r = MaxDescendantRight(*kids[i]);
        if (r > best)
        {
            best = r;
        }
    }
    return best;
}

// node 自身或其子孙里是否有 target。
bool Contains(const DuiControl& node, const DuiControl* target)
{
    if (&node == target)
    {
        return true;
    }
    const std::vector<std::unique_ptr<DuiControl> >& kids = node.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (Contains(*kids[i], target))
        {
            return true;
        }
    }
    return false;
}

// 在根的直接子控件里，找出包含 target 的那一个的下标；找不到返回 -1。
int TopLevelIndexOf(const DuiControl& root, const DuiControl* target)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = root.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (Contains(*kids[i], target))
        {
            return (int)i;
        }
    }
    return -1;
}

} // anonymous namespace

TEST_CASE("PG1 内容区很宽时，限宽页不超出最大宽度，其余页铺满", "[groupmanage][page]")
{
    for (size_t i = 0; i < sizeof(kWidthLimitedPages) / sizeof(kWidthLimitedPages[0]); ++i)
    {
        std::unique_ptr<DuiControl> page = LayoutPageAt96(kWidthLimitedPages[i], kWidePageWidth);
        REQUIRE(page != nullptr);
        INFO("limited page=" << kWidthLimitedPages[i]);
        const int maxRight = MaxDescendantRight(*page);
        INFO("maxRight=" << maxRight);
        CHECK(maxRight <= kExpectedPagePadding + kExpectedMaxContentWidth);
        //内容确实排到了接近最大宽度的位置，而不是更窄
        CHECK(maxRight >= kExpectedPagePadding + kExpectedMaxContentWidth - kFullWidthSlackPx);
    }
    for (size_t i = 0; i < sizeof(kFullWidthPages) / sizeof(kFullWidthPages[0]); ++i)
    {
        std::unique_ptr<DuiControl> page = LayoutPageAt96(kFullWidthPages[i], kWidePageWidth);
        REQUIRE(page != nullptr);
        INFO("full page=" << kFullWidthPages[i]);
        const int maxRight = MaxDescendantRight(*page);
        INFO("maxRight=" << maxRight);
        CHECK(maxRight >= kWidePageWidth - kExpectedPagePadding - kFullWidthSlackPx);
    }
}

TEST_CASE("PG2 内容区比最大宽度窄时，限宽页照常铺满、不越界", "[groupmanage][page]")
{
    for (size_t i = 0; i < sizeof(kWidthLimitedPages) / sizeof(kWidthLimitedPages[0]); ++i)
    {
        std::unique_ptr<DuiControl> page = LayoutPageAt96(kWidthLimitedPages[i], kNarrowPageWidth);
        REQUIRE(page != nullptr);
        INFO("limited page=" << kWidthLimitedPages[i]);
        const int maxRight = MaxDescendantRight(*page);
        INFO("maxRight=" << maxRight);
        CHECK(maxRight <= kNarrowPageWidth - kExpectedPagePadding);
        CHECK(maxRight >= kNarrowPageWidth - kExpectedPagePadding - kFullWidthSlackPx);
    }
}

TEST_CASE("PG3 标题与副标题紧挨着，副标题与正文之间留出距离", "[groupmanage][page]")
{
    for (size_t i = 0; i < sizeof(kPagesWithSubtitle) / sizeof(kPagesWithSubtitle[0]); ++i)
    {
        std::unique_ptr<DuiControl> page = LayoutPageAt96(kPagesWithSubtitle[i], kWidePageWidth);
        REQUIRE(page != nullptr);
        INFO("page=" << kPagesWithSubtitle[i]);
        DuiControl* title    = page->FindCtrlById(kIdPageTitle);
        DuiControl* subtitle = page->FindCtrlById(kIdPageSubtitle);
        REQUIRE(title != nullptr);
        REQUIRE(subtitle != nullptr);

        const int titleGap = subtitle->GetRect().top - title->GetRect().bottom;
        INFO("titleGap=" << titleGap);
        CHECK(titleGap >= 0);
        CHECK(titleGap <= kMaxTitleSubtitleGap);

        //副标题所在的那一块之后的下一块，就是正文
        const int blockIndex = TopLevelIndexOf(*page, subtitle);
        REQUIRE(blockIndex >= 0);
        REQUIRE((size_t)(blockIndex + 1) < page->Children().size());
        const DuiControl& next = *page->Children()[(size_t)(blockIndex + 1)];
        const int contentGap = next.GetRect().top - subtitle->GetRect().bottom;
        INFO("contentGap=" << contentGap);
        CHECK(contentGap >= kMinSubtitleContentGap);
    }
}

// =====================================================================
// DZ 组：危险区域
//
//   取色来自 design/群管理6.png：转让群主是中性灰卡片、配普通按钮；移除全部成员与
//   解散群聊是淡红卡片、配危险按钮（淡红底、深红字、常规字重）。解散群聊的说明带上
//   群名，群名过长时截断，避免卡片放不下。
// =====================================================================

namespace {

// 设计稿取色。
const COLORREF kNeutralCardBgExpected     = RGB(0xF5, 0xF4, 0xF1);
const COLORREF kNeutralCardBorderExpected = RGB(0xE4, 0xE3, 0xE0);
const COLORREF kDangerCardBgExpected      = RGB(0xFE, 0xF8, 0xF8);
const COLORREF kDangerCardBorderExpected  = RGB(0xFC, 0xDD, 0xDD);
const COLORREF kDangerBtnFillExpected     = RGB(0xFE, 0xE4, 0xE2);
const COLORREF kDangerBtnTextExpected     = RGB(0xB4, 0x23, 0x18);

// DZ2 离屏绘制危险按钮的尺寸（96 DPI 下的像素）与所铺底色（淡红卡片底）。
const int kDangerBtnWidth  = 96;
const int kDangerBtnHeight = 36;

// 按钮填充色至少应占到的像素数：去掉圆角与描边后仍占绝大部分，取面积的一半作下限。
const int kMinBtnFillPixels = kDangerBtnWidth * kDangerBtnHeight / 2;

// 解散说明里群名最多保留的字数。
const int kDismissNameMaxChars = 16;

// 按按钮 id 找到它所在的卡片：按钮 → 卡片内的横排 → 卡片。
DuiVBox* CardOfButton(DuiControl& page, UINT btnId)
{
    DuiControl* btn = page.FindCtrlById(btnId);
    if (btn == NULL || btn->GetParent() == NULL)
    {
        return NULL;
    }
    return dynamic_cast<DuiVBox*>(btn->GetParent()->GetParent());
}

} // anonymous namespace

TEST_CASE("DZ1 转让群主是中性卡片，另两项是淡红卡片", "[groupmanage][danger]")
{
    std::unique_ptr<DuiControl> page = LayoutPageAt96(kPageDanger, kWidePageWidth);
    REQUIRE(page != nullptr);

    DuiVBox* transfer = CardOfButton(*page, kIdBtnTransfer);
    DuiVBox* removeAll = CardOfButton(*page, kIdBtnRemoveAll);
    DuiVBox* dismiss   = CardOfButton(*page, kIdBtnDismiss);
    REQUIRE(transfer != nullptr);
    REQUIRE(removeAll != nullptr);
    REQUIRE(dismiss != nullptr);

    CHECK(transfer->GetBgColor() == kNeutralCardBgExpected);
    CHECK(transfer->GetBorderColor() == kNeutralCardBorderExpected);
    CHECK(removeAll->GetBgColor() == kDangerCardBgExpected);
    CHECK(removeAll->GetBorderColor() == kDangerCardBorderExpected);
    CHECK(dismiss->GetBgColor() == kDangerCardBgExpected);
    CHECK(dismiss->GetBorderColor() == kDangerCardBorderExpected);
}

TEST_CASE("DZ2 危险按钮为淡红底、深红字、常规字重", "[groupmanage][danger]")
{
    ScopedGdiplus gdip;
    REQUIRE(gdip.Ok());

    NavPaintResult r;
    {
        ScopedGmDpi guard(kDpi100);
        GroupManageButton btn;
        btn.SetText(_T("解散群聊"));
        btn.SetButtonStyle(kGmBtnDanger);
        btn.SetRect(RECT{ 0, 0, kDangerBtnWidth, kDangerBtnHeight });
        r = PaintToPixels(btn, kDangerBtnWidth, kDangerBtnHeight, kDangerCardBgExpected);
    }
    REQUIRE(!r.m_pixels.empty());

    CHECK(CountExact(r, kDangerBtnFillExpected) >= kMinBtnFillPixels);
    CHECK(MostInked(r, 0, kDangerBtnWidth, kDangerBtnFillExpected) == kDangerBtnTextExpected);
    CHECK(GroupManageButton::TextWeightForStyle(kGmBtnDanger) == kGmFontNormal);
}

TEST_CASE("DZ3 解散说明带群名，群名过长时截断", "[groupmanage][danger]")
{
    CHECK(BuildDismissDescription(_T("设计组·周会"))
          == CString(_T("解散后 设计组·周会 的聊天记录将全部删除，所有成员将被移出，此操作不可恢复")));

    //恰好 16 个字：原样保留
    const CString name16 = _T("一二三四五六七八九十一二三四五六");
    REQUIRE(name16.GetLength() == kDismissNameMaxChars);
    CHECK(BuildDismissDescription(name16).Find(name16) >= 0);

    //超过 16 个字：保留前 16 个字并加省略号
    const CString name20 = name16 + _T("七八九十");
    const CString desc = BuildDismissDescription(name20);
    CHECK(desc.Find(name16 + _T("…")) >= 0);
    CHECK(desc.Find(name20) < 0);
}

// =====================================================================
// FC 组：表单控件（输入框、搜索框、开关、下拉框）
//
//   取色来自 design/群管理4/5.png（群主视角）：输入框与下拉框描边为很浅的
//   #EDEDED，获得焦点时用品牌橙；开关打开为品牌橙、关闭为暖灰；下拉箭头为 #666666。
//   输入框与搜索框的描边色靠子类化 DuiEdit / DuiSearchBox 覆写 BorderColor 实现。
// =====================================================================

namespace {

// 设计稿取色。
const COLORREF kInputBorderNormal  = RGB(0xED, 0xED, 0xED);
const COLORREF kInputBorderHover   = RGB(0xD6, 0xD3, 0xCE);
const COLORREF kInputBorderFocused = RGB(0xFF, 0x7A, 0x45);
const COLORREF kSwitchOnExpected   = RGB(0xFF, 0x7A, 0x45);
const COLORREF kSwitchOffExpected  = RGB(0xD8, 0xD5, 0xD0);
const COLORREF kComboArrowExpected = RGB(0x66, 0x66, 0x66);

// 所有页面。
const int kAllPages[] = { kPageMembers, kPageApproval, kPageMute, kPageBasicInfo,
                          kPagePerms, kPageDanger, kPageSensitive, kPageAnnouncement };

// 一次遍历收集到的各类表单控件。
struct FormControls
{
    std::vector<DuiControl*>  m_inputs;     // 输入框 / 搜索框（不含下拉框内部自带的）
    std::vector<DuiSwitch*>   m_switches;   // 开关
    std::vector<DuiComboBox*> m_combos;     // 下拉框
};

/**
 *  遍历控件树收集表单控件。
 *    node：       当前节点；
 *    inCombo：    当前节点是否位于某个下拉框之内（下拉框内部自带的输入框不收集，
 *                 它的样式由下拉框负责）；
 *    out：        [出参] 收集结果。
 */
void CollectFormControls(DuiControl* node, bool inCombo, FormControls& out)
{
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(node);
    if (combo != NULL)
    {
        out.m_combos.push_back(combo);
        inCombo = true;
    }
    else if (dynamic_cast<DuiSwitch*>(node) != NULL)
    {
        out.m_switches.push_back(dynamic_cast<DuiSwitch*>(node));
    }
    else if (!inCombo && dynamic_cast<DuiEdit*>(node) != NULL)
    {
        out.m_inputs.push_back(node);
    }

    const std::vector<std::unique_ptr<DuiControl> >& kids = node->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        CollectFormControls(kids[i].get(), inCombo, out);
    }
}

// 构建全部页面并收集表单控件。trees 持有各页，调用方在用完 out 之前不得释放。
void CollectAllPages(std::vector<std::unique_ptr<DuiControl> >& trees, FormControls& out)
{
    for (size_t i = 0; i < sizeof(kAllPages) / sizeof(kAllPages[0]); ++i)
    {
        trees.push_back(LayoutPageAt96(kAllPages[i], kWidePageWidth));
        if (trees.back())
        {
            CollectFormControls(trees.back().get(), false, out);
        }
    }
}

} // anonymous namespace

TEST_CASE("FC1 输入框描边在各状态下取设计稿颜色", "[groupmanage][form]")
{
    //参数依次为：是否可用、是否有焦点、是否悬停
    CHECK(GmInputBorderColor(true, false, false) == kInputBorderNormal);
    CHECK(GmInputBorderColor(true, false, true) == kInputBorderHover);
    CHECK(GmInputBorderColor(true, true, false) == kInputBorderFocused);
    //有焦点时即便悬停也显示焦点色
    CHECK(GmInputBorderColor(true, true, true) == kInputBorderFocused);
    //禁用时一律常态色（底色另由 DuiEdit 置灰）
    CHECK(GmInputBorderColor(false, false, false) == kInputBorderNormal);
    CHECK(GmInputBorderColor(false, true, true) == kInputBorderNormal);
}

TEST_CASE("FC2 每一页的输入框与搜索框都换成了群管理窗的样式", "[groupmanage][form]")
{
    std::vector<std::unique_ptr<DuiControl> > trees;
    FormControls fc;
    CollectAllPages(trees, fc);

    //各页合计应有输入框：搜索框 1 + 基本信息 2 + 公告 2 + 管理词库 1
    const size_t kMinInputs = 6;
    REQUIRE(fc.m_inputs.size() >= kMinInputs);
    for (size_t i = 0; i < fc.m_inputs.size(); ++i)
    {
        INFO("input #" << i << " " << DescribeCtrl(*fc.m_inputs[i]));
        CHECK(IsGmStyledInput(fc.m_inputs[i]));
    }
}

TEST_CASE("FC3 开关打开为品牌橙、关闭为暖灰", "[groupmanage][form]")
{
    std::vector<std::unique_ptr<DuiControl> > trees;
    FormControls fc;
    CollectAllPages(trees, fc);

    //禁言页 1 个 + 群权限页 4 个
    const size_t kMinSwitches = 5;
    REQUIRE(fc.m_switches.size() >= kMinSwitches);
    for (size_t i = 0; i < fc.m_switches.size(); ++i)
    {
        INFO("switch #" << i);
        CHECK(fc.m_switches[i]->GetOnColor() == kSwitchOnExpected);
        CHECK(fc.m_switches[i]->GetOffColor() == kSwitchOffExpected);
    }
}

TEST_CASE("FC4 下拉框箭头为设计稿的灰色", "[groupmanage][form]")
{
    std::vector<std::unique_ptr<DuiControl> > trees;
    FormControls fc;
    CollectAllPages(trees, fc);

    //群权限页 2 个
    const size_t kMinCombos = 2;
    REQUIRE(fc.m_combos.size() >= kMinCombos);
    for (size_t i = 0; i < fc.m_combos.size(); ++i)
    {
        INFO("combo #" << i);
        CHECK(fc.m_combos[i]->GetArrowColor() == kComboArrowExpected);
    }
}

TEST_CASE("FC5 下拉框边框与输入框同色：常态浅灰，悬停 / 展开时品牌橙", "[groupmanage][form]")
{
    std::vector<std::unique_ptr<DuiControl> > trees;
    FormControls fc;
    CollectAllPages(trees, fc);

    const size_t kMinCombos = 2;
    REQUIRE(fc.m_combos.size() >= kMinCombos);
    for (size_t i = 0; i < fc.m_combos.size(); ++i)
    {
        INFO("combo #" << i);
        //与输入框同一套取色（GmInputBorderColor），下拉框的"激活"对应输入框的焦点
        CHECK(fc.m_combos[i]->GetBorderNormalColor() == kInputBorderNormal);
        CHECK(fc.m_combos[i]->GetBorderActiveColor() == kInputBorderFocused);
    }
}

// =====================================================================
// BI 组：基本信息页的表单行
//
//   按 design/群管理4.png：群名称、群号、群公告、群简介四行，左侧标签（可带一行
//   说明）与右侧控件上下居中；每行下面一条 1px 分隔线，行与线紧挨着。
//   此前标签列被拉满整行高度，文字从顶部开始排，看起来是顶对齐。
// =====================================================================

namespace {

// 基本信息页的表单行数（群主视角）。
const size_t kBasicFormRowCount = 4;

// 标签块与右侧控件竖直中心允许的偏差（像素）。
const int kMaxCenterOffsetPx = 1;

// 矩形的竖直中心（乘 2 免去取整）。
int DoubleCenterY(const RECT& rc)
{
    return rc.top + rc.bottom;
}

// 取基本信息页里的表单列表容器。page 须已排版。
DuiControl* BasicFormList(DuiControl& page)
{
    return page.FindCtrlById(kIdBasicFormList);
}

} // anonymous namespace

TEST_CASE("BI1 每一行的标签块与右侧控件上下居中对齐", "[groupmanage][basicinfo]")
{
    std::unique_ptr<DuiControl> page = LayoutPageAt96(kPageBasicInfo, kWidePageWidth);
    REQUIRE(page != nullptr);
    DuiControl* list = BasicFormList(*page);
    REQUIRE(list != nullptr);

    size_t rows = 0;
    const std::vector<std::unique_ptr<DuiControl> >& kids = list->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (dynamic_cast<DuiSeparator*>(kids[i].get()) != NULL)
        {
            continue;
        }
        ++rows;
        const std::vector<std::unique_ptr<DuiControl> >& cells = kids[i]->Children();
        REQUIRE(cells.size() >= 2);
        const int labelCenter2   = DoubleCenterY(cells[0]->GetRect());
        const int controlCenter2 = DoubleCenterY(cells[1]->GetRect());
        INFO("row #" << i << " label2=" << labelCenter2 << " control2=" << controlCenter2);
        CHECK(std::abs(labelCenter2 - controlCenter2) <= kMaxCenterOffsetPx * 2);
        //标签块不再被拉满整行
        CHECK(RectHeight(cells[0]->GetRect()) < RectHeight(kids[i]->GetRect()));
    }
    CHECK(rows == kBasicFormRowCount);
}

TEST_CASE("BI2 每一行下面紧跟一条 1px 分隔线，行与线之间没有空隙", "[groupmanage][basicinfo]")
{
    std::unique_ptr<DuiControl> page = LayoutPageAt96(kPageBasicInfo, kWidePageWidth);
    REQUIRE(page != nullptr);
    DuiControl* list = BasicFormList(*page);
    REQUIRE(list != nullptr);

    const std::vector<std::unique_ptr<DuiControl> >& kids = list->Children();
    REQUIRE(kids.size() == kBasicFormRowCount * 2);
    for (size_t i = 0; i + 1 < kids.size(); i += 2)
    {
        INFO("row #" << i);
        CHECK(dynamic_cast<DuiSeparator*>(kids[i].get()) == NULL);
        DuiSeparator* sep = dynamic_cast<DuiSeparator*>(kids[i + 1].get());
        REQUIRE(sep != NULL);
        CHECK(RectHeight(sep->GetRect()) == 1);
        CHECK(sep->GetRect().top == kids[i]->GetRect().bottom);
        if (i + 2 < kids.size())
        {
            CHECK(kids[i + 2]->GetRect().top == sep->GetRect().bottom);
        }
    }
}

TEST_CASE("BI3 群名称输入框提示留空时按成员自动生成", "[groupmanage][basicinfo][autoname]")
{
    std::unique_ptr<DuiControl> page = LayoutPageAt96(kPageBasicInfo, kWidePageWidth);
    REQUIRE(page != nullptr);
    DuiEdit* nameEdit = dynamic_cast<DuiEdit*>(page->FindCtrlById(kIdEditGroupName));
    REQUIRE(nameEdit != NULL);
    CHECK(nameEdit->GetPlaceholder() == CString(_T("留空则按成员自动生成")));
}

// =====================================================================
// LL 组：数据尚未到达 / 加载失败 / 已加载三种情形下的界面
//
//   群管理窗打开时模型只含群号、群名、本人角色（ApplyGroupContext 写入），成员与
//   加群申请要等应答到达。这一组钉住：数据没到之前界面上不出现任何成员卡、申请卡、
//   人数与计数角标，只显示加载提示；加载失败显示失败提示；到达后按真实内容显示。
//   提示行用 kIdLoadPending / kIdLoadFailed 区分，不比对文字。
// =====================================================================

namespace {

// 本组用例使用的群ID。
const int64_t kLoadCtxGroupId = 268435456LL;

// 协议侧的「群主」角色值（0 成员 / 1 管理员 / 2 群主）。
const int kNetRoleOwnerValue = 2;

/**
 *  造一个"刚开窗"的模型：新建后只写入群上下文，成员与申请都未加载。
 *  @return 群主视角的模型，群名为「群聊」。
 */
GroupManageModel MakeJustOpenedModel()
{
    GroupManageModel model;
    ApplyGroupContext(kLoadCtxGroupId, _T("群聊"), kNetRoleOwnerValue, model);
    return model;
}

/**
 *  往模型里追加一名成员。
 *    model：[出参] 目标模型；name：昵称；role：角色；muted：是否处于禁言中。
 */
void AddLoadTestMember(GroupManageModel& model, LPCTSTR name, GroupRole role, bool muted)
{
    GroupMember m;
    m.name = name;
    m.avatarText = CString(name).Left(1);
    m.role = role;
    m.userid = 100001 + (int64_t)model.m_members.size();
    m.muted = muted;
    model.m_members.push_back(m);

    if (muted)
    {
        MutedMember mm;
        mm.name = name;
        mm.avatarText = m.avatarText;
        mm.userid = m.userid;
        mm.permanent = true;
        model.m_muted.push_back(mm);
    }
}

/**
 *  往模型里追加一条加群申请。
 *    model：[出参] 目标模型；name：申请人昵称。
 */
void AddLoadTestRequest(GroupManageModel& model, LPCTSTR name)
{
    JoinRequest r;
    r.name = name;
    r.avatarText = CString(name).Left(1);
    r.requestid = 55 + (int64_t)model.m_requests.size();
    r.userid = 200001 + (int64_t)model.m_requests.size();
    model.m_requests.push_back(r);
}

/**
 *  按 96 DPI 构建并排版某一页。
 *    pageId：页面 id；model：数据来源。
 *  @return 排好版的页面，所有权归调用方。
 */
std::unique_ptr<DuiControl> LayoutLoadPageAt96(int pageId, const GroupManageModel& model)
{
    ScopedGmDpi guard(kDpi100);
    std::unique_ptr<DuiControl> page = BuildPage(pageId, model);
    if (page)
    {
        page->Layout(RECT{ 0, 0, kPageWidth, kPageHeight });
    }
    return page;
}

/**
 *  按 96 DPI 构建并排版顶栏。
 *    model：数据来源。
 *  @return 排好版的顶栏，所有权归调用方。
 */
std::unique_ptr<DuiControl> LayoutLoadHeaderAt96(const GroupManageModel& model)
{
    ScopedGmDpi guard(kDpi100);
    std::unique_ptr<DuiControl> header = BuildHeader(model);
    if (header)
    {
        header->Layout(RECT{ 0, 0, kHeaderWidth, kHeaderHeight });
    }
    return header;
}

// 统计页面里找得到的成员卡张数（按成员卡 id 段逐个查找）。
int CountMemberCards(DuiControl& page)
{
    int count = 0;
    for (int id = kIdMemberCardBase; id <= kIdMemberCardMax; ++id)
    {
        if (page.FindCtrlById(id) != nullptr)
        {
            ++count;
        }
    }
    return count;
}

// 读取导航里某一项的角标计数；找不到该导航项时返回 -1。
int NavBadgeOf(DuiControl& nav, int pageId)
{
    GroupManageNavItem* item = dynamic_cast<GroupManageNavItem*>(nav.FindCtrlById(pageId));
    if (item == nullptr)
    {
        return -1;
    }
    return item->GetBadgeCount();
}

} // anonymous namespace

TEST_CASE("LL1 成员未加载时顶栏不显示人数，群名与角色标签照常显示",
          "[groupmanage][layout][loadstate]")
{
    const GroupManageModel model = MakeJustOpenedModel();
    std::unique_ptr<DuiControl> header = LayoutLoadHeaderAt96(model);
    REQUIRE(header != nullptr);

    DuiControl* name = header->FindCtrlById(kIdHeaderName);
    DuiControl* role = header->FindCtrlById(kIdHeaderRole);
    REQUIRE(name != nullptr);
    REQUIRE(role != nullptr);
    CHECK(RectWidth(name->GetRect()) > 0);
    CHECK(RectWidth(role->GetRect()) > 0);

    //不能出现"· 0 名成员"
    CHECK(header->FindCtrlById(kIdHeaderMeta) == nullptr);
}

TEST_CASE("LL2 成员加载完成后顶栏显示人数，排在角色标签右侧",
          "[groupmanage][layout][loadstate]")
{
    GroupManageModel model = MakeJustOpenedModel();
    AddLoadTestMember(model, _T("韩晓燕"), kRoleOwner, false);
    model.m_membersState = kLoadDone;

    std::unique_ptr<DuiControl> header = LayoutLoadHeaderAt96(model);
    REQUIRE(header != nullptr);
    DuiControl* role = header->FindCtrlById(kIdHeaderRole);
    DuiControl* meta = header->FindCtrlById(kIdHeaderMeta);
    REQUIRE(role != nullptr);
    REQUIRE(meta != nullptr);
    CHECK(RectWidth(meta->GetRect()) > 0);
    CHECK(meta->GetRect().left >= role->GetRect().right);
}

TEST_CASE("LL3 刚开窗时导航各项都不显示计数角标", "[groupmanage][layout][loadstate]")
{
    std::unique_ptr<DuiControl> nav;
    {
        ScopedGmDpi guard(kDpi100);
        nav = BuildNav(MakeJustOpenedModel());
    }
    REQUIRE(nav != nullptr);

    //群主视角下四项都在导航里，计数都应为 0（不显示角标）
    CHECK(NavBadgeOf(*nav, kPageMembers) == 0);
    CHECK(NavBadgeOf(*nav, kPageApproval) == 0);
    CHECK(NavBadgeOf(*nav, kPageMute) == 0);
    CHECK(NavBadgeOf(*nav, kPageAnnouncement) == 0);

    //对照：示例数据下角标确实有值，说明上面的 0 不是因为读不到计数
    GroupManageModel mock;
    FillMockData(mock);
    std::unique_ptr<DuiControl> mockNav;
    {
        ScopedGmDpi guard(kDpi100);
        mockNav = BuildNav(mock);
    }
    REQUIRE(mockNav != nullptr);
    CHECK(NavBadgeOf(*mockNav, kPageMembers) == mock.MemberCount());
    CHECK(NavBadgeOf(*mockNav, kPageMembers) > 0);
}

TEST_CASE("LL4 成员未加载时成员页只有加载提示，没有成员卡", "[groupmanage][layout][loadstate]")
{
    const GroupManageModel model = MakeJustOpenedModel();
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMembers, model);
    REQUIRE(page != nullptr);

    CHECK(page->FindCtrlById(kIdLoadPending) != nullptr);
    CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
    CHECK(CountMemberCards(*page) == 0);

    //搜索框与邀请按钮照常存在，数据到达时页面顶部不会跳动
    CHECK(page->FindCtrlById(kIdSearchMembers) != nullptr);
    CHECK(page->FindCtrlById(kIdBtnInvite) != nullptr);
}

TEST_CASE("LL5 成员加载失败时成员页只有失败提示", "[groupmanage][layout][loadstate]")
{
    GroupManageModel model = MakeJustOpenedModel();
    model.m_membersState = kLoadFailed;
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMembers, model);
    REQUIRE(page != nullptr);

    CHECK(page->FindCtrlById(kIdLoadFailed) != nullptr);
    CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
    CHECK(CountMemberCards(*page) == 0);
}

TEST_CASE("LL6 成员已加载时成员卡数与成员数一致，不显示任何提示",
          "[groupmanage][layout][loadstate]")
{
    GroupManageModel model = MakeJustOpenedModel();
    AddLoadTestMember(model, _T("韩晓燕"), kRoleOwner, false);
    AddLoadTestMember(model, _T("谢知行"), kRoleAdmin, false);
    AddLoadTestMember(model, _T("苏沐辰"), kRoleMember, false);
    model.m_membersState = kLoadDone;

    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMembers, model);
    REQUIRE(page != nullptr);
    CHECK(CountMemberCards(*page) == 3);
    CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
    CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
}

TEST_CASE("LL7 加群审批页按申请的加载状态显示提示或申请卡", "[groupmanage][layout][loadstate]")
{
    //未加载：只有加载提示，不显示"待处理 N 条"副标题
    {
        const GroupManageModel model = MakeJustOpenedModel();
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageApproval, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdLoadPending) != nullptr);
        CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
        CHECK(page->FindCtrlById(kIdPageSubtitle) == nullptr);
        CHECK(page->FindCtrlById(kIdApprovalAcceptBase) == nullptr);
    }

    //加载失败：只有失败提示
    {
        GroupManageModel model = MakeJustOpenedModel();
        model.m_requestsState = kLoadFailed;
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageApproval, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdLoadFailed) != nullptr);
        CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
        CHECK(page->FindCtrlById(kIdPageSubtitle) == nullptr);
        CHECK(page->FindCtrlById(kIdApprovalAcceptBase) == nullptr);
    }

    //已加载 2 条：两张申请卡，有副标题，没有任何提示
    {
        GroupManageModel model = MakeJustOpenedModel();
        AddLoadTestRequest(model, _T("王语嫣"));
        AddLoadTestRequest(model, _T("陈致远"));
        model.m_requestsState = kLoadDone;
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageApproval, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdApprovalAcceptBase) != nullptr);
        CHECK(page->FindCtrlById(kIdApprovalAcceptBase + 1) != nullptr);
        CHECK(page->FindCtrlById(kIdApprovalAcceptBase + 2) == nullptr);
        CHECK(page->FindCtrlById(kIdPageSubtitle) != nullptr);
        CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
        CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
    }
}

TEST_CASE("LL8 禁言管理页按成员的加载状态显示提示或禁言卡", "[groupmanage][layout][loadstate]")
{
    //未加载：只有加载提示，不显示"禁言中 N 人"副标题
    {
        const GroupManageModel model = MakeJustOpenedModel();
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMute, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdLoadPending) != nullptr);
        CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
        CHECK(page->FindCtrlById(kIdPageSubtitle) == nullptr);
        CHECK(page->FindCtrlById(kIdMuteReleaseBase) == nullptr);
    }

    //加载失败：只有失败提示
    {
        GroupManageModel model = MakeJustOpenedModel();
        model.m_membersState = kLoadFailed;
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMute, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdLoadFailed) != nullptr);
        CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
        CHECK(page->FindCtrlById(kIdPageSubtitle) == nullptr);
        CHECK(page->FindCtrlById(kIdMuteReleaseBase) == nullptr);
    }

    //已加载、其中 1 人被禁言：一张禁言卡，有副标题，没有任何提示
    {
        GroupManageModel model = MakeJustOpenedModel();
        AddLoadTestMember(model, _T("韩晓燕"), kRoleOwner, false);
        AddLoadTestMember(model, _T("广告账号"), kRoleMember, true);
        model.m_membersState = kLoadDone;
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMute, model);
        REQUIRE(page != nullptr);
        CHECK(page->FindCtrlById(kIdMuteReleaseBase) != nullptr);
        CHECK(page->FindCtrlById(kIdMuteReleaseBase + 1) == nullptr);
        CHECK(page->FindCtrlById(kIdPageSubtitle) != nullptr);
        CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
        CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
    }
}

TEST_CASE("LL9 三个页面的提示行排版后都占到真实尺寸", "[groupmanage][layout][loadstate]")
{
    //防止布局 hint 的主轴写反，把提示行压成零尺寸（参见文件头的说明与 GL2）
    const int pageIds[] = { kPageMembers, kPageApproval, kPageMute };
    const LoadState states[] = { kLoadPending, kLoadFailed };
    const UINT statusIds[] = { kIdLoadPending, kIdLoadFailed };
    for (size_t p = 0; p < sizeof(pageIds) / sizeof(pageIds[0]); ++p)
    {
        for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); ++s)
        {
            GroupManageModel model = MakeJustOpenedModel();
            model.m_membersState = states[s];
            model.m_requestsState = states[s];
            std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(pageIds[p], model);
            REQUIRE(page != nullptr);

            INFO("pageId=" << pageIds[p] << ", state=" << states[s]);
            DuiControl* status = page->FindCtrlById(statusIds[s]);
            REQUIRE(status != nullptr);
            CHECK(RectWidth(status->GetRect()) > 0);
            CHECK(RectHeight(status->GetRect()) > 0);
        }
    }
}

// ---------------------------------------------------------------------
// LL10 ~ LL13：群资料（群简介、欢迎语、七项群权限、全员禁言）的加载状态
//
//   这些设置都来自群资料应答。应答到达之前或拉取失败时，模型里只有构造时的默认值，
//   界面若照常显示表单，默认值看上去与真实设置无异；管理员据此拨动开关还会把错误的值
//   写回服务端（例如打开「入群欢迎语」会顺带发出默认欢迎语，覆盖真实内容）。
// ---------------------------------------------------------------------

namespace {

// 群权限页上全部表单控件的 ctrl id：两个下拉框与四个开关。
const UINT kPermsFormIds[] =
{
    kIdComboJoinMethod, kIdComboAtAllScope,
    kIdSwitchHistory, kIdSwitchInvite, kIdSwitchWelcome, kIdSwitchSensitive,
};

const size_t kPermsFormIdCount = sizeof(kPermsFormIds) / sizeof(kPermsFormIds[0]);

/**
 *  统计页面里找得到的群权限表单控件个数。
 *    page：群权限页（借用）。
 *  @return 0 ~ kPermsFormIdCount。
 */
size_t CountPermsFormControls(DuiControl& page)
{
    size_t count = 0;
    for (size_t i = 0; i < kPermsFormIdCount; ++i)
    {
        if (page.FindCtrlById(kPermsFormIds[i]) != nullptr)
        {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("LL10 群资料未加载时，基本信息页只有加载提示，没有群名称与群简介输入框",
          "[groupmanage][layout][loadstate]")
{
    const GroupManageModel model = MakeJustOpenedModel();
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageBasicInfo, model);
    REQUIRE(page != nullptr);

    DuiControl* status = page->FindCtrlById(kIdLoadPending);
    REQUIRE(status != nullptr);
    CHECK(RectWidth(status->GetRect()) > 0);
    CHECK(RectHeight(status->GetRect()) > 0);
    CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
    CHECK(page->FindCtrlById(kIdEditGroupName) == nullptr);
    CHECK(page->FindCtrlById(kIdEditIntro) == nullptr);
}

TEST_CASE("LL11 群资料加载失败时，基本信息页只有失败提示", "[groupmanage][layout][loadstate]")
{
    GroupManageModel model = MakeJustOpenedModel();
    model.m_groupInfoState = kLoadFailed;
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageBasicInfo, model);
    REQUIRE(page != nullptr);

    DuiControl* status = page->FindCtrlById(kIdLoadFailed);
    REQUIRE(status != nullptr);
    CHECK(RectWidth(status->GetRect()) > 0);
    CHECK(RectHeight(status->GetRect()) > 0);
    CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
    CHECK(page->FindCtrlById(kIdEditGroupName) == nullptr);
    CHECK(page->FindCtrlById(kIdEditIntro) == nullptr);

    //群资料已加载时表单照常显示、没有提示行（对照组）
    model.m_groupInfoState = kLoadDone;
    std::unique_ptr<DuiControl> loaded = LayoutLoadPageAt96(kPageBasicInfo, model);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->FindCtrlById(kIdEditGroupName) != nullptr);
    CHECK(loaded->FindCtrlById(kIdEditIntro) != nullptr);
    CHECK(loaded->FindCtrlById(kIdLoadPending) == nullptr);
    CHECK(loaded->FindCtrlById(kIdLoadFailed) == nullptr);
}

TEST_CASE("LL12 群权限页按群资料的加载状态显示提示或表单", "[groupmanage][layout][loadstate]")
{
    //未加载：只有加载提示，两个下拉框与四个开关一个都没有
    {
        const GroupManageModel model = MakeJustOpenedModel();
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPagePerms, model);
        REQUIRE(page != nullptr);
        DuiControl* status = page->FindCtrlById(kIdLoadPending);
        REQUIRE(status != nullptr);
        CHECK(RectHeight(status->GetRect()) > 0);
        CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
        CHECK(CountPermsFormControls(*page) == 0);
    }

    //加载失败：只有失败提示
    {
        GroupManageModel model = MakeJustOpenedModel();
        model.m_groupInfoState = kLoadFailed;
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPagePerms, model);
        REQUIRE(page != nullptr);
        DuiControl* status = page->FindCtrlById(kIdLoadFailed);
        REQUIRE(status != nullptr);
        CHECK(RectHeight(status->GetRect()) > 0);
        CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
        CHECK(CountPermsFormControls(*page) == 0);
    }

    //已加载：表单齐全，没有任何提示
    {
        GroupManageModel model = MakeJustOpenedModel();
        model.m_groupInfoState = kLoadDone;
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPagePerms, model);
        REQUIRE(page != nullptr);
        CHECK(CountPermsFormControls(*page) == kPermsFormIdCount);
        CHECK(page->FindCtrlById(kIdLoadPending) == nullptr);
        CHECK(page->FindCtrlById(kIdLoadFailed) == nullptr);
    }
}

TEST_CASE("LL13 禁言管理页的「全员禁言」开关只在群资料加载后出现，且反映真实设置",
          "[groupmanage][layout][loadstate]")
{
    //成员已加载、群资料未加载或失败：禁言卡照常显示，但没有「全员禁言」开关
    const LoadState notLoaded[] = { kLoadPending, kLoadFailed };
    for (size_t i = 0; i < sizeof(notLoaded) / sizeof(notLoaded[0]); ++i)
    {
        GroupManageModel model = MakeJustOpenedModel();
        AddLoadTestMember(model, _T("广告账号"), kRoleMember, true);
        model.m_membersState = kLoadDone;
        model.m_groupInfoState = notLoaded[i];
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMute, model);
        REQUIRE(page != nullptr);
        INFO("groupInfoState=" << notLoaded[i]);
        CHECK(page->FindCtrlById(kIdSwitchMuteAll) == nullptr);
        CHECK(page->FindCtrlById(kIdMuteReleaseBase) != nullptr);
    }

    //群资料已加载：开关出现，勾选状态与群资料里的全员禁言一致
    const bool muteAllValues[] = { false, true };
    for (size_t i = 0; i < sizeof(muteAllValues) / sizeof(muteAllValues[0]); ++i)
    {
        GroupManageModel model = MakeJustOpenedModel();
        model.m_membersState = kLoadDone;
        model.m_groupInfoState = kLoadDone;
        model.m_muteAll = muteAllValues[i];
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMute, model);
        REQUIRE(page != nullptr);
        DuiSwitch* sw = dynamic_cast<DuiSwitch*>(page->FindCtrlById(kIdSwitchMuteAll));
        INFO("muteAll=" << muteAllValues[i]);
        REQUIRE(sw != nullptr);
        CHECK(sw->IsChecked() == muteAllValues[i]);
    }
}

// =====================================================================
// JM 组：群权限页「加群方式」下拉框与服务端加群方式取值的对应关系
//
//   下拉框的选中下标在收发两处都直接当作服务端的 group_join_mode 使用（群资料应答
//   原样写入 joinMethodIndex，修改时原样作为 joinmode 发出），中间没有换算。因此下拉框
//   第 N 项的文字必须正好是取值 N 的含义。2026-10-01 修复前前两项是反的：库里存
//   「需审批」(1) 的群显示成「允许任何人加入」，选「需要验证消息」实际发出的是自由加入 (0)。
// =====================================================================

namespace {

// 本组用例使用的群ID。
const int64_t kJoinModeGroupId = 268435464LL;

// 加群方式的取值与下拉框里应显示的文字。
struct JoinModeLabel
{
    int     m_mode;    // 服务端取值，见 group_join_mode
    LPCTSTR m_label;   // 下拉框里该项的文字
};

const JoinModeLabel kJoinModeLabels[] =
{
    { group_join_free,     _T("允许任何人加入") },
    { group_join_approval, _T("需要验证消息") },
    { group_join_forbid,   _T("禁止任何人加入") },
};

const size_t kJoinModeLabelCount = sizeof(kJoinModeLabels) / sizeof(kJoinModeLabels[0]);

/**
 *  造一个群资料已到达的模型：先写入群上下文，再按群资料应答写入加群方式。
 *    joinMode：群资料应答里的加群方式，取值见 group_join_mode。
 *  @return 群主视角的模型。
 */
GroupManageModel MakeModelWithJoinMode(int joinMode)
{
    GroupManageModel model;
    ApplyGroupContext(kJoinModeGroupId, _T("群聊"), kNetRoleOwnerValue, model);

    net::GroupInfoResult info;
    info.m_code = 0;
    info.m_groupid = kJoinModeGroupId;
    info.m_name = "group";
    info.m_myrole = kNetRoleOwnerValue;
    info.m_joinmode = joinMode;
    ApplyGroupInfo(info, model);
    return model;
}

/**
 *  在排好版的群权限页里找到「加群方式」下拉框。
 *    page：群权限页（借用）。
 *  @return 下拉框指针（借用），找不到返回 NULL。
 */
DuiComboBox* FindJoinCombo(DuiControl& page)
{
    return dynamic_cast<DuiComboBox*>(page.FindCtrlById(kIdComboJoinMethod));
}

} // namespace

TEST_CASE("JM1 「加群方式」下拉框第 N 项的文字正好是服务端取值 N 的含义", "[groupmanage][layout][joinmode]")
{
    const GroupManageModel model = MakeModelWithJoinMode(group_join_approval);
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPagePerms, model);
    REQUIRE(page != nullptr);
    DuiComboBox* combo = FindJoinCombo(*page);
    REQUIRE(combo != nullptr);

    REQUIRE(combo->GetCount() == (int)kJoinModeLabelCount);
    for (size_t i = 0; i < kJoinModeLabelCount; ++i)
    {
        INFO("mode = " << kJoinModeLabels[i].m_mode);
        CHECK(combo->GetItemText(kJoinModeLabels[i].m_mode) == CString(kJoinModeLabels[i].m_label));
    }
}

TEST_CASE("JM2 群资料里的加群方式原样显示为下拉框里含义相同的那一项", "[groupmanage][layout][joinmode]")
{
    for (size_t i = 0; i < kJoinModeLabelCount; ++i)
    {
        const GroupManageModel model = MakeModelWithJoinMode(kJoinModeLabels[i].m_mode);
        std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPagePerms, model);
        REQUIRE(page != nullptr);
        DuiComboBox* combo = FindJoinCombo(*page);
        REQUIRE(combo != nullptr);

        INFO("mode = " << kJoinModeLabels[i].m_mode);
        CHECK(combo->GetCurSel() == kJoinModeLabels[i].m_mode);
        CHECK(combo->GetText() == CString(kJoinModeLabels[i].m_label));
    }
}

TEST_CASE("JM3 群权限的加群方式默认值为「需要验证消息」，即服务端的需审批", "[groupmanage][joinmode]")
{
    const GroupPermissions perms;
    CHECK(perms.joinMethodIndex == group_join_approval);
}

// =====================================================================
// 成员管理页搜索（GMS）：按名字（含拼音）与职务过滤成员卡
// =====================================================================

namespace {

// 造一份示例数据模型（7 名成员，加载状态为完成），并设好成员页的搜索关键词。
GroupManageModel MakeMockModelWithKeyword(LPCTSTR keyword)
{
    GroupManageModel model;
    FillMockData(model);
    model.m_memberKeyword = keyword;
    return model;
}

// 按名字取成员在模型里的下标；找不到返回 -1。
int IndexOfMember(const GroupManageModel& model, LPCTSTR name)
{
    for (size_t i = 0; i < model.m_members.size(); ++i)
    {
        if (model.m_members[i].name == name)
        {
            return (int)i;
        }
    }
    return -1;
}

// 页面里有没有模型下标为 memberIndex 的成员卡。
bool HasMemberCard(DuiControl& page, int memberIndex)
{
    return memberIndex >= 0 && page.FindCtrlById(kIdMemberCardBase + memberIndex) != nullptr;
}

} // anonymous namespace

TEST_CASE("GMS1 搜索匹配：名字子串、拼音首字母、全拼、职务子串，英文不区分大小写", "[groupmanage][membersearch]")
{
    GroupManageModel model;
    FillMockData(model);
    const int su   = IndexOfMember(model, _T("苏沐辰"));   //职务：资深设计师
    const int song = IndexOfMember(model, _T("宋微之"));   //职务：后端工程师
    REQUIRE(su >= 0);
    REQUIRE(song >= 0);
    const GroupMember& mSu   = model.m_members[(size_t)su];
    const GroupMember& mSong = model.m_members[(size_t)song];

    CHECK(MemberMatchesKeyword(mSu, _T("苏沐")));        //名字子串
    CHECK(MemberMatchesKeyword(mSu, _T("smc")));         //拼音首字母
    CHECK(MemberMatchesKeyword(mSu, _T("SMC")));         //英文不区分大小写
    CHECK(MemberMatchesKeyword(mSu, _T("sumuchen")));    //全拼
    CHECK(MemberMatchesKeyword(mSong, _T("工程师")));    //职务子串
    CHECK_FALSE(MemberMatchesKeyword(mSu, _T("工程师")));
    CHECK_FALSE(MemberMatchesKeyword(mSu, _T("不存在的人")));
    CHECK(MemberMatchesKeyword(mSu, _T("")));            //空关键词视为全部命中
}

TEST_CASE("GMS2 输入「设计」只剩名字或职务含该词的成员，没有命中的分组整组不显示", "[groupmanage][membersearch]")
{
    const GroupManageModel model = MakeMockModelWithKeyword(_T("设计"));
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMembers, model);
    REQUIRE(page != nullptr);

    //林知夏（资深设计师，群主）、设计组组长（名字，管理员）、苏沐辰（资深设计师，成员）
    CHECK(CountMemberCards(*page) == 3);
    CHECK(HasMemberCard(*page, IndexOfMember(model, _T("林知夏"))));
    CHECK(HasMemberCard(*page, IndexOfMember(model, _T("设计组组长"))));
    CHECK(HasMemberCard(*page, IndexOfMember(model, _T("苏沐辰"))));
    CHECK_FALSE(HasMemberCard(*page, IndexOfMember(model, _T("谢知行"))));
    CHECK(model.CountByRoleMatching(kRoleOwner) == 1);
    CHECK(model.CountByRoleMatching(kRoleAdmin) == 1);
    CHECK(model.CountByRoleMatching(kRoleMember) == 1);
    CHECK(page->FindCtrlById(kIdMemberSearchEmpty) == nullptr);

    //输入「总监」时只有管理员谢知行命中，群主与普通成员两个分组都不显示
    const GroupManageModel only = MakeMockModelWithKeyword(_T("总监"));
    CHECK(only.CountByRoleMatching(kRoleOwner) == 0);
    CHECK(only.CountByRoleMatching(kRoleAdmin) == 1);
    CHECK(only.CountByRoleMatching(kRoleMember) == 0);
}

TEST_CASE("GMS3 一个也没命中时显示提示行，搜索框与邀请按钮仍在", "[groupmanage][membersearch]")
{
    const GroupManageModel model = MakeMockModelWithKeyword(_T("不存在的人"));
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMembers, model);
    REQUIRE(page != nullptr);

    CHECK(CountMemberCards(*page) == 0);
    CHECK(page->FindCtrlById(kIdMemberSearchEmpty) != nullptr);
    CHECK(page->FindCtrlById(kIdSearchMembers) != nullptr);
    CHECK(page->FindCtrlById(kIdBtnInvite) != nullptr);
}

TEST_CASE("GMS4 清空关键词后全部成员恢复，不显示提示行", "[groupmanage][membersearch]")
{
    const GroupManageModel model = MakeMockModelWithKeyword(_T(""));
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMembers, model);
    REQUIRE(page != nullptr);

    CHECK(CountMemberCards(*page) == (int)model.m_members.size());
    CHECK(page->FindCtrlById(kIdMemberSearchEmpty) == nullptr);
}

TEST_CASE("GMS5 过滤后的成员卡编号仍是基址加模型下标，右键菜单认的成员不会错位", "[groupmanage][membersearch]")
{
    const GroupManageModel model = MakeMockModelWithKeyword(_T("工程师"));
    std::unique_ptr<DuiControl> page = LayoutLoadPageAt96(kPageMembers, model);
    REQUIRE(page != nullptr);

    //宋微之（后端工程师）、唐砚秋（前端工程师）
    const int song = IndexOfMember(model, _T("宋微之"));
    const int tang = IndexOfMember(model, _T("唐砚秋"));
    CHECK(CountMemberCards(*page) == 2);
    CHECK(HasMemberCard(*page, song));
    CHECK(HasMemberCard(*page, tang));
    //编号对应的确实是这两位：按编号反查出来的下标与模型一致
    DuiControl* card = page->FindCtrlById(kIdMemberCardBase + song);
    REQUIRE(card != nullptr);
    CHECK(MemberIndexFromCardId(card->GetCtrlId()) == song);
}

TEST_CASE("GMS6 关键词首尾的空白先去掉再匹配，只有空白时视为全部命中", "[groupmanage][membersearch]")
{
    GroupManageModel model;
    FillMockData(model);
    const GroupMember& su = model.m_members[(size_t)IndexOfMember(model, _T("苏沐辰"))];
    CHECK(MemberMatchesKeyword(su, _T("  苏沐  ")));
    CHECK(MemberMatchesKeyword(su, _T("   ")));

    model.m_memberKeyword = _T("   ");
    CHECK(model.CountByRoleMatching(kRoleMember) == model.CountByRole(kRoleMember));
}

TEST_CASE("GMS7 关键词变化时只重建搜索栏以下的部分：搜索框还是原来那个控件，新卡片已排过版", "[groupmanage][membersearch]")
{
    GroupManageModel model = MakeMockModelWithKeyword(_T(""));
    std::unique_ptr<DuiControl> built = LayoutLoadPageAt96(kPageMembers, model);
    DuiVBox* page = dynamic_cast<DuiVBox*>(built.get());
    REQUIRE(page != nullptr);
    DuiControl* searchBefore = page->FindCtrlById(kIdSearchMembers);
    REQUIRE(searchBefore != nullptr);
    CHECK(CountMemberCards(*page) == (int)model.m_members.size());

    ScopedGmDpi guard(kDpi100);
    model.m_memberKeyword = _T("设计");
    RefilterMembersPage(page, model);

    CHECK(page->FindCtrlById(kIdSearchMembers) == searchBefore);
    CHECK(CountMemberCards(*page) == 3);
    //页面矩形没变，新卡片也必须排过版：只增删子控件、不强制重排时它们的矩形是空的，
    //界面上表现为搜索后列表一片空白（实机发现）
    DuiControl* card = page->FindCtrlById(kIdMemberCardBase + IndexOfMember(model, _T("苏沐辰")));
    REQUIRE(card != nullptr);
    const RECT rc = card->GetRect();
    CHECK(rc.right - rc.left > 0);
    CHECK(rc.bottom - rc.top > 0);

    //一个也没命中时，提示行同样排过版
    model.m_memberKeyword = _T("不存在的人");
    RefilterMembersPage(page, model);
    DuiControl* empty = page->FindCtrlById(kIdMemberSearchEmpty);
    REQUIRE(empty != nullptr);
    CHECK(empty->GetRect().bottom - empty->GetRect().top > 0);
}
