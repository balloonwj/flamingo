/**
 *  WindowPlacementPrefs.cpp —— 窗口位置的读写，说明见头文件。恢复时的矩形计算 ResolveRestoredWindowRect
 *  在 WindowPlacementResolve.cpp（2026-10-07 拆出，文件共享 DLL 也要用它，见那个文件的说明）。
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

} // namespace winplace
