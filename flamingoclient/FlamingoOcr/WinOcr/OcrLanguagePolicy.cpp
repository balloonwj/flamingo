/**
 *  默认识别语言的选择，说明见 OcrLanguagePolicy.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrLanguagePolicy.h"

#include <wchar.h>

namespace flamingoocr {

namespace {

//简体、繁体中文识别语言的代码前缀
const wchar_t* const kSimplifiedChinese = L"zh-Hans";
const wchar_t* const kTraditionalChinese = L"zh-Hant";

//按繁体中文识别的地区代码（台湾、香港、澳门）
const wchar_t* const kTraditionalRegions[] = { L"tw", L"hk", L"mo", L"hant" };

//ASCII 转小写
wchar_t LowerAscii(wchar_t ch)
{
    return (ch >= L'A' && ch <= L'Z') ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
}

std::wstring ToLowerAscii(const std::wstring& text)
{
    std::wstring out(text);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = LowerAscii(out[i]);
    return out;
}

} // namespace

std::wstring OcrLanguagePrefixForUi(const wchar_t* uiLanguage)
{
    if (uiLanguage == NULL || uiLanguage[0] == L'\0')
        return std::wstring();

    std::wstring lower = ToLowerAscii(uiLanguage);
    size_t dash = lower.find_first_of(L"-_");
    std::wstring primary = (dash == std::wstring::npos) ? lower : lower.substr(0, dash);
    if (primary != L"zh")
        return primary;

    //中文：按其后的地区或文字代码区分简繁
    if (dash != std::wstring::npos)
    {
        std::wstring rest = lower.substr(dash + 1);
        for (size_t i = 0; i < sizeof(kTraditionalRegions) / sizeof(kTraditionalRegions[0]); ++i)
        {
            if (rest.compare(0, wcslen(kTraditionalRegions[i]), kTraditionalRegions[i]) == 0)
                return kTraditionalChinese;
        }
    }
    return kSimplifiedChinese;
}

int FindOcrLanguageByPrefix(const std::vector<std::wstring>& tags, const std::wstring& prefix)
{
    if (prefix.empty())
        return -1;
    const std::wstring p = ToLowerAscii(prefix);
    for (size_t i = 0; i < tags.size(); ++i)
    {
        const std::wstring t = ToLowerAscii(tags[i]);
        if (t == p || (t.size() > p.size() && t.compare(0, p.size(), p) == 0 && t[p.size()] == L'-'))
            return static_cast<int>(i);
    }
    return -1;
}

bool SameLanguageTag(const std::wstring& a, const std::wstring& b)
{
    return ToLowerAscii(a) == ToLowerAscii(b);
}

} // namespace flamingoocr
