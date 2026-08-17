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
#include "../IULog.h"   // LOG_INFO
#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // EnableDropDispatch

namespace balloonchatwnd {

ChatDropController::ChatDropController()
    : m_sink(NULL)
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
    m_target.Unregister();
    m_inputTarget.Unregister();
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
