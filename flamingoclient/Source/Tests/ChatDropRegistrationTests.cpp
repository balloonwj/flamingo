/**
 *  ChatDropRegistrationTests.cpp
 *
 *  聊天窗「拖放目标归谁注册」的守卫用例（Suite CDR）。
 *
 *  背景：操作系统的拖放目标是按窗口注册的，<u>一个窗口只能注册一个</u>，后到的
 *  直接失败。聊天窗要把整个窗口做成投放区（拖文件进来就发送，见
 *  ChatDropController.h），而 balloonui 的富文本控件在第一次布局时会替自己向
 *  宿主申请开启拖放分发（DuiRichEdit::Layout → DuiHost::EnableDropDispatch），
 *  那一步就把窗口的拖放目标占走了。两者撞在一起的后果是聊天窗那份注册失败，
 *  <u>拖任何文件进聊天窗都没有反应</u>，且失败时界面上没有任何提示。
 *
 *  2026-08-17 实测就栽在这里：客户端日志里每开一次聊天窗就报一条
 *  "窗口级 RegisterDragDrop 失败"，单聊窗与群聊窗都是。
 *
 *  覆盖用例：
 *    CDR1 机制复现：原生富文本控件会占走宿主窗口的拖放目标；
 *    CDR2 让出之后可以注册：EnableDropDispatch(false) 之后外部注册成功；
 *    CDR3 单聊窗控件树不占走宿主窗口的拖放目标；
 *    CDR4 群聊窗控件树同上；
 *    CDR5 注册之后再布局一次，拖放目标不会被重新抢走；
 *    CDR6 聊天窗拖放接收器在带聊天布局的窗口上能注册成功（端到端）；
 *    CDR7 窗口已被别的控件占用时，接收器让出之后仍能注册成功（兜底那条路）；
 *    CDR8 从消息区进入、移到发送框再松手，路径能送达（整条拖放流程）；
 *    CDR9 最终落点仍在消息区时不当成发送。
 *
 *  本组用例<u>会创建真实的顶层窗口</u>（不显示、不进消息循环），这是本工程里
 *  少见的一类 —— 因为要断言的正是操作系统那一侧的注册结果，纯内存断言够不着。
 *  全局 _Module 由其它测试文件统一定义，本文件只是随 stdafx.h 引用其声明。
 *
 *  balloonwj@qq.com   2026-08-17
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>

#include "../UI/BalloonChatWnd/ChatWindowLayout.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"
#include "../UI/BalloonChatWnd/ChatDropController.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

using namespace balloonwjui;

namespace {

//测试窗口的尺寸（逻辑像素）。取一个聊天窗控件树能正常排版的常见尺寸；具体
//数值不影响结论 —— 本组用例只关心窗口的拖放目标注册给了谁，与排版无关。
const int kFrameWidth  = 800;
const int kFrameHeight = 600;

/**
 *  只用来占位的拖放目标。
 *
 *  测试拿它去 RegisterDragDrop，用返回的 HRESULT 判断这个窗口的拖放目标是
 *  空着的还是已经被别人占了 —— 它自己不接收任何投放，四个方法一律报告
 *  「不能放」。
 */
class ProbeDropTarget : public IDropTarget
{
public:
    ProbeDropTarget()
        : m_cRef(1)
    {
    }

    // ---- IUnknown ----

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
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

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        ++m_cRef;
        return m_cRef;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        --m_cRef;
        const ULONG nLeft = m_cRef;
        if (nLeft == 0)
        {
            delete this;
        }
        return nLeft;
    }

    // ---- IDropTarget ----

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* /*pDataObj*/, DWORD /*grfKeyState*/,
                                        POINTL /*pt*/, DWORD* pdwEffect) override
    {
        return Refuse(pdwEffect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/, POINTL /*pt*/,
                                       DWORD* pdwEffect) override
    {
        return Refuse(pdwEffect);
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* /*pDataObj*/, DWORD /*grfKeyState*/,
                                   POINTL /*pt*/, DWORD* pdwEffect) override
    {
        return Refuse(pdwEffect);
    }

private:
    //统一把效果置成「不能放」。三处拖动过程回调的收尾完全一样，抽出来免得写三遍。
    HRESULT Refuse(DWORD* pdwEffect)
    {
        if (pdwEffect != NULL)
        {
            *pdwEffect = DROPEFFECT_NONE;
        }
        return S_OK;
    }

    ULONG m_cRef;   // COM 引用计数；测试是单线程的，不做原子操作
};

