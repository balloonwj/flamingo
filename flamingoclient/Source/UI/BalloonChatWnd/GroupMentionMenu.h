/**
 *  GroupMentionMenu.h
 *
 *  群聊气泡里「@某人」片段的右键菜单 —— <u>纯模型</u>部分：给定被 @ 的成员是谁、
 *  是不是本人、还在不在群里，算出菜单该有哪几行、哪几行可点。不碰 HWND、不建
 *  真菜单，便于被 FlamingoUnit 测试工程直接调用断言。
 *
 *  真正弹出菜单、把选中项分发出去在 ChatBubble / 群聊窗那边做；本文件只回答
 *  「这个菜单长什么样」。写法对齐仓库既有的 MainPanelTrayMenu / MainPanelStatusMenu
 *  —— 菜单模型一律做成纯函数并配测试，避免菜单逻辑散落在窗口代码里没法验证。
 *
 *  菜单形态（2026-10-03 与用户确认的分组规则，与主面板几个右键菜单相同）：
 *      发消息
 *      @TA                     ← 成员已退群时置灰
 *      ──────────
 *      查看资料
 *      复制名字
 *      设为特别关注联系人 / 取消特别关注联系人 ← 按当前关注状态切换文案（2026-10-06 由「特别关注」改名）
 *      关注 TA 在本群的发言 / 取消关注 TA 在本群的发言 ← 成员仍在群里时才有（群关注名单，
 *                                                       docs/消息提醒模式与消息盒子方案.md §12）
 *      ──────────
 *      设为管理员 / 撤销管理员 ← 仅群主可见
 *      转让群主                ← 仅群主可见
 *      ──────────
 *      移出本群                ← 群主可移任何非群主；管理员只能移普通成员
 *  某一组整组为空时连同它前面的分隔线一起不出现，菜单首尾不会出现分隔线。
 *
 *  行为约定（与用户确认过）：
 *    · @ 的是本人   → 只留「查看资料」。自己跟自己聊天、自己 @ 自己都没意义。
 *    · 成员已退群   → 各项照常列出，但「@TA」置灰（他收不到）；「特别关注」不受影响
 *                     —— 关注的是这位同事本人，跟他还在不在这个群无关。
 *    · @所有人      → 压根不弹菜单（没有具体的人），由调用方据 ShouldShowMentionMenu 判断。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

#include <vector>
#include <string>

namespace balloonchatwnd {

/**
 *  菜单项标识。分发时按它 switch，不要按行下标 —— 行数会随上面的规则变化。
 */
enum MentionMenuId
{
    kMentionMenuChat        = 1,   // 发消息：打开与该成员的单聊窗
    kMentionMenuProfile     = 2,   // 查看资料：打开该成员的联系人资料窗
    kMentionMenuMentionMore = 3,   // @TA：在输入框光标处插入「@显示名 」
    kMentionMenuCopyName    = 4,   // 复制名字：把显示名放进剪贴板
    kMentionMenuSetAdmin    = 5,   // 设为管理员：仅群主可见，且目标是普通成员时
    kMentionMenuCancelAdmin = 6,   // 撤销管理员：仅群主可见，且目标已是管理员时
    kMentionMenuKick        = 7,   // 移出本群：群主可移任何非群主；管理员只能移普通成员
    kMentionMenuStarred     = 8,   // 特别关注 / 取消特别关注：文案随当前关注状态取反；被关注者上下线时桌面右下角弹提示
    kMentionMenuTransferOwner = 9, // 转让群主：仅群主可见，且目标是本群的其他成员时；确认后把群主转给 TA
    kMentionMenuGroupFollow = 10,  // 关注 TA 在本群的发言 / 取消关注：文案随本人在本群的关注名单取反；对本人、已退群成员不出现
};

/**
 *  菜单里的一行。
 */
struct MentionMenuEntry
{
    MentionMenuId id;        // 本行的标识，分发时用；分隔线该字段无意义
    const wchar_t* label;    // 显示文字；指向静态常量串，生命周期为进程级
    bool           enabled;  // 是否可点；false 时置灰但仍然显示
    bool           isSeparator; // true 表示这一行是分隔线，其余字段无意义

    MentionMenuEntry()
        : id(kMentionMenuChat)
        , label(L"")
        , enabled(true)
        , isSeparator(false)
    {
    }
};

/**
 *  菜单选中后投递给宿主聊天窗的负载（WM_CHAT_MENTION_ACTION 的 lParam）。
 *
 *  为什么要中转：气泡（ChatBubble）是通用控件，单聊群聊共用，它不认识"群成员表"
 *  "打开单聊窗"这些业务对象；而这四个动作全要碰业务。故气泡只负责命中与弹菜单，
 *  把"谁 + 选了哪一项"打包投给宿主窗去执行 —— 与文件气泡按钮的 ChatFileActionData
 *  是同一套路子。
 *
 *  生命周期：调用方在堆上 new 出来随 PostMessage 投出；宿主窗处理完负责 delete。
 *  投递失败（窗口已销毁）时由投递方自行 delete，不得泄漏。
 */
struct MentionActionData
{
    MentionMenuId id;           // 用户选中的菜单项
    long long     uid;          // 被 @ 成员的 userid
    std::wstring  displayName;  // 被 @ 成员的显示名（不含 '@'），复制名字 / 再 @ 一次要用

    MentionActionData()
        : id(kMentionMenuChat)
        , uid(0)
    {
    }
};

/**
 *  判断某个 @ 片段该不该弹右键菜单。
 *    mentionUid：该片段 @ 的 userid；kMentionUidAll(0) 表示 @所有人。
 *  @return 该弹返回 true；@所有人 返回 false（没有具体的人可操作）。
 */
