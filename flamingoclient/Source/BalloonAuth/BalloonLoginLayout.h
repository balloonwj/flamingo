/**
 *  BalloonLoginLayout.h
 *
 *  登录对话框 balloonui 客户区控件树的构造种子。把"登录界面长什么样"
 *  从对话框类里抽出成一个可独立编译、可被单元测试直接调用的纯函数模块
 *  —— CBalloonLoginDlg 与 FlamingoUnit 测试工程共用本文件。
 *
 *  设计稿：design/FlamingoIM-standalone.html 内 auth.jsx 的 LoginScreen。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL（Flamingo 侧由 stdafx.h 保证，FlamingoUnit 侧由其 stdafx.h
 *  保证）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonauth {

/**
 *  CtrlId —— 登录界面里所有"需要被对话框代码引用"的控件的数字 id。
 *
 *  balloonui 的 DuiXmlBuilder 用数字 UINT 作 ctrlId（XML 里写 id="100"），
 *  对话框侧再用 DuiControl::FindCtrlById(id) 取回控件指针。集中成一个
 *  enum，避免 XML 字符串与 C++ 代码各写一份魔数、对不上。
 *
 *  纯装饰、不需代码引用的控件（Banner 占位、底部 spacer）在 XML 里不写
 *  id，GetCtrlId() 返回 0 —— 0 视为"未命名"，不属于本 enum。
 */
enum CtrlId : UINT
{
    // ---- 顶部品牌 / 头像区 ----
    kIdLoginBanner   = 140,   // 顶部 Banner（AuthBanner）—— 对话框借此回填用户名

    // ---- 输入区 ----
    kIdAccountCombo  = 100,   // 账号下拉框：可编辑 DuiComboBox，可手输入也可下拉选历史账号
    kIdPasswordEdit  = 101,   // 密码输入框：DuiEditHost，密码模式

    // ---- 选项区 ----
    kIdRememberPwd   = 110,   // “记住密码”复选框：checkbox 风格 DuiButton
    kIdAutoLogin     = 111,   // “自动登录”复选框：checkbox 风格 DuiButton

    // ---- 主操作 ----
    kIdLoginButton   = 120,   // “登录”按钮：push 风格 DuiButton。登录期间文字变为
                              // “取消登录”，点击即中止本轮等待（见 LoginProgressState）

    // ---- 状态区（登录按钮下方，平时整块隐藏）----
    kIdStatusArea    = 150,   // 状态区容器（vbox）：登录中与登录失败时才显示
    kIdStatusText    = 151,   // 状态文字：登录中显示“正在登录…”，失败显示具体原因
    kIdStatusProgress = 152,  // 不确定进度条（marquee），仅登录中显示；相位由对话框定时器驱动

    // ---- 底部链接 ----
    kIdLinkRegister  = 130,   // “注册账号”链接：点击打开注册对话框
    kIdLinkNetwork   = 131,   // “网络设置”链接：点击打开网络设置对话框
    kIdLinkForgotPwd = 132,   // “忘记密码”链接：暂为占位（服务端无找回密码接口）
    // 133 曾是“扫码登录”链接，因服务端始终没有该接口、点了只弹“暂未开放”，
    // 已从登录页移除。此处保留空号不再复用，免得与老版本布局的 id 撞上。
};

/**
 *  构造登录对话框的客户区控件树（不含窗口边框 —— 边框由 DuiFrameWindow
 *  自身负责）。
 *
 *  返回：客户区根控件（一个 vbox），调用方拿到所有权；解析失败返回
 *        nullptr（内嵌 XML 为编译期常量、形态固定，理论上不会失败）。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildLoginBody();

/**
 *  ValidateLoginInput 的判定结果。
 */
enum LoginInputResult
{
    kLoginInputOk         = 0,   // 账号、密码均非空 —— 可发起登录
    kLoginInputNoAccount  = 1,   // 账号为空或纯空白 —— 应提示"请输入账号"
    kLoginInputNoPassword = 2,   // 密码为空或纯空白 —— 应提示"请输入密码"
};

/**
 *  校验登录输入。账号优先于密码判定（账号无效时不再看密码）。
 *    account：  账号文本（账号下拉框当前文本）。
 *    password： 密码文本（密码输入框当前文本）。
 *  返回：见 LoginInputResult。纯空白字符串（全空格 / 制表符等）视同空。
 */
LoginInputResult ValidateLoginInput(const CString& account, const CString& password);

/**
 *  MakePostRegisterFill 的产物 —— 注册成功后登录表单各控件应处的状态。
 *
 *  为什么要单独立一个结构体：注册成功后要动的不止账号一处（账号框、密码框、
 *  两个勾选框各有各的取值），把"该回填成什么样"从对话框代码里抽出来做成纯
 *  函数，才能不建窗口就把它测住 —— "密码没跟着回填"这个 bug 正是因为这段
 *  决策原先散在 UI 事件处理里、无从测起。
 */
struct PostRegisterFill
{
    bool    m_shouldFill;    // 是否应执行回填；false 时其余字段无意义，调用方保持界面原样
    CString m_account;       // 账号框应显示的账号（服务端注册时分配的那个）
    CString m_password;      // 密码框应显示的密码（用户注册时设置的明文，原样不作修剪）
    bool    m_rememberPwd;   // “记住密码”复选框应处的勾选状态
    bool    m_autoLogin;     // “自动登录”复选框应处的勾选状态
};

