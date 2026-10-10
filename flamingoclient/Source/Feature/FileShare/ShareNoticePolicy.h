/**
 *  文件共享弹窗通知在主程序一侧的判定（纯函数，可单测），ShareNoticePolicy.h
 *
 *  第 4 期（docs/文件共享方案.md 第 14 节）：FlamingoFileShare.dll 每次要弹「文件共享动态」「文件共享传输」之前经
 *  IFileShareHost::QueryNoticePolicy 问宿主能不能弹，宿主按两条规则回答：
 *    - 系统设置打开了「勿扰时段」、且当前在 22:00 到次日 08:00 之间：不弹（丢弃）。时段与系统设置窗口上写的说明一致；
 *      勿扰时段此前只存开关，文件共享的弹窗是第一处按它压制的地方；
 *    - 锁屏期间：按 balloonlock::DecideDuringLock 的来源 kLockEvtFileShareNotice 处理（推迟，解锁后补弹）。
 *  退出程序、退出登录之前，还有文件共享传输没完成时，确认框里加一句提示（FileShareTransferExitWarning）。
 *
 *  典型用法（CFileShareHost::QueryNoticePolicy）：
 *      return balloonfileshare::DecideShareNoticePolicy(dndEnabled, localHour,
 *          balloonlock::DecideDuringLock(balloonlock::kLockEvtFileShareNotice, balloonlock::IsScreenLocked()));
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

namespace balloonfileshare {

//勿扰时段的起止（本地时间的小时，含起不含止）：22:00 到次日 08:00，与系统设置窗口上「22:00 - 08:00 不接收任何提醒」一致
const int kDoNotDisturbStartHour = 22;
const int kDoNotDisturbEndHour = 8;

/**
 *  当前时刻是否在勿扰时段内。
 *  @param localHour 本地时间的小时，0~23
 *  @return 在 [22:00, 24:00) 或 [00:00, 08:00) 内返回 true
 */
bool IsInDoNotDisturbPeriod(int localHour);

/**
 *  文件共享的弹窗通知此刻怎么处理。
 *  @param dndEnabled 系统设置里是否打开了「勿扰时段」
 *  @param localHour  本地时间的小时，0~23
 *  @param lockAction balloonlock::DecideDuringLock(kLockEvtFileShareNotice, 是否锁屏) 的结果（LockEventAction）
 *  @return fileshare::FileShareNoticePolicy 的取值：勿扰时段内丢弃；锁屏推迟；锁屏判定为丢弃时丢弃；其余照常
 */
int DecideShareNoticePolicy(bool dndEnabled, int localHour, int lockAction);

/**
 *  退出程序、退出登录的确认框里关于文件共享传输的一句提示。
 *  @param count  本人作为访问者、尚未结束的文件共享传输数
 *  @param logout 为真时说「登出」，否则说「退出」
 *  @return 提示文字；count 为 0 时返回空串
 */
std::wstring FileShareTransferExitWarning(uint32_t count, bool logout);

} // namespace balloonfileshare
