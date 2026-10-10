/**
 *  主面板搜索下拉键盘操作的单元测试（Suite KS），SearchKeyboardTests.cpp
 *
 *  覆盖 balloonsearch::SearchSelection 与 NextAvailableTab：
 *    KS1  下方向键从「未选中」选到第一行，逐行往下，到末行不再动；
 *    KS2  上方向键逐行往上，到第一行再按回到「未选中」，未选中时不动；
 *    KS3  回车打开的行：有选中就是选中行，没有选中打开第一行，没有结果返回 -1；结果重建后取消选中；
 *    KS4  Ctrl+Tab 循环切换标签：越过末尾回到开头、反向同理、跳过置灰的标签、只剩当前一个可用时不动。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/SearchKeyboard.h"

using balloonsearch::SearchSelection;
using balloonsearch::NextAvailableTab;

TEST_CASE("KS1 下方向键逐行往下，到末行不再动", "[globalsearch]")
{
    SearchSelection s;
    s.Reset(3);
    CHECK(s.Current() == -1);
    CHECK(s.MoveDown());
    CHECK(s.Current() == 0);
    CHECK(s.MoveDown());
    CHECK(s.MoveDown());
    CHECK(s.Current() == 2);
    CHECK_FALSE(s.MoveDown());
    CHECK(s.Current() == 2);

    //没有结果时按什么都不动
    SearchSelection empty;
    empty.Reset(0);
    CHECK_FALSE(empty.MoveDown());
    CHECK(empty.Current() == -1);
}

TEST_CASE("KS2 上方向键逐行往上，第一行再按回到未选中", "[globalsearch]")
{
    SearchSelection s;
    s.Reset(3);
    CHECK_FALSE(s.MoveUp());   //未选中时不动
    s.MoveDown();
    s.MoveDown();
    CHECK(s.Current() == 1);
    CHECK(s.MoveUp());
    CHECK(s.Current() == 0);
    CHECK(s.MoveUp());
    CHECK(s.Current() == -1);
    CHECK_FALSE(s.MoveUp());
}

TEST_CASE("KS3 回车打开选中行，没有选中打开第一行，重建后取消选中", "[globalsearch]")
{
    SearchSelection s;
    s.Reset(4);
    CHECK(s.ActivationIndex() == 0);
    s.MoveDown();
    s.MoveDown();
    s.MoveDown();
    CHECK(s.ActivationIndex() == 2);

    //结果重建（行数变了）：选中取消，回车回到第一行
    s.Reset(2);
    CHECK(s.Current() == -1);
    CHECK(s.ActivationIndex() == 0);

    s.Reset(0);
    CHECK(s.ActivationIndex() == -1);
    s.Reset(-5);   //负数按 0 处理
    CHECK(s.Count() == 0);
}

TEST_CASE("KS4 Ctrl+Tab 循环切换标签并跳过置灰的", "[globalsearch]")
{
    const int kTabs = 5;
    const bool allOn[kTabs] = { true, true, true, true, true };
    CHECK(NextAvailableTab(0, 1, allOn, kTabs) == 1);
    CHECK(NextAvailableTab(4, 1, allOn, kTabs) == 0);    //越过末尾回到开头
    CHECK(NextAvailableTab(0, -1, allOn, kTabs) == 4);   //反向越过开头回到末尾
    CHECK(NextAvailableTab(2, -1, allOn, kTabs) == 1);

    //「消息」「文件」（下标 3、4）置灰
    const bool noDelayed[kTabs] = { true, true, true, false, false };
    CHECK(NextAvailableTab(2, 1, noDelayed, kTabs) == 0);
    CHECK(NextAvailableTab(0, -1, noDelayed, kTabs) == 2);

    //只剩当前一个可用：留在原地
    const bool onlyFirst[kTabs] = { true, false, false, false, false };
    CHECK(NextAvailableTab(0, 1, onlyFirst, kTabs) == 0);

    //available 为空视为都可用；step 不是 ±1 时按 +1；count 非正时返回 current
    CHECK(NextAvailableTab(1, 1, nullptr, kTabs) == 2);
    CHECK(NextAvailableTab(1, 7, allOn, kTabs) == 2);
    CHECK(NextAvailableTab(3, 1, allOn, 0) == 3);
}
