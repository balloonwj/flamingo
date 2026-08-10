/**
 *  flamingoAdmin —— 新建群弹窗 GroupModal。
 *
 *  企业详情屏「组织架构」tab 点击「新建群」时弹出：把本企业的成员拉进一个新群。
 *  交互形态为「单列表行内角色」：
 *    · 顶部填群名称（DuiEditHost 真输入框）；
 *    · 下方一个可滚动成员列表，列出本企业全部员工，每行三个可点区——
 *      勾选框(加入本群) + ★(设为群主，整列表唯一) + 复选(设为管理员，可多个)；
 *    · 设群主 / 管理员会自动把该成员标记为已加入；取消加入会一并清掉其角色。
 *
 *  实现与其它弹窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，
 *  开 / 关只切显隐；点遮罩不关闭。成员列表为手写自绘可滚动区，复用
 *  OverlayScrollbar 悬浮滚动条（见开发约定文档「UI 约定」）。群名输入框是
 *  DuiEditHost 真子窗。提交、关闭经各按钮的 ctrlId 上报 AdminFrame。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"

#include <vector>

namespace balloonwjui { class DuiEditHost; }

namespace flamingoadmin {

class OverlayScrollbar;   // 成员列表的悬浮滚动条子控件

// 新建群弹窗。
class GroupModal : public balloonwjui::DuiControl
{
public:
    GroupModal();

    // 载入某企业并重建成员候选列表（每次打开前调用）。从 DataStore 的该企业
    // 组织树里递归收集全部员工节点；同时清空群名输入与所有勾选 / 角色状态。
    //   enterpriseId：目标企业 ID。
    void SetEnterprise(const CString& enterpriseId);

    // 校验必填项：群名非空，且已指定群主。
    bool Validate() const;

    // 当前企业 ID。
    CString GetEnterpriseId() const { return m_entId; }

    // 群名称（已 Trim 由调用方负责）。
    CString GetGroupName() const;

    // 群主 userid；未指定群主时返回 0。
    long long GetOwnerId() const;

    // 管理员 userid 列表：已加入、非群主、且被标记为管理员的成员。
    std::vector<long long> GetAdminIds() const;

    // 普通成员 + 管理员的 userid 列表：即全部已勾选加入、非群主的成员
    //（服务端要求管理员必须是成员子集，故此列表包含管理员）。
    std::vector<long long> GetMemberIds() const;

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool                     OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 一名候选成员（企业内某员工）及其在本次建群中的选择状态。
    struct Member
    {
        long long m_userid;   // 员工 userid
        CString   m_name;     // 姓名
        CString   m_dept;     // 所属部门名（展示用）
        bool      m_joined;   // 是否勾选加入本群
        bool      m_owner;    // 是否被设为群主（整列表唯一）
        bool      m_admin;    // 是否被设为管理员

        Member()
            : m_userid(0), m_joined(false), m_owner(false), m_admin(false)
        {
        }
    };

    // 成员列表的一个可点击命中区（每帧绘制时重建）。
    struct Hit
    {
        RECT m_rc;      // 命中矩形（host-client 坐标）
        int  m_kind;    // 0=勾选/取消加入 1=设/取消群主 2=设/取消管理员
        int  m_index;   // 命中的成员在 m_members 中的下标
    };

    // 悬浮滚动条拖动滑块的回调：把新滚动量写回本弹窗并刷新成员列表面板。
    //   ctx：本 GroupModal 实例；newScroll：拖动产生的新滚动偏移。
    static void OnSbScroll(void* ctx, int newScroll);

    // 从组织树某节点递归收集全部员工到 m_members；deptName 为其所属部门名。
    void CollectMembers(const OrgNode& node, const CString& deptName);

    // 成员列表面板矩形（host-client 坐标）；Layout / 绘制 / 滚动 / 局部刷新共用。
    RECT ListRect() const;

    // 只把成员列表面板那块矩形标记为脏并刷新——避免整窗 Invalidate 牵连群名
    // DuiEditHost 真子窗导致闪烁（对齐 ComposeModal::InvalidatePicker 的做法）。
    void InvalidateList();

    // 自绘成员列表（含每行的勾选框 / 群主 / 管理员可点区，并重建 m_hits）。
    void PaintMemberList(HDC hdc, const RECT& rc);

    RECT m_panelRect{};                 // 居中卡片矩形
    CString m_entId;                    // 当前企业 ID

    std::vector<Member> m_members;      // 候选成员及其选择状态
    int m_scroll   = 0;                 // 列表滚动偏移（像素）
    int m_contentH = 0;                 // 列表内容总高（像素）

    std::vector<Hit> m_hits;            // 本帧成员列表命中区集合

    OverlayScrollbar* m_sb = nullptr;            // 成员列表悬浮滚动条（子控件）
    balloonwjui::DuiEditHost* m_editName = nullptr;  // 群名称输入框（子控件）
};

} // namespace flamingoadmin
