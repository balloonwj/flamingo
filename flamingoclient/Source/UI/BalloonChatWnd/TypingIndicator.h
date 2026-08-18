/**
 *  TypingIndicator.h
 *
 *  单聊「对方正在输入」提示的两个纯逻辑状态机 —— 发送侧节流器
 *  TypingSendThrottle 与接收侧状态 TypingPeerState。
 *
 *  之所以把它们从聊天窗里拆出来：这两段逻辑全是"什么时刻该做什么"的判断，
 *  与窗口、控件、网络都无关，独立出来才能直接写单元测试（见
 *  Tests/TypingIndicatorTests.cpp）。二者都不自己取时钟，当前时刻由调用方
 *  传入（通常是 GetTickCount64() 的毫秒值），与同目录的 ShakeRateLimiter 同一路数。
 *
 *  协议背景：cmd_typing_notify(1510) 是一条"不落库、不应答、不保证可靠"的
 *  提示包（见 docs/协议设计-v2.md 10.7）。正因为不可靠，发送侧要在持续输入
 *  时定期续发，接收侧要给显示状态设过期时间 —— 否则对端掉线时那句"正在
 *  输入…"会永远挂在界面上。
 *
 *  典型用法（发送侧，聊天窗内）：
 *      // 输入框内容变化时
 *      TypingAction act = m_typingThrottle.OnInputChanged(::GetTickCount64(), text.IsEmpty());
 *      // 定时器每秒一次
 *      TypingAction act = m_typingThrottle.OnTick(::GetTickCount64());
 *      // 按 act 决定发不发包、发 1 还是发 0
 *
 *  balloonwj@qq.com   2026-08-05
 */

#pragma once

namespace balloonchatwnd {

//==================== 时间参数 ====================

//发送侧：持续输入时两次 state=1 之间的续发间隔（毫秒）。
//取 4 秒是为了配合接收侧 6 秒的过期时间 —— 只要人还在打字，续发包总能赶在
//对端过期之前到达，中间还留了 2 秒余量容忍网络抖动。
const unsigned long long kTypingRepeatMs = 4000;

//发送侧：停手多久算"不再输入"、该发 state=0（毫秒）。
//3 秒是打字停顿与真正停手之间比较自然的分界：想下一句话的停顿通常短于它，
//切走去干别的事则明显长于它。
const unsigned long long kTypingIdleMs = 3000;

//接收侧：收到 state=1 后多久没有新消息就自动收起提示（毫秒）。
//必须大于发送侧的续发间隔 kTypingRepeatMs，否则对方还在打字提示就先没了。
const unsigned long long kTypingExpireMs = 6000;

//==================== 发送侧 ====================

/**
 *  节流器要求调用方执行的动作。调用方拿到后据此决定发不发包、发什么。
 */
enum TypingAction
{
    typing_action_none = 0,      //无需动作：本次变化不必让对端知道（节流窗内、或状态没变）
    typing_action_send_typing,   //发一包 state=1：告诉对端"我正在输入"
    typing_action_send_stop,     //发一包 state=0：告诉对端"我不输入了"
};

/**
 *  TypingSendThrottle —— 本端打字的发包节流器。
 *
 *  语义要点：
 *    · 从"没在通告"变为"有内容在输入"时立刻发一包 state=1，让对端第一时间看到；
 *    · 持续输入期间每 kTypingRepeatMs 才续发一包，不会每敲一键发一次；
 *    · 停手满 kTypingIdleMs 由 OnTick 发一包 state=0；
 *    · 输入框被清空、消息已发出、失焦、窗口关闭都立即收尾（发 state=0）；
 *    · 已经收尾之后不会重复发 state=0。
 *
 *  一个节流器对应一个会话（一个聊天窗一份）。
 */
class TypingSendThrottle
{
public:
    TypingSendThrottle()
        : m_active(false)
        , m_lastSentTick(0)
        , m_lastInputTick(0)
    {
    }

    /**
     *  输入框内容可能发生了变化时调用（打字、退格、粘贴、输入法上屏都算）。
     *    now：       当前时刻（毫秒）。
     *    inputEmpty：调用时输入框是否为空。为空说明用户把内容删光了，
     *                此时应当立即收尾，而不是继续通告"正在输入"。
     *  返回：调用方应执行的动作。
     */
    TypingAction OnInputChanged(unsigned long long now, bool inputEmpty)
    {
        if (inputEmpty)
        {
            //删光了：等同于停手，立即收尾（若本就没通告过则什么都不用做）
            return OnStop();
        }

        m_lastInputTick = now;

        if (!m_active)
        {
            //首次有内容：立刻通告，别让对端等到下一个续发周期
            m_active = true;
            m_lastSentTick = now;
            return typing_action_send_typing;
        }

        //已在通告中：只有距上次发包满了续发间隔才再发一包
        if (now - m_lastSentTick >= kTypingRepeatMs)
        {
            m_lastSentTick = now;
            return typing_action_send_typing;
        }

        return typing_action_none;
    }

