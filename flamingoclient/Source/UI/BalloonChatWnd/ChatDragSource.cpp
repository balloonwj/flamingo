/**
 *  从聊天记录里往外拖内容的实现，ChatDragSource.cpp
 *
 *  本 .cpp 被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，不 include stdafx.h（在
 *  Flamingo.vcxproj 里 PrecompiledHeader=NotUsing），自行引入 ATL 的 CString。
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
#include <shlobj.h>    // SHCreateDataObject / DROPFILES / CFSTR_PREFERREDDROPEFFECT
#include <stdlib.h>    // abs

#include "ChatDragSource.h"
#include "ChatClipboardIo.h"   // AllocGlobalCopy / GetChatClipFormat
#include "ChatMockData.h"      // ChatMsgKind

namespace balloonchatwnd {

namespace {

// FORMATETC 里"不分页，取整份数据"的约定取值。
const LONG kWholeDataIndex = -1;

// 往数据对象里放一份 HGLOBAL 格式的数据。成功后内存归数据对象；失败时自行释放。
bool SetGlobalFormat(IDataObject* pdo, CLIPFORMAT cf, const void* data, size_t bytes)
{
    HGLOBAL hMem = AllocGlobalCopy(data, bytes);
    if (hMem == NULL)
    {
        return false;
    }
    FORMATETC fe = { cf, NULL, DVASPECT_CONTENT, kWholeDataIndex, TYMED_HGLOBAL };
    STGMEDIUM sm;
    ::ZeroMemory(&sm, sizeof(sm));
    sm.tymed   = TYMED_HGLOBAL;
    sm.hGlobal = hMem;
    //最后一个参数为 TRUE：数据对象接管这块内存，由它负责释放。
    if (FAILED(pdo->SetData(&fe, &sm, TRUE)))
    {
        ::GlobalFree(hMem);
        return false;
    }
    return true;
}

// 放一份只含一个文件的文件列表（CF_HDROP）：DROPFILES 头 + 宽字符路径 + 两个结尾的 0
// （一个结束这条路径，一个结束整张列表）。
bool SetSingleFileDrop(IDataObject* pdo, const std::wstring& path)
{
    const size_t kTerminators = 2;   //路径结尾与列表结尾各一个 0
    std::vector<BYTE> buf(sizeof(DROPFILES) + (path.size() + kTerminators) * sizeof(wchar_t), 0);
    DROPFILES* df = (DROPFILES*)&buf[0];
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    ::CopyMemory(&buf[sizeof(DROPFILES)], path.c_str(), path.size() * sizeof(wchar_t));
    return SetGlobalFormat(pdo, CF_HDROP, &buf[0], buf.size());
}

/**
 *  拖拽源：决定拖拽何时结束（松开左键即放下、按 Esc 即取消），光标沿用系统默认样式。
 *  每次拖拽新建一个，拖完释放；只在发起拖拽的 UI 线程上使用。
 */
class ChatDropSource : public IDropSource
{
public:
    ChatDropSource()
        : m_refs(1)
    {
    }

    // IUnknown：只认 IUnknown 与 IDropSource 两个接口。
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDropSource)
        {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    // IUnknown：增加引用计数，返回新的计数。
    STDMETHODIMP_(ULONG) AddRef() override
    {
        return (ULONG)::InterlockedIncrement(&m_refs);
    }

    // IUnknown：减少引用计数，减到 0 时销毁自身；返回新的计数。
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG refs = ::InterlockedDecrement(&m_refs);
        if (refs == 0)
        {
            delete this;
        }
        return (ULONG)refs;
    }

    // IDropSource：拖拽过程中反复询问是否继续。按了 Esc 取消；松开左键在当前位置放下。
    STDMETHODIMP QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override
    {
        if (fEscapePressed)
        {
            return DRAGDROP_S_CANCEL;
        }
        if ((grfKeyState & MK_LBUTTON) == 0)
        {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }

    // IDropSource：光标反馈，沿用系统默认的拖拽光标。
    STDMETHODIMP GiveFeedback(DWORD /*dwEffect*/) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    LONG m_refs;   //COM 引用计数；为 0 时销毁
};

} // anonymous namespace

bool IsChatDragOrigin(int kind, bool pressInSelection, bool pressOnImage, bool imageReady)
{
    switch (kind)
    {
    //文字与大表情：只有按在已有选区内才是拖拽，按在选区外仍是重新选择文字
    case kMsgText:
    case kMsgEmoji:
        return pressInSelection;

    //图片：按在图片上、且本地原图可用才能拖
    case kMsgImage:
        return pressOnImage && imageReady;

    //文件、文件夹等：不支持拖出
    default:
        return false;
    }
}

bool IsBeyondDragThreshold(int dx, int dy, int thresholdX, int thresholdY)
{
    return ::abs(dx) > thresholdX || ::abs(dy) > thresholdY;
}

IDataObject* CreateChatDragDataObject(const std::vector<ChatClipSegment>& segs)
{
    if (segs.empty())
    {
        return NULL;
    }
    IDataObject* pdo = NULL;
    if (FAILED(::SHCreateDataObject(NULL, 0, NULL, NULL, IID_IDataObject, (void**)&pdo))
        || pdo == NULL)
    {
        return NULL;
    }

    //纯文字：连同结尾的 0 一起放。
    const std::wstring plain = ClipSegmentsToPlainText(segs);
    SetGlobalFormat(pdo, CF_UNICODETEXT, plain.c_str(), (plain.size() + 1) * sizeof(wchar_t));

    //Flamingo 聊天内容：UTF-8 文本，同样带上结尾的 0。
    const UINT format = GetChatClipFormat();
    if (format != 0)
    {
        const std::string rich = SerializeClipSegments(segs);
        SetGlobalFormat(pdo, (CLIPFORMAT)format, rich.c_str(), rich.size() + 1);
    }

    //恰好是一张图片：再放一份文件列表，可以直接拖到桌面、资源管理器或其它程序。
    if (segs.size() == 1 && segs[0].m_kind == kChatClipImage)
    {
        SetSingleFileDrop(pdo, segs[0].m_imagePath);
    }

    //首选效果：复制。拖到资源管理器时复制文件，不会把 Flamingo 缓存里的原文件移走。
    const DWORD effect = DROPEFFECT_COPY;
    SetGlobalFormat(pdo, (CLIPFORMAT)::RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT),
                    &effect, sizeof(effect));
    return pdo;
}

DWORD DoChatDragDrop(const std::vector<ChatClipSegment>& segs)
{
    IDataObject* pdo = CreateChatDragDataObject(segs);
    if (pdo == NULL)
    {
        return DROPEFFECT_NONE;
    }
    ChatDropSource* source = new ChatDropSource();
    DWORD effect = DROPEFFECT_NONE;
    //只允许"复制"：接收方即使想要"移动"也拿不到，原文件始终留在缓存里。
    const HRESULT hr = ::DoDragDrop(pdo, source, DROPEFFECT_COPY, &effect);
    source->Release();
    pdo->Release();
    return (hr == DRAGDROP_S_DROP) ? effect : DROPEFFECT_NONE;
}

} // namespace balloonchatwnd
