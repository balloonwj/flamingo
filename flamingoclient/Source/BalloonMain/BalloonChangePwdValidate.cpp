/**
 *  BalloonChangePwdValidate.cpp
 *
 *  修改密码输入校验的实现。纯逻辑、只依赖 <wchar.h>，不 include stdafx.h ——
 *  与 FlamingoUnit 测试工程共用，在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "BalloonChangePwdValidate.h"

#include <wchar.h>   // wcslen / wcscmp

namespace balloonmain {

namespace {

//宽字符串长度，nullptr 视同空串（长度 0）。
int WLen(const wchar_t* s)
{
    return (s != NULL) ? (int)::wcslen(s) : 0;
}

//宽字符串相等比较，nullptr 视同空串。
bool WEqual(const wchar_t* a, const wchar_t* b)
{
    const wchar_t* sa = (a != NULL) ? a : L"";
    const wchar_t* sb = (b != NULL) ? b : L"";
    return ::wcscmp(sa, sb) == 0;
}

} // anonymous namespace

ChangePwdInputResult ValidateChangePwdInput(const wchar_t* oldPwd,
                                            const wchar_t* newPwd,
                                            const wchar_t* confirmPwd)
{
    //当前密码不能为空
    if (WLen(oldPwd) == 0)
    {
        return kCpwEmptyOld;
    }

    //新密码不能为空
    if (WLen(newPwd) == 0)
    {
        return kCpwEmptyNew;
    }

    //新密码须达到最小长度
    if (WLen(newPwd) < kChangePwdMinLen)
    {
        return kCpwTooShort;
    }

    //两次输入的新密码须一致
    if (!WEqual(newPwd, confirmPwd))
    {
        return kCpwMismatch;
    }

    //新旧密码须不同
    if (WEqual(oldPwd, newPwd))
    {
        return kCpwSameAsOld;
    }

    return kCpwOk;
}

} // namespace balloonmain
