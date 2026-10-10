/**
 *  WindowPlacementResolve.cpp —— 按当前屏幕布局算出窗口恢复时应当使用的矩形（winplace::ResolveRestoredWindowRect），
 *  声明与规则说明见 WindowPlacementPrefs.h。
 *
 *  2026-10-07 从 WindowPlacementPrefs.cpp 拆出：那个文件里的读写函数依赖主程序的 CIniFile，而文件共享 DLL 也要用
 *  这个纯计算函数恢复「文件共享」窗口的位置，DLL 不允许编译包含 IniFile.h 的共用源文件（FileShareSourceGuardTests
 *  的 FDG1）。本文件只依赖 <windows.h> 与标准库，可被 Flamingo.vcxproj、FlamingoUnit.vcxproj 与
 *  FlamingoFileShare.vcxproj 共用；在两个 exe 工程里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-07
 */

#include "WindowPlacementPrefs.h"

namespace winplace {

namespace {

int Width(const RECT& rc)
{
    return rc.right - rc.left;
}

int Height(const RECT& rc)
{
    return rc.bottom - rc.top;
}

//把 value 限制在 [lo, hi]。lo 大于 hi 时（最小尺寸比工作区还大）以 hi 为准
int ClampInt(int value, int lo, int hi)
{
    if (value < lo)
    {
        value = lo;
    }
    if (value > hi)
    {
        value = hi;
    }
    return value;
}

//在工作区里按给定大小居中；大小先限制在 [最小尺寸, 工作区尺寸] 之内
RECT CenteredIn(const RECT& area, SIZE size, SIZE minSize)
{
    const int w = ClampInt(size.cx, minSize.cx, Width(area));
    const int h = ClampInt(size.cy, minSize.cy, Height(area));
    RECT rc;
    rc.left = area.left + (Width(area) - w) / 2;
    rc.top = area.top + (Height(area) - h) / 2;
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
    return rc;
}

} // namespace

RECT ResolveRestoredWindowRect(const WindowPlacementPrefs& saved, SIZE defaultSize, SIZE minSize,
                               const std::vector<RECT>& workAreas, const RECT& fallbackWorkArea)
{
    if (!saved.m_hasSaved || Width(saved.m_rect) <= 0 || Height(saved.m_rect) <= 0)
    {
        return CenteredIn(fallbackWorkArea, defaultSize, minSize);
    }

    //找与保存的矩形重叠面积最大的工作区
    const RECT* best = NULL;
    long long bestArea = 0;
    RECT bestOverlap;
    ::SetRectEmpty(&bestOverlap);
    for (size_t i = 0; i < workAreas.size(); ++i)
    {
        RECT overlap;
        if (!::IntersectRect(&overlap, &saved.m_rect, &workAreas[i]))
        {
            continue;
        }
        const long long area = (long long)Width(overlap) * Height(overlap);
        if (area > bestArea)
        {
            bestArea = area;
            best = &workAreas[i];
            bestOverlap = overlap;
        }
    }

    //重叠部分不够抓住标题栏（典型是那台显示器已经拔掉）：按没有保存过处理
    if (best == NULL || Width(bestOverlap) < kMinVisibleWidthPx || Height(bestOverlap) < kMinVisibleHeightPx)
    {
        return CenteredIn(fallbackWorkArea, defaultSize, minSize);
    }

    //尺寸限制在 [最小尺寸, 工作区尺寸]，再把位置平移到整个窗口都落在这个工作区里
    const int w = ClampInt(Width(saved.m_rect), minSize.cx, Width(*best));
    const int h = ClampInt(Height(saved.m_rect), minSize.cy, Height(*best));
    RECT rc;
    rc.left = ClampInt(saved.m_rect.left, best->left, best->right - w);
    rc.top = ClampInt(saved.m_rect.top, best->top, best->bottom - h);
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
    return rc;
}

} // namespace winplace
