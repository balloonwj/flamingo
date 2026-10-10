/**
 *  MainPanelConversationMenu.h
 *
 *  "消息"Tab 最近会话列表右键菜单的<u>纯数据模型</u>。把"菜单有哪些项、什么顺序、
 *  各项 id / 文本 / 是否可用"从 CBalloonMainPanel 里抽出成一个不依赖 HWND、不依赖
 *  Flamingo 业务对象的纯函数，便于被 FlamingoUnit 测试工程直接调用断言（真正的
 *  弹出与命令分发留在 CBalloonMainPanel::ShowConversationContextMenu）。
 *
 *  分组规则（2026-10-03 与用户确认，联系人 / 群 / 会话三个右键菜单共用）：主操作 → 查看 →
 *  标记与提醒 → 复制与编辑 → 管理 → 危险操作（只影响本人记录的在前，改变成员关系或删除会话的
 *  在最后），组与组之间一条分隔条；同一个动作在各菜单里叫法相同。
 *
 *  菜单结构（自上而下）：
 *      打开会话
 *      标记已读 / 标为未读    ← 有未读或已手动标为未读时为「标记已读」，否则为「标为未读」（2026-10-06，
 *                               docs/会话标为未读方案.md；此前未读为 0 时「标记已读」灰显）
 *      共享文件给TA / 共享文件到本群 ← 2026-10-03 起；单聊与群聊文案不同，本客户端的文件共享可用时才出现
 *      共享屏幕给TA / 共享屏幕到本群 ← 2026-10-03 起；本客户端的屏幕共享可用时才出现
 *      开启字幕模式 / 关闭字幕模式 ← 2026-10-05 起；按这个会话的字幕当前是否开着切换文案
 *      ──────────
 *      查看资料 / 查看群资料  ← 按会话类型切换文案
 *      查看消息记录
 *      ──────────
 *      添加到收藏 / 取消收藏  ← 按当前收藏状态切换文案
 *      置顶会话 / 取消置顶    ← 按当前置顶状态切换文案
 *      消息提醒设置 ▸         ← 2026-10-03 起取代「消息免打扰」「特别关注」两项，子菜单见 NotifyModeMenu.h
 *      ──────────
 *      复制名字              ← 仅单聊会话；复制列表上显示的名字（备注优先）
 *      复制群号 / 复制群名    ← 仅群会话，两项
 *      ──────────
 *      清空聊天记录
 *      ──────────
 *      删除该会话
 *      删除全部会话
 *
 *  菜单顶部原本还有一行会话名（禁用项，作标题），已经去掉：菜单是从会话行上右键点
 *  出来的，点的是哪条会话本来就一目了然，标题只是把功能项整体往下推一截。联系人与
 *  群聊两个右键菜单的同款标题项也一并去掉了，三处保持一致。
 *
 *  各项均<u>不带图标</u>，与同级的联系人右键菜单、群聊右键菜单保持一致（那两个菜单
 *  的图标资源尚未补齐，先统一为纯文字，避免同一处三个菜单观感不一）。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#pragma once

#include <windows.h>      // UINT
#include <vector>

namespace balloonmain {

/**
 *  ConversationMenuId —— 会话右键菜单各项的命令 id。点击后由 DuiMenu::TrackPopup
 *  返回该值，CBalloonMainPanel::ShowConversationContextMenu 据此分发。
 *
 *  取值<u>必须非 0</u>：0 是 DuiMenu::TrackPopup 的"未选中"（失焦 / ESC）返回值，
 *  被功能项占用的话，用户按 ESC 关掉菜单会被当成选了那一项。分隔条不是功能项、
 *  没有 id（其 ConversationMenuEntry::m_id 取 0）。
 */
enum ConversationMenuId : UINT
{
    // 1 曾是顶部会话名（禁用项，作菜单标题，由调用方追加）。该标题项已去掉，此 id
    // 一并撤销；日后新增菜单项另取新值，不要复用 1。
    kConvMenuOpen      = 2,   // 打开会话 —— 开聊天窗并清未读，等同于左键单击该行
    kConvMenuMarkRead  = 3,   // 标记已读 —— 不开窗，只清未读并上报已读位点
    kConvMenuHistory   = 4,   // 查看消息记录 —— 打开消息记录窗并定位到本会话
    kConvMenuProfile   = 5,   // 查看资料（单聊）/ 查看群资料（群聊）
    kConvMenuPin       = 6,   // 置顶会话 / 取消置顶
    // 7 曾是「消息免打扰 / 取消免打扰」，2026-10-03 由「消息提醒设置」二级菜单（kConvMenuNotifyMode）取代，不要复用
    kConvMenuDelete    = 8,   // 删除该会话 —— 仅从最近会话列表移除，不删聊天消息
    kConvMenuDeleteAll = 9,   // 删除全部会话 —— 同上，作用于列表里的每一条会话
    kConvMenuFavorite  = 10,  // 添加到收藏 / 取消收藏 —— 把该联系人或群加进「我的收藏」
    // 11 曾是「特别关注 / 取消特别关注」（仅单聊），2026-10-03 并入「消息提醒设置」二级菜单，不要复用
    kConvMenuCopyName      = 12,  // 复制名字 —— 仅单聊会话出现；复制列表上显示的名字（备注优先，其次昵称）
    kConvMenuCopyGroupNo   = 13,  // 复制群号 —— 仅群会话出现；复制十进制数字群号
    kConvMenuCopyGroupName = 14,  // 复制群名 —— 仅群会话出现
    kConvMenuClearHistory  = 15,  // 清空聊天记录 —— 本人视角清空该会话的聊天记录（本人各设备同步、对方与其他群成员不受影响），会话随之从列表移除
    kConvMenuShareFile     = 16,  // 共享文件给TA（单聊：发起共享对话框对象固定为对方，成功后打开单聊）/ 共享文件到本群（群聊：打开群聊窗与该群的发起共享对话框）；文件共享可用时才出现
    kConvMenuShareScreen   = 17,  // 共享屏幕给TA / 共享屏幕到本群 —— 打开该会话的聊天窗并开始屏幕共享（先选共享内容）；屏幕共享可用时才出现
    kConvMenuNotifyMode    = 18,  // 消息提醒设置 ▸ —— 二级菜单的父项，本身点不了；子菜单各项的 id 见 NotifyModeMenu.h
    kConvMenuSubtitle      = 19,  // 开启字幕模式 / 关闭字幕模式 —— 开关这个会话的桌面字幕，不打开聊天窗；m_canSubtitle 为 true 时才出现
    kConvMenuMarkUnread    = 20,  // 标为未读 —— 给会话画一个不带数字的小红点，存服务端、本人各设备同步；与「标记已读」二选一出现
    kConvMenuJoinLink      = 21,  // 生成加群链接 —— 仅群会话、m_canCreateLink 为 true 时出现，打开「生成加群链接」窗口（docs/邀请入群方案.md §12）
};

