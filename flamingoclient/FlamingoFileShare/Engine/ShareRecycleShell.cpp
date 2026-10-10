/**
 *  经 IFileOperation 把一个条目放进回收站的底层实现，见 ShareRecycleShell.h。
 *  不包含 stdafx.h：IFileOperation 等声明要求 _WIN32_IE >= 0x0700，而工程的 stdafx.h 定为 0x0601（见头文件说明）。
 *  balloonwj@qq.com   2026-10-03
 */
//目标系统下限与 DLL 相同（Windows 7）；_WIN32_IE 取 IE7 的取值，Windows 7 自带的外壳满足
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0700

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "ShareRecycleShell.h"

namespace fileshare {

namespace {

/**
 *  IFileOperation 的进度回调：PreDeleteItem 收到的标志不含 TSF_DELETE_RECYCLE_IF_POSSIBLE 时说明这一项会被彻底删除
 *  （实测经网络路径时如此），返回 ERROR_CANCELLED 中止整个操作；PostDeleteItem 记下是否在回收站里得到了新条目。
 *  对象建在栈上，引用计数只为满足接口约定，不负责释放；调用方在对象析构之前先 Unadvise。
 */
class RecycleGuardSink : public IFileOperationProgressSink
{
public:
    RecycleGuardSink()
        : m_ref(1)
        , m_vetoed(false)
        , m_recycledItem(false)
        , m_deleteReported(false)
    {
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink)
        {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return ++m_ref;
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        return --m_ref;
    }

    IFACEMETHODIMP StartOperations()
    {
        return S_OK;
    }

    IFACEMETHODIMP FinishOperations(HRESULT)
    {
        return S_OK;
    }

    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR)
    {
        return S_OK;
    }

    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*)
    {
        return S_OK;
    }

    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR)
    {
        return S_OK;
    }

    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*)
    {
        return S_OK;
    }

    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR)
    {
        return S_OK;
    }

    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*)
    {
        return S_OK;
    }

    IFACEMETHODIMP PreDeleteItem(DWORD dwFlags, IShellItem*)
    {
        if ((dwFlags & TSF_DELETE_RECYCLE_IF_POSSIBLE) == 0)
        {
            m_vetoed = true;
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        return S_OK;
    }

    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem*, HRESULT hrDelete, IShellItem* psiNewlyCreated)
    {
        if (SUCCEEDED(hrDelete))
        {
            m_deleteReported = true;
            if (psiNewlyCreated != NULL)
                m_recycledItem = true;
        }
        return S_OK;
    }

    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR)
    {
        return S_OK;
    }

    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*)
    {
        return S_OK;
    }

    IFACEMETHODIMP UpdateProgress(UINT, UINT)
    {
        return S_OK;
    }

    IFACEMETHODIMP ResetTimer()
    {
        return S_OK;
    }

    IFACEMETHODIMP PauseTimer()
    {
        return S_OK;
    }

    IFACEMETHODIMP ResumeTimer()
    {
        return S_OK;
    }

    ULONG m_ref;             //引用计数（只为满足接口约定）
    bool  m_vetoed;          //是否因不能放进回收站而中止了操作
    bool  m_recycledItem;    //删除成功时是否在回收站里得到了新条目；为假说明被彻底删除了
    bool  m_deleteReported;  //是否收到过删除成功的回报
};

} // namespace

void ShareShellRecycleItem(const wchar_t* absPath, ShareShellRecycleResult& out)
{
    out = ShareShellRecycleResult();
    IShellItem* item = NULL;
    HRESULT hr = ::SHCreateItemFromParsingName(absPath, NULL, IID_PPV_ARGS(&item));
    if (FAILED(hr))
    {
        out.m_stage = share_shell_stage_parse_path;
        out.m_hr = hr;
        return;
    }

    IFileOperation* op = NULL;
    hr = ::CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_ALL, IID_PPV_ARGS(&op));
    if (FAILED(hr))
    {
        item->Release();
        out.m_stage = share_shell_stage_create_op;
        out.m_hr = hr;
        return;
    }

    //放进回收站、不确认、不弹任何界面（含出错界面）、出错即停；不带 FOF_WANTNUKEWARNING（会弹确认框）
    op->SetOperationFlags(FOFX_RECYCLEONDELETE | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_EARLYFAILURE);
    RecycleGuardSink sink;
    DWORD cookie = 0;
    out.m_advised = SUCCEEDED(op->Advise(&sink, &cookie));
    hr = op->DeleteItem(item, NULL);
    if (SUCCEEDED(hr))
        hr = op->PerformOperations();
    BOOL aborted = FALSE;
    op->GetAnyOperationsAborted(&aborted);
    if (out.m_advised)
        op->Unadvise(cookie);
    op->Release();
    item->Release();

    out.m_hr = hr;
    out.m_aborted = (aborted != FALSE);
    out.m_vetoed = sink.m_vetoed;
    out.m_deleteReported = sink.m_deleteReported;
    out.m_recycledItem = sink.m_recycledItem;
}

} // namespace fileshare
