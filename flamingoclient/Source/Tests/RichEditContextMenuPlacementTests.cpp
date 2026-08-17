/**
 *  RichEditContextMenuPlacementTests.cpp
 *
 *  文本框右键菜单"跑到屏幕外"问题的回归测试。
 *
 *  问题原本长这样（2026-08-12 修复前）：
 *    1. 文本框控件的 ShowContextMenu 把光标点原样交给 TrackPopup，中间不做
 *       任何处理；
 *    2. DuiMenu::TrackPopup → TrackPopupEx → DuiMenuPopup::Open，而 Open 里直接
 *       SetWindowPos(NULL, screenX, screenY, ...)，菜单窗口左上角就钉在传入坐标；
 *    3. 于是光标靠近工作区右 / 下边缘时（聊天窗口最大化后输入框正好贴着屏幕
 *       底部），菜单整体跑出桌面，用户既看不见也点不着。
 *
 *  修复方式是把落点计算下沉到 balloonui/Controls/List/DuiMenuPlacement.h，并由
 *  DuiMenuPopup::Open 在定位窗口前统一调用，所以库内所有菜单（右键菜单、菜单栏
 *  下拉、子菜单）都自动受保护。
 *
 *  本文件断言的就是<u>库里真正在跑的那几个函数</u>（balloonwjui::
 *  ClampMenuOriginToRect / ClampSubMenuOrigin），菜单模型与尺寸也取自真实的
 *  BuildEditContextMenu + DuiMenu::MeasureSize，因此守的是真实行为，而不是测试
 *  文件里另写一份的模型。唯一没覆盖的是"Open 确实调用了这些函数"这一步 ——
 *  那需要真的弹出窗口，不适合放进纯逻辑测试。
 *
 *  用 Catch2 v3（amalgamated）作框架，不创建窗口、不真正弹菜单。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Input/EditContextMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenuPlacement.h"

using balloonwjui::BuildEditContextMenu;
using balloonwjui::ClampMenuOriginToRect;
using balloonwjui::ClampSubMenuOrigin;
using balloonwjui::DuiMenu;
using balloonwjui::EditContextCommandLabel;
using balloonwjui::EditContextMenuItem;
using balloonwjui::EditContextState;
using balloonwjui::EditCtxCmd_Separator;

namespace {

// 典型 1920x1080 显示器、任务栏在底部时的桌面工作区（工作区高 1040）。
// 与 PopupMenuPlacementTests.cpp 里的 kWork 同一口径，便于两组用例互相对照。
const RECT kWork = { 0, 0, 1920, 1040 };

// 右键时光标距工作区右下角的留白（像素）。取 8 是为了模拟真实场景：聊天窗口
// 最大化后输入框贴在窗口底部，在其右下角右键，光标离工作区边界只剩几像素。
const int kCornerInsetPx = 8;

// 屏幕中央锚点（像素），用于验证"离边缘足够远时落点不该被动过"。
const int kCenterAnchorX = 800;
const int kCenterAnchorY = 400;

// 按文本框控件 ShowContextMenu 的同一套规则把菜单模型填进 DuiMenu：
// 分隔条走 AppendSeparator，可用项走 AppendItem，灰显项走 AppendDisabled，
// 文案统一取 EditContextCommandLabel。
//   menu：  待填充的菜单（出参，调用前应为空）。
//   state： 文本框当前状态，决定菜单有哪几项、各项是否灰显。
void FillMenuLikeRichEditHost(DuiMenu& menu, const EditContextState& state)
{
    std::vector<EditContextMenuItem> model = BuildEditContextMenu(state);
    for (std::vector<EditContextMenuItem>::const_iterator it = model.begin();
         it != model.end(); ++it)
    {
        if (it->m_cmd == EditCtxCmd_Separator)
        {
            menu.AppendSeparator();
        }
        else if (it->m_enabled)
        {
            menu.AppendItem((UINT)it->m_cmd, EditContextCommandLabel(it->m_cmd));
        }
        else
        {
            menu.AppendDisabled((UINT)it->m_cmd, EditContextCommandLabel(it->m_cmd));
        }
    }
}

// 由左上角与尺寸算出菜单窗口占据的屏幕矩形（DuiMenuPopup::Open 正是用这两者
// 去调 SetWindowPos 的）。
RECT MenuRectAt(POINT origin, SIZE menuSize)
{
    RECT rc;
    rc.left = origin.x;
    rc.top = origin.y;
    rc.right = origin.x + menuSize.cx;
    rc.bottom = origin.y + menuSize.cy;
    return rc;
}

POINT MakePt(long x, long y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

SIZE MakeSize(long cx, long cy)
{
    SIZE s;
    s.cx = cx;
    s.cy = cy;
    return s;
}

RECT MakeRect(long l, long t, long r, long b)
{
    RECT rc;
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    return rc;
}

// 读写状态的聊天输入框：有文本、有选区、剪贴板里也有文本，五项菜单全部可用。
EditContextState MakeEditableState()
{
    EditContextState st;
    st.m_readOnly = false;
    st.m_isPassword = false;
    st.m_hasText = true;
    st.m_hasSelection = true;
    st.m_clipboardHasText = true;
    return st;
}

// 只读状态（公告详情正文那类）：菜单只剩 复制 / 分隔条 / 全选 三项，比读写模式矮。
EditContextState MakeReadOnlyState()
{
    EditContextState st = MakeEditableState();
    st.m_readOnly = true;
    return st;
}

} // anonymous namespace

TEST_CASE("RECM1 右下角右键：读写菜单整张留在工作区内", "[richeditmenu]")
{
    DuiMenu menu;
    FillMenuLikeRichEditHost(menu, MakeEditableState());

    SIZE sz = menu.MeasureSize();
    REQUIRE(sz.cx > 0);
    REQUIRE(sz.cy > 0);

    POINT anchor = MakePt(kWork.right - kCornerInsetPx, kWork.bottom - kCornerInsetPx);

    // 修复前落点原样用于定位，菜单右边界 = anchor.x + sz.cx 会远超工作区。这里
    // 把"修复前会溢出到哪"一并算出来放进 INFO，说明这条用例守的到底是什么。
    RECT rcBefore = MenuRectAt(anchor, sz);
    POINT origin = ClampMenuOriginToRect(anchor, sz, kWork);
    RECT rcAfter = MenuRectAt(origin, sz);

    INFO("菜单尺寸 " << sz.cx << "x" << sz.cy
         << "；锚点 (" << anchor.x << "," << anchor.y << ")"
         << "；修复前右/下边界 " << rcBefore.right << "/" << rcBefore.bottom
         << "（工作区 " << kWork.right << "/" << kWork.bottom << "）"
         << "；修复后落点 (" << origin.x << "," << origin.y << ")");

    // 修复前确实是溢出的 —— 这两条保证用例本身没跑偏（锚点选得够贴边）。
    CHECK(rcBefore.right > kWork.right);
    CHECK(rcBefore.bottom > kWork.bottom);

    // 修复后：四条边全部落在工作区内。
    CHECK(rcAfter.left >= kWork.left);
    CHECK(rcAfter.top >= kWork.top);
    CHECK(rcAfter.right <= kWork.right);
    CHECK(rcAfter.bottom <= kWork.bottom);

    // 策略是"右边放不下翻到锚点左侧、下边放不下翻到锚点上方"，所以落点应当被
    // 挪到锚点的左上方，而不是简单贴着边缘。
    CHECK(origin.x < anchor.x);
    CHECK(origin.y < anchor.y);
}

TEST_CASE("RECM2 右下角右键：只读菜单同样整张留在工作区内", "[richeditmenu]")
{
    DuiMenu menu;
    FillMenuLikeRichEditHost(menu, MakeReadOnlyState());

    SIZE sz = menu.MeasureSize();
    REQUIRE(sz.cx > 0);
    REQUIRE(sz.cy > 0);

    POINT anchor = MakePt(kWork.right - kCornerInsetPx, kWork.bottom - kCornerInsetPx);
    RECT rcBefore = MenuRectAt(anchor, sz);
    RECT rcAfter = MenuRectAt(ClampMenuOriginToRect(anchor, sz, kWork), sz);

    INFO("只读菜单尺寸 " << sz.cx << "x" << sz.cy
         << "；修复前右/下边界 " << rcBefore.right << "/" << rcBefore.bottom
         << "；修复后矩形 (" << rcAfter.left << "," << rcAfter.top
         << ")-(" << rcAfter.right << "," << rcAfter.bottom << ")");

    // 只读菜单比读写菜单矮（少了剪切 / 粘贴两行），修复前照样溢出 —— 说明问题
    // 跟菜单有几项无关，根子在落点没有修正过。
    CHECK(rcBefore.right > kWork.right);
    CHECK(rcBefore.bottom > kWork.bottom);

    CHECK(rcAfter.left >= kWork.left);
    CHECK(rcAfter.top >= kWork.top);
    CHECK(rcAfter.right <= kWork.right);
    CHECK(rcAfter.bottom <= kWork.bottom);
}

TEST_CASE("RECM3 屏幕中央右键：落点原样不动，不误伤正常情况", "[richeditmenu]")
{
    DuiMenu menu;
    FillMenuLikeRichEditHost(menu, MakeEditableState());

    SIZE sz = menu.MeasureSize();
    POINT anchor = MakePt(kCenterAnchorX, kCenterAnchorY);
    POINT origin = ClampMenuOriginToRect(anchor, sz, kWork);

    INFO("菜单尺寸 " << sz.cx << "x" << sz.cy
         << "；锚点 (" << anchor.x << "," << anchor.y << ")"
         << " → 落点 (" << origin.x << "," << origin.y << ")");

    // 离边缘足够远时不该做任何调整，否则菜单会莫名其妙偏离光标。
    CHECK(origin.x == anchor.x);
    CHECK(origin.y == anchor.y);
}

TEST_CASE("RECM4 重复处理不改变结果（幂等）", "[richeditmenu]")
{
    DuiMenu menu;
    FillMenuLikeRichEditHost(menu, MakeEditableState());

    SIZE sz = menu.MeasureSize();
    POINT anchor = MakePt(kWork.right - kCornerInsetPx, kWork.bottom - kCornerInsetPx);

    POINT once = ClampMenuOriginToRect(anchor, sz, kWork);
    POINT twice = ClampMenuOriginToRect(once, sz, kWork);

    INFO("一次 (" << once.x << "," << once.y << ")"
         << "；两次 (" << twice.x << "," << twice.y << ")");

    // 幂等很重要：调用方自己算好落点（如"弹在按钮上方"）再传进来时，
    // DuiMenuPopup::Open 内部还会再处理一遍，不能把它挪走。
    CHECK(twice.x == once.x);
    CHECK(twice.y == once.y);
}

TEST_CASE("RECM5 子菜单右侧放不下时翻到父菜单左侧、不盖住父菜单", "[richeditmenu]")
{
    // 父菜单已经贴在工作区右缘附近，子菜单再往右展开就出界了。
    const RECT parentRect = MakeRect(1700, 500, 1880, 700);
    const SIZE subSize = MakeSize(160, 120);

    // OpenSubMenuAt 算出的期望落点：父菜单右缘 - 1，纵向与父项行顶对齐。
    POINT preferred = MakePt(parentRect.right - 1, parentRect.top);
    POINT origin = ClampSubMenuOrigin(parentRect, preferred, subSize, kWork);
    RECT rc = MenuRectAt(origin, subSize);

    INFO("父菜单 (" << parentRect.left << "," << parentRect.top
         << ")-(" << parentRect.right << "," << parentRect.bottom << ")"
         << "；子菜单落点 (" << origin.x << "," << origin.y << ")"
         << "；子菜单右边界 " << rc.right);

    // 整张子菜单留在工作区内。
    CHECK(rc.left >= kWork.left);
    CHECK(rc.top >= kWork.top);
    CHECK(rc.right <= kWork.right);
    CHECK(rc.bottom <= kWork.bottom);

    // 翻到了父菜单左侧：子菜单右缘压在父菜单左缘上（保留 1 像素重叠），
    // 而不是简单左移盖住父菜单正文。
    CHECK(rc.right == parentRect.left + 1);
}

TEST_CASE("RECM6 子菜单纵向放不下时只上移、不翻到锚点上方", "[richeditmenu]")
{
    // 父菜单很靠下，子菜单按父项行顶对齐会超出工作区底边。
    const RECT parentRect = MakeRect(400, 900, 580, 1030);
    const SIZE subSize = MakeSize(160, 200);

    POINT preferred = MakePt(parentRect.right - 1, 1000);
    POINT origin = ClampSubMenuOrigin(parentRect, preferred, subSize, kWork);
    RECT rc = MenuRectAt(origin, subSize);

    INFO("子菜单落点 (" << origin.x << "," << origin.y << ")"
         << "；矩形 (" << rc.left << "," << rc.top
         << ")-(" << rc.right << "," << rc.bottom << ")");

    // 贴住工作区底边，而不是翻到 preferred.y 上方去 —— 子菜单必须留在父项附近。
    CHECK(rc.bottom == kWork.bottom);
    CHECK(rc.top >= kWork.top);

    // 横向放得下，就还在父菜单右侧。
    CHECK(origin.x == preferred.x);
}

TEST_CASE("RECM7 菜单比工作区还大时贴住左上角、不出现负坐标", "[richeditmenu]")
{
    // 极端退化情况：菜单尺寸超过整个工作区，两个方向都塞不下。
    const SIZE hugeSize = MakeSize(kWork.right + 200, kWork.bottom + 200);
    POINT anchor = MakePt(kWork.right - kCornerInsetPx, kWork.bottom - kCornerInsetPx);

    POINT origin = ClampMenuOriginToRect(anchor, hugeSize, kWork);

    INFO("超大菜单落点 (" << origin.x << "," << origin.y << ")");

    // 保证左上角可见（用户至少能看到菜单开头并关掉它），不能算出负坐标。
    CHECK(origin.x == kWork.left);
    CHECK(origin.y == kWork.top);
}
