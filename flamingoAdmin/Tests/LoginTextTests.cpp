/**
 *  flamingoAdmin —— 登录页文字的单元测试（LGN 系列）。
 *
 *  登录页的版本行原先写死「v2.0.0 · 内部构建 2026.05.18」；本机第一次运行时账号密码框预填
 *  测试库的账号与口令。本文件覆盖改动后的纯函数（Util/LoginText.h）：
 *    - LGN2：版本行为「v版本号 · 构建 年.月.日」，编译日期按编译器 __DATE__ 的格式解析；
 *    - LGN3：没有保存过账号时账号与密码都为空；保存过时按「记住密码」决定是否回填密码。
 *  （LGN1 原为「加密说明按是否启用 TLS 给出两种文字」，2026-10-04 按用户要求加密说明恢复为
 *  原来的固定文字，该用例随之删除。）
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/LoginText.h"
#include "Util/AppConfig.h"

namespace flamingoadmin { namespace test {

namespace {

// LGN2：版本行与编译日期的解析。
void TestVersionLine()
{
    BeginCase("LGN2 版本行为「v版本号 · 构建 年.月.日」，编译日期按 __DATE__ 格式解析");

    // __DATE__ 的格式是「Mmm dd yyyy」，日期不足两位时前面补空格。
    FA_EXPECT_STREQ(logintext::FormatBuildDate("Oct  4 2026"), _T("2026.10.04"));
    FA_EXPECT_STREQ(logintext::FormatBuildDate("Jan 31 2027"), _T("2027.01.31"));
    FA_EXPECT_STREQ(logintext::FormatBuildDate("Dec 25 2026"), _T("2026.12.25"));
    // 格式不对（月份缩写未知、长度不足、空指针）时返回空串，由调用方省略日期部分。
    FA_EXPECT_TRUE(logintext::FormatBuildDate("Foo  4 2026").IsEmpty());
    FA_EXPECT_TRUE(logintext::FormatBuildDate("Oct").IsEmpty());
    FA_EXPECT_TRUE(logintext::FormatBuildDate(nullptr).IsEmpty());

    FA_EXPECT_STREQ(logintext::MakeVersionLine(_T("2.0.0"), _T("2026.10.04")),
                    _T("v2.0.0 · 构建 2026.10.04"));
    FA_EXPECT_STREQ(logintext::MakeVersionLine(_T("2.0.0"), CString()), _T("v2.0.0"));

    // 实际显示的版本行用本次编译的日期：以「v」+ 版本号开头，带「 · 构建 」。
    const CString line = logintext::VersionLine();
    FA_EXPECT_EQ(line.Find(CString(_T("v")) + logintext::kAdminVersion), 0);
    FA_EXPECT_TRUE(line.Find(_T(" · 构建 ")) > 0);
}

// LGN3：登录框回填。
void TestPrefill()
{
    BeginCase("LGN3 没有保存过账号时账号与密码都为空，保存过时按「记住密码」回填");

    CString account;
    CString password;

    // 本机从未登录过：两个框都留空，不再预填测试账号。
    LoginConfig empty;
    logintext::ResolvePrefill(empty, account, password);
    FA_EXPECT_TRUE(account.IsEmpty());
    FA_EXPECT_TRUE(password.IsEmpty());

    // 记住了密码：账号与密码都回填。
    LoginConfig remembered;
    remembered.m_account          = _T("zhang@corp.com");
    remembered.m_password         = _T("secret");
    remembered.m_rememberPassword = true;
    logintext::ResolvePrefill(remembered, account, password);
    FA_EXPECT_STREQ(account, _T("zhang@corp.com"));
    FA_EXPECT_STREQ(password, _T("secret"));

    // 没勾记住密码：只回填账号（即便内存里残留了口令也不回填）。
    LoginConfig accountOnly = remembered;
    accountOnly.m_rememberPassword = false;
    logintext::ResolvePrefill(accountOnly, account, password);
    FA_EXPECT_STREQ(account, _T("zhang@corp.com"));
    FA_EXPECT_TRUE(password.IsEmpty());
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllLoginTextTests()
{
    TestVersionLine();
    TestPrefill();
}

}} // namespace flamingoadmin::test
