/**
 *  GroupFileUploadTracker.h
 *
 *  群聊窗「群文件」面板里「正在上传」若干行的纯逻辑跟踪器。
 *
 *  群文件上传与下载不同：被上传的文件还没进群文件库（要等上传成功、服务端登记、
 *  再经 1806 推回来才出现在列表里），所以列表里本来没有它的行。为了把上传进度也
 *  显示成文件行（而不是占用「上传文件」按钮），上传一发起就往顶部临时插一行
 *  「上传中」，上传成功后被推回来的真实条目替换、取消 / 失败则移除。本类就管这批
 *  临时行的数据：哪些在传、各自进度、最近发起的排最前。
 *
 *  本类是纯逻辑：只依赖 std，不触及 UI / 网络 / ATL（文件名用 std::wstring，窗口侧
 *  在 CString 边界上转一下），便于单元测试，见
 *  flamingoclient/Source/Tests/GroupFileUploadTrackerTests.cpp（Suite GFUT）。
 *
 *  与后端 net::GroupTransferQueue 的关系：后端按 uploadId 管「真正在传哪些、能否
 *  取消」；本类按同一个 uploadId 管「界面上显示哪些上传行、各自百分比」。两边靠
 *  uploadId 对应。
 *
 *  balloonwj@qq.com   2026-06-09
 */

#pragma once

#include <string>
#include <vector>

namespace balloonchatwnd {

/**
 *  GroupUploadEntry —— 一笔在途上传的展示信息（渲染一行「上传中」用）。
 */
struct GroupUploadEntry
{
    int          m_uploadId;   // 上传 id（与后端对应；进度 / 取消按它定位）
    std::wstring m_name;       // 文件名（含扩展名，用于显示与算角标）
    std::wstring m_ext;        // 扩展名（不含点，大小写不敏感；决定彩色角标）
    int          m_percent;    // 上传进度 0~100

    GroupUploadEntry()
        : m_uploadId(0)
        , m_percent(0)
    {
    }
};

/**
 *  GroupFileUploadTracker —— 「正在上传」行的内存模型（纯逻辑）。
 *
 *  典型用法：PickAndUploadGroupFile 拿到后端分配的 uploadId 后 Begin 一笔；收到上传
 *  进度回投按 uploadId SetProgress；上传成功 / 失败 / 取消时 Remove。RenderFilePanel
 *  每次重渲染时把 Items() 里的在途上传钉在库文件行之前（最近发起在最上）。
 */
class GroupFileUploadTracker
{
public:
    GroupFileUploadTracker();

    /**
     *  新发起一笔上传（进度初始为 0），插到最前（最近发起在最上）。
     *  若该 uploadId 已存在（理论上不会），则就地更新其文件名 / 扩展名并把进度归零，
     *  不产生重复行。
     *    uploadId： 后端分配的上传 id（>0）。
     *    name：     文件名（含扩展名）。
     *    ext：      扩展名（不含点）。
     */
    void Begin(int uploadId, const std::wstring& name, const std::wstring& ext);

    /**
     *  更新某笔上传的进度。percent 会被钳到 [0,100]。uploadId 不存在则无操作。
     *    uploadId： 目标上传 id。
     *    percent：  新进度（钳前可越界，内部钳到 0~100）。
     */
    void SetProgress(int uploadId, int percent);

    /**
     *  移除某笔上传（成功 / 失败 / 取消时调）。
     *    uploadId： 目标上传 id。
     *  返回：确有移除返回 true；id 不存在返回 false。
     */
    bool Remove(int uploadId);

    /**
     *  按 uploadId 查一笔。命中返回指向内部条目的指针（生命周期随 tracker，调用方
     *  用完即弃、勿长持），未命中返回 NULL。窗口侧用「是否查得到」来区分一笔上传
     *  结果是「用户已取消（查不到→静默忽略）」还是「真失败（查得到→提示）」。
     */
    const GroupUploadEntry* Find(int uploadId) const;

    // 在途上传笔数。
    int Count() const;

    // 顺序访问全部在途上传（index 0 = 最近发起），渲染用。
    const std::vector<GroupUploadEntry>& Items() const { return m_items; }

    // 清空（关窗等场景）。
    void Clear();

private:
    std::vector<GroupUploadEntry> m_items;   // 在途上传（index 0 = 最近发起）
};

} // namespace balloonchatwnd
