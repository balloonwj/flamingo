/**
 *  分享者一端按文件名搜索 ShareSearch 的实现，见 ShareSearch.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareSearch.h"

#include <windows.h>
#include <deque>

#include "ShareFileOps.h"
#include "SharePathGuard.h"
#include "ShareWinPath.h"

namespace fileshare {

namespace {

//控制字符的上界：码点小于它的都是控制字符，不允许出现在关键字里
const wchar_t kFirstPrintable = 0x20;

//一个待遍历的目录
struct PendingDir
{
    std::wstring m_abs;    //本机绝对路径
    std::string  m_rel;    //相对共享根的路径
    int          m_depth;  //离起点目录的层数（起点为 0）
};

//转成小写（按系统的大小写映射；只用于比较）
std::wstring Lower(const std::wstring& s)
{
    std::wstring out = s;
    if (!out.empty())
        ::CharLowerBuffW(&out[0], static_cast<DWORD>(out.size()));
    return out;
}

//拼接相对路径
std::string JoinRel(const std::string& dir, const std::string& name)
{
    return dir.empty() ? name : dir + "/" + name;
}

//一次搜索的执行状态：命中缓冲、计数与截止时间
class SearchRun
{
public:
    SearchRun(const ShareSearchParams& params, const std::atomic<bool>* cancel, IShareSearchSink* sink)
        : m_params(params)
        , m_cancel(cancel)
        , m_sink(sink)
        , m_lowerKeyword(Lower(params.m_keyword))
        , m_total(0)
        , m_checked(0)
        , m_start(::GetTickCount64())
        , m_lastFlush(m_start)
        , m_outcome(share_search_finished)
    {
    }

    int Execute()
    {
        std::deque<PendingDir> queue;
        PendingDir start;
        start.m_abs = m_params.m_startAbs;
        start.m_rel = m_params.m_startRel;
        start.m_depth = 0;
        queue.push_back(start);
        while (!queue.empty() && m_outcome == share_search_finished)
        {
            const PendingDir dir = queue.front();
            queue.pop_front();
            ScanOne(dir, queue);
        }
        //取消或接收方不再需要：不交出最后一批
        if (m_outcome == share_search_canceled || m_outcome == share_search_stopped)
            return m_outcome;
        const bool truncated = (m_outcome != share_search_finished);
        m_sink->OnSearchHits(m_hits, true, truncated);
        return m_outcome;
    }

private:
    //列一个目录：命中的放进缓冲，下级目录排进队列
    void ScanOne(const PendingDir& dir, std::deque<PendingDir>& queue)
    {
        WIN32_FIND_DATAW fd;
        const std::wstring pattern = ShareToLongPath(dir.m_abs + L"\\*");
        HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL,
                                      FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE)
            return;
        do
        {
            if (!CheckLimits())
                break;
            const std::wstring name(fd.cFileName);
            if (name == L"." || name == L"..")
                continue;
            //重解析点：不报告也不进入
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                continue;
            if (!m_params.m_includeHidden && (fd.dwFileAttributes & kShareHiddenAttrMask) != 0)
                continue;
            const std::wstring abs = dir.m_abs + L"\\" + name;
            if (CShareUploadRegistry::Instance().Contains(abs))
                continue;
            const std::string utf8Name = ShareWideToUtf8(name);
            //访问者无法用这样的名字再发请求
            if (!SharePathGuard::IsValidRelativePath(utf8Name, false))
                continue;
            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const std::string rel = JoinRel(dir.m_rel, utf8Name);
            if (m_params.m_matchAll || Lower(name).find(m_lowerKeyword) != std::wstring::npos)
                AddHit(rel, isDir, fd);
            if (m_outcome != share_search_finished)
                break;
            if (isDir && dir.m_depth + 1 <= kShareScanMaxDepth)
            {
                PendingDir child;
                child.m_abs = abs;
                child.m_rel = rel;
                child.m_depth = dir.m_depth + 1;
                queue.push_back(child);
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }

    //记一条命中；到上限时结束，攒够一批或隔得久了就先交出
    void AddHit(const std::string& rel, bool isDir, const WIN32_FIND_DATAW& fd)
    {
        ShareEntry e;
        e.m_name = rel;
        e.m_isDir = isDir;
        e.m_size = isDir ? 0 : ((static_cast<int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
        e.m_mtime = ShareFileOps::FileTimeToUnixMs(fd.ftLastWriteTime);
        m_hits.push_back(e);
        ++m_total;
        if (m_total >= static_cast<size_t>(m_params.m_maxResults))
        {
            m_outcome = share_search_truncated_count;
            return;
        }
        const ULONGLONG now = ::GetTickCount64();
        if (m_hits.size() >= kShareSearchFlushHits || now - m_lastFlush >= static_cast<ULONGLONG>(kShareSearchFlushIntervalMs))
            Flush(now);
    }

    //交出缓冲里的命中（不是最后一批）
    void Flush(ULONGLONG now)
    {
        m_lastFlush = now;
        if (m_hits.empty())
            return;
        if (!m_sink->OnSearchHits(m_hits, false, false))
            m_outcome = share_search_stopped;
        m_hits.clear();
    }

    //每隔若干个条目检查一次取消标志、耗时与是否该先交出一批；需要结束时返回 false
    bool CheckLimits()
    {
        if (m_outcome != share_search_finished)
            return false;
        if (++m_checked % kShareSearchCheckEvery != 1)
            return true;
        if (m_cancel != NULL && m_cancel->load())
        {
            m_outcome = share_search_canceled;
            return false;
        }
        const ULONGLONG now = ::GetTickCount64();
        if (now - m_start >= static_cast<ULONGLONG>(m_params.m_timeoutMs))
        {
            m_outcome = share_search_truncated_timeout;
            return false;
        }
        if (now - m_lastFlush >= static_cast<ULONGLONG>(kShareSearchFlushIntervalMs))
            Flush(now);
        return m_outcome == share_search_finished;
    }

    const ShareSearchParams&  m_params;        //参数（借用，只在 Run 期间有效）
    const std::atomic<bool>*  m_cancel;        //取消标志（可为 NULL）
    IShareSearchSink*         m_sink;          //接收方
    const std::wstring        m_lowerKeyword;  //小写的关键字
    std::vector<ShareEntry>   m_hits;          //尚未交出的命中
    size_t                    m_total;         //累计命中条数
    unsigned                  m_checked;       //已检查的条目数（决定何时看取消标志与耗时）
    const ULONGLONG           m_start;         //开始时间（毫秒）
    ULONGLONG                 m_lastFlush;     //上一次交出一批的时间
    int                       m_outcome;       //结束方式；进行中为 share_search_finished
};

} // namespace

int ShareSearch::Run(const ShareSearchParams& params, const std::atomic<bool>* cancel, IShareSearchSink* sink)
{
    if (sink == NULL)
        return share_search_stopped;
    SearchRun run(params, cancel, sink);
    return run.Execute();
}

bool ShareSearch::CheckKeyword(const std::string& utf8Keyword, std::wstring& wide)
{
    wide.clear();
    if (utf8Keyword.empty() || !ShareUtf8ToWide(utf8Keyword, wide) || wide.empty())
        return false;
    if (wide.size() > kShareSearchKeywordMaxChars)
        return false;
    for (size_t i = 0; i < wide.size(); ++i)
    {
        if (wide[i] < kFirstPrintable)
            return false;
    }
    return true;
}

bool ShareSearch::Matches(const std::wstring& name, const std::wstring& keyword)
{
    if (keyword.empty())
        return false;
    return Lower(name).find(Lower(keyword)) != std::wstring::npos;
}

} // namespace fileshare
