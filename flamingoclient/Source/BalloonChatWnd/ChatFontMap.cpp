/**
 *  ChatFontMap.cpp —— 聊天字体翻译的纯逻辑。
 *
 *  把 net::TextFont（收发双方共享的字体设置）翻译成 Win32 LOGFONT + 文字色。
 *  刻意与 ini 读写、全局状态（g_szHomePath 等）、具体控件解耦，单独成一个无依赖
 *  的翻译单元：既供 ApplyChatFontToEdit 的各重载共用，也让该逻辑可被单元测试
 *  （FlamingoUnit）直接覆盖而不必把 ini / 会话全局拖进测试 exe。
 *
 *  LOGFONT 各字段严格对齐 DuiEditHost::SetCtlFont 内部 CreateFont 的取值，保证
 *  输入框从 DuiEditHost 迁移到 DuiRichEditHost 后字体渲染视觉一致。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "ChatFontUtil.h"

namespace balloonchatwnd {

bool ChatFontToLogFont(const net::TextFont& font, LOGFONT& outLf, COLORREF& outColor)
{
    // 未设过字体：不填出参，交由调用方保留默认外观。
    if (!font.m_valid)
    {
        return false;
    }

    // 字号像素。<=0 兜底为 14，与 DuiEditHost::SetCtlFont 的兜底一致。
    int sizePx = font.m_size;
    if (sizePx <= 0)
    {
        sizePx = 14;
    }

    ::ZeroMemory(&outLf, sizeof(outLf));
    // 负字高 = 按像素指定字符高度，与 DuiEditHost 历史的 CreateFont(-sizePx,...) 对齐。
    outLf.lfHeight        = -sizePx;
    outLf.lfWeight        = font.m_bold      ? FW_BOLD : FW_NORMAL;
    outLf.lfItalic        = font.m_italic    ? TRUE : FALSE;
    outLf.lfUnderline     = font.m_underline ? TRUE : FALSE;
    outLf.lfStrikeOut     = font.m_strike    ? TRUE : FALSE;
    outLf.lfCharSet       = DEFAULT_CHARSET;
    outLf.lfOutPrecision  = OUT_TT_PRECIS;
    outLf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    outLf.lfQuality       = CLEARTYPE_QUALITY;
    outLf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    // 字体族固定微软雅黑（与发送 / 历史口径一致；TextFont 只承载可变属性）。
    ::lstrcpyn(outLf.lfFaceName, _T("Microsoft YaHei"), LF_FACESIZE);

    // m_colorRgb 是 0xRRGGBB（R 在高位），转成 Win32 COLORREF。
    outColor = RGB((font.m_colorRgb >> 16) & 0xFF,
                   (font.m_colorRgb >> 8) & 0xFF,
                   font.m_colorRgb & 0xFF);
    return true;
}

} // namespace balloonchatwnd
