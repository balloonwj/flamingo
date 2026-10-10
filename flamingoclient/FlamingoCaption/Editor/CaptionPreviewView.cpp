/**
 *  字幕编辑器的视频预览区，见 CaptionPreviewView.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionPreviewView.h"

#include <string.h>

#include "../Render/CaptionRenderer.h"
#include "../../Source/Common/I18n/I18n.h"   //UiFontName：提示文字的字体

namespace caption {

namespace {

//没有画面时的底色与提示文字颜色
const COLORREF kEmptyBg = RGB(24, 24, 28);
const COLORREF kMessageColor = RGB(200, 200, 205);
//提示文字的字号（像素）
const int kMessageFontPx = 16;
//录音模式（14.13）：当前字幕的字号、说话人的字号（像素），字幕文字颜色，字幕区左右留白（像素）
const int kAudioCaptionFontPx = 28;
const int kAudioSpeakerFontPx = 16;
const COLORREF kAudioCaptionColor = RGB(255, 255, 255);
const int kAudioCaptionPadX = 24;
//录音模式下提示文字占预览区上方的几分之一（分母）
const int kAudioMessageBandDivisor = 3;
//说话人一行与字幕之间的间距（像素）
const int kAudioSpeakerGap = 6;

//按像素字号建界面字体；调用方负责 DeleteObject
HFONT makeUiFont(int px, int weight)
{
    return ::CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, i18n::UiFontName());
}
//预乘透明度混合
const uint32_t kAlphaFull = 255;
const uint32_t kRoundHalf = 127;
//每像素位数（DIB）
const WORD kBitsPerPixel = 32;

} // namespace

CaptionPreviewView::CaptionPreviewView()
    : m_frameW(0)
    , m_frameH(0)
    , m_layerValid(false)
    , m_audioOnly(false)
{
    memset(&m_videoRect, 0, sizeof(m_videoRect));
    memset(&m_layerBounds, 0, sizeof(m_layerBounds));
}

void CaptionPreviewView::SetFrame(const std::vector<uint32_t>& bgra, int width, int height, const RECT& videoRect)
{
    if (width <= 0 || height <= 0 || bgra.size() < static_cast<size_t>(width) * height)
        return;
    const bool resized = (width != m_frameW || height != m_frameH || !::EqualRect(&videoRect, &m_videoRect));
    m_frame.assign(bgra.begin(), bgra.begin() + static_cast<size_t>(width) * height);
    m_frameW = width;
    m_frameH = height;
    m_videoRect = videoRect;
    if (resized)
        m_layerValid = false;
    Invalidate();
}

void CaptionPreviewView::ClearFrame()
{
    m_frame.clear();
    m_frameW = 0;
    m_frameH = 0;
    m_layerValid = false;
    Invalidate();
}

void CaptionPreviewView::SetCaption(const std::wstring& speaker, const std::wstring& text, const CaptionStyle& style,
                                    const CaptionPip& pip)
{
    const bool same = (speaker == m_speaker && text == m_text && memcmp(&style, &m_style, sizeof(style)) == 0 &&
                       memcmp(&pip, &m_pip, sizeof(pip)) == 0);
    if (same)
        return;
    m_speaker = speaker;
    m_text = text;
    m_style = style;
    m_pip = pip;
    m_layerValid = false;
    Invalidate();
}

void CaptionPreviewView::SetMessage(const std::wstring& text)
{
    if (text == m_message)
        return;
    m_message = text;
    Invalidate();
}

void CaptionPreviewView::SetAudioOnly(bool on)
{
    if (on == m_audioOnly)
        return;
    m_audioOnly = on;
    Invalidate();
}

bool CaptionPreviewView::IsAudioOnly() const
{
    return m_audioOnly;
}

void CaptionPreviewView::ensureLayer()
{
    if (m_layerValid)
        return;
    m_layerValid = true;
    memset(&m_layerBounds, 0, sizeof(m_layerBounds));
    const int w = m_videoRect.right - m_videoRect.left;
    const int h = m_videoRect.bottom - m_videoRect.top;
    if (m_text.empty() || w <= 0 || h <= 0)
        return;
    m_layer.assign(static_cast<size_t>(w) * h, 0);
    CaptionPaintParams p;
    p.m_style = m_style;
    p.m_width = w;
    p.m_height = h;
    p.m_refHeight = h;
    p.m_hasAvoid = m_pip.m_present;
    p.m_avoid.left = static_cast<LONG>(m_pip.m_left * w);
    p.m_avoid.top = static_cast<LONG>(m_pip.m_top * h);
    p.m_avoid.right = static_cast<LONG>(m_pip.m_right * w);
    p.m_avoid.bottom = static_cast<LONG>(m_pip.m_bottom * h);
    std::vector<CaptionPaintLine> lines(1);
    lines[0].m_speaker = m_speaker;
    lines[0].m_text = m_text;
    lines[0].m_partial = false;
    RECT bounds;
    if (!CaptionRenderer::Paint(lines, p, &m_layer[0], &bounds))
        return;
    if (bounds.left < 0)
        bounds.left = 0;
    if (bounds.top < 0)
        bounds.top = 0;
    if (bounds.right > w)
        bounds.right = w;
    if (bounds.bottom > h)
        bounds.bottom = h;
    m_layerBounds = bounds;
}

void CaptionPreviewView::OnPaint(HDC hdc, const RECT& rcDirty)
{
    (void)rcDirty;
    const RECT rc = GetRect();
    const int cw = rc.right - rc.left;
    const int ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0)
        return;
    if (m_frame.empty())
    {
        HBRUSH bg = ::CreateSolidBrush(kEmptyBg);
        ::FillRect(hdc, &rc, bg);
        ::DeleteObject(bg);
        ::SetBkMode(hdc, TRANSPARENT);
        //录音模式（14.13）：提示写在上方三分之一，下面的区域留给当前字幕；其余情况提示写在正中
        RECT messageRect = rc;
        if (m_audioOnly)
            messageRect.bottom = rc.top + ch / kAudioMessageBandDivisor;
        if (!m_message.empty())
        {
            HFONT font = makeUiFont(kMessageFontPx, FW_NORMAL);
            HGDIOBJ old = ::SelectObject(hdc, font);
            ::SetTextColor(hdc, kMessageColor);
            ::DrawTextW(hdc, m_message.c_str(), -1, &messageRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(hdc, old);
            ::DeleteObject(font);
        }
        if (m_audioOnly && !m_text.empty())
            paintAudioCaption(hdc, rc, messageRect.bottom);
        return;
    }
    //合成：帧 + 字幕图层（叠在视频矩形里）
    m_compose = m_frame;
    ensureLayer();
    const int lw = m_videoRect.right - m_videoRect.left;
    if (!m_text.empty() && m_layerBounds.right > m_layerBounds.left && lw > 0)
    {
        for (LONG y = m_layerBounds.top; y < m_layerBounds.bottom; ++y)
        {
            const LONG fy = m_videoRect.top + y;
            if (fy < 0 || fy >= m_frameH)
                continue;
            uint32_t* dst = &m_compose[static_cast<size_t>(fy) * m_frameW];
            const uint32_t* src = &m_layer[static_cast<size_t>(y) * lw];
            for (LONG x = m_layerBounds.left; x < m_layerBounds.right; ++x)
            {
                const LONG fx = m_videoRect.left + x;
                if (fx < 0 || fx >= m_frameW)
                    continue;
                const uint32_t s = src[x];
                const uint32_t a = s >> 24;
                if (a == 0)
                    continue;
                const uint32_t d = dst[fx];
                const uint32_t inv = kAlphaFull - a;
                const uint32_t r = ((s >> 16) & 0xFF) + (((d >> 16) & 0xFF) * inv + kRoundHalf) / kAlphaFull;
                const uint32_t g = ((s >> 8) & 0xFF) + (((d >> 8) & 0xFF) * inv + kRoundHalf) / kAlphaFull;
                const uint32_t b = (s & 0xFF) + ((d & 0xFF) * inv + kRoundHalf) / kAlphaFull;
                dst[fx] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_frameW;
    bmi.bmiHeader.biHeight = -m_frameH;   //负数表示自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = kBitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    //帧按控件大小取的；控件刚改变大小、新帧还没到时按比例拉伸
    ::SetStretchBltMode(hdc, HALFTONE);
    ::StretchDIBits(hdc, rc.left, rc.top, cw, ch, 0, 0, m_frameW, m_frameH, &m_compose[0], &bmi, DIB_RGB_COLORS,
                    SRCCOPY);
}

void CaptionPreviewView::paintAudioCaption(HDC hdc, const RECT& rc, LONG top)
{
    //字幕区：提示带以下、左右留白；先量出字幕的高度，连同说话人一行在字幕区里竖直居中
    RECT area = rc;
    area.top = top;
    area.left += kAudioCaptionPadX;
    area.right -= kAudioCaptionPadX;
    if (area.right <= area.left || area.bottom <= area.top)
        return;
    HFONT captionFont = makeUiFont(kAudioCaptionFontPx, FW_BOLD);
    HFONT speakerFont = makeUiFont(kAudioSpeakerFontPx, FW_NORMAL);
    HGDIOBJ old = ::SelectObject(hdc, captionFont);
    RECT measure = area;
    const UINT textFlags = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL;
    ::DrawTextW(hdc, m_text.c_str(), -1, &measure, textFlags | DT_CALCRECT);
    const int textH = measure.bottom - measure.top;
    const bool showSpeaker = m_style.m_showSpeaker && !m_speaker.empty();
    int speakerH = 0;
    if (showSpeaker)
    {
        ::SelectObject(hdc, speakerFont);
        RECT sm = area;
        ::DrawTextW(hdc, m_speaker.c_str(), -1, &sm, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
        speakerH = sm.bottom - sm.top + kAudioSpeakerGap;
    }
    const int totalH = textH + speakerH;
    LONG y = area.top + ((area.bottom - area.top) - totalH) / 2;
    if (y < area.top)
        y = area.top;
    if (showSpeaker)
    {
        ::SetTextColor(hdc, kMessageColor);
        RECT speakerRect = area;
        speakerRect.top = y;
        speakerRect.bottom = y + speakerH;
        ::DrawTextW(hdc, m_speaker.c_str(), -1, &speakerRect, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += speakerH;
    }
    ::SelectObject(hdc, captionFont);
    ::SetTextColor(hdc, kAudioCaptionColor);
    RECT textRect = area;
    textRect.top = y;
    ::DrawTextW(hdc, m_text.c_str(), -1, &textRect, textFlags);
    ::SelectObject(hdc, old);
    ::DeleteObject(captionFont);
    ::DeleteObject(speakerFont);
}

} // namespace caption
