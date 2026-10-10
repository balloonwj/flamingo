/**
 *  按类型取本机系统图标的实现，ShellTypeIcon.cpp
 *
 *  本文件同时编进主程序与 FlamingoFileShare.dll，因此不包含 stdafx.h，自己定义版本宏：SHGetStockIconInfo 与
 *  SHGetImageList 的特大 / 超大两档要求 Vista 及以上。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#ifndef WINVER
#define WINVER          0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT    0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE       0x0700
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>

#include <map>
#include <utility>

#include "ShellTypeIcon.h"

namespace shellicon {

namespace {

// 系统图像列表的四档，按图标从小到大排列（SHGetImageList 的 SHIL_* 取值）
const int kImageLists[] = { SHIL_SMALL, SHIL_LARGE, SHIL_EXTRALARGE, SHIL_JUMBO };

// 档位个数
const int kImageListCount = sizeof(kImageLists) / sizeof(kImageLists[0]);

// 下标缓存的键里，非文件类型的扩展名部分用的占位串（磁盘、文件夹与扩展名无关）
const wchar_t kNoExtKey[] = L"";

// 查询文件类型图标时拼的占位文件名主干：SHGFI_USEFILEATTRIBUTES 只看名字，不访问磁盘
const wchar_t kProbeStem[] = L"probe";

// 位图每个像素的字节数（32 位）
const int kBytesPerPixel = 4;

// 位图每个像素的位数
const WORD kBitsPerPixel = 32;

// 下标缓存：键为（条目类型, 扩展名），值为系统图像列表下标
typedef std::map<std::pair<int, std::wstring>, int> IndexCache;

// 图标句柄缓存：键为（图像列表档位, 下标），值为从该档图像列表取出的图标，ReleaseCache 时销毁
typedef std::map<std::pair<int, int>, HICON> IconCache;

IndexCache& Indexes()
{
    static IndexCache s_indexes;
    return s_indexes;
}

IconCache& Icons()
{
    static IconCache s_icons;
    return s_icons;
}

// 规范化条目类型：不认识的取值按文件处理
int NormalizeKind(int kind)
{
    if (kind == kItemDisk || kind == kItemFolder)
        return kind;
    return kItemFile;
}

// 向外壳查询图标下标（不缓存）
int QueryIndex(int kind, const std::wstring& ext)
{
    if (kind == kItemDisk)
    {
        SHSTOCKICONINFO info;
        ::ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        if (FAILED(::SHGetStockIconInfo(SIID_DRIVEFIXED, SHGSI_SYSICONINDEX, &info)))
            return -1;
        return info.iSysImageIndex;
    }

    std::wstring probe;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    if (kind == kItemFolder)
    {
        probe = kProbeStem;
        attributes = FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
        probe = std::wstring(kProbeStem) + ext;
    }
    SHFILEINFOW info;
    ::ZeroMemory(&info, sizeof(info));
    //SHGFI_USEFILEATTRIBUTES：只按名字与属性取类型图标，不访问磁盘
    const DWORD_PTR ok = ::SHGetFileInfoW(probe.c_str(), attributes, &info, sizeof(info),
                                          SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
    if (ok == 0)
        return -1;
    return info.iIcon;
}

// 取某一档系统图像列表；失败返回 NULL。返回的接口由调用方 Release。
IImageList* GetImageList(int list)
{
    IImageList* imageList = NULL;
    if (FAILED(::SHGetImageList(list, __uuidof(IImageList), reinterpret_cast<void**>(&imageList))))
        return NULL;
    return imageList;
}

// 各档图像列表的图标边长（像素），下标与 kImageLists 对应；0 表示尚未查询，-1 表示该档取不到
int* ListSizes()
{
    static int s_sizes[kImageListCount] = { 0 };
    return s_sizes;
}

// 按目标边长挑一档：不小于 px 的最小一档；都小于时取最大一档。各档边长只查询一次。
//   返回：档位（SHIL_* 取值）；一档都取不到时返回 -1。
int PickList(int px)
{
    int* sizes = ListSizes();
    int chosen = -1;
    for (int i = 0; i < kImageListCount; ++i)
    {
        if (sizes[i] == 0)
        {
            IImageList* imageList = GetImageList(kImageLists[i]);
            int cx = 0;
            int cy = 0;
            if (imageList != NULL)
            {
                imageList->GetIconSize(&cx, &cy);
                imageList->Release();
            }
            sizes[i] = (cx > 0) ? cx : -1;
        }
        if (sizes[i] < 0)
            continue;
        chosen = kImageLists[i];
        if (sizes[i] >= px)
            break;
    }
    return chosen;
}

// 取（档位, 下标）对应的图标句柄，缓存起来；失败返回 NULL
HICON GetIcon(int list, int index)
{
    const std::pair<int, int> key(list, index);
    IconCache& icons = Icons();
    IconCache::const_iterator it = icons.find(key);
    if (it != icons.end())
        return it->second;
    IImageList* imageList = GetImageList(list);
    if (imageList == NULL)
        return NULL;
    HICON icon = NULL;
    imageList->GetIcon(index, ILD_TRANSPARENT, &icon);
    imageList->Release();
    if (icon == NULL)
        return NULL;
    icons[key] = icon;
    return icon;
}

} // namespace

std::wstring ExtensionKey(const std::wstring& name)
{
    const size_t slash = name.find_last_of(L"\\/");
    const std::wstring file = (slash == std::wstring::npos) ? name : name.substr(slash + 1);
    const size_t dot = file.rfind(L'.');
    if (dot == std::wstring::npos || dot + 1 >= file.size())
        return std::wstring();
    std::wstring ext = file.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i)
    {
        if (ext[i] >= L'A' && ext[i] <= L'Z')
            ext[i] = static_cast<wchar_t>(ext[i] - L'A' + L'a');
    }
    return ext;
}

int TypeIconIndex(int kind, const std::wstring& name)
{
    const int k = NormalizeKind(kind);
    const std::wstring ext = (k == kItemFile) ? ExtensionKey(name) : std::wstring(kNoExtKey);
    const std::pair<int, std::wstring> key(k, ext);
    IndexCache& indexes = Indexes();
    IndexCache::const_iterator it = indexes.find(key);
    if (it != indexes.end())
        return it->second;
    const int index = QueryIndex(k, ext);
    //查询失败也记下来，免得每次绘制都向外壳再问一次
    indexes[key] = index;
    return index;
}

bool DrawTypeIcon(HDC hdc, int index, const RECT& rc)
{
    if (hdc == NULL || index < 0)
        return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int px = (w < h) ? w : h;
    if (px <= 0)
        return false;
    const int list = PickList(px);
    if (list < 0)
        return false;
    HICON icon = GetIcon(list, index);
    if (icon == NULL)
        return false;
    const int x = rc.left + (w - px) / 2;
    const int y = rc.top + (h - px) / 2;
    return ::DrawIconEx(hdc, x, y, icon, px, px, 0, NULL, DI_NORMAL) != FALSE;
}

bool DrawItemIcon(HDC hdc, int kind, const std::wstring& name, const RECT& rc)
{
    return DrawTypeIcon(hdc, TypeIconIndex(kind, name), rc);
}

HBITMAP CreateItemIconBitmap(int kind, const std::wstring& name, int px)
{
    if (px <= 0)
        return NULL;
    BITMAPINFO bmi;
    ::ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = px;
    //高度取负值：自上而下的位图
    bmi.bmiHeader.biHeight = -px;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = kBitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC screen = ::GetDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    if (bmp == NULL || bits == NULL)
    {
        if (bmp != NULL)
            ::DeleteObject(bmp);
        return NULL;
    }
    //全透明打底，DrawIconEx 在 32 位位图上按图标的透明度写入预乘后的颜色
    ::ZeroMemory(bits, static_cast<size_t>(px) * px * kBytesPerPixel);
    HDC mem = ::CreateCompatibleDC(NULL);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    const RECT rc = { 0, 0, px, px };
    const bool ok = DrawItemIcon(mem, kind, name, rc);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    if (!ok)
    {
        ::DeleteObject(bmp);
        return NULL;
    }
    return bmp;
}

void ReleaseCache()
{
    IconCache& icons = Icons();
    for (IconCache::iterator it = icons.begin(); it != icons.end(); ++it)
        ::DestroyIcon(it->second);
    icons.clear();
    Indexes().clear();
    int* sizes = ListSizes();
    for (int i = 0; i < kImageListCount; ++i)
        sizes[i] = 0;
}

size_t CachedTypeCount()
{
    return Indexes().size();
}

} // namespace shellicon
