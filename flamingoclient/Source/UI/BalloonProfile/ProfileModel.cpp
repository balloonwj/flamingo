/**
 *  ProfileModel.cpp
 *
 *  个人信息数据模型的实现。
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

#include "ProfileModel.h"
#include "../../Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

namespace balloonprofile {

namespace {

// 三个在线状态的中文显示名，下标对应 ProfileStatus。
const TCHAR* const kStatusText[kProfileStatusCount] =
{
    TR_NOOP(L"在线"),   // kProfileStatusOnline
    TR_NOOP(L"离开"),   // kProfileStatusAway
    TR_NOOP(L"忙碌"),   // kProfileStatusBusy
};

// 性别的中文规范值（模型里存的就是它），下标即编辑态下拉框的序号；显示时经 TR 换成当前界面语言
const TCHAR* const kGenderValues[kProfileGenderCount] =
{
    TR_NOOP(L"女"),
    TR_NOOP(L"男"),
    TR_NOOP(L"保密"),
};

// 三个在线状态的圆点颜色，下标对应 ProfileStatus。取自设计稿主题色板：
//   在线 #22C55E、离开 #F59E0B、勿扰 #EF4444。
const COLORREF kStatusColor[kProfileStatusCount] =
{
    RGB(0x22, 0xC5, 0x5E),   // kProfileStatusOnline：绿
    RGB(0xF5, 0x9E, 0x0B),   // kProfileStatusAway：橙
    RGB(0xEF, 0x44, 0x44),   // kProfileStatusBusy：红
};

} // anonymous namespace

// i18n-ignore-begin: 模型的模拟初始数据，打开资料窗时由真实资料覆盖，不显示给用户
ProfileModel::ProfileModel()
    : m_strName(_T("林知夏"))
    , m_strAccount(_T("88234561"))
    , m_strNickname(_T("林知夏"))
    , m_strSignature(_T("想做一件让自己骄傲的事"))
    , m_strGender(_T("女"))
    , m_strBirthday(_T("1996-08-12"))
    , m_strRegion(_T("上海"))
// i18n-ignore-end
    , m_strEmail(_T("lin.zhixia@flamingo.im"))
    , m_nStatus(kProfileStatusOnline)
{
}

bool ProfileModel::IsValidStatus(int status)
{
    return status >= 0 && status < kProfileStatusCount;
}

const CString& ProfileModel::Name() const
{
    return m_strName;
}

const CString& ProfileModel::Account() const
{
    return m_strAccount;
}

const CString& ProfileModel::Nickname() const
{
    return m_strNickname;
}

const CString& ProfileModel::Signature() const
{
    return m_strSignature;
}

const CString& ProfileModel::Gender() const
{
    return m_strGender;
}

const CString& ProfileModel::Birthday() const
{
    return m_strBirthday;
}

const CString& ProfileModel::Region() const
{
    return m_strRegion;
}

const CString& ProfileModel::Email() const
{
    return m_strEmail;
}

int ProfileModel::Status() const
{
    return m_nStatus;
}

void ProfileModel::SetNickname(const CString& value)
{
    m_strNickname = value;
}

void ProfileModel::SetSignature(const CString& value)
{
    m_strSignature = value;
}

void ProfileModel::SetGender(const CString& value)
{
    m_strGender = value;
}

void ProfileModel::SetBirthday(const CString& value)
{
    m_strBirthday = value;
}

void ProfileModel::SetRegion(const CString& value)
{
    m_strRegion = value;
}

void ProfileModel::SetEmail(const CString& value)
{
    m_strEmail = value;
}

void ProfileModel::SetStatus(int status)
{
    if (IsValidStatus(status))
    {
        m_nStatus = status;
    }
}

const TCHAR* ProfileStatusText(int status)
{
    if (status >= 0 && status < kProfileStatusCount)
    {
        return TR(kStatusText[status]);
    }
    return TR(kStatusText[kProfileStatusOnline]);
}

CString ProfileGenderText(const CString& value)
{
    const int index = ProfileGenderIndex(value);
    if (index < 0)
    {
        return value;
    }
    return CString(TR(kGenderValues[index]));
}

int ProfileGenderIndex(const CString& value)
{
    for (int i = 0; i < kProfileGenderCount; ++i)
    {
        if (value == kGenderValues[i])
        {
            return i;
        }
    }
    return -1;
}

CString ProfileGenderValueAt(int index)
{
    if (index < 0 || index >= kProfileGenderCount)
    {
        return CString();
    }
    return CString(kGenderValues[index]);
}

COLORREF ProfileStatusColor(int status)
{
    if (status >= 0 && status < kProfileStatusCount)
    {
        return kStatusColor[status];
    }
    return kStatusColor[kProfileStatusOnline];
}

} // namespace balloonprofile
