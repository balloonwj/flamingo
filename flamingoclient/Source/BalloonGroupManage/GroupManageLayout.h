/**
 *  GroupManageLayout.h
 *
 *  群管理对话框客户区控件树的构造接口。把「顶部 Header」「左侧导航」
 *  「右侧各页内容」三部分的构建拆成独立函数，由 CBalloonGroupManageWindow
 *  组装与切换。各页内容用 balloonui 现成控件 + 本目录自绘小控件以程序化
 *  方式搭建（不走 XML），便于由模型数据动态生成成员卡 / 申请卡等。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <memory>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloongroupmanage {

class GroupManageModel;

// =====================================================================
// 页面 id（同时用作左侧导航项的 ctrl id —— 点击导航项时窗口据此切页）
// =====================================================================
enum GmPageId
{
    kPageMembers   = 1001,   // 成员管理
    kPageApproval  = 1002,   // 加群审批
    kPageMute      = 1003,   // 禁言管理
    kPageBasicInfo = 1004,   // 基本信息
    kPagePerms     = 1005,   // 群权限
    kPageDanger    = 1006,   // 危险区域

    // 敏感词库管理。这是从群权限页「管理词库」按钮进入的<u>二级</u>界面，
    // 不出现在左侧导航里 —— 它属于"消息敏感词过滤"这一项的细节，放进导航
    // 会让六项主功能变成七项、层级也不对。页内左上角有返回群权限的入口。
    kPageSensitive = 1007,

    // 群公告。对照 design/群管理7.png —— 它在左侧导航里<u>自成一个分组</u>
    //（「群公告」组，夹在「成员」组与「设置」组之间），页内是「发布新公告」
    // 表单加「历史公告」列表。仅群主 / 管理员可见；普通成员在群聊窗右侧边栏
    // 的「公告」Tab 里看。
    kPageAnnouncement = 1008
};

// =====================================================================
// 各页内输入框的 ctrl id
// （供窗口按 id 取回控件、读写其中文本；只读下拉框不在此列）
// =====================================================================
enum GmEditId
{
    kIdSearchMembers    = 2001,   // 成员管理页：搜索框
    kIdEditGroupName    = 2002,   // 基本信息页：群名称
    //2003 曾是基本信息页的「群公告」多行输入框。群公告改为一条条带标题 / 发布人 /
    //优先级的记录后，编辑入口统一收到「群公告」页（kPageAnnouncement），基本信息页
    //只留一行只读摘要，故该 id 作废、不再复用，避免与旧版本的界面语义混淆。
    kIdEditIntro        = 2004,   // 基本信息页：群简介
    kIdEditSensitiveNew = 2005,    // 管理词库页：新增敏感词输入框
    kIdEditAnnounceTitle = 2006,   // 群公告页：新公告标题（单行，必填）
    kIdEditAnnounceBody  = 2007    // 群公告页：新公告正文（多行）
};

// =====================================================================
// 其它可点击控件 ctrl id（按钮 / 开关 / 下拉框；窗口按需处理通知）
// =====================================================================
enum GmCtrlId
{
    kIdBtnInvite        = 2101,   // 成员管理：邀请成员
    kIdBtnApproveAll    = 2111,   // 加群审批：批量同意
    kIdBtnRejectAll     = 2112,   // 加群审批：批量拒绝
    kIdSwitchMuteAll    = 2121,   // 禁言管理：全员禁言
    kIdBtnAddMute       = 2122,   // 禁言管理：添加禁言成员
    kIdBtnCopyGroupNo   = 2131,   // 基本信息：复制群号
    kIdBtnChangeAvatar  = 2132,   // 基本信息：更换头像
    kIdBtnReset         = 2133,   // 基本信息：重置
    kIdBtnSave          = 2134,   // 基本信息：保存修改
    kIdComboJoinMethod  = 2141,   // 群权限：加群方式
    kIdComboAtAllScope  = 2142,   // 群权限：谁能 @ 全体成员
    kIdSwitchHistory    = 2143,   // 群权限：新成员查看历史消息
    kIdSwitchInvite     = 2144,   // 群权限：成员邀请好友加群
    kIdSwitchWelcome    = 2145,   // 群权限：入群欢迎语
    kIdSwitchSensitive  = 2146,   // 群权限：消息敏感词过滤
    kIdBtnSensitiveDict = 2147,   // 群权限：管理词库
    kIdBtnTransfer      = 2161,   // 危险区域：转让群主
    kIdBtnRemoveAll     = 2162,   // 危险区域：移除全部成员
    kIdBtnDismiss       = 2163,   // 危险区域：解散群聊

    // 加群申请 / 禁言 列表里逐条按钮的 id 基址（实际 id = 基址 + 行号）。
    kIdApprovalAcceptBase = 2200,   // 申请卡「同意」
    kIdApprovalRejectBase = 2250,   // 申请卡「拒绝」
    kIdMuteExtendBase     = 2300,   // 禁言卡「延长」
    kIdMuteReleaseBase    = 2350,   // 禁言卡「解除」

    // 成员卡的 id 基址（实际 id = 基址 + 该成员在 model.m_members 里的下标）。
    // 成员卡本身不是按钮，给它 id 是为了右键时能认出点的是哪一位成员 ——
    // 通知里带的是 ctrlId，减去基址即得下标，再回模型里取 userid 与角色。
    // 基址取 2400 是为了与上面几组逐行按钮的 id 段（2200~2399，每段 50 个）
    // 拉开距离；成员上限按每群 500 人算，故本段独占 2400~2899。
    kIdMemberCardBase     = 2400,
    kIdMemberCardMax      = 2899,

    // ---- 管理词库页 ----
    kIdBtnSensitiveBack   = 2900,   // 返回群权限
    kIdBtnSensitiveAdd    = 2901,   // 添加敏感词

    // 逐条词的「删除」按钮 id 基址（实际 id = 基址 + 该词在 m_sensitiveWords
    // 里的下标）。单群词库按 200 条封顶，故独占 2910~3109。
    kIdSensitiveDelBase   = 2910,
    kIdSensitiveDelMax    = 3109,

    // ---- 群公告页 ----
    kIdChipSeverityNormal    = 3110,   // 优先级 chip：通知
    kIdChipSeverityImportant = 3111,   // 优先级 chip：重要
    kIdChipSeverityUrgent    = 3112,   // 优先级 chip：紧急
    kIdBtnAnnounceClear      = 3113,   // 清空（把标题与正文输入框都倒空）
    kIdBtnAnnouncePublish    = 3114,   // 发布公告
    kIdBtnAnnounceGoto       = 3115,   // 基本信息页上的「去群公告页管理」跳转按钮

    kIdAnnouncePagePrev      = 3116,   // 历史公告：上一页（‹）
    kIdAnnouncePageNext      = 3117,   // 历史公告：下一页（›）

    // 历史公告逐条按钮的 id 基址（实际 id = 基址 + 该条在<u>本页</u> m_announcements
    // 里的下标）。列表分页后一页至多 kAnnouncePageSize 条，故每段留 20 个足够，
    // 留出余量以防日后加大页容量。
    kIdAnnounceRepublishBase = 3120,   // 「重新发布」
    kIdAnnounceRepublishMax  = 3139,
    kIdAnnounceDeleteBase    = 3140,   // 「删除」（真删，不可恢复）
    kIdAnnounceDeleteMax     = 3159
};

// =====================================================================
// 构建函数
// =====================================================================

// 构建顶部 Header（群头像 + 群名 + 「我是群主·N 名成员」副标题）。
//   model：数据来源（读取群名 / 头像 / 成员数）。
//   返回：Header 控件（调用方负责放入根容器）。
std::unique_ptr<balloonwjui::DuiControl> BuildHeader(const GroupManageModel& model);

// 构建左侧导航栏（分组标题 + 6 个导航项，导航项 ctrl id 见 GmPageId）。
//   model：数据来源（读取各计数徽章数值）。
//   返回：导航栏控件。
std::unique_ptr<balloonwjui::DuiControl> BuildNav(const GroupManageModel& model);

// 构建某一页的内容（放进窗口右侧滚动视口）。
//   pageId：页面 id，取值见 GmPageId；越界返回空白页。
//   model： 数据来源。
//   返回：该页内容控件（其内部子控件高度均为固定值，便于滚动视口测高）。
std::unique_ptr<balloonwjui::DuiControl> BuildPage(int pageId,
                                                   const GroupManageModel& model);

} // namespace balloongroupmanage
