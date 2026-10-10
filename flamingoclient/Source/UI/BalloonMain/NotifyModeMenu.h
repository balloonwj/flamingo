/**
 *  NotifyModeMenu.h
 *
 *  右键菜单里「消息提醒设置 ▸」二级菜单的<u>纯数据模型</u>。会话列表、群列表、联系人（通讯录）
 *  三个右键菜单共用这一份：子菜单有哪些项、什么顺序、哪一项打勾、各项的命令 id，以及命令 id
 *  与提醒模式之间的换算（docs/消息提醒模式与消息盒子方案.md §4.1）。另附群聊窗成员菜单里
 *  「特别关注 / 取消特别关注」快捷项的文案。
 *
 *  子菜单形态（2026-10-03 与用户确认，三个菜单文字完全相同）：
 *      特别关注
 *      允许消息提醒
 *      只提醒 @我和关注的人  ← 只有群聊出现（2026-10-06，docs/消息提醒模式与消息盒子方案.md §12）
 *      接收消息但不提醒
 *      收进消息盒子且不提醒
 *      屏蔽消息              ← 只有群聊出现
 *      ──────────
 *      批量管理消息提醒
 *      管理关注的成员…       ← 只有群聊出现，打开选人窗口编辑本群的关注名单
 *  当前模式那一项打勾，其余不打勾。
 *
 *  本模块不碰窗口，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在
 *  Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法（CBalloonMainPanel 弹右键菜单时）：
 *      balloonwjui::DuiMenu sub;
 *      std::vector<NotifyModeMenuEntry> rows = BuildNotifyModeSubMenu(isGroup, currentMode);
 *      //逐行喂给 sub：分隔条走 AppendSeparator，m_checkable 的走 AppendChecked，其余走 AppendItem
 *      menu.AppendSubMenu(parentId, GetNotifyModeMenuTitle(), &sub);
 *      ...
 *      int mode = NotifyModeFromMenuId(chosen);   //点的是模式项时得到目标模式
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <windows.h>      // UINT
#include <vector>

namespace balloonmain {

/**
 *  NotifyModeMenuId —— 「消息提醒设置」子菜单各项的命令 id。点击后由 DuiMenu::TrackPopup 返回给
 *  弹出菜单的那一处分发。三个右键菜单各自的主菜单 id 都在 2~20 之间，这一段从 101 起，不会撞上。
 *  取值必须非 0（0 是 TrackPopup 的"未选中"）。
 */
enum NotifyModeMenuId : UINT
{
    kNotifyMenuStarred = 101,   // 特别关注 —— 收到消息时直接弹出聊天窗；联系人上下线时还会弹提示
    kNotifyMenuAllow   = 102,   // 允许消息提醒 —— 默认模式
    kNotifyMenuSilent  = 103,   // 接收消息但不提醒 —— 原「消息免打扰」
    kNotifyMenuMsgbox  = 104,   // 收进消息盒子且不提醒
    kNotifyMenuBlock   = 105,   // 屏蔽消息 —— 只有群聊出现
    kNotifyMenuMentionFollow = 106, // 只提醒 @我和关注的人 —— 只有群聊出现
    kNotifyMenuBatch   = 110,   // 批量管理消息提醒 —— 打开批量管理窗口
    kNotifyMenuManageFollow = 111,  // 管理关注的成员… —— 只有群聊出现，打开选人窗口编辑本群的关注名单
};

/**
 *  NotifyModeMenuEntry —— 子菜单里的一行（功能项或分隔条）。
 */
struct NotifyModeMenuEntry
{
    UINT           m_id;            // 功能项命令 id（见 NotifyModeMenuId）；分隔条取 0
    bool           m_isSeparator;   // 是否为分隔条；true 时其余字段无意义
    const wchar_t* m_label;         // 显示文本（宽字符常量串，进程级生命周期）；分隔条取 NULL
    bool           m_checkable;     // 是否为可打勾的模式项（喂 DuiMenu 时走 AppendChecked）；「批量管理」为 false
    bool           m_checked;       // 是否打勾：只有当前模式那一项为 true

