/**
 *  flamingoAdmin —— Widgets/CommonWidgets.cpp 单元测试。
 *
 *  当前覆盖 7 条 StatTile 用例,验证"自绘 → DuiVBox + 3 DuiLabel"迁移后
 *  仍能把 SetContent 的 4 个参数正确转写到 3 个 DuiLabel 子控件,以及
 *  卡片样式 4 setter 在构造期被正确设置。
 *
 *  所有用例都在【无 host】下裸构造 StatTile —— DuiControl::Invalidate /
 *  SetVisible / DuiLabel::SetText 在 m_pHost == nullptr 时全部 no-op
 *  (DuiControl.cpp:326-332、153-164、DuiLabel.cpp:28-32 已验),且本文
 *  件【绝不调用 OnPaint】,因此不必拿 HDC,也不必 init gdiplus。
 *
 *  断言走 tile.Children()[i] + dynamic_cast<balloonwjui::DuiLabel*> 拿
 *  子控件,顺序与 StatTile 构造期 AddChild 的顺序一一对应:
 *    Children()[0] = label   (第 1 行,小灰字,恒可见)
 *    Children()[1] = value   (第 2 行,大号深色,恒可见)
 *    Children()[2] = delta   (第 3 行,按 Accent 着色,可隐藏)
 *
 *  balloonwj@qq.com   2026-05-26
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Widgets/CommonWidgets.h"
#include "Theme/AdminTheme.h"

#include "Controls/Basic/DuiLabel.h"
#include "Controls/Layout/DuiLayout.h"

namespace flamingoadmin { namespace test {

namespace {

// 从 StatTile 的 Children() 里按下标取一个 DuiLabel*;越界或 dynamic_cast
// 失败均返回 nullptr,由调用方的 FA_EXPECT_NOTNULL 兜底报错。
//   tile:被测的 StatTile;
//   index:子下标 0/1/2 = label/value/delta;
//   返回:对应的 DuiLabel*,失败返回 nullptr。
balloonwjui::DuiLabel* GetChildLabel(const ui::StatTile& tile, size_t index)
{
    const auto& children = tile.Children();
    if (index >= children.size())
    {
        return nullptr;
    }
    return dynamic_cast<balloonwjui::DuiLabel*>(children[index].get());
}

} // anonymous namespace

// 依次运行 CommonWidgets 模块的全部单元测试用例。
void RunAllCommonWidgetsTests()
{
    using ui::StatTile;
    using ui::Accent;
    using ui::AccentColor;

    // ---- 用例 1:SetContent 把 3 个文字写到对应 DuiLabel,delta 颜色按 accent 着色 ----
    BeginCase("StatTile.SetContent_StoresAllFields:三行文字 + delta 颜色正确");
    {
        StatTile tile;
        tile.SetContent(_T("企业总数"), _T("128"), _T("+5"), Accent::Ok);

        balloonwjui::DuiLabel* lblL = GetChildLabel(tile, 0);
        balloonwjui::DuiLabel* lblV = GetChildLabel(tile, 1);
        balloonwjui::DuiLabel* lblD = GetChildLabel(tile, 2);
        FA_EXPECT_NOTNULL(lblL);
        FA_EXPECT_NOTNULL(lblV);
        FA_EXPECT_NOTNULL(lblD);

        FA_EXPECT_STREQ(lblL->GetText(), _T("企业总数"));
        FA_EXPECT_STREQ(lblV->GetText(), _T("128"));
        FA_EXPECT_STREQ(lblD->GetText(), _T("+5"));
        FA_EXPECT_EQ(static_cast<unsigned long>(lblD->GetTextColor()),
                     static_cast<unsigned long>(AccentColor(Accent::Ok)));
    }

    // ---- 用例 2:delta 为空时 delta DuiLabel 应被 SetVisible(false) ----
    BeginCase("StatTile.SetContent_EmptyDelta_HidesDeltaLabel:delta 空 → 隐藏第 3 行");
    {
        StatTile tile;
        tile.SetContent(_T("企业总数"), _T("128"), _T(""), Accent::None);

        balloonwjui::DuiLabel* lblL = GetChildLabel(tile, 0);
        balloonwjui::DuiLabel* lblV = GetChildLabel(tile, 1);
        balloonwjui::DuiLabel* lblD = GetChildLabel(tile, 2);
        FA_EXPECT_NOTNULL(lblL);
        FA_EXPECT_NOTNULL(lblV);
        FA_EXPECT_NOTNULL(lblD);

        // 前两行仍可见。
        FA_EXPECT_TRUE(lblL->IsVisible());
        FA_EXPECT_TRUE(lblV->IsVisible());
        // 第 3 行隐藏。
        FA_EXPECT_FALSE(lblD->IsVisible());
    }

    // ---- 用例 3:Accent::None 走中性灰 (kInk400) ----
    BeginCase("StatTile.SetContent_AccentNone_UsesNeutralColor:None → kInk400");
    {
        StatTile tile;
        tile.SetContent(_T("L"), _T("V"), _T("D"), Accent::None);

        balloonwjui::DuiLabel* lblD = GetChildLabel(tile, 2);
        FA_EXPECT_NOTNULL(lblD);
        FA_EXPECT_EQ(static_cast<unsigned long>(lblD->GetTextColor()),
                     static_cast<unsigned long>(theme::kInk400));
    }

    // ---- 用例 4:构造期 4 个卡片样式 setter 全部就位 ----
    BeginCase("StatTile.CardStyling_OnConstruction:底色 / 圆角 / 描边色 / 描边宽度");
    {
        StatTile tile;

        // 子类化 DuiVBox 后,4 个卡片样式 getter 可直接通过基类拿到。
        FA_EXPECT_EQ(static_cast<unsigned long>(tile.GetBgColor()),
                     static_cast<unsigned long>(theme::kInk0));
        FA_EXPECT_EQ(tile.GetCornerRadius(), theme::kRadiusLg);
        FA_EXPECT_EQ(static_cast<unsigned long>(tile.GetBorderColor()),
                     static_cast<unsigned long>(theme::kInk100));
        // 浮点比较走差值;1.0f 与 theme 端 1.0 描边宽对应,差值应 < 0.001。
        const float widthDelta = tile.GetBorderWidth() - 1.0f;
        const float widthAbs   = widthDelta < 0.0f ? -widthDelta : widthDelta;
        FA_EXPECT_TRUE(widthAbs < 0.001f);
    }

    // ---- 用例 5:全部 4 个 Accent 颜色覆盖 ----
    BeginCase("StatTile.SetContent_AllAccents_UseCorrectColor:Ok/Warn/Info/None 四色");
    {
        // 4 组 (accent, expected color) 数据驱动断言。
        struct Row
        {
            Accent   m_accent;
            COLORREF m_expected;
        };
        const Row rows[] =
        {
            { Accent::Ok,   theme::kColorOk   },
            { Accent::Warn, theme::kColorWarn },
            { Accent::Info, theme::kColorInfo },
            { Accent::None, theme::kInk400    },
        };

        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        {
            StatTile tile;
            tile.SetContent(_T("L"), _T("V"), _T("D"), rows[i].m_accent);

            balloonwjui::DuiLabel* lblD = GetChildLabel(tile, 2);
            FA_EXPECT_NOTNULL(lblD);
            FA_EXPECT_EQ(static_cast<unsigned long>(lblD->GetTextColor()),
                         static_cast<unsigned long>(rows[i].m_expected));
        }
    }

    // ---- 用例 6:重复 SetContent 三行文字按第二次值刷新,不丢子控件 ----
    BeginCase("StatTile.SetContent_Repeated_RefreshesAllFields:连调 2 次取第二次值");
    {
        StatTile tile;
        tile.SetContent(_T("旧标签"), _T("0"),  _T("初始"), Accent::None);
        tile.SetContent(_T("新标签"), _T("99"), _T("+1"),   Accent::Warn);

        // Children 数量仍是 3,没有意外被 add / remove。
        FA_EXPECT_EQ(tile.Children().size(), 3);

        balloonwjui::DuiLabel* lblL = GetChildLabel(tile, 0);
        balloonwjui::DuiLabel* lblV = GetChildLabel(tile, 1);
        balloonwjui::DuiLabel* lblD = GetChildLabel(tile, 2);
        FA_EXPECT_NOTNULL(lblL);
        FA_EXPECT_NOTNULL(lblV);
        FA_EXPECT_NOTNULL(lblD);

        FA_EXPECT_STREQ(lblL->GetText(), _T("新标签"));
        FA_EXPECT_STREQ(lblV->GetText(), _T("99"));
        FA_EXPECT_STREQ(lblD->GetText(), _T("+1"));
        FA_EXPECT_EQ(static_cast<unsigned long>(lblD->GetTextColor()),
                     static_cast<unsigned long>(theme::kColorWarn));
    }

    // ---- 用例 7:delta 可见性三段切换:带 → 空(隐藏)→ 带(恢复显示) ----
    BeginCase("StatTile.SetContent_DeltaVisibilityToggle:带 → 空 → 带");
    {
        StatTile tile;
        balloonwjui::DuiLabel* lblD = GetChildLabel(tile, 2);
        FA_EXPECT_NOTNULL(lblD);

        // 第一次:非空 delta,可见。
        tile.SetContent(_T("L"), _T("V"), _T("D1"), Accent::Info);
        FA_EXPECT_TRUE(lblD->IsVisible());

        // 第二次:空 delta,隐藏。
        tile.SetContent(_T("L"), _T("V"), _T(""), Accent::None);
        FA_EXPECT_FALSE(lblD->IsVisible());

        // 第三次:再次非空 delta,自动恢复显示。
        tile.SetContent(_T("L"), _T("V"), _T("D2"), Accent::Ok);
        FA_EXPECT_TRUE(lblD->IsVisible());
        FA_EXPECT_STREQ(lblD->GetText(), _T("D2"));
    }
}

}} // namespace flamingoadmin::test
