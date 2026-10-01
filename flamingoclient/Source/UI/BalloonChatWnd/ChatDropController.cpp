/**
 *  ChatDropController.cpp
 *
 *  聊天窗拖放接收器实现。设计取舍与"为什么要摘掉输入框的拖放目标"见
 *  ChatDropController.h 的文件头。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#include "stdafx.h"
#include "ChatDropController.h"

#include <functional>   // std::bind / std::placeholders
#include <string.h>     // strnlen
#include "../../Common/Log/IULog.h"   // LOG_INFO
#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // EnableDropDispatch
#include "ChatClipboardIo.h"          // GetChatClipFormat（Flamingo 聊天内容格式）

namespace balloonchatwnd {

namespace {

// FORMATETC 里"不分页，取整份数据"的约定取值。
const LONG kWholeDataIndex = -1;

} // anonymous namespace

/**
 *  注册在聊天窗 HWND 上的包装拖放目标。
 *
 *  拖进来的数据里有 Flamingo 聊天内容格式（从聊天记录里拖出来的文字、表情、图片）时，
 *  由 ChatDropController 的 OnContent* 系列处理 —— 落在发送框里就插入输入框，不发送；
 *  否则（例如从资源管理器拖文件）把每一步原样转给 balloonui 拖放辅助类原来的目标，
 *  拖文件的行为与以前完全一致。
 *
 *  是否按聊天内容处理在 DragEnter 时定下，整次拖拽保持不变：DragOver / DragLeave 不带
 *  数据对象，只能沿用进入时的判断。只在注册它的 UI 线程上使用。
 */
class ChatContentDropTarget : public IDropTarget
{
public:
    /**
     *  owner：拖放接收器，引用不持有，须在本对象注销之前一直有效。
     *  inner：辅助类原来的拖放目标；本对象 AddRef 持有一份，析构时 Release。
     */
    ChatContentDropTarget(ChatDropController* owner, IDropTarget* inner)
        : m_refs(1)
        , m_owner(owner)
        , m_inner(inner)
        , m_isContent(false)
    {
        if (m_inner != NULL)
        {
            m_inner->AddRef();
        }
    }

    // IUnknown：只认 IUnknown 与 IDropTarget 两个接口。
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            *ppv = static_cast<IDropTarget*>(this);
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

    // IDropTarget：拖入。先看数据里有没有聊天内容，据此决定整次拖拽由谁处理。
    STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt,
                           DWORD* pdwEffect) override
    {
        m_isContent = (m_owner != NULL && m_owner->HasChatContent(pDataObj));
        if (m_isContent)
        {
            *pdwEffect = m_owner->OnContentDragMove(pt, *pdwEffect, true);
            return S_OK;
        }
        return (m_inner != NULL) ? m_inner->DragEnter(pDataObj, grfKeyState, pt, pdwEffect)
                                 : E_UNEXPECTED;
    }

    // IDropTarget：拖动中。聊天内容按落点逐帧改判；其余转给原目标。
    STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
    {
        if (m_isContent)
        {
            *pdwEffect = m_owner->OnContentDragMove(pt, *pdwEffect, false);
            return S_OK;
        }
        return (m_inner != NULL) ? m_inner->DragOver(grfKeyState, pt, pdwEffect) : E_UNEXPECTED;
    }

    // IDropTarget：拖离窗口。
    STDMETHODIMP DragLeave() override
    {
        if (m_isContent)
        {
            m_isContent = false;
            m_owner->OnContentDragLeave();
            return S_OK;
        }
        return (m_inner != NULL) ? m_inner->DragLeave() : E_UNEXPECTED;
    }

    // IDropTarget：松手。
    STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt,
                      DWORD* pdwEffect) override
    {
        if (m_isContent)
        {
            m_isContent = false;
            *pdwEffect = m_owner->OnContentDrop(pDataObj, pt);
            return S_OK;
        }
        return (m_inner != NULL) ? m_inner->Drop(pDataObj, grfKeyState, pt, pdwEffect)
                                 : E_UNEXPECTED;
    }

private:
    ~ChatContentDropTarget()
    {
        if (m_inner != NULL)
        {
            m_inner->Release();
        }
    }

    LONG                m_refs;       // COM 引用计数；为 0 时销毁
    ChatDropController* m_owner;      // 拖放接收器，引用不持有
    IDropTarget*        m_inner;      // 辅助类原来的拖放目标，持有一份引用
    bool                m_isContent;  // 本次拖拽是否按聊天内容处理（DragEnter 时定下）
};

