/**
 *  帖子群在主程序一侧的判定规则（纯函数，docs/帖子群方案.md 4.7、7.2）：
 *    - 帖子群功能是否可用：帖子群 DLL 已加载并初始化、且本人所属企业的开关打开（登录应答 features.threadgroup 或 1013）；
 *    - 某个群要不要从群列表、会话列表、全局搜索、选人与转发对象里隐藏，以及要不要计入总未读数；
 *    - 打开一个群会话时建群聊窗、交给帖子群模块，还是什么也不打开；
 *    - 主程序是否替 DLL 发出一条 2700 段命令。
 *  各处统一调用这里的函数，不在各处各写一份判断。写成纯函数便于单元测试（Tests/ThreadGroupHostTests.cpp，标签 [threadgroup]）。
 *
 *  典型用法：
 *      const bool available = balloonthread::IsThreadGroupAvailable(host.IsModuleReady(), userMgr.m_bThreadGroupFeature);
 *      if (balloonthread::IsHiddenGroup(brief.m_grouptype, available))
 *          continue;   // 这个群不进列表
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>

namespace balloonthread {

//群类型的取值，与 Source/net/Protocol.h 的 group_type 一致（本头文件不包含协议头，便于各处直接引用；
//单元测试 TGE2 核对两边数值相同）
const int kGroupTypeNormal = 0;   //普通群
const int kGroupTypeThread = 1;   //帖子群

//2700 段的号段，与 FlamingoThreadGroup/ThreadGroupApi.h 的 kThreadCmdFirst / kThreadCmdLast 一致
const uint32_t kThreadCmdFirst = 2700;
const uint32_t kThreadCmdLast = 2799;

//打开一个群会话时该怎么做
enum GroupOpenAction
{
    group_open_chat_window   = 1,   //普通群：建（或激活）群聊窗
    group_open_thread_window = 2,   //帖子群且功能可用：交给帖子群模块打开帖子群窗口
    group_open_nothing       = 3,   //帖子群但功能不可用（DLL 缺失或企业关闭了开关）：不建任何窗口，帖子群此时本应已被隐藏
};

/**
 *  帖子群功能是否可用。
 *  @param moduleReady    帖子群 DLL 已加载并初始化成功
 *  @param featureEnabled 本人所属企业的帖子群开关打开
 *  @return 两者都满足返回 true
 */
inline bool IsThreadGroupAvailable(bool moduleReady, bool featureEnabled)
{
    return moduleReady && featureEnabled;
}

/**
 *  判断一个群类型是不是帖子群（不认识的取值一律当普通群，与解析时的口径一致）。
 *  @param groupType 群类型
 */
inline bool IsThreadGroupType(int groupType)
{
    return groupType == kGroupTypeThread;
}

/**
 *  一个群是否要从各处列表中隐藏（同时不计入总未读数）。
 *  @param groupType 群类型
 *  @param available 帖子群功能是否可用（IsThreadGroupAvailable 的结果）
 *  @return 帖子群且功能不可用时返回 true；普通群总是 false
 */
inline bool IsHiddenGroup(int groupType, bool available)
{
    return IsThreadGroupType(groupType) && !available;
}

/**
 *  打开一个群会话时该怎么做。
 *  @param groupType 群类型
 *  @param available 帖子群功能是否可用
 *  @return 取值见 GroupOpenAction
 */
inline int DecideGroupOpen(int groupType, bool available)
{
    if (!IsThreadGroupType(groupType))
        return group_open_chat_window;
    return available ? group_open_thread_window : group_open_nothing;
}

/**
 *  收到一条群消息时，用户是否正看着这个帖子群（帖子群没有群聊窗，「窗口开着」「正在看」都按帖子群窗口算，
 *  docs/帖子群方案.md 7.2）。
 *  @param groupType      群类型
 *  @param moduleWatching 帖子群模块报告的「正在看」（CThreadGroupHost::IsWatching：窗口在前台且没有最小化）
 *  @param screenLocked   客户端此刻是否锁屏（锁屏时用户看不到任何窗口）
 *  @return 帖子群、模块报告正在看且没有锁屏时返回 true；普通群总是 false（普通群按群聊窗判断）
 */
inline bool IsWatchingThreadGroup(int groupType, bool moduleWatching, bool screenLocked)
{
    return IsThreadGroupType(groupType) && moduleWatching && !screenLocked;
}

/**
 *  主程序是否替 DLL 发出一条命令。
 *  @param cmd       命令号
 *  @param bodyLen   包体字节数
 *  @param connected 主连接是否已连上
 *  @return 号段在 2700~2799、包体非空且已连上时返回 true
 */
inline bool CanSendThreadCommand(uint32_t cmd, uint32_t bodyLen, bool connected)
{
    return connected && bodyLen > 0 && cmd >= kThreadCmdFirst && cmd <= kThreadCmdLast;
}

//登录成功（含断线重连）时帖子群模块该怎么处理
enum ThreadLoginAction
{
    thread_login_activate   = 1,   //开关打开：确保 DLL 已加载并让模块进入已登录状态（已打开的窗口重新登记并刷新）
    thread_login_deactivate = 2,   //开关已被关掉而模块还处于已登录状态（断线期间后台关了开关，重连时才在登录应答里看到）：
                                   //与运行期收到 1013 由开变关相同，关闭全部帖子群窗口
    thread_login_none       = 3,   //开关关闭、模块本来就没有激活：什么也不做
};

/**
 *  登录成功时帖子群模块该怎么处理（2026-10-08 实机发现：断线期间关掉开关，重连后帖子群从列表里隐藏了，
 *  已打开的帖子群窗口却还开着）。
 *  @param featureEnabled 登录应答里的帖子群开关
 *  @param moduleActive   模块此刻是否处于已登录状态（上一次连接激活过、之后没有退出登录）
 *  @return 取值见 ThreadLoginAction
 */
inline int DecideThreadLogin(bool featureEnabled, bool moduleActive)
{
    if (featureEnabled)
        return thread_login_activate;
    return moduleActive ? thread_login_deactivate : thread_login_none;
}

/**
 *  一个收到的帧是否要转给帖子群模块。
 *  @param cmd 命令号
 *  @return 落在 2700~2799 时返回 true
 */
inline bool IsThreadFrame(uint32_t cmd)
{
    return cmd >= kThreadCmdFirst && cmd <= kThreadCmdLast;
}

} // namespace balloonthread
