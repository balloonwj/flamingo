/**
 *  客户端升级包目录索引单元测试（Suite UPI），test_updatepackageindex.cpp
 *
 *  覆盖两件事：
 *    - BaseDirForScene：仅 file_scene_update 走升级包目录，其余场景（含越界取值）
 *      一律保守回到聊天文件目录；
 *    - UpdatePackageIndex：空目录、基本命中、惰性重扫、文件内容被换掉、若干边界，
 *      以及重扫节流。
 *
 *  这些用例会真的在当前目录下建一个临时目录、往里写文件再删掉 —— 索引的行为
 *  （只扫一层、按大小与修改时间复用 MD5）离开真实文件系统就验不出来。时间由
 *  调用方以 nowMs 传入，故节流相关的断言完全确定、不依赖真实时钟。
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
#include "../fileserversrc/UpdatePackageIndex.h"
#include "../chatserversrc/Protocol.h"   //enum file_scene
#include "../utils/MD5.h"

//本套用例专用的临时目录，与其它几套用的库文件分开，互不干扰
static const char* const UPI_DIR = "flamingo_updatepkg_test";
//本套用例专用的子目录，用来验证"只扫一层"
static const char* const UPI_SUBDIR = "flamingo_updatepkg_test/sub";

//本套用例的时间基准（Unix 毫秒）。节流只看差值，取固定值即可，结果可复现。
static const int64_t UPI_BASE_MS = 1723363200000LL;

//建目录；已存在不算失败
static void makeDir(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
}

//删目录；非空时删不掉，调用方须先清文件
static void removeDir(const char* path)
{
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

//往 UPI_DIR 下写一个文件（内容为 content），返回其完整路径
static std::string writeFile(const std::string& name, const std::string& content)
{
    const std::string path = std::string(UPI_DIR) + "/" + name;
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return path;
}

//算一段内容的 MD5（小写十六进制），用来构造断言里的期望值
static std::string md5Of(const std::string& content)
{
    MD5 md5;
    md5.update(content.data(), content.size());
    return md5.toString();
}

//本套用例建过的文件名，收尾时逐个删掉
static std::vector<std::string> g_createdFiles;

//把临时目录恢复成干净的空目录
static void resetDir()
{
    for (size_t i = 0; i < g_createdFiles.size(); ++i)
        remove(g_createdFiles[i].c_str());
    g_createdFiles.clear();

    remove((std::string(UPI_SUBDIR) + "/inner.zip").c_str());
    removeDir(UPI_SUBDIR);
    removeDir(UPI_DIR);
    makeDir(UPI_DIR);
}

//写文件并登记，便于收尾时清理
static std::string addFile(const std::string& name, const std::string& content)
{
    const std::string path = writeFile(name, content);
    g_createdFiles.push_back(path);
    return path;
}

// ---------------------------------------------------------------------------
// UPI1  场景选目录
// ---------------------------------------------------------------------------

TEST_CASE("UPI1 ResolveDownloadPath 仅升级包场景走索引，其余场景直接拼接")
{
    resetDir();

    const std::string fileDir = "./filecache/";
    const std::string md5     = "9f2c8ab1d4e5f60718293a4b5c6d7e8f";

    //索引里放一个真实的升级包，文件名是人起的、与 MD5 无关
    const std::string content = "upgrade package resolved through index";
    addFile("Flamingo-3.3.0.zip", content);
    UpdatePackageIndex index;
    index.init(UPI_DIR, UPI_BASE_MS);

    std::string path;

    //升级包场景：按内容MD5 经索引反查出人命名的文件
    REQUIRE(ResolveDownloadPath(file_scene_update, md5Of(content), UPI_BASE_MS,
                                fileDir, index, path));
    CHECK(path == std::string(UPI_DIR) + "/Flamingo-3.3.0.zip");

    //升级包场景查不到时如实返回失败，不会退化成拼接出一个不存在的路径
    CHECK_FALSE(ResolveDownloadPath(file_scene_update, md5, UPI_BASE_MS,
                                    fileDir, index, path));
    CHECK(path.empty());

    //聊天文件与群文件：直接拼接，不查索引
    REQUIRE(ResolveDownloadPath(file_scene_chat_file, md5, UPI_BASE_MS, fileDir, index, path));
    CHECK(path == fileDir + md5);
    REQUIRE(ResolveDownloadPath(file_scene_group_file, md5, UPI_BASE_MS, fileDir, index, path));
    CHECK(path == fileDir + md5);

    //头像与图片消息由 imgserver 处理，在 fileserver 上本就会被场景门禁挡掉，
    //这里只确认它们不会误落到升级包目录
    REQUIRE(ResolveDownloadPath(file_scene_avatar, md5, UPI_BASE_MS, fileDir, index, path));
    CHECK(path == fileDir + md5);

    //未知与越界取值一律按"直接拼接"处理，绝不能摸到升级包目录
    REQUIRE(ResolveDownloadPath(0, md5, UPI_BASE_MS, fileDir, index, path));
    CHECK(path == fileDir + md5);
    REQUIRE(ResolveDownloadPath(-1, md5, UPI_BASE_MS, fileDir, index, path));
    CHECK(path == fileDir + md5);
    REQUIRE(ResolveDownloadPath(99, md5, UPI_BASE_MS, fileDir, index, path));
    CHECK(path == fileDir + md5);

    //空 MD5 一律失败
    CHECK_FALSE(ResolveDownloadPath(file_scene_chat_file, "", UPI_BASE_MS, fileDir, index, path));

    resetDir();
}

// ---------------------------------------------------------------------------
// UPI2  空目录与未配置
// ---------------------------------------------------------------------------

TEST_CASE("UPI2 空目录与未配置时查不到且不崩")
{
    resetDir();

    UpdatePackageIndex index;
    index.init(UPI_DIR, UPI_BASE_MS);
    CHECK(index.size() == 0);

    std::string path = "脏值";
    CHECK_FALSE(index.find("9f2c8ab1d4e5f60718293a4b5c6d7e8f", UPI_BASE_MS, path));
    //未命中时出参必须被清空，调用方不会拿到上一次的残留
    CHECK(path.empty());

    //空 MD5 直接判未命中，不去碰磁盘
    CHECK_FALSE(index.find("", UPI_BASE_MS, path));

    //目录未配置：功能整体关闭，任何查询都返回未命中
    UpdatePackageIndex disabled;
    disabled.init("", UPI_BASE_MS);
    CHECK(disabled.size() == 0);
    CHECK_FALSE(disabled.find("9f2c8ab1d4e5f60718293a4b5c6d7e8f", UPI_BASE_MS, path));

    //目录不存在也不算错误，只是扫不出条目
    UpdatePackageIndex missing;
    missing.init("flamingo_updatepkg_not_exist", UPI_BASE_MS);
    CHECK(missing.size() == 0);
}

// ---------------------------------------------------------------------------
// UPI3  基本命中
// ---------------------------------------------------------------------------

TEST_CASE("UPI3 按内容 MD5 命中人命名的文件")
{
    resetDir();

    const std::string contentA = "fake upgrade package A";
    const std::string contentB = "another package with different content";
    addFile("Flamingo-3.3.0.zip", contentA);
    addFile("Flamingo-3.4.0-beta.zip", contentB);

    UpdatePackageIndex index;
    index.init(UPI_DIR, UPI_BASE_MS);
    CHECK(index.size() == 2);

    //文件名是人起的，查询用的却是内容 MD5，这正是本类要搭的那座桥
    std::string path;
    REQUIRE(index.find(md5Of(contentA), UPI_BASE_MS, path));
    CHECK(path == std::string(UPI_DIR) + "/Flamingo-3.3.0.zip");

    REQUIRE(index.find(md5Of(contentB), UPI_BASE_MS, path));
    CHECK(path == std::string(UPI_DIR) + "/Flamingo-3.4.0-beta.zip");

    //大写十六进制的 MD5 同样能查到（令牌里按约定是小写，这里是防御性兼容）
    std::string upper = md5Of(contentA);
    for (size_t i = 0; i < upper.size(); ++i)
    {
        if (upper[i] >= 'a' && upper[i] <= 'z')
            upper[i] = static_cast<char>(upper[i] - 'a' + 'A');
    }
    CHECK(index.find(upper, UPI_BASE_MS, path));
}

// ---------------------------------------------------------------------------
// UPI4  惰性重扫
// ---------------------------------------------------------------------------

TEST_CASE("UPI4 新放进目录的包不必重启服务就能被找到")
{
    resetDir();

    UpdatePackageIndex index;
    index.init(UPI_DIR, UPI_BASE_MS);
    CHECK(index.size() == 0);

    //索引建好之后运维才把包放进来
    const std::string content = "package placed after index was built";
    addFile("Flamingo-3.5.0.zip", content);

    std::string path;

    //距上次扫描不足最小间隔：受节流约束，不重扫，因而查不到
    CHECK_FALSE(index.find(md5Of(content), UPI_BASE_MS + 1000, path));

    //越过最小间隔：触发一次重扫，随即命中
    REQUIRE(index.find(md5Of(content), UPI_BASE_MS + kUpdateIndexRescanMinIntervalMs, path));
    CHECK(path == std::string(UPI_DIR) + "/Flamingo-3.5.0.zip");
    CHECK(index.size() == 1);
}

// ---------------------------------------------------------------------------
// UPI5  文件内容被换掉
// ---------------------------------------------------------------------------

TEST_CASE("UPI5 同名文件内容被换掉后，旧 MD5 失效、新 MD5 生效")
{
    resetDir();

    const std::string oldContent = "old package content";
    addFile("Flamingo-3.3.0.zip", oldContent);

    UpdatePackageIndex index;
    index.init(UPI_DIR, UPI_BASE_MS);

    std::string path;
    REQUIRE(index.find(md5Of(oldContent), UPI_BASE_MS, path));

    //重新打包后覆盖同名文件。新内容长度与旧的不同 —— 修改时间的精度是秒，
    //同一秒内改写时 mtime 可能不变，靠大小变化也能让"文件没变"的判定失效。
    const std::string newContent = "brand new package content, noticeably longer than before";
    writeFile("Flamingo-3.3.0.zip", newContent);

    //强制重扫（运维在控制台点「扫描」走的就是这条路）
    index.rescan(UPI_BASE_MS + 60000);

    CHECK_FALSE(index.find(md5Of(oldContent), UPI_BASE_MS + 60000, path));
    REQUIRE(index.find(md5Of(newContent), UPI_BASE_MS + 60000, path));
    CHECK(path == std::string(UPI_DIR) + "/Flamingo-3.3.0.zip");
    //一个文件只应占一个条目，旧 MD5 的映射要被清掉而不是留着
    CHECK(index.size() == 1);
}

// ---------------------------------------------------------------------------
// UPI6  边界
// ---------------------------------------------------------------------------

TEST_CASE("UPI6 只扫一层；同内容多份与空文件都不出问题")
{
    resetDir();

    //子目录里的文件不参与索引：那里留给运维放归档
    makeDir(UPI_SUBDIR);
    {
        const std::string inner = std::string(UPI_SUBDIR) + "/inner.zip";
        std::ofstream out(inner.c_str(), std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        const std::string innerContent = "archived package in subdirectory";
        out.write(innerContent.data(), static_cast<std::streamsize>(innerContent.size()));
        out.close();
    }

    //同一份内容存了两个文件名：只应占一个 MD5 条目，且指向其中之一
    const std::string dup = "same content stored twice";
    addFile("Flamingo-3.3.0.zip", dup);
    addFile("Flamingo-3.3.0-backup.zip", dup);

    //空文件：MD5 有定义，不该引发异常
    addFile("empty.zip", "");

    UpdatePackageIndex index;
    index.init(UPI_DIR, UPI_BASE_MS);

    //两个同内容文件合成一个条目 + 一个空文件条目 = 2；子目录里那个不算
    CHECK(index.size() == 2);

    std::string path;
    REQUIRE(index.find(md5Of(dup), UPI_BASE_MS, path));
    CHECK((path == std::string(UPI_DIR) + "/Flamingo-3.3.0.zip"
           || path == std::string(UPI_DIR) + "/Flamingo-3.3.0-backup.zip"));

    REQUIRE(index.find(md5Of(""), UPI_BASE_MS, path));
    CHECK(path == std::string(UPI_DIR) + "/empty.zip");

    //子目录里那个包的 MD5 查不到
    CHECK_FALSE(index.find(md5Of("archived package in subdirectory"), UPI_BASE_MS, path));
}

// ---------------------------------------------------------------------------
// UPI7  重扫节流
// ---------------------------------------------------------------------------

TEST_CASE("UPI7 连续未命中不会反复全量重扫")
{
    resetDir();

    UpdatePackageIndex index;
    index.init(UPI_DIR, UPI_BASE_MS);

    std::string path;
    int64_t now = UPI_BASE_MS;

    //第一个包：越过间隔后被扫到
    const std::string first = "first package";
    addFile("first.zip", first);
    now = UPI_BASE_MS + kUpdateIndexRescanMinIntervalMs;
    REQUIRE(index.find(md5Of(first), now, path));

    //紧接着再放一个包。距刚才那次重扫不足最小间隔，期间无论查多少次伪造 MD5，
    //都不该触发重扫 —— 判据就是这个新包在此期间始终查不到。
    const std::string second = "second package";
    addFile("second.zip", second);

    CHECK_FALSE(index.find("00000000000000000000000000000001", now + 1, path));
    CHECK_FALSE(index.find("00000000000000000000000000000002", now + 2, path));
    CHECK_FALSE(index.find("00000000000000000000000000000003", now + 3, path));
    CHECK_FALSE(index.find(md5Of(second), now + 4, path));
    CHECK(index.size() == 1);

    //越过间隔后才会重扫，新包这时才可见
    REQUIRE(index.find(md5Of(second), now + kUpdateIndexRescanMinIntervalMs, path));
    CHECK(index.size() == 2);

    resetDir();
}
