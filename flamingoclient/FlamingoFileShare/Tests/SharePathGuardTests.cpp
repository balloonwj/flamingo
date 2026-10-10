/**
 *  文件共享路径安全检查单元测试（Suite SPG，标签 [sharepath]），SharePathGuardTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 11 步的 11a，2026-10-02 与用户确认的用例）：
 *    - SPG1 拒绝的相对路径：..、.、空段、绝对路径、盘符、UNC、\\?\、备用数据流、保留名、非法字符、
 *           首尾空格、末尾的点、反斜杠、非法 UTF-8；
 *    - SPG2 中文、超过 MAX_PATH 的超长路径、多层嵌套、中间带空格的合法路径照常通过；
 *    - SPG3 拼到共享根下之后仍在根内；D:\Projects2 不算 D:\Projects 的子目录；比较不区分大小写；
 *    - SPG4 单文件共享只接受空路径；
 *    - SPG5 重解析点：在测试程序目录下建目录联接，指向共享根之外的拒绝，指向根内的允许；
 *    - SPG6 超过 MAX_PATH 的深层位置上的目录联接同样能查出（第 12 步补充：此前文件系统调用没有加 \\?\ 前缀，
 *           深层路径被当作不存在，检查退到较浅的上级而漏掉联接）。
 *  本文件由 FlamingoUnit 编译，被测源文件 Engine/SharePathGuard.cpp 同时编进 FlamingoFileShare.dll。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <shlobj.h>   //SHCreateDirectoryExW
#include <stdlib.h>
#include <string>

#include "../Engine/SharePathGuard.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//SPG2：超过 MAX_PATH 的路径由多少段、每段多长组成（20 段 × 20 个字符，拼起来四百多个字符）
const int kLongPathSegments = 20;
const int kLongPathSegmentChars = 20;

//SPG5：在测试程序目录下建的临时目录名（每次运行前先整个删掉）
const wchar_t* const kReparseTestDir = L"spg5_reparse";

//SPG6：在测试程序目录下建的临时目录名，以及深层路径的段数与每段字符数（12 段 × 30 个字符，超过 MAX_PATH）
const wchar_t* const kLongReparseTestDir = L"spg6_long";
const int kDeepSegments = 12;
const int kDeepSegmentChars = 30;

//测试程序所在目录（不带结尾反斜杠）
std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s(buf);
    size_t pos = s.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? s : s.substr(0, pos);
}

//执行一条 cmd 命令并等它结束（建目录联接、删目录用）；返回 cmd 的退出码
int RunCmd(const std::wstring& command)
{
    std::wstring line = L"cmd.exe /c " + command;
    STARTUPINFOW si;
    ::ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    ::ZeroMemory(&pi, sizeof(pi));
    std::wstring mutableLine = line;
    if (!::CreateProcessW(NULL, &mutableLine[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return -1;
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

//一段 UTF-8 中文：「项目」
const char* const kChineseSegment = "\xE9\xA1\xB9\xE7\x9B\xAE";

} // namespace

TEST_CASE("SPG1 拒绝的相对路径", "[sharepath]")
{
    const char* bad[] =
    {
        "..", "a/..", "../a", "a/../b",            //上一级
        ".", "a/.", "./a",                          //当前目录
        "a//b", "/a", "a/",                         //空段与绝对路径
        "C:", "C:/a", "a/C:b",                      //盘符
        "//server/share", "\\\\server\\share",      //UNC
        "\\\\?\\C:\\a",                             //长路径前缀
        "a\\b",                                     //反斜杠（只认 / 作分隔符）
        "a.txt:stream", "a.txt::$DATA",             //备用数据流
        "CON", "con", "nul.txt", "a/COM1", "LPT9.log", "aux .txt",   //保留名（含带扩展名、扩展名前有空格）
        "a<b", "a>b", "a\"b", "a|b", "a?b", "a*b",   //非法字符
        " a", "a ", "a/ b", "a/b ",                 //首尾空格
        "a.", "a/b..", "a/b.",                      //末尾的点
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        INFO("path: " << bad[i]);
        CHECK_FALSE(SharePathGuard::IsValidRelativePath(bad[i], false));
    }

    //控制字符与非法 UTF-8
    CHECK_FALSE(SharePathGuard::IsValidRelativePath(std::string("a\x01" "b"), false));
    CHECK_FALSE(SharePathGuard::IsValidRelativePath(std::string("a\xFF" "b"), false));
    CHECK_FALSE(SharePathGuard::IsValidRelativePath(std::string("a\0b", 3), false));
}

TEST_CASE("SPG2 合法的相对路径照常通过", "[sharepath]")
{
    CHECK(SharePathGuard::IsValidRelativePath("", false));
    CHECK(SharePathGuard::IsValidRelativePath("a", false));
    CHECK(SharePathGuard::IsValidRelativePath("docs/2026/report.final.docx", false));
    CHECK(SharePathGuard::IsValidRelativePath("my files/a b.txt", false));
    CHECK(SharePathGuard::IsValidRelativePath(".hidden", false));
    CHECK(SharePathGuard::IsValidRelativePath("console.log", false));   //CON 只是前缀，不是保留名
    CHECK(SharePathGuard::IsValidRelativePath(std::string(kChineseSegment) + "/" + kChineseSegment + ".txt", false));

    //超过 MAX_PATH 的路径
    std::string longPath;
    for (int i = 0; i < kLongPathSegments; ++i)
    {
        if (i > 0)
            longPath += "/";
        longPath += std::string(kLongPathSegmentChars, static_cast<char>('a' + i));
    }
    REQUIRE(longPath.size() > MAX_PATH);
    CHECK(SharePathGuard::IsValidRelativePath(longPath, false));
}

TEST_CASE("SPG3 拼到共享根下之后仍在根内，前缀判定按目录边界、不区分大小写", "[sharepath]")
{
    std::wstring out;
    REQUIRE(SharePathGuard::ResolveUnderRoot(L"D:\\Projects", "src/main.cpp", false, out));
    CHECK(out == L"D:\\Projects\\src\\main.cpp");
    REQUIRE(SharePathGuard::ResolveUnderRoot(L"D:\\Projects\\", "src", false, out));
    CHECK(out == L"D:\\Projects\\src");
    REQUIRE(SharePathGuard::ResolveUnderRoot(L"D:\\Projects", "", false, out));
    CHECK(out == L"D:\\Projects");

    //磁盘共享：根是盘符根目录
    REQUIRE(SharePathGuard::ResolveUnderRoot(L"E:\\", "a/b", false, out));
    CHECK(out == L"E:\\a\\b");

    //非法的相对路径不拼
    CHECK_FALSE(SharePathGuard::ResolveUnderRoot(L"D:\\Projects", "../Projects2/x", false, out));

    //前缀判定
    CHECK(SharePathGuard::IsUnderRoot(L"D:\\Projects", L"D:\\Projects"));
    CHECK(SharePathGuard::IsUnderRoot(L"D:\\Projects", L"D:\\Projects\\a"));
    CHECK(SharePathGuard::IsUnderRoot(L"D:\\Projects\\", L"d:\\projects\\A\\b"));
    CHECK_FALSE(SharePathGuard::IsUnderRoot(L"D:\\Projects", L"D:\\Projects2"));
    CHECK_FALSE(SharePathGuard::IsUnderRoot(L"D:\\Projects", L"D:\\Projects2\\a"));
    CHECK_FALSE(SharePathGuard::IsUnderRoot(L"D:\\Projects", L"D:\\"));
    CHECK(SharePathGuard::IsUnderRoot(L"E:\\", L"E:\\a"));
    CHECK_FALSE(SharePathGuard::IsUnderRoot(L"E:\\", L"F:\\a"));
}

TEST_CASE("SPG4 单文件共享只接受空路径", "[sharepath]")
{
    CHECK(SharePathGuard::IsValidRelativePath("", true));
    CHECK_FALSE(SharePathGuard::IsValidRelativePath("a", true));
    CHECK_FALSE(SharePathGuard::IsValidRelativePath("a/b", true));

    std::wstring out;
    REQUIRE(SharePathGuard::ResolveUnderRoot(L"D:\\docs\\a.docx", "", true, out));
    CHECK(out == L"D:\\docs\\a.docx");
    CHECK_FALSE(SharePathGuard::ResolveUnderRoot(L"D:\\docs\\a.docx", "b", true, out));
}

TEST_CASE("SPG5 重解析点：指向共享根之外的拒绝，指向根内的允许", "[sharepath]")
{
    //在测试程序目录下建：root\inner、outside\secret，以及 root\toOutside -> outside、root\toInner -> root\inner
    const std::wstring base = ExeDir() + L"\\" + kReparseTestDir;
    RunCmd(L"rmdir /s /q \"" + base + L"\"");
    const std::wstring root = base + L"\\root";
    const std::wstring outside = base + L"\\outside";
    REQUIRE(::SHCreateDirectoryExW(NULL, (root + L"\\inner").c_str(), NULL) == ERROR_SUCCESS);
    REQUIRE(::SHCreateDirectoryExW(NULL, (outside + L"\\secret").c_str(), NULL) == ERROR_SUCCESS);
    REQUIRE(RunCmd(L"mklink /J \"" + root + L"\\toOutside\" \"" + outside + L"\"") == 0);
    REQUIRE(RunCmd(L"mklink /J \"" + root + L"\\toInner\" \"" + root + L"\\inner\"") == 0);

    //普通目录与根本身
    CHECK(SharePathGuard::IsReparseSafe(root, root));
    CHECK(SharePathGuard::IsReparseSafe(root, root + L"\\inner"));

    //经目录联接走出共享根：拒绝；目标本身、其下已存在与尚不存在的条目都一样
    CHECK_FALSE(SharePathGuard::IsReparseSafe(root, root + L"\\toOutside"));
    CHECK_FALSE(SharePathGuard::IsReparseSafe(root, root + L"\\toOutside\\secret"));
    CHECK_FALSE(SharePathGuard::IsReparseSafe(root, root + L"\\toOutside\\newdir"));

    //目录联接指回共享根之内：允许
    CHECK(SharePathGuard::IsReparseSafe(root, root + L"\\toInner"));
    CHECK(SharePathGuard::IsReparseSafe(root, root + L"\\toInner\\newfile.txt"));

    //尚不存在的普通条目：按最深一层已存在的上级判定
    CHECK(SharePathGuard::IsReparseSafe(root, root + L"\\inner\\a\\b"));

    RunCmd(L"rmdir /s /q \"" + base + L"\"");
}

TEST_CASE("SPG6 超过 MAX_PATH 的深层目录联接", "[sharepath]")
{
    //root\<12 段深层目录>\toOutside -> outside；联接先在浅处建好再移到深处（mklink 不支持超长路径）
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kLongReparseTestDir;
    sharetestfs::DeleteTree(base);
    const std::wstring root = base + L"\\root";
    const std::wstring outside = base + L"\\outside";
    const std::wstring deep = sharetestfs::DeepPath(root, kDeepSegments, kDeepSegmentChars);
    REQUIRE(deep.size() > MAX_PATH);
    REQUIRE(sharetestfs::MakeDirs(deep));
    REQUIRE(sharetestfs::MakeDirs(outside + L"\\secret"));
    REQUIRE(sharetestfs::MakeJunction(deep + L"\\toOutside", outside, base + L"\\tmpjunction"));

    //深层的普通目录：允许
    CHECK(SharePathGuard::IsReparseSafe(root, deep));
    CHECK(SharePathGuard::IsReparseSafe(root, deep + L"\\newfile.txt"));

    //深层的联接走出共享根：拒绝；联接本身、其下已存在与尚不存在的条目都一样
    CHECK_FALSE(SharePathGuard::IsReparseSafe(root, deep + L"\\toOutside"));
    CHECK_FALSE(SharePathGuard::IsReparseSafe(root, deep + L"\\toOutside\\secret"));
    CHECK_FALSE(SharePathGuard::IsReparseSafe(root, deep + L"\\toOutside\\newdir"));

    sharetestfs::DeleteTree(base);
}
