/**
 *  聊天记录管理器「文件」「图片」两页的窗口逻辑，BalloonHistoryWindowLibrary.cpp
 *
 *  CBalloonHistoryWindow 的一部分（同一个类，实现分到本文件，免得 BalloonHistoryWindow.cpp 继续变长）：左栏标签切换、
 *  文件 / 图片页的筛选与翻页、左栏按会话计数的列表、行操作（打开、所在文件夹、下载、定位、另存为、转发、复制文件名）
 *  与群文件下载（docs/聊天记录管理器方案.md §11.5、§11.6）。界面控件由 HistoryLibraryLayout 建，数据由
 *  HistoryLibraryModel 查本地库。
 *
 *  会销毁被点击控件的刷新（换列表内容、重填分页器）一律投递 WM_HISTORY_REFRESH_CONTENT 延后执行，与聊天记录页
 *  同一做法；RefreshContent 停在文件 / 图片页时把刷新转给 RefreshLibrary。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "BalloonHistoryWindow.h"

#include <atldlgs.h>   // CFileDialog（群文件下载的另存为对话框）
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"
#include "../../Common/Util/ClipboardText.h"        // CopyTextToClipboard（复制文件名）
#include "../../Common/Util/FileDialogFilter.h"     // filedlg::MakeFilter
#include "../../net/Protocol.h"                     // ::message_type_*
#include "../../net/FileTransfer.h"                 // 群文件下载 / 取消
#include "../../App/FlamingoClient.h"
#include "../BalloonMain/BalloonMainPanel.h"        // 拉齐群文件缓存、定位群文件、解析名字
#include "../BalloonMain/MainPanelTreeView.h"       // 左栏列表
#include "../BalloonMain/PopupMenuPlacement.h"      // ClampPopupMenuToWorkArea（菜单不越屏）
#include "../BalloonChatWnd/ChatImageSaveAs.h"      // SaveChatImageAs（图片另存为）
#include "../BalloonChatWnd/ChatMessageMenu.h"      // MakeImageSaveAsDefaultName
#include "../BalloonChatWnd/ChatReceivedFile.h"     // IsLocalFileUsable：群文件落地路径上的文件还在不在
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "HistoryLocalFile.h"       // ResolveRecordLocalPath
#include "HistoryRow.h"             // HistoryRow：文件页的行（悬停按钮）
#include "HistoryRowFormat.h"       // LocalDayKey
#include "HistoryRowMenu.h"         // IsHistoryRowImageType
#include "HistoryView.h"            // FormatFileSize / FormatDateGroupLabel / TimeRangeFromComboIndex
#include "HistoryWildcard.h"        // FindSearchHighlightRanges
#include "HistoryLibraryLinks.h"    // 链接页：识别链接、取域名、拼「复制链接」的文字
#include "../BalloonChatWnd/ChatLinkDetect.h"   // IsSafeLinkUrl：打开链接前的协议白名单

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//文件名搜索框停止输入多久之后才查询（毫秒），与聊天记录页的搜索框相同
const UINT kLibSearchDelayMs = 250;
//文件名搜索的延迟定时器；紧挨聊天记录管理器左栏的那几个（0x4803～0x4805）
const UINT_PTR kLibSearchTimerId = 0x4806;
//群文件缓存变化后合并刷新的延迟（毫秒）与定时器：主面板逐个群拉齐时会连发多次通知，最多每 400 毫秒刷新一次
const UINT kLibRefreshDelayMs = 400;
const UINT_PTR kLibRefreshTimerId = 0x4807;
//行菜单尺寸的估算值（像素），交给夹取函数（理由见 BalloonHistoryWindow.cpp 的 kRowMenuEstWidth）
const int kLibMenuEstWidth = 170;
const int kLibMenuEstItemHeight = 26;
//下载进度的满值
const int kLibPercentMax = 100;

//行菜单的命令
enum LibraryMenuCmd
{
    kLibMenuOpen     = 1,   //打开文件（图片为查看大图）
    kLibMenuFolder   = 2,   //打开所在文件夹
    kLibMenuDownload = 3,   //下载
    kLibMenuCancel   = 4,   //取消下载
    kLibMenuSaveAs   = 5,   //另存为
    kLibMenuLocate   = 6,   //定位到聊天（群文件为打开群聊窗的群文件页）
    kLibMenuForward  = 7,   //转发（只有聊天里的消息可以）
    kLibMenuCopyName = 8,   //复制文件名
    kLibMenuOpenLink = 9,   //链接页：用浏览器打开链接
    kLibMenuCopyText = 10,  //链接页复制链接；与我有关页复制正文
};

//取当前 Unix 毫秒时间
int64_t LibNowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    //FILETIME 为自 1601 起的 100ns 数；减去到 1970 的偏移后换算成毫秒
    const unsigned long long kEpochDiff100ns = 116444736000000000ULL;
    const unsigned long long k100nsPerMs = 10000ULL;
    return (int64_t)((u.QuadPart - kEpochDiff100ns) / k100nsPerMs);
}

//UTF-8 → CString
CString LibUtf8ToCString(const std::string& s)
{
    if (s.empty())
    {
        return CString();
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0)
    {
        return CString();
    }
    CString out;
    LPWSTR buf = out.GetBuffer(n);
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), buf, n);
    out.ReleaseBuffer(n);
    return out;
}

//CString → UTF-8
std::string LibCStringToUtf8(const CString& s)
{
    if (s.IsEmpty())
    {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out;
    out.resize((size_t)n);
    ::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), &out[0], n, NULL, NULL);
    return out;
}

//左栏按会话计数的文字：图片「N 张」、链接「N 条」、文件「N 个」
std::wstring LibCountText(int kind, long long count)
{
    if (kind == kHistoryLibraryImages)
    {
        return TRF(L"{0} 张", count);
    }
    if (kind == kHistoryLibraryLinks)
    {
        return TRF(L"{0} 条", count);
    }
    return TRF(L"{0} 个", count);
}

//把多行文字压成一行：换行与制表符换成空格，供列表单行显示
CString LibSingleLine(const CString& text)
{
    CString line = text;
    line.Replace(_T("\r\n"), _T(" "));
    line.Replace(_T('\r'), _T(' '));
    line.Replace(_T('\n'), _T(' '));
    line.Replace(_T('\t'), _T(' '));
    return line;
}

//时间范围 → 时间下拉的下标（前六项一一对应，其余按「全部时间」）
int LibComboIndexFromRange(HistoryTimeRange range)
{
    if (range >= kHistoryTimeAll && range <= kHistoryTime1Year)
    {
        return (int)range;
    }
    return (int)kHistoryTimeAll;
}

//在文件 / 图片页的根容器里按编号找控件（只在本页里找，不会找到聊天记录页的同号控件）
DuiControl* FindInLibrary(DuiControl* content, UINT id)
{
    if (content == NULL)
    {
        return NULL;
    }
    DuiControl* lib = content->FindCtrlById(kIdLibBody);
    return (lib != NULL) ? lib->FindCtrlById(id) : NULL;
}

//一个按编号连续排列的按钮段里的下标：id 落在 [base, base + 容量) 时返回 id - base，否则返回 -1
int IndexInRange(UINT id, UINT base)
{
    if (id >= base && id < base + (UINT)kIdLibRowCapacity)
    {
        return (int)(id - base);
    }
    return -1;
}

} // namespace

// ====================================================================================
// 标签切换与刷新
// ====================================================================================

void CBalloonHistoryWindow::SwitchManagerTab(int tab)
{
    if (tab < kHistoryManagerTabChat || tab > kHistoryManagerTabMentions)
    {
        tab = kHistoryManagerTabChat;
    }
    if (tab == m_mgrTab)
    {
        return;
    }
    LOG_INFO("history manager: tab %d -> %d.", m_mgrTab, tab);
    const bool wasChatList = ManagerShowsChatList();
    m_mgrTab = tab;
    DuiControl* content = GetClientContent();
    ApplyHistoryManagerTab(content, tab);

    if (tab == kHistoryManagerTabChat)
    {
        //回到聊天记录页：左栏换回会话列表（搜索词还在时连同跨会话搜索的结果），右侧按当前数据刷新一次。
        //在文件 / 图片 / 链接页时改过搜索词的话，跨会话搜索还没按新词查过，这里补发
        if (!m_mgrQuery.empty() && m_mgrHitsQuery != m_mgrQuery)
        {
            SubmitManagerSearch();
        }
        FillManagerTrees();
        SyncManagerSelection(true);
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
        return;
    }

    //标签与汇总类别的对应
    int kind = kHistoryLibraryFiles;
    switch (tab)
    {
    //图片
    case kHistoryManagerTabImages:
        kind = kHistoryLibraryImages;
        break;
    //链接
    case kHistoryManagerTabLinks:
        kind = kHistoryLibraryLinks;
        break;
    //与我有关：跨全部会话汇总，不沿用文件页里选过的某个会话
    case kHistoryManagerTabMentions:
        kind = kHistoryLibraryMentions;
        m_libModel.SetConversation(1, 0);
        break;
    //文件
    case kHistoryManagerTabFiles:
    default:
        kind = kHistoryLibraryFiles;
        break;
    }
    m_libModel.SetStore(ManagerStore());
    m_libModel.SetSelfUserId(SelfUserId());
    m_libModel.SetKind(kind);
    m_libModel.SetCtypeOnly(m_mgrTypeFilter);
    //关键字里的 from:名字 在链接页、与我有关页才用到，按本地库全部发言人解析一次
    m_libModel.SetSyntaxFromIds(ResolveSyntaxFromIds(m_libModel.Filter().m_keyword, m_libModel.Filter().m_wildcard,
                                                     true));
    ApplyHistoryLibraryKind(content, kind);
    LOG_INFO("history manager: left list %s.",
             (wasChatList == ManagerShowsChatList()) ? "unchanged" : "switched");
    //文件页要把群文件汇总进来：请主面板把群文件信息缓存拉齐（最近拉齐过的群不再拉），拉齐后经
    //NotifyGroupFileCacheChanged 刷新
    if (kind == kHistoryLibraryFiles && m_pMainPanel != NULL)
    {
        m_pMainPanel->SyncGroupFileCache(false);
    }
    RefreshLibrary(kLibRefreshAll);
}

bool CBalloonHistoryWindow::ManagerShowsChatList() const
{
    return m_mgrTab == kHistoryManagerTabChat || m_mgrTab == kHistoryManagerTabMentions;
}

void CBalloonHistoryWindow::EnterMentionsPage()
{
    LOG_INFO("history manager: enter mentions page.");
    SwitchManagerTab(kHistoryManagerTabMentions);
    SyncManagerSelection(true);
}

void CBalloonHistoryWindow::LeaveMentionsPage()
{
    if (m_mgrTab != kHistoryManagerTabMentions)
    {
        return;
    }
    LOG_INFO("history manager: leave mentions page for a conversation.");
    m_mgrTab = kHistoryManagerTabChat;
    ApplyHistoryManagerTab(GetClientContent(), kHistoryManagerTabChat);
    //与我有关页显示期间聊天记录页是隐藏的，期间到达的消息没有刷新进去：回来后完整刷新一次
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
}

void CBalloonHistoryWindow::EnsureChatTab()
{
    if (m_mgrTab == kHistoryManagerTabChat && !m_storageMode)
    {
        return;
    }
    LOG_INFO("history manager: back to chat tab for an entry that opens a conversation.");
    //停在本机存储占用页时同样回到聊天记录页（ApplyHistoryManagerTab 会把存储页隐藏）
    ResetStorageMode();
    m_mgrTab = kHistoryManagerTabChat;
    if (IsWindow())
    {
        ApplyHistoryManagerTab(GetClientContent(), kHistoryManagerTabChat);
        FillManagerTrees();
    }
}

void CBalloonHistoryWindow::RefreshLibrary(int scope)
{
    if (!IsWindow() || m_mgrTab == kHistoryManagerTabChat)
    {
        return;
    }
    DuiControl* content = GetClientContent();
    DuiScrollView* scroll = dynamic_cast<DuiScrollView*>(FindInLibrary(content, kIdLibScroll));
    if (content == NULL || scroll == NULL)
    {
        return;
    }
    const int64_t now = LibNowMs();
    m_libModel.SetStore(ManagerStore());
    m_libModel.SetSelfUserId(SelfUserId());
    bool ok = (scope == kLibRefreshPage) ? m_libModel.RefreshPage(now) : m_libModel.Refresh(now);
    //与我有关页的左栏是「聊天」标签的会话列表，不显示按会话的条数，不必统计
    if (scope == kLibRefreshAll && m_mgrTab != kHistoryManagerTabMentions)
    {
        ok = m_libModel.RefreshConvCounts(now) && ok;
    }

    const HistoryLibraryViewData data = BuildLibraryViewData();
    const RECT rcScroll = scroll->GetRect();
    scroll->SetContent(BuildHistoryLibraryList(data, rcScroll.right - rcScroll.left));
    //换了一页或换了筛选条件就是另一批内容，滚回顶部；同一页的重画（下载完成等）保持当前位置
    const HistoryLibraryFilter& now_filter = m_libModel.Filter();
    const bool sameShown = (now_filter.m_page == m_libShownFilter.m_page
                            && now_filter.m_kind == m_libShownFilter.m_kind
                            && now_filter.m_direction == m_libShownFilter.m_direction
                            && now_filter.m_source == m_libShownFilter.m_source
                            && now_filter.m_category == m_libShownFilter.m_category
                            && now_filter.m_range == m_libShownFilter.m_range
                            && now_filter.m_keyword == m_libShownFilter.m_keyword
                            && now_filter.m_wildcard == m_libShownFilter.m_wildcard
                            && now_filter.m_syntaxFromIds == m_libShownFilter.m_syntaxFromIds
                            && now_filter.m_relation == m_libShownFilter.m_relation
                            && now_filter.m_ctype == m_libShownFilter.m_ctype
                            && now_filter.m_cid == m_libShownFilter.m_cid
                            && now_filter.m_ctypeOnly == m_libShownFilter.m_ctypeOnly);
    if (!sameShown)
    {
        scroll->SetScrollPos(0);
    }
    m_libShownFilter = now_filter;
    scroll->ForceLayout(scroll->GetRect());
    scroll->Invalidate();
    ApplyHistoryLibraryChrome(content, data);

    if (scope == kLibRefreshAll)
    {
        FillManagerTrees();
    }
    LOG_INFO("history library: refreshed scope %d, kind %d, total %lld, page %d/%d, %u items, ok %d.",
             scope, now_filter.m_kind, (long long)m_libModel.Total(), m_libModel.CurrentPage(),
             m_libModel.PageCount(), (unsigned)m_libModel.Items().size(), (int)ok);
}

// ====================================================================================
// 渲染数据
// ====================================================================================

CString CBalloonHistoryWindow::LibraryConversationName(int ctype, int64_t cid) const
{
    for (size_t i = 0; i < m_mgrItems.size(); ++i)
    {
        if (m_mgrItems[i].m_ctype == ctype && m_mgrItems[i].m_cid == cid && !m_mgrItems[i].m_name.empty())
        {
            return CString(m_mgrItems[i].m_name.c_str());
        }
    }
    CString name;
    CString timeText;
    HBITMAP avatar = NULL;
    const_cast<CBalloonHistoryWindow*>(this)->ResolveManagerRow(ctype, cid, 0, name, timeText, avatar);
    return name;
}

bool CBalloonHistoryWindow::ResolveLibraryItemLocal(const HistoryLibraryItem& item, CString& outPath) const
{
    outPath.Empty();
    if (item.m_isGroupFile)
    {
        //群文件：缓存表登记的落地路径，文件还在、大小对得上才算本机有
        if (item.m_localPath.empty())
        {
            return false;
        }
        const CString path = LibUtf8ToCString(item.m_localPath);
        if (!balloonchatwnd::IsLocalFileUsable(std::wstring((LPCTSTR)path), item.m_size))
        {
            return false;
        }
        outPath = path;
        return true;
    }
    //帖子卡片里的一张图片：按那张图片的 MD5 找聊天图片缓存
    if (item.m_cardImageIndex >= 0)
    {
        const HistoryCardThumbView view = ResolveCardThumbView(CardImageOfLibraryItem(item));
        const DWORD attr = view.m_path.IsEmpty() ? INVALID_FILE_ATTRIBUTES : ::GetFileAttributesW(view.m_path);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            return false;
        }
        outPath = view.m_path;
        return true;
    }
    return ResolveRecordLocalFile(RecordFromLibraryItem(item), outPath);
}

int CBalloonHistoryWindow::LibraryItemState(const HistoryLibraryItem& item, int& outProgress) const
{
    outProgress = 0;
    CString local;
    if (ResolveLibraryItemLocal(item, local))
    {
        return kHistoryLibFileLocal;
    }
    if (item.m_isGroupFile)
    {
        std::map<std::string, int>::const_iterator it = m_libGroupDownloads.find(item.m_entryid);
        if (it != m_libGroupDownloads.end())
        {
            outProgress = it->second;
            return kHistoryLibFileDownloading;
        }
        return item.m_fileid.empty() ? kHistoryLibFileUnavailable : kHistoryLibFileRemote;
    }
    //帖子卡片里的一张图片：下载按图片的 MD5 登记
    if (item.m_cardImageIndex >= 0)
    {
        if (m_cardDownloading.count(item.m_md5) > 0)
        {
            return kHistoryLibFileDownloading;
        }
        return (item.m_fileid.empty() || item.m_md5.empty()) ? kHistoryLibFileUnavailable : kHistoryLibFileRemote;
    }
    if (m_downloadingMsgids.find(item.m_msgid) != m_downloadingMsgids.end())
    {
        return kHistoryLibFileDownloading;
    }
    //文件夹记录没有可下载的东西；在线直传的文件服务器上没有副本
    if (item.m_msgtype == ::message_type_folder || item.m_direct || item.m_fileid.empty())
    {
        return kHistoryLibFileUnavailable;
    }
    return kHistoryLibFileRemote;
}

HistoryLibraryViewData CBalloonHistoryWindow::BuildLibraryViewData() const
{
    HistoryLibraryViewData data;
    const HistoryLibraryFilter& f = m_libModel.Filter();
    const bool images = (f.m_kind == kHistoryLibraryImages);
    const int64_t now = LibNowMs();
    const int64_t self = SelfUserId();

    data.m_kind = f.m_kind;
    const CString convName = (f.m_cid != 0) ? LibraryConversationName(f.m_ctype, f.m_cid) : CString(TR(L"全部会话"));
    const long long total = (long long)m_libModel.Total();
    switch (f.m_kind)
    {
    //图片页
    case kHistoryLibraryImages:
        data.m_title = TRF(L"图片 · {0}", convName).c_str();
        data.m_subtitle = TRF(L"共 {0} 张图片，只包括本机已保存的聊天记录", total).c_str();
        break;
    //链接页
    case kHistoryLibraryLinks:
        data.m_title = TRF(L"链接 · {0}", convName).c_str();
        data.m_subtitle = TRF(L"共 {0} 条含链接的消息，只包括本机已保存的聊天记录", total).c_str();
        break;
    //与我有关页
    case kHistoryLibraryMentions:
        data.m_title = TR(L"与我有关");
        data.m_subtitle = TRF(L"共 {0} 条 @我的、回复我的与我发的消息，只包括本机已保存的聊天记录", total).c_str();
        break;
    //文件页
    case kHistoryLibraryFiles:
    default:
        data.m_title = TRF(L"文件 · {0}", convName).c_str();
        data.m_subtitle = TRF(L"共 {0} 个文件，聊天文件只包括本机已保存的记录", total).c_str();
        break;
    }
    data.m_showExpandLeft = m_mgrCollapsed;
    data.m_direction = f.m_direction;
    data.m_relation = f.m_relation;
    data.m_source = f.m_source;
    data.m_category = f.m_category;
    data.m_timeIndex = LibComboIndexFromRange(f.m_range);
    data.m_wildcard = f.m_wildcard;
    data.m_currentPage = m_libModel.CurrentPage();
    data.m_pageCount = m_libModel.PageCount();
    data.m_total = m_libModel.Total();

    const std::vector<HistoryLibraryItem>& items = m_libModel.Items();
    const bool keyword = LibraryHasKeyword(f);
    const std::wstring keywordW((LPCTSTR)LibUtf8ToCString(f.m_keyword));
    const int64_t todayKey = LocalDayKey(now);
    const bool textRows = (f.m_kind == kHistoryLibraryLinks || f.m_kind == kHistoryLibraryMentions);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const HistoryLibraryItem& item = items[i];
        const bool mine = (self > 0 && item.m_fromid == self);
        if (textRows)
        {
            data.m_textRows.push_back(BuildLibraryTextRow(item, keyword ? keywordW : std::wstring(), now));
            continue;
        }
        int progress = 0;
        const int state = LibraryItemState(item, progress);
        if (images)
        {
            //图片：缩略图状态与聊天记录页的图片网格同一套判断；同一天的排成一组
            HistoryRowView cell;
            if (item.m_cardImageIndex >= 0)
            {
                //帖子卡片里的一张图片（docs/帖子群方案.md 14.5）：按那张图片的 MD5 判断，单击看图时再下载
                const HistoryCardThumbView view = ResolveCardThumbView(CardImageOfLibraryItem(item));
                cell.m_hasImageThumb = true;
                cell.m_thumbImagePath = view.m_path;
                cell.m_thumbState = view.m_state;
                cell.m_thumbClickable = true;
                cell.m_thumbTooltip = view.m_tooltip;
            }
            else
            {
                FillImageThumb(cell, item.m_msgid, item.m_md5, item.m_fileid, (int)i);
            }
            cell.m_senderName = mine ? CString(TR(L"我")) : ResolveSenderName(item.m_fromid);
            cell.m_timeText = LibUtf8ToCString(FormatLibraryTime(item.m_timestamp, now));
            const int64_t dayKey = LocalDayKey(item.m_timestamp);
            if (i == 0 || dayKey != LocalDayKey(items[i - 1].m_timestamp))
            {
                cell.m_groupLabel = LibUtf8ToCString(FormatDateGroupLabel(dayKey, todayKey));
            }
            if (state == kHistoryLibFileRemote)
            {
                ++data.m_pendingDownloads;
            }
            data.m_imageCells.push_back(cell);
            continue;
        }

        HistoryLibraryFileRowView row;
        row.m_isFolder = (item.m_msgtype == ::message_type_folder);
        row.m_name = LibUtf8ToCString(item.m_name);
        row.m_ext = LibUtf8ToCString(FileExtFromName(item.m_name));
        if (keyword)
        {
            row.m_nameHighlights = FindSearchHighlightRanges(std::wstring((LPCTSTR)row.m_name), keywordW, f.m_wildcard);
        }
        row.m_isGroupFile = item.m_isGroupFile;
        const CString conv = LibraryConversationName(item.m_ctype, item.m_cid);
        if (item.m_isGroupFile)
        {
            row.m_mineTag = mine ? CString(TR(L"我上传的")) : CString();
            const CString uploader = !item.m_uploaderName.empty() ? LibUtf8ToCString(item.m_uploaderName)
                                                                  : ResolveSenderName(item.m_fromid);
            row.m_sourceText = mine ? conv : CString(TRF(L"{0}上传 · {1}", uploader, conv).c_str());
        }
        else if (mine)
        {
            row.m_mineTag = TR(L"我发送的");
            //单聊里本人发的写「发给 某某」，群里写群名
            row.m_sourceText = (item.m_ctype == 1) ? CString(TRF(L"发给 {0}", conv).c_str()) : conv;
        }
        else
        {
            row.m_sourceText = ResolveSenderName(item.m_fromid) + _T(" · ") + conv;
        }
        row.m_sizeText = LibUtf8ToCString(FormatFileSize(item.m_size));
        row.m_timeText = LibUtf8ToCString(FormatLibraryTime(item.m_timestamp, now));
        row.m_state = state;
        row.m_progress = progress;
        row.m_canLocate = item.m_isGroupFile || item.m_msgid > 0;
        //「下载本页全部」只下聊天里的文件：群文件每个都要选保存位置
        if (state == kHistoryLibFileRemote && !item.m_isGroupFile)
        {
            ++data.m_pendingDownloads;
        }
        data.m_fileRows.push_back(row);
    }

    //空状态
    if (images)
    {
        data.m_emptyTitle = TR(L"这里还没有图片");
        data.m_emptySub = TR(L"只包括本机已保存的聊天记录里的图片");
    }
    else if (textRows && keyword)
    {
        data.m_emptyTitle = TR(L"没有找到符合搜索条件的消息");
        data.m_emptySub = TR(L"换个词，或者把时间与其它筛选放宽试试");
    }
    else if (f.m_kind == kHistoryLibraryLinks)
    {
        data.m_emptyTitle = TR(L"这里还没有链接");
        data.m_emptySub = TR(L"只包括本机已保存的聊天记录里含链接的消息");
    }
    else if (f.m_kind == kHistoryLibraryMentions)
    {
        data.m_emptyTitle = TR(L"这里还没有与你有关的消息");
        data.m_emptySub = TR(L"只包括本机已保存的聊天记录；更早的老消息没有记下 @ 与引用信息");
    }
    else if (keyword)
    {
        data.m_emptyTitle = TRF(L"没有找到文件名包含「{0}」的文件", CString(keywordW.c_str())).c_str();
        data.m_emptySub = TR(L"换个词，或者把来源、类型与时间的筛选放宽试试");
    }
    else
    {
        data.m_emptyTitle = TR(L"这里还没有文件");
        data.m_emptySub = TR(L"包括本机已保存的聊天记录里的文件，以及本人所在群的群文件");
    }
    return data;
}

HistoryLibraryTextRowView CBalloonHistoryWindow::BuildLibraryTextRow(const HistoryLibraryItem& item,
                                                                     const std::wstring& keywordW, int64_t nowMs) const
{
    const HistoryLibraryFilter& f = m_libModel.Filter();
    const int64_t self = SelfUserId();
    const bool mine = (self > 0 && item.m_fromid == self);
    HistoryLibraryTextRowView row;
    row.m_timeText = LibUtf8ToCString(FormatLibraryTime(item.m_timestamp, nowMs));
    //来源：本人发的写「发给 某某」（单聊）或群名，别人发的写「发送人 · 会话」
    const CString conv = LibraryConversationName(item.m_ctype, item.m_cid);
    if (mine)
    {
        row.m_sourceText = (item.m_ctype == 1) ? CString(TRF(L"发给 {0}", conv).c_str()) : conv;
    }
    else
    {
        row.m_sourceText = ResolveSenderName(item.m_fromid) + _T(" · ") + conv;
    }

    if (f.m_kind == kHistoryLibraryLinks)
    {
        row.m_mineTag = mine ? CString(TR(L"我发送的")) : CString();
        //正文取 search_text（m_name）：内容 JSON 里的 '/' 被转义成 "//"，不能直接识别链接（见 HistoryLibraryLinks.h）
        HistoryLinkSummary summary;
        if (SummarizeLibraryLinks(item.m_name, summary))
        {
            row.m_title = summary.m_domain.c_str();
            row.m_text = summary.m_url.c_str();
            row.m_canOpen = balloonchatwnd::IsSafeLinkUrl(summary.m_url);
            if (summary.m_linkCount > 1)
            {
                row.m_sourceText += _T(" \x00B7 ");
                row.m_sourceText += TRF(L"等 {0} 个链接", summary.m_linkCount).c_str();
            }
        }
        else
        {
            //入库时按 http:// 判为含链接、识别时却没认出（如只写了「http://」）：显示正文
            row.m_text = LibSingleLine(LibUtf8ToCString(item.m_name));
        }
    }
    else
    {
        row.m_relation = item.m_relation;
        row.m_text = LibSingleLine(LibUtf8ToCString(DigestFromContent(item.m_msgtype, item.m_content)));
    }
    if (!keywordW.empty())
    {
        row.m_textHighlights = FindSearchHighlightRanges(std::wstring((LPCTSTR)row.m_text), keywordW, f.m_wildcard);
    }
    return row;
}

// ====================================================================================
// 左栏
// ====================================================================================

void CBalloonHistoryWindow::FillLibraryManagerTree()
{
    DuiControl* content = GetClientContent();
    balloonmain::MainPanelTreeView* convTree = (content != NULL)
        ? dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(kIdMgrConvTree)) : NULL;
    balloonmain::MainPanelTreeView* hitTree = (content != NULL)
        ? dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(kIdMgrHitTree)) : NULL;
    if (convTree == NULL || hitTree == NULL)
    {
        return;
    }
    const int kind = m_libModel.Filter().m_kind;
    convTree->Clear();
    hitTree->Clear();
    m_mgrConvNodes.clear();
    m_mgrHitNodes.clear();
    m_mgrHits.clear();
    m_libExtraNodes.clear();
    m_mentionsNode = -1;

    //第一行：全部会话
    m_libAllNode = convTree->AddRoot(_T(""));
    convTree->SetItemLabel(m_libAllNode, TR(L"全部会话"));
    LPCTSTR allSub = TR(L"全部文件");
    if (kind == kHistoryLibraryImages)
    {
        allSub = TR(L"全部图片");
    }
    else if (kind == kHistoryLibraryLinks)
    {
        allSub = TR(L"全部链接");
    }
    convTree->SetItemSubLabel(m_libAllNode, allSub);
    const std::wstring allCount = LibCountText(kind, (long long)m_libModel.CountAll());
    convTree->SetItemRightTopText(m_libAllNode, allCount.c_str());
    int rows = 1;

    //其余：有内容的会话，按左栏的先后（最近有消息的在前），类型胶囊与名字搜索照常生效
    std::set<std::pair<int, int64_t> > listed;
    const std::vector<int> order = FilterManagerConversations(m_mgrItems, m_mgrTypeFilter, m_mgrQuery,
                                                              std::vector<HistoryConversationKey>());
    for (size_t k = 0; k < order.size(); ++k)
    {
        const ManagerConversationItem& item = m_mgrItems[(size_t)order[k]];
        const int count = m_libModel.CountOf(item.m_ctype, item.m_cid);
        if (count <= 0)
        {
            continue;
        }
        CString name;
        CString timeText;
        HBITMAP avatar = NULL;
        ResolveManagerRow(item.m_ctype, item.m_cid, item.m_lastTimestamp, name, timeText, avatar);
        const int node = convTree->AddRoot(_T(""));
        convTree->SetItemLabel(node, item.m_name.c_str());
        convTree->SetItemSubLabel(node, (item.m_ctype == 2) ? TR(L"群聊") : TR(L"单聊"));
        const std::wstring countText = LibCountText(kind, count);
        convTree->SetItemRightTopText(node, countText.c_str());
        if (avatar != NULL)
        {
            convTree->SetItemIcon(node, avatar);
        }
        m_mgrConvNodes[node] = (size_t)order[k];
        listed.insert(std::make_pair(item.m_ctype, item.m_cid));
        ++rows;
    }

    //左栏会话列表里没有、却有内容的会话（只有群文件、还没有聊天记录的群）补在后面。名字搜索只做简单的包含匹配
    const std::vector<HistoryLibraryConvCount>& counts = m_libModel.ConvCounts();
    for (size_t i = 0; i < counts.size(); ++i)
    {
        const HistoryLibraryConvCount& c = counts[i];
        if (c.m_count <= 0 || listed.count(std::make_pair(c.m_ctype, c.m_cid)) != 0
            || !MatchesManagerTypeFilter(c.m_ctype, m_mgrTypeFilter))
        {
            continue;
        }
        CString name;
        CString timeText;
        HBITMAP avatar = NULL;
        ResolveManagerRow(c.m_ctype, c.m_cid, 0, name, timeText, avatar);
        if (!m_mgrQuery.empty() && name.Find(m_mgrQuery.c_str()) < 0)
        {
            continue;
        }
        const int node = convTree->AddRoot(_T(""));
        convTree->SetItemLabel(node, name);
        convTree->SetItemSubLabel(node, (c.m_ctype == 2) ? TR(L"群聊") : TR(L"单聊"));
        const std::wstring countText = LibCountText(kind, c.m_count);
        convTree->SetItemRightTopText(node, countText.c_str());
        if (avatar != NULL)
        {
            convTree->SetItemIcon(node, avatar);
        }
        m_libExtraNodes[node] = HistoryConversationKey(c.m_ctype, c.m_cid);
        ++rows;
    }

    HistoryManagerListView view;
    view.m_searching = false;
    view.m_convRows = rows;
    ApplyHistoryManagerListView(content, view);
    SyncLibrarySelection(false);
}

void CBalloonHistoryWindow::SyncLibrarySelection(bool scrollIntoView)
{
    DuiControl* content = GetClientContent();
    DuiTreeView* convTree = (content != NULL) ? dynamic_cast<DuiTreeView*>(content->FindCtrlById(kIdMgrConvTree)) : NULL;
    if (convTree == NULL)
    {
        return;
    }
    const HistoryLibraryFilter& f = m_libModel.Filter();
    int node = (f.m_cid == 0) ? m_libAllNode : -1;
    if (f.m_cid != 0)
    {
        for (std::map<int, size_t>::const_iterator it = m_mgrConvNodes.begin(); it != m_mgrConvNodes.end(); ++it)
        {
            const ManagerConversationItem& item = m_mgrItems[it->second];
            if (item.m_ctype == f.m_ctype && item.m_cid == f.m_cid)
            {
                node = it->first;
                break;
            }
        }
        for (std::map<int, HistoryConversationKey>::const_iterator it = m_libExtraNodes.begin();
             node < 0 && it != m_libExtraNodes.end(); ++it)
        {
            if (it->second.m_ctype == f.m_ctype && it->second.m_cid == f.m_cid)
            {
                node = it->first;
            }
        }
    }
    convTree->SetCurSel(node, false);
    convTree->Invalidate();
    //选中行滚进可见区域：「全部会话」在最上面，其余行没有专门的滚动需求，滚到顶部即可
    if (scrollIntoView && m_pMgrScroll != NULL && node == m_libAllNode)
    {
        m_pMgrScroll->SetScrollPos(0);
    }
}

void CBalloonHistoryWindow::OnLibraryConvClicked(int nodeId)
{
    if (nodeId == m_libAllNode)
    {
        m_libModel.SetConversation(1, 0);
    }
    else
    {
        std::map<int, size_t>::const_iterator it = m_mgrConvNodes.find(nodeId);
        std::map<int, HistoryConversationKey>::const_iterator ex = m_libExtraNodes.find(nodeId);
        if (it != m_mgrConvNodes.end() && it->second < m_mgrItems.size())
        {
            m_libModel.SetConversation(m_mgrItems[it->second].m_ctype, m_mgrItems[it->second].m_cid);
        }
        else if (ex != m_libExtraNodes.end())
        {
            m_libModel.SetConversation(ex->second.m_ctype, ex->second.m_cid);
        }
        else
        {
            return;
        }
    }
    LOG_INFO("history library: conversation ctype=%d, cid=%lld.", m_libModel.Filter().m_ctype,
             (long long)m_libModel.Filter().m_cid);
    SyncLibrarySelection(false);
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
}

// ====================================================================================
// 通知与定时器
// ====================================================================================

bool CBalloonHistoryWindow::HandleLibraryNotify(const DuiNotify* pNotify)
{
    if (pNotify == NULL || m_mgrTab == kHistoryManagerTabChat)
    {
        return false;
    }
    DuiControl* content = GetClientContent();
    const UINT id = pNotify->ctrlId;

    //左栏会话列表单击：只看那个会话或全部会话。与我有关页的左栏是「聊天」标签的会话列表，单击交回原来的分发
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdMgrConvTree && !ManagerShowsChatList())
    {
        OnLibraryConvClicked((int)pNotify->extra);
        return true;
    }
    //与我有关页的关系胶囊：编号连续，与 HistoryLibraryRelation 一一对应
    if (pNotify->code == DUIN_CLICK && id >= (UINT)kIdLibRelAll && id <= (UINT)kIdLibRelMine)
    {
        m_libModel.SetRelation((int)(id - kIdLibRelAll));
        LOG_INFO("history library: relation -> %d.", m_libModel.Filter().m_relation);
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
        return true;
    }
    //「»」：展开左栏
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdLibExpandLeft)
    {
        SetManagerCollapsed(false);
        return true;
    }
    //方向胶囊：编号连续，与 HistoryLibraryDirection 一一对应
    if (pNotify->code == DUIN_CLICK && id >= (UINT)kIdLibDirAll && id <= (UINT)kIdLibDirReceived)
    {
        m_libModel.SetDirection((int)(id - kIdLibDirAll));
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
        return true;
    }
    //来源、类型、时间下拉：下标与各自的取值一一对应
    if (pNotify->code == DUIN_VALUECHANGED
        && (id == (UINT)kIdLibSourceCombo || id == (UINT)kIdLibCategoryCombo || id == (UINT)kIdLibTimeCombo))
    {
        DuiComboBox* combo = dynamic_cast<DuiComboBox*>(FindInLibrary(content, id));
        const int sel = (combo != NULL) ? combo->GetCurSel() : -1;
        if (sel < 0)
        {
            return true;
        }
        if (id == (UINT)kIdLibSourceCombo)
        {
            m_libModel.SetSource(sel);
        }
        else if (id == (UINT)kIdLibCategoryCombo)
        {
            m_libModel.SetCategory(sel);
        }
        else
        {
            m_libModel.SetTimeRange(TimeRangeFromComboIndex(sel));
        }
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
        return true;
    }
    //文件名搜索框：停止输入一段时间后再查
    if (pNotify->code == DUIN_VALUECHANGED && id == (UINT)kIdLibSearch)
    {
        SetTimer(kLibSearchTimerId, kLibSearchDelayMs, NULL);
        return true;
    }
    //「通配符」复选框（extra 为 1 勾选、0 取消）：有关键字时按新的解释方式重查
    if (pNotify->code == DUIN_VALUECHANGED && id == (UINT)kIdLibWildcard)
    {
        const bool hadKeyword = !m_libModel.Filter().m_keyword.empty();
        m_libModel.SetWildcard(pNotify->extra != 0);
        m_libModel.SetSyntaxFromIds(ResolveSyntaxFromIds(m_libModel.Filter().m_keyword, m_libModel.Filter().m_wildcard,
                                                         true));
        LOG_INFO("history library: wildcard -> %d.", (int)(pNotify->extra != 0));
        if (hadKeyword)
        {
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
        }
        return true;
    }

    if (pNotify->code == DUIN_CLICK)
    {
        //分页
        int target = 0;
        if (id == (UINT)kIdLibPageFirst)
        {
            target = 1;
        }
        else if (id == (UINT)kIdLibPagePrev)
        {
            target = m_libModel.CurrentPage() - 1;
        }
        else if (id == (UINT)kIdLibPageNext)
        {
            target = m_libModel.CurrentPage() + 1;
        }
        else if (id == (UINT)kIdLibPageLast)
        {
            target = m_libModel.PageCount();
        }
        else if (id >= (UINT)kIdLibPageCellBase && id < (UINT)(kIdLibPageCellBase + kHistoryPagerMaxCells))
        {
            target = HistoryLibraryPageOfCell((int)(id - kIdLibPageCellBase), m_libModel.CurrentPage(),
                                              m_libModel.PageCount());
        }
        if (target > 0)
        {
            if (target != m_libModel.CurrentPage() && target <= m_libModel.PageCount())
            {
                m_libModel.GoToPage(target);
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
            }
            return true;
        }
        //「下载本页全部」
        if (id == (UINT)kIdLibBatchDownload)
        {
            DownloadLibraryPage();
            return true;
        }
        //行内按钮。「打开」在链接页是用浏览器打开链接
        int index = IndexInRange(id, kIdLibOpenBase);
        if (index >= 0)
        {
            if (m_libModel.Filter().m_kind == kHistoryLibraryLinks)
            {
                OpenLibraryLink(index);
            }
            else
            {
                OpenLibraryItem(index);
            }
            return true;
        }
        //链接页「复制链接」、与我有关页「复制」
        index = IndexInRange(id, kIdLibCopyBase);
        if (index >= 0)
        {
            CopyLibraryItemText(index);
            return true;
        }
        index = IndexInRange(id, kIdLibFolderBase);
        if (index >= 0)
        {
            OpenLibraryItemFolder(index);
            return true;
        }
        index = IndexInRange(id, kIdLibDownloadBase);
        if (index >= 0)
        {
            ToggleLibraryDownload(index);
            return true;
        }
        index = IndexInRange(id, kIdLibLocateBase);
        if (index >= 0)
        {
            LocateLibraryItem(index);
            return true;
        }
        index = IndexInRange(id, kIdLibMoreBase);
        if (index >= 0)
        {
            //「⋯」：在按钮下方弹出菜单，延后到点击事件解栈之后
            POINT pt;
            ::GetCursorPos(&pt);
            ::PostMessage(m_hWnd, WM_HISTORY_SHOW_LIB_MENU, (WPARAM)index, MAKELPARAM((WORD)pt.x, (WORD)pt.y));
            return true;
        }
        //图片页：单击缩略图 —— 本机有原图就看大图，没有就下载并在下完后打开
        index = IndexInRange(id, kIdLibGridBase);
        if (index >= 0)
        {
            ActivateLibraryItem(index);
            return true;
        }
    }
    //双击文件页的一行：本机有就打开，没有就下载
    if (pNotify->code == DUIN_DBLCLK)
    {
        const int index = IndexInRange(id, kIdLibRowBase);
        if (index >= 0)
        {
            ActivateLibraryItem(index);
            return true;
        }
    }
    return false;
}

bool CBalloonHistoryWindow::HandleLibraryTimer(UINT_PTR timerId)
{
    //文件名搜索框停止输入够久了
    if (timerId == kLibSearchTimerId)
    {
        ApplyLibrarySearch();
        return true;
    }
    //群文件缓存变化的合并刷新到点了
    if (timerId == kLibRefreshTimerId)
    {
        KillTimer(kLibRefreshTimerId);
        m_libRefreshPending = false;
        if (m_mgrTab == kHistoryManagerTabFiles)
        {
            RefreshLibrary(kLibRefreshAll);
        }
        return true;
    }
    return false;
}

void CBalloonHistoryWindow::ApplyLibrarySearch()
{
    KillTimer(kLibSearchTimerId);
    DuiEdit* search = dynamic_cast<DuiEdit*>(FindInLibrary(GetClientContent(), kIdLibSearch));
    if (search == NULL)
    {
        return;
    }
    CString text = search->GetText();
    text.Trim();
    const std::string keyword = LibCStringToUtf8(text);
    if (keyword == m_libModel.Filter().m_keyword)
    {
        return;
    }
    m_libModel.SetKeyword(keyword);
    //搜索语法里的 from:名字（链接页、与我有关页用）：按本地库里发过言的所有人的显示名换成 userid
    m_libModel.SetSyntaxFromIds(ResolveSyntaxFromIds(keyword, m_libModel.Filter().m_wildcard, true));
    LOG_INFO("history library: keyword bytes %u.", (unsigned)keyword.size());
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
}

void CBalloonHistoryWindow::NotifyGroupFileCacheChanged()
{
    if (!IsWindow() || m_mgrTab != kHistoryManagerTabFiles || m_libRefreshPending)
    {
        return;
    }
    m_libRefreshPending = true;
    SetTimer(kLibRefreshTimerId, kLibRefreshDelayMs, NULL);
}

void CBalloonHistoryWindow::OnGroupFileDownloadFinished(int64_t groupid, const std::string& entryid, bool ok,
                                                        const std::wstring& /*localPath*/)
{
    const bool mine = (m_libGroupDownloads.erase(entryid) > 0);
    LOG_INFO("history library: group file download finished, groupid=%lld, entryid=%s, ok=%d, mine=%d.",
             (long long)groupid, entryid.c_str(), (int)ok, (int)mine);
    if (!IsWindow())
    {
        return;
    }
    if (mine && !ok)
    {
        ::MessageBox(m_hWnd, TR(L"群文件下载失败，请检查网络后重试。"), TR(L"聊天记录管理"), MB_OK | MB_ICONWARNING);
    }
    if (m_mgrTab == kHistoryManagerTabFiles)
    {
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
    }
}

