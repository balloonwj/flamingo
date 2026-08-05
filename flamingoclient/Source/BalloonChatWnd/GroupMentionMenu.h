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
 *  行为约定（与用户确认过）：
 *    · @ 的是本人   → 只留「查看资料」。自己跟自己聊天、自己 @ 自己都没意义。
 *    · 成员已退群   → 四项照常列出，但「@TA」置灰（他收不到）。
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
    kMentionMenuChat        = 1,   // 与 TA 聊天：打开与该成员的单聊窗
    kMentionMenuProfile     = 2,   // 查看资料：打开该成员的联系人资料窗
    kMentionMenuMentionMore = 3,   // @TA：在输入框光标处插入「@显示名 」
    kMentionMenuCopyName    = 4,   // 复制 TA 的昵称：把显示名放进剪贴板
    kMentionMenuSetAdmin    = 5,   // 设为管理员：仅群主可见，且目标是普通成员时
    kMentionMenuCancelAdmin = 6,   // 撤销管理员：仅群主可见，且目标已是管理员时
    kMentionMenuKick        = 7,   // 移出本群：群主可移任何非群主；管理员只能移普通成员
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
    std::wstring  displayName;  // 被 @ 成员的显示名（不含 '@'），复制昵称 / 再 @ 一次要用

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
 *  末尾的「设为管理员 / 撤销管理员」只对群主出现，判定复用 grouprole::CanSetAdmin
 *  ——与群管理窗口成员卡右键菜单同一份规则，避免两处漂移。出现时前面会带一条
 *  分隔线，把"看 / 聊"这类日常动作与管理动作分开。
 *
 *    mentionUid：  该片段 @ 的 userid。
 *    selfUid：     本人 userid；传 0（未登录 / 未知）时一律按「不是本人」处理。
 *    stillInGroup：该成员是否仍在本群成员表里。
 *    myNetRole：   本人在本群的角色，取值见 grouprole::NetGroupRole（<u>网络侧</u>
 *                  取值：0 成员 1 管理员 2 群主）；负值表示未知 / 不在本群。
 *    targetNetRole：被 @ 成员在本群的角色，取值同上；负值表示未知 / 已不在本群。
 *
 *  @return 行模型（值语义，调用方拥有）；mentionUid 为 @所有人 时返回空 vector。
 */
std::vector<MentionMenuEntry> BuildMentionMenuModel(long long mentionUid,
                                                    long long selfUid,
                                                    bool stillInGroup,
                                                    int myNetRole,
                                                    int targetNetRole);

} // namespace balloonchatwnd
