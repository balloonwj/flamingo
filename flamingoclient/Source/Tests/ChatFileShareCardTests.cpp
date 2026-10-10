/**
 *  文件共享卡片单元测试（Suite CFS，标签 [sharecard]），ChatFileShareCardTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 16 步）：
 *    - CFS1 解析卡片正文（与服务端共用样例 kWireCardStored / kWireCardAugmented 逐字一致）：字段、合并的状态与权限；
 *           脏数据（缺 shareid、kind 非法）解析失败；会话摘要「[共享] 名称」；
 *    - CFS2 套用到气泡：别人的文件夹共享是「打开」、单个文件是「下载」，本人的卡片写「我的共享」、按钮是「在本机打开」
 *           （2026-10-03 起，此前本人的卡片没有按钮），已失效时不可点；
 *           text 为「[共享] 名称」；内容不可用时仍是一张没有按钮的卡片；
 *    - CFS3 状态的取法：DLL 的最新状态优先，其次是 content 里合并的状态，都没有时为「正在获取状态」；
 *           模块没有加载时状态写「当前客户端未启用共享功能」、按钮不可点；已失效时按钮不可点；
 *    - CFS4 状态变化时重填：暂不可用与已失效的原因文字，非共享卡片不受影响；
 *    - CFS5 右键菜单：共享卡片没有转发、合并转发、Pin、撤回，保留复制、引用回复、删除、多选；kind 镜像一致；
 *    - CFS6 本地库与消息记录窗：content 列存 share 子对象，预览「[共享] 名称」，搜索文本是共享名，行菜单没有转发与 Pin；
 *    - CFS7 分享者的真实路径只在本人的卡片上显示（2026-10-04 起）：别人的卡片即使正文带着路径（旧卡片）也不显示；
 *           本人的卡片显示服务端补上的路径，没有路径时不显示；
 *    - CFSSNAP 效果图（标签 [.snapshot]，默认不运行）：几种状态的卡片画进一个消息列表，存到 snapshots/fileshare_cards.png。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatFileShareCard.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"   // CFSSNAP：把几张卡片排进一个列表里画
#include "../UI/BalloonHistory/HistoryView.h"       // CFS6：消息记录窗的预览文字
#include "../UI/BalloonHistory/HistoryRowMenu.h"    // CFS6：消息记录窗的行菜单
#include "../UI/BalloonHistory/V2MsgQuery.h"        // CFS6：RecordFromNotify
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../../../flamingoserver/tests/fileshare_wire_samples.h"

using namespace balloonchatwnd;

namespace {

//共用样例里的数值
const long long kShareId = 42;
const long long kOwnerId = 10001;
const int kStatePerms = 5;

//一份单个文件共享的正文
const char* const kFileCard =
    "{\"displaypath\":\"D:\\\\docs\\\\plan.docx\",\"kind\":3,\"name\":\"plan.docx\",\"ownerid\":\"10001\",\"shareid\":\"43\"}";

//可用、模块已加载、没有 DLL 状态
ShareCardLiveState Live(bool moduleReady, bool hasState, int state, int reason)
{
    ShareCardLiveState live;
    live.m_moduleReady = moduleReady;
    live.m_hasState = hasState;
    live.m_state = state;
    live.m_reason = reason;
    return live;
}

ChatMessage Bubble(bool fromMe)
{
    ChatMessage msg;
    msg.kind = kMsgText;
    msg.fromMe = fromMe;
    msg.msgid = 900;
    return msg;
}

bool HasMenuId(const std::vector<ChatMessageMenuEntry>& model, int id)
{
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (!model[i].isSeparator && model[i].id == id)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("CFS1 parse share card content", "[sharecard]")
{
    net::ShareCardContent card;
    REQUIRE(net::MessageProtocolClient::parseShareCardContent(kWireCardStored, card));
    CHECK(card.m_shareId == kShareId);
    CHECK(card.m_ownerId == kOwnerId);
    CHECK(card.m_kind == share_kind_folder);
    CHECK(card.m_name == "Projects");
    //2026-10-04 起入库的卡片不带分享者的真实路径；旧卡片与发给分享者本人的卡片带
    CHECK(card.m_displayPath.empty());
    CHECK_FALSE(card.m_hasState);
    CHECK_FALSE(card.m_hasPerms);
    net::ShareCardContent legacy;
    REQUIRE(net::MessageProtocolClient::parseShareCardContent(kWireCardStoredLegacy, legacy));
    CHECK(legacy.m_displayPath == "D:\\Projects");
    net::ShareCardContent owner;
    REQUIRE(net::MessageProtocolClient::parseShareCardContent(kWireCardAugmentedOwner, owner));
    CHECK(owner.m_displayPath == "D:\\Projects");
    CHECK_FALSE(owner.m_hasPerms);

    REQUIRE(net::MessageProtocolClient::parseShareCardContent(kWireCardAugmented, card));
    CHECK(card.m_hasState);
    CHECK(card.m_state == share_state_available);
    CHECK(card.m_hasPerms);
    CHECK(card.m_myPerms == kStatePerms);

    CHECK_FALSE(net::MessageProtocolClient::parseShareCardContent("{\"kind\":2,\"name\":\"x\",\"ownerid\":\"1\"}", card));
    CHECK_FALSE(net::MessageProtocolClient::parseShareCardContent(
        "{\"kind\":9,\"name\":\"x\",\"ownerid\":\"1\",\"shareid\":\"2\"}", card));
    CHECK_FALSE(net::MessageProtocolClient::parseShareCardContent("not json", card));

    //会话摘要：「[共享] 名称」，与服务端 buildMessageDigest 一致
    net::MessageNotify notify;
    notify.m_msgtype = message_type_fileshare;
    notify.m_shareContent = kWireCardStored;
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[共享] Projects");
    notify.m_shareContent.clear();
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == u8"[共享]");

    //完整消息的解析：content 原文保存在 m_shareContent
    const std::string raw = std::string("{\"msgid\":\"77\",\"from\":\"10001\",\"to\":\"10002\",\"msgtype\":20,\"content\":")
        + kWireCardAugmented + ",\"timestamp\":1759370000000}";
    net::MessageNotify parsed;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(raw, parsed));
    CHECK(parsed.m_msgtype == message_type_fileshare);
    REQUIRE(net::MessageProtocolClient::parseShareCardContent(parsed.m_shareContent, card));
    CHECK(card.m_shareId == kShareId);
}

TEST_CASE("CFS2 apply card to bubble", "[sharecard]")
{
    ChatMessage other = Bubble(false);
    ApplyFileShareCard(other, kWireCardStored, L"张三", Live(true, false, 0, 0));
    CHECK(other.kind == kMsgFileShare);
    CHECK(other.shareId == kShareId);
    CHECK(other.shareKind == share_kind_folder);
    CHECK(other.shareName == L"Projects");
    CHECK(other.sharePath.IsEmpty());
    CHECK(other.shareOwnerLine == L"张三的共享");
    CHECK(other.shareButton == kShareCardButtonOpen);
    CHECK(other.text == L"[共享] Projects");
    CHECK(other.shareContent == kWireCardStored);
    CHECK(std::wstring(ShareCardButtonLabel(other.shareButton)) == L"打开");

    ChatMessage file = Bubble(false);
    ApplyFileShareCard(file, kFileCard, L"李四", Live(true, false, 0, 0));
    CHECK(file.shareButton == kShareCardButtonDownload);
    CHECK(std::wstring(ShareCardButtonLabel(file.shareButton)) == L"下载");

    //本人的卡片：「在本机打开」，状态还不知道时也可以点（打不开时 DLL 会说明原因）
    ChatMessage mine = Bubble(true);
    ApplyFileShareCard(mine, kWireCardStored, L"不会用到", Live(true, false, 0, 0));
    CHECK(mine.shareOwnerLine == L"我的共享");
    CHECK(mine.shareButton == kShareCardButtonOpenLocal);
    CHECK(std::wstring(ShareCardButtonLabel(mine.shareButton)) == L"在本机打开");
    CHECK(mine.shareButtonEnabled);
    //本人的单个文件共享同样是「在本机打开」
    ChatMessage mineFile = Bubble(true);
    ApplyFileShareCard(mineFile, kFileCard, L"不会用到", Live(true, true, share_state_available, 0));
    CHECK(mineFile.shareButton == kShareCardButtonOpenLocal);
    CHECK(mineFile.shareButtonEnabled);
    //本人的共享已取消：不可点
    ChatMessage mineGone = Bubble(true);
    ApplyFileShareCard(mineGone, kWireCardStored, L"不会用到",
                       Live(true, true, share_state_invalid, share_invalid_owner_canceled));
    CHECK(mineGone.shareButton == kShareCardButtonOpenLocal);
    CHECK_FALSE(mineGone.shareButtonEnabled);

    ChatMessage broken = Bubble(false);
    ApplyFileShareCard(broken, "{}", L"王五", Live(true, false, 0, 0));
    CHECK(broken.kind == kMsgFileShare);
    CHECK(broken.shareId == 0);
    CHECK(broken.shareName == L"共享");
    CHECK(broken.shareButton == kShareCardButtonNone);
    CHECK(broken.text == L"[共享]");
}

TEST_CASE("CFS3 state source priority", "[sharecard]")
{
    //content 里合并了「可用」；DLL 说暂不可用（分享者不在线）：以 DLL 为准
    ChatMessage a = Bubble(false);
    ApplyFileShareCard(a, kWireCardAugmented, L"张三",
                       Live(true, true, share_state_unavailable, share_unavailable_owner_offline));
    CHECK(a.shareState == share_state_unavailable);
    CHECK(a.shareStatus == L"暂不可用：分享者不在线");
    CHECK(a.shareButtonEnabled);

    //DLL 没有记录：用 content 里合并的
    ChatMessage b = Bubble(false);
    ApplyFileShareCard(b, kWireCardAugmented, L"张三", Live(true, false, 0, 0));
    CHECK(b.shareState == share_state_available);
    CHECK(b.shareStatus == L"可用");

    //两边都没有
    ChatMessage c = Bubble(false);
    ApplyFileShareCard(c, kWireCardStored, L"张三", Live(true, false, 0, 0));
    CHECK(c.shareState == 0);
    CHECK(c.shareStatus == L"正在获取状态");
    CHECK(c.shareButtonEnabled);

    //模块没有加载：按钮不可点
    ChatMessage d = Bubble(false);
    ApplyFileShareCard(d, kWireCardAugmented, L"张三", Live(false, false, 0, 0));
    CHECK(d.shareStatus == L"当前客户端未启用共享功能");
    CHECK_FALSE(d.shareButtonEnabled);

    //已失效：按钮不可点
    ChatMessage e = Bubble(false);
    ApplyFileShareCard(e, kWireCardStored, L"张三", Live(true, true, share_state_invalid, share_invalid_owner_canceled));
    CHECK(e.shareStatus == L"已失效：分享者已取消共享");
    CHECK_FALSE(e.shareButtonEnabled);
}

TEST_CASE("CFS4 state refresh", "[sharecard]")
{
    ChatMessage card = Bubble(false);
    ApplyFileShareCard(card, kWireCardStored, L"张三", Live(true, true, share_state_available, 0));
    CHECK(card.shareButtonEnabled);
    ApplyFileShareState(card, true, share_state_invalid, share_invalid_member_removed);
    CHECK(card.shareStatus == L"已失效：你已不是这个共享的成员");
    CHECK_FALSE(card.shareButtonEnabled);
    ApplyFileShareState(card, true, share_state_unavailable, share_unavailable_root_missing);
    CHECK(card.shareStatus == L"暂不可用：共享的内容在分享者电脑上已不存在");
    CHECK(card.shareButtonEnabled);
    ApplyFileShareState(card, true, share_state_unavailable, 0);
    CHECK(card.shareStatus == L"暂不可用");
    ApplyFileShareState(card, true, share_state_invalid, 0);
    CHECK(card.shareStatus == L"已失效");
    //本企业的文件共享已被关闭：显示原因、按钮不可点（第 3 期第 1 步）
    ApplyFileShareState(card, true, share_state_unavailable, share_unavailable_feature_disabled);
    CHECK(card.shareStatus == L"暂不可用：文件共享功能已被管理员关闭");
    CHECK_FALSE(card.shareButtonEnabled);
    ApplyFileShareState(card, true, share_state_available, 0);
    CHECK(card.shareButtonEnabled);

    //不是共享卡片：不受影响
    ChatMessage text = Bubble(false);
    text.text = L"hello";
    ApplyFileShareState(text, true, share_state_invalid, 0);
    CHECK(text.kind == kMsgText);
    CHECK(text.shareStatus.IsEmpty());

    CHECK(ShareCardStatusText(true, true, share_state_invalid, share_invalid_group_dismissed) == L"已失效：群已解散");
}

TEST_CASE("CFS5 message menu for share cards", "[sharecard]")
{
    CHECK(kChatMenuKindFileShare == (int)kMsgFileShare);
    CHECK_FALSE(CanForwardMessage(kChatMenuKindFileShare, 900, 0));
    CHECK_FALSE(CanMergeForwardMessage(kChatMenuKindFileShare, 900, 0));
    //调用方即使按普通消息传了可撤回、可 Pin、可转发，共享卡片也不给这几项
    const std::vector<ChatMessageMenuEntry> model = BuildChatMessageMenuModel(
        kChatMenuKindFileShare, kChatImageStateReady, false, true, true, false, true, false, false);
    CHECK(HasMenuId(model, kChatMsgMenuCopy));
    CHECK(HasMenuId(model, kChatMsgMenuReply));
    CHECK(HasMenuId(model, kChatMsgMenuDelete));
    CHECK(HasMenuId(model, kChatMsgMenuMultiSelect));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuForward));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuPin));
    CHECK_FALSE(HasMenuId(model, kChatMsgMenuRecall));
}

TEST_CASE("CFS6 local store and history window", "[sharecard]")
{
    net::MessageNotify notify;
    notify.m_msgid = 901;
    notify.m_from = kOwnerId;
    notify.m_msgtype = message_type_fileshare;
    notify.m_shareContent = kWireCardStored;
    const balloonhistory::HistoryRecord rec = balloonhistory::RecordFromNotify(notify, conversation_group, 900001);
    CHECK(rec.m_searchText == "Projects");
    CHECK(balloonhistory::PreviewFromContent(message_type_fileshare, rec.m_content) == "[共享] Projects");
    CHECK(balloonhistory::PreviewFromContent(message_type_fileshare, "{}") == "[共享]");

    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_fileshare, 901));
    balloonhistory::HistoryRowMenuContext ctx;
    ctx.m_msgType = message_type_fileshare;
    ctx.m_msgId = 901;
    const std::vector<balloonhistory::HistoryRowMenuItem> model = balloonhistory::BuildHistoryRowMenuModel(ctx);
    for (size_t i = 0; i < model.size(); ++i)
    {
        CHECK(model[i].m_id != balloonhistory::kHistoryRowMenuForward);
        CHECK(model[i].m_id != balloonhistory::kHistoryRowMenuPin);
    }
}

namespace {

//效果图的尺寸（像素）：宽度与聊天窗消息区相近，高度放得下全部卡片
const int kShareSnapWidth = 620;
const int kShareSnapHeight = 760;

//效果图位置（测试程序目录 Debug_Win32/Tests 往上三级是仓库根）
const wchar_t* const kShareSnapPath = L"\\..\\..\\..\\snapshots\\fileshare_cards.png";

//一份磁盘共享的正文
const char* const kDiskCard =
    "{\"displaypath\":\"E:\\\\\",\"kind\":1,\"name\":\"资料盘 (E:)\",\"ownerid\":\"10001\",\"shareid\":\"44\"}";

//在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID
bool FindSharePngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)&buf[0];
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//别人发出的卡片：带发送人名与头像字
ChatMessage SnapOthersShare(const char* content, long long msgid, const ShareCardLiveState& live)
{
    ChatMessage msg = Bubble(false);
    msg.msgid = msgid;
    msg.senderName = L"张三";
    msg.senderAvatarText = L"张";
    msg.senderAvatarColor = RGB(0x4F, 0x8E, 0xF7);
    msg.senderUserId = kOwnerId;
    ApplyFileShareCard(msg, content, L"张三", live);
    return msg;
}

} // namespace

TEST_CASE("CFS7 real path only on the sharer's own card", "[sharecard]")
{
    //别人的卡片：即使正文带着路径（服务端升级前缓存在本地库里的旧卡片）也不显示，名称照常
    ChatMessage other = Bubble(false);
    ApplyFileShareCard(other, kWireCardStoredLegacy, L"张三", Live(true, false, 0, 0));
    CHECK(other.sharePath.IsEmpty());
    CHECK(other.shareName == L"Projects");

    //本人的卡片：服务端只给本人补上的真实路径照常显示
    ChatMessage mine = Bubble(true);
    ApplyFileShareCard(mine, kWireCardAugmentedOwner, L"不会用到", Live(true, false, 0, 0));
    CHECK(mine.sharePath == L"D:\\Projects");

    //本人的卡片还没经服务端合并（正文里没有路径）：不显示路径这一行
    ChatMessage mineFresh = Bubble(true);
    ApplyFileShareCard(mineFresh, kWireCardStored, L"不会用到", Live(true, false, 0, 0));
    CHECK(mineFresh.sharePath.IsEmpty());
}

TEST_CASE("CFSSNAP share card snapshot", "[.snapshot][sharecard]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        //1. 别人共享的文件夹，可用
        list.AppendMessage(SnapOthersShare(kWireCardAugmented, 1001, Live(true, false, 0, 0)));
        //2. 别人共享的单个文件，分享者不在线
        list.AppendMessage(SnapOthersShare(kFileCard, 1002,
                                           Live(true, true, share_state_unavailable, share_unavailable_owner_offline)));
        //3. 别人共享的磁盘，已被取消
        list.AppendMessage(SnapOthersShare(kDiskCard, 1003, Live(true, true, share_state_invalid, share_invalid_owner_canceled)));
        //4. 本人共享的文件夹
        ChatMessage mine = Bubble(true);
        mine.msgid = 1004;
        ApplyFileShareCard(mine, kWireCardAugmented, L"", Live(true, false, 0, 0));
        list.AppendMessage(mine);
        //5. 本客户端没有加载文件共享模块
        list.AppendMessage(SnapOthersShare(kWireCardStored, 1005, Live(false, false, 0, 0)));

        RECT all = { 0, 0, kShareSnapWidth, kShareSnapHeight };
        list.ForceLayout(all);

        HDC screenDC = ::GetDC(NULL);
        REQUIRE(screenDC != NULL);
        HDC memDC = ::CreateCompatibleDC(screenDC);
        HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kShareSnapWidth, kShareSnapHeight);
        ::ReleaseDC(NULL, screenDC);
        REQUIRE(memDC != NULL);
        REQUIRE(bmp != NULL);
        HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
        HBRUSH bg = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        ::FillRect(memDC, &all, bg);
        ::DeleteObject(bg);
        list.OnPaint(memDC, all);
        ::GdiFlush();
        ::SelectObject(memDC, oldBmp);

        wchar_t exePath[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring dir(exePath);
        dir = dir.substr(0, dir.find_last_of(L'\\'));
        CLSID png;
        REQUIRE(FindSharePngEncoder(png));
        {
            Gdiplus::Bitmap image(bmp, NULL);
            CHECK(image.Save((dir + kShareSnapPath).c_str(), &png, NULL) == Gdiplus::Ok);
        }
        ::DeleteObject(bmp);
        ::DeleteDC(memDC);
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}