void CBalloonHistoryWindow::OnGroupFileDownloadProgress(int64_t /*groupid*/, const std::string& entryid,
                                                        int64_t received, int64_t total)
{
    std::map<std::string, int>::iterator it = m_libGroupDownloads.find(entryid);
    if (it == m_libGroupDownloads.end() || !IsWindow() || m_mgrTab != kHistoryManagerTabFiles)
    {
        return;
    }
    int pct = (total > 0) ? (int)((received * kLibPercentMax) / total) : 0;
    pct = (pct < 0) ? 0 : ((pct > kLibPercentMax) ? kLibPercentMax : pct);
    if (pct == it->second)
    {
        return;
    }
    it->second = pct;
    //那一行在当前页时就地更新状态文字，不重建列表
    const std::vector<HistoryLibraryItem>& items = m_libModel.Items();
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_isGroupFile && items[i].m_entryid == entryid)
        {
            const HistoryLibraryViewData data = BuildLibraryViewData();
            if (i < data.m_fileRows.size())
            {
                ApplyHistoryLibraryRowState(GetClientContent(), (int)i, data.m_fileRows[i]);
            }
            break;
        }
    }
}

// ====================================================================================
// 行操作
// ====================================================================================

const HistoryLibraryItem* CBalloonHistoryWindow::LibraryItemAt(int index) const
{
    const std::vector<HistoryLibraryItem>& items = m_libModel.Items();
    if (index < 0 || index >= (int)items.size())
    {
        return NULL;
    }
    return &items[(size_t)index];
}

