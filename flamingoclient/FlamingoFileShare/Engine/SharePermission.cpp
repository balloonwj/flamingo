/**
 *  文件共享的权限判定 SharePermission 的实现，见 SharePermission.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "SharePermission.h"

#include "../../Source/net/Protocol.h"   //share_kind / share_perm / error_code

namespace fileshare {

SharePermission::SharePermission(int kind, int perms)
    : m_kind(kind)
    , m_perms(perms)
{
}

void SharePermission::SetPerms(int perms)
{
    m_perms.store(perms);
}

int SharePermission::GetPerms() const
{
    return m_perms.load();
}

int SharePermission::Check(share_operation op) const
{
    const int required = RequiredPerm(op);
    if (required == 0)
        return error_code_ok;

    //单文件共享没有可以写入的目录，先于权限位判定：即便权限位齐全也不行
    if (m_kind == share_kind_file)
        return error_code_share_kind_mismatch;
    if ((m_perms.load() & required) == 0)
        return error_code_share_permission_denied;
    return error_code_ok;
}

int SharePermission::RequiredPerm(share_operation op)
{
    switch (op)
    {
    //上传需要上传位
    case share_op_upload:
        return share_perm_upload;
    //新建文件夹需要新建位
    case share_op_mkdir:
        return share_perm_mkdir;
    //重命名需要重命名位
    case share_op_rename:
        return share_perm_rename;
    //删除需要删除位
    case share_op_delete:
        return share_perm_delete;
    //移动需要移动位
    case share_op_move:
        return share_perm_move;
    //列目录、查条目、搜索、缩略图、下载：成员恒可执行，不占权限位
    default:
        return 0;
    }
}

} // namespace fileshare