ChatDropController::ChatDropController()
    : m_pContentTarget(NULL)
    , m_sink(NULL)
    , m_frameHwnd(NULL)
    , m_inputHwnd(NULL)
    , m_attached(false)
    , m_hintShown(false)
{
}

ChatDropController::~ChatDropController()
{
    Detach();
}

bool ChatDropController::Attach(balloonwjui::DuiHost* host, HWND inputHwnd, IChatDropSink* sink)
{
    if (m_attached)
    {
        Detach();
    }
    if (host == NULL || sink == NULL)
    {
        return false;
    }

    HWND frameHwnd = host->m_hWnd;
    if (frameHwnd == NULL || !::IsWindow(frameHwnd))
    {
        return false;
    }

    m_sink      = sink;
    m_frameHwnd = frameHwnd;
    m_inputHwnd = inputHwnd;

    //先把窗口让出来：balloonui 的无窗口控件共用宿主窗口，需要接收拖放的控件会
    //让宿主替它注册一个分发器，而一个窗口只能注册一个拖放目标。聊天窗要的是
    //"整窗接收"，窗口里没有别的控件需要单独收拖放，所以这份分发器一律让掉。
    //未开启时本调用为空操作，不会有副作用。
    if (host->IsDropDispatchEnabled())
    {
        host->EnableDropDispatch(false);
        LOG_INFO("Chat drop: released the host drop dispatch, the whole window receives drops now.");
    }

    WireCallbacks(m_target, false /*来自窗口那份目标*/);
    if (!m_target.Register(frameHwnd))
    {
        LOG_ERROR("Chat drop: RegisterDragDrop on the frame window failed, "
                  "dropping files into this window will not work.");
        m_sink      = NULL;
        m_frameHwnd = NULL;
        m_inputHwnd = NULL;
        return false;
    }

    //换上能认出 Flamingo 聊天内容的包装目标。辅助类先照常注册一次是有意的：它借此记下
    //窗口句柄，被转发过去的拖文件事件照样能把屏幕坐标换算成客户区坐标。之后撤下它、
    //把包装注册上去；包装注册不上时退回辅助类原来的目标，拖文件至少仍然可用。
    IDropTarget* inner = m_target.GetDropTarget();
    ChatContentDropTarget* wrapper = new ChatContentDropTarget(this, inner);
    ::RevokeDragDrop(frameHwnd);
    if (::RegisterDragDrop(frameHwnd, wrapper) == S_OK)
    {
        m_pContentTarget = wrapper;   //本类持有创建时的那一份引用，Detach 时释放
    }
    else
    {
        wrapper->Release();
        ::RegisterDragDrop(frameHwnd, inner);
        LOG_ERROR("Chat drop: failed to register the chat content drop target, "
                  "dragging chat content into this window will not work.");
    }

    //输入框那块单独再注册一份：先摘掉 RichEdit 自带的目标（一个 HWND 只能有一个
    //拖放目标，不摘的话 RegisterDragDrop 会返回 DRAGDROP_E_ALREADYREGISTERED），
    //再换上我们的。理由见头文件。
    if (m_inputHwnd != NULL && ::IsWindow(m_inputHwnd))
    {
        ::RevokeDragDrop(m_inputHwnd);
        WireCallbacks(m_inputTarget, true /*来自输入框那份目标*/);
        if (!m_inputTarget.Register(m_inputHwnd))
        {
            //退化情形：输入框那块收不了拖放，但消息区仍然可用，不算致命。
            LOG_ERROR("Chat drop: RegisterDragDrop on the input child window failed, "
                      "drops onto the input box will not respond.");
        }
    }

    m_attached = true;
    LOG_INFO("Chat drop is ready: frame=0x%p, input=0x%p.", frameHwnd, inputHwnd);
    return true;
}

void ChatDropController::WireCallbacks(balloonwjui::DuiDropTargetHelper& helper,
                                       bool fromInput)
{
    //用 std::bind 绑成员函数（仓库约定尽量不用 lambda）。第一个实参绑死 fromInput，
    //回调里据它区分事件来自哪一份目标 —— 两份目标的坐标系不同（窗口客户区 /
    //输入框客户区），不区分就会拿输入框的坐标去问"在不在发送框内"。
    helper.SetDragCallbacks(
        std::bind(&ChatDropController::OnDragEnter, this, fromInput,
                  std::placeholders::_1, std::placeholders::_2),
        std::bind(&ChatDropController::OnDragOver, this, fromInput,
                  std::placeholders::_1),
        std::bind(&ChatDropController::OnDragLeave, this));
    helper.SetFilesAtPointCallback(
        std::bind(&ChatDropController::OnFilesDropped, this, fromInput,
                  std::placeholders::_1, std::placeholders::_2));
}

