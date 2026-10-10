/**
 *  BalloonRegisterWindow.h
 *
 *  新版注册对话框 —— 基于 balloonui，对照 design/register01-03.png。
 *  窗口固定 380×610，顶部橙色 banner，下方两步注册流程：
 *    步骤 1 填写资料（昵称 / 密码 / 确认密码 / 手机号 / 公司名称）
 *    步骤 2 完成（展示服务端<u>自动生成</u>的账号与所加入的公司，提醒记住账号）
 *
 *  登录账号不由用户起名，一律由服务端按用户ID自动生成，注册成功后随应答
 *  回传，在完成页上突出显示。用户点"立即登录"关窗后，RunModalFromLogin 返回 IDOK，
 *  登录窗据此把账号回填进账号框（见 RegisteredAccount），再调 ReturnToLogin 翻回登录窗。
 *  2026-10-07 起登录窗与注册窗之间以「翻转」过渡切换（balloonauth::SwitchDuiWindows）。
 *  "提交注册"先跑客户端格式校验，通过后在工作线程里发 cmd_register(1101)
 *  并等应答；成功则推进到完成页，失败按服务端错误码弹中文提示。
 *  "立即登录"关闭窗口。
 *
 *  注册的语义是"加入一家已存在的企业"：用户填的公司全称须能在服务端精确
 *  匹配到唯一一家企业，注册成功后账号被自动挂进该企业的公司根部门。原先
 *  介于两步之间的"手机验证"已随短信验证码一起去掉（短信验证码现在只服务
 *  于找回密码）。
 *
 *  本窗口是注册功能唯一的实现：旧版 MFC 注册框 CRegisterDialog 已于
 *  2026-08-14 随 v1 清理删除。它是独立的 balloonui 窗口，调试期由命令行
 *  --register 入口直接弹出；接入登录流程另议。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "RegisterModel.h"

class CFlamingoClient;

// 请求重建客户区为某一步的自投消息。
// wParam = 目标步骤（见 balloonregister::RegisterStep）；lParam 未用。
//
// 为什么要绕这一道：按钮点击的 DUIN_CLICK 是 DuiHost::SendNotify 用
// ::SendMessage <u>同步</u>发上来的，处理函数是在按钮自己的 OnLButtonUp
// 调用栈里跑的。若就地 RebuildStepUi，等于在按钮的方法执行途中把这个按钮
// 连同整棵客户区树一起析构掉，是典型的"自杀式重建"。改为 PostMessage 把
// 重建推迟到本次点击的调用栈完全退出之后再做。
#define WM_REG_REBUILD_STEP  (WM_APP + 0x31)

// 注册工作线程把服务端应答回递给窗口的自投消息。
// wParam 未用；lParam = new 出来的 CBalloonRegisterWindow::RegisterResult*，
// 由窗口在处理完后 delete。
//
// 为什么要绕这一道：注册请求走的是 CIUSocket::Register 这类阻塞式同步调用
// （与登录一致），在 UI 线程里直接调会把界面卡住；故放到工作线程里发，收到
// 应答后再投消息回 UI 线程更新界面。
#define WM_REG_REGISTER_RESULT  (WM_APP + 0x32)

/**
 *  CBalloonRegisterWindow
 *
 *  注册对话框窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无边框
 *  自绘窗体与标题栏。固定尺寸、不可缩放，标题栏透明（橙色 banner 透出）、
 *  只保留最小化 / 关闭按钮、白色 glyph。
 *
 *  三步流程靠"重建客户区"实现：窗口尺寸不变，每次切换步骤调
 *  BuildRegisterBody(step) 生成该步的控件树并 SetClientContent 换上。
 */
class CBalloonRegisterWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  一次注册请求的结果，由工作线程填好后经 WM_REG_REGISTER_RESULT 回递
     *  给 UI 线程。设计成窗口的嵌套类是为了让线程函数与消息处理共用同一份
     *  定义，不额外暴露到全局命名空间。
     */
    struct RegisterResult
    {
        RegisterResult()
            : m_transportOk(false)
            , m_code(-1)          // -1：还没拿到任何服务端结果码
            , m_userid(0)
        {
        }

        bool    m_transportOk;      // 是否成功收到了一个完整应答帧；false 表示网络层就失败了
        int     m_code;             // 服务端结果码，0 成功，非 0 见 enum error_code
        int64_t m_userid;           // 新账号 userid，仅 m_code 为 0 时有效
        CString m_username;         // 服务端最终采用的登录账号，仅 m_code 为 0 时有效
        CString m_enterpriseName;   // 所加入企业的全称（库中原文），仅 m_code 为 0 时有效
    };

    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有。
     */
    explicit CBalloonRegisterWindow(CFlamingoClient* pFMGClient);
    ~CBalloonRegisterWindow();

    /**
     *  创建并显示注册窗口。
     *    hMainDlg：主面板的窗口句柄；关闭本窗口时
     *              据此请求退出程序（调试入口语义）。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  以模态方式打开注册窗口，从登录窗「翻转」过来（供登录窗调用，2026-10-07 起取代原来的 DoModal）。
     *  本窗口建在登录窗的位置上（上边缘对齐、水平居中，超出工作区时挪回来），登录窗被禁用并隐藏，
     *  两者之间播放翻转过渡（系统关闭了动画时直接切换，见 balloonauth::SwitchDuiWindows）；
     *  随后跑独立消息循环，直到用户点「返回」「立即登录」或关闭窗口。
     *
     *  返回时<u>本窗口仍显示着、登录窗仍隐藏且禁用</u>：调用方先按结果回填登录框（登录窗隐藏时也能改），
     *  再调用 ReturnToLogin 翻回登录窗，翻转画面里就是填好的登录窗。若调用方没有调用 ReturnToLogin，
     *  本对象析构时会直接恢复登录窗（不播放翻转），不会留下两个窗口都看不见的状态。
     *    login：登录窗（借用，须在 ReturnToLogin 或本对象析构之前一直有效），应当可见、可用。
     *  返回值：IDOK 表示注册成功且用户已确认（此时 RegisteredAccount() 与
     *          RegisteredPassword() 分别给出服务端分配的账号与用户设置的
     *          密码，调用方应把两者一并回填到登录框）；IDCANCEL 表示用户
     *          中途关闭、注册未成功、建窗失败，或模态期间收到了退出消息（WM_QUIT 已转投给外层循环）。
     */
    INT_PTR RunModalFromLogin(balloonwjui::DuiFrameWindow& login);

    /**
     *  从注册窗翻回登录窗：恢复登录窗可用，播放反向翻转（注册窗 → 登录窗；条件不满足时直接切换），
     *  登录窗显示并激活，随后销毁本窗口。只在 RunModalFromLogin 之后调用；重复调用安全（第二次什么都不做）。
     */
    void ReturnToLogin();

    /**
     *  取本次注册成功后服务端分配的登录账号。
     *  仅当 RunModalFromLogin 返回 IDOK 时有意义；其余情况为空串。
     *  返回：账号字符串的常引用，生命周期同本对象。
     */
    const CString& RegisteredAccount() const { return m_resultAccount; }

    /**
     *  取本次注册时用户设置的明文密码，供登录窗回填密码框用。
     *
     *  账号是服务端生成的、密码是用户自己设的，两样都得回填登录框，用户才能
     *  直接点"登录"；只回填账号的话，密码框里会留着登录窗打开时预填的上一个
     *  账号的密码，看上去像是"注册时设的密码没生效"。
     *
     *  仅当 RunModalFromLogin 返回 IDOK 时有意义；其余情况为空串。密码只在进程内传递，
     *  调用方不得把它写进任何落盘文件（是否记住密码由用户自己勾选决定）。
     *  返回：明文密码字符串的常引用，生命周期同本对象。
     */
    const CString& RegisteredPassword() const { return m_resultPassword; }

    BEGIN_MSG_MAP(CBalloonRegisterWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_REG_REBUILD_STEP, OnRebuildStepMsg)
        MESSAGE_HANDLER(WM_REG_REGISTER_RESULT, OnRegisterResultMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 创建窗口、配置外观、构建首步客户区。hWndParent 为父窗口（可为 NULL）。
    // show 为 true 时居中显示并激活（调试入口）；为 false 时保持隐藏，由调用方摆好位置再切换过来
    // （登录窗翻转过来时）。成功返回 true。CreateAndShow 与 RunModalFromLogin 共用此方法。
    bool BuildWindow(HWND hWndParent, bool show);

    // 不播放翻转、直接恢复登录窗（可用、显示、激活）并销毁本窗口：析构时调用方没有调用 ReturnToLogin 的兜底
    void RestoreLoginDirectly();

    // 结束 RunModalFromLogin 的模态循环，result 为它的返回值。
    void EndModal(INT_PTR result);

    // 配置 frame 外观（标题栏透明 / 按钮 / 9-grid 背景图）。须在 Create 后调。
    void ConfigureFrame();

    // 按当前步骤重建客户区控件树并接好控件。step 越界回退步骤 1。
    //   step：目标步骤，取值见 balloonregister::RegisterStep。
    //
    // 注意：本方法会析构掉整棵旧客户区树。<u>不要</u>在按钮点击的通知处理里
    // 直接调它 —— 那是在按钮自己的调用栈里销毁按钮本身。步骤切换一律走
    // PostRebuildStepUi 延后执行；只有 BuildWindow（建窗时，旧客户区还是
    // 空占位、也不在任何控件的调用栈里）可以直接调。
    void RebuildStepUi(int step);

    // 投一条 WM_REG_REBUILD_STEP，把"重建为 step 这一步"推迟到当前调用栈
    // 退出之后执行。窗口无效时什么都不做。
    //   step：目标步骤，取值见 balloonregister::RegisterStep。
    void PostRebuildStepUi(int step);

    // WM_REG_REBUILD_STEP：延后执行的客户区重建，wParam 为目标步骤。
    LRESULT OnRebuildStepMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 把当前步表单各输入框内容提交回 m_model（供校验 / 步骤切换用）。
    void CommitFormToModel();

    // WM_CLOSE：调试入口下关闭即请求退出程序。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_DUI_NOTIFY：处理返回 / 提交注册 / 立即登录等按钮点击。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_REG_REGISTER_RESULT：工作线程回递的注册结果，lParam 为
    // RegisterResult*（本处理函数负责 delete）。
    LRESULT OnRegisterResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // "提交注册"：跑客户端格式校验，通过则起工作线程发 cmd_register。
    // 请求进行中重复点击会被忽略（见 m_submitting）。
    void DoSubmitRegister();

    // 注册工作线程入口。pParam 为 RegisterThreadParam*，由本函数负责 delete。
    // 线程内发请求、等应答、解析，然后把 RegisterResult* 经
    // WM_REG_REGISTER_RESULT 投回窗口。
    static unsigned int __stdcall RegisterThreadProc(void* pParam);

    // 注册成功：把服务端返回的账号与公司名填进完成页并推进步骤。
    //   result：服务端应答的解析结果，其 m_code 必为 0。
    void OnRegisterSucceeded(const RegisterResult& result);

    // 注册失败：按服务端错误码弹一句中文提示，停留在填写页。
    //   result：服务端应答的解析结果，其 m_code 非 0 或 m_transportOk 为 false。
    void OnRegisterFailed(const RegisterResult& result);

    // "返回 / 立即登录"：关闭注册窗。
    void DoCloseWindow();

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），只引用不持有
    HWND             m_hMainDlg;      // 主面板句柄
    HBITMAP          m_hBgBitmap;     // 窗体 9-grid 背景图（橙 banner + 白），本类持有
    bool             m_modalRunning;  // RunModalFromLogin 的模态循环是否进行中
    INT_PTR          m_modalResult;   // RunModalFromLogin 的返回值
    bool             m_quitReceived;  // 模态循环里收到了 WM_QUIT（已转投给外层循环）：返回登录窗时不再播放翻转
    balloonwjui::DuiFrameWindow* m_returnLogin; // 待翻回的登录窗（借用）；RunModalFromLogin 时设置，ReturnToLogin 后清空
    bool             m_submitting;    // 是否有注册请求正在途中，用于拦住重复点击"提交注册"
    bool             m_registered;    // 本次是否已注册成功；决定 RunModalFromLogin 返回 IDOK 还是 IDCANCEL
    CString          m_resultAccount; // 注册成功后服务端分配的账号，完成页回显 + 回填登录框
    CString          m_resultCompany; // 注册成功后所加入企业的全称，完成页回显用
    CString          m_resultPassword;// 注册成功时用户设置的明文密码，仅用于回填登录窗密码框，不落盘
    balloonregister::RegisterModel m_model;   // 注册流程数据模型（字段 + 校验）
};