/**
 *  ConversationMenuState —— 生成菜单所需的会话当前状态。由 CBalloonMainPanel 从
 *  会话缓存（m_conversations）里取出被右键那条会话的字段填好后传入。
 */
struct ConversationMenuState
{
    bool m_isGroup;   // 是否群会话（ctype==2）；决定"查看资料"还是"查看群资料"
    bool m_pinned;    // 当前是否已置顶；决定置顶项文案是"置顶会话"还是"取消置顶"
    int  m_notifyMode; // 当前的消息提醒模式（Protocol.h 的 enum conversation_notify_mode）；决定子菜单里哪一项打勾
    int  m_unread;    // 当前未读消息数；与 m_unreadMarked 一起决定第二项是「标记已读」还是「标为未读」
    bool m_unreadMarked; // 当前是否已被手动标为未读（docs/会话标为未读方案.md）
    bool m_favorite;  // 该联系人 / 群当前是否已收藏；决定收藏项文案是"取消收藏"还是"添加到收藏"
    bool m_canShareFile;    // 本客户端的文件共享是否可用（FlamingoFileShare.dll 已加载且本企业开关打开）；为 false 时没有共享文件一项
    bool m_canShareScreen;  // 本客户端的屏幕共享是否可用（FlamingoMedia.dll 已加载）；为 false 时没有共享屏幕一项
    bool m_canSubtitle;     // 是否提供字幕模式一项（主面板总是置 true；默认 false，不关心这一项的调用方与旧用例不受影响）
    bool m_subtitleActive;  // 这个会话的字幕此刻是否开着；决定文案是"关闭字幕模式"还是"开启字幕模式"
    bool m_canCreateLink;   // 群会话是否提供「生成加群链接」（主面板总是置 true；默认 false，旧用例不受影响）

    ConversationMenuState()
        : m_isGroup(false)
        , m_pinned(false)
        , m_notifyMode(0)
        , m_unread(0)
        , m_unreadMarked(false)
        , m_favorite(false)
        , m_canShareFile(false)
        , m_canShareScreen(false)
        , m_canSubtitle(false)
        , m_subtitleActive(false)
        , m_canCreateLink(false)
    {
    }
};

/**
 *  ConversationMenuEntry —— 菜单里的一行（功能项或分隔条）。
 */
struct ConversationMenuEntry
{
    UINT           m_id;            // 功能项命令 id（见 ConversationMenuId）；分隔条取 0
    bool           m_isSeparator;   // 是否为分隔条；true 时 m_label / m_enabled 无意义
    const wchar_t* m_label;         // 显示文本（宽字符常量串，进程级生命周期）；分隔条取 NULL
    bool           m_enabled;       // 是否可点击；false 为灰显 no-op
    bool           m_hasNotifySubMenu; // 是否为「消息提醒设置」二级菜单的父项：喂 DuiMenu 时走 AppendSubMenu，
                                       // 子菜单由 BuildNotifyModeSubMenu 按 ConversationMenuState 的类型与模式给出

    ConversationMenuEntry()
        : m_id(0)
        , m_isSeparator(false)
        , m_label(NULL)
        , m_enabled(true)
        , m_hasNotifySubMenu(false)
    {
    }
};

/**
 *  构造会话右键菜单的行模型。纯函数、无副作用、不创建窗口；调用方据此逐行喂给
 *  DuiMenu（分隔条走 AppendSeparator，灰显项走 AppendDisabled，其余走 AppendItem）。
 *
 *    state：被右键那条会话的当前状态（类型 / 置顶 / 免打扰 / 未读数）。
 *
 *  返回：行模型 vector（值语义，调用方拥有）。m_label 指向静态常量串，调用方可直接
 *        持有使用，无需拷贝。
 */
std::vector<ConversationMenuEntry> BuildConversationMenuModel(const ConversationMenuState& state);

} // namespace balloonmain
