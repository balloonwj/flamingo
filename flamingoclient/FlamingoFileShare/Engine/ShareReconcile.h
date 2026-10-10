/**
 *  分享者上线比对（纯逻辑），ShareReconcile.h
 *
 *  分享者登录成功后，DLL 先用 2205（scope = 我的共享）拉取服务端上仍然有效的共享，再与本机 fileshare.db 的
 *  t_share_local 逐条比对（docs/文件共享方案.md 8.6），把每个共享归入下面四种情况之一：
 *    1. 服务端有、本机有、绑定的设备就是本机：检查共享根是否存在，然后用 2210 报告能否提供；
 *    2. 服务端有、绑定的设备是本机，但本机没有记录：无法提供，在「文件共享」窗口里标注「本机缺少记录」；
 *    3. 本机有、服务端已没有（已取消或从未建成）：删除本机记录；
 *    4. 服务端有、绑定的是别的设备：标注「位于另一台电脑」，不报告。本机若恰好也有记录（例如本机的设备
 *       标识被重新生成过），记录保留不删，换回原设备标识后仍可继续提供。
 *  本机还没有拿到 shareid 的记录（发起后应答尚未回来）不参与比对，既不报告也不删除，由发起流程自己收尾。
 *
 *  本模块只做集合比对，不访问文件系统；共享根是否存在由调用方（ShareService）用 ShareFileOps 检查后，
 *  再经 MakeServeItem 生成 2210 的一项。
 *  单元测试见 FlamingoFileShare/Tests/ShareReconcileTests.cpp（标签 [sharereconcile]）。
 *
 *  典型用法：
 *      ShareReconcilePlan plan = ShareReconcile::Plan(listResp.m_shares, localRecords);
 *      for (size_t i = 0; i < plan.m_removeLocal.size(); ++i)
 *          store.RemoveShare(plan.m_removeLocal[i]);
 *      std::vector<ShareServeItem> items;
 *      for (size_t i = 0; i < plan.m_report.size(); ++i)
 *          items.push_back(ShareReconcile::MakeServeItem(plan.m_report[i].m_shareId,
 *              ShareFileOps::IsRootAvailable(plan.m_report[i].m_rootPath, plan.m_report[i].m_kind)));
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "ShareLocalStore.h"
#include "ShareProtocolCodec.h"

namespace fileshare {

//上线比对的结果：四个列表互不重叠，每个列表内的顺序与输入顺序一致
struct ShareReconcilePlan
{
    std::vector<ShareLocalRecord> m_report;         //情况 1：要检查共享根并用 2210 报告的本机记录
    std::vector<int64_t>          m_missingLocal;   //情况 2：绑定本机但本机没有记录的 shareid
    std::vector<int64_t>          m_removeLocal;    //情况 3：服务端已没有、要删除本机记录的 shareid
    std::vector<int64_t>          m_otherDevice;    //情况 4：绑定别的设备的 shareid
};

/**
 *  ShareReconcile —— 分享者上线比对；全部为静态方法，不可实例化。
 */
class ShareReconcile
{
public:
    /**
     *  比对服务端的「我的共享」与本机记录。
     *  @param serverMine 2205（scope = 我的共享）应答里的共享列表；服务端只返回仍然有效的共享
     *  @param local      本机 t_share_local 的全部记录（含尚未拿到 shareid 的）
     *  @return 比对结果；同一个 shareid 在服务端列表里出现多次时只按第一次处理
     */
    static ShareReconcilePlan Plan(const std::vector<ShareListEntry>& serverMine,
                                   const std::vector<ShareLocalRecord>& local);

    /**
     *  生成 2210 的一项。
     *  @param shareId       共享 id
     *  @param rootAvailable 共享根此刻能否提供（存在，且类型与共享类型相符）
     *  @return 能提供时 available 为真、reason 为 0；否则 available 为假、reason 为
     *          share_unavailable_root_missing
     */
    static ShareServeItem MakeServeItem(int64_t shareId, bool rootAvailable);

private:
    ShareReconcile() = delete;
};

} // namespace fileshare
