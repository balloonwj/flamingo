/**
 *  ReconnectThrottle.h —— 断线重连时登录请求的节流：一轮重连里同一时刻只有一个登录请求在途。
 *
 *  背景：掉线后重连定时器每隔 reconnectinterval（默认 5 秒）触发一次，
 *  原先每次都调 CFlamingoClient::Login() 并弹一次"正在重连"的托盘气泡，不管上一次登录有没有
 *  结果。Login() 只是把请求放进发送线程的队列，单次尝试最多要等连接 3 秒、应答 3 秒，可能比
 *  定时器间隔还长，于是请求在队列里越积越多。本类记录"已有一个请求在途、还在等结果"，等到
 *  结果（成功或失败）再放行下一个。
 *
 *  兜底：按代码看，每个入队的登录请求最终都会投回一个结果（发送线程的 HandleLogon 只有一个
 *  出口）。万一结果没有回来，连续 kReconnectResultWaitTicks 次触发之后认为那次请求已丢失，
 *  照常重发，决不让重连因此停摆。
 *
 *  纯逻辑，不碰窗口、定时器与网络，FlamingoUnit 直接断言（Tests/ReconnectThrottleTests.cpp）。
 *  本文件不依赖 stdafx.h，与 FlamingoUnit 共用同一份源码。
 *
 *  典型用法（会话控制器）：
 *      开始新一轮重连时：     m_reconnectThrottle.Reset();
 *      重连定时器触发时：     if (m_reconnectThrottle.OnTick() != kReconnectTickWait && Login())
 *                                 m_reconnectThrottle.OnRequestQueued();
 *      收到登录结果时：       m_reconnectThrottle.OnResult();
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

namespace balloonmain {

// 等一个登录请求的结果最多等多少次定时器触发，超过就认为那次请求已丢失。
// 重连间隔默认 5 秒，6 次约 30 秒；单次尝试最多约 6 秒（连接 3 秒 + 应答 3 秒），留有充分余量。
const int kReconnectResultWaitTicks = 6;

// 重连定时器触发一次时该怎么做。
enum ReconnectTickAction
{
    kReconnectTickSend        = 0,   // 发出登录请求：本轮的第一次，或上一个请求已经有了结果
    kReconnectTickWait        = 1,   // 上一个登录请求还没有结果，本次不发、也不弹托盘气泡
    kReconnectTickResendStale = 2,   // 等了 kReconnectResultWaitTicks 次仍没有结果，认为那次请求
                                     // 已丢失，重新发出（调用方应记一条警告日志）
};

/**
 *  ReconnectThrottle —— 一轮断线重连里登录请求的节流状态。说明见文件头。
 *  只在界面线程使用，不做线程同步。
 */
class ReconnectThrottle
{
public:
    ReconnectThrottle();

    // 开始新一轮重连时调用：丢弃上一轮遗留的等待状态（例如那次请求发出后用户切到了离线，
    // 结果回来时不在重连状态，没有复位等待标志），本轮第一次触发照常发出。
    void Reset();

    /**
     *  重连定时器触发时调用，返回本次该怎么做。
     *  @return 见 enum ReconnectTickAction。返回 kReconnectTickSend 或 kReconnectTickResendStale
     *          时调用方应发出登录请求，并在请求确实入队后调用 OnRequestQueued
     *  副作用：等待中时累计已等待的触发次数；判定为请求已丢失时清除等待状态
     */
    ReconnectTickAction OnTick();

    // 登录请求确实进入发送队列后调用：进入"等结果"状态，重新开始计数。请求没能入队
    //（如缺账号或密码）时不要调用，下一次触发会照常再试。
    void OnRequestQueued();

    // 收到登录结果（成功或失败）时调用：清除等待状态，下一次触发可以再发。
    void OnResult();

    // 当前是否有一个登录请求在途、还在等结果。
    bool IsWaiting() const;

private:
    bool m_waiting;       // 是否有一个登录请求在途、还在等结果
    int  m_waitedTicks;   // 进入等待状态以来定时器已触发的次数，OnRequestQueued 时清零
};

} // namespace balloonmain
