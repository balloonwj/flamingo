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
#include "../../Session/GroupRoleRules.h"   // CanSetAdmin：设 / 撤管理员的门控，与群管理窗共用
#include "../../Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

// 各菜单项的显示文字。集中放在这里，免得实现里散着字面量、改文案要翻好几处。
// 「发消息」「复制名字」与主面板联系人 / 会话右键菜单逐字一致（2026-10-03 统一叫法）
const wchar_t* const kLabelChat        = TR_NOOP(L"发消息");
const wchar_t* const kLabelProfile     = TR_NOOP(L"查看资料");
const wchar_t* const kLabelMentionMore = L"@TA";
const wchar_t* const kLabelCopyName    = TR_NOOP(L"复制名字");
// 这两项的文案与群管理窗口成员卡菜单保持逐字一致，同一个动作在两处不该有两种叫法
const wchar_t* const kLabelSetAdmin    = TR_NOOP(L"设为管理员");
const wchar_t* const kLabelCancelAdmin = TR_NOOP(L"撤销管理员");
const wchar_t* const kLabelKick        = TR_NOOP(L"移出本群");
const wchar_t* const kLabelTransferOwner = TR_NOOP(L"转让群主");

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

// 往行模型末尾追加一条分隔线。模型还空着（分隔线会落在首行）、或上一行已经是分隔线
// （会连成两条）时不追加，调用点因此不必预判下一组是否为空。
void AppendSeparator(std::vector<MentionMenuEntry>& items)
{
    if (items.empty() || items.back().isSeparator)
    {
        return;
    }
    MentionMenuEntry e;
    e.label       = NULL;
    e.enabled     = false;
    e.isSeparator = true;
    items.push_back(e);
}

// 去掉行模型末尾的分隔线：最后一组整组为空时，它前面那条分隔线会挂在菜单底部。
void TrimTrailingSeparator(std::vector<MentionMenuEntry>& items)
{
    while (!items.empty() && items.back().isSeparator)
    {
        items.pop_back();
    }
}

} // anonymous namespace

bool ShouldShowMentionMenu(long long mentionUid)
{
    // @所有人 没有具体的对象 —— 发消息 / 资料 / 再 @ / 复制名字这几项没有一项讲得通，
    // 与其弹一个全灰的菜单，不如当普通文字，右键照常出气泡自己的菜单。
    return mentionUid != kMentionUidAll;
}

const wchar_t* GetMemberStarredMenuLabel(bool isStarred)
{
    return isStarred ? TR(L"取消特别关注联系人") : TR(L"设为特别关注联系人");
}

const wchar_t* GetGroupFollowMenuLabel(bool isGroupFollowed)
{
    return isGroupFollowed ? TR(L"取消关注 TA 在本群的发言") : TR(L"关注 TA 在本群的发言");
}

