/**
 *  分享者一端写操作单元测试（Suite SWO，标签 [sharewriteops]），ShareWriteOpsTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 2 步，规则见 8.5、9.1）：
 *    - SWO1 新建文件夹：成功；同名已存在（文件或文件夹）807；父目录不存在 809；共享根本身、非法名字 808；
 *           单文件共享 820；
 *    - SWO2 重命名：文件与目录改名；同目录已有同名 807；只改大小写照常执行；名字不变视为成功；共享根本身、
 *           新名字含分隔符或非法 808；源不存在 809；未含隐藏文件时隐藏条目按不存在处理 809；
 *    - SWO3 移动：文件与目录移到另一个目录；移到自己或自己的下级 808；目标已有同名 807；已在目标目录里视为成功；
 *           目标目录不存在全部 809；逐条结果与总结果码；
 *    - SWO4 删除：按全部条目的总字节数（目录递归求和）判定一次能否放进回收站，能放的逐条交给回收站；不能放时
 *           全部 810、一个也不交；不合法与不存在的条目逐条报错，其余照常删除；共享根本身 808；
 *    - SWO5 正在上传：半成品本身按不存在处理；目录之下有半成品时重命名、移动、删除这个目录都回 811；
 *    - SWO6 经目录联接走出共享根的路径一律 808，目录联接里的内容不计入删除的字节数；
 *    - SWO7 路径小工具：上级目录、最后一段、拼接、是否在某目录之下（不区分大小写、按段判断）。
 *  删除用假回收站记录调用，不往真实的回收站里放东西。
 *  本文件由 FlamingoUnit 编译，被测源文件 Engine/ShareWriteOps.cpp 同时编进 FlamingoFileShare.dll。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Engine/ShareFileOps.h"
#include "../Engine/ShareWinPath.h"
#include "../Engine/ShareWriteOps.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的临时目录名（每个用例开始前整个删掉）
const wchar_t* const kSwoTestDir = L"swo_test";

//几个测试文件的字节数
const size_t kFileA = 100;
const size_t kFileB = 2000;
const size_t kFileC = 30000;

//一段 UTF-8 中文：「资料」
const char* const kChineseUtf8 = "\xE8\xB5\x84\xE6\x96\x99";

/**
 *  假回收站：记下判定与删除的调用；可设定判定结果。Recycle 真的删掉条目（模拟进了回收站），便于检查结果。
 */
class FakeRecycler : public IShareRecycler
{
public:
    FakeRecycler()
        : m_checkCode(error_code_ok)
        , m_checkCalls(0)
        , m_checkedBytes(-1)
    {
    }

    virtual int CheckRecyclable(const std::wstring& anyPath, int64_t totalBytes)
    {
        (void)anyPath;
        ++m_checkCalls;
        m_checkedBytes = totalBytes;
        return m_checkCode;
    }

    virtual int Recycle(const std::wstring& absPath)
    {
        m_recycled.push_back(absPath);
        sharetestfs::DeleteTree(absPath);
        return error_code_ok;
    }

    int                       m_checkCode;     //CheckRecyclable 的返回值
    int                       m_checkCalls;    //CheckRecyclable 被调用的次数
    int64_t                   m_checkedBytes;  //最近一次判定的总字节数
    std::vector<std::wstring> m_recycled;      //交给 Recycle 的路径，按调用顺序
};

//本套用例的临时目录（每次调用都先清空再建），返回共享根
std::wstring FreshRoot()
{
    const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSwoTestDir;
    sharetestfs::DeleteTree(base);
    const std::wstring root = base + L"\\root";
    sharetestfs::MakeDirs(root);
    return root;
}

//文件夹共享的写操作上下文
ShareWriteContext FolderCtx(const std::wstring& root, IShareRecycler* recycler)
{
    ShareWriteContext ctx;
    ctx.m_rootPath = root;
    ctx.m_kind = share_kind_folder;
    ctx.m_includeHidden = false;
    ctx.m_recycler = recycler;
    return ctx;
}

