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
 *  解码失败（文件不存在、内容损坏、格式不支持）会连同文件当时的状态一起记下：IsFailed
 *  可查询，界面据此画破图占位；文件不变时再 Request 不会重新排队（否则"解码失败 → 通知
 *  重绘 → 重绘时再请求"会一直循环），文件一旦变化（如后来被下载下来）则自动清除记录、
 *  重新解码。
 *
 *  线程安全：Get / Request / IsFailed / RegisterListener / UnregisterListener 均加锁；后台线程
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

    // 查询这张图按该展示尺寸解码是否已失败（文件不存在、内容损坏或格式不支持）。线程安全；
    // 由 UI 线程在绘制时调用，用来决定画"加载中"占位还是破图占位。
    //   path / w / h：同 Get。
    // 返回：记录过失败、且文件自失败以来没有变化时返回 true。文件后来被下载下来或被替换
    //       （是否存在、大小、修改时间任一变化）时自动清除这条失败记录并返回 false，下一次
    //       Request 会重新解码。
    // 副作用：可能清除过期的失败记录；会读一次文件属性（仅对记录过失败的图片）。
    bool IsFailed(const std::wstring& path, int w, int h);

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

    // 把一个解码任务放进队列并唤醒 worker。调用方须已持有 m_mutex，并已确认该键
    // 既未缓存、也不在进行中。
    void EnqueueLocked(const std::wstring& key, const std::wstring& path, int w, int h);
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

    // 文件在某一时刻的状态，用来判断解码失败之后文件有没有变化。
    struct FileStamp
    {
        bool             m_exists;      // 文件是否存在
        ULARGE_INTEGER   m_size;        // 文件大小（字节）；不存在时为 0
        FILETIME         m_lastWrite;   // 最后修改时间；不存在时为 0
    };

    // 读取文件当前的状态（不加锁，调用方无须持有 m_mutex）。
    static FileStamp ReadFileStamp(const std::wstring& path);

    // 两份文件状态是否完全相同。
    static bool SameStamp(const FileStamp& a, const FileStamp& b);

    // 检查 key 对应的失败记录是否仍然有效：仍有效返回 true；文件已变化则删除记录并返回
    // false；没有记录返回 false。调用方须已持有 m_mutex，current 须在加锁前读好。
    bool CheckFailureLocked(const std::wstring& key, const FileStamp& current);

    std::mutex                               m_mutex;        // 保护以下所有共享态
    std::condition_variable                  m_cv;           // worker 等待新任务
    std::deque<Task>                         m_queue;        // 待解码任务队列
    std::set<std::wstring>                   m_inProgress;   // 进行中的缓存键（请求去重）
    std::map<std::wstring, Gdiplus::Bitmap*> m_cache;        // 缓存键 → 缩放图（本类持有，须 delete）
    std::map<std::wstring, FileStamp>        m_failed;       // 解码失败的缓存键 → 解码前读到的文件状态；
                                                             // 文件状态变化后由 CheckFailureLocked 删除
    std::set<HWND>                           m_listeners;    // 就绪时待 PostMessage 重绘的窗口
    std::thread                              m_worker;       // 后台解码线程（懒启动）
    bool                                     m_workerStarted;// worker 是否已启动
    bool                                     m_stop;         // 请求 worker 退出（仅析构时；正常不触发）
};

} // namespace balloonchatwnd
