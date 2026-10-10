/**
 *  AutoReplySync.cpp
 *
 *  「离开 / 忙碌自动回复」迁移到服务端的纯逻辑，说明见 AutoReplySync.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "AutoReplySync.h"

#include <windows.h>

#include "AutoReplyPolicy.h"               // kDefaultAwayReplyText / kDefaultBusyReplyText
#include "../../Common/I18n/I18n.h"        // TR：默认文字按界面语言翻译

namespace balloonmain {

namespace {

//宽字符转 UTF-8
std::string ToUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0)
        return std::string();
    std::string s((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

//UTF-8 转宽字符
std::wstring FromUtf8(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

//去掉首尾空白之后是否为空
bool IsBlank(const std::wstring& s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        const wchar_t c = s[i];
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n' && c != 0x3000)
            return false;
    }
    return true;
}

} // anonymous namespace

net::AutoReplyWire BuildAutoReplyWire(bool awayOn, const std::wstring& awayText, bool busyOn, const std::wstring& busyText)
{
    net::AutoReplyWire w;
    w.m_awayOn = awayOn;
    w.m_awayDefault = IsBlank(awayText);
    w.m_awayText = ToUtf8(w.m_awayDefault ? std::wstring(TR(kDefaultAwayReplyText)) : awayText);
    w.m_busyOn = busyOn;
    w.m_busyDefault = IsBlank(busyText);
    w.m_busyText = ToUtf8(w.m_busyDefault ? std::wstring(TR(kDefaultBusyReplyText)) : busyText);
    return w;
}

bool IsDefaultAutoReply(bool awayOn, const std::wstring& awayText, bool busyOn, const std::wstring& busyText)
{
    return !awayOn && !busyOn && IsBlank(awayText) && IsBlank(busyText);
}

void ApplyServerAutoReply(const net::AutoReplyWire& w, bool& awayOn, std::wstring& awayText, bool& busyOn,
                          std::wstring& busyText)
{
    awayOn = w.m_awayOn;
    awayText = w.m_awayDefault ? std::wstring() : FromUtf8(w.m_awayText);
    busyOn = w.m_busyOn;
    busyText = w.m_busyDefault ? std::wstring() : FromUtf8(w.m_busyText);
}

bool ShouldClientAutoReply(bool botFeatureOn)
{
    return !botFeatureOn;
}

} // namespace balloonmain
