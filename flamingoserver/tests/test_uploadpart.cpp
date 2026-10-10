/**
 *  上传分片文件命名与收尾策略单元测试（Suite UPP），test_uploadpart.cpp
 *
 *  原先的问题是：同一个文件被多个连接同时上传时，几个会话共用 <md5>.part，
 *  先收齐的会话改名失败，客户端收到「finalize file failed」。本套用例覆盖：
 *    - UPP1 公共分片登记表：同一 md5 只允许一个会话占用，释放后才能再占；不同 md5
 *      互不影响；多线程同时争抢时只有一个成功；
 *    - UPP2 分片路径：占到登记用 <md5>.part，没占到用带序号的私有分片；序号互不相同；
 *    - UPP3 收尾改名的三种结果，含「改名失败但最终文件已存在」按成功处理；
 *    - UPP4 复现多个连接同时上传同一文件的场景：一个会话打开着公共分片写到一半，另一个会话用私有
 *      分片先收齐并改名成功；前者随后收齐，也按成功收尾。
 *
 *  UPP3 / UPP4 会真的在当前目录下建一个临时目录、写文件再删掉 —— 改名与文件占用的
 *  行为离开真实文件系统验不出来。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <stdio.h>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <iterator>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "doctest.h"
#include "../fileserversrc/UploadPartPolicy.h"

//本套用例专用的临时目录（相对当前目录），与其它几套用例的文件分开，互不干扰
static const char* const UPP_DIR = "flamingo_uploadpart_test";
//本套用例用到的文件 MD5 串。策略本身不校验内容，取一个固定串即可
static const char* const UPP_MD5 = "0123456789abcdef0123456789abcdef";
//UPP1 多线程争抢时启动的线程数
static const int UPP_RACE_THREADS = 8;
//UPP4 模拟的上传文件长度（字节），分两半写入以模拟「写到一半」
static const size_t UPP_CONTENT_BYTES = 4096;

//建目录；已存在不算失败
static void uppMakeDir(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
}

//删目录；非空时删不掉，调用方须先删文件
static void uppRemoveDir(const char* path)
{
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

//临时目录路径，带结尾分隔符，与 FileSession::m_strFileBaseDir 的约定一致
static std::string uppBaseDir()
{
    return std::string(UPP_DIR) + "/";
}

//以二进制方式整体写入一个文件（覆盖已有内容）
static void uppWriteFile(const std::string& path, const std::string& content)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

//以二进制方式整体读出一个文件；文件不存在时返回空串
static std::string uppReadFile(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

//文件是否存在
static bool uppFileExists(const std::string& path)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

//生成一段确定的测试内容，每个字节取值随下标变化，便于发现错位与截断
static std::string uppMakeContent(size_t bytes)
{
    std::string s(bytes, '\0');
    for (size_t i = 0; i < bytes; ++i)
        s[i] = static_cast<char>('a' + (i % 26));
    return s;
}

//多线程争抢用例的线程函数：等发令信号后尝试占用同一 md5，成功则计数加一
static void uppRaceWorker(fileserver::UploadPartRegistry* registry,
                          std::atomic<bool>* go, std::atomic<int>* wins)
{
    while (!go->load())
        std::this_thread::yield();
    if (registry->claim(UPP_MD5))
        wins->fetch_add(1);
}

TEST_CASE("UPP1 公共分片登记表：同一 md5 只允许一个会话占用")
{
    fileserver::UploadPartRegistry registry;

    SUBCASE("占用后再占失败，释放后可再占")
    {
        CHECK(registry.claim(UPP_MD5));
        CHECK(registry.isClaimed(UPP_MD5));
        CHECK_FALSE(registry.claim(UPP_MD5));
        registry.release(UPP_MD5);
        CHECK_FALSE(registry.isClaimed(UPP_MD5));
        CHECK(registry.claim(UPP_MD5));
    }

    SUBCASE("大小写不同的同一 md5 视为同一项")
    {
        CHECK(registry.claim("ABCDEF0123456789ABCDEF0123456789"));
        CHECK_FALSE(registry.claim("abcdef0123456789abcdef0123456789"));
        registry.release("abcdef0123456789ABCDEF0123456789");
        CHECK_FALSE(registry.isClaimed("ABCDEF0123456789ABCDEF0123456789"));
    }

    SUBCASE("不同 md5 互不影响；释放未占用的 md5 不出错")
    {
        CHECK(registry.claim(UPP_MD5));
        CHECK(registry.claim("ffffffffffffffffffffffffffffffff"));
        registry.release("00000000000000000000000000000000");
        CHECK(registry.isClaimed(UPP_MD5));
        CHECK(registry.isClaimed("ffffffffffffffffffffffffffffffff"));
    }

    SUBCASE("多线程同时争抢同一 md5，只有一个成功")
    {
        std::atomic<bool> go(false);
        std::atomic<int> wins(0);
        std::vector<std::thread> threads;
        for (int i = 0; i < UPP_RACE_THREADS; ++i)
            threads.push_back(std::thread(uppRaceWorker, &registry, &go, &wins));
        go.store(true);
        for (size_t i = 0; i < threads.size(); ++i)
            threads[i].join();
        CHECK(wins.load() == 1);
    }
}

TEST_CASE("UPP2 占到登记用公共分片，没占到用带序号的私有分片")
{
    CHECK(fileserver::MakeUploadPartPath("dir/", "abc", true, 7) == "dir/abc.part");
    CHECK(fileserver::MakeUploadPartPath("dir/", "abc", false, 7) == "dir/abc.7.part");

    //序号从 1 起、每次不同，保证两个私有分片不会落到同一个文件上
    uint64_t t1 = fileserver::NextUploadPartTag();
    uint64_t t2 = fileserver::NextUploadPartTag();
    CHECK(t1 > 0);
    CHECK(t2 > 0);
    CHECK(t1 != t2);
    CHECK(fileserver::MakeUploadPartPath("dir/", "abc", false, t1)
          != fileserver::MakeUploadPartPath("dir/", "abc", false, t2));
}

TEST_CASE("UPP3 收尾改名的三种结果")
{
    uppMakeDir(UPP_DIR);
    const std::string base = uppBaseDir();
    const std::string finalPath = base + UPP_MD5;
    const std::string partPath = base + UPP_MD5 + ".part";
    const std::string content = "upload part content";
    remove(finalPath.c_str());
    remove(partPath.c_str());

    SUBCASE("最终文件不存在：改名成功")
    {
        uppWriteFile(partPath, content);
        CHECK(fileserver::FinalizeUploadPart(partPath, finalPath, false)
              == fileserver::upload_finalize_renamed);
        CHECK_FALSE(uppFileExists(partPath));
        CHECK(uppReadFile(finalPath) == content);
    }

    SUBCASE("调用方已知最终文件存在：不改名，删除分片")
    {
        uppWriteFile(finalPath, content);
        uppWriteFile(partPath, content);
        CHECK(fileserver::FinalizeUploadPart(partPath, finalPath, true)
              == fileserver::upload_finalize_existed);
        CHECK_FALSE(uppFileExists(partPath));
        CHECK(uppReadFile(finalPath) == content);
    }

    SUBCASE("调用方以为不存在、实际已被别的会话生成：按成功处理")
    {
        //Windows 上目标已存在时 rename 失败，须识别为 existed；Linux 上 rename 直接覆盖，
        //结果为 renamed。两种结果都算成功，分片都不应残留
        uppWriteFile(finalPath, content);
        uppWriteFile(partPath, content);
        fileserver::upload_finalize_result r =
            fileserver::FinalizeUploadPart(partPath, finalPath, false);
        CHECK(r != fileserver::upload_finalize_failed);
#ifdef _WIN32
        CHECK(r == fileserver::upload_finalize_existed);
#endif
        CHECK_FALSE(uppFileExists(partPath));
        CHECK(uppReadFile(finalPath) == content);
    }

    SUBCASE("分片与最终文件都不存在：失败")
    {
        CHECK(fileserver::FinalizeUploadPart(partPath, finalPath, false)
              == fileserver::upload_finalize_failed);
        CHECK_FALSE(uppFileExists(finalPath));
    }

    remove(finalPath.c_str());
    remove(partPath.c_str());
    uppRemoveDir(UPP_DIR);
}

TEST_CASE("UPP4 两个会话同时上传同一文件，先收齐与后收齐的都成功")
{
    uppMakeDir(UPP_DIR);
    const std::string base = uppBaseDir();
    const std::string finalPath = base + UPP_MD5;
    const std::string content = uppMakeContent(UPP_CONTENT_BYTES);
    const size_t half = UPP_CONTENT_BYTES / 2;
    remove(finalPath.c_str());

    fileserver::UploadPartRegistry registry;

    //会话 A 先到：占到公共分片，写到一半，句柄保持打开（对应客户端还在传）
    REQUIRE(registry.claim(UPP_MD5));
    const std::string pathA = fileserver::MakeUploadPartPath(base, UPP_MD5, true, 0);
    FILE* fpA = fopen(pathA.c_str(), "wb");
    REQUIRE(fpA != NULL);
    REQUIRE(fwrite(content.data(), 1, half, fpA) == half);
    fflush(fpA);

    //会话 B 后到：占不到公共分片，改用私有分片，并且先于 A 收齐
    CHECK_FALSE(registry.claim(UPP_MD5));
    const std::string pathB = fileserver::MakeUploadPartPath(base, UPP_MD5, false,
                                                             fileserver::NextUploadPartTag());
    CHECK(pathB != pathA);
    uppWriteFile(pathB, content);

#ifdef _WIN32
    //同一文件被多个连接同时上传时改名失败的根因：Windows 上别的句柄以写方式打开着的文件无法改名。
    //原先 B 与 A 共用 pathA，B 收尾时走的正是这一步
    CHECK(rename(pathA.c_str(), finalPath.c_str()) != 0);
#endif

    //B 收尾：私有分片改名成功
    CHECK(fileserver::FinalizeUploadPart(pathB, finalPath, false)
          == fileserver::upload_finalize_renamed);
    CHECK(uppReadFile(finalPath) == content);
    CHECK_FALSE(uppFileExists(pathB));

    //A 收齐后收尾：最终文件已由 B 生成，A 丢弃自己的分片，同样按成功处理
    REQUIRE(fwrite(content.data() + half, 1, UPP_CONTENT_BYTES - half, fpA)
            == UPP_CONTENT_BYTES - half);
    fclose(fpA);
    CHECK(fileserver::FinalizeUploadPart(pathA, finalPath, true)
          == fileserver::upload_finalize_existed);
    CHECK_FALSE(uppFileExists(pathA));
    CHECK(uppReadFile(finalPath) == content);
    registry.release(UPP_MD5);
    CHECK_FALSE(registry.isClaimed(UPP_MD5));

    remove(pathA.c_str());
    remove(pathB.c_str());
    remove(finalPath.c_str());
    uppRemoveDir(UPP_DIR);
}
