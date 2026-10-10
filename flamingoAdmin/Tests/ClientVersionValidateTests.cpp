/**
 *  flamingoAdmin —— 客户端版本「最低版本」校验的单元测试（CVV 系列）。
 *
 *  服务端比较版本号时逐段取数字、忽略非数字字符（LoginProtocol::isVersionAtLeast），
 *  因此最低版本写成「31」「3.1.o」都不会报错，却会让门槛高得离谱或悄悄变样；最低版本
 *  高于本版本时，装了这个版本的客户端也登录不了。控制台在提交前校验：
 *    - CVV1：版本号格式（2～4 段十进制数字，点分隔，每段 1～5 位）；
 *    - CVV2：最低版本不得高于版本号，等于时通过；比较规则与服务端一致（缺的段按 0）；
 *    - CVV3：最低版本留空时通过（表示不另设门槛），格式不对时给出中文提示。
 *
 *  全部为纯函数测试。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// CVV1：版本号格式。
void TestVersionTextFormat()
{
    BeginCase("CVV1 版本号格式：2～4 段数字、点分隔、每段 1～5 位");

    FA_EXPECT_TRUE(IsValidVersionText(_T("3.1.0")));
    FA_EXPECT_TRUE(IsValidVersionText(_T("2.0.0.1234")));
    FA_EXPECT_TRUE(IsValidVersionText(_T("3.1")));
    FA_EXPECT_TRUE(IsValidVersionText(_T("10.65535.0")));

    FA_EXPECT_FALSE(IsValidVersionText(_T("")));
    FA_EXPECT_FALSE(IsValidVersionText(_T("31")));            // 只有一段
    FA_EXPECT_FALSE(IsValidVersionText(_T("3.1.o")));         // 字母 o 不是数字 0
    FA_EXPECT_FALSE(IsValidVersionText(_T("3..1")));          // 空段
    FA_EXPECT_FALSE(IsValidVersionText(_T(".3.1")));          // 开头空段
    FA_EXPECT_FALSE(IsValidVersionText(_T("3.1.")));          // 结尾空段
    FA_EXPECT_FALSE(IsValidVersionText(_T("v3.1")));          // 带前缀
    FA_EXPECT_FALSE(IsValidVersionText(_T("3.1.0.0.0")));     // 五段
    FA_EXPECT_FALSE(IsValidVersionText(_T("3.123456.0")));    // 某段超过 5 位
    FA_EXPECT_FALSE(IsValidVersionText(_T("3. 1.0")));        // 段内有空白
    FA_EXPECT_FALSE(IsValidVersionText(_T("3．1．0")));       // 全角句点
}

// CVV2：最低版本与版本号的大小关系。
void TestMinVersionNotAboveVersion()
{
    BeginCase("CVV2 最低版本不得高于版本号，等于时通过");

    FA_EXPECT_TRUE(CompareVersionText(_T("3.1.0"), _T("3.1.0")) == 0);
    FA_EXPECT_TRUE(CompareVersionText(_T("3.1"), _T("3.1.0")) == 0);      // 缺的段按 0
    FA_EXPECT_TRUE(CompareVersionText(_T("3.10.0"), _T("3.9.9")) > 0);    // 按数值比，不按字符串
    FA_EXPECT_TRUE(CompareVersionText(_T("2.9"), _T("3.0.0")) < 0);

    FA_EXPECT_TRUE(CheckMinVersion(_T("3.1.0"), _T("3.3.0")).IsEmpty());
    FA_EXPECT_TRUE(CheckMinVersion(_T("3.3.0"), _T("3.3.0")).IsEmpty());
    FA_EXPECT_TRUE(CheckMinVersion(_T("3.3"), _T("3.3.0")).IsEmpty());
    FA_EXPECT_FALSE(CheckMinVersion(_T("3.4.0"), _T("3.3.0")).IsEmpty());
    FA_EXPECT_FALSE(CheckMinVersion(_T("3.10.0"), _T("3.9.0")).IsEmpty());
}

// CVV3：留空通过；格式不对时有提示。
void TestMinVersionEmptyAndMalformed()
{
    BeginCase("CVV3 最低版本留空时通过，格式不对时给出提示");

    FA_EXPECT_TRUE(CheckMinVersion(_T(""), _T("3.3.0")).IsEmpty());
    FA_EXPECT_TRUE(CheckMinVersion(_T("   "), _T("3.3.0")).IsEmpty());   // 只有空白按留空处理
    FA_EXPECT_TRUE(CheckMinVersion(_T(" 3.1.0 "), _T("3.3.0")).IsEmpty()); // 首尾空白不计

    const CString malformed = CheckMinVersion(_T("3.1.o"), _T("3.3.0"));
    FA_EXPECT_FALSE(malformed.IsEmpty());
    FA_EXPECT_TRUE(malformed.Find(_T("最低版本")) >= 0);

    // 版本号本身不合法时无法比较：只校验最低版本的格式，不因版本号报错（版本号由
    // 新建弹窗另行校验）。
    FA_EXPECT_TRUE(CheckMinVersion(_T("3.1.0"), _T("")).IsEmpty());
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllClientVersionValidateTests()
{
    TestVersionTextFormat();
    TestMinVersionNotAboveVersion();
    TestMinVersionEmptyAndMalformed();
}

}} // namespace flamingoadmin::test