    /**
     *  定时器 tick，用于检测"停手"。建议 1 秒调一次。
     *    now：当前时刻（毫秒）。
     *  返回：停手已满 kTypingIdleMs 时返回 typing_action_send_stop（并转入
     *        未通告态），否则返回 typing_action_none。
     */
    TypingAction OnTick(unsigned long long now)
    {
        if (!m_active)
        {
            return typing_action_none;
        }

        if (now - m_lastInputTick >= kTypingIdleMs)
        {
            m_active = false;
            return typing_action_send_stop;
        }

        return typing_action_none;
    }

    /**
     *  立即收尾：消息已发出、输入框失焦、窗口关闭、连接断开时调用。
     *  返回：此前确实在通告则返回 typing_action_send_stop（并转入未通告态）；
     *        本就没通告过则返回 typing_action_none（不发重复的 0 包）。
     */
    TypingAction OnStop()
    {
        if (!m_active)
        {
            return typing_action_none;
        }

        m_active = false;
        return typing_action_send_stop;
    }

    /**
     *  当前是否处于"已向对端通告正在输入、尚未通告停止"的状态。
     */
    bool IsActive() const
    {
        return m_active;
    }

    /**
     *  复位为初始状态，且<u>不</u>产生任何动作。用于"对端已经不可能再收到我们的
     *  包"的场合（如连接已断），此时发 state=0 也没有意义。
     */
    void Reset()
    {
        m_active = false;
        m_lastSentTick = 0;
        m_lastInputTick = 0;
    }

private:
    bool m_active;                        // 是否已通告"正在输入"且尚未通告停止
    unsigned long long m_lastSentTick;    // 上次发出 state=1 的时刻（毫秒）；仅 m_active 时有意义
    unsigned long long m_lastInputTick;   // 上次检测到输入变化的时刻（毫秒），用于判停手
};

//==================== 接收侧 ====================

/**
 *  TypingPeerState —— 对端"正在输入"的显示状态。
 *
 *  语义要点：
 *    · 收到 state=1 即进入"正在输入"，并把过期时刻往后推；
 *    · 收到 state=0 立即收起；
 *    · 对端掉线 / 崩溃导致 state=0 永远不来时，由 OnTick 在 kTypingExpireMs
 *      之后自动收起 —— 这是本类存在的主要理由；
 *    · 各方法都返回"显示状态是否发生了变化"，调用方据此决定要不要重绘，
 *      避免每秒无谓地刷界面。
 *
 *  一个状态对象对应一个会话（一个聊天窗一份）。
 */
class TypingPeerState
{
public:
    TypingPeerState()
        : m_typing(false)
        , m_expireTick(0)
    {
    }

    /**
     *  收到对端的 cmd_typing_notify 推送时调用。
     *    now：  当前时刻（毫秒）。
     *    state：推送里的输入状态，1 表示输入中、0 表示停止；其余值按停止处理。
     *  返回：显示状态发生了变化返回 true（调用方须刷新界面），否则 false。
     */
    bool OnNotify(unsigned long long now, int state)
    {
        if (state == 1)
        {
            //每收到一包就把过期时刻往后推，只要对方还在打字提示就不会消失
            m_expireTick = now + kTypingExpireMs;
            if (m_typing)
            {
                return false;
            }
            m_typing = true;
            return true;
        }

        return Clear();
    }

    /**
     *  定时器 tick，用于兜底过期。建议 1 秒调一次。
     *    now：当前时刻（毫秒）。
     *  返回：因过期而由"正在输入"变为"未在输入"返回 true，否则 false。
     */
    bool OnTick(unsigned long long now)
    {
        if (!m_typing)
        {
            return false;
        }

        //到点仍没等到新的 state=1，说明对端多半已经掉线，收起提示
        if (now >= m_expireTick)
        {
            m_typing = false;
            return true;
        }

        return false;
    }

    /**
     *  立即收起提示：收到对端发来的正式消息、窗口关闭、连接断开时调用。
     *  返回：此前正显示"正在输入"返回 true（须刷新界面），否则 false。
     */
    bool Clear()
    {
        if (!m_typing)
        {
            return false;
        }

        m_typing = false;
        m_expireTick = 0;
        return true;
    }

    /**
     *  对端当前是否正在输入（界面据此决定画不画那句提示）。
     */
    bool IsTyping() const
    {
        return m_typing;
    }

private:
    bool m_typing;                      // 对端当前是否正在输入
    unsigned long long m_expireTick;    // 提示的过期时刻（毫秒）；仅 m_typing 时有意义
};

} // namespace balloonchatwnd
