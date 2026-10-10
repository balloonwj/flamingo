/**
 *  文件共享权限判定单元测试（Suite SHR，标签 [shareperm]），SharePermissionTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 11 步的 11b，2026-10-02 与用户确认的用例）：
 *    - SHR1 五个写权限位各自独立，每种写操作只看自己对应的那一位；
 *    - SHR2 下载、列目录、查条目、搜索、缩略图不需要任何权限位；
 *    - SHR3 单文件共享的任何写操作都拒绝（回 820），即便权限位齐全；
 *    - SHR4 会话中途权限变化后立即生效。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Engine/SharePermission.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//五种写操作与各自需要的权限位
struct WriteOpCase
{
    share_operation m_op;     //操作
    int             m_perm;   //需要的权限位
};

const WriteOpCase kWriteOps[] =
{
    { share_op_upload, share_perm_upload },
    { share_op_mkdir,  share_perm_mkdir },
    { share_op_rename, share_perm_rename },
    { share_op_delete, share_perm_delete },
    { share_op_move,   share_perm_move },
};

//不需要任何权限位的操作
const share_operation kReadOps[] =
{
    share_op_list, share_op_stat, share_op_search, share_op_thumb, share_op_download
};

//五个写权限位全部置位
const int kAllPerms = share_perm_upload | share_perm_mkdir | share_perm_rename | share_perm_delete | share_perm_move;

} // namespace

TEST_CASE("SHR1 五个写权限位各自独立", "[shareperm]")
{
    for (size_t i = 0; i < sizeof(kWriteOps) / sizeof(kWriteOps[0]); ++i)
    {
        INFO("op " << static_cast<int>(kWriteOps[i].m_op));
        //只有对应的那一位：允许
        SharePermission only(share_kind_folder, kWriteOps[i].m_perm);
        CHECK(only.Check(kWriteOps[i].m_op) == error_code_ok);
        //其余四位都有、唯独缺这一位：拒绝
        SharePermission others(share_kind_folder, kAllPerms & ~kWriteOps[i].m_perm);
        CHECK(others.Check(kWriteOps[i].m_op) == error_code_share_permission_denied);
        //所需权限位与表一致
        CHECK(SharePermission::RequiredPerm(kWriteOps[i].m_op) == kWriteOps[i].m_perm);
    }
}

TEST_CASE("SHR2 读类操作不需要任何权限位", "[shareperm]")
{
    SharePermission none(share_kind_folder, 0);
    SharePermission disk(share_kind_disk, 0);
    for (size_t i = 0; i < sizeof(kReadOps) / sizeof(kReadOps[0]); ++i)
    {
        INFO("op " << static_cast<int>(kReadOps[i]));
        CHECK(none.Check(kReadOps[i]) == error_code_ok);
        CHECK(disk.Check(kReadOps[i]) == error_code_ok);
        CHECK(SharePermission::RequiredPerm(kReadOps[i]) == 0);
    }
}

TEST_CASE("SHR3 单文件共享的任何写操作都拒绝", "[shareperm]")
{
    SharePermission file(share_kind_file, kAllPerms);
    for (size_t i = 0; i < sizeof(kWriteOps) / sizeof(kWriteOps[0]); ++i)
    {
        INFO("op " << static_cast<int>(kWriteOps[i].m_op));
        CHECK(file.Check(kWriteOps[i].m_op) == error_code_share_kind_mismatch);
    }
    //下载与查条目照常允许
    CHECK(file.Check(share_op_download) == error_code_ok);
    CHECK(file.Check(share_op_stat) == error_code_ok);
}

TEST_CASE("SHR4 会话中途权限变化后立即生效", "[shareperm]")
{
    SharePermission perm(share_kind_folder, share_perm_upload);
    CHECK(perm.Check(share_op_upload) == error_code_ok);
    CHECK(perm.Check(share_op_delete) == error_code_share_permission_denied);

    perm.SetPerms(share_perm_delete);
    CHECK(perm.GetPerms() == share_perm_delete);
    CHECK(perm.Check(share_op_upload) == error_code_share_permission_denied);
    CHECK(perm.Check(share_op_delete) == error_code_ok);

    perm.SetPerms(0);
    CHECK(perm.Check(share_op_delete) == error_code_share_permission_denied);
    CHECK(perm.Check(share_op_download) == error_code_ok);
}
