/**
 *  BalloonGroupInfoLayout.h
 *
 *  「群资料」对话框的 balloonui 客户区控件树构造种子 + 数据注入 + 群名片
 *  查看/编辑双态切换。把对话框"长什么样"从对话框类里抽出成一个可独立编译、
 *  可被单元测试直接调用的纯逻辑模块 —— CBalloonGroupInfoDlg 与 FlamingoUnit
 *  测试工程共用本文件（与已删除的 BalloonProfileLayout 同套路）。
 *
 *  设计稿:design/群资料.png。自上而下:
 *    · 顶部 Banner:群头像(字形 monogram)+ 群名(大字)+ "群号 N · M 人" +
 *      "群主:<群主显示名>"
 *    · 群公告:浅橙填充圆角框(空时占位"暂无群公告")
 *    · "我在本群的昵称":右对齐值,整行点击就地编辑(view DuiLabel ↔ edit
 *      DuiEdit)
 *    · "消息提醒":DuiComboBox 下拉框,五种提醒模式(2026-10-03 起取代"消息免打扰"开关)
 *    · "置顶群聊":DuiSwitch
 *    · 底部:"查看消息记录"(本轮占位) / "退出群聊"(危险红)
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程共用;在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。include 它之前调用方须已引入 ATL/WTL
 *  (Flamingo 侧由 stdafx.h 保证)。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace net { struct GroupMemberItem; }   // ResolveXxx 入参,前置声明免引头

namespace balloonmain {

/**
 *  GroupInfoCtrlId —— 群资料对话框里"需要被代码引用"的所有控件数字 id。
 *
 *  取 360 段,与主面板(200 段)互不重叠(300 段原属个人信息对话框,该对话框 2026-10-07 已删除)。装饰性
 *  spacer / 固定静态文字在控件树里不写 id(GetCtrlId() 返 0),不属于本 enum。
 *  注意 DuiFrameWindow 保留 ctrlId 1/2/3(标题栏 min/max/close),360 段安全。
 */
enum GroupInfoCtrlId : UINT
{
    // ---- 顶部 Banner ----
    kIdGroupAvatar       = 360,  // 群头像格(字形 + 底色)
    kIdGroupName         = 361,  // 群名(大字;可点,点了复制完整群名)
    kIdGroupMeta         = 362,  // "群号 N · M 人"(可点,点了复制纯数字群号)
    kIdGroupOwner        = 363,  // "群主:<显示名>"

    // ---- 群公告 ----
    kIdGroupAnnouncement = 364,  // 公告正文 DuiLabel(置于填充圆角框内;空时显占位)

    // ---- 我在本群的昵称(查看/编辑双态;整行点击进编辑) ----
    kIdGroupCardLabel    = 365,  // 行左标签"我在本群的昵称"(可点,点了进编辑)
    kIdGroupCardView     = 366,  // 群名片值 查看 DuiLabel(可点,点了进编辑)
    kIdGroupCardEdit     = 367,  // 群名片值 编辑 DuiEdit(回车或失焦提交、Esc 取消;回车与 Esc 2026-10-07 起生效)

    // ---- 提醒模式与置顶 ----
    // 368 原为"消息免打扰" DuiSwitch,2026-10-03 改成下拉框 kIdGroupNotifyCombo,此号作废、不得复用
    kIdGroupPinSwitch    = 369,  // 置顶群聊 DuiSwitch

    // ---- 底部按钮 ----
    kIdGroupBtnHistory   = 370,  // 查看消息记录(打开本群的消息记录窗口)
    kIdGroupBtnQuit      = 371,  // 退出群聊(危险红)
    kIdGroupBtnDismiss   = 372,  // 解散群聊(危险红,仅群主可见)

    // ---- 提醒模式(续) ----
    kIdGroupNotifyCombo  = 373,  // 消息提醒 DuiComboBox;第 i 项即 NotifyModeMenu.h 的 GetNotifyModeChoices(true) 第 i 个模式

    // ---- 群简介(2026-10-10 补上;此前群管理页说「显示在群资料页」,这里却没有) ----
    kIdGroupIntro        = 374,  // 群简介正文 DuiLabel(单行,放不下以省略号结尾;空时显占位"暂无群简介")
};

