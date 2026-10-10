/**
 *  合并转发的聊天记录查看窗，见 MergedRecordWindow.h。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "MergedRecordWindow.h"

#include <shellapi.h>   // ShellExecuteW（打开链接 / 打开文件）
#include <time.h>       // time()（"今天"是哪一天）

#include "../../Common/Log/IULog.h"                        // LOG_INFO / LOG_ERROR
#include "../../Session/UserSessionData.h"                 // FMG_MSG_OPEN_MERGED_VIEWER
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "MergedRecordRows.h"                              // BuildMergedRecordRows
#include "HistoryRowFormat.h"                              // LocalDayKey
#include "FileLocalStore.h"                                // 文件本地落点登记库（按 md5 + 文件名查路径）
#include "../BalloonChatWnd/ChatLinkDetect.h"              // IsSafeLinkUrl（打开链接前的协议白名单）
#include "../BalloonChatWnd/ChatImageCache.h"              // 登记为解码完成通知的接收窗口
#include "../BalloonChatWnd/ImagePlaceholder.h"            // ResolveHistoryThumb
#include "../BalloonChatWnd/ChatFileLocation.h"            // ResolveLocalFilePath（本地落点优先级与回退）
#include "../BalloonChatWnd/ChatReceivedFile.h"            // IsLocalFileUsable / HandleLocalFileAction
#include "../BalloonChatWnd/ChatFileCardControl.h"         // 文件行里的聊天文件卡片（就地更新进度）
#include "../BalloonChatWnd/ChatFileAction.h"              // ChatFileActionData（卡片投来的动作）
#include "../BalloonImageViewer/BalloonImageViewerWindow.h"  // 看图窗（双击图片放大）
#include "../BalloonMain/TitleBarIcon.h"                   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../BalloonMain/OverlayScrollBarGeometry.h"       // ComputeResizeBorderInsetPx
#include "../../net/ImageTransfer.h"                       // 图片下载与缓存路径
#include "../../net/FileTransfer.h"                        // 文件下载与缓存路径
#include "../../net/CurrentUserAccount.h"                  // 缓存目录按登录账号区分
#include "../../net/UserCachePath.h"                       // UserCacheDir：取当前账号的语音缓存目录 ChatVoice
#include "../BalloonChatWnd/ChatVoiceBubble.h"             // DecideVoiceClick 等：语音条的点击判定与提示文字
#include "../BalloonChatWnd/ChatVoiceStrip.h"              // ChatVoiceStrip：就地更新播放与下载状态
#include "../BalloonMarkdown/MarkdownEditorManager.h"      // 点 Markdown 卡片打开预览窗口
#include "../BalloonMarkdown/MarkdownEditorModel.h"        // MarkdownPreviewMeta：预览窗口的「发送者 · 时间」
#include "../BalloonCode/CodeViewerWindow.h"                // 点代码卡片打开代码查看窗
#include "../../net/Protocol.h"                            // ::message_type_*
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;
using namespace balloonhistory;

namespace {

//窗口初始客户区尺寸与最小尺寸（像素）。比消息记录窗窄：这里没有筛选栏与分页栏，
//宽度只需放下一行正文
// 含标题栏的整窗初始尺寸（逻辑像素）。2026-10-02 修复 ResizeClient 多叠加系统边框的问题时由 600×640 加上
// 修复前多叠加的 18×47（120 DPI 实测），保持修复前的窗口大小。
const int kViewerWidth     = 618;
const int kViewerHeight    = 687;
const int kViewerMinWidth  = 420;
const int kViewerMinHeight = 360;

//标题栏高度与配色，与消息记录窗一致：两个窗口外观同属一类
const int      kTitleBarHeight  = 32;
const COLORREF kCaptionColor    = RGB(0x55, 0x55, 0x55);
const int      kTitleIconSize   = 16;
const COLORREF kTitleBarBg      = RGB(0xFF, 0xFF, 0xFF);

//客户区配色与尺寸，与消息记录窗 HistoryWindowLayout.cpp 的同名用途一致
const COLORREF kColorWhite    = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorSubtitle = RGB(0x9A, 0x98, 0x93);   // "共 N 条消息"
const COLORREF kColorSepLine  = RGB(0xEC, 0xEA, 0xE6);   // 分隔线
const int kPadH           = 20;   // 顶部一行的左右内边距
const int kHeaderPadTop   = 8;    // 顶部一行的上内边距
const int kHeaderPadBot   = 6;    // 顶部一行的下内边距
const int kHeaderH        = 32;   // 顶部一行的总高
const int kSubLineH       = 18;   // "共 N 条消息"文字高

//一秒的毫秒数：time() 返回秒，"今天"的日期键按毫秒换算
const int64_t kMsPerSecond = 1000;

//同时打开多个查看窗时（嵌套的一层、或连点了几张卡片），新窗口相对居中位置往右下错开的
//步长（像素）与最多错开几步。不错开的话新窗口与旧窗口完全重叠，看上去像是窗口没有变化
const int kCascadeStepPx   = 24;
const int kCascadeMaxSteps = 8;

//进程内当前打开着的查看窗个数，用来算新窗口的错开步数。只在界面线程里增减
int g_liveViewerCount = 0;

//ShellExecute 返回值不大于这个数表示失败（没有关联程序、文件刚被删掉等）
const INT_PTR kShellExecOkThreshold = 32;

//标题取不到时的缺省标题，与聊天窗合并卡片的缺省标题一致
const wchar_t* const kDefaultTitle = TR_NOOP(L"聊天记录");
//提示框标题
const wchar_t* const kMsgBoxTitle = TR_NOOP(L"聊天记录");

//UTF-8 转 CString（UTF-16）。空串安全
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    return CString(CA2W(utf8.c_str(), CP_UTF8));
}

//是不是图片类子消息（图片与截图）。
bool IsImageType(int msgtype)
{
    return msgtype == ::message_type_image || msgtype == ::message_type_screenshot;
}

} // anonymous namespace

CMergedRecordWindow::CMergedRecordWindow()
    : m_pScroll(NULL)
    , m_pSubtitle(NULL)
    , m_lastListWidth(0)
    , m_rebuildPending(false)
    , m_hostPanel(NULL)
    , m_pendingOpenMsgid(0)
    , m_hTitleIcon(NULL)
    , m_playingVoiceMsgid(0)
{
}

CMergedRecordWindow::~CMergedRecordWindow()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

const net::MergedRecord& CMergedRecordWindow::Record() const
{
    return m_record;
}

bool CMergedRecordWindow::Open(const std::string& contentJson,
                               const std::map<int64_t, CString>& fallbackNames,
                               HWND hostPanel)
{
    net::MergedRecord record;
    if (!net::MessageProtocolClient::parseMergedContent(contentJson, record))
    {
        LOG_ERROR("merged record viewer: content not parsable, length=%u.",
                  (unsigned)contentJson.size());
        return false;
    }
    m_record        = record;
    m_fallbackNames = fallbackNames;
    m_hostPanel     = hostPanel;
    //换了一份记录：上一份的下载、展开状态都不再适用
    m_downloadingMsgids.clear();
    m_failedImageMsgids.clear();
    m_expandedMsgids.clear();
    m_fileStates.Clear();
    m_pendingOpenMsgid = 0;

    const CString title = m_record.m_title.empty() ? CString(TR(kDefaultTitle))
                                                   : Utf8ToCStr(m_record.m_title);
    if (!IsWindow())
    {
        if (!BuildWindow())
        {
            return false;
        }
    }
    SetTitle(title);

    //顶部一行与列表都换成新记录的内容
    DuiLabel* label = dynamic_cast<DuiLabel*>(m_pSubtitle);
    if (label != NULL)
    {
        CString sub;
        sub.Format(TR(L"共 %u 条消息"), (unsigned)m_record.m_messages.size());
        label->SetText(sub);
    }
    RebuildList(true);

    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("merged record viewer opened: hwnd=0x%p, items=%u, host=0x%p.",
             m_hWnd, (unsigned)m_record.m_messages.size(), hostPanel);
    return true;
}

bool CMergedRecordWindow::BuildWindow()
{
    if (Create(NULL, CWindow::rcDefault, g_strAppTitle.c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("merged record viewer: Create failed.");
        return false;
    }

    //登记为图片缓存的通知接收窗口：缩略图在缓存未命中时请求后台解码，解码完成后经
    //WM_CHAT_IMAGE_READY 通知本窗口重画。注销在 OnDestroyMsg
    balloonchatwnd::ChatImageCache::Inst().RegisterListener(m_hWnd);

    SetTitleBarHeight(kTitleBarHeight);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    //标题栏透明，穿透显示的是客户区根控件的白底，故程序图标按白底合成
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
    m_taskIcons.ApplyTaskIcons(m_hWnd);
    SetButtons(true, true, true);   //最小化 / 最大化 / 关闭
    SetResizable(true);
    SetMinSize(kViewerMinWidth, kViewerMinHeight);
    //双击行打开图片、文件或下一层记录要靠双击
    //（2026-10-06 更正：balloonui 窗口的窗口类其实带 CS_DBLCLKS，系统本就会投递双击，这里的双击合成实际不起作用，保留无害）
    EnableDoubleClick(true);

    BuildClient();

    ResizeClient(kViewerWidth, kViewerHeight);
    CenterWindow();
    //已有别的查看窗开着时往右下错开，步数按打开着的个数取模，不会一路错出屏幕
    const int steps = g_liveViewerCount % kCascadeMaxSteps;
    if (steps > 0)
    {
        RECT rc = { 0, 0, 0, 0 };
        GetWindowRect(&rc);
        SetWindowPos(NULL, rc.left + steps * kCascadeStepPx, rc.top + steps * kCascadeStepPx,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ++g_liveViewerCount;
    return true;
}

void CMergedRecordWindow::BuildClient()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(0);
    root->SetGap(0);

    //顶部一行："共 N 条消息"。记录标题已在标题栏显示，客户区不再重复
    std::unique_ptr<DuiVBox> header(new DuiVBox());
    header->SetPadding(kPadH, kHeaderPadTop, kPadH, kHeaderPadBot);
    std::unique_ptr<DuiLabel> sub(new DuiLabel());
    sub->SetTextColor(kColorSubtitle);
    sub->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    m_pSubtitle = sub.get();
    header->AddChild(std::unique_ptr<DuiControl>(sub.release()), Hint().Fixed(kSubLineH));
    root->AddChild(std::move(header), Hint().Fixed(kHeaderH));

    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Horizontal);
    sep->SetColor(kColorSepLine);
    root->AddChild(std::unique_ptr<DuiControl>(sep.release()), Hint().Fixed(1));

    //列表：DuiScrollView 包一个承载各行的 vbox，vbox 由 BuildListContent 生成。右侧给
    //窗口缩放边框让出位置，否则滚动条贴着窗口右缘，鼠标移到滑块上只会变成缩放箭头
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetAutoContentHeight(true);
    // 滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
    m_pScroll = scroll.get();
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()),
                   Hint().Weight(1).Margin(0, 0, ListRightInset(), 0));

    SetClientContent(std::move(root));
}

int CMergedRecordWindow::ListRightInset() const
{
    const int inset = balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(), GetDpi(),
                                                              IsResizable());
    return (inset > 0) ? inset : 0;
}

int CMergedRecordWindow::ListWidth() const
{
    RECT rc = { 0, 0, 0, 0 };
    ::GetClientRect(m_hWnd, &rc);
    const int width = (rc.right - rc.left) - ListRightInset();
    return (width > 0) ? width : 0;
}

void CMergedRecordWindow::FillRowStates(std::vector<HistoryRowView>& rows) const
{
    const std::wstring account = net::CurrentUserAccount();
    //语音条：按当前账号的缓存目录补本机文件状态，再套上本窗口记下的下载中、播放中（第 4c 步）
    const std::wstring voiceDir = net::UserCacheDir(g_szDataRoot, account, L"ChatVoice");
    for (size_t v = 0; v < rows.size(); ++v)
    {
        if (!rows[v].m_isVoice)
        {
            continue;
        }
        ResolveVoiceRowLocalState(rows[v], voiceDir);
        if (rows[v].m_voice.voiceState != balloonchatwnd::kVoiceStateReady
            && m_downloadingVoices.count(rows[v].m_voice.voiceMd5) > 0)
        {
            rows[v].m_voice.voiceState = balloonchatwnd::kVoiceStateDownloading;
        }
        rows[v].m_voice.voicePlaying = (m_playingVoiceMsgid != 0 && rows[v].m_voice.msgid == m_playingVoiceMsgid);
    }
    for (size_t i = 0; i < rows.size() && i < m_record.m_messages.size(); ++i)
    {
        HistoryRowView& row = rows[i];
        const net::MessageNotify& sub = m_record.m_messages[i];
        row.m_expanded = (m_expandedMsgids.find(sub.m_msgid) != m_expandedMsgids.end());

        //文件行：本地已有完整文件时卡片为已完成（可打开），再套上本窗口发起过的下载状态
        if (row.m_chatFileCard)
        {
            CString localPath;
            if (ResolveLocalFile((int)i, localPath))
            {
                row.m_fileCard = BuildMergedFileCard(sub, std::wstring((LPCWSTR)localPath));
            }
            m_fileStates.Apply(row.m_fileCard);
            continue;
        }
        if (!row.m_hasImageThumb)
        {
            continue;
        }

        //缓存按登录账号隔离、按内容 MD5 命名；未登录（调试入口）时算不出路径
        std::wstring cache;
        if (!account.empty() && !sub.m_imageMd5.empty())
        {
            cache = net::ImageTransfer::CacheFilePathForMd5(account, sub.m_imageMd5);
            row.m_thumbImagePath = cache.c_str();
        }
        const bool downloadable   = !sub.m_imageFileId.empty() && !sub.m_imageMd5.empty();
        const bool fileExists     = !cache.empty()
                                 && ::GetFileAttributesW(cache.c_str()) != INVALID_FILE_ATTRIBUTES;
        const bool downloading    = (m_downloadingMsgids.find(sub.m_msgid) != m_downloadingMsgids.end());
        const bool downloadFailed = (m_failedImageMsgids.find(sub.m_msgid) != m_failedImageMsgids.end());
        //解码失败不在这里判断：判定时并不解码，由缩略图绘制时按图片缓存的失败记录改画破图
        const balloonchatwnd::HistoryThumbDecision decision =
            balloonchatwnd::ResolveHistoryThumb(downloadable, fileExists, downloading,
                                                downloadFailed, false);
        row.m_thumbState     = (int)decision.m_state;
        row.m_thumbClickable = decision.m_clickable;
        row.m_thumbTooltip   = decision.m_tooltip;
    }
}

void CMergedRecordWindow::RebuildList(bool force)
{
    if (m_pScroll == NULL)
    {
        return;
    }
    //行高按正文在列表实际宽度下折出几行来算，宽度没变就不必重建
    const int listWidth = ListWidth();
    if (!force && listWidth == m_lastListWidth)
    {
        return;
    }
    m_lastListWidth = listWidth;

    std::vector<HistoryRowView> rows;
    const int64_t todayKey = LocalDayKey((int64_t)::time(NULL) * kMsPerSecond);
    BuildMergedRecordRows(m_record, m_fallbackNames, todayKey, rows);
    FillRowStates(rows);

    //查看窗是只读的，行上不会出现 Pin / 定位按钮，不给它们预留位置，正文用满整行
    m_pScroll->SetContent(BuildListContent(rows, TR(L"这份聊天记录是空的"), _T(""), listWidth, false));
    m_pScroll->ForceLayout(m_pScroll->GetRect());
    m_pScroll->Invalidate();
}

void CMergedRecordWindow::PostForceRebuild()
{
    if (IsWindow())
    {
        PostMessage(WM_MERGED_VIEWER_REBUILD, 1, 0);
    }
}

bool CMergedRecordWindow::ResolveLocalFile(int row, CString& outPath) const
{
    outPath.Empty();
    if (row < 0 || row >= (int)m_record.m_messages.size())
    {
        return false;
    }
    const net::MessageNotify& sub = m_record.m_messages[(size_t)row];
    const std::wstring account = net::CurrentUserAccount();
    if (account.empty())
    {
        return false;
    }

    if (IsImageType(sub.m_msgtype))
    {
        //图片只有缓存这一处落点：按内容 MD5 命名
        if (sub.m_imageMd5.empty())
        {
            return false;
        }
        const std::wstring cache = net::ImageTransfer::CacheFilePathForMd5(account, sub.m_imageMd5);
        if (cache.empty() || ::GetFileAttributesW(cache.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }
        outPath = cache.c_str();
        return true;
    }

    if (sub.m_msgtype == ::message_type_file)
    {
        //文件有两处可能的落点：登记表里的路径与按 MD5 推导的下载缓存，优先级交给
        //ResolveLocalFilePath；两处都要求文件存在且大小相符
        std::wstring registered;
        CFileLocalStore::GetInstance().Query(sub.m_fileMd5, sub.m_fileName, registered);
        const std::wstring cache =
            net::FileTransfer::CacheFilePathFor(account, sub.m_fileMd5, sub.m_fileName);
        const std::wstring picked = balloonchatwnd::ResolveLocalFilePath(
            registered, balloonchatwnd::IsLocalFileUsable(registered, sub.m_fileSize),
            cache, balloonchatwnd::IsLocalFileUsable(cache, sub.m_fileSize));
        if (picked.empty())
        {
            return false;
        }
        outPath = picked.c_str();
        return true;
    }
    return false;
}

void CMergedRecordWindow::StartDownload(int row, bool openAfter)
{
    if (row < 0 || row >= (int)m_record.m_messages.size())
    {
        return;
    }
    const net::MessageNotify& sub = m_record.m_messages[(size_t)row];
    //调试入口没有登录态，算不出缓存目录、也连不上服务端；在线直传的文件服务器上没有副本，无从下载
    const std::wstring account = net::CurrentUserAccount();
    if (account.empty() || sub.m_msgid <= 0 || (sub.m_msgtype == ::message_type_file && sub.m_fileDirect))
    {
        return;
    }

    //同一条已经在下载：不重复发起，只补记"下完打开"。双击图片时，第一下单击可能已经
    //发起了一笔只下载的下载
    if (m_downloadingMsgids.find(sub.m_msgid) != m_downloadingMsgids.end()
        || m_fileStates.IsDownloading(sub.m_msgid))
    {
        if (openAfter)
        {
            m_pendingOpenMsgid = sub.m_msgid;
        }
        return;
    }

    if (IsImageType(sub.m_msgtype))
    {
        if (sub.m_imageFileId.empty() || sub.m_imageMd5.empty())
        {
            ::MessageBoxW(m_hWnd, TR(L"这条消息缺少图片信息，无法下载原图。"), TR(kMsgBoxTitle),
                          MB_OK | MB_ICONWARNING);
            return;
        }
        //下载按子消息的 msgid（即原消息的 msgid）发起：服务端签发下载凭证只看 fileid，
        //结果按 msgid 回投，主面板再转给各查看窗
        net::ImageTransfer::GetInstance().EnqueueDownload(
            sub.m_msgid, sub.m_imageFileId, sub.m_imageMd5, sub.m_imageSize, account);
    }
    else if (sub.m_msgtype == ::message_type_file)
    {
        if (sub.m_fileFileId.empty())
        {
            ::MessageBoxW(m_hWnd, TR(L"这条消息缺少文件信息，无法下载。"), TR(kMsgBoxTitle),
                          MB_OK | MB_ICONWARNING);
            return;
        }
        net::FileTransfer::GetInstance().EnqueueDownload(
            sub.m_msgid, sub.m_fileFileId, sub.m_fileName, sub.m_fileMd5, sub.m_fileSize, account);
    }
    else
    {
        return;
    }

    //图片记进 m_downloadingMsgids，文件记进状态表（卡片随之变成下载中、进度 0）
    if (sub.m_msgtype == ::message_type_file)
    {
        m_fileStates.Start(sub.m_msgid);
    }
    else
    {
        m_downloadingMsgids.insert(sub.m_msgid);
    }
    //重新发起下载：之前的失败记录作废，缩略图从破图换成"下载中"
    m_failedImageMsgids.erase(sub.m_msgid);
    if (openAfter)
    {
        m_pendingOpenMsgid = sub.m_msgid;
    }
    LOG_INFO("merged record viewer starts download: msgid=%lld, msgtype=%d, openAfter=%d.",
             (long long)sub.m_msgid, sub.m_msgtype, openAfter ? 1 : 0);
    PostForceRebuild();
}

void CMergedRecordWindow::OnRowDoubleClicked(int row)
{
    if (row < 0 || row >= (int)m_record.m_messages.size())
    {
        return;
    }
    const net::MessageNotify& sub = m_record.m_messages[(size_t)row];

    //嵌套记录：再打开一层
    if (sub.m_msgtype == ::message_type_merged)
    {
        OpenNested(sub.m_mergedContent);
        return;
    }

    //在线直传的文件（2026-10-05 起）：只是一行说明文字，服务器上没有副本，没有可打开或下载的东西
    if (sub.m_msgtype == ::message_type_file && sub.m_fileDirect)
    {
        return;
    }

    //图片与文件：本地有就直接打开，没有就先下载、下完自动打开
    if (IsImageType(sub.m_msgtype) || sub.m_msgtype == ::message_type_file)
    {
        CString localPath;
        if (!ResolveLocalFile(row, localPath))
        {
            StartDownload(row, true);
            return;
        }
        if (IsImageType(sub.m_msgtype))
        {
            //看图窗进程内共享一个实例，已开则换图并置前
            CBalloonImageViewerWindow::OpenShared(m_hWnd, localPath);
        }
        else
        {
            HINSTANCE hr = ::ShellExecuteW(m_hWnd, L"open", (LPCWSTR)localPath, NULL, NULL,
                                           SW_SHOWNORMAL);
            if ((INT_PTR)hr <= kShellExecOkThreshold)
            {
                ::MessageBoxW(m_hWnd, TR(L"打开文件失败，可能没有关联的打开方式。"), TR(kMsgBoxTitle),
                              MB_OK | MB_ICONWARNING);
            }
        }
    }
}

void CMergedRecordWindow::OpenNested(const std::string& contentJson)
{
    if (::IsWindow(m_hostPanel))
    {
        //主面板持有全部查看窗并负责把下载结果转给它们，嵌套的一层也交给它打开
        ::PostMessage(m_hostPanel, FMG_MSG_OPEN_MERGED_VIEWER, 0,
                      (LPARAM)new std::string(contentJson));
        return;
    }

    //调试入口没有主面板：由本窗口创建并持有下一层，本窗口销毁时一并销毁
    std::unique_ptr<CMergedRecordWindow> nested(new CMergedRecordWindow());
    if (nested->Open(contentJson, m_fallbackNames, NULL))
    {
        m_ownedNested.push_back(std::move(nested));
    }
}

void CMergedRecordWindow::OnImageDownloadFinished(int64_t msgid, bool ok,
                                                  const std::wstring& localPath)
{
    //不是本窗口发起的那一笔一概不管：聊天窗、消息记录窗自己的下载不该在这里打开看图窗
    if (m_downloadingMsgids.find(msgid) == m_downloadingMsgids.end())
    {
        return;
    }
    m_downloadingMsgids.erase(msgid);
    const bool wantOpen = (m_pendingOpenMsgid == msgid);
    if (wantOpen)
    {
        m_pendingOpenMsgid = 0;
    }

    if (ok)
    {
        m_failedImageMsgids.erase(msgid);
        if (wantOpen && !localPath.empty())
        {
            CBalloonImageViewerWindow::OpenShared(m_hWnd, CString(localPath.c_str()));
        }
    }
    else
    {
        //失败：缩略图换成破图并允许单击重试，不另弹提示，免得打断浏览
        m_failedImageMsgids.insert(msgid);
    }
    LOG_INFO("merged record viewer image download finished: msgid=%lld, ok=%d.",
             (long long)msgid, ok ? 1 : 0);
    PostForceRebuild();
}

void CMergedRecordWindow::OnFileDownloadFinished(int64_t msgid, bool ok,
                                                 const std::wstring& localPath, bool cancelled)
{
    //不是本窗口正在下载的文件一概不管（聊天窗、消息记录窗自己的下载）
    if (!m_fileStates.IsDownloading(msgid))
    {
        return;
    }
    m_fileStates.Finish(msgid, ok, localPath, cancelled);
    //卡片状态变了（高度可能也变了，例如下载中的进度条消失），重建列表
    PostForceRebuild();
    const bool wantOpen = (m_pendingOpenMsgid == msgid);
    if (wantOpen)
    {
        m_pendingOpenMsgid = 0;
    }
    LOG_INFO("merged record viewer file download finished: msgid=%lld, ok=%d, cancelled=%d.",
             (long long)msgid, ok ? 1 : 0, cancelled ? 1 : 0);

    if (ok)
    {
        if (wantOpen && !localPath.empty())
        {
            HINSTANCE hr = ::ShellExecuteW(m_hWnd, L"open", localPath.c_str(), NULL, NULL,
                                           SW_SHOWNORMAL);
            if ((INT_PTR)hr <= kShellExecOkThreshold)
            {
                ::MessageBoxW(m_hWnd, TR(L"文件已下载，但打开失败，可能没有关联的打开方式。"),
                              TR(kMsgBoxTitle), MB_OK | MB_ICONWARNING);
            }
        }
        return;
    }
    //失败与取消不另弹提示：卡片上已经显示「传输失败」/「已取消」并给出「重新下载」，
    //与聊天窗的文件气泡一致
}

void CMergedRecordWindow::OnFileDownloadProgress(int64_t msgid, int64_t received, int64_t total)
{
    const int kFullPercent = 100;
    const int percent = (total > 0) ? (int)(received * kFullPercent / total) : 0;
    if (!m_fileStates.Progress(msgid, percent))
    {
        return;
    }
    //进度变化不影响行高，就地更新那张卡片即可，不重建列表（下载时进度回投很频繁）
    const int row = RowOfFileMsgid(msgid);
    DuiControl* content = GetClientContent();
    if (row < 0 || content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatFileCardControl* card = dynamic_cast<balloonchatwnd::ChatFileCardControl*>(
        content->FindCtrlById((UINT)(kIdHistoryFileCardBase + row)));
    if (card == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage updated = card->CardMessage();
    m_fileStates.Apply(updated);
    card->SetCardMessage(updated);
}

int CMergedRecordWindow::RowOfFileMsgid(int64_t msgid) const
{
    for (size_t i = 0; i < m_record.m_messages.size(); ++i)
    {
        const net::MessageNotify& sub = m_record.m_messages[i];
        if (sub.m_msgtype == ::message_type_file && sub.m_msgid == msgid)
        {
            return (int)i;
        }
    }
    return -1;
}

LRESULT CMergedRecordWindow::OnChatFileAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    std::unique_ptr<balloonchatwnd::ChatFileActionData> d(
        (balloonchatwnd::ChatFileActionData*)lParam);
    if (!d)
    {
        return 0;
    }
    //打开文件、打开所在文件夹、另存为与会话无关，与聊天窗共用同一份处理
    if (balloonchatwnd::HandleLocalFileAction(m_hWnd, *d))
    {
        return 0;
    }

    switch (d->action)
    {
    //「下载」与失败 / 取消后的「重新下载」：按子消息下标发起下载，下完不自动打开
    case balloonchatwnd::kChatFileAccept:
    case balloonchatwnd::kChatFileRetry:
        StartDownload(RowOfFileMsgid(d->msgid), false);
        break;

    //取消正在进行的下载：只置位取消标志，卡片等传输层回投「已取消」后再变
    case balloonchatwnd::kChatFileCancel:
        if (m_fileStates.IsDownloading(d->msgid))
        {
            LOG_INFO("merged record viewer cancels file download: msgid=%lld.", (long long)d->msgid);
            net::FileTransfer::GetInstance().CancelDownload(d->msgid);
        }
        break;

    //查看窗里的文件卡片不会给出其余动作（如「拒绝」），记一条日志后忽略
    default:
        LOG_ERROR("merged record viewer got unexpected file action: action=%d.", d->action);
        break;
    }
    return 0;
}

LRESULT CMergedRecordWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = (const DuiNotify*)lParam;
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    const UINT id = pNotify->ctrlId;

    if (pNotify->code == DUIN_CLICK)
    {
        //行内图片缩略图：本地没有原图时单击下载（失败后单击重试），只下载、不打开看图窗。
        //缩略图 id = 基址 + 行下标，只有图片行才有
        if (id >= (UINT)kIdHistoryThumbBase
            && id < (UINT)kIdHistoryThumbBase + kIdHistoryThumbCapacity)
        {
            const int row = (int)(id - kIdHistoryThumbBase);
            CString localPath;
            if (!ResolveLocalFile(row, localPath))
            {
                StartDownload(row, false);
            }
            return 0;
        }
        //嵌套记录卡片的标题链接：打开下一层。链接 id = 基址 + 行下标，只有嵌套记录行才有
        if (id >= (UINT)kIdHistoryCardBase
            && id < (UINT)kIdHistoryCardBase + kIdHistoryCardCapacity)
        {
            OnRowDoubleClicked((int)(id - kIdHistoryCardBase));
            return 0;
        }
        //行内「展开 / 收起」链接：翻转该条的展开态，行高变了，重建列表。链接 id = 基址 +
        //行下标，只有正文超出折叠行数的行才有
        if (id >= (UINT)kIdHistoryExpandBase
            && id < (UINT)kIdHistoryExpandBase + kIdHistoryExpandCapacity)
        {
            const int row = (int)(id - kIdHistoryExpandBase);
            if (row >= 0 && row < (int)m_record.m_messages.size())
            {
                const int64_t msgid = m_record.m_messages[(size_t)row].m_msgid;
                if (m_expandedMsgids.find(msgid) != m_expandedMsgids.end())
                {
                    m_expandedMsgids.erase(msgid);
                }
                else
                {
                    m_expandedMsgids.insert(msgid);
                }
                PostForceRebuild();
            }
            return 0;
        }
    }
    else if (pNotify->code == DUIN_DBLCLK)
    {
        //双击整行：行 id = 基址 + 行下标
        if (id >= (UINT)kIdHistoryRowBase
            && id < (UINT)kIdHistoryRowBase + kIdHistoryRowCapacity)
        {
            OnRowDoubleClicked((int)(id - kIdHistoryRowBase));
            return 0;
        }
        //双击落在行内没有编号的子控件上（昵称、时间、空白处）：沿父链找回所在的行
        if (id == 0 && pNotify->pCtrl != NULL)
        {
            const int row = HistoryRowIndexOfControl(static_cast<const DuiControl*>(pNotify->pCtrl));
            if (row >= 0)
            {
                OnRowDoubleClicked(row);
                return 0;
            }
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMergedRecordWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CMergedRecordWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM wParam,
                                       LPARAM /*lParam*/, BOOL& bHandled)
{
    //最小化时客户区宽度为 0，不必重建；同一轮拖动里只投一条，处理时再取最新宽度
    if (wParam != SIZE_MINIMIZED && !m_rebuildPending)
    {
        m_rebuildPending = true;
        PostMessage(WM_MERGED_VIEWER_REBUILD, 0, 0);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CMergedRecordWindow::OnRebuildMsg(UINT /*uMsg*/, WPARAM wParam,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const bool force = (wParam != 0);
    if (!force)
    {
        m_rebuildPending = false;
    }
    RebuildList(force);
    return 0;
}

LRESULT CMergedRecordWindow::OnChatOpenLinkMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM lParam, BOOL& /*bHandled*/)
{
    std::wstring* pUrl = (std::wstring*)lParam;
    if (pUrl == NULL)
    {
        return 0;
    }
    //协议白名单：只放行 http / https，与聊天窗、消息记录窗同一口径
    if (balloonchatwnd::IsSafeLinkUrl(*pUrl))
    {
        ::ShellExecuteW(m_hWnd, L"open", pUrl->c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        LOG_ERROR("merged record viewer refused to open a non-http(s) link.");
    }
    delete pUrl;
    return 0;
}

LRESULT CMergedRecordWindow::OnImageReadyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //缩略图尺寸固定，解码完成只需重画，这次绘制会在缓存里命中真图
    Invalidate(FALSE);
    return 0;
}

balloonchatwnd::ChatVoiceStrip* CMergedRecordWindow::FindVoiceStrip(long long msgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || msgid == 0)
    {
        return NULL;
    }
    //语音条按「基址 + 行下标」编号，行下标就是子消息下标
    for (size_t i = 0; i < m_record.m_messages.size() && i < (size_t)kIdHistoryVoiceCapacity; ++i)
    {
        balloonchatwnd::ChatVoiceStrip* strip = dynamic_cast<balloonchatwnd::ChatVoiceStrip*>(
            content->FindCtrlById((UINT)(kIdHistoryVoiceBase + i)));
        if (strip != NULL && strip->VoiceMessage().msgid == msgid)
        {
            return strip;
        }
    }
    return NULL;
}

