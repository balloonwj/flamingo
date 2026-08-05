/**
 *  GroupMentionMenu.cpp
 *
 *  群聊气泡里「@某人」片段右键菜单的纯模型实现。说明见 GroupMentionMenu.h。
 *
 *  balloonwj@qq.com   2026-08-03
 */

// 本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
// （工程里对应的 PrecompiledHeader 设为 NotUsing），与 GroupMentionModel.cpp 同例。
#include "GroupMentionMenu.h"

#include "GroupMentionModel.h"   // kMentionUidAll
#include "../GroupRoleRules.h"   // CanSetAdmin：设 / 撤管理员的门控，与群管理窗共用

namespace balloonchatwnd {

namespace {

// 各菜单项的显示文字。集中放在这里，免得实现里散着字面量、改文案要翻好几处。
const wchar_t* const kLabelChat        = L"与 TA 聊天";
const wchar_t* const kLabelProfile     = L"查看资料";
const wchar_t* const kLabelMentionMore = L"@TA";
const wchar_t* const kLabelCopyName    = L"复制 TA 的昵称";
// 这两项的文案与群管理窗口成员卡菜单保持逐字一致，同一个动作在两处不该有两种叫法
const wchar_t* const kLabelSetAdmin    = L"设为管理员";
const wchar_t* const kLabelCancelAdmin = L"撤销管理员";
const wchar_t* const kLabelKick        = L"移出本群";

// 造一行菜单，省得每处写四行。
MentionMenuEntry MakeEntry(MentionMenuId id, const wchar_t* label, bool enabled)
{
    MentionMenuEntry e;
    e.id          = id;
    e.label       = label;
    e.enabled     = enabled;
    e.isSeparator = false;
    return e;
}

// 造一条分隔线。
MentionMenuEntry MakeSeparator()
{
    MentionMenuEntry e;
    e.label       = NULL;
    e.enabled     = false;
    e.isSeparator = true;
    return e;
}

} // anonymous namespace

bool ShouldShowMentionMenu(long long mentionUid)
{
    // @所有人 没有具体的对象 —— 聊天 / 资料 / 再 @ / 复制昵称四项没有一项讲得通，
    // 与其弹一个全灰的菜单，不如当普通文字，右键照常出气泡自己的菜单。
    return mentionUid != kMentionUidAll;
}

std::vector<MentionMenuEntry> BuildMentionMenuModel(long long mentionUid,
                                                    long long selfUid,
                                                    bool stillInGroup,
                                                    int myNetRole,
                                                    int targetNetRole)
{
    std::vector<MentionMenuEntry> items;
    if (!ShouldShowMentionMenu(mentionUid))
    {
        return items;
    }

    // @ 的是本人：只留「查看资料」。selfUid 为 0 表示未登录 / 取不到本人 id，
    // 此时无从判定是不是自己，按"不是自己"处理，四项照常 —— 宁可多给一项，
    // 也不要在正常场景下莫名其妙地少掉三项。
    const bool isSelf = (selfUid != 0 && mentionUid == selfUid);
    if (isSelf)
    {
        items.push_back(MakeEntry(kMentionMenuProfile, kLabelProfile, true));
        return items;
    }

    items.push_back(MakeEntry(kMentionMenuChat,    kLabelChat,    true));
    items.push_back(MakeEntry(kMentionMenuProfile, kLabelProfile, true));
    // 人已经不在群里了，再 @ 他也送不到，置灰而不是删掉 —— 保持菜单行数稳定，
    // 用户看得见"有这一项、只是现在不能用"，比项忽然消失更好理解。
    items.push_back(MakeEntry(kMentionMenuMentionMore, kLabelMentionMore, stillInGroup));
    items.push_back(MakeEntry(kMentionMenuCopyName, kLabelCopyName, true));

    // 管理动作放在最后，与上面的日常动作用分隔线隔开。判定与群管理窗口成员卡
    // 菜单共用 grouprole 的两个函数。这里不必再传 isSelf —— 上面 @ 到自己的
    // 分支已经提前返回了。
    // 注意两项的门控<u>不同</u>：设 / 撤管理员仅限群主，移出本群管理员也能做，
    // 故各问各的，不能合并成一个条件。
    const bool canSetAdmin = grouprole::CanSetAdmin(myNetRole, targetNetRole, false);
    const bool canKick     = grouprole::CanKickMember(myNetRole, targetNetRole, false);

    // 分隔线只在确实要追加管理项时加一条，避免菜单末尾挂一条孤零零的分隔线
    if (canSetAdmin || canKick)
    {
        items.push_back(MakeSeparator());
    }

    if (canSetAdmin)
    {
        if (grouprole::ShouldOfferCancelAdmin(targetNetRole))
        {
            items.push_back(MakeEntry(kMentionMenuCancelAdmin, kLabelCancelAdmin, true));
        }
        else
        {
            items.push_back(MakeEntry(kMentionMenuSetAdmin, kLabelSetAdmin, true));
        }
    }

    // 移出本群排在最末 —— 它是这个菜单里唯一不可撤销的操作，放在最远处，
    // 与群管理窗成员卡菜单的排法一致。
    if (canKick)
    {
        items.push_back(MakeEntry(kMentionMenuKick, kLabelKick, true));
    }
    return items;
}

} // namespace balloonchatwnd
