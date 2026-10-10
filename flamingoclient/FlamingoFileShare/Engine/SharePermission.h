/**
 *  文件共享的权限判定（分享者一端，纯逻辑），SharePermission.h
 *
 *  分享者为每个访问会话持有一份 SharePermission：共享类型来自本机的共享记录，权限位来自 2222 推给分享者
 *  那一帧的 perms，会话中途由 2206 更新。分享者执行访问者的每一个请求之前先调 Check（服务端在 2224 已经
 *  判过一次上传权限，这里再判一次，防止会话中途权限被收回，见 docs/文件共享方案.md 5.4、9.1）。
 *
 *  规则：下载、列目录、查条目、搜索、缩略图不需要任何权限位；五种写操作各看自己对应的那一位；单文件共享
 *  没有可以写入的目录，任何写操作都按「操作与共享类型不符」拒绝。
 *
 *  Check 可在工作线程上调用，SetPerms 在 UI 线程上调用：权限位用原子变量保存。
 *  单元测试见 FlamingoFileShare/Tests/SharePermissionTests.cpp（标签 [shareperm]）。
 *
 *  典型用法：
 *      SharePermission perm(share.kind, relayInfo.perms);
 *      int code = perm.Check(share_op_rename);
 *      if (code != error_code_ok)
 *          ...（回 sf_op_result，code 为 806 或 820）
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <atomic>

namespace fileshare {

//访问者在一个共享上可以发起的操作
enum share_operation
{
    share_op_list     = 1,    //列目录（sf_list）
    share_op_stat     = 2,    //查一个条目（sf_stat）
    share_op_search   = 3,    //按文件名搜索（sf_search）
    share_op_thumb    = 4,    //取缩略图或预览图（sf_thumb）
    share_op_download = 5,    //下载（传输子会话，方向为下载）
    share_op_upload   = 6,    //上传（传输子会话，方向为上传）
    share_op_mkdir    = 7,    //新建文件夹（sf_mkdir）
    share_op_rename   = 8,    //重命名（sf_rename）
    share_op_delete   = 9,    //删除，放进回收站（sf_delete）
    share_op_move     = 10,   //移动到共享内的另一个目录（sf_move）
};

/**
 *  SharePermission —— 一个访问会话上访问者的权限：共享类型 + 写权限位。
 */
class SharePermission
{
public:
    /**
     *  @param kind  共享类型，取值见 share_kind（Source/net/Protocol.h）
     *  @param perms 访问者的写权限位，取值为 share_perm 的组合
     */
    SharePermission(int kind, int perms);

    /**
     *  更新写权限位（收到 2206 或会话中途重新取得权限时），之后的 Check 立即按新值判定。线程安全。
     *  @param perms 新的写权限位
     */
    void SetPerms(int perms);

    /**
     *  取当前的写权限位。线程安全。
     *  @return 写权限位
     */
    int GetPerms() const;

    /**
     *  判定一项操作是否允许。线程安全。
     *  @param op 操作
     *  @return error_code_ok 允许；单文件共享上的写操作为 error_code_share_kind_mismatch；
     *          缺少对应权限位为 error_code_share_permission_denied
     */
    int Check(share_operation op) const;

    /**
     *  取一项操作需要的权限位。
     *  @param op 操作
     *  @return 对应的 share_perm 取值；不需要权限位的操作返回 0
     */
    static int RequiredPerm(share_operation op);

private:
    int              m_kind;    //共享类型；构造后不变
    std::atomic<int> m_perms;   //写权限位；UI 线程更新、工作线程读取
};

} // namespace fileshare
