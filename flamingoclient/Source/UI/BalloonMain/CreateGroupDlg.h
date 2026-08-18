/**
 *  CreateGroupDlg.h
 *
 *  "选人类"对话框 —— 新建群聊 / 邀请入群 / 添加禁言成员 / 转让群主
 *  四种用途共用同一套壳，用途见下面的 Purpose 枚举。类名沿用最早的"新建群聊"，
 *  职责已扩成"搜索 + 可勾选候选列表 + 右侧区 + 底部确认"这套通用骨架。
 *
 *  以下按最早的"新建群聊"用途描述 —— 对照 design/新建群.png。基于 balloonui DuiFrameWindow。
 *  左侧：搜索框 + 按部门分组的可勾选联系人列表（复选框 + 头像 + 姓名，复用
 *  balloonui DuiButton(复选框) / DuiAvatar / DuiLabel 组合，匹配设计稿）；右侧：
 *  群名输入（选填）+ 已选成员区（头像+姓名+✕，可移除）；底部：已选 N 人 + 取消 +
 *  创建群聊。点"创建群聊"经 cmd_create_group 发往服务端，应答回投后提示结果。
 *
 *  与主面板的关系：主面板"群"页底部"建群"按钮点击时创建并显示本对话框，把
 *  通讯录缓存（联系人 + 部门）与当前登录用户传入用于构建可勾选列表。建群应答
 *  由主面板转发到本对话框的 OnCreateGroupResult。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "MemberPickerTreeModel.h"   // MemberPickerNode（m_treeNodes 成员需完整类型）

#include <vector>
#include <map>
#include <string>
#include <stdint.h>

class CFlamingoClient;

namespace net { struct ContactItem; struct DepartmentItem; struct CreateGroupResponse; }
namespace balloonwjui { class DuiControl; class DuiEdit; class DuiScrollView;
                        class DuiLabel; class DuiButton; class DuiVBox; }
namespace balloonmain { class MemberPickerTreeView; }

/**
 *  CCreateGroupDlg —— 新建群聊对话框（非模态浮窗，owner = 主面板）。
 */
class CCreateGroupDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  本对话框的用途。
     *
     *  这几种场景的界面骨架是同一套 ——「搜索 + 可勾选候选列表 + 右侧区 + 底部确认」，
     *  差别只在候选来源、右侧放什么、单选还是多选、以及点确认后发哪条协议。
     *  与其各写一个几乎相同的对话框，不如把这些差异做成参数。
     */
    enum Purpose
    {
        kPurposeCreateGroup = 0,   // 新建群聊：候选=通讯录，多选，带群名输入，发 cmd_create_group
        kPurposeInvite      = 1,   // 邀请入群：候选=通讯录（排除已在群内的），多选，发 cmd_invite_to_group
        kPurposeMute        = 2,   // 添加禁言成员：候选=本群成员（排除群主与自己），多选，发 cmd_mute_member
        kPurposeTransfer    = 3    // 转让群主：候选=本群成员（排除自己），<u>单选</u>，发 cmd_transfer_owner
    };

    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有；用于取
     *                当前登录用户、发起建群请求。
     */
    explicit CCreateGroupDlg(CFlamingoClient* pFMGClient);
    ~CCreateGroupDlg();

    /**
     *  创建并显示对话框，用通讯录缓存构建可勾选联系人列表。已显示则前置。
     *    owner：owner 窗口句柄（主面板）。
     *    selfUserId：当前登录用户 userid，列表里排除自己。
     *    contacts：联系人资料池（本类内部复制一份）。
     *    departments：部门列表（本类内部复制一份），用于按部门分组与算分组标题。
     */
    void OpenForCreate(HWND owner, UINT selfUserId,
                       const std::vector<net::ContactItem>& contacts,
                       const std::vector<net::DepartmentItem>& departments);

    /**
     *  收到建群应答：成功提示"群聊创建成功"并关闭；失败弹框提示错误码。由主面板
     *  的 OnCreateGroupResult 转发。
     *    resp：建群应答解析结果。
     */
    void OnCreateGroupResult(const net::CreateGroupResponse& resp);

    /**
     *  以"选人"用途打开对话框（邀请入群 / 添加禁言成员 / 转让群主共用）。
     *
     *  与 OpenForCreate 的差别：不显示群名输入框，底部按钮文案与点击后发出的
     *  协议按 purpose 决定，kPurposeTransfer 还会切成单选。
     *
     *    owner：       owner 窗口句柄。
     *    purpose：     用途，取值见 Purpose；传 kPurposeCreateGroup 无意义。
     *    groupId：     目标群ID，提交时随协议发出；必须为正。
     *    selfUserId：  当前登录用户 userid，候选列表里排除自己。
     *    candidates：  候选人资料池（本类内部复制一份）。调用方负责先剔除不该
     *                  出现的人（如邀请时剔除已在群内的、禁言时剔除群主），
     *                  本类不认识"群"的概念、无从判断。
     *    departments： 部门列表，用于按部门分组；候选来自群成员时传空即可，
     *                  此时全部落进"未分组"一栏。
     */
    void OpenForPick(HWND owner, Purpose purpose, int64_t groupId, UINT selfUserId,
                     const std::vector<net::ContactItem>& candidates,
                     const std::vector<net::DepartmentItem>& departments);

    BEGIN_MSG_MAP(CCreateGroupDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  OpenForCreate 与 OpenForPick 共用的打开流程：存下候选数据、按需建窗、
     *  重建列表并前置显示。两个入口的差别只在事先设好的 m_purpose / m_groupId。
     *    owner：      owner 窗口句柄（当前实现传给 Create 的是 NULL，见实现内注释）；
     *    selfUserId： 当前登录用户 userid；
     *    contacts：   候选人资料池；
     *    departments：部门列表。
     */
    void OpenInternal(HWND owner, UINT selfUserId,
                      const std::vector<net::ContactItem>& contacts,
                      const std::vector<net::DepartmentItem>& departments);

    // 构建一次静态骨架（搜索框 / 群名框 / 两个滚动区 / 已选计数 / 取消+确认按钮）。
    void BuildUiOnce();

    // 构建底部栏（已选计数 + 取消 + 确认按钮）并挂到根容器上。各用途的右列版式
    // 不同、底部栏却是同一条，故抽出来共用，按钮文案由 ConfirmButtonText 决定。
    //   root：对话框根容器，本函数把底部栏 AddChild 进去。
    void BuildBottomBar(balloonwjui::DuiVBox& root);

    // 用 m_contacts + m_departments（按 filter 过滤）重建左侧可勾选候选树。
    //   filter：搜索词（空=不过滤）；按昵称/账号大小写不敏感子串匹配。
    // 树的层级 / 排序 / 过滤规则由 MemberPickerTreeModel 的纯函数决定，本函数只把
    // 它算出的节点序列喂给 DuiTreeView，并给每个节点挂一个 MemberPickerRow 行控件。
    void RebuildMemberList(const CString& filter);

    // 按当前 m_selected 刷新树里每个节点行控件的勾选态（成员两态、部门三态）。
    // 勾选变化后调用；不重建节点，只改行控件的显示。
    void RefreshTreeCheckStates();

    // 把树在滚动区里的高度设为它的内容总高 —— 单列 DuiTreeView 自己不滚，得靠外层
    // DuiScrollView 滚，故树必须"长到和内容一样高"。展开 / 收起部门后也要重调。
    void SyncTreeHeight();

    // 处理树里复选框被点击：成员节点切自己，部门节点按当前态全选 / 全不选其下成员。
    //   nodeId：被点中的树节点 id。
    void OnTreeCheckToggled(int nodeId);

    // 用 m_selected 重建右侧"已选成员"区，并刷新底部"已选 N 人"计数。
    void RebuildSelected();

    // 把一个 userid 加入 / 移出已选集合（去重、保序）。
    void AddSelected(UINT userId);
    void RemoveSelected(UINT userId);

    // 点底部确认按钮：按 m_purpose 决定发哪条协议。
    //   kPurposeCreateGroup —— 收集已选成员 + 群名（空则自动生成），发 cmd_create_group；
    //   其余用途 —— 见 DoPickConfirm。
    void DoCreate();

    // "选人"用途下点确认：按 m_purpose 发出对应协议并关窗。
    // 与建群不同，这三种用途都不等应答改界面 —— 服务端写成功后会推 1317/1318，
    // 由群管理窗统一刷新；失败也由它按 code 提示。
    void DoPickConfirm();

    // 当前用途下底部确认按钮该显示什么文案。
    LPCTSTR ConfirmButtonText() const;

    // 当前用途下窗口标题该显示什么。
    LPCTSTR WindowTitleText() const;

    // 在 m_contacts 缓存里按 userid 取联系人；找不到返回 NULL。
    const net::ContactItem* FindContact(UINT userId) const;

    // 取某联系人的显示名（昵称→账号→"用户<id>"）。
    CString DisplayNameOf(const net::ContactItem& c) const;

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），引用不持有
    UINT             m_selfUserId;    // 当前登录用户 userid（列表排除自己）

    // 本次打开的用途，决定界面细节与点确认后发哪条协议。取值见 Purpose。
    Purpose          m_purpose;

    // "选人"用途下的目标群ID；kPurposeCreateGroup 时为 0（那时群还不存在）。
    int64_t          m_groupId;

    std::vector<net::ContactItem>    m_contacts;     // 联系人资料池（复制）
    std::vector<net::DepartmentItem> m_departments;  // 部门列表（复制）

    // 已选成员 userid（保序，按勾选先后）。
    std::vector<UINT> m_selected;

    // 控件指针（借用，控件树持有）：动态刷新时用。
    balloonwjui::DuiEdit*   m_searchInput;   // 搜索联系人输入框
    balloonwjui::DuiEdit*   m_nameInput;     // 群聊名称输入框
    balloonwjui::DuiScrollView* m_memberScroll;  // 左侧候选树的滚动视口
    // 左侧候选树（借用，滚动区的内容容器持有）。单列模式 + 每节点挂 MemberPickerRow，
    // 滚轮由它上抛给 m_memberScroll。
    balloonmain::MemberPickerTreeView* m_memberTree;
    balloonwjui::DuiScrollView* m_chipScroll;    // 右侧已选成员滚动区
    balloonwjui::DuiLabel*      m_countLabel;    // 底部"已选 N 人"标签

    // 当前已构建的这套界面是按哪种用途搭的。本对话框对象由主面板长期持有、几种
    // 用途轮流复用，而各用途的控件集合并不相同（建群多一个群名输入框），故换用途
    // 重开时必须把窗口销毁重建，不能沿用上一次搭好的界面。
    Purpose m_builtPurpose;

    bool m_built;          // 骨架是否已构建
    bool m_creating;       // 是否已发出建群请求、等待应答（防重复提交）

    // 标题栏左侧图标位图（程序图标合成到标题栏底色上）。DuiFrameWindow::SetIcon 只
    // 借用不释放，故本类持有、析构时 DeleteObject。
    HBITMAP m_hTitleIcon;

    // 候选树的节点序列（BuildMemberPickerTree 的结果）。下标即"节点在树里的第几个"，
    // 与 m_nodeIdByIndex 一一对应。每次重建候选树时刷新。
    std::vector<balloonmain::MemberPickerNode> m_treeNodes;
    // 节点下标 → DuiTreeView 节点 id（AddRoot / AddChild 的返回值）。
    std::vector<int>                           m_nodeIdByIndex;
    // DuiTreeView 节点 id → 节点下标。收到复选框点击通知时靠它反查是哪个节点。
    std::map<int, int>                         m_indexByNodeId;
    // "已选区 ✕ 按钮控件 id → userid"：每次重建已选区时刷新。
    std::map<UINT, UINT>                 m_chipRemoveCtrlToUser;
};
