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
#include <objidl.h>
#include <shlwapi.h>   // SHCreateMemStream：内存图片经 IStream 交给 GDI+ 解码
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

#include "ChatImageCache.h"
#include "../../App/CustomMsgDef.h"   // WM_CHAT_IMAGE_READY

namespace balloonchatwnd {

namespace {

// 确保 GDI+ 在本模块（含 worker 线程）可用。多次 GdiplusStartup 是安全的（原见
// BalloonProfileDlg.cpp 的说明，该文件 2026-10-07 已删除）；本 TU 保留自己的 token，进程退出由 OS 回收，无须
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

const wchar_t* const ChatImageCache::kMemoryImageScheme = L"secretmem://";

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

std::wstring ChatImageCache::MakeKey(const std::wstring& path, int w, int h, int fit)
{
    wchar_t suffix[40] = { 0 };
    if (fit == kImageFitCover)
    {
        ::wsprintfW(suffix, L"|%dx%d|cover", w, h);
    }
    else
    {
        ::wsprintfW(suffix, L"|%dx%d", w, h);
    }
    return path + suffix;
}

void ChatImageCache::ComputeCoverCrop(int srcW, int srcH, int dstW, int dstH,
                                      int& outX, int& outY, int& outW, int& outH)
{
    outX = 0;
    outY = 0;
    outW = srcW;
    outH = srcH;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
    {
        return;
    }
    //比较 srcW/srcH 与 dstW/dstH 两个比例：交叉相乘，避免除法的误差；用 64 位防止大图溢出
    const long long srcCross = (long long)srcW * (long long)dstH;
    const long long dstCross = (long long)dstW * (long long)srcH;
    //取一半用的除数：四舍五入时加半个除数，居中时把余量平分到两边
    const long long kHalf = 2;
    if (srcCross > dstCross)
    {
        //原图更宽：高度取满，宽度按展示比例算出，左右两边裁掉
        long long w = ((long long)srcH * (long long)dstW + (long long)dstH / kHalf) / (long long)dstH;
        if (w < 1)
        {
            w = 1;
        }
        if (w > srcW)
        {
            w = srcW;
        }
        outW = (int)w;
        outX = (srcW - outW) / (int)kHalf;
    }
    else if (srcCross < dstCross)
    {
        //原图更高：宽度取满，高度按展示比例算出，上下两边裁掉
        long long h = ((long long)srcW * (long long)dstH + (long long)dstW / kHalf) / (long long)dstW;
        if (h < 1)
        {
            h = 1;
        }
        if (h > srcH)
        {
            h = srcH;
        }
        outH = (int)h;
        outY = (srcH - outH) / (int)kHalf;
    }
}

Gdiplus::Bitmap* ChatImageCache::ScaleToDisplay(Gdiplus::Bitmap* src, int w, int h, int fit)
{
    Gdiplus::Bitmap* dst = new Gdiplus::Bitmap(w, h, PixelFormat32bppARGB);
    if (dst->GetLastStatus() != Gdiplus::Ok)
    {
        delete dst;
        return NULL;
    }
    Gdiplus::Graphics g(dst);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    if (fit == kImageFitCover)
    {
        int cropX = 0;
        int cropY = 0;
        int cropW = 0;
        int cropH = 0;
        ComputeCoverCrop((int)src->GetWidth(), (int)src->GetHeight(), w, h, cropX, cropY, cropW, cropH);
        //图片边缘按镜像取样：不设置的话，高质量插值会在原图边界外取到透明像素，裁切区域贴着
        //原图边缘时，结果的四周会出现一圈半透明的浅边
        Gdiplus::ImageAttributes attr;
        attr.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        g.DrawImage(src, Gdiplus::Rect(0, 0, w, h), cropX, cropY, cropW, cropH, Gdiplus::UnitPixel, &attr);
    }
    else
    {
        g.DrawImage(src, Gdiplus::Rect(0, 0, w, h));
    }
    return dst;
}

Gdiplus::Bitmap* ChatImageCache::Get(const std::wstring& path, int w, int h, int fit)
{
    if (path.empty() || w <= 0 || h <= 0)
    {
        return NULL;
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    std::map<std::wstring, Gdiplus::Bitmap*>::iterator it =
        m_cache.find(MakeKey(path, w, h, fit));
    return (it != m_cache.end()) ? it->second : NULL;
}

void ChatImageCache::Request(const std::wstring& path, int w, int h, int fit)
{
    if (path.empty() || w <= 0 || h <= 0)
    {
        return;
    }
    const std::wstring key = MakeKey(path, w, h, fit);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_cache.find(key) != m_cache.end())
        {
            return;   // 已就绪
        }
        if (m_inProgress.find(key) != m_inProgress.end())
        {
            return;   // 已在排队 / 解码中
        }
        if (m_failed.find(key) == m_failed.end())
        {
            EnqueueLocked(key, path, w, h, fit);
            return;
        }
    }

