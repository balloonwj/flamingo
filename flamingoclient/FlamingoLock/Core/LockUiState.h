/**
 *  锁屏界面的交互状态机与键盘过滤规则（纯逻辑，不依赖窗口，可单测）。
 *
 *  · CLockUiState：平时只有动画；单击任意处淡入「解锁 / 重启 / 关机」；点「解锁」出现密码框；
 *    点「重启 / 关机」出现确认框；按钮或密码状态下 15 秒无操作回到只有动画。时刻由调用方传入。
 *  · DecideLockKey：低级键盘钩子里对每个按键事件的放行与拦截判定。
 *
 *  典型用法（锁屏线程）：
 *      state.OnClick(monitorIndex, now);                 // 鼠标单击（没点中任何按钮）
 *      LockUiCommand cmd = state.OnButton(kBtnShutdown, now);
 *      if (state.Tick(now)) 清空密码框;                    // 定时调用
 *
 *  方案见 docs/锁屏方案.md 第 4.4、4.5 节。
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

namespace flamingolock {

const uint32_t kUiIdleTimeoutMs = 15000;   // 按钮或密码状态下多久无操作回到只有动画（毫秒）

/**
 *  锁屏界面的阶段。
 */
enum LockUiPhase
{
    kUiIdle = 0,            // 只有动画：刚锁屏，或按钮淡出之后
    kUiButtons,             // 屏幕下方显示「解锁」「重启」「关机」
    kUiPassword,            // 按钮上方另有密码框，键盘输入送进密码框
    kUiConfirmShutdown,     // 「确定要关机吗？」确认框
    kUiConfirmRestart,      // 「确定要重启吗？」确认框
};

/**
 *  锁屏界面上可点的按钮。
 */
enum LockUiButton
{
    kBtnUnlock = 0,         // 解锁：出现密码框
    kBtnRestart,            // 重启：出现确认框
    kBtnShutdown,           // 关机：出现确认框
    kBtnConfirmOk,          // 确认框里的「关机」或「重启」
    kBtnConfirmCancel,      // 确认框里的「取消」
};

/**
 *  状态机要求调用方执行的动作。
 */
enum LockUiCommand
{
    kCmdNone = 0,           // 只是界面变化，没有额外动作
    kCmdFocusPassword,      // 密码框刚出现：让锁屏窗口取得键盘焦点、关闭输入法
    kCmdShutdown,           // 确认关机
    kCmdRestart,            // 确认重启
};

/**
 *  锁屏界面的交互状态机。非线程安全，只在锁屏线程上使用。
 */
class CLockUiState
{
public:
    CLockUiState();

    /**
     *  回到初始状态（只有动画、没有按钮栏）。锁屏开始时调用。
     */
    void Reset();

    /**
     *  当前阶段。
     */
    LockUiPhase Phase() const;

    /**
     *  按钮栏所在的显示器序号（锁屏窗口的序号）；只有动画时为 -1。
     */
    int ActiveMonitor() const;

    /**
     *  按钮栏是否可见（阶段不是 kUiIdle）。
     */
    bool IsBarVisible() const;

    /**
     *  键盘输入是否应送进密码框（只在 kUiPassword 阶段）。
     */
    bool AcceptsKeyboard() const;

    /**
     *  鼠标在某块显示器上单击，且没有点中任何按钮或输入框。只有动画时出现按钮栏；
     *  按钮或密码阶段点在别的显示器上时，按钮栏挪到那块显示器（阶段不变）；确认框打开时忽略。
     *  @param monitor 被点击的锁屏窗口序号（0 起）
     *  @param nowMs   当前时刻（毫秒）
     */
    void OnClick(int monitor, uint64_t nowMs);

    /**
     *  点中了某个按钮。不在当前阶段可见的按钮被忽略。
     *  @param button 按钮
     *  @param nowMs  当前时刻（毫秒）
     *  @return 调用方要执行的动作
     */
    LockUiCommand OnButton(LockUiButton button, uint64_t nowMs);

