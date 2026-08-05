/**
 *  PanelDragZone.cpp
 *
 *  "按住窗口空白处也能拖动整窗"命中判定的实现。纯坐标运算，与 FlamingoUnit
 *  测试工程共用、不 include stdafx.h，PCH 设 NotUsing。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "PanelDragZone.h"

namespace balloonmain {

bool IsPointInDragZone(POINT pt,
                       const RECT* dragRects, int dragCount,
                       const RECT* excludeRects, int excludeCount)
{
    // 没给可拖矩形 = 界面尚未构建 / 调用方没找到容器控件，判为不可拖，
    // 窗口侧据此原样返回基类算出的 HTCLIENT。
    if (dragRects == NULL || dragCount <= 0)
    {
        return false;
    }

    bool bInDragArea = false;
    for (int i = 0; i < dragCount; ++i)
    {
        if (::PtInRect(&dragRects[i], pt) != FALSE)
        {
            bInDragArea = true;
            break;
        }
    }

    if (!bInDragArea)
    {
        return false;
    }

    // 落在可拖区内，还要躲开里面那些可点击的子控件（按钮 / 头像 / 签名…），
    // 否则拖动会把它们的点击整个吃掉。
    if (excludeRects == NULL || excludeCount <= 0)
    {
        return true;
    }

    for (int i = 0; i < excludeCount; ++i)
    {
        if (::PtInRect(&excludeRects[i], pt) != FALSE)
        {
            return false;
        }
    }

    return true;
}

} // namespace balloonmain