bool IsDir(const std::wstring& path)
{
    const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsFile(const std::wstring& path)
{
    const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

//目录里某个条目的真实名字（检查只改大小写的改名是否生效）
std::wstring ActualName(const std::wstring& dir, const std::wstring& nameIgnoringCase)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((dir + L"\\" + nameIgnoringCase).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return std::wstring();
    ::FindClose(h);
    return fd.cFileName;
}

std::vector<std::string> Paths(const char* a, const char* b = NULL, const char* c = NULL)
{
    std::vector<std::string> v(1, a);
    if (b != NULL)
        v.push_back(b);
    if (c != NULL)
        v.push_back(c);
    return v;
}

} // namespace

TEST_CASE("SWO1 make directory", "[fileshare][sharewriteops]")
{
    const std::wstring root = FreshRoot();
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\a.txt", kFileA));
    const ShareWriteContext ctx = FolderCtx(root, NULL);

    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/new") == error_code_ok);
    CHECK(IsDir(root + L"\\docs\\new"));
    CHECK(ShareWriteOps::MakeDirectory(ctx, std::string("docs/") + kChineseUtf8) == error_code_ok);
    CHECK(ShareWriteOps::MakeDirectory(ctx, "top") == error_code_ok);
    CHECK(IsDir(root + L"\\top"));

    //同名已存在：文件夹与文件都不覆盖
    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/new") == error_code_share_name_conflict);
    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/a.txt") == error_code_share_name_conflict);
    CHECK(ShareWriteOps::MakeDirectory(ctx, "DOCS/NEW") == error_code_share_name_conflict);
    //父目录不存在
    CHECK(ShareWriteOps::MakeDirectory(ctx, "nope/new") == error_code_share_path_not_found);
    //父路径是文件
    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/a.txt/x") == error_code_share_path_not_found);
    //共享根本身、非法写法
    CHECK(ShareWriteOps::MakeDirectory(ctx, "") == error_code_share_path_invalid);
    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/../x") == error_code_share_path_invalid);
    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/a:b") == error_code_share_path_invalid);
    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/CON") == error_code_share_path_invalid);
    CHECK(ShareWriteOps::MakeDirectory(ctx, "docs/end.") == error_code_share_path_invalid);

    //单文件共享不支持
    ShareWriteContext fileCtx = ctx;
    fileCtx.m_rootPath = root + L"\\docs\\a.txt";
    fileCtx.m_kind = share_kind_file;
    CHECK(ShareWriteOps::MakeDirectory(fileCtx, "") == error_code_share_kind_mismatch);
}

TEST_CASE("SWO2 rename", "[fileshare][sharewriteops]")
{
    const std::wstring root = FreshRoot();
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs\\sub"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\a.txt", kFileA));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\b.txt", kFileB));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\secret"));
    ::SetFileAttributesW(ShareToLongPath(root + L"\\secret").c_str(), FILE_ATTRIBUTE_HIDDEN);
    const ShareWriteContext ctx = FolderCtx(root, NULL);
    std::string newRel;

    CHECK(ShareWriteOps::Rename(ctx, "docs/a.txt", "c.txt", newRel) == error_code_ok);
    CHECK(newRel == "docs/c.txt");
    CHECK(IsFile(root + L"\\docs\\c.txt"));
    CHECK_FALSE(IsFile(root + L"\\docs\\a.txt"));
    CHECK(ShareWriteOps::Rename(ctx, "docs/sub", kChineseUtf8, newRel) == error_code_ok);
    CHECK(newRel == std::string("docs/") + kChineseUtf8);

    //同目录已有同名
    CHECK(ShareWriteOps::Rename(ctx, "docs/c.txt", "b.txt", newRel) == error_code_share_name_conflict);
    CHECK(newRel.empty());
    CHECK(ShareWriteOps::Rename(ctx, "docs/c.txt", "B.TXT", newRel) == error_code_share_name_conflict);
    //只改大小写
    CHECK(ShareWriteOps::Rename(ctx, "docs/c.txt", "C.txt", newRel) == error_code_ok);
    CHECK(ActualName(root + L"\\docs", L"c.txt") == L"C.txt");
    //名字不变
    CHECK(ShareWriteOps::Rename(ctx, "docs/b.txt", "b.txt", newRel) == error_code_ok);
    CHECK(newRel == "docs/b.txt");

    //共享根本身、新名字含分隔符或非法
    CHECK(ShareWriteOps::Rename(ctx, "", "x", newRel) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Rename(ctx, "docs/b.txt", "a/b", newRel) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Rename(ctx, "docs/b.txt", "a\\b", newRel) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Rename(ctx, "docs/b.txt", "", newRel) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Rename(ctx, "docs/b.txt", "..", newRel) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Rename(ctx, "docs/b.txt", "NUL.txt", newRel) == error_code_share_path_invalid);
    //源不存在；隐藏条目按不存在处理
    CHECK(ShareWriteOps::Rename(ctx, "docs/none.txt", "x.txt", newRel) == error_code_share_path_not_found);
    CHECK(ShareWriteOps::Rename(ctx, "secret", "open", newRel) == error_code_share_path_not_found);
    ShareWriteContext hiddenCtx = ctx;
    hiddenCtx.m_includeHidden = true;
    CHECK(ShareWriteOps::Rename(hiddenCtx, "secret", "open", newRel) == error_code_ok);
}

