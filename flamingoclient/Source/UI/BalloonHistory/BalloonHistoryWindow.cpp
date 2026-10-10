/**
 *  历史消息记录窗口（DuiFrameWindow 外壳）实现，BalloonHistoryWindow.cpp
 *
 *  组装客户区，接 HistoryModel + 本地库取数渲染列表；交互（类型胶囊 / 时间下拉 /
 *  搜索 / 翻页）经 OnDuiNotify 改模型后延迟局部刷新（只换列表 + 重填分页器 + 更新底部
 *  信息与胶囊态，不动搜索框）。调试入口用 in-memory 预置库灌假消息。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"
#include "BalloonHistoryWindow.h"

#include <atldlgs.h>   // CFileDialog（导出另存对话框）
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM（WM_MOUSEMOVE 取落点）

#include <memory>
#include <vector>
#include <string>

#include "../../App/CustomMsgDef.h"   // WM_CLOSE_MAINDLG
#include "../../Session/UserSessionData.h"   // FMG_MSG_OPEN_MERGED_VIEWER
#include "../../Common/Log/IULog.h"          // LOG_INFO / LOG_ERROR
#include "../BalloonChatWnd/ChatLinkDetect.h"   // IsSafeLinkUrl（打开链接前的协议白名单）
#include "../BalloonChatWnd/ChatCodeMessage.h"  // CodeLangFromContentJson：代码片段行画成代码卡片
#include "../BalloonCode/CodeViewerWindow.h"    // 点代码卡片打开代码查看窗
#include "../BalloonMarkdown/MarkdownEditorManager.h"   // 点 Markdown 卡片打开预览窗口
#include "../BalloonMarkdown/MarkdownEditorModel.h"     // MarkdownPreviewMeta：预览窗口的「发送者 · 时间」

#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"     // 客户区顶部的轻提示条
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox（提示条挂在客户区根容器上）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

#include "HistoryView.h"       // PreviewFromContent / FullTextFromContent / TimeRangeFromComboIndex
#include "HistoryDateRange.h"  // 「选日期」弹层的日期检查、区间标签文字与预置日期
#include "HistoryRow.h"        // 列表行（悬停态由本窗按落点驱动）
#include "HistoryRowFormat.h"  // FormatTimeShort / LocalDayKey / ColorForId（与合并记录查看窗共用）
#include "../BalloonChat/BalloonChatMsgFormat.h"   // ParseSendBody（正文拆成文字段 + 表情段）
#include "../BalloonChatWnd/ChatInlineText.h"      // BuildFlatText（关键字要在扁平文本上找）
#include "../../net/Protocol.h"   // message_type_file（文件行的正文沿用"名字 · 大小"那份）
#include "../BalloonMain/BalloonMainPanel.h"   // 真实会话下解析发送者昵称
#include "../../net/IUSocket.h"        // 行内 Pin 按钮直接发 cmd_pin_message
#include "../../net/ImageTransfer.h"
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）   // CacheFilePathForMd5（算缩略图的本地缓存路径）
#include "../../App/FlamingoClient.h"      // 取当前登录用户 userid（缓存按账号隔离）
#include "../BalloonMain/TitleBarIcon.h"       // CreateProgramIconBitmap（标题栏左侧图标）
#include "../BalloonMain/PopupMenuPlacement.h" // ClampPopupMenuToWorkArea（菜单不越屏）
#include "../BalloonMain/OverlayScrollBarGeometry.h" // ComputeResizeBorderInsetPx（列表滚动条给缩放边框让位）
#include "../BalloonMain/MainPanelScrollView.h"     // 列表滚动视口；WM_TIMER 推进滚动条淡入淡出
#include "../../Common/Util/PerfStopWatch.h"        // 刷新列表的分段计时（记进日志）
#include "HistoryWindowKeys.h"                       // DecideHistoryKey（键盘快捷键判定）
#include "../BalloonChatWnd/ImagePlaceholder.h"     // ResolveHistoryThumb（图片缩略图该显示哪种占位）
#include "../BalloonChatWnd/ChatImageCache.h"       // 登记为解码完成通知的接收窗口
#include "../BalloonChatWnd/ChatClipboardContent.h" // BuildClipSegmentsFromBody（整条正文拆成剪贴板片段）
#include "../BalloonChatWnd/ChatClipboardIo.h"      // WriteChatContentToClipboard（按聊天内容格式写剪贴板）
#include "HistoryContentView.h"                     // 正文的文字选区（拖选复制）
#include "HistoryRowMenu.h"                    // 行右键菜单的纯模型
#include "HistoryExport.h"                     // 导出网页的生成与文件名处理
#include "HistoryImageGrid.h"                  // PickFilesToDownload：「文件」分类的「全部下载」
#include <commctrl.h>                          // TaskDialogIndirect（导出前问是否先加载全部）
#include <set>                                 // 导出图片的文件名去重
#include "../BalloonMain/ForwardRequest.h"     // balloonmain::ForwardRequest（行右键「转发」投给主面板）
#include "../BalloonChatWnd/ChatFileLocation.h"  // ResolveLocalFilePath（本地落点优先级）
#include "../BalloonChatWnd/ChatImageSaveAs.h"   // SaveChatImageAs（图片另存为）
#include "../BalloonChatWnd/ChatMessageMenu.h"   // MakeImageSaveAsDefaultName
#include "../BalloonImageViewer/BalloonImageViewerWindow.h"  // 看图窗（双击图片放大）
#include "FileLocalStore.h"                    // 文件本地落点登记库
#include "FolderLocalStore.h"                  // 文件夹本地落点登记库（按会话 id 记根目录）
#include "HistoryLocalFile.h"                  // ResolveRecordLocalPath：按记录找本机文件或文件夹
#include "../../net/FileTransfer.h"               // 文件下载 / 取消
#include "../../net/UserCachePath.h"              // UserCacheDir：取当前账号的语音缓存目录 ChatVoice
#include "../BalloonChatWnd/ChatVoiceBubble.h"    // DecideVoiceClick 等：语音条的点击判定与提示文字
#include "../BalloonChatWnd/ChatReceivedVoice.h"  // BuildStoredVoiceMessage：语音记录做成语音条
#include "../BalloonChatWnd/ChatVoiceStrip.h"     // ChatVoiceStrip：就地更新播放与下载状态
#include "../../Common/Util/ClipboardText.h"                  // CopyTextToClipboard（复制正文）
#include "../../Common/Util/ClipboardImage.h"                 // CopyImageFileToClipboard（复制图片）
#include <shellapi.h>                          // ShellExecute（打开文件 / 定位文件夹）
#include <shobjidl.h>                          // IFileOpenDialog：多个会话导出时选择文件夹
#include "../BalloonMain/MainPanelTreeView.h"  // 聊天记录管理器左栏的会话列表
#include "V2MsgSearchWorker.h"                 // 左栏跨会话搜索放到后台线程执行
#include "HistoryWildcard.h"                   // FindSearchHighlightRanges：勾选「通配符」时按通配符高亮
#include "HistorySearchSyntax.h"               // ParseHistorySearch / MatchSearchFromIds：搜索语法里的 from:名字
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"   // 左右分栏
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框过滤串的说明文字逐段翻译

using namespace balloonwjui;
using namespace balloonhistory;

// ---- 文件作用域常量 ----

//窗口初始客户区尺寸（逻辑像素），与设计稿比例接近。
// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修复 ResizeClient 多叠加系统边框的问题时由 880×690 加上
// 修复前多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小。
static const int kHisWindowWidth  = 898;
static const int kHisWindowHeight = 737;

//窗口最小尺寸（逻辑像素）。
//「跳到这一天」等往前拉取时，往前拉取最多重新开始几次（每次最多 kMaxFetchOlderRounds 轮、每轮
//约 100 条，合计约一万条）。日期选得过早时到这个数就放弃，不再无休止地向服务端要数据
static const int kMaxPendingJumpRestarts = 20;

static const int kHisMinWidth  = 640;
static const int kHisMinHeight = 460;

//标题栏高度（逻辑像素）—— 纤细透明，仅承载关闭按钮。
static const int kHisTitleBarHeight = 32;

//标题栏文字 / 按钮 glyph 颜色（深灰，配白色透明标题栏）。
static const COLORREF kHisCaptionColor = RGB(0x55, 0x55, 0x55);

//标题栏左侧图标边长（逻辑像素）。标题栏按 16x16 绘制，取同值免去缩放。
static const int kHisTitleIconSize = 16;

//标题栏实际显示的底色：本窗标题栏透明，穿透显示的是根控件背景（白）。图标须
//合成到这个色上，否则其透明区在 SRCCOPY 搬运后会变成黑块。
static const COLORREF kHisTitleBarBg = RGB(0xFF, 0xFF, 0xFF);

//每页条数：取 HistoryWindowLayout.h 的 kHistoryPageSize（行内按钮 id 段的容量检查也在那里）。
static const int kHisPageSize = kHistoryPageSize;

//搜索框停止输入多久之后才真正查询（毫秒）。原先每输入一个字就查一次，用输入法连续上屏时会
//连查好几次；停顿 250 毫秒再查，打字过程中不查，停下后的等待又短到察觉不到。回车立即查询。
static const UINT kSearchDebounceMs = 250;

//搜索延迟合并用的定时器 id，紧挨往前拉取的超时定时器（0x4801），便于排查
static const UINT_PTR kSearchDebounceTimerId = 0x4802;

//行右键菜单尺寸的估算值（像素）。Win32 的 HMENU 在弹出前拿不到精确尺寸，故按行数估算
//交给夹取函数 —— 估大一点只会让靠近屏幕边缘时更早翻向左 / 上，不会误伤（与 ChatBubble
//弹菜单时的做法一致）。菜单最长的一项是「打开所在文件夹」，6 个汉字。
static const int kRowMenuEstWidth      = 160;
static const int kRowMenuEstItemHeight = 26;

// ---- 聊天记录管理器左栏（docs/聊天记录管理器方案.md） ----

//窗口标题（任务栏、Alt+Tab 里显示的名字）。会话名显示在右侧标题区
static const TCHAR* const kHisManagerTitle = TR_NOOP(L"聊天记录管理");
//左栏刷新的合并延迟（毫秒）：主面板短时间内连发多次"会话有变化"（一批消息、一次会话列表同步）时
//只重建一次左栏
static const UINT kMgrRebuildDelayMs = 400;
static const UINT_PTR kMgrRebuildTimerId = 0x4803;
//切换会话后等多久才向服务端补拉（毫秒）：快速连点左栏时只给最后停下的会话发请求
static const UINT kMgrBackfillDelayMs = 300;
static const UINT_PTR kMgrBackfillTimerId = 0x4804;
//左栏搜索框停止输入多久之后才查询（毫秒），与右侧搜索框相同
static const UINT kMgrSearchDelayMs = 250;
static const UINT_PTR kMgrSearchTimerId = 0x4805;
//左栏搜索「聊天记录」一段最多列出几个会话
static const int kMgrHitLimit = 200;
//左栏会话行右键菜单的估算宽度（像素）与菜单项：打开聊天窗口 / 导出聊天记录 / 清空聊天记录
static const int kMgrMenuEstWidth = 150;
enum ManagerMenuCmd
{
    kMgrMenuOpenChat = 1,   //打开聊天窗口：交给主面板打开单聊窗或群聊窗
    kMgrMenuExport   = 2,   //导出聊天记录：先切到这个会话，再走右侧「导出」的流程
    kMgrMenuClear    = 3    //清空聊天记录：先切到这个会话，再走右侧「清空聊天记录」的流程
};

namespace {

//取当前 Unix 毫秒时间（供“近N天”换算与种子时间戳）。
int64_t NowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    //FILETIME 为自 1601 起的 100ns 数；减去到 1970 的偏移后换算成毫秒。
    const unsigned long long kEpochDiff100ns = 116444736000000000ULL;
    unsigned long long ms = (u.QuadPart - kEpochDiff100ns) / 10000ULL;
    return (int64_t)ms;
}

//把 Unix 毫秒格式化成“MM-DD HH:MM”（本地时区）。
CString FormatTime(int64_t timestampMs)
{
    __time64_t t = (__time64_t)(timestampMs / 1000);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return CString();
    }
    TCHAR buf[32];
    _stprintf_s(buf, _countof(buf), _T("%02d-%02d %02d:%02d"),
                tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return CString(buf);
}

//UTF-8 std::string → CString（UNICODE 下即 CStringW）。
CString Utf8ToCString(const std::string& s)
{
    if (s.empty())
    {
        return CString();
    }
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
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

//CString（宽）→ UTF-8 std::string（供搜索关键字传给本地库）。
std::string CStringToUtf8(const CString& s)
{
    if (s.IsEmpty())
    {
        return std::string();
    }
    int n = ::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out;
    out.resize((size_t)n);
    ::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), &out[0], n, NULL, NULL);
    return out;
}

//某个路径在磁盘上是否真的存在（且不是目录）。
//本地文件"在不在"要实测磁盘 —— 用户随时会把文件移走、改名、删掉，登记表里有记录
//不代表文件还在。
/**
 *  看帖子卡片图片时交给看图窗口的翻页回调（进程内唯一，docs/帖子群方案.md 14.5）。看图窗口只保存回调对象的指针，而
 *  消息记录窗口可能先于看图窗口关闭，所以回调对象常驻、只记下发起看图的消息记录窗口句柄：翻页时按句柄同步转给那个
 *  窗口（WM_HISTORY_CARD_NAVIGATE），窗口已关闭时什么也不做。
 */
class HistoryCardNavigator : public IImageViewerNavigator
{
public:
    HistoryCardNavigator()
        : m_target(NULL)
    {
    }

    //登记发起看图的消息记录窗口
    void SetTarget(HWND target)
    {
        m_target = target;
    }

    virtual void OnImageViewerNavigate(int delta) override
    {
        if (m_target != NULL && ::IsWindow(m_target))
        {
            ::SendMessage(m_target, WM_HISTORY_CARD_NAVIGATE, (WPARAM)(INT_PTR)delta, 0);
        }
    }

private:
    HWND m_target;   //发起看图的消息记录窗口（不拥有）；NULL 表示没有
};

//进程内唯一的翻页回调对象
HistoryCardNavigator g_historyCardNavigator;

bool PathExistsOnDisk(LPCWSTR path)
{
    if (path == NULL || path[0] == 0)
    {
        return false;
    }
    const DWORD attr = ::GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

//时间范围 → 下拉项下标（与 BuildHistoryWindowBody 里下拉项顺序一致）。
//两者是一一对应的，见 HistoryTimeRange 的注释；这里只做一次范围检查兜底。
//自定义起止（「只看这段时间」）不在下拉里，生效期间下拉隐藏；此时返回「全部时间」，
//这样清掉日期筛选、下拉重新显示时停在「全部时间」上，与模型一致。
int ComboIndexFromRange(HistoryTimeRange range)
{
    if (range >= kHistoryTimeAll && range <= kHistoryTime1Year)
    {
        return (int)range;
    }
    return (int)kHistoryTimeAll;
}

//清空容器的全部子控件（先复制指针清单再逐个 RemoveChild，避免遍历中容器变化）。
void RemoveAllChildren(DuiControl* box)
{
    if (box == NULL)
    {
        return;
    }
    std::vector<DuiControl*> kids;
    for (const auto& child : box->Children())
    {
        kids.push_back(child.get());
    }
    for (size_t i = 0; i < kids.size(); ++i)
    {
        box->RemoveChild(kids[i]);
    }
}

//按筛选条件算日期区间标签的文字：时间范围为自定义（「只看这段时间」）时是起止日期，
//如「9月1日 至 9月30日」；其余时间范围返回空串，表示不显示日期区间标签。
//  filter：当前筛选条件；nowMs：当前时间（Unix 毫秒），决定标签里要不要省略年份。
std::wstring DateRangeLabelOf(const HistoryFilter& filter, int64_t nowMs)
{
    if (filter.m_range != kHistoryTimeCustom)
    {
        return std::wstring();
    }
    HistoryDate from;
    HistoryDate to;
    HistoryDate today;
    if (!HistoryDateOf(filter.m_customMinMs, from) || !HistoryDateOf(filter.m_customMaxMs, to)
        || !HistoryDateOf(nowMs, today))
    {
        return std::wstring();
    }
    return FormatHistoryDateRangeLabel(from, to, today.m_year);
}

//一条记录是不是在线直传的文件（本地库 file 子对象带 direct，2026-10-05 起写入）；是的话顺带按 ResolveRecordLocalPath
//查好本机可用的那份文件，本机没有时 outLocalPath 为空。转发时据此决定重新发送还是提示无法转发
bool IsDirectFileRecord(const HistoryRecord& rec, std::wstring& outLocalPath)
{
    outLocalPath.clear();
    if (rec.m_msgtype != ::message_type_file || !ParseContentBrief(rec.m_msgtype, rec.m_content).m_fileDirect)
    {
        return false;
    }
    ResolveRecordLocalPath(rec, outLocalPath);
    return true;
}

} // anonymous namespace

CBalloonHistoryWindow::CBalloonHistoryWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hasNormalRect(false)
    , m_locateMsgid(0)
    , m_scrollToLocated(false)
    , m_pendingJumpStartMs(0)
    , m_pendingJumpEndMs(0)
    , m_pendingJumpRestarts(0)
    , m_multiSelect(false)
    , m_selection(net::kForwardMaxMessages)
    , m_pendingExportRestarts(0)
    , m_exportCancel(false)
    , m_exportRunning(false)
    , m_hMainDlg(NULL)
    , m_pMainPanel(NULL)
    , m_pScroll(NULL)
    , m_pPager(NULL)
    , m_pFooterInfo(NULL)
    , m_pToast(NULL)
    , m_pendingViewMsgid(0)   //没有"下载完自动看图"的待办
    , m_nextCardRequest(1)
    , m_cardRefreshPosted(false)
    , m_cardViewIndex(0)
    , m_lastPressInList(false)
    , m_menuCopySelection(false)
    , m_hTitleIcon(NULL)
    , m_pStore(NULL)
    , m_allSendersLoaded(false)
    , m_fetchingOlder(false)
    , m_fetchRounds(0)
    , m_fetchCountBefore(0)
    , m_fetchAnchor(0)
    , m_mgrSearchSeq(0)
    , m_mgrTypeFilter(kManagerTypeAll)
    , m_mgrCollapsed(false)
    , m_mgrPaneWidth(kMgrPaneDefaultW)
    , m_pendingBackfillCtype(0)
    , m_pendingBackfillCid(0)
    , m_pMgrScroll(NULL)
    , m_mgrRebuildPending(false)
    , m_mgrWildcard(false)
    , m_mgrTab(kHistoryManagerTabChat)
    , m_libAllNode(-1)
    , m_mentionsNode(-1)
    , m_storageMode(false)
    , m_storageSeq(0)
    , m_storageCleaning(false)
    , m_storageValid(false)
    , m_libRefreshPending(false)
    , m_timelineMode(false)
    , m_navCurrentMonthKey(0)
    , m_timelineStatsValid(false)
    , m_timelineStatsSeq(0)
    , m_timelineJumpPending(false)
{
    ::SetRectEmpty(&m_lastNormalRect);
    //语音条（第 4b 步）：开窗时没有在播放
    m_playingVoiceMsgid = 0;
}

CBalloonHistoryWindow::~CBalloonHistoryWindow()
{
    //后台导出线程（窗口没走 WM_DESTROY 就析构时）：让它中止并等它结束
    StopExportThread();
    //客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底；m_seedStore 自行析构关库。
    //标题栏图标位图归本类所有（DuiFrameWindow 只借用不释放），这里收回。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

void CBalloonHistoryWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;
    SeedDebugStore();
    if (!BuildWindow(NULL))
    {
        return;
    }
    LOG_INFO(_T("历史消息记录窗口已创建并显示。"));
}

void CBalloonHistoryWindow::OpenForConversation(CBalloonMainPanel* pMainPanel,
                                                CV2MsgStore* pStore,
                                                int ctype, int64_t cid, LPCTSTR titleName,
                                                int initialTypeFilter)
{
    //真实打开：关闭只销毁本窗、不退出程序（与调试入口区别在 m_hMainDlg=NULL）。
    m_hMainDlg = NULL;
    m_pMainPanel = pMainPanel;
    m_titleName = (titleName != NULL) ? titleName : _T("");
    //重新打开或换会话：上一次留下的定位高亮与多选都不再有意义
    ClearLocatedRow();
    ResetMultiSelect();
    //换了会话，还在等往前拉取的导出也作废，否则拉完写进文件的会是另一个会话的消息
    m_pendingExportPath.Empty();
    m_model.SetStore(pStore);
    m_pStore = pStore;

    //重新打开、或开着时换到别的会话：关键字与时间范围恢复为初始状态。
    //本窗口对象随主面板一直存在，关窗后模型里的筛选条件仍然保留，而新建的搜索框是空的；
    //不重置就会出现搜索框为空、列表却仍按上次关键字筛选的情况。
    //须在 SetConversation 之前判断，之后模型里已经是新会话了。
    const bool windowOpen = (IsWindow() != FALSE);
    const bool sameConversation = (m_model.Filter().m_ctype == ctype && m_model.Filter().m_cid == cid);
    const bool resetSearch = ShouldResetHistorySearch(windowOpen, sameConversation);
    LOG_INFO("history window: open ctype=%d, cid=%lld, window open %d, same conversation %d, reset search %d.",
             ctype, (long long)cid, (int)windowOpen, (int)sameConversation, (int)resetSearch);
    //开着时换到别的会话：上一个会话「全部下载」的批次作废，免得它的汇总提示出现在新会话里。
    //关窗时已在 OnDestroyMsg 里作废过，这里只需处理窗口开着的情形
    if (windowOpen && !sameConversation)
    {
        AbandonBatchDownload("conversation switched");
    }

    //换会话（或重新打开）：上一个会话在途的往前拉取与本会话无关，放弃
    StopFetchingOlder();
    m_model.SetConversation(ctype, cid);
    m_model.SetPageSize(kHisPageSize);
    if (resetSearch)
    {
        m_model.ResetSearchFilters();
    }
    //「通配符」复选框只在窗口打开期间保持：新打开时两处都恢复为不勾（docs/聊天记录管理器方案.md §11.4）；
    //文件 / 图片页的筛选条件同样恢复为初始状态。开着时从「消息记录」入口打开某个会话：停在文件 / 图片页的话切回聊天记录页
    if (!windowOpen)
    {
        m_model.SetWildcard(false);
        m_mgrWildcard = false;
        m_mgrTab = kHistoryManagerTabChat;
        m_libModel.ResetFilters();
        m_libGroupDownloads.clear();
        //时间轴模式同样只在窗口打开期间保持，新打开时回到列表模式（docs/聊天记录管理器方案.md §11.7）
        ResetTimelineState();
        //跨会话 from: 的候选人在新打开后第一次用到时重新读出
        m_allSendersLoaded = false;
        m_allSenders.clear();
        //本机存储占用页同样只在窗口打开期间保持，新打开时重新统计
        m_storageMode = false;
        m_storageValid = false;
        m_storageSeq = 0;
        m_storageCleaning = false;
    }
    else
    {
        EnsureChatTab();
    }

    //预选类型胶囊。取值来自窗口消息的一个字节，可能是任何数，越界就退回"全部"，
    //免得写进模型后查询条件对不上、列表莫名其妙空掉。
    if (initialTypeFilter >= kHistoryTypeAll && initialTypeFilter <= kHistoryTypePinned)
    {
        m_model.SetTypeFilter((HistoryTypeFilter)initialTypeFilter);
    }
    else
    {
        m_model.SetTypeFilter(kHistoryTypeAll);
    }

    if (IsWindow())
    {
        //已开：重定向到新会话，刷新并置顶。会话名显示在右侧标题区，窗口标题固定为「聊天记录管理」
        ApplyHistoryHeaderTitle(BodyRoot(), m_titleName, m_mgrCollapsed);
        //局部刷新不重建搜索框与时间下拉，模型重置了，这几个控件要另行恢复
        if (resetSearch)
        {
            ResetSearchControls();
        }
        //换了会话发送人名单就不同；同一会话重新点开也顺带按本地库更新一次
        ApplyCoverageBound();
        RefreshSenderCombo();
        RefreshContent();
        //左栏选中这个会话；它可能还不在左栏里（刚有第一条消息），一并安排一次左栏刷新
        SyncManagerSelection(true);
        NotifyConversationsChanged();
        //最小化的窗口只有 SW_RESTORE 能还原，SW_SHOW 对它无效：窗口虽被激活，却仍停留在
        //任务栏上，表现为再次点击聊天窗的「消息记录」按钮没有任何反应。做法与主面板
        //CBalloonMainPanel 从托盘还原自身时一致。
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        else
        {
            ShowWindow(SW_SHOW);
        }
        ::SetForegroundWindow(m_hWnd);
        return;
    }
    BuildWindow(NULL);
    LOG_INFO(_T("历史消息记录窗口已为会话打开。"));
}

bool CBalloonHistoryWindow::IsShowingConversation(int ctype, int64_t cid) const
{
    if (!::IsWindow(m_hWnd))
    {
        return false;
    }
    const HistoryFilter& filter = m_model.Filter();
    return filter.m_ctype == ctype && filter.m_cid == cid;
}

void CBalloonHistoryWindow::RefreshIfConversation(int ctype, int64_t cid)
{
    if (!IsWindow())
    {
        return;
    }
    const HistoryFilter& filter = m_model.Filter();
    if (filter.m_ctype == ctype && filter.m_cid == cid)
    {
        RefreshContent();
    }
    //本地库变了：左栏里这个会话的最后一条、甚至它在不在左栏里都可能变了
    NotifyConversationsChanged();
}

void CBalloonHistoryWindow::RefreshIfOpen()
{
    if (!IsWindow())
    {
        return;
    }
    RefreshContent();
    NotifyConversationsChanged();
}

void CBalloonHistoryWindow::OpenForConversationWithSearch(CBalloonMainPanel* pMainPanel, CV2MsgStore* pStore,
                                                          int ctype, int64_t cid, LPCTSTR titleName,
                                                          const std::string& keywordUtf8, int typeFilter,
                                                          int64_t locateMsgid)
{
    //先按普通方式打开（换会话时搜索条件恢复为初始状态，类型胶囊按 typeFilter 预选），再套用关键字与定位
    OpenForConversation(pMainPanel, pStore, ctype, cid, titleName, typeFilter);
    if (!IsWindow())
    {
        return;
    }
    if (!keywordUtf8.empty())
    {
        //主面板搜索下拉的关键字按普通字符匹配：右侧的「通配符」随之取消勾选，否则关键字里的 * ? 会被
        //当成通配符，搜出来的与下拉里看到的对不上
        SyncRightWildcard(false);
        //写进模型并同步右侧搜索框（不发通知，免得再排一次相同的查询）
        m_model.SetKeyword(keywordUtf8);
        SyncSyntaxFromIds();
        DuiControl* content = GetClientContent();
        DuiEdit* search = (content != NULL)
                        ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdHistorySearch)) : NULL;
        if (search != NULL)
        {
            search->SetTextNoNotify(Utf8ToCString(keywordUtf8));
        }
    }
    //按新的筛选条件统计并取第一页，下面算定位页号要用最新的总数
    RefreshContent();

    bool located = false;
    if (locateMsgid > 0 && m_pStore != NULL)
    {
        HistoryRecord rec;
        bool found = false;
        if (m_pStore->QueryRecord(ctype, cid, locateMsgid, rec, found) && found && !rec.m_recalled)
        {
            const int page = m_model.PageOfPosition(NowMs(), rec.m_timestamp, rec.m_msgid);
            if (page > 0)
            {
                m_model.GoToPage(page);
                m_locateMsgid = rec.m_msgid;
                m_scrollToLocated = true;
                located = true;
                //只换页：筛选条件没变，沿用刚统计出的总数
                RefreshContent(true);
            }
        }
    }
    LOG_INFO("history window: opened with search, ctype=%d, cid=%lld, keyword bytes=%u, type %d, "
             "locate msgid %lld, located %d, page %d/%d.",
             ctype, (long long)cid, (unsigned)keywordUtf8.size(), typeFilter, (long long)locateMsgid,
             (int)located, m_model.CurrentPage(), m_model.PageCount());
}

void CBalloonHistoryWindow::OpenManager(CBalloonMainPanel* pMainPanel, CV2MsgStore* pStore)
{
    if (IsWindow())
    {
        //已开：只还原并置前，不换会话（最小化的窗口要 SW_RESTORE 才能还原，理由见 OpenForConversation）
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        else
        {
            ShowWindow(SW_SHOW);
        }
        ::SetForegroundWindow(m_hWnd);
        return;
    }

    m_hMainDlg = NULL;
    m_pMainPanel = pMainPanel;
    m_model.SetStore(pStore);
    m_pStore = pStore;

    //先按当前数据合并一份左栏，决定打开哪个会话：上次查看的会话还在就选它，否则选第一个
    RebuildManagerItems();
    const HistoryFilter& last = m_model.Filter();
    const int index = ResolveManagerSelection(m_mgrItems, last.m_ctype, last.m_cid, -1);
    int ctype = 1;
    int64_t cid = 0;
    CString name;
    if (index >= 0)
    {
        ctype = m_mgrItems[(size_t)index].m_ctype;
        cid = m_mgrItems[(size_t)index].m_cid;
        name = m_mgrItems[(size_t)index].m_name.c_str();
    }
    LOG_INFO("history manager: open from menu, %u conversations, select ctype=%d, cid=%lld.",
             (unsigned)m_mgrItems.size(), ctype, (long long)cid);

    //与 OpenForConversation 新开窗口时一样：撤掉上一次留下的状态，搜索条件恢复为初始状态
    ClearLocatedRow();
    ResetMultiSelect();
    m_pendingExportPath.Empty();
    StopFetchingOlder();
    m_titleName = name;
    m_model.SetConversation(ctype, cid);
    m_model.SetPageSize(kHisPageSize);
    m_model.ResetSearchFilters();
    m_model.SetTypeFilter(kHistoryTypeAll);
    //新打开：两处「通配符」复选框都恢复为不勾，停在聊天记录页，文件 / 图片页的筛选条件恢复为初始状态
    m_model.SetWildcard(false);
    m_mgrWildcard = false;
    m_mgrTab = kHistoryManagerTabChat;
    m_libModel.ResetFilters();
    m_libGroupDownloads.clear();
    ResetTimelineState();
    m_allSendersLoaded = false;
    m_allSenders.clear();
    m_storageMode = false;
    m_storageValid = false;
    m_storageSeq = 0;
    m_storageCleaning = false;
    BuildWindow(NULL);
    if (cid != 0)
    {
        ScheduleBackfill(ctype, cid);
    }
}

