/**
 *  DeptPickDialog.h
 *
 *  「选择部门」小窗：个人机器人规则「发送者 ▸ 指定的部门」点「选择…」时由 FlamingoBot.dll 经宿主打开
 *  （docs/机器人方案.md §14.8）。左边是只含部门的组织树（复选框 + 部门名，可搜索），底部「已选 N 个部门（含下级部门）」
 *  与「取消」「确定」。确定时去掉已被上级包含的下级部门（选上级即含下级），把结果交给 IDeptPickSink 后关闭。
 *  控件树见 DeptPickLayout.h，树的纯逻辑见 DeptPickModel.h。
 *
 *  打开期间禁用所有者窗口（插件的规则编辑窗），关闭时恢复，与问答编辑窗同一做法。
 *
 *  典型用法（主面板）：
 *      m_deptPickDlg.Open(ownerHwnd, m_departments, preselected, &sink);
 *      //sink.OnDepartmentsPicked(ids) 在用户点「确定」时被调用
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../net/ContactsProtocolClient.h"   // net::DepartmentItem
#include "TitleBarIcon.h"                       // ProgramWindowIcons：标题栏左侧的程序图标
#include "ClosePromptDecision.h"                // ClosePromptHostGuard：打开期间禁用所有者窗口

namespace balloonmain {

/**
 *  选部门结果的接收方。只在 UI 线程上调用。
 */
class IDeptPickSink
{
public:
    virtual ~IDeptPickSink()
    {
    }

    /**
     *  用户点了「确定」：已整理的部门ID（去掉了重复的、已删除的、被上级包含的），按勾选先后排列，至少一个。
     *  调用之后对话框随即关闭。
     */
    virtual void OnDepartmentsPicked(const std::vector<int64_t>& deptIds) = 0;
};

/**
 *  CDeptPickDialog —— 「选择部门」小窗（非模态，打开期间禁用所有者窗口）。同一时刻只存在一个实例（主面板持有）；
 *  已打开时再次 Open 会换成新的内容并前置。
 */
class CDeptPickDialog : public balloonwjui::DuiFrameWindow
{
public:
    CDeptPickDialog();
    ~CDeptPickDialog();

    /**
     *  打开对话框。
     *    owner：       所有者窗口（插件的规则编辑窗），对话框居中到它、打开期间禁用它；可为 NULL。
     *    departments： 部门列表（主面板缓存的组织架构），复制一份保存。
     *    preselected： 已经选中的部门ID，打开时预先勾选；不在部门列表里的忽略。
     *    sink：        结果的接收方，借用不持有；须活到对话框关闭；为 NULL 时点「确定」只关闭。
     *  @return 窗口建出或前置成功返回 true
     */
    bool Open(HWND owner, const std::vector<net::DepartmentItem>& departments, const std::vector<int64_t>& preselected,
              IDeptPickSink* sink);

    /**
     *  关闭对话框并恢复所有者窗口；没有打开时什么都不做。不回调接收方。主面板登出或销毁前调用。
     */
    void CloseDialog();

    BEGIN_MSG_MAP(CDeptPickDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 按搜索框的文字重建部门树，并刷新计数、空提示与滚动范围
    void RebuildTree();

    // 切换一个树节点对应部门的勾选；节点不存在时什么都不做
    void Toggle(int nodeId);

    // 「确定」：整理结果、交给接收方，再投 WM_CLOSE 关闭自己（不在点击通知里直接销毁，开发约定文档 UI 约定第 13 条）
    void OnOk();

private:
    HWND                              m_ownerHwnd;     // 所有者窗口，借用
    IDeptPickSink*                    m_sink;          // 结果的接收方，借用；关闭后置空
    std::vector<net::DepartmentItem>  m_departments;   // 部门列表的副本，本次打开期间不变
    std::vector<int64_t>              m_checked;       // 已勾选的部门ID，按勾选先后排列（含因搜索没有列出的）
    std::set<int64_t>                 m_checkedSet;    // 与 m_checked 内容相同，便于查找
    std::map<int, int64_t>            m_nodeDept;      // 树节点 id → 部门ID；每次重建树时整体替换
    balloonmain::ClosePromptHostGuard m_ownerGuard;    // 是否由本窗口禁用了所有者、关闭时要不要恢复
    balloonmain::ProgramWindowIcons   m_programIcons;  // 标题栏左侧的程序图标（本类持有）
};

} // namespace balloonmain
