/**
 *  登录窗与注册窗品牌文字的测量与字号适配实现，BrandTextFit.cpp。说明见 BrandTextFit.h。
 *
 *  本文件被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，不包含 stdafx.h；在 Flamingo.vcxproj
 *  里须标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "BrandTextFit.h"

#include <wchar.h>

namespace balloonauth {

SIZE MeasureBrandText(HDC hdc, LPCWSTR text, LPCWSTR fontFace, int pixelHeight, bool bold)
{
    SIZE size = { 0, 0 };
    if (hdc == NULL || text == NULL || text[0] == L'\0' || pixelHeight <= 0)
        return size;

    // 与登录窗、注册窗绘制品牌文字时的建字体参数保持一致，否则测量结果与实际绘制不符
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace);
    if (hFont == NULL)
        return size;

    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &size);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return size;
}

BrandTextFitResult FitBrandTextSize(HDC hdc, LPCWSTR text, LPCWSTR fontFace,
                                    int desiredPx, int minPx, int maxWidth, bool bold)
{
    BrandTextFitResult result;
    result.m_pixelHeight = desiredPx;
    result.m_truncated = false;

    if (text == NULL || text[0] == L'\0')
        return result;

    if (minPx > desiredPx)
        minPx = desiredPx;
    if (minPx < 1)
        minPx = 1;

    for (int px = desiredPx; px >= minPx; --px)
    {
        if (MeasureBrandText(hdc, text, fontFace, px, bold).cx <= maxWidth)
        {
            result.m_pixelHeight = px;
            return result;
        }
    }

    result.m_pixelHeight = minPx;
    result.m_truncated = true;
    return result;
}

} // namespace balloonauth
