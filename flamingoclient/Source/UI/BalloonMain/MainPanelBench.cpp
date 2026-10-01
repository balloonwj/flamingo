/**
 *  主面板基准测试的实现。整体说明与计时口径见 MainPanelBench.h。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"
#include "MainPanelBench.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <algorithm>

#include "BalloonMainPanel.h"
#include "BalloonMainLayout.h"                          // kIdPaneMessages 等控件 id
#include "MainPanelTreeView.h"                          // IsItemFavorite（收藏徽章核对）
#include "../../App/FlamingoClient.h"                   // CFlamingoClient::m_UserConfig（临时静音）
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/PerfStopWatch.h"
#include "../../net/ImageTransfer.h"                    // net::AvatarDownloadResult
#include "../../net/MessageProtocolClient.h"            // net::MessageNotify
#include "../../net/Protocol.h"                         // presence_type / conversation_type / message_type
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTreeView.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"

namespace balloonmain {

namespace {

// 各场景的采样次数。取值以"单个场景几秒内跑完、又足以看出平均值与最慢值"为准。
const int kWheelSteps          = 60;    // 滚轮：向下、向上各滚这么多格
const int kWheelDelta          = 120;   // 一格滚轮的增量（WHEEL_DELTA）
const int kHoverPasses         = 3;     // 鼠标扫过可见行的趟数
const int kHoverMovesPerRow    = 2;     // 每行移动几次鼠标（每次半行高）
const int kFullRepaintRounds   = 20;    // 整窗重画次数
const int kIncomingMessages    = 20;    // 收到新消息的条数
const int kPresenceChanges     = 50;    // 上下线推送条数
const int kAvatarDownloads     = 5;     // 头像下载完成的次数
const int kFadeGestureSteps    = 5;     // 淡入淡出场景里先滚几格
const double kFadeIdleWallMs   = 1500.0;   // 滚完之后空闲观察多久（毫秒），覆盖淡入 + 停留 + 淡出
const int kMaxDrainMessages    = 20000; // 单次排空消息队列的上限，防止某个消息反复自投导致死循环

// FILETIME 的单位是 100 纳秒，换算到毫秒要除以它。
const double kFileTimeUnitsPerMs = 10000.0;

// 秒到毫秒的换算。消息时间戳是 Unix 毫秒。
const int64_t kMsPerSecond = 1000;

// 取当前线程的 CPU 时间（内核态 + 用户态，毫秒）。
double ThreadCpuMs()
{
    FILETIME ftCreate;
    FILETIME ftExit;
    FILETIME ftKernel;
    FILETIME ftUser;
    if (!::GetThreadTimes(::GetCurrentThread(), &ftCreate, &ftExit, &ftKernel, &ftUser))
    {
        return 0.0;
    }
    ULARGE_INTEGER k;
    ULARGE_INTEGER u;
    k.LowPart  = ftKernel.dwLowDateTime;
    k.HighPart = ftKernel.dwHighDateTime;
    u.LowPart  = ftUser.dwLowDateTime;
    u.HighPart = ftUser.dwHighDateTime;
    return (double)(k.QuadPart + u.QuadPart) / kFileTimeUnitsPerMs;
}

} // anonymous namespace

CMainPanelBench::CMainPanelBench(CBalloonMainPanel* pPanel, CFlamingoClient* pClient)
    : m_pPanel(pPanel)
    , m_pClient(pClient)
{
}

void CMainPanelBench::DrainMessages()
{
    MSG msg;
    int handled = 0;
    while (handled < kMaxDrainMessages && ::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            //不吞掉退出请求：原样放回队列，交给外层消息循环处理
            ::PostQuitMessage((int)msg.wParam);
            return;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        ++handled;
    }
    if (m_pPanel != NULL && m_pPanel->IsWindow())
    {
        ::UpdateWindow(m_pPanel->m_hWnd);
    }
}

double CMainPanelBench::PumpForAndMeasureCpuMs(double wallMs)
{
    const double cpuStart = ThreadCpuMs();
    PerfStopWatch sw;
    while (sw.ElapsedMs() < wallMs)
    {
        const DWORD remain = (DWORD)(wallMs - sw.ElapsedMs()) + 1;
        ::MsgWaitForMultipleObjects(0, NULL, FALSE, remain, QS_ALLINPUT);
        DrainMessages();
    }
    return ThreadCpuMs() - cpuStart;
}

void CMainPanelBench::Record(const std::string& name, const std::vector<double>& durations)
{
    if (durations.empty())
    {
        return;
    }
    double total = 0.0;
    double maxMs = 0.0;
    for (size_t i = 0; i < durations.size(); ++i)
    {
        total += durations[i];
        maxMs = (std::max)(maxMs, durations[i]);
    }
    const double avg = total / (double)durations.size();

    char line[256] = { 0 };
    ::sprintf_s(line, "bench: %-28s n=%-4d avg=%9.2f ms  max=%9.2f ms  total=%10.1f ms",
                name.c_str(), (int)durations.size(), avg, maxMs, total);
    LOG_INFO("%s", line);
    m_lines.push_back(line);
}

void CMainPanelBench::RecordValue(const std::string& name, double value, const char* unit)
{
    char line[256] = { 0 };
    ::sprintf_s(line, "bench: %-28s %.2f %s", name.c_str(), value, unit);
    LOG_INFO("%s", line);
    m_lines.push_back(line);
}

void CMainPanelBench::WriteSummaryFile()
{
    wchar_t path[MAX_PATH] = { 0 };
    const DWORD len = ::GetEnvironmentVariableW(L"FLAMINGO_BENCH_OUT", path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return;
    }
    FILE* fp = NULL;
    if (::_wfopen_s(&fp, path, L"wb") != 0 || fp == NULL)
    {
        LOG_WARNING("bench: failed to open the summary file.");
        return;
    }
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        fputs(m_lines[i].c_str(), fp);
        fputs("\r\n", fp);
    }
    fclose(fp);
}

RECT CMainPanelBench::PaneRect(unsigned int paneId) const
{
    RECT rc = { 0, 0, 0, 0 };
    balloonwjui::DuiControl* content = m_pPanel->GetClientContent();
    if (content == NULL)
    {
        return rc;
    }
    balloonwjui::DuiControl* pane = content->FindCtrlById(paneId);
    if (pane != NULL)
    {
        rc = pane->GetRect();
    }
    return rc;
}

int CMainPanelBench::TreeRowHeight(unsigned int treeId) const
{
    balloonwjui::DuiControl* content = m_pPanel->GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiTreeView* tv =
        dynamic_cast<balloonwjui::DuiTreeView*>(content->FindCtrlById(treeId));
    return (tv != NULL) ? tv->GetRowHeight() : 0;
}

void CMainPanelBench::RecordGuiResources(const std::string& label)
{
    const DWORD gdi  = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user = ::GetGuiResources(::GetCurrentProcess(), GR_USEROBJECTS);
    RecordValue("gdi-objects " + label, (double)gdi, "handles");
    RecordValue("user-objects " + label, (double)user, "handles");
}

void CMainPanelBench::LoadStressData(const StressDataSpec& spec)
{
    if (m_pPanel == NULL || !m_pPanel->IsWindow())
    {
        LOG_ERROR("bench: main panel is not created, stress data not loaded.");
        return;
    }

    PerfStopWatch sw;
    BuildStressData(spec, m_data);
    RecordValue("generate-data", sw.ElapsedMs(), "ms");
    LOG_INFO("bench: stress data contacts=%d departments=%d groups=%d conversations=%d seed=%u.",
             (int)m_data.m_contacts.m_contacts.size(), (int)m_data.m_contacts.m_departments.size(),
             (int)m_data.m_groups.m_groups.size(), (int)m_data.m_conversations.m_items.size(),
             spec.m_seed);

    RecordGuiResources("before-load");

    //三条填充路径与登录后的顺序一致：先通讯录（会话的显示名与头像依赖它），再群，再会话
    sw.Restart();
    m_pPanel->OnContactsArrived(m_data.m_contacts);
    RecordValue("load-contacts", sw.ElapsedMs(), "ms");

    sw.Restart();
    m_pPanel->OnUserGroupsArrived(m_data.m_groups);
    RecordValue("load-groups", sw.ElapsedMs(), "ms");

    sw.Restart();
    m_pPanel->OnConversationsArrived(m_data.m_conversations);
    RecordValue("load-conversations", sw.ElapsedMs(), "ms");

    //填充期间积压的消息（群资料请求失败的日志、重画等）一并处理掉，单独计时
    sw.Restart();
    DrainMessages();
    RecordValue("load-drain", sw.ElapsedMs(), "ms");

    RecordGuiResources("after-load");
}

void CMainPanelBench::BenchWheel(const std::string& name, unsigned int paneId, int steps, int delta)
{
    const RECT rc = PaneRect(paneId);
    if (::IsRectEmpty(&rc))
    {
        LOG_WARNING("bench: pane %u not found, wheel scenario skipped.", paneId);
        return;
    }
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    ::ClientToScreen(m_pPanel->m_hWnd, &pt);

    std::vector<double> samples;
    for (int i = 0; i < steps; ++i)
    {
        PerfStopWatch sw;
        ::SendMessage(m_pPanel->m_hWnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)delta), MAKELPARAM(pt.x, pt.y));
        DrainMessages();
        samples.push_back(sw.ElapsedMs());
    }
    Record(name, samples);
}

void CMainPanelBench::BenchFullRepaint()
{
    std::vector<double> samples;
    for (int i = 0; i < kFullRepaintRounds; ++i)
    {
        PerfStopWatch sw;
        ::InvalidateRect(m_pPanel->m_hWnd, NULL, FALSE);
        ::UpdateWindow(m_pPanel->m_hWnd);
        samples.push_back(sw.ElapsedMs());
    }
    Record("full-repaint", samples);
}

void CMainPanelBench::BenchHover()
{
    const RECT rc = PaneRect(kIdPaneMessages);
    const int rowH = TreeRowHeight(kIdMessagesTree);
    if (::IsRectEmpty(&rc) || rowH <= 0)
    {
        LOG_WARNING("bench: messages pane or tree not found, hover scenario skipped.");
        return;
    }
    const int x    = (rc.left + rc.right) / 2;
    const int step = (std::max)(1, rowH / kHoverMovesPerRow);

    std::vector<double> samples;
    for (int pass = 0; pass < kHoverPasses; ++pass)
    {
        for (int y = rc.top + 1; y < rc.bottom - 1; y += step)
        {
            PerfStopWatch sw;
            ::SendMessage(m_pPanel->m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
            DrainMessages();
            samples.push_back(sw.ElapsedMs());
        }
    }
    //把鼠标移出窗口，让悬停高亮复位，不影响后续场景
    ::SendMessage(m_pPanel->m_hWnd, WM_MOUSELEAVE, 0, 0);
    DrainMessages();
    Record("hover-move", samples);
}

void CMainPanelBench::BenchScrollFadeIdle()
{
    const RECT rc = PaneRect(kIdPaneMessages);
    if (::IsRectEmpty(&rc))
    {
        return;
    }
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    ::ClientToScreen(m_pPanel->m_hWnd, &pt);

    //先滚几格让滚动条淡入，再空闲观察一段时间，量这段时间里 UI 线程实际花掉的 CPU
    for (int i = 0; i < kFadeGestureSteps; ++i)
    {
        ::SendMessage(m_pPanel->m_hWnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)(-kWheelDelta)), MAKELPARAM(pt.x, pt.y));
    }
    DrainMessages();
    RecordValue("idle-after-scroll-cpu", PumpForAndMeasureCpuMs(kFadeIdleWallMs), "ms cpu in 1500 ms");
}

int CMainPanelBench::CountConversationOrderMismatches() const
{
    balloonwjui::DuiControl* content = m_pPanel->GetClientContent();
    balloonwjui::DuiTreeView* tv = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiTreeView*>(content->FindCtrlById(kIdMessagesTree))
        : NULL;
    if (tv == NULL)
    {
        return -1;
    }
    const std::vector<net::ConversationItem>& convs = m_pPanel->m_conversations;
    int mismatches = (tv->GetVisibleCount() == (int)convs.size()) ? 0 : 1;
    for (size_t i = 0; i < convs.size() && (int)i < tv->GetVisibleCount(); ++i)
    {
        std::map<ConvNodeKey, int>::const_iterator it =
            m_pPanel->m_convNodeId.find(ConvNodeKey(convs[i].m_ctype, convs[i].m_cid));
        if (it == m_pPanel->m_convNodeId.end() || tv->GetIdAtVisibleRow((int)i) != it->second)
        {
            ++mismatches;
        }
    }
    return mismatches;
}

std::vector<CMainPanelBench::TreeRowSnap> CMainPanelBench::SnapshotContactsTree() const
{
    std::vector<TreeRowSnap> rows;
    balloonwjui::DuiControl* content = m_pPanel->GetClientContent();
    MainPanelTreeView* tv = (content != NULL)
        ? dynamic_cast<MainPanelTreeView*>(content->FindCtrlById(kIdContactsTree))
        : NULL;
    if (tv == NULL)
    {
        return rows;
    }
    for (int i = 0; i < tv->GetVisibleCount(); ++i)
    {
        const int id = tv->GetIdAtVisibleRow(i);
        TreeRowSnap s;
        s.m_label    = tv->GetItemLabel(id);
        s.m_children = tv->GetChildCount(id);
        s.m_expanded = tv->IsExpanded(id);
        s.m_favorite = tv->IsItemFavorite(id);
        rows.push_back(s);
    }
    return rows;
}

int CMainPanelBench::CountSnapshotMismatches(const std::vector<TreeRowSnap>& a,
                                             const std::vector<TreeRowSnap>& b)
{
    int mismatches = (a.size() == b.size()) ? 0 : 1;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        if (a[i].m_label != b[i].m_label || a[i].m_children != b[i].m_children
            || a[i].m_expanded != b[i].m_expanded || a[i].m_favorite != b[i].m_favorite)
        {
            ++mismatches;
        }
    }
    return mismatches;
}

int CMainPanelBench::CountFavoriteMappingErrors() const
{
    balloonwjui::DuiControl* content = m_pPanel->GetClientContent();
    balloonwjui::DuiTreeView* tv = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiTreeView*>(content->FindCtrlById(kIdContactsTree))
        : NULL;
    if (tv == NULL)
    {
        return 1;
    }
    int errors = 0;

    //分区每一行恰好登记在一张表里
    const std::set<int>& section = m_pPanel->m_favoriteSectionNodes;
    for (std::set<int>::const_iterator it = section.begin(); it != section.end(); ++it)
    {
        const size_t hits = m_pPanel->m_contactNodeUserId.count(*it)
                          + m_pPanel->m_favoriteGroupNodeId.count(*it);
        if (hits != 1)
        {
            ++errors;
        }
    }

    //两张表里没有已删除节点的残留（联系人行与群行都一定有文字，查不到文字即已删除）
    for (std::map<int, UINT>::const_iterator it = m_pPanel->m_contactNodeUserId.begin();
         it != m_pPanel->m_contactNodeUserId.end(); ++it)
    {
        if (tv->GetItemLabel(it->first).IsEmpty())
        {
            ++errors;
        }
    }
    for (std::map<int, int64_t>::const_iterator it = m_pPanel->m_favoriteGroupNodeId.begin();
         it != m_pPanel->m_favoriteGroupNodeId.end(); ++it)
    {
        if (tv->GetItemLabel(it->first).IsEmpty())
        {
            ++errors;
        }
    }

    //分区行数与根节点的子节点个数一致
    if (m_pPanel->m_favoritesRootId != -1
        && tv->GetChildCount(m_pPanel->m_favoritesRootId) != (int)section.size())
    {
        ++errors;
    }
    return errors;
}

int CMainPanelBench::CountFavoriteBadgeErrors(int64_t userid, bool expected) const
{
    balloonwjui::DuiControl* content = m_pPanel->GetClientContent();
    MainPanelTreeView* tv = (content != NULL)
        ? dynamic_cast<MainPanelTreeView*>(content->FindCtrlById(kIdContactsTree))
        : NULL;
    if (tv == NULL)
    {
        return 1;
    }
    int errors = 0;
    int rows = 0;
    for (std::map<int, UINT>::const_iterator it = m_pPanel->m_contactNodeUserId.begin();
         it != m_pPanel->m_contactNodeUserId.end(); ++it)
    {
        if (it->second != (UINT)userid || m_pPanel->m_favoriteSectionNodes.count(it->first) != 0)
        {
            continue;
        }
        ++rows;
        if (tv->IsItemFavorite(it->first) != expected)
        {
            ++errors;
        }
    }
    return (rows > 0) ? errors : 1;   //一行都找不到也算问题
}

void CMainPanelBench::BenchFavoritesConsistency()
{
    //FSR1 / FSR2：灌入数据时群列表到达走的是"只重建收藏分区"，与整棵重建的结果比对
    const std::vector<TreeRowSnap> inPlace = SnapshotContactsTree();
    int mappingErrors = CountFavoriteMappingErrors();
    m_pPanel->RebuildContactsTree();
    DrainMessages();
    RecordValue("favorites-load-mismatch",
                (double)CountSnapshotMismatches(inPlace, SnapshotContactsTree()), "rows");

    //FSR3：挑一个还没收藏、在部门树里的联系人，收藏再取消，每次都与整棵重建比对
    int64_t userid = 0;
    const std::vector<net::ContactItem>& pool = m_pPanel->m_contacts;
    for (size_t i = 0; i < pool.size(); ++i)
    {
        if (!m_pPanel->m_favorites.IsFavorite(conversation_single, pool[i].m_userid)
            && m_pPanel->m_contactOrgNode.count((UINT)pool[i].m_userid) != 0)
        {
            userid = pool[i].m_userid;
            break;
        }
    }
    if (userid == 0)
    {
        LOG_WARNING("bench: no contact available for the favorite toggle check.");
        return;
    }

    std::vector<double> toggleMs;
    int toggleMismatch = 0;
    int badgeErrors = 0;
    const bool states[] = { true, false };
    for (size_t k = 0; k < _countof(states); ++k)
    {
        PerfStopWatch sw;
        m_pPanel->SetFavoriteState(conversation_single, userid, states[k]);
        DrainMessages();
        toggleMs.push_back(sw.ElapsedMs());

        badgeErrors   += CountFavoriteBadgeErrors(userid, states[k]);
        mappingErrors += CountFavoriteMappingErrors();
        const std::vector<TreeRowSnap> afterToggle = SnapshotContactsTree();
        m_pPanel->RebuildContactsTree();
        DrainMessages();
        toggleMismatch += CountSnapshotMismatches(afterToggle, SnapshotContactsTree());
    }
    Record("favorite-toggle", toggleMs);
    RecordValue("favorite-toggle-mismatch", (double)toggleMismatch, "rows");
    RecordValue("favorite-badge-errors", (double)badgeErrors, "rows");
    RecordValue("favorites-mapping-errors", (double)mappingErrors, "errors");
}

void CMainPanelBench::BenchIncomingMessages()
{
    //从会话列表末尾往前挑单聊会话：收到消息后它要从底部移到顶部，覆盖"重排"的开销
    const std::vector<net::ConversationItem>& items = m_data.m_conversations.m_items;
    std::vector<int64_t> senders;
    for (size_t i = items.size(); i > 0 && (int)senders.size() < kIncomingMessages; --i)
    {
        if (items[i - 1].m_ctype == conversation_single)
        {
            senders.push_back(items[i - 1].m_cid);
        }
    }

    std::vector<double> samples;
    for (size_t i = 0; i < senders.size(); ++i)
    {
        net::MessageNotify notify;
        notify.m_msgid     = 0;   //为 0 时不写本地消息库，只量界面开销
        notify.m_ctype     = conversation_single;
        notify.m_cid       = senders[i];
        notify.m_from      = senders[i];
        notify.m_to        = 0;
        notify.m_msgtype   = message_type_text;
        notify.m_text      = "bench message";
        notify.m_timestamp = (int64_t)::time(NULL) * kMsPerSecond;

        PerfStopWatch sw;
        m_pPanel->OnRecvChatMessage(notify);
        DrainMessages();
        samples.push_back(sw.ElapsedMs());
    }
    Record("incoming-message", samples);
    // 就地挪动之后，树的顺序必须与排好序的会话列表逐项一致。
    RecordValue("conversation-order-mismatch", (double)CountConversationOrderMismatches(), "rows");
}

void CMainPanelBench::BenchPresenceChanges()
{
    std::vector<double> samples;
    for (int i = 0; i < kPresenceChanges; ++i)
    {
        const int64_t userid   = m_data.ContactUserIdAt(i * 97);
        const int     presence = (i % 2 == 0) ? presence_online : presence_offline;
        PerfStopWatch sw;
        m_pPanel->OnPresenceChanged(userid, presence);
        DrainMessages();
        samples.push_back(sw.ElapsedMs());
    }
    Record("presence-change", samples);
}

void CMainPanelBench::BenchAvatarDownloads()
{
    //下载结果对应的文件 id 不属于任何联系人：量的是"每下完一张头像都要把列表过一遍"
    //这部分固定开销，与是否真的命中无关
    std::vector<double> samples;
    for (int i = 0; i < kAvatarDownloads; ++i)
    {
        net::AvatarDownloadResult result;
        result.m_fileid    = "bench-no-such-file-" + std::to_string(i);
        result.m_ok        = true;
        result.m_localpath = L"C:\\bench-no-such-file.png";
        PerfStopWatch sw;
        m_pPanel->OnAvatarDownloadResult(result);
        DrainMessages();
        samples.push_back(sw.ElapsedMs());
    }
    Record("avatar-download", samples);
}

void CMainPanelBench::BenchSearchTyping()
{
    balloonwjui::DuiControl* content = m_pPanel->GetClientContent();
    balloonwjui::DuiEdit* edit = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiEdit*>(content->FindCtrlById(kIdSearchBox))
        : NULL;
    if (edit == NULL)
    {
        LOG_WARNING("bench: search box not found, search scenario skipped.");
        return;
    }

    //先逐字输入拼音（每一步都要对全部联系人做一次拼音匹配），再输入中文
    const wchar_t* const kPinyinSteps[] = { L"z", L"zh", L"zha", L"zhan", L"zhang" };
    const wchar_t* const kHanziSteps[]  = { L"张", L"张伟" };

    std::vector<double> pinyin;
    for (size_t i = 0; i < _countof(kPinyinSteps); ++i)
    {
        PerfStopWatch sw;
        edit->SetText(kPinyinSteps[i]);
        DrainMessages();
        pinyin.push_back(sw.ElapsedMs());
    }
    Record("search-keystroke-pinyin", pinyin);

    std::vector<double> hanzi;
    for (size_t i = 0; i < _countof(kHanziSteps); ++i)
    {
        PerfStopWatch sw;
        edit->SetText(kHanziSteps[i]);
        DrainMessages();
        hanzi.push_back(sw.ElapsedMs());
    }
    Record("search-keystroke-hanzi", hanzi);

    edit->SetText(L"");
    m_pPanel->m_searchDropdown.HideDropdown();
    DrainMessages();
}

void CMainPanelBench::BenchTabSwitch()
{
    const unsigned int kTabs[] = { kIdTabContacts, kIdTabGroups, kIdTabMessages };
    std::vector<double> samples;
    for (size_t i = 0; i < _countof(kTabs); ++i)
    {
        PerfStopWatch sw;
        m_pPanel->SwitchTab(kTabs[i]);
        DrainMessages();
        samples.push_back(sw.ElapsedMs());
    }
    Record("tab-switch", samples);
}

void CMainPanelBench::RunScenarios()
{
    if (m_pPanel == NULL || !m_pPanel->IsWindow())
    {
        LOG_ERROR("bench: main panel is not created, scenarios skipped.");
        return;
    }

    //测试期间临时静音：收消息、上下线场景会连续触发几十次提示音
    const BOOL wasMuted = (m_pClient != NULL) ? m_pClient->m_UserConfig.IsEnableMute() : TRUE;
    if (m_pClient != NULL)
    {
        m_pClient->m_UserConfig.EnableMute(TRUE);
    }

    LOG_INFO("bench: scenarios begin.");
    PerfStopWatch total;

    m_pPanel->SwitchTab(kIdTabMessages);
    DrainMessages();

    BenchFavoritesConsistency();
    BenchFullRepaint();
    BenchWheel("wheel-messages-down", kIdPaneMessages, kWheelSteps, -kWheelDelta);
    BenchWheel("wheel-messages-up", kIdPaneMessages, kWheelSteps, kWheelDelta);
    BenchHover();
    BenchScrollFadeIdle();
    BenchIncomingMessages();
    BenchPresenceChanges();
    BenchAvatarDownloads();
    BenchSearchTyping();
    BenchTabSwitch();

    m_pPanel->SwitchTab(kIdTabContacts);
    DrainMessages();
    BenchWheel("wheel-contacts-down", kIdPaneContacts, kWheelSteps, -kWheelDelta);
    m_pPanel->SwitchTab(kIdTabGroups);
    DrainMessages();
    BenchWheel("wheel-groups-down", kIdPaneGroups, kWheelSteps, -kWheelDelta);
    m_pPanel->SwitchTab(kIdTabMessages);
    DrainMessages();

    RecordGuiResources("after-scenarios");
    RecordValue("scenarios-total", total.ElapsedMs(), "ms");

    if (m_pClient != NULL)
    {
        m_pClient->m_UserConfig.EnableMute(wasMuted);
    }
    LOG_INFO("bench: scenarios end.");
    WriteSummaryFile();
}

} // namespace balloonmain
