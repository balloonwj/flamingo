/**
 *  AutoReplySettingsRowsTests.cpp
 *
 *  系统设置「聊天」页「离开 / 忙碌时自动回复」输入框行的测试（ARS2，与用户确认过的用例，2026-10-03）：
 *  两组各有一个开关与一个输入框；关掉其中一个开关时只有它的输入框置灰，另一组不受影响；输入框限长
 *  100 个字，超出的文字存进配置前截断。按用户后来的要求，输入框单独占一行、多行显示，ARS2 一并核对
 *  多行与自动换行；ARS3 核对输入框那一行的排版（占满宽度、定高、下方留间距）。
 *
 *  只构造控件树，不建设置窗口。开关行在设置窗口里由 AddSwitchRow 生成，这里直接建两个 DuiSwitch
 *  代替，与输入框行一起放进同一个容器。用 Catch2 v3（amalgamated）作框架。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonSettings/AutoReplySettingsRows.h"
#include "../UI/BalloonMain/AutoReplyPolicy.h"   // kAutoReplyMaxChars / 默认文字
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"

using namespace balloonsettings;

namespace {

// 两组控件的 id，取值与设置窗口无关，只要互不相同
const UINT kAwaySwitchId = 311;
const UINT kAwayEditId   = 312;
const UINT kBusySwitchId = 313;
const UINT kBusyEditId   = 314;

// 开关行高（像素），与设置页 kRowH 同值
const int kRowH = 52;

// 「聊天」页内容区宽度（像素），输入框行按它排版
const int kPageW = 534;

// 离开那组的初始文字
const wchar_t* const kAwayText = L"我在开会，一小时后回复。";

AutoReplyEditSpec MakeSpec(UINT editId, bool on, const wchar_t* text, const wchar_t* placeholder)
{
    AutoReplyEditSpec spec;
    spec.m_editId = editId;
    spec.m_on = on;
    spec.m_text = text;
    spec.m_placeholder = placeholder;
    return spec;
}

// 建一个开关，代替设置窗口 AddSwitchRow 生成的那一行
std::unique_ptr<balloonwjui::DuiSwitch> MakeSwitch(UINT id, bool on)
{
    std::unique_ptr<balloonwjui::DuiSwitch> sw(new balloonwjui::DuiSwitch());
    sw->SetCtrlId(id);
    sw->SetChecked(on, /*animated*/false, /*notify*/false);
    return sw;
}

//离开那组打开、忙碌那组关闭，按设置页的顺序竖着放：开关、输入框、开关、输入框
std::unique_ptr<balloonwjui::DuiVBox> BuildBothGroups()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->AddChild(MakeSwitch(kAwaySwitchId, true), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    root->AddChild(BuildAutoReplyEditRow(MakeSpec(kAwayEditId, true, kAwayText,
                                                  balloonmain::kDefaultAwayReplyText)),
                   balloonwjui::DuiLayout::Hint().Fixed(kAutoReplyEditRowH));
    root->AddChild(MakeSwitch(kBusySwitchId, false), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    root->AddChild(BuildAutoReplyEditRow(MakeSpec(kBusyEditId, false, L"",
                                                  balloonmain::kDefaultBusyReplyText)),
                   balloonwjui::DuiLayout::Hint().Fixed(kAutoReplyEditRowH));
    return root;
}

} // anonymous namespace

