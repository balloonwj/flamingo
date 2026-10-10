/**
 *  主面板搜索框「最近搜索」列表的单元测试（Suite RS），RecentSearchListTests.cpp
 *
 *  覆盖 balloonsearch::RecentSearchList：
 *    RS1  新关键字排在最前；去掉首尾空白（含全角空格）；空串不记；
 *    RS2  重复的关键字只留一份并挪到最前，英文字母不区分大小写、换成这次的写法；
 *    RS3  超过容量时丢掉最旧的；过长的关键字截断；
 *    RS4  序列化与反序列化往返一致；配置被改坏（空段、重复、超量）时整理成合法列表；
 *    RS5  删除与清空。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/RecentSearchList.h"

#include <string>

using balloonsearch::RecentSearchList;

TEST_CASE("RS1 新关键字排在最前，去掉首尾空白，空串不记", "[globalsearch]")
{
    RecentSearchList list;
    CHECK(list.Add(L"设计稿"));
    CHECK(list.Add(L"  周报 \t"));
    CHECK(list.Add(L"\x3000合同\x3000"));   //全角空格
    CHECK_FALSE(list.Add(L"   "));
    CHECK_FALSE(list.Add(L""));
    REQUIRE(list.Items().size() == 3);
    CHECK(list.Items()[0] == L"合同");
    CHECK(list.Items()[1] == L"周报");
    CHECK(list.Items()[2] == L"设计稿");

    //已经排在最前且写法相同：不算变化
    CHECK_FALSE(list.Add(L"合同"));
}

TEST_CASE("RS2 重复的关键字挪到最前并换成这次的写法", "[globalsearch]")
{
    RecentSearchList list;
    list.Add(L"Report");
    list.Add(L"设计稿");
    list.Add(L"周报");
    CHECK(list.Add(L"report"));
    REQUIRE(list.Items().size() == 3);
    CHECK(list.Items()[0] == L"report");
    CHECK(list.Items()[1] == L"周报");
    CHECK(list.Items()[2] == L"设计稿");
}

TEST_CASE("RS3 超过容量丢掉最旧的，过长的截断", "[globalsearch]")
{
    RecentSearchList list;
    for (size_t i = 0; i < balloonsearch::kRecentSearchCapacity + 3; ++i)
    {
        list.Add(L"kw" + std::to_wstring(i));
    }
    REQUIRE(list.Items().size() == balloonsearch::kRecentSearchCapacity);
    CHECK(list.Items()[0] == L"kw" + std::to_wstring(balloonsearch::kRecentSearchCapacity + 2));
    CHECK(list.Items().back() == L"kw3");

    RecentSearchList longOne;
    longOne.Add(std::wstring(balloonsearch::kRecentSearchMaxChars + 20, L'a'));
    REQUIRE(longOne.Items().size() == 1);
    CHECK(longOne.Items()[0].size() == balloonsearch::kRecentSearchMaxChars);
}

TEST_CASE("RS4 序列化往返一致，改坏的配置被整理", "[globalsearch]")
{
    RecentSearchList list;
    list.Add(L"设计稿");
    list.Add(L"周报");
    list.Add(L"合同");
    const std::wstring text = list.Serialize();

    RecentSearchList back;
    back.Deserialize(text);
    REQUIRE(back.Items().size() == 3);
    CHECK(back.Items()[0] == L"合同");
    CHECK(back.Items()[1] == L"周报");
    CHECK(back.Items()[2] == L"设计稿");
    CHECK(back.Serialize() == text);

    //空段与重复：整理掉，保持「靠前的更近」
    const wchar_t sep = balloonsearch::kRecentSearchSeparator;
    std::wstring messy = L"a";
    messy += sep;
    messy += sep;
    messy += L"b";
    messy += sep;
    messy += L"A";
    RecentSearchList tidy;
    tidy.Deserialize(messy);
    REQUIRE(tidy.Items().size() == 2);
    CHECK(tidy.Items()[0] == L"a");
    CHECK(tidy.Items()[1] == L"b");

    //空串
    RecentSearchList none;
    none.Deserialize(L"");
    CHECK(none.Items().empty());
    CHECK(none.Serialize().empty());
}

TEST_CASE("RS5 删除与清空", "[globalsearch]")
{
    RecentSearchList list;
    list.Add(L"Alpha");
    list.Add(L"设计稿");
    CHECK(list.Remove(L"alpha"));
    CHECK_FALSE(list.Remove(L"不存在"));
    REQUIRE(list.Items().size() == 1);
    list.Clear();
    CHECK(list.Items().empty());
}
