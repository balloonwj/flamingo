/**
 *  传输子会话清单与落点规划单元测试（Suite STP，标签 [sharetransfer]），ShareTransferPlanTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 14 步的 14b；STP1~3 在测试程序目录下建真实的文件与目录联接）：
 *    - STP1 下载清单：目录递归扫描、文件直接成项，跳过隐藏条目与目录联接，相对路径以条目名开头，带字节数与修改时间；
 *           读源文件的根是条目所在的目录；
 *    - STP2 下载清单的拒绝：条目为空或不在同一目录、写法非法为 808，不存在或是隐藏条目为 809，单文件共享的条目不是空串
 *           为 820；单文件共享的空串条目就是那个文件；
 *    - STP3 上传清单：本机同一目录下的文件与目录，包含隐藏文件；不在同一目录为 808，不存在为 809；
 *    - STP4 顶层名字的提取与替换；
 *    - STP5 选不重名的名字：文件拆扩展名、目录不拆、点开头的名字不拆，试满仍被占用返回失败；
 *    - STP6 上传的顶层同名检查。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <set>
#include <string>
#include <vector>

#include "../Engine/ShareTransferPlan.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"

using namespace fileshare;
using foldertransfer::FolderManifest;
using foldertransfer::FolderRelayFileEntry;

namespace {

//本套用例在测试程序目录下建的临时目录名（每个用例开始前整个删掉）
const wchar_t* const kStpTestDir = L"stp_test";

//几个文件的字节数
const size_t kFileA = 11;
const size_t kFileB = 22;
const size_t kReadme = 33;

//本套用例的临时目录（每次调用都先清空再建）
std::wstring FreshBase()
{
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kStpTestDir;
    sharetestfs::DeleteTree(base);
    sharetestfs::MakeDirs(base);
    return base;
}

//建一个下载用的共享根：docs/a.txt、docs/sub/b.txt、readme.txt、隐藏目录 secret/x.txt、docs/toOutside（目录联接）
std::wstring MakeShareRoot(const std::wstring& base)
{
    const std::wstring root = base + L"\\root";
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs\\sub"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\secret"));
    REQUIRE(sharetestfs::MakeDirs(base + L"\\outside"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\a.txt", kFileA));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\sub\\b.txt", kFileB));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\readme.txt", kReadme));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\secret\\x.txt", kFileA));
    REQUIRE(sharetestfs::WriteFileBytes(base + L"\\outside\\leak.txt", kFileA));
    REQUIRE(::SetFileAttributesW((root + L"\\secret").c_str(), FILE_ATTRIBUTE_HIDDEN));
    REQUIRE(sharetestfs::RunCmd(L"mklink /J \"" + root + L"\\docs\\toOutside\" \"" + base + L"\\outside\"") == 0);
    return root;
}

//清单里文件的相对路径集合
std::set<std::string> FilePaths(const FolderManifest& m)
{
    std::set<std::string> s;
    for (size_t i = 0; i < m.m_files.size(); ++i)
        s.insert(m.m_files[i].m_relativePath);
    return s;
}

//假的名字占用判断：集合里的路径算已存在
class FakeProbe : public IShareNameProbe
{
public:
    FakeProbe()
        : m_all(false)
    {
    }

    virtual bool Exists(const std::wstring& fullPath) override
    {
        return m_all || m_taken.find(fullPath) != m_taken.end();
    }

    std::set<std::wstring> m_taken;   //已存在的完整路径
    bool                   m_all;     //为真时一切路径都算已存在
};

} // namespace

TEST_CASE("STP1 下载清单的生成", "[sharetransfer]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = MakeShareRoot(base);

    std::vector<std::string> items;
    items.push_back("docs");
    items.push_back("readme.txt");
    FolderManifest m;
    std::wstring sourceRoot;
    REQUIRE(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot) == error_code_ok);
    CHECK(m.m_folderName.empty());
    CHECK(sourceRoot == root);

    //目录：docs 与 docs/sub；目录联接 toOutside 不在其中
    std::set<std::string> dirs(m.m_dirs.begin(), m.m_dirs.end());
    CHECK(dirs.size() == 2);
    CHECK(dirs.count("docs") == 1);
    CHECK(dirs.count("docs/sub") == 1);
    //文件：两层目录下的文件与顶层的 readme.txt；联接指向的 leak.txt 不在其中
    std::set<std::string> expected;
    expected.insert("docs/a.txt");
    expected.insert("docs/sub/b.txt");
    expected.insert("readme.txt");
    CHECK(FilePaths(m) == expected);
    for (size_t i = 0; i < m.m_files.size(); ++i)
    {
        INFO(m.m_files[i].m_relativePath);
        CHECK(m.m_files[i].m_mtimeMs > 0);
        if (m.m_files[i].m_relativePath == "docs/sub/b.txt")
            CHECK(m.m_files[i].m_sizeBytes == static_cast<int64_t>(kFileB));
    }

    //子目录里的条目：根是条目所在的目录，相对路径就是条目名
    items.clear();
    items.push_back("docs/a.txt");
    REQUIRE(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot) == error_code_ok);
    CHECK(sourceRoot == root + L"\\docs");
    REQUIRE(m.m_files.size() == 1);
    CHECK(m.m_files[0].m_relativePath == "a.txt");

    //含隐藏文件的共享：隐藏目录可以作为条目
    items.clear();
    items.push_back("secret");
    REQUIRE(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, true, items, m, sourceRoot) == error_code_ok);
    CHECK(FilePaths(m).count("secret/x.txt") == 1);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("STP2 下载清单的拒绝与单文件共享", "[sharetransfer]")
{
    const std::wstring base = FreshBase();
    const std::wstring root = MakeShareRoot(base);
    FolderManifest m;
    std::wstring sourceRoot;
    std::vector<std::string> items;

    CHECK(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot)
          == error_code_share_path_invalid);
    items.push_back("docs/a.txt");
    items.push_back("readme.txt");
    CHECK(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot)
          == error_code_share_path_invalid);
    items.clear();
    items.push_back("../outside");
    CHECK(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot)
          == error_code_share_path_invalid);
    items.clear();
    items.push_back("docs/toOutside");
    CHECK(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot)
          == error_code_share_path_invalid);
    items.clear();
    items.push_back("nosuch.txt");
    CHECK(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot)
          == error_code_share_path_not_found);
    items.clear();
    items.push_back("secret");
    CHECK(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot)
          == error_code_share_path_not_found);
    items.clear();
    items.push_back("");
    CHECK(ShareTransferPlan::BuildDownloadManifest(root, share_kind_folder, false, items, m, sourceRoot)
          == error_code_share_path_invalid);

    //单文件共享：空串条目就是那个文件，根是它所在的目录
    const std::wstring file = root + L"\\readme.txt";
    items.clear();
    items.push_back("");
    REQUIRE(ShareTransferPlan::BuildDownloadManifest(file, share_kind_file, false, items, m, sourceRoot) == error_code_ok);
    CHECK(sourceRoot == root);
    REQUIRE(m.m_files.size() == 1);
    CHECK(m.m_files[0].m_relativePath == "readme.txt");
    CHECK(m.m_files[0].m_sizeBytes == static_cast<int64_t>(kReadme));
    items[0] = "readme.txt";
    CHECK(ShareTransferPlan::BuildDownloadManifest(file, share_kind_file, false, items, m, sourceRoot)
          == error_code_share_kind_mismatch);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("STP3 上传清单的生成与拒绝", "[sharetransfer]")
{
    const std::wstring base = FreshBase();
    const std::wstring local = base + L"\\local";
    REQUIRE(sharetestfs::MakeDirs(local + L"\\photos\\2026"));
    REQUIRE(sharetestfs::WriteFileBytes(local + L"\\photos\\2026\\p.jpg", kFileA));
    REQUIRE(sharetestfs::WriteFileBytes(local + L"\\photos\\.hidden", kFileB));
    REQUIRE(::SetFileAttributesW((local + L"\\photos\\.hidden").c_str(), FILE_ATTRIBUTE_HIDDEN));
    REQUIRE(sharetestfs::WriteFileBytes(local + L"\\note.txt", kReadme));
    REQUIRE(sharetestfs::MakeDirs(base + L"\\other"));
    REQUIRE(sharetestfs::WriteFileBytes(base + L"\\other\\x.txt", kFileA));

    std::vector<std::wstring> items;
    items.push_back(local + L"\\photos");
    items.push_back(local + L"\\note.txt");
    FolderManifest m;
    std::wstring sourceRoot;
    REQUIRE(ShareTransferPlan::BuildUploadManifest(items, m, sourceRoot) == error_code_ok);
    CHECK(sourceRoot == local);
    std::set<std::string> expected;
    expected.insert("photos/2026/p.jpg");
    expected.insert("photos/.hidden");
    expected.insert("note.txt");
    CHECK(FilePaths(m) == expected);

    //不在同一目录
    items.push_back(base + L"\\other\\x.txt");
    CHECK(ShareTransferPlan::BuildUploadManifest(items, m, sourceRoot) == error_code_share_path_invalid);
    //不存在
    items.clear();
    items.push_back(local + L"\\nosuch.txt");
    CHECK(ShareTransferPlan::BuildUploadManifest(items, m, sourceRoot) == error_code_share_path_not_found);
    items.clear();
    CHECK(ShareTransferPlan::BuildUploadManifest(items, m, sourceRoot) == error_code_share_path_invalid);

    sharetestfs::DeleteTree(base);
}

TEST_CASE("STP4 顶层名字的提取与替换", "[sharetransfer]")
{
    std::vector<std::string> dirs;
    dirs.push_back("docs");
    dirs.push_back("docs/sub");
    std::vector<FolderRelayFileEntry> files;
    FolderRelayFileEntry f;
    f.m_relpath = "docs/a.txt";
    files.push_back(f);
    f.m_relpath = "readme.txt";
    files.push_back(f);
    const std::vector<std::string> names = ShareTransferPlan::TopLevelNames(dirs, files);
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "docs");
    CHECK(names[1] == "readme.txt");

    CHECK(ShareTransferPlan::ReplaceTopLevel("docs/sub/b.txt", "docs", "docs(2)") == "docs(2)/sub/b.txt");
    CHECK(ShareTransferPlan::ReplaceTopLevel("docs", "docs", "docs(2)") == "docs(2)");
    CHECK(ShareTransferPlan::ReplaceTopLevel("docsx/a", "docs", "docs(2)") == "docsx/a");
}

TEST_CASE("STP5 选不重名的名字", "[sharetransfer]")
{
    const std::wstring dir = L"C:\\target";
    FakeProbe probe;
    std::wstring out;

    REQUIRE(ShareTransferPlan::ChooseFreeName(dir, L"a.txt", false, probe, out));
    CHECK(out == L"a.txt");

    probe.m_taken.insert(L"C:\\target\\a.txt");
    probe.m_taken.insert(L"C:\\target\\a(2).txt");
    REQUIRE(ShareTransferPlan::ChooseFreeName(dir, L"a.txt", false, probe, out));
    CHECK(out == L"a(3).txt");

    probe.m_taken.insert(L"C:\\target\\docs.v1");
    REQUIRE(ShareTransferPlan::ChooseFreeName(dir, L"docs.v1", true, probe, out));
    CHECK(out == L"docs.v1(2)");

    probe.m_taken.insert(L"C:\\target\\.profile");
    REQUIRE(ShareTransferPlan::ChooseFreeName(dir, L".profile", false, probe, out));
    CHECK(out == L".profile(2)");

    probe.m_all = true;
    CHECK_FALSE(ShareTransferPlan::ChooseFreeName(dir, L"a.txt", false, probe, out));
}

TEST_CASE("STP6 上传的顶层同名检查", "[sharetransfer]")
{
    FakeProbe probe;
    probe.m_taken.insert(L"D:\\share\\in\\photos");
    std::vector<std::string> names;
    names.push_back("note.txt");
    CHECK(ShareTransferPlan::FindTopLevelConflict(L"D:\\share\\in", names, probe).empty());
    names.push_back("photos");
    CHECK(ShareTransferPlan::FindTopLevelConflict(L"D:\\share\\in\\", names, probe) == "photos");
}
