/**
 *  标注工具条效果预览图标的单元测试（标签 [shareicon]），ShareMenuIconTests.cpp
 *  背景：2026-10-05 联调反馈「各个绘图工具要有图标标识，不然有些无法想象出来绘制效果」。
 *  覆盖：
 *    - SSIC1 十三组下拉菜单里每个可点的项（含子菜单里的项）都有图标，图标里有看得见的像素；分隔线与提示行没有图标；
 *    - SSIC2 同一层菜单里任意两项的图标互不相同（看图标就能分辨效果）；
 *    - SSIC3 当前选中的项与未选中时的图标不同（DuiMenu 勾选与图标二选一，选中画进图标里）；
 *    - SSIC4 子菜单父项用子菜单里打勾那一项的图标；
 *    - SSIC5 分组按钮的图标：当前工具在组内时为当前工具，否则为代表项；颜色组没有（按钮前面是色块）；
 *    - SSIC6 没有图标的命令（撤销、退出等）返回 false 且不改动输出；
 *    - SSIC7 工具条上「绘图工具」按钮与四个样式按钮前面都有图标（颜色组为色块），换了当前工具后「绘图工具」按钮的
 *      图标与文字随之更换（2026-10-05 起九个工具组收进「绘图工具」）；
 *    - SSIC8 工具条可以收起和展开：收起后窗口变窄、分组按钮不在了、出现「展开工具条」，展开后恢复原来的宽度与按钮；
 *    - SSIC9 「绘图工具」菜单：九个工具组按工具条原来的先后各为一个子菜单，标题为组名，内容与原来这一组的下拉菜单相同；
 *    - SSIC10 工具条上不再有九个工具组按钮，只有一个「绘图工具」按钮，四个样式按钮仍在，整条宽度不超过 940 像素
 *             （2026-10-08 加了「选择」按钮后由 900 放宽）；
 *    - SSIC11 收起状态下关闭工具条，之后再更新状态、再显示：不访问已随窗口销毁的「展开工具条」按钮；
 *    - SSICY（隐藏用例，"[shareicon-snap]"）：全部图标放大排成一张图，存为 snapshots/annot_menu_icons.png，供人工核对。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string.h>
#include <string>
#include <vector>
#include <objidl.h>
#include <gdiplus.h>

#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../FlamingoMedia/Share/ShareAnnotationMenuIcons.h"
#include "../../FlamingoMedia/Share/ShareAnnotationMenus.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"

namespace {

//每像素字节数
const int kSicBpp = 4;
//至少要有这么多个不透明度大于零的像素才算「看得见」
const int kSicMinVisiblePixels = 6;

typedef std::vector<uint8_t> SicImage;

//画一条命令的图标；没有图标时返回空
SicImage SicRender(const screenshare::AnnotMenuChoice& c, const screenshare::AnnotBarState& state, bool checked,
                   int size)
{
    SicImage img(static_cast<size_t>(size) * size * kSicBpp, 0);
    if (!screenshare::RenderAnnotChoiceIcon(c, state, checked, size, &img[0], size * kSicBpp))
        return SicImage();
    return img;
}

int SicVisible(const SicImage& img)
{
    int n = 0;
    for (size_t i = 3; i < img.size(); i += kSicBpp)
    {
        if (img[i] > 0)
            ++n;
    }
    return n;
}

//逐层检查一组菜单：可点项都有图标且看得见、同层图标互不相同
void SicCheckLevel(const std::vector<screenshare::AnnotMenuItem>& items, const screenshare::AnnotBarState& state,
                   int group)
{
    std::vector<SicImage> seen;
    std::vector<std::wstring> names;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const screenshare::AnnotMenuItem& it = items[i];
        if (!it.m_children.empty())
            SicCheckLevel(it.m_children, state, group);
        screenshare::AnnotMenuChoice c;
        const bool has = screenshare::AnnotMenuItemIconChoice(it, c);
        if (it.m_separator || !it.m_enabled)
        {
            CHECK_FALSE(has);
            continue;
        }
        INFO("group " << group << " item " << i);
        REQUIRE(has);
        //颜色项沿用色块，不走效果图标
        if (it.m_swatch >= 0)
            continue;
        const SicImage img = SicRender(c, state, false, screenshare::kAnnotMenuIconSize);
        REQUIRE_FALSE(img.empty());
        CHECK(SicVisible(img) >= kSicMinVisiblePixels);
        //子菜单父项与其中一个子项图标相同，不参与同层比较
        if (!it.m_children.empty())
            continue;
        for (size_t k = 0; k < seen.size(); ++k)
        {
            //先算出布尔值再断言：直接比较两个字节数组时，失败信息会把整个数组展开打印
            const bool same = (seen[k] == img);
            INFO("same icon as earlier item " << k << " (command " << c.m_command << ", value " << c.m_value << ")");
            CHECK_FALSE(same);
        }
        seen.push_back(img);
    }
}

} // namespace

TEST_CASE("SSIC1 SSIC2 every menu item has a distinct visible icon", "[shareicon]")
{
    screenshare::AnnotBarState state;
    for (int g = 0; g < screenshare::kAnnotGroupCount; ++g)
        SicCheckLevel(screenshare::BuildAnnotGroupMenu(g, state), state, g);
    //换成浅色（白色）时图标仍然看得见：样例改用深灰
    state.m_color = 0xFFFFFF;
    for (int g = 0; g < screenshare::kAnnotGroupCount; ++g)
        SicCheckLevel(screenshare::BuildAnnotGroupMenu(g, state), state, g);
}

TEST_CASE("SSIC3 checked icon differs from unchecked", "[shareicon]")
{
    screenshare::AnnotBarState state;
    screenshare::AnnotMenuChoice c;
    c.m_command = screenshare::annot_bar_tool;
    c.m_value = screenshare::annot_tool_marker;
    const SicImage plain = SicRender(c, state, false, screenshare::kAnnotMenuIconSize);
    const SicImage checked = SicRender(c, state, true, screenshare::kAnnotMenuIconSize);
    REQUIRE_FALSE(plain.empty());
    REQUIRE_FALSE(checked.empty());
    const bool same = (plain == checked);
    CHECK_FALSE(same);
    CHECK(SicVisible(checked) > SicVisible(plain));
}

TEST_CASE("SSIC4 submenu parent uses the checked child's icon", "[shareicon]")
{
    screenshare::AnnotBarState state;
    state.m_endCap = screenshare::annot_cap_diamond;
    const std::vector<screenshare::AnnotMenuItem> items =
        screenshare::BuildAnnotGroupMenu(screenshare::annot_group_arrow, state);
    bool found = false;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_children.empty())
            continue;
        screenshare::AnnotMenuChoice c;
        REQUIRE(screenshare::AnnotMenuItemIconChoice(items[i], c));
        if (c.m_command == screenshare::annot_bar_end_cap)
        {
            CHECK(c.m_value == screenshare::annot_cap_diamond);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("SSIC5 group button icon follows the current tool", "[shareicon]")
{
    screenshare::AnnotBarState state;
    screenshare::AnnotMenuChoice c;
    //当前工具是马克笔：画笔组为马克笔，线条组为代表项直线
    state.m_tool = screenshare::annot_tool_marker;
    REQUIRE(screenshare::AnnotGroupButtonChoice(screenshare::annot_group_pen, state, c));
    CHECK(c.m_command == screenshare::annot_bar_tool);
    CHECK(c.m_value == screenshare::annot_tool_marker);
    REQUIRE(screenshare::AnnotGroupButtonChoice(screenshare::annot_group_line, state, c));
    CHECK(c.m_value == screenshare::annot_tool_line);
    REQUIRE(screenshare::AnnotGroupButtonChoice(screenshare::annot_group_shape, state, c));
    CHECK(c.m_value == screenshare::annot_tool_rect);
    //当前工具是星形：形状组为星形，画笔组为代表项钢笔
    state.m_tool = screenshare::annot_tool_star;
    REQUIRE(screenshare::AnnotGroupButtonChoice(screenshare::annot_group_shape, state, c));
    CHECK(c.m_value == screenshare::annot_tool_star);
    REQUIRE(screenshare::AnnotGroupButtonChoice(screenshare::annot_group_pen, state, c));
    CHECK(c.m_value == screenshare::annot_tool_pen);
    //样式组按当前样式
    state.m_lineStyle = screenshare::annot_line_dot;
    REQUIRE(screenshare::AnnotGroupButtonChoice(screenshare::annot_group_line_style, state, c));
    CHECK(c.m_command == screenshare::annot_bar_line_style);
    CHECK(c.m_value == screenshare::annot_line_dot);
    //每个分组按钮（颜色组除外）都有图标且看得见
    for (int g = 0; g < screenshare::kAnnotGroupCount; ++g)
    {
        INFO("group " << g);
        const bool has = screenshare::AnnotGroupButtonChoice(g, state, c);
        if (g == screenshare::annot_group_color)
        {
            CHECK_FALSE(has);
            continue;
        }
        REQUIRE(has);
        CHECK(SicVisible(SicRender(c, state, false, screenshare::kAnnotButtonIconSize)) >= kSicMinVisiblePixels);
    }
}

TEST_CASE("SSIC6 commands without an icon leave the output untouched", "[shareicon]")
{
    screenshare::AnnotBarState state;
    const int noIcon[] = { screenshare::annot_bar_undo, screenshare::annot_bar_clear, screenshare::annot_bar_permission,
                           screenshare::annot_bar_exit, screenshare::annot_bar_save, 0 };
    for (size_t i = 0; i < sizeof(noIcon) / sizeof(noIcon[0]); ++i)
    {
        screenshare::AnnotMenuChoice c;
        c.m_command = noIcon[i];
        CHECK_FALSE(screenshare::AnnotChoiceHasIcon(c));
        std::vector<uint8_t> img(static_cast<size_t>(screenshare::kAnnotMenuIconSize) * screenshare::kAnnotMenuIconSize *
                                 kSicBpp, 0x5A);
        const std::vector<uint8_t> before = img;
        CHECK_FALSE(screenshare::RenderAnnotChoiceIcon(c, state, false, screenshare::kAnnotMenuIconSize, &img[0],
                                                       screenshare::kAnnotMenuIconSize * kSicBpp));
        const bool untouched = (img == before);
        CHECK(untouched);
    }
}

namespace {

//工具条的参照矩形（屏幕坐标）：放在屏幕左上角附近一块，工具条摆在它下方
const RECT kSicAnchor = { 40, 40, 1240, 640 };

//工具条上某一组的分组按钮；没有时返回 NULL
balloonwjui::DuiButton* SicGroupButton(screenshare::CShareAnnotationBar& bar, int group)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    if (content == NULL)
        return NULL;
    return dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(static_cast<UINT>(screenshare::CShareAnnotationBar::kIdGroupBase + group)));
}

//工具条上的「绘图工具」按钮；没有时返回 NULL
balloonwjui::DuiButton* SicToolsButton(screenshare::CShareAnnotationBar& bar)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    if (content == NULL)
        return NULL;
    return dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(static_cast<UINT>(screenshare::CShareAnnotationBar::kIdTools)));
}

int SicWindowWidth(screenshare::CShareAnnotationBar& bar)
{
    RECT rc;
    bar.GetWindowRect(&rc);
    return rc.right - rc.left;
}

} // namespace

TEST_CASE("SSIC7 tools and style buttons on the bar carry icons", "[shareicon]")
{
    screenshare::CShareAnnotationBar bar;
    bar.Show(kSicAnchor, NULL, false);
    REQUIRE(bar.IsWindow());
    //四个样式组按钮都有图标（颜色组为色块）
    const int styleGroups[] = { screenshare::annot_group_color, screenshare::annot_group_width,
                                screenshare::annot_group_line_style, screenshare::annot_group_fill };
    for (size_t i = 0; i < sizeof(styleGroups) / sizeof(styleGroups[0]); ++i)
    {
        INFO("group " << styleGroups[i]);
        balloonwjui::DuiButton* b = SicGroupButton(bar, styleGroups[i]);
        REQUIRE(b != NULL);
        CHECK(b->GetLeadingIcon() != NULL);
    }
    //「绘图工具」按钮有图标；换成马克笔后图标换了一张，文字写当前工具
    balloonwjui::DuiButton* tools = SicToolsButton(bar);
    REQUIRE(tools != NULL);
    const HBITMAP before = tools->GetLeadingIcon();
    CHECK(before != NULL);
    screenshare::AnnotBarState state;
    state.m_tool = screenshare::annot_tool_marker;
    bar.SetState(state);
    tools = SicToolsButton(bar);
    REQUIRE(tools != NULL);
    CHECK(tools->GetLeadingIcon() != NULL);
    CHECK(tools->GetLeadingIcon() != before);
    CHECK(std::wstring(tools->GetText().GetString()) == screenshare::AnnotToolsButtonText(state));
    bar.Hide();
}

TEST_CASE("SSIC8 the bar can be collapsed and expanded", "[shareicon]")
{
    screenshare::CShareAnnotationBar bar;
    bar.Show(kSicAnchor, NULL, false);
    REQUIRE(bar.IsWindow());
    const int fullW = SicWindowWidth(bar);
    CHECK_FALSE(bar.IsCollapsed());
    bar.SetCollapsed(true);
    CHECK(bar.IsCollapsed());
    CHECK(SicWindowWidth(bar) < fullW / 2);
    CHECK(SicToolsButton(bar) == NULL);
    REQUIRE(bar.GetClientContent() != NULL);
    CHECK(bar.GetClientContent()->FindCtrlById(screenshare::CShareAnnotationBar::kIdExpand) != NULL);
    bar.SetCollapsed(false);
    CHECK_FALSE(bar.IsCollapsed());
    CHECK(SicWindowWidth(bar) == fullW);
    CHECK(SicToolsButton(bar) != NULL);
    bar.Hide();
}

namespace {

//工具条整条宽度的上限（像素，96 DPI 下）：收纳之前约 1500，收纳后约 850；2026-10-08 加了「选择」按钮后约 930，
//用户同意把上限由 900 放宽到 940（docs/屏幕共享与录制方案.md 第 14.11 节）
const int kSicMaxBarW = 940;
//工具组的个数
const size_t kSicToolGroups = 9;

//两组菜单项的 id 与文字逐项相同（含子菜单）
bool SicSameMenu(const std::vector<screenshare::AnnotMenuItem>& a, const std::vector<screenshare::AnnotMenuItem>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_id != b[i].m_id || a[i].m_text != b[i].m_text || a[i].m_separator != b[i].m_separator ||
            !SicSameMenu(a[i].m_children, b[i].m_children))
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("SSIC9 tools menu holds the nine tool groups", "[shareicon]")
{
    screenshare::AnnotBarState state;
    state.m_tool = screenshare::annot_tool_star;
    const std::vector<screenshare::AnnotMenuItem> menu = screenshare::BuildAnnotToolsMenu(state);
    REQUIRE(menu.size() == kSicToolGroups);
    size_t k = 0;
    for (int i = 0; i < screenshare::kAnnotGroupCount; ++i)
    {
        const int g = screenshare::kAnnotGroupOrder[i];
        if (!screenshare::AnnotIsToolGroup(g))
            continue;
        INFO("group " << g);
        REQUIRE(k < menu.size());
        CHECK(menu[k].m_text == screenshare::AnnotGroupName(g));
        CHECK(SicSameMenu(menu[k].m_children, screenshare::BuildAnnotGroupMenu(g, state)));
        ++k;
    }
    CHECK(k == kSicToolGroups);
    //样式组不是工具组
    CHECK_FALSE(screenshare::AnnotIsToolGroup(screenshare::annot_group_color));
    CHECK_FALSE(screenshare::AnnotIsToolGroup(screenshare::annot_group_width));
    CHECK_FALSE(screenshare::AnnotIsToolGroup(screenshare::annot_group_line_style));
    CHECK_FALSE(screenshare::AnnotIsToolGroup(screenshare::annot_group_fill));
    //按钮文字写当前工具
    CHECK(screenshare::AnnotToolsButtonText(state) ==
          screenshare::AnnotGroupButtonText(screenshare::annot_group_shape, state));
}

TEST_CASE("SSIC10 the bar keeps one tools button instead of nine", "[shareicon]")
{
    screenshare::CShareAnnotationBar bar;
    bar.Show(kSicAnchor, NULL, false);
    REQUIRE(bar.IsWindow());
    for (int g = 0; g < screenshare::kAnnotGroupCount; ++g)
    {
        INFO("group " << g);
        if (screenshare::AnnotIsToolGroup(g))
            CHECK(SicGroupButton(bar, g) == NULL);
        else
            CHECK(SicGroupButton(bar, g) != NULL);
    }
    CHECK(SicToolsButton(bar) != NULL);
    CHECK(SicWindowWidth(bar) <= kSicMaxBarW);
    bar.Hide();
}

TEST_CASE("SSIC11 hiding a collapsed bar leaves no dangling button", "[shareicon]")
{
    screenshare::CShareAnnotationBar bar;
    bar.Show(kSicAnchor, NULL, false);
    REQUIRE(bar.IsWindow());
    bar.SetCollapsed(true);
    bar.Hide();
    CHECK_FALSE(bar.IsWindow());
    //关闭之后控制器仍可能同步状态（换工具、换颜色），之后再进入标注模式
    screenshare::AnnotBarState state;
    state.m_tool = screenshare::annot_tool_marker;
    bar.SetState(state);
    bar.Show(kSicAnchor, NULL, false);
    REQUIRE(bar.IsWindow());
    CHECK(bar.IsCollapsed());
    bar.Hide();
}

namespace {

//放大倍数、每行放几个图标、图标之间的间隔（像素，放大后）
const int kSnapScale = 4;
const int kSnapPerRow = 12;
const int kSnapGap = 8;

void SnapCollect(const std::vector<screenshare::AnnotMenuItem>& items, std::vector<screenshare::AnnotMenuChoice>& out,
                 std::vector<bool>& checked)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].m_children.empty())
        {
            SnapCollect(items[i].m_children, out, checked);
            continue;
        }
        screenshare::AnnotMenuChoice c;
        if (items[i].m_swatch < 0 && screenshare::AnnotMenuItemIconChoice(items[i], c))
        {
            out.push_back(c);
            checked.push_back(items[i].m_checked);
        }
    }
}

} // namespace

TEST_CASE("SSICY icon sheet snapshot", "[.][shareicon-snap]")
{
    screenshare::AnnotBarState state;
    std::vector<screenshare::AnnotMenuChoice> all;
    std::vector<bool> checked;
    for (int g = 0; g < screenshare::kAnnotGroupCount; ++g)
        SnapCollect(screenshare::BuildAnnotGroupMenu(screenshare::kAnnotGroupOrder[g], state), all, checked);
    const int cell = screenshare::kAnnotMenuIconSize * kSnapScale + kSnapGap;
    const int rows = static_cast<int>((all.size() + kSnapPerRow - 1) / kSnapPerRow);
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
    {
        Gdiplus::Bitmap sheet(kSnapPerRow * cell + kSnapGap, rows * cell + kSnapGap, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&sheet);
        g.Clear(Gdiplus::Color(255, 255, 255, 255));
        g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        for (size_t i = 0; i < all.size(); ++i)
        {
            SicImage img = SicRender(all[i], state, checked[i], screenshare::kAnnotMenuIconSize);
            if (img.empty())
                continue;
            Gdiplus::Bitmap icon(screenshare::kAnnotMenuIconSize, screenshare::kAnnotMenuIconSize,
                                 screenshare::kAnnotMenuIconSize * kSicBpp, PixelFormat32bppPARGB, &img[0]);
            const int x = kSnapGap + static_cast<int>(i % kSnapPerRow) * cell;
            const int y = kSnapGap + static_cast<int>(i / kSnapPerRow) * cell;
            g.DrawImage(&icon, x, y, screenshare::kAnnotMenuIconSize * kSnapScale,
                        screenshare::kAnnotMenuIconSize * kSnapScale);
        }
        CLSID png;
        const CLSID kPngEncoder = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
        png = kPngEncoder;
        CHECK(sheet.Save(L"E:\\flamingo\\snapshots\\annot_menu_icons.png", &png, NULL) == Gdiplus::Ok);
    }
    Gdiplus::GdiplusShutdown(token);
}
