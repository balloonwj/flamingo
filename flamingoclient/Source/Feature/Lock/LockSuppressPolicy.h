/**
 *  锁屏期间客户端各类「自己冒出来」的东西怎么处理（纯函数，可单测）。
 *
 *  锁屏期间客户端照常在线、照常收消息，但不弹任何窗口、不放任何提示音；提醒类弹窗延到解锁后补弹。
 *  每一类来源对应一个 LockEventKind，接入点统一调用 DecideDuringLock，不各自判断（docs/锁屏方案.md 第 4.9 节）。
 *
 *  典型用法：
 *      if (balloonlock::DecideDuringLock(balloonlock::kLockEvtStarredPresence, balloonlock::IsScreenLocked())
 *          != balloonlock::kLockActNormal)
 *      {
 *          return;   // 丢弃
 *      }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

namespace balloonlock {

/**
 *  锁屏期间需要判定的来源。
 */
enum LockEventKind
{
    kLockEvtMessagePopup = 0,     // 新消息到达：自动打开聊天窗、闪任务栏（DecideArrival 的弹窗与任务栏部分）
    kLockEvtChatWindowRaise,      // 已打开的聊天窗收到消息时抖动、提到前台
    kLockEvtSubtitle,             // 桌面字幕显示新收到的消息原文
    kLockEvtAnnouncement,         // 平台公告弹窗
    kLockEvtGroupAnnouncement,    // 群公告弹窗
    kLockEvtStarredPresence,      // 特别关注的上线提醒
    kLockEvtMemoReminder,         // 备忘提醒（右下角弹窗、全屏提醒）
    kLockEvtUpdatePrompt,         // 自动升级的提示窗口
    kLockEvtForcedUpdateExit,     // 强制升级下载完后「启动更新程序并退出」
    kLockEvtKickout,              // 被挤下线的提示框（连接照常断开）
    kLockEvtRaisePanel,           // 断线重连成功后把主面板提到前台
    kLockEvtDockSlideOut,         // 停靠在屏幕边缘的主面板因光标靠边而滑出
    kLockEvtTrayBalloon,          // 托盘气泡提示（断线重连中、最小化到托盘）
    kLockEvtNotifySound,          // 提示音（新消息、上线、文件夹邀请、来电铃声等）
    kLockEvtGlobalHotkey,         // 全局快捷键（打开主面板、截图、录屏）
    kLockEvtFileShareNotice,      // 文件共享的右下角通知（别人操作了本人的共享、本人的传输结束，docs/文件共享方案.md 第 14 节）
    kLockEvtCount,                // 来源种数，不是一种来源
};

/**
 *  判定结果。
 */
enum LockEventAction
{
    kLockActNormal = 0,           // 照常处理（未锁屏时一律如此）
    kLockActDefer,                // 先不处理，记下来，解锁后按到达顺序补做
    kLockActDrop,                 // 丢弃（时效性的东西，解锁后再出现已无意义）
};

/**
 *  锁屏期间某一类来源怎么处理。
 *  @param kind   来源
 *  @param locked 当前是否锁屏中；为 false 时一律返回 kLockActNormal
 *  @return 处理方式
 */
LockEventAction DecideDuringLock(LockEventKind kind, bool locked);

/**
 *  解锁时要不要重新注册登录后才有的全局快捷键（「打开主面板」Ctrl+Alt+D、录屏 Ctrl+Alt+R）：锁屏前（或锁屏期间）
 *  已经登录的才恢复；登录前锁屏、解锁时仍未登录的不注册。截图快捷键 2026-10-07 起不论是否登录都恢复，不受本函数
 *  影响（balloonscreenshot::ResumeAppHotkeys，docs/截图独立模块方案.md 3.5 节）。
 *  @param loggedIn 解锁时是否已登录
 *  @return 需要重新注册返回 true
 */
bool ShouldResumeHotkeysAfterUnlock(bool loggedIn);

} // namespace balloonlock
