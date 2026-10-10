/**
 *  屏幕共享分享链接文字的单元测试（标签 [sharelink]），ShareLinkInviteTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareLinkInvite（docs/屏幕共享与录制方案.md 第 12.4 节，第 25 期）：
 *    - SLI1 链接码格式：长度 10、只用 56 个字母数字；不合法的码拼不出链接；
 *    - SLI2 拼链接与取链接码互为逆运算；前缀不区分大小写；前缀之后多一个字符、少一个字符、含非法字符都不认；
 *           别的协议、只有前缀的都不认；
 *    - SLI3 邀请文字：有标题、没标题；有密码时第二行写密码，没有时只有一行；
 *    - SLI4 凭链接加入的确认正文：写发起人、标题与描述；需要密码时说明；强制模式时接上强制模式的说明；没有发起人名时写「对方」；
 *    - SLI5 聊天里认出分享链接（Source/UI/BalloonChatWnd/ChatLinkDetect）：与 DLL 拼出的链接一致；链接码不合规的不认；
 *           尾随标点剥掉；网址开关关掉时仍认分享链接，分享链接开关关掉时不认；IsScreenShareLinkUrl 与 ParseShareLinkUrl 结论一致；
 *    - SLI6 共享协议（ShareProtocolClient）：1752 / 1753 请求的组装、应答的解析，1722 带链接码，1725 快照里的链接访客标记；
 *    - SLI7 发起端能否用 P2P（ShareHostP2PAllowed）：只有发起人加一名观看者、且没有云端录制时可以；
 *    - SLI8 转发目标选择窗的「发送一段文字」模式：标题、内容标题与预览，可以多选。
 *  界面文字按没有语言包时的简体中文原文比较。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../../FlamingoMedia/Share/ShareLinkInvite.h"
#include "../../FlamingoMedia/Share/ShareP2PRoute.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../UI/BalloonChatWnd/ChatLinkDetect.h"
#include "../UI/BalloonMain/ForwardRequest.h"
#include "../UI/BalloonMain/ForwardTargetModel.h"

using namespace screenshare;

TEST_CASE("SLI1 share link code format", "[sharelink]")
{
    CHECK(IsValidShareLinkCode(L"Ab3dE7kQ9z"));
    CHECK(IsValidShareLinkCode(L"2222222222"));
    CHECK_FALSE(IsValidShareLinkCode(L""));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ9"));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ9zz"));
    //易混字符 0 O o 1 l I 不在字符表里
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ90"));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ9O"));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ9o"));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ91"));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ9l"));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ9I"));
    CHECK_FALSE(IsValidShareLinkCode(L"Ab3dE7kQ9/"));
    CHECK_FALSE(IsValidShareLinkCode(std::wstring(L"Ab3dE7kQ9\0", 10)));
    CHECK(MakeShareLinkUrl("Ab3dE7kQ90").empty());
    CHECK(MakeShareLinkUrl("").empty());
}

TEST_CASE("SLI2 share link url round trip", "[sharelink]")
{
    const std::wstring url = MakeShareLinkUrl("Ab3dE7kQ9z");
    CHECK(url == L"flamingo://screenshare/Ab3dE7kQ9z");
    std::string code;
    REQUIRE(ParseShareLinkUrl(url, code));
    CHECK(code == "Ab3dE7kQ9z");
    //前缀不区分大小写，链接码区分大小写原样取出
    std::string upper;
    REQUIRE(ParseShareLinkUrl(L"FLAMINGO://ScreenShare/Ab3dE7kQ9z", upper));
    CHECK(upper == "Ab3dE7kQ9z");

    std::string untouched = "keep";
    CHECK_FALSE(ParseShareLinkUrl(L"flamingo://screenshare/Ab3dE7kQ9zz", untouched));
    CHECK_FALSE(ParseShareLinkUrl(L"flamingo://screenshare/Ab3dE7kQ9", untouched));
    CHECK_FALSE(ParseShareLinkUrl(L"flamingo://screenshare/Ab3dE7kQ9z/", untouched));
    CHECK_FALSE(ParseShareLinkUrl(L"flamingo://screenshare/Ab3dE7kQ9z?x=1", untouched));
    CHECK_FALSE(ParseShareLinkUrl(L"flamingo://screenshare/", untouched));
    CHECK_FALSE(ParseShareLinkUrl(L"flamingo://filshare/Ab3dE7kQ9z", untouched));
    CHECK_FALSE(ParseShareLinkUrl(L"https://screenshare/Ab3dE7kQ9z", untouched));
    CHECK_FALSE(ParseShareLinkUrl(L" flamingo://screenshare/Ab3dE7kQ9z", untouched));
    CHECK(untouched == "keep");
}

TEST_CASE("SLI3 share invite text", "[sharelink]")
{
    const std::wstring url = L"flamingo://screenshare/Ab3dE7kQ9z";
    CHECK(MakeShareInviteText(L"周会", url, L"") == L"邀请你观看屏幕共享「周会」：flamingo://screenshare/Ab3dE7kQ9z");
    CHECK(MakeShareInviteText(L"", url, L"") == L"邀请你观看屏幕共享：flamingo://screenshare/Ab3dE7kQ9z");
    CHECK(MakeShareInviteText(L"周会", url, L"123456") ==
          L"邀请你观看屏幕共享「周会」：flamingo://screenshare/Ab3dE7kQ9z\n密码：123456");
}

TEST_CASE("SLI4 link join confirm text", "[sharelink]")
{
    const std::wstring plain = MakeShareLinkJoinConfirmText(L"张三", L"周会", L"本周进度", false, false);
    CHECK(plain == L"加入 张三 的屏幕共享？\n\n「周会」\n本周进度");
    const std::wstring locked = MakeShareLinkJoinConfirmText(L"张三", L"周会", L"", true, false);
    CHECK(locked == L"加入 张三 的屏幕共享？\n\n「周会」\n\n这场共享设了密码，加入时需要输入密码。");
    const std::wstring forced = MakeShareLinkJoinConfirmText(L"", L"", L"", false, true);
    CHECK(forced.find(L"加入 对方 的屏幕共享？") == 0);
    CHECK(forced.find(L"强制观看") != std::wstring::npos);
    //没有标题时描述也不写
    const std::wstring noTitle = MakeShareLinkJoinConfirmText(L"张三", L"", L"本周进度", false, false);
    CHECK(noTitle == L"加入 张三 的屏幕共享？\n");
}

TEST_CASE("SLI5 chat recognises share links", "[sharelink]")
{
    const std::wstring url = MakeShareLinkUrl("Ab3dE7kQ9z");
    //聊天里认出的链接与 DLL 拼出的链接、取出的链接码一致
    const std::wstring text = L"邀请你观看屏幕共享「周会」：" + url + L"。";
    const std::vector<balloonchatwnd::LinkSpan> spans = balloonchatwnd::DetectLinks(text, true, true);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].m_scheme == balloonchatwnd::kLinkSchemeScreenShare);
    CHECK(spans[0].m_url == url);
    CHECK(text.substr(spans[0].m_off, spans[0].m_len) == url);
    CHECK(balloonchatwnd::IsScreenShareLinkUrl(spans[0].m_url));
    std::string code;
    CHECK(ParseShareLinkUrl(spans[0].m_url, code));
    CHECK(code == "Ab3dE7kQ9z");
    //网址开关关掉时分享链接照样认，网址不认；分享链接开关关掉时不认分享链接
    const std::wstring mixed = L"see https://a.com and " + url;
    const std::vector<balloonchatwnd::LinkSpan> onlyShare = balloonchatwnd::DetectLinks(mixed, false, true);
    REQUIRE(onlyShare.size() == 1);
    CHECK(onlyShare[0].m_scheme == balloonchatwnd::kLinkSchemeScreenShare);
    const std::vector<balloonchatwnd::LinkSpan> onlyWeb = balloonchatwnd::DetectLinks(mixed, true, false);
    REQUIRE(onlyWeb.size() == 1);
    CHECK(onlyWeb[0].m_scheme == balloonchatwnd::kLinkSchemeHttps);
    //默认两种都认
    CHECK(balloonchatwnd::DetectLinks(mixed).size() == 2);
    //链接码不合规的不认：多一位、少一位、夹了易混字符
    CHECK(balloonchatwnd::DetectLinks(L"flamingo://screenshare/Ab3dE7kQ9zz", true, true).empty());
    CHECK(balloonchatwnd::DetectLinks(L"flamingo://screenshare/Ab3dE7kQ9", true, true).empty());
    CHECK(balloonchatwnd::DetectLinks(L"flamingo://screenshare/Ab3dE7kQ90", true, true).empty());
    //前缀大小写不敏感
    CHECK(balloonchatwnd::IsScreenShareLinkUrl(L"FLAMINGO://ScreenShare/Ab3dE7kQ9z"));
    //IsScreenShareLinkUrl 与 ParseShareLinkUrl 结论一致
    const wchar_t* samples[] = { L"flamingo://screenshare/Ab3dE7kQ9z", L"flamingo://screenshare/Ab3dE7kQ9",
                                 L"flamingo://screenshare/Ab3dE7kQ9z/", L"https://screenshare/Ab3dE7kQ9z",
                                 L"flamingo://screenshare/AAAAAAAAAI" };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        INFO("sample " << i);
        std::string c;
        CHECK(balloonchatwnd::IsScreenShareLinkUrl(samples[i]) == ParseShareLinkUrl(samples[i], c));
    }
    //分享链接不是可以交给浏览器打开的网址
    CHECK_FALSE(balloonchatwnd::IsSafeLinkUrl(url));
}

TEST_CASE("SLI6 share link protocol", "[sharelink]")
{
    //1752 请求：取得不带 nopassword，设置免输密码时带
    CHECK(ShareProtocolClient::buildLink("r1", share_link_get, true) == "{\"action\":1,\"roomid\":\"r1\"}");
    CHECK(ShareProtocolClient::buildLink("r1", share_link_set_nopassword, true) ==
          "{\"action\":4,\"nopassword\":1,\"roomid\":\"r1\"}");
    CHECK(ShareProtocolClient::buildLinkResolve("Ab3dE7kQ9z") == "{\"linkcode\":\"Ab3dE7kQ9z\"}");
    //1722 带链接码；不带时与原来相同
    CHECK(ShareProtocolClient::buildJoin("r1", "", "Ab3dE7kQ9z") == "{\"linkcode\":\"Ab3dE7kQ9z\",\"roomid\":\"r1\"}");
    CHECK(ShareProtocolClient::buildJoin("r1", "123456", "") == ShareProtocolClient::buildJoin("r1", "123456"));
    //1752 应答
    ShareLinkResult r;
    REQUIRE(ShareProtocolClient::parseLinkResult(
        "{\"code\":0,\"msg\":\"\",\"roomid\":\"r1\",\"linkcode\":\"Ab3dE7kQ9z\",\"nopassword\":1}", r));
    CHECK(r.m_code == 0);
    CHECK(r.m_roomId == "r1");
    CHECK(r.m_linkCode == "Ab3dE7kQ9z");
    CHECK(r.m_noPassword);
    CHECK_FALSE(ShareProtocolClient::parseLinkResult("{\"msg\":\"x\"}", r));
    //1753 应答：会话成员带会话，链接访客不带
    ShareLinkSummary s;
    REQUIRE(ShareProtocolClient::parseLinkSummary(
        "{\"code\":0,\"msg\":\"\",\"status\":1,\"roomid\":\"r1\",\"title\":\"t\",\"desc\":\"d\",\"host\":\"5127\","
        "\"hostname\":\"h\",\"locked\":1,\"forced\":0,\"member\":1,\"ctype\":2,\"cid\":\"9001\"}",
        s));
    CHECK(s.m_status == share_link_status_active);
    CHECK(s.m_roomId == "r1");
    CHECK(s.m_hostId == 5127);
    CHECK(s.m_hostName == "h");
    CHECK(s.m_locked);
    CHECK_FALSE(s.m_forced);
    CHECK(s.m_member);
    CHECK(s.m_ctype == 2);
    CHECK(s.m_cid == 9001);
    ShareLinkSummary guest;
    REQUIRE(ShareProtocolClient::parseLinkSummary(
        "{\"code\":0,\"status\":1,\"roomid\":\"r1\",\"host\":\"5127\",\"member\":0}", guest));
    CHECK_FALSE(guest.m_member);
    CHECK(guest.m_ctype == 0);
    CHECK(guest.m_cid == 0);
    ShareLinkSummary failed;
    REQUIRE(ShareProtocolClient::parseLinkSummary("{\"code\":733,\"msg\":\"\"}", failed));
    CHECK(failed.m_code == 733);
    CHECK(failed.m_status == share_link_status_invalid);
    //1725 快照里的链接访客标记
    ShareStateNotify n;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"r1\",\"event\":1,\"actor\":\"7001\",\"host\":\"5127\",\"hostonline\":1,\"members\":["
        "{\"uid\":\"5127\",\"seat\":0},{\"uid\":\"7001\",\"seat\":1,\"vialink\":1}]}",
        n));
    REQUIRE(n.m_members.size() == 2);
    CHECK_FALSE(n.m_members[0].m_viaLink);
    CHECK(n.m_members[1].m_viaLink);
}

TEST_CASE("SLI7 host keeps p2p only for a single viewer", "[sharelink]")
{
    CHECK(ShareHostP2PAllowed(2, false));
    CHECK_FALSE(ShareHostP2PAllowed(3, false));
    CHECK_FALSE(ShareHostP2PAllowed(1, false));
    CHECK_FALSE(ShareHostP2PAllowed(2, true));
}

TEST_CASE("SLI8 forward picker sends a piece of text", "[sharelink]")
{
    balloonmain::ForwardRequest request;
    request.m_text = L"邀请你观看屏幕共享「周会」：flamingo://screenshare/Ab3dE7kQ9z";
    CHECK(balloonmain::ForwardPickerTitle(request) == L"发送邀请");
    CHECK(balloonmain::ForwardPickerContentTitle(request) == L"邀请内容");
    CHECK(balloonmain::BuildForwardPreviewText(request) == request.m_text);
    CHECK_FALSE(balloonmain::ForwardPickerSingleSelect(request));
}
