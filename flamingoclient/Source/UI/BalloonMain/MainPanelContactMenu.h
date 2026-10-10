/**
 *  MainPanelContactMenu.h
 *
 *  通讯录联系人行右键菜单的<u>纯数据模型</u>。把"菜单有哪些项、什么顺序、
 *  各项 id / 文本"从 CBalloonMainPanel 里抽出成一个不依赖 HWND、不依赖
 *  Flamingo 业务对象的纯函数，便于被 FlamingoUnit 测试工程直接调用断言
 *  （真正的弹出与命令分发留在 CBalloonMainPanel::ShowContactContextMenu）。
 *  写法与同级的 MainPanelConversationMenu / MainPanelStatusMenu 一致。
 *
 *  菜单有两种形态，由 ContactMenuState::m_isSelf 决定：
 *
 *  分组规则（2026-10-03 与用户确认，联系人 / 群 / 会话三个右键菜单共用）：主操作 → 查看 →
 *  标记与提醒 → 复制与编辑 → 管理 → 危险操作（只影响本人记录的在前，改变成员关系或删除会话的
 *  在最后），组与组之间一条分隔条；同一个动作在各菜单里叫法相同。
 *
 *  一、右键的是<u>同事</u>（m_isSelf 为 false）——完整菜单：
 *      发消息
 *      语音通话
 *      视频通话
 *      共享文件给TA           ← 2026-10-03 起；本客户端的文件共享可用时才出现
 *      共享屏幕给TA           ← 2026-10-03 起；本客户端的屏幕共享可用时才出现
 *      开启字幕模式 / 关闭字幕模式 ← 2026-10-05 起；按与该联系人单聊的字幕当前是否开着切换文案
 *      ──────────
 *      查看资料
 *      查看消息记录
 *      ──────────
 *      添加到收藏 / 取消收藏  ← 按当前收藏状态切换文案
 *      消息提醒设置 ▸          ← 2026-10-03 起取代「特别关注」「消息免打扰」两项，子菜单见 NotifyModeMenu.h
 *      ──────────
 *      修改备注
 *      复制名字               ← 复制列表上显示的名字（有备注用备注，没有用昵称）
 *      ──────────
 *      清空聊天记录
 *
 *  二、右键的是<u>自己</u>（m_isSelf 为 true）——只有一项：
 *      查看资料
 *
 *  为什么自己那份要砍到只剩一项：通讯录里现在能看到自己（好让用户看清自己在
 *  组织架构里的位置），但"给自己发消息""给自己改备注"这类动作要么无意义要么
 *  荒唐。与其把它们灰显摆在那儿，不如整项不出现。
 *
 *  历史：菜单里原本还有"移动到分组""加入黑名单""删除好友"三项。企业版取消了
 *  好友关系（通讯录即企业组织架构，联系人的归属由部门表达），这三项连同整个
 *  好友功能一并移除。
 *
 *  各项均<u>不带图标</u>，与同级的会话右键菜单、群聊右键菜单保持一致。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#pragma once

#include <windows.h>      // UINT
#include <vector>

namespace balloonmain {

/**
 *  ContactMenuId —— 联系人右键菜单各项的命令 id。点击后由 DuiMenu::TrackPopup
 *  返回该值，CBalloonMainPanel::ShowContactContextMenu 据此分发。
 *
 *  取值<u>必须非 0</u>：0 是 DuiMenu::TrackPopup 的"未选中"（失焦 / ESC）返回值，
 *  被功能项占用的话，用户按 ESC 关掉菜单会被当成选了那一项。分隔条不是功能项、
 *  没有 id（其 ContactMenuEntry::m_id 取 0）。
 *
 *  这些取值与本模型抽出之前 CBalloonMainPanel 里那份 ContactMenuId 逐一对应，
 *  没有重新编号——避免存量代码里任何按数字比对的地方悄悄错位。
 */