// ---- 群资料对话框尺寸(逻辑像素,对照 design/群资料.png) ----
// 含标题栏的整窗初始尺寸。2026-10-02 修复 ResizeClient 叠加系统边框的问题时由 460×408 加上原先多叠加的 18×47
//（120 DPI 实测），保持当时的窗口大小；2026-10-10 补群简介一段（小标题、正文与间距）时整窗加高 kGroupInfoIntroBlockH。
// 放在头文件里，供布局单测按设计尺寸检查排得下。
const int kGroupInfoIntroBlockH = 52;    // 群简介一段占的高度：小标题 18 + 间距 4 + 正文 20 + 间距 10
const int kGroupInfoWidth       = 478;   // 整窗初始宽
const int kGroupInfoHeight      = 455 + kGroupInfoIntroBlockH;   // 整窗初始高（含标题栏）
const int kGroupInfoTitleBarH   = 36;    // 标题栏高
const int kGroupInfoMinWidth    = 460;   // 整窗最小宽
// 整窗最小高（含标题栏）：按最小宽排下全部内容实测需要 469（用例 GI10），取 470。原先的 408 连没有群简介时的内容
// 都排不下（差 9 像素，窗口拉到最小时底部按钮越出窗口），2026-10-10 补群简介时由 GI10 查出并一并改正
const int kGroupInfoMinHeight   = 470;

/**
 *  GroupInfoViewData —— 群资料对话框的数据输入(纯字段,与对话框类解耦,
 *  便于单测各种边界)。所有文本字段为 UI 宽字符 CString,调用方负责把
 *  网络层 UTF-8 转成 CString 后填入。
 */
struct GroupInfoViewData
{
    __int64  groupid;       // 群ID(用于"群号"显示与头像底色 hash)
    CString  groupName;     // 群名(空时头像 / 标题用占位)
    int      memberCount;   // 群成员总数(用于"M 人")
    CString  ownerName;     // 群主显示名(群名片优先,空回退账号昵称;未知时空串)
    CString  intro;         // 群简介(空时显占位"暂无群简介";群管理「基本信息」页里编辑)
    CString  announcement;  // 群公告(空时显占位"暂无群公告")
    CString  myCard;        // 我在本群的昵称(空时显占位"未设置")
    int      notifyMode;    // 消息提醒模式当前态(Protocol.h 的 enum conversation_notify_mode)
    bool     pinned;        // 置顶群聊当前态
    bool     isOwner;       // 本人是否为群主:为 true 时底部多出"解散群聊"按钮,"退出群聊"走先转让再退出

    GroupInfoViewData()
        : groupid(0)
        , memberCount(0)
        , notifyMode(0)
        , pinned(false)
        , isOwner(false)
    {
    }
};

/**
 *  构造「群资料」对话框的客户区控件树(程序化组合 balloonui 现成控件)。
 *  初始处于"查看态":群名片 edit(kIdGroupCardEdit)隐藏、view 可见。
 *  群名片编辑态切换由 SetGroupInfoCardEditMode 负责。
 *
 *  返回:客户区根控件(一个 vbox),调用方拿走所有权;构造失败返 nullptr
 *  (形态固定,理论上不会失败)。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildGroupInfoBody();

/**
 *  把 GroupInfoViewData 写入对话框控件树:群名、"群号 N · M 人"、群主行、
 *  群简介(空→占位)、公告(空→占位)、我的群名片(view 文字 + edit 初值)、提醒模式下拉框与置顶开关初值,并按 data.isOwner
 *  显示 / 隐藏"解散群聊"按钮(显隐变化时自行让底部按钮行重新排版)。群名片的查看 / 编辑态
 *  可见性不在这里切换(由 SetGroupInfoCardEditMode 处理)。
 *    root  BuildGroupInfoBody 返回的客户区根控件;null 返 false。
 *    data  群资料字段。
 *  返回:成功 true;root 为 null 或核心控件(群名 label)找不到时 false。
 */
bool ApplyGroupInfoDataToView(balloonwjui::DuiControl* root,
                              const GroupInfoViewData& data);