TEST_CASE("SWO3 move", "[fileshare][sharewriteops]")
{
    const std::wstring root = FreshRoot();
    REQUIRE(sharetestfs::MakeDirs(root + L"\\src\\inner"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dst"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\src\\a.txt", kFileA));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\src\\b.txt", kFileB));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\dst\\b.txt", kFileB));
    const ShareWriteContext ctx = FolderCtx(root, NULL);
    std::vector<ShareOpItem> results;

    //一个成功、一个同名冲突、一个不存在：逐条给出，总结果为第一条失败
    CHECK(ShareWriteOps::Move(ctx, Paths("src/a.txt", "src/b.txt", "src/none.txt"), "dst", results)
          == error_code_share_name_conflict);
    REQUIRE(results.size() == 3);
    CHECK(results[0].m_path == "src/a.txt");
    CHECK(results[0].m_code == error_code_ok);
    CHECK(results[1].m_code == error_code_share_name_conflict);
    CHECK(results[2].m_code == error_code_share_path_not_found);
    CHECK(IsFile(root + L"\\dst\\a.txt"));
    CHECK(IsFile(root + L"\\src\\b.txt"));

    //目录移动
    CHECK(ShareWriteOps::Move(ctx, Paths("src/inner"), "dst", results) == error_code_ok);
    CHECK(IsDir(root + L"\\dst\\inner"));
    //移到自己或自己的下级
    CHECK(ShareWriteOps::Move(ctx, Paths("dst"), "dst/inner", results) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Move(ctx, Paths("dst"), "DST", results) == error_code_share_path_invalid);
    //"dst" 与 "dst2" 不是上下级
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dst2"));
    CHECK(ShareWriteOps::Move(ctx, Paths("dst"), "dst2", results) == error_code_ok);
    CHECK(IsDir(root + L"\\dst2\\dst\\inner"));
    //已经在目标目录里
    CHECK(ShareWriteOps::Move(ctx, Paths("dst2/dst"), "dst2", results) == error_code_ok);
    CHECK(IsDir(root + L"\\dst2\\dst"));
    //移到共享根
    CHECK(ShareWriteOps::Move(ctx, Paths("dst2/dst/a.txt"), "", results) == error_code_ok);
    CHECK(IsFile(root + L"\\a.txt"));
    //目标目录不存在：全部 809
    CHECK(ShareWriteOps::Move(ctx, Paths("a.txt", "src/b.txt"), "nope", results) == error_code_share_path_not_found);
    REQUIRE(results.size() == 2);
    CHECK(results[1].m_code == error_code_share_path_not_found);
    //目标是文件
    CHECK(ShareWriteOps::Move(ctx, Paths("src/b.txt"), "a.txt", results) == error_code_share_path_not_found);
    //共享根本身
    CHECK(ShareWriteOps::Move(ctx, Paths(""), "src", results) == error_code_share_path_invalid);
}

