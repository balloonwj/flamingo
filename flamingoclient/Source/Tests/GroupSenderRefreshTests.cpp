/**
 *  GroupSenderRefreshTests.cpp
 *
 *  群聊气泡发送者显示信息的解析与刷新测试（标签 [groupsender]）。
 *
 *  背景：群聊窗改为成员列表与首屏历史并行请求，历史可能先到。先到时对方消息按备用
 *  信息（"用户<id>"）显示，成员到达后用 ApplyGroupSendersToList 就地刷新。
 *
 *  覆盖：
 *    G1 历史先到、成员后到：刷新后发送者名字、字形、底色、真图路径换成成员表里的值
 *    G2 成员先到：解析直接得到真实信息；名字、字形为空时按规则回退
 *    G3 自己发的消息不受刷新影响
 *    G4 成员表里查不到的发送者（已退群）保持备用信息
 *    G5 刷新只改显示信息：条数、顺序、滚动位置都不变
 *    GSR1 依次查成员表与通讯录：成员表命中时以成员信息为准，不再查通讯录
 *    GSR2 成员表里没有（已退群）、通讯录里有：用通讯录里的昵称与头像
 *    GSR3 两处都没有：查不到，解析结果回退"用户<id>"
 *    GSR4 成员表到达后刷新：退群者的气泡换成通讯录里的名字，而不是保持"用户<id>"（BUG-49）
 *
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <vector>

#include "../UI/BalloonChatWnd/GroupSenderRefresh.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

using namespace balloonchatwnd;

namespace {

// 测试里的几个 userid：本人、在成员表里的成员、已退群（不在成员表里）的人。
const int64_t kSelfId    = 5127;
const int64_t kMemberId  = 5308;
const int64_t kLeftId    = 9999;

// 查不到成员时的备用底色，以及成员表里那位成员的底色。
const COLORREF kFallbackColor = RGB(0x88, 0x88, 0x88);
const COLORREF kMemberColor   = RGB(0x3B, 0x82, 0xF6);

// 列表尺寸（像素）与撑出滚动范围用的消息条数。
const int kListW    = 400;
const int kListH    = 300;
const int kMsgCount = 30;

// 查询回调：从 std::map 里查。
bool LookupFromMap(void* ctx, int64_t userId, GroupSenderRender& out)
{
    const std::map<int64_t, GroupSenderRender>* members =
        static_cast<const std::map<int64_t, GroupSenderRender>*>(ctx);
    std::map<int64_t, GroupSenderRender>::const_iterator it = members->find(userId);
    if (it == members->end())
    {
        return false;
    }
    out = it->second;
    return true;
}

// 成员表：只有 kMemberId 一个人。
std::map<int64_t, GroupSenderRender> MakeMembers()
{
    GroupSenderRender r;
    r.m_name      = _T("韩晓燕888");
    r.m_glyph     = _T("燕");
    r.m_color     = kMemberColor;
    r.m_imagePath = _T("C:\\avatar\\5308.png");
    std::map<int64_t, GroupSenderRender> members;
    members[kMemberId] = r;
    return members;
}

// 造一条对方发来、发送者信息按"成员表里查不到"回退的消息（模拟历史先于成员到达）。
ChatMessage MakeFallbackMessage(int64_t senderId, const CString& text)
{
    const GroupSenderRender r = ResolveGroupSender(senderId, NULL, kFallbackColor);
    ChatMessage m = MakeTextMessage(false, text);
    m.senderUserId          = senderId;
    m.senderName            = r.m_name;
    m.senderAvatarText      = r.m_glyph;
    m.senderAvatarColor     = r.m_color;
    m.senderAvatarImagePath = r.m_imagePath;
    return m;
}

// 取列表里的全部消息气泡（跳过时间分隔行）。
std::vector<ChatBubble*> MessageBubbles(ChatMessageList& list)
{
    std::vector<ChatBubble*> out;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().kind != kMsgTime)
        {
            out.push_back(b);
        }
    }
    return out;
}

} // anonymous namespace

TEST_CASE("G1 历史先到、成员后到：刷新后换成成员表里的信息", "[groupsender]")
{
    ChatMessageList list;
    list.AppendMessage(MakeFallbackMessage(kMemberId, _T("第一条")));
    list.AppendMessage(MakeFallbackMessage(kMemberId, _T("第二条")));
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);

    //刷新前是备用信息
    std::vector<ChatBubble*> bubbles = MessageBubbles(list);
    REQUIRE(bubbles.size() == 2);
    CHECK(bubbles[0]->GetMessage().senderName == CString(_T("用户5308")));

    std::map<int64_t, GroupSenderRender> members = MakeMembers();
    CHECK(ApplyGroupSendersToList(list, &LookupFromMap, &members) == 2);

    for (size_t i = 0; i < bubbles.size(); ++i)
    {
        INFO("bubble " << i);
        const ChatMessage& m = bubbles[i]->GetMessage();
        CHECK(m.senderName == CString(_T("韩晓燕888")));
        CHECK(m.senderAvatarText == CString(_T("燕")));
        CHECK(m.senderAvatarColor == kMemberColor);
        CHECK(m.senderAvatarImagePath == CString(_T("C:\\avatar\\5308.png")));
    }
}

TEST_CASE("G2 成员先到时直接得到真实信息，空字段按规则回退", "[groupsender]")
{
    SECTION("成员表里有完整信息")
    {
        const std::map<int64_t, GroupSenderRender> members = MakeMembers();
        const GroupSenderRender r =
            ResolveGroupSender(kMemberId, &members.find(kMemberId)->second, kFallbackColor);
        CHECK(r.m_name == CString(_T("韩晓燕888")));
        CHECK(r.m_glyph == CString(_T("燕")));
        CHECK(r.m_color == kMemberColor);
        CHECK(r.m_imagePath == CString(_T("C:\\avatar\\5308.png")));
    }

    SECTION("查不到：用户<id>、首字、备用底色、无真图")
    {
        const GroupSenderRender r = ResolveGroupSender(kLeftId, NULL, kFallbackColor);
        CHECK(r.m_name == CString(_T("用户9999")));
        CHECK(r.m_glyph == CString(_T("用")));
        CHECK(r.m_color == kFallbackColor);
        CHECK(r.m_imagePath.IsEmpty());
    }

    SECTION("查到了但名字、字形为空")
    {
        GroupSenderRender partial;
        partial.m_color = kMemberColor;
        const GroupSenderRender r = ResolveGroupSender(kMemberId, &partial, kFallbackColor);
        CHECK(r.m_name == CString(_T("用户5308")));
        CHECK(r.m_glyph == CString(_T("用")));
        //底色取成员表里的，不用备用底色
        CHECK(r.m_color == kMemberColor);
    }
}

TEST_CASE("G3 自己发的消息不受刷新影响", "[groupsender]")
{
    ChatMessage mine = MakeTextMessage(true, _T("我发的"));
    mine.senderUserId     = kSelfId;
    mine.senderAvatarText = _T("张");

    ChatMessageList list;
    list.AppendMessage(mine);
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);

    //成员表里即使有本人，也不去改本人气泡（本人气泡不显示发送者名）
    std::map<int64_t, GroupSenderRender> members = MakeMembers();
    members[kSelfId] = members[kMemberId];
    CHECK(ApplyGroupSendersToList(list, &LookupFromMap, &members) == 0);

    const ChatMessage& m = MessageBubbles(list)[0]->GetMessage();
    CHECK(m.senderName.IsEmpty());
    CHECK(m.senderAvatarText == CString(_T("张")));
}

TEST_CASE("G4 成员表里查不到的发送者保持备用信息", "[groupsender]")
{
    ChatMessageList list;
    list.AppendMessage(MakeFallbackMessage(kLeftId, _T("已退群的人发的")));
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);

    std::map<int64_t, GroupSenderRender> members = MakeMembers();
    CHECK(ApplyGroupSendersToList(list, &LookupFromMap, &members) == 0);

    const ChatMessage& m = MessageBubbles(list)[0]->GetMessage();
    CHECK(m.senderName == CString(_T("用户9999")));
    CHECK(m.senderAvatarText == CString(_T("用")));
    CHECK(m.senderAvatarColor == kFallbackColor);
}

TEST_CASE("G5 刷新只改显示信息，条数、顺序、滚动位置不变", "[groupsender]")
{
    ChatMessageList list;
    for (int i = 0; i < kMsgCount; ++i)
    {
        ChatMessage m = MakeFallbackMessage((i % 2 == 0) ? kMemberId : kLeftId,
            _T("一条足够长、会折成两行的群聊消息，用来撑出远大于视口的内容高度。"));
        m.msgid = 800 + i;
        list.AppendMessage(m);
    }
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);
    //停在中间某条消息上，而不是底部
    REQUIRE(list.ScrollToMsgid(810));
    const int scrollBefore = list.ScrollY();
    const size_t countBefore = list.Children().size();
    std::vector<long long> orderBefore;
    std::vector<ChatBubble*> before = MessageBubbles(list);
    for (size_t i = 0; i < before.size(); ++i)
    {
        orderBefore.push_back(before[i]->Msgid());
    }

    std::map<int64_t, GroupSenderRender> members = MakeMembers();
    CHECK(ApplyGroupSendersToList(list, &LookupFromMap, &members) == kMsgCount / 2);

    CHECK(list.Children().size() == countBefore);
    CHECK(list.ScrollY() == scrollBefore);
    std::vector<ChatBubble*> after = MessageBubbles(list);
    REQUIRE(after.size() == orderBefore.size());
    for (size_t i = 0; i < after.size(); ++i)
    {
        CHECK(after[i]->Msgid() == orderBefore[i]);
    }
}

// =====================================================================
// GSR1-GSR4：成员表查不到时再查通讯录（BUG-49，2026-10-02）
//
// 已经退群或被移出的人不在成员表里，他以前发的消息原先一律显示"用户<id>"。企业版里这些人
// 通常仍在通讯录里，故成员表查不到时再查主面板的通讯录缓存，两处都没有才回退。
// =====================================================================

namespace {

// 通讯录里那位已退群同事的底色。
const COLORREF kContactColor = RGB(0x10, 0xB9, 0x81);

// 通讯录：只有已退群的 kLeftId 一个人（kMemberId 也放一份不同的名字，用来确认成员表优先）。
std::map<int64_t, GroupSenderRender> MakeContacts()
{
    std::map<int64_t, GroupSenderRender> contacts;
    GroupSenderRender left;
    left.m_name  = _T("已退群的同事");
    left.m_glyph = _T("退");
    left.m_color = kContactColor;
    contacts[kLeftId] = left;

    GroupSenderRender member;
    member.m_name  = _T("通讯录里的名字");
    member.m_color = kContactColor;
    contacts[kMemberId] = member;
    return contacts;
}

// 成员表与通讯录两份数据，供组合查询回调使用。
struct SenderSources
{
    std::map<int64_t, GroupSenderRender> m_members;    // 本群成员表
    std::map<int64_t, GroupSenderRender> m_contacts;   // 通讯录缓存
};

// 组合查询回调：先成员表、再通讯录，即群聊窗传给 ApplyGroupSendersToList 的那种回调。
bool LookupChainFromSources(void* ctx, int64_t userId, GroupSenderRender& out)
{
    SenderSources* src = static_cast<SenderSources*>(ctx);
    return LookupGroupSenderChain(userId, &LookupFromMap, &src->m_members,
                                  &LookupFromMap, &src->m_contacts, out);
}

// 记录通讯录查询被调用的次数，用来确认成员表命中时不再查通讯录。
int g_contactLookupCalls = 0;

// 计数版通讯录查询回调。
bool CountingContactLookup(void* ctx, int64_t userId, GroupSenderRender& out)
{
    ++g_contactLookupCalls;
    return LookupFromMap(ctx, userId, out);
}

} // anonymous namespace

TEST_CASE("GSR1 成员表命中时以成员信息为准，不再查通讯录", "[groupsender][groupexit]")
{
    std::map<int64_t, GroupSenderRender> members = MakeMembers();
    std::map<int64_t, GroupSenderRender> contacts = MakeContacts();
    g_contactLookupCalls = 0;

    GroupSenderRender out;
    REQUIRE(LookupGroupSenderChain(kMemberId, &LookupFromMap, &members,
                                   &CountingContactLookup, &contacts, out));
    CHECK(out.m_name == CString(_T("韩晓燕888")));
    CHECK(out.m_color == kMemberColor);
    CHECK(g_contactLookupCalls == 0);
}

TEST_CASE("GSR2 成员表里没有、通讯录里有：用通讯录里的昵称与头像", "[groupsender][groupexit]")
{
    std::map<int64_t, GroupSenderRender> members = MakeMembers();
    std::map<int64_t, GroupSenderRender> contacts = MakeContacts();

    GroupSenderRender out;
    REQUIRE(LookupGroupSenderChain(kLeftId, &LookupFromMap, &members,
                                   &LookupFromMap, &contacts, out));
    const GroupSenderRender r = ResolveGroupSender(kLeftId, &out, kFallbackColor);
    CHECK(r.m_name == CString(_T("已退群的同事")));
    CHECK(r.m_glyph == CString(_T("退")));
    CHECK(r.m_color == kContactColor);

    //没有通讯录查询回调（调试入口没有宿主主面板）：只查成员表
    GroupSenderRender none;
    CHECK_FALSE(LookupGroupSenderChain(kLeftId, &LookupFromMap, &members, NULL, NULL, none));
}

TEST_CASE("GSR3 两处都查不到：解析结果回退为用户<id>", "[groupsender][groupexit]")
{
    std::map<int64_t, GroupSenderRender> members = MakeMembers();
    std::map<int64_t, GroupSenderRender> contacts = MakeContacts();
    const int64_t kStranger = 7777;

    GroupSenderRender out;
    CHECK_FALSE(LookupGroupSenderChain(kStranger, &LookupFromMap, &members,
                                       &LookupFromMap, &contacts, out));
    const GroupSenderRender r = ResolveGroupSender(kStranger, NULL, kFallbackColor);
    CHECK(r.m_name == CString(_T("用户7777")));
    CHECK(r.m_color == kFallbackColor);
}

TEST_CASE("GSR4 成员表到达后刷新：退群者的气泡换成通讯录里的名字", "[groupsender][groupexit]")
{
    //历史先到：退群者与成员的消息都按"用户<id>"画出来
    ChatMessageList list;
    list.AppendMessage(MakeFallbackMessage(kLeftId, _T("已退群的人发的")));
    list.AppendMessage(MakeFallbackMessage(kMemberId, _T("成员发的")));
    RECT rc = { 0, 0, kListW, kListH };
    list.ForceLayout(rc);

    //成员表到达，群聊窗用组合查询刷新
    SenderSources src;
    src.m_members = MakeMembers();
    src.m_contacts = MakeContacts();
    CHECK(ApplyGroupSendersToList(list, &LookupChainFromSources, &src) == 2);

    const std::vector<ChatBubble*> bubbles = MessageBubbles(list);
    REQUIRE(bubbles.size() == 2);
    CHECK(bubbles[0]->GetMessage().senderName == CString(_T("已退群的同事")));
    CHECK(bubbles[0]->GetMessage().senderAvatarColor == kContactColor);
    //成员仍以成员表为准，不被通讯录里的名字覆盖
    CHECK(bubbles[1]->GetMessage().senderName == CString(_T("韩晓燕888")));
}
