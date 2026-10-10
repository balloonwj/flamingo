/**
 *  主面板底部右侧主操作按钮的取值规则单元测试（Suite MBA，标签 [mainpanel][bottomaction]），MainPanelBottomActionTests.cpp
 *
 *  2026-10-03 起，非群页签在文件共享可用时由「查找」改为「文件共享」。覆盖：
 *    - MBA1 「群」页签一律是「建群」，与文件共享是否可用无关；
 *    - MBA2 其余页签：文件共享可用时是「文件共享」，不可用时仍是「查找」；不认识的动作按「查找」显示。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonMain/MainPanelBottomAction.h"

namespace {

//不认识的动作值
const int kUnknownAction = 99;

} // namespace

TEST_CASE("MBA1 群页签是建群", "[mainpanel][bottomaction]")
{
    CHECK(balloonmain::BottomActionFor(true, true) == balloonmain::kBottomActionCreateGroup);
    CHECK(balloonmain::BottomActionFor(true, false) == balloonmain::kBottomActionCreateGroup);
    CHECK(std::wstring(balloonmain::BottomActionText(balloonmain::kBottomActionCreateGroup)) == L"建群");
}

TEST_CASE("MBA2 其余页签按文件共享是否可用取文件共享或查找", "[mainpanel][bottomaction]")
{
    CHECK(balloonmain::BottomActionFor(false, true) == balloonmain::kBottomActionFileShare);
    CHECK(std::wstring(balloonmain::BottomActionText(balloonmain::kBottomActionFileShare)) == L"文件共享");
    CHECK(balloonmain::BottomActionFor(false, false) == balloonmain::kBottomActionFind);
    CHECK(std::wstring(balloonmain::BottomActionText(balloonmain::kBottomActionFind)) == L"查找");
    CHECK(std::wstring(balloonmain::BottomActionText(kUnknownAction)) == L"查找");
}
