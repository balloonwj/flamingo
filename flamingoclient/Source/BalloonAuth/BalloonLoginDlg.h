/**
 *  BalloonLoginDlg.h
 *
 *  登录对话框 —— 基于 balloonui 的 CLoginDlg 替代实现，取代旧的
 *  Source/LoginDlg.h（依赖 SkinLib 皮肤库的 CDialogImpl 版本）。
 *
 *  设计稿：design/FlamingoIM-standalone.html 内 auth.jsx 的 LoginScreen。
 *  P0 范围：骨架 —— 公共接口（成员 + 方法）与旧 CLoginDlg 一一对应；
 *  DoModal 以手写模态消息循环实现；窗口以一个 DuiFrameWindow 弹出、
 *  可显示 / 最小化 / 关闭。真正的登录界面控件树（Banner / 头像 / 账号
 *  密码 / 登录按钮）由 P1a 的 BalloonLoginLayout 构建。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / struct DuiNotify
#include "../LoginAccountList.h"      // LOGIN_ACCOUNT_INFO / CLoginAccountList
#include "../UserSessionData.h"       // FMG_MSG_UPDATE_RESULT / FMG_MSG_UPDATE_LOGIN_CONTINUE

class CFlamingoClient;

// balloonui 控件 —— 仅作成员指针类型用，前置声明即可，真定义在 .cpp 引入。
namespace balloonwjui { class DuiComboBox; class DuiEditHost; class DuiButton; }
// 自绘 Banner 控件 —— 同上，前置声明即可。
namespace balloonauth { class AuthBanner; }

// 延迟执行"删除一个历史账号"。wParam = 该账号在下拉框里的项索引，无堆负载。
//
// 为什么必须延迟：这个动作由账号下拉浮层里的删除叉触发，通知是在<u>浮层窗口的
// 消息处理栈里</u>同步发出来的。要是就地弹二次确认框，模态框会泵消息，浮层随即
// 因失去焦点而自我销毁（OnFinalMessage 里 delete this），等模态框返回、栈退回
// DuiComboBoxPopup::OnDuiNotify 时那个对象已经没了 —— 实测表现为 atlwin.h 的
// "Object deleted before window was destroyed" 断言。PostMessage 一拍之后再弹，
// 浮层早已正常关闭，就没有这个问题了。
//
// 用 WM_APP 段而不是 WM_USER 段：客户端的 FMG_MSG_* 全在 WM_USER 段，且受
// CFlamingoClient::ProxyWndProc 的 FMG_MSG_FIRST / FMG_MSG_LAST 闸门约束；
// 本消息是登录窗投给自己的，不经代理窗，另起一段最省心、也不会与那批撞号。
#define WM_APP_REMOVE_ACCOUNT   (WM_APP + 1)

/**
 *  CBalloonLoginDlg
 *
 *  旧 CLoginDlg 的等价替代。继承 balloonwjui::DuiFrameWindow —— 由它
 *  提供调用方（CMainDlg）依赖的 WTL CWindowImpl 接口：Create /
 *  ShowWindow / IsWindow / SetFocus / PostMessage / m_hWnd 等。
 *
 *  公共接口与旧 CLoginDlg 保持一致，故 CMainDlg 侧调用点除 #include
 *  行外无需改动（见文件末尾的 typedef 兼容别名）。
 */
class CBalloonLoginDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象指针，本类只引用、不持有；可为 NULL，
     *                由调用方在 DoLogin / DoModal 之前补设。
     */
    explicit CBalloonLoginDlg(CFlamingoClient* pFMGClient);
    ~CBalloonLoginDlg();

    /**
     *  读取当前登录账号信息（账号 / 密码 / 状态 / 记住密码 / 自动登录）。
     *    lpAccount：[出参] 接收账号信息的缓冲区，不可为 NULL。
     *  返回：成功 TRUE；lpAccount 为 NULL 时 FALSE。
     */
    BOOL GetLoginAccountInfo(LOGIN_ACCOUNT_INFO* lpAccount);

    /**
     *  设置登录账号信息（自动登录路径用：调用方先填好再调 DoLogin）。
     *    lpAccount：账号信息源，不可为 NULL。
     *  返回：成功 TRUE；lpAccount 为 NULL 时 FALSE。
     */
    BOOL SetLoginAccountInfo(const LOGIN_ACCOUNT_INFO* lpAccount);

    /**
     *  设置默认账号 —— 窗口打开时预填到账号输入框。
     *    pszDefaultAccount：账号字符串，NULL 视为空。
     */
    void SetDefaultAccount(PCTSTR pszDefaultAccount);

    /**
     *  设置默认密码 —— 与 SetDefaultAccount 配套预填。
     *    pszDefaultPassword：密码字符串，NULL 视为空。
     */
    void SetDefaultPassword(PCTSTR pszDefaultPassword);

    /**
     *  发起登录 —— 起一个后台线程，用 m_stAccountInfo 里的账号 / 密码
     *  向服务器请求登录，结果通过 FMG_MSG_LOGIN_RESULT 消息回投给主窗口
     *  代理窗口。可在登录窗口尚未创建时直接调用（自动登录路径）。
     */
    void DoLogin();

    /**
     *  模态显示登录窗口。
     *    hWndParent：  父窗口句柄，模态期间被禁用；可为 NULL。
     *    dwInitParam： 兼容旧 CDialogImpl::DoModal 的第二参，本实现未使用。
     *  返回：IDOK     —— 用户已发起登录流程；
     *        IDCANCEL —— 用户关闭窗口、放弃登录。
     */
    INT_PTR DoModal(HWND hWndParent, LPARAM dwInitParam = 0);

    // WM_DUI_NOTIFY 路由客户区控件通知（登录按钮 / 链接 / 复选框）；
    // WM_SYSCOMMAND / WM_CLOSE 自行处理（最小化转隐藏、关闭结束模态）；
    // 其余消息（含标题栏 min/close 按钮通知）经 CHAIN 交回 DuiFrameWindow。
    BEGIN_MSG_MAP(CBalloonLoginDlg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_SYSCOMMAND, OnSysCommand)
        MESSAGE_HANDLER(WM_CLOSE,      OnCloseMsg)
        MESSAGE_HANDLER(WM_APP_REMOVE_ACCOUNT, OnRemoveAccount)
        //登录前那次检查更新的结果（升级窗 / UpdateManager 投来）
        MESSAGE_HANDLER(FMG_MSG_UPDATE_RESULT, OnPreLoginUpdateResult)
        //用户关掉了升级窗，被拦下的登录可以继续了
        MESSAGE_HANDLER(FMG_MSG_UPDATE_LOGIN_CONTINUE, OnUpdateLoginContinue)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

public:
    CFlamingoClient*   m_lpFMGClient;        // 所属客户端，引用不持有
    CLoginAccountList* m_pLoginAccountList;  // 历史登录账号列表，引用不持有

