/**
 *  MainPanelConversationMenu.h
 *
 *  "消息"Tab 最近会话列表右键菜单的<u>纯数据模型</u>。把"菜单有哪些项、什么顺序、
 *  各项 id / 文本 / 是否可用"从 CBalloonMainPanel 里抽出成一个不依赖 HWND、不依赖
 *  Flamingo 业务对象的纯函数，便于被 FlamingoUnit 测试工程直接调用断言（真正的
 *  弹出与命令分发留在 CBalloonMainPanel::ShowConversationContextMenu）。
 *
 *  菜单结构（自上而下）：
 *      打开会话
 *      标记已读              ← 未读为 0 时灰显
 *      查看历史消息
 *      查看资料 / 查看群资料  ← 按会话类型切换文案
 *      添加到收藏 / 取消收藏  ← 按当前收藏状态切换文案
 *      ──────────
 *      置顶会话 / 取消置顶    ← 按当前置顶状态切换文案
 *      消息免打扰 / 取消免打扰 ← 按当前免打扰状态切换文案
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
    kConvMenuHistory   = 4,   // 查看历史消息 —— 打开历史消息记录窗并定位到本会话
    kConvMenuProfile   = 5,   // 查看资料（单聊）/ 查看群资料（群聊）
    kConvMenuPin       = 6,   // 置顶会话 / 取消置顶
    kConvMenuMute      = 7,   // 消息免打扰 / 取消免打扰
    kConvMenuDelete    = 8,   // 删除该会话 —— 仅从最近会话列表移除，不删聊天消息
    kConvMenuDeleteAll = 9,   // 删除全部会话 —— 同上，作用于列表里的每一条会话
    kConvMenuFavorite  = 10,  // 添加到收藏 / 取消收藏 —— 把该联系人或群加进「我的收藏」
};

/**
 *  ConversationMenuState —— 生成菜单所需的会话当前状态。由 CBalloonMainPanel 从
 *  会话缓存（m_conversations）里取出被右键那条会话的字段填好后传入。
 */
struct ConversationMenuState
{
    bool m_isGroup;   // 是否群会话（ctype==2）；决定"查看资料"还是"查看群资料"
    bool m_pinned;    // 当前是否已置顶；决定置顶项文案是"置顶会话"还是"取消置顶"
    bool m_muted;     // 当前是否已免打扰；决定免打扰项文案的正反
    int  m_unread;    // 当前未读消息数；为 0 时"标记已读"灰显（已经全读完了，点了没意义）
    bool m_favorite;  // 该联系人 / 群当前是否已收藏；决定收藏项文案是"取消收藏"还是"添加到收藏"

    ConversationMenuState()
        : m_isGroup(false)
        , m_pinned(false)
        , m_muted(false)
        , m_unread(0)
        , m_favorite(false)
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

    ConversationMenuEntry()
        : m_id(0)
        , m_isSeparator(false)
        , m_label(NULL)
        , m_enabled(true)
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