    NotifyModeMenuEntry()
        : m_id(0)
        , m_isSeparator(false)
        , m_label(NULL)
        , m_checkable(false)
        , m_checked(false)
    {
    }
};

/**
 *  二级菜单父项的文字「消息提醒设置」。三个右键菜单共用，保证叫法相同。
 *  返回：宽字符常量串，进程级生命周期。
 */
const wchar_t* GetNotifyModeMenuTitle();

/**
 *  构造「消息提醒设置」子菜单的行模型。纯函数。
 *    isGroup：     是否群会话；为 false 时不出现「只提醒 @我和关注的人」「屏蔽消息」与「管理关注的成员…」。
 *    currentMode： 该会话当前的提醒模式（Protocol.h 的 enum conversation_notify_mode），对应那一项打勾；
 *                  不认识的值按允许提醒处理。
 *  返回：行模型 vector（值语义，调用方拥有）；m_label 指向静态常量串，可直接持有使用。
 */
std::vector<NotifyModeMenuEntry> BuildNotifyModeSubMenu(bool isGroup, int currentMode);

/**
 *  把子菜单命令 id 换算成目标提醒模式。纯函数。
 *    id： TrackPopup 的返回值。
 *  返回：六个模式项之一时返回对应的 enum conversation_notify_mode 取值；「批量管理」「管理关注的成员…」、
 *        分隔条、其它菜单的 id 一律返回 -1。
 */
int NotifyModeFromMenuId(UINT id);

/**
 *  资料窗「消息提醒」下拉框（群资料窗、联系人资料窗）里各选项对应的提醒模式，顺序与二级菜单相同：
 *  特别关注、允许消息提醒、（群聊）只提醒 @我和关注的人、接收消息但不提醒、收进消息盒子且不提醒，
 *  群聊再加屏蔽消息。下拉框的第 i 项即本函数结果的第 i 个模式。纯函数。
 *    isGroup： 是否群会话；为 false 时没有「只提醒 @我和关注的人」与屏蔽消息。
 */
std::vector<int> GetNotifyModeChoices(bool isGroup);

/**
 *  提醒模式在资料窗下拉框里的选项下标（GetNotifyModeChoices(isGroup) 中的位置）。纯函数。
 *    isGroup： 是否群会话。
 *    mode：    enum conversation_notify_mode 的取值；不认识的值、以及单聊传入只有群聊才有的模式时按允许提醒处理。
 *  返回：选项下标，必在 [0, GetNotifyModeChoices(isGroup).size()) 之内。
 */
int NotifyModeChoiceIndex(bool isGroup, int mode);

/**
 *  资料窗下拉框的选项下标换算成提醒模式，NotifyModeChoiceIndex 的逆运算。纯函数。
 *    isGroup： 是否群会话。
 *    index：   DuiComboBox::GetCurSel() 的返回值；越界（含 -1）时按允许提醒处理。
 *  返回：enum conversation_notify_mode 的取值。
 */
int NotifyModeOfChoice(bool isGroup, int index);

/**
 *  某种提醒模式在菜单与下拉框里的文字（与二级菜单逐字相同）。
 *    mode： enum conversation_notify_mode 的取值；不认识的值按允许提醒处理。
 *  返回：宽字符常量串，进程级生命周期。
 */
const wchar_t* GetNotifyModeLabel(int mode);

/**
 *  群聊窗成员菜单（成员列表、气泡里的 @ 片段与发送者头像共用）里特别关注快捷项的文案，
 *  随该成员当前是否被特别关注取反：「特别关注」/「取消特别关注」。
 *    isStarred： 该成员的单聊会话当前是否为特别关注。
 *  返回：宽字符常量串，进程级生命周期。
 */
const wchar_t* GetStarredMenuLabel(bool isStarred);

} // namespace balloonmain