void CBalloonHistoryWindow::OpenForConversationAtTime(CBalloonMainPanel* pMainPanel, CV2MsgStore* pStore,
                                                      int ctype, int64_t cid, LPCTSTR titleName,
                                                      const std::string& keywordUtf8, int64_t timeMs)
{
    OpenForConversationWithSearch(pMainPanel, pStore, ctype, cid, titleName, keywordUtf8, kHistoryTypeAll, 0);
    if (!IsWindow() || timeMs <= 0)
    {
        return;
    }
    //那天的起点：按本地时区取年月日，再换算回那天 0 点。终点取命中的时刻本身，跳过去的是
    //那天到这一刻为止最晚的一条命中，也就是服务器报告的那一条
    const int64_t kMsPerSecond = 1000;
    const int kTmYearBase = 1900;
    __time64_t t = (__time64_t)(timeMs / kMsPerSecond);
    struct tm tmv;
    int64_t dayStartMs = 0;
    int64_t dayEndMs = 0;
    if (_localtime64_s(&tmv, &t) != 0
        || !LocalDayBoundsMs(tmv.tm_year + kTmYearBase, tmv.tm_mon + 1, tmv.tm_mday, dayStartMs, dayEndMs))
    {
        LOG_WARNING("history window: cannot compute the day of %lld, open without jumping.", (long long)timeMs);
        return;
    }
    LOG_INFO("history window: open at time %lld, ctype=%d, cid=%lld.", (long long)timeMs, ctype, (long long)cid);
    ClearLocatedRow();
    ApplyDateJump(dayStartMs, timeMs, false);
    //ApplyDateJump 已按筛选条件统计并取好了页，只换页的刷新不会再统计一次（同「跳到这一天」）
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

void CBalloonHistoryWindow::OpenManagerWithSearch(CBalloonMainPanel* pMainPanel, CV2MsgStore* pStore,
                                                  const std::wstring& query)
{
    OpenManager(pMainPanel, pStore);
    if (!IsWindow() || query.empty())
    {
        return;
    }
    //跨会话搜索聊天内容只在聊天记录页有
    EnsureChatTab();
    //左栏折叠着时搜索框看不见，先展开
    if (m_mgrCollapsed)
    {
        SetManagerCollapsed(false);
    }
    //窗口原本就开着、限定了搜索范围时：恢复为全部会话，与主面板搜索的范围一致
    if (!m_mgrScope.empty())
    {
        ApplyManagerScope(std::vector<HistoryConversationKey>());
    }
    DuiControl* content = GetClientContent();
    DuiEdit* search = (content != NULL) ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdMgrSearch)) : NULL;
    if (search == NULL)
    {
        return;
    }
    LOG_INFO("history manager: open with search, chars %u.", (unsigned)query.size());
    //主面板搜索下拉的关键字按普通字符匹配：左栏的「通配符」随之取消勾选。原先勾着时即使搜索词
    //没变也要重搜一次，把记下的搜索词清掉，ApplyManagerSearch 就不会因为「词没变」直接返回
    if (SyncManagerWildcard(false))
    {
        m_mgrQuery.clear();
    }
    //填入后立即搜索，不等搜索框文字变化触发的延迟计时（ApplyManagerSearch 会停掉那个定时器）
    search->SetText(query.c_str());
    ApplyManagerSearch();
}

namespace {

//导出前"还有更早的消息没加载"时用户的选择
enum HistoryExportChoice
{
    kHistoryExportLoadAll    = 0,   //先从服务端加载全部，再导出
    kHistoryExportLoadedOnly = 1,   //只导出已经加载到本地的
    kHistoryExportCancel     = 2    //不导出
};

//TaskDialog 自定义按钮的编号，避开 IDOK / IDCANCEL 这类系统按钮编号
const int kExportBtnLoadAll    = 1001;
const int kExportBtnLoadedOnly = 1002;

//判断图片格式要读的文件头字节数：WebP 要看到第 12 个字节
const DWORD kImageHeaderBytes = 16;

//导出用的完整时间文字「YYYY-MM-DD HH:MM」（本地时区）。列表里显示的时间省掉了年份，导出文件
//脱离了窗口，要带上年份才看得懂
std::wstring FormatExportTime(int64_t timestampMs)
{
    const int64_t kMsPerSecond = 1000;
    const int kTmYearBase = 1900;
    __time64_t t = (__time64_t)(timestampMs / kMsPerSecond);
    struct tm tmv;
    if (_localtime64_s(&tmv, &t) != 0)
    {
        return std::wstring();
    }
    wchar_t buf[32] = { 0 };
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d", tmv.tm_year + kTmYearBase, tmv.tm_mon + 1,
               tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return buf;
}

//读文件开头的几个字节，判断图片格式并返回扩展名（见 ImageExtensionFromHeader）
std::wstring ImageExtensionOfFile(const CString& path)
{
    unsigned char head[kImageHeaderBytes] = { 0 };
    DWORD got = 0;
    HANDLE file = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        ::ReadFile(file, head, kImageHeaderBytes, &got, NULL);
        ::CloseHandle(file);
    }
    return ImageExtensionFromHeader(head, got);
}

//弹「选择文件夹」对话框（Vista 起的 IFileOpenDialog，FOS_PICKFOLDERS）。多个会话导出到文件夹时用
//  owner：对话框的所有者窗口；title：对话框标题；outDir：[出参] 选定的文件夹完整路径
//@return 用户选定时返回 true；取消或对话框创建失败返回 false
bool PickExportFolder(HWND owner, LPCTSTR title, std::wstring& outDir)
{
    outDir.clear();
    IFileOpenDialog* dlg = NULL;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))
        || dlg == NULL)
    {
        LOG_ERROR("history export: create folder picker failed.");
        return false;
    }
    DWORD options = 0;
    dlg->GetOptions(&options);
    dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title);
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner)))
    {
        IShellItem* item = NULL;
        if (SUCCEEDED(dlg->GetResult(&item)) && item != NULL)
        {
            PWSTR path = NULL;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != NULL)
            {
                outDir = path;
                ::CoTaskMemFree(path);
                ok = !outDir.empty();
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

} // namespace

void CBalloonHistoryWindow::ExportHistory()
{
    //2026-10-06 起「导出」先弹导出选项浮层（docs/聊天记录管理器方案.md §12.7），选定后由 BeginExport 继续
    OpenExportPopup();
}

void CBalloonHistoryWindow::BeginExport(const HistoryExportOptions& opts)
{
    if (m_exportRunning)
    {
        ShowToast(TR(L"正在导出，请稍候"));
        return;
    }
    m_lastExportOptions = opts;
    const bool current = (opts.m_scope == kHistoryExportScopeCurrent);
    //只有当前会话才问「还有更早的消息没加载」：多个会话一律只导出本机已保存的
    bool loadAll = false;
    if (current && ShouldPromptUnloadedBeforeExport(HasUnloadedOlderNow()))
    {
        const int choice = AskUnloadedBeforeExport();
        LOG_INFO("history window: export with unloaded older messages, choice %d.", choice);
        if (choice == kHistoryExportCancel)
        {
            return;
        }
        loadAll = (choice == kHistoryExportLoadAll);
    }

    const size_t convCount = current ? 1 : opts.m_conversations.size();
    std::wstring target;
    if (!AskExportTarget(opts, convCount, target))
    {
        return;
    }

    //先加载全部：记下导出目标与选项，开始往前拉取；拉到最早一条后由 OnExportReadyMsg 导出
    if (loadAll && m_pStore != NULL && m_pMainPanel != NULL)
    {
        m_pendingExportPath = target.c_str();
        m_pendingExportOptions = opts;
        m_pendingExportRestarts = 0;
        ShowToast(TR(L"正在加载全部消息，加载完成后自动导出"));
        StartFetchAllForExport();
        return;
    }
    StartExportJob(opts, target);
}

int CBalloonHistoryWindow::AskUnloadedBeforeExport()
{
    TASKDIALOG_BUTTON buttons[2];
    buttons[0].nButtonID = kExportBtnLoadAll;
    buttons[0].pszButtonText = TR(L"先加载全部再导出");
    buttons[1].nButtonID = kExportBtnLoadedOnly;
    buttons[1].pszButtonText = TR(L"只导出已加载的");

    CString content;
    content.Format(TR(L"当前只加载了 %lld 条，更早的消息还在服务器上。只导出已加载的，导出文件里就没有更早的消息。"),
                   (long long)m_model.Total());

    TASKDIALOGCONFIG cfg;
    ::ZeroMemory(&cfg, sizeof(cfg));
    cfg.cbSize             = sizeof(cfg);
    cfg.hwndParent         = m_hWnd;
    cfg.dwFlags            = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.dwCommonButtons    = TDCBF_CANCEL_BUTTON;
    cfg.pszWindowTitle     = TR(L"导出消息记录");
    cfg.pszMainInstruction = TR(L"还有更早的消息没有加载");
    cfg.pszContent         = content;
    cfg.pButtons           = buttons;
    cfg.cButtons           = ARRAYSIZE(buttons);
    cfg.nDefaultButton     = kExportBtnLoadAll;

    int pressed = IDCANCEL;
    if (FAILED(::TaskDialogIndirect(&cfg, &pressed, NULL, NULL)))
    {
        return kHistoryExportCancel;
    }
    if (pressed == kExportBtnLoadAll)
    {
        return kHistoryExportLoadAll;
    }
    if (pressed == kExportBtnLoadedOnly)
    {
        return kHistoryExportLoadedOnly;
    }
    return kHistoryExportCancel;
}

bool CBalloonHistoryWindow::AskExportTarget(const HistoryExportOptions& opts, size_t convCount,
                                            std::wstring& outTarget)
{
    outTarget.clear();
    //多个会话、不打包：选一个文件夹，每个会话一个子文件夹
    if (!opts.m_zip && convCount > 1)
    {
        return PickExportFolder(m_hWnd, TR(L"选择导出到哪个文件夹"), outTarget);
    }
    //打包为 zip，或单个会话：另存为对话框。默认文件名「消息记录_<会话名>」（多个会话时「聊天记录」）
    const bool single = (convCount <= 1);
    CString defName = single ? CString(TRF(L"消息记录_{0}", m_titleName).c_str()) : CString(TR(L"聊天记录"));
    std::wstring fileFilter;
    LPCTSTR defExt = NULL;
    if (opts.m_zip)
    {
        //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
        fileFilter = filedlg::MakeFilter(TR(L"zip 压缩包 (*.zip)"), L"*.zip");
        defExt = _T("zip");
    }
    else if (opts.m_html)
    {
        fileFilter = filedlg::MakeFilter(TR(L"网页 (*.html)"), L"*.html");
        defExt = _T("html");
    }
    else
    {
        fileFilter = filedlg::MakeFilter(TR(L"文本文件 (*.txt)"), L"*.txt");
        defExt = _T("txt");
    }
    defName = CString(SanitizeExportFileName(std::wstring((LPCTSTR)defName)).c_str());
    CFileDialog dlg(FALSE, defExt, defName, OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY,
                    fileFilter.c_str(), m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return false;
    }
    outTarget = dlg.m_szFileName;
    return !outTarget.empty();
}

void CBalloonHistoryWindow::StartFetchAllForExport()
{
    if (m_fetchingOlder)
    {
        return;   //已有一轮在途：等它并入后，停止分支会接着拉
    }
    const HistoryFilter& filter = m_model.Filter();
    CoverRange cover;
    if (m_pStore == NULL || !m_pStore->GetCoverage(filter.m_ctype, filter.m_cid, cover) || cover.m_reachedOldest)
    {
        //已经没有更早的可拉：直接导出
        ::PostMessage(m_hWnd, WM_HISTORY_EXPORT_READY, 0, 0);
        return;
    }
    m_fetchRounds = 0;
    m_fetchCountBefore = m_model.Total();
    RequestOlderPage(cover);
}

LRESULT CBalloonHistoryWindow::OnExportReadyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_pendingExportPath.IsEmpty())
    {
        return 0;
    }
    const CString path = m_pendingExportPath;
    m_pendingExportPath.Empty();
    //按导出浮层里选的选项导出（全部消息已拉到本地）
    StartExportJob(m_pendingExportOptions, std::wstring((LPCTSTR)path));
    return 0;
}

bool CBalloonHistoryWindow::QueryExportRecords(int ctype, int64_t cid, int range, std::vector<HistoryRecord>& out)
{
    out.clear();
    CV2MsgStore* store = ManagerStore();
    if (store == NULL)
    {
        return false;
    }
    //当前会话按「当前筛选条件」导出：与右侧列表同一套条件（类型、关键字、发送人、时间）
    const bool sameAsList = (range == kHistoryExportRangeFilter && ctype == m_model.Filter().m_ctype
                             && cid == m_model.Filter().m_cid);
    if (sameAsList)
    {
        ApplyCoverageBound();
        return m_model.QueryAll(NowMs(), out);
    }
    //其它会话、或选了某个时间范围：全部类型，只按时间筛；与右侧列表一样按覆盖区间只取可见的
    HistoryFilter f;
    f.m_ctype = ctype;
    f.m_cid = cid;
    const int timeRange = ExportRangeToTimeRange(range);
    f.m_range = (timeRange >= 0) ? (HistoryTimeRange)timeRange : kHistoryTimeAll;
    if (m_pMainPanel != NULL)
    {
        CoverRange cover;
        store->GetCoverage(ctype, cid, cover);
        f.m_minMsgid = CoverageMinVisibleMsgid(cover, false);
    }
    const int kBatch = 500;
    f.m_pageSize = kBatch;
    const int64_t now = NowMs();
    for (f.m_page = 1;; ++f.m_page)
    {
        std::vector<HistoryRecord> page;
        if (!store->QueryPage(f, now, page))
        {
            return false;
        }
        out.insert(out.end(), page.begin(), page.end());
        if ((int)page.size() < kBatch)
        {
            break;
        }
    }
    return true;
}

HistoryExportConvJob CBalloonHistoryWindow::BuildExportConvJob(const CString& title,
                                                               const std::vector<HistoryRecord>& all,
                                                               const std::wstring& docName,
                                                               const HistoryExportOptions& opts)
{
    HistoryExportConvJob job;
    job.m_docName = docName;
    job.m_messageCount = (int)all.size();
    //附件放在与文档同目录的「<文档名>_files」文件夹，文档里用相对路径引用
    const std::wstring filesDirName = ExportFilesDirName(docName);
    std::set<std::wstring> usedNames;
    std::vector<HistoryExportRow> rows;
    CString text;
    if (opts.m_html)
    {
        rows.reserve(all.size());
    }
    else
    {
        //纯文本：时间正序（旧→新）便于从上往下阅读；每行“[时间] 昵称：内容”
        text += TRF(L"消息记录 · {0}", title).c_str();
        text += _T("\r\n");
        text += _T("========================================\r\n");
    }
    for (int i = (int)all.size() - 1; i >= 0; --i)
    {
        const HistoryRecord& rec = all[(size_t)i];
        const CString sender = HistorySenderName(rec.m_msgtype, ResolveSenderName(rec.m_fromid));
        const HistoryContentBrief brief = ParseContentBrief(rec.m_msgtype, rec.m_content);
        //本机有原图或文件、并且选了带上时，作为附件复制进 _files（文件名不重复）
        std::wstring attachedRel;
        const bool image = IsHistoryRowImageType(rec.m_msgtype);
        const bool file = IsHistoryRowFileType(rec.m_msgtype);
        CString local;
        if (((image && opts.m_includeImages) || (file && opts.m_includeFiles)) && ResolveRecordLocalFile(rec, local))
        {
            std::wstring name;
            if (image)
            {
                wchar_t base[32] = { 0 };
                swprintf_s(base, L"%lld", (long long)rec.m_msgid);
                name = std::wstring(base) + ImageExtensionOfFile(local);
            }
            else
            {
                name = std::wstring((LPCTSTR)Utf8ToCString(brief.m_fileName));
            }
            name = UniqueExportFileName(SanitizeExportFileName(name), usedNames);
            HistoryExportAttachment att;
            att.m_source = (LPCTSTR)local;
            att.m_relPath = filesDirName + L"/" + name;
            job.m_attachments.push_back(att);
            attachedRel = att.m_relPath;
        }

        if (!opts.m_html)
        {
            text += _T("[");
            text += FormatTime(rec.m_timestamp);
            text += _T("] ");
            text += sender;
            text += _T("：");
            text += Utf8ToCString(PreviewFromContent(rec.m_msgtype, rec.m_content));
            if (!attachedRel.empty())
            {
                text += _T(" (");
                text += attachedRel.c_str();
                text += _T(")");
            }
            text += _T("\r\n");
            continue;
        }

        HistoryExportRow row;
        row.m_time = FormatExportTime(rec.m_timestamp);
        row.m_sender = (LPCTSTR)sender;
        if (image)
        {
            row.m_kind = kHistoryExportImage;
            row.m_imageRelPath = attachedRel;
        }
        else if (file)
        {
            row.m_kind = kHistoryExportFile;
            row.m_text = (LPCTSTR)Utf8ToCString(brief.m_fileName);
            if (!attachedRel.empty())
            {
                row.m_text += L" (" + attachedRel + L")";
            }
        }
        else
        {
            row.m_kind = kHistoryExportText;
            row.m_text = (LPCTSTR)Utf8ToCString(PreviewFromContent(rec.m_msgtype, rec.m_content));
        }
        rows.push_back(row);
    }
    if (opts.m_html)
    {
        job.m_docText = BuildHistoryHtml(TRF(L"消息记录 · {0}", title), rows);
    }
    else
    {
        job.m_docText = (LPCTSTR)text;
    }
    LOG_INFO("history export: conversation document built, %u messages, %u attachments, html %d.",
             (unsigned)all.size(), (unsigned)job.m_attachments.size(), (int)opts.m_html);
    return job;
}


// i18n-ignore-begin: 调试入口 --history 的模拟数据
void CBalloonHistoryWindow::SeedDebugStore()
{
    if (!m_seedStore.Open(L":memory:"))
    {
        LOG_ERROR(_T("历史窗调试预置库打开失败。"));
        return;
    }
    m_titleName = _T("苏沐辰");
    m_senderNames[1001] = _T("苏沐辰");
    m_senderNames[9999] = _T("林知夏");

    const int64_t cid = 1001;
    const int64_t now = NowMs();
    const int64_t kSpacingMs = 12LL * 3600LL * 1000LL;   //每条间隔 12 小时，跨度约 13 天
    const int kCount = 27;

    //调试入口的假正文。最后两条是<u>专门用来目检新增渲染分支</u>的，不要删：
    //  · 倒数第二条带 /f["<id>"] 内联表情标记 —— 不留一条的话，调试入口永远走不到
    //    "把表情画成图"那条分支，画坏了也看不出来；
    //  · 最后一条足够长，会超过折叠行数上限，用来核对「展开 / 收起」链接与行高。
    //JSON 字符串里的引号要转义，故表情标记写成 /f[\\"5\\"]。
    const char* kTexts[] =
    {
        "改了发你看下", "你觉得这个排版怎么样？", "左对齐更好，居中显得太弱了",
        "收到，很有意思",
        "好的 /f[\\\"5\\\"] 我这就改，改完发你 /f[\\\"12\\\"]",
        "对了，关于这次品牌升级我整理了几点想法：主色调建议从橙色微调到更暖一些的"
        "珊瑚色，辅助色保留现在的灰绿搭配；字体方面思源黑体确实更适合长段落阅读，"
        "标题也可以试试更重的字重。另外 banner 那块的留白我觉得还可以再松一点，"
        "现在四周挤得有些满，视线没有落脚的地方。图标风格我倾向于统一走线性，"
        "实心的那套在小尺寸下会糊成一团，列表页里尤其明显。插画那部分先不动，"
        "等主色定下来再统一调，不然改两遍。还有一件事，移动端的按钮高度现在是"
        "40，我觉得可以提到 44，拇指点起来会舒服很多，安卓那边也好对齐。"
        "你看周五之前能不能出一版对比稿？我们约个时间一起过一遍。",
        //带网址的一条 —— 用来目检链接识别那条渲染分支（蓝色 + 下划线 + 可点）。
        //网址按原样写：正文格式里的转义只认"紧跟表情标记的双斜杠"，网址不受影响。
        "参考资料见 https://example.com/brand/guide?v=2 还有 www.example.org 都看下",
    };
    //上面那张假正文表的条数。取数组长度而不是写死 —— 往表里加一条却忘了改这里的话，
    //新加的那条永远轮不上，等于白加（本来写死的是 6）。
    const int kTextCount = (int)(sizeof(kTexts) / sizeof(kTexts[0]));

    const char* kFiles[] = { "设计规范-v2.docx", "参考案例.zip", "季度报告.xlsx" };
    //与 kFiles 一一对应的文件大小（字节），让调试入口里的文件卡片显示出大小那一行。
    const char* kFileSizes[] = { "90112", "5242880", "187187" };
    //图片的原图宽高（像素），轮流套用：宽图、高图、普通照片，好在调试入口里核对预览图
    //按原图等比缩放的效果。
    const char* kImageSizes[] = { "\"w\":1408,\"h\":870", "\"w\":400,\"h\":1200", "\"w\":640,\"h\":480" };
    const int kImageSizeCount = (int)(sizeof(kImageSizes) / sizeof(kImageSizes[0]));

    int64_t msgid = 1001;
    for (int k = 0; k < kCount; ++k)
    {
        HistoryRecord rec;
        rec.m_msgid = msgid++;
        rec.m_ctype = 1;
        rec.m_cid = cid;
        rec.m_fromid = (k % 2 == 0) ? 1001 : 9999;
        rec.m_timestamp = now - (int64_t)(kCount - 1 - k) * kSpacingMs;
        rec.m_recalled = false;

        int kind = k % 5;
        if (kind == 0)
        {
            std::string text = kTexts[k % kTextCount];
            rec.m_msgtype = 1;
            rec.m_content = std::string("{\"text\":\"") + text + "\"}";
            rec.m_searchText = text;
            rec.m_isLink = false;
        }
        else if (kind == 1)
        {
            std::string text = std::string("看下这个链接 https://example.com/p") + std::to_string(k);
            rec.m_msgtype = 1;
            rec.m_content = std::string("{\"text\":\"") + text + "\"}";
            rec.m_searchText = text;
            rec.m_isLink = true;
        }
        else if (kind == 2)
        {
            rec.m_msgtype = 2;
            rec.m_content = std::string("{\"image\":{\"fileid\":\"img\",")
                          + kImageSizes[k % kImageSizeCount] + "}}";
            rec.m_searchText = "";
            rec.m_isLink = false;
        }
        else if (kind == 3)
        {
            std::string name = kFiles[k % 3];
            rec.m_msgtype = 3;
            rec.m_content = std::string("{\"file\":{\"name\":\"") + name
                          + "\",\"size\":\"" + kFileSizes[k % 3] + "\"}}";
            rec.m_searchText = name;
            rec.m_isLink = false;
        }
        else
        {
            rec.m_msgtype = 4;
            rec.m_content = "{}";
            rec.m_searchText = "";
            rec.m_isLink = false;
        }
        m_seedStore.Upsert(rec);
    }

    //再预置一个群会话与另一个单聊，让调试入口的左栏（聊天记录管理器）有几行可看、可切换、可按
    //类型筛选与搜索。名字也放进 m_senderNames（调试入口下 ResolveManagerRow 据它取会话名）
    const int kDebugCtypeGroup = 2;
    const int64_t kDebugGroupId = 600001;
    const int64_t kDebugPeerId = 9999;
    m_senderNames[kDebugGroupId] = _T("产品研发群");
    const char* kGroupTexts[] = { "明天上午十点评审，大家准时", "周报已经发到群文件了", "收到" };
    const int kGroupTextCount = (int)(sizeof(kGroupTexts) / sizeof(kGroupTexts[0]));
    for (int k = 0; k < kGroupTextCount; ++k)
    {
        HistoryRecord rec;
        rec.m_msgid = msgid++;
        rec.m_ctype = kDebugCtypeGroup;
        rec.m_cid = kDebugGroupId;
        rec.m_fromid = (k % 2 == 0) ? 1001 : 9999;
        //群里的消息比上面那个单聊的最后一条晚一点，左栏里排在最前面
        rec.m_timestamp = now + (int64_t)(k + 1) * 60LL * 1000LL;
        rec.m_msgtype = 1;
        rec.m_content = std::string("{\"text\":\"") + kGroupTexts[k] + "\"}";
        rec.m_searchText = kGroupTexts[k];
        m_seedStore.Upsert(rec);
    }
    const char* kPeerTexts[] = { "周报模板我改了一版", "好的" };
    const int kPeerTextCount = (int)(sizeof(kPeerTexts) / sizeof(kPeerTexts[0]));
    for (int k = 0; k < kPeerTextCount; ++k)
    {
        HistoryRecord rec;
        rec.m_msgid = msgid++;
        rec.m_ctype = 1;
        rec.m_cid = kDebugPeerId;
        rec.m_fromid = (k % 2 == 0) ? kDebugPeerId : 1001;
        //这个单聊的最后一条在三天前，左栏里排在最后
        rec.m_timestamp = now - 3LL * 24LL * 3600LL * 1000LL + (int64_t)k * 60LL * 1000LL;
        rec.m_msgtype = 1;
        rec.m_content = std::string("{\"text\":\"") + kPeerTexts[k] + "\"}";
        rec.m_searchText = kPeerTexts[k];
        m_seedStore.Upsert(rec);
    }

    m_model.SetStore(&m_seedStore);
    m_model.SetConversation(1, cid);
    m_model.SetPageSize(kHisPageSize);
}
// i18n-ignore-end

