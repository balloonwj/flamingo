/**
 *  拖出任务、数据对象与拖放源的实现，见 ShareDragData.h。
 *  不包含 stdafx.h：IDataObjectAsyncCapability 等声明要求 _WIN32_IE >= 0x0700，而工程的 stdafx.h 定为 0x0601。
 *  balloonwj@qq.com   2026-10-03
 */
//目标系统下限与 DLL 相同（Windows 7）；_WIN32_IE 取 IE7 的取值，Windows 7 自带的外壳满足
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0700

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "ShareDragData.h"

#include "../Engine/ShareWinPath.h"

namespace fileshare {

namespace {

//1601-01-01 到 1970-01-01 之间的 100 纳秒数，以及 1 毫秒等于多少个 100 纳秒
const ULONGLONG kEpochDelta100ns = 116444736000000000ULL;
const ULONGLONG kTicksPerMs = 10000ULL;

//下载失败时流返回的错误（资源管理器据此提示复制失败）
const HRESULT kReadFailed = HRESULT_FROM_WIN32(ERROR_READ_FAULT);

//把 Unix 毫秒换成 FILETIME
FILETIME UnixMsToFileTime(int64_t unixMs)
{
    const ULONGLONG t = (unixMs > 0) ? static_cast<ULONGLONG>(unixMs) * kTicksPerMs + kEpochDelta100ns : 0;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(t & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(t >> 32);
    return ft;
}

//在当前线程上处理已有的消息（放置方不支持异步、在 UI 线程上读流时，等待期间让下载事件照常处理）
void PumpPendingMessages()
{
    MSG msg;
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

/**
 *  一个文件的内容流：第一次读时等文件下载完成，之后从本机文件顺序读出。聚合自由线程封送器。
 */
class DragFileStream : public IStream
{
public:
    DragFileStream(const std::shared_ptr<CShareDragJob>& job, const std::wstring& localPath, int64_t size, DWORD uiThread)
        : m_ref(1)
        , m_job(job)
        , m_localPath(localPath)
        , m_size(size)
        , m_uiThread(uiThread)
        , m_file(INVALID_HANDLE_VALUE)
        , m_pos(0)
        , m_ftm(NULL)
    {
        ::CoCreateFreeThreadedMarshaler(static_cast<IStream*>(this), &m_ftm);
    }

    ~DragFileStream()
    {
        if (m_file != INVALID_HANDLE_VALUE)
            ::CloseHandle(m_file);
        if (m_ftm != NULL)
            m_ftm->Release();
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (ppv == NULL)
            return E_POINTER;
        if (riid == IID_IMarshal && m_ftm != NULL)
            return m_ftm->QueryInterface(riid, ppv);
        if (riid == IID_IUnknown || riid == IID_IStream || riid == IID_ISequentialStream)
        {
            *ppv = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return static_cast<ULONG>(::InterlockedIncrement(&m_ref));
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        const LONG r = ::InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return static_cast<ULONG>(r);
    }

    IFACEMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead)
    {
        if (pcbRead != NULL)
            *pcbRead = 0;
        if (pv == NULL)
            return STG_E_INVALIDPOINTER;
        if (!WaitForFile())
            return kReadFailed;
        DWORD got = 0;
        if (!::ReadFile(m_file, pv, cb, &got, NULL))
            return kReadFailed;
        m_pos += got;
        if (pcbRead != NULL)
            *pcbRead = got;
        return (got < cb) ? S_FALSE : S_OK;
    }

    IFACEMETHODIMP Write(const void*, ULONG, ULONG*)
    {
        return STG_E_ACCESSDENIED;
    }

    IFACEMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos)
    {
        //只支持查询位置与回到开头（资源管理器复制时顺序读）
        if (origin == STREAM_SEEK_CUR && move.QuadPart == 0)
        {
            if (newPos != NULL)
                newPos->QuadPart = static_cast<ULONGLONG>(m_pos);
            return S_OK;
        }
        if (origin == STREAM_SEEK_SET && move.QuadPart == 0)
        {
            if (m_file != INVALID_HANDLE_VALUE)
                ::SetFilePointer(m_file, 0, NULL, FILE_BEGIN);
            m_pos = 0;
            if (newPos != NULL)
                newPos->QuadPart = 0;
            return S_OK;
        }
        return STG_E_INVALIDFUNCTION;
    }

    IFACEMETHODIMP SetSize(ULARGE_INTEGER)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP Commit(DWORD)
    {
        return S_OK;
    }

    IFACEMETHODIMP Revert()
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP Stat(STATSTG* st, DWORD)
    {
        if (st == NULL)
            return STG_E_INVALIDPOINTER;
        memset(st, 0, sizeof(*st));
        st->type = STGTY_STREAM;
        st->cbSize.QuadPart = static_cast<ULONGLONG>(m_size);
        return S_OK;
    }

    IFACEMETHODIMP Clone(IStream**)
    {
        return E_NOTIMPL;
    }

private:
    //等文件下载完成并打开；下载失败或被取消时返回 false
    bool WaitForFile()
    {
        if (m_file != INVALID_HANDLE_VALUE)
            return true;
        const bool onUiThread = (::GetCurrentThreadId() == m_uiThread);
        while (true)
        {
            //下载失败或被取消：不必再等
            if (m_job->IsFinished() && m_job->GetCode() != 0)
                return false;
            //文件在下载完成、校验通过之后才改成正式名，出现即可读
            const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(m_localPath).c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                m_file = ::CreateFileW(ShareToLongPath(m_localPath).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                return m_file != INVALID_HANDLE_VALUE;
            }
            //下载已成功结束文件却不在：条目表与实际下载的内容对不上
            if (m_job->IsFinished())
                return false;
            if (onUiThread)
                PumpPendingMessages();
            ::Sleep(kShareDragPollMs);
        }
    }

    LONG                            m_ref;        //引用计数
    std::shared_ptr<CShareDragJob>  m_job;        //拖出任务
    std::wstring                    m_localPath;  //下载完成后文件所在的本机路径
    int64_t                         m_size;       //文件字节数
    DWORD                           m_uiThread;   //UI 线程 id（在它上面等待时要抽取消息）
    HANDLE                          m_file;       //打开的本机文件；还没打开时为 INVALID_HANDLE_VALUE
    int64_t                         m_pos;        //已读出的字节数
    IUnknown*                       m_ftm;        //聚合的自由线程封送器
};

/**
 *  拖出的数据对象。聚合自由线程封送器；异步模式在构造时就打开。
 */
class DragDataObject : public IDataObject, public IDataObjectAsyncCapability
{
public:
    DragDataObject(const std::shared_ptr<CShareDragJob>& job, IShareDragDelegate* delegate)
        : m_ref(1)
        , m_job(job)
        , m_delegate(delegate)
        , m_uiThread(::GetCurrentThreadId())
        , m_asyncMode(TRUE)
        , m_inOperation(FALSE)
        , m_ftm(NULL)
        , m_cfDescriptor(static_cast<CLIPFORMAT>(::RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW)))
        , m_cfContents(static_cast<CLIPFORMAT>(::RegisterClipboardFormatW(CFSTR_FILECONTENTS)))
    {
        ::CoCreateFreeThreadedMarshaler(static_cast<IDataObject*>(this), &m_ftm);
    }

    ~DragDataObject()
    {
        if (m_ftm != NULL)
            m_ftm->Release();
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (ppv == NULL)
            return E_POINTER;
        if (riid == IID_IMarshal && m_ftm != NULL)
            return m_ftm->QueryInterface(riid, ppv);
        if (riid == IID_IUnknown || riid == IID_IDataObject)
            *ppv = static_cast<IDataObject*>(this);
        else if (riid == IID_IDataObjectAsyncCapability)
            *ppv = static_cast<IDataObjectAsyncCapability*>(this);
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return static_cast<ULONG>(::InterlockedIncrement(&m_ref));
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        const LONG r = ::InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return static_cast<ULONG>(r);
    }

    IFACEMETHODIMP GetData(FORMATETC* fe, STGMEDIUM* sm)
    {
        if (fe == NULL || sm == NULL)
            return E_INVALIDARG;
        if (fe->cfFormat == m_cfDescriptor && (fe->tymed & TYMED_HGLOBAL) != 0)
            return BuildDescriptor(sm);
        if (fe->cfFormat == m_cfContents && (fe->tymed & TYMED_ISTREAM) != 0)
        {
            const std::vector<ShareDragItem>& items = m_job->GetItems();
            if (fe->lindex < 0 || static_cast<size_t>(fe->lindex) >= items.size() || items[fe->lindex].m_isDir)
                return DV_E_LINDEX;
            //不支持异步的放置方：第一次取内容时才知道放置开始了
            BeginDownload();
            const ShareDragItem& item = items[fe->lindex];
            sm->tymed = TYMED_ISTREAM;
            sm->pstm = new DragFileStream(m_job, m_job->GetTempDir() + L"\\" + item.m_dropName, item.m_size, m_uiThread);
            sm->pUnkForRelease = NULL;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP QueryGetData(FORMATETC* fe)
    {
        if (fe == NULL)
            return E_INVALIDARG;
        if ((fe->cfFormat == m_cfDescriptor && (fe->tymed & TYMED_HGLOBAL) != 0)
            || (fe->cfFormat == m_cfContents && (fe->tymed & TYMED_ISTREAM) != 0))
            return S_OK;
        return DV_E_FORMATETC;
    }

    IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out)
    {
        if (out != NULL)
            out->ptd = NULL;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL)
    {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** ppenum)
    {
        if (direction != DATADIR_GET)
            return E_NOTIMPL;
        FORMATETC formats[2];
        formats[0].cfFormat = m_cfDescriptor;
        formats[0].ptd = NULL;
        formats[0].dwAspect = DVASPECT_CONTENT;
        formats[0].lindex = -1;
        formats[0].tymed = TYMED_HGLOBAL;
        formats[1] = formats[0];
        formats[1].cfFormat = m_cfContents;
        formats[1].tymed = TYMED_ISTREAM;
        return ::SHCreateStdEnumFmtEtc(2, formats, ppenum);
    }

    IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*)
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    IFACEMETHODIMP DUnadvise(DWORD)
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**)
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    //IDataObjectAsyncCapability：异步模式由本对象在构造时打开，放置方只会查询
    IFACEMETHODIMP SetAsyncMode(BOOL fDoOpAsync)
    {
        m_asyncMode = fDoOpAsync;
        return S_OK;
    }

