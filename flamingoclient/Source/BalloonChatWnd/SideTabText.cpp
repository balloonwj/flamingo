/**
 *  SideTabText.cpp
 *
 *  SideTabText.h 的实现 —— 边栏 Tab「标题 + 数量」文案拼接。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "SideTabText.h"

namespace balloonchatwnd {

// 标题与数量之间的分隔：一个半角空格（与设计稿 "成员 28" 的写法一致）。
const wchar_t kSideTabSeparator = L' ';

std::wstring MakeSideTabText(const wchar_t* title, int count)
{
    std::wstring text = (title != NULL) ? std::wstring(title) : std::wstring();

    // 数量未知：只给标题，不拼数字 —— 与「已拉取且为 0」区分开。
    if (count < 0)
    {
        return text;
    }

    // 手写十进制转换而不用 swprintf：本模块不引平台头文件，保持纯 std 依赖。
    std::wstring digits;
    if (count == 0)
    {
        digits = L"0";
    }
    else
    {
        int rest = count;
        while (rest > 0)
        {
            digits.insert(digits.begin(), (wchar_t)(L'0' + (rest % 10)));
            rest /= 10;
        }
    }

    // 标题为空时不留头部空格，直接给数字。
    if (!text.empty())
    {
        text += kSideTabSeparator;
    }
    text += digits;
    return text;
}

} // namespace balloonchatwnd
