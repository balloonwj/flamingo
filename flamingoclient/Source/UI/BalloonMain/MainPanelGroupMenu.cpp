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

#include <stdio.h>

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

    // 第一组：进入、查看与整理这个群。收藏项紧跟在"查看群资料"之后，与联系人 / 会话两个
    // 右键菜单同一位置，文案由 FavoritesModel 统一给出。
    PushItem(items, kGroupMenuEnter,    L"进入群聊");
    PushItem(items, kGroupMenuViewInfo, L"查看群资料");
    PushItem(items, kGroupMenuFavorite, FavoritesModel::GetMenuLabel(state.m_favorite));
    PushItem(items, kGroupMenuViewHistory, L"查看历史消息");
    // 复制两项紧跟查看类动作：都是"取这个群的信息"，摆在一起好找
    PushItem(items, kGroupMenuCopyGroupNo,   L"复制群号");
    PushItem(items, kGroupMenuCopyGroupName, L"复制群名");
    if (state.m_canManage)
    {
        PushItem(items, kGroupMenuManage, L"群管理");
    }
    // 文案随当前状态取反，与会话列表右键菜单同一套写法。
    PushItem(items, kGroupMenuPin,  state.m_pinned ? L"取消置顶" : L"置顶群聊");
    PushItem(items, kGroupMenuMute, state.m_muted  ? L"取消免打扰" : L"消息免打扰");
    PushSeparator(items);

    // 第二组：影响较大的动作，与上面的日常动作用分隔条隔开，减少误点。
    PushItem(items, kGroupMenuClearMsgs, L"清空聊天记录");
    PushItem(items, kGroupMenuQuit,      L"退出群聊");
    // 解散只有群主能做，服务端也只认群主；其余角色不显示，免得点了才被拒绝
    if (state.m_isOwner)
    {
        PushItem(items, kGroupMenuDismiss, L"解散群聊");
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
