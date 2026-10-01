/**
 *  聊天内容剪贴板读写的实现，ChatClipboardIo.cpp
 *
 *  本 .cpp 被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用（聊天气泡在两个工程里都会
 *  编译，它要调用本文件），不 include stdafx.h（在 Flamingo.vcxproj 里
 *  PrecompiledHeader=NotUsing），自行引入 ATL 的 CString 与 GDI+。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <gdiplus.h>
#include <string.h>

#include "ChatClipboardIo.h"
#include "../../Common/Util/ClipboardImage.h"   // CreateClipboardBitmapFromFile（图片的位图副本）

namespace balloonchatwnd {

HGLOBAL AllocGlobalCopy(const void* data, size_t bytes)
{
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem == NULL)
    {
        return NULL;
    }
    void* dst = ::GlobalLock(hMem);
    if (dst == NULL)
    {
        ::GlobalFree(hMem);
        return NULL;
    }
    if (bytes > 0)
    {
        ::CopyMemory(dst, data, bytes);
    }
    ::GlobalUnlock(hMem);
    return hMem;
}

namespace {

// 把一块全局内存交给剪贴板。成功后所有权归剪贴板；失败时自行释放。
bool SetClipboardGlobal(UINT format, HGLOBAL hMem)
{
    if (hMem == NULL)
    {
        return false;
    }
    if (::SetClipboardData(format, hMem) == NULL)
    {
        ::GlobalFree(hMem);
        return false;
    }
    return true;
}

// 片段序列是否恰好只有一张图片（此时额外放一份位图，方便粘到其它程序）。
bool IsSingleImage(const std::vector<ChatClipSegment>& segs)
{
    return segs.size() == 1 && segs[0].m_kind == kChatClipImage;
}

} // anonymous namespace

UINT GetChatClipFormat()
{
    //同名格式在系统里只注册一次，重复注册拿到的是同一个编号；这里再缓存一下省去系统调用。
    static UINT s_format = 0;
    if (s_format == 0)
    {
        s_format = ::RegisterClipboardFormatW(kChatClipFormatName);
    }
    return s_format;
}

bool IsChatContentOnClipboard()
{
    const UINT format = GetChatClipFormat();
    return format != 0 && ::IsClipboardFormatAvailable(format) != FALSE;
}

bool WriteChatContentToClipboard(HWND owner, const std::vector<ChatClipSegment>& segs)
{
    if (segs.empty())
    {
        return false;
    }

    const std::wstring plain = ClipSegmentsToPlainText(segs);
    const std::string  rich  = SerializeClipSegments(segs);

    //位图在打开剪贴板之前生成：解码大图要一点时间，不该在占着剪贴板的时候做。
    HBITMAP hBmp = NULL;
    if (IsSingleImage(segs))
    {
        hBmp = flamingo::CreateClipboardBitmapFromFile(segs[0].m_imagePath.c_str());
    }

    if (!::OpenClipboard(owner))
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return false;
    }
    ::EmptyClipboard();

    //纯文字：连同结尾的 0 一起拷。
    const bool textOk = SetClipboardGlobal(
        CF_UNICODETEXT, AllocGlobalCopy(plain.c_str(), (plain.size() + 1) * sizeof(wchar_t)));

    //Flamingo 自己的格式：UTF-8 文本，同样带上结尾的 0，读的一方以此确定长度。
    const UINT format = GetChatClipFormat();
    if (format != 0)
    {
        SetClipboardGlobal(format, AllocGlobalCopy(rich.c_str(), rich.size() + 1));
    }

    //位图：写入成功后归剪贴板，失败时自己释放。
    if (hBmp != NULL && ::SetClipboardData(CF_BITMAP, hBmp) == NULL)
    {
        ::DeleteObject(hBmp);
    }

    ::CloseClipboard();
    return textOk;
}

bool ReadChatContentFromClipboard(HWND owner, std::vector<ChatClipSegment>& out)
{
    out.clear();
    if (!IsChatContentOnClipboard())
    {
        return false;
    }
    if (!::OpenClipboard(owner))
    {
        return false;
    }

    std::string data;
    HANDLE hData = ::GetClipboardData(GetChatClipFormat());
    if (hData != NULL)
    {
        const char* src = (const char*)::GlobalLock(hData);
        if (src != NULL)
        {
            //只在这块内存的范围内找结尾的 0，数据被别的程序改坏了也不会越界读。
            const size_t size = ::GlobalSize(hData);
            data.assign(src, ::strnlen(src, size));
            ::GlobalUnlock(hData);
        }
    }
    ::CloseClipboard();

    return ParseClipSegments(data, out);
}

} // namespace balloonchatwnd
