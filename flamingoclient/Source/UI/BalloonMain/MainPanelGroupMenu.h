/**
 *  MainPanelGroupMenu.h
 *
 *  主面板群列表（「我的群聊」）行右键菜单的<u>纯数据模型</u>。把"菜单有哪些项、什么顺序、
 *  各项 id / 文本"从 CBalloonMainPanel 里抽出成一个不依赖 HWND、不依赖 Flamingo 业务对象的
 *  纯函数，便于被 FlamingoUnit 测试工程直接调用断言（真正的弹出与命令分发留在
 *  CBalloonMainPanel::ShowGroupContextMenu）。写法与同级的 MainPanelContactMenu /
 *  MainPanelConversationMenu 一致。
 *
 *  分组规则（2026-10-03 与用户确认，联系人 / 群 / 会话三个右键菜单共用）：主操作 → 查看 →
 *  标记与提醒 → 复制与编辑 → 管理 → 危险操作（只影响本人记录的在前，改变成员关系或删除会话的
 *  在最后），组与组之间一条分隔条；同一个动作在各菜单里叫法相同。
 *
 *  菜单形态：
 *      进入群聊
 *      开启字幕模式 / 关闭字幕模式 ← 2026-10-05 起；按本群的字幕当前是否开着切换文案
 *      ──────────
 *      查看群资料
 *      查看消息记录
 *      群管理                  ← 仅群主 / 管理员出现
 *      ──────────
 *      添加到收藏 / 取消收藏   ← 按当前收藏状态切换文案
 *      置顶群聊 / 取消置顶     ← 按当前置顶状态切换文案
 *      消息提醒设置 ▸          ← 2026-10-03 起取代「消息免打扰」，子菜单见 NotifyModeMenu.h
 *      ──────────
 *      复制群号
 *      复制群名
 *      ──────────
 *      清空聊天记录
 *      ──────────
 *      退出群聊
 *      解散群聊                ← 仅群主出现
 *
 *  另附一个小工具 FormatGroupNumber：把群 id 格式化成群号文字，菜单「复制群号」、会话菜单
 *  「复制群号」、群资料窗口点击群号复制共用，保证几处复制出来的内容一致。
 *
 *  典型用法：
 *      GroupMenuState st;
 *      st.m_canManage = (role == 群主 || role == 管理员);
 *      std::vector<GroupMenuEntry> rows = BuildGroupMenuModel(st);
 *      // 逐行喂给 DuiMenu：分隔条走 AppendSeparator，其余走 AppendItem
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <windows.h>      // UINT
#include <string>
#include <vector>

namespace balloonmain {

/**
 *  GroupMenuId —— 群列表右键菜单各项的命令 id。点击后由 DuiMenu::TrackPopup 返回该值，
 *  CBalloonMainPanel::ShowGroupContextMenu 据此分发。
 *
 *  取值<u>必须非 0</u>：0 是 DuiMenu::TrackPopup 的"未选中"（失焦 / ESC）返回值。分隔条不是
 *  功能项、没有 id（其 GroupMenuEntry::m_id 取 0）。2～10 与本模型抽出之前 CBalloonMainPanel
 *  里那份枚举逐一对应，没有重新编号。
 */
enum GroupMenuId : UINT
{
    // 1 曾是顶部群名（禁用项，作菜单标题），已与联系人 / 会话两个菜单一同去掉。此 id 撤销，
    // 日后不要复用 1。
    kGroupMenuEnter       = 2,    // 进入群聊 —— 打开群聊窗
    kGroupMenuViewInfo    = 3,    // 查看群资料 —— 打开群资料窗口
    kGroupMenuManage      = 4,    // 群管理 —— 打开群管理窗口（仅群主 / 管理员可见）
    kGroupMenuPin         = 5,    // 置顶群聊 / 取消置顶
    // 6 曾是「消息免打扰 / 取消免打扰」，2026-10-03 由「消息提醒设置」二级菜单（kGroupMenuNotifyMode）取代，不要复用
    kGroupMenuClearMsgs   = 7,    // 清空聊天记录 —— 本人视角清空本群的聊天记录，本人各设备同步，其他群成员不受影响
    kGroupMenuQuit        = 8,    // 退出群聊 —— 群主点了走"先转让群主再退出"，其余成员确认后直接退群
    kGroupMenuViewHistory = 9,    // 查看消息记录 —— 打开消息记录窗口并定位到本群
    kGroupMenuFavorite    = 10,   // 添加到收藏 / 取消收藏
    kGroupMenuCopyGroupNo   = 11, // 复制群号 —— 把群号（十进制数字）写入剪贴板
    kGroupMenuCopyGroupName = 12, // 复制群名 —— 把群名写入剪贴板
    kGroupMenuDismiss     = 13,   // 解散群聊 —— 仅群主可见，确认后解散本群
    kGroupMenuNotifyMode  = 14,   // 消息提醒设置 ▸ —— 二级菜单的父项，本身点不了；子菜单各项的 id 见 NotifyModeMenu.h
    kGroupMenuSubtitle    = 15,   // 开启字幕模式 / 关闭字幕模式 —— 开关本群的桌面字幕，不打开群聊窗；m_canSubtitle 为 true 时才出现
    kGroupMenuJoinLink    = 16,   // 生成加群链接 —— 打开「生成加群链接」窗口（docs/邀请入群方案.md §12）；m_canCreateLink 为 true 时才出现
};