private:
    /**
     *  WM_SYSCOMMAND 处理：SC_MINIMIZE 转为隐藏窗口（与旧 CLoginDlg 行为
     *  一致 —— 登录窗最小化后驻留托盘，由托盘菜单重新唤出）；其余
     *  syscommand（SC_CLOSE 等）置 bHandled=FALSE 交回基类 / DefWindowProc。
     */
    LRESULT OnSysCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CLOSE 处理：以 IDCANCEL 结束模态循环。窗口本身在 DoModal 的
     *  循环退出后统一 DestroyWindow，此处不直接销毁。
     */
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_APP_REMOVE_ACCOUNT 处理：真正执行"删除一个历史账号"（弹二次确认、
     *  删列表、落盘、刷界面）。由 OnDuiNotify 收到删除叉通知后 PostMessage
     *  过来，延后一拍执行的理由见该消息号的定义处。
     *    wParam：该账号在账号下拉框里的项索引。
     */
    LRESULT OnRemoveAccount(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  登录前那次检查更新的结果。lParam 是堆上的 update::UpdateTaskResult*，本函数
     *  处理完后 delete。
     *
     *  查出可用新版本就弹升级窗并<u>不再继续登录</u>；已是最新 / 检查失败一律照常登录
     *  —— 检查更新这条链路自己挂了不该把用户挡在门外。
     */
    LRESULT OnPreLoginUpdateResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //用户关掉升级窗（点「稍后」）后继续被拦下的那次登录。强制升级时升级窗关不掉，
    //这条消息不会到来，登录也就一直进行不下去。
    LRESULT OnUpdateLoginContinue(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  发起一次"登录前检查更新"。
     *  @return true 表示已发起、正在等结果，调用方应<u>暂不登录</u>；
     *          false 表示发不出去（连不上服务器等），调用方照常登录即可。
     */
    bool StartPreLoginUpdateCheck();

    //把登录按钮切到 / 切回"正在检查更新"外观。
    //  checking：true 显示检查中并禁用按钮，false 恢复原样。
    void SetLoginButtonChecking(bool checking);

    /**
     *  WM_DUI_NOTIFY 处理：分发客户区控件的点击 / 值变化通知 —— 登录
     *  按钮、注册 / 网络设置 / 忘记密码链接、记住密码 / 自动
     *  登录复选框、账号下拉框选择。未命中的通知置 bHandled=FALSE 交回
     *  DuiFrameWindow（标题栏按钮等）。
     */
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // “登录”按钮点击：校验输入 → 采集账号信息 → 发起登录 → 结束模态。
    void OnLoginClicked();

    // 点击头像下方"在线状态胶囊"后弹出在线状态选择菜单（在线 / 离开 / 忙碌）。锚定在胶囊
    // 正下方；选中后把所选 v2 状态存入 m_loginPresence、并刷新 Banner 胶囊的颜色与文字。
    void ShowLoginStatusMenu();

    // “注册账号”链接点击：模态打开注册对话框；注册成功后回填账号 / 密码。
    void OnRegisterClicked();

    // “网络设置”链接点击：模态打开网络设置对话框。
    void OnNetworkClicked();

    // 检测新版本，用户确认后走自动升级流程（仅 Release 构建会调用）。

    // 用 m_pLoginAccountList 的历史账号填充账号下拉框，并按默认账号 /
    // 最后登录账号预填账号与密码。须在 InitLoginUi 建好控件树后调用。
    void PopulateAccountCombo();

    /**
     *  按账号名加载其历史登录信息到界面（密码 / 记住密码 / 自动登录）。
     *    lpszAccount：账号名；在 m_pLoginAccountList 里查不到时不改动界面。
     */
    void LoadAccountIntoUi(LPCTSTR lpszAccount);

    /**
     *  从历史账号列表里删掉一个账号（账号下拉里点删除叉触发）。
     *
     *  先弹二次确认 —— 删掉就找不回来了，而删除叉紧挨着账号文字，误点的代价
     *  不该是"账号和聊天记录都没了"。确认框里另给一个「同时删除本地聊天记录」
     *  的勾选项，默认<u>不勾</u>：多数人只是想让下拉列表干净点，不是要清历史。
     *
     *  确认后依次：从 CLoginAccountList 删除并落盘、从下拉框摘掉该项、必要时
     *  把界面切到另一个账号；勾了那个选项的话再删掉 Users\<账号>\ 整个目录。
     *
     *    comboIndex：下拉框里的项索引（DUICBN_ITEMDELETE 带回来的 extra）。
     *                越界、或该项对应的账号已不在历史列表里时什么都不做。
     */
    void RemoveAccountFromHistory(int comboIndex);

    /**
     *  把当前账号的两个勾选项（记住密码 / 自动登录）就地写回历史账号列表并落盘。
     *
     *  存在理由：勾选项此前要等到<u>登录成功之后</u>才由 CMainDlg 落盘，于是用户
     *  取消勾选后如果直接关掉登录窗、并不登录，改动就丢了，下次启动照旧自动登录。
     *  改成点一下复选框就落一次盘，"取消自动登录"才真的取消得掉。
     *
     *  取消"记住密码"时会一并清空该账号存下的密码 —— 否则文件里仍留着明文，
     *  这个取消就名不副实。
     *  账号为空、或该账号尚未在本机成功登录过（不在列表里）时什么都不做：此刻
     *  替它建条目会凭空多出一条从没登录成功过的账号，入列应当等登录成功后由
     *  CMainDlg 用真实采集到的信息来做。
     */
    void PersistCheckStateForCurrentAccount();

    /**
     *  构建并装载登录窗客户区控件树（来自 BalloonLoginLayout），缓存
     *  关键控件指针，对内嵌 HWND 的控件调 EnsureCreated，并填充历史
     *  账号下拉框。须在窗口 Create 之后调用。
     */
    void InitLoginUi();

    /**
     *  配置无边框窗体外观（须在 Create 之后调用）：标题栏透明、白色
     *  min/close glyph、隐藏标题文字，并装一张"上橙下白"的 9-grid 背景图
     *  —— 透出顶部一条橙色与下方 AuthBanner 渐变衔接，其余白色作为表单
     *  区底色。这样视觉上 Banner 直抵窗口顶端、与设计稿一致。
     */
    void ConfigureFrame();

    /**
     *  把账号同步到 Banner（决定头像首字母、头像下方显示的名字、头像底色）。
     *  账号变化时调用。
     *    account：当前账号文本。由调用方经 ResolveAccountFromCombo 取得
     *             —— 本函数<u>不</u>自己去读下拉框，原因见该函数的注释。
     */
    void UpdateBannerUserName(const CString& account);

    /**
     *  取"账号下拉框此刻代表的账号"。
     *
     *  之所以不一律用 DuiComboBox::GetText()：从下拉列表选中一项时，
     *  DuiComboBox 先同步发出值变化通知、之后才把选中项文本写进内嵌 EDIT，
     *  处理通知的当口 GetText() 读到的还是切换前的旧账号（详见
     *  balloonauth::ShouldReadAccountFromComboItem 的注释）。
     *    nNotifyExtra：值变化通知里的 extra（用户选中的项索引）；并非由通知
     *                  触发的调用点传 balloonauth::kComboExtraNoItem。
     *  返回：账号文本；下拉框尚未建好时返回空串。
     */
    CString ResolveAccountFromCombo(int nNotifyExtra) const;

    /**
     *  结束 DoModal 的模态消息循环。
     *    result：写入 DoModal 的返回值（IDOK / IDCANCEL）。
     */
    void EndModal(INT_PTR result);

    /**
     *  登录工作线程入口。pParam 为 CBalloonLoginDlg*。
     *  返回值仅用于满足 _beginthreadex 的签名，调用方不关心。
     */
    static UINT WINAPI LoginThreadProc(void* pParam);

private:
    // ---- 缓存的客户区控件指针 ----
    // 由 InitLoginUi 填充；指向客户区控件树里的节点，所有权属于控件树，
    // 本类不负责释放。未 InitLoginUi 时为 NULL。
    balloonwjui::DuiComboBox* m_pAccountCombo;   // 账号下拉框
    balloonwjui::DuiEditHost* m_pPasswordEdit;   // 密码输入框
    balloonwjui::DuiButton*   m_pLoginButton;    // “登录”按钮（检查更新期间要改文案并禁用）
    balloonwjui::DuiButton*   m_pRememberPwd;    // “记住密码”复选框
    balloonwjui::DuiButton*   m_pAutoLogin;      // “自动登录”复选框
    balloonauth::AuthBanner*  m_pBanner;         // 顶部品牌 / 头像 Banner（含可点击的在线状态胶囊）

    // 本次登录选择的在线状态（v2 presence_type，见 net/Protocol.h；与 UserSessionData.h 的
    // kMyPresence* 同值）。OnLoginClicked 据状态下拉填好，LoginThreadProc 取它作登录 presence。
    int                m_loginPresence;

    LOGIN_ACCOUNT_INFO m_stAccountInfo;       // 当前待登录的账号信息
    CString            m_strDefaultAccount;   // 预填账号，空表示不预填
    CString            m_strDefaultPassword;  // 预填密码，与预填账号配套
    //本次进程内是否已经做过"登录前检查更新"。只查一次：用户点「稍后」之后再点登录
    //不该又被拦一遍，否则永远登不进去。
    bool               m_preLoginUpdateChecked;

    bool               m_modalRunning;        // 模态消息循环是否在运行
    INT_PTR            m_modalResult;         // DoModal 的返回值缓存
    HBITMAP            m_hBgBitmap;           // 窗体 9-grid 背景图，本类持有并释放
};

// 兼容别名：CMainDlg 等调用点仍用旧名 CLoginDlg，本 typedef 让它们
// 在不改类型名的前提下指向新的 balloonui 实现。
typedef CBalloonLoginDlg CLoginDlg;
