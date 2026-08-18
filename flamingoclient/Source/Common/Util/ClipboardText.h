/**
 *  ClipboardText.h
 *
 *  极小工具:把一段 Unicode 文本写入系统剪贴板(CF_UNICODETEXT)。仅一个 inline
 *  函数、纯 header,无需加进工程文件。供"点击账号复制到剪贴板"等场景共用
 *  (个人信息窗 CBalloonProfileDlg、联系人资料窗 CBalloonContactWindow)。
 *
 *  本工程为 _UNICODE,TCHAR == WCHAR == UTF-16,正好对应 CF_UNICODETEXT。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <windows.h>
#include <tchar.h>

namespace flamingo {

/**
 *  把 text 写入系统剪贴板(CF_UNICODETEXT)。
 *    text  待复制文本;NULL 或空串时不做任何事并返 false。
 *  返回:成功 true;打开剪贴板 / 分配内存 / 写入失败返 false。
 *  说明:成功写入后内存块所有权转交系统,本函数不再释放;失败路径自行释放,
 *  不泄漏。剪贴板归属当前任务(OpenClipboard(NULL))。
 */
inline bool CopyTextToClipboard(LPCTSTR text)
{
    if (text == NULL || text[0] == _T('\0'))
    {
        return false;
    }
    if (!::OpenClipboard(NULL))
    {
        return false;
    }

    bool ok = false;
    ::EmptyClipboard();

    const size_t chars = ::_tcslen(text) + 1;          // 含结尾 '\0'
    const size_t bytes = chars * sizeof(TCHAR);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != NULL)
    {
        void* dst = ::GlobalLock(hMem);
        if (dst != NULL)
        {
            ::CopyMemory(dst, text, bytes);
            ::GlobalUnlock(hMem);
            if (::SetClipboardData(CF_UNICODETEXT, hMem) != NULL)
            {
                ok = true;            // 所有权已转交系统
            }
            else
            {
                ::GlobalFree(hMem);   // 写入失败,自行释放
            }
        }
        else
        {
            ::GlobalFree(hMem);
        }
    }

    ::CloseClipboard();
    return ok;
}

} // namespace flamingo