void CBalloonHistoryWindow::OpenLibraryItem(int index)
{
    const HistoryLibraryItem* item = LibraryItemAt(index);
    if (item == NULL)
    {
        return;
    }
    if (item->m_cardImageIndex >= 0)
    {
        OpenCardImages(item->m_msgtype, item->m_content, item->m_cardImageIndex, item->m_msgid);
        return;
    }
    if (IsHistoryRowImageType(item->m_msgtype))
    {
        ViewRecordImage(RecordFromLibraryItem(*item));
        return;
    }
    CString local;
    if (!ResolveLibraryItemLocal(*item, local))
    {
        return;
    }
    //文件夹记录的本机落点是一个目录：用资源管理器打开它
    if (item->m_msgtype == ::message_type_folder)
    {
        ::ShellExecute(m_hWnd, _T("explore"), local, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    OpenRowFile(local, true);
}

void CBalloonHistoryWindow::OpenLibraryItemFolder(int index)
{
    const HistoryLibraryItem* item = LibraryItemAt(index);
    CString local;
    if (item == NULL || !ResolveLibraryItemLocal(*item, local))
    {
        return;
    }
    OpenRowFolder(local, true);
}

void CBalloonHistoryWindow::ToggleLibraryDownload(int index)
{
    const HistoryLibraryItem* found = LibraryItemAt(index);
    if (found == NULL)
    {
        return;
    }
    //复制一份：下面可能弹模态框，期间列表可能被刷新
    const HistoryLibraryItem item = *found;
    int progress = 0;
    const int state = LibraryItemState(item, progress);

    //帖子卡片里的一张图片：只能下载（这种下载没有取消接口，下载中时什么也不做）
    if (item.m_cardImageIndex >= 0)
    {
        if (state == kHistoryLibFileRemote)
        {
            RequestCardImage(CardImageOfLibraryItem(item));
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
        }
        return;
    }
    if (!item.m_isGroupFile)
    {
        const HistoryRecord rec = RecordFromLibraryItem(item);
        if (state == kHistoryLibFileDownloading)
        {
            CancelRecordDownload(rec);
        }
        else if (state == kHistoryLibFileRemote)
        {
            StartRecordDownload(rec, false, true);
        }
        return;
    }

    //群文件：正在下载就取消
    if (state == kHistoryLibFileDownloading)
    {
        net::FileTransfer::GetInstance().CancelGroupFileDownload(item.m_entryid);
        m_libGroupDownloads.erase(item.m_entryid);
        LOG_INFO("history library: group file download cancelled, entryid=%s.", item.m_entryid.c_str());
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
        return;
    }
    if (state != kHistoryLibFileRemote || m_pMainPanel == NULL || SelfUserId() <= 0)
    {
        return;
    }
    //群文件要用户选定保存位置（与群聊窗的群文件面板一致）
    CString defName = LibUtf8ToCString(item.m_name);
    if (defName.IsEmpty())
    {
        defName = TR(L"文件");
    }
    CString defExt;
    const int dot = defName.ReverseFind(_T('.'));
    if (dot > 0 && dot + 1 < defName.GetLength())
    {
        defExt = defName.Mid(dot + 1);
    }
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"所有文件 (*.*)"), L"*.*");
    CFileDialog dlg(FALSE, defExt.IsEmpty() ? NULL : (LPCTSTR)defExt, defName,
                    OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY, fileFilter.c_str(), m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return;
    }
    net::FileTransfer::GetInstance().EnqueueGroupFileDownload(item.m_cid, item.m_entryid, item.m_fileid, item.m_name,
                                                              item.m_md5, item.m_size, SelfUserId(),
                                                              std::wstring(dlg.m_szFileName));
    m_libGroupDownloads[item.m_entryid] = 0;
    LOG_INFO("history library: group file download started, groupid=%lld, entryid=%s.",
             (long long)item.m_cid, item.m_entryid.c_str());
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

void CBalloonHistoryWindow::LocateLibraryItem(int index)
{
    const HistoryLibraryItem* item = LibraryItemAt(index);
    if (item == NULL)
    {
        return;
    }
    if (item->m_isGroupFile)
    {
        if (m_pMainPanel != NULL)
        {
            m_pMainPanel->ShowGroupFileInChat(item->m_cid, item->m_entryid);
        }
        return;
    }
    LocateRecord(RecordFromLibraryItem(*item));
}

void CBalloonHistoryWindow::OpenLibraryLink(int index)
{
    const HistoryLibraryItem* item = LibraryItemAt(index);
    if (item == NULL)
    {
        return;
    }
    const std::vector<std::wstring> links = LibraryLinksOfText(item->m_name);
    //协议白名单：只放行 http / https，与聊天窗、聊天记录页同一口径
    if (links.empty() || !balloonchatwnd::IsSafeLinkUrl(links[0]))
    {
        LOG_WARNING("history library: msgid=%lld has no link that can be opened.", (long long)item->m_msgid);
        return;
    }
    ::ShellExecuteW(m_hWnd, L"open", links[0].c_str(), NULL, NULL, SW_SHOWNORMAL);
    LOG_INFO("history library: open link of msgid=%lld.", (long long)item->m_msgid);
}

void CBalloonHistoryWindow::CopyLibraryItemText(int index)
{
    const HistoryLibraryItem* item = LibraryItemAt(index);
    if (item == NULL)
    {
        return;
    }
    if (m_libModel.Filter().m_kind == kHistoryLibraryLinks)
    {
        const std::wstring text = JoinLinksForClipboard(LibraryLinksOfText(item->m_name));
        if (!text.empty() && flamingo::CopyTextToClipboard(CString(text.c_str())))
        {
            ShowToast(TR(L"已复制链接"));
        }
        return;
    }
    //与我有关页：文字消息复制正文（search_text 是还原过的正文），其它类型复制它的摘要
    const CString text = (item->m_msgtype == ::message_type_text && !item->m_name.empty())
                       ? LibUtf8ToCString(item->m_name)
                       : LibUtf8ToCString(PreviewFromContent(item->m_msgtype, item->m_content));
    if (!text.IsEmpty() && flamingo::CopyTextToClipboard(text))
    {
        ShowToast(TR(L"已复制"));
    }
}

void CBalloonHistoryWindow::ActivateLibraryItem(int index)
{
    const HistoryLibraryItem* item = LibraryItemAt(index);
    if (item == NULL)
    {
        return;
    }
    //链接页双击打开链接；与我有关页双击定位到聊天
    if (m_libModel.Filter().m_kind == kHistoryLibraryLinks)
    {
        OpenLibraryLink(index);
        return;
    }
    if (m_libModel.Filter().m_kind == kHistoryLibraryMentions)
    {
        LocateLibraryItem(index);
        return;
    }
    //帖子卡片里的一张图片：看图，可以左右翻看这张卡片的全部图片（本机没有时先下载，下完再打开）
    if (item->m_cardImageIndex >= 0)
    {
        OpenCardImages(item->m_msgtype, item->m_content, item->m_cardImageIndex, item->m_msgid);
        return;
    }
    //图片：看大图（本机没有原图时先下载，下完自动打开）
    if (IsHistoryRowImageType(item->m_msgtype))
    {
        ViewRecordImage(RecordFromLibraryItem(*item));
        return;
    }
    int progress = 0;
    const int state = LibraryItemState(*item, progress);
    if (state == kHistoryLibFileLocal)
    {
        OpenLibraryItem(index);
    }
    else if (state == kHistoryLibFileRemote)
    {
        ToggleLibraryDownload(index);
    }
}

void CBalloonHistoryWindow::SaveLibraryItemAs(int index)
{
    const HistoryLibraryItem* found = LibraryItemAt(index);
    CString local;
    if (found == NULL || !ResolveLibraryItemLocal(*found, local))
    {
        return;
    }
    const HistoryLibraryItem item = *found;
    if (IsHistoryRowImageType(item.m_msgtype) || item.m_cardImageIndex >= 0)
    {
        const std::wstring path((LPCTSTR)local);
        balloonchatwnd::SaveChatImageAs(m_hWnd, path, balloonchatwnd::MakeImageSaveAsDefaultName(path, item.m_msgid));
        return;
    }
    SaveLocalFileAs(LibUtf8ToCString(item.m_name), local);
}

void CBalloonHistoryWindow::CopyLibraryItemName(int index)
{
    const HistoryLibraryItem* item = LibraryItemAt(index);
    if (item == NULL || item->m_name.empty())
    {
        return;
    }
    if (flamingo::CopyTextToClipboard(LibUtf8ToCString(item->m_name)))
    {
        ShowToast(TR(L"已复制文件名"));
    }
}

void CBalloonHistoryWindow::DownloadLibraryPage()
{
    int started = 0;
    const std::vector<HistoryLibraryItem> items = m_libModel.Items();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const HistoryLibraryItem& item = items[i];
        int progress = 0;
        if (item.m_isGroupFile || LibraryItemState(item, progress) != kHistoryLibFileRemote)
        {
            continue;
        }
        //帖子卡片里的一张图片：按图片下载，失败时缩略图显示破图
        if (item.m_cardImageIndex >= 0)
        {
            RequestCardImage(CardImageOfLibraryItem(item));
            ++started;
            continue;
        }
        const HistoryRecord rec = RecordFromLibraryItem(item);
        if (StartRecordDownload(rec, false, false))
        {
            //文件的结果按批次汇总提示一次（与「文件」分类的「全部下载」同一做法）；图片下载失败时缩略图显示破图
            if (!IsHistoryRowImageType(item.m_msgtype))
            {
                m_batchDownload.Add(item.m_msgid);
            }
            ++started;
        }
    }
    LOG_INFO("history library: download page, %d started.", started);
    if (started > 0)
    {
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
    }
}

