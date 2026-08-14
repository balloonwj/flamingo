/**
 *  NotificationSound.h
 *
 *  客户端通知提示音的<u>纯逻辑模型 + 异步播放入口</u>。把"哪个事件该响哪个 wav、
 *  好友 presence 怎样跳变才算'刚上线'"这些可判定的逻辑，从带窗口 / Win32 副作用的
 *  调用点（CBalloonMainPanel 收消息 / 上线处理）里抽出，做成不依赖 HWND、不依赖
 *  Flamingo 业务对象的纯函数，便于 FlamingoUnit 测试工程直接断言；真正调
 *  ::sndPlaySound 异步出声的部分（PlayNotifySound）单独放在 NotificationSoundPlayer.cpp，
 *  只编进 Flamingo 主工程、不污染测试工程。
 *
 *  典型用法：
 *      // 收到他人新消息、且其聊天窗未打开时：
 *      balloonmain::PlayNotifySound(balloonmain::kNotifySoundNewMessage, muted);
 *      // 好友从离线跳变为在线时：
 *      if (balloonmain::IsBuddyJustCameOnline(oldPresence, newPresence))
 *          balloonmain::PlayNotifySound(balloonmain::kNotifySoundBuddyOnline, muted);
 *
 *  balloonwj@qq.com   2026-06-05
 */

#pragma once

namespace balloonmain {

/**
 *  NotifySoundKind —— 通知提示音的种类，决定播放 Bin\Sound\ 下哪个 wav 文件。
 */
enum NotifySoundKind
{
    kNotifySoundNewMessage   = 0,   // 收到他人发来的新单聊 / 群聊消息 → Sound\msg.wav
    kNotifySoundBuddyOnline  = 1,   // 好友从离线跳变为在线 / 离开 / 忙碌（上线）→ Sound\global.wav
    kNotifySoundAnnouncement = 2,   // 收到公告通知 → Sound\system.wav（本期暂不接入，留作后续公告功能调用）

