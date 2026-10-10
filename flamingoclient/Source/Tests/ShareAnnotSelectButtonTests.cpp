/**
 *  标注工具条上的「选择」按钮（标签 [annotselect]），ShareAnnotSelectButtonTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.11 节第 6 条（2026-10-08 用户确认）。原先要选「选择」得点「绘图工具」、在九个工具组里
 *  移到「选择」、再点二级菜单里的「选择对象」，切回画笔又要走一遍菜单。现在「绘图工具」按钮左边多一个只有图标的「选择」按钮：
 *  点一下进入选择（按钮高亮），再点一下或点「绘图工具」回到上一个画图工具；选择状态下「绘图工具」仍显示上一个画图工具。
 *  覆盖：
 *    - SASB1 点「选择」后的目标工具：不是选择工具时进入选择；是选择工具时回到最近一次用的画图工具，取值不认识时为钢笔；
 *    - SASB2 ApplyAnnotChoice 记下最近一次用的画图工具：选画图工具、会切到某个工具的样式时更新，选「选择」或只改颜色时不变；
 *    - SASB3 选择状态下「绘图工具」按钮的文字是最近一次用的画图工具的写法；
 *    - SASB4 工具条窗口：「选择」按钮紧挨「绘图工具」左边；画图时「绘图工具」为主色、「选择」为默认色，选择状态下相反；
 *            点「选择」投出选工具命令（进入选择 / 回到上一个画图工具）；选择状态下点「绘图工具」直接回到上一个画图工具，
 *            不弹菜单；整条放得下全部按钮。
 *  框架 Catch2 v3。会建工具条窗口（屏幕外的位置），测完即销毁。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Share/ShareAnnotation.h"
#include "../../FlamingoMedia/Share/ShareAnnotationBar.h"
#include "../../FlamingoMedia/Share/ShareAnnotationMenus.h"
#include "../../FlamingoMedia/Share/ShareAnnotationSession.h"
#include "../../FlamingoMedia/Share/ShareMessages.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//参照矩形（屏幕坐标）：工具条摆在它下方或底部
const RECT kSasbAnchor = { 100, 100, 1500, 800 };
//「选择」与「绘图工具」之间最多隔多少像素（工具条按钮的间距；中间不能夹别的按钮）
const int kSasbMaxGap = 12;
//不认识的工具取值
const int kSasbUnknownTool = 9999;
//最右边那个按钮至少要有多宽（像素），才算没有被压扁（「退出标注」的设计宽度为 80）
const int kSasbMinButtonWidth = 40;

//在控件树里找最靠右的按钮：记下它的右边与宽度
void SasbRightmostButton(balloonwjui::DuiControl* ctrl, int& right, int& width)
{
    if (ctrl == NULL)
        return;
    if (dynamic_cast<balloonwjui::DuiButton*>(ctrl) != NULL && ctrl->IsVisible() && ctrl->GetRect().right > right)
    {
        right = ctrl->GetRect().right;
        width = ctrl->GetRect().right - ctrl->GetRect().left;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& children = ctrl->Children();
    for (size_t i = 0; i < children.size(); ++i)
        SasbRightmostButton(children[i].get(), right, width);
}

HWND SasbNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

balloonwjui::DuiButton* SasbButton(CShareAnnotationBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

void SasbClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//取走通知窗口里最后一条标注命令；没有时返回 false
bool SasbTakeCommand(HWND notify, WPARAM& wParam, LPARAM& lParam)
{
    MSG msg;
    bool got = false;
    while (::PeekMessageW(&msg, notify, kMsgShareAnnotCommand, kMsgShareAnnotCommand, PM_REMOVE))
    {
        got = true;
        wParam = msg.wParam;
        lParam = msg.lParam;
    }
    return got;
}

//工具条投给自己的「弹菜单」消息是否在队列里（取走不处理，免得真的弹出菜单）
bool SasbTakeOwnMenuRequest(CShareAnnotationBar& bar)
{
    MSG msg;
    bool got = false;
    while (::PeekMessageW(&msg, bar.m_hWnd, CShareAnnotationBar::kMsgShowMenu, CShareAnnotationBar::kMsgShowMenu,
                          PM_REMOVE))
        got = true;
    return got;
}

AnnotMenuChoice SasbChoice(int command, int value)
{
    AnnotMenuChoice c;
    c.m_command = command;
    c.m_value = value;
    return c;
}

} // namespace

TEST_CASE("SASB1 the select button toggles between select and the last drawing tool", "[annotselect]")
{
    AnnotBarState s;
    //默认是钢笔：进入选择
    CHECK(AnnotSelectButtonTarget(s) == kAnnotSelect);
    //选择状态：回到最近一次用的画图工具
    s.m_tool = kAnnotSelect;
    s.m_lastDrawTool = annot_tool_marker;
    CHECK(AnnotSelectButtonTarget(s) == annot_tool_marker);
    s.m_lastDrawTool = kAnnotEraser;
    CHECK(AnnotSelectButtonTarget(s) == kAnnotEraser);
    //最近一次的工具不认识：回到钢笔
    s.m_lastDrawTool = kSasbUnknownTool;
    CHECK(AnnotSelectButtonTarget(s) == annot_tool_pen);
    //橡皮等其他工具：进入选择
    s.m_tool = kAnnotEraser;
    CHECK(AnnotSelectButtonTarget(s) == kAnnotSelect);
}

TEST_CASE("SASB2 applying a choice remembers the last drawing tool", "[annotselect]")
{
    AnnotBarState s;
    CHECK(s.m_lastDrawTool == annot_tool_pen);
    REQUIRE(ApplyAnnotChoice(SasbChoice(annot_bar_tool, annot_tool_marker), s));
    CHECK(s.m_lastDrawTool == annot_tool_marker);
    //进入选择：最近一次的画图工具不变
    REQUIRE(ApplyAnnotChoice(SasbChoice(annot_bar_tool, kAnnotSelect), s));
    CHECK(s.m_tool == kAnnotSelect);
    CHECK(s.m_lastDrawTool == annot_tool_marker);
    //只改颜色：不变
    REQUIRE(ApplyAnnotChoice(SasbChoice(annot_bar_color, 0x00FF00), s));
    CHECK(s.m_lastDrawTool == annot_tool_marker);
    //会切到某个工具的样式（数字标号样式切到数字标号工具）：更新
    REQUIRE(ApplyAnnotChoice(SasbChoice(annot_bar_badge_style, annot_badge_circle), s));
    CHECK(s.m_tool == annot_tool_badge);
    CHECK(s.m_lastDrawTool == annot_tool_badge);
    //橡皮也算画图工具
    REQUIRE(ApplyAnnotChoice(SasbChoice(annot_bar_tool, kAnnotEraser), s));
    CHECK(s.m_lastDrawTool == kAnnotEraser);
}

TEST_CASE("SASB3 the tools button shows the last drawing tool while selecting", "[annotselect]")
{
    AnnotBarState drawing;
    drawing.m_tool = annot_tool_marker;
    AnnotBarState selecting = drawing;
    selecting.m_tool = kAnnotSelect;
    selecting.m_lastDrawTool = annot_tool_marker;
    CHECK(AnnotToolsButtonText(selecting) == AnnotToolsButtonText(drawing));
    //最近一次的工具不认识：按钢笔写
    AnnotBarState pen;
    selecting.m_lastDrawTool = kSasbUnknownTool;
    CHECK(AnnotToolsButtonText(selecting) == AnnotToolsButtonText(pen));
}

TEST_CASE("SASB4 the select button on the bar", "[annotselect]")
{
    const HWND notify = SasbNotifyWindow();
    CShareAnnotationBar bar;
    bar.Show(kSasbAnchor, notify, true);
    REQUIRE(bar.IsWindow());
    balloonwjui::DuiButton* select = SasbButton(bar, CShareAnnotationBar::kIdSelect);
    balloonwjui::DuiButton* tools = SasbButton(bar, CShareAnnotationBar::kIdTools);
    REQUIRE(select != NULL);
    REQUIRE(tools != NULL);
    //紧挨「绘图工具」左边
    CHECK(select->GetRect().right <= tools->GetRect().left);
    CHECK(tools->GetRect().left - select->GetRect().right <= kSasbMaxGap);
    //画图时：「绘图工具」主色、「选择」默认色
    CHECK(tools->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    CHECK(select->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    //点「选择」：投出选工具命令，进入选择
    WPARAM w = 0;
    LPARAM l = 0;
    SasbTakeCommand(notify, w, l);
    SasbClick(bar.m_hWnd, CShareAnnotationBar::kIdSelect);
    REQUIRE(SasbTakeCommand(notify, w, l));
    CHECK(static_cast<int>(w) == annot_bar_tool);
    CHECK(static_cast<int>(l) == kAnnotSelect);
    //控制器套用后回写状态：选择状态下相反，「绘图工具」仍写上一个画图工具
    AnnotBarState s;
    s.m_tool = kAnnotSelect;
    s.m_lastDrawTool = annot_tool_marker;
    bar.SetState(s);
    CHECK(select->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    CHECK(tools->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    AnnotBarState marker;
    marker.m_tool = annot_tool_marker;
    CHECK(std::wstring(tools->GetText().GetString()) == AnnotToolsButtonText(marker));
    //再点「选择」：回到上一个画图工具
    SasbClick(bar.m_hWnd, CShareAnnotationBar::kIdSelect);
    REQUIRE(SasbTakeCommand(notify, w, l));
    CHECK(static_cast<int>(w) == annot_bar_tool);
    CHECK(static_cast<int>(l) == annot_tool_marker);
    //选择状态下点「绘图工具」：直接回到上一个画图工具，不弹菜单
    SasbTakeOwnMenuRequest(bar);
    SasbClick(bar.m_hWnd, CShareAnnotationBar::kIdTools);
    REQUIRE(SasbTakeCommand(notify, w, l));
    CHECK(static_cast<int>(w) == annot_bar_tool);
    CHECK(static_cast<int>(l) == annot_tool_marker);
    CHECK_FALSE(SasbTakeOwnMenuRequest(bar));
    //整条放得下全部按钮：最右边的按钮（「退出标注」）在窗口之内，且没有被压扁
    int rightmost = 0;
    int rightmostWidth = 0;
    SasbRightmostButton(bar.GetClientContent(), rightmost, rightmostWidth);
    RECT client = { 0, 0, 0, 0 };
    ::GetClientRect(bar.m_hWnd, &client);
    CHECK(rightmost > 0);
    CHECK(rightmost <= client.right);
    CHECK(rightmostWidth > kSasbMinButtonWidth);
    bar.Hide();
    ::DestroyWindow(notify);
}
