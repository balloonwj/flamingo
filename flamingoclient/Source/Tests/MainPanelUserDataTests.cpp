/**
 *  MainPanelUserDataTests.cpp
 *
 *  P2 测试:验证"当前登录用户字段 → 主面板 Banner"那段映射 + 注入逻辑。
 *
 *  helper 不依赖 CBuddyInfo,4 个原始字段(userid + account + nickname +
 *  signature)直接传入 —— 单测<u>无需</u>把 UserSessionData.cpp 拖进
 *  FlamingoUnit 工程。
 *
 *  覆盖用例:
 *    H1.1  普通用户字段映射到 HeaderUserDataPack
 *    H1.2  昵称为空时 pack.nickname 也为空(让 Header 用 account 兜底)
 *    H1.3  avatarText 取昵称首字符;空昵称时取 account 首字符
 *    H1.4  avatarColor 按 userid 哈希分配:不同 userid 得不同色,同 userid 稳定
 *    H2.1  ApplyUserDataToMainPanel 把数据注入到 MainPanelHeader
 *    H2.2  ApplyUserDataToMainPanel 对 null root 返 false 不崩
 *
 *  用 Catch2 v3(amalgamated)。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义,本文件只引用。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/BalloonMainLayout.h"
#include "../UI/BalloonMain/MainPanelHeader.h"
#include "../UI/BalloonMain/MainPanelUserData.h"

using namespace balloonwjui;
using namespace balloonmain;

// =====================================================================
// H1. 纯函数:BuildHeaderUserData 字段映射
// =====================================================================

TEST_CASE("H1.1 普通用户字段映射到 HeaderUserDataPack",
          "[p2][header][userdata]")
{
    HeaderUserDataPack pack;
    BuildHeaderUserData(5312, _T("MY0312"), _T("王雅平"),
                        _T("用数据验证每个判断，知行合一。"), pack);

    CHECK(pack.nickname  == CString(_T("王雅平")));
    CHECK(pack.account   == CString(_T("MY0312")));
    CHECK(pack.signature == CString(_T("用数据验证每个判断，知行合一。")));
    // P2 阶段 unreadTotal 占位为 0,RecentList 接入后再扩展。
    CHECK(pack.unreadTotal == 0);
}

TEST_CASE("H1.2 昵称为空时 pack.nickname 也为空(让 Header 用 account 兜底)",
          "[p2][header][userdata]")
{
    HeaderUserDataPack pack;
    BuildHeaderUserData(5001, _T("MY0001"), _T(""), _T(""), pack);

    // pack 不做"昵称→account"兜底,因为 Header.OnPaint 自己已经做过
    // (m_nickname.IsEmpty() ? m_account : m_nickname)。helper 只搬数据。
    CHECK(pack.nickname.IsEmpty());
    CHECK(pack.account == CString(_T("MY0001")));
    CHECK(pack.signature.IsEmpty());
}

TEST_CASE("H1.3 avatarText 取昵称首字符;空昵称时取 account 首字符",
          "[p2][header][userdata]")
{
    SECTION("有昵称 —— 取昵称首字符(汉字宽字符)")
    {
        HeaderUserDataPack pack;
        BuildHeaderUserData(5312, _T("MY0312"), _T("王雅平"), _T(""), pack);
        CHECK(pack.avatarText == CString(_T("王")));
    }
    SECTION("空昵称 —— 退回取 account 首字符")
    {
        HeaderUserDataPack pack;
        BuildHeaderUserData(5001, _T("MY0001"), _T(""), _T(""), pack);
        CHECK(pack.avatarText == CString(_T("M")));
    }
    SECTION("昵称账号都空 —— avatarText 也为空(防 IsEmpty 之后取 [0] 越界)")
    {
        HeaderUserDataPack pack;
        BuildHeaderUserData(0, _T(""), _T(""), _T(""), pack);
        CHECK(pack.avatarText.IsEmpty());
    }
}

TEST_CASE("H1.4 avatarColor 按 userid 哈希分配:不同 userid 得不同色,同 userid 稳定",
          "[p2][header][userdata]")
{
    HeaderUserDataPack pa, pb;
    // 5001 % 4 = 1,5312 % 4 = 0 —— 选不同调色板项。
    BuildHeaderUserData(5001, _T("MY0001"), _T("叶磊"), _T(""), pa);
    BuildHeaderUserData(5312, _T("MY0312"), _T("王雅平"), _T(""), pb);
    CHECK(pa.avatarColor != pb.avatarColor);

    // 同 userid 调两次应得相同色(无随机性)。
    HeaderUserDataPack pa2;
    BuildHeaderUserData(5001, _T("MY0001"), _T("叶磊"), _T(""), pa2);
    CHECK(pa.avatarColor == pa2.avatarColor);
}

// =====================================================================
// H2. 集成:ApplyUserDataToMainPanel 把数据真注入到 Header 控件
// =====================================================================

TEST_CASE("H2.1 ApplyUserDataToMainPanel 把数据注入到 MainPanelHeader",
          "[p2][header][userdata]")
{
    std::unique_ptr<DuiControl> root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    DuiControl* hdr = root->FindCtrlById(kIdHeader);
    REQUIRE(hdr != nullptr);
    MainPanelHeader* mph = dynamic_cast<MainPanelHeader*>(hdr);
    REQUIRE(mph != nullptr);

    // 注入前:Header 处于 Mock 模式。
    REQUIRE(mph->GetHasUserData() == false);

    // presence 传 2(离开),验证它被注入到 Header。
    REQUIRE(ApplyUserDataToMainPanel(root.get(), 5312, _T("MY0312"),
                                     _T("王雅平"),
                                     _T("用数据验证每个判断，知行合一。"), 2) == true);

    // 注入后:Header 切到注入模式,各字段与 pack 一致。
    CHECK(mph->GetHasUserData() == true);
    CHECK(mph->GetNickname()    == CString(_T("王雅平")));
    CHECK(mph->GetAccount()     == CString(_T("MY0312")));
    CHECK(mph->GetSignature()   == CString(_T("用数据验证每个判断，知行合一。")));
    CHECK(mph->GetAvatarText()  == CString(_T("王")));
    CHECK(mph->GetUnreadTotal() == 0);
    CHECK(mph->GetPresence()    == 2);   // 离开(presence_away)被正确注入
}

TEST_CASE("H2.2 ApplyUserDataToMainPanel 对 null root 返 false 不崩",
          "[p2][header][userdata]")
{
    CHECK(ApplyUserDataToMainPanel(nullptr, 5312, _T("MY0312"),
                                   _T("王雅平"), _T("..."), 1) == false);
}

TEST_CASE("H2.3 未读公告数与会话未读数各走各的，互不影响",
          "[p2][header][announce]")
{
    // Banner 底部那一行显示的是<u>未读公告</u>数（消息盒子的入口），而 SetUserData 的
    // unreadTotal 参数是<u>会话</u>未读总数。两者语义不同、来源不同（前者来自本地公告库
    // CAnnounceStore，后者现阶段恒为 0），本用例把这条边界钉住：将来谁想图省事把公告
    // 未读塞进 unreadTotal（或反过来），这里就会红。
    std::unique_ptr<DuiControl> root = BuildMainPanelBody();
    REQUIRE(root != nullptr);

    MainPanelHeader* mph = dynamic_cast<MainPanelHeader*>(root->FindCtrlById(kIdHeader));
    REQUIRE(mph != nullptr);

    // 默认都是 0。
    CHECK(mph->GetAnnounceUnread() == 0);
    CHECK(mph->GetUnreadTotal() == 0);

    // 注入用户资料（unreadTotal 走 pack，恒为 0）不会动公告未读数。
    REQUIRE(ApplyUserDataToMainPanel(root.get(), 5312, _T("MY0312"),
                                     _T("王雅平"), _T("签名"), 1) == true);
    CHECK(mph->GetAnnounceUnread() == 0);

    // 注入公告未读数，只影响它自己。
    mph->SetAnnounceUnread(6);
    CHECK(mph->GetAnnounceUnread() == 6);
    CHECK(mph->GetUnreadTotal() == 0);

    // 再注入一次用户资料（改头像那条路径会走到），公告未读数不该被冲掉 ——
    // 否则收到公告后随便改个签名，角标就没了。
    REQUIRE(ApplyUserDataToMainPanel(root.get(), 5312, _T("MY0312"),
                                     _T("王雅平"), _T("新签名"), 1) == true);
    CHECK(mph->GetAnnounceUnread() == 6);

    // 负数按 0 处理，不会出现"负条新公告"。
    mph->SetAnnounceUnread(-3);
    CHECK(mph->GetAnnounceUnread() == 0);
}