/**
 *  算出注册成功后登录表单应回填成什么样。
 *
 *  规则：
 *    · 账号为空或纯空白 —— 视为服务端没给出账号（注册成功的应答里理论上
 *      不会这样），m_shouldFill 为 false，调用方应保持登录界面原样并记一条
 *      错误日志；
 *    · 账号有效 —— 账号与密码分别回填，密码<u>原样</u>写入、不作 Trim
 *      （首尾空格在密码里是有效字符，去掉就登不上去了）；
 *    · 两个勾选框一律回到未勾选 —— 用户此刻还没表达过"记住密码"的意愿，
 *      而且这与调用方同时写进历史账号列表的那条记录（空密码 + 两项皆否）
 *      保持一致，免得界面显示的与盘上存的对不上。
 *
 *    registeredAccount： 注册成功后服务端分配的账号。
 *    registeredPassword：用户在注册窗里设置的明文密码；传空串则密码框被清空
 *                        —— 宁可空着，也不能把上一个账号的密码留在框里。
 *  返回：见 PostRegisterFill。纯函数，无副作用，便于单元测试。
 */
PostRegisterFill MakePostRegisterFill(const CString& registeredAccount,
                                      const CString& registeredPassword);

/**
 *  决定登录窗顶部 Banner 头像方块的底色。
 *
 *  规则（与"切换账号头像跟着变 / 未登录账号显示默认头像"的需求对应）：
 *    · 已登录过的账号（账号在历史登录列表里）—— 按账号字符串稳定取一个
 *      调色板色，使不同账号头像可凭颜色区分，同一账号每次恒定同色；
 *    · 未登录过的账号、或账号为空 —— 一律用默认橙色方块（= 调色板第 0 个
 *      RGB(0xFF,0x7A,0x45)），即"现在默认的头像"。
 *
 *    account：       当前账号文本（账号下拉框当前文本）。
 *    isKnownAccount：该账号是否在历史登录列表里；true 表示曾登录成功过。
 *                    由调用方查历史列表（CLoginAccountList::Find）得出。
 *  返回：头像方块底色 COLORREF。纯函数，无副作用，便于单元测试。
 */
COLORREF ChooseBannerAvatarColor(const CString& account, bool isKnownAccount);

/**
 *  选出登录窗头像下方那行要显示的名字。
 *
 *  规则（与"切换到登录过的账号时显示他的姓名"的需求对应）：
 *    · 历史登录列表里存过该账号的姓名 —— 显示姓名，头像里的首字母也随之
 *      变成姓名的首字；
 *    · 没有姓名（账号没登录过、老版本客户端存下的账号、或服务器返回的
 *      昵称为空）—— 显示账号本身，与本需求之前的表现一致。
 *
 *    account： 当前账号文本（账号下拉框当前文本）。
 *    nickName：该账号在历史登录列表里存下的姓名；账号查不到时传空串。
 *  返回：头像下方要显示的文本。纯函数，无副作用，便于单元测试。
 */
CString ChooseBannerDisplayName(const CString& account, const CString& nickName);

/**
 *  账号下拉框值变化通知里"没有选中项"的 extra 取值。
 *
 *  DuiComboBox 在 editable 模式下手工输入、且输入文本不完全匹配任何一项时，
 *  以 -1 作为通知的 extra（见 balloonui/Controls/Input/DuiComboBox.h 的事件
 *  表）。本模块也用它表示"本次并非由通知触发，只想读输入框里现在的文本"。
 */
const int kComboExtraNoItem = -1;

/**
 *  判断账号下拉框的值变化通知，该按<u>项索引</u>取账号，还是<u>读输入框文本</u>。
 *
 *  为什么需要这个判断：DuiComboBox 从下拉列表选中一项时，是"先同步发出
 *  DUIN_VALUECHANGED 通知、之后才把选中项文本写进内嵌 EDIT"（见
 *  DuiComboBox::OnPopupSelected —— SetCurSel(notify=true) 排在
 *  m_edit->SetText 之前，而通知走的是同步 SendMessage）。于是在通知处理函数
 *  里调 GetText() 读到的仍是切换<u>前</u>的旧账号，据它刷新界面就会出现
 *  "账号框已经变成新账号了，头像下方的名字和回填的密码却还停在上一个账号"。
 *  通知带来的项索引在那一刻则已经是正确的，故优先用它。
 *
 *    notifyExtra：通知里的 extra。>= 0 为用户选中的项索引；kComboExtraNoItem
 *                 (-1) 表示手工输入、或调用方并非由通知触发。
 *    itemCount：  下拉框当前的项数（取值 >= 0），用于挡掉越界索引。
 *  返回：true 表示应当用 GetItemText(notifyExtra) 取账号；false 表示应当读
 *        输入框当前文本 GetText()。纯函数，无副作用，便于单元测试。
 */
bool ShouldReadAccountFromComboItem(int notifyExtra, int itemCount);

} // namespace balloonauth