/**
 *  记事用的投放接收方。
 *
 *  按真实聊天窗的口径实现 IsChatDropZone —— 只有落在指定矩形（发送框）内才算
 *  投放区；另外两个回调把收到的东西记下来供断言核对。
 */
class ProbeDropSink : public balloonchatwnd::IChatDropSink
{
public:
    ProbeDropSink()
        : m_hintVisible(false)
        , m_dropCount(0)
        , m_forceAsFile(false)
    {
        ::SetRectEmpty(&m_rcZone);
    }

    // 设定投放区矩形（本窗口客户区坐标）。不设则任何落点都不算投放区。
    void SetZone(const RECT& rc)
    {
        m_rcZone = rc;
    }

    bool IsChatDropZone(const POINT& ptClient) override
    {
        return ::PtInRect(&m_rcZone, ptClient) != FALSE;
    }

    void OnChatDropHintChanged(bool visible) override
    {
        m_hintVisible = visible;
    }

    void OnChatFilesDropped(const std::vector<std::wstring>& paths,
                            bool forceAsFile) override
    {
        ++m_dropCount;
        m_paths       = paths;
        m_forceAsFile = forceAsFile;
    }

    bool m_hintVisible;                   // 提示浮层当前该不该显示
    int  m_dropCount;                     // 收到过几次投放
    std::vector<std::wstring> m_paths;    // 最近一次投放收到的路径
    bool m_forceAsFile;                   // 最近一次投放是否按住了 Shift

private:
    RECT m_rcZone;   // 算作投放区的矩形（客户区坐标）
};

/**
 *  只提供 CF_HDROP 的最小 IDataObject，供测试自行驱动拖放流程。
 *
 *  形态照抄 balloonui 自己的拖放用例（DuiDropTargetTests.cpp）：DROPFILES 头 +
 *  若干以 '\0' 结尾的宽字符路径 + 一个结尾哨兵。
 */
class ProbeFilesDataObject : public IDataObject
{
public:
    explicit ProbeFilesDataObject(const std::wstring& path)
        : m_ref(1)
        , m_path(path)
    {
    }

    // ---- IUnknown ----

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == NULL)
        {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDataObject)
        {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return (ULONG)InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG nLeft = InterlockedDecrement(&m_ref);
        if (nLeft == 0)
        {
            delete this;
        }
        return (ULONG)nLeft;
    }

    // ---- IDataObject：只实现 QueryGetData / GetData，其余一律不支持 ----

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pFmt) override
    {
        if (pFmt == NULL)
        {
            return E_POINTER;
        }
        if (pFmt->cfFormat == CF_HDROP && (pFmt->tymed & TYMED_HGLOBAL) != 0)
        {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pFmt, STGMEDIUM* pMed) override
    {
        if (pFmt == NULL || pMed == NULL)
        {
            return E_POINTER;
        }
        if (pFmt->cfFormat != CF_HDROP || (pFmt->tymed & TYMED_HGLOBAL) == 0)
        {
            return DV_E_FORMATETC;
        }

        HGLOBAL hBlock = BuildHDrop();
        if (hBlock == NULL)
        {
            return E_OUTOFMEMORY;
        }
        ::ZeroMemory(pMed, sizeof(*pMed));
        pMed->tymed          = TYMED_HGLOBAL;
        pMed->hGlobal        = hBlock;
        pMed->pUnkForRelease = NULL;   // 由调用方 ReleaseStgMedium 负责释放
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    //现搓一块 CF_HDROP 内存。
    HGLOBAL BuildHDrop() const
    {
        //路径本体 + 它的结尾 '\0' + 整张列表的结尾哨兵。
        const SIZE_T nPayloadChars = m_path.size() + 1 + 1;
        const SIZE_T nTotalBytes   = sizeof(DROPFILES) + nPayloadChars * sizeof(wchar_t);

        HGLOBAL hBlock = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, nTotalBytes);
        if (hBlock == NULL)
        {
            return NULL;
        }

        DROPFILES* pDrop = (DROPFILES*)::GlobalLock(hBlock);
        pDrop->pFiles = sizeof(DROPFILES);
        pDrop->fWide  = TRUE;

        wchar_t* pText = (wchar_t*)((BYTE*)pDrop + sizeof(DROPFILES));
        ::memcpy(pText, m_path.c_str(), m_path.size() * sizeof(wchar_t));
        //GMEM_ZEROINIT 已经把剩下的字节清零，路径结尾与列表哨兵都是现成的。
        ::GlobalUnlock(hBlock);
        return hBlock;
    }

    LONG         m_ref;    // COM 引用计数
    std::wstring m_path;   // 本假对象每次 GetData 都交出去的那条路径
};

