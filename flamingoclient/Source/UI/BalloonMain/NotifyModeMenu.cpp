/**
 *  NotifyModeMenu.cpp
 *
 *  「消息提醒设置」二级菜单行模型的实现。只装配数据，不碰任何窗口，Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。本文件含中文宽字符字面量，两个工程都开了 /utf-8。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "NotifyModeMenu.h"
#include "../../net/Protocol.h"   // enum conversation_notify_mode
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// 追加一个可打勾的模式项
void PushMode(std::vector<NotifyModeMenuEntry>& items, UINT id, const wchar_t* label, bool checked)
{
    NotifyModeMenuEntry e;
    e.m_id        = id;
    e.m_label     = label;
    e.m_checkable = true;
    e.m_checked   = checked;
    items.push_back(e);
}

// 某种提醒模式在二级菜单里的命令 id；不认识的值按允许提醒
UINT MenuIdOfMode(int mode)
{
    switch (mode)
    {
    //特别关注
    case notify_mode_starred:
        return kNotifyMenuStarred;

    //接收消息但不提醒
    case notify_mode_silent:
        return kNotifyMenuSilent;

    //收进消息盒子且不提醒
    case notify_mode_msgbox:
        return kNotifyMenuMsgbox;

    //屏蔽消息
    case notify_mode_block:
        return kNotifyMenuBlock;

    //只提醒 @我和关注的人
    case notify_mode_mention_follow:
        return kNotifyMenuMentionFollow;

    //允许消息提醒，以及不认识的值
    default:
        return kNotifyMenuAllow;
    }
}

} // anonymous namespace

const wchar_t* GetNotifyModeMenuTitle()
{
    return TR(L"消息提醒设置");
}

std::vector<int> GetNotifyModeChoices(bool isGroup)
{
    std::vector<int> modes;
    modes.push_back(notify_mode_starred);
    modes.push_back(notify_mode_allow);
    //只提醒 @我和关注的人：单聊没有 @，也没有可关注的其他成员
    if (isGroup)
        modes.push_back(notify_mode_mention_follow);
    modes.push_back(notify_mode_silent);
    modes.push_back(notify_mode_msgbox);
    //屏蔽只对群聊开放：屏蔽同事的单聊消息对方毫不知情，容易漏掉工作消息
    if (isGroup)
        modes.push_back(notify_mode_block);
    return modes;
}

int NotifyModeChoiceIndex(bool isGroup, int mode)
{
    const std::vector<int> modes = GetNotifyModeChoices(isGroup);
    int allowIndex = 0;
    for (size_t i = 0; i < modes.size(); ++i)
    {
        if (modes[i] == mode)
            return (int)i;
        if (modes[i] == notify_mode_allow)
            allowIndex = (int)i;
    }
    //不认识的值、单聊传入的屏蔽消息：按允许提醒
    return allowIndex;
}

int NotifyModeOfChoice(bool isGroup, int index)
{
    const std::vector<int> modes = GetNotifyModeChoices(isGroup);
    if (index < 0 || index >= (int)modes.size())
        return notify_mode_allow;
    return modes[(size_t)index];
}

const wchar_t* GetNotifyModeLabel(int mode)
{
    switch (mode)
    {
    //特别关注
    case notify_mode_starred:
        return TR(L"特别关注");

    //接收消息但不提醒
    case notify_mode_silent:
        return TR(L"接收消息但不提醒");

    //收进消息盒子且不提醒
    case notify_mode_msgbox:
        return TR(L"收进消息盒子且不提醒");

    //屏蔽消息
    case notify_mode_block:
        return TR(L"屏蔽消息");

    //只提醒 @我和关注的人
    case notify_mode_mention_follow:
        return TR(L"只提醒 @我和关注的人");

    //允许消息提醒，以及不认识的值
    default:
        return TR(L"允许消息提醒");
    }
}

std::vector<NotifyModeMenuEntry> BuildNotifyModeSubMenu(bool isGroup, int currentMode)
{
    //不认识的模式按允许提醒处理，与 NotifyModeModel 查不到时的口径一致
    int mode = currentMode;
    if (mode < notify_mode_allow || mode > notify_mode_mention_follow)
        mode = notify_mode_allow;
    if (!isGroup && (mode == notify_mode_block || mode == notify_mode_mention_follow))
        mode = notify_mode_allow;

    //模式项的顺序与文字和资料窗下拉框共用一份（GetNotifyModeChoices / GetNotifyModeLabel）
    std::vector<NotifyModeMenuEntry> items;
    const std::vector<int> modes = GetNotifyModeChoices(isGroup);
    for (size_t i = 0; i < modes.size(); ++i)
    {
        PushMode(items, MenuIdOfMode(modes[i]), GetNotifyModeLabel(modes[i]), modes[i] == mode);
    }

    NotifyModeMenuEntry sep;
    sep.m_isSeparator = true;
    items.push_back(sep);

    NotifyModeMenuEntry batch;
    batch.m_id    = kNotifyMenuBatch;
    batch.m_label = TR(L"批量管理消息提醒");
    items.push_back(batch);

    //群的关注名单：任何模式下都可以编辑，名单只在「只提醒 @我和关注的人」下影响提醒，
    //其它模式下用来给摘要加「[关注] 」前缀
    if (isGroup)
    {
        NotifyModeMenuEntry follow;
        follow.m_id    = kNotifyMenuManageFollow;
        follow.m_label = TR(L"管理关注的成员…");
        items.push_back(follow);
    }
    return items;
}

int NotifyModeFromMenuId(UINT id)
{
    switch (id)
    {
    //特别关注
    case kNotifyMenuStarred:
        return notify_mode_starred;

    //允许消息提醒
    case kNotifyMenuAllow:
        return notify_mode_allow;

    //接收消息但不提醒
    case kNotifyMenuSilent:
        return notify_mode_silent;

    //收进消息盒子且不提醒
    case kNotifyMenuMsgbox:
        return notify_mode_msgbox;

    //只提醒 @我和关注的人
    case kNotifyMenuMentionFollow:
        return notify_mode_mention_follow;

    //屏蔽消息
    case kNotifyMenuBlock:
        return notify_mode_block;

    //批量管理、分隔条、其它菜单的 id：不是模式项
    default:
        return -1;
    }
}

const wchar_t* GetStarredMenuLabel(bool isStarred)
{
    return isStarred ? TR(L"取消特别关注") : TR(L"特别关注");
}

} // namespace balloonmain