LRESULT CMergedRecordWindow::OnChatOpenCodeViewer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                  BOOL& /*bHandled*/)
{
    balloonchatwnd::CodeViewerOpenRequest* pReq = (balloonchatwnd::CodeViewerOpenRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    ballooncode::CCodeViewerWindow::Show(m_hWnd, pReq->m_code, pReq->m_lang, pReq->m_key, pReq->m_notes);
    delete pReq;
    return 0;
}

LRESULT CMergedRecordWindow::OnChatOpenMarkdownPreview(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
                                                       BOOL& /*bHandled*/)
{
    //lParam 为堆上的请求，用完在这里释放
    std::unique_ptr<balloonchatwnd::MarkdownPreviewOpenRequest> req((balloonchatwnd::MarkdownPreviewOpenRequest*)lParam);
    if (!req)
    {
        return 0;
    }
    //合并记录里卡片不区分本人，预览窗口显示原发送者的名字
    balloonmd::MarkdownPreviewDoc doc;
    doc.m_sourceUtf8 = req->m_sourceUtf8;
    doc.m_title = req->m_title;
    doc.m_meta = balloonmd::MarkdownPreviewMeta(req->m_sender, req->m_timestampMs);
    doc.m_key = req->m_key;
    balloonmd::CMarkdownEditorManager::GetInstance().OpenPreview(doc);
    LOG_INFO("merged record window: open markdown preview, key=%s, bytes=%u.", req->m_key.c_str(),
             (unsigned)req->m_sourceUtf8.size());
    return 0;
}

LRESULT CMergedRecordWindow::OnChatVoiceActionMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam,
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
    //就绪的语音：开始播放；合并记录里没有已听状态，不写本地库
    case balloonchatwnd::kVoiceClickPlay:
    {
        balloonvoice::VoicePlayKey key;
        key.m_msgid = msg.msgid;
        if (!hub.StartPlay(this, key, std::wstring((LPCWSTR)msg.voiceLocalPath)))
        {
            ::MessageBoxW(m_hWnd, balloonchatwnd::VoicePlayFailedHint().c_str(), TR(L"聊天记录"),
                          MB_OK | MB_ICONWARNING);
            LOG_WARNING("merged viewer: voice play failed, msgid=%lld.", msg.msgid);
            break;
        }
        m_playingVoiceMsgid = msg.msgid;
        balloonchatwnd::ChatMessage playing = msg;
        playing.voicePlaying = true;
        playing.voicePlayFrame = 0;
        strip->SetVoiceMessage(playing, false);
        LOG_INFO("merged viewer: voice playing, msgid=%lld.", msg.msgid);
        break;
    }
    //正在播放：停止；语音中心通知本窗口播放结束，语音条在 OnVoicePlayEnded 里退出播放中
    case balloonchatwnd::kVoiceClickStop:
        hub.StopPlay();
        break;
    //还在下载：语音条已画成下载中，不另外提示
    case balloonchatwnd::kVoiceClickWaitDownload:
        break;
    //本机没有：下载到 ChatVoice\<md5>.ogg，结果按 md5 经主面板转回来（OnVoiceDownloadFinished）
    case balloonchatwnd::kVoiceClickRetryDownload:
    {
        const std::wstring voiceDir = net::UserCacheDir(g_szDataRoot, net::CurrentUserAccount(), L"ChatVoice");
        if (msg.voiceFileId.empty() || msg.voiceMd5.empty() || voiceDir.empty())
        {
            break;
        }
        m_downloadingVoices.insert(msg.voiceMd5);
        net::FileTransfer::GetInstance().EnqueueVoiceDownload(msg.voiceFileId, msg.voiceMd5, msg.voiceSize,
                                                              balloonvoice::VoiceCachePath(voiceDir, msg.voiceMd5));
        balloonchatwnd::ChatMessage downloading = msg;
        downloading.voiceState = balloonchatwnd::kVoiceStateDownloading;
        strip->SetVoiceMessage(downloading, false);
        LOG_INFO("merged viewer: voice download started, md5=%s.", msg.voiceMd5.c_str());
        break;
    }
    //其它取值不会出现
    default:
        break;
    }
    return 0;
}