bool ShouldShowMentionMenu(long long mentionUid);

/**
 *  算出 @ 片段右键菜单的行模型。
 *
 *  「设为管理员 / 撤销管理员」与「转让群主」只对群主出现，判定复用 grouprole::CanSetAdmin
 *  与 grouprole::CanTransferOwnerTo ——与群管理窗口成员卡右键菜单同一份规则，避免两处漂移。
 *  「移出本群」的判定是 grouprole::CanKickMember，单独一组排在最末。分组与分隔线见文件头。
 *
 *    mentionUid：  该片段 @ 的 userid。
 *    selfUid：     本人 userid；传 0（未登录 / 未知）时一律按「不是本人」处理。
 *    stillInGroup：该成员是否仍在本群成员表里。
 *    myNetRole：   本人在本群的角色，取值见 grouprole::NetGroupRole（<u>网络侧</u>
 *                  取值：0 成员 1 管理员 2 群主）；负值表示未知 / 不在本群。
 *    targetNetRole：被 @ 成员在本群的角色，取值同上；负值表示未知 / 已不在本群。
 *    isStarred：    该成员当前是否已被本人特别关注，决定「设为特别关注联系人 / 取消特别关注联系人」
 *                   一项的文案正反。缺省 false —— 取不到关注名单时按"未关注"显示，
 *                   点下去就是去关注，是这两种误判里代价较小的那个。
 *    isGroupFollowed：本人是否已在本群关注 TA 的发言，决定「关注 TA 在本群的发言 / 取消关注 TA 在本群的发言」
 *                   的文案正反；缺省 false，理由同 isStarred。
 *
 *  @return 行模型（值语义，调用方拥有）；mentionUid 为 @所有人 时返回空 vector。
 */
std::vector<MentionMenuEntry> BuildMentionMenuModel(long long mentionUid,
                                                    long long selfUid,
                                                    bool stillInGroup,
                                                    int myNetRole,
                                                    int targetNetRole,
                                                    bool isStarred = false,
                                                    bool isGroupFollowed = false);

/**
 *  去掉行模型里的「@TA」一项（kMentionMenuMentionMore），用于没有聊天输入框的窗口：帖子群窗口的群成员面板
 *  （docs/帖子群方案.md 14.3）。「@TA」与「发消息」同在第一组，去掉后该组仍有「发消息」，分隔线不必调整。
 *    items：BuildMentionMenuModel 的结果。
 *  @return 去掉「@TA」之后的行模型（值语义）；原本没有这一项时与 items 相同。
 */
std::vector<MentionMenuEntry> RemoveMentionMenuEntry(const std::vector<MentionMenuEntry>& items);

//BuildThreadMentionMenuModel 的 targetNetRole 除群角色 0 / 1 / 2 之外的两个取值，与帖子群插件接口
//（FlamingoThreadGroup/ThreadGroupApi.h 的 kMemberRoleLeft / kMemberRoleUnknown）相同
const int kThreadTargetRoleLeft    = -1;   // 对方不在群成员名单里：已退群或被移出
const int kThreadTargetRoleUnknown = -2;   // 帖子群窗口的成员名单还没取到：按仍在群里、角色未知处理

/**
 *  帖子群详情里点「@某人」弹出的成员菜单的行模型（docs/帖子群方案.md §15.1 第 4 条）：与帖子群窗口群成员面板的
 *  右键菜单逐项相同——同样由 BuildMentionMenuModel 算出、同样去掉「@TA」（帖子群窗口没有聊天输入框）。
 *  对方已不在群里时按 BuildMentionMenuModel 的原有规则处理（stillInGroup 为 false）；名单还没取到时按仍在群里、
 *  对方角色未知处理，此时管理类的几项不出现（服务端执行时仍会再校验权限）。
 *    mentionUid：     被 @ 的人。
 *    selfUid：        本人 userid；0 表示未知。
 *    myNetRole：      本人在本群的角色（网络侧取值：0 成员 1 管理员 2 群主）；负值表示未知。
 *    targetNetRole：  对方的群角色（同上），或 kThreadTargetRoleLeft / kThreadTargetRoleUnknown。
 *    isStarred / isGroupFollowed：同 BuildMentionMenuModel。
 *  @return 行模型（值语义）；mentionUid 为 @所有人 时返回空 vector。
 */
std::vector<MentionMenuEntry> BuildThreadMentionMenuModel(long long mentionUid,
                                                          long long selfUid,
                                                          int myNetRole,
                                                          int targetNetRole,
                                                          bool isStarred,
                                                          bool isGroupFollowed);

/**
 *  群成员菜单里「特别关注」一项的文字（2026-10-06 起与主面板联系人菜单的「特别关注」区分开：
 *  群里另有「关注 TA 在本群的发言」，两项都叫「关注」容易混淆）。
 *    isStarred：该成员当前是否已被本人特别关注。
 *  @return 「设为特别关注联系人」或「取消特别关注联系人」，已按当前界面语言翻译；指向进程级字符串。
 */
const wchar_t* GetMemberStarredMenuLabel(bool isStarred);

/**
 *  群成员菜单里「关注 TA 在本群的发言」一项的文字。
 *    isGroupFollowed：本人是否已在本群关注 TA 的发言。
 *  @return 「关注 TA 在本群的发言」或「取消关注 TA 在本群的发言」，已按当前界面语言翻译；指向进程级字符串。
 */
const wchar_t* GetGroupFollowMenuLabel(bool isGroupFollowed);

} // namespace balloonchatwnd
