/**
 *  NotifyModePolicy.h
 *
 *  收到一条消息（或文件夹传输邀请）时，按会话的消息提醒模式决定「计不计未读、动不动会话行、
 *  闪不闪托盘、响不响提示音、弹不弹聊天窗」的<u>纯函数</u>。行为表见
 *  docs/消息提醒模式与消息盒子方案.md §3.1。
 *
 *  之所以抽出来：主面板收单聊消息、收群消息、收文件夹邀请三条路径都要做同一套判断，散在
 *  CBalloonMainPanel 里各写一份必然走样，也没法脱离窗口断言。本模块不碰窗口、不碰网络，
 *  可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里
 *  本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  调用顺序（与窗口抖动现有的处理一致）：
 *      1. 用收到消息<u>那一刻</u>的状态（窗口开没开、用户在不在看）调 DecideArrival；
 *      2. m_popupWindow 为真时先把聊天窗建出来并提到前台；
 *      3. 按 m_updateRow / m_countUnread 更新会话行；
 *      4. 重新判断用户此刻是否正看着聊天窗（刚弹出来的窗口在前台就算在看），在看就把该会话
 *         标为已读 —— 弹出的窗口里消息已当场被看到，不该再留未读；
 *      5. 按 m_flashTray / m_flashTaskbar / m_playSound 提醒。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <vector>

#include "../../net/ConversationsProtocolClient.h"   // net::ConversationItem

namespace balloonmain {

class NotifyModeModel;

/**
 *  ArrivalInput —— 判定一条到达消息所需的状态。全部取收到消息<u>那一刻</u>、弹出聊天窗之前的值。
 */
struct ArrivalInput
{
    int  m_mode;        // 该会话的提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode
    bool m_isGroup;     // 是否群消息；单聊为 false
    bool m_atMe;        // 是否 @ 到本人（含 @所有人）；单聊恒为 false
    bool m_countable;   // 这条消息是否「别人发给我的」：ConversationsProtocolClient::shouldCountAsUnread
                        // 的结果。自己从别的设备同步回来的、服务端生成的群系统消息为 false
    bool m_windowOpen;  // 该会话的聊天窗此刻是否已打开（不论是否在前台）
    bool m_watching;    // 用户此刻是否正看着该聊天窗（可见、未最小化、在前台）
    bool m_isShake;     // 是否单聊的窗口抖动
    bool m_screenLocked;// 客户端此刻是否处于锁屏中（docs/锁屏方案.md）：锁屏时不弹窗、不闪任务栏、不响，
                        // 会话行、未读数与托盘闪烁照常；m_watching 按 false 处理（用户看不到任何窗口）
    bool m_fromFollowed;// 发送人是否在本人对这个群的关注名单里（docs/消息提醒模式与消息盒子方案.md §12）；
                        // 单聊恒为 false。只在模式 5「只提醒 @我和关注的人」下影响提醒
    bool m_hidden;      // 该会话此刻是否从各处列表里隐藏（帖子群功能不可用时的帖子群，docs/帖子群方案.md 4.7）：
                        // 会话数据与未读数照常更新，但不弹窗、不闪托盘与任务栏、不响提示音

    ArrivalInput()
        : m_mode(0)
        , m_isGroup(false)
        , m_atMe(false)
        , m_countable(false)
        , m_windowOpen(false)
        , m_watching(false)
        , m_isShake(false)
        , m_screenLocked(false)
        , m_fromFollowed(false)
        , m_hidden(false)
    {
    }
};

/**
 *  ArrivalDecision —— 判定结果，各项含义见每个字段的注释。
 */
struct ArrivalDecision
{
    bool m_popupWindow;   // 建出（如需）并把聊天窗提到前台：单聊窗口抖动、特别关注的会话窗口没开时
    bool m_updateRow;     // 更新会话行的摘要、时间与位置（不在列表里时新建一行）；屏蔽的会话为 false
    bool m_countUnread;   // 会话未读数 +1（之后若用户正看着窗口，调用方会再整体标为已读）
    bool m_flashTray;     // 在托盘闪该会话头像并进托盘浮窗的名单
    bool m_flashTaskbar;  // 闪烁已打开聊天窗的任务栏按钮、不把窗口提到前台：特别关注的会话窗口开着但没在看时
    bool m_playSound;     // 播放新消息提示音（仍受全局「关闭所有声音」约束，由调用方处理）

    ArrivalDecision()
        : m_popupWindow(false)
        , m_updateRow(false)
        , m_countUnread(false)
        , m_flashTray(false)
        , m_flashTaskbar(false)
        , m_playSound(false)
    {
    }
};

/**
 *  判定一条到达消息该怎么处理。纯函数。
 *    in： 收到消息那一刻的状态，见 ArrivalInput。
 *  返回：判定结果，见 ArrivalDecision 与 docs/消息提醒模式与消息盒子方案.md §3.1。
 */
ArrivalDecision DecideArrival(const ArrivalInput& in);

/**
 *  判断某种模式下会话行的未读角标是否画成灰色（「有新消息，但不必打扰你」）。
 *  接收不提醒、收进消息盒子、只提醒 @我和关注的人三种为 true；屏蔽的会话不画角标，另见 ShowsUnreadBadge。
 */
bool IsQuietBadgeMode(int mode);

/**
 *  判断某种模式下会话行是否显示未读角标：屏蔽的会话不显示（服务端照常计数，本端忽略），
 *  其余模式显示。
 */
bool ShowsUnreadBadge(int mode);

/**
 *  判断会话行要不要画「未读小红点」（会话被手动标为未读，docs/会话标为未读方案.md）。纯函数。
 *    unread： 会话的未读数；大于 0 时以数字胶囊为准，不画红点。
 *    marked： 会话是否被手动标为未读。
 *    mode：   会话的提醒模式；屏蔽的会话不画（与 ShowsUnreadBadge 同口径）。灰色与否由行的提醒模式决定，
 *             不在本函数里判断。
 *  @return 要画返回 true
 */
bool ShowsUnreadDot(int unread, bool marked, int mode);

/**
 *  判断把会话改成某种模式时，是否要同时清掉它正在闪的托盘提醒：改成接收不提醒、收进消息盒子、
 *  屏蔽、只提醒 @我和关注的人四种「平时不提醒」的模式时为 true；改成允许提醒、特别关注时为 false。
 */
bool ShouldClearTrayOnModeChange(int newMode);

/**
 *  判断一次模式变化是不是「解除屏蔽」：原来是屏蔽、改成了别的模式。解除屏蔽时服务端会把已读位点
 *  推进到该会话最新一条（docs/消息提醒模式与消息盒子方案.md §3.2），客户端要跟着把本地未读清零，
 *  并重拉会话列表取回屏蔽期间停住的摘要与时间。
 *    oldMode / newMode：变化前后的模式（enum conversation_notify_mode）。
 */
bool IsLeavingBlock(int oldMode, int newMode);

/**
 *  统计「消息」页签红色角标的未读总数：只算允许提醒与特别关注两种模式的会话
 *  （2026-10-03 起；此前免打扰会话的未读也算在内）。
 *    items： 会话列表（主面板的 m_conversations）。
 *    modes： 模式表。
 *  返回：未读总数；负的未读数（脏数据）按 0 处理。
 */
int ComputeAlertingUnread(const std::vector<net::ConversationItem>& items, const NotifyModeModel& modes);

} // namespace balloonmain