void CBalloonHistoryWindow::ShowLibraryMenu(int index, POINT ptScreen)
{
    const HistoryLibraryItem* found = LibraryItemAt(index);
    if (found == NULL)
    {
        return;
    }
    const HistoryLibraryItem item = *found;
    const bool cardImage = (item.m_cardImageIndex >= 0);
    const bool image = IsHistoryRowImageType(item.m_msgtype) || cardImage;
    int progress = 0;
    const int state = LibraryItemState(item, progress);

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return;
    }
    int count = 0;
    if (state == kHistoryLibFileLocal || image)
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuOpen, image ? TR(L"查看大图") : TR(L"打开"));
        ++count;
    }
    if (state == kHistoryLibFileLocal)
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuFolder, TR(L"打开所在文件夹"));
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuSaveAs, TR(L"另存为"));
        count += 2;
    }
    if (state == kHistoryLibFileRemote)
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuDownload, TR(L"下载"));
        ++count;
    }
    //帖子卡片图片的下载没有取消接口，不放「取消下载」
    if (state == kHistoryLibFileDownloading && !cardImage)
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuCancel, TR(L"取消下载"));
        ++count;
    }
    if (count > 0)
    {
        ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }
    ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuLocate,
                  item.m_isGroupFile ? TR(L"在群文件中查看") : TR(L"定位到聊天"));
    ++count;
    //转发只对聊天里的消息有意义（群文件没有对应的消息）；调试入口没有主面板，不放
    if (!item.m_isGroupFile && m_pMainPanel != NULL
        && CanForwardHistoryRow(item.m_msgtype, item.m_msgid))
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuForward, TR(L"转发"));
        ++count;
    }
    if (!image && !item.m_name.empty())
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuCopyName, TR(L"复制文件名"));
        ++count;
    }

    SIZE menuSize;
    menuSize.cx = kLibMenuEstWidth;
    menuSize.cy = (LONG)count * kLibMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(ptScreen, menuSize);
    ::SetForegroundWindow(m_hWnd);   //菜单要靠前台窗口才能在点击别处时正常收起
    const int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0, m_hWnd, NULL);
    ::DestroyMenu(menu);
    LOG_INFO("history library: menu on item %d, command %d.", index, cmd);
    switch (cmd)
    {
    //打开文件；图片为查看大图
    case kLibMenuOpen:
        OpenLibraryItem(index);
        break;
    //打开所在文件夹
    case kLibMenuFolder:
        OpenLibraryItemFolder(index);
        break;
    //下载、取消下载
    case kLibMenuDownload:
    case kLibMenuCancel:
        ToggleLibraryDownload(index);
        break;
    //另存为
    case kLibMenuSaveAs:
        SaveLibraryItemAs(index);
        break;
    //定位到聊天，或在群聊窗里查看这个群文件
    case kLibMenuLocate:
        LocateLibraryItem(index);
        break;
    //转发：交给主面板的转发目标选择窗
    case kLibMenuForward:
        ForwardRecord(RecordFromLibraryItem(item));
        break;
    //复制文件名
    case kLibMenuCopyName:
        CopyLibraryItemName(index);
        break;
    //菜单收起、什么都没选
    default:
        break;
    }
}

