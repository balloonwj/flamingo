/**
 *  会话控制器：从弹出登录窗到退出登录之间，一个登录会话的全部编排集中在这一个类里 ——
 *  发起登录、处理登录结果、掉线与自动重连、被踢下线、切换在线状态、连接态广播、登出并
 *  退回登录界面，以及本机历史登录账号列表的读写。
 *
 *  登录流程与在线状态之所以放在同一个类里，是因为它们共用同一批状态：会话状态机
 *  m_nSessionStatus、是否已登录 m_bAlreadyLogin、是否正处于「从离线恢复在线」
 *  m_bResumingOnline、以及重连定时器 m_dwReconnectTimerId。这四项被两边交叉读写，
 *  拆成两个类就得在它们之间架一层互相访问的接口，得不偿失。
 *
 *  本类实现 ISessionStateHost：托盘控制器与自动离开控制器要问的「当前在线状态 / 连接态 /
 *  登录态」以及要请求的「切换状态 / 登出 / 退出」，答案都在本类手里。
 *
 *  典型用法：由 CBalloonMainPanel 按值持有，创建窗口后调一次 Attach，并在自己的消息映射
 *  里加一行 CHAIN_MSG_MAP_MEMBER(m_sessionController)，十一条消息即由本类接管。
 *
 *      // CBalloonMainPanel 的消息映射
 *      CHAIN_MSG_MAP_MEMBER(m_sessionController)
 *
 *      // 窗口建好之后
 *      m_sessionController.Attach(m_hWnd, this, m_lpFMGClient, pAppHost);
 *      m_sessionController.StartInitialLogin();
 *
 *      // 宿主的 WM_TIMER 处理里
 *      if (m_sessionController.PumpTimer(nIDEvent))
 *          return;
 *
 *  本类不是窗口，只是一个带消息映射的普通对象，靠 CHAIN_MSG_MAP_MEMBER 搭在宿主窗口的
 *  消息派发链上。理由与其它几个协作类相同，见 MainPanelRdCoordinator.h。
 *
 *  balloonwj@qq.com   2026-08-19
 */

#pragma once

#include "MainPanelSessionHost.h"               // balloonmain::ISessionStateHost（本类实现它）
#include "../BalloonAuth/BalloonLoginDlg.h"     // CLoginDlg（本类按值持有登录窗）
#include "../../Session/LoginAccountList.h"     // CLoginAccountList / LOGIN_ACCOUNT_INFO
#include "../../Session/UserSessionData.h"      // FMG_MSG_* 消息号

class CBalloonMainPanel;
class CFlamingoClient;

namespace balloonmain {

//会话状态机。它描述的是「这个登录会话走到哪一步了」，与用户选择的在线状态
//（presence）是两回事：用户选「离开」时会话仍然是 LOGIN 态。
enum SessionStatus
{
    kSessionNotLogin,       //尚未登录，停在登录界面
    kSessionLogining,       //已发出登录请求，正在等应答
    kSessionLogin,          //登录成功，会话正常
    kSessionReconnecting,   //连接断开，正在自动重连
    kSessionUserGoOffline   //用户主动选择了「离线」，此时不自动重连
};

//自动重连定时器的标识。挂在宿主窗口（主面板）上。
const UINT_PTR kReconnectTimerId = 5;

//「显示 / 隐藏主面板」全局热键的标识，登录成功后注册在主面板窗口上。
const int kShowPanelHotKeyId = 1001;

//该热键的主键，与注册时给的 MOD_CONTROL | MOD_ALT 一起构成 Ctrl+Alt+D。
//此前它是全局变量 g_cHotKey，定义在 v1 的字体选择框里，取值一直是 'D'、也没有任何
//界面能改它；那个文件已随 v1 清理删除，故就地固化为常量。
const TCHAR kShowPanelHotKey = _T('D');

/**
 *  会话控制器需要向进程层请求的事项。
 *
 *  目前只有一项：退出整个进程。登录被服务端以「版本过低」拒绝后用户选择升级、升级包
 *  就绪要求重启、用户从托盘选「退出程序」，这几条路最终都要走进程的完整退出序列
 *  （销毁窗口、投 WM_QUIT），而那个序列不归本类。
 */
struct ISessionAppHost
{
    virtual ~ISessionAppHost() {}