/**
 *  把客户区坐标换成 IDropTarget 各方法要的屏幕坐标。
 *    hWnd：    宿主窗口。
 *    ptClient：客户区坐标。
 *  @return 屏幕坐标。
 */
POINTL ToScreenPoint(HWND hWnd, const POINT& ptClient)
{
    POINT pt = ptClient;
    ::ClientToScreen(hWnd, &pt);

    POINTL ptl;
    ptl.x = pt.x;
    ptl.y = pt.y;
    return ptl;
}

// 取一个矩形的中心点。测试要往「消息区里」「发送框里」各投一个点，取中心最稳妥。
POINT CenterOf(const RECT& rc)
{
    POINT pt;
    pt.x = (rc.left + rc.right) / 2;
    pt.y = (rc.top + rc.bottom) / 2;
    return pt;
}

/**
 *  OLE 初始化的作用域守卫。
 *
 *  RegisterDragDrop 要求调用线程先 OleInitialize，否则一律失败 —— 客户端在
 *  WinMain 里做过一次（Startup.cpp），测试进程得自己做。
 */
class OleScope
{
public:
    OleScope()
    {
        m_hr = ::OleInitialize(NULL);
    }

    ~OleScope()
    {
        if (SUCCEEDED(m_hr))
        {
            ::OleUninitialize();
        }
    }

    // OLE 是否初始化成功。失败时本组用例没有意义，调用方直接 REQUIRE 掉。
    bool Ok() const
    {
        return SUCCEEDED(m_hr);
    }

private:
    HRESULT m_hr;   // OleInitialize 的返回值，析构时据它决定要不要配平
};

/**
 *  建一个<u>不显示</u>的顶层 DUI 窗口。
 *
 *  拖放目标的注册与窗口可见不可见无关，所以不加 WS_VISIBLE —— 否则每跑一次
 *  测试都会有一串窗口在屏幕上闪过。
 *    frame：[出参] 待创建的窗口对象，须尚未创建。
 *  @return 创建成功返回 true。
 */
bool CreateHiddenFrame(DuiFrameWindow& frame)
{
    RECT rc;
    rc.left   = 0;
    rc.top    = 0;
    rc.right  = kFrameWidth;
    rc.bottom = kFrameHeight;

    HWND hWnd = frame.Create(NULL, rc, _T("ChatDropRegistrationTests"), WS_OVERLAPPEDWINDOW);
    return hWnd != NULL;
}

/**
 *  拿探针去注册这个窗口的拖放目标，返回系统给的结果。
 *
 *  注册成功的话立刻撤销并释放探针，不给后面的用例留下残留状态 —— 本函数只是
 *  在问「这个窗口的拖放目标现在空着吗」，不是真要接收拖放。
 *    hWnd：待检查的窗口。
 *  @return RegisterDragDrop 的返回值：S_OK 表示原本空着，
 *          DRAGDROP_E_ALREADYREGISTERED 表示已经被别人占了。
 */
