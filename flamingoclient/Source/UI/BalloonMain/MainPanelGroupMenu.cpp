/**
 *  MainPanelGroupMenu.cpp
 *
 *  群列表右键菜单行模型的实现。仅装配数据（项 id / 文本 / 分隔条），不碰任何窗口 —— 故可被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj 里本文件
 *  需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文本）。两个工程均已开启 /utf-8 编译选项，无需 BOM。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "MainPanelGroupMenu.h"
#include "FavoritesModel.h"   // GetMenuLabel（收藏项文案，与联系人 / 会话右键菜单共用同一份）
#include "NotifyModeMenu.h"   // GetNotifyModeMenuTitle（「消息提醒设置」三处菜单共用同一份文字）

#include <stdio.h>
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// 追加一行功能项。
void PushItem(std::vector<GroupMenuEntry>& items, UINT id, const wchar_t* label)
{
    GroupMenuEntry e;
    e.m_id = id;
    e.m_isSeparator = false;
    e.m_label = label;
    items.push_back(e);
}

// 追加一条分隔条。
void PushSeparator(std::vector<GroupMenuEntry>& items)
{
    GroupMenuEntry e;
    e.m_id = 0;
    e.m_isSeparator = true;
    e.m_label = NULL;
    items.push_back(e);
}

// 群号文字的缓冲长度（字符）：64 位整数的十进制表示最多 20 位，留足余量
const size_t kGroupNumberBufChars = 32;

} // anonymous namespace

std::vector<GroupMenuEntry> BuildGroupMenuModel(const GroupMenuState& state)
{
    std::vector<GroupMenuEntry> items;

    // 分组规则与联系人、会话两个右键菜单相同（2026-10-03 统一）：主操作 / 查看与管理 /
    // 标记与提醒 / 复制 / 清空 / 退出，组与组之间一条分隔条。

    // 第一组：主操作。
    // 帖子群没有群聊窗，第一项打开的是帖子群窗口，文案随之改为「打开帖子群」（docs/帖子群方案.md 14.1）
    PushItem(items, kGroupMenuEnter, state.m_isThreadGroup ? TR(L"打开帖子群") : TR(L"进入群聊"));
    // 字幕模式：开关本群的桌面字幕，只弹出字幕、不打开群聊窗（docs/聊天字幕模式方案.md 第 1 节第 12 条）。
    // 帖子群没有聊天消息流，字幕模式没有内容可显示，不提供
    if (state.m_canSubtitle && !state.m_isThreadGroup)
        PushItem(items, kGroupMenuSubtitle, state.m_subtitleActive ? TR(L"关闭字幕模式") : TR(L"开启字幕模式"));
    PushSeparator(items);

    // 第二组：查看这个群；群管理只给群主与管理员，放在查看之后
    PushItem(items, kGroupMenuViewInfo,    TR(L"查看群资料"));
    PushItem(items, kGroupMenuViewHistory, TR(L"查看消息记录"));
    if (state.m_canManage)
    {
        PushItem(items, kGroupMenuManage, TR(L"群管理"));
    }
    PushSeparator(items);

    // 第三组：标记与提醒。收藏的文案由 FavoritesModel 统一给出，置顶随当前状态取反，消息提醒设置
    // 是二级菜单（2026-10-03 起取代「消息免打扰」），与会话列表右键菜单同一套写法。
    PushItem(items, kGroupMenuFavorite, FavoritesModel::GetMenuLabel(state.m_favorite));
    PushItem(items, kGroupMenuPin,  state.m_pinned ? TR(L"取消置顶") : TR(L"置顶群聊"));
    PushItem(items, kGroupMenuNotifyMode, GetNotifyModeMenuTitle());
    items.back().m_hasNotifySubMenu = true;
    PushSeparator(items);

    // 第四组：取这个群的信息。
    PushItem(items, kGroupMenuCopyGroupNo,   TR(L"复制群号"));
    PushItem(items, kGroupMenuCopyGroupName, TR(L"复制群名"));
    // 生成加群链接：与复制群号同属「把这个群分享出去」一类，放在同一组（docs/邀请入群方案.md §12）
    if (state.m_canCreateLink)
    {
        PushItem(items, kGroupMenuJoinLink, TR(L"生成加群链接"));
    }
    PushSeparator(items);

    // 第五组：清空聊天记录，只影响本人这一方的记录。
    PushItem(items, kGroupMenuClearMsgs, TR(L"清空聊天记录"));
    PushSeparator(items);

    // 第六组：改变成员关系的动作，放在最后。解散只有群主能做，服务端也只认群主；其余角色
    // 不显示，免得点了才被拒绝
    PushItem(items, kGroupMenuQuit, TR(L"退出群聊"));
    if (state.m_isOwner)
    {
        PushItem(items, kGroupMenuDismiss, TR(L"解散群聊"));
    }
    return items;
}

std::wstring FormatGroupNumber(long long groupId)
{
    if (groupId <= 0)
    {
        return std::wstring();
    }
    wchar_t buf[kGroupNumberBufChars] = { 0 };
    swprintf_s(buf, L"%lld", groupId);
    return std::wstring(buf);
}

} // namespace balloonmain
