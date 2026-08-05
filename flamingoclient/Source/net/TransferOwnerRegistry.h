/**
 *  传输信令归属登记表（客户端侧，纯逻辑）。
 *
 *  图片传输（ImageTransfer，走 imgserver）与文件传输（FileTransfer，走 fileserver）
 *  都在同一条 chatserver 连接上发 cmd_request_upload(1801) / cmd_request_download(1802)
 *  信令，帧 seq 来自 CIUSocket 同一个自增计数器。应答异步回到
 *  CIUSocket::HandleV2Frame 时，需要据 seq 判断这条应答属于图片还是文件，转交给
 *  对应传输管理器的等待槽。本表负责"发请求时记下 seq→归属、收应答时按 seq 取回
 *  归属"这一映射。
 *
 *  纯逻辑、不含线程同步（由 CIUSocket 用其自身互斥量保护），便于单元测试，见
 *  flamingoclient/Source/Tests/TransferOwnerRegistryTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <stdint.h>
#include <map>
#include <cstddef>

namespace net {

//传输归属：标识一条上传 / 下载信令属于哪类传输，决定其应答转交给哪个管理器。
enum TransferOwner
{
    transfer_owner_image = 1,   //图片传输（ImageTransfer，走 imgserver；scene=头像 / 图片消息）
    transfer_owner_file  = 2,   //文件传输（FileTransfer，走 fileserver；scene=聊天文件 / 群文件）
};

//传输信令归属登记表；按 seq 记录与取回归属。非线程安全，调用方须自行加锁。
class TransferOwnerRegistry
{
public:
    TransferOwnerRegistry();

    /**
     *  记录某帧 seq 的传输归属（发 cmd_request_upload / cmd_request_download 后调用）。
     *  同一 seq 重复登记以最后一次为准。为防"发了请求但应答始终未回"的 seq 无限
     *  堆积，在册条数超过上限 kMaxEntries 时淘汰 seq 最小（最旧）的条目。
     *  @param seq   申请信令所用的帧 seq
     *  @param owner 传输归属，取值见 enum TransferOwner
     */
    void registerSeq(uint32_t seq, int owner);

    /**
     *  按 seq 取回并移除其传输归属（收到 1801 / 1802 应答时调用）。
     *  @param seq   应答帧的 seq
     *  @param owner [出参] 命中时写入登记的归属；未命中不改写
     *  @return 命中并移除返回 true；该 seq 未登记返回 false
     */
    bool takeOwner(uint32_t seq, int& owner);

    //当前在册条数（主要供单元测试断言淘汰行为）。
    size_t size() const;

private:
    //在册条数上限：正常情况下应答很快回来即被取走，登记表常驻很小；设上限仅为
    //兜底"超时始终未应答的 seq"长期不被取走的极端情况，避免无界增长。
    static const size_t kMaxEntries = 1024;

    //seq → 归属。用 std::map（有序）而非 unordered_map：超限淘汰时按 seq 升序取
    //最旧，begin() 即最小键，淘汰定位为 O(1)。seq 由 CIUSocket 单调自增分配，故
    //最小键近似"最早发出、最可能已超时无应答"的请求。
    std::map<uint32_t, int> m_owners;
};

} // namespace net
