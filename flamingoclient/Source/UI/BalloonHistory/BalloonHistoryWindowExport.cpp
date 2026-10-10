/**
 *  聊天记录管理器「导出」增强的窗口逻辑，BalloonHistoryWindowExport.cpp
 *
 *  CBalloonHistoryWindow 的一部分（同一个类，实现分到本文件）：导出选项浮层的打开与提交、把各会话整理成导出任务、
 *  在后台线程上执行（写文件、复制附件或写 zip）并显示进度与结果（docs/聊天记录管理器方案.md §12.7）。
 *  问保存位置（AskExportTarget）与整理一个会话的文档（BuildExportConvJob）在 BalloonHistoryWindow.cpp，那里有
 *  原来导出用的几个辅助函数。
 *
 *  后台线程只碰磁盘：查库、解析名字都在界面线程上做完，线程拿到的是整理好的任务（HistoryExportJob）。进度经
 *  WM_HISTORY_EXPORT_PROGRESS、结果经 WM_HISTORY_EXPORT_DONE 投回；窗口销毁时置取消标志并等线程结束。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "BalloonHistoryWindow.h"

#include <shellapi.h>   // ShellExecute：打开导出所在的文件夹

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"
#include "HistoryExport.h"        // ExportFilesDirName
#include "HistoryExportJob.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//进度提示的刷新粒度：完成的百分比每变化这么多才更新一次提示条，免得消息过多
const int kProgressStepPercent = 5;
const int kPercentFull = 100;
//多个会话或打包时，每个会话文件夹里的文档名（不含扩展名；使用处经 TR 取译文）
const wchar_t* const kDocBaseName = TR_NOOP(L"聊天记录");

//文档文件名：「聊天记录.html」或「聊天记录.txt」（按界面语言翻译）
std::wstring ExportDocName(bool html)
{
    return std::wstring(TR(kDocBaseName)) + (html ? L".html" : L".txt");
}

//路径的文件名部分
std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

/**
 *  ExportProgressPoster —— 后台导出线程的进度回调：按百分比粒度投递 WM_HISTORY_EXPORT_PROGRESS，读取取消标志。
 */
class ExportProgressPoster : public IHistoryExportProgress
{
public:
    ExportProgressPoster(HWND target, std::atomic<bool>* cancel)
        : m_target(target)
        , m_cancel(cancel)
        , m_lastPercent(-1)
    {
    }

    //完成一步：百分比每跨过一档投递一次
    void OnExportProgress(int done, int total)
    {
        const int percent = (total > 0) ? (done * kPercentFull / total) : kPercentFull;
        if (percent / kProgressStepPercent != m_lastPercent / kProgressStepPercent || done == total)
        {
            m_lastPercent = percent;
            ::PostMessage(m_target, WM_HISTORY_EXPORT_PROGRESS, (WPARAM)done, (LPARAM)total);
        }
    }

    //窗口销毁时置位的取消标志
    bool ExportCancelled()
    {
        return m_cancel != NULL && m_cancel->load();
    }

private:
    HWND               m_target;        //投递目标窗口
    std::atomic<bool>* m_cancel;        //取消标志（借用，归窗口所有；窗口在线程结束前不会释放它）
    int                m_lastPercent;   //上一次投递时的百分比
};

//后台导出线程的入口：执行任务，结果投回窗口；投递失败（窗口已销毁）时自己释放结果
void ExportThreadMain(HWND target, HistoryExportJob job, std::atomic<bool>* cancel)
{
    ExportProgressPoster poster(target, cancel);
    HistoryExportResult* result = new HistoryExportResult(RunHistoryExportJob(job, &poster));
    if (!::PostMessage(target, WM_HISTORY_EXPORT_DONE, 0, (LPARAM)result))
    {
        delete result;
    }
}

} // namespace