bool CBalloonHistoryWindow::BuildWindow(HWND hWndParent)
{
    if (Create(hWndParent, CWindow::rcDefault, g_strAppTitle.c_str(),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("历史消息记录窗口 Create 失败。"));
        return false;
    }

    //登记为图片缓存的通知接收窗口：列表里的缩略图在缓存未命中时请求后台解码，解码
    //完成后缓存经 WM_CHAT_IMAGE_READY 通知本窗口重画，缩略图才会从灰底换成真图。
    //不登记的话要等下一次整页重画（例如切换分类）才显示。注销在 OnDestroyMsg。
    balloonchatwnd::ChatImageCache::Inst().RegisterListener(m_hWnd);
    LOG_INFO("history window: registered to image cache, hwnd=0x%p.", m_hWnd);

    //标题栏显示「聊天记录管理」；当前会话名显示在右侧标题区（ApplyHistoryHeaderTitle）。
    SetTitle(TR(kHisManagerTitle));
    SetTitleBarHeight(kHisTitleBarHeight);

    //左栏的宽度与折叠状态：按账号配置恢复（调试入口没有客户端对象，用默认值）。搜索范围只在
    //窗口打开期间有效，每次新开都恢复为全部会话
    m_mgrPaneWidth = kMgrPaneDefaultW;
    m_mgrCollapsed = false;
    if (m_lpFMGClient != NULL)
    {
        const int savedWidth = m_lpFMGClient->m_UserConfig.GetHistoryMgrPaneWidth();
        if (savedWidth > 0)
        {
            m_mgrPaneWidth = (savedWidth < kMgrPaneMinW) ? kMgrPaneMinW : savedWidth;
        }
        m_mgrCollapsed = m_lpFMGClient->m_UserConfig.IsHistoryMgrCollapsed();
    }
    m_mgrScope.clear();
    m_mgrQuery.clear();
    m_mgrHitsRaw.clear();
    m_mgrHitsQuery.clear();
    m_mgrSearchSeq = 0;
    SetTitleBarTransparent(true);
    SetTitleTextColor(kHisCaptionColor);
    SetCaptionGlyphColor(kHisCaptionColor);

    //标题栏左侧图标：用程序主图标。本窗标题栏透明，穿透显示的是根控件底色（白，
    //见 HistoryWindowLayout 里 root->SetBgColor），故按白底合成。
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kHisTitleIconSize, kHisTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
    m_taskIcons.ApplyTaskIcons(m_hWnd);
    SetButtons(true, true, true);   //最小化 / 最大化 / 关闭
    SetResizable(true);
    ApplyManagerMinSize(false);

    //开启双击合成：列表行的"双击整行即定位"要靠双击。
    //（2026-10-06 更正：balloonui 窗口的窗口类其实带 CS_DBLCLKS，系统本就会投递双击，这里的双击合成实际不起作用，保留无害）
    EnableDoubleClick(true);

    InitUi();

    //每次打开都重新记录还原态矩形，上一次打开时记下的不再作数
    m_hasNormalRect = false;
    //默认宽度：右侧与原来的消息记录窗一样宽，左栏展开时再加上左栏与分隔条
    const int defaultWidth = m_mgrCollapsed
                           ? (kHisWindowWidth + kMgrSplitBarCollapsedW)
                           : (kHisWindowWidth + m_mgrPaneWidth + kMgrSplitBarW);
    ResizeClient(defaultWidth, kHisWindowHeight);
    CenterWindow();
    //先按默认大小居中，再按账号配置恢复上次的位置与大小（没有保存过时保持居中）
    const bool maximize = RestoreWindowPlacement();
    ShowWindow(maximize ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    //窗口有了实际大小，左栏才能把选中的会话滚进可见区域
    SyncManagerSelection(true);
    return true;
}

HistoryViewData CBalloonHistoryWindow::BuildViewData(bool pageOnly)
{
    int64_t now = NowMs();
    //先按覆盖区间设显示下界，再重查：断层以下的旧行不显示（见 ApplyCoverageBound）。
    //显示下界变了就等于筛选条件变了，此时即便 pageOnly 为 true，RefreshPage 也会改做完整刷新。
    ApplyCoverageBound();
    if (pageOnly)
    {
        m_model.RefreshPage(now);
    }
    else
    {
        m_model.Refresh(now);
    }

    HistoryViewData data;
    data.m_title = TRF(L"消息记录 · {0}", m_titleName).c_str();
    //标题区：会话名，以及左栏折叠时才出现的「»」
    data.m_conversationName = m_titleName;
    data.m_showExpandLeft = m_mgrCollapsed;
    //「通配符」复选框只在建界面时用到这个值，之后由复选框自己保持
    data.m_wildcard = m_model.Filter().m_wildcard;
    //清空要经主面板发请求：调试入口没有主面板，不放这个按钮
    data.m_showClearConversation = (m_pMainPanel != NULL);
    data.m_totalCount = (int)m_model.Total();
    data.m_currentPage = m_model.CurrentPage();
    data.m_pageCount = m_model.PageCount();
    data.m_activeChip = (int)m_model.Filter().m_type;
    data.m_activeTimeIndex = ComboIndexFromRange(m_model.Filter().m_range);
    data.m_dateRangeLabel = DateRangeLabelOf(m_model.Filter(), now);

    //空状态文案按当前筛选条件算：搜不到就提示换个词、Pin 分类空就顺带说怎么钉，
    //这比一句通用的"暂无内容"有用得多。列表非空时这两项用不上。
    //时间轴模式下关键字不筛选，按实际查询的条件给文案（QueryFilter 去掉了只用来高亮的关键字）
    const HistoryEmptyText emptyText = BuildEmptyStateText(m_model.QueryFilter());
    data.m_emptyTitle    = Utf8ToCString(emptyText.m_title);
    data.m_emptySubtitle = Utf8ToCString(emptyText.m_subtitle);

    const std::vector<HistoryRecord>& recs = m_model.Records();

    //本页语音的已听状态（语音消息第 4b 步）：分页查询不读 played 列，模型里的 HistoryRecord::m_played 恒为默认的
    //已听，这里按本页的语音 msgid 一次查出本地库里记为已听的。调试入口没有本地库、或查询失败时一律按已听，不画红点
    std::vector<int64_t> voiceIds;
    for (size_t v = 0; v < recs.size(); ++v)
    {
        if (recs[v].m_msgtype == ::message_type_voice)
        {
            voiceIds.push_back(recs[v].m_msgid);
        }
    }
    std::set<int64_t> playedVoiceIds;
    bool voicePlayedKnown = false;
    if (!voiceIds.empty() && m_pStore != NULL)
    {
        voicePlayedKnown = m_pStore->LoadPlayedVoiceIds(m_model.Filter().m_ctype, m_model.Filter().m_cid, voiceIds,
                                                        playedVoiceIds);
    }

    int imageOrdinal = 0;   //本页第几张图片，仅调试入口轮流套用缩略图状态时用
    for (size_t i = 0; i < recs.size(); ++i)
    {
        HistoryRowView row;
        //系统提示写成「系统消息」、灰色头像，不再按发送方 0 解析成「用户0」（2026-10-03 起）
        row.m_senderName = HistorySenderName(recs[i].m_msgtype, ResolveSenderName(recs[i].m_fromid));
        row.m_avatarColor = HistoryAvatarColor(recs[i].m_msgtype, recs[i].m_fromid);
        row.m_timeText = FormatTimeShort(recs[i].m_timestamp);
        row.m_previewText = Utf8ToCString(PreviewFromContent(recs[i].m_msgtype, recs[i].m_content));

        //昵称下方那块内容的数据。图片取内容MD5 换算缓存路径、取原图宽高定预览尺寸；
        //文件取扩展名、文件名与大小做成卡片，并把预览文字从 "[文件] 名字" 换成
        //"名字 · 大小"（搜索高亮与导出按这一份算）。
        const HistoryContentBrief brief =
            ParseContentBrief(recs[i].m_msgtype, recs[i].m_content);
        if (IsHistoryRowImageType(recs[i].m_msgtype))
        {
            //图片行一律画预览图：本地有原图画图，没有则按状态画"需要下载 / 下载中 / 破图"。
            //按消息类型而不是"有没有 MD5"来认图片 —— 缺 MD5 的老消息也该显示破图，而不是
            //什么都不画。
            FillImageThumb(row, recs[i].m_msgid, brief.m_imageMd5, brief.m_imageFileId,
                           imageOrdinal);
            row.m_imageWidth  = brief.m_imageWidth;
            row.m_imageHeight = brief.m_imageHeight;
            ++imageOrdinal;
        }
        else if (recs[i].m_msgtype == ::message_type_voice)
        {
            //语音：画成与聊天窗同款的语音条（第 4 期）。本机有文件为就绪，没有时标为需要下载、点了再下；
            //已听取本地库的 played 列，本窗口刚播放过的也算；正在播放、正在下载的按窗口记下的状态画
            const std::wstring voiceDir = net::UserCacheDir(g_szDataRoot, net::CurrentUserAccount(), L"ChatVoice");
            const bool fromMe = (SelfUserId() != 0 && recs[i].m_fromid == SelfUserId());
            const bool played = !voicePlayedKnown || playedVoiceIds.count(recs[i].m_msgid) > 0
                             || m_playedVoices.count(recs[i].m_msgid) > 0;
            row.m_isVoice = true;
            row.m_voiceShowDot = true;
            row.m_voice = balloonchatwnd::BuildStoredVoiceMessage(
                recs[i].m_msgid, fromMe, brief.m_voiceFileId, brief.m_voiceMd5, brief.m_voiceDuration,
                brief.m_voiceSize, brief.m_voiceWave, played, voiceDir);
            if (row.m_voice.voiceState != balloonchatwnd::kVoiceStateReady
                && m_downloadingVoices.count(brief.m_voiceMd5) > 0)
            {
                row.m_voice.voiceState = balloonchatwnd::kVoiceStateDownloading;
            }
            row.m_voice.voicePlaying = (m_playingVoiceMsgid != 0 && m_playingVoiceMsgid == recs[i].m_msgid);
        }
        else if (!brief.m_fileName.empty())
        {
            row.m_thumbFileExt = Utf8ToCString(FileExtensionOf(brief.m_fileName));
            row.m_fileName     = Utf8ToCString(brief.m_fileName);

            CString text = row.m_fileName;
            const std::string sizeText = FormatFileSize(brief.m_fileSize);
            if (!sizeText.empty())
            {
                row.m_fileSizeText = Utf8ToCString(sizeText);
                text += _T(" · ");
                text += row.m_fileSizeText;
            }
            row.m_previewText = text;
        }
        else if (::message_type_folder == recs[i].m_msgtype)
        {
            //文件夹传输记录：把预览换成"[文件夹] 名字 · N 个文件 · 大小 · 结局"。
            //这条记录没有可下载的内容，也没有缩略图，一行文字就是它的全部信息，
            //故规模与结局都写进去（拼装口径见 BuildFolderRowText，有单测）。
            row.m_previewText = Utf8ToCString(BuildFolderRowText(brief));
        }
        else if (::message_type_merged == recs[i].m_msgtype)
        {
            //合并转发的聊天记录：画成聊天记录卡片（标题 + 条数），单击标题打开查看窗。
            //预览与正文仍是「[聊天记录] 标题」，搜索高亮与导出按它算
            row.m_isMergedCard = true;
            row.m_mergedTitle  = Utf8ToCString(brief.m_mergedTitle);
            row.m_mergedCount  = brief.m_mergedCount;
        }
        else if ((recs[i].m_msgtype == ::message_type_thread_post || recs[i].m_msgtype == ::message_type_thread_reply)
                 && !brief.m_threadImages.empty())
        {
            //帖子卡片与补充卡片带的图片：正文下面排一行缩略图（docs/帖子群方案.md 14.5），本机没有的随即下载
            FillCardThumbs(row, brief.m_threadImages,
                           IsImageGridMode() ? brief.m_threadImages.size() : kHistoryCardMaxThumbs);
        }
        row.m_msgid = recs[i].m_msgid;
        row.m_pinned = recs[i].m_pinned;

        //完整正文：文本消息保留 /f["<id>"] 原始标记，交给行内容块把表情画成图。
        //非文本消息（图片 / 文件 / 语音…）与预览同口径，都是"[图片]"这类占位。
        //文件行上面已经把预览换成了"名字 · 大小"，正文也跟着用那一份 —— 否则
        //展开的时候会突然变回"[文件] 名字"，前后不一致。
        if (recs[i].m_msgtype == ::message_type_file && !brief.m_fileName.empty())
        {
            row.m_fullText = row.m_previewText;
        }
        //文件夹记录同理：展开态与收起态显示同一行文字，否则展开时会突然从
        //"[文件夹] 名字 · 128 个文件 · 2.4 GB · 已完成" 变回只剩 "[文件夹] 名字"
        else if (recs[i].m_msgtype == ::message_type_folder)
        {
            row.m_fullText = row.m_previewText;
        }
        else
        {
            row.m_fullText =
                Utf8ToCString(FullTextFromContent(recs[i].m_msgtype, recs[i].m_content));
            //代码片段：昵称下方画代码卡片（docs/代码片段方案.md 第 3.4 节），代码取上面的正文
            int codeLang = 0;
            std::vector<codesnippet::CodeNote> codeNotes;
            if (balloonchatwnd::CodeLangFromContentJson(recs[i].m_msgtype, recs[i].m_content, codeLang, &codeNotes,
                                                        recs[i].m_fromid))
            {
                row.m_isCode = true;
                row.m_codeLang = codeLang;
                row.m_codeNotes = codeNotes;
            }
            //Markdown 消息：FlamingoMarkdown.dll 可用时昵称下方画 Markdown 卡片（docs/Markdown消息方案.md 第 12 节），
            //点卡片打开预览窗口；DLL 不可用或解析失败时仍按普通文字显示源文本
            else if (balloonchatwnd::IsMarkdownContentJson(recs[i].m_msgtype, recs[i].m_content))
            {
                const bool fromMe = (SelfUserId() != 0 && recs[i].m_fromid == SelfUserId());
                row.m_isMarkdown = balloonchatwnd::MakeMarkdownCardInfo(row.m_fullText,
                                                                        std::wstring((LPCWSTR)row.m_senderName), fromMe,
                                                                        recs[i].m_timestamp, recs[i].m_msgid,
                                                                        row.m_markdownCard);
            }
        }

        //展开态取自模型（按 msgid 记），故局部刷新与翻页往返都不会把它冲掉。
        row.m_expanded = m_model.IsExpanded(recs[i].m_msgid);

        //多选模式：行首复选框的勾选状态取自选择集合（按 msgid 记），翻页往返不丢
        row.m_selectMode = m_multiSelect;
        row.m_selected = m_multiSelect && m_selection.IsSelected(recs[i].m_msgid);

        data.m_rows.push_back(row);
    }

    //各行所属的"天"，供下面插日期分组头用。"天"须按本地时区算。
    std::vector<int64_t> dayKeys;
    dayKeys.reserve(recs.size());
    for (size_t i = 0; i < recs.size(); ++i)
    {
        dayKeys.push_back(LocalDayKey(recs[i].m_timestamp));
    }

    //注：这里<u>不再</u>把连续同一人的消息折叠成"省掉头像与名字"。
    //折叠原本是为了提高密度，但实测下来它把<u>消息之间的边界</u>抹掉了 —— 同一人连发
    //三条，看上去就是一条带换行的长消息，用户会奇怪"一条消息怎么有三个 Pin 按钮"。
    //消息记录是"按条查找"的界面，边界比密度重要，故每条都完整显示头像 / 名字 / 时间。
    //ComputeCompactFlags 与它的用例留着不删：折叠这件事本身没错，只是不适合这个界面，
    //日后若做"紧凑模式"开关可以直接复用。

    //日期分组头：每当"天"与上一行不同就插一条。首行恒插 —— 页首必须交代清楚是哪天，
    //否则翻到中间某页会看到一堆只有 HH:MM 的行、完全不知道是什么时候的。
    //分组在页内做、不跨页合并（见 HistoryRowView::m_groupLabel 的说明）。
    const int64_t todayKey = LocalDayKey((int64_t)::time(NULL) * 1000);
    for (size_t i = 0; i < data.m_rows.size() && i < dayKeys.size(); ++i)
    {
        if (i == 0 || dayKeys[i] != dayKeys[i - 1])
        {
            data.m_rows[i].m_groupLabel =
                Utf8ToCString(FormatDateGroupLabel(dayKeys[i], todayKey));
        }
    }

    //搜索命中高亮：关键字为空时区间集为空，预览标签表现与普通标签一致。
    //勾选了「通配符」时按通配符找命中（FindSearchHighlightRanges 分派）
    const std::string& keyword = m_model.Filter().m_keyword;
    const bool wildcard = m_model.Filter().m_wildcard;
    if (HasKeywordFilter(m_model.Filter()))
    {
        const CString keywordW = Utf8ToCString(keyword);
        const std::wstring kw((LPCWSTR)keywordW);
        for (size_t i = 0; i < data.m_rows.size(); ++i)
        {
            //关键字要在"<u>扁平文本</u>"上找，不能在原始正文上找 —— 行内容块
            //（HistoryContentView）认的高亮区间是扁平口径：一个内联表情占<u>一个</u>
            //字符位（U+FFFC 占位）。而原始正文里一个表情是 `/f["5"]` 这么七八个字符，
            //拿它算出来的下标喂给内容块，含表情的消息高亮会整体往后错位。
            std::vector<balloonchat::ChatSegment> segs =
                balloonchat::ParseSendBody(data.m_rows[i].m_fullText);
            const std::wstring flat = balloonchatwnd::BuildFlatText(segs);
            data.m_rows[i].m_highlights = FindSearchHighlightRanges(flat, kw, wildcard);
        }
    }

    //时间轴模式：各行改成时间轴样式，日期分组头写上星期几与当天条数，命中的行节点标黄
    data.m_timeline = m_timelineMode;
    if (m_timelineMode)
    {
        DecorateTimelineRows(data);
    }
    return data;
}

CString CBalloonHistoryWindow::ResolveSenderName(int64_t fromid) const
{
    //真实会话：走主面板按 userid 解析昵称（本人 / 联系人 / 回退）。
    if (m_pMainPanel != NULL)
    {
        return m_pMainPanel->ResolveSenderName(fromid);
    }
    //调试预置：查 m_senderNames。
    std::map<int64_t, CString>::const_iterator it = m_senderNames.find(fromid);
    if (it != m_senderNames.end())
    {
        return it->second;
    }
    CString name;
    name.Format(TR(L"用户%lld"), (long long)fromid);
    return name;
}

std::vector<int64_t> CBalloonHistoryWindow::ResolveSyntaxFromIds(const std::string& keywordUtf8, bool wildcard,
                                                                 bool acrossConversations)
{
    const HistorySearchQuery q = ParseHistorySearch(keywordUtf8, wildcard);
    if (q.m_fromNames.empty())
    {
        return std::vector<int64_t>();
    }
    std::vector<int64_t> candidates;
    if (acrossConversations)
    {
        CV2MsgStore* store = ManagerStore();
        if (!m_allSendersLoaded && store != NULL)
        {
            m_allSendersLoaded = store->QueryAllSenders(m_allSenders);
            LOG_INFO("history window: loaded %u senders for from: matching (ok %d).",
                     (unsigned)m_allSenders.size(), (int)m_allSendersLoaded);
        }
        candidates = m_allSenders;
    }
    else if (!m_model.QuerySenders(candidates))
    {
        LOG_WARNING("history window: query senders for from: matching failed.");
    }
    std::vector<std::string> names;
    names.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        names.push_back(CStringToUtf8(ResolveSenderName(candidates[i])));
    }
    const std::vector<int64_t> ids = MatchSearchFromIds(q, candidates, names);
    LOG_INFO("history window: from: %u names matched %u of %u candidates (across %d).",
             (unsigned)q.m_fromNames.size(), (unsigned)ids.size(), (unsigned)candidates.size(),
             (int)acrossConversations);
    return ids;
}

void CBalloonHistoryWindow::SyncSyntaxFromIds()
{
    const HistoryFilter& f = m_model.Filter();
    m_model.SetSyntaxFromIds(ResolveSyntaxFromIds(f.m_keyword, f.m_wildcard, false));
}

void CBalloonHistoryWindow::InitUi()
{
    HistoryViewData data = BuildViewData(false);
    //列表滚动视口右侧要给窗口缩放边框让出的宽度。与 DuiFrameWindow::OnNcHitTestMsg
    //同一口径：边框宽度是 96-dpi 逻辑像素，按当前显示器 DPI 折算为物理像素。本函数在
    //Create 之后调用，此时 GetDpi 已能取到有效值。
    const int listRightInsetPx = balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(),
                                                                         GetDpi(),
                                                                         IsResizable());
    LOG_INFO("history window: list right inset %d px (border %d, dpi %d, resizable %d).",
             listRightInsetPx, GetBorderPx(), GetDpi(), (int)IsResizable());
    //客户区根容器是左右分栏：左栏是会话列表，右侧容器里是原来那整套消息记录界面与文件 / 图片页
    //（docs/聊天记录管理器方案.md §11.5），按左栏标签只显示其一
    m_libModel.SetStore(ManagerStore());
    m_libModel.SetSelfUserId(SelfUserId());
    const HistoryLibraryViewData libData = BuildLibraryViewData();
    SetClientContent(BuildHistoryManagerRoot(BuildHistoryManagerPane(m_mgrTypeFilter, m_mgrScope.size(),
                                                                     m_mgrWildcard, m_mgrTab),
                                             BuildHistoryWindowBody(data, listRightInsetPx),
                                             BuildHistoryLibraryBody(libData, 0, listRightInsetPx),
                                             m_mgrPaneWidth, m_mgrCollapsed));
    ApplyHistoryManagerTab(GetClientContent(), m_mgrTab);

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    //捕获要复用的控件指针（所有权在控件树）。
    m_pScroll = dynamic_cast<DuiScrollView*>(content->FindCtrlById(kIdHistoryScroll));
    m_pPager = content->FindCtrlById(kIdHistoryPager);
    m_pFooterInfo = content->FindCtrlById(kIdHistoryFooterInfo);
    m_pMgrScroll = dynamic_cast<DuiScrollView*>(content->FindCtrlById(kIdMgrScroll));

    //轻提示条挂在分栏右侧容器的末尾（基类按子控件顺序绘制，末尾即最上层），布局 Fixed(0)
    //使它不占竖直方向的空间，落点由 DuiToast 自己按"顶部居中"计算。与聊天窗同一做法。
    //挂在右侧容器而不是聊天记录页上：停在文件 / 图片页时聊天记录页是隐藏的，挂在它上面的提示条看不见
    m_pToast = NULL;
    DuiVBox* rootBox = dynamic_cast<DuiVBox*>(content->FindCtrlById(kIdMgrRightHost));
    if (rootBox == NULL)
    {
        rootBox = dynamic_cast<DuiVBox*>(BodyRoot());
    }
    if (rootBox != NULL)
    {
        std::unique_ptr<DuiToast> toast(new DuiToast());
        m_pToast = toast.get();
        rootBox->AddChild(std::move(toast), DuiLayout::Hint().Fixed(0));
    }

    //发送人名单要查本地库、经主面板解析名字，布局函数里只放了「全部成员」一项
    RefreshSenderCombo();
    //新建的列表从顶部显示；记下这一份，之后的刷新据此判断要不要滚回顶部
    m_shownFilter = m_model.Filter();

    //左栏：搜索范围弹层的提交回调（弹层对象随窗口对象一直存在，注册一次即可，重复注册无害）
    m_scopePopup.SetSubmitCallback(&CBalloonHistoryWindow::OnScopePopupThunk, this);
    m_exportPopup.SetSubmitCallback(&CBalloonHistoryWindow::OnExportPopupThunk, this);
    RebuildManagerList();

    LOG_INFO(_T("历史消息记录窗客户区 UI 初始化完成。"));
}

LRESULT CBalloonHistoryWindow::OnMouseMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& bHandled)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    UpdateRowHover(pt);

    //只旁听、不消费：DuiHost 还要靠这条消息驱动按钮的 hover、滚动条拖动等等。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonHistoryWindow::OnMouseLeaveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/, BOOL& bHandled)
{
    //鼠标离开整个窗口：把悬停态全撤掉，否则会留一行"亮着"的。
    POINT none;
    none.x = -1;
    none.y = -1;
    UpdateRowHover(none);

    bHandled = FALSE;
    return 0;
}

void CBalloonHistoryWindow::UpdateRowHover(POINT pt)
{
    //文件 / 图片页的行另有自己的列表
    if (m_mgrTab != kHistoryManagerTabChat)
    {
        UpdateLibraryRowHover(pt);
        return;
    }
    if (m_pScroll == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* content = m_pScroll->GetContent();
    if (content == NULL)
    {
        return;
    }

    //落点必须同时落在滚动视口内：滚出可视区的行其矩形仍在，只按行矩形判会让
    //视口外的行也亮起来。
    const RECT viewRc = m_pScroll->GetRect();
    const bool inView = (pt.x >= viewRc.left && pt.x < viewRc.right
                         && pt.y >= viewRc.top && pt.y < viewRc.bottom);

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = content->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        HistoryRow* row = dynamic_cast<HistoryRow*>(kids[i].get());
        if (row == NULL)
        {
            //日期分组头等非行控件跳过。
            continue;
        }
        const RECT rc = row->GetRect();
        const bool hit = inView
            && pt.x >= rc.left && pt.x < rc.right
            && pt.y >= rc.top && pt.y < rc.bottom;
        //SetHovered 对"值没变"自己会短路，这里不必再判一次。
        row->SetHovered(hit);
    }
}

// =======================================================================
// 行右键菜单（C2）与图片 / 文件的行内操作（C3 / C4）
//
// 右键与悬停一样<u>按几何位置在窗口层判</u>，不靠控件分发：DuiHost::OnRButtonDown
// 只把事件发给 HitTopMost 命中的那个最深控件，右键点在正文或某枚按钮上时，行容器
// 根本收不到。按落点算"点在哪一行"才不受子控件切换影响。
// =======================================================================

LRESULT CBalloonHistoryWindow::OnRButtonUpMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& bHandled)
{
    //只旁听、不消费 —— 置 FALSE 让 DuiFrameWindow / DuiHost 照常继续处理，
    //否则整个界面的右键交互都会失灵。
    bHandled = FALSE;

    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);

    //文件 / 图片页：右键某一行（某一张）弹出它的菜单，同样延后到鼠标事件解栈之后
    if (m_mgrTab != kHistoryManagerTabChat)
    {
        const int index = LibraryIndexAtPoint(pt);
        if (index >= 0)
        {
            POINT ptScreen = pt;
            ::ClientToScreen(m_hWnd, &ptScreen);
            ::PostMessage(m_hWnd, WM_HISTORY_SHOW_LIB_MENU, (WPARAM)index,
                          MAKELPARAM((WORD)ptScreen.x, (WORD)ptScreen.y));
        }
        return 0;
    }

    const int row = IsImageGridMode() ? GridIndexAtPoint(pt) : RowIndexAtPoint(pt);
    if (row < 0)
    {
        return 0;   //没点在任何一行（或网格里的任何一张缩略图）上：点到了筛选行、分页栏或空白处
    }
    //多选模式下行上的操作都在底部操作栏里，右键不再弹单条菜单，免得两套操作混在一起
    if (m_multiSelect)
    {
        return 0;
    }

    POINT ptScreen = pt;
    ::ClientToScreen(m_hWnd, &ptScreen);

    //TrackPopupMenu 是个模态消息循环，直接在这里跑会与 DuiHost 的鼠标捕获打架
    //（菜单收起之后界面点击失灵）。投递出去，等鼠标事件解栈之后再弹。
    ::PostMessage(m_hWnd, WM_HISTORY_SHOW_ROW_MENU, (WPARAM)row,
                  MAKELPARAM((WORD)ptScreen.x, (WORD)ptScreen.y));
    return 0;
}

LRESULT CBalloonHistoryWindow::OnChatOpenLinkMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM lParam, BOOL& /*bHandled*/)
{
    std::wstring* pUrl = (std::wstring*)lParam;
    if (pUrl == NULL)
    {
        return 0;
    }

    // 协议白名单：只放行 http / https，与两个聊天窗同一口径。
    if (balloonchatwnd::IsSafeLinkUrl(*pUrl))
    {
        ::ShellExecuteW(m_hWnd, L"open", pUrl->c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        LOG_ERROR("消息记录窗拒绝打开非 http/https 链接。");
    }
    delete pUrl;
    return 0;
}

LRESULT CBalloonHistoryWindow::OnShowRowMenuMsg(UINT /*uMsg*/, WPARAM wParam,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    const int row = (int)wParam;
    POINT ptScreen;
    //MAKELPARAM 打包时按 WORD 截断，取出时须按有符号还原 —— 多显示器布局下屏幕坐标
    //可能为负，当成无符号会把菜单弹到几万像素之外。
    ptScreen.x = GET_X_LPARAM(lParam);
    ptScreen.y = GET_Y_LPARAM(lParam);

    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return 0;
    }

    const std::vector<HistoryRowMenuItem> model =
        BuildHistoryRowMenuModel(BuildRowMenuContext(row));
    if (model.empty())
    {
        return 0;
    }

    //右键点在某条正文的选区上：这次菜单里的「复制」只复制选中部分。选区在右键时不会
    //被清掉（正文只处理左键），故此刻判断的就是用户看到的那片高亮。
    {
        POINT ptClient = ptScreen;
        ::ScreenToClient(m_hWnd, &ptClient);
        const HistoryContentView* selected = FindSelectedContentView();
        m_menuCopySelection = (selected != NULL && selected->PointInTextSelection(ptClient));
    }

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return 0;
    }
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].m_isSeparator)
        {
            ::AppendMenu(menu, MF_SEPARATOR, 0, NULL);
        }
        else
        {
            //菜单命令 id 直接用菜单项标识：它们都是从 1 起的小正整数，而
            //TrackPopupMenu 用 0 表示"用户没选"，不会撞。
            ::AppendMenuW(menu, MF_STRING, (UINT_PTR)model[i].m_id,
                          model[i].m_label.c_str());
        }
    }

    //落点必须夹进锚点所在显示器的工作区：列表行靠近窗口底边时，直接把光标坐标交给
    //TrackPopupMenu 会让菜单跑到桌面外看不见（开发约定文档的菜单不越屏规则）。
    //Win32 的 HMENU 在弹出前拿不到精确尺寸，按行数估算即可 —— 估大一点只会让靠边时
    //更早翻向左 / 上，不会误伤（与 ChatBubble 的做法一致）。
    SIZE menuSize;
    menuSize.cx = kRowMenuEstWidth;
    menuSize.cy = (LONG)model.size() * kRowMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(ptScreen, menuSize);

    ::SetForegroundWindow(m_hWnd);   //菜单要靠前台窗口才能在点击别处时正常收起
    const int cmd = ::TrackPopupMenu(menu,
                                     TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                     at.x, at.y, 0, m_hWnd, NULL);
    ::DestroyMenu(menu);

    if (cmd != 0)
    {
        ExecuteRowMenuCommand(cmd, row);
    }
    return 0;
}

bool CBalloonHistoryWindow::IsImageGridMode() const
{
    //时间轴模式下图片也按时间轴样式逐条列出，不排成网格
    return m_model.Filter().m_type == kHistoryTypeImage && !m_multiSelect && !m_timelineMode;
}

int CBalloonHistoryWindow::GridIndexAtPoint(POINT pt) const
{
    if (m_pScroll == NULL)
    {
        return -1;
    }
    balloonwjui::DuiControl* content = m_pScroll->GetContent();
    if (content == NULL)
    {
        return -1;
    }
    //与 RowIndexAtPoint 同一处理：落点须在滚动视口之内，滚出可视区的缩略图矩形仍在
    const RECT viewRc = m_pScroll->GetRect();
    if (!::PtInRect(&viewRc, pt))
    {
        return -1;
    }
    //逐格比对，命中的格子按 m_gridCells 换算回记录下标（帖子卡片的几格都对应同一条记录）
    for (size_t i = 0; i < m_gridCells.size(); ++i)
    {
        const balloonwjui::DuiControl* tile = content->FindCtrlById((UINT)(kIdHistoryGridBase + i));
        if (tile != NULL)
        {
            const RECT rc = tile->GetRect();
            if (::PtInRect(&rc, pt))
            {
                return m_gridCells[i].m_row;
            }
        }
    }
    return -1;
}

std::vector<int> CBalloonHistoryWindow::PendingFileDownloadRows() const
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    std::vector<HistoryFileRowState> states;
    states.reserve(recs.size());
    for (size_t i = 0; i < recs.size(); ++i)
    {
        HistoryFileRowState st;
        st.m_msgid = recs[i].m_msgid;
        st.m_isFile = IsHistoryRowFileType(recs[i].m_msgtype);
        if (st.m_isFile)
        {
            CString local;
            st.m_hasLocal = ResolveRecordLocalFile(recs[i], local);
            st.m_downloading = (m_downloadingMsgids.find(recs[i].m_msgid) != m_downloadingMsgids.end());
        }
        states.push_back(st);
    }
    //按 msgid 挑出来再换回行下标：同一页里 msgid 不重复
    const std::vector<int64_t> picked = PickFilesToDownload(states);
    std::vector<int> rows;
    for (size_t k = 0; k < picked.size(); ++k)
    {
        for (size_t i = 0; i < recs.size(); ++i)
        {
            if (recs[i].m_msgid == picked[k])
            {
                rows.push_back((int)i);
                break;
            }
        }
    }
    return rows;
}

void CBalloonHistoryWindow::DownloadAllFilesOnPage()
{
    const std::vector<int> rows = PendingFileDownloadRows();
    LOG_INFO("history window: download all files on page, %u files.", (unsigned)rows.size());
    const std::vector<HistoryRecord>& recs = m_model.Records();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        //与菜单「下载」同口径：只把文件拿到本地，下完不打开。逐笔发起时不刷新列表，
        //否则一页几十个文件就要连续重建几十次。发起成功的记入本批，失败时不逐条弹框
        if (StartRowDownload(rows[i], false, false))
        {
            m_batchDownload.Add(recs[(size_t)rows[i]].m_msgid);
        }
    }
    if (!rows.empty())
    {
        //发起下载不改变消息总数，只需重建当前页：「全部下载」按钮随之消失，各行显示下载中
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
        CString tip;
        tip.Format(TR(L"开始下载 %u 个文件"), (unsigned)rows.size());
        ShowToast(tip);
    }
}

int CBalloonHistoryWindow::RowIndexAtPoint(POINT pt) const
{
    if (m_pScroll == NULL)
    {
        return -1;
    }
    balloonwjui::DuiControl* content = m_pScroll->GetContent();
    if (content == NULL)
    {
        return -1;
    }

    //落点必须同时落在滚动视口内：滚出可视区的行其矩形仍在，只按行矩形判会把视口外的
    //行也算命中（与 UpdateRowHover 同一处理）。
    const RECT viewRc = m_pScroll->GetRect();
    if (pt.x < viewRc.left || pt.x >= viewRc.right
        || pt.y < viewRc.top || pt.y >= viewRc.bottom)
    {
        return -1;
    }

    //行下标由 ctrlId 反算（id = kIdHistoryRowBase + 行下标），<u>不能</u>按"第几个
    //HistoryRow 子控件"数 —— 中间还夹着日期分组头。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = content->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const HistoryRow* rowCtrl = dynamic_cast<const HistoryRow*>(kids[i].get());
        if (rowCtrl == NULL)
        {
            continue;
        }
        const RECT rc = rowCtrl->GetRect();
        if (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom)
        {
            const UINT id = rowCtrl->GetCtrlId();
            if (id >= (UINT)kIdHistoryRowBase
                && id < (UINT)kIdHistoryRowBase + kIdHistoryRowCapacity)
            {
                return (int)(id - (UINT)kIdHistoryRowBase);
            }
            return -1;
        }
    }
    return -1;
}

int64_t CBalloonHistoryWindow::SelfUserId() const
{
    //缓存与下载目录都按登录账号隔离，取不到 userid 就什么都算不出来
    //（调试入口没有登录态，此时一律当作"本地没有文件"处理）。
    if (m_lpFMGClient == NULL)
    {
        return 0;
    }
    return (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
}

bool CBalloonHistoryWindow::ResolveRowLocalFile(int row, CString& outPath) const
{
    outPath.Empty();

    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return false;
    }
    return ResolveRecordLocalFile(recs[(size_t)row], outPath);
}

bool CBalloonHistoryWindow::ResolveRecordLocalFile(const HistoryRecord& rec, CString& outPath) const
{
    outPath.Empty();

    //缓存与下载目录都按登录账号隔离，调试入口没有登录态，一律当作"本地没有文件"
    if (SelfUserId() <= 0)
    {
        return false;
    }
    //查找规则与主面板搜索下拉共用，见 HistoryLocalFile.h
    std::wstring path;
    if (!ResolveRecordLocalPath(rec, path))
    {
        return false;
    }
    outPath = path.c_str();
    return true;
}

HistoryRowMenuContext CBalloonHistoryWindow::BuildRowMenuContext(int row) const
{
    HistoryRowMenuContext ctx;

    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return ctx;   //空上下文：菜单只会剩通用项
    }
    const HistoryRecord& rec = recs[(size_t)row];

    ctx.m_msgType = rec.m_msgtype;
    ctx.m_pinned  = rec.m_pinned;
    ctx.m_msgId   = rec.m_msgid;
    //按实际交给本地库的条件判断：时间轴模式下关键字只高亮、不筛选，列表本来就保留着上下文
    ctx.m_canViewContext = ShouldOfferViewContext(m_model.QueryFilter(), rec.m_msgid);
    //清空要经主面板发请求：调试入口没有主面板，不给这一项
    ctx.m_canClearConversation = (m_pMainPanel != NULL);
    ctx.m_downloading =
        (m_downloadingMsgids.find(rec.m_msgid) != m_downloadingMsgids.end());

    CString local;
    ctx.m_hasLocalFile = ResolveRowLocalFile(row, local);
    return ctx;
}

