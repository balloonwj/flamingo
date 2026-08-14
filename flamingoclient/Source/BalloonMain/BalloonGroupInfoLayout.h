/**
 *  BalloonGroupInfoLayout.h
 *
 *  「群资料」对话框的 balloonui 客户区控件树构造种子 + 数据注入 + 群名片
 *  查看/编辑双态切换。把对话框"长什么样"从对话框类里抽出成一个可独立编译、
 *  可被单元测试直接调用的纯逻辑模块 —— CBalloonGroupInfoDlg 与 FlamingoUnit
 *  测试工程共用本文件（与 BalloonProfileLayout 同套路）。
 *
 *  设计稿:design/群资料.png。自上而下:
 *    · 顶部 Banner:群头像(字形 monogram)+ 群名(大字)+ "群号 N · M 人" +
 *      "群主:<群主显示名>"
 *    · 群公告:浅橙填充圆角框(空时占位"暂无群公告")
 *    · "我在本群的昵称":右对齐值,整行点击就地编辑(view DuiLabel ↔ edit
 *      DuiEditHost)
 *    · "消息免打扰" / "置顶群聊":各一个 DuiSwitch
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
 *  取 360 段,与个人信息对话框(300 段)、主面板(200 段)互不重叠。装饰性
 *  spacer / 固定静态文字在控件树里不写 id(GetCtrlId() 返 0),不属于本 enum。
 *  注意 DuiFrameWindow 保留 ctrlId 1/2/3(标题栏 min/max/close),360 段安全。
 */
enum GroupInfoCtrlId : UINT
{
    // ---- 顶部 Banner ----
    kIdGroupAvatar       = 360,  // 群头像格(字形 + 底色)
    kIdGroupName         = 361,  // 群名(大字)
    kIdGroupMeta         = 362,  // "群号 N · M 人"
    kIdGroupOwner        = 363,  // "群主:<显示名>"

    // ---- 群公告 ----
    kIdGroupAnnouncement = 364,  // 公告正文 DuiLabel(置于填充圆角框内;空时显占位)

    // ---- 我在本群的昵称(查看/编辑双态;整行点击进编辑) ----
    kIdGroupCardLabel    = 365,  // 行左标签"我在本群的昵称"(可点,点了进编辑)
    kIdGroupCardView     = 366,  // 群名片值 查看 DuiLabel(可点,点了进编辑)
    kIdGroupCardEdit     = 367,  // 群名片值 编辑 DuiEditHost(回车/失焦提交)

    // ---- 开关 ----
    kIdGroupMuteSwitch   = 368,  // 消息免打扰 DuiSwitch
    kIdGroupPinSwitch    = 369,  // 置顶群聊 DuiSwitch

    // ---- 底部按钮 ----
    kIdGroupBtnHistory   = 370,  // 查看消息记录(本轮占位)
    kIdGroupBtnQuit      = 371,  // 退出群聊(危险红)
};

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
    CString  announcement;  // 群公告(空时显占位"暂无群公告")
    CString  myCard;        // 我在本群的昵称(空时显占位"未设置")
    bool     mute;          // 消息免打扰当前态
    bool     pinned;        // 置顶群聊当前态

    GroupInfoViewData()
        : groupid(0)
        , memberCount(0)
        , mute(false)
        , pinned(false)
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
 *  公告(空→占位)、我的群名片(view 文字 + edit 初值)、两个开关初值。
 *  不切换可见性(由对话框 / SetGroupInfoCardEditMode 处理)。
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

} // namespace balloonmain
