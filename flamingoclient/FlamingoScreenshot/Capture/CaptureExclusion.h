/**
 *  CaptureExclusion.h
 *
 *  截图时把指定窗口（发起截图的聊天窗）排除在抓屏结果之外，即「截图时隐藏当前窗口」。
 *  两种方式（docs/截图快捷键与隐藏当前窗口方案.md 第 3 节）：
 *    · Win10 2004（build 19041）起：对窗口设置显示属性 WDA_EXCLUDEFROMCAPTURE。窗口在屏幕上
 *      照常显示，BitBlt 抓到的是它背后的内容，设置与恢复都即时生效，窗口不会闪烁
 *      （2026-10-05 实测，tmp/shotwda/wda_test.exe）；
 *    · 更早的系统：先隐藏窗口，等桌面重新合成后再抓屏，截图界面显示出来之后再恢复显示。
 *  CCaptureExclusion 在析构时自动恢复，调用方提前返回时窗口也不会一直处于被排除的状态——
 *  否则之后的屏幕共享与录屏都会看不到这个窗口。
 *
 *  典型用法：
 *      CCaptureExclusion exclusion;
 *      exclusion.Begin(chatHwnd);       // 抓屏之前
 *      ... BitBlt 抓整个虚拟桌面，建好截图界面并显示 ...
 *      exclusion.Restore();             // 截图界面显示之后（析构时也会自动调用）
 *
 *  只在 UI 线程（窗口所在线程）调用。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>

namespace balloonscreenshot {

// 支持 WDA_EXCLUDEFROMCAPTURE 的最低 Windows build 号（Windows 10 2004）。更早的系统上该值
// 会被当作 WDA_MONITOR 处理，抓屏得到一块黑色，所以只能改用隐藏窗口的办法。
const DWORD kExcludeFromCaptureMinBuild = 19041;

// 把窗口排除在抓屏之外的方式。
enum CaptureExclusionMode
{
    kExcludeNone = 0,     // 不排除：没有指定窗口，或窗口已不存在、不可见、已最小化（本来就不在画面里）
    kExcludeByAffinity,   // 设置显示属性 WDA_EXCLUDEFROMCAPTURE：窗口照常显示，抓屏抓到的是它背后的内容
    kExcludeByHiding,     // 先隐藏窗口再抓屏，恢复时重新显示（不激活）；老系统，或设置显示属性失败时使用
};

/**
 *  选择排除方式（纯函数，用例 SX1）。
 *  @param windowUsable 窗口存在、可见且没有最小化
 *  @param osBuild      Windows 的 build 号（见 GetWindowsBuildNumber）
 *  @return 窗口不可用时为 kExcludeNone；build 号不低于 kExcludeFromCaptureMinBuild 时为
 *          kExcludeByAffinity；否则为 kExcludeByHiding
 */
CaptureExclusionMode ChooseExclusionMode(bool windowUsable, DWORD osBuild);

/**
 *  取当前系统的 Windows build 号。经 ntdll 的 RtlGetVersion 读取，不受程序清单兼容性
 *  设置的影响（GetVersionEx 在没有声明兼容 Win10 的程序里会返回 Win8 的版本号）。
 *  @return build 号；取不到时返回 0（ChooseExclusionMode 随之选择隐藏窗口的办法）
 */
DWORD GetWindowsBuildNumber();

/**
 *  CCaptureExclusion —— 一次截图期间把一个窗口排除在抓屏之外，析构时自动恢复。
 *  不可拷贝；每次截图用一个新对象。
 */
class CCaptureExclusion
{
public:
    CCaptureExclusion();
    ~CCaptureExclusion();   // 尚未恢复时自动调用 Restore

    /**
     *  开始排除：按 ChooseExclusionMode 选择方式并立即生效。设置显示属性失败时改为隐藏窗口。
     *  @param hwnd 要排除的窗口（借用，不持有）；NULL 时什么也不做
     *  @return 实际采用的方式
     *  副作用：kExcludeByHiding 时窗口被隐藏，并等待桌面重新合成一帧（最多约 100 毫秒）
     */
    CaptureExclusionMode Begin(HWND hwnd);

    /**
     *  按指定方式开始排除，不看系统版本。供单测覆盖老系统上的退路（用例 SX2），产品代码用 Begin。
     *  @param hwnd 要排除的窗口（借用，不持有）；NULL、不存在、不可见或已最小化时什么也不做
     *  @param mode 要采用的方式；kExcludeNone 时什么也不做
     *  @return 实际采用的方式
     */
    CaptureExclusionMode BeginAs(HWND hwnd, CaptureExclusionMode mode);

    /**
     *  恢复窗口原状：恢复原来的显示属性，或重新显示（不激活）被隐藏的窗口。
     *  可重复调用，第二次起不做任何事；窗口已被销毁时同样不做任何事。
     */
    void Restore();

    // 当前采用的方式；Restore 之后为 kExcludeNone
    CaptureExclusionMode Mode() const { return m_mode; }

private:
    CCaptureExclusion(const CCaptureExclusion&);
    CCaptureExclusion& operator=(const CCaptureExclusion&);

    HWND                 m_hwnd;          // 被排除的窗口（借用）；Restore 之后为 NULL
    CaptureExclusionMode m_mode;          // 当前采用的方式
    DWORD                m_oldAffinity;   // 设置显示属性之前的原值，恢复时写回（一般为 WDA_NONE）
};

} // namespace balloonscreenshot