void CBalloonHistoryWindow::ExecuteRowMenuCommand(int menuId, int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];

    //任何菜单命令都算"下一次操作"，先撤掉定位高亮；「查看上下文」会在下面重新设置
    ClearLocatedRow();

    CString localPath;
    const bool hasLocal = ResolveRowLocalFile(row, localPath);

    switch (menuId)
    {
    case kHistoryRowMenuCopy:
        CopyRowText(row);
        break;

    case kHistoryRowMenuCopyImage:
        if (hasLocal)
        {
            if (!flamingo::CopyImageFileToClipboard((LPCWSTR)localPath))
            {
                ::MessageBox(m_hWnd, TR(L"复制图片失败，可能文件已被移动或删除。"),
                             TR(L"消息记录"), MB_OK | MB_ICONWARNING);
            }
        }
        break;

    case kHistoryRowMenuSaveImageAs:
        if (hasLocal)
        {
            //默认文件名与聊天窗里那条路同口径（按 msgid + 原图扩展名给一个可读名字）。
            balloonchatwnd::SaveChatImageAs(
                m_hWnd, std::wstring((LPCWSTR)localPath),
                balloonchatwnd::MakeImageSaveAsDefaultName(
                    std::wstring((LPCWSTR)localPath), (long long)rec.m_msgid));
        }
        break;

    case kHistoryRowMenuOpenFile:
        OpenRowFile(localPath, hasLocal);
        break;

    case kHistoryRowMenuOpenFolder:
        OpenRowFolder(localPath, hasLocal);
        break;

    //文件夹传输记录：打开那个目录<u>本身</u>，而不是像文件那样打开父目录再选中它。
    //路径必须带引号，否则含空格的路径会被 explorer 拆成多个参数。
    case kHistoryRowMenuOpenFolderRoot:
        if (hasLocal && !localPath.IsEmpty())
        {
            CString folderParam;
            folderParam.Format(_T("\"%s\""), (LPCTSTR)localPath);
            ::ShellExecute(m_hWnd, _T("open"), _T("explorer.exe"), folderParam,
                           NULL, SW_SHOWNORMAL);
        }
        break;

    case kHistoryRowMenuSaveFileAs:
        if (hasLocal)
        {
            SaveRowFileAs(row, localPath);
        }
        break;

    case kHistoryRowMenuDownload:
        //从菜单点「下载」纯粹是把文件拿到本地，下完<u>不</u>弹看图窗 ——
        //那是"双击缩略图"才有的意图。
        StartRowDownload(row, false);
        break;

    case kHistoryRowMenuCancelDownload:
        CancelRowDownload(row);
        break;

    case kHistoryRowMenuLocate:
        LocateRow(row);
        break;

    //查看上下文：清掉筛选，在本窗口里翻到这一行所在的页并高亮它
    case kHistoryRowMenuViewContext:
        ViewRowContext(row);
        break;

    //多选：进入多选模式并预先选中这一行
    case kHistoryRowMenuMultiSelect:
        EnterMultiSelect(row);
        break;

    case kHistoryRowMenuPin:
        //与行内那枚 Pin 按钮完全同口径：只发请求，界面等服务端推送回来再变，
        //不做乐观更新。无权取消时由服务端拒绝并提示。
        if (m_pMainPanel != NULL && rec.m_msgid > 0)
        {
            const HistoryFilter& filter = m_model.Filter();
            CIUSocket::GetInstance().PinMessage(filter.m_ctype, filter.m_cid,
                                                (long long)rec.m_msgid, !rec.m_pinned);
        }
        break;

    //转发这一条：打开主面板的目标选择窗，按逐条转发发出
    case kHistoryRowMenuForward:
        ForwardRow(row);
        break;

    //清空整个会话：与底部「清空聊天记录」按钮同一条路径
    case kHistoryRowMenuClearConversation:
        RequestClearConversation();
        break;

    //删除这一条（本人视角）：先确认，再发 cmd_delete_message。与 Pin 同口径只发请求，
    //本地库删行、本窗刷新都等服务端推 cmd_messages_deleted_notify 回来再做。
    //调试入口（没有主面板）下没有服务端可问，不响应
    case kHistoryRowMenuDelete:
        if (m_pMainPanel != NULL && rec.m_msgid > 0)
        {
            const int answer = ::MessageBox(m_hWnd,
                balloonchat::BuildDeleteMessagesConfirmText(1),
                TR(L"删除消息"), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (answer == IDOK)
            {
                CIUSocket::GetInstance().DeleteMessage((int64_t)rec.m_msgid);
                LOG_INFO("History window requests delete message, msgid=%lld.",
                         (long long)rec.m_msgid);
            }
        }
        break;

    default:
        break;
    }
}

void CBalloonHistoryWindow::CopyRowText(int row)
{
    //弹菜单时算好的"复制选区"标记只对这一次菜单有效，读出来就复位。
    const bool copySelection = m_menuCopySelection;
    m_menuCopySelection = false;

    //右键点在选区上：只复制选中部分。
    if (copySelection)
    {
        const HistoryContentView* selected = FindSelectedContentView();
        if (selected != NULL)
        {
            const std::vector<balloonchatwnd::ChatClipSegment> segs =
                selected->GetSelectedClipSegments();
            const bool ok = balloonchatwnd::WriteChatContentToClipboard(m_hWnd, segs);
            LOG_INFO("history window: copied selection by menu, %d segment(s), ok=%d.",
                     (int)segs.size(), (int)ok);
            return;
        }
    }

    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }

    const CString full =
        Utf8ToCString(FullTextFromContent(recs[(size_t)row].m_msgtype,
                                          recs[(size_t)row].m_content));
    if (full.IsEmpty())
    {
        return;
    }

    //整条正文按聊天内容格式写剪贴板，与聊天窗复制同口径：同时写纯文字（表情写成
    //「[表情]」，粘到记事本等处读得懂）与本程序自己的格式（粘回聊天输入框时表情能还原）。
    const std::vector<balloonchatwnd::ChatClipSegment> segs =
        balloonchatwnd::BuildClipSegmentsFromBody(full);
    const bool ok = balloonchatwnd::WriteChatContentToClipboard(m_hWnd, segs);
    LOG_INFO("history window: copied whole message by menu, %d segment(s), ok=%d.",
             (int)segs.size(), (int)ok);
}

LRESULT CBalloonHistoryWindow::OnLButtonDownMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& bHandled)
{
    //只旁听：记下落点是否在列表视口里，然后照常交给 DuiHost 分发。
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    m_lastPressInList = false;
    //停在文件 / 图片页时看的是那一页的列表视口（聊天记录页隐藏着，它的视口矩形是隐藏前的旧值）
    DuiControl* list = m_pScroll;
    if (m_mgrTab != kHistoryManagerTabChat)
    {
        DuiControl* content = GetClientContent();
        DuiControl* lib = (content != NULL) ? content->FindCtrlById(kIdLibBody) : NULL;
        list = (lib != NULL) ? lib->FindCtrlById(kIdLibScroll) : NULL;
    }
    if (list != NULL)
    {
        const RECT rc = list->GetRect();
        m_lastPressInList = (::PtInRect(&rc, pt) != FALSE);
    }
    //时间轴导航条：按在某个年标题行或月份行上时，等鼠标事件处理完再展开 / 收起或跳转（处理时会重建导航条）
    if (m_timelineMode && m_mgrTab == kHistoryManagerTabChat)
    {
        HandleTimelineNavClick(pt);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonHistoryWindow::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam,
                                            LPARAM /*lParam*/, BOOL& bHandled)
{
    //GetKeyState 返回值的最高位：置位表示该键此刻正被按住。
    const SHORT kKeyHeldBit = (SHORT)0x8000;

    const bool ctrlDown = (::GetKeyState(VK_CONTROL) & kKeyHeldBit) != 0;
    const bool altDown  = (::GetKeyState(VK_MENU) & kKeyHeldBit) != 0;
    const bool shiftDown = (::GetKeyState(VK_SHIFT) & kKeyHeldBit) != 0;

    //一、快捷键（Esc / PageUp / PageDown / Ctrl+Home / Ctrl+End / Ctrl+F / 搜索框里的回车 / F3 / Shift+F3），
    //    判定规则见 HistoryWindowKeys.h。返回"不处理"时继续往下判断 Ctrl+C
    DuiControl* content = GetClientContent();
    DuiEdit* search = (content != NULL)
                    ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdHistorySearch)) : NULL;
    //文件 / 图片页：快捷键对着那一页的文件名搜索框与分页（图片页没有搜索框）
    if (m_mgrTab != kHistoryManagerTabChat)
    {
        DuiControl* lib = (content != NULL) ? content->FindCtrlById(kIdLibBody) : NULL;
        search = NULL;
        if (lib != NULL && m_libModel.Filter().m_kind == kHistoryLibraryFiles)
        {
            search = dynamic_cast<DuiEdit*>(lib->FindCtrlById(kIdLibSearch));
        }
    }

    //焦点在左栏搜索框时：Esc（框里有字）清空左栏搜索、回车立即搜索，Ctrl+F 照常转到右侧搜索框；
    //其余按键都交给左栏搜索框，不当作右侧的翻页快捷键。框里没字时 Esc 照常关窗
    DuiEdit* mgrSearch = (content != NULL)
                       ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdMgrSearch)) : NULL;
    if (mgrSearch != NULL && GetDuiFocus() == mgrSearch && !m_lastPressInList)
    {
        if (wParam == VK_ESCAPE && !mgrSearch->GetText().IsEmpty())
        {
            mgrSearch->SetTextNoNotify(_T(""));
            ApplyManagerSearch();
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            ApplyManagerSearch();
            return 0;
        }
        const bool focusRightSearch = (wParam == 'F' && ctrlDown && !altDown);
        if (wParam != VK_ESCAPE && !focusRightSearch)
        {
            bHandled = FALSE;
            return 0;
        }
    }

    //"焦点在搜索框"按用户最近一次点在哪儿来算：点列表里的消息行时，DuiHost 只在点中可接收
    //焦点的控件时才转移焦点（DuiHost::OnLButtonDown 的 IsTabStop 判断），消息行不接收焦点，
    //DUI 焦点会一直留在窗口打开时就获得焦点的搜索框上。只看 DUI 焦点的话，Ctrl+Home / Ctrl+End
    //永远被当成输入框的按键，翻不了页
    const bool searchFocused = (search != NULL && GetDuiFocus() == search && !m_lastPressInList);
    const bool searchHasText = (search != NULL && !search->GetText().IsEmpty());
    const int action = DecideHistoryKey((UINT)wParam, ctrlDown, altDown, shiftDown, searchFocused, searchHasText);
    if (action != kHistoryKeyNone)
    {
        ExecuteKeyAction(action, search);
        return 0;
    }

    //二、Ctrl+C（不带 Alt）复制列表里的选区
    if (wParam != 'C' || !ctrlDown || altDown || !m_lastPressInList)
    {
        bHandled = FALSE;
        return 0;
    }

    const HistoryContentView* selected = FindSelectedContentView();
    if (selected == NULL)
    {
        bHandled = FALSE;   //列表里没有选区：交给获得焦点的控件，不吞按键
        return 0;
    }

    const std::vector<balloonchatwnd::ChatClipSegment> segs = selected->GetSelectedClipSegments();
    const bool ok = balloonchatwnd::WriteChatContentToClipboard(m_hWnd, segs);
    LOG_INFO("history window: copied selection by Ctrl+C, %d segment(s), ok=%d.",
             (int)segs.size(), (int)ok);
    return 0;
}

void CBalloonHistoryWindow::ExecuteKeyAction(int action, DuiEdit* search)
{
    //任何快捷键操作都算"下一次操作"，撤掉定位高亮
    ClearLocatedRow();
    //文件 / 图片页：翻页对着那一页的模型，搜索对着文件名搜索框；关窗与聚焦搜索框与聊天记录页相同
    if (m_mgrTab != kHistoryManagerTabChat)
    {
        int target = 0;
        switch (action)
        {
        //PageUp：上一页
        case kHistoryKeyPrevPage:
            target = m_libModel.CurrentPage() - 1;
            break;
        //PageDown：下一页
        case kHistoryKeyNextPage:
            target = m_libModel.CurrentPage() + 1;
            break;
        //Ctrl+Home：第一页
        case kHistoryKeyFirstPage:
            target = 1;
            break;
        //Ctrl+End：最后一页
        case kHistoryKeyLastPage:
            target = m_libModel.PageCount();
            break;
        //Esc（文件名搜索框有焦点且有字）：清空并立即按空关键字重查
        case kHistoryKeyClearSearch:
            if (search != NULL)
            {
                search->SetTextNoNotify(_T(""));
            }
            ApplyLibrarySearch();
            return;
        //文件名搜索框里按回车：立即搜索
        case kHistoryKeySearchNow:
            ApplyLibrarySearch();
            return;
        //关窗、聚焦搜索框：与聊天记录页相同，交给下面的通用处理
        default:
            break;
        }
        if (target > 0)
        {
            if (target <= m_libModel.PageCount() && target != m_libModel.CurrentPage())
            {
                m_libModel.GoToPage(target);
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
            }
            return;
        }
        if (action != kHistoryKeyClose && action != kHistoryKeyFocusSearch)
        {
            return;
        }
    }
    switch (action)
    {
    //Esc（搜索框没有焦点或框里没字）：关闭窗口，与点标题栏关闭按钮相同
    case kHistoryKeyClose:
        PostMessage(WM_CLOSE);
        break;

    //Esc（搜索框有焦点且有字）：清空搜索框并立即按空关键字重查
    case kHistoryKeyClearSearch:
        if (search != NULL)
        {
            search->SetText(_T(""));
        }
        ApplySearchKeyword();
        break;

    //PageUp / PageDown：上一页 / 下一页（页号越界由模型夹取）
    case kHistoryKeyPrevPage:
        GoToPageAndRefresh(m_model.CurrentPage() - 1);
        break;

    case kHistoryKeyNextPage:
        GoToPageAndRefresh(m_model.CurrentPage() + 1);
        break;

    //Ctrl+Home / Ctrl+End：第一页 / 最后一页
    case kHistoryKeyFirstPage:
        GoToPageAndRefresh(1);
        break;

    case kHistoryKeyLastPage:
        GoToPageAndRefresh(m_model.PageCount());
        break;

    //Ctrl+F：焦点移到搜索框并全选，接着打字即替换原来的关键字
    case kHistoryKeyFocusSearch:
        if (search != NULL)
        {
            SetDuiFocus(search);
            search->SelectAll();
            search->Invalidate();
            //焦点按"最近一次点在哪儿"来算（见 OnKeyDownMsg），这里等同于点了一下搜索框
            m_lastPressInList = false;
        }
        break;

    //搜索框里按回车：不等输入停顿，立即按当前文字搜索
    case kHistoryKeySearchNow:
        ApplySearchKeyword();
        break;

    //F3：时间轴模式下跳到下一处命中（列表模式下不处理）
    case kHistoryKeyNextHit:
    //Shift+F3：时间轴模式下跳到上一处命中
    case kHistoryKeyPrevHit:
        if (m_timelineMode)
        {
            GoToHit(action == kHistoryKeyNextHit);
        }
        break;

    //不处理的按键在调用方就已放行，走不到这里
    default:
        break;
    }
    LOG_INFO("history window: key action %d.", action);
}

void CBalloonHistoryWindow::ApplySearchKeyword()
{
    //立即查询时（回车、清空搜索、延迟到点）把还在计时的那次取消，免得同一个关键字再查一遍
    KillTimer(kSearchDebounceTimerId);

    DuiControl* content = GetClientContent();
    DuiEdit* search = (content != NULL)
                    ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdHistorySearch)) : NULL;
    if (search == NULL)
    {
        return;
    }
    m_model.SetKeyword(CStringToUtf8(search->GetText()));
    SyncSyntaxFromIds();
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::GoToPageAndRefresh(int page)
{
    m_model.GoToPage(page);
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

//在控件树里递归找第一个带选区的正文控件。
static HistoryContentView* FindSelectedContentViewIn(DuiControl* node)
{
    if (node == NULL)
    {
        return NULL;
    }
    HistoryContentView* view = dynamic_cast<HistoryContentView*>(node);
    if (view != NULL && view->HasTextSelection())
    {
        return view;
    }
    const std::vector<std::unique_ptr<DuiControl>>& kids = node->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        HistoryContentView* found = FindSelectedContentViewIn(kids[i].get());
        if (found != NULL)
        {
            return found;
        }
    }
    return NULL;
}

HistoryContentView* CBalloonHistoryWindow::FindSelectedContentView() const
{
    //只在列表视口里找：选区只会出现在消息行的正文里。
    return FindSelectedContentViewIn(m_pScroll);
}

void CBalloonHistoryWindow::RequestClearConversation()
{
    //调试入口没有主面板；左栏一个会话都没有时右侧没有选中会话（cid 为 0），都无从清空
    if (m_pMainPanel == NULL || m_model.Filter().m_cid == 0)
    {
        return;
    }
    const int ctype = m_model.Filter().m_ctype;
    const int64_t cid = m_model.Filter().m_cid;
    const bool sent = m_pMainPanel->RequestClearConversation(ctype, cid, m_hWnd);
    LOG_INFO("history window: clear conversation requested, ctype=%d, cid=%lld, sent=%d.",
             ctype, (long long)cid, sent ? 1 : 0);
}

void CBalloonHistoryWindow::OpenRowFile(const CString& localPath, bool hasLocal)
{
    if (!hasLocal || localPath.IsEmpty())
    {
        return;
    }
    //ShellExecute 返回值 <= 32 表示失败（没有关联程序、文件刚被删掉等）。
    const INT_PTR kShellExecOkThreshold = 32;
    HINSTANCE hr = ::ShellExecute(m_hWnd, _T("open"), localPath, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hr <= kShellExecOkThreshold)
    {
        ::MessageBox(m_hWnd, TR(L"打开文件失败，可能没有关联的打开方式。"),
                     TR(L"消息记录"), MB_OK | MB_ICONWARNING);
    }
}

void CBalloonHistoryWindow::OpenRowFolder(const CString& localPath, bool hasLocal)
{
    if (!hasLocal || localPath.IsEmpty())
    {
        return;
    }
    //资源管理器定位并选中该文件。路径<u>必须带引号</u> —— 不带的话含空格的路径会被
    //explorer 拆成多个参数，结果打开的是"我的文档"而不是定位到文件。
    CString param;
    param.Format(_T("/select,\"%s\""), (LPCTSTR)localPath);
    ::ShellExecute(m_hWnd, _T("open"), _T("explorer.exe"), param, NULL, SW_SHOWNORMAL);
}

void CBalloonHistoryWindow::SaveRowFileAs(int row, const CString& localPath)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryContentBrief brief =
        ParseContentBrief(recs[(size_t)row].m_msgtype, recs[(size_t)row].m_content);
    SaveLocalFileAs(Utf8ToCString(brief.m_fileName), localPath);
}

void CBalloonHistoryWindow::SaveLocalFileAs(const CString& fileName, const CString& localPath)
{
    if (localPath.IsEmpty())
    {
        return;
    }
    CString defName = fileName;
    if (defName.IsEmpty())
    {
        defName = TR(L"文件");
    }

    //默认扩展名取原文件名的后缀，使另存出来的文件仍能被正确打开。
    CString defExt;
    const int dot = defName.ReverseFind(_T('.'));
    if (dot > 0 && dot + 1 < defName.GetLength())
    {
        defExt = defName.Mid(dot + 1);
    }

    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"所有文件 (*.*)"), L"*.*");
    CFileDialog dlg(FALSE, defExt.IsEmpty() ? NULL : (LPCTSTR)defExt, defName,
                    OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY,
                    fileFilter.c_str(), m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return;
    }

    //整字节拷贝，不做任何转换 —— 另存出来的必须与原文件逐字节一致。
    if (!::CopyFile(localPath, dlg.m_szFileName, FALSE))
    {
        ::MessageBox(m_hWnd, TR(L"另存文件失败，请检查目标位置是否可写。"),
                     TR(L"消息记录"), MB_OK | MB_ICONWARNING);
        return;
    }
    LOG_INFO(_T("消息记录：文件另存为完成。"));
}

bool CBalloonHistoryWindow::StartRowDownload(int row, bool viewAfter, bool refreshAfter)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return false;
    }
    return StartRecordDownload(recs[(size_t)row], viewAfter, refreshAfter);
}

bool CBalloonHistoryWindow::StartRecordDownload(const HistoryRecord& rec, bool viewAfter, bool refreshAfter)
{
    //调试入口用的是预置假库、没有真实连接，发出去也没意义。
    if (m_pMainPanel == NULL)
    {
        return false;
    }
    const int64_t selfId = SelfUserId();
    if (selfId <= 0 || rec.m_msgid <= 0)
    {
        return false;
    }

    //同一条已经在下载：不重复发起。双击缩略图时，第一下单击会先发起一笔"只下载"的
    //下载，紧接着的双击走到这里时只需补记"下完打开看图窗"。
    if (m_downloadingMsgids.find(rec.m_msgid) != m_downloadingMsgids.end())
    {
        if (viewAfter)
        {
            m_pendingViewMsgid = rec.m_msgid;
        }
        LOG_INFO("history window: download already in progress, msgid=%lld, viewAfter=%d.",
                 (long long)rec.m_msgid, (int)viewAfter);
        return false;
    }

    const HistoryContentBrief brief = ParseContentBrief(rec.m_msgtype, rec.m_content);

    if (IsHistoryRowImageType(rec.m_msgtype))
    {
        if (brief.m_imageFileId.empty())
        {
            //没有 fileid 就无从下载（老消息缺字段等）。告诉用户，别让他一直等。
            ::MessageBox(m_hWnd, TR(L"这条消息缺少图片信息，无法下载原图。"),
                         TR(L"消息记录"), MB_OK | MB_ICONWARNING);
            return false;
        }
        net::ImageTransfer::GetInstance().EnqueueDownload(
            (int64_t)rec.m_msgid, brief.m_imageFileId, brief.m_imageMd5,
            brief.m_imageSize, net::CurrentUserAccount());
    }
    else if (IsHistoryRowFileType(rec.m_msgtype))
    {
        if (brief.m_fileId.empty())
        {
            ::MessageBox(m_hWnd, TR(L"这条消息缺少文件信息，无法下载。"),
                         TR(L"消息记录"), MB_OK | MB_ICONWARNING);
            return false;
        }
        net::FileTransfer::GetInstance().EnqueueDownload(
            (int64_t)rec.m_msgid, brief.m_fileId, brief.m_fileName,
            brief.m_fileMd5, brief.m_fileSize, net::CurrentUserAccount());
    }
    else
    {
        return false;   //其余类型没有可下载的东西
    }

    m_downloadingMsgids.insert(rec.m_msgid);
    //重新发起下载：之前的失败记录作废，缩略图从破图换成"下载中"。
    m_failedImageMsgids.erase(rec.m_msgid);
    if (viewAfter)
    {
        m_pendingViewMsgid = rec.m_msgid;
    }
    LOG_INFO("history window: download started, msgid=%lld, viewAfter=%d.",
             (long long)rec.m_msgid, (int)viewAfter);

    //菜单项要从「下载」变成「取消下载」，刷一次列表。
    if (refreshAfter)
    {
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
    }
    return true;
}

void CBalloonHistoryWindow::AbandonBatchDownload(const char* reason)
{
    const std::vector<int64_t> unfinished = m_batchDownload.Clear();
    if (unfinished.empty())
    {
        return;
    }
    //这些下载仍在后台继续，文件照常落到本地；本窗口只是不再跟踪，结果回来时直接忽略
    for (size_t i = 0; i < unfinished.size(); ++i)
    {
        m_downloadingMsgids.erase(unfinished[i]);
    }
    LOG_INFO("history window: batch download abandoned (%s), %u unfinished downloads no longer tracked.",
             reason, (unsigned)unfinished.size());
}

void CBalloonHistoryWindow::CancelRowDownload(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    CancelRecordDownload(recs[(size_t)row]);
}

void CBalloonHistoryWindow::CancelRecordDownload(const HistoryRecord& rec)
{
    if (IsHistoryRowFileType(rec.m_msgtype))
    {
        net::FileTransfer::GetInstance().CancelDownload((int64_t)rec.m_msgid);
    }
    //图片下载没有取消接口（net::ImageTransfer 只提供 EnqueueDownload）。图片通常只有
    //几百 KB、几秒就完，为它单开一条取消链路不划算；这里只把本窗口的跟踪状态撤掉，
    //传输仍会跑完并落进缓存 —— 那份缓存下次看图时还用得上，不算浪费。
    m_downloadingMsgids.erase(rec.m_msgid);
    if (m_pendingViewMsgid == rec.m_msgid)
    {
        m_pendingViewMsgid = 0;   //撤掉"下完自动看图"的待办
    }
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::OpenRowMergedRecord(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];
    if (rec.m_msgtype != ::message_type_merged)
    {
        return;
    }
    const HistoryContentBrief brief = ParseContentBrief(rec.m_msgtype, rec.m_content);
    if (brief.m_mergedContent.empty())
    {
        LOG_ERROR("history window: merged record has no content, msgid=%lld.",
                  (long long)rec.m_msgid);
        return;
    }
    //主面板持有全部查看窗并负责把下载结果转给它们，查看窗只能由它打开；调试入口没有主面板
    if (m_pMainPanel == NULL || !m_pMainPanel->IsWindow())
    {
        LOG_INFO("history window cannot open merged record viewer: no main panel.");
        return;
    }
    std::string* pContent = new std::string(brief.m_mergedContent);
    if (!::PostMessage(m_pMainPanel->m_hWnd, FMG_MSG_OPEN_MERGED_VIEWER, 0, (LPARAM)pContent))
    {
        delete pContent;
        return;
    }
    LOG_INFO("history window requests merged record viewer, msgid=%lld.", (long long)rec.m_msgid);
}

void CBalloonHistoryWindow::ForwardRow(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    ForwardRecord(recs[(size_t)row]);
}

void CBalloonHistoryWindow::ForwardRecord(const HistoryRecord& rec)
{
    if (!CanForwardHistoryRow(rec.m_msgtype, rec.m_msgid))
    {
        LOG_INFO("history window: row cannot be forwarded, msgid=%lld, msgtype=%d.",
                 (long long)rec.m_msgid, rec.m_msgtype);
        return;
    }
    //目标选择窗与转发调度都在主面板，调试入口没有主面板
    if (m_pMainPanel == NULL || !m_pMainPanel->IsWindow())
    {
        LOG_INFO("history window cannot forward: no main panel.");
        return;
    }

    //只有一条消息，按逐条转发发出：对方看到的就是原消息本身，而不是一张合并卡片。
    //m_srcWnd 填本窗口，主面板据此在本窗口上显示转发结果
    //在线直传的文件（2026-10-05 起）：服务器上没有副本，按消息转发出去对方打不开。本机有文件就以离线文件重新发送，
    //没有就提示无法转发
    std::wstring directPath;
    const bool direct = IsDirectFileRecord(rec, directPath);
    if (direct && directPath.empty())
    {
        LOG_INFO("history window: direct file without local copy cannot be forwarded, msgid=%lld.",
                 (long long)rec.m_msgid);
        ShowToast(TR(L"在线直传的文件无法转发"));
        return;
    }

    balloonmain::ForwardRequest* pReq = new balloonmain::ForwardRequest();
    pReq->m_srcCtype = rec.m_ctype;
    pReq->m_srcCid = rec.m_cid;
    pReq->m_srcWnd = m_hWnd;
    pReq->m_merge = false;
    if (direct)
    {
        pReq->m_reuploadFiles.push_back(directPath);
    }
    else
    {
        pReq->m_msgids.push_back(rec.m_msgid);
    }
    if (!::PostMessage(m_pMainPanel->m_hWnd, FMG_MSG_OPEN_FORWARD_PICKER, 0, (LPARAM)pReq))
    {
        delete pReq;
        LOG_ERROR("history window: failed to post forward request, msgid=%lld.",
                  (long long)rec.m_msgid);
        return;
    }
    LOG_INFO("history window requests forward, ctype=%d, cid=%lld, msgid=%lld, reupload direct file=%d.",
             rec.m_ctype, (long long)rec.m_cid, (long long)rec.m_msgid, direct ? 1 : 0);
}

void CBalloonHistoryWindow::ShowToast(LPCTSTR text)
{
    if (!IsWindow() || m_pToast == NULL || text == NULL)
    {
        return;
    }
    m_pToast->Show(text);
}

void CBalloonHistoryWindow::ViewRowImage(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    ViewRecordImage(recs[(size_t)row]);
}

void CBalloonHistoryWindow::ViewRecordImage(const HistoryRecord& rec)
{
    CString localPath;
    if (ResolveRecordLocalFile(rec, localPath))
    {
        //本地已有原图：直接开看图窗（进程内共享一个实例，已开则换图置前）。
        CBalloonImageViewerWindow::OpenShared(m_hWnd, localPath);
        return;
    }

    //本地没有：发起下载，下完自动开看图窗。双击的意图就是"我要看这张图"，
    //下载只是达成它的手段，故这里 viewAfter 传 true。
    StartRecordDownload(rec, true, true);
}

void CBalloonHistoryWindow::DownloadRowThumb(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];
    if (!IsHistoryRowImageType(rec.m_msgtype))
    {
        return;
    }

    //按当前状态重新判定能否下载：建列表之后状态可能已经变了（例如另一笔下载刚好完成）。
    CString localPath;
    if (ResolveRowLocalFile(row, localPath))
    {
        return;   //本地已有原图，缩略图下一次刷新就会显示出来
    }
    if (m_downloadingMsgids.find(rec.m_msgid) != m_downloadingMsgids.end())
    {
        return;   //已在下载
    }
    const HistoryContentBrief brief = ParseContentBrief(rec.m_msgtype, rec.m_content);
    if (brief.m_imageFileId.empty() || brief.m_imageMd5.empty())
    {
        return;   //缺少下载信息，缩略图本就不可点击；走到这里说明控件上的状态已过期
    }

    //单击只是"把原图下下来"，不打开看图窗；想看大图请双击这一行。
    LOG_INFO("history window: thumbnail click starts image download, msgid=%lld.",
             (long long)rec.m_msgid);
    StartRowDownload(row, false);
}

namespace {

// 调试入口里图片行轮流套用的几种缩略图状态，按"调用 ResolveHistoryThumb 的入参"给出，
// 与真实情况走同一套判断。每一项依次是：可以下载、本地已有原图、正在下载、下载失败过。
struct DebugThumbInputs
{
    bool m_downloadable;
    bool m_fileExists;
    bool m_downloading;
    bool m_downloadFailed;
};

const DebugThumbInputs kDebugThumbInputs[] =
{
    { true,  false, false, false },   // 需要下载
    { true,  false, true,  false },   // 下载中
    { true,  false, false, true  },   // 下载失败，可单击重试
    { false, false, false, false },   // 缺少下载信息的破图
};

// 上表的项数。
const int kDebugThumbInputCount = (int)(sizeof(kDebugThumbInputs) / sizeof(kDebugThumbInputs[0]));

} // anonymous namespace

