/**
 *  「图片文字识别」发起识别的实现，见 OcrEntry.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "OcrEntry.h"

#include "OcrResultWindow.h"
#include "../../Feature/Ocr/OcrLauncher.h"
#include "../../Feature/Ocr/OcrModuleHost.h"
#include "../../Feature/Ocr/OcrPrompt.h"
#include "../../Common/Log/IULog.h"

namespace balloonocr {

namespace {

//前置判断：可以识别时返回 true；否则弹提示并返回 false
bool PassOrPrompt(HWND owner)
{
    ocr::OcrPrompt prompt;
    if (ocr::CheckOcrBeforeRecognize(ocr::COcrModuleHost::GetInstance().GetSnapshot(), prompt))
        return true;
    LOG_INFO("ocr entry: recognition not started, showing a prompt (ask settings=%d).", prompt.m_askOpenSettings ? 1 : 0);
    ocr::ShowOcrPrompt(owner, prompt);
    return false;
}

//识别图片文件：前置判断通过后打开结果窗口
bool LaunchFile(HWND owner, const std::wstring& path, int quarterTurns)
{
    if (path.empty() || !PassOrPrompt(owner))
        return false;
    return COcrResultWindow::GetInstance().OpenForFile(owner, path, quarterTurns);
}

//识别内存图像：前置判断通过后打开结果窗口
bool LaunchImage(HWND owner, const ocr::OcrBgraImage& image)
{
    if (!PassOrPrompt(owner))
        return false;
    return COcrResultWindow::GetInstance().OpenForImage(owner, image);
}

} // anonymous namespace

void RegisterOcrLaunchers()
{
    ocr::SetOcrLaunchers(LaunchFile, LaunchImage);
}

bool LaunchOcrWindow(HWND owner)
{
    if (!PassOrPrompt(owner))
        return false;
    return COcrResultWindow::GetInstance().OpenEmpty(owner);
}

} // namespace balloonocr
