/**
 *  GroupDeptHeaderItem.h
 *
 *  群聊窗「成员」页签部门树视图里的部门标题行（docs/群成员部门树方案.md）：折叠箭头 + 部门名 + 人数。
 *  与成员行 GroupMemberItem 同一行高、同一套悬停底色，按层级缩进。左键单击请父列表
 *  （GroupMemberList）折叠或展开它的下级行；折叠状态由列表按部门 id 记住，本控件只负责显示。
 *
 *  本控件是 UI 约定第 2 条「末选」的自绘控件：群成员列表整体是自绘的（固定行高堆叠、细滑块、
 *  过滤），部门树只是在其中多出一种行，2026-10-06 与用户确认扩展现有自绘列表，不换成 DuiTreeView。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  典型用法（群聊窗建部门树时）：
 *      std::unique_ptr<GroupDeptHeaderItem> head(new GroupDeptHeaderItem());
 *      head->SetDept(deptId, name, count, depth);
 *      list->AddRow(std::unique_ptr<DuiControl>(head.release()), parentRow);
 *
 *  balloonwj@qq.com   2026-10-06
 */

#pragma once

#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

// 部门树每深一层的缩进（像素）。成员行与部门标题行共用
const int kGroupDeptIndentPx = 14;

/**
 *  算某一层级的缩进量。层级越界（负数）按 0 处理；层级很深时封顶，避免名字被挤没。
 *    depth：层级，顶级为 0。
 *  @return 缩进像素。纯函数。
 */
int GroupDeptIndentPx(int depth);

/**
 *  GroupDeptHeaderItem —— 部门树里的一行部门标题。
 */
class GroupDeptHeaderItem : public balloonwjui::DuiControl
{
public:
    GroupDeptHeaderItem();

    /**
     *  设置这一行显示的部门。
     *    deptId： 部门 id；「未分配部门」为 kDeptTreeUnassignedId（见 GroupMemberDeptTree.h）。
     *    name：   部门名（已转好的显示文字）。
     *    count：  本部门及下级部门里的本群成员数，画在名字后面。
     *    depth：  层级，顶级为 0，决定缩进。
     */
    void SetDept(int64_t deptId, const CString& name, int count, int depth);

    // 部门 id（SetDept 设入的值）
    int64_t DeptId() const { return m_deptId; }

    // 部门名（SetDept 设入的值）
    const CString& Name() const { return m_name; }

    // 人数（SetDept 设入的值）
    int Count() const { return m_count; }

    // 层级（SetDept 设入的值）
    int Depth() const { return m_depth; }

    /**
     *  设置折叠状态（由列表调用）。折叠时箭头朝右，展开时朝下。状态变化时请求重绘。
     */
    void SetCollapsed(bool collapsed);

    // 当前是否折叠
    bool IsCollapsed() const { return m_collapsed; }

    /**
     *  设置是否处于搜索中（由列表调用）。搜索时命中成员所在的部门全部自动展开，箭头一律画成展开，
     *  点击也不改变折叠状态；清空关键字后恢复原来的折叠状态。状态变化时请求重绘。
     */
    void SetSearching(bool searching);

    // DuiControl 覆写：画悬停底、折叠箭头、部门名与人数。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：左键按下请父列表切换本部门的折叠状态（搜索中不切换）。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    // DuiControl 覆写：悬停变化时重画悬停底（UI 约定第 9 条）。
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;

    // DuiControl 覆写：用普通箭头光标。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    int64_t m_deptId;     // 部门 id；「未分配部门」为 kDeptTreeUnassignedId
    CString m_name;       // 部门名（显示文字）
    int     m_count;      // 本部门及下级部门里的本群成员数
    int     m_depth;      // 层级，顶级为 0
    bool    m_collapsed;  // 是否折叠；由列表按部门 id 记住并在建行时设入
    bool    m_searching;  // 列表是否处于搜索中；为 true 时箭头画成展开
};

} // namespace balloonchatwnd
