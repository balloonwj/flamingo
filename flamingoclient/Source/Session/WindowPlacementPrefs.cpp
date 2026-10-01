/**
 *  WindowPlacementPrefs.cpp —— 窗口位置的读写与恢复计算，说明见头文件。
 *  balloonwj@qq.com   2026-10-01
 */

#include "WindowPlacementPrefs.h"

#include <tchar.h>
#include <atlstr.h>   // CString：拼键名

#include "../Common/Util/IniFile.h"

namespace winplace {

namespace {

//配置里缺这个键时的读出值。宽高读出它就说明没有保存过，见 LoadWindowPlacementPrefs
const long kMissingValue = 0;

CString KeyName(LPCTSTR keyPrefix, LPCTSTR suffix)
{
    return CString(keyPrefix) + suffix;
}

//读一个可能为负数的整数。CIniFile::ReadInt 底层是 GetPrivateProfileInt，返回类型是 UINT：
//写入 -1500 后读出的是它按 32 位无符号数解释的值 4294965796，须转回有符号数才是 -1500
//（2026-10-01 实测）。副屏在主屏左边或上边时窗口坐标就是负数
long ReadSignedInt(CIniFile& ini, LPCTSTR filePath, LPCTSTR section, const CString& key)
{
    return (long)(int)ini.ReadInt(section, key, kMissingValue, filePath);
}

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

void LoadWindowPlacementPrefs(CIniFile& ini, LPCTSTR filePath, LPCTSTR section, LPCTSTR keyPrefix,
                              WindowPlacementPrefs& out)
{
    out = WindowPlacementPrefs();
    const long x = ReadSignedInt(ini, filePath, section, KeyName(keyPrefix, _T("X")));
    const long y = ReadSignedInt(ini, filePath, section, KeyName(keyPrefix, _T("Y")));
    const long w = ReadSignedInt(ini, filePath, section, KeyName(keyPrefix, _T("Width")));
    const long h = ReadSignedInt(ini, filePath, section, KeyName(keyPrefix, _T("Height")));
    //宽高缺失或不为正数：没有保存过，或者配置被手工改坏了，都按没有保存过处理
    if (w <= 0 || h <= 0)
    {
        return;
    }
    out.m_hasSaved = true;
    out.m_rect.left = x;
    out.m_rect.top = y;
    out.m_rect.right = x + w;
    out.m_rect.bottom = y + h;
    out.m_maximized = (ReadSignedInt(ini, filePath, section, KeyName(keyPrefix, _T("Maximized"))) != 0);
}

void SaveWindowPlacementPrefs(CIniFile& ini, LPCTSTR filePath, LPCTSTR section, LPCTSTR keyPrefix,
                              const WindowPlacementPrefs& prefs)
{
    if (!prefs.m_hasSaved)
    {
        return;
    }
    ini.WriteInt(section, KeyName(keyPrefix, _T("X")), prefs.m_rect.left, filePath);
    ini.WriteInt(section, KeyName(keyPrefix, _T("Y")), prefs.m_rect.top, filePath);
    ini.WriteInt(section, KeyName(keyPrefix, _T("Width")), Width(prefs.m_rect), filePath);
    ini.WriteInt(section, KeyName(keyPrefix, _T("Height")), Height(prefs.m_rect), filePath);
    ini.WriteInt(section, KeyName(keyPrefix, _T("Maximized")), prefs.m_maximized ? 1 : 0, filePath);
}

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