    IFACEMETHODIMP GetAsyncMode(BOOL* pfIsOpAsync)
    {
        if (pfIsOpAsync == NULL)
            return E_POINTER;
        *pfIsOpAsync = m_asyncMode;
        return S_OK;
    }

    IFACEMETHODIMP StartOperation(IBindCtx*)
    {
        m_inOperation = TRUE;
        BeginDownload();
        return S_OK;
    }

    IFACEMETHODIMP InOperation(BOOL* pfInAsyncOp)
    {
        if (pfInAsyncOp == NULL)
            return E_POINTER;
        *pfInAsyncOp = m_inOperation;
        return S_OK;
    }

    IFACEMETHODIMP EndOperation(HRESULT, IBindCtx*, DWORD)
    {
        m_inOperation = FALSE;
        m_job->MarkOperationEnded();
        return S_OK;
    }

private:
    //放置开始了：第一次调用时请委托方开始下载（只能在 UI 线程上调用委托方）
    void BeginDownload()
    {
        if (!m_job->TryBeginDownload())
            return;
        if (::GetCurrentThreadId() != m_uiThread || m_delegate == NULL || !m_delegate->OnDragDownloadStart(m_job))
            m_job->MarkFinished(static_cast<int>(kReadFailed));
    }

    //FILEGROUPDESCRIPTORW：每个条目一项
    HRESULT BuildDescriptor(STGMEDIUM* sm)
    {
        const std::vector<ShareDragItem>& items = m_job->GetItems();
        if (items.empty())
            return DV_E_FORMATETC;
        const size_t bytes = sizeof(FILEGROUPDESCRIPTORW) + (items.size() - 1) * sizeof(FILEDESCRIPTORW);
        HGLOBAL h = ::GlobalAlloc(GHND, bytes);
        if (h == NULL)
            return E_OUTOFMEMORY;
        FILEGROUPDESCRIPTORW* group = static_cast<FILEGROUPDESCRIPTORW*>(::GlobalLock(h));
        group->cItems = static_cast<UINT>(items.size());
        for (size_t i = 0; i < items.size(); ++i)
        {
            FILEDESCRIPTORW& d = group->fgd[i];
            d.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_WRITESTIME | FD_PROGRESSUI;
            d.dwFileAttributes = items[i].m_isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            const ULONGLONG size = static_cast<ULONGLONG>(items[i].m_size > 0 ? items[i].m_size : 0);
            d.nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFULL);
            d.nFileSizeHigh = static_cast<DWORD>(size >> 32);
            d.ftLastWriteTime = UnixMsToFileTime(items[i].m_mtime);
            wcsncpy_s(d.cFileName, items[i].m_dropName.c_str(), _TRUNCATE);
        }
        ::GlobalUnlock(h);
        sm->tymed = TYMED_HGLOBAL;
        sm->hGlobal = h;
        sm->pUnkForRelease = NULL;
        return S_OK;
    }