HRESULT ProbeDropRegistration(HWND hWnd)
{
    ProbeDropTarget* pProbe = new ProbeDropTarget();
    const HRESULT hr = ::RegisterDragDrop(hWnd, pProbe);
    if (SUCCEEDED(hr))
    {
        ::RevokeDragDrop(hWnd);
    }
    pProbe->Release();
    return hr;
}

} // namespace

// =====================================================================
// CDR1 / CDR2：机制本身
// =====================================================================

TEST_CASE("CDR1 原生富文本控件会占走宿主窗口的拖放目标")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    //一个默认状态的富文本控件：它的拖放是开着的，第一次布局就会向宿主申请分发。
    std::unique_ptr<DuiControl> root(new DuiRichEdit());
    frame.SetClientContent(std::move(root));

    //宿主已经替它注册了分发器 —— 这正是聊天窗那份注册失败的根源。
    CHECK(frame.IsDropDispatchEnabled());
    CHECK(ProbeDropRegistration(frame.m_hWnd) == DRAGDROP_E_ALREADYREGISTERED);

    frame.DestroyWindow();
}

TEST_CASE("CDR2 宿主让出分发之后窗口的拖放目标可以重新注册")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    std::unique_ptr<DuiControl> root(new DuiRichEdit());
    frame.SetClientContent(std::move(root));
    REQUIRE(frame.IsDropDispatchEnabled());

    //让出：撤销宿主那份注册。这是聊天窗那侧的兜底做法。
    frame.EnableDropDispatch(false);

    CHECK_FALSE(frame.IsDropDispatchEnabled());
    CHECK(ProbeDropRegistration(frame.m_hWnd) == S_OK);

    frame.DestroyWindow();
}

// =====================================================================
// CDR3 / CDR4：两个聊天窗的控件树都不许占走窗口的拖放目标
// =====================================================================

TEST_CASE("CDR3 单聊窗控件树不占走宿主窗口的拖放目标")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    //与真实单聊窗同一条构建路径（不预填 mock 消息，本组用例用不上）。
    std::unique_ptr<DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root);
    frame.SetClientContent(std::move(root));

    CHECK_FALSE(frame.IsDropDispatchEnabled());
    CHECK(ProbeDropRegistration(frame.m_hWnd) == S_OK);

    frame.DestroyWindow();
}

TEST_CASE("CDR4 群聊窗控件树不占走宿主窗口的拖放目标")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    std::unique_ptr<DuiControl> root = balloonchatwnd::BuildGroupChatWindowBody(false);
    REQUIRE(root);
    frame.SetClientContent(std::move(root));

    CHECK_FALSE(frame.IsDropDispatchEnabled());
    CHECK(ProbeDropRegistration(frame.m_hWnd) == S_OK);

    frame.DestroyWindow();
}

// =====================================================================
// CDR5：注册之后不能被后续布局抢走
// =====================================================================

TEST_CASE("CDR5 聊天窗注册拖放目标之后再布局不会被抢走")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    std::unique_ptr<DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root);
    frame.SetClientContent(std::move(root));

    //模拟聊天窗那一步：把自己的拖放目标注册上去，并<u>保持注册</u>。
    ProbeDropTarget* pOwn = new ProbeDropTarget();
    REQUIRE(::RegisterDragDrop(frame.m_hWnd, pOwn) == S_OK);

    //窗口尺寸变化、显隐变化都会引发重新布局。富文本控件申请分发的那段代码就在
    //布局里，所以必须确认它不会在事后把注册抢回去。
    DuiControl* pContent = frame.GetClientContent();
    REQUIRE(pContent != NULL);
    RECT rc = pContent->GetRect();
    pContent->ForceLayout(rc);

    //仍然是我们那份：宿主没有开分发，而窗口的拖放目标是占着的。
    CHECK_FALSE(frame.IsDropDispatchEnabled());
    CHECK(ProbeDropRegistration(frame.m_hWnd) == DRAGDROP_E_ALREADYREGISTERED);

    ::RevokeDragDrop(frame.m_hWnd);
    pOwn->Release();

    frame.DestroyWindow();
}