/**
 *  GroupMenuState —— 生成菜单所需的当前状态，由 CBalloonMainPanel 在弹菜单前填好传入。
 */
struct GroupMenuState
{
    bool m_canManage;   // 本人是不是群主或管理员；为 true 时出现"群管理"
    bool m_pinned;      // 当前是否已置顶；决定置顶项文案是"置顶群聊"还是"取消置顶"
    int  m_notifyMode;  // 当前的消息提醒模式（Protocol.h 的 enum conversation_notify_mode）；决定子菜单里哪一项打勾
    bool m_favorite;    // 当前是否已收藏；决定收藏项文案是"取消收藏"还是"添加到收藏"
    bool m_isOwner;     // 本人是不是群主；为 true 时在"退出群聊"之后多出"解散群聊"
    bool m_canSubtitle;     // 是否提供字幕模式一项（主面板总是置 true；默认 false，不关心这一项的调用方与旧用例不受影响）
    bool m_subtitleActive;  // 本群的字幕此刻是否开着；决定文案是"关闭字幕模式"还是"开启字幕模式"
    bool m_canCreateLink;   // 是否提供「生成加群链接」（主面板总是置 true，群禁止加入时由服务端拒绝；默认 false，旧用例不受影响）
    bool m_isThreadGroup;   // 是不是帖子群：第一项改为「打开帖子群」，不提供字幕模式（docs/帖子群方案.md 14.1）

    GroupMenuState()
        : m_canManage(false)
        , m_pinned(false)
        , m_notifyMode(0)
        , m_favorite(false)
        , m_isOwner(false)
        , m_canSubtitle(false)
        , m_subtitleActive(false)
        , m_canCreateLink(false)
        , m_isThreadGroup(false)
    {
    }
};

/**
 *  GroupMenuEntry —— 菜单里的一行（功能项或分隔条）。
 */
struct GroupMenuEntry
{
    UINT           m_id;            // 功能项命令 id（见 GroupMenuId）；分隔条取 0
    bool           m_isSeparator;   // 是否为分隔条；true 时 m_label 无意义
    const wchar_t* m_label;         // 显示文本（宽字符常量串，进程级生命周期）；分隔条取 NULL
    bool           m_hasNotifySubMenu; // 是否为「消息提醒设置」二级菜单的父项：喂 DuiMenu 时走 AppendSubMenu，
                                       // 子菜单由 BuildNotifyModeSubMenu(true, 当前模式) 给出

    GroupMenuEntry()
        : m_id(0)
        , m_isSeparator(false)
        , m_label(NULL)
        , m_hasNotifySubMenu(false)
    {
    }
};

/**
 *  构造群列表右键菜单的行模型。纯函数、无副作用、不创建窗口。
 *    state：被右键那个群的当前状态（能否管理 / 是否群主 / 置顶 / 提醒模式 / 收藏）。
 *  @return 行模型 vector（值语义，调用方拥有）。m_label 指向静态常量串，可直接持有使用。
 */
std::vector<GroupMenuEntry> BuildGroupMenuModel(const GroupMenuState& state);

/**
 *  把群 id 格式化成群号文字（十进制数字，不带"群号"字样与分隔符），复制群号时写入剪贴板的
 *  就是它。群资料窗口显示的"群号 N"里的 N 与此一致。
 *    groupId：群 id；不大于 0 时返回空串（没有可复制的群号）。
 *  @return 群号文字。纯函数。
 */
std::wstring FormatGroupNumber(long long groupId);

} // namespace balloonmain
