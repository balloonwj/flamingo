/**
 *  MainPanelContactMenu.cpp
 *
 *  联系人右键菜单行模型的实现。仅装配数据（项 id / 文本 / 分隔条），不碰任何
 *  窗口 —— 故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。
 *  在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文本）。两个工程均已开启 /utf-8 编译选项，源码按
 *  UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#include "MainPanelContactMenu.h"
#include "FavoritesModel.h"   // GetMenuLabel（收藏项文案，与会话右键菜单共用同一份）
#include "NotifyModeMenu.h"   // GetNotifyModeMenuTitle（「消息提醒设置」三处菜单共用同一份文字）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// 追加一行功能项。
void PushItem(std::vector<ContactMenuEntry>& items, UINT id, const wchar_t* label)
{
    ContactMenuEntry e;
    e.m_id          = id;
    e.m_isSeparator = false;
    e.m_label       = label;
    items.push_back(e);
}

// 追加一条分隔线。
void PushSeparator(std::vector<ContactMenuEntry>& items)
{
    ContactMenuEntry e;
    e.m_id          = 0;
    e.m_isSeparator = true;
    e.m_label       = NULL;
    items.push_back(e);
}

} // anonymous namespace

std::vector<ContactMenuEntry> BuildContactMenuModel(const ContactMenuState& state)
{
    std::vector<ContactMenuEntry> items;

    // 右键的是自己：整份菜单只留"查看资料"一项。
    // 通讯录里能看到自己是为了让用户看清自己在组织架构里的位置，但"给自己发消息"
    // "给自己改备注"这些动作要么无意义要么荒唐，整项不出现比灰显在那里更干净。
    // 也不放分隔条——只有一项，分隔条无处可分。
    if (state.m_isSelf)
    {
        PushItem(items, kContactMenuViewProfile, TR(L"查看资料"));
        return items;
    }

    // 分组规则与群、会话两个右键菜单相同（2026-10-03 统一）：沟通 / 查看 / 标记与提醒 /
    // 名字 / 危险操作，组与组之间一条分隔条。同一个动作在各菜单里叫法相同。

    // 第一组：与该联系人直接沟通的动作。
    PushItem(items, kContactMenuSendMsg, TR(L"发消息"));
    PushItem(items, kContactMenuVoice,   TR(L"语音通话"));
    PushItem(items, kContactMenuVideo,   TR(L"视频通话"));
    // 共享文件、共享屏幕也是与这个人直接协作的动作，归入第一组；对应功能不可用时整项不出现
    if (state.m_canShareFile)
        PushItem(items, kContactMenuShareFile, TR(L"共享文件给TA"));
    if (state.m_canShareScreen)
        PushItem(items, kContactMenuShareScreen, TR(L"共享屏幕给TA"));
    // 字幕模式：开关与这个人单聊的桌面字幕，只弹出字幕、不打开聊天窗（docs/聊天字幕模式方案.md 第 1 节第 12 条）
    if (state.m_canSubtitle)
        PushItem(items, kContactMenuSubtitle, state.m_subtitleActive ? TR(L"关闭字幕模式") : TR(L"开启字幕模式"));
    PushSeparator(items);

    // 第二组：查看。
    PushItem(items, kContactMenuViewProfile, TR(L"查看资料"));
    PushItem(items, kContactMenuViewHistory, TR(L"查看消息记录"));
    PushSeparator(items);

    // 第三组：给这个人打标记、设置提醒方式。收藏的文案由 FavoritesModel 统一给出；消息提醒设置是
    // 二级菜单（2026-10-03 起取代「特别关注」「消息免打扰」两项），三处菜单叫法相同。
    PushItem(items, kContactMenuFavorite,
             FavoritesModel::GetMenuLabel(state.m_favorite));
    PushItem(items, kContactMenuNotifyMode, GetNotifyModeMenuTitle());
    items.back().m_hasNotifySubMenu = true;
    PushSeparator(items);

    // 第四组：与这个名字有关的动作。复制的是列表上显示的名字（备注优先）。
    PushItem(items, kContactMenuRemark,   TR(L"修改备注"));
    PushItem(items, kContactMenuCopyName, TR(L"复制名字"));

    // 第五组：清空聊天记录。不可恢复，单独一组放在最底下，与上面的日常动作拉开距离。
    PushSeparator(items);
    PushItem(items, kContactMenuClearHistory, TR(L"清空聊天记录"));

    // 企业版没有好友关系，原先的"移动到分组""加入黑名单""删除好友"都随好友功能一并去掉了：
    // 通讯录是企业组织架构，归属由部门表达，也谈不上"删掉一个同事"。

    return items;
}

} // namespace balloonmain
