/**
 *  通话中共享屏幕的纯判断（docs/屏幕共享与录制方案.md 第 8 期）。
 *
 *  一对一通话接通后，任一方可以在通话窗里点「共享屏幕」，把这通通话所在的单聊变成一场屏幕共享：
 *  对端收到「开始共享」推送时不弹右下角提示，直接加入观看；双方的声音继续经由通话传输，共享房间里
 *  不再另开麦克风与摄像头（否则对方会听到两遍、摄像头也会被两边争用）。
 *
 *  本文件只做这几项判断，不碰窗口与网络，全部为 header-only 的 inline 函数，由 FlamingoUnit 直接测试。
 *  真正的按钮、自动加入与提示仍在 CCallController / CShareController 里。
 *
 *  典型用法：
 *      if (screenshare::IsInCallShare(m_callPeerId, ctype, cid)) ...                // 当前共享是否走通话的声音
 *      switch (screenshare::DecideInCallShareClick(hostingWithPeer, hostBusy, viewing)) ...
 *      if (screenshare::ShouldAutoJoinInCall(n.m_ctype, n.m_hostId, m_callPeerId, idle)) JoinShare(...);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

//通话窗「共享屏幕」按钮被点击后要做的事
enum InCallShareClickAction
{
    //现在没有共享、也没有在看共享：打开选择窗，发起与通话对端的共享
    in_call_share_start = 0,
    //正在与通话对端共享（含正在选择内容、等服务端应答）：结束这场共享
    in_call_share_stop = 1,
    //正在别的会话里共享：提示先结束那一场，不发起
    in_call_share_busy_hosting = 2,
    //正在观看某个共享（含通话对端的共享）：提示先离开，不发起
    in_call_share_busy_viewing = 3,
};

/**
 *  判断一场共享是否属于「通话中共享」：本端正在一对一通话，共享在单聊里，且共享的另一方就是通话对端。
 *  属于时共享房间不开麦克风与摄像头，声音经由通话传输。
 *  @param callPeerId  本端当前已接通的通话的对端 userid；没有通话时为 0
 *  @param ctype       共享所在会话的类型：1 单聊，2 群聊（与 conversation_type 取值一致）
 *  @param sharePeerId 共享的另一方：本端发起时为单聊对端，本端观看时为发起人
 *  @return 属于通话中共享返回 true
 */
inline bool IsInCallShare(int64_t callPeerId, int ctype, int64_t sharePeerId)
{
    //单聊会话类型的取值，与 conversation_type 的 conversation_single 一致（本文件不引协议头，单列一份）
    const int kSingleChat = 1;
    return callPeerId != 0 && ctype == kSingleChat && sharePeerId == callPeerId;
}

/**
 *  决定通话窗「共享屏幕」按钮被点击后的动作。
 *  @param hostingWithPeer 本端是否正在与通话对端共享（选择内容、等应答、推流中都算）
 *  @param hostingOther    本端是否正在别的会话里共享（hostingWithPeer 为 true 时忽略）
 *  @param viewing         本端是否正在观看某个共享
 *  @return 取值见 InCallShareClickAction
 */
inline int DecideInCallShareClick(bool hostingWithPeer, bool hostingOther, bool viewing)
{
    if (hostingWithPeer)
        return in_call_share_stop;
    if (hostingOther)
        return in_call_share_busy_hosting;
    if (viewing)
        return in_call_share_busy_viewing;
    return in_call_share_start;
}

/**
 *  收到「有人开始共享」推送时，是否不弹提示、直接加入观看：单聊、发起人正是通话对端、本端此刻既没有在共享也没有在观看。
 *  @param ctype      共享所在会话的类型：1 单聊，2 群聊
 *  @param hostId     发起人 userid
 *  @param callPeerId 本端当前已接通的通话的对端 userid；没有通话时为 0
 *  @param idle       本端此刻是否既没有在共享也没有在观看
 *  @return 应直接加入返回 true；否则按原有规则弹提示
 */
inline bool ShouldAutoJoinInCall(int ctype, int64_t hostId, int64_t callPeerId, bool idle)
{
    return idle && IsInCallShare(callPeerId, ctype, hostId);
}

/**
 *  观看中的共享结束时，是否弹提示框告诉用户。通话中的共享是自动加入的，对方正常结束时只关观看窗
 *  （通话窗还在，看得出共享已结束）；对方网络中断导致的结束仍要提示，否则用户不知道原因。
 *  @param inCallShare 这场共享是否属于通话中共享（见 IsInCallShare）
 *  @param hostTimeout 结束原因是否为「发起人网络中断、超过宽限期」
 *  @return 需要弹提示框返回 true
 */
inline bool ShouldPromptShareEnded(bool inCallShare, bool hostTimeout)
{
    return !inCallShare || hostTimeout;
}

/**
 *  通话窗「共享屏幕」按钮的文字。
 *  @param hostingWithPeer 本端是否正在与通话对端共享
 *  @return 「共享屏幕」或「停止共享」
 */
inline const wchar_t* InCallShareButtonText(bool hostingWithPeer)
{
    return hostingWithPeer ? TR(L"停止共享") : TR(L"共享屏幕");
}

} // namespace screenshare