LRESULT CBalloonHistoryWindow::OnShowLibraryMenuMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    //坐标可能为负（副屏在主屏左侧或上方），按有符号还原
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    if (m_mgrTab != kHistoryManagerTabChat)
    {
        const int kind = m_libModel.Filter().m_kind;
        if (kind == kHistoryLibraryLinks || kind == kHistoryLibraryMentions)
        {
            ShowLibraryTextMenu((int)wParam, pt);
        }
        else
        {
            ShowLibraryMenu((int)wParam, pt);
        }
    }
    return 0;
}

void CBalloonHistoryWindow::ShowLibraryTextMenu(int index, POINT ptScreen)
{
    const HistoryLibraryItem* found = LibraryItemAt(index);
    if (found == NULL)
    {
        return;
    }
    const HistoryLibraryItem item = *found;
    const bool links = (m_libModel.Filter().m_kind == kHistoryLibraryLinks);
    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return;
    }
    int count = 0;
    if (links)
    {
        const std::vector<std::wstring> urls = LibraryLinksOfText(item.m_name);
        if (!urls.empty() && balloonchatwnd::IsSafeLinkUrl(urls[0]))
        {
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuOpenLink, TR(L"打开链接"));
            ++count;
        }
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuCopyText, TR(L"复制链接"));
    }
    else
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuCopyText, TR(L"复制"));
    }
    ++count;
    ::AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuLocate, TR(L"定位到聊天"));
    ++count;
    //转发要经主面板，调试入口没有主面板时不放
    if (m_pMainPanel != NULL && CanForwardHistoryRow(item.m_msgtype, item.m_msgid))
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kLibMenuForward, TR(L"转发"));
        ++count;
    }

    SIZE menuSize;
    menuSize.cx = kLibMenuEstWidth;
    menuSize.cy = (LONG)count * kLibMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(ptScreen, menuSize);
    ::SetForegroundWindow(m_hWnd);   //菜单要靠前台窗口才能在点击别处时正常收起
    const int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0, m_hWnd, NULL);
    ::DestroyMenu(menu);
    LOG_INFO("history library: text menu on item %d, command %d.", index, cmd);
    switch (cmd)
    {
    //链接页：打开链接
    case kLibMenuOpenLink:
        OpenLibraryLink(index);
        break;
    //复制链接或正文
    case kLibMenuCopyText:
        CopyLibraryItemText(index);
        break;
    //定位到聊天里那一条
    case kLibMenuLocate:
        LocateLibraryItem(index);
        break;
    //转发：交给主面板的转发目标选择窗
    case kLibMenuForward:
        ForwardRecord(RecordFromLibraryItem(item));
        break;
    //菜单收起、什么都没选
    default:
        break;
    }
}

