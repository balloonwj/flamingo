/**
 *  屏幕共享与独立录制的互斥规则，ShareRecordMutex.h
 *
 *  2026-10-07 与用户确认的规则（docs/屏幕共享与录制方案.md 第 13 节）：
 *    - 独立录制进行中（含录制选择窗已打开、框选遮罩中）：不能发起屏幕共享，所有入口都算（聊天窗、群聊窗、主面板菜单、
 *      托盘、联系人右键、通话窗「共享屏幕」、多人共享里观看者获批后的选择窗与开始推流）；
 *    - 自己正在共享（主持人，或多人共享里自己正在共享，含共享选择窗已打开）：不能发起独立录制；共享控制条上的
 *      「录下这场共享」不受影响（它是共享录制，不是独立录制）；
 *    - 只是在观看别人的共享：不限制独立录制。
 *  正在录制时再发起录制由录制控制器原有的判断负责。正在共享（或正在观看别人的共享）时再发起共享，2026-10-07 起也并进来
 *  （CheckShareStartConflict），被拦下时与互斥的两种一样用轻量提示（红色 toast），不再弹模态消息框。
 *  纯函数，不碰窗口，单元测试见 Source/Tests/ShareRecordMutexTests.cpp（标签 [sharerecmutex]）。
 *
 *  典型用法：
 *      发起独立录制（CMediaModule::StartRecording）：
 *          const int conflict = CheckScreenStartConflict(screen_start_record, false, shareBusy);
 *      发起共享（CShareController::StartShare）：
 *          const int conflict = CheckShareStartConflict(hosting, watching, recordBusy);
 *      if (conflict != screen_start_ok) { 提示 ScreenStartConflictText(conflict)；return false; }
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>

namespace screenshare {

//要发起的是哪一种
enum ScreenStartKind
{
    screen_start_share  = 1,   //发起屏幕共享（含通话中共享、多人共享里观看者开始共享自己的屏幕）
    screen_start_record = 2,   //发起独立录制（录屏、录白板、只录声音）
};

//检查结果
enum ScreenStartConflict
{
    screen_start_ok                  = 0,   //可以发起
    screen_start_blocked_by_record   = 1,   //独立录制进行中（或录制选择窗、框选遮罩开着），不能发起共享
    screen_start_blocked_by_share    = 2,   //自己正在共享（或共享选择窗开着），不能发起独立录制
    screen_start_blocked_by_hosting  = 3,   //自己正在共享（主持人从打开选择窗到结束共享），不能再发起一场共享
    screen_start_blocked_by_watching = 4,   //正在观看别人的共享，要先离开才能发起共享
};

/**
 *  发起之前检查共享与独立录制是否冲突。
 *  @param kind           要发起的是哪一种，取值见 ScreenStartKind；其余取值一律放行
 *  @param recordBusy     独立录制是否占用中：正在录制（不含共享录制）、录制选择窗开着、或正在框选区域
 *  @param shareBusy      自己是否正在共享：主持人从打开选择窗到结束共享的整个过程，或多人共享里自己从打开选择窗到停止共享
 *  @return 取值见 ScreenStartConflict
 */
int CheckScreenStartConflict(int kind, bool recordBusy, bool shareBusy);

/**
 *  发起屏幕共享之前的完整检查：同类冲突（自己正在共享、正在观看别人的共享）与独立录制互斥。
 *  先后顺序与 2026-10-07 之前 StartShare 里的判断一致：先看自己是否在共享，再看是否在观看，最后看独立录制。
 *  @param hosting    自己是否正在共享：主持人从打开选择窗到结束共享的整个过程
 *  @param watching   是否正在观看别人的共享（从加入到离开）
 *  @param recordBusy 独立录制是否占用中，含义同 CheckScreenStartConflict
 *  @return 取值见 ScreenStartConflict：screen_start_ok、screen_start_blocked_by_hosting、
 *          screen_start_blocked_by_watching 或 screen_start_blocked_by_record
 */
int CheckShareStartConflict(bool hosting, bool watching, bool recordBusy);

/**
 *  被拦下时给用户看的提示（已经过 TR 翻译）。
 *  @param conflict CheckScreenStartConflict 或 CheckShareStartConflict 的结果
 *  @return 提示文字；screen_start_ok 或未知取值时为空
 */
std::wstring ScreenStartConflictText(int conflict);

} // namespace screenshare
