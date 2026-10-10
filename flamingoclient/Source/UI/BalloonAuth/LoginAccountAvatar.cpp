/**
 *  登录窗的账号头像，见 LoginAccountAvatar.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "LoginAccountAvatar.h"

#include "../BalloonMain/AvatarSpec.h"        // DecodeAvatar / AvatarFallbackGlyph：与主面板同一口径
#include "../BalloonMain/AvatarPalette.h"     // AvatarColorByIndex / AvatarColorForText
#include "../BalloonMain/MainPanelAvatar.h"   // MakeMonogramAvatar：字形头像
#include "../BalloonMain/AvatarImageUtil.h"   // MakeRoundedAvatarFromFile：照片头像
#include "../../Common/Log/IULog.h"           // LOG_WARNING
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

namespace balloonauth {

namespace {

// 本机没登录过的账号用的底色：调色板第 0 个（原来的大头像默认的橙色方块）
const int kUnknownAccountColorIndex = 0;

} // namespace

LoginAvatarSpec ResolveLoginAvatar(const CString& account, const LOGIN_ACCOUNT_INFO* info, const std::wstring& photoPath)
{
    LoginAvatarSpec spec;
    // 本机没登录过：账号首字，默认底色
    if (info == NULL)
    {
        spec.m_glyph = balloonmain::AvatarFallbackGlyph((LPCTSTR)account);
        spec.m_color = balloonmain::AvatarColorByIndex(kUnknownAccountColorIndex);
        return spec;
    }

    // 显示名：姓名，没有姓名时账号
    const CString nickName(info->szNickName);
    const CString name = nickName.IsEmpty() ? account : nickName;

    // 这次升级之前存下的账号还没有 userid：姓名首字，底色按账号取（与原来的大头像一致）
    if (info->nUserId == 0)
    {
        spec.m_glyph = balloonmain::AvatarFallbackGlyph((LPCTSTR)name);
        spec.m_color = balloonmain::AvatarColorForText((LPCTSTR)account);
        return spec;
    }

    // 与主面板同一口径
    const balloonmain::AvatarSpec decoded = balloonmain::DecodeAvatar(info->nFaceType, std::string(info->szCustomFace),
                                                                      (LPCTSTR)name, (int64_t)info->nUserId);
    spec.m_glyph = decoded.m_glyph;
    spec.m_color = decoded.m_color;
    if (decoded.m_kind == balloonmain::avatar_kind_image && !photoPath.empty())
        spec.m_photoPath = photoPath;
    return spec;
}

CLoginAvatarBitmaps::CLoginAvatarBitmaps()
    : m_cache(balloonmain::MakeMonogramAvatar, balloonmain::MakeRoundedAvatarFromFile)
{
}

HBITMAP CLoginAvatarBitmaps::GetBitmap(const LoginAvatarSpec& spec, int sizePx)
{
    if (sizePx <= 0)
        return NULL;
    if (!spec.m_photoPath.empty())
    {
        HBITMAP photo = m_cache.GetImage(spec.m_photoPath.c_str(), sizePx);
        if (photo != NULL)
            return photo;
        // 照片解码失败（文件损坏、格式不认识）：改画字形
        LOG_WARNING("login avatar: photo could not be decoded, glyph used instead.");
    }
    return m_cache.GetMonogram((LPCTSTR)spec.m_glyph, spec.m_color, sizePx);
}

void CLoginAvatarBitmaps::Clear()
{
    m_cache.Clear();
}

size_t CLoginAvatarBitmaps::Count() const
{
    return m_cache.Count();
}

int FillAccountCombo(balloonwjui::DuiComboBox* combo, CLoginAccountList* list, CLoginAvatarBitmaps& bitmaps,
                     LoginPhotoPathFn photoPathFn)
{
    if (combo == NULL || list == NULL)
        return 0;

    // 浮层每行「头像 - 账号 - 姓名」：行高放得下头像
    combo->SetItemHeight(kAccountRowHeight);
    combo->SetIconSize(kAccountAvatarPx);

    int filled = 0;
    const int count = list->GetCount();
    for (int i = 0; i < count; ++i)
    {
        const LOGIN_ACCOUNT_INFO* lpAccount = list->GetAccountInfo(i);
        if (lpAccount == NULL)
            continue;
        const CString account(lpAccount->szUser);
        const std::wstring photo = (photoPathFn != NULL) ? photoPathFn(account, lpAccount) : std::wstring();
        const LoginAvatarSpec spec = ResolveLoginAvatar(account, lpAccount, photo);
        const int index = combo->AddString(account);
        combo->SetItemIcon(index, bitmaps.GetBitmap(spec, kAccountAvatarPx));
        combo->SetItemSubText(index, lpAccount->szNickName);
        ++filled;
    }
    return filled;
}

} // namespace balloonauth
