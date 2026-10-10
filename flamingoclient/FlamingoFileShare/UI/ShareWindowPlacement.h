/**
 *  ShareWindowPlacement.h
 *
 *  文件共享 DLL 里窗口位置与大小的保存和恢复（2026-10-07 起，用于「文件共享」窗口 CShareCenterWindow；同日起也用于共享浏览窗口
 *  CShareBrowserWindow，所有共享的浏览窗口共用 [ShareBrowser] 一个小节，同时打开多个时错开摆放）。
 *
 *  位置存在本人个人目录下的 ChatLog\fileshare_ui.ini（与 DLL 的 fileshare.db 同一目录），按账号各自保存。
 *  不用主程序的 CIniFile / CUserConfig：DLL 拿不到主程序的账号配置对象，且 DLL 编译的共用源文件不得包含 IniFile.h
 *  （FileShareSourceGuardTests 的 FDG1），这里直接调 Win32 的 GetPrivateProfileInt / WritePrivateProfileString。
 *  恢复时的矩形计算复用主程序的纯函数 winplace::ResolveRestoredWindowRect（Source/Session/WindowPlacementResolve.cpp）。
 *
 *  配置文件格式（坐标为屏幕坐标、物理像素，可以为负数）：
 *      [ShareCenter]
 *      X=-1500
 *      Y=120
 *      Width=1080
 *      Height=652
 *      Maximized=0
 *
 *  典型用法：
 *      //打开窗口时
 *      winplace::WindowPlacementPrefs saved;
 *      LoadSharePlacement(iniPath.c_str(), kShareCenterPlacementSection, saved);
 *      RECT rc = ResolveSharePlacementRect(saved, defSize, minSize, ownerHwnd);
 *      //关闭窗口时
 *      SaveSharePlacement(iniPath.c_str(), kShareCenterPlacementSection, prefs);
 *
 *  balloonwj@qq.com   2026-10-07
 */

#pragma once

#include <windows.h>
#include <stdint.h>
#include <set>
#include <string>
#include <vector>

#include "../../Source/Session/WindowPlacementPrefs.h"   //WindowPlacementPrefs / ResolveRestoredWindowRect

namespace fileshare {

//「文件共享」窗口位置在 fileshare_ui.ini 里的小节名
extern const wchar_t* const kShareCenterPlacementSection;

//共享浏览窗口位置在 fileshare_ui.ini 里的小节名；所有共享的浏览窗口共用这一份，最后关闭的那个窗口的位置留下来
extern const wchar_t* const kShareBrowserPlacementSection;

//同时打开多个浏览窗口时，新窗口相对上一个错开的距离（像素，横向与纵向相同）。与联系人资料窗相同取 24：错开之后
//上一个窗口的标题文字仍露在外面，看得出下面还压着一个窗口
const int kShareBrowserCascadeStep = 24;

/**
 *  由个人目录拼出保存窗口位置的配置文件全路径：<个人目录>ChatLog\fileshare_ui.ini。
 *    personalDir：本人的个人目录（带结尾反斜杠，见 FileShareSessionInfo::m_personalDir）；为空时返回空串。
 *  @return 配置文件全路径；本函数不创建目录、不读写文件。
 */
std::wstring SharePlacementIniPath(const wchar_t* personalDir);

/**
 *  从配置文件读一组窗口位置。
 *    iniPath：配置文件全路径；为空或文件不存在时 out.m_hasSaved 为 false。
 *    section：小节名，如 kShareCenterPlacementSection。
 *    out：    读出的结果。宽或高缺失、或不为正数时 m_hasSaved 为 false。
 *  本函数只读文件，不修改它。
 */
void LoadSharePlacement(const wchar_t* iniPath, const wchar_t* section, winplace::WindowPlacementPrefs& out);

/**
 *  把一组窗口位置写进配置文件。prefs.m_hasSaved 为 false、或 iniPath 为空时什么都不写。
 *    iniPath：配置文件全路径；所在目录须已存在（文件共享登录时已建好 ChatLog 目录）。
 *    section：小节名。
 *    prefs：  要保存的位置。
 *  @return 全部写入成功返回 true
 */
bool SaveSharePlacement(const wchar_t* iniPath, const wchar_t* section, const winplace::WindowPlacementPrefs& prefs);

/**
 *  按当前的屏幕布局算出窗口打开时应当使用的还原态矩形：枚举各显示器的工作区，没有保存过（或保存的位置所在的
 *  显示器已经拔掉）时在 anchor 所在显示器的工作区里按默认大小居中，其余规则见 winplace::ResolveRestoredWindowRect。
 *    saved：      上次保存的位置。
 *    defaultSize：默认窗口大小，物理像素。
 *    minSize：    最小窗口大小，物理像素。
 *    anchor：     用来确定「当前屏幕」的窗口（一般是打开本窗口的主面板或聊天窗）；为 NULL 或已失效时取主显示器。
 *  @return 窗口矩形，屏幕坐标、物理像素。本函数只读系统状态，无副作用。
 */
RECT ResolveSharePlacementRect(const winplace::WindowPlacementPrefs& saved, SIZE defaultSize, SIZE minSize, HWND anchor);

/**
 *  算错开摆放后的窗口矩形（纯函数，屏幕坐标）。用于同时打开多个浏览窗口时，新窗口不完全盖住上一个。
 *  规则：没有上一个窗口时用起点 start；有上一个窗口时左上角取它的左上角向右下各错开 step 像素，这样摆会超出工作区的
 *  右边或下边时回到起点。最后把窗口夹进工作区：窗口比工作区还大时左上角对齐工作区的左上角。窗口大小始终与 start 相同。
 *    start：          不考虑错开时窗口应在的矩形（按保存的位置或居中算出，见 ResolveSharePlacementRect）。
 *    workArea：       起点所在显示器的工作区；宽或高不为正数时不夹。
 *    hasPrevious：    是否有上一个仍开着的浏览窗口。
 *    previousTopLeft：上一个浏览窗口的左上角，hasPrevious 为 false 时不用。
 *    step：           错开距离（像素），一般传 kShareBrowserCascadeStep；不为正数时按 0 处理。
 *  @return 窗口矩形。无副作用。
 */
RECT CascadeSharePlacementRect(const RECT& start, const RECT& workArea, bool hasPrevious, POINT previousTopLeft, int step);

/**
 *  在按打开先后记下的共享 id 里，找出最近打开、窗口仍开着、且不是 exclude 的那一个（纯函数）。用于决定新浏览窗口相对谁错开。
 *    openOrder： 浏览窗口的打开先后（同一个共享可以出现多次，以最后一次为准）。
 *    openShares：此刻窗口仍开着的共享 id。
 *    exclude：   正在打开的共享 id，不算在内。
 *  @return 找到的共享 id；没有时返回 0
 */
int64_t LatestOpenShare(const std::vector<int64_t>& openOrder, const std::set<int64_t>& openShares, int64_t exclude);

} // namespace fileshare
