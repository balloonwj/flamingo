/**
 *  ChatAvatarResolve.cpp —— 聊天窗头像呈现解析实现。
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"
#include "ChatAvatarResolve.h"

#include "../BalloonMain/AvatarSpec.h"   // balloonmain::DecodeAvatar / AvatarSpec / avatar_kind_image
#include "../../net/ImageTransfer.h"
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）        // 真图缓存路径 + 入队下载

namespace balloonchatwnd {

AvatarRender ResolveAvatarRender(int64_t userId, int facetype,
                                 const std::string& customface,
                                 LPCTSTR fallbackName, int64_t selfId)
{
    AvatarRender out;

    // 先解出字形 + 底色（真图回退时也复用这套占位）。
    balloonmain::AvatarSpec spec =
        balloonmain::DecodeAvatar(facetype, customface, fallbackName, userId);
    out.m_glyph = spec.m_glyph;
    out.m_color = spec.m_color;

    // 真图头像：缓存命中给真图路径，未命中入队下载并暂回退字形。
    if (spec.m_kind == balloonmain::avatar_kind_image && !customface.empty() && selfId > 0)
    {
        std::wstring path =
            net::ImageTransfer::AvatarCacheFilePathForFileId(net::CurrentUserAccount(), customface);
        if (!path.empty()
            && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            out.m_imagePath = path.c_str();
        }
        else
        {
            // 未缓存：入队下载（按 fileid 去重），下载完成后由聊天窗刷新本气泡 / 列表。
            net::ImageTransfer::GetInstance().EnqueueAvatarDownload(
                customface, net::CurrentUserAccount());
        }
    }

    return out;
}

} // namespace balloonchatwnd
