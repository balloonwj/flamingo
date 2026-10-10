/**
 *  主面板快捷键判定的单测（标签 [panelkey]），PanelShortcutPolicyTests.cpp
 *
 *  背景：系统设置「快捷键」页列出的「快速搜索 Ctrl + K」没有实现，搜索框里的 Esc 只收起下拉。
 *    PKEY1 Ctrl+K 把光标移到搜索框；带 Shift / Alt 或不带 Ctrl 时不处理；
 *    PKEY2 Esc：下拉开着先收起（不论有没有文字）；下拉收起、有文字时清空；都没有时不处理（不隐藏主面板）；
 *          带 Ctrl / Alt 的 Esc 不处理；
 *    PKEY3 其余按键一律不处理；
 *    PKEY4 源文件守卫：主面板在 WM_KEYDOWN 处理里调用了判定函数，并对四种动作都有分支。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-10
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>

#include "../UI/BalloonMain/PanelShortcutPolicy.h"

using namespace balloonmain;

namespace {

// 构造一次按键输入
PanelShortcutInput MakeKey(UINT vk, bool ctrl, bool shift, bool alt, bool dropdownShown, bool hasText)
{
    PanelShortcutInput in;
    in.m_vk = vk;
    in.m_ctrl = ctrl;
    in.m_shift = shift;
    in.m_alt = alt;
    in.m_dropdownShown = dropdownShown;
    in.m_searchHasText = hasText;
    return in;
}

// 读整个文件；读不到时返回空串
std::string PkeyReadFile(const std::string& path)
{
    std::string out;
    FILE* fp = NULL;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || fp == NULL)
        return out;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        out.append(buf, n);
    fclose(fp);
    return out;
}

// 向上回溯查找客户端根目录时允许的最大层级数（测试程序在 <运行目录>\Tests，回到根要 2 层）
const int kPkeyMaxParentDepth = 10;

// 推出 Source 目录；推不出时返回空串。
// 先看 __FILE__（编译器给的是绝对路径时直接可用），否则从测试程序所在目录逐级向上找 FlamingoClient.sln
std::string PkeySourceRoot()
{
    const std::string file = __FILE__;
    const std::string marker = "\\Tests\\";
    const size_t pos = file.rfind(marker);
    if (pos != std::string::npos && file.find(":\\") != std::string::npos)
        return file.substr(0, pos);

    char exePath[MAX_PATH] = { 0 };
    if (::GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0)
        return std::string();
    std::string dir = exePath;
    for (int i = 0; i < kPkeyMaxParentDepth; ++i)
    {
        const size_t slash = dir.find_last_of('\\');
        if (slash == std::string::npos)
            break;
        dir = dir.substr(0, slash);
        if (::GetFileAttributesA((dir + "\\FlamingoClient.sln").c_str()) != INVALID_FILE_ATTRIBUTES)
            return dir + "\\Source";
    }
    return std::string();
}

} // namespace

TEST_CASE("PKEY1 Ctrl+K 把光标移到搜索框，带 Shift / Alt 或不带 Ctrl 时不处理", "[panelkey]")
{
    CHECK(DecidePanelShortcut(MakeKey('K', true, false, false, false, false)) == kPanelKeyFocusSearch);
    //搜索框里已有文字、下拉开着时同样移到搜索框
    CHECK(DecidePanelShortcut(MakeKey('K', true, false, false, true, true)) == kPanelKeyFocusSearch);
    CHECK(DecidePanelShortcut(MakeKey('K', true, true, false, false, false)) == kPanelKeyNone);
    CHECK(DecidePanelShortcut(MakeKey('K', true, false, true, false, false)) == kPanelKeyNone);
    CHECK(DecidePanelShortcut(MakeKey('K', false, false, false, false, false)) == kPanelKeyNone);
}

TEST_CASE("PKEY2 Esc 先收起下拉、再清空文字，都没有时不处理", "[panelkey]")
{
    CHECK(DecidePanelShortcut(MakeKey(VK_ESCAPE, false, false, false, true, true)) == kPanelKeyCloseDropdown);
    CHECK(DecidePanelShortcut(MakeKey(VK_ESCAPE, false, false, false, true, false)) == kPanelKeyCloseDropdown);
    CHECK(DecidePanelShortcut(MakeKey(VK_ESCAPE, false, false, false, false, true)) == kPanelKeyClearSearch);
    //没有下拉、没有文字：不处理，Esc 不隐藏主面板
    CHECK(DecidePanelShortcut(MakeKey(VK_ESCAPE, false, false, false, false, false)) == kPanelKeyNone);
    //Shift+Esc 按普通 Esc 处理；Ctrl+Esc、Alt+Esc 是系统组合键，不处理
    CHECK(DecidePanelShortcut(MakeKey(VK_ESCAPE, false, true, false, false, true)) == kPanelKeyClearSearch);
    CHECK(DecidePanelShortcut(MakeKey(VK_ESCAPE, true, false, false, true, true)) == kPanelKeyNone);
    CHECK(DecidePanelShortcut(MakeKey(VK_ESCAPE, false, false, true, true, true)) == kPanelKeyNone);
}

TEST_CASE("PKEY3 其余按键一律不处理", "[panelkey]")
{
    CHECK(DecidePanelShortcut(MakeKey('J', true, false, false, true, true)) == kPanelKeyNone);
    CHECK(DecidePanelShortcut(MakeKey(VK_RETURN, false, false, false, true, true)) == kPanelKeyNone);
    CHECK(DecidePanelShortcut(MakeKey(VK_DOWN, false, false, false, true, true)) == kPanelKeyNone);
}

TEST_CASE("PKEY4 主面板在 WM_KEYDOWN 处理里调用了快捷键判定，四种动作都有分支", "[panelkey]")
{
    const std::string root = PkeySourceRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = PkeyReadFile(root + "\\UI\\BalloonMain\\BalloonMainPanel.cpp");
    REQUIRE_FALSE(source.empty());
    const size_t begin = source.find("LRESULT CBalloonMainPanel::OnSearchNavKeyDown(");
    REQUIRE(begin != std::string::npos);
    const size_t end = source.find("\nLRESULT CBalloonMainPanel::", begin + 1);
    const std::string body = source.substr(begin, (end == std::string::npos) ? std::string::npos : end - begin);
    CHECK(body.find("DecidePanelShortcut(") != std::string::npos);
    CHECK(body.find("kPanelKeyFocusSearch") != std::string::npos);
    CHECK(body.find("kPanelKeyCloseDropdown") != std::string::npos);
    CHECK(body.find("kPanelKeyClearSearch") != std::string::npos);
}
