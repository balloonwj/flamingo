/**
 *  flamingoAdmin —— 文件共享管理组件与宿主之间的转发端口实现，说明见 FileShareUi.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Screens/FileShareUi.h"
#include "Util/Logger.h"

namespace flamingoadmin { namespace fsui {

Port::Port()
    : m_sink(nullptr)
    , m_sinkUser(nullptr)
    , m_uiHandler(nullptr)
    , m_uiHandlerUser(nullptr)
{
}

void Port::SetRequestSink(RequestSinkFn fn, void* user)
{
    m_sink = fn;
    m_sinkUser = user;
}

void Port::SetUiRequestHandler(UiRequestFn fn, void* user)
{
    m_uiHandler = fn;
    m_uiHandlerUser = user;
}

void Port::Send(HttpRequest& request, ApiTag tag) const
{
    request.m_tag = tag;
    if (m_sink == nullptr)
    {
        FA_LOGW(_T("file share ui: no request sink, request dropped, tag: %d"), static_cast<int>(tag));
        return;
    }
    m_sink(m_sinkUser, request);
}

void Port::Toast(const CString& text, bool isError) const
{
    UiRequest req;
    req.m_kind = kUiToast;
    req.m_text = text;
    req.m_isError = isError;
    Raise(req);
}

void Port::Confirm(const CString& title, const CString& text, const CString& okText, bool danger,
                   ConfirmTarget* target) const
{
    UiRequest req;
    req.m_kind = kUiConfirm;
    req.m_title = title;
    req.m_text = text;
    req.m_okText = okText;
    req.m_danger = danger;
    req.m_target = target;
    Raise(req);
}

void Port::Raise(const UiRequest& request) const
{
    if (m_uiHandler == nullptr)
    {
        FA_LOGW(_T("file share ui: no ui handler, ui request dropped, kind: %d"), request.m_kind);
        //没有宿主时确认框不会弹出，直接按取消回告，发起方才能把界面恢复原状
        if (request.m_kind == kUiConfirm && request.m_target != nullptr)
            request.m_target->OnConfirmResult(false);
        return;
    }
    m_uiHandler(m_uiHandlerUser, request);
}

}} // namespace flamingoadmin::fsui
