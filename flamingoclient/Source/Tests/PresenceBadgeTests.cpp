/**
 *  PresenceBadgeTests.cpp
 *
 *  在线状态徽标（PresenceBadge）的测试。本次改版的核心主张是"四个状态靠<u>形状</u>
 *  就能区分、不依赖颜色"，故这里的重头戏是两条不变量：
 *    · 四种 presence 映射到四种<u>两两不同</u>的字形；
 *    · 一切越界取值都归到"离线"，且与 NormalizePresenceForMenu /
 *      GetPresenceVisual 的兜底口径一致（三者一旦分叉，就会出现"胶囊说离线、
 *      图标却画成别的状态"）。
 *  另外覆盖改版后的配色（四色两两不同、离线灰足够深）、连接态下该画哪个徽标，
 *  以及徽标位图的尺寸与像素格式。
 *
 *  用 Catch2 v3（amalgamated）作框架。位图那几条会真的建 DIB 并跑 GDI+ 绘制，
 *  但不创建窗口、不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cmath>       // pow —— 算离线灰对白底的对比度
#include <vector>

#include "../BalloonMain/PresenceBadge.h"
#include "../BalloonMain/MainPanelIcons.h"       // IconKind（断言字形取值）
#include "../BalloonMain/MainPanelStatusMenu.h"  // GetPresenceVisual / NormalizePresenceForMenu
#include "../BalloonMain/MainPanelMockData.h"    // MockPresenceFromStatus（mock 与真实数据同源）
#include "../BalloonMain/OnlineCapability.h"     // GetConnectionBadgePresence（连接态 → 徽标）
#include "../net/Protocol.h"                     // presence_type

using namespace balloonmain;

namespace {

// 本文件反复用到的四个状态，顺序与状态菜单一致（在线 / 离开 / 忙碌 / 离线）。
const int kFourPresences[4] =
{
    presence_online, presence_away, presence_busy, presence_offline
};

// 取某个颜色对白底的对比度（WCAG 相对亮度比）。离线徽标是个细环，灰太浅就看不见了，
// 故用它来守住"离线灰不能再被改浅"这条底线。
double ContrastAgainstWhite(COLORREF c)
{
    double ch[3];
    ch[0] = GetRValue(c) / 255.0;
    ch[1] = GetGValue(c) / 255.0;
    ch[2] = GetBValue(c) / 255.0;

    for (int i = 0; i < 3; ++i)
    {
        if (ch[i] <= 0.03928)
        {
            ch[i] = ch[i] / 12.92;
        }
        else
        {
            ch[i] = pow((ch[i] + 0.055) / 1.055, 2.4);
        }
    }

    const double lum = 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
    // 白底亮度为 1.0，公式 (L1 + 0.05) / (L2 + 0.05)。
    return (1.0 + 0.05) / (lum + 0.05);
}

} // anonymous namespace

// =====================================================================
// PB1-PB3. 字形映射 —— 本次改版的核心保证
// =====================================================================

// 已作废的在线状态取值：4 曾经是「隐身」，本产品不提供该功能（见 net/Protocol.h）。
// 老服务端仍可能推上来，故保留这些用例，确认它被当作越界值归到"离线"。
static const int kRetiredPresence = 4;
// 已作废的 mock 状态取值：3 曾经是「隐身」，见 BalloonMain/MainPanelMockData.h。
static const int kRetiredMockStatus = 3;

TEST_CASE("PB1 四个状态各有各的字形", "[presencebadge][shape]")
{
    CHECK(GetPresenceBadgeIconKind(presence_online)  == kIconPresenceOnline);
    CHECK(GetPresenceBadgeIconKind(presence_away)    == kIconPresenceAway);
    CHECK(GetPresenceBadgeIconKind(presence_busy)    == kIconPresenceBusy);
    CHECK(GetPresenceBadgeIconKind(presence_offline) == kIconPresenceOffline);
}

TEST_CASE("PB2 四种字形两两不同 —— 去掉颜色也能区分", "[presencebadge][shape]")
{
    // 这是改版的立足点：改版前四个状态共用一个实心圆点，只有颜色不同，于是 10px 下
    // 橙与红几乎同色、红绿色盲用户完全分不出在线与忙碌。只要有两个状态映射到同一种
    // 字形，这条保证就破了。
    for (int i = 0; i < 4; ++i)
    {
        for (int j = i + 1; j < 4; ++j)
        {
            INFO("比较 presence " << kFourPresences[i] << " 与 " << kFourPresences[j]);
            CHECK(GetPresenceBadgeIconKind(kFourPresences[i])
                  != GetPresenceBadgeIconKind(kFourPresences[j]));
        }
    }
}

TEST_CASE("PB3 越界取值都画成离线的空心环", "[presencebadge][shape]")
{
    CHECK(GetPresenceBadgeIconKind(kRetiredPresence) == kIconPresenceOffline);
    CHECK(GetPresenceBadgeIconKind(999)                == kIconPresenceOffline);
    CHECK(GetPresenceBadgeIconKind(-1)                 == kIconPresenceOffline);

    // 与另外两处兜底同口径：任取一个值，"归一后的状态"与"直接取字形"必须指向同一种字形。
    const int probes[6] = { kRetiredPresence, 999, -1, 7, 100, -42 };
    for (int i = 0; i < 6; ++i)
    {
        INFO("presence = " << probes[i]);
        CHECK(GetPresenceBadgeIconKind(probes[i])
              == GetPresenceBadgeIconKind(NormalizePresenceForMenu(probes[i])));
    }
}

// =====================================================================
// PB4-PB5. 配色 —— 颜色降为辅助通道，但仍不能自己打架
// =====================================================================

TEST_CASE("PB4 四个状态色两两不同", "[presencebadge][color]")
{
    for (int i = 0; i < 4; ++i)
    {
        for (int j = i + 1; j < 4; ++j)
        {
            INFO("比较 presence " << kFourPresences[i] << " 与 " << kFourPresences[j]);
            CHECK(GetPresenceVisual(kFourPresences[i]).dotColor
                  != GetPresenceVisual(kFourPresences[j]).dotColor);
        }
    }
}

TEST_CASE("PB5 离线灰对白底的对比度够高", "[presencebadge][color]")
{
    // 离线徽标是空心环，环细，灰一浅就在浅色行底上消失。改版前的 #9CA3AF 只有 2.3:1，
    // 现在的 #6B7280 约 4.8:1。这里守住 4.0 这条线，防止日后有人又把它调浅。
    const double contrast = ContrastAgainstWhite(GetPresenceVisual(presence_offline).dotColor);
    INFO("离线灰对白底对比度 = " << contrast);
    CHECK(contrast > 4.0);
}

// =====================================================================
// PB6. 连接态 —— 形状与颜色必须由同一套规则决定
// =====================================================================

TEST_CASE("PB6 掉线重连期间画离线环，与胶囊的灰点同口径", "[presencebadge][conn]")
{
    // 连接就绪：画用户自己选的那个状态。
    CHECK(GetConnectionBadgePresence(presence_online, kConnUiOnline) == presence_online);
    CHECK(GetConnectionBadgePresence(presence_away,   kConnUiOnline) == presence_away);
    CHECK(GetConnectionBadgePresence(presence_busy,   kConnUiOnline) == presence_busy);

    // 重连中 / 主动离线：无论用户选的是什么，都画离线环 —— 否则会出现"点是灰的、
    // 形状却是在线实心圆"。这里连"用户选的是在线"这种最容易出错的组合一起验。
    CHECK(GetConnectionBadgePresence(presence_online, kConnUiConnecting) == presence_offline);
    CHECK(GetConnectionBadgePresence(presence_busy,   kConnUiConnecting) == presence_offline);
    CHECK(GetConnectionBadgePresence(presence_online, kConnUiOffline)    == presence_offline);

    // 形状与颜色同口径：非在线连接态下，徽标颜色必须等于胶囊圆点色。
    const int states[2] = { kConnUiConnecting, kConnUiOffline };
    for (int i = 0; i < 2; ++i)
    {
        INFO("连接态 = " << states[i]);
        const int badge = GetConnectionBadgePresence(presence_online, states[i]);
        CHECK(GetPresenceVisual(badge).dotColor
              == GetConnectionPresenceVisual(presence_online, states[i]).dotColor);
    }
}

// =====================================================================
// PB7. mock 数据与真实数据同源
// =====================================================================

TEST_CASE("PB7 mock 状态换算到 presence 后与真实数据画法一致", "[presencebadge][mock]")
{
    CHECK(MockPresenceFromStatus(kMockOnline)    == presence_online);
    CHECK(MockPresenceFromStatus(kMockAway)      == presence_away);
    CHECK(MockPresenceFromStatus(kMockBusy)      == presence_busy);
    CHECK(MockPresenceFromStatus(kMockOffline)   == presence_offline);
    CHECK(MockPresenceFromStatus(kRetiredMockStatus) == presence_offline);   // 已作废取值按离线

    // mock 的状态点颜色必须与正式配色逐字相同，否则调试入口里核对过的观感到了
    // 登录后又变一副样子。
    const int mocks[5] = { kMockOnline, kMockAway, kMockBusy, kRetiredMockStatus, kMockOffline };
    for (int i = 0; i < 5; ++i)
    {
        INFO("mock status = " << mocks[i]);
        CHECK(MockStatusColor(mocks[i])
              == GetPresenceVisual(MockPresenceFromStatus(mocks[i])).dotColor);
    }
}

// =====================================================================
// PB8-PB9. 徽标位图 —— 交给 DuiTreeView::SetItemStatusIcon 的那张图
// =====================================================================

TEST_CASE("PB8 徽标位图尺寸与像素格式正确", "[presencebadge][bitmap]")
{
    const int kSizePx = 12;   // 树里状态点用的边长

    for (int i = 0; i < 4; ++i)
    {
        INFO("presence = " << kFourPresences[i]);
        HBITMAP hBmp = CreatePresenceBadgeBitmap(kFourPresences[i], kSizePx);
        REQUIRE(hBmp != NULL);

        BITMAP bm = {};
        REQUIRE(::GetObject(hBmp, sizeof(bm), &bm) != 0);
        CHECK(bm.bmWidth  == kSizePx);
        CHECK(bm.bmHeight == kSizePx);
        // 32bpp 带 alpha —— DuiTreeView 走 AlphaBlend(AC_SRC_ALPHA)，位深不对就是黑块。
        CHECK(bm.bmBitsPixel == 32);

        ::DeleteObject(hBmp);
    }
}

TEST_CASE("PB9 边长非法时返回 NULL，不崩不泄漏", "[presencebadge][bitmap]")
{
    CHECK(CreatePresenceBadgeBitmap(presence_online, 0)  == NULL);
    CHECK(CreatePresenceBadgeBitmap(presence_online, -1) == NULL);
}

TEST_CASE("PB10 徽标中心像素印证四种字形真的画出来了", "[presencebadge][bitmap]")
{
    // 前面几条验的是"映射到不同的字形编号"，这条验的是"画出来真的不一样"：万一某个
    // 绘制分支写漏了（比如忘了画白横杠），编号仍然不同，但图会退化成一个纯色圆点。
    //
    // 判据取<u>正中那一个像素</u>，四种字形在这一点上恰好各不相同：
    //   · 在线 —— 实心圆的圆心，是状态色；
    //   · 离开 —— 时针与分针都从圆心出发，圆心被白色指针盖住；
    //   · 忙碌 —— 横杠横穿圆心，同样是白色；
    //   · 离线 —— 空心环的孔正在圆心，完全透明。
    // 取 16px 时圆心恰好落在像素 (8,8)（viewBox 的 12 → 12/24*16 = 8）。
    const int kSizePx  = 16;
    const int kCenter  = kSizePx / 2;
    // 判"接近白色"的通道下限。抗锯齿会让边缘像素偏灰，但圆心是字形内部的实心区域，
    // 正常应当就是 255；留出余量只是不想让它对渲染实现过度敏感。
    const BYTE kNearWhite = 200;

    for (int i = 0; i < 4; ++i)
    {
        const int presence = kFourPresences[i];
        INFO("presence = " << presence);

        HBITMAP hBmp = CreatePresenceBadgeBitmap(presence, kSizePx);
        REQUIRE(hBmp != NULL);

        BITMAP bm = {};
        REQUIRE(::GetObject(hBmp, sizeof(bm), &bm) != 0);
        REQUIRE(bm.bmBits != NULL);          // CreateDIBSection 建的 DIB，像素可直接读

        // top-down DIB，第 0 行就是视觉首行；每像素 BGRA 四字节。
        const BYTE* p = static_cast<const BYTE*>(bm.bmBits)
                        + ((size_t)kCenter * kSizePx + kCenter) * 4;
        const BYTE b = p[0];
        const BYTE g = p[1];
        const BYTE r = p[2];
        const BYTE a = p[3];

        if (presence == presence_offline)
        {
            // 空心环的孔：完全透明（预乘 alpha 下 rgb 也应为 0）。
            CHECK(a == 0);
        }
        else if (presence == presence_online)
        {
            // 实心圆的圆心：不透明，且就是这个状态的配色。
            const COLORREF want = GetPresenceVisual(presence).dotColor;
            CHECK(a == 255);
            CHECK(r == GetRValue(want));
            CHECK(g == GetGValue(want));
            CHECK(b == GetBValue(want));
        }
        else
        {
            // 离开的指针、忙碌的横杠都压过圆心，那里应当是白的。
            CHECK(a == 255);
            CHECK(r >= kNearWhite);
            CHECK(g >= kNearWhite);
            CHECK(b >= kNearWhite);
        }

        ::DeleteObject(hBmp);
    }
}
