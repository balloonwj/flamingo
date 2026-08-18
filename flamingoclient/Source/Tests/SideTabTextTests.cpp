/**
 *  群聊窗右侧边栏 Tab 文案拼接单元测试（Suite STAB），SideTabTextTests.cpp
 *
 *  覆盖：数量未知（kSideTabCountUnknown / 任意负数）时只出标题不带数字、
 *       数量为 0 时写成 "标题 0"（与「未加载」在界面上可区分）、正数与多位数
 *       的拼接、标题为空 / NULL 的边界。框架 Catch2 v3。
 *  纯逻辑，不创建窗口、不联网。
 *  balloonwj@qq.com   2026-08-03
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonChatWnd/SideTabText.h"

using namespace balloonchatwnd;

TEST_CASE("STAB1 数量未知时只显示标题，不带数字")
{
    // 群成员 / 群文件尚未从服务端取回的阶段：Tab 保持光秃秃的标题。
    CHECK(MakeSideTabText(L"成员", kSideTabCountUnknown) == std::wstring(L"成员"));
    CHECK(MakeSideTabText(L"群文件", kSideTabCountUnknown) == std::wstring(L"群文件"));

    // 哨兵值以外的任意负数同样按「未知」处理。
    CHECK(MakeSideTabText(L"群文件", -7) == std::wstring(L"群文件"));
}

TEST_CASE("STAB2 数量为 0 时写成「标题 0」，与「未加载」可区分")
{
    // 已拉取但群里确实一个文件都没有：显式写出 0，而不是退回光秃秃的标题。
    CHECK(MakeSideTabText(L"群文件", 0) == std::wstring(L"群文件 0"));
    CHECK(MakeSideTabText(L"群文件", 0) != MakeSideTabText(L"群文件", kSideTabCountUnknown));
}

TEST_CASE("STAB3 正数按「标题 数字」拼接")
{
    CHECK(MakeSideTabText(L"成员", 1) == std::wstring(L"成员 1"));
    CHECK(MakeSideTabText(L"成员", 28) == std::wstring(L"成员 28"));
    CHECK(MakeSideTabText(L"群文件", 8) == std::wstring(L"群文件 8"));
}

TEST_CASE("STAB4 多位数不丢位、不倒序")
{
    // 手写十进制转换的进位与顺序：大群（几百人）与大文件库都要拼对。
    CHECK(MakeSideTabText(L"成员", 100) == std::wstring(L"成员 100"));
    CHECK(MakeSideTabText(L"成员", 507) == std::wstring(L"成员 507"));
    CHECK(MakeSideTabText(L"群文件", 1024) == std::wstring(L"群文件 1024"));
    CHECK(MakeSideTabText(L"成员", 2147483647) == std::wstring(L"成员 2147483647"));
}

TEST_CASE("STAB5 标题为空或 NULL 的边界")
{
    // 标题为空时不留头部空格，只给数字；数量也未知时给空串。
    CHECK(MakeSideTabText(L"", 5) == std::wstring(L"5"));
    CHECK(MakeSideTabText(L"", kSideTabCountUnknown) == std::wstring(L""));
    CHECK(MakeSideTabText(NULL, 5) == std::wstring(L"5"));
    CHECK(MakeSideTabText(NULL, kSideTabCountUnknown) == std::wstring(L""));
}
