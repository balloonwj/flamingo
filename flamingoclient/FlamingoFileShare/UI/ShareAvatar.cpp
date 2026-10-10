/**
 *  文件共享窗口里成员头像的来源与位图 CShareAvatarSource 的实现，见 ShareAvatar.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "ShareAvatar.h"

#include <string.h>   //memset / wcsncpy_s

#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/MainPanelAvatar.h"   //MakeMonogramAvatar：字形头像，与主面板联系人列表同一画法
#include "../../Source/UI/BalloonMain/AvatarImageUtil.h"   //MakeRoundedAvatarFromFile：照片头像
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"

namespace fileshare {

namespace {

//按头像描述取位图：照片在本机且能解码时为照片，否则为字形
HBITMAP BitmapFor(balloonmain::CAvatarBitmapCache& cache, const FileShareAvatarInfo& info, int sizePx)
{
    if (sizePx <= 0)
        return NULL;
    if (info.m_imagePath[0] != L'\0')
    {
        HBITMAP photo = cache.GetImage(info.m_imagePath, sizePx);
        if (photo != NULL)
            return photo;
        //照片解码失败（文件损坏、格式不认识）：改画字形
        LOG_WARNING("share avatar: photo could not be decoded, glyph used instead.");
    }
    return cache.GetMonogram(info.m_glyph, static_cast<COLORREF>(info.m_color), sizePx);
}

} // namespace

CShareAvatarSource::CShareAvatarSource()
    : m_host(NULL)
    , m_cache(balloonmain::MakeMonogramAvatar, balloonmain::MakeRoundedAvatarFromFile)
{
}

CShareAvatarSource::~CShareAvatarSource()
{
    Clear();
}

void CShareAvatarSource::SetHost(IFileShareHost* host)
{
    m_host = host;
}

FileShareAvatarInfo CShareAvatarSource::Query(int64_t userId) const
{
    FileShareAvatarInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    if (m_host != NULL && m_host->GetUserAvatar(userId, &info))
        return info;

    //宿主不给：问号与中性灰
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    wcsncpy_s(info.m_glyph, kFileShareGlyphChars, kUnknownAvatarGlyph, _TRUNCATE);
    info.m_color = static_cast<uint32_t>(kUnknownAvatarColor);
    return info;
}

HBITMAP CShareAvatarSource::GetBitmap(int64_t userId, int sizePx)
{
    return BitmapFor(m_cache, Query(userId), sizePx);
}

std::unique_ptr<balloonwjui::DuiAvatar> CShareAvatarSource::MakeAvatar(int64_t userId, int sizePx)
{
    const FileShareAvatarInfo info = Query(userId);
    std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
    avatar->SetShape(balloonwjui::DuiAvatar::ShapeCircle);
    //名字与底色只在位图生成失败时用到：DuiAvatar 没有位图时自己画字形
    avatar->SetName(info.m_glyph);
    avatar->SetFallbackBgColor(static_cast<COLORREF>(info.m_color));
    avatar->SetBitmap(BitmapFor(m_cache, info, sizePx));
    return avatar;
}

void CShareAvatarSource::Clear()
{
    m_cache.Clear();
}

size_t CShareAvatarSource::BitmapCount() const
{
    return m_cache.Count();
}

} // namespace fileshare
