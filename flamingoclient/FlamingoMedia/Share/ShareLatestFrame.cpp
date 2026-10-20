/**
 *  一路画面的「最新一帧」缓存，说明见 ShareLatestFrame.h。
 *  balloonwj@qq.com   2026-10-14
 */
#include "ShareLatestFrame.h"

#include <string.h>

namespace screenshare {

namespace {

//BGRA 每像素字节数
const int kLatestBytesPerPixel = 4;

} // namespace

ShareLatestFrame::ShareLatestFrame()
    : m_width(0)
    , m_height(0)
    , m_serial(0)
{
}

bool ShareLatestFrame::Store(const uint8_t* pixels, int width, int height, int stride)
{
    const int rowBytes = width * kLatestBytesPerPixel;
    if (pixels == NULL || width <= 0 || height <= 0 || stride < rowBytes)
        return false;
    std::lock_guard<std::mutex> guard(m_mutex);
    m_frame.resize(static_cast<size_t>(rowBytes) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y)
        memcpy(&m_frame[static_cast<size_t>(y) * rowBytes], pixels + static_cast<size_t>(y) * stride, rowBytes);
    m_width = width;
    m_height = height;
    ++m_serial;
    return true;
}

bool ShareLatestFrame::Get(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_frame.empty())
        return false;
    bgra = m_frame;
    width = m_width;
    height = m_height;
    serial = m_serial;
    return true;
}

bool ShareLatestFrame::GetSerial(uint64_t& serial) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_frame.empty())
        return false;
    serial = m_serial;
    return true;
}

bool ShareLatestFrame::HasFrame() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return !m_frame.empty();
}

void ShareLatestFrame::Size(int& width, int& height) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    width = m_frame.empty() ? 0 : m_width;
    height = m_frame.empty() ? 0 : m_height;
}

void ShareLatestFrame::Clear()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_frame.clear();
    m_width = 0;
    m_height = 0;
}

} // namespace screenshare