void CBalloonHistoryWindow::FillImageThumb(HistoryRowView& row, int64_t msgid,
                                           const std::string& imageMd5,
                                           const std::string& imageFileId,
                                           int imageOrdinal) const
{
    row.m_hasImageThumb = true;

    balloonchatwnd::HistoryThumbDecision decision;
    const int64_t selfId = SelfUserId();
    if (m_pMainPanel == NULL || selfId <= 0)
    {
        //调试入口：没有登录态，算不出缓存路径、也发不了下载。按本页第几张图片轮流套用
        //几种状态，好在调试入口下核对"需要下载 / 下载中 / 破图"的样子。
        const DebugThumbInputs& in = kDebugThumbInputs[imageOrdinal % kDebugThumbInputCount];
        decision = balloonchatwnd::ResolveHistoryThumb(in.m_downloadable, in.m_fileExists,
                                                       in.m_downloading, in.m_downloadFailed,
                                                       false);
    }
    else
    {
        //缓存按登录账号隔离、按内容 MD5 命名，故要拿当前登录账号才能算出路径。
        std::wstring cache;
        if (!imageMd5.empty())
        {
            cache = net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), imageMd5);
            row.m_thumbImagePath = cache.c_str();
        }
        const bool downloadable   = !imageFileId.empty() && !imageMd5.empty();
        const bool fileExists     = !cache.empty() && PathExistsOnDisk(cache.c_str());
        const bool downloading    = (m_downloadingMsgids.find(msgid) != m_downloadingMsgids.end());
        const bool downloadFailed = (m_failedImageMsgids.find(msgid) != m_failedImageMsgids.end());
        //文件损坏（解码失败）不在这里判断：判定时并不解码，由缩略图绘制时按图片缓存的
        //失败记录改画破图。
        decision = balloonchatwnd::ResolveHistoryThumb(downloadable, fileExists, downloading,
                                                       downloadFailed, false);
    }

    row.m_thumbState     = (int)decision.m_state;
    row.m_thumbClickable = decision.m_clickable;
    row.m_thumbTooltip   = decision.m_tooltip;
}

void CBalloonHistoryWindow::FillCardThumbs(HistoryRowView& row, const std::vector<HistoryCardImage>& images,
                                           size_t maxShown)
{
    //列表里最多 3 张、多出的写「+N」；网格里每张一格，全部填上
    HistoryThumbStrip strip = ComputeThumbStrip(images.size());
    if (maxShown > kHistoryCardMaxThumbs)
    {
        strip.m_shown = (images.size() < maxShown) ? images.size() : maxShown;
        strip.m_extra = 0;
    }
    row.m_cardThumbExtra = (int)strip.m_extra;
    const bool live = (m_pMainPanel != NULL && SelfUserId() > 0);
    for (size_t k = 0; k < strip.m_shown; ++k)
    {
        const HistoryCardImage& image = images[k];
        //本机没有、也没有失败过：随即下载（帖子群窗口看过的图已在同一个缓存里）；失败过的等用户单击时再重试
        if (live && !image.m_fileId.empty() && !image.m_md5.empty() && m_cardFailed.count(image.m_md5) == 0)
        {
            const std::wstring cache = net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), image.m_md5);
            if (!PathExistsOnDisk(cache.c_str()))
            {
                RequestCardImage(image);
            }
        }
        row.m_cardThumbs.push_back(ResolveCardThumbView(image));
    }
}

HistoryCardThumbView CBalloonHistoryWindow::ResolveCardThumbView(const HistoryCardImage& image) const
{
    HistoryCardThumbView view;
    view.m_width = image.m_width;
    view.m_height = image.m_height;
    //调试入口没有登录态：算不出缓存路径，也不下载，缩略图按「缺少下载信息」画破图
    const bool live = (m_pMainPanel != NULL && SelfUserId() > 0);
    std::wstring cache;
    if (live && !image.m_md5.empty())
    {
        cache = net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), image.m_md5);
        view.m_path = cache.c_str();
    }
    const bool downloadable = live && !image.m_fileId.empty() && !image.m_md5.empty();
    const bool fileExists = !cache.empty() && PathExistsOnDisk(cache.c_str());
    const bool downloading = (m_cardDownloading.count(image.m_md5) > 0);
    const bool failed = (m_cardFailed.count(image.m_md5) > 0);
    const balloonchatwnd::HistoryThumbDecision decision =
        balloonchatwnd::ResolveHistoryThumb(downloadable, fileExists, downloading, failed, false);
    view.m_state = (int)decision.m_state;
    view.m_tooltip = decision.m_tooltip;
    return view;
}

HistoryCardImage CBalloonHistoryWindow::CardImageOfLibraryItem(const HistoryLibraryItem& item)
{
    HistoryCardImage image;
    if (item.m_cardImageIndex < 0)
    {
        return image;
    }
    image.m_fileId = item.m_fileid;
    image.m_md5 = item.m_md5;
    image.m_size = item.m_size;
    return image;
}

void CBalloonHistoryWindow::OpenCardImages(int msgtype, const std::string& content, int index, int64_t msgid)
{
    const HistoryContentBrief brief = ParseContentBrief(msgtype, content);
    if (index < 0 || (size_t)index >= brief.m_threadImages.size())
    {
        return;
    }
    LOG_INFO("history window: open thread card image, msgid=%lld, index=%d, count=%u.", (long long)msgid, index,
             (unsigned)brief.m_threadImages.size());
    m_cardViewImages = brief.m_threadImages;
    m_cardViewIndex = (size_t)index;
    ShowCardViewerImage();
}

void CBalloonHistoryWindow::RequestCardImage(const HistoryCardImage& image)
{
    if (image.m_fileId.empty() || image.m_md5.empty() || !IsWindow()
        || m_cardDownloading.count(image.m_md5) > 0)
    {
        return;
    }
    const uint32_t requestId = m_nextCardRequest;
    ++m_nextCardRequest;
    if (m_nextCardRequest == 0)
    {
        m_nextCardRequest = 1;
    }
    m_cardRequests[requestId] = image.m_md5;
    m_cardDownloading.insert(image.m_md5);
    m_cardFailed.erase(image.m_md5);
    LOG_INFO("history window: thread card image download, request=%u, fileid=%s.", (unsigned)requestId,
             image.m_fileId.c_str());
    net::ImageTransfer::GetInstance().EnqueuePlainDownload(requestId, image.m_fileId, image.m_md5, m_hWnd,
                                                           WM_HISTORY_CARD_IMAGE_DONE);
}

void CBalloonHistoryWindow::OpenRowCardImage(int row, int thumb)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size() || thumb < 0)
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];
    OpenCardImages(rec.m_msgtype, rec.m_content, thumb, rec.m_msgid);
}

void CBalloonHistoryWindow::ShowCardViewerImage()
{
    if (m_cardViewIndex >= m_cardViewImages.size())
    {
        return;
    }
    const HistoryCardImage& image = m_cardViewImages[m_cardViewIndex];
    std::wstring cache;
    if (m_pMainPanel != NULL && SelfUserId() > 0 && !image.m_md5.empty())
    {
        cache = net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), image.m_md5);
    }
    if (!cache.empty() && PathExistsOnDisk(cache.c_str()))
    {
        m_cardPendingViewMd5.clear();
        //只有一张时不需要翻页，不登记翻页回调
        IImageViewerNavigator* navigator = NULL;
        if (m_cardViewImages.size() > 1)
        {
            g_historyCardNavigator.SetTarget(m_hWnd);
            navigator = &g_historyCardNavigator;
        }
        CBalloonImageViewerWindow::OpenSharedWithNavigator(m_hWnd, cache.c_str(), navigator);
        return;
    }
    if (image.m_fileId.empty() || image.m_md5.empty() || cache.empty())
    {
        //缺少下载信息（或调试入口没有登录态）：无从下载
        ShowToast(TR(L"图片下载失败，请检查网络后重试。"));
        return;
    }
    //本机没有：先下载，下完再打开（这张图已在下载时只登记待办，下完照样打开）
    m_cardPendingViewMd5 = image.m_md5;
    m_cardFailed.erase(image.m_md5);
    RequestCardImage(image);
}

LRESULT CBalloonHistoryWindow::OnCardImageDoneMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    std::unique_ptr<net::PlainTransferResult> result(reinterpret_cast<net::PlainTransferResult*>(lParam));
    if (!result)
    {
        return 0;
    }
    std::map<uint32_t, std::string>::iterator it = m_cardRequests.find(result->m_requestId);
    if (it == m_cardRequests.end())
    {
        return 0;
    }
    const std::string md5 = it->second;
    m_cardRequests.erase(it);
    m_cardDownloading.erase(md5);
    if (result->m_ok)
    {
        m_cardFailed.erase(md5);
    }
    else
    {
        m_cardFailed.insert(md5);
        LOG_WARNING("history window: thread card image download failed, request=%u, code=%d.",
                    (unsigned)result->m_requestId, result->m_errcode);
    }
    //用户正等着看这张图
    if (!m_cardPendingViewMd5.empty() && m_cardPendingViewMd5 == md5)
    {
        m_cardPendingViewMd5.clear();
        if (result->m_ok)
        {
            ShowCardViewerImage();
        }
        else
        {
            ::MessageBox(m_hWnd, TR(L"图片下载失败，请检查网络后重试。"), TR(L"消息记录"), MB_OK | MB_ICONWARNING);
        }
    }
    //刷新本页，缩略图换成图片或破图；一批结果只刷新一次
    if (!m_cardRefreshPosted && IsWindow())
    {
        m_cardRefreshPosted = true;
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
    }
    return 0;
}

LRESULT CBalloonHistoryWindow::OnCardNavigateMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int delta = (int)(INT_PTR)wParam;
    if (m_cardViewImages.empty())
    {
        return 0;
    }
    //到头或到尾时停在原处，不循环
    if (delta < 0 && m_cardViewIndex > 0)
    {
        --m_cardViewIndex;
    }
    else if (delta > 0 && m_cardViewIndex + 1 < m_cardViewImages.size())
    {
        ++m_cardViewIndex;
    }
    else
    {
        return 0;
    }
    ShowCardViewerImage();
    return 0;
}

void CBalloonHistoryWindow::OnImageDownloadFinished(int64_t msgid, bool ok,
                                                    const std::wstring& localPath)
{
    //不是本窗口发起的那几笔一概不管 —— 聊天窗自己的下载不该在这里弹窗。
    if (m_downloadingMsgids.find(msgid) == m_downloadingMsgids.end())
    {
        return;
    }
    m_downloadingMsgids.erase(msgid);

    const bool wantView = (m_pendingViewMsgid == msgid);
    if (wantView)
    {
        m_pendingViewMsgid = 0;
    }

    if (!ok)
    {
        LOG_ERROR("history window: image download failed, msgid=%lld, wantView=%d.",
                  (long long)msgid, (int)wantView);
        //记下失败：缩略图据此显示破图，并允许单击重试。
        m_failedImageMsgids.insert(msgid);
        //只有"双击要看大图"引起的下载失败才弹提示框 —— 那时用户明确要看这张图，需要一个
        //交代。单击缩略图或菜单「下载」引起的失败，由缩略图变成破图来表示，不再弹框打断。
        if (wantView)
        {
            ::MessageBox(m_hWnd, TR(L"图片下载失败，请检查网络后重试。"),
                         TR(L"消息记录"), MB_OK | MB_ICONWARNING);
        }
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
        return;
    }
    m_failedImageMsgids.erase(msgid);

    //成功：缩略图这下能画出来了，刷一次列表；若这笔下载本就是"双击要看大图"引起的，
    //顺带把看图窗打开。
    if (wantView && !localPath.empty())
    {
        CBalloonImageViewerWindow::OpenShared(m_hWnd, localPath.c_str());
    }
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

balloonchatwnd::ChatVoiceStrip* CBalloonHistoryWindow::FindVoiceStrip(long long msgid)
{
    return FindHistoryVoiceStrip(GetClientContent(), msgid);
}

void CBalloonHistoryWindow::MarkVoicePlayedEverywhere(long long msgid)
{
    const HistoryFilter& filter = m_model.Filter();
    if (m_pMainPanel != NULL && m_pMainPanel->IsWindow())
    {
        //交给主面板：写本地库，再让已打开的聊天窗与本窗口去掉同一条的红点；负载由主面板 delete
        balloonchatwnd::VoicePlayedMark* mark = new balloonchatwnd::VoicePlayedMark();
        mark->m_ctype = filter.m_ctype;
        mark->m_cid = filter.m_cid;
        mark->m_msgid = msgid;
        if (::PostMessage(m_pMainPanel->m_hWnd, FMG_MSG_VOICE_PLAYED, 0, (LPARAM)mark))
        {
            return;
        }
        delete mark;
        LOG_WARNING("history window: post voice played failed, msgid=%lld, write store directly.", msgid);
    }
    //调试入口没有主面板，或投递失败：直接写本地库
    if (m_pStore != NULL)
    {
        m_pStore->MarkVoicePlayed(filter.m_ctype, filter.m_cid, msgid);
    }
}

void CBalloonHistoryWindow::OnVoicePlayedElsewhere(int ctype, int64_t cid, long long msgid)
{
    if (!IsWindow())
    {
        return;
    }
    const HistoryFilter& filter = m_model.Filter();
    if (filter.m_ctype != ctype || filter.m_cid != cid)
    {
        return;
    }
    //记下来：翻页或重建列表时，即使本地库的写入还没完成，这一条也按已听显示
    m_playedVoices.insert(msgid);
    if (MarkHistoryVoicePlayed(GetClientContent(), filter.m_ctype, filter.m_cid, ctype, cid, msgid))
    {
        LOG_INFO("history window: voice played elsewhere, red dot cleared, ctype=%d, cid=%lld, msgid=%lld.", ctype,
                 (long long)cid, msgid);
    }
}

LRESULT CBalloonHistoryWindow::OnChatOpenMarkdownPreview(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                         BOOL& /*bHandled*/)
{
    //lParam 为堆上的请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::MarkdownPreviewOpenRequest> req((balloonchatwnd::MarkdownPreviewOpenRequest*)lParam);
    if (!req)
    {
        return 0;
    }
    //与聊天窗相同：本人发的显示为「我」
    const std::wstring sender = req->m_fromMe ? std::wstring(TR(L"我")) : req->m_sender;
    balloonmd::MarkdownPreviewDoc doc;
    doc.m_sourceUtf8 = req->m_sourceUtf8;
    doc.m_title = req->m_title;
    doc.m_meta = balloonmd::MarkdownPreviewMeta(sender, req->m_timestampMs);
    doc.m_key = req->m_key;
    balloonmd::CMarkdownEditorManager::GetInstance().OpenPreview(doc);
    LOG_INFO("history window: open markdown preview, key=%s, bytes=%u.", req->m_key.c_str(),
             (unsigned)req->m_sourceUtf8.size());
    return 0;
}

LRESULT CBalloonHistoryWindow::OnChatOpenCodeViewer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                    BOOL& /*bHandled*/)
{
    balloonchatwnd::CodeViewerOpenRequest* pReq = (balloonchatwnd::CodeViewerOpenRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    // 只是打开查看窗：记录窗里不能追加批注（docs/代码片段方案.md 第 11.3 节）
    if (pReq->m_action != balloonchatwnd::kCodeViewerActionView)
    {
        delete pReq;
        return 0;
    }
    // 批注卡片写出作者：名字与列表里的发送人同一口径
    ballooncode::CodeViewerExtras extras;
    for (size_t i = 0; i < pReq->m_notes.size(); ++i)
    {
        const long long author = pReq->m_notes[i].m_authorId;
        if (author > 0 && extras.m_authorNames.find(author) == extras.m_authorNames.end())
        {
            extras.m_authorNames[author] = std::wstring((LPCWSTR)ResolveSenderName((int64_t)author));
        }
    }
    ballooncode::CCodeViewerWindow::Show(m_hWnd, pReq->m_code, pReq->m_lang, pReq->m_key, pReq->m_notes, extras);
    delete pReq;
    return 0;
}

LRESULT CBalloonHistoryWindow::OnChatVoiceActionMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                    BOOL& /*bHandled*/)
{
    balloonchatwnd::VoiceActionRequest* req = reinterpret_cast<balloonchatwnd::VoiceActionRequest*>(lParam);
    if (req == NULL)
    {
        return 0;
    }
    const long long msgid = req->m_msgid;
    delete req;
    balloonchatwnd::ChatVoiceStrip* strip = FindVoiceStrip(msgid);
    if (strip == NULL)
    {
        return 0;
    }
    const balloonchatwnd::ChatMessage msg = strip->VoiceMessage();
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    switch (balloonchatwnd::DecideVoiceClick(msg))
    {
    //就绪的语音：开始播放；对方发来、还没听过的在本地库标为已听（红点只记在本机，方案 V7）
    case balloonchatwnd::kVoiceClickPlay:
    {
        balloonvoice::VoicePlayKey key;
        key.m_msgid = msg.msgid;
        if (!hub.StartPlay(this, key, std::wstring((LPCWSTR)msg.voiceLocalPath)))
        {
            ShowToast(balloonchatwnd::VoicePlayFailedHint().c_str());
            LOG_WARNING("history window: voice play failed, msgid=%lld.", msg.msgid);
            break;
        }
        m_playingVoiceMsgid = msg.msgid;
        balloonchatwnd::ChatMessage playing = msg;
        playing.voicePlaying = true;
        playing.voicePlayFrame = 0;
        playing.voicePlayed = true;
        strip->SetVoiceMessage(playing, true);
        if (!msg.fromMe && !msg.voicePlayed)
        {
            m_playedVoices.insert(msg.msgid);
            MarkVoicePlayedEverywhere(msg.msgid);
        }
        LOG_INFO("history window: voice playing, msgid=%lld.", msg.msgid);
        break;
    }
    //正在播放：停止；语音中心通知本窗口播放结束，语音条在 OnVoicePlayEnded 里退出播放中
    case balloonchatwnd::kVoiceClickStop:
        hub.StopPlay();
        break;
    //还在下载：提示稍候
    case balloonchatwnd::kVoiceClickWaitDownload:
        ShowToast(balloonchatwnd::VoiceDownloadingHint().c_str());
        break;
    //本机没有：下载到 ChatVoice\<md5>.ogg，结果按 md5 经主面板转回来（OnVoiceDownloadFinished）
    case balloonchatwnd::kVoiceClickRetryDownload:
    {
        const std::wstring voiceDir = net::UserCacheDir(g_szDataRoot, net::CurrentUserAccount(), L"ChatVoice");
        if (msg.voiceFileId.empty() || msg.voiceMd5.empty() || voiceDir.empty())
        {
            ShowToast(balloonchatwnd::VoicePlayFailedHint().c_str());
            break;
        }
        m_downloadingVoices.insert(msg.voiceMd5);
        net::FileTransfer::GetInstance().EnqueueVoiceDownload(msg.voiceFileId, msg.voiceMd5, msg.voiceSize,
                                                              balloonvoice::VoiceCachePath(voiceDir, msg.voiceMd5));
        balloonchatwnd::ChatMessage downloading = msg;
        downloading.voiceState = balloonchatwnd::kVoiceStateDownloading;
        strip->SetVoiceMessage(downloading, true);
        LOG_INFO("history window: voice download started, md5=%s.", msg.voiceMd5.c_str());
        break;
    }
    //其它取值不会出现
    default:
        break;
    }
    return 0;
}

void CBalloonHistoryWindow::OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs,
                                                int64_t /*totalMs*/)
{
    balloonchatwnd::ChatVoiceStrip* strip = FindVoiceStrip(key.m_msgid);
    if (strip == NULL)
    {
        return;
    }
    const int frame = balloonchatwnd::VoicePlayFrame(playedMs);
    //帧没变时不重画：进度约每 100 毫秒来一次，图标每 kVoicePlayFrameMs 毫秒才换一帧
    if (strip->VoiceMessage().voicePlayFrame != frame)
    {
        balloonchatwnd::ChatMessage m = strip->VoiceMessage();
        m.voicePlayFrame = frame;
        strip->SetVoiceMessage(m, true);
    }
}

void CBalloonHistoryWindow::OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int /*reason*/)
{
    if (m_playingVoiceMsgid == key.m_msgid)
    {
        m_playingVoiceMsgid = 0;
    }
    balloonchatwnd::ChatVoiceStrip* strip = FindVoiceStrip(key.m_msgid);
    if (strip != NULL)
    {
        balloonchatwnd::ChatMessage m = strip->VoiceMessage();
        m.voicePlaying = false;
        m.voicePlayFrame = 0;
        strip->SetVoiceMessage(m, true);
    }
}

void CBalloonHistoryWindow::OnVoiceDownloadFinished(const std::string& md5, bool ok)
{
    if (!IsWindow())
    {
        return;
    }
    const bool mine = m_downloadingVoices.erase(md5) > 0;
    if (mine && !ok)
    {
        LOG_ERROR("history window: voice download failed, md5=%s.", md5.c_str());
    }
    //无论是不是本窗口发起的都刷新一次：语音条的本机文件状态在构造行数据时重新判断
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
}

void CBalloonHistoryWindow::OnFileDownloadFinished(int64_t msgid, bool ok,
                                                   const std::wstring& /*localPath*/,
                                                   bool cancelled)
{
    if (m_downloadingMsgids.find(msgid) == m_downloadingMsgids.end())
    {
        return;
    }
    m_downloadingMsgids.erase(msgid);

    //无论成败都刷一次：成功了菜单要从「下载」变成「打开 / 另存为」，
    //失败或取消了要从「取消下载」变回「下载」。先投递再提示，提示框弹出期间列表已是新状态。
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);

    const HistoryBatchFinish batch = m_batchDownload.OnFinished(msgid, ok, cancelled);
    if (!batch.m_inBatch)
    {
        //单独发起的下载（右键菜单「下载」）：失败即提示。用户主动取消不算失败，不弹提示。
        if (!ok && !cancelled)
        {
            LOG_ERROR("history window: file download failed, msgid=%lld.", (long long)msgid);
            ::MessageBox(m_hWnd, TR(L"文件下载失败，请检查网络后重试。"),
                         TR(L"消息记录"), MB_OK | MB_ICONWARNING);
        }
        return;
    }

    //「全部下载」的一笔：不逐条提示，等整批结束后汇总一次
    if (!ok && !cancelled)
    {
        LOG_ERROR("history window: file download failed in batch, msgid=%lld, %u still running.",
                  (long long)msgid, (unsigned)m_batchDownload.PendingCount());
    }
    if (!batch.m_batchDone)
    {
        return;
    }
    LOG_INFO("history window: batch download finished, succeeded=%d, failed=%d, cancelled=%d.",
             batch.m_succeeded, batch.m_failed, batch.m_cancelled);
    const CString notice = BuildBatchNoticeText(batch).c_str();
    switch (BatchNoticeKind(batch))
    {
    //有失败：弹一个提示框，说明失败与成功各几个
    case kHistoryBatchNoticeFailed:
        ::MessageBox(m_hWnd, notice, TR(L"消息记录"), MB_OK | MB_ICONWARNING);
        break;

    //全部成功：窗口顶部显示一条短暂的提示
    case kHistoryBatchNoticeAllDone:
        ShowToast(notice);
        break;

    //整批都被用户取消：不提示
    default:
        break;
    }
}

void CBalloonHistoryWindow::OnRowDoubleClicked(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    const bool isImage = (row >= 0 && row < (int)recs.size()
                          && IsHistoryRowImageType(recs[(size_t)row].m_msgtype));
    LOG_INFO("history window: row %d double-clicked, image=%d.", row, (int)isImage);

    //图片行的双击是"看大图"（本地没有原图时先下载、下完自动开），其余类型是"定位到
    //聊天窗"。图片行的定位只留行内那枚「定位」按钮。这条取舍是与用户确认过的，见
    //docs/消息记录内容交互方案.md §10-2。
    if (isImage)
    {
        ViewRowImage(row);
        return;
    }
    LocateRow(row);
}

void CBalloonHistoryWindow::LocateRow(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    LocateRecord(recs[(size_t)row]);
}

void CBalloonHistoryWindow::LocateRecord(const HistoryRecord& rec)
{
    //跳到聊天窗并滚动定位到该消息。调试入口没有主面板（m_pMainPanel==NULL），
    //也就没有可跳的聊天窗，直接忽略。
    if (m_pMainPanel == NULL || rec.m_msgid <= 0)
    {
        return;
    }
    m_pMainPanel->LocateMessageInChat(rec.m_ctype, rec.m_cid, (long long)rec.m_msgid);
}

void CBalloonHistoryWindow::JumpToPage(int page)
{
    //页号夹取交给模型（GoToPage 自己会夹到 [1, PageCount]），这里只挡掉"根本没输
    //数字"的情形 —— _ttoi 对空串与非数字都返回 0，直接翻过去会跳到第 1 页，
    //而用户其实什么都没提交。
    if (page <= 0)
    {
        return;
    }
    if (page == m_model.CurrentPage())
    {
        return;
    }
    //手输页号提交也算一次操作，撤掉定位高亮
    ClearLocatedRow();
    m_model.GoToPage(page);
    //与其余翻页入口同口径：延迟刷新，避免在被点击控件自身的事件回调里销毁它。
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

void CBalloonHistoryWindow::RefreshContent(bool pageOnly)
{
    //停在文件 / 图片页时，聊天记录页是隐藏的：各处「数据变了要刷新」的请求都改为刷新文件 / 图片页
    //（只换页对应只换页，其余连同左栏的计数一起刷新）。切回聊天记录页时会再完整刷新一次聊天记录
    if (m_mgrTab != kHistoryManagerTabChat)
    {
        RefreshLibrary(pageOnly ? kLibRefreshPage : kLibRefreshAll);
        return;
    }

    //完整刷新意味着数据或筛选条件可能变了；下面的待跳日期重试会把 pageOnly 改掉，先记下
    const bool fullRefresh = !pageOnly;

    //还有在等往前拉取的「跳到这一天」：完整刷新多半是又拉到了一批更早的消息，先重试一次。重试已经
    //按新数据统计并取好了页，下面按只换页出列表即可
    if (!pageOnly && m_pendingJumpEndMs != 0)
    {
        ApplyDateJump(m_pendingJumpStartMs, m_pendingJumpEndMs, true);
        pageOnly = true;
    }

    //时间轴模式（docs/聊天记录管理器方案.md §11.7）：先按当前条件统计导航数据与命中位置，列表各行要据此
    //标出命中。完整刷新时数据可能变了，必定重新统计；只换页时条件没变就沿用上次的结果
    bool jumpToFirstHit = false;
    if (m_timelineMode)
    {
        ApplyCoverageBound();
        jumpToFirstHit = RefreshTimelineStats(fullRefresh);
    }

    //分两段计时（查询、建列表与排版），记进日志：每页条数、搜索频率是否合适，看这条日志即可
    PerfStopWatch swQuery;
    HistoryViewData data = BuildViewData(pageOnly);
    const double queryMs = swQuery.ElapsedMs();
    PerfStopWatch swBuild;

    //1. 列表：整体换新内容。
    if (m_pScroll != NULL)
    {
        //行高按正文在列表实际宽度下折出几行来算，故必须把真实宽度传下去 ——
        //传 0 会走兜底宽度，窗口一旦不是那个宽度，长消息的行高就会偏（要么被下一行
        //压住，要么下面空出一截）。
        const RECT rcScroll = m_pScroll->GetRect();
        const int listWidth = rcScroll.right - rcScroll.left;
        if (IsImageGridMode())
        {
            //帖子卡片每张图片一格（docs/帖子群方案.md 14.5），格子与记录的对应记在 m_gridCells
            const std::vector<HistoryRowView> cells = ExpandHistoryGridCells(data.m_rows, kIdHistoryGridCapacity,
                                                                             m_gridCells);
            m_pScroll->SetContent(BuildImageGridContent(cells, data.m_emptyTitle,
                                                        data.m_emptySubtitle, listWidth));
        }
        else
        {
            m_gridCells.clear();
            //「文件」分类：本页有没下载的文件时，列表顶部放「全部下载」
            const int pendingDownloads = (m_model.Filter().m_type == kHistoryTypeFile && !m_multiSelect)
                                       ? (int)PendingFileDownloadRows().size() : 0;
            m_pScroll->SetContent(BuildListContent(data.m_rows, data.m_emptyTitle,
                                                   data.m_emptySubtitle, listWidth, true,
                                                   pendingDownloads));
        }
        //换内容时 DuiScrollView 保留原来的滚动位置。换了一页或换了筛选条件就是另一批消息，
        //滚回顶部从头看；同一页的重画（图片下载完成等）保持用户当前的位置。「查看上下文」
        //「跳到这一天」随后由 ApplyLocatedRow 滚到高亮的那一行
        if (ShouldResetListScroll(m_shownFilter, m_model.Filter()))
        {
            m_pScroll->SetScrollPos(0);
        }
        m_shownFilter = m_model.Filter();
        m_pScroll->ForceLayout(m_pScroll->GetRect());
        m_pScroll->Invalidate();
    }

    //2. 分页器：清空旧按钮 + 按新页数重填。
    if (m_pPager != NULL)
    {
        RemoveAllChildren(m_pPager);
        PopulatePagerButtons(m_pPager, data.m_currentPage, data.m_pageCount);
        //分页栏的宽度随页码格数变化，要整行重排，不能只排分页栏自己
        RelayoutFooter();
    }

    //3. 底部信息与标题副行（总数 / 页码可能变）。
    UpdateFooterInfo();

    DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        //跳页微调框：范围随总页数走、当前值随当前页走。它在底部行里、<u>不</u>随局部
        //刷新重建（重建的只有列表与分页器），故要在这里就地同步。SetValue 传
        //notify=false —— 同步显示不该反过来再触发一次翻页。
        DuiSpinBox* jump = dynamic_cast<DuiSpinBox*>(content->FindCtrlById(kIdHistoryPageJump));
        if (jump != NULL)
        {
            jump->SetRange(1, (data.m_pageCount > 0) ? data.m_pageCount : 1);
            jump->SetValue(data.m_currentPage, false);
            jump->Invalidate();
        }

        DuiLabel* sub = dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdHistorySubtitle));
        if (sub != NULL)
        {
            //同底部信息栏：还有更早的记录没加载时写"已加载 N 条消息，翻到最后一页会继续加载更早的"
            const std::wstring text = BuildHistorySubtitleText(data.m_totalCount, HasUnloadedOlderNow());
            sub->SetText(CString(text.c_str()));
        }

        //4. 类型胶囊选中态（就地改 Variant，不重建，保住搜索框焦点）。
        //   下标须与 HistoryTypeFilter 的取值一一对应，新增胶囊时两处一起改。
        const int kChipCount = 7;
        const UINT kChipIds[kChipCount] =
        {
            kIdHistoryChipAll, kIdHistoryChipText, kIdHistoryChipImage,
            kIdHistoryChipFile, kIdHistoryChipVoice, kIdHistoryChipLink,
            kIdHistoryChipPin,
        };
        for (int i = 0; i < kChipCount; ++i)
        {
            DuiButton* chip = dynamic_cast<DuiButton*>(content->FindCtrlById(kChipIds[i]));
            if (chip != NULL)
            {
                chip->SetVariant((i == data.m_activeChip)
                                 ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
                chip->Invalidate();
            }
        }
    }

    //count queries 是窗口打开以来统计总数的累计次数：只换页的刷新不应让它增加
    LOG_INFO("History refresh: ctype=%d, cid=%lld, type=%d, keyword bytes=%u, total=%d, page %d/%d, "
             "rows=%u, page only %d, count queries %lld, query %.1f ms, build %.1f ms.",
             m_model.Filter().m_ctype, (long long)m_model.Filter().m_cid, (int)m_model.Filter().m_type,
             (unsigned)m_model.Filter().m_keyword.size(), data.m_totalCount, data.m_currentPage,
             data.m_pageCount, (unsigned)data.m_rows.size(), (int)pageOnly,
             (long long)m_model.CountQueryTimes(), queryMs, swBuild.ElapsedMs());

    //「查看上下文」「跳到这一天」翻到的那一行：加高亮，刚翻过去时再滚到它
    ApplyLocatedRow();

    //时间轴模式：更新命中条与导航条（高亮当前所在的月份）；刚换了关键字时跳到第一处命中（另投一次只换页的刷新）
    if (m_timelineMode)
    {
        UpdateHitBar();
        RebuildTimelineNav();
        if (jumpToFirstHit)
        {
            JumpToHit(0);
        }
    }

    //4. 重查之后看要不要向服务端往前拉更早的消息（停在末页、覆盖区间还没到最早时）
    CheckFetchOlderAfterRefresh();
}