    // 记录过失败：文件没有变化就不再排队。否则每次重绘都会再解码一次、解码完又通知
    // 重绘，形成一个停不下来的循环。读文件属性放在锁外，读完再回到锁里判断。
    const FileStamp current = ReadFileStamp(path);
    std::lock_guard<std::mutex> lk(m_mutex);
    if (CheckFailureLocked(key, current))
    {
        return;
    }
    // 两次加锁之间别的线程可能已经把它排进队列，这里再确认一遍。
    if (m_cache.find(key) != m_cache.end() || m_inProgress.find(key) != m_inProgress.end())
    {
        return;
    }
    EnqueueLocked(key, path, w, h, fit);
}

bool ChatImageCache::IsFailed(const std::wstring& path, int w, int h, int fit)
{
    if (path.empty() || w <= 0 || h <= 0)
    {
        return false;
    }
    const std::wstring key = MakeKey(path, w, h, fit);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_failed.find(key) == m_failed.end())
        {
            return false;   // 绝大多数图片走这里，不必读文件属性
        }
    }
    const FileStamp current = ReadFileStamp(path);
    std::lock_guard<std::mutex> lk(m_mutex);
    return CheckFailureLocked(key, current);
}

void ChatImageCache::EnqueueLocked(const std::wstring& key, const std::wstring& path, int w, int h, int fit)
{
    m_inProgress.insert(key);

    Task task;
    task.m_path = path;
    task.m_w    = w;
    task.m_h    = h;
    task.m_fit  = fit;
    m_queue.push_back(task);

    EnsureWorkerLocked();
    m_cv.notify_one();
}

ChatImageCache::FileStamp ChatImageCache::ReadFileStamp(const std::wstring& path)
{
    FileStamp stamp;
    stamp.m_exists = false;
    stamp.m_size.QuadPart = 0;
    stamp.m_lastWrite.dwLowDateTime  = 0;
    stamp.m_lastWrite.dwHighDateTime = 0;

    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
    {
        return stamp;   // 不存在或无法访问，都按"不存在"记
    }
    stamp.m_exists = true;
    stamp.m_size.LowPart  = data.nFileSizeLow;
    stamp.m_size.HighPart = data.nFileSizeHigh;
    stamp.m_lastWrite     = data.ftLastWriteTime;
    return stamp;
}

bool ChatImageCache::SameStamp(const FileStamp& a, const FileStamp& b)
{
    return a.m_exists == b.m_exists
        && a.m_size.QuadPart == b.m_size.QuadPart
        && ::CompareFileTime(&a.m_lastWrite, &b.m_lastWrite) == 0;
}

bool ChatImageCache::CheckFailureLocked(const std::wstring& key, const FileStamp& current)
{
    std::map<std::wstring, FileStamp>::iterator it = m_failed.find(key);
    if (it == m_failed.end())
    {
        return false;
    }
    if (SameStamp(it->second, current))
    {
        return true;
    }
    // 文件自失败以来有变化（多半是后来被下载下来了）：删掉失败记录，让它重新解码。
    m_failed.erase(it);
    return false;
}

void ChatImageCache::EnsureWorkerLocked()
{
    if (!m_workerStarted)
    {
        m_workerStarted = true;
        m_worker = std::thread(&ChatImageCache::WorkerLoop, this);
    }
}

