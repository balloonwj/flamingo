/**
 *  flamingoAdmin —— 修改登录密码后同步「记住密码」口令的单元测试（RPW 系列）。
 *
 *  平台设置页修改登录密码成功后，ini 里「记住密码」保存的仍是旧口令，下次打开控制台会
 *  预填旧密码、登录失败。修复后由 AppConfig::ApplyPasswordChange 算出应保存的登录配置：
 *    - RPW1：保存的账号就是当前账号且勾了记住密码：口令换成新密码；
 *    - RPW2：没勾记住密码：不保存口令（仍为空）；
 *    - RPW3：保存的是别的账号：原样不动。
 *
 *  全部为纯函数测试，不读写 ini。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AppConfig.h"

namespace flamingoadmin { namespace test {

namespace {

// 构造一份保存的登录配置。
//   account：账号；password：口令明文；remember：是否勾了记住密码。
LoginConfig MakeSaved(LPCTSTR account, LPCTSTR password, bool remember)
{
    LoginConfig cfg;
    cfg.m_account          = account;
    cfg.m_password         = password;
    cfg.m_rememberPassword = remember;
    return cfg;
}

// RPW1：同一账号、勾了记住密码。
void TestSameAccountRemembered()
{
    BeginCase("RPW1 同一账号且勾了记住密码：保存的口令换成新密码");

    const LoginConfig saved = MakeSaved(_T("admin@flamingo.com"), _T("old-pw"), true);
    const LoginConfig out = AppConfig::ApplyPasswordChange(saved, _T("admin@flamingo.com"),
                                                           _T("new-pw"));
    FA_EXPECT_STREQ(out.m_password, _T("new-pw"));
    FA_EXPECT_TRUE(out.m_rememberPassword);
    FA_EXPECT_STREQ(out.m_account, _T("admin@flamingo.com"));

    // 账号大小写不同也视为同一账号（登录名不区分大小写）。
    const LoginConfig mixed = AppConfig::ApplyPasswordChange(saved, _T("Admin@Flamingo.COM"),
                                                             _T("new-pw"));
    FA_EXPECT_STREQ(mixed.m_password, _T("new-pw"));
}

// RPW2：没勾记住密码。
void TestNotRemembered()
{
    BeginCase("RPW2 没勾记住密码：不保存口令");

    const LoginConfig saved = MakeSaved(_T("admin@flamingo.com"), _T(""), false);
    const LoginConfig out = AppConfig::ApplyPasswordChange(saved, _T("admin@flamingo.com"),
                                                           _T("new-pw"));
    FA_EXPECT_TRUE(out.m_password.IsEmpty());
    FA_EXPECT_FALSE(out.m_rememberPassword);
}

// RPW3：保存的是别的账号。
void TestOtherAccount()
{
    BeginCase("RPW3 保存的是别的账号：原样不动");

    const LoginConfig saved = MakeSaved(_T("other@flamingo.com"), _T("other-pw"), true);
    const LoginConfig out = AppConfig::ApplyPasswordChange(saved, _T("admin@flamingo.com"),
                                                           _T("new-pw"));
    FA_EXPECT_STREQ(out.m_password, _T("other-pw"));
    FA_EXPECT_STREQ(out.m_account, _T("other@flamingo.com"));
    FA_EXPECT_TRUE(out.m_rememberPassword);
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllRememberPasswordTests()
{
    TestSameAccountRemembered();
    TestNotRemembered();
    TestOtherAccount();
}

}} // namespace flamingoadmin::test
