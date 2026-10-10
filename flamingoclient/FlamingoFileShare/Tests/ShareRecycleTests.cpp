/**
 *  回收站判定单元测试（Suite SRY，标签 [sharerecycle]），ShareRecycleTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 2 期第 2 步，规则依据 8.5 的实证）：
 *    - SRY1 系统默认的回收站容量：前 40 GiB 取 10%、其余取 5%，与本机两个卷注册表里的实际值吻合；
 *    - SRY2 判定规则：只有本机固定盘、没设为直接删除、总字节数不超过容量时才能放进回收站，其余一律 810；
 *    - SRY3 查本机卷：测试程序所在卷是固定盘、容量大于 0；UNC 路径判为网络盘、不能放进回收站。
 *  只读系统信息，不往回收站里放任何东西（真实的删除由第 2 期的联调用例覆盖）。
 *  本文件由 FlamingoUnit 编译，被测源文件 Engine/ShareRecycle.cpp 同时编进 FlamingoFileShare.dll。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>

#include "../Engine/ShareRecycle.h"
#include "../../Source/net/Protocol.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//1 MiB 与 1 GiB，单位：字节
const int64_t kMiB = 1024LL * 1024;
const int64_t kGiB = 1024LL * kMiB;

//实测的两个卷：卷容量（字节）与注册表里的回收站容量（MB），见方案 8.5
const int64_t kVolumeEBytes = 549754761216LL;
const int64_t kVolumeERegistryMb = 28262;
const int64_t kVolumeCBytes = 321255370752LL;
const int64_t kVolumeCRegistryMb = 17366;

//估算值与注册表值允许相差的 MB 数（注册表值是系统按 MB 取整后的结果）
const int64_t kToleranceMb = 1;

//小于 40 GiB 的卷：20 GiB
const int64_t kSmallVolumeBytes = 20 * kGiB;

//判定用的回收站容量与总字节数
const int64_t kCapacity = 100 * kMiB;

//一个本机不存在的 UNC 路径；只做字符串层面的卷判定，不会真的访问网络
const wchar_t* const kUncPath = L"\\\\fileshare-test-host\\share\\a.txt";

int64_t AbsDiff(int64_t a, int64_t b)
{
    return (a > b) ? a - b : b - a;
}

ShareVolumeRecycleInfo FixedVolume(int64_t capacity)
{
    ShareVolumeRecycleInfo info;
    info.m_driveType = DRIVE_FIXED;
    info.m_capacityBytes = capacity;
    return info;
}

} // namespace

TEST_CASE("SRY1 default recycle bin capacity", "[fileshare][sharerecycle]")
{
    CHECK(AbsDiff(ShareRecyclePolicy::DefaultCapacityBytes(kVolumeEBytes) / kMiB, kVolumeERegistryMb) <= kToleranceMb);
    CHECK(AbsDiff(ShareRecyclePolicy::DefaultCapacityBytes(kVolumeCBytes) / kMiB, kVolumeCRegistryMb) <= kToleranceMb);
    CHECK(ShareRecyclePolicy::DefaultCapacityBytes(kSmallVolumeBytes) == kSmallVolumeBytes / kShareRecycleFirstTierDivisor);
    CHECK(ShareRecyclePolicy::DefaultCapacityBytes(0) == 0);
    CHECK(ShareRecyclePolicy::DefaultCapacityBytes(-1) == 0);
}

TEST_CASE("SRY2 recyclable decision", "[fileshare][sharerecycle]")
{
    CHECK(ShareRecyclePolicy::Check(FixedVolume(kCapacity), 0) == error_code_ok);
    CHECK(ShareRecyclePolicy::Check(FixedVolume(kCapacity), kCapacity) == error_code_ok);
    //超过容量：系统会直接彻底删除，必须拒绝
    CHECK(ShareRecyclePolicy::Check(FixedVolume(kCapacity), kCapacity + 1) == error_code_share_recycle_unavailable);
    //容量取不到
    CHECK(ShareRecyclePolicy::Check(FixedVolume(0), 0) == error_code_share_recycle_unavailable);

    ShareVolumeRecycleInfo nuke = FixedVolume(kCapacity);
    nuke.m_nukeOnDelete = true;
    CHECK(ShareRecyclePolicy::Check(nuke, 0) == error_code_share_recycle_unavailable);

    //本机固定盘之外一律拒绝
    const UINT others[] = { DRIVE_REMOTE, DRIVE_REMOVABLE, DRIVE_CDROM, DRIVE_RAMDISK, DRIVE_UNKNOWN, DRIVE_NO_ROOT_DIR };
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); ++i)
    {
        ShareVolumeRecycleInfo info = FixedVolume(kCapacity);
        info.m_driveType = others[i];
        CHECK(ShareRecyclePolicy::Check(info, 0) == error_code_share_recycle_unavailable);
    }
}

TEST_CASE("SRY3 query local volume", "[fileshare][sharerecycle]")
{
    ShareVolumeRecycleInfo info;
    REQUIRE(CShareSystemRecycler::QueryVolume(sharetestfs::ExeDir(), info));
    CHECK(info.m_driveType == static_cast<UINT>(DRIVE_FIXED));
    CHECK(info.m_capacityBytes > 0);

    ShareVolumeRecycleInfo unc;
    CShareSystemRecycler::QueryVolume(kUncPath, unc);
    CHECK(unc.m_driveType != static_cast<UINT>(DRIVE_FIXED));
    CShareSystemRecycler recycler;
    CHECK(recycler.CheckRecyclable(kUncPath, 0) == error_code_share_recycle_unavailable);
}