// =====================================================================
// CDR6 / CDR7：聊天窗拖放接收器本身
// =====================================================================

TEST_CASE("CDR6 拖放接收器在带聊天布局的窗口上能注册成功")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    std::unique_ptr<DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root);
    frame.SetClientContent(std::move(root));

    //这一条是端到端的：走的就是聊天窗 InitUi 里那一句。它失败就等于实机上
    //"拖文件进聊天窗没反应"。
    ProbeDropSink sink;
    balloonchatwnd::ChatDropController ctrl;
    CHECK(ctrl.Attach(&frame, NULL, &sink));
    CHECK(ctrl.IsAttached());

    ctrl.Detach();
    frame.DestroyWindow();
}

TEST_CASE("CDR7 窗口已被别的控件占用时接收器让出之后仍能注册成功")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    //故意放一个拖放开着的原生富文本控件，让宿主先把窗口的拖放目标占走 ——
    //模拟"将来又有别的控件来申请分发"这种情形。
    std::unique_ptr<DuiControl> root(new DuiRichEdit());
    frame.SetClientContent(std::move(root));
    REQUIRE(frame.IsDropDispatchEnabled());

    //接收器应当自己把窗口让出来再注册，而不是就此失败。
    ProbeDropSink sink;
    balloonchatwnd::ChatDropController ctrl;
    CHECK(ctrl.Attach(&frame, NULL, &sink));
    CHECK(ctrl.IsAttached());
    CHECK_FALSE(frame.IsDropDispatchEnabled());

    ctrl.Detach();
    frame.DestroyWindow();
}

// =====================================================================
// CDR8 / CDR9：把「进入 → 移动 → 松手」整条流程走一遍
//
// 这两条针对的是 2026-08-17 实测的第二个缺陷：拖放接收器原先把「进入点在不在
// 发送框内」直接当作 DragEnter 的返回值，而库那一侧约定返回 false 表示<u>整次</u>
// 拖放拒收，此后既不再逐帧询问也不会派发投放。光标是从窗口边缘进来的，进入点
// 几乎总在上方的消息区，于是每一次拖放都在进窗口那一瞬间被判死。
// =====================================================================

TEST_CASE("CDR8 从消息区进入再移到发送框松手，路径能送达")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    std::unique_ptr<DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root);
    frame.SetClientContent(std::move(root));

    //按真实聊天窗的口径取两块区域：发送框是输入框的父容器，消息区是那个列表控件。
    DuiControl* pContent = frame.GetClientContent();
    REQUIRE(pContent != NULL);
    DuiControl* pInput = pContent->FindCtrlById(balloonchatwnd::kIdInputEdit);
    REQUIRE(pInput != NULL);
    DuiControl* pSendBox = pInput->GetParent();
    REQUIRE(pSendBox != NULL);
    DuiControl* pMsgArea = pContent->FindCtrlById(balloonchatwnd::kIdMessageArea);
    REQUIRE(pMsgArea != NULL);

    const RECT rcSendBox = pSendBox->GetRect();
    const RECT rcMsgArea = pMsgArea->GetRect();
    REQUIRE(rcSendBox.bottom > rcSendBox.top);
    REQUIRE(rcMsgArea.bottom > rcMsgArea.top);

    ProbeDropSink sink;
    sink.SetZone(rcSendBox);

    balloonchatwnd::ChatDropController ctrl;
    REQUIRE(ctrl.Attach(&frame, NULL, &sink));

    IDropTarget* pTarget = ctrl.GetFrameDropTarget();
    REQUIRE(pTarget != NULL);

    const std::wstring kDroppedFolder = L"D:\\work\\项目资料";
    ProbeFilesDataObject* pData = new ProbeFilesDataObject(kDroppedFolder);

    //第一步：光标从消息区进入窗口。此处<u>不是</u>投放区，提示不该亮，但整次拖放
    //也不该就此作废。
    const POINT ptEnter = CenterOf(rcMsgArea);
    DWORD dwEffect = DROPEFFECT_COPY;
    REQUIRE(pTarget->DragEnter(pData, MK_LBUTTON, ToScreenPoint(frame.m_hWnd, ptEnter),
                               &dwEffect) == S_OK);
    CHECK_FALSE(sink.m_hintVisible);

    //第二步：移到发送框上。逐帧判定必须在这里改判成「能放」，提示也该亮起来。
    const POINT ptOver = CenterOf(rcSendBox);
    dwEffect = DROPEFFECT_COPY;
    REQUIRE(pTarget->DragOver(MK_LBUTTON, ToScreenPoint(frame.m_hWnd, ptOver),
                              &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_COPY);
    CHECK(sink.m_hintVisible);

    //第三步：松手。路径要如实送到聊天窗那一侧。
    dwEffect = DROPEFFECT_COPY;
    REQUIRE(pTarget->Drop(pData, MK_LBUTTON, ToScreenPoint(frame.m_hWnd, ptOver),
                          &dwEffect) == S_OK);
    CHECK(sink.m_dropCount == 1);
    REQUIRE(sink.m_paths.size() == 1);
    CHECK(sink.m_paths[0] == kDroppedFolder);

    pData->Release();
    ctrl.Detach();
    frame.DestroyWindow();
}

