/**
 *  屏幕共享的「共享内容」枚举与缩略图，ShareSources.h
 *
 *  为选择窗提供：全部显示器（附对应的 DXGI 显卡与输出序号，整屏采集要用）、可共享的顶层窗口（按 Z 序，过滤规则见
 *  ShareSourcePolicy.h）、显示器与窗口的缩略图；另提供「把本进程的窗口排除在采集之外」与「取窗口可见外框」两个
 *  小工具，供控制条、边框、标注层等使用。
 *
 *  这些函数直接调用系统接口，不进自动化单测；规则部分在 ShareSourcePolicy 里单测，整体由调试入口
 *  （ShareDebugEntries，命令行 --sharecapture）输出日志与图片供人工核对。只在 UI 线程或调用方自己的线程里同步调用。
 *
 *  典型用法：
 *      std::vector<screenshare::ShareMonitor> monitors;
 *      screenshare::EnumerateShareMonitors(monitors);
 *      HBITMAP thumb = screenshare::CaptureMonitorThumbnail(monitors[0].m_rect, 320, 180);
 *      ...
 *      ::DeleteObject(thumb);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace screenshare {

//一块显示器
struct ShareMonitor
{
    HMONITOR     m_monitor;   //显示器句柄
    RECT         m_rect;      //在虚拟桌面上的矩形（物理像素）
    bool         m_primary;   //是否主显示器
    std::wstring m_device;    //设备名，如 \\.\DISPLAY1
    int          m_adapter;   //对应的 DXGI 显卡序号；找不到时为 -1（只能走 GDI 降级）
    int          m_output;    //对应的 DXGI 输出序号；找不到时为 -1

    ShareMonitor();
};

//一个可共享的顶层窗口
struct ShareWindow
{
    HWND         m_hwnd;        //窗口句柄
    std::wstring m_title;       //窗口标题
    RECT         m_bounds;      //可见外框（物理像素）
    bool         m_minimized;   //是否已最小化

    ShareWindow();
};

/**
 *  枚举全部显示器，并为每块找到对应的 DXGI 输出（枚举全部显卡）。主显示器排在最前，其余按左上角坐标排序。
 *  @param out [出参] 显示器列表，先清空再填入
 *  @return 至少找到一块显示器返回 true
 */
bool EnumerateShareMonitors(std::vector<ShareMonitor>& out);

/**
 *  枚举可共享的顶层窗口，按 Z 序（最上面的在前），过滤规则见 ShouldListShareWindow。
 *  @param out [出参] 窗口列表，先清空再填入
 */
void EnumerateShareWindows(std::vector<ShareWindow>& out);

/**
 *  取显示器的缩略图：从屏幕 DC 按比例缩进框内（HALFTONE）。
 *  @param rect 显示器矩形
 *  @param boxW / boxH 缩略图框尺寸，单位像素
 *  @return 32 位 DIB 位图，调用方负责 DeleteObject；失败返回 NULL
 */
HBITMAP CaptureMonitorThumbnail(const RECT& rect, int boxW, int boxH);

/**
 *  取窗口的缩略图：PrintWindow（PW_RENDERFULLCONTENT，可取到 GPU 渲染与被遮挡的窗口）后按比例缩进框内。
 *  已最小化的窗口取不到内容，返回 NULL，调用方显示占位图。
 *  @return 32 位 DIB 位图，调用方负责 DeleteObject；失败返回 NULL
 */
HBITMAP CaptureWindowThumbnail(HWND hwnd, int boxW, int boxH);

/**
 *  把本进程的一个窗口排除在一切屏幕采集之外（SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)，Win10 2004 起有效）。
 *  控制条、边框、本机摄像头小窗、标注透明层都要调用，否则会被采进共享画面。
 *  @return 设置成功返回 true；系统不支持时返回 false（记日志，窗口照常显示）
 */
bool ExcludeWindowFromCapture(HWND hwnd);

/**
 *  设置窗口能否被采集（2026-10-08，「允许截图」，docs/屏幕共享与录制方案.md 第 14.11 节第 5 条）。
 *  @param hwnd    顶层窗口
 *  @param allowed true 取消排除（WDA_NONE），截图与屏幕采集都能取到它；false 排除（同 ExcludeWindowFromCapture）
 *  @return 设置成功返回 true
 */
bool SetWindowCaptureAllowed(HWND hwnd, bool allowed);

/**
 *  本机的工具条与控制条（标注工具条、共享控制条、共享者控制条、录制条）是否允许截图。只对当前这场共享或这次录制有效：
 *  打开时由菜单的「允许截图」设置，共享结束、录制结束时设回 false。标注层、边框、右下角提示、摄像头小窗不受它影响，一直排除。
 *  @param allowed 是否允许
 */
void SetBarCaptureAllowed(bool allowed);

//「允许截图」开关当前是否打开
bool IsBarCaptureAllowed();

/**
 *  按「允许截图」开关设置一个工具条或控制条窗口：打开时允许采集，关闭时排除。工具条、控制条建好窗口后调用，代替
 *  ExcludeWindowFromCapture；开关变化时对当前显示着的各条再调用一次。
 *  @param hwnd 工具条或控制条的顶层窗口
 *  @return 设置成功返回 true
 */
bool ApplyBarCaptureState(HWND hwnd);

/**
 *  取窗口的可见外框（DWMWA_EXTENDED_FRAME_BOUNDS，不含 Win10 的隐形缩放边框）；取不到时退回 GetWindowRect。
 */
RECT GetShareWindowBounds(HWND hwnd);

/**
 *  取窗口标题（UTF-16）。
 */
std::wstring GetShareWindowTitle(HWND hwnd);

/**
 *  把一张 32 位 DIB 的透明通道全部置为 255（不透明），颜色不变。GDI 往 32 位 DIB 上画东西时不写透明通道（保持 0），
 *  而 balloonui 按钮按预乘透明度绘制背景位图，透明通道为 0 的像素会整块画成透明；选择窗的格子位图交给按钮之前须先调用。
 *  @param dib 32 位 DIB（CreateDIBSection 创建）；不拥有
 *  @return 处理了返回 true；dib 为 NULL、不是 DIB 或不是 32 位时返回 false，不做任何事
 */
bool MakeDibOpaque(HBITMAP dib);

} // namespace screenshare