    LONG                            m_ref;           //引用计数
    std::shared_ptr<CShareDragJob>  m_job;           //拖出任务
    IShareDragDelegate*             m_delegate;      //委托方（不拥有）
    DWORD                           m_uiThread;      //建本对象的线程（UI 线程）
    BOOL                            m_asyncMode;     //是否异步（构造时为真）
    BOOL                            m_inOperation;   //是否在异步操作中
    IUnknown*                       m_ftm;           //聚合的自由线程封送器
    CLIPFORMAT                      m_cfDescriptor;  //CFSTR_FILEDESCRIPTORW 的格式号
    CLIPFORMAT                      m_cfContents;    //CFSTR_FILECONTENTS 的格式号
};

/**
 *  拖放源：按 Esc 取消、松开左键放置、光标用系统默认的。
 */
class DropSource : public IDropSource
{
public:
    DropSource()
        : m_ref(1)
    {
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (ppv == NULL)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource)
        {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return static_cast<ULONG>(::InterlockedIncrement(&m_ref));
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        const LONG r = ::InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return static_cast<ULONG>(r);
    }

    IFACEMETHODIMP QueryContinueDrag(BOOL escapePressed, DWORD keyState)
    {
        if (escapePressed)
            return DRAGDROP_S_CANCEL;
        if ((keyState & MK_LBUTTON) == 0)
            return DRAGDROP_S_DROP;
        return S_OK;
    }