    // ---- 音视频通话音效 ----------------------------------------------------
    // 前两种是<u>循环</u>播放（响到状态改变为止），须用 StartLoopSound / StopLoopSound；
    // 后三种是一次性的，用 PlayNotifySound 即可。
    //
    // 素材为正弦合成，见 Bin\\Sound\\call_*.wav。回铃与忙音取 450Hz 中国电信标准音。
    kNotifySoundCallOutgoing  = 3,  // 本端呼出、等对方接听的回铃音（循环）→ Sound\\call_outgoing.wav
    kNotifySoundCallIncoming  = 4,  // 收到来电的铃声（循环）→ Sound\\call_incoming.wav
    kNotifySoundCallConnected = 5,  // 通话接通的提示（一次性）→ Sound\\call_connected.wav
    kNotifySoundCallEnded     = 6,  // 通话正常结束的提示（一次性）→ Sound\\call_ended.wav
    kNotifySoundCallBusy      = 7,  // 对方拒绝 / 无应答 / 忙 / 不在线（一次性）→ Sound\\call_busy.wav
};

/**
 *  某种提示音是否为"循环播放"类。循环类须经 StartLoopSound 起、StopLoopSound 停；
 *  一次性类用 PlayNotifySound 播一遍即可。纯函数。
 *
 *    kind：提示音种类。
 *
 *  返回：true 表示循环类（当前为呼出回铃与来电铃声两种）。
 */
bool IsLoopSound(NotifySoundKind kind);

/**
 *  取某种提示音对应的 wav 文件相对路径（相对程序所在目录，含子目录 Sound\）。
 *  纯函数、无副作用。
 *
 *    kind：提示音种类（见 NotifySoundKind）。
 *
 *  返回：形如 L"Sound\\msg.wav" 的宽字符常量串（进程级静态生命周期，调用方可直接
 *        拼到 GetAppPath() 之后使用）；kind 非法时返回空串 L""。
 */
const wchar_t* NotifySoundRelativePath(NotifySoundKind kind);

/**
 *  判断好友的一次 presence 变化是否构成"刚上线" —— 即由不可见态（离线）
 *  跳变为可见在线态（在线 / 离开 / 忙碌），据此决定是否播放上线提示音。纯函数。
 *
 *  设计意图：只在"真正上线"这一跳变响一次，避免在线↔离开↔忙碌之间来回切换、或本来
 *  就在线又收到一条状态刷新时反复出声。presence 取值对齐 presence_type
 *  （离线 0 / 在线 1 / 离开 2 / 忙碌 3，见 net/Protocol.h）。
 *
 *    oldPresence：变化前的 presence 值（通讯录缓存里的旧值；查不到时调用方按离线 0 传入）。
 *    newPresence：变化后的 presence 值（服务端推送的新值）。
 *
 *  返回：true 表示这是一次"上线"跳变、应当播放上线提示音；否则 false。
 */
bool IsBuddyJustCameOnline(int oldPresence, int newPresence);

/**
 *  判断好友的一次 presence 变化是否构成"刚下线" —— 即由可见在线态（在线 / 离开 /
 *  忙碌）跳变为不可见态（离线）。与上面那条<u>严格对称</u>，共用同一套
 *  "可见在线"口径（IsVisibleOnline），故两者不会对同一次跳变都成立、也不会都不成立。
 *
 *  用途：「特别关注」的桌面提示要在被关注者上线<u>和</u>下线时各弹一次，故需要这条。
 *  上线提示音不用它（下线不出声）。
 *
 *  与上线同理，在线↔离开↔忙碌之间的来回切换<u>不算</u>下线 —— 对方锁个屏就提示一次，
 *  一上午能弹十几次。
 *
 *    oldPresence：变化前的 presence 值（通讯录缓存里的旧值；查不到时调用方按离线 0 传入）。
 *    newPresence：变化后的 presence 值（服务端推送的新值）。
 *
 *  返回：true 表示这是一次"下线"跳变；否则 false。
 */
bool IsBuddyJustWentOffline(int oldPresence, int newPresence);

/**
 *  按种类异步播放通知提示音。内部用 ::sndPlaySound(..., SND_ASYNC) 提交播放，<u>不阻塞</u>
 *  调用线程（须在 UI 线程调用）。实现位于 NotificationSoundPlayer.cpp（仅 Flamingo
 *  主工程编译，带 Win32 / 程序路径依赖；不进 FlamingoUnit 测试工程）。
 *
 *    kind： 提示音种类（见 NotifySoundKind）。
 *    muted：当前是否已"关闭所有声音"（即 CUserConfig::IsEnableMute()）；为 true 时本
 *           函数直接静默返回、不出声。托盘"关闭所有声音"与系统设置"提示音"开关写的是
 *           同一个静音位，故此处一处门控即同时受这两个开关控制。
 *
 *  无返回值；副作用为向系统提交一次异步声音播放（在静音 / 路径非法 / 文件缺失时什么都不做）。
 */
void PlayNotifySound(NotifySoundKind kind, bool muted);

/**
 *  开始循环播放某种提示音（呼出回铃 / 来电铃声）。内部用 ::PlaySound 的
 *  SND_LOOP | SND_ASYNC 提交，不阻塞调用线程（须在 UI 线程调用）。
 *
 *  <u>同一时刻只允许一路循环</u>：再次调用会先停掉上一路。呼叫与来电不会并存，
 *  这一约束足够，也避免了多路铃声叠加。
 *
 *    kind： 提示音种类；非循环类（见 IsLoopSound）一律忽略、不出声。
 *    muted：是否已"关闭所有声音"，为 true 时静默返回。
 *
 *  副作用：向系统提交一次循环播放。<u>必须</u>由调用方在通话状态改变时调
 *  StopLoopSound 停掉 —— 忘停会一直响，性质同"关窗不停摄像头"。
 */
void StartLoopSound(NotifySoundKind kind, bool muted);

/**
 *  停止当前正在循环的提示音。无循环在播时为无副作用的空操作，故可放心重复调用、
 *  也可在各条收尾路径上都兜一次（推荐这么做：铃声停不下来比多调一次糟得多）。
 */
void StopLoopSound();

} // namespace balloonmain