// =====================================================================
// 按覆盖区间显示、翻到末页时向服务端往前拉（docs/清理聊天记录方案.md §9.2）
// =====================================================================

namespace {

//往前拉取的超时定时器 id 与超时时长。balloonui 在宿主窗口上不设定时器（它用线程定时器，
//菜单用的 0xD3 / 0xD4 设在菜单自己的弹出窗口上），取一个醒目的值便于排查
const UINT_PTR kFetchOlderTimerId = 0x4801;
const UINT kFetchOlderTimeoutMs = 15000;

//往前拉一轮要几条：与服务端 cmd_sync_messages 的单页上限一致
const int kFetchOlderPageSize = 100;

} // anonymous namespace

void CBalloonHistoryWindow::UpdateFooterInfo()
{
    if (m_pFooterInfo == NULL)
    {
        return;
    }
    DuiLabel* info = dynamic_cast<DuiLabel*>(m_pFooterInfo);
    if (info == NULL)
    {
        return;
    }
    //还有更早的记录没加载时写"已加载 N 条"，免得用户以为就这么多（文字规则见 BuildHistoryFooterText）
    const std::wstring text = BuildHistoryFooterText(m_model.Total(), m_model.CurrentPage(),
                                                     m_model.PageCount(), HasUnloadedOlderNow(),
                                                     m_fetchingOlder);
    info->SetText(CString(text.c_str()));

    //文字长短变了，放不放得下要重新判断
    RelayoutFooter();
}

void CBalloonHistoryWindow::RelayoutFooter()
{
    DuiControl* footer = (m_pPager != NULL) ? m_pPager->GetParent() : NULL;
    if (footer == NULL)
    {
        return;
    }
    footer->ForceLayout(footer->GetRect());
    footer->Invalidate();
}

void CBalloonHistoryWindow::ApplyCoverageBound()
{
    //调试入口（没有真实本地库、没有主面板）：示例数据没有覆盖区间，不限制
    if (m_pStore == NULL || m_pMainPanel == NULL)
    {
        m_model.SetMinMsgid(0);
        return;
    }

    const HistoryFilter& filter = m_model.Filter();
    CoverRange cover;
    m_pStore->GetCoverage(filter.m_ctype, filter.m_cid, cover);
    m_model.SetMinMsgid(CoverageMinVisibleMsgid(cover, filter.m_type == kHistoryTypePinned));
}

void CBalloonHistoryWindow::CheckFetchOlderAfterRefresh()
{
    if (m_pStore == NULL || m_pMainPanel == NULL || !IsWindow())
    {
        return;
    }

    const HistoryFilter& filter = m_model.Filter();
    CoverRange cover;
    if (!m_pStore->GetCoverage(filter.m_ctype, filter.m_cid, cover))
    {
        return;
    }

    //一、有一轮在途：覆盖区间的下界降到锚点以下、或已拉到最早，说明这一轮已经并入
    if (m_fetchingOlder)
    {
        const bool roundDone = cover.m_reachedOldest
                            || (cover.m_valid && cover.m_lo < m_fetchAnchor);
        if (!roundDone)
        {
            return;
        }
        ++m_fetchRounds;
        if (ShouldContinueFetchOlder(m_fetchRounds, kMaxFetchOlderRounds, m_fetchCountBefore,
                                     m_model.Total(), filter.m_pageSize, cover.m_reachedOldest))
        {
            RequestOlderPage(cover);
        }
        else if (!m_pendingExportPath.IsEmpty() && !cover.m_reachedOldest
                 && m_pendingExportRestarts < kMaxPendingJumpRestarts)
        {
            //「先加载全部再导出」还没拉到最早一条：本次的轮数用完了也接着拉
            ++m_pendingExportRestarts;
            LOG_INFO("History window keeps fetching older for a pending export, restart %d.",
                     m_pendingExportRestarts);
            m_fetchRounds = 0;
            m_fetchCountBefore = m_model.Total();
            RequestOlderPage(cover);
        }
        else if (m_pendingJumpEndMs != 0 && !cover.m_reachedOldest
                 && m_pendingJumpRestarts < kMaxPendingJumpRestarts)
        {
            //「跳到这一天」还没拉到那一天：本次的轮数用完了也接着拉，直到找到或拉到最早
            ++m_pendingJumpRestarts;
            LOG_INFO("History window keeps fetching older for a pending date jump, restart %d.",
                     m_pendingJumpRestarts);
            m_fetchRounds = 0;
            m_fetchCountBefore = m_model.Total();
            RequestOlderPage(cover);
        }
        else
        {
            if (m_pendingJumpEndMs != 0 && !cover.m_reachedOldest)
            {
                //接着拉的次数也用完了：放弃这次跳转，免得没完没了地向服务端要数据
                LOG_INFO("History window gives up the pending date jump after %d restarts.",
                         m_pendingJumpRestarts);
                m_pendingJumpStartMs = 0;
                m_pendingJumpEndMs = 0;
                ShowToast(TR(L"所选日期太早，已停止加载，请稍后再试"));
            }
            LOG_INFO("History window stops fetching older, ctype=%d, cid=%lld, rounds=%d, "
                     "count %lld -> %lld, reachedOldest=%d.",
                     filter.m_ctype, (long long)filter.m_cid, m_fetchRounds,
                     (long long)m_fetchCountBefore, (long long)m_model.Total(),
                     cover.m_reachedOldest ? 1 : 0);
            StopFetchingOlder();
            UpdateFooterInfo();
            //拉到的更早消息里可能有新的发送人
            RefreshSenderCombo();
            //「先加载全部再导出」：拉到最早一条，或接着拉的次数用完了，都开始导出（后者导出的是
            //已经拉到的部分，写完时如实提示）
            if (!m_pendingExportPath.IsEmpty())
            {
                ::PostMessage(m_hWnd, WM_HISTORY_EXPORT_READY, 0, 0);
            }
        }
        return;
    }

    //二、没有在途：停在末页、区间还没到最早时开始拉
    const bool isPinned = (filter.m_type == kHistoryTypePinned);
    const bool isLastPage = (m_model.CurrentPage() >= m_model.PageCount());
    bool hasTimeLowerBound = false;
    int64_t timeLowerBoundMs = 0;
    int64_t oldestCoveredMs = 0;
    if (!LoadOlderFetchInputs(cover, hasTimeLowerBound, timeLowerBoundMs, oldestCoveredMs))
    {
        return;
    }

    if (ShouldFetchOlder(cover, isLastPage, isPinned, hasTimeLowerBound, timeLowerBoundMs,
                         oldestCoveredMs))
    {
        m_fetchRounds = 0;
        m_fetchCountBefore = m_model.Total();
        RequestOlderPage(cover);
    }
}

bool CBalloonHistoryWindow::LoadOlderFetchInputs(CoverRange& cover, bool& hasTimeLowerBound,
                                                 int64_t& timeLowerBoundMs, int64_t& oldestCoveredMs)
{
    if (m_pStore == NULL || m_pMainPanel == NULL)
    {
        return false;
    }

    const HistoryFilter& filter = m_model.Filter();
    if (!m_pStore->GetCoverage(filter.m_ctype, filter.m_cid, cover))
    {
        return false;
    }

    hasTimeLowerBound = false;
    timeLowerBoundMs = 0;
    if (filter.m_range != kHistoryTimeAll)
    {
        int64_t maxMs = 0;
        TimeRangeToBounds(filter.m_range, NowMs(), filter.m_customMinMs, filter.m_customMaxMs,
                          timeLowerBoundMs, maxMs);
        hasTimeLowerBound = true;
    }

    //区间内一行都没有时，"区间内最早一条的时间"无从谈起：取最大值，让时间筛选这一条
    //永远不会挡住拉取
    oldestCoveredMs = 0;
    bool found = false;
    m_pStore->QueryOldestTimestampFrom(filter.m_ctype, filter.m_cid, cover.m_lo,
                                       oldestCoveredMs, found);
    if (!found)
    {
        oldestCoveredMs = INT64_MAX;
    }
    return true;
}

bool CBalloonHistoryWindow::HasUnloadedOlderNow()
{
    CoverRange cover;
    bool hasTimeLowerBound = false;
    int64_t timeLowerBoundMs = 0;
    int64_t oldestCoveredMs = 0;
    if (!LoadOlderFetchInputs(cover, hasTimeLowerBound, timeLowerBoundMs, oldestCoveredMs))
    {
        return false;
    }
    const bool isPinned = (m_model.Filter().m_type == kHistoryTypePinned);
    return HasUnloadedOlder(cover, isPinned, hasTimeLowerBound, timeLowerBoundMs, oldestCoveredMs);
}

void CBalloonHistoryWindow::RequestOlderPage(const CoverRange& cover)
{
    const HistoryFilter& filter = m_model.Filter();
    m_fetchingOlder = true;
    m_fetchAnchor = cover.m_lo;
    CIUSocket::GetInstance().RequestSyncMessages(filter.m_ctype, filter.m_cid,
                                                 cover.m_lo, kFetchOlderPageSize);
    //应答迟迟不到（断网、服务端出错）时不能让"正在加载"一直挂着
    SetTimer(kFetchOlderTimerId, kFetchOlderTimeoutMs, NULL);
    UpdateFooterInfo();
    LOG_INFO("History window fetches older page, ctype=%d, cid=%lld, before=%lld, round=%d.",
             filter.m_ctype, (long long)filter.m_cid, (long long)cover.m_lo, m_fetchRounds + 1);
}

void CBalloonHistoryWindow::StopFetchingOlder()
{
    m_fetchingOlder = false;
    m_fetchAnchor = 0;
    m_fetchRounds = 0;
    if (IsWindow())
    {
        KillTimer(kFetchOlderTimerId);
    }
}

LRESULT CBalloonHistoryWindow::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/,
                                          BOOL& bHandled)
{
    //搜索框停止输入 kSearchDebounceMs 了：按搜索框当前的文字查询（ApplySearchKeyword 会停掉定时器）
    if (wParam == kSearchDebounceTimerId)
    {
        ApplySearchKeyword();
        return 0;
    }

    //文件 / 图片页的两个定时器：文件名搜索的延迟、群文件缓存变化的合并刷新
    if (HandleLibraryTimer((UINT_PTR)wParam))
    {
        return 0;
    }

    //左栏数据来源的变化合并到点：重建左栏
    if (wParam == kMgrRebuildTimerId)
    {
        KillTimer(kMgrRebuildTimerId);
        m_mgrRebuildPending = false;
        RebuildManagerList();
        return 0;
    }

    //切换会话后停留够了：给最后停下的会话补拉
    if (wParam == kMgrBackfillTimerId)
    {
        KillTimer(kMgrBackfillTimerId);
        if (m_pMainPanel != NULL && m_pendingBackfillCid != 0)
        {
            LOG_INFO("history manager: backfill ctype=%d, cid=%lld.",
                     m_pendingBackfillCtype, (long long)m_pendingBackfillCid);
            m_pMainPanel->RequestHistoryBackfill(m_pendingBackfillCtype, m_pendingBackfillCid);
        }
        m_pendingBackfillCid = 0;
        return 0;
    }

    //左栏搜索框停止输入够久了：按当前文字搜索
    if (wParam == kMgrSearchTimerId)
    {
        ApplyManagerSearch();
        return 0;
    }

    if (wParam != kFetchOlderTimerId)
    {
        bHandled = FALSE;
        return 0;
    }

    //定时器只用来判超时，到点即停，不重复触发
    KillTimer(kFetchOlderTimerId);
    if (m_fetchingOlder)
    {
        LOG_ERROR("History window: fetching older page timed out, anchor=%lld.",
                  (long long)m_fetchAnchor);
        StopFetchingOlder();
        UpdateFooterInfo();
    }
    return 0;
}

bool CBalloonHistoryWindow::IsOwnOlderFetch(int ctype, int64_t cid, int64_t beforeMsgid) const
{
    if (!m_fetchingOlder)
    {
        return false;
    }
    const HistoryFilter& filter = m_model.Filter();
    return filter.m_ctype == ctype && filter.m_cid == cid && beforeMsgid == m_fetchAnchor;
}

void CBalloonHistoryWindow::ResetSearchControls()
{
    //还在计时的那次搜索作废：它要查的是重置之前的关键字
    KillTimer(kSearchDebounceTimerId);

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    //不发通知：模型已由调用方重置，发出通知只会再排一次相同的查询
    DuiEdit* search = dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdHistorySearch));
    if (search != NULL)
    {
        search->SetTextNoNotify(_T(""));
    }
    DuiComboBox* sender = dynamic_cast<DuiComboBox*>(content->FindCtrlById(kIdHistorySenderCombo));
    if (sender != NULL)
    {
        sender->SetCurSel(0, false);
    }
    SyncTimeFilterControls();
}

void CBalloonHistoryWindow::SyncTimeFilterControls()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    const HistoryFilter& filter = m_model.Filter();
    //不发通知：模型已由调用方改好，发出通知只会再排一次相同的查询
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(content->FindCtrlById(kIdHistoryTimeCombo));
    if (combo != NULL)
    {
        combo->SetCurSel(ComboIndexFromRange(filter.m_range), false);
    }
    ApplyHistoryDateRangeChip(content, DateRangeLabelOf(filter, NowMs()));
    //标签宽度随日期文字变化，重排后整行重画
    DuiControl* filterRow = content->FindCtrlById(kIdHistoryFilterRow);
    if (filterRow != NULL)
    {
        filterRow->Invalidate();
    }
}

void CBalloonHistoryWindow::ViewRowContext(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row < 0 || row >= (int)recs.size())
    {
        return;
    }
    //ShowContextOf 会重查，recs 随之失效，先把这一条复制出来
    const HistoryRecord target = recs[(size_t)row];

    ApplyCoverageBound();
    const bool found = m_model.ShowContextOf(NowMs(), target);
    //模型里的筛选条件已经清掉，搜索框、时间下拉、发送人下拉与日期区间标签跟着恢复
    ResetSearchControls();
    LOG_INFO("history window: view context of msgid=%lld, found %d, page %d/%d.",
             (long long)target.m_msgid, (int)found, m_model.CurrentPage(), m_model.PageCount());
    if (found)
    {
        m_locateMsgid = target.m_msgid;
        m_scrollToLocated = true;
    }
    //模型已经按新条件统计并取好了页，只换页的刷新不会再统计一次
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

//在当前页的记录里找 msgid，找到返回行下标，否则返回 -1。
static int FindRowOfMsgid(const std::vector<HistoryRecord>& recs, int64_t msgid)
{
    for (size_t i = 0; i < recs.size(); ++i)
    {
        if (recs[i].m_msgid == msgid)
        {
            return (int)i;
        }
    }
    return -1;
}

//按行下标取列表里的行控件（所有权在控件树）。要从滚动视图的内容开始找：DuiScrollView
//装进来的内容不在它的子控件表里，从客户区根控件往下 FindCtrlById 找不到列表里的行。
static HistoryRow* FindRowControl(DuiScrollView* scroll, int row)
{
    DuiControl* list = (scroll != NULL) ? scroll->GetContent() : NULL;
    if (list == NULL || row < 0)
    {
        return NULL;
    }
    return dynamic_cast<HistoryRow*>(list->FindCtrlById((UINT)(kIdHistoryRowBase + row)));
}

void CBalloonHistoryWindow::ApplyLocatedRow()
{
    if (m_locateMsgid == 0 || m_pScroll == NULL)
    {
        return;
    }
    const int row = FindRowOfMsgid(m_model.Records(), m_locateMsgid);
    //网格模式（「图片」分类）没有行，定位到那张缩略图：缩略图不画高亮底色，只滚到它
    HistoryRow* rowCtrl = FindRowControl(m_pScroll, row);
    DuiControl* locatedCtrl = rowCtrl;
    if (locatedCtrl == NULL && IsImageGridMode() && row >= 0 && m_pScroll->GetContent() != NULL)
    {
        //定位到这条记录的第一格（帖子卡片占几格时取第一张）
        for (size_t i = 0; i < m_gridCells.size(); ++i)
        {
            if (m_gridCells[i].m_row == row)
            {
                locatedCtrl = m_pScroll->GetContent()->FindCtrlById((UINT)(kIdHistoryGridBase + i));
                break;
            }
        }
    }
    if (locatedCtrl == NULL)
    {
        //这一条已经不在当前页了（例如被删除），不再找它
        LOG_INFO("history window: located msgid=%lld not found on this page (row %d).",
                 (long long)m_locateMsgid, row);
        m_scrollToLocated = false;
        return;
    }
    if (rowCtrl != NULL)
    {
        rowCtrl->SetLocated(true);
    }
    if (!m_scrollToLocated)
    {
        return;
    }
    m_scrollToLocated = false;

    //行在内容里的纵向位置 = 行当前的位置 - 视口顶 + 当前滚动量。滚到让这一行居中；
    //越界由滚动条自己限制在 [0, 最大滚动量]
    const RECT rcView = m_pScroll->GetRect();
    const RECT rcRow = locatedCtrl->GetRect();
    const int rowTopInContent = rcRow.top - rcView.top + m_pScroll->GetScrollPos();
    const int viewH = rcView.bottom - rcView.top;
    const int rowH = rcRow.bottom - rcRow.top;
    int target = rowTopInContent - (viewH - rowH) / 2;
    if (target < 0)
    {
        target = 0;
    }
    LOG_INFO("history window: located row %d (msgid=%lld), scroll to %d.",
             row, (long long)m_locateMsgid, target);
    m_pScroll->SetScrollPos(target);
    m_pScroll->Invalidate();
}

void CBalloonHistoryWindow::ClearLocatedRow()
{
    //用户做了别的操作，还在等往前拉取的「跳到日期」也一并作罢
    if (m_pendingJumpEndMs != 0)
    {
        LOG_INFO("history window: pending date jump cancelled.");
        m_pendingJumpStartMs = 0;
        m_pendingJumpEndMs = 0;
    }
    if (m_locateMsgid == 0)
    {
        return;
    }
    LOG_INFO("history window: clear located msgid=%lld.", (long long)m_locateMsgid);
    HistoryRow* rowCtrl = FindRowControl(m_pScroll, FindRowOfMsgid(m_model.Records(), m_locateMsgid));
    if (rowCtrl != NULL)
    {
        rowCtrl->SetLocated(false);
    }
    m_locateMsgid = 0;
    m_scrollToLocated = false;
}

void CBalloonHistoryWindow::OpenDateJumpPopup(UINT anchorId)
{
    DuiControl* content = GetClientContent();
    DuiControl* anchorCtrl = (content != NULL) ? content->FindCtrlById(anchorId) : NULL;
    if (anchorCtrl == NULL)
    {
        return;
    }
    //控件矩形是本窗口客户区坐标，弹层要屏幕坐标
    RECT anchor = anchorCtrl->GetRect();
    ::MapWindowPoints(m_hWnd, NULL, reinterpret_cast<LPPOINT>(&anchor), 2);

    const int64_t nowMs = NowMs();
    HistoryDate today;
    HistoryDateOf(nowMs, today);

    //预置日期：「只看这段时间」正在生效时取它的起止，便于在原区间上修改；否则取当前页第一条
    //消息的日期，当前页为空时取今天
    const HistoryFilter& filter = m_model.Filter();
    const std::vector<HistoryRecord>& recs = m_model.Records();
    HistoryDate from;
    HistoryDate to;
    ComputeHistoryDatePopupDefaults(filter.m_range == kHistoryTimeCustom, filter.m_customMinMs,
                                    filter.m_customMaxMs, !recs.empty(),
                                    recs.empty() ? 0 : recs.front().m_timestamp, nowMs, from, to);

    //年份范围：本地最早一条消息的年份到今年；查不到就取预置日期与今年中较早的那一年
    int minYear = (from.m_year < today.m_year) ? from.m_year : today.m_year;
    if (m_pStore != NULL)
    {
        int64_t oldestMs = 0;
        bool found = false;
        if (m_pStore->QueryOldestTimestampFrom(filter.m_ctype, filter.m_cid, filter.m_minMsgid, oldestMs, found)
            && found)
        {
            HistoryDate oldest;
            if (HistoryDateOf(oldestMs, oldest) && oldest.m_year < minYear)
            {
                minYear = oldest.m_year;
            }
        }
    }

    m_dateJumpPopup.SetSubmitCallback(&CBalloonHistoryWindow::OnDatePopupThunk, this);
    m_dateJumpPopup.OpenBelow(anchor, from, to, minYear, today.m_year);
}

bool CBalloonHistoryWindow::OnDatePopupThunk(void* userdata, int action, const HistoryDate& from,
                                             const HistoryDate& to)
{
    CBalloonHistoryWindow* self = static_cast<CBalloonHistoryWindow*>(userdata);
    if (self == NULL || !self->IsWindow())
    {
        //窗口已经不在：没有可应用的对象，让弹层关闭即可
        return true;
    }
    switch (action)
    {
    //「跳到这一天」或在任一输入框里按回车：只用「从」那一天
    case kHistoryDatePopupJump:
        return self->JumpToDay(from);

    //「只看这段时间」：按起止日期筛选
    case kHistoryDatePopupFilter:
        return self->FilterByDateRange(from, to);

    //其余取值弹层不会交回；按已处理关闭弹层
    default:
        return true;
    }
}

bool CBalloonHistoryWindow::JumpToDay(const HistoryDate& day)
{
    int64_t startMs = 0;
    int64_t endMs = 0;
    if (!LocalDayBoundsMs(day.m_year, day.m_month, day.m_day, startMs, endMs))
    {
        LOG_INFO("history window: jump to date %d-%d-%d, no such day.", day.m_year, day.m_month, day.m_day);
        ShowToast(TR(L"这一天不存在，请重新选择日期"));
        return false;
    }

    //跳转在全部时间里进行：时间筛选（近 N 天或「只看这段时间」）多半不包含所选那天，保留它只会
    //跳到之前最近的一条或末页。关键字、类型与发送人是用户有意设的，保留
    if (m_model.Filter().m_range != kHistoryTimeAll)
    {
        LOG_INFO("history window: date jump clears time range %d.", (int)m_model.Filter().m_range);
        m_model.SetTimeRange(kHistoryTimeAll);
        SyncTimeFilterControls();
    }

    ClearLocatedRow();
    ApplyDateJump(startMs, endMs, false);
    //JumpToDate 已按（可能刚改过的）筛选条件统计并取好了页，只换页的刷新不会再统计一次
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
    return true;
}

bool CBalloonHistoryWindow::FilterByDateRange(const HistoryDate& from, const HistoryDate& to)
{
    int64_t startMs = 0;
    int64_t endMs = 0;
    const HistoryDateRangeCheck check = CheckHistoryDateRange(from, to, startMs, endMs);
    LOG_INFO("history window: date range %d-%d-%d .. %d-%d-%d, check %d.", from.m_year, from.m_month,
             from.m_day, to.m_year, to.m_month, to.m_day, (int)check);
    switch (check)
    {
    //起止都存在、先后正确：往下应用
    case kHistoryDateRangeOk:
        break;

    //开始日期不存在（如 2 月 30 日）
    case kHistoryDateRangeStartInvalid:
        ShowToast(TR(L"开始日期不存在，请重新选择"));
        return false;

    //结束日期不存在
    case kHistoryDateRangeEndInvalid:
        ShowToast(TR(L"结束日期不存在，请重新选择"));
        return false;

    //开始日期晚于结束日期
    case kHistoryDateRangeStartAfterEnd:
        ShowToast(TR(L"开始日期不能晚于结束日期"));
        return false;

    //未知结果：不应用，弹层保持打开
    default:
        return false;
    }

    m_model.SetCustomRange(startMs, endMs);
    ClearLocatedRow();
    SyncTimeFilterControls();
    //筛选条件变了，完整刷新（重新统计总数）
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
    return true;
}

void CBalloonHistoryWindow::ApplyDateJump(int64_t startMs, int64_t endMs, bool retry)
{
    ApplyCoverageBound();
    const HistoryDateJump jump = m_model.JumpToDate(NowMs(), startMs, endMs);
    LOG_INFO("history window: jump to date [%lld, %lld], retry %d, result %d, msgid=%lld, page %d/%d.",
             (long long)startMs, (long long)endMs, (int)retry, jump.m_result, (long long)jump.m_msgid,
             m_model.CurrentPage(), m_model.PageCount());
    switch (jump.m_result)
    {
    //那天有消息：高亮那天最晚的一条
    case kHistoryDateJumpExact:
        m_pendingJumpStartMs = 0;
        m_pendingJumpEndMs = 0;
        m_locateMsgid = jump.m_msgid;
        m_scrollToLocated = true;
        break;

    //那天没有消息：高亮之前最近的一条，并说明跳到的不是所选那天
    case kHistoryDateJumpEarlier:
        m_pendingJumpStartMs = 0;
        m_pendingJumpEndMs = 0;
        m_locateMsgid = jump.m_msgid;
        m_scrollToLocated = true;
        ShowToast(TR(L"所选日期没有消息，已跳到之前最近的一条"));
        break;

    //已加载的消息里那天及以前都没有：服务端还有更早的就记为待跳日期，已经停在末页，往前拉取会
    //照常进行，每拉到一批由 RefreshContent 重试；服务端也没有了才算真的没有
    case kHistoryDateJumpNoneBefore:
        if (HasUnloadedOlderNow())
        {
            m_pendingJumpStartMs = startMs;
            m_pendingJumpEndMs = endMs;
            if (!retry)
            {
                m_pendingJumpRestarts = 0;
                ShowToast(TR(L"正在加载更早的消息，加载到所选日期后自动跳过去"));
            }
        }
        else
        {
            m_pendingJumpStartMs = 0;
            m_pendingJumpEndMs = 0;
            ShowToast(TR(L"所选日期及以前没有消息，已跳到最后一页"));
        }
        break;

    //查询失败：页号不变
    default:
        m_pendingJumpStartMs = 0;
        m_pendingJumpEndMs = 0;
        ShowToast(TR(L"跳转失败，请稍后重试"));
        break;
    }
}