TEST_CASE("ARS2 两组开关各带一个多行输入框，关掉哪个开关只置灰它的输入框", "[autoreply][autoreplysettings]")
{
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildBothGroups();

    balloonwjui::DuiSwitch* awaySw = dynamic_cast<balloonwjui::DuiSwitch*>(root->FindCtrlById(kAwaySwitchId));
    balloonwjui::DuiSwitch* busySw = dynamic_cast<balloonwjui::DuiSwitch*>(root->FindCtrlById(kBusySwitchId));
    balloonwjui::DuiEdit* awayEdit = dynamic_cast<balloonwjui::DuiEdit*>(root->FindCtrlById(kAwayEditId));
    balloonwjui::DuiEdit* busyEdit = dynamic_cast<balloonwjui::DuiEdit*>(root->FindCtrlById(kBusyEditId));
    REQUIRE(awaySw != NULL);
    REQUIRE(busySw != NULL);
    REQUIRE(awayEdit != NULL);
    REQUIRE(busyEdit != NULL);

    //多行、自动换行
    CHECK(awayEdit->IsMultiLine());
    CHECK(awayEdit->IsWordWrap());
    CHECK(busyEdit->IsMultiLine());
    CHECK(busyEdit->IsWordWrap());

    //初始状态跟随配置：离开打开、可编辑，忙碌关闭、置灰
    CHECK(awayEdit->IsEnabled());
    CHECK_FALSE(busyEdit->IsEnabled());

    //输入框里是配置里的原文；为空的那组显示默认文字作占位提示
    CHECK(std::wstring((LPCTSTR)awayEdit->GetText()) == kAwayText);
    CHECK(busyEdit->GetText().IsEmpty());
    CHECK(std::wstring((LPCTSTR)busyEdit->GetPlaceholder()) == balloonmain::kDefaultBusyReplyText);

    //限长 100 个字
    CHECK(awayEdit->GetMaxLength() == (int)balloonmain::kAutoReplyMaxChars);
    CHECK(busyEdit->GetMaxLength() == (int)balloonmain::kAutoReplyMaxChars);

    //打开忙碌开关：忙碌的输入框可编辑，离开那组不受影响
    busySw->SetChecked(true, /*animated*/false, /*notify*/false);
    ApplyAutoReplyInputEnabled(root.get(), kBusySwitchId, kBusyEditId);
    CHECK(busyEdit->IsEnabled());
    CHECK(awayEdit->IsEnabled());

    //关掉离开开关：只有离开的输入框置灰
    awaySw->SetChecked(false, /*animated*/false, /*notify*/false);
    ApplyAutoReplyInputEnabled(root.get(), kAwaySwitchId, kAwayEditId);
    CHECK_FALSE(awayEdit->IsEnabled());
    CHECK(busyEdit->IsEnabled());

    //找不到控件或根为空时什么都不做，不崩溃
    ApplyAutoReplyInputEnabled(NULL, kAwaySwitchId, kAwayEditId);
    ApplyAutoReplyInputEnabled(root.get(), kAwaySwitchId, kBusySwitchId + kBusyEditId);

    //超过 100 个字的文字存进配置前截断，没超过的原样保留（去空白在发送时才做）；多行文字的换行保留
    const std::wstring longText(balloonmain::kAutoReplyMaxChars + 20, L'字');
    CHECK(AutoReplyTextForConfig(longText.c_str()).size() == balloonmain::kAutoReplyMaxChars);
    CHECK(AutoReplyTextForConfig(L"  稍后回复  ") == L"  稍后回复  ");
    CHECK(AutoReplyTextForConfig(L"第一行\r\n第二行") == L"第一行\r\n第二行");
    CHECK(AutoReplyTextForConfig(NULL).empty());
}

TEST_CASE("ARS3 输入框单独占一行：占满宽度、定高，下方留间距", "[autoreply][autoreplysettings]")
{
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildBothGroups();
    const int total = kRowH * 2 + kAutoReplyEditRowH * 2;
    root->ForceLayout(RECT{ 0, 0, kPageW, total });

    const RECT rcAwaySw = root->FindCtrlById(kAwaySwitchId)->GetRect();
    const RECT rcAwayEdit = root->FindCtrlById(kAwayEditId)->GetRect();
    const RECT rcBusySw = root->FindCtrlById(kBusySwitchId)->GetRect();
    const RECT rcBusyEdit = root->FindCtrlById(kBusyEditId)->GetRect();

    //输入框紧接在开关行下方，占满整行宽度
    CHECK(rcAwayEdit.top == rcAwaySw.bottom);
    CHECK(rcAwayEdit.left == 0);
    CHECK(rcAwayEdit.right == kPageW);
    CHECK(rcAwayEdit.bottom - rcAwayEdit.top == kAutoReplyEditH);

    //输入框下方留 kAutoReplyEditBottomGap，下一组的开关行从那之后开始
    CHECK(rcBusySw.top == rcAwayEdit.bottom + kAutoReplyEditBottomGap);
    CHECK(rcBusyEdit.top == rcBusySw.bottom);
    CHECK(rcBusyEdit.right - rcBusyEdit.left == kPageW);
    CHECK(rcBusyEdit.bottom - rcBusyEdit.top == kAutoReplyEditH);

    //整行高度 = 输入框高 + 下方间距
    CHECK(kAutoReplyEditRowH == kAutoReplyEditH + kAutoReplyEditBottomGap);
}