    // 请求退出整个进程。实现方应执行完整的退出序列，本方法返回后进程即将结束。
    virtual void RequestAppExit() = 0;
};

/**
 *  会话控制器。
 *
 *  生命周期与宿主窗口一致：Attach 绑定协作对象，StartInitialLogin 发起进程启动后的
 *  第一次登录，此后一直在世 —— 登出并不销毁本类，它要负责把登录窗重新弹出来。
 */
class CSessionController : public ISessionStateHost
{
public:
    /**
     *  构造。
     *    pFMGClient  网络客户端，本类不拥有。登录窗是本类的值成员，构造时就需要它。
     */
    explicit CSessionController(CFlamingoClient* pFMGClient);
    ~CSessionController();

    /**
     *  绑定宿主与协作对象。须在宿主窗口创建之后、发起登录之前调用一次。
     *
     *    hHost     宿主窗口句柄（主面板），本类不拥有。四处用途：重连定时器的宿主；
     *              提示框的属主；登出请求（FMG_MSG_LOGOUT）投递的目标；升级检查结果的
     *              回投目标。
     *    pPanel    主面板，本类不拥有。登录成功后要显示它、登出时要重置它。不可为 NULL。
     *    pClient   网络客户端，本类不拥有。登录、断连、读写用户配置都经它。不可为 NULL。
     *    pAppHost  进程层回调接口，本类不拥有。不可为 NULL。
     */
    void Attach(HWND hHost, CBalloonMainPanel* pPanel, CFlamingoClient* pClient,
                ISessionAppHost* pAppHost);

    /**
     *  发起进程启动后的第一次登录：勾了自动登录就直接登，否则弹出登录窗。
     *  返回 false 表示用户直接关掉了登录窗、没有走正常登录流程，调用方应停止后续初始化。
     */
    bool StartInitialLogin();

    /**
     *  把定时器消息交给本类处理。
     *    nIDEvent  WM_TIMER 带来的定时器标识。
     *  返回 true 表示这个定时器归本类，宿主不必再往下判断。
     */
    bool PumpTimer(UINT_PTR nIDEvent);

    /**
     *  只供命令行调试入口 --mainpanel 使用：把「已登录」标志置真。
     *
     *  该入口模拟的是「已经登录、主面板已打开」这个局面，而它并没有真的登录。托盘
     *  右键菜单的形态据这个标志在「未登录」与「已登录」两种之间选一种，不置真的话
     *  弹出来的是未登录那套两行菜单，四个在线状态项根本不出现，也就无从核对。
     *  正式流程中不得调用本方法 —— 真正的登录成功由 OnLoginResult 置位。
     */
    void MarkLoggedInForDebugPreview();

    // ---- balloonmain::ISessionStateHost 的实现 ----

    // 取「我当前的在线状态」。主面板在时以状态胶囊为准 —— 胶囊是当前状态在界面上的
    // 唯一呈现，托盘跟它取同一个值，两处才不会一个说在线、一个说离线。
    virtual int GetCurrentPresence();

    // 取当前界面连接态（在线 / 正在连接 / 离线），由在线状态与真实连接状态共同推导。
    virtual int GetConnectionUiState();

    // 本进程是否已经登录成功过。
    virtual bool IsLoggedIn();

    // 会话是否处于正常登录态（已登录成功，且非登录中 / 重连中 / 用户主动离线）。
    virtual bool IsSessionEstablished();

    // 被踢下线的模态提示框是否正开着。
    virtual bool IsKickoutNoticeShowing();

    // 把「当前应可见的主界面」唤到前台：登录前是登录窗，登录后是主面板。
    virtual void ShowLoginOrMainPanel();

