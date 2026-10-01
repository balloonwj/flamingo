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
#include "StarredModel.h"     // GetMenuLabel（特别关注项文案，四处菜单共用同一份）

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
        PushItem(items, kContactMenuViewProfile, L"查看资料");
        return items;
    }

    // 第一组：与该联系人直接沟通的动作。
    PushItem(items, kContactMenuSendMsg, L"发消息");
    PushItem(items, kContactMenuVoice,   L"语音通话");
    PushItem(items, kContactMenuVideo,   L"视频通话");
    PushSeparator(items);

    // 第二组：查看与整理这个联系人。
    PushItem(items, kContactMenuViewProfile, L"查看资料");
    // 收藏项紧跟在"查看资料"之后，与会话 / 群两个右键菜单同一位置。文案随当前
    // 状态取反，与下面的免打扰同一套写法；取反的文案由 FavoritesModel 统一给出，
    // 免得三处菜单各写一份措辞不同的字面量。
    PushItem(items, kContactMenuFavorite,
             FavoritesModel::GetMenuLabel(state.m_favorite));
    // 特别关注紧跟收藏 —— 两者都是"给这个人打个标记"，摆在一起用户好找。文案同样
    // 由模型统一给出（四处菜单共用一份措辞，见 StarredModel::GetMenuLabel）。
    PushItem(items, kContactMenuStarred,
             StarredModel::GetMenuLabel(state.m_starred));
    PushItem(items, kContactMenuViewHistory, L"查看历史消息");
    PushItem(items, kContactMenuRemark,      L"修改备注");
    // 复制的是列表上显示的名字（备注优先），紧跟"修改备注"：两项都与这个名字有关
    PushItem(items, kContactMenuCopyName,    L"复制联系人名");
    PushSeparator(items);

    // 第三组：打扰控制。文案随当前状态取反。
    PushItem(items, kContactMenuMute,
             state.m_muted ? L"取消免打扰" : L"消息免打扰");

    // 企业版没有好友关系，原先第二组末尾的"移动到分组"、第三组的"加入黑名单"、
    // 以及单独一组的"删除好友"都随好友功能一并去掉了：通讯录是企业组织架构，
    // 归属由部门表达，也谈不上"删掉一个同事"。

    return items;
}

} // namespace balloonmain
