/**
 *  LoginAccountListTests.cpp
 *
 *  本机历史登录账号列表（CLoginAccountList）的读写测试（Suite LAL）。
 *
 *  覆盖：内存增删查、保存/加载往返、旧版账号文件（无姓名字段）的兼容读取、
 *  最后登录账号与自动登录标志，以及头像尾块（2026-10-04 起，LAL-AV1～AV5）、规范账号尾块与
 *  用手机号登录时的列表键（2026-10-06 起，LAL-AN1～AN8）。用临时目录下的 .dat 文件跑真实磁盘 I/O，
 *  框架 Catch2 v3。
 *
 *  兼容性用例不复用产品结构体，而是自带一份"旧版磁盘布局"的定义与异或
 *  写入逻辑 —— 这样产品结构体日后再扩展字段时，本文件仍然在校验"能不能
 *  读老用户的 LoginAccountList.dat"，而不会跟着一起变形失去意义。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "catch_amalgamated.hpp"

#include "../Session/LoginAccountList.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>   //strcmp / strncpy_s：头像内容是 UTF-8 窄字符串
#include <string>

namespace {

// 账号列表文件的异或密钥 —— 与 CLoginAccountList::Encrypt / Decrypt 同值
// （见 LoginAccountList.cpp）。测试自带一份，不依赖产品代码的私有实现。
const unsigned char kXorKey = 0x88;

// 旧版（加姓名字段之前）的磁盘记录布局。老用户机器上已存在的
// LoginAccountList.dat 就是按这个布局逐条写下去的，故本结构体<u>不得</u>
// 随产品的 LOGIN_ACCOUNT_INFO 一起演进。
struct LoginAccountRecordV1
{
    TCHAR szUser[32];       // 账号名
    TCHAR szPwd[32];        // 密码（不记住密码时为空）
    int   nStatus;          // 登录状态
    BOOL  bRememberPwd;     // 记住密码
    BOOL  bAutoLogin;       // 自动登录
};

// 取临时目录下的一个 .dat 全路径，并先删掉同名旧文件，保证每个用例从空文件开始。
//   name：文件名，各用例传不同名，避免跨用例互相干扰。
std::wstring FreshTempDat(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    //文件名里带上进程号：两份测试进程同时跑（命令行一份 + VS 里一份）时，
    //固定文件名会让它们共用同一个文件，先起的占着、后起的打开就失败，
    //表现为看似偶发、复跑又好了的失败。同 HistoryModelTests::FreshTempDb。
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    return path;
}

// 按账号列表文件的加密方式（整块异或 kXorKey）写一段数据。
//   fp： 已打开的二进制写文件句柄，不可为 NULL；
//   buf：待写数据；len：字节数。
void WriteEncrypted(FILE* fp, const void* buf, size_t len)
{
    std::string tmp((const char*)buf, len);
    for (size_t i = 0; i < tmp.size(); ++i)
    {
        tmp[i] = (char)(((unsigned char)tmp[i]) ^ kXorKey);
    }
    fwrite(tmp.data(), 1, tmp.size(), fp);
}

// 按账号列表文件的加密方式读一段数据（读出后整块异或还原）。
//   fp： 已打开的二进制读文件句柄，不可为 NULL；
//   buf：接收缓冲区；len：字节数。
// 返回：读满 len 个字节返回 TRUE，否则 FALSE。
BOOL ReadDecrypted(FILE* fp, void* buf, size_t len)
{
    if (fread(buf, 1, len, fp) != len)
    {
        return FALSE;
    }
    unsigned char* p = (unsigned char*)buf;
    for (size_t i = 0; i < len; ++i)
    {
        p[i] = (unsigned char)(p[i] ^ kXorKey);
    }
    return TRUE;
}

// 姓名尾块的起始魔数与版本号 —— 与 LoginAccountList.cpp 里的同名常量同值。
// 测试自带一份，理由同 kXorKey：产品端改布局时，本文件仍在校验旧布局。
const int kNickSectionMagic = 0x464C4131;
const int kNickSectionVersion = 1;

// 在手工拼出来的账号文件末尾追加姓名尾块。
//   fp：      已打开的二进制写文件句柄，写入位置须停在主体记录之后；
//   nicks：   逐条姓名，顺序与主体记录一一对应；
//   nCount：  姓名条数，须与主体里的记录条数一致。
void WriteNickTrailer(FILE* fp, const wchar_t* const* nicks, int nCount)
{
    WriteEncrypted(fp, &kNickSectionMagic, sizeof(kNickSectionMagic));
    WriteEncrypted(fp, &kNickSectionVersion, sizeof(kNickSectionVersion));
    WriteEncrypted(fp, &nCount, sizeof(nCount));

    for (int i = 0; i < nCount; ++i)
    {
        TCHAR szNickName[kLoginAccountNickNameLen] = { 0 };
        wcsncpy_s(szNickName, _countof(szNickName), nicks[i], _TRUNCATE);
        WriteEncrypted(fp, szNickName, sizeof(szNickName));
    }
}

// 头像尾块的起始魔数、版本号与头像内容的定长字节数 —— 与 LoginAccountList.cpp / .h 里的
// 同名常量同值（2026-10-04 新增）。测试自带一份，理由同 kXorKey。
const int kAvatarSectionMagic = 0x464C4132;
const int kAvatarSectionVersion = 1;
const int kAvatarCustomFaceBytes = 64;

// 在手工拼出来的账号文件末尾追加头像尾块（须写在姓名尾块之后）。
//   fp：          已打开的二进制写文件句柄，写入位置须停在姓名尾块之后；
//   userIds：     逐条 userid，顺序与主体记录一一对应；
//   faceTypes：   逐条头像类型；
//   customFaces： 逐条头像内容（UTF-8）；
//   nCount：      条数，须与主体里的记录条数一致。
void WriteAvatarTrailer(FILE* fp, const long long* userIds, const int* faceTypes,
                        const char* const* customFaces, int nCount)
{
    WriteEncrypted(fp, &kAvatarSectionMagic, sizeof(kAvatarSectionMagic));
    WriteEncrypted(fp, &kAvatarSectionVersion, sizeof(kAvatarSectionVersion));
    WriteEncrypted(fp, &nCount, sizeof(nCount));

    for (int i = 0; i < nCount; ++i)
    {
        char szCustomFace[kAvatarCustomFaceBytes] = { 0 };
        strncpy_s(szCustomFace, _countof(szCustomFace), customFaces[i], _TRUNCATE);
        WriteEncrypted(fp, &userIds[i], sizeof(userIds[i]));
        WriteEncrypted(fp, &faceTypes[i], sizeof(faceTypes[i]));
        WriteEncrypted(fp, szCustomFace, sizeof(szCustomFace));
    }
}

// 填一条旧版磁盘记录，铺平兼容性用例的样板代码。
LoginAccountRecordV1 MakeV1Record(const wchar_t* user, const wchar_t* pwd,
                                  int status, BOOL rememberPwd, BOOL autoLogin)
{
    LoginAccountRecordV1 rec;
    memset(&rec, 0, sizeof(rec));
    wcsncpy_s(rec.szUser, _countof(rec.szUser), user, _TRUNCATE);
    wcsncpy_s(rec.szPwd, _countof(rec.szPwd), pwd, _TRUNCATE);
    rec.nStatus = status;
    rec.bRememberPwd = rememberPwd;
    rec.bAutoLogin = autoLogin;
    return rec;
}

} // namespace

TEST_CASE("LAL1 新增账号后可按账号名查回，重复账号被拒")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, TRUE));
    CHECK(list.GetCount() == 2);

    LOGIN_ACCOUNT_INFO* lpFound = list.Find(_T("bob"));
    REQUIRE(lpFound != NULL);
    CHECK(wcscmp(lpFound->szUser, _T("bob")) == 0);
    CHECK(wcscmp(lpFound->szPwd, _T("pwd_b")) == 0);
    CHECK(lpFound->bAutoLogin == TRUE);

    // 同名账号再添加一次应被拒绝，且不产生重复条目。
    CHECK(list.Add(_T("bob"), _T("other"), 1, TRUE, TRUE) == FALSE);
    CHECK(list.GetCount() == 2);

    CHECK(list.Find(_T("nobody")) == NULL);
}

TEST_CASE("LAL2 保存后重新加载，账号数据与最后登录账号完好")
{
    std::wstring path = FreshTempDat(L"flamingo_lal2.dat");

    {
        CLoginAccountList list;
        REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
        REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 2, TRUE, TRUE));
        list.SetLastLoginUser(_T("bob"));
        REQUIRE(list.SaveFile(path.c_str()));
    }

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    REQUIRE(loaded.GetCount() == 2);

    LOGIN_ACCOUNT_INFO* lpAlice = loaded.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szPwd, _T("pwd_a")) == 0);
    CHECK(lpAlice->nStatus == 1);
    CHECK(lpAlice->bRememberPwd == TRUE);
    CHECK(lpAlice->bAutoLogin == FALSE);

    LOGIN_ACCOUNT_INFO* lpLast = loaded.GetLastLoginAccountInfo();
    REQUIRE(lpLast != NULL);
    CHECK(wcscmp(lpLast->szUser, _T("bob")) == 0);
    CHECK(loaded.IsAutoLogin() == TRUE);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL3 旧版账号文件能被完整读出，记录不错位")
{
    std::wstring path = FreshTempDat(L"flamingo_lal3.dat");

    // 手工拼一个旧版文件：记录数、最后登录索引，之后逐条旧版记录，
    // 每次写入整块异或 —— 与老版本 SaveFile 的落盘方式一致。
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);

    int nCount = 3;
    int nLastLoginUser = 2;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));

    LoginAccountRecordV1 rec0 = MakeV1Record(L"old_user0", L"old_pwd0", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"old_user1", L"", 1, FALSE, FALSE);
    LoginAccountRecordV1 rec2 = MakeV1Record(L"old_user2", L"old_pwd2", 3, TRUE, TRUE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    WriteEncrypted(fp, &rec2, sizeof(rec2));
    fclose(fp);

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    REQUIRE(loaded.GetCount() == 3);

    // 逐条核对 —— 任何一处错位都会让账号名读成乱码。
    LOGIN_ACCOUNT_INFO* lp0 = loaded.GetAccountInfo(0);
    REQUIRE(lp0 != NULL);
    CHECK(wcscmp(lp0->szUser, L"old_user0") == 0);
    CHECK(wcscmp(lp0->szPwd, L"old_pwd0") == 0);
    CHECK(lp0->bRememberPwd == TRUE);

    LOGIN_ACCOUNT_INFO* lp1 = loaded.GetAccountInfo(1);
    REQUIRE(lp1 != NULL);
    CHECK(wcscmp(lp1->szUser, L"old_user1") == 0);
    CHECK(lp1->bRememberPwd == FALSE);

    LOGIN_ACCOUNT_INFO* lp2 = loaded.GetAccountInfo(2);
    REQUIRE(lp2 != NULL);
    CHECK(wcscmp(lp2->szUser, L"old_user2") == 0);
    CHECK(lp2->nStatus == 3);
    CHECK(lp2->bAutoLogin == TRUE);

    // 最后登录账号索引也要跟着还原。
    CHECK(loaded.GetLastLoginUser() == 2);
    LOGIN_ACCOUNT_INFO* lpLast = loaded.GetLastLoginAccountInfo();
    REQUIRE(lpLast != NULL);
    CHECK(wcscmp(lpLast->szUser, L"old_user2") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL5 姓名随账号一起落盘并能读回")
{
    std::wstring path = FreshTempDat(L"flamingo_lal5.dat");

    {
        CLoginAccountList list;
        REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
        REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
        // 登录成功后写入姓名。
        REQUIRE(list.SetNickName(_T("alice"), L"张三") == TRUE);
        REQUIRE(list.SetNickName(_T("bob"), L"李四") == TRUE);
        REQUIRE(list.SaveFile(path.c_str()));
    }

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    REQUIRE(loaded.GetCount() == 2);

    LOGIN_ACCOUNT_INFO* lpAlice = loaded.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);
    // 姓名不能串到别的账号上。
    LOGIN_ACCOUNT_INFO* lpBob = loaded.Find(_T("bob"));
    REQUIRE(lpBob != NULL);
    CHECK(wcscmp(lpBob->szNickName, L"李四") == 0);
    CHECK(wcscmp(lpBob->szPwd, _T("pwd_b")) == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL6 旧版文件读入后姓名为空，保存即升级为新版且姓名保留")
{
    std::wstring path = FreshTempDat(L"flamingo_lal6.dat");

    // 先造一个旧版文件（老用户机器上的状态）。
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);
    int nCount = 2;
    int nLastLoginUser = 1;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));
    LoginAccountRecordV1 rec0 = MakeV1Record(L"old_user0", L"old_pwd0", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"old_user1", L"old_pwd1", 1, TRUE, TRUE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    fclose(fp);

    // 读入：账号信息完好，姓名为空（旧版根本没存过）。
    CLoginAccountList list;
    REQUIRE(list.LoadFile(path.c_str()));
    REQUIRE(list.GetCount() == 2);
    LOGIN_ACCOUNT_INFO* lpOld0 = list.Find(L"old_user0");
    REQUIRE(lpOld0 != NULL);
    CHECK(wcslen(lpOld0->szNickName) == 0);

    // 此后登录成功写入姓名并保存 —— 文件升级成新版。
    REQUIRE(list.SetNickName(L"old_user0", L"王五") == TRUE);
    REQUIRE(list.SaveFile(path.c_str()));

    // 存下姓名之后，文件的第一个字段<u>仍然</u>是账号条数 —— 姓名是追加在
    // 文件末尾的，主体格式没变，老版本客户端读它照旧正常。
    FILE* fpCheck = _wfopen(path.c_str(), L"rb");
    REQUIRE(fpCheck != NULL);
    int nFirstField = 0;
    REQUIRE(fread(&nFirstField, sizeof(nFirstField), 1, fpCheck) == 1);
    fclose(fpCheck);
    unsigned char* pFirstByte = (unsigned char*)&nFirstField;
    for (size_t i = 0; i < sizeof(nFirstField); ++i)
    {
        pFirstByte[i] = (unsigned char)(pFirstByte[i] ^ kXorKey);
    }
    CHECK(nFirstField == 2);

    // 再读回：姓名与其余字段都在，最后登录索引也没丢。
    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    REQUIRE(reloaded.GetCount() == 2);
    LOGIN_ACCOUNT_INFO* lpUpgraded = reloaded.Find(L"old_user0");
    REQUIRE(lpUpgraded != NULL);
    CHECK(wcscmp(lpUpgraded->szNickName, L"王五") == 0);
    CHECK(wcscmp(lpUpgraded->szPwd, L"old_pwd0") == 0);
    LOGIN_ACCOUNT_INFO* lpOther = reloaded.Find(L"old_user1");
    REQUIRE(lpOther != NULL);
    CHECK(wcslen(lpOther->szNickName) == 0);
    CHECK(reloaded.GetLastLoginUser() == 1);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL7 SetNickName 只认已存在的账号，传空指针即清空姓名")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));

    // 账号不在列表里 —— 拒绝，不新增条目。
    CHECK(list.SetNickName(_T("nobody"), L"赵六") == FALSE);
    CHECK(list.GetCount() == 1);
    CHECK(list.SetNickName(NULL, L"赵六") == FALSE);

    REQUIRE(list.SetNickName(_T("alice"), L"张三") == TRUE);
    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);

    // 服务器没给昵称时清空 —— 登录窗据此回退显示账号。
    REQUIRE(list.SetNickName(_T("alice"), NULL) == TRUE);
    CHECK(wcslen(lpAlice->szNickName) == 0);

    // 超长姓名要被截断且保留结束符，不能越界。
    std::wstring longName(kLoginAccountNickNameLen + 20, L'x');
    REQUIRE(list.SetNickName(_T("alice"), longName.c_str()) == TRUE);
    CHECK(wcslen(lpAlice->szNickName) == (size_t)(kLoginAccountNickNameLen - 1));
}

TEST_CASE("LAL9 带姓名的新文件，按老版本客户端的读法照样读得出账号")
{
    std::wstring path = FreshTempDat(L"flamingo_lal9.dat");

    {
        CLoginAccountList list;
        REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
        REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 2, TRUE, TRUE));
        REQUIRE(list.SetNickName(_T("alice"), L"张三") == TRUE);
        REQUIRE(list.SetNickName(_T("bob"), L"李四") == TRUE);
        list.SetLastLoginUser(_T("bob"));
        REQUIRE(list.SaveFile(path.c_str()));
    }

    // 完全照老版本客户端的读法来：读条数、读最后登录索引，再按旧记录布局
    // 逐条读，读完就收工 —— 文件末尾多出来的姓名尾块它根本不去碰。
    // 这一条用例守住的是"用户切回旧版客户端也不会读崩"。
    FILE* fp = _wfopen(path.c_str(), L"rb");
    REQUIRE(fp != NULL);

    int nCount = 0;
    REQUIRE(ReadDecrypted(fp, &nCount, sizeof(nCount)) == TRUE);
    CHECK(nCount == 2);

    int nLastLoginUser = 0;
    REQUIRE(ReadDecrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser)) == TRUE);
    CHECK(nLastLoginUser == 1);

    LoginAccountRecordV1 rec0;
    memset(&rec0, 0, sizeof(rec0));
    REQUIRE(ReadDecrypted(fp, &rec0, sizeof(rec0)) == TRUE);
    CHECK(wcscmp(rec0.szUser, L"alice") == 0);
    CHECK(wcscmp(rec0.szPwd, L"pwd_a") == 0);
    CHECK(rec0.bRememberPwd == TRUE);

    LoginAccountRecordV1 rec1;
    memset(&rec1, 0, sizeof(rec1));
    REQUIRE(ReadDecrypted(fp, &rec1, sizeof(rec1)) == TRUE);
    CHECK(wcscmp(rec1.szUser, L"bob") == 0);
    CHECK(rec1.nStatus == 2);
    CHECK(rec1.bAutoLogin == TRUE);

    fclose(fp);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL8 不记住密码的账号，密码与姓名都是空串")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, FALSE, FALSE));

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcslen(lpAlice->szPwd) == 0);
    CHECK(wcslen(lpAlice->szNickName) == 0);
}

TEST_CASE("LAL4 最后登录账号与自动登录标志随账号切换")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, TRUE));

    // 默认指向第 0 条。
    CHECK(list.GetLastLoginUser() == 0);
    CHECK(list.IsAutoLogin() == FALSE);

    list.SetLastLoginUser(_T("bob"));
    CHECK(list.GetLastLoginUser() == 1);
    CHECK(list.IsAutoLogin() == TRUE);

    // 传入不存在的账号时保持原值不变。
    list.SetLastLoginUser(_T("nobody"));
    CHECK(list.GetLastLoginUser() == 1);

    LOGIN_ACCOUNT_INFO stCopy;
    memset(&stCopy, 0, sizeof(stCopy));
    REQUIRE(list.GetLastLoginAccountInfo(&stCopy) == TRUE);
    CHECK(wcscmp(stCopy.szUser, _T("bob")) == 0);
}

// =====================================================================
// LAL-A —— "取消自动登录"依赖的落盘语义
//
// 会话控制器的 ClearAutoLoginFlag / ForgetSavedPasswordForCurrentAccount 与
// CBalloonLoginDlg::PersistCheckStateForCurrentAccount 都是"改 LOGIN_ACCOUNT_INFO
// 的字段 + SaveFile"这个套路。那三个函数本身挂在窗口类上、测试工程不编译它们，
// 但它们赖以成立的这一层语义能在这里钉死：改完落盘、重新加载后必须还是改后的值。
// 这一层要是悄悄坏了，表现就是"取消了自动登录，下次启动还是自动登进去"。
// =====================================================================

TEST_CASE("LAL-A1 清掉自动登录标志后落盘,重新加载仍为假", "[lal][autologin]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_a1.dat");

    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, TRUE));

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    lpAlice->bAutoLogin = FALSE;
    REQUIRE(list.SaveFile(path.c_str()));

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpReloaded = reloaded.Find(_T("alice"));
    REQUIRE(lpReloaded != NULL);
    CHECK(lpReloaded->bAutoLogin == FALSE);
}

TEST_CASE("LAL-A2 清标志只影响目标账号", "[lal][autologin]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_a2.dat");

    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, TRUE));
    REQUIRE(list.Add(_T("bob"),   _T("pwd_b"), 1, TRUE, TRUE));

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    lpAlice->bAutoLogin = FALSE;
    REQUIRE(list.SaveFile(path.c_str()));

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpReloadedAlice = reloaded.Find(_T("alice"));
    LOGIN_ACCOUNT_INFO* lpReloadedBob   = reloaded.Find(_T("bob"));
    REQUIRE(lpReloadedAlice != NULL);
    REQUIRE(lpReloadedBob != NULL);
    CHECK(lpReloadedAlice->bAutoLogin == FALSE);
    CHECK(lpReloadedBob->bAutoLogin == TRUE);   // 没被殃及
}

TEST_CASE("LAL-A3 账号不在列表里时 Find 返回空,调用方据此跳过", "[lal][autologin]")
{
    // ClearAutoLoginFlag 的防御分支:账号还没在本机成功登录过时什么都不做。
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, TRUE));
    CHECK(list.Find(_T("nobody")) == NULL);
}

TEST_CASE("LAL-A4 清自动登录标志不动记住密码与密码本身", "[lal][autologin]")
{
    // 登出只是"别自动登进去",不该顺手把用户记住的密码也弄没了 —— 下次打开
    // 登录窗密码仍要回填得出来,只是不再自动登录。
    const std::wstring path = FreshTempDat(L"flamingo_lal_a4.dat");

    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, TRUE));

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    lpAlice->bAutoLogin = FALSE;
    REQUIRE(list.SaveFile(path.c_str()));

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpReloaded = reloaded.Find(_T("alice"));
    REQUIRE(lpReloaded != NULL);
    CHECK(lpReloaded->bAutoLogin == FALSE);
    CHECK(lpReloaded->bRememberPwd == TRUE);
    CHECK(wcscmp(lpReloaded->szPwd, _T("pwd_a")) == 0);
}

TEST_CASE("LAL-A5 清掉最后登录账号的标志后 IsAutoLogin 返回假", "[lal][autologin]")
{
    // IsAutoLogin 只看"最后登录账号"那一条(会话控制器的 StartInitialLogin 据它决定要不要
    // 跳过登录窗),故清标志之后必须能从这个入口看出来。
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"),   _T("pwd_b"), 1, TRUE, TRUE));

    list.SetLastLoginUser(_T("bob"));
    REQUIRE(list.IsAutoLogin() == TRUE);

    LOGIN_ACCOUNT_INFO* lpBob = list.Find(_T("bob"));
    REQUIRE(lpBob != NULL);
    lpBob->bAutoLogin = FALSE;
    CHECK(list.IsAutoLogin() == FALSE);
}

TEST_CASE("LAL-A6 密码错误后忘掉密码:密码清空且两个勾选项一并取消", "[lal][autologin]")
{
    // 对应会话控制器的 ForgetSavedPasswordForCurrentAccount。存下的密码已经不对了,
    // 留着只会让勾了自动登录的用户每次开机被自动登一次、再被弹一次"密码错误"。
    const std::wstring path = FreshTempDat(L"flamingo_lal_a6.dat");

    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("stale_pwd"), 1, TRUE, TRUE));

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    lpAlice->szPwd[0]     = _T('\0');
    lpAlice->bRememberPwd = FALSE;
    lpAlice->bAutoLogin   = FALSE;
    REQUIRE(list.SaveFile(path.c_str()));

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpReloaded = reloaded.Find(_T("alice"));
    REQUIRE(lpReloaded != NULL);
    CHECK(wcslen(lpReloaded->szPwd) == 0);
    CHECK(lpReloaded->bRememberPwd == FALSE);
    CHECK(lpReloaded->bAutoLogin == FALSE);
    // 账号本身仍在列表里 —— 只是忘掉密码,不是把这个账号从历史里抹掉。
    CHECK(wcscmp(lpReloaded->szUser, _T("alice")) == 0);
}

TEST_CASE("LAL-A7 取消记住密码时一并清空密码", "[lal][autologin]")
{
    // 对应 CBalloonLoginDlg::PersistCheckStateForCurrentAccount。不清的话文件里
    // 仍留着明文密码,这个"取消记住密码"就名不副实。
    const std::wstring path = FreshTempDat(L"flamingo_lal_a7.dat");

    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, TRUE));

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    // 取消"记住密码"必然连带取消"自动登录"(两个复选框的联动),密码一并清空。
    lpAlice->bRememberPwd = FALSE;
    lpAlice->bAutoLogin   = FALSE;
    lpAlice->szPwd[0]     = _T('\0');
    REQUIRE(list.SaveFile(path.c_str()));

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpReloaded = reloaded.Find(_T("alice"));
    REQUIRE(lpReloaded != NULL);
    CHECK(wcslen(lpReloaded->szPwd) == 0);
    CHECK(lpReloaded->bRememberPwd == FALSE);
    CHECK(lpReloaded->bAutoLogin == FALSE);
}

// =====================================================================
// ALB —— 系统设置里「自动登录」开关对账号列表的修改(ApplyAutoLoginSetting)
//
// 见 docs/自动登录方案.md §4。开关在登录成功之后才能拨动，此时账号必然已在列表里，
// 本次登录用的密码也在会话控制器手里，故"打开"时顺带存下密码、打开记住密码，
// 否则下次启动登录窗里没有密码可填，自动登录无从谈起。
// =====================================================================

TEST_CASE("ALB1 关闭自动登录只清该标志,不动记住密码与已存的密码", "[lal][autologin]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, TRUE));

    CHECK(list.ApplyAutoLoginSetting(_T("alice"), FALSE, _T("ignored")) == TRUE);

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(lpAlice->bAutoLogin == FALSE);
    CHECK(lpAlice->bRememberPwd == TRUE);
    // 关闭时传入的密码被忽略，原先存下的密码保持不变。
    CHECK(wcscmp(lpAlice->szPwd, _T("pwd_a")) == 0);
}

TEST_CASE("ALB2 打开自动登录时一并打开记住密码并存下本次密码", "[lal][autologin]")
{
    // 典型场景：本次登录时没勾"记住密码"，列表里只存了账号、密码为空。
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T(""), 1, FALSE, FALSE));

    CHECK(list.ApplyAutoLoginSetting(_T("alice"), TRUE, _T("pwd_now")) == TRUE);

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(lpAlice->bAutoLogin == TRUE);
    CHECK(lpAlice->bRememberPwd == TRUE);
    CHECK(wcscmp(lpAlice->szPwd, _T("pwd_now")) == 0);

    // 打开时传空密码：不改动已存的密码。
    REQUIRE(list.ApplyAutoLoginSetting(_T("alice"), FALSE, NULL) == TRUE);
    CHECK(list.ApplyAutoLoginSetting(_T("alice"), TRUE, _T("")) == TRUE);
    CHECK(lpAlice->bAutoLogin == TRUE);
    CHECK(wcscmp(lpAlice->szPwd, _T("pwd_now")) == 0);
}

TEST_CASE("ALB3 账号为空或不在列表里时返回失败,列表不变", "[lal][autologin]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));

    CHECK(list.ApplyAutoLoginSetting(_T("nobody"), TRUE, _T("pwd_x")) == FALSE);
    CHECK(list.ApplyAutoLoginSetting(NULL, TRUE, _T("pwd_x")) == FALSE);

    CHECK(list.GetCount() == 1);
    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(lpAlice->bAutoLogin == FALSE);
    CHECK(lpAlice->bRememberPwd == TRUE);
    CHECK(wcscmp(lpAlice->szPwd, _T("pwd_a")) == 0);
}

TEST_CASE("ALB4 打开 / 关闭后落盘,重新加载仍是修改后的值", "[lal][autologin]")
{
    const std::wstring path = FreshTempDat(L"flamingo_alb4.dat");

    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T(""), 1, FALSE, FALSE));
    REQUIRE(list.Add(_T("bob"),   _T("pwd_b"), 1, TRUE, TRUE));

    REQUIRE(list.ApplyAutoLoginSetting(_T("alice"), TRUE, _T("pwd_a")) == TRUE);
    REQUIRE(list.ApplyAutoLoginSetting(_T("bob"), FALSE, NULL) == TRUE);
    REQUIRE(list.SaveFile(path.c_str()));

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpAlice = reloaded.Find(_T("alice"));
    LOGIN_ACCOUNT_INFO* lpBob   = reloaded.Find(_T("bob"));
    REQUIRE(lpAlice != NULL);
    REQUIRE(lpBob != NULL);
    CHECK(lpAlice->bAutoLogin == TRUE);
    CHECK(lpAlice->bRememberPwd == TRUE);
    CHECK(wcscmp(lpAlice->szPwd, _T("pwd_a")) == 0);
    CHECK(lpBob->bAutoLogin == FALSE);
    CHECK(lpBob->bRememberPwd == TRUE);
    CHECK(wcscmp(lpBob->szPwd, _T("pwd_b")) == 0);
}

TEST_CASE("ALB5 关闭最后登录账号的自动登录后 IsAutoLogin 返回假,超长密码被截断", "[lal][autologin]")
{
    // IsAutoLogin 决定下次启动时登录窗是否自动开始登录，关掉开关后必须从这个入口看得出来。
    CLoginAccountList list;
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, TRUE));
    list.SetLastLoginUser(_T("bob"));
    REQUIRE(list.IsAutoLogin() == TRUE);

    REQUIRE(list.ApplyAutoLoginSetting(_T("bob"), FALSE, NULL) == TRUE);
    CHECK(list.IsAutoLogin() == FALSE);

    REQUIRE(list.ApplyAutoLoginSetting(_T("bob"), TRUE, NULL) == TRUE);
    CHECK(list.IsAutoLogin() == TRUE);

    // 密码字段容量有限(见 LOGIN_ACCOUNT_INFO::szPwd，含结束符)，超长时截断且必须以 NUL 结尾。
    const size_t kPwdCapacity = sizeof(LOGIN_ACCOUNT_INFO::szPwd) / sizeof(TCHAR);
    const std::wstring longPwd(kPwdCapacity * 2, L'x');
    REQUIRE(list.ApplyAutoLoginSetting(_T("bob"), TRUE, longPwd.c_str()) == TRUE);
    LOGIN_ACCOUNT_INFO* lpBob = list.Find(_T("bob"));
    REQUIRE(lpBob != NULL);
    CHECK(wcslen(lpBob->szPwd) == kPwdCapacity - 1);
}

// =====================================================================
// RLS —— 登录成功后记入账号列表(RecordLoginSuccess)不得保存未要求记住的密码
//
// 原先会话控制器在登录成功时把登录窗采集的整条信息 memcpy 进列表，没勾"记住密码"时
// 密码也照样落盘(2026-09-30 修复)。登录窗只在勾了"记住密码"时才回填密码，界面上
// 看不出问题，只有这组用例能发现密码被写进了文件。
// =====================================================================

namespace {

// 按登录窗采集的样子填一条本次登录的账号信息，铺平 RLS 用例的样板代码。
LOGIN_ACCOUNT_INFO MakeLoginInfo(const wchar_t* user, const wchar_t* pwd,
                                 int status, BOOL rememberPwd, BOOL autoLogin)
{
    LOGIN_ACCOUNT_INFO info;
    memset(&info, 0, sizeof(info));
    wcsncpy_s(info.szUser, _countof(info.szUser), user, _TRUNCATE);
    wcsncpy_s(info.szPwd, _countof(info.szPwd), pwd, _TRUNCATE);
    info.nStatus = status;
    info.bRememberPwd = rememberPwd;
    info.bAutoLogin = autoLogin;
    return info;
}

} // namespace

TEST_CASE("RLS1 已有账号这次没勾记住密码:列表里的密码被清空", "[lal][remember]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("old_pwd"), 1, TRUE, FALSE));

    const LOGIN_ACCOUNT_INFO info = MakeLoginInfo(L"alice", L"typed_pwd", 1, FALSE, FALSE);
    CHECK(list.RecordLoginSuccess(&info) == TRUE);

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(lpAlice->bRememberPwd == FALSE);
    CHECK(wcslen(lpAlice->szPwd) == 0);
    // 传入的信息只读：会话控制器之后还要用这份密码(系统设置里打开自动登录时存下它)。
    CHECK(wcscmp(info.szPwd, L"typed_pwd") == 0);
}

TEST_CASE("RLS2 已有账号这次勾了记住密码:密码更新为本次的密码", "[lal][remember]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("old_pwd"), 1, TRUE, FALSE));

    const LOGIN_ACCOUNT_INFO info = MakeLoginInfo(L"alice", L"new_pwd", 3, TRUE, TRUE);
    CHECK(list.RecordLoginSuccess(&info) == TRUE);

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szPwd, L"new_pwd") == 0);
    CHECK(lpAlice->bRememberPwd == TRUE);
    CHECK(lpAlice->bAutoLogin == TRUE);
    CHECK(lpAlice->nStatus == 3);
    CHECK(list.GetCount() == 1);   // 更新原记录，不新增
}

TEST_CASE("RLS3 新账号没勾记住密码:加入列表且密码为空", "[lal][remember]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));

    const LOGIN_ACCOUNT_INFO info = MakeLoginInfo(L"alice", L"typed_pwd", 1, FALSE, FALSE);
    CHECK(list.RecordLoginSuccess(&info) == TRUE);

    CHECK(list.GetCount() == 2);
    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcslen(lpAlice->szPwd) == 0);
    CHECK(lpAlice->bRememberPwd == FALSE);
}

TEST_CASE("RLS4 更新已有账号时已存的姓名保持不变", "[lal][remember]")
{
    // 登录窗采集的信息里没有姓名，整条覆盖会把已存的姓名清空。
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.SetNickName(_T("alice"), L"张三"));

    const LOGIN_ACCOUNT_INFO info = MakeLoginInfo(L"alice", L"pwd_a", 1, TRUE, FALSE);
    CHECK(list.RecordLoginSuccess(&info) == TRUE);

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);
}

TEST_CASE("RLS5 传入 NULL 或账号为空串时返回失败,列表不变", "[lal][remember]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));

    CHECK(list.RecordLoginSuccess(NULL) == FALSE);
    const LOGIN_ACCOUNT_INFO blank = MakeLoginInfo(L"", L"pwd_x", 1, TRUE, FALSE);
    CHECK(list.RecordLoginSuccess(&blank) == FALSE);

    CHECK(list.GetCount() == 1);
    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szPwd, L"pwd_a") == 0);
}

TEST_CASE("RLS6 没勾记住密码的登录落盘后读回,密码仍为空", "[lal][remember]")
{
    const std::wstring path = FreshTempDat(L"flamingo_rls6.dat");

    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("old_pwd"), 1, TRUE, FALSE));
    const LOGIN_ACCOUNT_INFO info = MakeLoginInfo(L"alice", L"typed_pwd", 1, FALSE, FALSE);
    REQUIRE(list.RecordLoginSuccess(&info) == TRUE);
    REQUIRE(list.SaveFile(path.c_str()));

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpAlice = reloaded.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcslen(lpAlice->szPwd) == 0);
    CHECK(lpAlice->bRememberPwd == FALSE);
}

TEST_CASE("RLS7 读入旧文件时清掉未勾记住密码的记录里残留的密码", "[lal][remember]")
{
    // 修复之前的客户端已经在用户机器上写下了这样的记录：记住密码为假，密码却存在文件里。
    // 读入时一并清理，下次写文件即为干净的内容；勾了记住密码的记录不受影响。
    const std::wstring path = FreshTempDat(L"flamingo_rls7.dat");

    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);
    int nCount = 2;
    int nLastLoginUser = 0;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));
    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"leaked_pwd", 1, FALSE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"bob", L"pwd_b", 1, TRUE, FALSE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    fclose(fp);

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpAlice = loaded.Find(_T("alice"));
    LOGIN_ACCOUNT_INFO* lpBob   = loaded.Find(_T("bob"));
    REQUIRE(lpAlice != NULL);
    REQUIRE(lpBob != NULL);
    CHECK(wcslen(lpAlice->szPwd) == 0);
    CHECK(wcscmp(lpBob->szPwd, L"pwd_b") == 0);
}

// =====================================================================
// LAL-D —— 删除账号时"最后登录账号"下标的修正
//
// 登录窗下拉里那个删除叉走的就是 CLoginAccountList::Del。m_nLastLoginUser
// 存的是<u>下标</u>而不是账号名，删掉一项之后它前面的项整体前移；不跟着调整
// 就会指向另一个人 —— 表现为下次打开登录窗自动填的是别的账号，甚至越界。
// =====================================================================

TEST_CASE("LAL-D1 删掉排在最后登录账号之前的项，最后登录账号不变", "[lal][del]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("carol"), _T("pwd_c"), 1, TRUE, FALSE));

    list.SetLastLoginUser(_T("carol"));
    REQUIRE(list.GetLastLoginUser() == 2);

    // 删掉排在它前面的 alice：下标要跟着前移一位，指向的人不能变。
    REQUIRE(list.Del(0) == TRUE);
    CHECK(list.GetLastLoginUser() == 1);

    LOGIN_ACCOUNT_INFO stCopy;
    memset(&stCopy, 0, sizeof(stCopy));
    REQUIRE(list.GetLastLoginAccountInfo(&stCopy) == TRUE);
    CHECK(wcscmp(stCopy.szUser, _T("carol")) == 0);
}

TEST_CASE("LAL-D2 删掉排在最后登录账号之后的项，下标不动", "[lal][del]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("carol"), _T("pwd_c"), 1, TRUE, FALSE));

    list.SetLastLoginUser(_T("alice"));
    REQUIRE(list.GetLastLoginUser() == 0);

    REQUIRE(list.Del(2) == TRUE);
    CHECK(list.GetLastLoginUser() == 0);

    LOGIN_ACCOUNT_INFO stCopy;
    memset(&stCopy, 0, sizeof(stCopy));
    REQUIRE(list.GetLastLoginAccountInfo(&stCopy) == TRUE);
    CHECK(wcscmp(stCopy.szUser, _T("alice")) == 0);
}

TEST_CASE("LAL-D3 删掉最后登录账号本身，退回第一个账号", "[lal][del]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("carol"), _T("pwd_c"), 1, TRUE, FALSE));

    list.SetLastLoginUser(_T("bob"));
    REQUIRE(list.GetLastLoginUser() == 1);

    REQUIRE(list.Del(1) == TRUE);
    CHECK(list.GetLastLoginUser() == 0);

    LOGIN_ACCOUNT_INFO stCopy;
    memset(&stCopy, 0, sizeof(stCopy));
    REQUIRE(list.GetLastLoginAccountInfo(&stCopy) == TRUE);
    CHECK(wcscmp(stCopy.szUser, _T("alice")) == 0);
}

TEST_CASE("LAL-D4 删掉末尾那个（同时也是最后登录账号），下标不越界", "[lal][del]")
{
    // 这条是最容易崩的情形：不夹取的话 m_nLastLoginUser 会停在已经不存在的下标上。
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));

    list.SetLastLoginUser(_T("bob"));
    REQUIRE(list.GetLastLoginUser() == 1);

    REQUIRE(list.Del(1) == TRUE);
    CHECK(list.GetCount() == 1);
    CHECK(list.GetLastLoginUser() == 0);

    LOGIN_ACCOUNT_INFO stCopy;
    memset(&stCopy, 0, sizeof(stCopy));
    REQUIRE(list.GetLastLoginAccountInfo(&stCopy) == TRUE);
    CHECK(wcscmp(stCopy.szUser, _T("alice")) == 0);
}

TEST_CASE("LAL-D5 删空整个列表：不崩，且查不到最后登录账号", "[lal][del]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    list.SetLastLoginUser(_T("alice"));

    REQUIRE(list.Del(0) == TRUE);
    CHECK(list.GetCount() == 0);

    // 列表空了，取"最后登录账号"必须失败而不是读到野内存。
    LOGIN_ACCOUNT_INFO stCopy;
    memset(&stCopy, 0, sizeof(stCopy));
    CHECK(list.GetLastLoginAccountInfo(&stCopy) == FALSE);
}

TEST_CASE("LAL-D6 越界下标删除被拒，列表与最后登录账号都不受影响", "[lal][del]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
    list.SetLastLoginUser(_T("bob"));

    CHECK(list.Del(-1) == FALSE);
    CHECK(list.Del(2) == FALSE);
    CHECK(list.GetCount() == 2);
    CHECK(list.GetLastLoginUser() == 1);
}

TEST_CASE("LAL-D7 删除后落盘，重新加载仍是删除后的样子", "[lal][del]")
{
    // 删完必须真的写回文件，否则重启客户端那个账号又回来了。
    const std::wstring path = FreshTempDat(L"lal_del.dat");

    {
        CLoginAccountList list;
        REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
        REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
        REQUIRE(list.Add(_T("carol"), _T("pwd_c"), 1, TRUE, FALSE));
        list.SetLastLoginUser(_T("carol"));

        REQUIRE(list.Del(0) == TRUE);
        REQUIRE(list.SaveFile(path.c_str()) == TRUE);
    }

    {
        CLoginAccountList loaded;
        REQUIRE(loaded.LoadFile(path.c_str()) == TRUE);
        CHECK(loaded.GetCount() == 2);
        CHECK(loaded.Find(_T("alice")) == NULL);
        CHECK(loaded.Find(_T("bob")) != NULL);
        CHECK(loaded.Find(_T("carol")) != NULL);
        CHECK(loaded.GetLastLoginUser() == 1);
    }

    ::DeleteFileW(path.c_str());
}

// ===========================================================================
// Suite LAL-B：用户名为空的账号记录
//
// 2026-08-13 客户端曾在登录成功时账号信息尚未回填的情况下，往账号文件里追加
// 过一条用户名为空的记录，并把它设成最后登录账号。表现是登录窗启动时账号框
// 空空如也、账号下拉列表末尾多出一个空白行，看起来像"历史账号没加载"。
// 产品端已从两头堵：Add 不再收空账号名，LoadFile 读到这种记录直接丢弃。
// ===========================================================================

TEST_CASE("LAL-B1 Add 拒收空账号名", "[lal][blank]")
{
    CLoginAccountList list;

    // 空串与空指针都要挡住，且不能在列表里留下任何痕迹。
    CHECK(list.Add(_T(""), _T("pwd"), 1, TRUE, FALSE) == FALSE);
    CHECK(list.Add(NULL, _T("pwd"), 1, TRUE, FALSE) == FALSE);
    CHECK(list.GetCount() == 0);

    // 正常账号照收不误 —— 别把有效路径一起挡了。
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    CHECK(list.GetCount() == 1);
}

TEST_CASE("LAL-B2 文件里夹着空账号记录，读入后被丢弃", "[lal][blank]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_b2.dat");

    // 手工拼一个"中间夹着空记录"的文件 —— 这种文件用 Add 造不出来（已被
    // LAL-B1 那条规则挡住），只能照磁盘布局直接写。
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);

    int nCount = 3;
    int nLastLoginUser = 0;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));

    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"pwd_a", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"", L"", 0, FALSE, FALSE);
    LoginAccountRecordV1 rec2 = MakeV1Record(L"carol", L"pwd_c", 1, TRUE, FALSE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    WriteEncrypted(fp, &rec2, sizeof(rec2));
    fclose(fp);

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    CHECK(loaded.GetCount() == 2);
    CHECK(loaded.Find(_T("alice")) != NULL);
    CHECK(loaded.Find(_T("carol")) != NULL);

    // 剩下的每一条都必须有账号名 —— 空白行正是用户看到的那个症状。
    for (int i = 0; i < loaded.GetCount(); ++i)
    {
        LOGIN_ACCOUNT_INFO* lpAccount = loaded.GetAccountInfo(i);
        REQUIRE(lpAccount != NULL);
        CHECK(wcslen(lpAccount->szUser) > 0);
    }

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-B3 最后登录索引指向空记录时，退回第一个账号", "[lal][blank]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_b3.dat");

    // 复现用户机器上那份文件的形态：空记录排在末尾，且正是最后登录账号。
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);

    int nCount = 3;
    int nLastLoginUser = 2;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));

    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"pwd_a", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"bob", L"pwd_b", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec2 = MakeV1Record(L"", L"", 0, FALSE, FALSE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    WriteEncrypted(fp, &rec2, sizeof(rec2));
    fclose(fp);

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    CHECK(loaded.GetCount() == 2);

    // 关键断言：最后登录账号必须是一个真实存在、账号名非空的账号，
    // 登录窗才有东西可预填。真正的最后登录账号已无从考证，退回第一个。
    LOGIN_ACCOUNT_INFO* lpLast = loaded.GetLastLoginAccountInfo();
    REQUIRE(lpLast != NULL);
    CHECK(wcslen(lpLast->szUser) > 0);
    CHECK(wcscmp(lpLast->szUser, L"alice") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-B4 清理空记录不会让姓名错位", "[lal][blank]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_b4.dat");

    // 姓名尾块按位置与主体记录一一对应，所以清理空记录必须发生在尾块读完
    // 之后。若提前删，尾块里 carol 的姓名就会被安到 alice 头上 —— 这条用例
    // 就是钉住这个顺序的。
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);

    int nCount = 3;
    int nLastLoginUser = 0;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));

    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"pwd_a", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"", L"", 0, FALSE, FALSE);
    LoginAccountRecordV1 rec2 = MakeV1Record(L"carol", L"pwd_c", 1, TRUE, FALSE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    WriteEncrypted(fp, &rec2, sizeof(rec2));

    const wchar_t* nicks[3] = { L"张三", L"", L"王五" };
    WriteNickTrailer(fp, nicks, 3);
    fclose(fp);

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    REQUIRE(loaded.GetCount() == 2);

    LOGIN_ACCOUNT_INFO* lpAlice = loaded.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);

    LOGIN_ACCOUNT_INFO* lpCarol = loaded.Find(_T("carol"));
    REQUIRE(lpCarol != NULL);
    CHECK(wcscmp(lpCarol->szNickName, L"王五") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-B5 读入后再落盘，坏文件被就地修好", "[lal][blank]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_b5.dat");

    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);

    int nCount = 2;
    int nLastLoginUser = 1;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));

    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"pwd_a", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"", L"", 0, FALSE, FALSE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    fclose(fp);

    {
        // 一次正常的"加载 —— 保存"往返即可把坏记录清出文件，用户不必手工
        // 删账号文件、也不会丢掉其余历史账号。
        CLoginAccountList loaded;
        REQUIRE(loaded.LoadFile(path.c_str()));
        REQUIRE(loaded.SaveFile(path.c_str()));
    }

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    CHECK(reloaded.GetCount() == 1);
    LOGIN_ACCOUNT_INFO* lpAccount = reloaded.GetAccountInfo(0);
    REQUIRE(lpAccount != NULL);
    CHECK(wcscmp(lpAccount->szUser, L"alice") == 0);

    ::DeleteFileW(path.c_str());
}

// ---- 头像尾块（2026-10-04 起）：登录窗的账号下拉列表与大头像按它画该账号的头像 ----

namespace {

// 测试账号的 userid
const long long kAvAliceId = 5127;
const long long kAvBobId = 5308;
const long long kAvCarolId = 5309;
// 头像类型：字形头像、自定义照片（见 AvatarSpec.h 的 AvatarKind）
const int kAvFaceGlyph = 1;
const int kAvFacePhoto = 2;
// 字形头像的头像内容「张|3」（UTF-8 字节）与一个照片文件 id
const char* const kAvGlyphFace = "\xE5\xBC\xA0|3";
const char* const kAvPhotoFace = "9f86d081884c7d65";

} // namespace

TEST_CASE("LAL-AV1 头像信息随账号一起落盘并能读回", "[lal][avatar]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_av1.dat");

    {
        CLoginAccountList list;
        REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
        REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
        REQUIRE(list.SetNickName(_T("alice"), L"张三"));
        REQUIRE(list.SetAvatar(_T("alice"), kAvAliceId, kAvFaceGlyph, kAvGlyphFace));
        REQUIRE(list.SetAvatar(_T("bob"), kAvBobId, kAvFacePhoto, kAvPhotoFace));
        REQUIRE(list.SaveFile(path.c_str()));
    }

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    REQUIRE(loaded.GetCount() == 2);

    LOGIN_ACCOUNT_INFO* lpAlice = loaded.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(lpAlice->nUserId == kAvAliceId);
    CHECK(lpAlice->nFaceType == kAvFaceGlyph);
    CHECK(strcmp(lpAlice->szCustomFace, kAvGlyphFace) == 0);
    // 姓名尾块不受头像尾块影响
    CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);

    LOGIN_ACCOUNT_INFO* lpBob = loaded.Find(_T("bob"));
    REQUIRE(lpBob != NULL);
    CHECK(lpBob->nUserId == kAvBobId);
    CHECK(lpBob->nFaceType == kAvFacePhoto);
    CHECK(strcmp(lpBob->szCustomFace, kAvPhotoFace) == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-AV2 只有姓名尾块的文件读入后头像为空，补上头像后落盘两段都在", "[lal][avatar]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_av2.dat");

    // 这次升级之前的客户端写的文件：主体 + 姓名尾块，没有头像尾块。
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);
    int nCount = 2;
    int nLastLoginUser = 0;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));
    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"pwd_a", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"bob", L"pwd_b", 1, TRUE, FALSE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    const wchar_t* nicks[2] = { L"张三", L"李四" };
    WriteNickTrailer(fp, nicks, 2);
    fclose(fp);

    {
        CLoginAccountList loaded;
        REQUIRE(loaded.LoadFile(path.c_str()));
        LOGIN_ACCOUNT_INFO* lpAlice = loaded.Find(_T("alice"));
        REQUIRE(lpAlice != NULL);
        CHECK(lpAlice->nUserId == 0);
        CHECK(lpAlice->nFaceType == 0);
        CHECK(lpAlice->szCustomFace[0] == '\0');
        CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);

        // 下次登录成功后补上头像，再落盘
        REQUIRE(loaded.SetAvatar(_T("bob"), kAvBobId, kAvFacePhoto, kAvPhotoFace));
        REQUIRE(loaded.SaveFile(path.c_str()));
    }

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpBob = reloaded.Find(_T("bob"));
    REQUIRE(lpBob != NULL);
    CHECK(lpBob->nUserId == kAvBobId);
    CHECK(strcmp(lpBob->szCustomFace, kAvPhotoFace) == 0);
    CHECK(wcscmp(lpBob->szNickName, L"李四") == 0);
    LOGIN_ACCOUNT_INFO* lpAlice = reloaded.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(lpAlice->nUserId == 0);
    CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-AV3 SetAvatar 只认已存在的账号，头像内容为空指针时清空、超长时截断", "[lal][avatar]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    CHECK(list.SetAvatar(_T("nobody"), kAvAliceId, kAvFaceGlyph, kAvGlyphFace) == FALSE);
    CHECK(list.SetAvatar(NULL, kAvAliceId, kAvFaceGlyph, kAvGlyphFace) == FALSE);

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    REQUIRE(list.SetAvatar(_T("alice"), kAvAliceId, kAvFacePhoto, kAvPhotoFace));
    CHECK(strcmp(lpAlice->szCustomFace, kAvPhotoFace) == 0);

    // 头像内容传空指针：清空（回到自动头像）
    REQUIRE(list.SetAvatar(_T("alice"), kAvAliceId, 0, NULL));
    CHECK(lpAlice->nFaceType == 0);
    CHECK(lpAlice->szCustomFace[0] == '\0');
    CHECK(lpAlice->nUserId == kAvAliceId);

    // 超长：截断到定长，并保留结束符
    const std::string longFace(kAvatarCustomFaceBytes * 2, 'a');
    REQUIRE(list.SetAvatar(_T("alice"), kAvAliceId, kAvFacePhoto, longFace.c_str()));
    CHECK(strlen(lpAlice->szCustomFace) == static_cast<size_t>(kAvatarCustomFaceBytes - 1));
}

TEST_CASE("LAL-AV4 清理空记录与删除账号都不会让头像错位", "[lal][avatar]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_av4.dat");

    // 主体三条，中间一条账号为空；姓名尾块与头像尾块都按位置对应三条
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);
    int nCount = 3;
    int nLastLoginUser = 0;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));
    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"pwd_a", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"", L"", 0, FALSE, FALSE);
    LoginAccountRecordV1 rec2 = MakeV1Record(L"carol", L"pwd_c", 1, TRUE, FALSE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    WriteEncrypted(fp, &rec2, sizeof(rec2));
    const wchar_t* nicks[3] = { L"张三", L"", L"王五" };
    WriteNickTrailer(fp, nicks, 3);
    const long long ids[3] = { kAvAliceId, 0, kAvCarolId };
    const int faces[3] = { kAvFaceGlyph, 0, kAvFacePhoto };
    const char* customFaces[3] = { kAvGlyphFace, "", kAvPhotoFace };
    WriteAvatarTrailer(fp, ids, faces, customFaces, 3);
    fclose(fp);

    {
        CLoginAccountList loaded;
        REQUIRE(loaded.LoadFile(path.c_str()));
        REQUIRE(loaded.GetCount() == 2);
        LOGIN_ACCOUNT_INFO* lpCarol = loaded.Find(_T("carol"));
        REQUIRE(lpCarol != NULL);
        CHECK(lpCarol->nUserId == kAvCarolId);
        CHECK(strcmp(lpCarol->szCustomFace, kAvPhotoFace) == 0);

        // 删掉排在前面的 alice 后落盘：carol 的头像跟着 carol 走
        REQUIRE(loaded.Del(0));
        REQUIRE(loaded.SaveFile(path.c_str()));
    }

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    REQUIRE(reloaded.GetCount() == 1);
    LOGIN_ACCOUNT_INFO* lpCarol = reloaded.GetAccountInfo(0);
    REQUIRE(lpCarol != NULL);
    CHECK(wcscmp(lpCarol->szUser, L"carol") == 0);
    CHECK(lpCarol->nUserId == kAvCarolId);
    CHECK(lpCarol->nFaceType == kAvFacePhoto);
    CHECK(strcmp(lpCarol->szCustomFace, kAvPhotoFace) == 0);
    CHECK(wcscmp(lpCarol->szNickName, L"王五") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-AV5 登录成功再次记入已有账号时，已存的头像保持不变", "[lal][avatar]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T("pwd_a"), 1, TRUE, FALSE));
    REQUIRE(list.SetAvatar(_T("alice"), kAvAliceId, kAvFacePhoto, kAvPhotoFace));

    // 登录窗采集的信息里没有头像
    LOGIN_ACCOUNT_INFO info;
    memset(&info, 0, sizeof(info));
    wcscpy_s(info.szUser, _countof(info.szUser), L"alice");
    wcscpy_s(info.szPwd, _countof(info.szPwd), L"pwd_new");
    info.nStatus = 1;
    info.bRememberPwd = TRUE;
    REQUIRE(list.RecordLoginSuccess(&info));

    LOGIN_ACCOUNT_INFO* lpAlice = list.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(wcscmp(lpAlice->szPwd, L"pwd_new") == 0);
    CHECK(lpAlice->nUserId == kAvAliceId);
    CHECK(lpAlice->nFaceType == kAvFacePhoto);
    CHECK(strcmp(lpAlice->szCustomFace, kAvPhotoFace) == 0);
}

// ---------------------------------------------------------------------------
// 规范账号尾块（2026-10-06 起，docs/登录账号与手机号登录方案.md）：账号与手机号都能登录、
// 账号不区分大小写之后，历史列表的键（登录窗里输入的原文）可能与服务端返回的账号不同，
// 而本地数据目录 Users\<账号>\ 按后者建。
// ---------------------------------------------------------------------------

namespace {

// 规范账号尾块的起始魔数、版本号与每条的定长字符数 —— 与 LoginAccountList.cpp / .h 里的
// 同名常量同值。测试自带一份，理由同 kXorKey。
const int kAcctNameSectionMagic = 0x464C4133;
const int kAcctNameSectionVersion = 1;
const int kAcctNameLen = 32;

// 用手机号登录的那条记录：键是手机号，服务端返回的规范账号是带前缀的随机账号
const wchar_t* const kAnPhone = L"13591257075";
const wchar_t* const kAnAccount = L"FL.43350744";

// 在手工拼出来的账号文件末尾追加规范账号尾块（须写在头像尾块之后）。
//   fp：      已打开的二进制写文件句柄，写入位置须停在头像尾块之后；
//   names：   逐条规范账号，顺序与主体记录一一对应；
//   nCount：  条数，须与主体里的记录条数一致；
//   nVersion：写入的版本号，正常为 kAcctNameSectionVersion，测「不认识的版本」时传别的值。
void WriteAccountNameTrailer(FILE* fp, const wchar_t* const* names, int nCount, int nVersion)
{
    WriteEncrypted(fp, &kAcctNameSectionMagic, sizeof(kAcctNameSectionMagic));
    WriteEncrypted(fp, &nVersion, sizeof(nVersion));
    WriteEncrypted(fp, &nCount, sizeof(nCount));

    for (int i = 0; i < nCount; ++i)
    {
        TCHAR szName[kAcctNameLen] = { 0 };
        wcsncpy_s(szName, _countof(szName), names[i], _TRUNCATE);
        WriteEncrypted(fp, szName, sizeof(szName));
    }
}

// 写一个带姓名、头像两个尾块的三条记录文件（中间一条账号为空），可选再带规范账号尾块。
//   path：         目标文件全路径；
//   withNames：    true 时追加规范账号尾块；
//   nNameVersion： 规范账号尾块的版本号（withNames 为 false 时忽略）。
void WriteThreeRecordFile(const std::wstring& path, bool withNames, int nNameVersion)
{
    FILE* fp = _wfopen(path.c_str(), L"wb");
    REQUIRE(fp != NULL);
    int nCount = 3;
    int nLastLoginUser = 2;
    WriteEncrypted(fp, &nCount, sizeof(nCount));
    WriteEncrypted(fp, &nLastLoginUser, sizeof(nLastLoginUser));
    LoginAccountRecordV1 rec0 = MakeV1Record(L"alice", L"pwd_a", 1, TRUE, FALSE);
    LoginAccountRecordV1 rec1 = MakeV1Record(L"", L"", 0, FALSE, FALSE);
    LoginAccountRecordV1 rec2 = MakeV1Record(kAnPhone, L"pwd_p", 1, TRUE, TRUE);
    WriteEncrypted(fp, &rec0, sizeof(rec0));
    WriteEncrypted(fp, &rec1, sizeof(rec1));
    WriteEncrypted(fp, &rec2, sizeof(rec2));
    const wchar_t* nicks[3] = { L"张三", L"", L"手机号登录者" };
    WriteNickTrailer(fp, nicks, 3);
    const long long ids[3] = { kAvAliceId, 0, kAvCarolId };
    const int faces[3] = { kAvFaceGlyph, 0, kAvFacePhoto };
    const char* customFaces[3] = { kAvGlyphFace, "", kAvPhotoFace };
    WriteAvatarTrailer(fp, ids, faces, customFaces, 3);
    if (withNames)
    {
        const wchar_t* names[3] = { L"", L"", kAnAccount };
        WriteAccountNameTrailer(fp, names, 3, nNameVersion);
    }
    fclose(fp);
}

} // namespace

TEST_CASE("LAL-AN1 规范账号随账号一起落盘并能读回，姓名与头像不受影响", "[lal][acctname]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_an1.dat");

    {
        CLoginAccountList list;
        REQUIRE(list.Add(kAnPhone, _T("pwd_p"), 1, TRUE, TRUE));
        REQUIRE(list.Add(_T("bob"), _T("pwd_b"), 1, TRUE, FALSE));
        REQUIRE(list.SetNickName(kAnPhone, L"手机号登录者"));
        REQUIRE(list.SetAvatar(kAnPhone, kAvCarolId, kAvFacePhoto, kAvPhotoFace));
        REQUIRE(list.SetAccountName(kAnPhone, kAnAccount));
        REQUIRE(list.SaveFile(path.c_str()));
    }

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    REQUIRE(loaded.GetCount() == 2);

    LOGIN_ACCOUNT_INFO* lpPhone = loaded.Find(kAnPhone);
    REQUIRE(lpPhone != NULL);
    CHECK(wcscmp(lpPhone->szAccountName, kAnAccount) == 0);
    CHECK(wcscmp(LoginAccountDataName(lpPhone), kAnAccount) == 0);
    CHECK(wcscmp(lpPhone->szNickName, L"手机号登录者") == 0);
    CHECK(lpPhone->nUserId == kAvCarolId);
    CHECK(strcmp(lpPhone->szCustomFace, kAvPhotoFace) == 0);
    CHECK(lpPhone->bAutoLogin == TRUE);

    // 没存规范账号的记录：读回为空串，本地数据目录名按键本身
    LOGIN_ACCOUNT_INFO* lpBob = loaded.Find(_T("bob"));
    REQUIRE(lpBob != NULL);
    CHECK(lpBob->szAccountName[0] == _T('\0'));
    CHECK(wcscmp(LoginAccountDataName(lpBob), L"bob") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-AN2 2026-10-04 版文件（姓名、头像两个尾块）读入后规范账号为空，补上后落盘三段都在", "[lal][acctname]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_an2.dat");
    WriteThreeRecordFile(path, false, kAcctNameSectionVersion);

    {
        CLoginAccountList loaded;
        REQUIRE(loaded.LoadFile(path.c_str()));
        REQUIRE(loaded.GetCount() == 2);
        LOGIN_ACCOUNT_INFO* lpPhone = loaded.Find(kAnPhone);
        REQUIRE(lpPhone != NULL);
        CHECK(lpPhone->szAccountName[0] == _T('\0'));
        CHECK(wcscmp(LoginAccountDataName(lpPhone), kAnPhone) == 0);
        CHECK(lpPhone->nUserId == kAvCarolId);
        CHECK(wcscmp(lpPhone->szNickName, L"手机号登录者") == 0);

        // 下次登录成功后补上规范账号，再落盘
        REQUIRE(loaded.SetAccountName(kAnPhone, kAnAccount));
        REQUIRE(loaded.SaveFile(path.c_str()));
    }

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    LOGIN_ACCOUNT_INFO* lpPhone = reloaded.Find(kAnPhone);
    REQUIRE(lpPhone != NULL);
    CHECK(wcscmp(lpPhone->szAccountName, kAnAccount) == 0);
    CHECK(wcscmp(lpPhone->szNickName, L"手机号登录者") == 0);
    CHECK(lpPhone->nUserId == kAvCarolId);
    CHECK(strcmp(lpPhone->szCustomFace, kAvPhotoFace) == 0);
    LOGIN_ACCOUNT_INFO* lpAlice = reloaded.Find(_T("alice"));
    REQUIRE(lpAlice != NULL);
    CHECK(lpAlice->szAccountName[0] == _T('\0'));
    CHECK(wcscmp(lpAlice->szNickName, L"张三") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-AN3 SetAccountName 只认已存在的账号，传空指针即清空，超长时截断", "[lal][acctname]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(kAnPhone, _T("pwd_p"), 1, TRUE, FALSE));
    CHECK(list.SetAccountName(_T("nobody"), kAnAccount) == FALSE);
    CHECK(list.SetAccountName(NULL, kAnAccount) == FALSE);

    LOGIN_ACCOUNT_INFO* lpPhone = list.Find(kAnPhone);
    REQUIRE(lpPhone != NULL);
    REQUIRE(list.SetAccountName(kAnPhone, kAnAccount));
    CHECK(wcscmp(lpPhone->szAccountName, kAnAccount) == 0);

    REQUIRE(list.SetAccountName(kAnPhone, NULL));
    CHECK(lpPhone->szAccountName[0] == _T('\0'));

    const std::wstring longName(kAcctNameLen * 2, L'A');
    REQUIRE(list.SetAccountName(kAnPhone, longName.c_str()));
    CHECK(wcslen(lpPhone->szAccountName) == static_cast<size_t>(kAcctNameLen - 1));
}

TEST_CASE("LAL-AN4 清理空记录与删除账号都不会让规范账号错位", "[lal][acctname]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_an4.dat");
    WriteThreeRecordFile(path, true, kAcctNameSectionVersion);

    {
        CLoginAccountList loaded;
        REQUIRE(loaded.LoadFile(path.c_str()));
        REQUIRE(loaded.GetCount() == 2);
        LOGIN_ACCOUNT_INFO* lpPhone = loaded.Find(kAnPhone);
        REQUIRE(lpPhone != NULL);
        CHECK(wcscmp(lpPhone->szAccountName, kAnAccount) == 0);
        LOGIN_ACCOUNT_INFO* lpAlice = loaded.Find(_T("alice"));
        REQUIRE(lpAlice != NULL);
        CHECK(lpAlice->szAccountName[0] == _T('\0'));

        // 删掉排在前面的 alice 后落盘：规范账号跟着手机号那条走
        REQUIRE(loaded.Del(0));
        REQUIRE(loaded.SaveFile(path.c_str()));
    }

    CLoginAccountList reloaded;
    REQUIRE(reloaded.LoadFile(path.c_str()));
    REQUIRE(reloaded.GetCount() == 1);
    LOGIN_ACCOUNT_INFO* lpPhone = reloaded.GetAccountInfo(0);
    REQUIRE(lpPhone != NULL);
    CHECK(wcscmp(lpPhone->szUser, kAnPhone) == 0);
    CHECK(wcscmp(lpPhone->szAccountName, kAnAccount) == 0);
    CHECK(wcscmp(lpPhone->szNickName, L"手机号登录者") == 0);
    CHECK(lpPhone->nUserId == kAvCarolId);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-AN5 规范账号尾块版本不认识时整块忽略，其余数据照常读出", "[lal][acctname]")
{
    const std::wstring path = FreshTempDat(L"flamingo_lal_an5.dat");
    WriteThreeRecordFile(path, true, kAcctNameSectionVersion + 1);

    CLoginAccountList loaded;
    REQUIRE(loaded.LoadFile(path.c_str()));
    REQUIRE(loaded.GetCount() == 2);
    LOGIN_ACCOUNT_INFO* lpPhone = loaded.Find(kAnPhone);
    REQUIRE(lpPhone != NULL);
    CHECK(lpPhone->szAccountName[0] == _T('\0'));
    CHECK(lpPhone->nUserId == kAvCarolId);
    CHECK(wcscmp(lpPhone->szNickName, L"手机号登录者") == 0);

    ::DeleteFileW(path.c_str());
}

TEST_CASE("LAL-AN6 登录成功再次记入已有账号时，已存的规范账号保持不变", "[lal][acctname]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(kAnPhone, _T("pwd_p"), 1, TRUE, FALSE));
    REQUIRE(list.SetAccountName(kAnPhone, kAnAccount));

    // 登录窗采集的信息里没有规范账号
    LOGIN_ACCOUNT_INFO info;
    memset(&info, 0, sizeof(info));
    wcscpy_s(info.szUser, _countof(info.szUser), kAnPhone);
    wcscpy_s(info.szPwd, _countof(info.szPwd), L"pwd_new");
    info.nStatus = 1;
    info.bRememberPwd = TRUE;
    REQUIRE(list.RecordLoginSuccess(&info));

    LOGIN_ACCOUNT_INFO* lpPhone = list.Find(kAnPhone);
    REQUIRE(lpPhone != NULL);
    CHECK(wcscmp(lpPhone->szPwd, L"pwd_new") == 0);
    CHECK(wcscmp(lpPhone->szAccountName, kAnAccount) == 0);
}

TEST_CASE("LAL-AN7 取列表键与本地数据目录名的两个函数", "[lal][acctname]")
{
    // 输入的原文非空时用原文
    CHECK(wcscmp(PickLoginAccountListKey(kAnPhone, kAnAccount), kAnPhone) == 0);
    // 原文为空串或空指针时退回规范账号
    CHECK(wcscmp(PickLoginAccountListKey(L"", kAnAccount), kAnAccount) == 0);
    CHECK(wcscmp(PickLoginAccountListKey(NULL, kAnAccount), kAnAccount) == 0);
    // 两者都取不到时为空串，不是空指针
    LPCTSTR lpszNone = PickLoginAccountListKey(NULL, NULL);
    REQUIRE(lpszNone != NULL);
    CHECK(lpszNone[0] == _T('\0'));

    LPCTSTR lpszNoRecord = LoginAccountDataName(NULL);
    REQUIRE(lpszNoRecord != NULL);
    CHECK(lpszNoRecord[0] == _T('\0'));
}

TEST_CASE("LAL-AN8 用手机号登录后退出：按输入的原文能清掉自动登录，按规范账号查不到这条记录", "[lal][acctname]")
{
    // 复现会话控制器的流程：登录成功记入手机号那条（勾了自动登录），再记下规范账号
    CLoginAccountList list;
    LOGIN_ACCOUNT_INFO info;
    memset(&info, 0, sizeof(info));
    wcscpy_s(info.szUser, _countof(info.szUser), kAnPhone);
    wcscpy_s(info.szPwd, _countof(info.szPwd), L"pwd_p");
    info.nStatus = 1;
    info.bRememberPwd = TRUE;
    info.bAutoLogin = TRUE;
    REQUIRE(list.RecordLoginSuccess(&info));
    REQUIRE(list.SetAccountName(kAnPhone, kAnAccount));

    // 登录后本人资料里的账号是规范账号；拿它查历史列表查不到（修复前退出时就是这样漏清的）
    CHECK(list.Find(kAnAccount) == NULL);

    // 修复后退出时用输入的原文查
    LOGIN_ACCOUNT_INFO* lpAccount = list.Find(PickLoginAccountListKey(info.szUser, kAnAccount));
    REQUIRE(lpAccount != NULL);
    lpAccount->bAutoLogin = FALSE;
    CHECK(list.IsAutoLogin() == FALSE);
    // 删除账号连同本地数据时，目录按规范账号
    CHECK(wcscmp(LoginAccountDataName(lpAccount), kAnAccount) == 0);
}