TEST_CASE("SWO4 delete", "[fileshare][sharewriteops]")
{
    const std::wstring root = FreshRoot();
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dir\\deep"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\dir\\a.txt", kFileA));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\dir\\deep\\b.txt", kFileB));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\c.txt", kFileC));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\keep.txt", kFileA));
    std::vector<ShareOpItem> results;

    //判定不通过：全部 810、一个也不交给回收站
    FakeRecycler refuse;
    refuse.m_checkCode = error_code_share_recycle_unavailable;
    ShareWriteContext ctx = FolderCtx(root, &refuse);
    CHECK(ShareWriteOps::Delete(ctx, Paths("dir", "c.txt"), results) == error_code_share_recycle_unavailable);
    REQUIRE(results.size() == 2);
    CHECK(results[0].m_code == error_code_share_recycle_unavailable);
    CHECK(results[1].m_code == error_code_share_recycle_unavailable);
    CHECK(refuse.m_checkCalls == 1);
    CHECK(refuse.m_checkedBytes == static_cast<int64_t>(kFileA + kFileB + kFileC));
    CHECK(refuse.m_recycled.empty());
    CHECK(IsDir(root + L"\\dir"));

    //判定通过：合法的逐条交给回收站，不合法与不存在的逐条报错
    FakeRecycler accept;
    ctx.m_recycler = &accept;
    CHECK(ShareWriteOps::Delete(ctx, Paths("dir", "none.txt", "c.txt"), results) == error_code_share_path_not_found);
    REQUIRE(results.size() == 3);
    CHECK(results[0].m_code == error_code_ok);
    CHECK(results[1].m_code == error_code_share_path_not_found);
    CHECK(results[2].m_code == error_code_ok);
    CHECK(accept.m_checkCalls == 1);
    CHECK(accept.m_checkedBytes == static_cast<int64_t>(kFileA + kFileB + kFileC));
    REQUIRE(accept.m_recycled.size() == 2);
    CHECK(accept.m_recycled[0] == root + L"\\dir");
    CHECK(accept.m_recycled[1] == root + L"\\c.txt");
    CHECK_FALSE(IsDir(root + L"\\dir"));
    CHECK(IsFile(root + L"\\keep.txt"));

    //共享根本身、非法写法：不判定、不删除
    FakeRecycler untouched;
    ctx.m_recycler = &untouched;
    CHECK(ShareWriteOps::Delete(ctx, Paths("", "../x"), results) == error_code_share_path_invalid);
    CHECK(results[1].m_code == error_code_share_path_invalid);
    CHECK(untouched.m_checkCalls == 0);
    CHECK(untouched.m_recycled.empty());

    //没有回收站：按不能放进回收站处理
    ctx.m_recycler = NULL;
    CHECK(ShareWriteOps::Delete(ctx, Paths("keep.txt"), results) == error_code_share_recycle_unavailable);
    CHECK(IsFile(root + L"\\keep.txt"));
}

