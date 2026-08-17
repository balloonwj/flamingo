/**
 *  MainPanelUserData.cpp
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程共用。在 Flamingo.vcxproj 里本文件需
 *  标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-28
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖(等价 NewChatDemo/stdafx.h) ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "MainPanelUserData.h"
#include "MainPanelHeader.h"
#include "BalloonMainLayout.h"    // kIdHeader
#include "AvatarSpec.h"           // DecodeAvatar：按 (facetype,customface) 解出字形 / 底色

namespace balloonmain {

namespace {

// avatarColor 调色板 —— 4 色,沿用 contacts 列表 mock 数据里出现过的同款
// 颜色(MainPanelMockData.cpp:89/107/110/113),让 Banner 自画头像与列表里
// 出现的"自己"那一条视觉一致。
const COLORREF kHeaderAvatarPalette[] = {
    RGB(0xFF, 0x7A, 0x45),   // 橙
    RGB(0x3B, 0x82, 0xF6),   // 蓝
    RGB(0xA8, 0x55, 0xF7),   // 紫
    RGB(0x10, 0xB9, 0x81),   // 绿
};
const int kHeaderAvatarPaletteSize =
    sizeof(kHeaderAvatarPalette) / sizeof(kHeaderAvatarPalette[0]);

// 取宽字符串首字符为子串(空串返回空串)。中文等 BMP 字符 1 个 WCHAR 即可;
// 极少数 BMP 外字符(emoji 等)会被 Left(1) 截 surrogate 一半,当前数据源
// (用户名/账号)都是 BMP 范围内,不处理这种边界。
CString FirstChar(const CString& s)
{
    return s.IsEmpty() ? CString() : s.Left(1);
}

} // anonymous namespace

COLORREF GetUserAvatarColor(UINT userid)
{
    return kHeaderAvatarPalette[userid % kHeaderAvatarPaletteSize];
}

void BuildHeaderUserData(UINT userid,
                        const CString& account,
                        const CString& nickname,
                        const CString& signature,
                        HeaderUserDataPack& out)
{
    out.nickname  = nickname;
    out.account   = account;
    out.signature = signature;

    // 头像字优先用昵称首字符;昵称为空退回取 account 首字符;都空则留空,
    // 由 Header 自己决定不画头像字(MainPanelHeader::OnPaint 会显示空格)。
    out.avatarText = nickname.IsEmpty() ? FirstChar(account) : FirstChar(nickname);

    // avatarColor 按 userid 取模分配,稳定无随机性 —— 同一用户多次登录或
    // 在多处出现时颜色一致。
    out.avatarColor = GetUserAvatarColor(userid);

    // P2 阶段 unreadTotal 占位为 0,RecentList 接入后再从会话未读数求和。
    out.unreadTotal = 0;
}

bool ApplyUserDataToMainPanel(balloonwjui::DuiControl* root,
                              UINT userid,
                              const CString& account,
                              const CString& nickname,
                              const CString& signature,
                              int presence)
{
    if (root == NULL)
    {
        return false;
    }
    balloonwjui::DuiControl* hdr = root->FindCtrlById(kIdHeader);
    if (hdr == NULL)
    {
        return false;
    }
    MainPanelHeader* mph = dynamic_cast<MainPanelHeader*>(hdr);
    if (mph == NULL)
    {
        return false;
    }

    HeaderUserDataPack pack;
    BuildHeaderUserData(userid, account, nickname, signature, pack);

    mph->SetUserData(pack.nickname, pack.account, pack.signature,
                     pack.avatarText, pack.avatarColor, pack.unreadTotal, presence);
    // Invalidate 自带 host 守卫(DuiControl::Invalidate 见 DuiControl.cpp:326),
    // 未挂 host(单元测试场景)时为 no-op,安全。
    mph->Invalidate();
    return true;
}

bool ApplyUserDataToMainPanel(balloonwjui::DuiControl* root,
                              UINT userid,
                              const CString& account,
                              const CString& nickname,
                              const CString& signature,
                              int presence,
                              int facetype,
                              const std::string& customfaceUtf8)
{
    if (root == NULL)
    {
        return false;
    }
    balloonwjui::DuiControl* hdr = root->FindCtrlById(kIdHeader);
    if (hdr == NULL)
    {
        return false;
    }
    MainPanelHeader* mph = dynamic_cast<MainPanelHeader*>(hdr);
    if (mph == NULL)
    {
        return false;
    }

    HeaderUserDataPack pack;
    BuildHeaderUserData(userid, account, nickname, signature, pack);

    // 字形头像：用 customface 编码的字形 + 调色板底色覆盖自动值；其它类型（自动 /
    // 第一期自定义图）维持上面算好的自动渲染，确保历史账号不变色。
    AvatarSpec spec = DecodeAvatar(facetype, customfaceUtf8,
                                   nickname.IsEmpty() ? account : nickname,
                                   (int64_t)userid);
    if (spec.m_kind == avatar_kind_glyph)
    {
        pack.avatarText  = spec.m_glyph;
        pack.avatarColor = spec.m_color;
    }

    mph->SetUserData(pack.nickname, pack.account, pack.signature,
                     pack.avatarText, pack.avatarColor, pack.unreadTotal, presence);
    mph->Invalidate();
    return true;
}

} // namespace balloonmain
