/**
 *  屏幕共享「选择要共享的内容」窗口的纯逻辑规则，SharePickerPolicy.cpp
 *  说明见 SharePickerPolicy.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
//rand_s 只有在包含 <stdlib.h> 之前定义了 _CRT_RAND_S 才会声明，所以这一行必须放在所有 #include 之前
#define _CRT_RAND_S
#include "SharePickerPolicy.h"

#include <stdlib.h>

namespace screenshare {

namespace {

//6 位十进制数的模：随机数对它取余得到末 6 位
const uint32_t kSharePasswordModulus = 1000000;

bool isAsciiDigit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

} // namespace

bool IsValidSharePassword(const std::wstring& text)
{
    if (text.size() != kSharePasswordLength)
        return false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (!isAsciiDigit(text[i]))
            return false;
    }
    return true;
}

std::wstring FilterSharePasswordInput(const std::wstring& text)
{
    std::wstring out;
    for (size_t i = 0; i < text.size() && out.size() < kSharePasswordLength; ++i)
    {
        if (isAsciiDigit(text[i]))
            out.push_back(text[i]);
    }
    return out;
}

std::wstring MakeSharePassword(uint32_t randomValue)
{
    uint32_t value = randomValue % kSharePasswordModulus;
    //从个位起逐位写入，不足 6 位的高位自然留为 '0'
    std::wstring out(kSharePasswordLength, L'0');
    for (size_t i = 0; i < kSharePasswordLength; ++i)
    {
        out[kSharePasswordLength - 1 - i] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    }
    return out;
}

std::wstring GenerateSharePassword()
{
    unsigned int value = 0;
    if (rand_s(&value) != 0)
    {
        //rand_s 失败的情况极少见（系统的加密随机数服务不可用），退回用高精度计数器的低位
        LARGE_INTEGER counter;
        ::QueryPerformanceCounter(&counter);
        value = static_cast<unsigned int>(counter.LowPart ^ ::GetTickCount());
    }
    return MakeSharePassword(static_cast<uint32_t>(value));
}

std::string SharePasswordToUtf8(const std::wstring& password)
{
    std::string out;
    out.reserve(password.size());
    for (size_t i = 0; i < password.size(); ++i)
    {
        //合法密码只含 ASCII 数字；万一混进非 ASCII 字符，换成 '?'，服务端校验时会按格式错误拒绝
        const wchar_t c = password[i];
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

bool SharePickerOffersPassword(int mode)
{
    return mode == share_picker_start;
}

bool SharePickerOffersForced(int mode)
{
    return mode == share_picker_start;
}

bool SharePickerOffersRecordBlock(int mode)
{
    return mode == share_picker_start;
}

bool SharePickerOffersInfo(int mode)
{
    return mode == share_picker_start || mode == share_picker_start_in_call;
}

bool SharePickerOffersCaption(int mode)
{
    return mode == share_picker_record || mode == share_picker_start || mode == share_picker_start_in_call;
}

int SharePickerConfirmKindFor(int mode)
{
    if (mode == share_picker_record)
        return share_confirm_record;
    //本人已在共享时是换内容；经主持人同意、还没开始共享时（share_picker_switch_to_start）是开始共享
    if (mode == share_picker_switch)
        return share_confirm_switch;
    return share_confirm_start;
}

bool SharePickerIsSwitchLayout(int mode)
{
    return mode == share_picker_switch || mode == share_picker_switch_to_start;
}

bool SharePickerCanSubmit(bool hasSelection, bool passwordOn, const std::wstring& password)
{
    if (!hasSelection)
        return false;
    return !passwordOn || IsValidSharePassword(password);
}

bool SharePickerCanRecordAudioOnly(bool mic, bool system)
{
    return mic || system;
}

ShareSourceKey::ShareSourceKey()
    : m_monitor(false)
    , m_adapter(0)
    , m_output(0)
    , m_hwnd(NULL)
{
}

int FindShareSourceAfterRefresh(const ShareSourceKey& previous, const std::vector<ShareSourceKey>& items)
{
    //句柄为 NULL 的窗口项无从辨认，不去匹配
    if (!previous.m_monitor && previous.m_hwnd == NULL)
        return -1;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ShareSourceKey& item = items[i];
        if (item.m_monitor != previous.m_monitor)
            continue;
        if (previous.m_monitor)
        {
            if (item.m_adapter == previous.m_adapter && item.m_output == previous.m_output)
                return static_cast<int>(i);
        }
        else if (item.m_hwnd == previous.m_hwnd)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace screenshare