/**
 *  在「群资料」对话框群名片行的 查看态 / 编辑态 之间切换(纯 DUI 逻辑,可单测)。
 *    · 翻转 kIdGroupCardView / kIdGroupCardEdit 的可见性;
 *    · 对群名片行容器 ForceLayout(自身矩形),绕开 DuiControl::SetRect 在矩形
 *      未变时的提前返回(同 SetProfileEditMode 的坑规避),保证刚可见的编辑框
 *      拿到正确矩形、点得到、显示得出来。
 *    root  BuildGroupInfoBody 返回的客户区根控件;null 返 false。
 *    edit  true 切到编辑态(显 edit、隐 view);false 切回查看态。
 *  返回:root 非空即 true。
 */
bool SetGroupInfoCardEditMode(balloonwjui::DuiControl* root, bool edit);

/**
 *  从群成员列表里挑出"群主显示名":命中 userid==ownerid 的成员后,优先用其
 *  群名片(m_card),群名片为空则回退账号昵称(m_nickname)。找不到群主返回空串。
 *  纯函数,返回 UTF-8(与 GroupMemberItem 字段同口径),便于单测;调用方再转 CString。
 *    members  群成员列表(cmd_get_group_members 解析结果)。
 *    ownerid  群主 userid(来自 cmd_get_group_info)。
 *  返回:群主显示名(UTF-8);未命中返回空串。
 */
std::string ResolveGroupOwnerName(const std::vector<net::GroupMemberItem>& members,
                                  __int64 ownerid);

/**
 *  从群成员列表里挑出"我在本群的昵称(群名片)":命中 userid==myUserid 的成员后,
 *  优先用其群名片(m_card),为空则回退账号昵称(m_nickname)。找不到自己返回空串。
 *  纯函数,返回 UTF-8,便于单测;调用方再转 CString。
 *    members   群成员列表。
 *    myUserid  本人 userid。
 *  返回:我的群名片显示值(UTF-8);未命中返回空串。
 */
std::string ResolveMyGroupCard(const std::vector<net::GroupMemberItem>& members,
                               __int64 myUserid);

/**
 *  群资料窗口里点击群名 / 群号那一行时要复制的文字。
 *    data：   当前展示的群资料。
 *    ctrlId： 被点击的控件编号：kIdGroupName 复制完整群名；kIdGroupMeta 复制纯数字群号（不带
 *             "群号"字样与人数，与 balloonmain::FormatGroupNumber 一致）；其余编号不复制。
 *    text：   [出参] 要复制的文字；返回 false 时不可用。
 *  @return 有可复制的文字时返回 true；控件编号不对应、群名为空或群 id 不大于 0 时返回 false。纯函数。
 */
bool GroupInfoCopyText(const GroupInfoViewData& data, UINT ctrlId, CString& text);

/**
 *  「查看消息记录」按钮要打开的会话：群聊类型与群 id，交给主面板的 OpenHistory。
 *    data：  当前展示的群资料。
 *    ctype： [出参] 会话类型，恒为 2（群聊）。
 *    cid：   [出参] 群 id。
 *  @return 群 id 有效（大于 0）时返回 true；否则返回 false，出参不可用。纯函数。
 */
bool GroupInfoHistoryTarget(const GroupInfoViewData& data, int& ctype, __int64& cid);

/**
 *  复制提示条（挂在根容器上的 DuiToast）距根容器顶边的偏移，供 DuiToast::SetTopOffset 使用。
 *  提示条默认距顶 40 像素，正好压在群号那一行上；按本函数的结果放置后，提示条的文字框紧贴头部
 *  资料区（头像、群名、群号、群主）的下边，刚复制的群名、群号不会被遮住。
 *    root： BuildGroupInfoBody 建出的根控件，须已排过版；不转移所有权，不修改控件。
 *  @return 偏移（像素，与控件矩形同一单位，因此随 DPI 自动缩放）：头像与群主两行底边中较低者减去
 *          根容器顶边。root 为空或找不到头部控件时返回 -1，调用方应保持提示条原有位置。
 */
int GroupInfoToastTopOffset(balloonwjui::DuiControl* root);

} // namespace balloonmain
