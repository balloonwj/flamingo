/**
 *  BalloonProfileLayout.h
 *
 *  「个人信息」对话框的 balloonui 客户区控件树构造种子。把对话框长什么样
 *  从对话框类里抽出成一个可独立编译、可被单元测试直接调用的纯函数模块
 *  —— CBalloonProfileDlg 与 FlamingoUnit 测试工程共用本文件。
 *
 *  设计稿:design/个人信息.png(查看态) / design/个人信息02.png(编辑态)。
 *  同一窗口承载两套右侧值控件:查看态用 DuiLabel,编辑态用 DuiEditHost +
 *  DuiComboBox;两套同时建好,按 SetVisible 切换。底部 4 个按钮(关闭/
 *  编辑资料/取消/保存)同样两两切换。
 *
 *  本文件遵循 balloonui 头文件约定:include 它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#pragma once

#include <memory>
#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonmain {

/**
 *  CtrlId —— 个人信息对话框里"需要被代码引用"的所有控件数字 id。
 *
 *  与主面板 CtrlId(200 段)互不重叠,取 300 段。装饰性 spacer / 静态文字
 *  在 XML 里不写 id,GetCtrlId() 返 0,不属于本 enum。
 */
enum ProfileCtrlId : UINT
{
    // ---- 顶部信息区 ----
    kIdProfileAvatar      = 300,  // 头像格(显示头像字 + 底色,后续可挂自定义头像位图)
    kIdProfileNickname    = 301,  // 顶部大字昵称
    kIdProfileAccount     = 302,  // "账号 MY0308" 行(显示登录账号名;ModeLink 可点复制)

    // ---- 6 行信息;每行两套右侧控件,View 是查看态 DuiLabel,Edit 是
    //      编辑态 DuiEditHost(性别除外:DuiComboBox)。
    kIdNicknameView       = 320,  // 昵称查看 DuiLabel
    kIdNicknameEdit       = 321,  // 昵称编辑 DuiEditHost
    kIdSignatureView      = 322,  // 个性签名查看 DuiLabel
    kIdSignatureEdit      = 323,  // 个性签名编辑 DuiEditHost
    kIdGenderView         = 324,  // 性别查看 DuiLabel
    kIdGenderEdit         = 325,  // 性别编辑 DuiComboBox
    kIdBirthdayView       = 326,  // 生日查看 DuiLabel
    kIdBirthdayEdit       = 327,  // 生日编辑 DuiEditHost(YYYY-MM-DD)
    kIdLocationView       = 328,  // 所在地查看 DuiLabel
    kIdLocationEdit       = 329,  // 所在地编辑 DuiEditHost
    kIdEmailView          = 330,  // 邮箱查看 DuiLabel
    kIdEmailEdit          = 331,  // 邮箱编辑 DuiEditHost
    // 下面两行<u>只有查看态</u>，没有配套的编辑控件：部门与职位由管理员在
    // admin 控制台维护，本人改不了，编辑态下它们原样保持只读展示。
    kIdDeptView           = 332,  // 部门查看 DuiLabel（形如"产品部 / 设计组"）
    kIdPositionView       = 333,  // 职位查看 DuiLabel

    // ---- 底部按钮(同一时刻只显 2 个,与 View/Edit 模式一致) ----
    kIdBtnClose           = 340,  // 查看态:关闭
    kIdBtnEdit            = 341,  // 查看态:编辑资料
    kIdBtnCancel          = 342,  // 编辑态:取消
    kIdBtnSave            = 343,  // 编辑态:保存
};

/**
 *  ProfileDataInput —— 个人信息对话框的数据输入,从 CBuddyInfo 拆解出
 *  helper 需要的字段。
 *
 *  把"CBuddyInfo → 控件填值"解耦,避免 helper 直接依赖 CBuddyInfo —— 测试
 *  工程不引 UserSessionData.cpp,纯字段结构便于单测各种边界。
 */
struct ProfileDataInput
{
    UINT     userid;       // 用户ID,顶部"账号 X"行显示;同时决定 avatar 配色
    CString  account;      // 账号(MY0312 这种),昵称为空时的兜底显示
    CString  nickname;     // 昵称
    CString  signature;    // 个性签名
    int      gender;       // 0=男,1=女(沿用 GroupInfoDlg.cpp:159 / LogonUserInfoDlg.cpp:288 约定)
    long     birthday;     // YYYYMMDD 整数(如 19950402);0 = 未设置
    CString  address;      // 所在地
    CString  email;        // 邮箱
    // 下面两项来自企业组织架构（cmd_get_contacts 下发的部门树），不是用户资料的
    // 一部分，本人改不了，展示为只读。取不到时留空，对应行显示"—"。
    CString  dept;         // 部门路径，形如"产品部 / 设计组"
    CString  position;     // 职位
};

/**
 *  把 ProfileDataInput 写入对话框控件树:
 *    · 顶部 Banner:头像底色 / fallback name、昵称、"账号 X" 三个控件
 *    · 6 行 View 系列 DuiLabel(查看态文字)
 *    · 6 行 Edit 系列控件初值(DuiEditHost SetText / DuiComboBox AddString + SetCurSel)
 *
 *  本函数不切换可见性(由对话框类按"查看 / 编辑"模式自行处理)。
 *    root  BuildProfileBody 返回的客户区根控件;null 返 false。
 *    info  当前用户字段。
 *  返回:成功 true;root 为 null 或核心控件(顶部 nickname label)找不到时 false。
 */
bool ApplyProfileDataToView(balloonwjui::DuiControl* root,
                            const ProfileDataInput& info);

