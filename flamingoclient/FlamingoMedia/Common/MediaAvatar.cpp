/**
 *  媒体 DLL 里用户与群头像的来源与位图，实现见 MediaAvatar.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "MediaAvatar.h"

#include <string.h>   //memset / wcsncpy_s

#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/MainPanelAvatar.h"   //MakeMonogramAvatar：字形头像，与主面板联系人列表同一画法
#include "../../Source/UI/BalloonMain/AvatarImageUtil.h"   //MakeRoundedAvatarFromFile：照片头像
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"

namespace flamingomedia {

namespace {

//问号与中性灰：来源不给头像时的描述
MediaAvatarInfo UnknownAvatar()
{
    MediaAvatarInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    wcsncpy_s(info.m_glyph, kMediaAvatarGlyphChars, kUnknownAvatarGlyph, _TRUNCATE);
    info.m_color = static_cast<uint32_t>(kUnknownAvatarColor);
    return info;
}

//一份空白描述（m_cbSize 已填）：交给来源填写
MediaAvatarInfo EmptyAvatar()
{
    MediaAvatarInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    return info;
}

//按头像描述取位图：照片在本机且能解码时为照片，否则为字形
HBITMAP BitmapFor(balloonmain::CAvatarBitmapCache& cache, const MediaAvatarInfo& info, int sizePx)
{
    if (sizePx <= 0)
        return NULL;
    if (info.m_imagePath[0] != L'\0')
    {
        HBITMAP photo = cache.GetImage(info.m_imagePath, sizePx);
        if (photo != NULL)
            return photo;
        //照片解码失败（文件损坏、格式不认识）：改画字形
        LOG_WARNING("media avatar: photo could not be decoded, glyph used instead.");
    }
    return cache.GetMonogram(info.m_glyph, static_cast<COLORREF>(info.m_color), sizePx);
}

} // namespace

CHostAvatarQuery::CHostAvatarQuery()
    : m_host(NULL)
{
}

void CHostAvatarQuery::SetHost(IMediaHost* host)
{
    m_host = host;
}

bool CHostAvatarQuery::QueryUserAvatar(int64_t userId, MediaAvatarInfo* out)
{
    return m_host != NULL && m_host->GetUserAvatar(userId, out);
}

bool CHostAvatarQuery::QueryGroupAvatar(int64_t groupId, MediaAvatarInfo* out)
{
    return m_host != NULL && m_host->GetGroupAvatar(groupId, out);
}

CMediaAvatarSource::CMediaAvatarSource()
    : m_query(NULL)
    , m_cache(balloonmain::MakeMonogramAvatar, balloonmain::MakeRoundedAvatarFromFile)
{
}

CMediaAvatarSource::~CMediaAvatarSource()
{
    Clear();
}

void CMediaAvatarSource::SetHost(IMediaHost* host)
{
    m_hostQuery.SetHost(host);
    m_query = (host != NULL) ? &m_hostQuery : NULL;
}

void CMediaAvatarSource::SetQuery(IMediaAvatarQuery* query)
{
    m_query = query;
}

MediaAvatarInfo CMediaAvatarSource::QueryUser(int64_t userId) const
{
    MediaAvatarInfo info = EmptyAvatar();
    if (m_query != NULL && m_query->QueryUserAvatar(userId, &info))
        return info;
    return UnknownAvatar();
}

MediaAvatarInfo CMediaAvatarSource::QueryGroup(int64_t groupId) const
{
    MediaAvatarInfo info = EmptyAvatar();
    if (m_query != NULL && m_query->QueryGroupAvatar(groupId, &info))
        return info;
    return UnknownAvatar();
}

HBITMAP CMediaAvatarSource::GetUserBitmap(int64_t userId, int sizePx)
{
    return BitmapFor(m_cache, QueryUser(userId), sizePx);
}

HBITMAP CMediaAvatarSource::GetGroupBitmap(int64_t groupId, int sizePx)
{
    return BitmapFor(m_cache, QueryGroup(groupId), sizePx);
}

std::unique_ptr<balloonwjui::DuiAvatar> CMediaAvatarSource::MakeUserAvatar(int64_t userId, int sizePx)
{
    std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
    Apply(avatar.get(), QueryUser(userId), sizePx);
    return avatar;
}

std::unique_ptr<balloonwjui::DuiAvatar> CMediaAvatarSource::MakeGroupAvatar(int64_t groupId, int sizePx)
{
    std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
    Apply(avatar.get(), QueryGroup(groupId), sizePx);
    return avatar;
}

void CMediaAvatarSource::ApplyUser(balloonwjui::DuiAvatar* avatar, int64_t userId, int sizePx)
{
    if (avatar != NULL)
        Apply(avatar, QueryUser(userId), sizePx);
}

void CMediaAvatarSource::ApplyGroup(balloonwjui::DuiAvatar* avatar, int64_t groupId, int sizePx)
{
    if (avatar != NULL)
        Apply(avatar, QueryGroup(groupId), sizePx);
}

void CMediaAvatarSource::Apply(balloonwjui::DuiAvatar* avatar, const MediaAvatarInfo& info, int sizePx)
{
    avatar->SetShape(balloonwjui::DuiAvatar::ShapeCircle);
    //名字与底色只在位图生成失败时用到：DuiAvatar 没有位图时自己画字形
    avatar->SetName(info.m_glyph);
    avatar->SetFallbackBgColor(static_cast<COLORREF>(info.m_color));
    avatar->SetBitmap(BitmapFor(m_cache, info, sizePx));
}

void CMediaAvatarSource::Clear()
{
    m_cache.Clear();
}

size_t CMediaAvatarSource::BitmapCount() const
{
    return m_cache.Count();
}

} // namespace flamingomedia