// ====================================================================================
// 按落点找行：悬停与右键
// ====================================================================================

int CBalloonHistoryWindow::LibraryIndexAtPoint(POINT pt) const
{
    DuiControl* content = const_cast<CBalloonHistoryWindow*>(this)->GetClientContent();
    DuiScrollView* scroll = dynamic_cast<DuiScrollView*>(FindInLibrary(content, kIdLibScroll));
    if (scroll == NULL || !scroll->IsVisible())
    {
        return -1;
    }
    const RECT viewRc = scroll->GetRect();
    if (pt.x < viewRc.left || pt.x >= viewRc.right || pt.y < viewRc.top || pt.y >= viewRc.bottom)
    {
        return -1;
    }
    DuiControl* list = scroll->GetContent();
    if (list == NULL)
    {
        return -1;
    }
    const bool images = (m_libModel.Filter().m_kind == kHistoryLibraryImages);
    const UINT base = images ? (UINT)kIdLibGridBase : (UINT)kIdLibRowBase;
    const int count = (int)m_libModel.Items().size();
    for (int i = 0; i < count && i < kIdLibRowCapacity; ++i)
    {
        DuiControl* ctrl = list->FindCtrlById(base + (UINT)i);
        if (ctrl == NULL)
        {
            continue;
        }
        const RECT rc = ctrl->GetRect();
        if (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom)
        {
            return i;
        }
    }
    return -1;
}

void CBalloonHistoryWindow::UpdateLibraryRowHover(POINT pt)
{
    DuiControl* list = FindInLibrary(GetClientContent(), kIdLibList);
    if (list == NULL || m_libModel.Filter().m_kind == kHistoryLibraryImages)
    {
        return;
    }
    const int hit = (pt.x < 0 && pt.y < 0) ? -1 : LibraryIndexAtPoint(pt);
    const std::vector<std::unique_ptr<DuiControl> >& kids = list->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        HistoryRow* row = dynamic_cast<HistoryRow*>(kids[i].get());
        if (row != NULL)
        {
            row->SetHovered(row->GetCtrlId() == (UINT)(kIdLibRowBase + hit));
        }
    }
}
