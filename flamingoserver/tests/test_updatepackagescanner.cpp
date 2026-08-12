/**
 *  客户端升级包目录扫描器单元测试（Suite UPS），test_updatepackagescanner.cpp
 *
 *  覆盖 ScanUpdatePackages 的五条行为：空目录与目录不存在、列出文件名与大小与 MD5、
 *  zip 魔数识别、只扫一层、同内容不同名的文件都列出（与索引不同，扫描不去重）。
 *
 *  这些用例会真的在当前目录下建一个临时目录、往里写文件再删掉 —— 扫描器的行为
 *  离开真实文件系统就验不出来。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include <stdio.h>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "doctest.h"
#include "../chatserversrc/UpdatePackageScanner.h"
#include "../utils/MD5.h"

//本套用例专用的临时目录，与 UPI 套用的那个分开，避免互相干扰
static const char* const UPS_DIR = "flamingo_updatescan_test";
//子目录，用来验证"只扫一层"
static const char* const UPS_SUBDIR = "flamingo_updatescan_test/archive";

//一个最小的合法 zip 文件头：前四字节是本地文件头魔数 PK\x03\x04。
//扫描器只看这四个字节，后面跟什么都不影响 iszip 的判定。
static const char ZIP_HEAD[4] = { 'P', 'K', '\x03', '\x04' };

static void makeDir(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
}

static void removeDir(const char* path)
{
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

//本套用例建过的文件，收尾时逐个删掉
static std::vector<std::string> g_upsFiles;

//往指定目录写一个文件
static std::string writeFileTo(const std::string& dir, const std::string& name,
                               const std::string& content)
{
    const std::string path = dir + "/" + name;
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    if (!content.empty())
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return path;
}

//往扫描目录写一个文件并登记，便于收尾清理
static std::string addFile(const std::string& name, const std::string& content)
{
    const std::string path = writeFileTo(UPS_DIR, name, content);
    g_upsFiles.push_back(path);
    return path;
}

//构造一份"看起来像 zip"的内容：以魔数开头，后面随便跟点东西
static std::string zipLike(const std::string& tail)
{
    return std::string(ZIP_HEAD, sizeof(ZIP_HEAD)) + tail;
}

static std::string md5Of(const std::string& content)
{
    MD5 md5;
    md5.update(content.data(), content.size());
    return md5.toString();
}

//把临时目录恢复成干净的空目录
static void resetScanDir()
{
    for (size_t i = 0; i < g_upsFiles.size(); ++i)
        remove(g_upsFiles[i].c_str());
    g_upsFiles.clear();

    remove((std::string(UPS_SUBDIR) + "/old.zip").c_str());
    removeDir(UPS_SUBDIR);
    removeDir(UPS_DIR);
    makeDir(UPS_DIR);
}

//在结果列表里按文件名找一项；找不到返回 NULL
static const UpdatePackageCandidate* findByName(
    const std::vector<UpdatePackageCandidate>& list, const std::string& name)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].filename == name)
            return &list[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// UPS1  空目录与目录不存在
// ---------------------------------------------------------------------------

TEST_CASE("UPS1 空目录、目录不存在、未配置都返回空列表且不报错")
{
    resetScanDir();

    std::vector<UpdatePackageCandidate> list;
    list.resize(3);   //先塞点脏值，确认出参被清空

    ScanUpdatePackages(UPS_DIR, list);
    CHECK(list.empty());

    ScanUpdatePackages("flamingo_updatescan_not_exist", list);
    CHECK(list.empty());

    //未配置升级包目录：同样是空列表，不是错误
    ScanUpdatePackages("", list);
    CHECK(list.empty());
}

// ---------------------------------------------------------------------------
// UPS2  列出文件名、大小与 MD5
// ---------------------------------------------------------------------------

TEST_CASE("UPS2 列出候选包的文件名、字节数与内容 MD5")
{
    resetScanDir();

    const std::string a = zipLike("package for version 3.3.0");
    const std::string b = zipLike("package for version 3.4.0, a bit longer than the first one");
    addFile("Flamingo-3.3.0.zip", a);
    addFile("Flamingo-3.4.0.zip", b);

    std::vector<UpdatePackageCandidate> list;
    ScanUpdatePackages(UPS_DIR, list);
    REQUIRE(list.size() == 2);

    //按文件名字典序排列，换台机器结果一致
    CHECK(list[0].filename == "Flamingo-3.3.0.zip");
    CHECK(list[1].filename == "Flamingo-3.4.0.zip");

    //MD5 与字节数由扫描器算出，控制台据此填进版本记录，无需人工输入
    CHECK(list[0].md5 == md5Of(a));
    CHECK(list[0].size == static_cast<int64_t>(a.size()));
    CHECK(list[1].md5 == md5Of(b));
    CHECK(list[1].size == static_cast<int64_t>(b.size()));
}

// ---------------------------------------------------------------------------
// UPS3  zip 魔数识别
// ---------------------------------------------------------------------------

TEST_CASE("UPS3 按文件头魔数标出哪些不是 zip")
{
    resetScanDir();

    addFile("good.zip", zipLike("a real looking package"));
    //放错文件的典型：把说明文档或 exe 丢了进来
    addFile("readme.txt", "this is not a zip file at all");
    //只有三个字节，连魔数都读不满
    addFile("truncated.zip", "PK\x03");
    //空文件
    addFile("empty.zip", "");

    std::vector<UpdatePackageCandidate> list;
    ScanUpdatePackages(UPS_DIR, list);
    REQUIRE(list.size() == 4);

    const UpdatePackageCandidate* good = findByName(list, "good.zip");
    REQUIRE(good != NULL);
    CHECK(good->iszip);

    //不是 zip 的照样列出来 —— 管理员需要看到目录里真实有什么，才能发现自己放错了
    const UpdatePackageCandidate* txt = findByName(list, "readme.txt");
    REQUIRE(txt != NULL);
    CHECK_FALSE(txt->iszip);

    const UpdatePackageCandidate* truncated = findByName(list, "truncated.zip");
    REQUIRE(truncated != NULL);
    CHECK_FALSE(truncated->iszip);

    const UpdatePackageCandidate* empty = findByName(list, "empty.zip");
    REQUIRE(empty != NULL);
    CHECK_FALSE(empty->iszip);
    CHECK(empty->size == 0);
    //空文件的 MD5 有定义，不该因此被丢掉
    CHECK(empty->md5 == md5Of(""));
}

// ---------------------------------------------------------------------------
// UPS4  只扫一层
// ---------------------------------------------------------------------------

TEST_CASE("UPS4 子目录里的文件不参与扫描")
{
    resetScanDir();

    addFile("current.zip", zipLike("current package"));

    //归档目录里放一个旧包，不应出现在候选列表里
    makeDir(UPS_SUBDIR);
    writeFileTo(UPS_SUBDIR, "old.zip", zipLike("archived old package"));

    std::vector<UpdatePackageCandidate> list;
    ScanUpdatePackages(UPS_DIR, list);

    REQUIRE(list.size() == 1);
    CHECK(list[0].filename == "current.zip");
    CHECK(findByName(list, "old.zip") == NULL);
    //子目录本身也不能被当成一个候选项
    CHECK(findByName(list, "archive") == NULL);
}

// ---------------------------------------------------------------------------
// UPS5  同内容不去重
// ---------------------------------------------------------------------------

TEST_CASE("UPS5 同内容不同名的文件都列出来，不做去重")
{
    resetScanDir();

    //与 fileserver 侧的索引不同：索引是"按 MD5 找一个文件"，故同内容只留一个；
    //扫描是给人看的，两份都要列出来，管理员才能自己决定清理哪一个。
    const std::string same = zipLike("identical package content");
    addFile("Flamingo-3.3.0.zip", same);
    addFile("Flamingo-3.3.0-backup.zip", same);

    std::vector<UpdatePackageCandidate> list;
    ScanUpdatePackages(UPS_DIR, list);

    REQUIRE(list.size() == 2);
    CHECK(list[0].md5 == list[1].md5);
    CHECK(findByName(list, "Flamingo-3.3.0.zip") != NULL);
    CHECK(findByName(list, "Flamingo-3.3.0-backup.zip") != NULL);

    resetScanDir();
}
