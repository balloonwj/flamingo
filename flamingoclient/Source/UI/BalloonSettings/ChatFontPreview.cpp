/**
 *  ChatFontPreview.cpp
 *
 *  系统设置「默认聊天字体」预览的实现，说明见 ChatFontPreview.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 自带 ATL/WTL 前置依赖，与 FlamingoUnit
 *  测试工程逐字共用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "ChatFontPreview.h"
#include "../BalloonChatWnd/ChatFontUtil.h"   // ChatFontToLogFont：与聊天输入框同一套字体翻译
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

namespace balloonsettings {

const TCHAR* const kChatFontPreviewText = TR_NOOP(L"Aa 字体预览");
const TCHAR* const kChatFontUnsetText   = TR_NOOP(L"未设置");

void ApplyChatFontPreview(balloonwjui::DuiLabel* label, const net::TextFont& font, HFONT& ioFont)
{
    // 设置过：用与聊天输入框相同的翻译规则建字体，预览与实际输入的外观一致
    HFONT newFont = NULL;
    LOGFONT lf;
    COLORREF color = kChatFontUnsetColor;
    if (balloonchatwnd::ChatFontToLogFont(font, lf, color))
    {
        newFont = ::CreateFontIndirect(&lf);
    }

    if (label != NULL)
    {
        if (newFont != NULL)
        {
            label->SetText(TR(kChatFontPreviewText));
            label->SetFont(newFont);
            label->SetTextColor(color);
        }
        else
        {
            // 未设置（或字体建不出来）：退回标签默认字体，灰字提示未设置
            label->SetText(TR(kChatFontUnsetText));
            label->SetFont(NULL);
            label->SetTextColor(kChatFontUnsetColor);
        }
    }

    // 标签已换用新字体（或默认字体），旧句柄不再被引用，可以释放
    if (ioFont != NULL)
    {
        ::DeleteObject(ioFont);
    }
    ioFont = newFont;
}

} // namespace balloonsettings
