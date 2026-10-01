/**
 *  ImageCacheStoreTests.cpp
 *
 *  本地图片缓存落地写入（net::StoreImageToCache）的测试。
 *
 *  背景：2026-09-30 发现发送聊天图片成功后，图片从未写进本地缓存
 *  Users\<账号>\ChatImage\<md5>，消息记录窗只认这个缓存，于是自己发出去的图片
 *  在消息记录窗里一律显示「需要下载」。修复是发送成功后把内存里的图片字节写进缓存，
 *  写入这一步即本文件所测的函数。
 *
 *  读取侧只判断文件存在、大小一致就认定它完整，所以本组用例重点钉住：写完不留临时
 *  文件；已有同大小的文件不重写；大小不符的残留会被覆盖；参数无效时不创建任何文件。
 *
 *  用例在系统临时目录下建一个带进程 id 的子目录，避免两个测试进程同时运行时互相
 *  干扰；每条用例结束时删掉自己用到的文件。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../net/ImageCacheStore.h"

#include <string>
#include <vector>

namespace {

// 测试用的图片字节（内容不必是真图片：本函数只负责落地，不解码）。
const char kSampleBytes[] = "flamingo image cache store sample bytes";
// 与 kSampleBytes 等长、内容不同的一段字节，用来模拟"缓存里已有同大小文件"。
const char kSameSizeOtherBytes[] = "FLAMINGO IMAGE CACHE STORE SAMPLE BYTES";
// 比 kSampleBytes 短的一段字节，用来模拟"写了一半的残留文件"。
const char kHalfWrittenBytes[] = "flamingo image";

// 取字面量数组的内容为 std::string（不含结尾的 \0）。
template <size_t N>
std::string BytesOf(const char (&arr)[N])
{
    return std::string(arr, N - 1);
}

// 本进程专用的临时目录（以反斜杠结尾），首次调用时创建。
std::wstring TestDir()
{
    wchar_t tmp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = tmp;
    dir += L"flamingo_ics_";
    dir += std::to_wstring((unsigned long long)::GetCurrentProcessId());
    dir += L"\\";
    ::CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

// 用例用到的目标文件路径；name 用于区分不同用例。
std::wstring TargetPath(const wchar_t* name)
{
    return TestDir() + name;
}

// 删除目标文件及其临时文件。
void RemoveTarget(const std::wstring& path)
{
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + net::kImageCacheStoringSuffix).c_str());
}

// 文件是否存在（且不是目录）。
bool FileExists(const std::wstring& path)
{
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 把整段字节写成文件（覆盖），用来预先摆好"缓存里已有的文件"；成功返回 true。
bool WriteRaw(const std::wstring& path, const std::string& bytes)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0;
    const bool ok = ::WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, NULL) != FALSE
                    && written == (DWORD)bytes.size();
    ::CloseHandle(h);
    return ok;
}

// 读回整个文件；文件不存在时返回空串。
std::string ReadRaw(const std::wstring& path)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return std::string();
    }
    const DWORD size = ::GetFileSize(h, NULL);
    std::string out;
    if (size != INVALID_FILE_SIZE && size > 0)
    {
        out.resize(size);
        DWORD read = 0;
        ::ReadFile(h, &out[0], size, &read, NULL);
        out.resize(read);
    }
    ::CloseHandle(h);
    return out;
}

} // namespace

TEST_CASE("ICS1 目标文件不存在：写入成功，内容逐字节一致", "[imgcache]")
{
    const std::wstring path = TargetPath(L"ics1");
    RemoveTarget(path);

    REQUIRE(net::StoreImageToCache(path, BytesOf(kSampleBytes)));
    CHECK(ReadRaw(path) == BytesOf(kSampleBytes));

    RemoveTarget(path);
}

TEST_CASE("ICS2 写入完成后不留下临时文件", "[imgcache]")
{
    const std::wstring path = TargetPath(L"ics2");
    RemoveTarget(path);

    REQUIRE(net::StoreImageToCache(path, BytesOf(kSampleBytes)));
    CHECK(FileExists(path));
    CHECK_FALSE(FileExists(path + net::kImageCacheStoringSuffix));

    RemoveTarget(path);
}

TEST_CASE("ICS3 已有同大小的文件：跳过不重写，返回成功", "[imgcache]")
{
    const std::wstring path = TargetPath(L"ics3");
    RemoveTarget(path);

    //缓存按内容 MD5 命名，同名同大小即视为同一内容。这里故意放一份等长但内容不同的
    //文件：调用后内容若仍是它，就证明函数确实跳过了、没有重写。
    REQUIRE(WriteRaw(path, BytesOf(kSameSizeOtherBytes)));
    REQUIRE(BytesOf(kSameSizeOtherBytes).size() == BytesOf(kSampleBytes).size());

    CHECK(net::StoreImageToCache(path, BytesOf(kSampleBytes)));
    CHECK(ReadRaw(path) == BytesOf(kSameSizeOtherBytes));
    CHECK_FALSE(FileExists(path + net::kImageCacheStoringSuffix));

    RemoveTarget(path);
}

TEST_CASE("ICS4 已有文件但大小不符：用新数据覆盖", "[imgcache]")
{
    const std::wstring path = TargetPath(L"ics4");
    RemoveTarget(path);

    //模拟早先写了一半留下的残留：读取侧按大小比对会判它不完整，这里必须覆盖掉。
    REQUIRE(WriteRaw(path, BytesOf(kHalfWrittenBytes)));

    CHECK(net::StoreImageToCache(path, BytesOf(kSampleBytes)));
    CHECK(ReadRaw(path) == BytesOf(kSampleBytes));
    CHECK_FALSE(FileExists(path + net::kImageCacheStoringSuffix));

    RemoveTarget(path);
}

TEST_CASE("ICS5 路径为空或数据为空：返回失败，不创建任何文件", "[imgcache]")
{
    CHECK_FALSE(net::StoreImageToCache(std::wstring(), BytesOf(kSampleBytes)));

    const std::wstring path = TargetPath(L"ics5");
    RemoveTarget(path);

    CHECK_FALSE(net::StoreImageToCache(path, std::string()));
    CHECK_FALSE(FileExists(path));
    CHECK_FALSE(FileExists(path + net::kImageCacheStoringSuffix));

    RemoveTarget(path);
}
