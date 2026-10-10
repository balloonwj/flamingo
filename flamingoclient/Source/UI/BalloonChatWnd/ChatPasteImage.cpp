/**
 *  ChatPasteImage.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "ChatPasteImage.h"

#include <objidl.h>     // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <vector>

#include "../../Common/Log/IULog.h"

#pragma comment(lib, "gdiplus.lib")

namespace balloonchatwnd {

namespace {

const DWORD kBitsPerDword = 32;            // DIB 每行按 32 位对齐
const DWORD kBytesPerDword = 4;            // 每个 DWORD 的字节数
const DWORD kMaxPaletteBits = 8;           // 位数不超过它时带调色板
const DWORD kBitfieldsMaskCount = 3;       // BI_BITFIELDS 且头为 BITMAPINFOHEADER 时，紧跟三个 DWORD 位掩码
const WORD kSupportedBits[] = { 1, 4, 8, 16, 24, 32 };   // 支持的每像素位数
const wchar_t* const kPngMime = L"image/png";             // GDI+ 编码器的 MIME 类型
const wchar_t* const kPasteFilePattern = L"%sflamingo_paste_%lu_%u.png";   // 临时文件名：目录、时刻、序号

// 本进程内递增的序号，避免同一毫秒里两次粘贴撞名。只在界面线程上使用
unsigned g_pasteSerial = 0;

// 找 GDI+ 的 PNG 编码器
bool FindPngEncoder(CLSID& clsid)
{
    UINT count = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0)
        return false;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, kPngMime) == 0)
        {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

bool IsSupportedBits(WORD bits)
{
    for (size_t i = 0; i < sizeof(kSupportedBits) / sizeof(kSupportedBits[0]); ++i)
    {
        if (kSupportedBits[i] == bits)
            return true;
    }
    return false;
}

} // namespace

ChatPasteKind DecideChatPaste(bool hasChatContent, bool hasText, bool hasImage)
{
    if (hasChatContent)
        return kChatPasteContent;
    if (hasText)
        return kChatPasteDefault;
    if (hasImage)
        return kChatPasteImage;
    return kChatPasteDefault;
}

bool ClipboardHasText()
{
    return ::IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE || ::IsClipboardFormatAvailable(CF_TEXT) != FALSE;
}

bool ClipboardHasImage()
{
    return ::IsClipboardFormatAvailable(CF_DIB) != FALSE || ::IsClipboardFormatAvailable(CF_DIBV5) != FALSE
           || ::IsClipboardFormatAvailable(CF_BITMAP) != FALSE;
}

bool SaveDibAsPng(const void* dib, size_t bytes, const std::wstring& path)
{
    if (dib == NULL || path.empty() || bytes < sizeof(BITMAPINFOHEADER))
        return false;
    const BITMAPINFOHEADER* bih = static_cast<const BITMAPINFOHEADER*>(dib);
    if (bih->biSize < sizeof(BITMAPINFOHEADER) || bih->biSize > bytes || bih->biWidth <= 0 || bih->biHeight == 0
        || !IsSupportedBits(bih->biBitCount))
        return false;
    if (bih->biCompression != BI_RGB && bih->biCompression != BI_BITFIELDS)
        return false;

    //像素数据前面的颜色表：调色板，或 BI_BITFIELDS 的三个位掩码（只有头是 BITMAPINFOHEADER 时才单独跟在后面）
    size_t colorTableBytes = 0;
    if (bih->biBitCount <= kMaxPaletteBits)
    {
        const DWORD entries = (bih->biClrUsed != 0) ? bih->biClrUsed : (1u << bih->biBitCount);
        colorTableBytes = static_cast<size_t>(entries) * sizeof(RGBQUAD);
    }
    else if (bih->biCompression == BI_BITFIELDS && bih->biSize == sizeof(BITMAPINFOHEADER))
    {
        colorTableBytes = kBitfieldsMaskCount * sizeof(DWORD);
    }
    const size_t height = static_cast<size_t>((bih->biHeight > 0) ? bih->biHeight : -bih->biHeight);
    const size_t stride = ((static_cast<size_t>(bih->biWidth) * bih->biBitCount + kBitsPerDword - 1) / kBitsPerDword)
                          * kBytesPerDword;
    const size_t pixelOffset = bih->biSize + colorTableBytes;
    if (bytes < pixelOffset || bytes - pixelOffset < stride * height)
        return false;

    CLSID png;
    if (!FindPngEncoder(png))
        return false;
    const BYTE* pixels = static_cast<const BYTE*>(dib) + pixelOffset;
    Gdiplus::Bitmap bitmap(reinterpret_cast<const BITMAPINFO*>(dib), const_cast<BYTE*>(pixels));
    if (bitmap.GetLastStatus() != Gdiplus::Ok)
        return false;
    if (bitmap.Save(path.c_str(), &png, NULL) != Gdiplus::Ok)
    {
        ::DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

bool ReadClipboardImageToPng(HWND owner, std::wstring& outPath)
{
    outPath.clear();
    wchar_t dir[MAX_PATH] = { 0 };
    const DWORD n = ::GetTempPathW(MAX_PATH, dir);
    if (n == 0 || n >= MAX_PATH)
    {
        LOG_WARNING("chat paste image: no temp directory.");
        return false;
    }
    wchar_t path[MAX_PATH] = { 0 };
    ::swprintf_s(path, MAX_PATH, kPasteFilePattern, dir, ::GetTickCount(), ++g_pasteSerial);

    if (!::OpenClipboard(owner))
    {
        LOG_WARNING("chat paste image: OpenClipboard failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        return false;
    }
    bool saved = false;
    HANDLE data = ::GetClipboardData(CF_DIB);
    if (data != NULL)
    {
        const void* dib = ::GlobalLock(data);
        const SIZE_T bytes = ::GlobalSize(data);
        if (dib != NULL)
        {
            saved = SaveDibAsPng(dib, static_cast<size_t>(bytes), path);
            ::GlobalUnlock(data);
        }
    }
    ::CloseClipboard();
    if (!saved)
    {
        LOG_WARNING("chat paste image: no usable CF_DIB on the clipboard (data=%p).", data);
        return false;
    }
    outPath = path;
    LOG_INFO("chat paste image: clipboard image saved to a temp png.");
    return true;
}

} // namespace balloonchatwnd
