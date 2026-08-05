/**
 *  登录账号生成规则单元测试，test_accountname.cpp
 *  Suite AN：MakeEmployeeAccount。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#include "doctest.h"
#include "../chatserversrc/AccountName.h"

TEST_CASE("AN1 MakeEmployeeAccount 按用户ID生成 EMP 前缀账号")
{
    CHECK(MakeEmployeeAccount(1) == "EMP1");
    CHECK(MakeEmployeeAccount(1024) == "EMP1024");
    //与 admin 控制台历史上生成的账号格式一致，存量账号无需迁移。
    CHECK(MakeEmployeeAccount(90000012) == "EMP90000012");
}

TEST_CASE("AN2 MakeEmployeeAccount 对非法用户ID返回空串")
{
    //ID 分配一旦出问题，宁可返回空串让建号失败，也不要造出
    //"EMP0" / "EMP-1" 这种会与别人相撞的账号。
    CHECK(MakeEmployeeAccount(0).empty());
    CHECK(MakeEmployeeAccount(-1).empty());
}

TEST_CASE("AN3 不同用户ID生成的账号互不相同")
{
    //账号的唯一性完全建立在用户ID唯一之上，这里把这条契约钉死。
    CHECK(MakeEmployeeAccount(1) != MakeEmployeeAccount(2));
    CHECK(MakeEmployeeAccount(10) != MakeEmployeeAccount(100));
    //前缀拼接不能产生歧义：EMP1 + "0" 不应等于 EMP10 的那种前缀混淆。
    CHECK(MakeEmployeeAccount(10) == "EMP10");
    CHECK(MakeEmployeeAccount(1) == "EMP1");
}
