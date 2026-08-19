/**
 *  会话状态宿主接口：主面板的各个协作类需要向宿主打听或请求的事项，集中在这一个接口上。
 *
 *  背景：托盘控制器要按在线状态与连接态决定图标形态，自动离开控制器要按在线状态决定
 *  该不该切走，两者问的是同一批问题，请求的也是同一批动作（切换在线状态 / 登出 / 退出）。
 *  这些状态既不归托盘、也不归自动离开，而是归登录流程与在线状态管理。把它们收进一个
 *  接口，协作类就不必知道由谁实现 —— 目前由会话控制器实现。
 *
 *  本接口只依赖基本类型，不引入任何头文件，任何协作类都可以放心包含。
 *
 *  balloonwj@qq.com   2026-08-19
 */

#pragma once

namespace balloonmain {

/**
 *  会话状态宿主。实现者须保证所有方法都在主线程（界面线程）上被调用。
 */
struct ISessionStateHost
{
    virtual ~ISessionStateHost() {}

    // 当前在线状态（kMyPresenceOnline / kMyPresenceAway / kMyPresenceBusy /
    // kMyPresenceOffline，取值见 Session/UserSessionData.h 的 MyPresenceValue）。
    // 托盘图标的状态角标、悬停提示的状态文字、托盘右键菜单里标出「当前」的那一项，
    // 以及自动离开的判定输入，都据它决定。
    virtual int GetCurrentPresence() = 0;

    // 当前界面连接态（kConnUiOnline / kConnUiConnecting / kConnUiOffline，取值见
    // UI/BalloonMain/OnlineCapability.h 的 ConnectionUiState）。掉线时托盘图标转灰、
    // 右键菜单里四个状态项置灰，都据它决定。
    virtual int GetConnectionUiState() = 0;

    // 本进程是否已经登录成功过。托盘右键菜单据它在「未登录」与「已登录」两种形态间
    // 选一种；「禁止机器休眠」据它决定停在登录界面时不占用系统电源请求。
    virtual bool IsLoggedIn() = 0;

    // 会话是否处于正常登录态 —— 已登录成功，且不处于登录中 / 重连中 / 用户主动离线。
    // 这是「现在能不能向服务端发业务请求」的判据，比 IsLoggedIn 严格。自动离开据它
    // 决定要不要真的下发一次状态变更。
    virtual bool IsSessionEstablished() = 0;

    // 被踢下线的模态提示框是否正开着。开着时托盘的点击一律忽略，避免在模态框之上
    // 又弹出主面板。
    virtual bool IsKickoutNoticeShowing() = 0;

    // 唤起界面：已登录就把主面板调到前台，未登录则把登录窗调到前台。
    virtual void ShowLoginOrMainPanel() = 0;

    // 请求切换本人的在线状态。
    //   presence  目标状态（kMyPresenceOnline 等）。
    //   source    变更来源，取值见 Session/UserSessionData.h 的 PresenceChangeSource：
    //             托盘菜单属于用户手动操作，传 kPresenceChangeManual；自动离开传
    //             kPresenceChangeAuto。实现方据它维护「这个离开是不是自动切过去的」。
    virtual void ChangePresence(int presence, int source) = 0;

    // 用户从托盘右键菜单选了「退出登录」。
    virtual void RequestLogout() = 0;

    // 用户从托盘右键菜单选了「退出程序」。
    virtual void RequestExit() = 0;
};

} // namespace balloonmain