void CBalloonHistoryWindow::EnterMultiSelect(int row)
{
    if (!m_multiSelect)
    {
        m_multiSelect = true;
        m_selection.Clear();
        ShowSelectBar(true);
    }
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (row >= 0 && row < (int)recs.size() && !m_selection.IsSelected(recs[(size_t)row].m_msgid))
    {
        //在线直传的文件连同本机路径一起记下，组装转发时单独处理
        std::wstring directPath;
        const bool direct = IsDirectFileRecord(recs[(size_t)row], directPath);
        m_selection.Toggle(recs[(size_t)row].m_msgid, recs[(size_t)row].m_msgtype, direct, directPath, NULL);
    }
    UpdateSelectInfo();
    LOG_INFO("history window: enter multi-select, row %d, selected %u.", row, (unsigned)m_selection.Count());
    //只换页刷新：同一批消息，按新的多选状态重建各行（行首出现复选框），滚动位置不变
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

void CBalloonHistoryWindow::ExitMultiSelect()
{
    if (!m_multiSelect)
    {
        return;
    }
    ResetMultiSelect();
    LOG_INFO("history window: leave multi-select.");
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

void CBalloonHistoryWindow::ResetMultiSelect()
{
    if (!m_multiSelect)
    {
        return;
    }
    m_multiSelect = false;
    m_selection.Clear();
    if (IsWindow())
    {
        ShowSelectBar(false);
    }
}

void CBalloonHistoryWindow::ShowSelectBar(bool show)
{
    //操作栏与分页栏都是右侧根容器的直接子控件，要对右侧根容器强制重排：客户区根容器现在是
    //左右分栏，对它 ForceLayout 时右侧根容器的矩形没变，不会再往下排（开发约定文档 UI 约定第 5 条）
    DuiControl* content = BodyRoot();
    if (content == NULL)
    {
        return;
    }
    DuiControl* bar = content->FindCtrlById(kIdHistorySelectBar);
    //底部分页栏没有自己的 id，取左侧信息文字的父容器
    DuiControl* footer = (m_pFooterInfo != NULL) ? m_pFooterInfo->GetParent() : NULL;
    if (bar != NULL)
    {
        bar->SetVisible(show);
    }
    if (footer != NULL)
    {
        footer->SetVisible(!show);
    }
    //SetVisible 不会重排（开发约定文档 UI 约定第 5 条），显隐变化后整个右侧按当前尺寸重排一次
    content->ForceLayout(content->GetRect());
    content->Invalidate();
}

void CBalloonHistoryWindow::UpdateSelectInfo()
{
    DuiControl* content = GetClientContent();
    DuiLabel* info = (content != NULL)
                   ? dynamic_cast<DuiLabel*>(content->FindCtrlById(kIdHistorySelectInfo)) : NULL;
    if (info == NULL)
    {
        return;
    }
    CString text;
    text.Format(TR(L"已选 %u 条"), (unsigned)m_selection.Count());
    info->SetText(text);
    info->Invalidate();
}

void CBalloonHistoryWindow::ToggleRowSelection(int row)
{
    const std::vector<HistoryRecord>& recs = m_model.Records();
    if (!m_multiSelect || row < 0 || row >= (int)recs.size())
    {
        return;
    }
    const HistoryRecord& rec = recs[(size_t)row];
    bool full = false;
    //在线直传的文件连同本机路径一起记下，组装转发时单独处理
    std::wstring directPath;
    const bool direct = IsDirectFileRecord(rec, directPath);
    const bool selected = m_selection.Toggle(rec.m_msgid, rec.m_msgtype, direct, directPath, &full);

    //复选框自己已经翻转了勾选状态；与选择集合不一致时（选满了、或 msgid 未分配）按集合改回来，
    //不发通知，免得又走到这里
    DuiControl* list = (m_pScroll != NULL) ? m_pScroll->GetContent() : NULL;
    DuiButton* box = (list != NULL)
                   ? dynamic_cast<DuiButton*>(list->FindCtrlById((UINT)(kIdHistorySelectBase + row))) : NULL;
    if (box != NULL && box->IsChecked() != selected)
    {
        box->SetCheck(selected, false);
    }
    if (full)
    {
        CString tip;
        tip.Format(TR(L"最多只能选 %u 条"), (unsigned)net::kForwardMaxMessages);
        ShowToast(tip);
    }
    UpdateSelectInfo();
}

void CBalloonHistoryWindow::ForwardSelection(bool merge)
{
    if (m_selection.Count() == 0)
    {
        ShowToast(TR(L"请先勾选要转发的消息"));
        return;
    }
    //按转发模式整理：在线直传的文件逐条转发时本机有文件的改为重新发送、没有的单独计数（2026-10-05 起）
    balloonmain::ForwardRequest collected;
    m_selection.BuildForward(merge, collected);
    if (collected.m_msgids.empty() && collected.m_reuploadFiles.empty())
    {
        //选中的只有本机已没有文件的在线直传文件时，说清楚原因
        const bool onlyDirect = collected.m_skippedDirect > 0 && collected.m_skipped == 0;
        ShowToast(onlyDirect ? TR(L"在线直传的文件无法转发") : TR(L"选中的消息都不能转发"));
        return;
    }
    //目标选择窗与转发调度都在主面板，调试入口没有主面板
    if (m_pMainPanel == NULL || !m_pMainPanel->IsWindow())
    {
        LOG_INFO("history window cannot forward selection: no main panel.");
        return;
    }

    //m_srcWnd 填本窗口，主面板据此在本窗口上显示转发结果；合并转发的标题由主面板生成
    const HistoryFilter& filter = m_model.Filter();
    balloonmain::ForwardRequest* pReq = new balloonmain::ForwardRequest(collected);
    pReq->m_srcCtype = filter.m_ctype;
    pReq->m_srcCid = filter.m_cid;
    pReq->m_srcWnd = m_hWnd;
    if (!::PostMessage(m_pMainPanel->m_hWnd, FMG_MSG_OPEN_FORWARD_PICKER, 0, (LPARAM)pReq))
    {
        delete pReq;
        LOG_ERROR("history window: failed to post forward request for %u messages.", (unsigned)collected.m_msgids.size());
        return;
    }
    LOG_INFO("history window requests forward of selection, merge %d, count %u, skipped %u, reupload %u, "
             "skippeddirect %u, directplaceholders %u.",
             (int)merge, (unsigned)collected.m_msgids.size(), (unsigned)collected.m_skipped,
             (unsigned)collected.m_reuploadFiles.size(), (unsigned)collected.m_skippedDirect,
             (unsigned)collected.m_directPlaceholders);
    ExitMultiSelect();
}

void CBalloonHistoryWindow::DeleteSelection()
{
    if (m_selection.Count() == 0)
    {
        ShowToast(TR(L"请先勾选要删除的消息"));
        return;
    }
    //调试入口用的是预置假库、没有服务端可问
    if (m_pMainPanel == NULL)
    {
        LOG_INFO("history window cannot delete selection: no main panel.");
        return;
    }
    const std::vector<int64_t> msgids = m_selection.SelectedMsgids();
    const int answer = ::MessageBox(m_hWnd,
        balloonchat::BuildDeleteMessagesConfirmText(msgids.size()),
        TR(L"删除消息"), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer != IDOK)
    {
        return;
    }
    //与聊天窗多选删除同口径：逐条发出，本地库删行、本窗刷新等服务端推送删除通知回来再做
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        CIUSocket::GetInstance().DeleteMessage(msgids[i]);
    }
    LOG_INFO("history window requests delete of %u selected messages.", (unsigned)msgids.size());
    ExitMultiSelect();
}

void CBalloonHistoryWindow::RefreshSenderCombo()
{
    DuiControl* content = GetClientContent();
    DuiComboBox* combo = (content != NULL)
                       ? dynamic_cast<DuiComboBox*>(content->FindCtrlById(kIdHistorySenderCombo)) : NULL;
    if (combo == NULL)
    {
        return;
    }

    std::vector<int64_t> senders;
    if (!m_model.QuerySenders(senders))
    {
        LOG_WARNING("history window: query senders failed, ctype=%d, cid=%lld.",
                    m_model.Filter().m_ctype, (long long)m_model.Filter().m_cid);
    }

    //整份重建：第 0 项「全部成员」，其余按查询结果的顺序（最近发过言的在前）
    combo->ResetContent();
    m_senderIds.clear();
    combo->AddString(TR(kHistorySenderAllText));
    m_senderIds.push_back(0);
    for (size_t i = 0; i < senders.size(); ++i)
    {
        combo->AddString(ResolveSenderName(senders[i]));
        m_senderIds.push_back(senders[i]);
    }

    //选中模型当前的发送人。找不到（这个人的消息都被删掉或撤回了）就改回「全部成员」，
    //否则下拉显示的与实际的筛选条件对不上
    const int64_t current = m_model.Filter().m_fromid;
    int sel = 0;
    for (size_t i = 0; i < m_senderIds.size(); ++i)
    {
        if (m_senderIds[i] == current)
        {
            sel = (int)i;
            break;
        }
    }
    combo->SetCurSel(sel, false);
    if (current > 0 && sel == 0)
    {
        m_model.SetSenderFilter(0);
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
    }
    LOG_INFO("history window: sender list refreshed, ctype=%d, cid=%lld, senders=%u, selected %lld.",
             m_model.Filter().m_ctype, (long long)m_model.Filter().m_cid,
             (unsigned)senders.size(), (long long)m_model.Filter().m_fromid);
}

LRESULT CBalloonHistoryWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonHistoryWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& bHandled)
{
    //从图片缓存注销，避免解码完成后向已销毁的窗口句柄投递消息。放在 WM_DESTROY 而不是
    //OnCloseMsg 里：主面板登出时会直接 DestroyWindow，不经过 WM_CLOSE。
    balloonchatwnd::ChatImageCache::Inst().UnregisterListener(m_hWnd);
    LOG_INFO("history window: unregistered from image cache, hwnd=0x%p.", m_hWnd);
    //帖子卡片图片的下载：删除队列里还没送达的结果对象，作废全部登记（之后到达的结果会因窗口已销毁而由传输层删除）
    MSG pending;
    while (::PeekMessage(&pending, m_hWnd, WM_HISTORY_CARD_IMAGE_DONE, WM_HISTORY_CARD_IMAGE_DONE, PM_REMOVE))
    {
        delete reinterpret_cast<net::PlainTransferResult*>(pending.lParam);
    }
    m_cardRequests.clear();
    m_cardDownloading.clear();
    m_cardFailed.clear();
    m_cardRefreshPosted = false;
    m_cardViewImages.clear();
    m_cardViewIndex = 0;
    m_cardPendingViewMd5.clear();
    g_historyCardNavigator.SetTarget(NULL);
    //本窗口正在播放语音时一并停止，之后语音中心不再给本窗口发播放通知（语音消息第 4b 步）
    balloonvoice::CVoiceHub::GetInstance().DetachPlayListener(this);
    m_playingVoiceMsgid = 0;
    //同理放在 WM_DESTROY：登出时直接销毁窗口，也要记下位置
    SaveWindowPlacement();
    //窗口没了，「全部下载」那一批的汇总提示也无处显示，作废
    AbandonBatchDownload("window destroyed");
    //左栏跨会话搜索：已投进消息队列、还没处理的结果在这里释放；之后才查完的，后台线程投递失败时自己释放
    const int drained = CV2MsgSearchWorker::DrainPendingResponses(m_hWnd, WM_HISTORY_MGR_SEARCH_DONE);
    m_mgrSearchSeq = 0;
    if (drained > 0)
    {
        LOG_INFO("history manager: %d pending search result(s) released on destroy.", drained);
    }
    //时间轴统计：同上，释放已投进消息队列的结果，在途的那一次不再等
    const int drainedStats = CV2MsgSearchWorker::DrainPendingResponses(m_hWnd, WM_HISTORY_TIMELINE_STATS_DONE);
    m_timelineStatsSeq = 0;
    m_timelineJumpPending = false;
    if (drainedStats > 0)
    {
        LOG_INFO("history timeline: %d pending stats result(s) released on destroy.", drainedStats);
    }
    //文件页：还没有结果的群文件下载照常在后台跑完（落地路径由主面板登记），本窗口不再跟踪；合并刷新作废
    m_libGroupDownloads.clear();
    m_libRefreshPending = false;
    //本机存储占用页：同上，释放已投进消息队列的统计或清理结果
    const int drainedStorage = CV2MsgSearchWorker::DrainPendingResponses(m_hWnd, WM_HISTORY_STORAGE_DONE);
    m_storageSeq = 0;
    if (drainedStorage > 0)
    {
        LOG_INFO("history storage: %d pending result(s) released on destroy.", drainedStorage);
    }
    //导出：让后台线程中止并等它结束，释放已投进消息队列的结果
    StopExportThread();
    //只旁听，不消费：让 DuiFrameWindow 继续做它自己的销毁收尾。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonHistoryWindow::OnWindowPosChangedMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                     LPARAM /*lParam*/, BOOL& bHandled)
{
    //最大化或最小化时的窗口矩形不是还原态的矩形，不记
    if (!IsZoomed() && !IsIconic())
    {
        GetWindowRect(&m_lastNormalRect);
        m_hasNormalRect = true;
    }
    bHandled = FALSE;
    return 0;
}

//EnumDisplayMonitors 的回调：把每台显示器的工作区追加到 lParam 指向的 std::vector<RECT>。
static BOOL CALLBACK CollectMonitorWorkAreaProc(HMONITOR hMonitor, HDC /*hdc*/, LPRECT /*rc*/, LPARAM lParam)
{
    std::vector<RECT>* areas = reinterpret_cast<std::vector<RECT>*>(lParam);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(hMonitor, &mi))
    {
        areas->push_back(mi.rcWork);
    }
    return TRUE;
}

bool CBalloonHistoryWindow::RestoreWindowPlacement()
{
    if (m_lpFMGClient == NULL)
    {
        return false;
    }
    const winplace::WindowPlacementPrefs& saved = m_lpFMGClient->m_UserConfig.GetHistoryDlgPlacement();
    if (!saved.m_hasSaved)
    {
        LOG_INFO("history window: no saved placement, keep default size centered.");
        return false;
    }

    //默认大小取 ResizeClient 之后的窗口大小；最小大小与 SetMinSize 一致，都是物理像素
    RECT current;
    GetWindowRect(&current);
    SIZE defaultSize;
    defaultSize.cx = current.right - current.left;
    defaultSize.cy = current.bottom - current.top;

    std::vector<RECT> workAreas;
    ::EnumDisplayMonitors(NULL, NULL, CollectMonitorWorkAreaProc, reinterpret_cast<LPARAM>(&workAreas));
    MONITORINFO fallback;
    fallback.cbSize = sizeof(fallback);
    ::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), &fallback);

    const RECT rc = winplace::ResolveRestoredWindowRect(saved, defaultSize, GetMinSize(),
                                                        workAreas, fallback.rcWork);
    SetWindowPos(NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    LOG_INFO("history window: restore placement saved=(%ld,%ld %ldx%ld) -> (%ld,%ld %ldx%ld), "
             "maximized %d, monitors %u.",
             saved.m_rect.left, saved.m_rect.top, saved.m_rect.right - saved.m_rect.left,
             saved.m_rect.bottom - saved.m_rect.top, rc.left, rc.top, rc.right - rc.left,
             rc.bottom - rc.top, (int)saved.m_maximized, (unsigned)workAreas.size());
    return saved.m_maximized;
}

void CBalloonHistoryWindow::SaveWindowPlacement()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    //左栏的宽度与折叠状态与窗口矩形无关，先记下（窗口一次都没有处于还原态时也要记）
    m_lpFMGClient->m_UserConfig.SetHistoryMgrPaneLayout(m_mgrPaneWidth, m_mgrCollapsed);
    if (!m_hasNormalRect)
    {
        m_lpFMGClient->SaveUserConfig();
        LOG_INFO("history manager: save pane width %d, collapsed %d.", m_mgrPaneWidth, (int)m_mgrCollapsed);
        return;
    }
    winplace::WindowPlacementPrefs prefs;
    prefs.m_hasSaved = true;
    prefs.m_rect = m_lastNormalRect;
    //最小化时关窗（例如在任务栏上右键关闭）：按最小化之前是否最大化来记
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    const bool restoreToMax = IsIconic() && GetWindowPlacement(&wp) && (wp.flags & WPF_RESTORETOMAXIMIZED) != 0;
    prefs.m_maximized = (IsZoomed() != FALSE) || restoreToMax;

    m_lpFMGClient->m_UserConfig.SetHistoryDlgPlacement(prefs);
    m_lpFMGClient->SaveUserConfig();
    LOG_INFO("history window: save placement (%ld,%ld %ldx%ld), maximized %d, pane width %d, collapsed %d.",
             prefs.m_rect.left, prefs.m_rect.top, prefs.m_rect.right - prefs.m_rect.left,
             prefs.m_rect.bottom - prefs.m_rect.top, (int)prefs.m_maximized, m_mgrPaneWidth,
             (int)m_mgrCollapsed);
}

LRESULT CBalloonHistoryWindow::OnImageReadyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //某张缩略图在后台解码完成：重画即可，缩略图这次会在缓存里命中、画出真图。
    //缩略图尺寸是固定的，不需要重排。
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}

LRESULT CBalloonHistoryWindow::OnRefreshContentMsg(UINT /*uMsg*/, WPARAM wParam,
                                                   LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //卡片图片的下载结果投递的那次刷新执行到了，之后的结果可以再投递
    m_cardRefreshPosted = false;
    //只有明确标了"只换页"才省掉统计总数；其余取值（含将来新增的投递点忘了带标记）一律完整刷新
    RefreshContent(wParam == (WPARAM)kHistoryRefreshPageOnly);
    return 0;
}

//取值变化由用户直接操作产生的那几个筛选与翻页控件：搜索框、时间与发送人下拉、跳页微调框
//本体（▲▼）。其余控件的 DUIN_VALUECHANGED（滚动条、跳页框内部输入框等）可能由程序改写或
//滚动触发，不算用户的"下一次操作"。「选日期」、日期区间标签都是按钮，走点击，不在此列。
static bool IsUserFilterControl(UINT id)
{
    return id == kIdHistorySearch
        || id == kIdHistoryTimeCombo
        || id == kIdHistorySenderCombo
        || id == kIdHistoryPageJump;
}

LRESULT CBalloonHistoryWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    DuiControl* content = GetClientContent();
    UINT id = pNotify->ctrlId;

    //用户操作算"下一次操作"，撤掉「查看上下文」「跳到这一天」留下的定位高亮：所有点击，以及
    //搜索框、各下拉框、跳页微调框的取值变化。取值变化只认这几个控件（见 IsUserFilterControl），
    //不能来者不拒：列表滚动时滚动条会发 DUIN_VALUECHANGED（滚动只是在看上下文，不算操作，
    //而且滚到高亮行那一下本身就会触发它）；每次刷新同步页号时程序改写跳页框内部输入框的文字，
    //它也会发（2026-10-01 实测，两者都会让高亮刚加上就被撤掉）。手输页号的提交走 JumpToPage，在那里撤
    const bool userValueChange = (pNotify->code == DUIN_VALUECHANGED && IsUserFilterControl(id));
    if (pNotify->code == DUIN_CLICK || userValueChange)
    {
        ClearLocatedRow();
    }

    // ---- 聊天记录管理器左栏（编号都在 kIdMgr* 段，判定时通知码与编号一起比对） ----

    //本机存储占用：左栏最下面的入口与存储页里的按钮（docs/聊天记录管理器方案.md §12.6）
    if (HandleStorageNotify(pNotify))
    {
        return 0;
    }
    //停在存储页时点了左栏的标签、会话或命中行：先离开存储页，再照原来的分发处理这次点击
    if (m_storageMode && pNotify->code == DUIN_CLICK
        && ((id >= (UINT)kIdMgrTabChat && id <= (UINT)kIdMgrTabLinks) || id == (UINT)kIdMgrConvTree
            || id == (UINT)kIdMgrHitTree))
    {
        LeaveStoragePage();
    }

    //左栏标签：聊天 / 文件 / 图片 / 链接（编号连续，5130..5133 与 HistoryManagerTab 的 0～3 一一对应）
    if (pNotify->code == DUIN_CLICK && id >= (UINT)kIdMgrTabChat && id <= (UINT)kIdMgrTabLinks)
    {
        SwitchManagerTab((int)(id - kIdMgrTabChat));
        return 0;
    }
    //停在文件 / 图片页时，本页的控件与左栏会话列表的单击由 HandleLibraryNotify 处理；不是本页的控件返回 false，
    //照原来的分发继续往下走
    if (HandleLibraryNotify(pNotify))
    {
        return 0;
    }

    //左栏会话树单击某行：切到那个会话
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdMgrConvTree)
    {
        OnManagerConvClicked((int)pNotify->extra);
        return 0;
    }
    //左栏命中树单击某行：切到那个会话，并把搜索词带进右侧搜索框
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdMgrHitTree)
    {
        OnManagerHitClicked((int)pNotify->extra);
        return 0;
    }
    //左栏会话树右键某行：等鼠标事件解栈之后再弹菜单（理由见 WM_HISTORY_SHOW_MGR_MENU）
    if (pNotify->code == (UINT)DuiTreeView::DUITVN_RCLICK && id == (UINT)kIdMgrConvTree)
    {
        POINT ptScreen;
        ::GetCursorPos(&ptScreen);
        ::PostMessage(m_hWnd, WM_HISTORY_SHOW_MGR_MENU, (WPARAM)pNotify->extra,
                      MAKELPARAM((WORD)ptScreen.x, (WORD)ptScreen.y));
        return 0;
    }
    //「«」折叠左栏 /「»」展开左栏
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdMgrCollapse)
    {
        SetManagerCollapsed(true);
        return 0;
    }
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdHistoryExpandLeft)
    {
        SetManagerCollapsed(false);
        return 0;
    }
    //类型筛选：编号连续，5110..5112 → ManagerTypeFilter 0..2
    if (pNotify->code == DUIN_CLICK && id >= (UINT)kIdMgrTypeAll && id <= (UINT)kIdMgrTypeGroup)
    {
        m_mgrTypeFilter = (int)(id - kIdMgrTypeAll);
        ApplyHistoryManagerTypeChips(content, m_mgrTypeFilter);
        LOG_INFO("history manager: type filter -> %d.", m_mgrTypeFilter);
        FillManagerTrees();
        //文件 / 图片页：「全部会话」随之只看单聊或群聊（ManagerTypeFilter 的 0/1/2 与 m_ctypeOnly 取值相同）
        if (m_mgrTab != kHistoryManagerTabChat)
        {
            m_libModel.SetCtypeOnly(m_mgrTypeFilter);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
        }
        return 0;
    }
    //范围按钮：打开勾选范围的弹层
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdMgrScope)
    {
        OpenManagerScopePopup();
        return 0;
    }
    //范围按钮右侧的「×」：恢复为全部会话
    if (pNotify->code == DUIN_CLICK && id == (UINT)kIdMgrScopeClear)
    {
        ApplyManagerScope(std::vector<HistoryConversationKey>());
        return 0;
    }
    //左栏搜索框文字变化：停止输入一段时间后再查（会跨会话查本地库）
    if (pNotify->code == DUIN_VALUECHANGED && id == (UINT)kIdMgrSearch)
    {
        SetTimer(kMgrSearchTimerId, kMgrSearchDelayMs, NULL);
        return 0;
    }
    //左栏「通配符」复选框：切换后按同一个搜索词重新跨会话搜索（extra 为 1 勾选、0 取消）
    if (pNotify->code == DUIN_VALUECHANGED && id == (UINT)kIdMgrWildcard)
    {
        SetManagerWildcard(pNotify->extra != 0);
        return 0;
    }
    //拖完分隔条：记下左栏宽度（extra 为左栏的像素宽度），右侧列表按新宽度重新折行
    if (pNotify->code == DUIN_VALUECHANGED && id == (UINT)kIdMgrSplitter)
    {
        if (!m_mgrCollapsed && (int)pNotify->extra > 0)
        {
            m_mgrPaneWidth = (int)pNotify->extra;
            LOG_INFO("history manager: pane width -> %d.", m_mgrPaneWidth);
        }
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
        return 0;
    }

    //改完模型状态后统一 PostMessage 延迟刷新——避免在被点击控件（页码按钮等）的事件
    //回调里销毁它本身。
    if (pNotify->code == DUIN_CLICK)
    {
        //类型胶囊：id 连续，510..516 → 类型枚举 0..6（末尾那枚是 Pin）。
        if (id >= kIdHistoryChipAll && id <= kIdHistoryChipPin)
        {
            m_model.SetTypeFilter((HistoryTypeFilter)(id - kIdHistoryChipAll));
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            return 0;
        }
        //搜索行最右边的「列表 | 时间轴」（docs/聊天记录管理器方案.md §11.7）
        if (id == (UINT)kIdHistoryViewList || id == (UINT)kIdHistoryViewTimeline)
        {
            SetTimelineMode(id == (UINT)kIdHistoryViewTimeline);
            return 0;
        }
        //命中条的「▲ 上一处」「▼ 下一处」
        if (id == (UINT)kIdHistoryHitPrev || id == (UINT)kIdHistoryHitNext)
        {
            GoToHit(id == (UINT)kIdHistoryHitNext);
            return 0;
        }
        //分页固定按钮与下面的页码按钮：只换页，刷新时沿用上次统计的总数。
        if (id == kIdHistoryPageFirst)
        {
            m_model.GoToPage(1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
            return 0;
        }
        if (id == kIdHistoryPagePrev)
        {
            m_model.GoToPage(m_model.CurrentPage() - 1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
            return 0;
        }
        if (id == kIdHistoryPageNext)
        {
            m_model.GoToPage(m_model.CurrentPage() + 1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
            return 0;
        }
        if (id == kIdHistoryPageLast)
        {
            m_model.GoToPage(m_model.PageCount());
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
            return 0;
        }
        //页码按钮：id = 基址 + (页号-1)。上界按<u>本段自己的容量</u>算，不拿下一段的
        //基址当界 —— 那样一旦本段用满就会溢进下一段、被别的分支接走（见
        //HistoryWindowLayout.h 里三段 id 的说明）。
        if (id >= (UINT)kIdHistoryPageNumBase
            && id < (UINT)kIdHistoryPageNumBase + kIdHistoryPageNumCapacity)
        {
            m_model.GoToPage((int)(id - kIdHistoryPageNumBase) + 1);
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
            return 0;
        }
        //导出按钮：把当前筛选条件下的全部消息导出为 .txt（弹另存对话框，同步阻塞；
        //导出按钮自身不会被刷新销毁，无 use-after-free 风险）。
        if (id == kIdHistoryExport)
        {
            ExportHistory();
            return 0;
        }
        //「清空聊天记录」：清空当前查看的会话
        if (id == kIdHistoryClearConversation)
        {
            RequestClearConversation();
            return 0;
        }
        //「选日期」：在按钮下方打开「从 / 到」日期弹层，提交后由 JumpToDay 或 FilterByDateRange 处理
        if (id == kIdHistoryDatePick)
        {
            OpenDateJumpPopup(kIdHistoryDatePick);
            return 0;
        }
        //日期区间标签上的日期文字：在标签下方重新打开弹层，预置为当前区间，便于修改
        if (id == kIdHistoryDateRangeLabel)
        {
            OpenDateJumpPopup(kIdHistoryDateRangeLabel);
            return 0;
        }
        //日期区间标签的关闭叉号：清掉日期筛选，回到「全部时间」。叉号只是随标签隐藏、不会被销毁
        if (id == kIdHistoryDateRangeClear)
        {
            LOG_INFO("history window: date range filter cleared.");
            m_model.SetTimeRange(kHistoryTimeAll);
            SyncTimeFilterControls();
            ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
            return 0;
        }
        //「图片」分类网格里的缩略图：看大图，本地没有时先下载、下完自动打开
        if (id >= (UINT)kIdHistoryGridBase && id < (UINT)kIdHistoryGridBase + kIdHistoryGridCapacity)
        {
            //格子按 m_gridCells 换算回记录：帖子卡片里的图片按卡片看图（可左右翻看），其余是图片消息本身
            const size_t cell = (size_t)(id - kIdHistoryGridBase);
            if (cell < m_gridCells.size())
            {
                if (m_gridCells[cell].m_image >= 0)
                {
                    OpenRowCardImage(m_gridCells[cell].m_row, m_gridCells[cell].m_image);
                }
                else
                {
                    ViewRowImage(m_gridCells[cell].m_row);
                }
            }
            return 0;
        }
        //「文件」分类顶部的「全部下载」
        if (id == kIdHistoryBatchDownload)
        {
            DownloadAllFilesOnPage();
            return 0;
        }
        //多选操作栏的四个按钮
        if (id == kIdHistorySelectMerge)
        {
            ForwardSelection(true);
            return 0;
        }
        if (id == kIdHistorySelectEach)
        {
            ForwardSelection(false);
            return 0;
        }
        if (id == kIdHistorySelectDelete)
        {
            DeleteSelection();
            return 0;
        }
        if (id == kIdHistorySelectCancel)
        {
            ExitMultiSelect();
            return 0;
        }
        //行内“定位”按钮：跳到聊天窗并滚动定位到该消息（仅真实会话、有主面板时生效；
        //调试入口 m_pMainPanel==NULL 不处理）。按钮 id = 基址 + 当前页行下标。
        if (id >= (UINT)kIdHistoryLocateBase
            && id < (UINT)kIdHistoryLocateBase + kIdHistoryLocateCapacity)
        {
            LocateRow((int)(id - kIdHistoryLocateBase));
            return 0;
        }
        //帖子卡片下的缩略图：看图（本机没有时先下载，下完再打开），可以左右翻看这张卡片的全部图片。
        //编号 = 基址 + 行下标 × 每行张数 + 第几张（docs/帖子群方案.md 14.5）
        int cardRow = 0;
        int cardThumb = 0;
        if (ResolveHistoryCardThumbClick(id, cardRow, cardThumb))
        {
            OpenRowCardImage(cardRow, cardThumb);
            return 0;
        }
        //行内图片缩略图：本地没有原图时单击下载（下载失败后单击重试）。只下载、不打开
        //看图窗；要看大图请双击这一行（缩略图会把双击转交给行）。
        //缩略图 id = 基址 + 当前页行下标；只有图片行才有，同一页里是稀疏的。
        if (id >= (UINT)kIdHistoryThumbBase
            && id < (UINT)kIdHistoryThumbBase + kIdHistoryThumbCapacity)
        {
            DownloadRowThumb((int)(id - kIdHistoryThumbBase));
            return 0;
        }
        //行内聊天记录卡片的标题链接：打开聊天记录查看窗。链接 id = 基址 + 当前页行下标，
        //只有合并转发的聊天记录行才有
        if (id >= (UINT)kIdHistoryCardBase
            && id < (UINT)kIdHistoryCardBase + kIdHistoryCardCapacity)
        {
            OpenRowMergedRecord((int)(id - kIdHistoryCardBase));
            return 0;
        }
        //行内“展开 / 收起”链接：翻转该条的展开态并局部刷新。
        //
        //与“定位 / Pin”不同，这一个<u>纯粹是本地视图状态</u>，不发任何请求、不等服务端。
        //状态记在模型里（按 msgid），故翻页离开再翻回来仍然是展开的。
        //链接 id = 基址 + 当前页行下标；只有正文超出折叠行数的行才有这个控件，
        //故这一段的 id 在同一页里是稀疏的，不能假设“第 i 个链接就是第 i 行”。
        if (id >= (UINT)kIdHistoryExpandBase
            && id < (UINT)kIdHistoryExpandBase + kIdHistoryExpandCapacity)
        {
            const int row = (int)(id - kIdHistoryExpandBase);
            const std::vector<HistoryRecord>& recs = m_model.Records();
            if (row >= 0 && row < (int)recs.size())
            {
                m_model.ToggleExpanded(recs[(size_t)row].m_msgid);
                //行高变了，整个列表要重建。延迟到事件解栈之后再做 —— 与其它交互同路子，
                //避免在 DUI 通知的同步派发栈里销毁刚刚发出通知的那个控件。
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, 0, 0);
            }
            return 0;
        }
        //行内“Pin / 取消”按钮与时间右侧的图钉：只发请求，界面等服务端推
        //cmd_message_pinned_notify 回来、主面板刷新本窗口时才变——与气泡右键菜单那条路
        //完全同口径，不做乐观更新。两者的 id 各占一段（基址 + 当前页行下标），按钮按当前
        //状态取反、图钉恒为取消，解析统一交给 ResolveHistoryPinClick。
        {
            const std::vector<HistoryRecord>& recs = m_model.Records();
            std::vector<bool> pagePinned;
            pagePinned.reserve(recs.size());
            for (size_t i = 0; i < recs.size(); ++i)
            {
                pagePinned.push_back(recs[i].m_pinned);
            }
            int  row       = -1;
            bool targetPin = false;
            if (ResolveHistoryPinClick(id, pagePinned, row, targetPin))
            {
                const HistoryFilter& filter = m_model.Filter();
                const HistoryRecord& rec = recs[(size_t)row];
                LOG_INFO("history window: pin click, ctrlId=%u, row=%d, msgid=%lld, pin=%d.",
                         id, row, (long long)rec.m_msgid, targetPin ? 1 : 0);
                //调试入口用的是预置假库、没有真实会话，发出去也没意义。
                if (m_pMainPanel != NULL && rec.m_msgid > 0)
                {
                    CIUSocket::GetInstance().PinMessage(filter.m_ctype, filter.m_cid,
                                                        (long long)rec.m_msgid, targetPin);
                }
                return 0;
            }
        }
    }
    else if (pNotify->code == DUIN_VALUECHANGED)
    {
        //搜索框文字变化：不立即查询，(重新)开始计时，停止输入 kSearchDebounceMs 后在 OnTimerMsg
        //里查询（搜索框持久、不会被刷新重建，到时再读它的最新文字即可）
        if (id == kIdHistorySearch && content != NULL)
        {
            SetTimer(kSearchDebounceTimerId, kSearchDebounceMs, NULL);
            return 0;
        }
        //「通配符」复选框：关键字的解释方式变了，有关键字时按新方式重查（extra 为 1 勾选、0 取消）
        if (id == kIdHistoryWildcard)
        {
            ApplyRightWildcard(pNotify->extra != 0);
            return 0;
        }
        //时间下拉切换：只有 6 个预设区间。自定义起止由「选日期」弹层设置，生效期间下拉隐藏
        if (id == kIdHistoryTimeCombo && content != NULL)
        {
            DuiComboBox* combo = dynamic_cast<DuiComboBox*>(content->FindCtrlById(kIdHistoryTimeCombo));
            if (combo != NULL)
            {
                m_model.SetTimeRange(TimeRangeFromComboIndex(combo->GetCurSel()));
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
            }
            return 0;
        }
        //行首复选框勾选或取消：id = 基址 + 当前页行下标。复选框随后还会发一条 DUIN_CLICK，
        //点击分支不处理这一段 id，不会重复切换
        if (id >= (UINT)kIdHistorySelectBase
            && id < (UINT)kIdHistorySelectBase + kIdHistorySelectCapacity)
        {
            ToggleRowSelection((int)(id - kIdHistorySelectBase));
            return 0;
        }
        //发送人下拉切换：按选中项对应的 userid 筛选（第 0 项「全部成员」对应 0，即不限）
        if (id == kIdHistorySenderCombo && content != NULL)
        {
            DuiComboBox* combo = dynamic_cast<DuiComboBox*>(content->FindCtrlById(kIdHistorySenderCombo));
            const int sel = (combo != NULL) ? combo->GetCurSel() : -1;
            if (sel >= 0 && sel < (int)m_senderIds.size())
            {
                m_model.SetSenderFilter(m_senderIds[(size_t)sel]);
                LOG_INFO("history window: sender filter -> %lld.", (long long)m_senderIds[(size_t)sel]);
                ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
            }
            return 0;
        }
        //跳页微调框的 ▲▼ 被点：extra 是新值，直接翻过去。手输入的提交走下面的
        //KILLFOCUS 分支（DuiSpinBox 不会把手输入自动同步到自身的值）。
        if (id == kIdHistoryPageJump)
        {
            JumpToPage((int)pNotify->extra);
            return 0;
        }
    }
    else if (pNotify->code == DUIN_DBLCLK)
    {
        //双击整行。行 id = 基址 + 当前页行下标，与「定位」按钮认的是同一个下标。
        if (id >= (UINT)kIdHistoryRowBase
            && id < (UINT)kIdHistoryRowBase + kIdHistoryRowCapacity)
        {
            OnRowDoubleClicked((int)(id - kIdHistoryRowBase));
            return 0;
        }
        //双击落在行内没有编号的子控件上（昵称、时间、行内空白）：收到双击的是那个子
        //控件，由它以自己的编号（0）发出通知。沿父链找回所在的行，按双击整行处理。
        //有编号的控件（Pin / 定位按钮等）不走这条路，它们的双击另有含义或不处理；
        //正文文字上的双击由正文控件自己处理为选中整条，不会发出这条通知。
        if (id == 0 && pNotify->pCtrl != NULL)
        {
            const int row = HistoryRowIndexOfControl(
                static_cast<const DuiControl*>(pNotify->pCtrl));
            if (row >= 0)
            {
                OnRowDoubleClicked(row);
                return 0;
            }
        }
    }
    else if (pNotify->code == DUIN_KILLFOCUS)
    {
        //跳页微调框内部 EDIT 失焦：把用户手敲的页号提交。放在失焦而不是每键提交，
        //是因为敲 "137" 的过程中会依次经过 1、13，每一步都翻一次页毫无意义。
        if (id == kIdHistoryPageJumpEdit && content != NULL)
        {
            DuiSpinBox* jump = dynamic_cast<DuiSpinBox*>(content->FindCtrlById(kIdHistoryPageJump));
            if (jump != NULL && jump->GetEdit() != NULL)
            {
                JumpToPage(_ttoi(jump->GetEdit()->GetText()));
            }
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}

// =====================================================================
// 聊天记录管理器左栏（docs/聊天记录管理器方案.md）
// =====================================================================

DuiControl* CBalloonHistoryWindow::BodyRoot() const
{
    DuiControl* content = GetClientContent();
    return (content != NULL) ? content->FindCtrlById(kIdHistoryBody) : NULL;
}

CV2MsgStore* CBalloonHistoryWindow::ManagerStore()
{
    if (m_pStore != NULL)
    {
        return m_pStore;
    }
    //调试入口：用预置的假库，左栏里列出假库里的会话
    return m_seedStore.IsOpen() ? &m_seedStore : NULL;
}

void CBalloonHistoryWindow::ApplyManagerMinSize(bool growIfNeeded)
{
    const int minW = m_mgrCollapsed
                   ? (kHisMinWidth + kMgrSplitBarCollapsedW)
                   : (kMgrPaneMinW + kMgrSplitBarW + kMgrBodyMinW);
    SetMinSize(minW, kHisMinHeight);
    if (!growIfNeeded || !IsWindow() || IsZoomed() || IsIconic())
    {
        return;
    }
    RECT rc;
    GetWindowRect(&rc);
    if (rc.right - rc.left < minW)
    {
        //展开左栏后窗口比新的最小宽度窄：向右加宽，左上角不动
        SetWindowPos(NULL, 0, 0, minW, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        LOG_INFO("history manager: window widened to %d for the expanded pane.", minW);
    }
}

void CBalloonHistoryWindow::NotifyConversationsChanged()
{
    if (!IsWindow() || m_mgrRebuildPending)
    {
        return;
    }
    m_mgrRebuildPending = true;
    SetTimer(kMgrRebuildTimerId, kMgrRebuildDelayMs, NULL);
}

void CBalloonHistoryWindow::RebuildManagerItems()
{
    PerfStopWatch sw;
    std::vector<ManagerServerConversation> server;
    std::set<int64_t> myGroups;
    bool groupsLoaded = false;
    if (m_pMainPanel != NULL)
    {
        m_pMainPanel->CollectHistoryManagerSources(server, myGroups, groupsLoaded);
    }
    std::vector<LocalConversationBrief> local;
    CV2MsgStore* store = ManagerStore();
    if (store != NULL && !store->QueryConversationList(local))
    {
        LOG_ERROR("history manager: query local conversation list failed.");
    }
    const double queryMs = sw.ElapsedMs();

    m_mgrItems = MergeManagerConversations(server, local, myGroups, groupsLoaded);
    for (size_t i = 0; i < m_mgrItems.size(); ++i)
    {
        CString name;
        CString timeText;
        HBITMAP avatar = NULL;
        ResolveManagerRow(m_mgrItems[i].m_ctype, m_mgrItems[i].m_cid, m_mgrItems[i].m_lastTimestamp,
                          name, timeText, avatar);
        m_mgrItems[i].m_name = (LPCTSTR)name;
    }
    LOG_INFO("history manager: merged %u server + %u local -> %u conversations, groups loaded %d, "
             "local query %.1f ms, total %.1f ms.",
             (unsigned)server.size(), (unsigned)local.size(), (unsigned)m_mgrItems.size(),
             (int)groupsLoaded, queryMs, sw.ElapsedMs());
}

void CBalloonHistoryWindow::RebuildManagerList()
{
    //重建前右侧显示的会话在不在左栏里、在第几行：重建后它不见了就切到相邻的那一行
    const int curCtype = m_model.Filter().m_ctype;
    const int64_t curCid = m_model.Filter().m_cid;
    const int oldIndex = FindManagerConversation(m_mgrItems, curCtype, curCid);

    RebuildManagerItems();
    if (IsWindow())
    {
        FillManagerTrees();
    }

    if (oldIndex >= 0 && FindManagerConversation(m_mgrItems, curCtype, curCid) < 0)
    {
        const int next = ResolveManagerSelection(m_mgrItems, curCtype, curCid, oldIndex);
        LOG_INFO("history manager: current conversation ctype=%d, cid=%lld left the list, switch to row %d.",
                 curCtype, (long long)curCid, next);
        if (next >= 0)
        {
            const ManagerConversationItem item = m_mgrItems[(size_t)next];
            SwitchConversation(item.m_ctype, item.m_cid, CString(item.m_name.c_str()), std::string());
        }
        else
        {
            //左栏一个会话都不剩：右侧不再显示任何会话，只留空状态
            SwitchConversation(1, 0, CString(), std::string());
        }
    }
}

void CBalloonHistoryWindow::FillManagerTrees()
{
    //文件 / 图片 / 链接页：左栏是「全部会话」+ 按会话计数的列表
    if (!ManagerShowsChatList())
    {
        FillLibraryManagerTree();
        return;
    }
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonmain::MainPanelTreeView* convTree =
        dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(kIdMgrConvTree));
    balloonmain::MainPanelTreeView* hitTree =
        dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(kIdMgrHitTree));
    if (convTree == NULL || hitTree == NULL)
    {
        return;
    }
    PerfStopWatch sw;

    //1. 会话：按类型、名字与范围筛选。不在搜索时第一行是「与我有关」（docs/聊天记录管理器方案.md §12.5）
    convTree->Clear();
    m_mgrConvNodes.clear();
    m_mentionsNode = -1;
    if (m_mgrQuery.empty())
    {
        m_mentionsNode = convTree->AddRoot(_T(""));
        convTree->SetItemLabel(m_mentionsNode, TR(L"与我有关"));
        convTree->SetItemSubLabel(m_mentionsNode, TR(L"@我的、回复我的、我发的消息"));
    }
    const std::vector<int> rows = FilterManagerConversations(m_mgrItems, m_mgrTypeFilter, m_mgrQuery, m_mgrScope);
    for (size_t k = 0; k < rows.size(); ++k)
    {
        const ManagerConversationItem& item = m_mgrItems[(size_t)rows[k]];
        CString name;
        CString timeText;
        HBITMAP avatar = NULL;
        ResolveManagerRow(item.m_ctype, item.m_cid, item.m_lastTimestamp, name, timeText, avatar);
        const int node = convTree->AddRoot(_T(""));
        convTree->SetItemLabel(node, item.m_name.c_str());
        convTree->SetItemSubLabel(node, item.m_digest.c_str());
        convTree->SetItemRightTopText(node, timeText);
        if (avatar != NULL)
        {
            convTree->SetItemIcon(node, avatar);
        }
        m_mgrConvNodes[node] = (size_t)rows[k];
    }

    //2. 聊天记录：取最近一次跨会话搜索的结果（SubmitManagerSearch 在后台线程查，查完由
    //   OnManagerSearchDoneMsg 写进 m_mgrHitsRaw），按类型筛选（范围已在查询里限定）。结果对应的
    //   不是当前搜索词时说明新结果还没回来，这一段先不显示
    hitTree->Clear();
    m_mgrHitNodes.clear();
    m_mgrHits.clear();
    HistoryManagerListView view;
    view.m_searching = !m_mgrQuery.empty();
    view.m_convRows = (int)rows.size() + ((m_mentionsNode >= 0) ? 1 : 0);
    const bool hitsPending = view.m_searching && (m_mgrHitsQuery != m_mgrQuery);
    if (view.m_searching && !hitsPending)
    {
        const std::vector<ConversationHit>& hits = m_mgrHitsRaw;
        for (size_t i = 0; i < hits.size(); ++i)
        {
            if (!MatchesManagerTypeFilter(hits[i].m_ctype, m_mgrTypeFilter))
            {
                continue;
            }
            CString name;
            CString timeText;
            HBITMAP avatar = NULL;
            ResolveManagerRow(hits[i].m_ctype, hits[i].m_cid, hits[i].m_latestTimestamp, name, timeText, avatar);
            const std::wstring sub = BuildManagerHitText(hits[i].m_hitCount,
                                                         std::wstring((LPCTSTR)Utf8ToCString(hits[i].m_latestText)));
            const int node = hitTree->AddRoot(_T(""));
            hitTree->SetItemLabel(node, name);
            hitTree->SetItemSubLabel(node, sub.c_str());
            hitTree->SetItemRightTopText(node, timeText);
            if (avatar != NULL)
            {
                hitTree->SetItemIcon(node, avatar);
            }
            m_mgrHits.push_back(hits[i]);
            m_mgrHitNodes[node] = m_mgrHits.size() - 1;
        }
        view.m_hitRows = (int)m_mgrHits.size();
        view.m_hitHeader = TR(L"聊天记录 · 仅搜索本机已保存的记录");
    }

    //3. 两段都没有行时的提示
    if (hitsPending)
    {
        view.m_emptyText = TR(L"正在搜索…");
    }
    else if (view.m_searching)
    {
        view.m_emptyText = TR(L"没有找到相关的会话或聊天记录");
    }
    else if (m_mgrItems.empty())
    {
        view.m_emptyText = TR(L"还没有聊天记录");
    }
    else
    {
        view.m_emptyText = TR(L"没有符合条件的会话");
    }
    ApplyHistoryManagerListView(content, view);
    SyncManagerSelection(false);
    LOG_INFO("history manager: list filled, type %d, query chars %u, scope %u, %d conversations, %d hits, %.1f ms.",
             m_mgrTypeFilter, (unsigned)m_mgrQuery.size(), (unsigned)m_mgrScope.size(), view.m_convRows,
             view.m_hitRows, sw.ElapsedMs());
}

void CBalloonHistoryWindow::SyncManagerSelection(bool scrollIntoView)
{
    //文件 / 图片 / 链接页：选中项跟着那一页当前看的会话走
    if (!ManagerShowsChatList())
    {
        SyncLibrarySelection(scrollIntoView);
        return;
    }
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    DuiTreeView* convTree = dynamic_cast<DuiTreeView*>(content->FindCtrlById(kIdMgrConvTree));
    DuiTreeView* hitTree = dynamic_cast<DuiTreeView*>(content->FindCtrlById(kIdMgrHitTree));
    DuiControl* listBox = content->FindCtrlById(kIdMgrListBox);
    if (convTree == NULL || hitTree == NULL || listBox == NULL)
    {
        return;
    }
    //与我有关页：选中第一行「与我有关」，命中树不选
    if (m_mgrTab == kHistoryManagerTabMentions)
    {
        convTree->SetCurSel(m_mentionsNode, false);
        hitTree->SetCurSel(-1, false);
        convTree->Invalidate();
        hitTree->Invalidate();
        if (scrollIntoView && m_pMgrScroll != NULL)
        {
            m_pMgrScroll->SetScrollPos(0);
        }
        return;
    }
    const int ctype = m_model.Filter().m_ctype;
    const int64_t cid = m_model.Filter().m_cid;

    //当前会话在会话树里就选中那一行；不在（名字没匹配上搜索词）而在命中树里，就选中命中树那一行
    int convNode = -1;
    int convPos = -1;
    int pos = 0;
    for (std::map<int, size_t>::const_iterator it = m_mgrConvNodes.begin(); it != m_mgrConvNodes.end(); ++it, ++pos)
    {
        const ManagerConversationItem& item = m_mgrItems[it->second];
        if (item.m_ctype == ctype && item.m_cid == cid)
        {
            convNode = it->first;
            convPos = pos;
            break;
        }
    }
    int hitNode = -1;
    int hitPos = -1;
    if (convNode < 0)
    {
        pos = 0;
        for (std::map<int, size_t>::const_iterator it = m_mgrHitNodes.begin(); it != m_mgrHitNodes.end(); ++it, ++pos)
        {
            const ConversationHit& hit = m_mgrHits[it->second];
            if (hit.m_ctype == ctype && hit.m_cid == cid)
            {
                hitNode = it->first;
                hitPos = pos;
                break;
            }
        }
    }
    //不发通知：选中项只是跟着右侧走，不能反过来再触发一次切换
    convTree->SetCurSel(convNode, false);
    hitTree->SetCurSel(hitNode, false);
    convTree->Invalidate();
    hitTree->Invalidate();

    //选中行在滚动视口之外时滚进来。节点按 AddRoot 的先后编号递增，map 里的先后即显示的先后
    if (!scrollIntoView || m_pMgrScroll == NULL || (convNode < 0 && hitNode < 0))
    {
        return;
    }
    const DuiTreeView* tree = (convNode >= 0) ? convTree : hitTree;
    //会话树第一行是「与我有关」时，各会话行都往下错一行
    const int rowPos = (convNode >= 0) ? (convPos + ((m_mentionsNode >= 0) ? 1 : 0)) : hitPos;
    const int rowTop = (tree->GetRect().top - listBox->GetRect().top) + rowPos * kMgrRowHeight;
    const RECT viewRc = m_pMgrScroll->GetRect();
    const int viewH = viewRc.bottom - viewRc.top;
    if (viewH <= 0)
    {
        return;
    }
    const int scrollPos = m_pMgrScroll->GetScrollPos();
    if (rowTop < scrollPos)
    {
        m_pMgrScroll->SetScrollPos(rowTop);
    }
    else if (rowTop + kMgrRowHeight > scrollPos + viewH)
    {
        m_pMgrScroll->SetScrollPos(rowTop + kMgrRowHeight - viewH);
    }
}

void CBalloonHistoryWindow::SwitchConversation(int ctype, int64_t cid, const CString& name,
                                               const std::string& keywordUtf8, bool keywordWildcard)
{
    const bool same = (m_model.Filter().m_ctype == ctype && m_model.Filter().m_cid == cid);
    if (same && keywordUtf8.empty())
    {
        SyncManagerSelection(true);
        return;
    }
    LOG_INFO("history manager: switch to ctype=%d, cid=%lld, keyword bytes=%u.",
             ctype, (long long)cid, (unsigned)keywordUtf8.size());

    //与 OpenForConversation 换会话时相同：上一个会话的定位高亮、多选、往前拉取、待导出任务都作废
    ClearLocatedRow();
    ResetMultiSelect();
    m_pendingExportPath.Empty();
    if (!same)
    {
        AbandonBatchDownload("conversation switched");
    }
    StopFetchingOlder();

    m_titleName = name;
    m_model.SetConversation(ctype, cid);
    m_model.ResetSearchFilters();
    m_model.SetTypeFilter(kHistoryTypeAll);
    ResetSearchControls();
    if (!keywordUtf8.empty())
    {
        //从左栏搜索命中点进来：右侧按同一个词、同一种解释方式（是否通配符）筛选，搜索框里看得到这个词；
        //否则左栏按通配符搜出了命中，右侧却按普通字符筛，点进来是空的
        SyncRightWildcard(keywordWildcard);
        m_model.SetKeyword(keywordUtf8);
        SyncSyntaxFromIds();
        DuiControl* content = GetClientContent();
        DuiEdit* search = (content != NULL)
                        ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdHistorySearch)) : NULL;
        if (search != NULL)
        {
            search->SetTextNoNotify(Utf8ToCString(keywordUtf8));
        }
    }
    ApplyHistoryHeaderTitle(BodyRoot(), m_titleName, m_mgrCollapsed);
    ApplyCoverageBound();
    RefreshSenderCombo();
    RefreshContent();
    SyncManagerSelection(true);
    if (cid != 0)
    {
        ScheduleBackfill(ctype, cid);
    }
}

