/**
 *  消息记录窗的键盘快捷键判定的实现，HistoryWindowKeys.cpp
 *  各按键的口径见 HistoryWindowKeys.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "HistoryWindowKeys.h"

namespace balloonhistory {

int DecideHistoryKey(UINT vk, bool ctrlDown, bool altDown, bool searchFocused, bool searchHasText)
{
    //按住 Alt 的组合键留给系统（Alt+F4、系统菜单等）
    if (altDown)
    {
        return kHistoryKeyNone;
    }

    if (ctrlDown)
    {
        switch (vk)
        {
        //Ctrl+F：到搜索框里去
        case 'F':
            return kHistoryKeyFocusSearch;

        //Ctrl+Home：第一页。焦点在搜索框时是输入框的"光标移到开头"，不抢
        case VK_HOME:
            return searchFocused ? kHistoryKeyNone : kHistoryKeyFirstPage;

        //Ctrl+End：最后一页，同上
        case VK_END:
            return searchFocused ? kHistoryKeyNone : kHistoryKeyLastPage;

        //其余 Ctrl 组合（含 Ctrl+C，由窗口自己判断）不处理
        default:
            return kHistoryKeyNone;
        }
    }

    switch (vk)
    {
    //Esc：搜索框里有字时先清空搜索，再按一次才关窗；焦点不在搜索框时直接关窗
    case VK_ESCAPE:
        return (searchFocused && searchHasText) ? kHistoryKeyClearSearch : kHistoryKeyClose;

    //PageUp：上一页。单行搜索框里这个键本来没有用处，焦点在搜索框时同样翻页
    case VK_PRIOR:
        return kHistoryKeyPrevPage;

    //PageDown：下一页，同上
    case VK_NEXT:
        return kHistoryKeyNextPage;

    //回车：只在搜索框里表示"立即搜索"，别处不处理
    case VK_RETURN:
        return searchFocused ? kHistoryKeySearchNow : kHistoryKeyNone;

    //没有列出的键交还给获得焦点的控件
    default:
        return kHistoryKeyNone;
    }
}

} // namespace balloonhistory