    IFACEMETHODIMP GiveFeedback(DWORD)
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    LONG m_ref;   //引用计数
};

} // namespace

CShareDragJob::CShareDragJob(const std::wstring& tempDir, const std::vector<ShareDragItem>& items)
    : m_tempDir(tempDir)
    , m_items(items)
    , m_begun(false)
    , m_marking(false)
    , m_finished(false)
    , m_code(0)
    , m_opEnded(false)
{
}

CShareDragJob::~CShareDragJob()
{
    if (!m_tempDir.empty())
        ShareDeleteTree(m_tempDir);
}

const std::wstring& CShareDragJob::GetTempDir() const
{
    return m_tempDir;
}

const std::vector<ShareDragItem>& CShareDragJob::GetItems() const
{
    return m_items;
}

bool CShareDragJob::TryBeginDownload()
{
    bool expected = false;
    return m_begun.compare_exchange_strong(expected, true);
}

void CShareDragJob::MarkFinished(int code)
{
    //先写结果码再置「已结束」：读的一方看到已结束时结果码一定是最终的
    bool expected = false;
    if (!m_marking.compare_exchange_strong(expected, true))
        return;
    m_code.store(code);
    m_finished.store(true);
}

bool CShareDragJob::IsFinished() const
{
    return m_finished.load();
}

int CShareDragJob::GetCode() const
{
    return m_code.load();
}

void CShareDragJob::MarkOperationEnded()
{
    m_opEnded.store(true);
}

bool CShareDragJob::IsOperationEnded() const
{
    return m_opEnded.load();
}

IDataObject* CreateShareDragDataObject(const std::shared_ptr<CShareDragJob>& job, IShareDragDelegate* delegate)
{
    return new DragDataObject(job, delegate);
}

IDropSource* CreateShareDropSource()
{
    return new DropSource();
}

HRESULT ShareDropOnFolder(IDataObject* data, const std::wstring& folder)
{
    IShellItem* item = NULL;
    HRESULT hr = ::SHCreateItemFromParsingName(folder.c_str(), NULL, IID_PPV_ARGS(&item));
    if (FAILED(hr))
        return hr;
    IDropTarget* target = NULL;
    hr = item->BindToHandler(NULL, BHID_SFUIObject, IID_PPV_ARGS(&target));
    item->Release();
    if (FAILED(hr))
        return hr;
    //与松开鼠标时相同的调用顺序：先进入、再放置；放置坐标对目录的放置目标没有意义
    POINTL pt = { 0, 0 };
    DWORD effect = DROPEFFECT_COPY;
    hr = target->DragEnter(data, MK_LBUTTON, pt, &effect);
    if (SUCCEEDED(hr))
    {
        effect = DROPEFFECT_COPY;
        hr = target->Drop(data, MK_LBUTTON, pt, &effect);
    }
    target->Release();
    return hr;
}

IDataObject* ShareCreateFileListData(const std::vector<std::wstring>& paths)
{
    //DROPFILES 之后是以 '\0' 分隔、以两个 '\0' 结尾的宽字符路径表
    size_t chars = 1;
    for (size_t i = 0; i < paths.size(); ++i)
        chars += paths[i].size() + 1;
    const size_t bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (mem == NULL)
        return NULL;
    DROPFILES* drop = static_cast<DROPFILES*>(::GlobalLock(mem));
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    wchar_t* cursor = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
    for (size_t i = 0; i < paths.size(); ++i)
    {
        memcpy(cursor, paths[i].c_str(), paths[i].size() * sizeof(wchar_t));
        cursor += paths[i].size() + 1;
    }
    ::GlobalUnlock(mem);

    IDataObject* data = NULL;
    if (FAILED(::SHCreateDataObject(NULL, 0, NULL, NULL, IID_PPV_ARGS(&data))))
    {
        ::GlobalFree(mem);
        return NULL;
    }
    FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm;
    sm.tymed = TYMED_HGLOBAL;
    sm.hGlobal = mem;
    sm.pUnkForRelease = NULL;
    //fRelease 为 TRUE：数据对象接管这块内存
    if (FAILED(data->SetData(&fe, &sm, TRUE)))
    {
        ::GlobalFree(mem);
        data->Release();
        return NULL;
    }
    return data;
}

} // namespace fileshare
