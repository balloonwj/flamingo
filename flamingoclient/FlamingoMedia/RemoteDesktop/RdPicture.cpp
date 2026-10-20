/**
 *  远程桌面控制端选用画面的规则，见 RdPicture.h。
 *  balloonwj@qq.com   2026-10-15
 */
#include "RdPicture.h"

namespace remotedesktop {

RdPictureSelector::RdPictureSelector()
    : m_active(rd_picture_none)
{
}

bool RdPictureSelector::OnLegacyKeyframe()
{
    const int before = m_active;
    m_active = rd_picture_legacy;
    return before != m_active;
}

bool RdPictureSelector::OnVideoFrame()
{
    const int before = m_active;
    m_active = rd_picture_video;
    return before != m_active;
}

int RdPictureSelector::Active() const
{
    return m_active;
}

void RdPictureSelector::Reset()
{
    m_active = rd_picture_none;
}

} // namespace remotedesktop