    // 请求切换本人的在线状态。据连接态分别走 GoOffline / GoOnline / 直接下发协议。
    virtual void ChangePresence(int presence, int source);

    // 用户选了「退出登录」。必须异步：登出要销毁子窗口并进入登录窗的模态循环，
    // 不能跑在菜单分发的调用栈上。
    virtual void RequestLogout();

    // 用户选了「退出程序」。
    virtual void RequestExit();

    BEGIN_MSG_MAP_EX(CSessionController)
        //网络故障（连接建立失败 / 连接断开）：据会话状态决定是否起自动重连
        MESSAGE_HANDLER_EX(FMG_MSG_NET_ERROR, OnNetError)
        //登录应答。网络线程用 SendMessage 同步投递并读取返回值，故不可改成异步
        MESSAGE_HANDLER_EX(FMG_MSG_LOGIN_RESULT, OnLoginResult)
        //登录成功后那次静默检查更新的结果：只有查出可用新版本才弹升级窗
        MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_RESULT, OnSilentUpdateResult)
        //升级窗请求直接退出程序，好让更新器覆盖文件
        MESSAGE_HANDLER_EX(FMG_MSG_EXIT_FOR_UPDATE, OnExitForUpdate)
        //v2 更新资料应答（个性签名）：转发到主面板，并把昵称同步进历史账号列表
        MESSAGE_HANDLER_EX(FMG_MSG_V2_UPDATE_PROFILE_RESULT, OnV2UpdateProfileResult)
        //主面板状态菜单 / 托盘菜单请求切换在线状态
        MESSAGE_HANDLER_EX(FMG_MSG_CHANGE_MY_PRESENCE, OnChangeMyPresence)
        //登出请求：二次确认后断开连接、关掉所有子窗口并重新弹出登录窗
        MESSAGE_HANDLER_EX(FMG_MSG_LOGOUT, OnLogout)
        //v2 修改在线状态应答：失败时把状态胶囊回退
        MESSAGE_HANDLER_EX(FMG_MSG_V2_CHANGE_PRESENCE_RESULT, OnV2ChangePresenceResult)
        //v2 联系人在线状态变更推送：刷新联系人树上的状态点
        MESSAGE_HANDLER_EX(FMG_MSG_V2_PRESENCE_CHANGED, OnV2PresenceChanged)
        //v2 被踢下线推送：进入离线态、停自动重连并弹提示
        MESSAGE_HANDLER_EX(FMG_MSG_V2_KICKOUT, OnV2Kickout)
        //本机网络由断开恢复：过自动重连守卫，非离线态才连回服务器
        MESSAGE_HANDLER_EX(FMG_MSG_NETWORK_RESUMED, OnNetworkResumed)
    END_MSG_MAP()

private:
    // ---- 十一条消息的处理器 ----
    // wParam / lParam 的含义写在各自实现的头部注释里；凡 lParam 为堆上结构体的，
    // 处理完由处理器自己 delete。
    LRESULT OnNetError(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnLoginResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnSilentUpdateResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnExitForUpdate(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2UpdateProfileResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnChangeMyPresence(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnLogout(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2ChangePresenceResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2PresenceChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnV2Kickout(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnNetworkResumed(UINT uMsg, WPARAM wParam, LPARAM lParam);

    /**
     *  发起一次登录。
     *    bAutoLogin  TRUE 表示按本机存下的账号直接登录，不弹登录窗。
     *  返回 false 表示用户直接关掉了登录窗、没有走正常登录流程。
     */
    bool StartLogin(BOOL bAutoLogin = FALSE);

    /**
     *  断开连接、关掉所有窗口、清空登录态并重新弹出登录窗 —— 用户主动登出与账号在别处
     *  登录被顶下线共用的收尾序列。调用方须跑在由 PostMessage 投递而来的干净调用栈上：
     *  本函数要重置主面板、并进入登录窗的模态循环。
     *    exitKind       本次退出登录的方式，取值见 balloonmain::LoginExitKind。
     *    kickoutReason  被踢原因，取值见 balloonmain::KickoutReasonValue；主动登出时忽略。
     */
    void ReturnToLoginWindow(int exitKind, int kickoutReason);

    /**
     *  「从离线恢复在线」失败时的收尾：把会话状态、状态胶囊、托盘图标、界面门禁全部拨回
     *  离线态，然后按需提示。与首次登录失败的区别在于不弹登录窗。
     *    lpszReason  给用户看的失败原因；传 NULL 表示不弹提示。
     */
    void ResumeOnlineFailed(LPCTSTR lpszReason);

    // 与聊天服务器的连接是否已就绪（已连上且完成登录）。
    bool IsServerConnected() const;

    // 把当前连接态广播给主面板与所有已打开的聊天窗，由它们决定输入区是否置灰、
    // 顶部提示条显示什么。状态发生任何变化时都要调用。
    void BroadcastConnectionState();

    // 用户选了「退出程序」时的实际动作：登录窗还开着就关它，否则请求进程退出。
    void DoExit();

    // 清掉某账号在本机存下的「自动登录」标志并立即落盘。
    //   lpszAccount  账号名；NULL、空串、或该账号不在历史列表里时只记日志、不做事。
    void ClearAutoLoginFlag(LPCTSTR lpszAccount);

    // 忘掉当前账号在本机存下的密码，并把「记住密码」与「自动登录」一并取消后落盘。
    // 用在服务端明确回了「密码错误」之后。
    void ForgetSavedPasswordForCurrentAccount();

    // 把历史登录账号列表写回 Bin\Users\LoginAccountList.dat。写失败只记日志。
    void SaveLoginAccountListFile();

    // 把当前登录账号的昵称同步进历史登录账号列表 —— 与列表里存的姓名不一致时才写入
    // 并落盘（用户刚改过昵称的情形）。
    void SyncNickNameToLoginAccountList();

    // 登录成功后创建该账号需要的本地目录（缓存、头像等）。
    void CreateEssentialDirectories();

    HWND m_hHost;                       // 宿主窗口句柄（主面板），借用不拥有
    CBalloonMainPanel* m_pPanel;        // 主面板，借用不拥有
    CFlamingoClient* m_lpFMGClient;     // 网络客户端，借用不拥有
    ISessionAppHost* m_pAppHost;        // 进程层回调接口，借用不拥有

    CLoginDlg m_LoginDlg;               // 登录窗，本类按值持有；登出后重新弹出的也是它
    CLoginAccountList m_LoginAccountList;   // 本机历史登录账号列表，来自 Users\LoginAccountList.dat
    LOGIN_ACCOUNT_INFO m_stAccountInfo;  // 当前即将登录 / 已登录的账号信息

    int m_nSessionStatus;               // 会话状态机，取值见 SessionStatus
    BOOL m_bAlreadyLogin;               // 本进程是否已经登录成功过；登出后不复位（用于区分「从未登录」与「登录过又退出」）

    //本次登录是否由「从离线恢复在线」发起（用户在离线态下选了在线 / 离开 / 忙碌）。
    //登录失败时据它决定：恢复在线失败就退回离线态并提示，而不是像首次登录失败那样
    //弹出登录窗把用户踢回登录界面。登录有结果（成功或失败）后复位。
    bool m_bResumingOnline;

    DWORD m_dwReconnectTimerId;         // 自动重连定时器标识；0 表示未启动
    bool m_bEnableReconnect;            // 是否启用断线重连，来自 flamingo.ini
    UINT m_uReconnectInterval;          // 重连时间间隔，必须大于 0，单位毫秒
    bool m_bAlreadySendReloginRequest;  // 重连过程中是否已经发送过登录请求包

    //被踢下线提示框是否正在显示。它是模态框、跑嵌套消息循环，期间托盘点击与重复的
    //被踢下线通知都要挡掉，否则会重入「回到登录界面」的收尾序列。
    bool m_bKickoutNoticeShowing;
};

} // namespace balloonmain
