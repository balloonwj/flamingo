/**
 *  BalloonChangePwdValidateTests.cpp
 *
 *  修改密码输入校验 ValidateChangePwdInput 的测试：覆盖空旧 / 空新 / 新密码
 *  不足 8 位 / 两次不一致 / 新旧相同 / 合法 六种结论。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯逻辑、不触 UI。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/BalloonChangePwdValidate.h"

using namespace balloonmain;

TEST_CASE("CPV1 当前密码为空 → kCpwEmptyOld", "[changepwd]")
{
    CHECK(ValidateChangePwdInput(L"", L"newpass12", L"newpass12") == kCpwEmptyOld);
    CHECK(ValidateChangePwdInput(NULL, L"newpass12", L"newpass12") == kCpwEmptyOld);
}

TEST_CASE("CPV2 新密码为空 → kCpwEmptyNew", "[changepwd]")
{
    CHECK(ValidateChangePwdInput(L"oldpass1", L"", L"") == kCpwEmptyNew);
}

TEST_CASE("CPV3 新密码不足 8 位 → kCpwTooShort", "[changepwd]")
{
    // "short7" 为 6 个字符，少于 kChangePwdMinLen(8)
    CHECK(ValidateChangePwdInput(L"oldpass1", L"short7", L"short7") == kCpwTooShort);
}

TEST_CASE("CPV4 两次新密码不一致 → kCpwMismatch", "[changepwd]")
{
    CHECK(ValidateChangePwdInput(L"oldpass1", L"newpass12", L"newpass13") == kCpwMismatch);
}

TEST_CASE("CPV5 新密码与当前密码相同 → kCpwSameAsOld", "[changepwd]")
{
    CHECK(ValidateChangePwdInput(L"samepass1", L"samepass1", L"samepass1") == kCpwSameAsOld);
}

TEST_CASE("CPV6 合法输入 → kCpwOk", "[changepwd]")
{
    CHECK(ValidateChangePwdInput(L"oldpass1", L"newpass12", L"newpass12") == kCpwOk);
}