void CBalloonHistoryWindow::OpenExportPopup()
{
    if (m_exportRunning)
    {
        ShowToast(TR(L"正在导出，请稍候"));
        return;
    }
    DuiControl* content = GetClientContent();
    DuiControl* button = (content != NULL) ? content->FindCtrlById(kIdHistoryExport) : NULL;
    RECT anchor = { 0, 0, 0, 0 };
    if (button != NULL && button->IsVisible())
    {
        anchor = button->GetRect();
    }
    else
    {
        //从左栏右键菜单进来时按钮可能不可见：以窗口客户区右下角为锚点
        GetClientRect(&anchor);
        anchor.left = anchor.right;
        anchor.top = anchor.bottom;
    }
    ::ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&anchor.left));
    ::ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&anchor.right));

    //「选择会话」时列出左栏的全部会话，顺序与左栏相同
    std::vector<CHistoryScopePopup::Entry> entries;
    entries.reserve(m_mgrItems.size());
    for (size_t i = 0; i < m_mgrItems.size(); ++i)
    {
        CHistoryScopePopup::Entry e;
        e.m_key = HistoryConversationKey(m_mgrItems[i].m_ctype, m_mgrItems[i].m_cid);
        e.m_name = m_mgrItems[i].m_name;
        CString name;
        CString timeText;
        ResolveManagerRow(m_mgrItems[i].m_ctype, m_mgrItems[i].m_cid, m_mgrItems[i].m_lastTimestamp,
                          name, timeText, e.m_avatar);
        entries.push_back(e);
    }
    //勾选的初始值：上一次导出勾过的会话；没有时勾上当前会话
    HistoryExportOptions opts = m_lastExportOptions;
    if (opts.m_conversations.empty() && m_model.Filter().m_cid != 0)
    {
        opts.m_conversations.push_back(HistoryConversationKey(m_model.Filter().m_ctype, m_model.Filter().m_cid));
    }
    m_exportPopup.OpenAbove(anchor, entries, opts);
}

void CBalloonHistoryWindow::OnExportPopupThunk(void* userdata, const HistoryExportOptions& opts)
{
    CBalloonHistoryWindow* self = static_cast<CBalloonHistoryWindow*>(userdata);
    if (self != NULL && self->IsWindow())
    {
        self->BeginExport(opts);
    }
}

void CBalloonHistoryWindow::StartExportJob(const HistoryExportOptions& opts, const std::wstring& target)
{
    if (m_exportRunning || target.empty())
    {
        return;
    }
    //要导出的会话与显示名
    std::vector<HistoryConversationKey> keys;
    if (opts.m_scope == kHistoryExportScopeCurrent)
    {
        keys.push_back(HistoryConversationKey(m_model.Filter().m_ctype, m_model.Filter().m_cid));
    }
    else
    {
        keys = opts.m_conversations;
    }

    HistoryExportJob job;
    job.m_zip = opts.m_zip;
    job.m_singleFile = !opts.m_zip && keys.size() == 1;
    job.m_target = target;
    std::set<std::wstring> usedFolders;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const bool current = (keys[i].m_ctype == m_model.Filter().m_ctype && keys[i].m_cid == m_model.Filter().m_cid);
        const CString name = current ? m_titleName : LibraryConversationName(keys[i].m_ctype, keys[i].m_cid);
        //「按当前筛选条件」只对当前会话有意义，其余会话按全部时间
        const int range = (opts.m_range == kHistoryExportRangeFilter && !current) ? 0 : opts.m_range;
        std::vector<HistoryRecord> all;
        if (!QueryExportRecords(keys[i].m_ctype, keys[i].m_cid, range, all))
        {
            LOG_ERROR("history export: query ctype=%d, cid=%lld failed.", keys[i].m_ctype, (long long)keys[i].m_cid);
            continue;
        }
        if (all.empty())
        {
            continue;
        }
        const std::wstring docName = job.m_singleFile ? FileNameOf(target) : ExportDocName(opts.m_html);
        HistoryExportConvJob conv = BuildExportConvJob(name, all, docName, opts);
        conv.m_folderName = ExportFolderName(std::wstring((LPCTSTR)name), usedFolders);
        job.m_convs.push_back(conv);
    }
    if (job.m_convs.empty())
    {
        ::MessageBox(m_hWnd, TR(L"所选范围内没有可导出的消息。"), TR(L"导出消息记录"), MB_OK | MB_ICONINFORMATION);
        return;
    }
    //选了多个会话、只有一个有消息时仍按文件夹（或 zip）的布局导出：用户选的是文件夹，不是文档路径
    LOG_INFO("history export: start, %u conversations, zip %d, single file %d.", (unsigned)job.m_convs.size(),
             (int)job.m_zip, (int)job.m_singleFile);

    //上一次的线程已经结束（结果处理时 join 过），这里保险起见再等一次
    if (m_exportThread.joinable())
    {
        m_exportThread.join();
    }
    m_exportCancel.store(false);
    m_exportRunning = true;
    ShowToast(TR(L"正在导出…"));
    m_exportThread = std::thread(ExportThreadMain, m_hWnd, job, &m_exportCancel);
}

