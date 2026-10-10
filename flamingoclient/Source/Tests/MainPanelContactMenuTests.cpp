/**
 *  MainPanelContactMenuTests.cpp
 *
 *  通讯录联系人右键菜单（MainPanelContactMenu）的测试：验证纯函数
 *  BuildContactMenuModel() 产出的行模型 —— 右键同事时的条目数 / 顺序 /
 *  分隔条位置（防回归），右键自己时砍成只剩"查看资料"，命令 id 的唯一性与
 *  非 0 约束，随状态切换的文案（收藏），以及「消息提醒设置」二级菜单父项。
 *  文件末尾的 MENU-N1 横跨联系人 / 群 / 会话 / @某人 / 消息记录行五个右键菜单，核对同一个
 *  动作在各处的叫法逐字相同（2026-10-03 统一叫法时与用户确认）。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <set>
#include <vector>
#include <cwchar>          // wcscmp —— 比对菜单项中文文本

#include "../UI/BalloonMain/MainPanelContactMenu.h"
#include "../UI/BalloonMain/FavoritesModel.h"   // GetMenuLabel（收藏项文案须与本模型一致）
#include "../UI/BalloonMain/MainPanelGroupMenu.h"          // MENU-N1：跨菜单核对叫法
#include "../UI/BalloonMain/MainPanelConversationMenu.h"   // MENU-N1
#include "../UI/BalloonChatWnd/GroupMentionMenu.h"         // MENU-N1
#include "../UI/BalloonHistory/HistoryRowMenu.h"           // MENU-N1
#include "../net/Protocol.h"                               // MENU-N1：::message_type_text；enum conversation_notify_mode

using namespace balloonmain;

namespace {

// 造一份"右键的是同事"的状态。
ContactMenuState MakePeerState()
{
    ContactMenuState st;
    st.m_isSelf     = false;
    st.m_notifyMode = notify_mode_allow;
    st.m_favorite   = false;
    return st;
}

// 造一份"右键的是自己"的状态。
ContactMenuState MakeSelfState()
{
    ContactMenuState st;
    st.m_isSelf = true;
    return st;
}

// 模型里是否含有某个命令 id 的功能项。
bool HasId(const std::vector<ContactMenuEntry>& model, UINT id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].m_isSeparator && model[i].m_id == id)
        {
            return true;
        }
    }
    return false;
}

// 取某个命令 id 的显示文本；找不到返回 NULL。
const wchar_t* LabelOf(const std::vector<ContactMenuEntry>& model, UINT id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].m_isSeparator && model[i].m_id == id)
        {
            return model[i].m_label;
        }
    }
    return NULL;
}

} // anonymous namespace

TEST_CASE("CTM1 右键同事：五组依次是沟通、查看、标记与提醒、名字、清空，组间各一条分隔条", "[contactmenu]")
{
    const std::vector<ContactMenuEntry> model = BuildContactMenuModel(MakePeerState());

    // 10 个功能项 + 4 条分隔线 = 14 行。这条断言是防回归的锚：菜单形态一个字都不该悄悄变。
    // 2026-10-03 按统一的分组规则重排（沟通 / 查看 / 标记与提醒 / 名字 / 危险操作），
    // 「查看历史消息」改叫「查看消息记录」、「复制联系人名」改叫「复制名字」，与其它菜单同一叫法；
    // 同日「特别关注」「消息免打扰」两项合并成「消息提醒设置」二级菜单。
    REQUIRE(model.size() == 14);

    size_t i = 0;
    CHECK(model[i].m_id == kContactMenuSendMsg);        ++i;
    CHECK(model[i].m_id == kContactMenuVoice);          ++i;
    CHECK(model[i].m_id == kContactMenuVideo);          ++i;
    CHECK(model[i].m_isSeparator);                      ++i;
    CHECK(model[i].m_id == kContactMenuViewProfile);    ++i;
    CHECK(model[i].m_id == kContactMenuViewHistory);    ++i;
    CHECK(model[i].m_isSeparator);                      ++i;
    CHECK(model[i].m_id == kContactMenuFavorite);       ++i;
    CHECK(model[i].m_id == kContactMenuNotifyMode);     ++i;
    CHECK(model[i].m_isSeparator);                      ++i;
    CHECK(model[i].m_id == kContactMenuRemark);         ++i;
    CHECK(model[i].m_id == kContactMenuCopyName);       ++i;
    CHECK(model[i].m_isSeparator);                      ++i;
    CHECK(model[i].m_id == kContactMenuClearHistory);   ++i;
    CHECK(i == model.size());   // 逐项对完，正好走到末尾，没有漏检的行

    CHECK(wcscmp(LabelOf(model, kContactMenuViewHistory), L"查看消息记录") == 0);
    CHECK(wcscmp(LabelOf(model, kContactMenuCopyName), L"复制名字") == 0);
}

TEST_CASE("CTM1b 好友相关的三项已彻底移除", "[contactmenu]")
{
    // 企业版通讯录就是企业组织架构，联系人的归属由部门表达，既没有好友分组，
    // 也谈不上"删掉一个同事"。这三项不是灰显、是整项不存在。
    // 用字面量而不是枚举值来断言 —— 枚举项本身已经删了，这里要防的是"日后
    // 有人复用 7 / 9 / 10 这三个编号"。
    const std::vector<ContactMenuEntry> peer = BuildContactMenuModel(MakePeerState());
    CHECK_FALSE(HasId(peer, 7));    // 曾经的"移动到分组"
    CHECK_FALSE(HasId(peer, 9));    // 曾经的"加入黑名单"
    CHECK_FALSE(HasId(peer, 10));   // 曾经的"删除好友"
}

TEST_CASE("CTM2 右键自己：只剩查看资料一项", "[contactmenu]")
{
    const std::vector<ContactMenuEntry> model = BuildContactMenuModel(MakeSelfState());

    REQUIRE(model.size() == 1);
    CHECK_FALSE(model[0].m_isSeparator);
    CHECK(model[0].m_id == kContactMenuViewProfile);
    REQUIRE(model[0].m_label != NULL);
    CHECK(wcscmp(model[0].m_label, L"查看资料") == 0);
}

TEST_CASE("CTM3 右键自己：那些对自己无意义的项一个都不出现", "[contactmenu]")
{
    const std::vector<ContactMenuEntry> model = BuildContactMenuModel(MakeSelfState());

    // 与自己不能聊天，也谈不上给自己改备注。这些项必须整项不出现，而不是灰显
    // —— 灰显仍会让用户以为"本来可以，只是现在不行"。
    CHECK_FALSE(HasId(model, kContactMenuSendMsg));
    CHECK_FALSE(HasId(model, kContactMenuVoice));
    CHECK_FALSE(HasId(model, kContactMenuVideo));
    CHECK_FALSE(HasId(model, kContactMenuRemark));
    CHECK_FALSE(HasId(model, kContactMenuNotifyMode));
    CHECK_FALSE(HasId(model, kContactMenuFavorite));
    CHECK_FALSE(HasId(model, kContactMenuViewHistory));
    //复制自己的名字没有意义，自己那份菜单里同样不出现
    CHECK_FALSE(HasId(model, kContactMenuCopyName));

    // 只有一项时不该还留着分隔条 —— 没有东西可分。
    for (size_t i = 0; i < model.size(); ++i)
    {
        CHECK_FALSE(model[i].m_isSeparator);
    }
}

TEST_CASE("CTM4 命令 id 唯一且非 0（两种状态都验）", "[contactmenu]")
{
    // 0 是 DuiMenu::TrackPopup 的"未选中"返回值：被功能项占用的话，用户按 ESC
    // 关掉菜单会被当成选了那一项。
    ContactMenuState states[2];
    states[0] = MakePeerState();
    states[1] = MakeSelfState();

    for (size_t s = 0; s < 2; ++s)
    {
        const std::vector<ContactMenuEntry> model = BuildContactMenuModel(states[s]);
        std::set<UINT> seen;
        for (size_t i = 0; i < model.size(); ++i)
        {
            if (model[i].m_isSeparator)
            {
                CHECK(model[i].m_id == 0);
                CHECK(model[i].m_label == NULL);
                continue;
            }
            CHECK(model[i].m_id != 0);
            CHECK(model[i].m_label != NULL);
            CHECK(seen.insert(model[i].m_id).second);   // 未重复出现过
        }
    }
}

TEST_CASE("CTM5 收藏的文案随当前状态取反，「消息提醒设置」父项文字固定", "[contactmenu]")
{
    SECTION("允许提醒 / 未收藏")
    {
        ContactMenuState st = MakePeerState();
        st.m_notifyMode = notify_mode_allow;
        st.m_favorite   = false;
        const std::vector<ContactMenuEntry> model = BuildContactMenuModel(st);

        const wchar_t* notify = LabelOf(model, kContactMenuNotifyMode);
        REQUIRE(notify != NULL);
        CHECK(wcscmp(notify, L"消息提醒设置") == 0);

        // 收藏项文案与会话右键菜单共用 FavoritesModel 那一份，避免措辞漂移。
        const wchar_t* fav = LabelOf(model, kContactMenuFavorite);
        REQUIRE(fav != NULL);
        CHECK(wcscmp(fav, FavoritesModel::GetMenuLabel(false)) == 0);
    }

    SECTION("特别关注 / 已收藏")
    {
        ContactMenuState st = MakePeerState();
        st.m_notifyMode = notify_mode_starred;
        st.m_favorite   = true;
        const std::vector<ContactMenuEntry> model = BuildContactMenuModel(st);

        const wchar_t* notify = LabelOf(model, kContactMenuNotifyMode);
        REQUIRE(notify != NULL);
        CHECK(wcscmp(notify, L"消息提醒设置") == 0);

        const wchar_t* fav = LabelOf(model, kContactMenuFavorite);
        REQUIRE(fav != NULL);
        CHECK(wcscmp(fav, FavoritesModel::GetMenuLabel(true)) == 0);
    }
}

TEST_CASE("CTM6 是自己时 notifyMode / favorite 不影响结果", "[contactmenu]")
{
    // 自己那份菜单里根本没有消息提醒设置与收藏两项，故这两个状态取什么值都应产出
    // 同一份模型。这条防的是"以后有人给自己那份也加了状态相关项却忘了想清楚"。
    ContactMenuState a = MakeSelfState();
    a.m_notifyMode = notify_mode_allow;
    a.m_favorite   = false;

    ContactMenuState b = MakeSelfState();
    b.m_notifyMode = notify_mode_starred;
    b.m_favorite   = true;

    const std::vector<ContactMenuEntry> ma = BuildContactMenuModel(a);
    const std::vector<ContactMenuEntry> mb = BuildContactMenuModel(b);

    REQUIRE(ma.size() == mb.size());
    for (size_t i = 0; i < ma.size(); ++i)
    {
        CHECK(ma[i].m_id == mb[i].m_id);
        CHECK(ma[i].m_isSeparator == mb[i].m_isSeparator);
        if (ma[i].m_label != NULL && mb[i].m_label != NULL)
        {
            CHECK(wcscmp(ma[i].m_label, mb[i].m_label) == 0);
        }
    }
}

TEST_CASE("CTM7 清空聊天记录：右键同事时单独一组排在最后，右键自己时不出现", "[contactmenu][clearconv]")
{
    const std::vector<ContactMenuEntry> peer = BuildContactMenuModel(MakePeerState());
    REQUIRE(peer.size() >= 2);
    //最后一项，前面隔一条分隔线，与日常动作拉开距离
    CHECK(peer.back().m_id == kContactMenuClearHistory);
    REQUIRE(peer.back().m_label != NULL);
    CHECK(wcscmp(peer.back().m_label, L"清空聊天记录") == 0);
    CHECK(peer[peer.size() - 2].m_isSeparator);
    //新编号非 0，且不复用已撤销的 1、7、9、10
    CHECK(kContactMenuClearHistory != 0);
    CHECK(kContactMenuClearHistory != 1);
    CHECK(kContactMenuClearHistory != 7);
    CHECK(kContactMenuClearHistory != 9);
    CHECK(kContactMenuClearHistory != 10);

    //右键自己：没有和自己的单聊，不出现
    ContactMenuState self = MakePeerState();
    self.m_isSelf = true;
    const std::vector<ContactMenuEntry> mine = BuildContactMenuModel(self);
    for (size_t i = 0; i < mine.size(); ++i)
    {
        CHECK(mine[i].m_id != kContactMenuClearHistory);
    }
}

// =====================================================================
// MENU-N1 —— 同一个动作在各右键菜单里叫法相同（2026-10-03）
// =====================================================================

namespace {

// MENU-N1 里 @某人 菜单用到的两个 userid：本人与被 @ 的同事，取值任意，只要不同且非 0。
const long long kWordingSelfUid  = 10001;
const long long kWordingOtherUid = 10002;
// MENU-N1 里消息记录行用到的 msgid：任意已分配的值，让「删除」「多选」等项出现。
const int64_t   kWordingMsgId    = 20261003;
// grouprole 网络侧角色取值：0 为普通成员（见 Session/GroupRoleRules.h）。
const int       kWordingNetMember = 0;

// 主面板三个菜单（联系人 / 群 / 会话）的行模型里取指定 id 那一项的文案；没有该项时返回空串。
template <typename Entry>
std::wstring MainMenuLabelOf(const std::vector<Entry>& rows, UINT id)
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!rows[i].m_isSeparator && rows[i].m_id == id && rows[i].m_label != NULL)
        {
            return rows[i].m_label;
        }
    }
    return std::wstring();
}

// @某人 菜单的行模型里取指定 id 那一项的文案；没有该项时返回空串。
std::wstring MentionLabelOf(const std::vector<balloonchatwnd::MentionMenuEntry>& rows,
                            balloonchatwnd::MentionMenuId id)
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!rows[i].isSeparator && rows[i].id == id && rows[i].label != NULL)
        {
            return rows[i].label;
        }
    }
    return std::wstring();
}

// 消息记录行菜单的行模型里取指定 id 那一项的文案；没有该项时返回空串。
std::wstring HistoryLabelOf(const std::vector<balloonhistory::HistoryRowMenuItem>& rows,
                            balloonhistory::HistoryRowMenuId id)
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!rows[i].m_isSeparator && rows[i].m_id == id)
        {
            return rows[i].m_label;
        }
    }
    return std::wstring();
}

} // anonymous namespace

TEST_CASE("MENU-N1 同一个动作在各右键菜单里叫法相同", "[contactmenu][menuwording]")
{
    //五个菜单都取"状态全为否"的形态：未收藏、允许提醒、未置顶
    const std::vector<ContactMenuEntry> contact = BuildContactMenuModel(MakePeerState());
    const std::vector<GroupMenuEntry> group = BuildGroupMenuModel(GroupMenuState());
    ConversationMenuState singleState;
    singleState.m_isGroup = false;
    const std::vector<ConversationMenuEntry> convSingle = BuildConversationMenuModel(singleState);
    ConversationMenuState groupState;
    groupState.m_isGroup = true;
    const std::vector<ConversationMenuEntry> convGroup = BuildConversationMenuModel(groupState);
    const std::vector<balloonchatwnd::MentionMenuEntry> mention =
        balloonchatwnd::BuildMentionMenuModel(kWordingOtherUid, kWordingSelfUid, true,
                                              kWordingNetMember, kWordingNetMember);
    balloonhistory::HistoryRowMenuContext historyCtx;
    historyCtx.m_msgType = ::message_type_text;
    historyCtx.m_msgId = kWordingMsgId;
    historyCtx.m_canClearConversation = true;
    const std::vector<balloonhistory::HistoryRowMenuItem> history =
        balloonhistory::BuildHistoryRowMenuModel(historyCtx);

    //发消息：联系人菜单与 @某人 菜单（原先 @某人 菜单叫「与 TA 聊天」）
    CHECK(MainMenuLabelOf(contact, kContactMenuSendMsg) == L"发消息");
    CHECK(MentionLabelOf(mention, balloonchatwnd::kMentionMenuChat) == L"发消息");

    //查看资料：联系人、单聊会话、@某人 三处；群与群会话叫「查看群资料」
    CHECK(MainMenuLabelOf(contact, kContactMenuViewProfile) == L"查看资料");
    CHECK(MainMenuLabelOf(convSingle, kConvMenuProfile) == L"查看资料");
    CHECK(MentionLabelOf(mention, balloonchatwnd::kMentionMenuProfile) == L"查看资料");
    CHECK(MainMenuLabelOf(group, kGroupMenuViewInfo) == L"查看群资料");
    CHECK(MainMenuLabelOf(convGroup, kConvMenuProfile) == L"查看群资料");

    //查看消息记录：联系人、群、单聊与群会话（原先叫「查看历史消息」）
    CHECK(MainMenuLabelOf(contact, kContactMenuViewHistory) == L"查看消息记录");
    CHECK(MainMenuLabelOf(group, kGroupMenuViewHistory) == L"查看消息记录");
    CHECK(MainMenuLabelOf(convSingle, kConvMenuHistory) == L"查看消息记录");
    CHECK(MainMenuLabelOf(convGroup, kConvMenuHistory) == L"查看消息记录");

    //复制名字：联系人、单聊会话、@某人（原先分别叫「复制联系人名」「复制 TA 的昵称」）
    CHECK(MainMenuLabelOf(contact, kContactMenuCopyName) == L"复制名字");
    CHECK(MainMenuLabelOf(convSingle, kConvMenuCopyName) == L"复制名字");
    CHECK(MentionLabelOf(mention, balloonchatwnd::kMentionMenuCopyName) == L"复制名字");

    //复制群号 / 复制群名：群菜单与群会话菜单
    CHECK(MainMenuLabelOf(group, kGroupMenuCopyGroupNo) == L"复制群号");
    CHECK(MainMenuLabelOf(convGroup, kConvMenuCopyGroupNo) == L"复制群号");
    CHECK(MainMenuLabelOf(group, kGroupMenuCopyGroupName) == L"复制群名");
    CHECK(MainMenuLabelOf(convGroup, kConvMenuCopyGroupName) == L"复制群名");

    //特别关注：@某人菜单里的快捷项。2026-10-06 起群成员菜单里叫「设为特别关注联系人」，与「关注 TA 在本群的发言」
    //区分开（docs/消息提醒模式与消息盒子方案.md §12.1）；联系人与会话菜单的二级菜单里仍叫「特别关注」
    CHECK(MentionLabelOf(mention, balloonchatwnd::kMentionMenuStarred) == L"设为特别关注联系人");

    //添加到收藏 / 消息提醒设置：联系人、群、会话
    CHECK(MainMenuLabelOf(contact, kContactMenuFavorite) == L"添加到收藏");
    CHECK(MainMenuLabelOf(group, kGroupMenuFavorite) == L"添加到收藏");
    CHECK(MainMenuLabelOf(convSingle, kConvMenuFavorite) == L"添加到收藏");
    CHECK(MainMenuLabelOf(contact, kContactMenuNotifyMode) == L"消息提醒设置");
    CHECK(MainMenuLabelOf(group, kGroupMenuNotifyMode) == L"消息提醒设置");
    CHECK(MainMenuLabelOf(convSingle, kConvMenuNotifyMode) == L"消息提醒设置");
    CHECK(MainMenuLabelOf(convGroup, kConvMenuNotifyMode) == L"消息提醒设置");

    //清空聊天记录：联系人、群、会话、消息记录行
    CHECK(MainMenuLabelOf(contact, kContactMenuClearHistory) == L"清空聊天记录");
    CHECK(MainMenuLabelOf(group, kGroupMenuClearMsgs) == L"清空聊天记录");
    CHECK(MainMenuLabelOf(convSingle, kConvMenuClearHistory) == L"清空聊天记录");
    CHECK(HistoryLabelOf(history, balloonhistory::kHistoryRowMenuClearConversation) == L"清空聊天记录");

    //共享文件给TA / 共享屏幕给TA：联系人与单聊会话（功能可用时出现）；群会话叫「…到本群」
    ContactMenuState contactShare = MakePeerState();
    contactShare.m_canShareFile = true;
    contactShare.m_canShareScreen = true;
    const std::vector<ContactMenuEntry> contactWithShare = BuildContactMenuModel(contactShare);
    ConversationMenuState singleShare = singleState;
    singleShare.m_canShareFile = true;
    singleShare.m_canShareScreen = true;
    const std::vector<ConversationMenuEntry> convSingleShare = BuildConversationMenuModel(singleShare);
    ConversationMenuState groupShare = groupState;
    groupShare.m_canShareFile = true;
    groupShare.m_canShareScreen = true;
    const std::vector<ConversationMenuEntry> convGroupShare = BuildConversationMenuModel(groupShare);
    CHECK(MainMenuLabelOf(contactWithShare, kContactMenuShareFile) == L"共享文件给TA");
    CHECK(MainMenuLabelOf(convSingleShare, kConvMenuShareFile) == L"共享文件给TA");
    CHECK(MainMenuLabelOf(contactWithShare, kContactMenuShareScreen) == L"共享屏幕给TA");
    CHECK(MainMenuLabelOf(convSingleShare, kConvMenuShareScreen) == L"共享屏幕给TA");
    CHECK(MainMenuLabelOf(convGroupShare, kConvMenuShareFile) == L"共享文件到本群");
    CHECK(MainMenuLabelOf(convGroupShare, kConvMenuShareScreen) == L"共享屏幕到本群");
}

TEST_CASE("CTM8 共享文件给TA、共享屏幕给TA：在视频通话之后，功能可用时才出现，右键自己时不出现", "[contactmenu][menushare]")
{
    //两项功能都可用：紧跟在「视频通话」之后，仍在第一组
    ContactMenuState both = MakePeerState();
    both.m_canShareFile = true;
    both.m_canShareScreen = true;
    const std::vector<ContactMenuEntry> model = BuildContactMenuModel(both);
    REQUIRE(model.size() == 16);
    CHECK(model[2].m_id == kContactMenuVideo);
    CHECK(model[3].m_id == kContactMenuShareFile);
    CHECK(model[4].m_id == kContactMenuShareScreen);
    CHECK(model[5].m_isSeparator);
    CHECK(wcscmp(LabelOf(model, kContactMenuShareFile), L"共享文件给TA") == 0);
    CHECK(wcscmp(LabelOf(model, kContactMenuShareScreen), L"共享屏幕给TA") == 0);

    //只有文件共享可用：只出现「共享文件给TA」
    ContactMenuState fileOnly = MakePeerState();
    fileOnly.m_canShareFile = true;
    const std::vector<ContactMenuEntry> fileModel = BuildContactMenuModel(fileOnly);
    CHECK(HasId(fileModel, kContactMenuShareFile));
    CHECK_FALSE(HasId(fileModel, kContactMenuShareScreen));

    //都不可用：两项都不出现，菜单与原来相同
    const std::vector<ContactMenuEntry> none = BuildContactMenuModel(MakePeerState());
    CHECK_FALSE(HasId(none, kContactMenuShareFile));
    CHECK_FALSE(HasId(none, kContactMenuShareScreen));

    //右键自己：即便功能可用也不出现
    ContactMenuState self = MakeSelfState();
    self.m_canShareFile = true;
    self.m_canShareScreen = true;
    const std::vector<ContactMenuEntry> selfModel = BuildContactMenuModel(self);
    CHECK_FALSE(HasId(selfModel, kContactMenuShareFile));
    CHECK_FALSE(HasId(selfModel, kContactMenuShareScreen));

    //新编号不与已有的、包括已撤销的编号冲突
    CHECK(kContactMenuShareFile != kContactMenuShareScreen);
    CHECK(kContactMenuShareFile > kContactMenuClearHistory);
}
