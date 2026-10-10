/**
 *  RegisterModel.cpp
 *
 *  注册对话框数据模型的实现 —— 字段读写 + 两步流程推进 + 纯客户端
 *  校验逻辑。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "RegisterModel.h"
#include "../../App/BrandText.h"         // g_strDefaultCompany / kDefaultCompany：注册的默认公司
#include "../../Common/I18n/I18n.h"      // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../../net/Protocol.h"          // enum error_code：注册应答的服务端错误码

namespace balloonregister {

namespace {

// 判断单个字符是否是数字 '0'..'9'。
bool IsDigitChar(TCHAR c)
{
    return c >= _T('0') && c <= _T('9');
}

// 判断整个字符串是否全为数字。空串视为 false。
bool IsAllDigits(const CString& s)
{
    if (s.IsEmpty())
    {
        return false;
    }
    for (int i = 0; i < s.GetLength(); ++i)
    {
        if (!IsDigitChar(s.GetAt(i)))
        {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

// =======================================================================
// 默认公司
// =======================================================================

CString RegDefaultCompany()
{
    //启动时读过 flamingo.ini 后用配置值；单元测试等不走启动流程的场合尚未读取，用内置值
    if (g_strDefaultCompany.empty())
        return CString(brandtext::kDefaultCompany);

    return CString(g_strDefaultCompany.c_str());
}

// =======================================================================
// 构造
// =======================================================================

RegisterModel::RegisterModel()
    : m_currentStep(kRegStepFillInfo)
    , m_company(RegDefaultCompany())
{
    // 公司名默认填好默认公司，注册窗打开时组合框显示的就是它；其余 CString 成员默认即空字符串。
}

// =======================================================================
// 步骤
// =======================================================================

bool RegisterModel::IsValidStep(int step)
{
    return step >= kRegStepFillInfo && step <= kRegStepCount;
}

int RegisterModel::CurrentStep() const
{
    return m_currentStep;
}

void RegisterModel::SetCurrentStep(int step)
{
    if (IsValidStep(step))
    {
        m_currentStep = step;
    }
}

int RegisterModel::GoNextStep()
{
    if (m_currentStep < kRegStepCount)
    {
        ++m_currentStep;
    }
    return m_currentStep;
}

int RegisterModel::GoPrevStep()
{
    if (m_currentStep > kRegStepFillInfo)
    {
        --m_currentStep;
    }
    return m_currentStep;
}

// =======================================================================
// 字段读写
// =======================================================================

void RegisterModel::SetNickname(LPCTSTR v)
{
    m_nickname = (v != NULL) ? v : _T("");
}

const CString& RegisterModel::Nickname() const
{
    return m_nickname;
}

void RegisterModel::SetPassword(LPCTSTR v)
{
    m_password = (v != NULL) ? v : _T("");
}

const CString& RegisterModel::Password() const
{
    return m_password;
}

void RegisterModel::SetConfirmPassword(LPCTSTR v)
{
    m_confirmPassword = (v != NULL) ? v : _T("");
}

const CString& RegisterModel::ConfirmPassword() const
{
    return m_confirmPassword;
}

void RegisterModel::SetPhone(LPCTSTR v)
{
    m_phone = (v != NULL) ? v : _T("");
}

const CString& RegisterModel::Phone() const
{
    return m_phone;
}

void RegisterModel::SetCompany(LPCTSTR v)
{
    m_company = (v != NULL) ? v : _T("");
}

const CString& RegisterModel::Company() const
{
    return m_company;
}

// =======================================================================
// 校验
// =======================================================================

int RegisterModel::ValidateForm() const
{
    // 昵称：非空（去除首尾空白后仍有内容）。
    CString nick = m_nickname;
    nick.Trim();
    if (nick.IsEmpty())
    {
        return kRegValidNickEmpty;
    }

    // 这里没有账号校验：登录账号由服务端按用户ID自动生成，用户填不了。

    // 密码：非空。
    if (m_password.IsEmpty())
    {
        return kRegValidPwdEmpty;
    }

    // 密码：长度不少于下限。
    if (m_password.GetLength() < kRegPasswordMinLen)
    {
        return kRegValidPwdTooShort;
    }

    // 两次密码一致。
    if (m_password != m_confirmPassword)
    {
        return kRegValidPwdMismatch;
    }

    // 手机号：固定 11 位、全数字、首位为 '1'。
    if (m_phone.GetLength() != kRegPhoneLen
        || !IsAllDigits(m_phone)
        || m_phone.GetAt(0) != _T('1'))
    {
        return kRegValidPhoneFormat;
    }

    // 公司名称：非空（去除首尾空白后仍有内容）。服务端也会去掉首尾空白再
    // 做精确匹配，这里先按同样口径判空，免得放一个纯空格的名字发上去。
    CString company = m_company;
    company.Trim();
    if (company.IsEmpty())
    {
        return kRegValidCompanyEmpty;
    }

    // 公司名称：长度不超过服务端能存下的上限。
    if (company.GetLength() > kRegCompanyMaxLen)
    {
        return kRegValidCompanyTooLong;
    }

    return kRegValidOk;
}

LPCTSTR RegisterModel::ValidateMessage(int result)
{
    switch (result)
    {
    case kRegValidOk:
        return TR(L"校验通过。");
    case kRegValidNickEmpty:
        return TR(L"请填写昵称。");
    case kRegValidPwdEmpty:
        return TR(L"请设置密码。");
    case kRegValidPwdTooShort:
        return TR(L"密码长度至少 6 位。");
    case kRegValidPwdMismatch:
        return TR(L"两次输入的密码不一致。");
    case kRegValidPhoneFormat:
        return TR(L"请输入正确的 11 位手机号。");
    case kRegValidCompanyEmpty:
        return TR(L"请填写公司名称。");
    case kRegValidCompanyTooLong:
        return TR(L"公司名称过长，请填写公司全称的正确写法。");
    default:
        return TR(L"输入有误，请检查后重试。");
    }
}

LPCTSTR RegisterModel::ServerErrorMessage(int code)
{
    switch (code)
    {
    //公司全称匹配不到企业
    case error_code_enterprise_not_exist:
        return TR(L"找不到这家公司，请核对公司全称是否填写正确；\n"
                  L"若确认无误，请联系公司管理员确认企业是否已开通。");
    //企业待审核或已停用
    case error_code_enterprise_disabled:
        return TR(L"这家公司当前不可加入（尚未开通或已停用），请联系公司管理员。");
    //企业尚未建立公司根部门
    case error_code_enterprise_no_root_dept:
        return TR(L"这家公司的组织架构尚未初始化，暂时无法加入，请联系公司管理员。");
    //企业在管理台关闭了「允许用户自助注册」，新员工只能由管理员新建账号
    case error_code_register_closed:
        return TR(L"这家公司已关闭自助注册，请联系公司管理员为你开通账号。");
    //手机号已被注册
    case error_code_already_registered:
        return TR(L"该账号或手机号已被注册，请更换后重试。");
    //服务端落库建号失败：与用户填的内容无关（登录账号由服务端自动生成），只能稍后重试
    case error_code_register_fail:
        return TR(L"注册失败，请稍后重试。");
    //请求不完整
    case error_code_bad_request:
        return TR(L"提交的信息不完整，请检查各项内容后重试。");
    //昵称命中所选企业的敏感词库（docs/敏感词方案.md 8.6）
    case error_code_profile_sensitive_word:
        return TR(L"昵称包含敏感词，请修改后重试。");
    //其余错误码与传输失败
    default:
        return TR(L"注册失败，请稍后重试。");
    }
}

} // namespace balloonregister
