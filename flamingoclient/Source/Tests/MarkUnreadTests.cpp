/**
 *  会话「标为未读」（客户端侧）单元测试，MarkUnreadTests.cpp，标签 [markunread]
 *
 *  主面板会话列表右键可以把一个会话「标为未读」：画一个不带数字的小红点，打开会话或「标记已读」时
 *  清除；标记存服务端（t_conversation.f_unread_mark），经 1513 设置、1514 同步、1501 下发。
 *  方案见 docs/会话标为未读方案.md。本文件覆盖：
 *    MU-C1  协议：1501 / 1514 解析出 m_unreadMark，老服务端不带时为 false；1513 的构造
 *    MU-C2  会话右键菜单里「标为未读」与「标记已读」二选一
 *    MU-C4  清除决策：已标记的会话被打开或「标记已读」时要发 1513 清除，没标记的不发；消息盒子行的汇总
 *  行装饰（小红点的绘制）MU-C3 在 MainPanelRowMarkTests.cpp，与其它行标记的绘制用例放在一起。
 *
 *  典型用法：FlamingoUnit.exe "[markunread]"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"
#include "../net/ConversationsProtocolClient.h"
#include "../net/Protocol.h"   // conversation_single / conversation_group / notify_mode_*
#include "../UI/BalloonMain/ConversationListOps.h"         // SetConversationUnreadMark / IsConversationUnreadMarked
#include "../UI/BalloonMain/MainPanelConversationMenu.h"   // BuildConversationMenuModel
#include "../UI/BalloonMain/MsgBoxModel.h"                 // ComputeMsgBoxSummary
#include "../UI/BalloonMain/NotifyModeModel.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

#include <string>
#include <vector>

namespace {

//本组用到的会话：一个单聊对端、一个群、一个不在列表里的联系人
const int64_t kMuPeer   = 100021;
const int64_t kMuGroup  = 268435611LL;
const int64_t kMuAbsent = 100099;

//有未读时的未读数
const int kMuUnread = 2;

//把 JSON 文本解析成 Json::Value，失败返回 false
bool ParseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//造一条会话
net::ConversationItem MakeConv(int ctype, int64_t cid, int unread, bool marked)
{
    net::ConversationItem c;
    c.m_ctype      = ctype;
    c.m_cid        = cid;
    c.m_unread     = unread;
    c.m_unreadMark = marked;
    return c;
}

//在菜单行模型里找某个 id；找不到返回 -1
int IndexOf(const std::vector<balloonmain::ConversationMenuEntry>& items, UINT id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].m_isSeparator && items[i].m_id == id)
            return (int)i;
    }
    return -1;
}

} // anonymous namespace

TEST_CASE("MU-C1a 1501 解析出 unreadmark，0 / 1 与布尔值都认，老服务端不带时为 false", "[markunread]")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
        "{\"ctype\":1,\"cid\":\"100021\",\"unread\":0,\"pinned\":false,\"notifymode\":0,\"unreadmark\":1,\"lastmsgid\":\"5\"},"
        "{\"ctype\":2,\"cid\":\"268435611\",\"unread\":0,\"pinned\":false,\"notifymode\":0,\"unreadmark\":true,\"lastmsgid\":\"6\"},"
        "{\"ctype\":1,\"cid\":\"100022\",\"unread\":0,\"pinned\":false,\"notifymode\":0,\"unreadmark\":0,\"lastmsgid\":\"7\"},"
        "{\"ctype\":1,\"cid\":\"100023\",\"unread\":0,\"pinned\":false,\"notifymode\":0,\"lastmsgid\":\"8\"}"
        "],\"notifymodes\":[]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 4);
    CHECK(out.m_items[0].m_unreadMark);
    CHECK(out.m_items[1].m_unreadMark);
    CHECK_FALSE(out.m_items[2].m_unreadMark);
    CHECK_FALSE(out.m_items[3].m_unreadMark);
}

TEST_CASE("MU-C1b 1514 解析出 unreadmark；1513 只在设置时带这个字段，且写成布尔值", "[markunread]")
{
    net::ConversationChangedNotify n;
    REQUIRE(net::ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"ctype\":2,\"cid\":\"268435611\",\"pinned\":false,\"notifymode\":0,\"deleted\":false,\"unreadmark\":true}", n));
    CHECK(n.m_unreadMark);
    net::ConversationChangedNotify old;
    REQUIRE(net::ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"ctype\":2,\"cid\":\"268435611\",\"pinned\":true,\"notifymode\":0,\"deleted\":false}", old));
    CHECK_FALSE(old.m_unreadMark);

    //标为未读 / 清除：只带 ctype、cid、unreadmark 三个字段
    Json::Value mark;
    REQUIRE(ParseJson(net::ConversationsProtocolClient::buildSetUnreadMarkRequest(conversation_group, kMuGroup, true), mark));
    CHECK(mark["unreadmark"].isBool());
    CHECK(mark["unreadmark"].asBool());
    CHECK(mark["cid"].asString() == std::to_string(kMuGroup));
    CHECK(mark.size() == 3);
    Json::Value clear;
    REQUIRE(ParseJson(net::ConversationsProtocolClient::buildSetUnreadMarkRequest(conversation_single, kMuPeer, false), clear));
    CHECK(clear["unreadmark"].isBool());
    CHECK_FALSE(clear["unreadmark"].asBool());

    //置顶请求不带 unreadmark，服务端就不会动这一列
    Json::Value pin;
    REQUIRE(ParseJson(net::ConversationsProtocolClient::buildSetConversationRequest(conversation_single, kMuPeer,
                                                                                    true, true, false, 0), pin));
    CHECK_FALSE(pin.isMember("unreadmark"));
}

TEST_CASE("MU-C2 会话菜单：「标为未读」与「标记已读」二选一，都可点", "[markunread]")
{
    //没有未读、没标记：「标为未读」
    balloonmain::ConversationMenuState plain;
    plain.m_unread = 0;
    std::vector<balloonmain::ConversationMenuEntry> items = balloonmain::BuildConversationMenuModel(plain);
    REQUIRE(IndexOf(items, balloonmain::kConvMenuMarkUnread) == 1);
    CHECK(IndexOf(items, balloonmain::kConvMenuMarkRead) < 0);
    CHECK(items[1].m_enabled);
    CHECK(std::wstring(items[1].m_label) == L"标为未读");

    //已标记：「标记已读」
    balloonmain::ConversationMenuState marked;
    marked.m_unreadMarked = true;
    items = balloonmain::BuildConversationMenuModel(marked);
    REQUIRE(IndexOf(items, balloonmain::kConvMenuMarkRead) == 1);
    CHECK(IndexOf(items, balloonmain::kConvMenuMarkUnread) < 0);
    CHECK(items[1].m_enabled);

    //有未读：「标记已读」
    balloonmain::ConversationMenuState unread;
    unread.m_unread = kMuUnread;
    items = balloonmain::BuildConversationMenuModel(unread);
    REQUIRE(IndexOf(items, balloonmain::kConvMenuMarkRead) == 1);
    CHECK(IndexOf(items, balloonmain::kConvMenuMarkUnread) < 0);

    //两者的行数相同：菜单形态不随状态跳动
    CHECK(balloonmain::BuildConversationMenuModel(plain).size() == balloonmain::BuildConversationMenuModel(marked).size());
}

TEST_CASE("MU-C4a 清除决策：已标记的会话清除时返回 true（要发 1513），没标记或不在列表里返回 false", "[markunread]")
{
    std::vector<net::ConversationItem> convs;
    convs.push_back(MakeConv(conversation_single, kMuPeer, 0, true));
    convs.push_back(MakeConv(conversation_group, kMuGroup, kMuUnread, false));

    //已标记：清除时返回 true；已读位点有没有前进与此无关
    CHECK(balloonmain::IsConversationUnreadMarked(convs, conversation_single, kMuPeer));
    CHECK(balloonmain::SetConversationUnreadMark(convs, conversation_single, kMuPeer, false));
    CHECK_FALSE(balloonmain::IsConversationUnreadMarked(convs, conversation_single, kMuPeer));
    //再清一次：没有变化，不重复发
    CHECK_FALSE(balloonmain::SetConversationUnreadMark(convs, conversation_single, kMuPeer, false));

    //没标记的会话：不发
    CHECK_FALSE(balloonmain::SetConversationUnreadMark(convs, conversation_group, kMuGroup, false));
    //不在列表里的会话：不发、不凭空建行
    CHECK_FALSE(balloonmain::SetConversationUnreadMark(convs, conversation_single, kMuAbsent, true));
    CHECK(convs.size() == 2);

    //标为未读：只改这一个标志，不动未读数
    CHECK(balloonmain::SetConversationUnreadMark(convs, conversation_group, kMuGroup, true));
    CHECK(convs[1].m_unread == kMuUnread);
    CHECK_FALSE(balloonmain::SetConversationUnreadMark(convs, conversation_group, kMuGroup, true));
}

TEST_CASE("MU-C4b 消息盒子行：盒内有被标为未读的会话时汇总带上标记", "[markunread]")
{
    std::vector<net::ConversationItem> convs;
    convs.push_back(MakeConv(conversation_group, kMuGroup, 0, false));
    convs.push_back(MakeConv(conversation_single, kMuPeer, 0, true));
    balloonmain::NotifyModeModel modes;
    modes.SetMode(conversation_group, kMuGroup, notify_mode_msgbox);

    //标着的会话不在盒子里：盒子行不带标记
    CHECK_FALSE(balloonmain::ComputeMsgBoxSummary(convs, modes).m_unreadMarked);

    //盒子里的会话被标为未读：带上标记
    convs[0].m_unreadMark = true;
    const balloonmain::MsgBoxSummary sum = balloonmain::ComputeMsgBoxSummary(convs, modes);
    CHECK(sum.m_count == 1);
    CHECK(sum.m_unreadMarked);
}
