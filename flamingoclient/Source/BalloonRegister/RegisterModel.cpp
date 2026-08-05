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
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "RegisterModel.h"

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
// 构造
// =======================================================================

RegisterModel::RegisterModel()
    : m_currentStep(kRegStepFillInfo)
{
    // CString 成员默认即空字符串，无需显式初始化。
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
        return _T("校验通过。");
    case kRegValidNickEmpty:
        return _T("请填写昵称。");
    case kRegValidPwdEmpty:
        return _T("请设置密码。");
    case kRegValidPwdTooShort:
        return _T("密码长度至少 6 位。");
    case kRegValidPwdMismatch:
        return _T("两次输入的密码不一致。");
    case kRegValidPhoneFormat:
        return _T("请输入正确的 11 位手机号。");
    case kRegValidCompanyEmpty:
        return _T("请填写公司名称。");
    case kRegValidCompanyTooLong:
        return _T("公司名称过长，请填写公司全称的正确写法。");
    default:
        return _T("输入有误，请检查后重试。");
    }
}

} // namespace balloonregister