void CBalloonHistoryWindow::ScheduleBackfill(int ctype, int64_t cid)
{
    if (m_pMainPanel == NULL || cid == 0)
    {
        return;
    }
    m_pendingBackfillCtype = ctype;
    m_pendingBackfillCid = cid;
    //重新计时：连续切换时只有最后停下的那个会话会被补拉
    SetTimer(kMgrBackfillTimerId, kMgrBackfillDelayMs, NULL);
}

void CBalloonHistoryWindow::SetManagerCollapsed(bool collapsed)
{
    if (m_mgrCollapsed == collapsed)
    {
        return;
    }
    m_mgrCollapsed = collapsed;
    ApplyHistoryManagerCollapsed(GetClientContent(), collapsed, m_mgrPaneWidth, m_titleName);
    ApplyManagerMinSize(!collapsed);
    LOG_INFO("history manager: pane %s, width %d.", collapsed ? "collapsed" : "expanded", m_mgrPaneWidth);
    //右侧列表变宽或变窄了，长消息要按新宽度重新折行
    ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshPageOnly, 0);
}

void CBalloonHistoryWindow::ApplyRightWildcard(bool wildcard)
{
    if (m_model.Filter().m_wildcard == wildcard)
    {
        return;
    }
    m_model.SetWildcard(wildcard);
    SyncSyntaxFromIds();
    LOG_INFO("history window: wildcard -> %d, keyword bytes %u.", (int)wildcard,
             (unsigned)m_model.Filter().m_keyword.size());
    //没有关键字时解释方式不影响结果，不必重查
    if (!m_model.Filter().m_keyword.empty())
    {
        ::PostMessage(m_hWnd, WM_HISTORY_REFRESH_CONTENT, kHistoryRefreshFull, 0);
    }
}

void CBalloonHistoryWindow::SyncRightWildcard(bool wildcard)
{
    m_model.SetWildcard(wildcard);
    DuiControl* content = GetClientContent();
    DuiButton* box = (content != NULL) ? dynamic_cast<DuiButton*>(content->FindCtrlById(kIdHistoryWildcard)) : NULL;
    if (box != NULL && box->IsChecked() != wildcard)
    {
        box->SetCheck(wildcard, false);
    }
}

void CBalloonHistoryWindow::SetManagerWildcard(bool wildcard)
{
    if (m_mgrWildcard == wildcard)
    {
        return;
    }
    m_mgrWildcard = wildcard;
    LOG_INFO("history manager: wildcard -> %d, query chars %u.", (int)wildcard, (unsigned)m_mgrQuery.size());
    if (m_mgrQuery.empty())
    {
        return;
    }
    //同一个搜索词换了解释方式：旧结果作废，「聊天记录」一段显示「正在搜索…」直到新结果回来
    m_mgrHitsQuery.clear();
    SubmitManagerSearch();
    FillManagerTrees();
}

bool CBalloonHistoryWindow::SyncManagerWildcard(bool wildcard)
{
    const bool changed = (m_mgrWildcard != wildcard);
    m_mgrWildcard = wildcard;
    DuiControl* content = GetClientContent();
    DuiButton* box = (content != NULL) ? dynamic_cast<DuiButton*>(content->FindCtrlById(kIdMgrWildcard)) : NULL;
    if (box != NULL && box->IsChecked() != wildcard)
    {
        box->SetCheck(wildcard, false);
    }
    return changed;
}

void CBalloonHistoryWindow::ApplyManagerSearch()
{
    KillTimer(kMgrSearchTimerId);
    DuiControl* content = GetClientContent();
    DuiEdit* search = (content != NULL) ? dynamic_cast<DuiEdit*>(content->FindCtrlById(kIdMgrSearch)) : NULL;
    if (search == NULL)
    {
        return;
    }
    CString text = search->GetText();
    text.Trim();
    const std::wstring query = (LPCTSTR)text;
    if (query == m_mgrQuery)
    {
        return;
    }
    m_mgrQuery = query;
    LOG_INFO("history manager: search chars %u.", (unsigned)m_mgrQuery.size());
    //先发起跨会话搜索（后台线程执行），再立即重填：「会话」一段按名字匹配马上出来，「聊天记录」一段等结果。
    //文件 / 图片 / 链接页的搜索框只按名字找会话，不跨会话搜内容；切回聊天记录页时再补发（见 SwitchManagerTab）。
    //与我有关页的左栏与聊天记录页相同，照常搜
    if (ManagerShowsChatList())
    {
        SubmitManagerSearch();
    }
    FillManagerTrees();
}

void CBalloonHistoryWindow::SubmitManagerSearch()
{
    if (m_mgrQuery.empty())
    {
        //没有搜索词：旧结果作废，在途请求的结果回来时序号对不上，会被丢弃
        m_mgrSearchSeq = 0;
        m_mgrHitsRaw.clear();
        m_mgrHitsQuery.clear();
        return;
    }
    const std::string keywordUtf8 = CStringToUtf8(CString(m_mgrQuery.c_str()));
    //搜索语法里的 from:名字：按本地库里发过言的所有人的显示名换成 userid（名字只能在界面线程上解析）
    const std::vector<int64_t> fromIds = ResolveSyntaxFromIds(keywordUtf8, m_mgrWildcard, true);

    //真实会话：交给主面板的后台查询线程，界面线程不等结果
    CV2MsgSearchWorker* worker = (m_pMainPanel != NULL) ? m_pMainPanel->SearchWorker() : NULL;
    if (worker != NULL)
    {
        V2SearchRequest req;
        req.m_kind = kV2SearchManager;
        req.m_keywordUtf8 = keywordUtf8;
        req.m_limit = kMgrHitLimit;
        req.m_scope = m_mgrScope;
        req.m_wildcard = m_mgrWildcard;
        req.m_filter.m_syntaxFromIds = fromIds;
        req.m_target = m_hWnd;
        req.m_msg = WM_HISTORY_MGR_SEARCH_DONE;
        m_mgrSearchSeq = worker->Submit(req);
        LOG_INFO("history manager: search submitted, seq %u, keyword bytes %u, scope %u, wildcard %d.",
                 m_mgrSearchSeq, (unsigned)keywordUtf8.size(), (unsigned)m_mgrScope.size(), (int)m_mgrWildcard);
        if (m_mgrSearchSeq != 0)
        {
            return;
        }
        //提交失败（本地库打不开等）：不会有结果回来，按「没有命中」处理，不让左栏一直停在「正在搜索…」
        m_mgrHitsRaw.clear();
        m_mgrHitsQuery = m_mgrQuery;
        return;
    }

    //调试入口（没有主面板与后台线程）：预置的假库只有几十行，直接在界面线程上查
    m_mgrSearchSeq = 0;
    m_mgrHitsRaw.clear();
    CV2MsgStore* store = ManagerStore();
    if (store != NULL
        && !store->SearchConversationsForManager(keywordUtf8, kMgrHitLimit, m_mgrScope, m_mgrHitsRaw, m_mgrWildcard,
                                                 fromIds))
    {
        LOG_ERROR("history manager: search local messages failed.");
    }
    m_mgrHitsQuery = m_mgrQuery;
}

LRESULT CBalloonHistoryWindow::OnManagerSearchDoneMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam,
                                                      BOOL& /*bHandled*/)
{
    //结果对象归本窗口释放：先接管，函数返回时自动 delete
    std::unique_ptr<V2SearchResponse> resp(reinterpret_cast<V2SearchResponse*>(lParam));
    const uint32_t seq = (uint32_t)wParam;
    if (resp.get() == NULL || seq == 0 || seq != m_mgrSearchSeq || !IsWindow())
    {
        LOG_INFO("history manager: search result seq %u dropped (latest %u).", seq, m_mgrSearchSeq);
        return 0;
    }
    m_mgrSearchSeq = 0;
    m_mgrHitsRaw = resp->m_conversations;
    m_mgrHitsQuery = m_mgrQuery;
    LOG_INFO("history manager: search result seq %u, ok %d, %u conversations, query %.1f ms.",
             seq, (int)resp->m_ok, (unsigned)m_mgrHitsRaw.size(), resp->m_elapsedMs);
    FillManagerTrees();
    return 0;
}

void CBalloonHistoryWindow::ResolveManagerRow(int ctype, int64_t cid, int64_t timestampMs,
                                              CString& outName, CString& outTime, HBITMAP& outAvatar)
{
    if (m_pMainPanel != NULL)
    {
        m_pMainPanel->ResolveHistoryManagerRow(ctype, cid, timestampMs, outName, outTime, outAvatar);
        return;
    }
    //调试入口：没有通讯录与群资料，名字取预置的昵称表（SeedDebugStore 把群名也放在里面），
    //表里没有的群写成「群<id>」
    const int kCtypeGroup = 2;
    outAvatar = NULL;
    std::map<int64_t, CString>::const_iterator it = m_senderNames.find(cid);
    if (it != m_senderNames.end())
    {
        outName = it->second;
    }
    else if (ctype == kCtypeGroup)
    {
        outName.Format(TR(L"群%lld"), (long long)cid);
    }
    else
    {
        outName = ResolveSenderName(cid);
    }
    outTime = FormatTimeShort(timestampMs);
}

void CBalloonHistoryWindow::OnManagerConvClicked(int nodeId)
{
    //第一行「与我有关」
    if (nodeId >= 0 && nodeId == m_mentionsNode)
    {
        EnterMentionsPage();
        return;
    }
    std::map<int, size_t>::const_iterator it = m_mgrConvNodes.find(nodeId);
    if (it == m_mgrConvNodes.end() || it->second >= m_mgrItems.size())
    {
        return;
    }
    const ManagerConversationItem item = m_mgrItems[it->second];
    LeaveMentionsPage();
    SwitchConversation(item.m_ctype, item.m_cid, CString(item.m_name.c_str()), std::string());
}

void CBalloonHistoryWindow::OnManagerHitClicked(int nodeId)
{
    std::map<int, size_t>::const_iterator it = m_mgrHitNodes.find(nodeId);
    if (it == m_mgrHitNodes.end() || it->second >= m_mgrHits.size())
    {
        return;
    }
    const ConversationHit hit = m_mgrHits[it->second];
    CString name;
    CString timeText;
    HBITMAP avatar = NULL;
    ResolveManagerRow(hit.m_ctype, hit.m_cid, hit.m_latestTimestamp, name, timeText, avatar);
    LeaveMentionsPage();
    SwitchConversation(hit.m_ctype, hit.m_cid, name, CStringToUtf8(CString(m_mgrQuery.c_str())), m_mgrWildcard);
}

LRESULT CBalloonHistoryWindow::OnShowManagerMenuMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam,
                                                    BOOL& /*bHandled*/)
{
    POINT ptScreen;
    //MAKELPARAM 打包时按 WORD 截断，取出时须按有符号还原（多显示器下屏幕坐标可能为负）
    ptScreen.x = GET_X_LPARAM(lParam);
    ptScreen.y = GET_Y_LPARAM(lParam);
    ShowManagerConvMenu((int)wParam, ptScreen);
    return 0;
}

void CBalloonHistoryWindow::ShowManagerConvMenu(int nodeId, POINT ptScreen)
{
    std::map<int, size_t>::const_iterator it = m_mgrConvNodes.find(nodeId);
    if (it == m_mgrConvNodes.end() || it->second >= m_mgrItems.size())
    {
        return;
    }
    const ManagerConversationItem item = m_mgrItems[it->second];

    HMENU menu = ::CreatePopupMenu();
    if (menu == NULL)
    {
        return;
    }
    int itemCount = 0;
    //打开聊天窗口、清空聊天记录都要经主面板，调试入口没有主面板时不放
    if (m_pMainPanel != NULL)
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kMgrMenuOpenChat, TR(L"打开聊天窗口"));
        ++itemCount;
    }
    ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kMgrMenuExport, TR(L"导出聊天记录"));
    ++itemCount;
    if (m_pMainPanel != NULL)
    {
        ::AppendMenuW(menu, MF_STRING, (UINT_PTR)kMgrMenuClear, TR(L"清空聊天记录"));
        ++itemCount;
    }

    //落点夹进锚点所在显示器的工作区（开发约定文档的菜单不越屏规则），尺寸按行数估算
    SIZE menuSize;
    menuSize.cx = kMgrMenuEstWidth;
    menuSize.cy = (LONG)itemCount * kRowMenuEstItemHeight;
    const POINT at = balloonmain::ClampPopupMenuToWorkArea(ptScreen, menuSize);
    ::SetForegroundWindow(m_hWnd);   //菜单要靠前台窗口才能在点击别处时正常收起
    const int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                     at.x, at.y, 0, m_hWnd, NULL);
    ::DestroyMenu(menu);
    //右键时 DuiTreeView 把选中项挪到了被右键的那一行，菜单收起后对齐回右侧当前的会话
    SyncManagerSelection(false);
    LOG_INFO("history manager: row menu on ctype=%d, cid=%lld, command %d.",
             item.m_ctype, (long long)item.m_cid, cmd);

    const CString name(item.m_name.c_str());
    switch (cmd)
    {
    //打开聊天窗口：与主面板会话列表单击同一套动作（开窗、清未读）
    case kMgrMenuOpenChat:
        if (m_pMainPanel != NULL)
        {
            m_pMainPanel->OpenConversation(item.m_ctype, item.m_cid);
        }
        break;

    //导出聊天记录：先切到这个会话，再走右侧「导出」的流程（含"还有更早的消息没加载"的询问）
    case kMgrMenuExport:
        SwitchConversation(item.m_ctype, item.m_cid, name, std::string());
        ExportHistory();
        break;

    //清空聊天记录：先切到这个会话，再走右侧「清空聊天记录」的流程（确认框在主面板那边）
    case kMgrMenuClear:
        SwitchConversation(item.m_ctype, item.m_cid, name, std::string());
        RequestClearConversation();
        break;

    //没有选任何一项（点了别处、按了 Esc）
    default:
        break;
    }
}

void CBalloonHistoryWindow::OpenManagerScopePopup()
{
    DuiControl* content = GetClientContent();
    DuiControl* button = (content != NULL) ? content->FindCtrlById(kIdMgrScope) : NULL;
    if (button == NULL)
    {
        return;
    }
    RECT anchor = button->GetRect();
    ::ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&anchor.left));
    ::ClientToScreen(m_hWnd, reinterpret_cast<POINT*>(&anchor.right));

    //列出左栏的全部会话（不受类型筛选与搜索词限制），顺序与左栏相同
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
    m_scopePopup.OpenBelow(anchor, entries, m_mgrScope);
}

void CBalloonHistoryWindow::OnScopePopupThunk(void* userdata, const std::vector<HistoryConversationKey>& keys)
{
    CBalloonHistoryWindow* self = static_cast<CBalloonHistoryWindow*>(userdata);
    if (self != NULL && self->IsWindow())
    {
        self->ApplyManagerScope(keys);
    }
}

void CBalloonHistoryWindow::ApplyManagerScope(const std::vector<HistoryConversationKey>& keys)
{
    m_mgrScope = NormalizeManagerScope(keys);
    ApplyHistoryManagerScope(GetClientContent(), m_mgrScope.size());
    LOG_INFO("history manager: scope -> %u conversations.", (unsigned)m_mgrScope.size());
    //范围变了，正在显示的命中也要按新范围重查。先作废旧结果，让「聊天记录」一段显示为等待中
    if (!m_mgrQuery.empty())
    {
        m_mgrHitsQuery.clear();
        SubmitManagerSearch();
    }
    FillManagerTrees();
}
