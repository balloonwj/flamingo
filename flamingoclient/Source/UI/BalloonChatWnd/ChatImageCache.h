/**
 *  聊天图片"展示尺寸"缩放缓存与后台解码服务。
 *
 *  大图（如 2.48M 的 JPEG）首次显示时，在 UI 线程上解码原图 + 缩放到展示尺寸会造成
 *  几百毫秒的卡顿（拖动 / 滚动时尤其明显）。本服务把"按路径解码原图 + 缩放到展示
 *  尺寸"放到一个后台工作线程做：UI 线程绘制时只查缓存 —— 命中即拿到缩好的小图直接
 *  画；未命中则发起一次后台请求并先画占位，后台缩放完成后向已注册的聊天窗
 *  PostMessage(WM_CHAT_IMAGE_READY)，由窗口重排 / 重绘消息区让气泡再画一次（这次命
 *  中缓存）。如此首次解码大图也在后台线程，UI 线程零冻结。
 *
 *  缓存键 = 图片本地路径 + 展示宽 + 展示高（同一张图不同展示尺寸各缓存一份）。缩放
 *  结果按 app 生命周期常驻（一次聊天会话内图片数量有限）。本类是进程级单例，worker
 *  线程懒启动、随进程退出由 OS 回收（解码空闲时 worker 多在条件变量上等待，退出风险
 *  极低）。
 *
 *  线程安全：Get / Request / RegisterListener / UnregisterListener 均加锁；后台线程
 *  产出的 Gdiplus::Bitmap 在构造完成后才加锁发布到缓存，UI 线程加锁读取后只读不改。
 *
 *  典型用法（气泡绘制）：
 *      Gdiplus::Bitmap* img = ChatImageCache::Inst().Get(path, w, h);
 *      if (img != NULL) { g.DrawImage(img, rc); }
 *      else { ChatImageCache::Inst().Request(path, w, h); 画占位; }
 *  聊天窗创建后 RegisterListener(m_hWnd)、销毁前 UnregisterListener(m_hWnd)，并处理
 *  WM_CHAT_IMAGE_READY → 重绘消息区。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <windows.h>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>

// 仅用作指针类型，前置声明，避免在头文件引入 <gdiplus.h>。
namespace Gdiplus { class Bitmap; }

namespace balloonchatwnd {

class ChatImageCache
{
public:
    // 进程级单例。首次调用懒构造（故意泄漏，避免静态析构与 GdiplusShutdown 的顺序
    // 问题），worker 线程随进程退出由 OS 回收。
    static ChatImageCache& Inst();

    // 取已缓存的"展示尺寸"缩放图。命中返回非空指针（缓存持有，调用方只读、勿 delete /
    // 勿修改）；未命中或参数非法返回 NULL。线程安全；由 UI 线程在气泡绘制时调用。
    //   path：图片本地文件完整路径；w / h：展示尺寸（像素，须 > 0）。
    Gdiplus::Bitmap* Get(const std::wstring& path, int w, int h);

    // 请求后台解码 + 缩放（按缓存键去重；已缓存 / 进行中则忽略）。完成后向所有已注册
    // 窗口 PostMessage(WM_CHAT_IMAGE_READY)。线程安全；由气泡在 Get 未命中时调用。
    //   path / w / h：同 Get。
    void Request(const std::wstring& path, int w, int h);

    // 注册 / 注销"图片就绪需重绘"的窗口（聊天窗 HWND）。聊天窗创建后注册、销毁前注销，
    // 避免向已销毁窗口 PostMessage。线程安全。
    void RegisterListener(HWND hwnd);
    void UnregisterListener(HWND hwnd);

private:
    ChatImageCache();
    ~ChatImageCache();
    ChatImageCache(const ChatImageCache&);
    ChatImageCache& operator=(const ChatImageCache&);

    // 懒启动 worker 线程。调用方须已持有 m_mutex。
    void EnsureWorkerLocked();
    // 后台线程主循环：取任务 → 解码缩放 → 发布到缓存 → 通知窗口重绘。
    void WorkerLoop();

    // 由 (路径, 宽, 高) 组成缓存键。
    static std::wstring MakeKey(const std::wstring& path, int w, int h);

    // 一个待解码任务。
    struct Task
    {
        std::wstring m_path;   // 图片本地路径
        int          m_w;      // 展示宽
        int          m_h;      // 展示高
    };

    std::mutex                               m_mutex;        // 保护以下所有共享态
    std::condition_variable                  m_cv;           // worker 等待新任务
    std::deque<Task>                         m_queue;        // 待解码任务队列
    std::set<std::wstring>                   m_inProgress;   // 进行中的缓存键（请求去重）
    std::map<std::wstring, Gdiplus::Bitmap*> m_cache;        // 缓存键 → 缩放图（本类持有，须 delete）
    std::set<HWND>                           m_listeners;    // 就绪时待 PostMessage 重绘的窗口
    std::thread                              m_worker;       // 后台解码线程（懒启动）
    bool                                     m_workerStarted;// worker 是否已启动
    bool                                     m_stop;         // 请求 worker 退出（仅析构时；正常不触发）
};

} // namespace balloonchatwnd