std::vector<MentionMenuEntry> BuildMentionMenuModel(long long mentionUid,
                                                    long long selfUid,
                                                    bool stillInGroup,
                                                    int myNetRole,
                                                    int targetNetRole,
                                                    bool isStarred,
                                                    bool isGroupFollowed)
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
        items.push_back(MakeEntry(kMentionMenuProfile, TR(kLabelProfile), true));
        return items;
    }

    // 分组（2026-10-03 与用户确认，与主面板几个右键菜单同一套规则）：沟通 / 查看与标记 /
    // 改变身份 / 移出，组与组之间一条分隔线，不可撤销的动作永远在最后。

    // 第一组：与这个人沟通。人已经不在群里了，再 @ 他也送不到，置灰而不是删掉 ——
    // 保持菜单行数稳定，用户看得见"有这一项、只是现在不能用"，比项忽然消失更好理解。
    items.push_back(MakeEntry(kMentionMenuChat, TR(kLabelChat), true));
    items.push_back(MakeEntry(kMentionMenuMentionMore, kLabelMentionMore, stillInGroup));
    AppendSeparator(items);

    // 第二组：查看这个人、取他的名字、给他打标记。
    // 特别关注<u>不受 stillInGroup 影响</u>：它设置的是与这位同事的单聊会话（来消息直接弹出
    // 聊天窗、上下线时弹提示），跟他还在不在这个群没有关系 —— 退了群照样是同事。群成员菜单里
    // 它叫「设为特别关注联系人」，与下面的「关注 TA 在本群的发言」区分开（2026-10-06 与用户确认）。
    items.push_back(MakeEntry(kMentionMenuProfile, TR(kLabelProfile), true));
    items.push_back(MakeEntry(kMentionMenuCopyName, TR(kLabelCopyName), true));
    items.push_back(MakeEntry(kMentionMenuStarred, GetMemberStarredMenuLabel(isStarred), true));
    // 关注 TA 在本群的发言：本群的关注名单只能填本群成员（服务端校验），人已退群时不出现
    if (stillInGroup)
    {
        items.push_back(MakeEntry(kMentionMenuGroupFollow, GetGroupFollowMenuLabel(isGroupFollowed), true));
    }
    AppendSeparator(items);

    // 后两组是管理动作，判定与群管理窗口成员卡菜单共用 grouprole 的函数。这里不必再传
    // isSelf —— 上面 @ 到自己的分支已经提前返回了。
    // 注意门控<u>不同</u>：设 / 撤管理员与转让群主仅限群主，移出本群管理员也能做，
    // 故各问各的，不能合并成一个条件。
    const bool canSetAdmin = grouprole::CanSetAdmin(myNetRole, targetNetRole, false);
    const bool canKick     = grouprole::CanKickMember(myNetRole, targetNetRole, false);
    const bool canTransfer = grouprole::CanTransferOwnerTo(myNetRole, targetNetRole, false);

    // 第三组：改变对方在群里的身份。
    if (canSetAdmin)
    {
        if (grouprole::ShouldOfferCancelAdmin(targetNetRole))
        {
            items.push_back(MakeEntry(kMentionMenuCancelAdmin, TR(kLabelCancelAdmin), true));
        }
        else
        {
            items.push_back(MakeEntry(kMentionMenuSetAdmin, TR(kLabelSetAdmin), true));
        }
    }

    // 转让群主排在角色项之后：同属"改变对方在群里的身份"，新群主还能再转回来，比移出温和
    if (canTransfer)
    {
        items.push_back(MakeEntry(kMentionMenuTransferOwner, TR(kLabelTransferOwner), true));
    }
    AppendSeparator(items);

    // 第四组：移出本群，单独一组排在最末 —— 它是这个菜单里唯一不可撤销的操作，放在最远处，
    // 与群管理窗成员卡菜单的排法一致。
    if (canKick)
    {
        items.push_back(MakeEntry(kMentionMenuKick, TR(kLabelKick), true));
    }

    // 没有管理项时，第二组之后那条分隔线会挂在菜单底部，这里去掉。
    TrimTrailingSeparator(items);
    return items;
}

std::vector<MentionMenuEntry> RemoveMentionMenuEntry(const std::vector<MentionMenuEntry>& items)
{
    std::vector<MentionMenuEntry> out;
    out.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isSeparator && items[i].id == kMentionMenuMentionMore)
            continue;
        out.push_back(items[i]);
    }
    return out;
}

std::vector<MentionMenuEntry> BuildThreadMentionMenuModel(long long mentionUid,
                                                          long long selfUid,
                                                          int myNetRole,
                                                          int targetNetRole,
                                                          bool isStarred,
                                                          bool isGroupFollowed)
{
    //不在成员名单里才算已退群；名单还没取到时按仍在群里处理
    const bool stillInGroup = (targetNetRole != kThreadTargetRoleLeft);
    //已退群与名单未到两种情况，对方角色都按「未知」（负值）交给 BuildMentionMenuModel
    const int role = (targetNetRole >= 0) ? targetNetRole : -1;
    return RemoveMentionMenuEntry(BuildMentionMenuModel(mentionUid, selfUid, stillInGroup, myNetRole, role,
                                                        isStarred, isGroupFollowed));
}

} // namespace balloonchatwnd
