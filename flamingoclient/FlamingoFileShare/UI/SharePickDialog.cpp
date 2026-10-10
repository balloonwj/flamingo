/**
 *  弹系统选择框的公共函数 SharePickItems 的实现，见 SharePickDialog.h。
 *  原先写在 ShareBrowserWindow.cpp 里，第 15 步发起共享对话框也要用，移到这里共用。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "SharePickDialog.h"

#include <shlobj.h>      //SHGetKnownFolderPath
#include <shobjidl.h>    //IFileOpenDialog
#include <knownfolders.h>

namespace fileshare {

bool SharePickItems(HWND owner, bool folders, bool multi, bool defaultDownloads, const wchar_t* title,
                    std::vector<std::wstring>& out)
{
    out.clear();
    IFileOpenDialog* dlg = NULL;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return false;
    DWORD options = 0;
    dlg->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM;
    if (folders)
        options |= FOS_PICKFOLDERS;
    if (multi && !folders)
        options |= FOS_ALLOWMULTISELECT;
    dlg->SetOptions(options);
    dlg->SetTitle(title);
    if (defaultDownloads)
    {
        PWSTR downloads = NULL;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &downloads)))
        {
            //本工程的 _WIN32_WINNT 较低，头文件不声明 SHCreateItemFromParsingName（Vista 起提供），运行时从 shell32 取
            typedef HRESULT (WINAPI* CreateItemFn)(PCWSTR, IBindCtx*, REFIID, void**);
            HMODULE shell = ::GetModuleHandleW(L"shell32.dll");
            CreateItemFn createItem = (shell != NULL)
                ? reinterpret_cast<CreateItemFn>(::GetProcAddress(shell, "SHCreateItemFromParsingName")) : NULL;
            IShellItem* folder = NULL;
            if (createItem != NULL && SUCCEEDED(createItem(downloads, NULL, IID_PPV_ARGS(&folder))))
            {
                dlg->SetFolder(folder);
                folder->Release();
            }
            ::CoTaskMemFree(downloads);
        }
    }

    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner)))
    {
        IShellItemArray* results = NULL;
        if (SUCCEEDED(dlg->GetResults(&results)))
        {
            DWORD count = 0;
            results->GetCount(&count);
            for (DWORD i = 0; i < count; ++i)
            {
                IShellItem* item = NULL;
                if (FAILED(results->GetItemAt(i, &item)))
                    continue;
                PWSTR path = NULL;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                {
                    out.push_back(path);
                    ::CoTaskMemFree(path);
                }
                item->Release();
            }
            results->Release();
            ok = !out.empty();
        }
    }
    dlg->Release();
    return ok;
}

} // namespace fileshare
