/**
 *  文件消息视觉共用件的实现，ChatFileVisual.cpp
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"
#include "ChatFileVisual.h"

namespace balloonchatwnd {

COLORREF FileTypeColor(const CString& type)
{
    if (type == _T("pdf"))  { return RGB(0xE5, 0x39, 0x35); }
    if (type == _T("xlsx")) { return RGB(0x22, 0xA5, 0x65); }
    if (type == _T("docx")) { return RGB(0x15, 0x65, 0xC0); }
    if (type == _T("fig"))  { return RGB(0xA6, 0x5E, 0xFF); }
    return RGB(0x66, 0x66, 0x66);
}

} // namespace balloonchatwnd
