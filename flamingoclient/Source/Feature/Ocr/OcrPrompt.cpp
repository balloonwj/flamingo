/**
 *  点击识别入口时的前置判断与提示，说明见 OcrPrompt.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrPrompt.h"

#include <shellapi.h>

#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"

namespace ocr {

OcrPrompt::OcrPrompt()
    : m_askOpenSettings(false)
{
}

bool CheckOcrBeforeRecognize(const OcrHostSnapshot& snapshot, OcrPrompt& prompt)
{
    prompt = OcrPrompt();
    prompt.m_title = TR(L"图片文字识别");

    //模块还没加载好或不可用：正常情况下入口不会显示，这里只处理极少见的时序情况
    if (snapshot.m_state == ocr_host_not_started || snapshot.m_state == ocr_host_loading)
    {
        prompt.m_message = TR(L"文字识别组件正在加载，请稍后再试。");
        return false;
    }
    if (snapshot.m_state != ocr_host_ready)
    {
        prompt.m_message = TR(L"文字识别组件不可用，重新安装客户端可以恢复。");
        return false;
    }

    switch (snapshot.m_availability)
    {
    //引擎可用：直接开始识别
    case flamingoocr::ocr_available:
        return true;

    //操作系统不支持（如 Win7 没有系统自带的识别引擎）
    case flamingoocr::ocr_os_unsupported:
        prompt.m_message = TR(L"当前系统不支持文字识别，需要 Windows 10 或更高版本。");
        return false;

    //没有识别语言：提示安装语言组件；引擎给出了设置地址时询问是否打开
    case flamingoocr::ocr_no_language:
        prompt.m_message = TR(L"系统未安装文字识别所需的语言组件。请在系统设置的「语言」页面添加中文或英文，装好后重新启动客户端。");
        if (!snapshot.m_settingsUri.empty())
        {
            prompt.m_message += L"\n\n";
            prompt.m_message += TR(L"现在打开系统设置吗？");
            prompt.m_askOpenSettings = true;
            prompt.m_settingsUri = snapshot.m_settingsUri;
        }
        return false;

    //引擎初始化失败及不认识的取值
    default:
        prompt.m_message = TR(L"文字识别引擎无法启动，请稍后重试或重新安装客户端。");
        return false;
    }
}

void ShowOcrPrompt(HWND owner, const OcrPrompt& prompt)
{
    if (!prompt.m_askOpenSettings)
    {
        ::MessageBoxW(owner, prompt.m_message.c_str(), prompt.m_title.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (::MessageBoxW(owner, prompt.m_message.c_str(), prompt.m_title.c_str(), MB_YESNO | MB_ICONINFORMATION) != IDYES)
        return;

    HINSTANCE result = ::ShellExecuteW(owner, L"open", prompt.m_settingsUri.c_str(), NULL, NULL, SW_SHOWNORMAL);
    //ShellExecute 返回值不大于 32 表示失败
    const INT_PTR kShellExecuteFailureMax = 32;
    if (reinterpret_cast<INT_PTR>(result) <= kShellExecuteFailureMax)
        LOG_WARNING("ocr prompt: failed to open the system settings page, code %d.", static_cast<int>(reinterpret_cast<INT_PTR>(result)));
}

} // namespace ocr
