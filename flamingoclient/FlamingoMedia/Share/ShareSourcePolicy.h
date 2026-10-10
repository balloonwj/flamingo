/**
 *  屏幕共享「共享内容」选择的纯逻辑规则，ShareSourcePolicy.h
 *
 *  选择窗要列出可共享的显示器与顶层窗口，并为每个显示器找到对应的 DXGI 输出（采集整屏用 Desktop Duplication，
 *  必须枚举全部显卡，见 docs/屏幕共享与录制方案.md 5.5 与第 0 期 0c 的实测）。哪些窗口该列出、显示器对应哪块显卡的
 *  哪个输出、缩略图按什么尺寸画，这几条规则抽成不依赖系统调用的纯函数，便于单元测试，见
 *  flamingoclient/Source/Tests/ShareSourcePolicyTests.cpp；真正的枚举与抓图在 ShareSources.h。
 *
 *  典型用法：
 *      ShareWindowFacts facts = 收集某个窗口的事实;
 *      if (screenshare::ShouldListShareWindow(facts)) 列入选择窗;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace screenshare {

//列入选择窗的窗口的最小可见尺寸，单位像素：再小的窗口多半是隐形的辅助窗口
const int kShareWindowMinSize = 50;

//判断一个顶层窗口是否列入选择窗所需的事实（由 ShareSources 从系统收集）
struct ShareWindowFacts
{
    bool         m_visible;      //IsWindowVisible
    bool         m_cloaked;      //DWM 隐藏（在别的虚拟桌面上、或挂起的 UWP 应用）
    bool         m_toolWindow;   //扩展样式带 WS_EX_TOOLWINDOW
    bool         m_appWindow;    //扩展样式带 WS_EX_APPWINDOW（强制出现在任务栏）
    bool         m_noActivate;   //扩展样式带 WS_EX_NOACTIVATE
    bool         m_hasOwner;     //有属主窗口（对话框、浮动面板一类）
    bool         m_ownProcess;   //属于 Flamingo 自己的进程（控制条、边框等不能共享自己）
    bool         m_minimized;    //已最小化（仍列出：恢复后即可采集）
    int          m_width;        //可见外框宽，单位像素
    int          m_height;       //可见外框高，单位像素
    std::wstring m_title;        //窗口标题
    std::wstring m_className;    //窗口类名

    ShareWindowFacts();
};

/**
 *  判断一个顶层窗口是否列入选择窗。列出的条件：可见、未被 DWM 隐藏、标题非空白、不属于本进程、不是系统外壳窗口
 *  （桌面、任务栏）；工具窗口与有属主的窗口只有带 WS_EX_APPWINDOW 时才列；带 WS_EX_NOACTIVATE 且不带
 *  WS_EX_APPWINDOW 的不列；未最小化的窗口宽高都不小于 kShareWindowMinSize。
 *  @param facts 窗口事实
 *  @return 应列入返回 true
 */
bool ShouldListShareWindow(const ShareWindowFacts& facts);

//一块 DXGI 输出：所在显卡序号、输出序号与它对应的显示器
struct ShareDxgiOutputRef
{
    int      m_adapter;   //EnumAdapters1 的序号
    int      m_output;    //该显卡上 EnumOutputs 的序号
    HMONITOR m_monitor;   //DXGI_OUTPUT_DESC::Monitor

    ShareDxgiOutputRef(int adapter, int output, HMONITOR monitor);
};

/**
 *  为显示器找到对应的 DXGI 输出。同一显示器出现多次时取第一个（按显卡、输出序号的枚举顺序）。
 *  @param outputs 全部显卡上的全部输出
 *  @param monitor 要找的显示器
 *  @param adapter [出参] 找到时回填显卡序号
 *  @param output  [出参] 找到时回填输出序号
 *  @return 找到返回 true
 */
bool FindDxgiOutputForMonitor(const std::vector<ShareDxgiOutputRef>& outputs, HMONITOR monitor, int& adapter,
                              int& output);

/**
 *  按原宽高比把源尺寸缩进框内，只缩不放；结果至少 1×1。源尺寸或框尺寸不大于 0 时结果为 0×0。
 *  @param srcW / srcH 源尺寸，单位像素
 *  @param boxW / boxH 框尺寸，单位像素
 *  @param outW / outH [出参] 结果尺寸
 */
void FitShareSize(int srcW, int srcH, int boxW, int boxH, int& outW, int& outH);

} // namespace screenshare