    /**
     *  按下 Esc：确认框 → 按钮；密码 → 按钮；按钮 → 只有动画。
     *  @param nowMs 当前时刻（毫秒）
     *  @return 阶段回到只有动画时返回 true（调用方据此清空密码框）
     */
    bool OnEscape(uint64_t nowMs);

    /**
     *  有用户操作（鼠标在锁屏窗口内移动、在密码框里打字），重新计算无操作时长。只有动画时不起作用。
     *  @param nowMs 当前时刻（毫秒）
     */
    void OnActivity(uint64_t nowMs);

    /**
     *  定时调用：按钮或密码阶段无操作满 kUiIdleTimeoutMs 时回到只有动画；确认框打开期间不计时。
     *  @param nowMs 当前时刻（毫秒）
     *  @return 本次刚回到只有动画时返回 true（调用方据此清空密码框）
     */
    bool Tick(uint64_t nowMs);

private:
    LockUiPhase m_phase;          // 当前阶段
    int         m_monitor;        // 按钮栏所在的锁屏窗口序号；只有动画时为 -1
    uint64_t    m_lastActivityMs; // 最近一次用户操作的时刻（毫秒）
};

/**
 *  低级键盘钩子收到的一个按键事件。修饰键状态由钩子自己按收到的按下 / 抬起维护
 *  （被拦截的按键不会更新系统的按键状态，GetAsyncKeyState 不可靠）。
 */
struct LockKeyEvent
{
    uint32_t m_vk;          // 虚拟键码
    bool     m_keyUp;       // 是抬起事件
    bool     m_altDown;     // Alt 是否按着（钩子自己维护，并与 KBDLLHOOKSTRUCT 的 LLKHF_ALTDOWN 取「或」）
    bool     m_ctrlDown;    // Ctrl 是否按着
    bool     m_winDown;     // 左或右 Win 键是否按着
    bool     m_injected;    // 是程序合成的事件（LLKHF_INJECTED），例如远程桌面控制方的模拟输入
};

/**
 *  钩子对一个按键事件的处理。
 */
enum LockKeyDecision
{
    kKeyPass = 0,           // 放行，交给系统照常处理
    kKeyBlock,              // 拦截，系统与其它程序都收不到
};

/**
 *  锁屏期间对一个按键事件的判定，依次为：
 *  0. 程序合成的按键一律拦截（远程控制方不能在锁屏界面上打字）；
 *  1. Win 键本身（按下与抬起）以及 Win 按着时的任何键一律拦截；
 *  2. 其它键的抬起一律放行（锁屏前按住的键在锁屏中松开，系统才不会以为它一直按着）；
 *  3. Apps 键、PrintScreen 拦截；
 *  4. Alt 按着时除修饰键以外的键一律拦截（Alt+Tab、Alt+Esc、Alt+F4、Alt+Space）；
 *  5. Ctrl+Esc、Ctrl+Shift+Esc 拦截；
 *  6. 其余按键只在「密码框已出现且前台是锁屏窗口」时放行，否则拦截。
 *  @param e                按键事件
 *  @param foregroundIsLock 前台窗口是不是本模块的锁屏窗口
 *  @param passwordActive   锁屏界面是否处于密码阶段
 *  @return 放行或拦截
 */
LockKeyDecision DecideLockKey(const LockKeyEvent& e, bool foregroundIsLock, bool passwordActive);

/**
 *  锁屏期间对一个鼠标事件的判定：程序合成的（LLMHF_INJECTED）按键、滚轮事件拦截，合成的移动放行
 *  （防休眠每 25 秒合成一次零位移移动，见 CKeepAwakeSession::Nudge）；真实的鼠标事件一律放行
 *  （锁屏窗口盖住所有显示器，点到的只会是锁屏窗口）。
 *  @param message  鼠标消息（WM_MOUSEMOVE、WM_LBUTTONDOWN、WM_MOUSEWHEEL 等）
 *  @param injected 是否程序合成
 *  @return 放行或拦截
 */
LockKeyDecision DecideLockMouse(uint32_t message, bool injected);

} // namespace flamingolock
