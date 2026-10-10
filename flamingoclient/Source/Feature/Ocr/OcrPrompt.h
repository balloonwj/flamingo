/**
 *  点击「图片文字识别」入口时的前置判断与提示（docs/图片文字识别方案.md 第 5.2、8.4 节）。
 *
 *  入口只在识别模块可用（COcrModuleHost::CanShowEntry）时显示；引擎能否识别另看可用性：
 *    - 可用 → 直接开始识别；
 *    - 系统不支持（Win7）→ 提示需要 Windows 10 或更高版本；
 *    - 没有识别语言 → 提示安装语言组件，并询问是否打开系统设置；
 *    - 引擎初始化失败 → 提示稍后重试或重新安装客户端。
 *
 *  CheckOcrBeforeRecognize 是纯函数（配单元测试 OCRH5）；ShowOcrPrompt 弹系统提示框，只在界面线程调用。
 *
 *  典型用法：
 *
 *      ocr::OcrPrompt prompt;
 *      if (!ocr::CheckOcrBeforeRecognize(ocr::COcrModuleHost::GetInstance().GetSnapshot(), prompt))
 *      {
 *          ocr::ShowOcrPrompt(m_hWnd, prompt);
 *          return;
 *      }
 *      ...开始识别...
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include <string>

#include "OcrModuleHost.h"

namespace ocr {

/**
 *  一条提示。
 */
struct OcrPrompt
{
    std::wstring m_title;             //提示框标题
    std::wstring m_message;           //提示正文
    bool         m_askOpenSettings;   //是否询问「打开系统设置」（提示框为「是 / 否」两个按钮）
    std::wstring m_settingsUri;       //用户选「是」时打开的地址；m_askOpenSettings 为 false 时为空

    OcrPrompt();
};

/**
 *  点击入口后的前置判断。
 *  @param snapshot 识别模块宿主的状态快照
 *  @param prompt   [出参] 返回 false 时填好要显示的提示
 *  @return 可以直接开始识别时返回 true
 */
bool CheckOcrBeforeRecognize(const OcrHostSnapshot& snapshot, OcrPrompt& prompt);

/**
 *  显示提示框；需要时在用户确认后打开系统设置。只在界面线程调用，模态，返回前阻塞。
 *  @param owner  提示框的所有者窗口，可为 NULL
 *  @param prompt 要显示的提示
 */
void ShowOcrPrompt(HWND owner, const OcrPrompt& prompt);

} // namespace ocr
