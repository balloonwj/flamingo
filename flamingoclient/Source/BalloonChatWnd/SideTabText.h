/**
 *  SideTabText.h
 *
 *  群聊窗右侧边栏 Tab 的文案拼接（纯逻辑）—— 把「标题 + 数量」拼成 Tab 上
 *  显示的那行字，例如 "成员 12" / "群文件 8"。数量尚未取到时只显示标题
 *  （"成员"），使「还没加载出来」与「确实是 0」在界面上可区分。
 *
 *  单独成一个模块而不是写在窗口类里，是为了让这段呈现规则能脱离窗口与网络
 *  单独跑测试，见 flamingoclient/Source/Tests/SideTabTextTests.cpp。
 *
 *  本模块是纯逻辑：只依赖 std::wstring，不触及 UI / 网络 / ATL。
 *
 *  典型用法：
 *      tab->SetText(MakeSideTabText(L"成员", (int)m_memberInfo.size()).c_str());
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <string>

namespace balloonchatwnd {

// 数量未知的哨兵值：传给 MakeSideTabText 时 Tab 上只显示标题、不带数字。
// 用于「群成员 / 群文件尚未从服务端取回」的阶段。
const int kSideTabCountUnknown = -1;

/**
 *  拼接边栏 Tab 的显示文案。
 *    title： Tab 标题（如 L"成员" / L"群文件"），不含数量；传 NULL 视为空串。
 *    count： 数量。>= 0 时拼成 "<title> <count>"（含 0，即 "群文件 0"）；
 *            为负（见 kSideTabCountUnknown）表示数量未知，只返回标题本身。
 *  返回：拼好的文案。title 为空且数量已知时返回纯数字文本。
 */
std::wstring MakeSideTabText(const wchar_t* title, int count);

} // namespace balloonchatwnd
