/**
 *  SignatureEditModelTests.cpp
 *
 *  签名提交判定纯逻辑（SignatureEditModel::DecideSignatureCommit）的测试：
 *  trim、与原值相同→不发、改动→发送并去空白、清空允许、空对空不发、超长拦截、
 *  正好上限可发。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/SignatureEditModel.h"

using namespace balloonmain;

TEST_CASE("SEM1 与原值相同 → NoChange", "[signmodel]")
{
    SignCommitDecision d = DecideSignatureCommit(_T("hello"), _T("hello"), kSignatureMaxLen);
    CHECK(d.action == kSignCommitNoChange);
}

TEST_CASE("SEM2 trim 后与原值相同 → NoChange", "[signmodel]")
{
    SignCommitDecision d = DecideSignatureCommit(_T("  hello  "), _T("hello"), kSignatureMaxLen);
    CHECK(d.action == kSignCommitNoChange);
}

TEST_CASE("SEM3 改动 → Send，trimmed 去掉首尾空白", "[signmodel]")
{
    SignCommitDecision d = DecideSignatureCommit(_T("  world  "), _T("hello"), kSignatureMaxLen);
    CHECK(d.action == kSignCommitSend);
    CHECK(d.trimmed == _T("world"));
}

TEST_CASE("SEM4 清空（原非空、编辑 trim 后为空）→ Send 空串", "[signmodel]")
{
    SignCommitDecision d = DecideSignatureCommit(_T("   "), _T("hello"), kSignatureMaxLen);
    CHECK(d.action == kSignCommitSend);
    CHECK(d.trimmed == _T(""));
}

TEST_CASE("SEM5 原空、编辑也空 → NoChange（不发空包）", "[signmodel]")
{
    SignCommitDecision d = DecideSignatureCommit(_T("   "), _T(""), kSignatureMaxLen);
    CHECK(d.action == kSignCommitNoChange);
}

TEST_CASE("SEM6 超过上限 → TooLong", "[signmodel]")
{
    CString tooLong(_T('a'), kSignatureMaxLen + 1);   // 128 个 a
    SignCommitDecision d = DecideSignatureCommit(tooLong, _T("hello"), kSignatureMaxLen);
    CHECK(d.action == kSignCommitTooLong);
}

TEST_CASE("SEM7 正好到上限 → Send", "[signmodel]")
{
    CString atLimit(_T('a'), kSignatureMaxLen);       // 127 个 a
    SignCommitDecision d = DecideSignatureCommit(atLimit, _T("hello"), kSignatureMaxLen);
    CHECK(d.action == kSignCommitSend);
    CHECK(d.trimmed.GetLength() == kSignatureMaxLen);
}
