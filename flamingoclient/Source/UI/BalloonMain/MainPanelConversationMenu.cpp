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
#include "StarredModel.h"     // GetMenuLabel（特别关注项文案，四处菜单共用同一份）

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

    // 第一组：进入 / 读取这条会话的日常动作。
    PushItem(items, kConvMenuOpen, L"打开会话", true);
    // 未读为 0 时"标记已读"灰显——已经全读完了，点它不会有任何变化，给个可点的项
    // 反而让人以为漏了什么没读。
    PushItem(items, kConvMenuMarkRead, L"标记已读", state.m_unread > 0);
    PushItem(items, kConvMenuHistory, L"查看历史消息", true);
    // 单聊看的是联系人资料，群聊看的是群资料，两者打开的是不同对话框，文案必须区分。
    PushItem(items, kConvMenuProfile,
             state.m_isGroup ? L"查看群资料" : L"查看资料", true);
    // 收藏项紧跟在"查看资料"之后，与联系人 / 群两个右键菜单同一位置。文案随当前
    // 状态取反，与下面的置顶 / 免打扰同一套写法。
    PushItem(items, kConvMenuFavorite,
             state.m_favorite ? L"取消收藏" : L"添加到收藏", true);
    // 特别关注紧跟收藏，与联系人右键菜单同一位置。<u>只在单聊会话上出现</u>：特别关注
    // 关心的是"这个人上线 / 下线了"，群本身没有上下线可言，在群会话上摆这一项只会
    // 让用户以为能关注整个群。故群会话这一项整项不出现，而不是灰显。
    if (!state.m_isGroup)
    {
        PushItem(items, kConvMenuStarred,
                 StarredModel::GetMenuLabel(state.m_starred), true);
    }

    // 第二组：会话属性开关。文案随当前状态取反，用户一眼能看出点下去会变成什么。
    PushSeparator(items);
    PushItem(items, kConvMenuPin,
             state.m_pinned ? L"取消置顶" : L"置顶会话", true);
    PushItem(items, kConvMenuMute,
             state.m_muted ? L"取消免打扰" : L"消息免打扰", true);

    // 第三组：删除。单独一组、放在最底下，与上面的日常动作拉开距离，减少误点。
    PushSeparator(items);
    PushItem(items, kConvMenuDelete, L"删除该会话", true);
    PushItem(items, kConvMenuDeleteAll, L"删除全部会话", true);

    return items;
}

} // namespace balloonmain
