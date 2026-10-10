/**
 *  警告框的类型、颜色与标题，说明见 MdAlert.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "MdAlert.h"

#include <windows.h>
#include <stdio.h>

namespace flamingomd {

namespace {

//各类型的类型名（大写），下标为类型减一
const wchar_t* const kKeywords[kMdAlertTypeCount] = { L"NOTE", L"TIP", L"IMPORTANT", L"WARNING", L"CAUTION" };

//各类型的主色（竖条、图标、标题），参照 GitHub 浅色主题，下标为类型减一
const COLORREF kColors[kMdAlertTypeCount] =
{
    RGB(0x09, 0x69, 0xDA),   //NOTE：蓝
    RGB(0x1A, 0x7F, 0x37),   //TIP：绿
    RGB(0x82, 0x50, 0xDF),   //IMPORTANT：紫
    RGB(0x9A, 0x67, 0x00),   //WARNING：琥珀
    RGB(0xCF, 0x22, 0x2E),   //CAUTION：红
};

//各类型的底色：主色在白底上的浅色，下标为类型减一
const COLORREF kBackColors[kMdAlertTypeCount] =
{
    RGB(0xF0, 0xF6, 0xFD),   //NOTE
    RGB(0xEE, 0xF7, 0xF0),   //TIP
    RGB(0xF5, 0xF1, 0xFC),   //IMPORTANT
    RGB(0xFC, 0xF6, 0xE8),   //WARNING
    RGB(0xFD, 0xF0, 0xF0),   //CAUTION
};

//越界时用的颜色：灰色竖条、浅灰底色
const COLORREF kFallbackColor = RGB(0x8A, 0x8A, 0x8A);
const COLORREF kFallbackBackColor = RGB(0xF6, 0xF6, 0xF6);

//「RRGGBB」加结尾 0 的缓冲区大小
const size_t kHexColorBufferSize = 8;

//宿主设置的标题（空串表示未设置，显示类型名），下标为类型减一。宿主在初始化后、排版之前设置一次
std::wstring g_titles[kMdAlertTypeCount];

//类型是否有效
bool ValidType(int type)
{
    return type >= md_alert_note && type <= md_alert_caution;
}

} // namespace

int MdAlertTypeFromKeyword(const std::wstring& keyword)
{
    for (int i = 0; i < kMdAlertTypeCount; ++i)
    {
        if (_wcsicmp(keyword.c_str(), kKeywords[i]) == 0)
            return i + 1;
    }
    return md_alert_none;
}

std::wstring MdAlertKeyword(int type)
{
    return ValidType(type) ? std::wstring(kKeywords[type - 1]) : std::wstring();
}

void SetMdAlertTitles(const wchar_t* const* titles, uint32_t count)
{
    for (int i = 0; i < kMdAlertTypeCount; ++i)
    {
        const bool given = (titles != NULL && static_cast<uint32_t>(i) < count && titles[i] != NULL);
        g_titles[i] = given ? std::wstring(titles[i]) : std::wstring();
    }
}

std::wstring MdAlertTitle(int type)
{
    if (!ValidType(type))
        return std::wstring();
    return g_titles[type - 1].empty() ? std::wstring(kKeywords[type - 1]) : g_titles[type - 1];
}

uint32_t MdAlertColor(int type)
{
    return static_cast<uint32_t>(ValidType(type) ? kColors[type - 1] : kFallbackColor);
}

uint32_t MdAlertBackColor(int type)
{
    return static_cast<uint32_t>(ValidType(type) ? kBackColors[type - 1] : kFallbackBackColor);
}

std::string MdAlertColorHex(uint32_t color)
{
    char buf[kHexColorBufferSize] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return std::string(buf);
}

} // namespace flamingomd
