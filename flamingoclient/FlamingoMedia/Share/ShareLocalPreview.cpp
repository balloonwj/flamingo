/**
 *  多人共享里本人那一块画面的本机预览，见 ShareLocalPreview.h。
 *  balloonwj@qq.com   2026-10-12
 */
#include "ShareLocalPreview.h"

namespace screenshare {

ShareLocalPreview::ShareLocalPreview()
    : m_seat(-1)
    , m_hasFrame(false)
    , m_lastMs(0)
{
}

void ShareLocalPreview::SetSeat(int seat)
{
    m_seat.store(seat < 0 ? -1 : seat);
    if (seat >= 0)
        return;
    m_latest.Clear();
    m_hasFrame.store(false);
}

int ShareLocalPreview::Seat() const
{
    return m_seat.load();
}

bool ShareLocalPreview::IsEnabled() const
{
    return m_seat.load() >= 0;
}

bool ShareLocalPreview::HasFrame() const
{
    return m_hasFrame.load();
}

bool ShareLocalPreview::Offer(const uint8_t* pixels, int width, int height, int stride, int64_t nowMs)
{
    if (!IsEnabled() || pixels == NULL || width <= 0 || height <= 0)
        return false;
    std::lock_guard<std::mutex> guard(m_offerMutex);
    //尺寸没变、离上一帧又不够间隔：不留；第一帧与尺寸变化后的第一帧立即留下
    int lastW = 0;
    int lastH = 0;
    m_latest.Size(lastW, lastH);
    const bool sameSize = m_hasFrame.load() && width == lastW && height == lastH;
    if (sameSize && nowMs - m_lastMs < kLocalPreviewIntervalMs)
        return false;
    //行跨度不够等不合法的参数由 Store 拒绝
    if (!m_latest.Store(pixels, width, height, stride))
        return false;
    m_lastMs = nowMs;
    m_hasFrame.store(true);
    return true;
}

bool ShareLocalPreview::Get(int seat, std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const
{
    if (seat < 0 || seat != m_seat.load())
        return false;
    return m_latest.Get(bgra, width, height, serial);
}

bool ShareLocalPreview::GetSerial(int seat, uint64_t& serial) const
{
    if (seat < 0 || seat != m_seat.load())
        return false;
    return m_latest.GetSerial(serial);
}

} // namespace screenshare
