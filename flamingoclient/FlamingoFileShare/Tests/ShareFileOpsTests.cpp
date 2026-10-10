/**
 *  分享者一端文件系统操作单元测试（Suite SFO，标签 [sharefileops]），ShareFileOpsTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 12 步的 12b）：
 *    - SFO1 列目录：先目录后文件、同类按名字不区分大小写排序，大小、修改时间、目录标志正确，中文名正常；
 *           空目录返回空列表；目录不存在、路径指向文件时回 809；
 *    - SFO2 隐藏与系统属性：未勾选包含隐藏文件时这类条目不列出、勾选时列出并带属性位；路径上经过隐藏目录时
 *           HasHiddenComponent 为真，共享根本身带隐藏属性不算；
 *    - SFO3 查条目与共享根可用性：文件、目录、不存在；单文件共享的根必须是文件、其余必须是目录；
 *    - SFO4 超过 MAX_PATH 的深层目录照常可以列出与查条目；
 *    - SFO5 时间换算与错误码归类。
 *  本文件由 FlamingoUnit 编译，被测源文件 Engine/ShareFileOps.cpp 同时编进 FlamingoFileShare.dll。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Engine/ShareFileOps.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的临时目录名（每个用例开始前整个删掉）
const wchar_t* const kSfoTestDir = L"sfo_test";

//SFO1 写入的两个文件的字节数
const size_t kSmallFileBytes = 10;
const size_t kLargeFileBytes = 300000;

//SFO1 给文件设定的修改时间：2000-01-01 00:00:00 UTC 对应的 Unix 毫秒与 FILETIME 值
const int64_t kY2kUnixMs = 946684800000LL;
const uint64_t kY2kFileTime = 125911584000000000ULL;

//SFO4：深层路径的段数与每段字符数（12 段 × 30 个字符，拼起来超过 MAX_PATH）
const int kDeepSegments = 12;
const int kDeepSegmentChars = 30;

//64 位数拆成 FILETIME 两个字段时，高位字段右移的位数
const int kHighDwordShift = 32;

//一段 UTF-8 中文：「项目」，及其 UTF-16 写法
const char* const kChineseUtf8 = "\xE9\xA1\xB9\xE7\x9B\xAE";
const wchar_t* const kChineseWide = L"\x9879\x76EE";

//本套用例的临时目录（每次调用都先清空再建）
std::wstring FreshBase()
{
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSfoTestDir;
    sharetestfs::DeleteTree(base);
    sharetestfs::MakeDirs(base);
    return base;
}

//把文件的修改时间设为 2000-01-01
bool SetY2kMtime(const std::wstring& path)
{
    HANDLE h = ::CreateFileW(ShareToLongPath(path).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(kY2kFileTime & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(kY2kFileTime >> kHighDwordShift);
    const BOOL ok = ::SetFileTime(h, NULL, NULL, &ft);
    ::CloseHandle(h);
    return ok != FALSE;
}

//取条目名列表，便于整体比较顺序
std::vector<std::string> Names(const std::vector<ShareEntry>& entries)
{
    std::vector<std::string> names;
    for (size_t i = 0; i < entries.size(); ++i)
        names.push_back(entries[i].m_name);
    return names;
}

} // namespace

TEST_CASE("SFO1 列目录的顺序与字段", "[sharefileops]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\beta"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\Alpha"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\" + kChineseWide));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\empty"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\b.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\A.bin", kLargeFileBytes));
    REQUIRE(SetY2kMtime(root + L"\\A.bin"));

    std::vector<ShareEntry> entries;
    REQUIRE(ShareFileOps::ListDirectory(root, false, entries) == error_code_ok);

    //先目录（Alpha、beta、empty、中文名），再文件（A.bin、b.txt）；同类不区分大小写
    std::vector<std::string> expected;
    expected.push_back("Alpha");
    expected.push_back("beta");
    expected.push_back("empty");
    expected.push_back(kChineseUtf8);
    expected.push_back("A.bin");
    expected.push_back("b.txt");
    CHECK(Names(entries) == expected);

    REQUIRE(entries.size() == expected.size());
    CHECK(entries[0].m_isDir);
    CHECK(entries[0].m_size == 0);
    CHECK_FALSE(entries[4].m_isDir);
    CHECK(entries[4].m_size == static_cast<int64_t>(kLargeFileBytes));
    CHECK(entries[4].m_mtime == kY2kUnixMs);
    CHECK(entries[5].m_size == static_cast<int64_t>(kSmallFileBytes));
    CHECK(entries[5].m_mtime > kY2kUnixMs);

    //根目录写成带结尾反斜杠的形式结果相同
    std::vector<ShareEntry> again;
    REQUIRE(ShareFileOps::ListDirectory(root + L"\\", false, again) == error_code_ok);
    CHECK(Names(again) == expected);

    //空目录：成功，空列表
    std::vector<ShareEntry> none;
    CHECK(ShareFileOps::ListDirectory(root + L"\\empty", false, none) == error_code_ok);
    CHECK(none.empty());

    //目录不存在、路径指向文件：809
    CHECK(ShareFileOps::ListDirectory(root + L"\\nosuchdir", false, none) == error_code_share_path_not_found);
    CHECK(ShareFileOps::ListDirectory(root + L"\\b.txt", false, none) == error_code_share_path_not_found);
    CHECK(none.empty());

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SFO2 隐藏与系统属性的过滤", "[sharefileops]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\hiddendir\\inner"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\plain\\sub"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\hidden.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\system.txt", kSmallFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\visible.txt", kSmallFileBytes));
    REQUIRE(::SetFileAttributesW((root + L"\\hiddendir").c_str(), FILE_ATTRIBUTE_HIDDEN));
    REQUIRE(::SetFileAttributesW((root + L"\\hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN));
    REQUIRE(::SetFileAttributesW((root + L"\\system.txt").c_str(), FILE_ATTRIBUTE_SYSTEM));

    //未勾选：只剩 plain 与 visible.txt
    std::vector<ShareEntry> entries;
    REQUIRE(ShareFileOps::ListDirectory(root, false, entries) == error_code_ok);
    std::vector<std::string> expected;
    expected.push_back("plain");
    expected.push_back("visible.txt");
    CHECK(Names(entries) == expected);

    //勾选：全部列出，属性位只保留只读、隐藏、系统
    REQUIRE(ShareFileOps::ListDirectory(root, true, entries) == error_code_ok);
    REQUIRE(entries.size() == 5);
    CHECK(entries[0].m_name == "hiddendir");
    CHECK(entries[0].m_attrs == FILE_ATTRIBUTE_HIDDEN);
    CHECK(entries[2].m_name == "hidden.txt");
    CHECK(entries[2].m_attrs == FILE_ATTRIBUTE_HIDDEN);
    CHECK(entries[3].m_name == "system.txt");
    CHECK(entries[3].m_attrs == FILE_ATTRIBUTE_SYSTEM);
    CHECK(entries[4].m_attrs == 0);

    //路径上经过隐藏目录：为真；条目自身隐藏：为真；普通路径：为假；尚不存在的一级之后不再检查
    CHECK(ShareFileOps::HasHiddenComponent(root, root + L"\\hiddendir"));
    CHECK(ShareFileOps::HasHiddenComponent(root, root + L"\\hiddendir\\inner"));
    CHECK(ShareFileOps::HasHiddenComponent(root, root + L"\\hidden.txt"));
    CHECK(ShareFileOps::HasHiddenComponent(root, root + L"\\system.txt"));
    CHECK_FALSE(ShareFileOps::HasHiddenComponent(root, root + L"\\plain\\sub"));
    CHECK_FALSE(ShareFileOps::HasHiddenComponent(root, root));
    CHECK_FALSE(ShareFileOps::HasHiddenComponent(root, root + L"\\nosuch\\x"));

    //共享根本身带隐藏属性（如磁盘根目录）不算
    REQUIRE(::SetFileAttributesW(root.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
    CHECK_FALSE(ShareFileOps::HasHiddenComponent(root, root + L"\\plain"));
    CHECK_FALSE(ShareFileOps::HasHiddenComponent(root + L"\\", root + L"\\plain\\sub"));

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SFO3 查条目与共享根可用性", "[sharefileops]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dir"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\" + kChineseWide + L".txt", kSmallFileBytes));

    ShareEntry e;
    REQUIRE(ShareFileOps::StatEntry(root + L"\\" + kChineseWide + L".txt", e) == error_code_ok);
    CHECK(e.m_name == std::string(kChineseUtf8) + ".txt");
    CHECK_FALSE(e.m_isDir);
    CHECK(e.m_size == static_cast<int64_t>(kSmallFileBytes));
    CHECK(e.m_mtime > 0);

    REQUIRE(ShareFileOps::StatEntry(root + L"\\dir", e) == error_code_ok);
    CHECK(e.m_name == "dir");
    CHECK(e.m_isDir);
    CHECK(e.m_size == 0);

    CHECK(ShareFileOps::StatEntry(root + L"\\nosuch.txt", e) == error_code_share_path_not_found);

    //盘符根目录：名字取 "X:"
    const std::wstring drive = base.substr(0, 3);
    REQUIRE(ShareFileOps::StatEntry(drive, e) == error_code_ok);
    CHECK(e.m_name == ShareWideToUtf8(drive.substr(0, 2)));
    CHECK(e.m_isDir);

    //共享根可用性：类型必须相符，不存在为假
    CHECK(ShareFileOps::IsRootAvailable(root, share_kind_folder));
    CHECK(ShareFileOps::IsRootAvailable(drive, share_kind_disk));
    CHECK_FALSE(ShareFileOps::IsRootAvailable(root, share_kind_file));
    CHECK(ShareFileOps::IsRootAvailable(root + L"\\" + kChineseWide + L".txt", share_kind_file));
    CHECK_FALSE(ShareFileOps::IsRootAvailable(root + L"\\" + kChineseWide + L".txt", share_kind_folder));
    CHECK_FALSE(ShareFileOps::IsRootAvailable(root + L"\\gone", share_kind_folder));
    CHECK_FALSE(ShareFileOps::IsRootAvailable(L"", share_kind_folder));

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SFO4 超过 MAX_PATH 的深层目录", "[sharefileops]")
{
    const std::wstring base = FreshBase();
    const std::wstring deep = sharetestfs::DeepPath(base + L"\\root", kDeepSegments, kDeepSegmentChars);
    REQUIRE(deep.size() > MAX_PATH);
    REQUIRE(sharetestfs::MakeDirs(deep + L"\\leafdir"));
    REQUIRE(sharetestfs::WriteFileBytes(deep + L"\\leaf.txt", kSmallFileBytes));

    std::vector<ShareEntry> entries;
    REQUIRE(ShareFileOps::ListDirectory(deep, false, entries) == error_code_ok);
    std::vector<std::string> expected;
    expected.push_back("leafdir");
    expected.push_back("leaf.txt");
    CHECK(Names(entries) == expected);

    ShareEntry e;
    REQUIRE(ShareFileOps::StatEntry(deep + L"\\leaf.txt", e) == error_code_ok);
    CHECK(e.m_size == static_cast<int64_t>(kSmallFileBytes));
    CHECK(ShareFileOps::IsRootAvailable(deep, share_kind_folder));
    CHECK_FALSE(ShareFileOps::HasHiddenComponent(base + L"\\root", deep + L"\\leaf.txt"));

    sharetestfs::DeleteTree(base);
}

TEST_CASE("SFO5 时间换算与错误码归类", "[sharefileops]")
{
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(kY2kFileTime & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(kY2kFileTime >> kHighDwordShift);
    CHECK(ShareFileOps::FileTimeToUnixMs(ft) == kY2kUnixMs);

    //早于 1970 年：0
    FILETIME early;
    early.dwLowDateTime = 1;
    early.dwHighDateTime = 0;
    CHECK(ShareFileOps::FileTimeToUnixMs(early) == 0);

    CHECK(ShareFileOps::MapWin32Error(ERROR_FILE_NOT_FOUND) == error_code_share_path_not_found);
    CHECK(ShareFileOps::MapWin32Error(ERROR_PATH_NOT_FOUND) == error_code_share_path_not_found);
    CHECK(ShareFileOps::MapWin32Error(ERROR_NOT_READY) == error_code_share_path_not_found);
    CHECK(ShareFileOps::MapWin32Error(ERROR_ACCESS_DENIED) == error_code_share_io_fail);
    CHECK(ShareFileOps::MapWin32Error(ERROR_SHARING_VIOLATION) == error_code_share_io_fail);
}