TEST_CASE("CDR9 最终落点仍在消息区时不当成发送")
{
    OleScope ole;
    REQUIRE(ole.Ok());

    DuiFrameWindow frame;
    REQUIRE(CreateHiddenFrame(frame));

    std::unique_ptr<DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root);
    frame.SetClientContent(std::move(root));

    DuiControl* pContent = frame.GetClientContent();
    REQUIRE(pContent != NULL);
    DuiControl* pInput = pContent->FindCtrlById(balloonchatwnd::kIdInputEdit);
    REQUIRE(pInput != NULL);
    DuiControl* pSendBox = pInput->GetParent();
    REQUIRE(pSendBox != NULL);
    DuiControl* pMsgArea = pContent->FindCtrlById(balloonchatwnd::kIdMessageArea);
    REQUIRE(pMsgArea != NULL);

    ProbeDropSink sink;
    sink.SetZone(pSendBox->GetRect());

    balloonchatwnd::ChatDropController ctrl;
    REQUIRE(ctrl.Attach(&frame, NULL, &sink));

    IDropTarget* pTarget = ctrl.GetFrameDropTarget();
    REQUIRE(pTarget != NULL);

    ProbeFilesDataObject* pData = new ProbeFilesDataObject(L"D:\\work\\素材");

    //整个过程都待在消息区里：能放的判定始终为假，松手也不该被当成发送 ——
    //"整窗收下拖放"放宽的只是进入那一步，最终落点仍然要在发送框内才算数。
    const POINT ptMsg = CenterOf(pMsgArea->GetRect());
    DWORD dwEffect = DROPEFFECT_COPY;
    REQUIRE(pTarget->DragEnter(pData, MK_LBUTTON, ToScreenPoint(frame.m_hWnd, ptMsg),
                               &dwEffect) == S_OK);

    dwEffect = DROPEFFECT_COPY;
    REQUIRE(pTarget->DragOver(MK_LBUTTON, ToScreenPoint(frame.m_hWnd, ptMsg),
                              &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_NONE);
    CHECK_FALSE(sink.m_hintVisible);

    dwEffect = DROPEFFECT_COPY;
    REQUIRE(pTarget->Drop(pData, MK_LBUTTON, ToScreenPoint(frame.m_hWnd, ptMsg),
                          &dwEffect) == S_OK);
    CHECK(sink.m_dropCount == 0);

    pData->Release();
    ctrl.Detach();
    frame.DestroyWindow();
}