LRESULT CBalloonHistoryWindow::OnExportProgressMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    const int done = (int)wParam;
    const int total = (int)lParam;
    if (!m_exportRunning || total <= 0 || done >= total)
    {
        return 0;
    }
    ShowToast(TRF(L"正在导出… {0}%", done * kPercentFull / total).c_str());
    return 0;
}

LRESULT CBalloonHistoryWindow::OnExportDoneMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    std::unique_ptr<HistoryExportResult> result(reinterpret_cast<HistoryExportResult*>(lParam));
    if (m_exportThread.joinable())
    {
        m_exportThread.join();
    }
    m_exportRunning = false;
    if (result.get() == NULL || !IsWindow())
    {
        return 0;
    }
    LOG_INFO("history export: done, ok %d, too large %d, cancelled %d, %d conversations, %d messages, "
             "%d attachments, %d failed.", (int)result->m_ok, (int)result->m_tooLarge, (int)result->m_cancelled,
             result->m_convs, result->m_messages, result->m_attachmentsDone, result->m_attachmentsFailed);
    if (result->m_cancelled)
    {
        return 0;
    }
    if (!result->m_ok)
    {
        const LPCTSTR why = result->m_tooLarge
            ? TR(L"导出失败：打包后的文件超过 4 GB，zip 放不下。请取消「打包为 zip」，改为导出到文件夹。")
            : TR(L"导出失败：无法写入文件。请检查保存位置是否可写、磁盘空间是否足够。");
        ::MessageBox(m_hWnd, why, TR(L"导出消息记录"), MB_OK | MB_ICONWARNING);
        return 0;
    }
    std::wstring done = TRF(L"已导出 {0} 个会话，共 {1} 条消息。", result->m_convs, result->m_messages);
    if (result->m_attachmentsDone > 0 || result->m_attachmentsFailed > 0)
    {
        done += L"\n";
        done += TRF(L"附带图片与文件 {0} 个，{1} 个没能带上（本机文件已不存在或正被占用）。",
                    result->m_attachmentsDone, result->m_attachmentsFailed);
    }
    done += L"\n\n";
    done += TR(L"是否打开所在的文件夹？");
    if (::MessageBox(m_hWnd, done.c_str(), TR(L"导出消息记录"), MB_YESNO | MB_ICONINFORMATION) == IDYES)
    {
        //选中导出的文件或文件夹（多个会话不打包时导出到的就是那个文件夹，直接打开它）
        const bool folder = !m_lastExportOptions.m_zip && result->m_convs > 1;
        const std::wstring args = folder ? (L"\"" + result->m_target + L"\"")
                                         : (L"/select,\"" + result->m_target + L"\"");
        ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
    }
    return 0;
}

void CBalloonHistoryWindow::StopExportThread()
{
    if (!m_exportThread.joinable())
    {
        return;
    }
    m_exportCancel.store(true);
    m_exportThread.join();
    m_exportRunning = false;
    //线程结束前投进消息队列的结果在这里释放（窗口已在销毁，不会再处理它）
    if (m_hWnd != NULL)
    {
        MSG msg;
        while (::PeekMessage(&msg, m_hWnd, WM_HISTORY_EXPORT_DONE, WM_HISTORY_EXPORT_DONE, PM_REMOVE))
        {
            delete reinterpret_cast<HistoryExportResult*>(msg.lParam);
        }
    }
    LOG_INFO("history export: background thread stopped.");
}
