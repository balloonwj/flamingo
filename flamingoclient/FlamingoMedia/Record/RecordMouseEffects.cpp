/**
 *  独立录屏的鼠标效果，说明见 RecordMouseEffects.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "RecordMouseEffects.h"

namespace screenrecord {

namespace {

//每像素字节数（BGRA）
const int kBytesPerPixel = 4;

} // namespace

RECT RecordSourceScreenRect(const RECT& monitorRect, const RECT& crop)
{
    //不裁剪：整块显示器
    if (crop.right <= crop.left || crop.bottom <= crop.top)
        return monitorRect;
    RECT rc;
    rc.left = monitorRect.left + crop.left;
    rc.top = monitorRect.top + crop.top;
    rc.right = monitorRect.left + crop.right;
    rc.bottom = monitorRect.top + crop.bottom;
    return rc;
}

RecordMouseDecorator::RecordMouseDecorator()
    : m_settingsChanged(true)
    , m_geometry(NULL)
{
}

RecordMouseDecorator::~RecordMouseDecorator()
{
}

void RecordMouseDecorator::SetGeometry(const IRecordSourceGeometry* geometry)
{
    m_geometry = geometry;
}

void RecordMouseDecorator::SetSettings(const screenshare::ShareMouseEffectSettings& settings)
{
    std::lock_guard<std::mutex> guard(m_settingsMutex);
    m_settings = settings;
    m_settingsChanged = true;
}

screenshare::ShareMouseEffectSettings RecordMouseDecorator::Settings() const
{
    std::lock_guard<std::mutex> guard(m_settingsMutex);
    return m_settings;
}

void RecordMouseDecorator::Reset()
{
    m_effects.Reset();
}

void RecordMouseDecorator::sampleCursor(POINT& pt, bool& left, bool& right, int64_t nowMs)
{
    m_sampler.Sample(pt, left, right, nowMs);
}

void RecordMouseDecorator::Decorate(uint8_t* bgra, int width, int height, int64_t nowMs)
{
    if (bgra == NULL || width <= 0 || height <= 0)
        return;
    //取走新设置
    {
        std::lock_guard<std::mutex> guard(m_settingsMutex);
        if (m_settingsChanged)
        {
            m_effects.SetSettings(m_settings);
            m_settingsChanged = false;
        }
    }
    //三项都关着、或不知道录的是屏幕上哪一块：不取光标、不画
    const screenshare::ShareMouseEffectSettings& settings = m_effects.Settings();
    if ((!settings.m_halo && !settings.m_click && !settings.m_trail) || m_geometry == NULL)
        return;
    POINT pt;
    bool left = false;
    bool right = false;
    sampleCursor(pt, left, right, nowMs);
    POINT framePt;
    framePt.x = 0;
    framePt.y = 0;
    const bool inside = screenshare::MapScreenToFrame(pt, m_geometry->SourceScreenRect(), width, height, framePt);
    m_effects.Update(framePt, inside, left, right, nowMs);
    m_effects.Render(bgra, width, height, width * kBytesPerPixel, nowMs);
}

} // namespace screenrecord
