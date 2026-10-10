/**
 *  PanelDragZoneTests.cpp
 *
 *  "按住空白处拖动窗口"命中判定（PanelDragZone）的测试：验证纯函数
 *  IsPointInDragZone() —— 顶部渐变 Banner 与底部栏的空白处可拖、两块里的可点击
 *  子区域（头像 / 昵称 / 签名 / 状态胶囊 / 公告中心 / 消息盒子 / 汉堡菜单按钮 / 主操作按钮）
 *  不可拖、可拖区之外的列表区不可拖，以及边界与退化输入（空矩形、空数组、空指针）
 *  的处理。
 *
 *  几何取自主面板的真实布局（客户区 320x680、标题栏 32、Banner 高 108、底部栏
 *  高 44），坐标系为窗口客户区坐标，与 CBalloonMainPanel::CollectDragZoneRects
 *  收集到的矩形同一口径。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不取显示器信息。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/PanelDragZone.h"

using namespace balloonmain;

namespace {

POINT MakePt(long x, long y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

// ---- 主面板的典型布局（客户区坐标）------------------------------------
// 客户区 320x680，标题栏占顶部 32 —— 故 Banner 从 y=32 起、高 108；底部栏高 44，
// 贴在客户区底边。两块之间是搜索框 / Tab 条 / 列表区，均不可拖。
const RECT kHeaderRc    = {   0,  32, 320, 140 };   // 顶部渐变 Banner
const RECT kBottomBarRc = {   0, 636, 320, 680 };   // 底部栏

// Banner 里 6 块可点击区域（Banner 自己在 OnPaint 里按实际绘制位置算出来的）。
const RECT kAvatarRc = {  14,  44,  66,  96 };      // 头像（含外圈）
const RECT kNameRc   = {  78,  50, 150,  70 };      // 昵称文字
const RECT kStatusRc = {  78,  72, 140,  90 };      // 在线状态胶囊
const RECT kSignRc   = {  78,  95, 200, 113 };      // 个性签名文字
const RECT kBellRc   = {  16, 103,  43, 130 };      // 公告中心按钮（通知盒 + 溢出的角标）
const RECT kMsgBoxRc = {  48, 103,  75, 130 };      // 消息盒子按钮（2026-10-04 起，铃铛右侧，含溢出的角标）

// 底部栏里 2 个按钮：左padding 8 + 汉堡按钮宽 34；主操作按钮宽 80、右padding 8。
const RECT kMenuBtnRc    = {   8, 636,  42, 680 };  // 汉堡菜单按钮
const RECT kActionBtnRc  = { 232, 636, 312, 680 };  // "查找" / "建群"按钮

// 按 CollectDragZoneRects 的顺序拼出完整的两组矩形。
const RECT kDragRects[] = { kHeaderRc, kBottomBarRc };
const RECT kExcludeRects[] = { kAvatarRc, kNameRc, kSignRc, kStatusRc, kBellRc,
                               kMsgBoxRc, kMenuBtnRc, kActionBtnRc };

const int kDragCount    = (int)(sizeof(kDragRects) / sizeof(kDragRects[0]));
const int kExcludeCount = (int)(sizeof(kExcludeRects) / sizeof(kExcludeRects[0]));

// 用完整的两组矩形判一个点，省得每条用例都把 4 个参数抄一遍。
bool CanDrag(long x, long y)
{
    return IsPointInDragZone(MakePt(x, y), kDragRects, kDragCount,
                             kExcludeRects, kExcludeCount);
}

} // anonymous namespace

TEST_CASE("DZ1 底部栏空白处可拖动", "[dragzone]")
{
    // 两个按钮之间那一大片（含按钮与 spacer 之间 6px 的 gap）。
    CHECK(CanDrag(150, 658));
    CHECK(CanDrag(45,  658));    // 汉堡按钮右侧的 gap
    CHECK(CanDrag(230, 658));    // 主操作按钮左侧的 gap
    CHECK(CanDrag(4,   658));    // 左 padding（按钮之外）
    CHECK(CanDrag(316, 658));    // 右 padding（按钮之外）
}

TEST_CASE("DZ2 底部栏两个按钮上不可拖动", "[dragzone]")
{
    CHECK_FALSE(CanDrag(25,  658));    // 汉堡菜单按钮正中
    CHECK_FALSE(CanDrag(272, 658));    // "查找" / "建群"按钮正中
}

TEST_CASE("DZ3 Banner 空白渐变区可拖动", "[dragzone]")
{
    CHECK(CanDrag(280, 60));     // 昵称右侧的空白
    CHECK(CanDrag(280, 120));    // 底部两个按钮右侧的空白
    CHECK(CanDrag(100, 118));    // 原先铃铛右侧文字的位置：文字改成悬停提示后可以拖动
    CHECK(CanDrag(40,  100));    // 头像下方、公告中心上方的空隙
    CHECK(CanDrag(160, 36));     // 紧贴 Banner 顶边（标题栏之下第一行）
}

TEST_CASE("DZ4 Banner 里 6 块可点击区域不可拖动", "[dragzone]")
{
    CHECK_FALSE(CanDrag(40,  70));     // 头像
    CHECK_FALSE(CanDrag(100, 60));     // 昵称
    CHECK_FALSE(CanDrag(100, 80));     // 状态胶囊
    CHECK_FALSE(CanDrag(100, 104));    // 个性签名
    CHECK_FALSE(CanDrag(30,  118));    // 公告中心按钮
    CHECK_FALSE(CanDrag(60,  118));    // 消息盒子按钮
}

TEST_CASE("DZ5 可拖区之外一律不可拖动", "[dragzone]")
{
    CHECK_FALSE(CanDrag(160, 16));     // 标题栏（由基类判 HTCAPTION，不归本函数管）
    CHECK_FALSE(CanDrag(160, 158));    // 搜索框
    CHECK_FALSE(CanDrag(160, 190));    // Tab 条
    CHECK_FALSE(CanDrag(160, 400));    // 列表区
    CHECK_FALSE(CanDrag(160, 630));    // 列表区最下一行（底部栏之上）
}

TEST_CASE("DZ6 可拖矩形边界：左/上算在内，右/下算在外", "[dragzone]")
{
    // 与 ::PtInRect 同语义，也与控件矩形自身的语义一致。
    CHECK(CanDrag(kBottomBarRc.left, kBottomBarRc.top));            // 左上角在内
    CHECK_FALSE(CanDrag(kBottomBarRc.right, 658));                  // 右边界在外
    CHECK_FALSE(CanDrag(160, kBottomBarRc.bottom));                 // 下边界在外
    CHECK(CanDrag(160, kHeaderRc.bottom - 1));                      // Banner 最后一行仍在内
    CHECK_FALSE(CanDrag(160, kHeaderRc.bottom));                    // 再往下就出 Banner 了
}

TEST_CASE("DZ7 排除矩形边界：左/上被排除，右/下仍可拖", "[dragzone]")
{
    // 排除矩形与可拖区之间既不重叠也不漏缝：按钮右边界那一列已经属于空白。
    CHECK_FALSE(CanDrag(kMenuBtnRc.left, kMenuBtnRc.top));          // 按钮左上角：不可拖
    CHECK(CanDrag(kMenuBtnRc.right, 658));                          // 按钮右边界：可拖
    CHECK_FALSE(CanDrag(kMenuBtnRc.right - 1, 658));                // 按钮最后一列：不可拖
    CHECK(CanDrag(kActionBtnRc.right, 658));                        // 主操作按钮右边界：可拖
    CHECK_FALSE(CanDrag(kAvatarRc.left, kAvatarRc.top));            // 头像左上角：不可拖
    CHECK(CanDrag(kAvatarRc.right, kAvatarRc.bottom));              // 头像右下角：可拖
}

TEST_CASE("DZ8 空矩形不命中：首帧未绘制时整块可拖", "[dragzone]")
{
    // MainPanelHeader 画第一帧之前，5 块命中矩形都是 SetRectEmpty 出来的全 0 矩形。
    // 此时期望 Banner 整块可拖，而不是"全 0 矩形把 (0,0) 附近吃掉"。
    const RECT emptyExcludes[] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 },
                                   { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    const RECT headerOnly[] = { kHeaderRc };

    CHECK(IsPointInDragZone(MakePt(40, 70), headerOnly, 1, emptyExcludes, 5));
    CHECK(IsPointInDragZone(MakePt(280, 60), headerOnly, 1, emptyExcludes, 5));

    // 反过来，空矩形当可拖矩形也不该命中任何点。
    const RECT emptyDrags[] = { { 0, 0, 0, 0 } };
    CHECK_FALSE(IsPointInDragZone(MakePt(0, 0), emptyDrags, 1, NULL, 0));
}

TEST_CASE("DZ9 退化输入：没有可拖矩形就哪里都不能拖", "[dragzone]")
{
    // 界面尚未构建（CollectDragZoneRects 返回 false）时的两种表现形式。
    CHECK_FALSE(IsPointInDragZone(MakePt(150, 658), NULL, 0, kExcludeRects, kExcludeCount));
    CHECK_FALSE(IsPointInDragZone(MakePt(150, 658), kDragRects, 0, kExcludeRects, kExcludeCount));
}

TEST_CASE("DZ10 退化输入：没有排除矩形则可拖区整块可拖", "[dragzone]")
{
    CHECK(IsPointInDragZone(MakePt(25, 658), kDragRects, kDragCount, NULL, 0));
    CHECK(IsPointInDragZone(MakePt(25, 658), kDragRects, kDragCount, kExcludeRects, 0));

    // 但可拖区之外依然不可拖 —— 排除矩形为空不等于"整个窗口可拖"。
    CHECK_FALSE(IsPointInDragZone(MakePt(160, 400), kDragRects, kDragCount, NULL, 0));
}
