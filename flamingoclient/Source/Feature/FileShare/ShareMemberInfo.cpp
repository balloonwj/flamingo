/**
 *  文件共享宿主回送会话成员时的转换，见 ShareMemberInfo.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareMemberInfo.h"

#include <string.h>   // memset / memcpy

#include "../../net/GroupProtocolClient.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../UI/BalloonMain/AvatarSpec.h"   //DecodeAvatar：没设置过头像的人画显示名首字与按 userid 取的底色
#include "../../Common/I18n/I18n.h"   // TRF8 / TR_NOOP：UTF-8 界面文字的多语言查找

namespace balloonfileshare {

namespace {

//UTF-8 后续字节的高两位为 10
const unsigned char kUtf8ContinuationMask = 0xC0;
const unsigned char kUtf8ContinuationBits = 0x80;

//通讯录里查不到、名片与昵称又都为空时的显示名模板（与主面板 ContactDisplayName 的兜底一致），{0} 为用户 id；
//只做标记（TR_NOOP），使用处用 TRF8 取译文
const char* const kFallbackNameTemplate = TR_NOOP("用户{0}");

//通讯录显示名（UTF-8）；directory 为空时用兜底格式
std::string DirectoryName(int64_t userId, IFileShareDirectory* directory)
{
    if (directory != NULL)
    {
        const std::wstring name = directory->GetShareUserDisplayName(userId);
        if (!name.empty())
            return EncodeUtil::UnicodeToUtf8(name);
    }
    return TRF8(kFallbackNameTemplate, static_cast<long long>(userId));
}

} // namespace

void CopyUtf8Truncated(const std::string& text, char* buf, size_t bytes)
{
    if (buf == NULL || bytes == 0)
        return;
    size_t n = text.size();
    if (n >= bytes)
    {
        n = bytes - 1;
        //截断点落在多字节字符中间时往前退到该字符的首字节
        while (n > 0 && (static_cast<unsigned char>(text[n]) & kUtf8ContinuationMask) == kUtf8ContinuationBits)
            --n;
    }
    memcpy(buf, text.data(), n);
    buf[n] = '\0';
}

std::vector<fileshare::FileShareMemberInfo> BuildGroupMemberInfos(const net::GroupMembersResult& result,
                                                                  IFileShareDirectory* directory)
{
    std::vector<fileshare::FileShareMemberInfo> out;
    for (size_t i = 0; i < result.m_members.size(); ++i)
    {
        const net::GroupMemberItem& item = result.m_members[i];
        fileshare::FileShareMemberInfo info;
        memset(&info, 0, sizeof(info));
        info.m_userId = item.m_userid;
        info.m_role = item.m_role;
        //群名片、昵称、通讯录显示名，取第一个非空的（与群聊窗成员列表一致）
        const std::string& name = !item.m_card.empty() ? item.m_card
            : (!item.m_nickname.empty() ? item.m_nickname : DirectoryName(item.m_userid, directory));
        CopyUtf8Truncated(name, info.m_name, fileshare::kFileShareTextBytes);
        out.push_back(info);
    }
    return out;
}

fileshare::FileShareMemberInfo BuildPeerMemberInfo(int64_t peerUserId, IFileShareDirectory* directory)
{
    fileshare::FileShareMemberInfo info;
    memset(&info, 0, sizeof(info));
    info.m_userId = peerUserId;
    CopyUtf8Truncated(DirectoryName(peerUserId, directory), info.m_name, fileshare::kFileShareTextBytes);
    return info;
}

ShareUserAvatar DefaultShareUserAvatar(int64_t userId, const std::wstring& name)
{
    //facetype 0 即「没设置过头像」，DecodeAvatar 取显示名首字（为空时为「?」）与按 userid 取的底色
    const balloonmain::AvatarSpec spec = balloonmain::DecodeAvatar(balloonmain::avatar_kind_auto, std::string(),
                                                                   name.c_str(), userId);
    ShareUserAvatar avatar;
    avatar.m_glyph = spec.m_glyph.GetString();
    avatar.m_color = spec.m_color;
    return avatar;
}

bool FillAvatarInfo(const ShareUserAvatar& avatar, fileshare::FileShareAvatarInfo* out)
{
    if (out == NULL || out->m_cbSize != sizeof(fileshare::FileShareAvatarInfo))
        return false;
    const uint32_t cbSize = out->m_cbSize;
    memset(out, 0, sizeof(*out));
    out->m_cbSize = cbSize;

    //字形：最多放 kFileShareGlyphChars - 1 个 UTF-16 单元；截断点落在代理对的后半个上时往前退一个，不留下半个 emoji
    size_t glyphLen = avatar.m_glyph.size();
    if (glyphLen >= fileshare::kFileShareGlyphChars)
    {
        glyphLen = fileshare::kFileShareGlyphChars - 1;
        if (IS_LOW_SURROGATE(avatar.m_glyph[glyphLen]))
            --glyphLen;
    }
    memcpy(out->m_glyph, avatar.m_glyph.data(), glyphLen * sizeof(wchar_t));
    out->m_color = static_cast<uint32_t>(avatar.m_color);

    //照片路径：放不下就留空，改画字形
    if (avatar.m_imagePath.size() < fileshare::kFileSharePathChars)
        memcpy(out->m_imagePath, avatar.m_imagePath.data(), avatar.m_imagePath.size() * sizeof(wchar_t));
    return true;
}

} // namespace balloonfileshare
