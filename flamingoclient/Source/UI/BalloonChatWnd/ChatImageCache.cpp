/**
 *  ChatImageCache.cpp —— 聊天图片缩放缓存与后台解码服务实现。
 *
 *  本 .cpp 与 ChatBubble.cpp / ChatMessageList.cpp 一样被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用，故<u>不</u> include Flamingo 的 stdafx.h，也不需要
 *  ATL/WTL —— 只用到 <windows.h> + <gdiplus.h> + 标准库 + 自定义消息号。在
 *  Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ChatImageCache.h"
#include "../../App/CustomMsgDef.h"   // WM_CHAT_IMAGE_READY

namespace balloonchatwnd {

namespace {

// 确保 GDI+ 在本模块（含 worker 线程）可用。多次 GdiplusStartup 是安全的（见
// BalloonProfileDlg.cpp 的说明）；本 TU 保留自己的 token，进程退出由 OS 回收，无须
// GdiplusShutdown。worker 线程启动时调一次即可。
void EnsureGdiplus()
{
    static ULONG_PTR s_token = 0;
    static bool      s_done  = false;
    if (!s_done)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&s_token, &input, NULL);
        s_done = true;
    }
}

} // anonymous namespace

ChatImageCache& ChatImageCache::Inst()
{
    // 故意泄漏：避免静态析构顺序与 GdiplusShutdown 冲突；worker 线程随进程退出由 OS
    // 回收。聊天图片缓存是 app 生命周期服务，泄漏一份单例无实际影响。
    static ChatImageCache* s_inst = new ChatImageCache();
    return *s_inst;
}

ChatImageCache::ChatImageCache()
    : m_workerStarted(false)
    , m_stop(false)
{
}

ChatImageCache::~ChatImageCache()
{
    // 单例故意泄漏，正常不会析构；此处仅为完整性：停 worker、释放缓存位图。
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    for (std::map<std::wstring, Gdiplus::Bitmap*>::iterator it = m_cache.begin();
         it != m_cache.end(); ++it)
    {
        delete it->second;
    }
}

std::wstring ChatImageCache::MakeKey(const std::wstring& path, int w, int h)
{
    wchar_t suffix[40] = { 0 };
    ::wsprintfW(suffix, L"|%dx%d", w, h);
    return path + suffix;
}

Gdiplus::Bitmap* ChatImageCache::Get(const std::wstring& path, int w, int h)
{
    if (path.empty() || w <= 0 || h <= 0)
    {
        return NULL;
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    std::map<std::wstring, Gdiplus::Bitmap*>::iterator it =
        m_cache.find(MakeKey(path, w, h));
    return (it != m_cache.end()) ? it->second : NULL;
}

void ChatImageCache::Request(const std::wstring& path, int w, int h)
{
    if (path.empty() || w <= 0 || h <= 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    std::wstring key = MakeKey(path, w, h);
    if (m_cache.find(key) != m_cache.end())
    {
        return;   // 已就绪
    }
    if (m_inProgress.find(key) != m_inProgress.end())
    {
        return;   // 已在排队 / 解码中
    }
    m_inProgress.insert(key);

    Task task;
    task.m_path = path;
    task.m_w    = w;
    task.m_h    = h;
    m_queue.push_back(task);

    EnsureWorkerLocked();
    m_cv.notify_one();
}

void ChatImageCache::EnsureWorkerLocked()
{
    if (!m_workerStarted)
    {
        m_workerStarted = true;
        m_worker = std::thread(&ChatImageCache::WorkerLoop, this);
    }
}

void ChatImageCache::RegisterListener(HWND hwnd)
{
    if (hwnd == NULL)
    {
        return;
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    m_listeners.insert(hwnd);
}

void ChatImageCache::UnregisterListener(HWND hwnd)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_listeners.erase(hwnd);
}

void ChatImageCache::WorkerLoop()
{
    EnsureGdiplus();
    for (;;)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            while (!m_stop && m_queue.empty())
            {
                m_cv.wait(lk);
            }
            if (m_stop)
            {
                return;
            }
            task = m_queue.front();
            m_queue.pop_front();
        }

        // ---- 解码 + 缩放（无锁，耗时操作）----
        // 把原图高质量缩到展示尺寸 task.m_w x task.m_h，得到一张展示尺寸的小图。
        Gdiplus::Bitmap* scaled = NULL;
        {
            Gdiplus::Bitmap src(task.m_path.c_str(), FALSE);
            if (src.GetLastStatus() == Gdiplus::Ok)
            {
                Gdiplus::Bitmap* dst = new Gdiplus::Bitmap(
                    task.m_w, task.m_h, PixelFormat32bppARGB);
                if (dst->GetLastStatus() == Gdiplus::Ok)
                {
                    Gdiplus::Graphics g(dst);
                    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    g.DrawImage(&src, Gdiplus::Rect(0, 0, task.m_w, task.m_h));
                    scaled = dst;
                }
                else
                {
                    delete dst;
                }
            }
        }

        // ---- 发布到缓存 + 取监听窗口快照（持锁）----
        std::set<HWND> listeners;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            std::wstring key = MakeKey(task.m_path, task.m_w, task.m_h);
            m_inProgress.erase(key);
            if (scaled != NULL)
            {
                m_cache[key] = scaled;   // 缓存持有，后续命中复用
            }
            listeners = m_listeners;
        }

        // ---- 通知窗口重绘（无锁 PostMessage）----
        // 解码失败也通知无副作用（占位维持现状）；广播给所有聊天窗，由各窗口的气泡按
        // 自己的图片路径查缓存，命中才换成真图。
        for (std::set<HWND>::iterator it = listeners.begin(); it != listeners.end(); ++it)
        {
            if (::IsWindow(*it))
            {
                ::PostMessage(*it, WM_CHAT_IMAGE_READY, 0, 0);
            }
        }
    }
}

} // namespace balloonchatwnd
