/**
 *  ShareWindowPlacement.cpp —— 文件共享 DLL 窗口位置的保存与恢复，说明见头文件。
 *  balloonwj@qq.com   2026-10-07
 */

#include "stdafx.h"
#include "ShareWindowPlacement.h"

#include <vector>

namespace fileshare {

const wchar_t* const kShareCenterPlacementSection = L"ShareCenter";
const wchar_t* const kShareBrowserPlacementSection = L"ShareBrowser";

namespace {

//个人目录下存放配置文件的子目录与文件名（与 fileshare.db 同一目录）
const wchar_t* const kPlacementIniRelativePath = L"ChatLog\\fileshare_ui.ini";

//配置里缺这个键时的读出值：宽高读出它就说明没有保存过
const int kMissingValue = 0;

//整数转成十进制文本时的缓冲区长度（含符号与结尾 '\0'），32 位整数最长 11 个字符
const size_t kIntTextChars = 16;

//读一个可能为负数的整数。GetPrivateProfileInt 的返回类型是 UINT：写入 -1500 后读出的是它按 32 位无符号数
//解释的值，须转回有符号数才是 -1500（与 WindowPlacementPrefs.cpp 的 ReadSignedInt 同一处理）
int ReadSignedInt(const wchar_t* iniPath, const wchar_t* section, const wchar_t* key)
{
    return (int)::GetPrivateProfileIntW(section, key, kMissingValue, iniPath);
}

bool WriteInt(const wchar_t* iniPath, const wchar_t* section, const wchar_t* key, int value)
{
    wchar_t text[kIntTextChars] = { 0 };
    _itow_s(value, text, kIntTextChars, 10);
    return ::WritePrivateProfileStringW(section, key, text, iniPath) != FALSE;
}

//EnumDisplayMonitors 的回调：把每台显示器的工作区追加到 lParam 指向的 vector
BOOL CALLBACK CollectWorkAreaProc(HMONITOR monitor, HDC /*hdc*/, LPRECT /*rc*/, LPARAM lParam)
{
    std::vector<RECT>* areas = reinterpret_cast<std::vector<RECT>*>(lParam);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(monitor, &mi))
    {
        areas->push_back(mi.rcWork);
    }
    return TRUE;
}

} // namespace

std::wstring SharePlacementIniPath(const wchar_t* personalDir)
{
    if (personalDir == NULL || personalDir[0] == L'\0')
    {
        return std::wstring();
    }
    std::wstring path(personalDir);
    if (path[path.size() - 1] != L'\\')
    {
        path += L'\\';
    }
    path += kPlacementIniRelativePath;
    return path;
}

void LoadSharePlacement(const wchar_t* iniPath, const wchar_t* section, winplace::WindowPlacementPrefs& out)
{
    out = winplace::WindowPlacementPrefs();
    if (iniPath == NULL || iniPath[0] == L'\0' || section == NULL)
    {
        return;
    }
    const int x = ReadSignedInt(iniPath, section, L"X");
    const int y = ReadSignedInt(iniPath, section, L"Y");
    const int w = ReadSignedInt(iniPath, section, L"Width");
    const int h = ReadSignedInt(iniPath, section, L"Height");
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
    out.m_maximized = (ReadSignedInt(iniPath, section, L"Maximized") != 0);
}

bool SaveSharePlacement(const wchar_t* iniPath, const wchar_t* section, const winplace::WindowPlacementPrefs& prefs)
{
    if (!prefs.m_hasSaved || iniPath == NULL || iniPath[0] == L'\0' || section == NULL)
    {
        return false;
    }
    bool ok = WriteInt(iniPath, section, L"X", prefs.m_rect.left);
    ok = WriteInt(iniPath, section, L"Y", prefs.m_rect.top) && ok;
    ok = WriteInt(iniPath, section, L"Width", prefs.m_rect.right - prefs.m_rect.left) && ok;
    ok = WriteInt(iniPath, section, L"Height", prefs.m_rect.bottom - prefs.m_rect.top) && ok;
    ok = WriteInt(iniPath, section, L"Maximized", prefs.m_maximized ? 1 : 0) && ok;
    return ok;
}

RECT ResolveSharePlacementRect(const winplace::WindowPlacementPrefs& saved, SIZE defaultSize, SIZE minSize, HWND anchor)
{
    std::vector<RECT> workAreas;
    ::EnumDisplayMonitors(NULL, NULL, CollectWorkAreaProc, reinterpret_cast<LPARAM>(&workAreas));

    //「当前屏幕」：anchor 所在的显示器；没有 anchor 时取主显示器（屏幕坐标原点所在的那一台）
    POINT origin = { 0, 0 };
    HMONITOR monitor = (anchor != NULL && ::IsWindow(anchor))
        ? ::MonitorFromWindow(anchor, MONITOR_DEFAULTTOPRIMARY)
        : ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO fallback;
    fallback.cbSize = sizeof(fallback);
    if (!::GetMonitorInfoW(monitor, &fallback))
    {
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback.rcWork, 0);
    }
    return winplace::ResolveRestoredWindowRect(saved, defaultSize, minSize, workAreas, fallback.rcWork);
}

RECT CascadeSharePlacementRect(const RECT& start, const RECT& workArea, bool hasPrevious, POINT previousTopLeft, int step)
{
    const int width = start.right - start.left;
    const int height = start.bottom - start.top;
    POINT topLeft;
    topLeft.x = start.left;
    topLeft.y = start.top;
    if (hasPrevious)
    {
        const int offset = (step > 0) ? step : 0;
        POINT next;
        next.x = previousTopLeft.x + offset;
        next.y = previousTopLeft.y + offset;
        //错开之后超出工作区的右边或下边：回到起点
        if (next.x + width <= workArea.right && next.y + height <= workArea.bottom)
            topLeft = next;
    }
    //夹进工作区；窗口比工作区还大时左上角对齐工作区的左上角
    if (workArea.right > workArea.left && workArea.bottom > workArea.top)
    {
        if (topLeft.x + width > workArea.right)
            topLeft.x = workArea.right - width;
        if (topLeft.y + height > workArea.bottom)
            topLeft.y = workArea.bottom - height;
        if (topLeft.x < workArea.left)
            topLeft.x = workArea.left;
        if (topLeft.y < workArea.top)
            topLeft.y = workArea.top;
    }
    RECT rc;
    rc.left = topLeft.x;
    rc.top = topLeft.y;
    rc.right = topLeft.x + width;
    rc.bottom = topLeft.y + height;
    return rc;
}

int64_t LatestOpenShare(const std::vector<int64_t>& openOrder, const std::set<int64_t>& openShares, int64_t exclude)
{
    for (std::vector<int64_t>::const_reverse_iterator it = openOrder.rbegin(); it != openOrder.rend(); ++it)
    {
        if (*it != exclude && openShares.count(*it) > 0)
            return *it;
    }
    return 0;
}

} // namespace fileshare
