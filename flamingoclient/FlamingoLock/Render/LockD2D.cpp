/**
 *  LockD2D.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "LockD2D.h"

#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：日期与星期的写法随界面语言

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace flamingolock {

namespace {

const float kColorChannelMax = 255.0f;          // 颜色分量的最大值
const uint64_t kFileTimeTicksPerMs = 10000;     // FILETIME 以 100 纳秒为单位，每毫秒的刻数
//1601-01-01 到 1970-01-01 之间的 100 纳秒刻数（FILETIME 与 Unix 时间的起点差）
const uint64_t kEpochDiffTicks = 116444736000000000ULL;
const float kHugeLayoutWidth = 100000.0f;       // 量文字宽度时给的布局宽度上限（足够大，不会折行）

// 星期的写法：下标 0 为星期日，与 SYSTEMTIME::wDayOfWeek 一致；到使用处再 TR
const wchar_t* const kWeekdays[] =
{
    TR_NOOP(L"星期日"), TR_NOOP(L"星期一"), TR_NOOP(L"星期二"), TR_NOOP(L"星期三"),
    TR_NOOP(L"星期四"), TR_NOOP(L"星期五"), TR_NOOP(L"星期六"),
};
const int kWeekdayCount = 7;   // 一周的天数

float Clamp01(float t)
{
    return (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
}

} // namespace

D2D1_COLOR_F ColorRgb(uint32_t rgb, float alpha)
{
    D2D1_COLOR_F c;
    c.r = static_cast<float>((rgb >> 16) & 0xFF) / kColorChannelMax;
    c.g = static_cast<float>((rgb >> 8) & 0xFF) / kColorChannelMax;
    c.b = static_cast<float>(rgb & 0xFF) / kColorChannelMax;
    c.a = alpha;
    return c;
}

D2D1_COLOR_F MixColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t)
{
    const float k = Clamp01(t);
    D2D1_COLOR_F c;
    c.r = a.r + (b.r - a.r) * k;
    c.g = a.g + (b.g - a.g) * k;
    c.b = a.b + (b.b - a.b) * k;
    c.a = a.a + (b.a - a.a) * k;
    return c;
}

D2D1_RECT_F ToRectF(const RECT& rc)
{
    return D2D1::RectF(static_cast<float>(rc.left), static_cast<float>(rc.top),
                       static_cast<float>(rc.right), static_cast<float>(rc.bottom));
}

std::wstring PickFontFamily(IDWriteFactory* dwrite, const wchar_t* const* candidates, int count)
{
    if (count <= 0)
    {
        return std::wstring();
    }
    CComPtr<IDWriteFontCollection> fonts;
    if (dwrite != NULL && SUCCEEDED(dwrite->GetSystemFontCollection(&fonts, FALSE)) && fonts != NULL)
    {
        for (int i = 0; i < count; ++i)
        {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(candidates[i], &index, &exists)) && exists)
            {
                return candidates[i];
            }
        }
    }
    return candidates[count - 1];
}

CComPtr<IDWriteTextFormat> MakeTextFormat(IDWriteFactory* dwrite, const std::wstring& family, float size,
                                          DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT hAlign,
                                          DWRITE_PARAGRAPH_ALIGNMENT vAlign)
{
    CComPtr<IDWriteTextFormat> format;
    if (dwrite == NULL || size <= 0.0f)
    {
        return format;
    }
    if (FAILED(dwrite->CreateTextFormat(family.c_str(), NULL, weight, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &format)) || format == NULL)
    {
        return CComPtr<IDWriteTextFormat>();
    }
    format->SetTextAlignment(hAlign);
    format->SetParagraphAlignment(vAlign);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

float MeasureTextWidth(IDWriteFactory* dwrite, IDWriteTextFormat* format, const std::wstring& text)
{
    if (dwrite == NULL || format == NULL || text.empty())
    {
        return 0.0f;
    }
    CComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
                                        kHugeLayoutWidth, format->GetFontSize() * 2.0f, &layout)) || layout == NULL)
    {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics;
    if (FAILED(layout->GetMetrics(&metrics)))
    {
        return 0.0f;
    }
    return metrics.widthIncludingTrailingWhitespace;
}

void DrawTextIn(ID2D1RenderTarget* rt, IDWriteTextFormat* format, const std::wstring& text,
                const D2D1_RECT_F& rect, ID2D1Brush* brush)
{
    if (rt == NULL || format == NULL || brush == NULL || text.empty())
    {
        return;
    }
    rt->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush,
                 D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

uint64_t NowWallMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (u.QuadPart - kEpochDiffTicks) / kFileTimeTicksPerMs;
}

SYSTEMTIME LocalTimeFromWallMs(uint64_t wallMs)
{
    const uint64_t ms = (wallMs == 0) ? NowWallMs() : wallMs;
    ULARGE_INTEGER u;
    u.QuadPart = ms * kFileTimeTicksPerMs + kEpochDiffTicks;
    FILETIME utc;
    utc.dwLowDateTime = u.LowPart;
    utc.dwHighDateTime = u.HighPart;
    FILETIME local;
    SYSTEMTIME st;
    ::ZeroMemory(&st, sizeof(st));
    if (::FileTimeToLocalFileTime(&utc, &local))
    {
        ::FileTimeToSystemTime(&local, &st);
    }
    return st;
}

void FormatClockTexts(std::wstring& timeText, std::wstring& dateText, uint64_t wallMs)
{
    const SYSTEMTIME st = LocalTimeFromWallMs(wallMs);
    wchar_t buf[16];
    swprintf_s(buf, L"%02u:%02u", static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute));
    timeText = buf;
    const int wd = (st.wDayOfWeek < kWeekdayCount) ? st.wDayOfWeek : 0;
    dateText = TRF(L"{0}月{1}日", static_cast<int>(st.wMonth), static_cast<int>(st.wDay));
    dateText += L"  ";
    dateText += TR(kWeekdays[wd]);
}

} // namespace flamingolock
