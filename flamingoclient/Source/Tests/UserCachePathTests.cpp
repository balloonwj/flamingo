/**
 *  按登录账号隔离的本地缓存目录口径单元测试（Suite UCP），UserCachePathTests.cpp
 *
 *  背景：Users 目录下原本混着两套命名 —— 配置 / 聊天记录 / 头像按登录账号
 *  （Users\MY0309\），而文件、图片、头像三处缓存按数字 userid（Users\5309\），
 *  同一个人被劈成两个目录。现已统一到登录账号，本组用例钉住这个口径。
 *
 *  覆盖：目录拼接、结尾反斜杠的容错、空入参、账号净化与目录穿越防护。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-11
 */
#include "catch_amalgamated.hpp"
#include "../net/UserCachePath.h"

#include <windows.h>   //ED2：在临时目录里实际建目录、写 ini

#include <string>
#include <vector>

using namespace net;

TEST_CASE("UCP1 按登录账号拼出缓存目录", "[usercachepath]")
{
    // 这是本次改动的核心口径：Users 下一律用登录账号，不再出现数字 userid。
    CHECK(UserCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\", L"MY0309", L"ChatFile")
          == L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0309\\ChatFile\\");
    CHECK(UserCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\", L"MY0309", L"ChatImage")
          == L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0309\\ChatImage\\");
    CHECK(UserCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\", L"EMP5321", L"Avatar")
          == L"E:\\flamingo\\flamingoclient\\Bin\\Users\\EMP5321\\Avatar\\");
}

TEST_CASE("UCP2 子目录留空时只拼到账号那一层", "[usercachepath]")
{
    CHECK(UserCacheDir(L"C:\\app\\", L"MY0309", NULL) == L"C:\\app\\Users\\MY0309\\");
    CHECK(UserCacheDir(L"C:\\app\\", L"MY0309", L"") == L"C:\\app\\Users\\MY0309\\");
}

TEST_CASE("UCP3 主目录没带结尾反斜杠也能拼对", "[usercachepath]")
{
    // 全局 g_szHomePath 的约定是以反斜杠结尾，但这个约定全靠各处自觉。少了一个
    // 反斜杠会拼出 "C:\appUsers\..." 这种查不出问题的路径，故在此兜一道。
    CHECK(UserCacheDir(L"C:\\app", L"MY0309", L"ChatFile") == L"C:\\app\\Users\\MY0309\\ChatFile\\");
}

TEST_CASE("UCP4 主目录或账号为空：返回空串，交调用方按无缓存处理", "[usercachepath]")
{
    // 未登录时账号是空的。这时候宁可返回空串让调用方判"没有缓存"，也不能退化成
    // 相对路径，把缓存写到进程当前工作目录里去。
    CHECK(UserCacheDir(L"", L"MY0309", L"ChatFile").empty());
    CHECK(UserCacheDir(L"C:\\app\\", L"", L"ChatFile").empty());
    CHECK(UserCacheDir(L"", L"", L"ChatFile").empty());
}

TEST_CASE("UCP5 正常账号原样保留", "[usercachepath]")
{
    // 服务端生成的账号只有字母和数字，净化不该动它们。
    CHECK(SanitizeAccountForPath(L"MY0309") == L"MY0309");
    CHECK(SanitizeAccountForPath(L"EMP5321") == L"EMP5321");
}

TEST_CASE("UCP6 账号里的路径分隔符被净化，写不出 Users 目录之外", "[usercachepath]")
{
    // 账号规则将来放宽、或本地历史数据里混进异常账号时，不能让它把缓存写到别处去。
    // 分隔符换成下划线后，开头那两个点被"去首尾点"一并吃掉，剩下的 ".." 只是普通
    // 目录名的一部分 —— 没有分隔符跟着，它跳不出任何一层。
    CHECK(SanitizeAccountForPath(L"..\\..\\windows") == L"_.._windows");
    CHECK(SanitizeAccountForPath(L"a/b") == L"a_b");
    CHECK(SanitizeAccountForPath(L"C:evil") == L"C_evil");

    // 拼进完整路径后，账号仍是 Users 下的一层普通目录，跳不出去。
    CHECK(UserCacheDir(L"C:\\app\\", L"..\\..\\windows", L"ChatFile")
          == L"C:\\app\\Users\\_.._windows\\ChatFile\\");
}

TEST_CASE("UCP7 账号里的保留字符与控制字符换成下划线", "[usercachepath]")
{
    CHECK(SanitizeAccountForPath(L"a*b?c\"d<e>f|g") == L"a_b_c_d_e_f_g");
    CHECK(SanitizeAccountForPath(L"a\tb") == L"a_b");
}

TEST_CASE("UCP8 首尾的点与空白被去掉", "[usercachepath]")
{
    // Windows 建目录时会把结尾的点和空格默默吃掉。留着的话，本函数拼出来的路径
    // 与磁盘上真实的目录名对不上，缓存就会"每次都找不到"。
    CHECK(SanitizeAccountForPath(L"  MY0309  ") == L"MY0309");
    CHECK(SanitizeAccountForPath(L"MY0309...") == L"MY0309");
    CHECK(SanitizeAccountForPath(L"...") == L"");
    CHECK(SanitizeAccountForPath(L"   ") == L"");

    // 净化后什么都不剩，等同于拿不到账号，须返回空串。
    CHECK(UserCacheDir(L"C:\\app\\", L"...", L"ChatFile").empty());
}

TEST_CASE("UCP9 账号配置文件是账号目录下的 <账号>.ini", "[usercachepath]")
{
    CHECK(UserConfigFilePath(L"C:\\app\\Bin\\", L"EMP5317") == L"C:\\app\\Bin\\Users\\EMP5317\\EMP5317.ini");
    // 主目录没带结尾反斜杠时同 UCP3，补上再拼
    CHECK(UserConfigFilePath(L"C:\\app\\Bin", L"EMP5317") == L"C:\\app\\Bin\\Users\\EMP5317\\EMP5317.ini");
    // 目录名与文件名用同一个净化结果（规则见 UCP6），配置文件不会落到账号目录之外
    CHECK(UserConfigFilePath(L"C:\\app\\", L"..\\..\\windows")
          == L"C:\\app\\Users\\_.._windows\\_.._windows.ini");
}

TEST_CASE("UCP10 主目录或账号为空时拼不出配置文件路径", "[usercachepath]")
{
    CHECK(UserConfigFilePath(L"", L"EMP5317").empty());
    CHECK(UserConfigFilePath(L"C:\\app\\", L"").empty());
    CHECK(UserConfigFilePath(L"C:\\app\\", L"...").empty());
}

// =====================================================================
// ED 组：登录后必须事先建好的账号目录
//
//   账号配置（<账号>.ini 经 WritePrivateProfileString 写入）、最近联系人列表、本地消息库
//   （ChatLog\msgv2.db，只建最后一级 ChatLog）都假定 <数据根目录>Users\<账号>\ 已经存在。
//   2026-10-01 修复前，登录流程把这一级建在程序目录下，用户改过数据目录之后数据根目录下
//   没有它：实测本地消息库在登录后打不开，WritePrivateProfileString 返回错误码 3
//   （ERROR_PATH_NOT_FOUND）。
// =====================================================================

namespace {

// ED2 临时目录名的前缀，便于在残留时认出是本组用例留下的。
const wchar_t* const kEd2TempPrefix = L"FlamingoUnit_ED2_";

// 判断某个目录是否存在。
bool DirExists(const std::wstring& dir)
{
    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// 拼一个尚不存在的两级临时目录：<系统临时目录>FlamingoUnit_ED2_<进程号>_<毫秒数>\nested\，
// 结尾带反斜杠。用两级是为了覆盖「数据根目录本身也不存在」的情形。
std::wstring MakeMissingTempRoot(std::wstring& outerDir)
{
    wchar_t tempPath[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempPath);
    outerDir = tempPath;
    outerDir += kEd2TempPrefix;
    outerDir += std::to_wstring((unsigned long long)::GetCurrentProcessId());
    outerDir += L"_";
    outerDir += std::to_wstring((unsigned long long)::GetTickCount64());
    outerDir += L"\\";
    return outerDir + L"nested\\";
}

} // namespace

TEST_CASE("ED1 必需目录全部位于数据根目录下，账号目录就是配置文件所在的目录", "[usercachepath][essentialdirs]")
{
    const std::wstring root = L"D:\\FlamingoData\\";
    const std::vector<std::wstring> dirs = UserEssentialDirs(root, L"MY0127");

    REQUIRE(dirs.size() == 3);
    CHECK(dirs[0] == L"D:\\FlamingoData\\Users\\MY0127\\");
    CHECK(dirs[1] == L"D:\\FlamingoData\\Users\\MY0127\\ChatImage\\");
    CHECK(dirs[2] == L"D:\\FlamingoData\\Users\\MY0127\\UserThumb\\");
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        INFO("dir index = " << i);
        CHECK(dirs[i].compare(0, root.size(), root) == 0);
    }

    //配置文件与本地消息库都放在账号目录下，账号目录必须在列表里，且与它们同一口径
    CHECK(dirs[0] == UserCacheDir(root, L"MY0127", NULL));
    CHECK(UserConfigFilePath(root, L"MY0127") == dirs[0] + L"MY0127.ini");

    //数据根目录没带结尾反斜杠时同 UCP3，补上再拼
    CHECK(UserEssentialDirs(L"D:\\FlamingoData", L"MY0127") == dirs);
}

TEST_CASE("ED2 数据根目录本身不存在时逐级建出账号目录，之后账号配置能写进去", "[usercachepath][essentialdirs]")
{
    std::wstring outer;
    const std::wstring root = MakeMissingTempRoot(outer);
    REQUIRE_FALSE(DirExists(root));

    const std::wstring config = UserConfigFilePath(root, L"MY0127");
    REQUIRE_FALSE(config.empty());

    //修复前的情形：账号目录不存在，写配置失败、错误码 3。错误码紧接着调用读出来，
    //免得断言宏内部的处理改掉它
    const BOOL wroteBefore = ::WritePrivateProfileStringW(L"App", L"Mute", L"0", config.c_str());
    const DWORD errorBefore = ::GetLastError();
    CHECK_FALSE(wroteBefore);
    CHECK(errorBefore == (DWORD)ERROR_PATH_NOT_FOUND);

    CHECK(EnsureUserEssentialDirs(root, L"MY0127"));
    const std::vector<std::wstring> dirs = UserEssentialDirs(root, L"MY0127");
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        INFO("dir index = " << i);
        CHECK(DirExists(dirs[i]));
    }
    CHECK(::WritePrivateProfileStringW(L"App", L"Mute", L"0", config.c_str()));

    //目录都已存在时再调一次仍返回成功（每次登录、断线重连成功后都会调）
    CHECK(EnsureUserEssentialDirs(root, L"MY0127"));

    //清理：先删配置文件，再从最深一级往上删目录
    ::DeleteFileW(config.c_str());
    for (size_t i = dirs.size(); i > 0; --i)
    {
        ::RemoveDirectoryW(dirs[i - 1].c_str());
    }
    ::RemoveDirectoryW((root + L"Users\\").c_str());
    ::RemoveDirectoryW(root.c_str());
    ::RemoveDirectoryW(outer.c_str());
    CHECK_FALSE(DirExists(outer));
}

TEST_CASE("ED3 账号拿不到时不建任何目录，穿越字符被净化后目录仍在 Users 之内", "[usercachepath][essentialdirs]")
{
    //账号为空、或净化后什么都不剩（同 UCP7 / UCP10）：没有可建的目录，创建返回失败
    CHECK(UserEssentialDirs(L"C:\\app\\", L"").empty());
    CHECK(UserEssentialDirs(L"C:\\app\\", L"...").empty());
    CHECK(UserEssentialDirs(L"", L"MY0127").empty());

    std::wstring outer;
    const std::wstring root = MakeMissingTempRoot(outer);
    CHECK_FALSE(EnsureUserEssentialDirs(root, L""));
    CHECK_FALSE(EnsureUserEssentialDirs(root, L"..."));
    CHECK_FALSE(DirExists(outer));   //失败时一级目录都不应建出来

    //含穿越字符的账号按 UCP6 的规则净化成普通目录名，全部目录都在 <根>Users\ 之内
    const std::vector<std::wstring> dirs = UserEssentialDirs(L"C:\\app\\", L"..\\..\\windows");
    REQUIRE(dirs.size() == 3);
    CHECK(dirs[0] == L"C:\\app\\Users\\_.._windows\\");
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        CHECK(dirs[i].compare(0, wcslen(L"C:\\app\\Users\\"), L"C:\\app\\Users\\") == 0);
    }
}
