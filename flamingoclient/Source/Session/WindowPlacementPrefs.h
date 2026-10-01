/**
 *  WindowPlacementPrefs.h
 *
 *  窗口位置与大小的保存和恢复：窗口关闭时把还原态的矩形与是否最大化写进账号配置
 *  （<账号>.ini），下次打开时读回来，并按当前的屏幕布局算出一个一定看得见的矩形。
 *  目前用于消息记录窗口（配置里的 [HistoryDlg] 一节）。
 *
 *  恢复时要处理几种屏幕布局变化：上次所在的显示器已经拔掉、分辨率调小后窗口比工作区
 *  还大、窗口有一部分跑到了屏幕外面。ResolveRestoredWindowRect 是纯函数，不读系统
 *  状态，各显示器的工作区由调用方传入，因此可以完全用单元测试覆盖（见
 *  Tests/WindowPlacementPrefsTests.cpp）。
 *
 *  本模块被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程共用，在 Flamingo.vcxproj
 *  里需标记 PrecompiledHeader=NotUsing。
 *
 *  配置文件里的格式（以消息记录窗口为例，坐标为屏幕坐标、物理像素，可以为负数）：
 *      [HistoryDlg]
 *      HistoryDlgX=-1500
 *      HistoryDlgY=120
 *      HistoryDlgWidth=1100
 *      HistoryDlgHeight=860
 *      HistoryDlgMaximized=0
 *
 *  典型用法：
 *      //打开窗口时
 *      RECT rc = winplace::ResolveRestoredWindowRect(cfg.GetHistoryDlgPlacement(), defSize,
 *                                                    minSize, workAreas, fallbackWorkArea);
 *      //关闭窗口时
 *      cfg.SetHistoryDlgPlacement(prefs);
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <windows.h>
#include <vector>

class CIniFile;

namespace winplace {

//一个窗口上次关闭时的位置与大小
struct WindowPlacementPrefs
{
    //是否保存过。false 表示配置里没有这一组值（第一次打开，或配置被删），其余字段无意义，
    //打开时按默认大小居中显示
    bool m_hasSaved;
    //还原态（非最大化时）的窗口矩形：屏幕坐标，物理像素。副屏在主屏左边或上边时可以为负数
    RECT m_rect;
    //关闭时窗口是否处于最大化。为 true 时打开后先按 m_rect 摆好还原态，再最大化
    bool m_maximized;

    WindowPlacementPrefs()
        : m_hasSaved(false)
        , m_maximized(false)
    {
        ::SetRectEmpty(&m_rect);
    }
};

/**
 *  从配置文件读一组窗口位置。
 *    ini：       配置读写对象（引用，不持有）。
 *    filePath：  配置文件全路径。
 *    section：   小节名，如 _T("HistoryDlg")。
 *    keyPrefix： 键名前缀，如 _T("HistoryDlg")，实际的键为前缀加 X / Y / Width / Height / Maximized。
 *    out：       读出的结果。宽或高缺失、或不为正数时 m_hasSaved 为 false。
 *  本函数只读文件，不修改它。
 */
void LoadWindowPlacementPrefs(CIniFile& ini, LPCTSTR filePath, LPCTSTR section, LPCTSTR keyPrefix,
                              WindowPlacementPrefs& out);

/**
 *  把一组窗口位置写进配置文件。参数含义同 LoadWindowPlacementPrefs。
 *  prefs.m_hasSaved 为 false 时什么都不写，配置里原有的值保持不变。
 */
void SaveWindowPlacementPrefs(CIniFile& ini, LPCTSTR filePath, LPCTSTR section, LPCTSTR keyPrefix,
                              const WindowPlacementPrefs& prefs);

/**
 *  按当前的屏幕布局，算出窗口打开时应当使用的还原态矩形。
 *
 *  规则：
 *    · 没有保存过：用默认大小，在 fallbackWorkArea 里居中；
 *    · 保存的矩形与任何一个工作区的重叠部分都不足以抓住标题栏（宽不足
 *      kMinVisibleWidthPx 或高不足 kMinVisibleHeightPx，典型是那台显示器已经拔掉）：
 *      同样用默认大小在 fallbackWorkArea 里居中；
 *    · 否则以重叠面积最大的那个工作区为准：宽高先限制在 [最小尺寸, 工作区尺寸] 之内，
 *      再把位置平移到整个窗口都落在该工作区里。
 *
 *    saved：           上次保存的位置（见 LoadWindowPlacementPrefs）。
 *    defaultSize：     默认窗口大小，物理像素。
 *    minSize：         最小窗口大小，物理像素；与工作区尺寸冲突时以工作区为准。
 *    workAreas：       当前每台显示器的工作区（屏幕坐标），可以为空。
 *    fallbackWorkArea：找不到合适的工作区时用来居中的工作区，通常取主显示器或父窗口所在显示器。
 *  @return 窗口矩形，屏幕坐标、物理像素。本函数无副作用。
 */
RECT ResolveRestoredWindowRect(const WindowPlacementPrefs& saved, SIZE defaultSize, SIZE minSize,
                               const std::vector<RECT>& workAreas, const RECT& fallbackWorkArea);

//保存的矩形与某个工作区至少要重叠这么宽、这么高，才认为窗口还"看得见"、能用鼠标抓住
//标题栏拖回来；单位物理像素。取值参照标题栏的高度与一段可以按住的宽度
const int kMinVisibleWidthPx  = 120;
const int kMinVisibleHeightPx = 40;

} // namespace winplace
