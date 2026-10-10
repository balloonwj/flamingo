/**
 *  聊天记录管理器「本机存储占用」页的窗口逻辑，BalloonHistoryWindowStorage.cpp
 *
 *  CBalloonHistoryWindow 的一部分（同一个类，实现分到本文件）：左栏最下面「本机存储占用」入口、右侧存储页的统计、
 *  显示与清理（docs/聊天记录管理器方案.md §12.6）。统计与清理都要碰磁盘，交给主面板的后台查询线程
 *  （CV2MsgSearchWorker 的 kV2SearchStorageStats / kV2SearchStorageClean），结果经 WM_HISTORY_STORAGE_DONE 送回；
 *  调试入口没有后台线程，直接在界面线程上做。界面控件由 HistoryStorageLayout 建，统计与路径由 HistoryStorageStats 算。
 *
 *  存储页与左栏标签相互独立：进入时记下 m_storageMode，右侧只显示存储页；点左栏的标签、会话或命中行时先离开存储页，
 *  再按原来的分发处理（见 BalloonHistoryWindow.cpp 的 OnDuiNotify）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "BalloonHistoryWindow.h"

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"
#include "../../Session/UserSessionData.h"          // g_szDataRoot：本地数据根目录
#include "../../net/UserCachePath.h"                // UserCacheDir：按账号拼缓存目录
#include "../../net/CurrentUserAccount.h"           // net::CurrentUserAccount
#include "../BalloonMain/BalloonMainPanel.h"        // 后台查询线程
#include "../BalloonSettings/CacheCleanPlan.h"      // FormatByteSize：字节数的显示文字
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "HistoryStorageLayout.h"
#include "V2MsgSearchWorker.h"

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//会话行里某一类没有缓存时显示的文字
const wchar_t kNoSizeText[] = L"-";

//在客户区里找右侧容器
DuiControl* FindRightHost(DuiControl* content)
{
    return (content != NULL) ? content->FindCtrlById(kIdMgrRightHost) : NULL;
}

//字节数的显示文字；0 时为「-」
CString SizeCellText(int64_t bytes)
{
    if (bytes <= 0)
    {
        return CString(kNoSizeText);
    }
    return CString(balloonsettings::FormatByteSize((unsigned long long)bytes).c_str());
}

//字节数的显示文字（汇总行用，0 时也写出来）
std::wstring SizeText(int64_t bytes)
{
    return balloonsettings::FormatByteSize((unsigned long long)((bytes > 0) ? bytes : 0));
}

} // namespace

void CBalloonHistoryWindow::EnterStoragePage()
{
    DuiControl* content = GetClientContent();
    DuiLayout* host = dynamic_cast<DuiLayout*>(FindRightHost(content));
    if (host == NULL)
    {
        return;
    }
    //第一次进入：把存储页加进右侧容器（与聊天记录页、文件页叠放，只显示其一）
    if (host->FindCtrlById(kIdStorBody) == NULL)
    {
        HistoryStorageViewData empty;
        empty.m_showExpandLeft = m_mgrCollapsed;
        std::unique_ptr<DuiControl> page = BuildHistoryStorageBody(empty, 0);
        page->SetVisible(false);
        host->AddChild(std::move(page), DuiLayout::Hint().Weight(1));
    }
    m_storageMode = true;
    LOG_INFO("history storage: enter page, stats valid %d.", (int)m_storageValid);
    ApplyHistoryManagerStorage(content, true, m_mgrTab);
    if (!m_storageValid && m_storageSeq == 0)
    {
        SubmitStorageStats();
    }
    ApplyStorageView();
}

void CBalloonHistoryWindow::LeaveStoragePage()
{
    if (!m_storageMode)
    {
        return;
    }
    LOG_INFO("history storage: leave page.");
    ResetStorageMode();
    ApplyHistoryManagerStorage(GetClientContent(), false, m_mgrTab);
    //存储页显示期间另外两页是隐藏的，期间的数据变化没有刷新进去：回来后完整刷新一次
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
}

void CBalloonHistoryWindow::ResetStorageMode()
{
    m_storageMode = false;
}

HistoryStorageDirs CBalloonHistoryWindow::CurrentStorageDirs()
{
    HistoryStorageDirs dirs;
    const std::wstring account = net::CurrentUserAccount();
    dirs.m_dataRoot = g_szDataRoot;
    dirs.m_imageDir = net::UserCacheDir(g_szDataRoot, account, L"ChatImage");
    dirs.m_fileDir = net::UserCacheDir(g_szDataRoot, account, L"ChatFile");
    dirs.m_voiceDir = net::UserCacheDir(g_szDataRoot, account, L"ChatVoice");
    CV2MsgStore* store = ManagerStore();
    if (store != NULL)
    {
        dirs.m_dbPath = store->DbPath();
    }
    return dirs;
}

void CBalloonHistoryWindow::SubmitStorageStats()
{
    const HistoryStorageDirs dirs = CurrentStorageDirs();
    CV2MsgSearchWorker* worker = (m_pMainPanel != NULL) ? m_pMainPanel->SearchWorker() : NULL;
    if (worker != NULL)
    {
        V2SearchRequest req;
        req.m_kind = kV2SearchStorageStats;
        req.m_storageDirs = dirs;
        req.m_target = m_hWnd;
        req.m_msg = WM_HISTORY_STORAGE_DONE;
        m_storageSeq = worker->Submit(req);
        m_storageCleaning = false;
        LOG_INFO("history storage: stats submitted, seq %u.", m_storageSeq);
        if (m_storageSeq != 0)
        {
            return;
        }
    }
    //调试入口（或提交失败）：直接在界面线程上统计。调试入口的假库只有几十行、缓存目录为空，很快
    m_storageSeq = 0;
    CV2MsgStore* store = ManagerStore();
    m_storageRefs.clear();
    m_storageValid = (store != NULL) && store->QueryCacheRefs(m_storageRefs);
    m_storageStats = m_storageValid ? ComputeStorageStats(m_storageRefs, dirs) : HistoryStorageStats();
    LOG_INFO("history storage: stats computed on ui thread, ok %d, %u refs.", (int)m_storageValid,
             (unsigned)m_storageRefs.size());
}

void CBalloonHistoryWindow::CleanStorage(int rowIndex)
{
    if (!m_storageValid || m_storageSeq != 0)
    {
        return;
    }
    int ctype = 1;
    int64_t cid = 0;
    int64_t bytes = m_storageStats.CacheBytes();
    CString name;
    if (rowIndex >= 0)
    {
        if (rowIndex >= (int)m_storageStats.m_convs.size())
        {
            return;
        }
        const HistoryStorageConv& c = m_storageStats.m_convs[(size_t)rowIndex];
        ctype = c.m_ctype;
        cid = c.m_cid;
        bytes = c.TotalBytes();
        name = LibraryConversationName(ctype, cid);
    }
    const std::wstring sizeText = SizeText(bytes);
    //同一个文件也发在别的会话里时，清理后那边也要重新下载：确认框里一并说明
    const std::wstring prompt = (rowIndex >= 0)
        ? TRF(L"确定清理「{0}」在本机的图片、文件与语音缓存吗？（共 {1}）\n\n聊天记录不会被删除，需要查看时可以重新下载；同一个文件也发在别的会话里的，那边也要重新下载。", name, sizeText)
        : TRF(L"确定清理全部会话在本机的图片、文件与语音缓存吗？（共 {0}）\n\n聊天记录不会被删除，需要查看时可以重新下载。", sizeText);
    if (::MessageBox(m_hWnd, prompt.c_str(), TR(L"本机存储占用"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return;
    }
    const HistoryStorageDirs dirs = CurrentStorageDirs();
    const std::vector<std::wstring> paths = StorageCleanTargets(m_storageRefs, ctype, cid, dirs);
    LOG_INFO("history storage: clean ctype=%d, cid=%lld, %u paths.", ctype, (long long)cid, (unsigned)paths.size());

    CV2MsgSearchWorker* worker = (m_pMainPanel != NULL) ? m_pMainPanel->SearchWorker() : NULL;
    if (worker != NULL)
    {
        V2SearchRequest req;
        req.m_kind = kV2SearchStorageClean;
        req.m_storageDirs = dirs;
        req.m_cleanPaths = paths;
        req.m_target = m_hWnd;
        req.m_msg = WM_HISTORY_STORAGE_DONE;
        m_storageSeq = worker->Submit(req);
        m_storageCleaning = (m_storageSeq != 0);
        if (m_storageSeq != 0)
        {
            ApplyStorageView();
            return;
        }
    }
    //调试入口（或提交失败）：直接在界面线程上删
    int cleaned = 0;
    int failed = 0;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        if (DeleteStoragePath(paths[i], dirs))
        {
            ++cleaned;
        }
        else
        {
            ++failed;
        }
    }
    LOG_INFO("history storage: cleaned on ui thread, %d ok, %d failed.", cleaned, failed);
    SubmitStorageStats();
    ApplyStorageView();
}

void CBalloonHistoryWindow::ApplyStorageView()
{
    DuiControl* content = GetClientContent();
    if (content == NULL || content->FindCtrlById(kIdStorBody) == NULL)
    {
        return;
    }
    HistoryStorageViewData data;
    data.m_showExpandLeft = m_mgrCollapsed;
    data.m_subtitle = TR(L"只统计聊天里的图片、文件、语音在本机的缓存，另存到别处的文件不在其中");
    data.m_busy = (m_storageSeq != 0);
    if (m_storageCleaning)
    {
        data.m_emptyText = TR(L"正在清理…");
    }
    else if (m_storageSeq != 0 || !m_storageValid)
    {
        data.m_emptyText = TR(L"正在统计…");
    }
    else
    {
        data.m_emptyText = TR(L"本机没有聊天图片、文件与语音的缓存");
    }

    if (m_storageValid)
    {
        const HistoryStorageStats& s = m_storageStats;
        data.m_summary = TRF(L"缓存合计 {0}：图片 {1} · 文件 {2} · 语音 {3}；聊天记录库 {4}（不参与清理）",
                             SizeText(s.CacheBytes()), SizeText(s.m_totalBytes[kStorageImage]),
                             SizeText(s.m_totalBytes[kStorageFile]), SizeText(s.m_totalBytes[kStorageVoice]),
                             SizeText(s.m_dbBytes)).c_str();
        data.m_canCleanAll = (s.CacheBytes() > 0);
        for (size_t i = 0; i < s.m_convs.size() && (int)i < kIdStorRowCapacity; ++i)
        {
            const HistoryStorageConv& c = s.m_convs[i];
            HistoryStorageRowView row;
            row.m_name = LibraryConversationName(c.m_ctype, c.m_cid);
            row.m_imageText = SizeCellText(c.m_bytes[kStorageImage]);
            row.m_fileText = SizeCellText(c.m_bytes[kStorageFile]);
            row.m_voiceText = SizeCellText(c.m_bytes[kStorageVoice]);
            row.m_totalText = SizeCellText(c.TotalBytes());
            data.m_rows.push_back(row);
        }
        if ((int)s.m_convs.size() > kIdStorRowCapacity)
        {
            data.m_subtitle = TRF(L"按占用从多到少列出前 {0} 个会话，共 {1} 个会话有缓存", kIdStorRowCapacity,
                                  (int)s.m_convs.size()).c_str();
        }
    }
    else
    {
        data.m_summary = TR(L"正在统计…");
    }
    ApplyHistoryStorageData(content, data);
}

bool CBalloonHistoryWindow::HandleStorageNotify(const DuiNotify* pNotify)
{
    if (pNotify == NULL || pNotify->code != DUIN_CLICK)
    {
        return false;
    }
    const UINT id = pNotify->ctrlId;
    //左栏最下面的入口
    if (id == (UINT)kIdMgrStorage)
    {
        EnterStoragePage();
        return true;
    }
    if (!m_storageMode)
    {
        return false;
    }
    //「»」：展开左栏
    if (id == (UINT)kIdStorExpandLeft)
    {
        SetManagerCollapsed(false);
        return true;
    }
    //重新统计
    if (id == (UINT)kIdStorRefresh)
    {
        if (m_storageSeq == 0)
        {
            SubmitStorageStats();
            ApplyStorageView();
        }
        return true;
    }
    //清理全部会话的缓存（确认框是模态的，延后到点击事件解栈之后不必要：按钮本身不会被销毁，列表重建在确认之后）
    if (id == (UINT)kIdStorCleanAll)
    {
        CleanStorage(-1);
        return true;
    }
    //某个会话行的「清理缓存」
    if (id >= (UINT)kIdStorCleanBase && id < (UINT)(kIdStorCleanBase + kIdStorRowCapacity))
    {
        CleanStorage((int)(id - kIdStorCleanBase));
        return true;
    }
    return false;
}

LRESULT CBalloonHistoryWindow::OnStorageDoneMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    //结果对象归本窗口释放：先接管，函数返回时自动 delete
    std::unique_ptr<V2SearchResponse> resp(reinterpret_cast<V2SearchResponse*>(lParam));
    const uint32_t seq = (uint32_t)wParam;
    if (resp.get() == NULL || seq == 0 || seq != m_storageSeq || !IsWindow())
    {
        LOG_INFO("history storage: result seq %u dropped (latest %u).", seq, m_storageSeq);
        return 0;
    }
    m_storageSeq = 0;
    if (resp->m_kind == kV2SearchStorageClean)
    {
        m_storageCleaning = false;
        LOG_INFO("history storage: clean done, %d ok, %d failed, %.1f ms.", resp->m_cleaned, resp->m_cleanFailed,
                 resp->m_elapsedMs);
        if (resp->m_cleanFailed > 0)
        {
            ShowToast(TRF(L"已清理 {0} 项缓存，{1} 项正在使用、没有删除", resp->m_cleaned, resp->m_cleanFailed).c_str());
        }
        else
        {
            ShowToast(TRF(L"已清理 {0} 项缓存", resp->m_cleaned).c_str());
        }
        //清理之后重新统计
        SubmitStorageStats();
        ApplyStorageView();
        return 0;
    }
    m_storageValid = resp->m_ok;
    m_storageStats = resp->m_storage;
    m_storageRefs.swap(resp->m_cacheRefs);
    LOG_INFO("history storage: stats done, ok %d, %u refs, %u conversations, cache %lld bytes, db %lld bytes, %.1f ms.",
             (int)resp->m_ok, (unsigned)m_storageRefs.size(), (unsigned)m_storageStats.m_convs.size(),
             (long long)m_storageStats.CacheBytes(), (long long)m_storageStats.m_dbBytes, resp->m_elapsedMs);
    ApplyStorageView();
    return 0;
}