void ChatDropController::Detach()
{
    if (!m_attached)
    {
        return;
    }
    //辅助类注销时会对窗口调 RevokeDragDrop，撤下的正是包装目标；随后释放本类持有的那份引用。
    m_target.Unregister();
    m_inputTarget.Unregister();
    if (m_pContentTarget != NULL)
    {
        m_pContentTarget->Release();
        m_pContentTarget = NULL;
    }
    m_sink      = NULL;
    m_frameHwnd = NULL;
    m_inputHwnd = NULL;
    m_attached  = false;
    m_hintShown = false;
}

IDropTarget* ChatDropController::GetFrameDropTarget()
{
    if (!m_attached)
    {
        return NULL;
    }
    //返回实际注册在窗口上的那一个：包装目标注册成功时是它，否则是辅助类原来的目标。
    if (m_pContentTarget != NULL)
    {
        return m_pContentTarget;
    }
    return m_target.GetDropTarget();
}

bool ChatDropController::IsForceAsFileHeld()
{
    //最高位为 1 表示当前按下。
    return (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool ChatDropController::InDropZone(bool fromInput, const POINT& ptClient) const
{
    //输入框本身就是发送框的一部分，落在它上面必然算数（而且它给的坐标是输入框
    //客户区的，拿去问宿主窗会答错）。
    if (fromInput)
    {
        return true;
    }
    if (m_sink == NULL)
    {
        return false;
    }
    return m_sink->IsChatDropZone(ptClient);
}

bool ChatDropController::OnDragEnter(bool fromInput, const POINT& ptClient, bool hasFiles)
{
    //只收文件。位图（从看图软件里直接拖一张图过来）本期不接 —— 发图仍走
    //工具栏「图片」按钮与截图那两条既有链路。
    if (!hasFiles)
    {
        return false;
    }

    //进入点在不在发送框内，只用来决定要不要显示提示，<u>不能拿来当本方法的返回值</u>。
    //
    //DuiDropTargetHelper 那一侧的约定是：本回调返回 false 表示<u>整次拖放</u>拒收，
    //此后 DragOver 不再逐帧询问、Drop 也直接忽略（见 DuiDropTarget.cpp 里 DragEnter
    //与 DragOver 的实现）。而光标总是从窗口边缘进来的，发送框又在窗口下半部分，
    //进入点几乎必然落在上方的消息区 —— 若按落点返回，每一次拖放都在进窗口那一瞬间
    //被判死，用户再把光标移到发送框上也毫无反应。2026-08-17 实测就栽在这里。
    //
    //故只要拖的是文件就一律先收下，"哪一块能放"完全交给逐帧的 OnDragOver 决定；
    //真落手时 OnFilesDropped 还会按最终落点再兜一道底，落在消息区的投放仍然不会
    //被当成发送（守卫用例 CDR8 / CDR9）。
    const bool inZone = InDropZone(fromInput, ptClient);
    //落点判定是这套交互的关键，进来一次记一条：万一发送框的范围算歪了（布局改动
    //导致取到的父控件不对），从日志一眼能看出来，不必靠猜。
    LOG_INFO("Chat drop: drag enter (%d,%d), source=%s, inSendBox=%d.",
             (int)ptClient.x, (int)ptClient.y, fromInput ? "input" : "frame",
             inZone ? 1 : 0);
    SetHintVisible(inZone);
    return true;
}

bool ChatDropController::OnDragOver(bool fromInput, const POINT& ptClient)
{
    //逐帧改判：光标在同一个窗口里从消息区移到发送框（或反过来）是不会再触发
    //DragEnter 的，能不能放只能在这里跟着落点变。
    const bool inZone = InDropZone(fromInput, ptClient);
    SetHintVisible(inZone);
    return inZone;
}

void ChatDropController::OnDragLeave()
{
    SetHintVisible(false);
}

void ChatDropController::OnFilesDropped(bool fromInput, const std::vector<CString>& files,
                                        const POINT& ptClient)
{
    //落手后 balloonui 那侧会补一次 DragLeave，提示由它收起，这里不必重复。
    if (m_sink == NULL || files.empty())
    {
        return;
    }

    //再确认一次落点：DragOver 已经把消息区判成"不能放"、系统也不该在那儿投过来，
    //但落点是最终事实，按它兜一道底，免得消息区上的投放被当成发送。
    if (!InDropZone(fromInput, ptClient))
    {
        LOG_INFO("Chat drop: the drop point is outside the send box, this drop is ignored.");
        return;
    }

    std::vector<std::wstring> paths;
    paths.reserve(files.size());
    for (std::vector<CString>::size_type i = 0; i < files.size(); ++i)
    {
        paths.push_back(std::wstring((LPCWSTR)files[i]));
    }

    const bool forceAsFile = IsForceAsFileHeld();
    LOG_INFO("Chat drop received %d paths, shift=%d.", (int)paths.size(), forceAsFile ? 1 : 0);
    m_sink->OnChatFilesDropped(paths, forceAsFile);
}

bool ChatDropController::HasChatContent(IDataObject* pdo) const
{
    const UINT format = GetChatClipFormat();
    if (pdo == NULL || format == 0)
    {
        return false;
    }
    FORMATETC fe = { (CLIPFORMAT)format, NULL, DVASPECT_CONTENT, kWholeDataIndex, TYMED_HGLOBAL };
    return pdo->QueryGetData(&fe) == S_OK;
}

DWORD ChatDropController::OnContentDragMove(const POINTL& ptScreen, DWORD allowed, bool entering)
{
    POINT pt;
    pt.x = ptScreen.x;
    pt.y = ptScreen.y;
    if (::IsWindow(m_frameHwnd))
    {
        ::ScreenToClient(m_frameHwnd, &pt);
    }
    //与拖文件同一套判定：只有发送框能放，消息区显示禁止符。拖放源只允许"复制"。
    const bool inZone = InDropZone(false, pt);
    if (entering)
    {
        LOG_INFO("Chat drop: chat content drag enter (%d,%d), inSendBox=%d.",
                 (int)pt.x, (int)pt.y, inZone ? 1 : 0);
    }
    SetHintVisible(inZone);
    return (inZone && (allowed & DROPEFFECT_COPY) != 0) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

void ChatDropController::OnContentDragLeave()
{
    SetHintVisible(false);
}

DWORD ChatDropController::OnContentDrop(IDataObject* pdo, const POINTL& ptScreen)
{
    SetHintVisible(false);

    POINT pt;
    pt.x = ptScreen.x;
    pt.y = ptScreen.y;
    if (::IsWindow(m_frameHwnd))
    {
        ::ScreenToClient(m_frameHwnd, &pt);
    }
    //最终落点兜底：DragOver 已把消息区判成不能放，但落点是最终事实。
    if (m_sink == NULL || !InDropZone(false, pt))
    {
        LOG_INFO("Chat drop: chat content dropped outside the send box, ignored.");
        return DROPEFFECT_NONE;
    }

    //取出 Flamingo 聊天内容（UTF-8 文本，结尾带 0）并还原成片段。
    FORMATETC fe = { (CLIPFORMAT)GetChatClipFormat(), NULL, DVASPECT_CONTENT, kWholeDataIndex,
                     TYMED_HGLOBAL };
    STGMEDIUM sm;
    ::ZeroMemory(&sm, sizeof(sm));
    if (pdo == NULL || FAILED(pdo->GetData(&fe, &sm)))
    {
        LOG_ERROR("Chat drop: failed to read the chat content from the drop data.");
        return DROPEFFECT_NONE;
    }
    std::string data;
    const char* src = (const char*)::GlobalLock(sm.hGlobal);
    if (src != NULL)
    {
        //只在这块内存的范围内找结尾的 0，数据被改坏了也不会越界读。
        data.assign(src, ::strnlen(src, ::GlobalSize(sm.hGlobal)));
        ::GlobalUnlock(sm.hGlobal);
    }
    ::ReleaseStgMedium(&sm);

    std::vector<ChatClipSegment> segs;
    if (!ParseClipSegments(data, segs) || segs.empty())
    {
        LOG_ERROR("Chat drop: the dropped chat content could not be parsed.");
        return DROPEFFECT_NONE;
    }
    LOG_INFO("Chat drop: chat content dropped into the send box, %u segment(s).",
             (unsigned)segs.size());
    m_sink->OnChatContentDropped(segs);
    return DROPEFFECT_COPY;
}

void ChatDropController::SetHintVisible(bool visible)
{
    if (m_hintShown == visible)
    {
        return;
    }
    m_hintShown = visible;
    if (m_sink != NULL)
    {
        m_sink->OnChatDropHintChanged(visible);
    }
}

} // namespace balloonchatwnd