TEST_CASE("SWO5 entries busy with an upload", "[fileshare][sharewriteops]")
{
    const std::wstring root = FreshRoot();
    REQUIRE(sharetestfs::MakeDirs(root + L"\\up\\inner"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dst"));
    const std::wstring part = root + L"\\up\\inner\\big.bin.part";
    REQUIRE(sharetestfs::WriteFileBytes(part, kFileA));
    CShareUploadRegistry::Instance().Add(part);
    FakeRecycler recycler;
    const ShareWriteContext ctx = FolderCtx(root, &recycler);
    std::string newRel;
    std::vector<ShareOpItem> results;

    //半成品本身按不存在处理
    CHECK(ShareWriteOps::Rename(ctx, "up/inner/big.bin.part", "x", newRel) == error_code_share_path_not_found);
    //目录之下有半成品：重命名、移动、删除都拒绝
    CHECK(ShareWriteOps::Rename(ctx, "up", "up2", newRel) == error_code_share_io_fail);
    CHECK(ShareWriteOps::Rename(ctx, "up/inner", "inner2", newRel) == error_code_share_io_fail);
    CHECK(ShareWriteOps::Move(ctx, Paths("up"), "dst", results) == error_code_share_io_fail);
    CHECK(ShareWriteOps::Delete(ctx, Paths("up"), results) == error_code_share_io_fail);
    CHECK(recycler.m_recycled.empty());
    //别的目录不受影响；上传结束后照常
    CHECK(ShareWriteOps::Rename(ctx, "dst", "dst2", newRel) == error_code_ok);
    CShareUploadRegistry::Instance().Remove(part);
    CHECK(ShareWriteOps::Rename(ctx, "up", "up2", newRel) == error_code_ok);
}

TEST_CASE("SWO6 junctions", "[fileshare][sharewriteops]")
{
    const std::wstring root = FreshRoot();
    const std::wstring outside = sharetestfs::ExeDir() + L"\\" + kSwoTestDir + L"\\outside";
    REQUIRE(sharetestfs::MakeDirs(outside));
    REQUIRE(sharetestfs::WriteFileBytes(outside + L"\\o.txt", kFileC));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dir"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\dir\\a.txt", kFileA));
    REQUIRE(sharetestfs::MakeJunction(root + L"\\dir\\link", outside, sharetestfs::ExeDir() + L"\\" + kSwoTestDir + L"\\jtmp"));
    FakeRecycler recycler;
    const ShareWriteContext ctx = FolderCtx(root, &recycler);
    std::string newRel;
    std::vector<ShareOpItem> results;

    //经联接走出共享根
    CHECK(ShareWriteOps::MakeDirectory(ctx, "dir/link/new") == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Rename(ctx, "dir/link/o.txt", "p.txt", newRel) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Delete(ctx, Paths("dir/link/o.txt"), results) == error_code_share_path_invalid);
    CHECK(ShareWriteOps::Move(ctx, Paths("dir/a.txt"), "dir/link", results) == error_code_share_path_invalid);
    CHECK(IsFile(outside + L"\\o.txt"));

    //删除含联接的目录：联接里的内容不计入字节数
    CHECK(ShareWriteOps::Delete(ctx, Paths("dir"), results) == error_code_ok);
    CHECK(recycler.m_checkedBytes == static_cast<int64_t>(kFileA));
    CHECK(IsFile(outside + L"\\o.txt"));
}

TEST_CASE("SWO7 relative path helpers", "[fileshare][sharewriteops]")
{
    CHECK(ShareWriteOps::ParentOf("a/b/c") == "a/b");
    CHECK(ShareWriteOps::ParentOf("a") == "");
    CHECK(ShareWriteOps::LastSegment("a/b/c") == "c");
    CHECK(ShareWriteOps::LastSegment("a") == "a");
    CHECK(ShareWriteOps::Join("", "x") == "x");
    CHECK(ShareWriteOps::Join("a/b", "x") == "a/b/x");
    CHECK(ShareWriteOps::IsSameOrUnder("a/b", "a"));
    CHECK(ShareWriteOps::IsSameOrUnder("A/B", "a/b"));
    CHECK(ShareWriteOps::IsSameOrUnder("anything", ""));
    CHECK_FALSE(ShareWriteOps::IsSameOrUnder("ab", "a"));
    CHECK_FALSE(ShareWriteOps::IsSameOrUnder("a", "a/b"));
    CHECK(ShareWriteOps::EqualsIgnoreCase(std::string(kChineseUtf8) + "A", std::string(kChineseUtf8) + "a"));
    CHECK_FALSE(ShareWriteOps::EqualsIgnoreCase("a", "b"));
    CHECK(ShareWriteOps::EqualsIgnoreCase("", ""));
}