bool ChatImageCache::IsMemoryPath(const std::wstring& path)
{
    const size_t schemeLen = ::wcslen(kMemoryImageScheme);
    return path.size() > schemeLen && path.compare(0, schemeLen, kMemoryImageScheme) == 0;
}

bool ChatImageCache::PutMemoryImage(const std::wstring& path, const void* bytes, size_t len)
{
    if (!IsMemoryPath(path) || bytes == NULL || len == 0)
    {
        return false;
    }
    const unsigned char* begin = static_cast<const unsigned char*>(bytes);
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<unsigned char>& slot = m_memImages[path];
    if (!slot.empty())
    {
        ::SecureZeroMemory(&slot[0], slot.size());
    }
    slot.assign(begin, begin + len);
    //同一伪路径换了内容：作废按旧内容缩放过的位图与失败记录（键为「伪路径|宽x高」，居中裁切的再加后缀）
    const std::wstring keyPrefix = path + L"|";
    for (std::map<std::wstring, Gdiplus::Bitmap*>::iterator it = m_cache.begin(); it != m_cache.end();)
    {
        if (it->first.compare(0, keyPrefix.size(), keyPrefix) == 0)
        {
            WipeAndDeleteBitmap(it->second);
            m_cache.erase(it++);
        }
        else
        {
            ++it;
        }
    }
    for (std::map<std::wstring, FileStamp>::iterator it = m_failed.begin(); it != m_failed.end();)
    {
        if (it->first.compare(0, keyPrefix.size(), keyPrefix) == 0)
        {
            m_failed.erase(it++);
        }
        else
        {
            ++it;
        }
    }
    return true;
}

bool ChatImageCache::CopyMemoryImage(const std::wstring& path, std::vector<unsigned char>& out)
{
    out.clear();
    std::lock_guard<std::mutex> lk(m_mutex);
    std::map<std::wstring, std::vector<unsigned char> >::const_iterator it = m_memImages.find(path);
    if (it == m_memImages.end() || it->second.empty())
    {
        return false;
    }
    out = it->second;
    return true;
}