void CMergedRecordWindow::OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs,
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
        strip->SetVoiceMessage(m, false);
    }
}

void CMergedRecordWindow::OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int /*reason*/)
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
        strip->SetVoiceMessage(m, false);
    }
}

void CMergedRecordWindow::OnVoiceDownloadFinished(const std::string& md5, bool ok)
{
    if (!IsWindow())
    {
        return;
    }
    const bool mine = m_downloadingVoices.erase(md5) > 0;
    if (mine && !ok)
    {
        LOG_ERROR("merged viewer: voice download failed, md5=%s.", md5.c_str());
    }
    //语音条的本机文件状态在重建列表时重新判断（FillRowStates）
    PostForceRebuild();
}

LRESULT CMergedRecordWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& bHandled)
{
    //放在 WM_DESTROY 而不是 WM_CLOSE：主面板登出时会直接 DestroyWindow，不经过 WM_CLOSE
    balloonchatwnd::ChatImageCache::Inst().UnregisterListener(m_hWnd);
    //本窗口正在播放语音时一并停止，之后语音中心不再给本窗口发播放通知（语音消息第 4c 步）
    balloonvoice::CVoiceHub::GetInstance().DetachPlayListener(this);
    m_playingVoiceMsgid = 0;
    if (g_liveViewerCount > 0)
    {
        --g_liveViewerCount;
    }
    //本窗口自己打开的下一层（调试入口）一并销毁：它们是独立的顶层窗口，系统不会代为销毁，
    //而 ATL 窗口对象析构时仍持有窗口句柄会触发断言
    for (size_t i = 0; i < m_ownedNested.size(); ++i)
    {
        if (m_ownedNested[i] && m_ownedNested[i]->IsWindow())
        {
            m_ownedNested[i]->DestroyWindow();
        }
    }
    m_pScroll   = NULL;
    m_pSubtitle = NULL;
    m_lastListWidth = 0;
    bHandled = FALSE;
    return 0;
}
