/**
 *  MainPanelConversationMenu.cpp
 *
 *  会话右键菜单行模型的实现。仅装配数据（项 id / 文本 / 可用性 / 分隔条），不碰
 *  任何窗口 —— 故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。
 *  在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文本）。两个工程均已开启 /utf-8 编译选项，源码按
 *  UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "MainPanelConversationMenu.h"
#include "NotifyModeMenu.h"   // GetNotifyModeMenuTitle（「消息提醒设置」三处菜单共用同一份文字）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// 追加一行功能项。enabled 为 false 时调用方应走 DuiMenu::AppendDisabled 渲染成灰显。
void PushItem(std::vector<ConversationMenuEntry>& items, UINT id,
              const wchar_t* label, bool enabled)
{
    ConversationMenuEntry e;
    e.m_id          = id;
    e.m_isSeparator = false;
    e.m_label       = label;
    e.m_enabled     = enabled;
    items.push_back(e);
}

// 追加一条分隔线。
void PushSeparator(std::vector<ConversationMenuEntry>& items)
{
    ConversationMenuEntry e;
    e.m_id          = 0;
    e.m_isSeparator = true;
    e.m_label       = NULL;
    e.m_enabled     = true;
    items.push_back(e);
}

} // anonymous namespace

std::vector<ConversationMenuEntry> BuildConversationMenuModel(const ConversationMenuState& state)
{
    std::vector<ConversationMenuEntry> items;

    // 分组规则与联系人、群两个右键菜单相同（2026-10-03 统一）：主操作 / 查看 / 标记与提醒 /
    // 复制 / 清空 / 删除会话，组与组之间一条分隔条。

    // 第一组：主操作。第二项在「标记已读」与「标为未读」之间二选一（docs/会话标为未读方案.md）：
    // 有未读或已手动标为未读时是「标记已读」，否则是「标为未读」，位置不变，菜单形态不随未读数跳动。
    PushItem(items, kConvMenuOpen, TR(L"打开会话"), true);
    if (state.m_unread > 0 || state.m_unreadMarked)
        PushItem(items, kConvMenuMarkRead, TR(L"标记已读"), true);
    else
        PushItem(items, kConvMenuMarkUnread, TR(L"标为未读"), true);
    // 共享文件、共享屏幕：针对这个会话的协作动作，放在「标记已读」之后、仍在第一组（2026-10-03 与菜单会话商定）；
    // 单聊写「给TA」，群聊写「到本群」；对应功能不可用时整项不出现
    if (state.m_canShareFile)
        PushItem(items, kConvMenuShareFile, state.m_isGroup ? TR(L"共享文件到本群") : TR(L"共享文件给TA"), true);
    if (state.m_canShareScreen)
        PushItem(items, kConvMenuShareScreen, state.m_isGroup ? TR(L"共享屏幕到本群") : TR(L"共享屏幕给TA"), true);
    // 字幕模式：开关这个会话的桌面字幕，只弹出字幕、不打开聊天窗（docs/聊天字幕模式方案.md 第 1 节第 12 条）
    if (state.m_canSubtitle)
        PushItem(items, kConvMenuSubtitle, state.m_subtitleActive ? TR(L"关闭字幕模式") : TR(L"开启字幕模式"), true);
    PushSeparator(items);

    // 第二组：查看。
    PushItem(items, kConvMenuProfile,
             state.m_isGroup ? TR(L"查看群资料") : TR(L"查看资料"), true);
    PushItem(items, kConvMenuHistory, TR(L"查看消息记录"), true);
    PushSeparator(items);

    // 第三组：标记与提醒。消息提醒设置是二级菜单（2026-10-03 起取代「消息免打扰」「特别关注」两项），
    // 单聊与群聊都有，子菜单里屏蔽一项只给群聊。
    PushItem(items, kConvMenuFavorite,
             state.m_favorite ? TR(L"取消收藏") : TR(L"添加到收藏"), true);
    PushItem(items, kConvMenuPin,
             state.m_pinned ? TR(L"取消置顶") : TR(L"置顶会话"), true);
    PushItem(items, kConvMenuNotifyMode, GetNotifyModeMenuTitle(), true);
    items.back().m_hasNotifySubMenu = true;
    PushSeparator(items);

    // 第四组：复制。单聊复制列表上显示的名字，群聊复制群号与群名。
    if (state.m_isGroup)
    {
        PushItem(items, kConvMenuCopyGroupNo,   TR(L"复制群号"), true);
        PushItem(items, kConvMenuCopyGroupName, TR(L"复制群名"), true);
        // 生成加群链接（docs/邀请入群方案.md §12）
        if (state.m_canCreateLink)
        {
            PushItem(items, kConvMenuJoinLink, TR(L"生成加群链接"), true);
        }
    }
    else
    {
        PushItem(items, kConvMenuCopyName, TR(L"复制名字"), true);
    }
    PushSeparator(items);

    // 第五组：清空聊天记录，只影响本人这一方的记录。
    PushItem(items, kConvMenuClearHistory, TR(L"清空聊天记录"), true);
    PushSeparator(items);

    // 第六组：把会话从列表拿掉，放在最后。
    PushItem(items, kConvMenuDelete, TR(L"删除该会话"), true);
    PushItem(items, kConvMenuDeleteAll, TR(L"删除全部会话"), true);

    return items;
}

} // namespace balloonmain