size_t ChatImageCache::RemoveMemoryImages(const std::wstring& prefix)
{
    if (!IsMemoryPath(prefix) && prefix != kMemoryImageScheme)
    {
        return 0;
    }
    size_t removed = 0;
    std::lock_guard<std::mutex> lk(m_mutex);
    for (std::map<std::wstring, std::vector<unsigned char> >::iterator it = m_memImages.begin(); it != m_memImages.end();)
    {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
        {
            if (!it->second.empty())
            {
                ::SecureZeroMemory(&it->second[0], it->second.size());
            }
            m_memImages.erase(it++);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    for (std::map<std::wstring, Gdiplus::Bitmap*>::iterator it = m_cache.begin(); it != m_cache.end();)
    {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
        {
            WipeAndDeleteBitmap(it->second);
            m_cache.erase(it++);
        }
        else
        {
            ++it;
        }
    }
    for (std::map<std::wstring, FileStamp>::iterator it = m_failed.begin(); it != m_failed.end();)
    {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
        {
            m_failed.erase(it++);
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

Gdiplus::Bitmap* ChatImageCache::DecodeFromMemory(const std::vector<unsigned char>& bytes)
{
    if (bytes.empty())
    {
        return NULL;
    }
    IStream* stream = ::SHCreateMemStream(&bytes[0], static_cast<UINT>(bytes.size()));
    if (stream == NULL)
    {
        return NULL;
    }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream, FALSE);
    if (bmp != NULL && bmp->GetLastStatus() != Gdiplus::Ok)
    {
        delete bmp;
        bmp = NULL;
    }
    //GDI+ 的 Bitmap 在整个生命周期里都可能回头读流，因此先拷成一张与流无关的 32 位位图再释放流
    Gdiplus::Bitmap* copy = NULL;
    if (bmp != NULL)
    {
        copy = new Gdiplus::Bitmap(bmp->GetWidth(), bmp->GetHeight(), PixelFormat32bppARGB);
        if (copy->GetLastStatus() == Gdiplus::Ok)
        {
            Gdiplus::Graphics g(copy);
            g.DrawImage(bmp, 0, 0, bmp->GetWidth(), bmp->GetHeight());
        }
        else
        {
            delete copy;
            copy = NULL;
        }
        delete bmp;
    }
    stream->Release();
    return copy;
}

void ChatImageCache::WipeAndDeleteBitmap(Gdiplus::Bitmap* bmp)
{
    if (bmp == NULL)
    {
        return;
    }
    Gdiplus::Rect rc(0, 0, bmp->GetWidth(), bmp->GetHeight());
    Gdiplus::BitmapData data;
    if (rc.Width > 0 && rc.Height > 0
        && bmp->LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok)
    {
        const int stride = data.Stride >= 0 ? data.Stride : -data.Stride;
        for (UINT y = 0; y < data.Height; ++y)
        {
            unsigned char* row = static_cast<unsigned char*>(data.Scan0) + static_cast<size_t>(y) * data.Stride;
            ::SecureZeroMemory(row, static_cast<size_t>(stride));
        }
        bmp->UnlockBits(&data);
    }
    delete bmp;
}

void ChatImageCache::RegisterListener(HWND hwnd)
{
    if (!::IsWindow(hwnd))
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

        // 解码前先记下文件状态。解码失败时连同它一起记下，之后文件一旦变化（被下载下来、
        // 被替换）就能认出来并重新解码。必须在解码之前读：若在解码之后读，解码期间恰好
        // 写好的新文件会被当成"失败时的样子"，从此再也不会重试。
        // 内存图片没有文件，读到的总是「不存在」，失败记录只能由 PutMemoryImage / RemoveMemoryImages 清除。
        const bool fromMemory = IsMemoryPath(task.m_path);
        const FileStamp before = ReadFileStamp(task.m_path);

        // 内存图片：持锁拷出字节，在锁外解码；已被删除（退出了密聊）时按失败处理
        Gdiplus::Bitmap* memSrc = NULL;
        if (fromMemory)
        {
            std::vector<unsigned char> bytes;
            CopyMemoryImage(task.m_path, bytes);
            memSrc = DecodeFromMemory(bytes);
            if (!bytes.empty())
            {
                ::SecureZeroMemory(&bytes[0], bytes.size());
            }
        }

        // ---- 解码 + 缩放（无锁，耗时操作）----
        // 把原图按 task.m_fit 高质量缩到展示尺寸 task.m_w x task.m_h，得到一张展示尺寸的小图。
        Gdiplus::Bitmap* scaled = NULL;
        if (fromMemory)
        {
            if (memSrc != NULL)
            {
                scaled = ScaleToDisplay(memSrc, task.m_w, task.m_h, task.m_fit);
                WipeAndDeleteBitmap(memSrc);
            }
        }
        else
        {
            Gdiplus::Bitmap src(task.m_path.c_str(), FALSE);
            if (src.GetLastStatus() == Gdiplus::Ok)
            {
                scaled = ScaleToDisplay(&src, task.m_w, task.m_h, task.m_fit);
            }
        }

        // ---- 发布到缓存 + 取监听窗口快照（持锁）----
        std::set<HWND> listeners;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            std::wstring key = MakeKey(task.m_path, task.m_w, task.m_h, task.m_fit);
            m_inProgress.erase(key);
            //内存图片在解码期间被删除了（退出了密聊）：丢弃解码结果，不再发布
            if (scaled != NULL && fromMemory && m_memImages.find(task.m_path) == m_memImages.end())
            {
                WipeAndDeleteBitmap(scaled);
                scaled = NULL;
            }
            if (scaled != NULL)
            {
                m_cache[key] = scaled;   // 缓存持有，后续命中复用
                m_failed.erase(key);
            }
            else
            {
                m_failed[key] = before;  // 记下失败；文件不变就不再排队解码
            }
            listeners = m_listeners;
        }

        // ---- 通知窗口重绘（无锁 PostMessage）----
        // 解码失败也要通知：界面据此重绘，经 IsFailed 查到失败后改画破图占位。失败已记下，
        // 重绘时再 Request 不会重新排队，所以不会形成循环。广播给所有已登记的窗口，由各
        // 窗口按自己的图片路径查缓存，命中才换成真图。
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
