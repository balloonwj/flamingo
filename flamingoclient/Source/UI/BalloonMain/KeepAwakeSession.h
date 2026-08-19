/**
 *  KeepAwakeSession.h
 *
 *  「禁止机器休眠」中实际调用 Win32 API 的封装层：获取与释放系统电源请求、合成一次
 *  输入事件，以及输出排查所需的日志。是否执行这些动作由纯逻辑的 KeepAwakePolicy 判定，
 *  本类只负责执行，两者的分工与 AutoAwayPolicy / 自动离开控制器相同。
 *
 *  为什么需要两套机制（2026-08-12 本机实测，探针脚本见 tools/keepawake_probe.ps1）：
 *    · 系统电源请求能够阻止休眠与关闭显示器，但<u>不影响输入空闲计时器</u>；
 *    · 屏幕保护程序与自动锁屏依据的正是该计时器，只有向系统合成输入事件才能推迟它们。
 *  完整的实测数据记录在 KeepAwakePolicy.h 的头注释中。
 *
 *  <u>日志约定</u>：本类的日志一律以「禁止机器休眠：」开头，便于事后用
 *  `findstr 禁止机器休眠 Logs\*.log` 一次性检索。要判断"某次静置期间是否发生了锁屏"，
 *  除本类的日志外还需配合自动离开控制器 OnSessionChange 输出的会话锁定 / 解锁日志 ——
 *  那一条是"锁屏确实发生过"的直接证据，无需推断。
 *
 *  本类不是单例，也不跨线程：由自动离开控制器按值持有，只在主线程（定时器与消息处理）中
 *  访问。析构时若仍持有电源请求会自动释放，进程正常退出不会遗留副作用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <windows.h>

namespace balloonmain {

/**
 *  CKeepAwakeSession —— 系统电源请求的持有者，同时负责合成输入事件。
 */
class CKeepAwakeSession
{
public:
    CKeepAwakeSession();
    ~CKeepAwakeSession();

    /**
     *  获取系统电源请求，使本机在持有期间不休眠、不关闭显示器。
     *
     *  优先使用 Windows 7 起提供的 PowerCreateRequest / PowerSetRequest：它是进程级的
     *  （不像 SetThreadExecutionState 绑定在调用线程上），并且管理员执行
     *  `powercfg /requests` 时能看到是哪个程序、以何种理由阻止休眠，便于企业环境中排查
     *  "该机器为何不休眠"。该 API 不可用或调用失败时回落到 SetThreadExecutionState，
     *  两条路径都会输出日志。
     *
     *  已经持有时直接返回 true，不重复获取。
     *
     *  @return 成功持有（无论走哪条路径）返回 true；两条路径都失败返回 false。
     */
    bool Acquire();

    /**
     *  释放系统电源请求，本机恢复正常的休眠与关屏行为。未持有时不执行任何动作。
     */
    void Release();

    // 当前是否持有系统电源请求。
    bool IsHolding() const;

    /**
     *  向系统合成一次输入事件，把全系统的输入空闲计时器重置为 0，从而推迟屏幕保护程序
     *  与自动锁屏。
     *
     *  合成的是<u>位移量为 0 的鼠标移动事件</u>（dx = dy = 0）：光标不会移动、不会投递到
     *  任何窗口、也不会被识别为点击，但足以使 GetLastInputInfo 归零（已实测）。不采用
     *  合成按键，是因为按键会真实投递给前台窗口，可能触发其它程序的快捷键。
     *
     *  它只能<u>阻止</u>屏保与锁屏启动，不会唤醒已经黑屏或已锁定的屏幕 —— 实测屏保启动
     *  之后再合成输入事件，屏幕保护程序仍在运行。这正是本功能所需要的行为。
     *
     *  每次调用都会输出一条日志，包含合成前的全系统空闲秒数与 SendInput 的返回值。
     *
     *  @return SendInput 成功合成返回 true，否则返回 false。
     */
    bool Nudge();

    /**
     *  把与"无操作多久后锁屏"相关的系统设置快照输出到日志：屏幕保护程序是否启用、
     *  屏保超时、恢复时是否需要密码，以及组策略的计算机不活动限制。
     *
     *  开启本功能时输出一次。有了这一条，事后查看日志即可知道被测机器当时的策略配置，
     *  无需再询问"那台机器几分钟锁屏"。
     */
    static void LogIdlePolicySnapshot();

    /**
     *  取当前全系统的输入空闲时长（毫秒），即距最后一次键盘或鼠标操作经过的时长。
     *  取不到时返回 0。供日志与调用方使用。
     */
    static DWORD QuerySystemIdleMs();

private:
    HANDLE m_hPowerRequest;   // PowerCreateRequest 返回的句柄；NULL 表示未使用该路径
    bool   m_execStateHeld;   // 是否正在使用 SetThreadExecutionState 这条回落路径
};

} // namespace balloonmain
