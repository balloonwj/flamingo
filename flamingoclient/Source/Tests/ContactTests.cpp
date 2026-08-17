/**
 *  ContactTests.cpp
 *
 *  联系人资料对话框的契约测试：
 *    C1-C8  BuildContactBody 产出的控件树符合"控件点名契约" —— 头像 /
 *           头部资料块 / 标签流式容器 / 备注名当前值 / 备注名文本框 /
 *           分组下拉 / 三个开关 / 两个底部按钮都能按既定数字 id 找到、
 *           类型正确、id 互不重复。
 *    C9-C15 ContactModel 数据模型：默认值、标签增删、空标签被拒、分组
 *           选择、三个开关读写、Switch / Group 越界被忽略。
 *    C32-C34 标签区行数与滚动范围（SyncContactTagArea）：按对话框真实宽度
 *           布局过、停在窗口默认尺寸上、以及完全没布局过这三种状态下，算出来
 *           的行数与滚动视口内容总高度必须一致。
 *    C16-C27 部门路径整宽区块：路径分段（SplitDeptPathSegments）、按层级
 *           贪心装箱折行（PackDeptPathLines）、区块与客户区高度
 *           （ContactDeptBlockHeight / ContactBodyHeight），以及区块在
 *           控件树里的契约（按 id 找得到、无部门时不可见且不占高度）。
 *    C35-C36 快捷操作行：容器与四个按钮的 id / 类型 / 文字 / 归属，以及
 *           客户区高度确实把这一行算了进去。
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 / 操作模型 + 断言。
 *
 *  本文件不注册进 FlamingoUnit.vcxproj —— 由主 agent 集成时一并加入。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonContact/ContactLayout.h"
#include "../BalloonContact/ContactModel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"   // 快捷操作行四个按钮的公共基类
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"  // DuiScrollView（标签区滚动视口）

using namespace balloonwjui;
using namespace ballooncontact;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectContactIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectContactIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateContactId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

// 统计某个 ctrlId 在 ids 里出现了几次。用来断言"这个号在控件树里存在，且只有一个"。
int CountContactId(const std::vector<UINT>& ids, UINT id)
{
    return (int)std::count(ids.begin(), ids.end(), id);
}

// ---- 客户区高度用的基准常量（逻辑像素）-------------------------------

// 2026-08-17 加入快捷操作行<u>之前</u>，"没有部门路径、标签只占一行"时的客户区高度。
// 574 是内嵌 XML 各区块高度自上而下的累加值，出处见 ContactLayout.cpp 里
// kContactBodyBaseHeight 的注释。
const int kTestBodyHeightBeforeQuickRow = 574;

// 快捷操作行给客户区带来的高度增量 = 行高 + 它<u>下方</u>那一个 spacer。上方那个
// spacer（头部与分隔线之间的间距）原先就在，不计入增量。
const int kTestQuickRowExtra = kContactQuickRowHeight + kContactQuickRowGap;

// 当前"没有部门路径、标签只占一行"时的客户区高度。
const int kTestContactBaseBodyHeight =
    kTestBodyHeightBeforeQuickRow + kTestQuickRowExtra;

// ---- C32-C34 标签区行数 / 滚动范围用的测试夹具 -------------------------

// 标签胶囊的行高与行距（逻辑像素），与内嵌 XML 里 contact-tagscroll 的
// row-height="24" / gap="6" 一致。用来把"几行"换算成滚动视口的内容总高度。
const int kTestTagRowHeight = 24;
const int kTestTagRowGap    = 6;

// 100% 缩放下对话框实际的客户区宽度（逻辑像素）。DuiFrameWindow 在
// WM_NCCALCSIZE 里把整个窗口矩形当客户区，客户区因此比 ResizeClient 指定的
// kContactBodyWidth（420）多出左右两条缩放边框。438 这个数取自实测：
// Bin\Logs 里 "tag area rebuilt" 一行打出的标签流式容器矩形是 20,222-410，
// 加上滚动条那 8 像素即滚动视口宽 398 = 438 - 左右内边距 40。
const int kTestRealClientWidth = 438;

// 窗口 Create(CWindow::rcDefault) 之后、ApplyBodyHeight 里 ResizeClient 之前，
// 客户区的宽度（逻辑像素）。1422 同样取自实测：那一次布局里标签区滚动视口
// 量到的宽度是 1382 = 1422 - 左右内边距 40。
const int kTestDefaultSizedClientWidth = 1422;

// 测试用的标签条数。取 kContactTagMaxCount 的实际上限 10 —— 与真实场景一致。
const int kTestTagCount = 10;

// 造一棵客户区控件树，往标签流式容器里塞 tagCount 枚标签胶囊 + 末尾的
// 「+ 添加标签」按钮（构成与 CBalloonContactWindow::RebuildTags 一致），
// 然后按 clientWidth 布局一次 —— 模拟窗口那一次 LayoutSkeleton。
//
//   tagCount：要塞几枚标签胶囊。
//   clientWidth：布局用的客户区宽度（逻辑像素）。
//   返回：客户区根控件（所有权归调用方）；构建失败返回空指针。
std::unique_ptr<DuiControl> BuildLaidOutTagBody(int tagCount, int clientWidth)
{
    std::unique_ptr<DuiControl> root = BuildContactBody();
    if (!root)
    {
        return root;
    }
    DuiControl* flow = root->FindCtrlById((UINT)kIdTagFlow);
    if (flow == nullptr)
    {
        return root;
    }

    // 标签文字统一取 6 个汉字 + 序号，让这批标签在对话框真实宽度下必然排到
    // 三行以上（能验出滚动范围），而在窗口默认尺寸那种超宽布局下又刚好排得下
    // 一行（正是本组用例要复现的错误场景）。
    for (int i = 0; i < tagCount; ++i)
    {
        CString text;
        text.Format(_T("标签示例内容%d"), i);
        flow->AddChild(CreateTagChip(text, (UINT)(kIdTagChipBase + i)));
    }
    flow->AddChild(CreateAddTagButton());

    // 客户区高度取"没有部门路径、标签一行"的基准高度 —— 标签区的行数是按宽度
    // 数出来的，高度多少不影响判定。
    RECT rcClient;
    rcClient.left   = 0;
    rcClient.top    = 0;
    rcClient.right  = clientWidth;
    rcClient.bottom = ContactBodyHeight(0, 1);
    root->ForceLayout(rcClient);
    return root;
}

// 取标签区滚动视口当前的内容总高度（逻辑像素）；取不到滚动视口返回 -1。
int TagScrollContentHeight(DuiControl* root)
{
    DuiScrollView* scroll = dynamic_cast<DuiScrollView*>(
        root->FindCtrlById((UINT)kIdTagScroll));
    if (scroll == nullptr)
    {
        return -1;
    }
    return scroll->GetContentHeight();
}

// 取标签区滚动视口当前布局出来的宽度（逻辑像素）；取不到滚动视口返回 -1。
int TagScrollWidth(DuiControl* root)
{
    DuiControl* scroll = root->FindCtrlById((UINT)kIdTagScroll);
    if (scroll == nullptr)
    {
        return -1;
    }
    const RECT rc = scroll->GetRect();
    return rc.right - rc.left;
}

} // anonymous namespace

// =====================================================================
// C1-C8 联系人资料对话框布局契约
// =====================================================================

TEST_CASE("C1 BuildContactBody 返回非空控件树", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("C2 联系人资料对话框含头像且类型正确", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiAvatar*>(root->FindCtrlById(kIdAvatar)) != nullptr);
}

TEST_CASE("C3 联系人资料对话框含头部资料块与标签流式容器", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    // 头部资料块与标签流式容器是文件局部类型，只能验"按 id 找得到"。
    CHECK(root->FindCtrlById(kIdHeaderInfo) != nullptr);
    CHECK(root->FindCtrlById(kIdTagFlow)    != nullptr);
}

TEST_CASE("C4 联系人资料对话框含备注名当前值与行内文本框", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdRemarkValue) != nullptr);
    CHECK(dynamic_cast<DuiEditHost*>(
              root->FindCtrlById(kIdRemarkEdit)) != nullptr);
}

TEST_CASE("C5 联系人资料对话框含分组下拉框", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiComboBox*>(
              root->FindCtrlById(kIdGroupCombo)) != nullptr);
}

TEST_CASE("C6 联系人资料对话框含三个 DuiSwitch 开关", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiSwitch*>(
              root->FindCtrlById(kIdSwitchMute)) != nullptr);
    CHECK(dynamic_cast<DuiSwitch*>(
              root->FindCtrlById(kIdSwitchFavorite)) != nullptr);
    CHECK(dynamic_cast<DuiSwitch*>(
              root->FindCtrlById(kIdSwitchStarred)) != nullptr);

    // 三个开关的编号必须正是约定的 1030 / 1033 / 1034 —— 主面板那一侧按这几个号
    // 对接，改错了不报错也不崩溃，只表现为界面上拨了没反应。
    CHECK((UINT)kIdSwitchMute     == 1030u);
    CHECK((UINT)kIdSwitchFavorite == 1033u);
    CHECK((UINT)kIdSwitchStarred  == 1034u);

    // 1031 曾是「置顶聊天」开关（置顶是会话的属性、不是人的属性，会话列表右键菜单
    // 里已有该项），1032 曾是「加入黑名单」开关（企业版取消好友关系后移除），两行
    // 都已整行去掉。用字面量断言而不是枚举 —— 枚举项已经删了，这里要防的是"日后
    // 有人复用这两个编号"。
    CHECK(root->FindCtrlById(1031) == nullptr);
    CHECK(root->FindCtrlById(1032) == nullptr);
}

TEST_CASE("C7 联系人资料对话框含关闭 / 发消息底部按钮", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdBtnClose) != nullptr);
    CHECK(root->FindCtrlById(kIdBtnSend)  != nullptr);
}

TEST_CASE("C8 联系人资料对话框控件 id 互不重复", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectContactIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateContactId(ids));

    // 逐个点名：本对话框对外承诺的每个号都要在控件树里、且只出现一次。上面那条
    // "无重复"只保证已存在的号不打架，保证不了某个号<u>压根没建出来</u>（XML 里
    // 写漏一行同样是全绿）。
    const UINT kDeclaredIds[] =
    {
        (UINT)kIdAvatar,        (UINT)kIdHeaderInfo,   (UINT)kIdDeptPath,
        (UINT)kIdCopyToast,     (UINT)kIdTagFlow,      (UINT)kIdTagBlock,
        (UINT)kIdTagScroll,     (UINT)kIdRemarkValue,  (UINT)kIdRemarkEditBtn,
        (UINT)kIdRemarkEdit,    (UINT)kIdGroupCombo,
        (UINT)kIdSwitchMute,    (UINT)kIdSwitchFavorite, (UINT)kIdSwitchStarred,
        (UINT)kIdBtnClose,      (UINT)kIdBtnSend,
        (UINT)kIdBtnVoice,      (UINT)kIdBtnVideo,
        (UINT)kIdBtnHistory,    (UINT)kIdBtnLocate,    (UINT)kIdQuickRow,
    };
    const int kDeclaredCount = sizeof(kDeclaredIds) / sizeof(kDeclaredIds[0]);
    for (int i = 0; i < kDeclaredCount; ++i)
    {
        CHECK(CountContactId(ids, kDeclaredIds[i]) == 1);
    }
}

// =====================================================================
// C9-C15 ContactModel 数据模型契约
// =====================================================================

TEST_CASE("C9 ContactModel 默认 mock 数据正确", "[contact][model]")
{
    ContactModel model;
    CHECK(model.Name()    == CString(_T("江屿白")));
    CHECK(model.Remark()  == CString(_T("设计组组长")));
    CHECK(model.Account() == CString(_T("10923847")));
    CHECK(model.Region()  == CString(_T("北京")));
    CHECK(model.Status()  == kContactStatusAway);
    // 头像占位字母 = 真名首字。
    CHECK(model.AvatarLetter() == CString(_T("江")));
}

TEST_CASE("C10 ContactModel 默认有两个标签「朋友」「设计」", "[contact][model]")
{
    ContactModel model;
    REQUIRE(model.TagCount() == 2);
    CHECK(model.Tag(0) == CString(_T("朋友")));
    CHECK(model.Tag(1) == CString(_T("设计")));
}

TEST_CASE("C11 ContactModel 标签可追加", "[contact][model]")
{
    ContactModel model;
    const int before = model.TagCount();

    CHECK(model.AddTag(_T("同学")) == true);
    REQUIRE(model.TagCount() == before + 1);
    CHECK(model.Tag(before) == CString(_T("同学")));
}

TEST_CASE("C12 ContactModel 空白标签被拒绝", "[contact][model]")
{
    ContactModel model;
    const int before = model.TagCount();

    CHECK(model.AddTag(_T("")) == false);       // 空字符串
    CHECK(model.AddTag(_T("   ")) == false);    // 纯空白
    CHECK(model.AddTag(NULL) == false);         // 空指针
    CHECK(model.TagCount() == before);          // 数量不变
}

TEST_CASE("C13 ContactModel 标签可按下标删除、越界忽略", "[contact][model]")
{
    ContactModel model;
    REQUIRE(model.TagCount() == 2);

    // 删除第 0 个「朋友」—— 后续标签整体前移。
    model.RemoveTag(0);
    REQUIRE(model.TagCount() == 1);
    CHECK(model.Tag(0) == CString(_T("设计")));

    // 越界下标删除应被忽略。
    model.RemoveTag(99);
    model.RemoveTag(-1);
    CHECK(model.TagCount() == 1);
}

TEST_CASE("C14 ContactModel 分组默认选中「产品部 · 设计组」、可切换", "[contact][model]")
{
    ContactModel model;
    REQUIRE(model.GroupCount() >= 2);

    // 默认选中下标 1，对应「产品部 · 设计组」。
    CHECK(model.SelectedGroup() == 1);
    CHECK(model.Group(1) == CString(_T("产品部 · 设计组")));

    // 切换到下标 0。
    model.SetSelectedGroup(0);
    CHECK(model.SelectedGroup() == 0);

    // 越界忽略。
    model.SetSelectedGroup(999);
    model.SetSelectedGroup(-1);
    CHECK(model.SelectedGroup() == 0);
}

TEST_CASE("C15 ContactModel 三个开关默认全关、可读写、越界忽略", "[contact][model]")
{
    ContactModel model;

    // 默认全关。
    CHECK(model.Switch(kContactSwitchMute)     == false);
    CHECK(model.Switch(kContactSwitchFavorite) == false);
    CHECK(model.Switch(kContactSwitchStarred)  == false);

    // 逐个打开，互不串扰。
    model.SetSwitch(kContactSwitchFavorite, true);
    CHECK(model.Switch(kContactSwitchMute)     == false);
    CHECK(model.Switch(kContactSwitchFavorite) == true);
    CHECK(model.Switch(kContactSwitchStarred)  == false);

    model.SetSwitch(kContactSwitchStarred, true);
    CHECK(model.Switch(kContactSwitchMute)     == false);
    CHECK(model.Switch(kContactSwitchFavorite) == true);
    CHECK(model.Switch(kContactSwitchStarred)  == true);

    // 越界 kind：写忽略、读返回 false。用 kContactSwitchCount 表达"第一个越界下标"，
    // 开关增减时本用例不必改数字；这一条同时锁住"三个开关的下标是 0/1/2 连续编号、
    // kContactSwitchCount 确实等于 3"。
    CHECK(kContactSwitchCount == 3);
    CHECK((int)kContactSwitchStarred == kContactSwitchCount - 1);
    model.SetSwitch(kContactSwitchCount, true);
    CHECK(model.Switch(kContactSwitchCount) == false);
    model.SetSwitch(-1, true);
    CHECK(model.Switch(-1) == false);
    model.SetSwitch(99, true);
    CHECK(model.Switch(99) == false);
}

// =====================================================================
// C16-C20 部门路径分段（SplitDeptPathSegments）
// =====================================================================

TEST_CASE("C16 部门路径为空时切不出任何段", "[contact][dept]")
{
    std::vector<CString> segments;

    SplitDeptPathSegments(NULL, segments);
    CHECK(segments.empty());

    SplitDeptPathSegments(_T(""), segments);
    CHECK(segments.empty());

    // 只有分隔符与空白，同样切不出有效段。
    SplitDeptPathSegments(_T("  /  / "), segments);
    CHECK(segments.empty());
}

TEST_CASE("C17 单级部门路径切成一段", "[contact][dept]")
{
    std::vector<CString> segments;
    SplitDeptPathSegments(_T("研发部"), segments);

    REQUIRE(segments.size() == 1);
    CHECK(segments[0] == CString(_T("研发部")));
}

TEST_CASE("C18 多级部门路径按层级切段并去掉分隔符两侧空格", "[contact][dept]")
{
    std::vector<CString> segments;
    SplitDeptPathSegments(_T("明远科技有限公司 / 技术中心 / 研发部 / 前端组"),
                          segments);

    REQUIRE(segments.size() == 4);
    CHECK(segments[0] == CString(_T("明远科技有限公司")));
    CHECK(segments[1] == CString(_T("技术中心")));
    CHECK(segments[2] == CString(_T("研发部")));
    CHECK(segments[3] == CString(_T("前端组")));
}

TEST_CASE("C19 连续分隔符与首尾空白不产生空段", "[contact][dept]")
{
    std::vector<CString> segments;

    // 连续分隔符。
    SplitDeptPathSegments(_T("总部 // 技术中心 /  / 研发部"), segments);
    REQUIRE(segments.size() == 3);
    CHECK(segments[0] == CString(_T("总部")));
    CHECK(segments[1] == CString(_T("技术中心")));
    CHECK(segments[2] == CString(_T("研发部")));

    // 首尾多余的分隔符与空白。
    SplitDeptPathSegments(_T("   / 总部 / 研发部 /   "), segments);
    REQUIRE(segments.size() == 2);
    CHECK(segments[0] == CString(_T("总部")));
    CHECK(segments[1] == CString(_T("研发部")));
}

TEST_CASE("C20 32 级超长部门路径逐级切开不丢段", "[contact][dept]")
{
    // 32 是拼接侧（CBalloonMainPanel::ComputeContactDeptInfo）沿 parentdeptid
    // 上溯时的层级上限，这里按上限造一条最长的路径。
    const int kLevels = 32;
    CString path;
    for (int i = 0; i < kLevels; ++i)
    {
        CString level;
        level.Format(_T("第%d级部门"), i);
        if (i > 0)
        {
            path += _T(" / ");
        }
        path += level;
    }

    std::vector<CString> segments;
    SplitDeptPathSegments(path, segments);

    REQUIRE((int)segments.size() == kLevels);
    CHECK(segments[0] == CString(_T("第0级部门")));
    CHECK(segments[kLevels - 1] == CString(_T("第31级部门")));
}

// =====================================================================
// C21-C25 部门路径折行装箱（PackDeptPathLines）
// =====================================================================
//
// 这些用例只喂"已经量好的宽度"，不碰 GDI：装箱本身是纯整数运算，字体与
// 系统 DPI 不该影响它的判定。

TEST_CASE("C21 没有段时折出 0 行", "[contact][dept]")
{
    std::vector<int> widths;
    std::vector<int> lineEndSeg;

    CHECK(PackDeptPathLines(widths, 10, 380, 3, lineEndSeg) == 0);
    CHECK(lineEndSeg.empty());
}

TEST_CASE("C22 一行放得下时不折行", "[contact][dept]")
{
    std::vector<int> widths;
    widths.push_back(100);
    widths.push_back(80);
    widths.push_back(60);
    std::vector<int> lineEndSeg;

    // 100 + 10 + 80 + 10 + 60 = 260 <= 380。
    REQUIRE(PackDeptPathLines(widths, 10, 380, 3, lineEndSeg) == 1);
    REQUIRE(lineEndSeg.size() == 1);
    CHECK(lineEndSeg[0] == 3);   // 一行覆盖全部 3 段
}

TEST_CASE("C23 行宽正好等于可用宽度时仍留在同一行", "[contact][dept]")
{
    std::vector<int> widths;
    widths.push_back(100);
    widths.push_back(100);
    std::vector<int> lineEndSeg;

    // 100 + 10 + 100 = 210，与可用宽度相等 —— 边界取 <=，不该换行。
    REQUIRE(PackDeptPathLines(widths, 10, 210, 3, lineEndSeg) == 1);
    CHECK(lineEndSeg[0] == 2);
}

TEST_CASE("C24 只超出 1 像素就换行", "[contact][dept]")
{
    std::vector<int> widths;
    widths.push_back(100);
    widths.push_back(100);
    std::vector<int> lineEndSeg;

    // 210 > 209，第二段挪到下一行。
    REQUIRE(PackDeptPathLines(widths, 10, 209, 3, lineEndSeg) == 2);
    REQUIRE(lineEndSeg.size() == 2);
    CHECK(lineEndSeg[0] == 1);
    CHECK(lineEndSeg[1] == 2);
}

TEST_CASE("C25 单段自身超宽时仍占一行、不丢段", "[contact][dept]")
{
    std::vector<int> widths;
    widths.push_back(500);   // 比整行还宽
    widths.push_back(60);
    std::vector<int> lineEndSeg;

    // 行首段无条件放进本行（绘制层再按字符切开它），第二段另起一行。
    REQUIRE(PackDeptPathLines(widths, 10, 380, 3, lineEndSeg) == 2);
    CHECK(lineEndSeg[0] == 1);
    CHECK(lineEndSeg[1] == 2);
}

TEST_CASE("C26 行数达到上限后剩余各段并入最后一行", "[contact][dept]")
{
    std::vector<int> widths;
    for (int i = 0; i < 5; ++i)
    {
        widths.push_back(100);
    }
    std::vector<int> lineEndSeg;

    // 可用宽度只放得下每行两段，但最多允许两行 —— 后 3 段全部并进第 2 行，
    // 由绘制层做中段省略。
    REQUIRE(PackDeptPathLines(widths, 10, 210, 2, lineEndSeg) == 2);
    REQUIRE(lineEndSeg.size() == 2);
    CHECK(lineEndSeg[0] == 2);
    CHECK(lineEndSeg[1] == 5);
}

TEST_CASE("C27 可用宽度为 0 时每行一段且不死循环", "[contact][dept]")
{
    std::vector<int> widths;
    widths.push_back(30);
    widths.push_back(30);
    widths.push_back(30);
    std::vector<int> lineEndSeg;

    // 什么都放不下，但行首段必须无条件放进去，否则会一直换行排不完。
    REQUIRE(PackDeptPathLines(widths, 10, 0, 3, lineEndSeg) == 3);
    CHECK(lineEndSeg[0] == 1);
    CHECK(lineEndSeg[1] == 2);
    CHECK(lineEndSeg[2] == 3);

    // maxLines 小于 1 按 1 行处理，同样要收得住。
    REQUIRE(PackDeptPathLines(widths, 10, 0, 0, lineEndSeg) == 1);
    CHECK(lineEndSeg[0] == 3);
}

// =====================================================================
// C28-C29 区块高度与客户区高度
// =====================================================================

TEST_CASE("C28 部门路径区块高度随行数递增、0 行不占高", "[contact][dept]")
{
    // 没有部门 → 区块完全不占位。
    CHECK(ContactDeptBlockHeight(0) == 0);
    CHECK(ContactDeptBlockHeight(-1) == 0);

    // 有内容时每多一行就高一个行高，且各行高度差相等。
    const int one   = ContactDeptBlockHeight(1);
    const int two   = ContactDeptBlockHeight(2);
    const int three = ContactDeptBlockHeight(3);
    CHECK(one > 0);
    CHECK(two - one == three - two);
    CHECK(two > one);
}

TEST_CASE("C29 无部门、标签一行时客户区高度等于各区块高度的直接累加", "[contact][dept]")
{
    // 这一条锁的是"没有部门、标签只有一行的联系人，窗口高度就是内嵌 XML 各区块
    // 高度的累加值"，不多一像素也不少一像素。
    CHECK(ContactBodyHeight(0, 1) == kTestContactBaseBodyHeight);

    // 有部门时高度 = 基准高度 + 部门区块高度。
    CHECK(ContactBodyHeight(1, 1) == kTestContactBaseBodyHeight + ContactDeptBlockHeight(1));
    CHECK(ContactBodyHeight(3, 1) == kTestContactBaseBodyHeight + ContactDeptBlockHeight(3));
}

TEST_CASE("C29b 标签区高度随行数递增、封顶三行", "[contact][tag]")
{
    // 一行就是内嵌 XML 里写死的那个高度。
    const int one   = ContactTagBlockHeight(1);
    const int two   = ContactTagBlockHeight(2);
    const int three = ContactTagBlockHeight(3);
    CHECK(one == 76);
    // 每多一行增高"行高 24 + 行距 6"。
    CHECK(two - one == 30);
    CHECK(three - two == 30);

    // 0 行与负数都按一行处理 —— 一个标签都没有时也要留出位置给「+ 添加标签」。
    CHECK(ContactTagBlockHeight(0) == one);
    CHECK(ContactTagBlockHeight(-1) == one);

    // 超过上限的行数不再增高，多出来的行交给滚动视口。
    CHECK(ContactTagBlockHeight(kContactTagMaxRows + 1) == three);
    CHECK(ContactTagBlockHeight(100) == three);
}

TEST_CASE("C29c 部门行数与标签行数一起决定客户区高度", "[contact][tag]")
{
    // 两块的增量必须同时体现在同一个高度值里 —— 各算各的会互相覆盖。
    const int tagExtra2 = ContactTagBlockHeight(2) - ContactTagBlockHeight(1);
    const int tagExtra3 = ContactTagBlockHeight(3) - ContactTagBlockHeight(1);
    CHECK(ContactBodyHeight(0, 2) == kTestContactBaseBodyHeight + tagExtra2);
    CHECK(ContactBodyHeight(0, 3) == kTestContactBaseBodyHeight + tagExtra3);
    CHECK(ContactBodyHeight(2, 3)
          == kTestContactBaseBodyHeight + ContactDeptBlockHeight(2) + tagExtra3);
}

// =====================================================================
// C30-C31 部门路径区块在控件树里的契约
// =====================================================================

TEST_CASE("C30 控件树含部门路径区块且没有部门时不占位", "[contact][dept]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    // 区块本身是布局文件局部类型，只能验"按 id 找得到"。
    DuiControl* block = root->FindCtrlById(kIdDeptPath);
    REQUIRE(block != nullptr);

    // 没有部门：折出 0 行、区块不可见、区块高度为 0。
    const int lineCount = ConfigureContactDeptPath(root.get(), _T(""));
    CHECK(lineCount == 0);
    CHECK(block->IsVisible() == false);
    CHECK(ContactDeptBlockHeight(lineCount) == 0);
    CHECK(ContactBodyHeight(lineCount, 1) == kTestContactBaseBodyHeight);
}

TEST_CASE("C31 有部门时区块可见且至少折出一行", "[contact][dept]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    DuiControl* block = root->FindCtrlById(kIdDeptPath);
    REQUIRE(block != nullptr);

    const int lineCount = ConfigureContactDeptPath(
        root.get(), _T("明远科技有限公司 / 技术中心 / 研发部 / 前端组"));
    CHECK(lineCount >= 1);
    CHECK(lineCount <= kContactDeptPathMaxLines);
    CHECK(block->IsVisible() == true);
    CHECK(ContactDeptBlockHeight(lineCount) > 0);

    // 再切回没有部门的联系人：区块要能收回去，不留残迹。
    const int emptyCount = ConfigureContactDeptPath(root.get(), NULL);
    CHECK(emptyCount == 0);
    CHECK(block->IsVisible() == false);
}

// =====================================================================
// C32-C34 标签区行数与滚动范围（SyncContactTagArea）
// =====================================================================
//
// 这几条用例针对的是"首次打开对话框时标签区行数算错"这类问题。窗口的创建
// 顺序是 Create(CWindow::rcDefault) → InitUi（构建控件树、同步标签区）→
// ApplyBodyHeight（ResizeClient 到真实宽度），所以界面第一次构建时控件树是
// 按窗口的<u>默认尺寸</u>布局过的，标签区量到的宽度远大于对话框真实宽度。
// 行数据此算成一行，滚动视口的内容总高度也就只有一行 —— 第二行往后的标签
// 既显示不出来、也滚不到。

TEST_CASE("C32 标签区在对话框真实宽度下排到三行以上并给出对应滚动范围",
          "[contact][tag]")
{
    // 基准用例：控件树已按对话框真实客户区宽度布局过，这是标签区应有的结果。
    std::unique_ptr<DuiControl> root =
        BuildLaidOutTagBody(kTestTagCount, kTestRealClientWidth);
    REQUIRE(root != nullptr);

    // 先确认夹具本身没走样：滚动视口拿到的正是"客户区宽度减左右内边距"。
    CHECK(TagScrollWidth(root.get())
          == kTestRealClientWidth - kContactBodyPaddingH * 2);

    const int rows = SyncContactTagArea(root.get());

    // 可见行数封顶在 kContactTagMaxRows，多出来的行交给滚动视口。
    CHECK(rows == kContactTagMaxRows);

    // 这批标签实际排了 4 行，故内容总高度要比三行的视口高 —— 有了这段高度差，
    // 滚动条才会出现、第四行才滚得到。
    const int contentHeight = TagScrollContentHeight(root.get());
    const int threeRowHeight = kContactTagMaxRows * kTestTagRowHeight
                             + (kContactTagMaxRows - 1) * kTestTagRowGap;
    CHECK(contentHeight > threeRowHeight);
}

TEST_CASE("C33 标签区在窗口尚未调整到真实尺寸时仍按对话框宽度算行数",
          "[contact][tag]")
{
    // 与 C32 同一批标签，区别只在于控件树停在窗口默认尺寸（约 1422 宽）上 ——
    // 首次打开对话框时就是这个状态。
    std::unique_ptr<DuiControl> root =
        BuildLaidOutTagBody(kTestTagCount, kTestDefaultSizedClientWidth);
    REQUIRE(root != nullptr);

    // 量到的宽度确实是那个不可信的大值（复现前提）。
    CHECK(TagScrollWidth(root.get())
          == kTestDefaultSizedClientWidth - kContactBodyPaddingH * 2);

    const int rows = SyncContactTagArea(root.get());
    const int contentHeight = TagScrollContentHeight(root.get());

    // 与 C32 的基准逐项对齐：量到的宽度不可信时应当退回按对话框宽度算，结果必须与
    // 已按真实宽度布局过的那棵树一致。
    //
    // 修复前这两条都不成立：量到 1382 像素照单全收，十枚标签被算成 1 行、滚动视口
    // 的内容总高度只有一行的 24 像素（实测值，见本次修复的记录）。
    std::unique_ptr<DuiControl> reference =
        BuildLaidOutTagBody(kTestTagCount, kTestRealClientWidth);
    REQUIRE(reference != nullptr);
    const int refRows = SyncContactTagArea(reference.get());
    const int refContentHeight = TagScrollContentHeight(reference.get());

    CHECK(rows == refRows);
    CHECK(contentHeight == refContentHeight);

    // 再把绝对值也钉住：可见行数封顶三行，内容总高度必须容得下实际排出来的四行。
    CHECK(rows == kContactTagMaxRows);
    CHECK(contentHeight > kContactTagMaxRows * kTestTagRowHeight
                        + (kContactTagMaxRows - 1) * kTestTagRowGap);
}

TEST_CASE("C34 标签区在控件树完全没布局过时也算得出行数", "[contact][tag]")
{
    // BuildContactBody 刚建出来的树所有控件矩形都是全零，量不到任何宽度 ——
    // 这条路径原本就有兜底，这里把它一并钉住，防止改判据时把它改丢。
    std::unique_ptr<DuiControl> root = BuildContactBody();
    REQUIRE(root != nullptr);
    DuiControl* flow = root->FindCtrlById((UINT)kIdTagFlow);
    REQUIRE(flow != nullptr);
    for (int i = 0; i < kTestTagCount; ++i)
    {
        CString text;
        text.Format(_T("标签示例内容%d"), i);
        flow->AddChild(CreateTagChip(text, (UINT)(kIdTagChipBase + i)));
    }
    flow->AddChild(CreateAddTagButton());

    CHECK(TagScrollWidth(root.get()) == 0);

    std::unique_ptr<DuiControl> reference =
        BuildLaidOutTagBody(kTestTagCount, kTestRealClientWidth);
    REQUIRE(reference != nullptr);

    CHECK(SyncContactTagArea(root.get()) == SyncContactTagArea(reference.get()));
    CHECK(TagScrollContentHeight(root.get())
          == TagScrollContentHeight(reference.get()));
}

// =====================================================================
// C35-C36 快捷操作行（语音通话 / 视频通话 / 历史消息 / 组织架构）
// =====================================================================

TEST_CASE("C35 联系人资料对话框含快捷操作行与四个按钮", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    DuiControl* quickRow = root->FindCtrlById(kIdQuickRow);
    REQUIRE(quickRow != nullptr);

    // 四个按钮的具体类型（ContactActionButton）是布局文件局部类型，测试里拿不到它的
    // 声明，只能验到它的公共基类 DuiButton；再各自核对文字，连带验证 XML 的 text
    // 属性没有写串行。
    const UINT kQuickIds[] =
    {
        (UINT)kIdBtnVoice, (UINT)kIdBtnVideo, (UINT)kIdBtnHistory, (UINT)kIdBtnLocate
    };
    const TCHAR* const kQuickTexts[] =
    {
        _T("语音通话"), _T("视频通话"), _T("历史消息"), _T("组织架构")
    };
    const int kQuickCount = sizeof(kQuickIds) / sizeof(kQuickIds[0]);

    for (int i = 0; i < kQuickCount; ++i)
    {
        DuiButton* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(kQuickIds[i]));
        REQUIRE(btn != nullptr);
        CHECK(btn->GetText() == CString(kQuickTexts[i]));
        // 四个按钮必须挂在快捷操作行这一个容器里 —— 散落在别处的话就不会等宽平分
        // 这一行，界面上会看出宽度不齐。
        CHECK(btn->GetParent() == quickRow);
    }

    // 快捷操作行只有这四个按钮，没有多也没有少。
    CHECK((int)quickRow->Children().size() == kQuickCount);

    // 编号必须正是约定的 1042-1046 —— 主面板那一侧按这几个号对接。
    CHECK((UINT)kIdBtnVoice   == 1042u);
    CHECK((UINT)kIdBtnVideo   == 1043u);
    CHECK((UINT)kIdBtnHistory == 1044u);
    CHECK((UINT)kIdBtnLocate  == 1045u);
    CHECK((UINT)kIdQuickRow   == 1046u);
}

TEST_CASE("C36 客户区高度已把快捷操作行算进去", "[contact][tag]")
{
    // 漏掉这一行的高度不会有任何报错，只表现为底部两个按钮被挤出窗口 —— 正是
    // 部门路径与标签区当初踩过的那个坑，这里把它一并钉住。
    CHECK(ContactBodyHeight(0, 1) - kTestBodyHeightBeforeQuickRow
          == kContactQuickRowHeight + kContactQuickRowGap);
    CHECK(ContactBodyHeight(0, 1) == kTestContactBaseBodyHeight);

    // 快捷操作行是固定高度的一行，部门路径与标签区的增量照旧各加各的，不受它影响。
    CHECK(ContactBodyHeight(2, 1) - ContactBodyHeight(0, 1)
          == ContactDeptBlockHeight(2));
    CHECK(ContactBodyHeight(0, 3) - ContactBodyHeight(0, 1)
          == ContactTagBlockHeight(3) - ContactTagBlockHeight(1));
}
