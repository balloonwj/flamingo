/**
 *  flamingoAdmin —— 本地口令保护（DPAPI）单元测试。
 *
 *  背景：管理控制台的"记住密码"要把口令留在 exe 同目录的 flamingoAdmin.ini 里。
 *  ini 是明文文本文件，故口令先经 DPAPI 加密、再 Base64 成一行 ASCII 存入。
 *  本组用例覆盖往返一致性、异常输入的安全失败，以及"密文不得含明文 / 换行"这两条
 *  与落盘格式直接相关的约束。
 *
 *  注意这些用例真的会调用 DPAPI（密钥由当前 Windows 账户派生），因此只能在
 *  Windows 上跑，且加解密须在同一个用户会话内完成 —— 这正是生产环境的用法。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/DpapiSecret.h"

namespace flamingoadmin { namespace test {

// 依次运行本地口令保护的全部单元测试用例。
void RunAllDpapiSecretTests()
{
    // ---- 组 1：往返一致性 ----
    BeginCase("ProtectSecret / UnprotectSecret：加密再解密得回原文");
    {
        const CString plain = _T("P@ssw0rd!2026");

        CString cipher;
        FA_EXPECT_TRUE(ProtectSecret(plain, cipher));
        FA_EXPECT_FALSE(cipher.IsEmpty());

        CString back;
        FA_EXPECT_TRUE(UnprotectSecret(cipher, back));
        FA_EXPECT_STREQ(back, plain);
    }

    // 内部固定按 UTF-8 编码，不受系统区域设置影响，故中文与重音字符也要能原样还原。
    BeginCase("含中文与特殊字符的口令同样原样还原");
    {
        const CString plain = _T("口令一二三 #$%&*()_+ eu");

        CString cipher;
        FA_EXPECT_TRUE(ProtectSecret(plain, cipher));

        CString back;
        FA_EXPECT_TRUE(UnprotectSecret(cipher, back));
        FA_EXPECT_STREQ(back, plain);
    }

    // ---- 组 2：空值不算失败 ----
    // 对应"用户没勾记住密码"与"把密码清空了"这两种正常情形。
    BeginCase("空明文 / 空密文：返回成功且输出为空，不算失败");
    {
        CString cipher;
        FA_EXPECT_TRUE(ProtectSecret(CString(), cipher));
        FA_EXPECT_TRUE(cipher.IsEmpty());

        CString plain;
        FA_EXPECT_TRUE(UnprotectSecret(CString(), plain));
        FA_EXPECT_TRUE(plain.IsEmpty());
    }

    // ---- 组 3：落盘格式约束 ----
    // 密文里出现换行，写进 ini 就是断的（一个键只能占一行），读回来必然截断。
    BeginCase("密文是单行：不含回车与换行");
    {
        CString cipher;
        FA_EXPECT_TRUE(ProtectSecret(_T("some-long-password-0123456789-0123456789"), cipher));
        FA_EXPECT_TRUE(cipher.Find(_T('\r')) < 0);
        FA_EXPECT_TRUE(cipher.Find(_T('\n')) < 0);
    }

    // 这条防的是"哪天有人把加密悄悄换成了编码"。
    BeginCase("密文中不得直接出现明文口令");
    {
        const CString plain = _T("VerySecretValue12345");

        CString cipher;
        FA_EXPECT_TRUE(ProtectSecret(plain, cipher));
        FA_EXPECT_TRUE(cipher.Find(plain) < 0);
    }

    // ---- 组 4：异常输入安全失败 ----
    BeginCase("非法 Base64：解密失败且出参被清空");
    {
        CString plain(_T("留下的脏数据"));
        FA_EXPECT_FALSE(UnprotectSecret(_T("这不是合法的 Base64!!!"), plain));
        FA_EXPECT_TRUE(plain.IsEmpty());
    }

    // 密文被截断，典型成因是读 ini 的缓冲不够被切掉一半。
    BeginCase("截断的密文：解密失败且出参被清空");
    {
        CString cipher;
        FA_EXPECT_TRUE(ProtectSecret(_T("password-to-truncate"), cipher));
        FA_EXPECT_TRUE(cipher.GetLength() > 20);

        CString plain;
        FA_EXPECT_FALSE(UnprotectSecret(cipher.Left(cipher.GetLength() / 2), plain));
        FA_EXPECT_TRUE(plain.IsEmpty());
    }

    // Base64 仍合法、内容被改：靠 DPAPI 自身的完整性校验拦下。
    BeginCase("被篡改的密文：解密失败且出参被清空");
    {
        CString cipher;
        FA_EXPECT_TRUE(ProtectSecret(_T("password-to-tamper"), cipher));

        CString tampered = cipher;
        const int pos = tampered.GetLength() / 2;
        tampered.SetAt(pos, tampered[pos] == _T('A') ? _T('B') : _T('A'));

        CString plain;
        FA_EXPECT_FALSE(UnprotectSecret(tampered, plain));
        FA_EXPECT_TRUE(plain.IsEmpty());
    }

    // ---- 组 5：同一口令两次加密的密文不同 ----
    // 不是安全强度要求，而是防止实现被换成"确定性编码"而不自知。
    BeginCase("同一口令两次加密：密文不同，但都能解回同一明文");
    {
        const CString plain = _T("same-password-twice");

        CString first;
        CString second;
        FA_EXPECT_TRUE(ProtectSecret(plain, first));
        FA_EXPECT_TRUE(ProtectSecret(plain, second));
        FA_EXPECT_FALSE(first == second);

        CString backFirst;
        CString backSecond;
        FA_EXPECT_TRUE(UnprotectSecret(first, backFirst));
        FA_EXPECT_TRUE(UnprotectSecret(second, backSecond));
        FA_EXPECT_STREQ(backFirst, plain);
        FA_EXPECT_STREQ(backSecond, plain);
    }
}

}} // namespace flamingoadmin::test
