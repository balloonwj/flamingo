/**
 *  RegisterModel.h
 *
 *  注册对话框的数据模型 —— 持有注册流程里用户填写的全部字段
 *  （昵称 / 密码 / 确认密码 / 手机号 / 公司名称）、当前所处步骤，
 *  并提供纯客户端校验逻辑（两次密码一致、手机号格式、公司名非空等）。
 *
 *  这里<u>没有</u>登录账号字段：账号不由用户起名，一律由服务端按用户ID
 *  自动生成，注册成功后随应答回传，展示在完成页上。
 *
 *  注册的语义是"加入一家已存在的企业"：用户填的公司全称须能在服务端
 *  精确匹配到唯一一家企业，注册成功后账号会被自动挂进该企业的公司根
 *  部门。公司是否存在只有服务端知道，本模型只做格式层面的校验。
 *
 *  纯数据 + 校验，不涉及任何 UI / 网络 —— 校验逻辑要可单测，故与
 *  窗口代码彻底分离。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （需要 CString）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

namespace balloonregister {

/**
 *  注册流程的两个步骤。枚举值也用作步骤指示器圆点的序号基准。
 *  原先介于两者之间的"手机验证"一步已随短信验证码一起去掉：注册不再
 *  下发验证码，手机号并入第一步收取。
 */
enum RegisterStep
{
    kRegStepFillInfo = 1,   // 步骤 1：填写资料（昵称 / 密码 / 确认密码 / 手机号 / 公司名称）
    kRegStepDone     = 2,   // 步骤 2：完成（展示注册结果，引导去登录）
};

// 注册流程的总步骤数。
const int kRegStepCount = 2;

/**
 *  字段校验结果。逐值对应一种校验失败原因，kRegValidOk 表示通过。
 *  窗口代码据此弹出对应中文提示。
 */
enum RegisterValidateResult
{
    kRegValidOk             = 0,  // 校验通过：所有字段合法
    kRegValidNickEmpty      = 1,  // 失败：昵称为空
    kRegValidPwdEmpty       = 2,  // 失败：密码为空
    kRegValidPwdTooShort    = 3,  // 失败：密码长度不足最小位数
    kRegValidPwdMismatch    = 4,  // 失败：两次输入的密码不一致
    kRegValidPhoneFormat    = 5,  // 失败：手机号不是 11 位且以 1 开头的数字串
    kRegValidCompanyEmpty   = 6,  // 失败：公司名称为空（去除首尾空白后仍为空）
    kRegValidCompanyTooLong = 7,  // 失败：公司名称超过服务端能存下的长度上限
};

// 密码长度下限（位）。
const int kRegPasswordMinLen = 6;

// 手机号固定长度（中国大陆手机号 11 位）。
const int kRegPhoneLen = 11;

// 公司名称长度上限（字符数）。取值与服务端 t_enterprise.f_name 的
// varchar(128) 对齐（MySQL 的 VARCHAR(N) 按字符计，不是按字节），
// 超过这个长度的名字在库里根本存不下，也就必然匹配不到任何企业，
// 与其发上去等一个"公司不存在"，不如在本地就拦住并给出准确提示。
const int kRegCompanyMaxLen = 128;

// 取默认公司的全称。注册窗「公司名称」组合框的下拉里只有这一项，打开注册窗时默认选中它，
// 用户不改就注册进这家公司。取值来自 flamingo.ini 的 [ui] defaultcompany（启动时读入
// g_strDefaultCompany，见 App/BrandText.h），须与服务端 chatserver.conf 的 defaultenterprise
// 配成同一个值：chatserver 启动时保证库里有那家企业。
// 返回：g_strDefaultCompany；尚未读取配置（为空串，如单元测试里）时返回内置的
// brandtext::kDefaultCompany，即 FlamingoIM.org。
CString RegDefaultCompany();

/**
 *  RegisterModel —— 注册流程的数据模型。
 *
 *  持有用户在表单里输入的所有字段以及"当前在第几步"。窗口在步骤切换时：
 *  先把表单内容提交回本模型（SetXxx），再按校验结果决定能否推进 / 回退。
 *  校验逻辑（ValidateForm）是纯函数，不依赖任何控件，便于单测。
 */
class RegisterModel
{
public:
    // 构造：当前步置于步骤 1，公司名为默认公司 RegDefaultCompany()，其余字段为空。
    RegisterModel();

    // ---- 步骤 ----

    // 读取当前所处步骤，取值见 RegisterStep。
    int  CurrentStep() const;

    // 直接设置当前步骤。step 越界（不在 1..kRegStepCount）则忽略。
    //   step：目标步骤，取值见 RegisterStep。
    void SetCurrentStep(int step);

    // 推进到下一步。已在最后一步则保持不变。返回推进后的步骤号。
    int  GoNextStep();

    // 回退到上一步。已在第一步则保持不变。返回回退后的步骤号。
    int  GoPrevStep();

    // ---- 字段读写（窗口在表单 <-> 模型间同步用）----
    // 所有 setter 接受 nullptr，按空字符串处理。

    void           SetNickname(LPCTSTR v);
    const CString& Nickname() const;

    void           SetPassword(LPCTSTR v);
    const CString& Password() const;

    void           SetConfirmPassword(LPCTSTR v);
    const CString& ConfirmPassword() const;

    void           SetPhone(LPCTSTR v);
    const CString& Phone() const;

    void           SetCompany(LPCTSTR v);
    const CString& Company() const;

    // ---- 校验（纯函数，可单测）----

    // 校验"填写资料"这一步的全部字段。
    //   返回：kRegValidOk 表示全部合法；否则为首个失败原因（见
    //         RegisterValidateResult）。校验顺序：昵称非空 → 密码非空 →
    //         密码长度 → 两次密码一致 → 手机号格式 → 公司名非空 →
    //         公司名长度。
    //   注意：公司是否真实存在只有服务端能判定，本函数不做任何此类判断；
    //         登录账号由服务端生成，这里也就没有账号相关的校验。
    int ValidateForm() const;

    // 把一个校验结果翻译成可直接展示给用户的中文提示文字。
    //   result：取值见 RegisterValidateResult。
    //   返回：常量字符串指针（静态存储，调用方不得释放）。
    static LPCTSTR ValidateMessage(int result);

    // 把注册应答的服务端错误码翻译成给用户看的提示文字（经 TR 换成当前界面语言）。服务端应答里的
    // msg 是英文诊断串，只进日志，不给用户看。
    //   code：服务端结果码，取值见 net/Protocol.h 的 enum error_code；不认识的码给出通用的「注册失败」提示。
    //   返回：常量字符串指针（静态存储，调用方不得释放）。
    static LPCTSTR ServerErrorMessage(int code);

private:
    // 判断 step 是否是合法步骤号（1..kRegStepCount）。
    static bool IsValidStep(int step);

private:
    int     m_currentStep;       // 当前步骤，取值见 RegisterStep
    CString m_nickname;          // 昵称（步骤 1）
    CString m_password;          // 设置的密码（步骤 1）
    CString m_confirmPassword;   // 确认密码（步骤 1），须与 m_password 一致
    CString m_phone;             // 手机号（步骤 1），11 位、以 1 开头
    CString m_company;           // 公司全称（步骤 1），须与服务端 t_enterprise.f_name 完全一致
};

} // namespace balloonregister
