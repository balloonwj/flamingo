/**
 *  登录账号生成规则单元测试，test_accountname.cpp
 *  Suite AN：MakeEmployeeAccount。
 *  2026-10-06（docs/登录账号与手机号登录方案.md）起：
 *    - LGA1：MakeRandomAccount，前缀为空时用 EMP，数字部分恰好 8 位，越界返回空串；
 *    - LGA2：NormalizeAccountPrefix，字母与点号、1～10 个字符、至少一个字母、转大写，空串表示清除；
 *    - LGA8：IsValidPhoneNumber，11 位以 1 开头的数字合格，其余不合格。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#include <string>

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

TEST_CASE("LGA1 MakeRandomAccount：前缀 + 8 位数字，前缀为空时用 EMP，数字越界返回空串")
{
    CHECK(MakeRandomAccount("", 48203917) == "EMP48203917");
    CHECK(MakeRandomAccount("HW.", 48203917) == "HW.48203917");
    CHECK(MakeRandomAccount("FL.IM", 10000000) == "FL.IM10000000");
    CHECK(MakeRandomAccount("", RANDOM_ACCOUNT_NUMBER_MAX) == "EMP99999999");

    //数字部分必须恰好 8 位：越界（含 7 位、9 位、负数）返回空串，让建号失败而不是造出格式不一的账号
    CHECK(MakeRandomAccount("", RANDOM_ACCOUNT_NUMBER_MIN - 1).empty());
    CHECK(MakeRandomAccount("", RANDOM_ACCOUNT_NUMBER_MAX + 1).empty());
    CHECK(MakeRandomAccount("HW", -1).empty());
}

TEST_CASE("LGA2 NormalizeAccountPrefix：字母与点号、1～10 个字符、至少一个字母、转大写，空串表示清除")
{
    std::string out = "unchanged";
    CHECK(NormalizeAccountPrefix("hw", out));
    CHECK(out == "HW");
    CHECK(NormalizeAccountPrefix(" fl.im ", out));
    CHECK(out == "FL.IM");
    CHECK(NormalizeAccountPrefix("HW.", out));
    CHECK(out == "HW.");
    CHECK(NormalizeAccountPrefix("jingcheng.", out));
    CHECK(out == "JINGCHENG.");
    CHECK(NormalizeAccountPrefix("ABCDEFGHIJ", out));   //恰好 10 个字符
    CHECK(out == "ABCDEFGHIJ");

    //空串与纯空白：清除前缀（合法），规整结果为空串
    out = "x";
    CHECK(NormalizeAccountPrefix("", out));
    CHECK(out.empty());
    out = "x";
    CHECK(NormalizeAccountPrefix("   ", out));
    CHECK(out.empty());

    //不合格：超长、含数字、含横线、含汉字、只有点号、中间有空格
    CHECK_FALSE(NormalizeAccountPrefix("ABCDEFGHIJK", out));
    CHECK_FALSE(NormalizeAccountPrefix("HW1", out));
    CHECK_FALSE(NormalizeAccountPrefix("H-W", out));
    CHECK_FALSE(NormalizeAccountPrefix(u8"华为", out));
    CHECK_FALSE(NormalizeAccountPrefix("..", out));
    CHECK_FALSE(NormalizeAccountPrefix("H W", out));
}

TEST_CASE("LGA8 IsValidPhoneNumber：11 位以 1 开头的数字合格，其余不合格")
{
    CHECK(IsValidPhoneNumber("13800005001"));
    CHECK(IsValidPhoneNumber("19912345678"));
    CHECK(IsValidPhoneNumber("10000000000"));

    CHECK_FALSE(IsValidPhoneNumber(""));
    CHECK_FALSE(IsValidPhoneNumber("1380000500"));      //10 位
    CHECK_FALSE(IsValidPhoneNumber("138000050011"));    //12 位
    CHECK_FALSE(IsValidPhoneNumber("23800005001"));     //不以 1 开头
    CHECK_FALSE(IsValidPhoneNumber("138 0000 500"));    //含空格
    CHECK_FALSE(IsValidPhoneNumber("138-0000-500"));    //含横线
    CHECK_FALSE(IsValidPhoneNumber("1380000500a"));     //含字母
    CHECK_FALSE(IsValidPhoneNumber("+8613800005"));     //含加号
    CHECK_FALSE(IsValidPhoneNumber("EMP48203917"));     //账号不是手机号
}
