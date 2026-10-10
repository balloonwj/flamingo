/**
 *  ScreenshotRequestBuilder.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ScreenshotRequestBuilder.h"

namespace balloonscreenshot {

ScreenshotOptions::ScreenshotOptions()
    : m_excludeHwnd(NULL)
    , m_startFullScreen(false)
    , m_sendFn(NULL)
    , m_stageFn(NULL)
    , m_ctx(NULL)
{
}

flamingoshot::ScreenshotRequest MakeScreenshotRequest(HWND owner, const ScreenshotOptions& opts)
{
    flamingoshot::ScreenshotRequest request;
    request.m_cbSize = sizeof(request);
    request.m_owner = owner;
    request.m_excludeHwnd = opts.m_excludeHwnd;
    request.m_startFullScreen = opts.m_startFullScreen;
    return request;
}

CScreenshotCallbackSink::CScreenshotCallbackSink(const ScreenshotOptions& opts)
    : m_sendFn(opts.m_sendFn)
    , m_stageFn(opts.m_stageFn)
    , m_ctx(opts.m_ctx)
{
}

bool CScreenshotCallbackSink::HasTarget() const
{
    return m_sendFn != NULL || m_stageFn != NULL;
}

void CScreenshotCallbackSink::OnSend(const wchar_t* pngPath)
{
    if (m_sendFn != NULL)
    {
        m_sendFn(m_ctx, pngPath);
    }
}

void CScreenshotCallbackSink::OnStage(const wchar_t* pngPath)
{
    if (m_stageFn != NULL)
    {
        m_stageFn(m_ctx, pngPath);
    }
}

} // namespace balloonscreenshot