enum ContactMenuId : UINT
{
    // 1 曾是顶部联系人名（禁用项，作菜单标题）。该标题项已去掉——右键菜单是从
    // 联系人行上点出来的，点的是谁本来就一目了然，再顶一行名字只是把功能项往下推。
    // 此 id 一并撤销，日后新增菜单项另取新值，不要复用 1。
    kContactMenuSendMsg      = 2,    // 发消息 —— 打开与该联系人的单聊窗
    kContactMenuVoice        = 3,    // 语音通话 —— 转给通话协调器发起呼叫
    kContactMenuVideo        = 4,    // 视频通话 —— 同上，媒体类型为视频
    kContactMenuViewProfile  = 5,    // 查看资料 —— 同事打开联系人资料窗，自己打开个人信息窗
    kContactMenuRemark       = 6,    // 修改备注 —— 打开资料窗并让备注行直接进入编辑态
    // 7 曾是"移动到分组"、9 曾是"加入黑名单"、10 曾是"删除好友"。企业版取消了
    // 好友关系（通讯录即企业组织架构，归属由部门表达），这三项连同好友功能一并
    // 移除。三个编号留空不复用，避免与旧版本的菜单模型对不上号。
    // 8 曾是「消息免打扰 / 取消免打扰」，2026-10-03 由「消息提醒设置」二级菜单（kContactMenuNotifyMode）取代，不要复用
    kContactMenuViewHistory  = 11,   // 查看消息记录 —— 打开消息记录窗并定位到本会话
    kContactMenuFavorite     = 12,   // 添加到收藏 / 取消收藏（文案随当前状态取反）
    // 13 曾是「特别关注 / 取消特别关注」，2026-10-03 并入「消息提醒设置」二级菜单，不要复用
    kContactMenuCopyName     = 14,   // 复制名字 —— 把列表上显示的名字（备注优先，其次昵称）写入剪贴板；右键自己时不出现
    kContactMenuClearHistory = 15,   // 清空聊天记录 —— 本人视角清空与该联系人的单聊记录，本人各设备同步、对方不受影响；右键自己时不出现
    kContactMenuShareFile    = 16,   // 共享文件给TA —— 打开发起共享对话框、对象固定为该联系人，成功后打开与他的单聊窗；文件共享可用时才出现，右键自己时不出现
    kContactMenuShareScreen  = 17,   // 共享屏幕给TA —— 打开与该联系人的单聊窗并开始屏幕共享（先选共享内容）；屏幕共享可用时才出现，右键自己时不出现
    kContactMenuNotifyMode   = 18,   // 消息提醒设置 ▸ —— 二级菜单的父项，本身点不了；子菜单各项的 id 见 NotifyModeMenu.h；右键自己时不出现
    kContactMenuSubtitle     = 19,   // 开启字幕模式 / 关闭字幕模式 —— 开关与该联系人单聊的桌面字幕，不打开聊天窗；m_canSubtitle 为 true 时才出现，右键自己时不出现
};

/**
 *  ContactMenuState —— 生成菜单所需的当前状态。由 CBalloonMainPanel 在弹菜单
 *  之前从各处缓存里取出被右键那个联系人的字段填好后传入。
 */
struct ContactMenuState
{
    bool m_isSelf;    // 被右键的是不是当前登录用户本人；true 时菜单只剩"查看资料"
    int  m_notifyMode; // 与该联系人单聊的消息提醒模式（Protocol.h 的 enum conversation_notify_mode）；决定子菜单里哪一项打勾。m_isSelf 为 true 时无意义
    bool m_favorite;  // 当前是否已收藏；决定收藏项文案是"取消收藏"还是"添加到收藏"。m_isSelf 为 true 时无意义
    bool m_canShareFile;    // 本客户端的文件共享是否可用（FlamingoFileShare.dll 已加载且本企业开关打开）；为 false 时没有「共享文件给TA」
    bool m_canShareScreen;  // 本客户端的屏幕共享是否可用（FlamingoMedia.dll 已加载）；为 false 时没有「共享屏幕给TA」
    bool m_canSubtitle;     // 是否提供字幕模式一项（主面板总是置 true；默认 false，不关心这一项的调用方与旧用例不受影响）
    bool m_subtitleActive;  // 与该联系人单聊的字幕此刻是否开着；决定文案是"关闭字幕模式"还是"开启字幕模式"

    ContactMenuState()
        : m_isSelf(false)
        , m_notifyMode(0)
        , m_favorite(false)
        , m_canShareFile(false)
        , m_canShareScreen(false)
        , m_canSubtitle(false)
        , m_subtitleActive(false)
    {
    }
};

/**
 *  ContactMenuEntry —— 菜单里的一行（功能项或分隔条）。
 */
struct ContactMenuEntry
{
    UINT           m_id;            // 功能项命令 id（见 ContactMenuId）；分隔条取 0
    bool           m_isSeparator;   // 是否为分隔条；true 时 m_label 无意义
    const wchar_t* m_label;         // 显示文本（宽字符常量串，进程级生命周期）；分隔条取 NULL
    bool           m_hasNotifySubMenu; // 是否为「消息提醒设置」二级菜单的父项：喂 DuiMenu 时走 AppendSubMenu，
                                       // 子菜单由 BuildNotifyModeSubMenu(false, 当前模式) 给出

    ContactMenuEntry()
        : m_id(0)
        , m_isSeparator(false)
        , m_label(NULL)
        , m_hasNotifySubMenu(false)
    {
    }
};

/**
 *  构造联系人右键菜单的行模型。纯函数、无副作用、不创建窗口；调用方据此逐行
 *  喂给 DuiMenu（分隔条走 AppendSeparator，其余走 AppendItem）。
 *
 *    state：被右键那个联系人的当前状态（是否本人 / 提醒模式 / 收藏）。
 *
 *  返回：行模型 vector（值语义，调用方拥有）。m_label 指向静态常量串，调用方
 *        可直接持有使用，无需拷贝。
 */
std::vector<ContactMenuEntry> BuildContactMenuModel(const ContactMenuState& state);

} // namespace balloonmain