/**
 *  把 "YYYY-MM-DD" 文本解析成 YYYYMMDD 整数（内部 FormatBirthday 的逆操作）。
 *  保存资料时用它把生日编辑框的文本转回 m_nBirthday / 协议所需的整数。
 *    · 空串（去首尾空白后）→ 0：表示"未设生日"，与 FormatBirthday(0)=="" 往返一致;
 *    · 格式非法（长度不对 / 分隔符不是 '-' / 含非数字 / 年月日越界）→ -1：调用方据此
 *      提示用户"生日格式应为 YYYY-MM-DD"，不提交;
 *    · 合法 → 对应 YYYYMMDD 整数（如 "1995-04-02" → 19950402）。
 *  范围校验与 FormatBirthday 同口径：年 1900-9999、月 1-12、日 1-31（不做"2 月 30 日"
 *  这类逐月天数精校，与现有 FormatBirthday 保持一致即可）。
 */
long ParseProfileBirthday(const CString& text);

/**
 *  个人信息对话框主按钮(编辑资料 / 保存)圆角橙底的圆角半径(逻辑像素)。
 *  与 balloonui DuiButton 内置 kCornerRadiusPx(8)保持一致,使主按钮与关闭 /
 *  取消(Variant::Default 内置 8px 圆角)的圆角风格统一。生成圆角橙位图的尺寸、
 *  以及 SetBgInsets 的四角 9-grid 切片宽度都引用它,保证四角不被 stretch 拉直。
 */
const int kProfilePrimaryBtnRadius = 8;

/**
 *  ProfileVisualResources —— 个人信息对话框视觉精修需要的位图资源集合。
 *
 *  caller(CBalloonProfileDlg)在 Open 时一次性 GDI 创建,持有到析构 ——
 *  helper 只读取,不 DeleteObject。任意字段 NULL 时,helper 跳过对应那一
 *  路视觉(便于单元测试用 default-constructed 空 struct)。
 *
 *  设计稿对应(dialogs.jsx PersonalInfoDialog + components.jsx Btn/Modal):
 *    · hPrimaryBg/Hover/Pressed  --accent #FF7A45 圆角橙底(8px 圆角 + alpha),
 *                                编辑资料 / 保存按钮的 9-grid 底图,hover / pressed 渐深
 *  (原有的状态胶囊浅橙/浅灰底与 4 个状态色圆点已随胶囊一并去除。)
 */
struct ProfileVisualResources
{
    HBITMAP hPrimaryBg;          // 主按钮圆角橙底 normal(9-grid,圆角见 kProfilePrimaryBtnRadius)
    HBITMAP hPrimaryBgHover;     // 主按钮圆角橙底 hover(比 normal 略深)
    HBITMAP hPrimaryBgPressed;   // 主按钮圆角橙底 pressed(比 hover 更深)

    ProfileVisualResources()
        : hPrimaryBg(NULL)
        , hPrimaryBgHover(NULL)
        , hPrimaryBgPressed(NULL)
    {
    }
};

/**
 *  设置「个人信息」对话框按钮的视觉样式(本期视觉精修):
 *    · 关闭 / 取消:DuiButton::Variant::Default(白底灰边深字)
 *    · 编辑资料 / 保存:Variant::Primary + SetBgBitmap(圆角橙底)+ 白字
 *
 *  root  BuildProfileBody 返回的控件树;null 时返 false。
 *  res   位图资源集合,字段允许 NULL(对应路径跳过)。
 *  返回:成功 true;root 为 null 或核心按钮找不到时 false。
 */
bool ApplyProfileButtonStyles(balloonwjui::DuiControl* root,
                              const ProfileVisualResources& res);

/**
 *  在「个人信息」对话框的查看态 / 编辑态之间切换(纯 DUI 逻辑,可单测)。
 *    · 翻转 6 组 view/edit 控件 + 4 个底部按钮(Close/Edit ↔ Cancel/Save)的可见性;
 *    · 对受影响的容器(6 个字段行 hbox + footer hbox)逐个 ForceLayout(自身矩形),
 *      绕开 DuiControl::SetRect 在矩形未变时的提前返回 —— 这些容器自身矩形在两态间
 *      不变,只靠根 ForceLayout 不会重排其子,会导致刚切到可见的编辑框 /
 *      Cancel/Save 停留在 {0,0,0,0} 而点不到、显示不出来。
 *
 *  注意:本函数只处理控件可见性与几何重排,<u>不</u>负责 DuiEditHost 的子 EDIT
 *  创建(EnsureCreated 由对话框在窗口就绪后调一次);但本函数的重排会触发
 *  DuiEditHost::Layout 用 SetWindowPos 把已创建的子 EDIT 定位到正确位置。
 *    root  BuildProfileBody 返回的客户区根控件;null 返 false。
 *    edit  true 切到编辑态;false 切到查看态。
 *  返回:root 非空即 true。
 */
bool SetProfileEditMode(balloonwjui::DuiControl* root, bool edit);

/**
 *  构造「个人信息」对话框的客户区控件树。两套右侧控件 + 两套底部按钮
 *  都建好,初始处于"查看态":Edit 系列 ctrlId 控件 SetVisible(false),
 *  View 系列可见;底部 Close/Edit 可见,Cancel/Save 不可见。模式切换
 *  由 SetProfileEditMode 负责。
 *
 *  返回:客户区根控件(一个 vbox),调用方拿走所有权;解析失败返
 *        nullptr(内嵌 XML 为编译期常量、形态固定,理论上不会失败)。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildProfileBody();

} // namespace balloonmain
