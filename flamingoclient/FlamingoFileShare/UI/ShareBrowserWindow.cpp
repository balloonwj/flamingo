/**
 *  共享浏览窗口 CShareBrowserWindow 的实现，见 ShareBrowserWindow.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "ShareBrowserWindow.h"

#include <functional>
#include <memory>
#include <objidl.h>
#include <gdiplus.h>

#include "SharePickDialog.h"

#include "../Engine/ShareBrowseModel.h"
#include "../Engine/ShareProtocolCodec.h"
#include "../../Source/net/Protocol.h"
#include "../Engine/ShareService.h"
#include "../Engine/ShareSearch.h"
#include "../Engine/ShareThumbnail.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/Graphics/ShellTypeIcon.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/DuiDropTarget.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找
#include "../../Source/UI/BalloonMain/P2PBadge.h"   // 「P2P」徽章：传输行的数据经直连链路传输时画在状态文字左边（2026-10-04 起）

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiVirtualList;
using balloonwjui::DuiMenu;
using balloonwjui::DuiEdit;

namespace fileshare {

namespace {

// ---- 尺寸（像素）----
const int kContentWidth      = 760;   //标题栏以下内容区的默认宽度（整窗宽度与之相同）
const int kContentHeight     = 520;   //标题栏以下内容区的默认高度
const int kMinWindowWidth    = 520;   //整窗最小宽度
const int kMinWindowHeight   = 360;   //整窗最小高度（含标题栏）
const int kTitleBarHeight    = 32;    //标题栏高，与客户端其余窗口一致
const int kBannerHeight      = 30;    //横幅高
const int kToolbarHeight     = 36;    //导航行（上一级、路径、搜索框、刷新）高
const int kActionBarHeight   = 32;    //操作行（下载、上传、新建文件夹）高
const int kSearchBoxWidth    = 200;   //搜索框宽
const int kSearchBoxVMargin  = 4;     //搜索框上下留白（比按钮矮一点）
const int kTileWidth         = 112;   //缩略图视图一格的宽
const int kTileHeight        = 134;   //缩略图视图一格的高（图片区加名字）
const int kTileGap           = 8;     //格与格之间的间距
const int kGridPadding       = 8;     //网格左右两侧的留白
const int kTileImageEdge     = 96;    //缩略图的长边（向分享者要的就是这个尺寸）
const int kTileInset         = 4;     //图片区离格子上边的距离
const int kTileNameHeight    = 24;    //名字一行的高
const int kGridIconSize      = 48;    //缩略图视图里目录与非图片文件的系统图标边长
const UINT_PTR kThumbTimerId = 0x5A10;//缩略图视图的定时器 id（与基类可能用到的定时器错开）
const UINT kThumbTimerMs     = 150;   //缩略图视图检查可见格子的间隔，单位：毫秒
const size_t kMaxThumbsInFlight = 6;  //同时在途的缩略图请求上限（分享者只有 4 条工作线程）
const int kToolButtonWidth   = 72;    //「上一级」「刷新」按钮宽
const int kHeaderHeight      = 28;    //表头高
const int kSizeColumnWidth   = 110;   //「大小」列宽
const int kTimeColumnWidth   = 150;   //「修改时间」列宽
const int kScrollBarWidth    = 12;    //列表滚动条宽（DuiVirtualList 的默认值）。表头与数据行的列都按「始终留出滚动条」计算，
                                      //有没有滚动条两者都对齐
const int kRowHeight         = 28;    //列表行高
const int kStatusHeight      = 26;    //状态栏高
const int kSidePadding       = 12;    //左右内边距
const int kCellPadding       = 8;     //单元格左右内边距
const int kIconSize          = 16;    //类型图标边长（系统小图标的尺寸）
const int kIconTextGap       = 8;     //图标与名称之间的间距
const int kBannerPadding     = 8;     //横幅文字的左右内边距
const int kPathGap           = 8;     //路径文字与两侧按钮的间距
const int kWideButtonWidth   = 96;    //「上传文件」「上传文件夹」按钮宽
const int kTransferPanelHeight = 160; //传输列表区高（含标题行）
const int kTransferHeaderHeight = 28; //传输列表区标题行高
const int kTransferRowHeight = 42;    //传输列表行高（两行：名称与状态、进度条）
const int kProgressBarHeight = 6;     //进度条高
const int kProgressBarBottom = 8;     //进度条离行底的距离
const int kTransferTextHeight = 22;   //传输行第一行文字的高度
const int kPercentScale      = 100;   //百分比的满值

// ---- 控件 id（只在本窗口内唯一）----
const UINT kIdUp          = 101;   //「上一级」
const UINT kIdRefresh     = 102;   //「刷新」
const UINT kIdDownload    = 103;   //「下载」
const UINT kIdUploadFiles = 104;   //「上传文件」
const UINT kIdUploadFolder= 105;   //「上传文件夹」
const UINT kIdCancelXfer  = 106;   //传输列表的「取消」
const UINT kIdHeaderName  = 111;   //表头：名称
const UINT kIdHeaderSize  = 112;   //表头：大小
const UINT kIdHeaderTime  = 113;   //表头：修改时间
const UINT kIdNewFolder   = 107;   //「新建文件夹」
const UINT kIdFileList    = 114;   //文件列表（右键通知按它判定）
const UINT kIdSearchBox   = 115;   //搜索框
const UINT kIdViewToggle  = 116;   //「缩略图 / 列表」切换按钮

// ---- 右键菜单项的命令号（DuiMenu::TrackPopup 的返回值）----
const UINT kMenuOpen      = 1001;  //打开
const UINT kMenuDownload  = 1002;  //下载
const UINT kMenuRename    = 1003;  //重命名
const UINT kMenuDelete    = 1004;  //删除
const UINT kMenuMove      = 1005;  //移动到…
const UINT kMenuNewFolder = 1006;  //新建文件夹
const UINT kMenuRefresh   = 1007;  //刷新
const UINT kMenuPreview   = 1008;  //预览

// ---- 名字输入窗口的用途编号 ----
const int kNamePurposeNewFolder = 1;   //新建文件夹
const int kNamePurposeRename    = 2;   //重命名

// ---- 配色 ----
const COLORREF kBgColor         = RGB(0xFF, 0xFF, 0xFF);   //客户区白底
const COLORREF kBannerBgColor   = RGB(0xFF, 0xF4, 0xE5);   //横幅浅橙底
const COLORREF kBannerTextColor = RGB(0x8A, 0x4B, 0x00);   //横幅深橙字
const COLORREF kWeakTextColor   = RGB(0x9A, 0x98, 0x93);   //状态栏、大小、时间的弱化灰
const COLORREF kTextColor       = RGB(0x1F, 0x1F, 0x1F);   //名称
const COLORREF kTrackColor      = RGB(0xE6, 0xEA, 0xF0);   //进度条底色
const COLORREF kBarColor        = RGB(0x3D, 0x7E, 0xF0);   //进度条填充
const COLORREF kDoneColor       = RGB(0x2E, 0x9E, 0x5B);   //完成的状态文字
const COLORREF kFailColor       = RGB(0xD9, 0x3B, 0x3B);   //失败的状态文字
const COLORREF kSelectedBg      = RGB(0xEA, 0xF1, 0xFE);   //选中行的底色，与「文件共享」窗口一致
const COLORREF kHoverBg         = RGB(0xF5, 0xF7, 0xFA);   //鼠标所在行的底色

// ---- 文字 ----
const wchar_t* const kWindowTitle     = TR_NOOP(L"文件共享");
const wchar_t* const kTitleSeparator  = L" - ";
const wchar_t* const kPathSeparator   = L" / ";
const wchar_t* const kUpText          = TR_NOOP(L"上一级");
const wchar_t* const kRefreshText     = TR_NOOP(L"刷新");
const wchar_t* const kHeaderNameText  = TR_NOOP(L"名称");
const wchar_t* const kHeaderSizeText  = TR_NOOP(L"大小");
const wchar_t* const kHeaderTimeText  = TR_NOOP(L"修改时间");
const wchar_t* const kAscendingMark   = L" \x25B2";   //▲
const wchar_t* const kDescendingMark  = L" \x25BC";   //▼
const wchar_t* const kConnectingText  = TR_NOOP(L"正在连接分享者…");
const wchar_t* const kLoadingText     = TR_NOOP(L"正在加载…");
const wchar_t* const kCountFormat     = TR_NOOP(L"共 %u 项");
const wchar_t* const kDownloadText    = TR_NOOP(L"下载");
const wchar_t* const kUploadFilesText = TR_NOOP(L"上传文件");
const wchar_t* const kUploadFolderText= TR_NOOP(L"上传文件夹");
const wchar_t* const kCancelText      = TR_NOOP(L"取消");
const wchar_t* const kTransfersTitle  = TR_NOOP(L"传输列表");
const wchar_t* const kDirDownload     = TR_NOOP(L"下载");
const wchar_t* const kDirUpload       = TR_NOOP(L"上传");
const wchar_t* const kXferConnecting  = TR_NOOP(L"正在连接…");
const wchar_t* const kXferQueued      = TR_NOOP(L"排队中");
const wchar_t* const kXferDone        = TR_NOOP(L"已完成");
const wchar_t* const kXferCanceled    = TR_NOOP(L"已取消");
const wchar_t* const kXferFailPrefix  = TR_NOOP(L"失败：");
const wchar_t* const kXferMoreFormat  = TR_NOOP(L"%s 等 %u 项");
const wchar_t* const kNoSelectionText = TR_NOOP(L"请先选中要下载的文件或文件夹");
const wchar_t* const kPickFolderTitle = TR_NOOP(L"选择下载到的文件夹");
const wchar_t* const kPickUploadTitle = TR_NOOP(L"选择要上传的文件");
const wchar_t* const kPickUploadDirTitle = TR_NOOP(L"选择要上传的文件夹");
const wchar_t* const kNewFolderText   = TR_NOOP(L"新建文件夹");
const wchar_t* const kNewFolderPrompt = TR_NOOP(L"文件夹名称");
const wchar_t* const kNewFolderDefault= TR_NOOP(L"新建文件夹");
const wchar_t* const kRenameTitle     = TR_NOOP(L"重命名");
const wchar_t* const kRenamePrompt    = TR_NOOP(L"新名称");
const wchar_t* const kMenuOpenText    = TR_NOOP(L"打开");
const wchar_t* const kMenuDownloadText= TR_NOOP(L"下载…");
const wchar_t* const kMenuRenameText  = TR_NOOP(L"重命名…");
const wchar_t* const kMenuDeleteText  = TR_NOOP(L"删除");
const wchar_t* const kMenuMoveText    = TR_NOOP(L"移动到…");
const wchar_t* const kMenuNewFolderText = TR_NOOP(L"新建文件夹…");
const wchar_t* const kMenuRefreshText = TR_NOOP(L"刷新");
const wchar_t* const kDeleteTitle     = TR_NOOP(L"删除");
const wchar_t* const kNoticeSeparator = L" · ";
const wchar_t* const kSearchPlaceholder = TR_NOOP(L"搜索当前文件夹");
const wchar_t* const kSearchingFormat = TR_NOOP(L"正在搜索…已找到 %u 项");
const wchar_t* const kSearchDoneFormat = TR_NOOP(L"找到 %u 项");
const wchar_t* const kSearchTruncatedFormat = TR_NOOP(L"只显示前 %u 项：结果太多或搜索用时太长，可以换一个更具体的关键字");
const wchar_t* const kSearchFailPrefix = TR_NOOP(L"搜索失败：");
const wchar_t* const kSearchPathFormat = TR_NOOP(L"在「%s」中搜索「%s」");
const wchar_t* const kMenuPreviewText = TR_NOOP(L"预览");
const wchar_t* const kViewGridText = TR_NOOP(L"缩略图");
const wchar_t* const kViewListText = TR_NOOP(L"列表");
const wchar_t* const kPreviewLoadingText = TR_NOOP(L"正在加载预览图…");
const wchar_t* const kPreviewFailPrefix = TR_NOOP(L"预览失败：");
const wchar_t* const kPreviewNoCacheText = TR_NOOP(L"预览失败：无法写入本机的预览缓存目录");
const wchar_t* const kPreviewFilePrefix = L"\\preview_";
const wchar_t* const kPreviewFileExt = L".jpg";
const wchar_t* const kPreviewFilePattern = L"\\preview_*.jpg";

//搜索提示文字的缓冲区字符数（含路径与关键字）
const size_t kSearchTextBufChars = 512;

//传输行的状态
const int kXferRunning = 0;
const int kXferDoneState = 1;
const int kXferFailed = 2;

//状态栏文字缓冲区的字符数
const size_t kStatusBufChars = 64;

//拖出：拖动开始时等待选中目录的目录树最多多久，单位：毫秒
const DWORD kDragTreeWaitMs = 5000;

//拖出：等待目录树期间每轮处理完消息后休眠多久，单位：毫秒（避免空转占满一个核）
const DWORD kDragTreeWaitSliceMs = 10;

//拖出：目录树条目超过上限时记在 m_dragTreeErrors 里的值（错误码都是正数，不会与它相同）
const int kDragTreeTooMany = -1;

//GetKeyState 的返回值里表示「按着」的位
const SHORT kKeyDownMask = static_cast<SHORT>(0x8000);

const wchar_t* const kDropHintText    = TR_NOOP(L"松开鼠标即上传到当前文件夹");
const wchar_t* const kDragWaitText    = TR_NOOP(L"正在读取文件夹的内容…");
const wchar_t* const kDragTempFailText = TR_NOOP(L"无法创建本机临时目录，不能拖出");
const wchar_t* const kDragStartedText = TR_NOOP(L"正在下载拖出的内容，完成后由目标文件夹复制");

//UTF-8 转成界面用的宽字符串
std::wstring Wide(const std::string& utf8)
{
    std::wstring w;
    ShareUtf8ToWide(utf8, w);
    return w;
}

} // namespace

CShareBrowserWindow::CShareBrowserWindow()
    : m_service(NULL)
    , m_visitor(NULL)
    , m_shareId(0)
    , m_cascadeFrom(NULL)
    , m_hasNormalRect(false)
    , m_sortColumn(share_sort_name)
    , m_sortAscending(true)
    , m_perms(0)
    , m_root(NULL)
    , m_bannerBox(NULL)
    , m_banner(NULL)
    , m_upButton(NULL)
    , m_pathLabel(NULL)
    , m_refreshButton(NULL)
    , m_toolbar(NULL)
    , m_downloadButton(NULL)
    , m_uploadFiles(NULL)
    , m_uploadFolder(NULL)
    , m_newFolder(NULL)
    , m_searchMode(false)
    , m_searchReqId(0)
    , m_uiHost(NULL)
    , m_previewIndex(0)
    , m_previewReqId(0)
    , m_gridMode(false)
    , m_gridColumns(1)
    , m_thumbInFlight(0)
    , m_pressIndex(-1)
    , m_deferredSelect(-1)
    , m_treeReqId(0)
    , m_dropHintShown(false)
    , m_searchBox(NULL)
    , m_viewToggle(NULL)
    , m_header(NULL)
    , m_transferPanel(NULL)
    , m_transferList(NULL)
    , m_headerName(NULL)
    , m_headerSize(NULL)
    , m_headerTime(NULL)
    , m_list(NULL)
    , m_status(NULL)
{
    ::SetRectEmpty(&m_lastNormalRect);
}

void CShareBrowserWindow::SetPlacement(const std::wstring& iniPath, HWND cascadeFrom)
{
    m_placementIni = iniPath;
    m_cascadeFrom = cascadeFrom;
}

CShareBrowserWindow::~CShareBrowserWindow()
{
    Close();
}

bool CShareBrowserWindow::Open(HWND owner, CShareService* service, int64_t shareId)
{
    if (service == NULL)
        return false;

    //上次关闭时是最大化的：新建窗口后最大化显示
    bool maximize = false;
    if (!IsWindow())
    {
        m_service = service;
        m_shareId = shareId;
        m_visitor = m_service->AcquireVisitor(shareId, this);
        if (m_visitor == NULL)
        {
            LOG_ERROR("share browser: no visitor for shareid %lld (service not attached).", static_cast<long long>(shareId));
            return false;
        }

        //父窗口传 NULL：作为独立的顶层窗口；owner 只用来居中
        if (Create(NULL, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("share browser: Create failed, error: %lu.", ::GetLastError());
            m_service->ReleaseVisitor(shareId);
            m_visitor = NULL;
            return false;
        }

        SetTitle(TR(kWindowTitle));
        SetTitleBarHeight(kTitleBarHeight);
        SetButtons(true, true, true);
        //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 图标（图标资源在 exe 里，见 TitleBarIcon.h）
        SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        m_programIcons.ApplyTaskIcons(m_hWnd);
        SetResizable(true);
        SetMinSize(kMinWindowWidth, kMinWindowHeight);
        BuildContent();
        //DuiFrameWindow 的 ResizeClient 设的是整窗尺寸，标题栏也在其内（UI 约定第 7 条）
        ResizeClient(kContentWidth, kContentHeight + kTitleBarHeight);
        //2026-10-07 起不再以参照窗口（聊天窗）为中心：第一次在它所在显示器的工作区居中，之后按上次关闭时的位置与大小，
        //同时开着别的浏览窗口时相对它错开
        HWND anchor = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
        maximize = ApplySavedPlacement(anchor);

        //拖入上传：整个窗口都是放置目标，是否接收由 OnUploadDragEnter 按权限判定
        m_dropHelper.reset(new balloonwjui::DuiDropTargetHelper());
        m_dropHelper->SetCallbacks(std::bind(&CShareBrowserWindow::OnUploadFilesDropped, this, std::placeholders::_1),
                                   balloonwjui::DuiDropTargetHelper::BitmapCallback());
        m_dropHelper->SetDragCallbacks(std::bind(&CShareBrowserWindow::OnUploadDragEnter, this, std::placeholders::_1,
                                                 std::placeholders::_2),
                                       balloonwjui::DuiDropTargetHelper::DragOverCallback(),
                                       std::bind(&CShareBrowserWindow::OnUploadDragLeave, this));
        if (!m_dropHelper->Register(m_hWnd))
            LOG_WARNING("share browser: cannot register the upload drop target (OLE not initialized?).");

        m_currentPath.clear();
        m_entries.clear();
        m_errorText.clear();
        m_shareStateText.clear();
        UpdatePathBar();
        LOG_INFO("share browser opened, shareid: %lld.", static_cast<long long>(shareId));
        //上次关闭窗口时还在进行的传输（第 4 期起在后台继续）：接回传输列表
        AdoptBackgroundTransfers();
        //后台的访问状态机可能已经就绪：它不会再报一次状态变化，按「就绪」刷新标题、权限与上传按钮
        if (m_visitor->GetState() == share_visitor_ready)
            OnVisitorStateChanged(share_visitor_ready, 0);
        RequestList(std::string());
    }

    ShowWindow(maximize ? SW_SHOWMAXIMIZED : SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    //初始焦点放在文件列表上：否则焦点落在第一个可用的按钮（共享根时是「刷新」）上，按钮带着焦点虚线框
    if (m_list != NULL)
        m_list->SetFocus();
    return true;
}

void CShareBrowserWindow::Close()
{
    //先关两个小窗口：它们打开时禁用了本窗口，关闭时会恢复；选目录窗口的关闭回调在 m_visitor 置空后什么也不做
    m_nameDialog.Close();
    m_folderPick.Close();
    m_movePaths.clear();
    m_renamePath.clear();
    m_opNotice.clear();
    m_searchMode = false;
    m_searchReqId = 0;
    m_searchKeyword.clear();
    //不再作为预览的来源：看图窗里的 ← / → 不再转给本窗口
    if (m_uiHost != NULL)
        m_uiHost->UiPreviewSourceGone(this);
    if (m_visitor != NULL && m_previewReqId != 0)
        m_visitor->CancelThumb(m_previewReqId);
    m_previewReqId = 0;
    m_previewPaths.clear();
    m_previewIndex = 0;
    ClearThumbCache();
    m_gridMode = false;
    m_selection.Clear();
    m_pressIndex = -1;
    m_deferredSelect = -1;
    m_dragTrees.clear();
    m_dragTreeErrors.clear();
    m_treeQueue.clear();
    m_treePath.clear();
    m_treeReqId = 0;
    //还在为拖出下载的传输（第 4 期起）：窗口关闭后照常进行，拖出任务的结束由业务总控经结束回调完成，这里只丢掉计数
    m_dragByTransfer.clear();
    if (m_dropHelper)
    {
        m_dropHelper->Unregister();
        m_dropHelper.reset();
    }
    m_dropHintShown = false;
    m_statusBeforeDrop.clear();
    if (IsWindow())
        ::KillTimer(m_hWnd, kThumbTimerId);
    if (m_service != NULL && m_visitor != NULL)
    {
        m_service->ReleaseVisitor(m_shareId);
        m_visitor = NULL;
    }
    if (IsWindow())
    {
        //销毁之前保存位置：最小化状态下关闭时还要读窗口的还原态信息
        SavePlacement();
        DestroyWindow();
        LOG_INFO("share browser closed, shareid: %lld.", static_cast<long long>(m_shareId));
    }
    m_hasNormalRect = false;
    m_root = NULL;
    m_bannerBox = NULL;
    m_banner = NULL;
    m_upButton = NULL;
    m_pathLabel = NULL;
    m_refreshButton = NULL;
    m_toolbar = NULL;
    m_downloadButton = NULL;
    m_uploadFiles = NULL;
    m_uploadFolder = NULL;
    m_newFolder = NULL;
    m_searchBox = NULL;
    m_viewToggle = NULL;
    m_header = NULL;
    m_transferPanel = NULL;
    m_transferList = NULL;
    m_transfers.clear();
    m_headerName = NULL;
    m_headerSize = NULL;
    m_headerTime = NULL;
    m_list = NULL;
    m_status = NULL;
}

void CShareBrowserWindow::OnVisitorStateChanged(int state, int errCode)
{
    switch (state)
    {
    //正在打开：状态栏提示，横幅清掉上一次的错误
    case share_visitor_opening:
    case share_visitor_awaiting_relay:
    case share_visitor_connecting:
    case share_visitor_waiting_hello:
        m_errorText.clear();
        SetBanner(m_shareStateText);
        SetStatus(TR(kConnectingText));
        break;

    //就绪：标题换成共享名称（2026-10-04 起不再用取自真实路径的根名），路径栏同步
    case share_visitor_ready:
        if (m_visitor != NULL && (!m_visitor->GetShareName().empty() || !m_visitor->GetRootName().empty()))
            SetTitle((RootLabel() + kTitleSeparator + TR(kWindowTitle)).c_str());
        if (m_visitor != NULL)
            m_perms = m_visitor->GetPerms();
        UpdatePathBar();
        UpdateUploadButtons();
        break;

    //失败：横幅显示原因（排队的传输由业务总控以同一原因结束，经传输列表与结束回调报告）
    case share_visitor_failed:
        m_errorText = ShareBrowseModel::ErrorText(errCode);
        SetBanner(m_errorText);
        SetStatus(std::wstring());
        break;

    //空闲回收：界面保持原样，下一次操作时自动重新打开
    default:
        break;
    }
}

void CShareBrowserWindow::OnVisitorListed(const std::string& path, int code, const std::vector<ShareEntry>& entries)
{
    //选目标文件夹窗口打开期间，列目录请求都是它发的
    if (m_folderPick.IsOpen())
    {
        m_folderPick.OnListed(path, code, entries);
        return;
    }
    //单文件共享没有目录可列：列表里只放那个文件本身，供下载
    if (code == error_code_share_kind_mismatch && m_visitor != NULL && m_visitor->GetKind() == share_kind_file)
    {
        ShareEntry file;
        file.m_name = m_visitor->GetRootName();
        std::vector<ShareEntry> single(1, file);
        OnVisitorListed(std::string(), 0, single);
        return;
    }
    if (code != 0)
    {
        m_errorText = ShareBrowseModel::ErrorText(code);
        SetBanner(m_errorText);
        SetStatus(std::wstring());
        LOG_INFO("share browser list failed, shareid: %lld, code: %d.", static_cast<long long>(m_shareId), code);
        return;
    }

    m_errorText.clear();
    SetBanner(m_shareStateText);
    m_currentPath = path;
    m_entries = entries;
    //列表内容换了：选择与拖出用的目录树都作废
    m_selection.Clear();
    m_deferredSelect = -1;
    m_dragTrees.clear();
    m_dragTreeErrors.clear();
    m_treeQueue.clear();
    m_treePath.clear();
    m_treeReqId = 0;
    ApplySort();
    UpdatePathBar();
    if (m_list != NULL)
        m_list->SetScrollPos(0);
    //目录内容可能变了（刷新、写操作之后）：缩略图重新取
    ClearThumbCache();

    wchar_t buf[kStatusBufChars] = { 0 };
    _snwprintf_s(buf, kStatusBufChars, _TRUNCATE, TR(kCountFormat), static_cast<unsigned>(m_entries.size()));
    //刚完成的写操作：刷新完之后在条目数后面附上结果
    std::wstring status(buf);
    if (!m_opNotice.empty())
    {
        status += kNoticeSeparator + m_opNotice;
        m_opNotice.clear();
    }
    SetStatus(status);
    LOG_INFO("share browser listed, shareid: %lld, entries: %u.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(m_entries.size()));
    UpdateUploadButtons();
}

void CShareBrowserWindow::OnVisitorPermsChanged(int perms)
{
    m_perms = perms;
    UpdateUploadButtons();
}

void CShareBrowserWindow::OnVisitorShareState(int state, int reason)
{
    m_shareStateText = ShareBrowseModel::StateText(state, reason);
    //有错误时错误优先；共享恢复可用时清掉横幅
    SetBanner(m_errorText.empty() ? m_shareStateText : m_errorText);
}

void CShareBrowserWindow::OnVisitorOpResult(int64_t reqId, int opType, int code, const std::vector<ShareOpItem>& items)
{
    const std::wstring text = ShareBrowseModel::OpResultText(opType, code, items);
    LOG_INFO("share browser write op result, shareid: %lld, reqid: %lld, type: %d, code: %d.",
             static_cast<long long>(m_shareId), static_cast<long long>(reqId), opType, code);
    //至少有一条成功（或整体成功）时目录内容变了：刷新，并在刷新完的状态栏上附上结果；全部失败时只显示原因
    bool anySucceeded = (code == error_code_ok);
    for (size_t i = 0; i < items.size() && !anySucceeded; ++i)
        anySucceeded = (items[i].m_code == error_code_ok);
    if (anySucceeded)
    {
        m_opNotice = text;
        if (m_searchMode)
            StartSearch(m_searchKeyword);
        else
            RequestList(m_currentPath);
        return;
    }
    SetStatus(text);
}

void CShareBrowserWindow::OnVisitorSearchResult(int64_t reqId, const std::vector<ShareEntry>& hits, bool done,
                                                bool truncated, int code)
{
    if (!m_searchMode || reqId != m_searchReqId)
        return;
    if (done)
        m_searchReqId = 0;
    if (code != 0)
    {
        SetStatus(TR(kSearchFailPrefix) + ShareBrowseModel::ErrorText(code));
        LOG_INFO("share browser search failed, shareid: %lld, code: %d.", static_cast<long long>(m_shareId), code);
        return;
    }
    m_entries.insert(m_entries.end(), hits.begin(), hits.end());
    ApplySort();
    UpdateSearchStatus(done, truncated);
    if (done)
        LOG_INFO("share browser search done, shareid: %lld, hits: %u, truncated: %d.", static_cast<long long>(m_shareId),
                 static_cast<unsigned>(m_entries.size()), truncated ? 1 : 0);
}

bool CShareBrowserWindow::StartSearch(const std::wstring& keyword)
{
    if (m_visitor == NULL || !IsWindow())
        return false;
    if (keyword.empty())
    {
        //关键字清空：回到目录列表
        if (m_searchMode)
            RequestList(m_currentPath);
        return false;
    }
    m_searchMode = true;
    m_searchKeyword = keyword;
    //搜索框与正在搜索的关键字保持一致（写操作之后重新搜索、测试直接调用时都不是从搜索框发起的）
    if (m_searchBox != NULL && std::wstring(m_searchBox->GetText().GetString()) != keyword)
        m_searchBox->SetTextNoNotify(keyword.c_str());
    m_entries.clear();
    m_selection.Clear();
    m_deferredSelect = -1;
    m_dragTrees.clear();
    m_dragTreeErrors.clear();
    m_treeQueue.clear();
    m_treePath.clear();
    m_treeReqId = 0;
    ClearThumbCache();
    ApplySort();
    if (m_list != NULL)
        m_list->SetScrollPos(0);
    UpdatePathBar();
    m_searchReqId = m_visitor->Search(m_currentPath, ShareWideToUtf8(keyword), static_cast<int64_t>(::GetTickCount64()));
    UpdateSearchStatus(false, false);
    LOG_INFO("share browser search started, shareid: %lld, reqid: %lld.", static_cast<long long>(m_shareId),
             static_cast<long long>(m_searchReqId));
    return true;
}

bool CShareBrowserWindow::IsSearchMode() const
{
    return m_searchMode;
}

void CShareBrowserWindow::UpdateSearchStatus(bool done, bool truncated)
{
    wchar_t buf[kSearchTextBufChars] = { 0 };
    const unsigned count = static_cast<unsigned>(m_entries.size());
    if (!done)
        _snwprintf_s(buf, kSearchTextBufChars, _TRUNCATE, TR(kSearchingFormat), count);
    else if (truncated)
        _snwprintf_s(buf, kSearchTextBufChars, _TRUNCATE, TR(kSearchTruncatedFormat), count);
    else
        _snwprintf_s(buf, kSearchTextBufChars, _TRUNCATE, TR(kSearchDoneFormat), count);
    std::wstring status(buf);
    //写操作之后重新搜索：结束时附上写操作的结果
    if (done && !m_opNotice.empty())
    {
        status += kNoticeSeparator + m_opNotice;
        m_opNotice.clear();
    }
    SetStatus(status);
}

void CShareBrowserWindow::SetUiHost(IShareUiHost* host)
{
    m_uiHost = host;
}

bool CShareBrowserWindow::PreviewRow(int rowIndex)
{
    if (m_visitor == NULL || !IsWindow() || rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_entries.size())
        return false;
    const ShareEntry& entry = m_entries[static_cast<size_t>(rowIndex)];
    if (entry.m_isDir || !ShareThumbnail::IsImageName(entry.m_name))
        return false;
    //当前列表里的全部图片按显示顺序记下，看图窗里在它们之间翻页
    m_previewPaths.clear();
    m_previewIndex = 0;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].m_isDir || !ShareThumbnail::IsImageName(m_entries[i].m_name))
            continue;
        if (i == static_cast<size_t>(rowIndex))
            m_previewIndex = m_previewPaths.size();
        m_previewPaths.push_back(RowPath(static_cast<int>(i)));
    }
    RequestPreview();
    return true;
}

void CShareBrowserWindow::OnPreviewNavigate(int delta)
{
    if (m_previewPaths.empty() || m_visitor == NULL)
        return;
    const int64_t next = static_cast<int64_t>(m_previewIndex) + delta;
    //到头了：不绕回
    if (next < 0 || next >= static_cast<int64_t>(m_previewPaths.size()))
        return;
    m_previewIndex = static_cast<size_t>(next);
    RequestPreview();
}

void CShareBrowserWindow::RequestPreview()
{
    if (m_visitor == NULL || m_previewIndex >= m_previewPaths.size())
        return;
    //上一张还没来就翻页：不再要上一张
    if (m_previewReqId != 0)
        m_visitor->CancelThumb(m_previewReqId);
    m_previewReqId = m_visitor->RequestThumb(m_previewPaths[m_previewIndex], PreviewEdge(),
                                             static_cast<int64_t>(::GetTickCount64()));
    SetStatus(TR(kPreviewLoadingText));
}

int CShareBrowserWindow::PreviewEdge() const
{
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    int edge = kShareThumbMaxEdge;
    if (::GetMonitorInfoW(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
    {
        const int w = mi.rcMonitor.right - mi.rcMonitor.left;
        const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        edge = (w > h) ? w : h;
    }
    return ShareThumbnail::ClampEdge(edge);
}

void CShareBrowserWindow::OnVisitorThumbResult(int64_t reqId, const std::string& path, int code, int width, int height,
                                               const std::string& jpeg)
{
    if (reqId != 0 && reqId != m_previewReqId)
    {
        //缩略图视图的格子
        std::map<std::string, ThumbCell>::iterator it = m_thumbCache.find(path);
        if (it == m_thumbCache.end() || it->second.m_reqId != reqId)
            return;
        it->second.m_reqId = 0;
        if (m_thumbInFlight > 0)
            --m_thumbInFlight;
        if (code != 0 || jpeg.empty())
        {
            it->second.m_failed = true;
        }
        else
        {
            //JPEG 解码成位图（GDI+ 已由业务总控在 Attach 时初始化）
            HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, jpeg.size());
            IStream* stream = NULL;
            if (mem != NULL)
            {
                memcpy(::GlobalLock(mem), jpeg.data(), jpeg.size());
                ::GlobalUnlock(mem);
                if (FAILED(::CreateStreamOnHGlobal(mem, TRUE, &stream)))
                {
                    ::GlobalFree(mem);
                    stream = NULL;
                }
            }
            HBITMAP bmp = NULL;
            if (stream != NULL)
            {
                {
                    Gdiplus::Bitmap decoded(stream);
                    if (decoded.GetLastStatus() == Gdiplus::Ok)
                        decoded.GetHBITMAP(Gdiplus::Color(0xFF, 0xFF, 0xFF), &bmp);
                }
                stream->Release();
            }
            it->second.m_bitmap = bmp;
            it->second.m_width = width;
            it->second.m_height = height;
            it->second.m_failed = (bmp == NULL);
        }
        if (m_list != NULL)
            m_list->Invalidate();
        return;
    }
    if (reqId != m_previewReqId || reqId == 0)
        return;
    m_previewReqId = 0;
    if (code != 0)
    {
        SetStatus(TR(kPreviewFailPrefix) + ShareBrowseModel::ErrorText(code));
        LOG_INFO("share browser preview failed, shareid: %lld, code: %d.", static_cast<long long>(m_shareId), code);
        return;
    }
    const std::wstring dir = (m_service != NULL) ? m_service->GetPreviewCacheDir() : std::wstring();
    if (dir.empty())
    {
        SetStatus(TR(kPreviewNoCacheText));
        return;
    }
    //上次的预览图可能还被看图窗占用着：先删能删的，再用新名字写，不覆盖
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((dir + kPreviewFilePattern).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            ::DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    wchar_t stamp[kStatusBufChars] = { 0 };
    _snwprintf_s(stamp, kStatusBufChars, _TRUNCATE, L"%llu", static_cast<unsigned long long>(::GetTickCount64()));
    const std::wstring file = dir + kPreviewFilePrefix + stamp + kPreviewFileExt;
    HANDLE f = ::CreateFileW(ShareToLongPath(file).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    const bool ok = (f != INVALID_HANDLE_VALUE)
                    && ::WriteFile(f, jpeg.data(), static_cast<DWORD>(jpeg.size()), &written, NULL) != FALSE
                    && written == jpeg.size();
    if (f != INVALID_HANDLE_VALUE)
        ::CloseHandle(f);
    if (!ok)
    {
        SetStatus(TR(kPreviewNoCacheText));
        return;
    }
    SetStatus(Wide(path));
    if (m_uiHost != NULL)
        m_uiHost->UiOpenImage(m_hWnd, file, this);
    LOG_INFO("share browser preview shown, shareid: %lld, bytes: %u.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(jpeg.size()));
}

void CShareBrowserWindow::SetGridMode(bool grid)
{
    if (m_gridMode == grid || !IsWindow())
        return;
    const int selected = SelectedEntry();
    m_gridMode = grid;
    if (m_viewToggle != NULL)
        m_viewToggle->SetText(grid ? TR(kViewListText) : TR(kViewGridText));
    //缩略图视图没有「大小」「修改时间」两列：隐藏表头；SetVisible 不会重排，对父容器强制重排（UI 约定第 5 条）
    if (m_header != NULL && m_root != NULL)
    {
        m_header->SetVisible(!grid);
        m_root->ForceLayout(m_root->GetRect());
        m_root->Invalidate();
    }
    RefreshListRows();
    //选择在两种视图之间保留，把当前项滚到可见处
    if (selected >= 0 && m_list != NULL)
        m_list->EnsureVisible((grid && m_gridColumns > 0) ? selected / m_gridColumns : selected);
    if (grid)
    {
        ::SetTimer(m_hWnd, kThumbTimerId, kThumbTimerMs, NULL);
        RequestVisibleThumbs();
    }
    else
    {
        ::KillTimer(m_hWnd, kThumbTimerId);
    }
    LOG_INFO("share browser view changed, shareid: %lld, grid: %d.", static_cast<long long>(m_shareId), grid ? 1 : 0);
}

bool CShareBrowserWindow::IsGridMode() const
{
    return m_gridMode;
}

size_t CShareBrowserWindow::GetCachedThumbCount() const
{
    size_t n = 0;
    for (std::map<std::string, ThumbCell>::const_iterator it = m_thumbCache.begin(); it != m_thumbCache.end(); ++it)
    {
        if (it->second.m_bitmap != NULL)
            ++n;
    }
    return n;
}

void CShareBrowserWindow::SelectEntry(int index)
{
    m_selection.SelectOnly((index >= 0 && static_cast<size_t>(index) < m_entries.size()) ? index : -1);
    if (m_list != NULL)
        m_list->Invalidate();
}

int CShareBrowserWindow::SelectedEntry() const
{
    const int primary = m_selection.Primary();
    if (primary >= 0 && m_selection.Contains(primary))
        return primary;
    const std::vector<int> rows = m_selection.Sorted();
    return rows.empty() ? -1 : rows[0];
}

std::vector<int> CShareBrowserWindow::ActionRows(int rowIndex) const
{
    if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_entries.size())
        return std::vector<int>();
    if (m_selection.Contains(rowIndex))
        return m_selection.Sorted();
    return std::vector<int>(1, rowIndex);
}

void CShareBrowserWindow::SelectRows(const std::vector<int>& rows)
{
    m_selection.Clear();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i] >= 0 && static_cast<size_t>(rows[i]) < m_entries.size())
            m_selection.Click(rows[i], true, false);
    }
    if (m_list != NULL)
        m_list->Invalidate();
}

std::vector<int> CShareBrowserWindow::GetSelectedRows() const
{
    return m_selection.Sorted();
}

std::vector<std::string> CShareBrowserWindow::SelectedPaths() const
{
    //当前项的路径放在最后，恢复时最后选中它，使它仍是当前项
    std::vector<std::string> paths;
    const std::vector<int> rows = m_selection.Sorted();
    const int primary = SelectedEntry();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i] != primary)
            paths.push_back(RowPath(rows[i]));
    }
    if (primary >= 0)
        paths.push_back(RowPath(primary));
    return paths;
}

void CShareBrowserWindow::RestoreSelection(const std::vector<std::string>& paths)
{
    m_selection.Clear();
    if (paths.empty())
        return;
    const std::string primaryPath = paths.back();
    //按路径找回各条目的新位置，原来的当前项最后选中，保持为当前项
    int primaryRow = -1;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const std::string path = RowPath(static_cast<int>(i));
        for (size_t k = 0; k < paths.size(); ++k)
        {
            if (paths[k] != path)
                continue;
            if (path == primaryPath)
                primaryRow = static_cast<int>(i);
            else
                m_selection.Click(static_cast<int>(i), true, false);
            break;
        }
    }
    if (primaryRow >= 0)
        m_selection.Click(primaryRow, true, false);
}

void CShareBrowserWindow::RefreshListRows()
{
    if (m_list == NULL)
        return;
    if (m_gridMode)
    {
        ShareGridMetrics metrics;
        metrics.m_tileWidth = kTileWidth;
        metrics.m_tileHeight = kTileHeight;
        metrics.m_gap = kTileGap;
        metrics.m_padding = kGridPadding;
        const RECT rc = m_list->GetRect();
        m_gridColumns = ShareBrowseModel::GridColumns(rc.right - rc.left - kScrollBarWidth, metrics);
        m_list->SetRowHeight(kTileHeight + kTileGap);
        m_list->SetRowCount(ShareBrowseModel::GridRows(m_entries.size(), m_gridColumns));
    }
    else
    {
        m_list->SetRowHeight(kRowHeight);
        m_list->SetRowCount(static_cast<int>(m_entries.size()));
    }
    //设了行数之后按当前矩形重新排版，滚动条才会按新的行数摆放（balloonui 2026-10-03 已在设行数时自行处理，这里保留无害）
    m_list->ForceLayout(m_list->GetRect());
    m_list->Invalidate();
}

void CShareBrowserWindow::RequestVisibleThumbs()
{
    if (!m_gridMode || m_visitor == NULL || m_list == NULL || m_list->GetRowHeight() <= 0 || m_gridColumns <= 0)
        return;
    const RECT rc = m_list->GetRect();
    const int rowH = m_list->GetRowHeight();
    const int firstRow = m_list->GetScrollPos() / rowH;
    const int lastRow = (m_list->GetScrollPos() + (rc.bottom - rc.top)) / rowH;
    for (int row = firstRow; row <= lastRow && m_thumbInFlight < kMaxThumbsInFlight; ++row)
    {
        for (int col = 0; col < m_gridColumns && m_thumbInFlight < kMaxThumbsInFlight; ++col)
        {
            const size_t idx = static_cast<size_t>(row) * static_cast<size_t>(m_gridColumns) + static_cast<size_t>(col);
            if (idx >= m_entries.size())
                return;
            const ShareEntry& e = m_entries[idx];
            if (e.m_isDir || !ShareThumbnail::IsImageName(e.m_name))
                continue;
            const std::string path = RowPath(static_cast<int>(idx));
            if (m_thumbCache.find(path) != m_thumbCache.end())
                continue;
            ThumbCell cell;
            cell.m_reqId = m_visitor->RequestThumb(path, kTileImageEdge, static_cast<int64_t>(::GetTickCount64()));
            m_thumbCache[path] = cell;
            ++m_thumbInFlight;
        }
    }
}

void CShareBrowserWindow::ClearThumbCache()
{
    for (std::map<std::string, ThumbCell>::iterator it = m_thumbCache.begin(); it != m_thumbCache.end(); ++it)
    {
        if (it->second.m_bitmap != NULL)
            ::DeleteObject(it->second.m_bitmap);
        if (it->second.m_reqId != 0 && m_visitor != NULL)
            m_visitor->CancelThumb(it->second.m_reqId);
    }
    m_thumbCache.clear();
    m_thumbInFlight = 0;
}

LRESULT CShareBrowserWindow::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kThumbTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    if (!m_gridMode || m_list == NULL)
        return 0;
    //列表宽度变了（窗口缩放）：按新的列数重排
    ShareGridMetrics metrics;
    metrics.m_tileWidth = kTileWidth;
    metrics.m_tileHeight = kTileHeight;
    metrics.m_gap = kTileGap;
    metrics.m_padding = kGridPadding;
    const RECT rc = m_list->GetRect();
    if (ShareBrowseModel::GridColumns(rc.right - rc.left - kScrollBarWidth, metrics) != m_gridColumns)
        RefreshListRows();
    RequestVisibleThumbs();
    return 0;
}

void CShareBrowserWindow::PaintGridRow(HDC hdc, int rowIndex, const RECT& rowRect)
{
    ShareGridMetrics metrics;
    metrics.m_tileWidth = kTileWidth;
    metrics.m_tileHeight = kTileHeight;
    metrics.m_gap = kTileGap;
    metrics.m_padding = kGridPadding;
    HFONT oldFont = static_cast<HFONT>(::SelectObject(hdc, m_list->GetDefaultFont()));
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    for (int col = 0; col < m_gridColumns; ++col)
    {
        const size_t idx = static_cast<size_t>(rowIndex) * static_cast<size_t>(m_gridColumns) + static_cast<size_t>(col);
        if (idx >= m_entries.size())
            break;
        const ShareEntry& entry = m_entries[idx];
        const RECT tile = ShareBrowseModel::GridTileRect(rowRect, col, metrics);
        if (m_selection.Contains(static_cast<int>(idx)))
        {
            HBRUSH bg = ::CreateSolidBrush(kSelectedBg);
            ::FillRect(hdc, &tile, bg);
            ::DeleteObject(bg);
        }
        //图片区：缩略图居中；目录与其它文件画图标
        const int areaTop = tile.top + kTileInset;
        const int areaCx = (tile.left + tile.right) / 2;
        const int areaCy = areaTop + kTileImageEdge / 2;
        std::map<std::string, ThumbCell>::const_iterator it = m_thumbCache.end();
        if (!entry.m_isDir)
            it = m_thumbCache.find(RowPath(static_cast<int>(idx)));
        if (it != m_thumbCache.end() && it->second.m_bitmap != NULL)
        {
            HDC mem = ::CreateCompatibleDC(hdc);
            HGDIOBJ old = ::SelectObject(mem, it->second.m_bitmap);
            ::BitBlt(hdc, areaCx - it->second.m_width / 2, areaCy - it->second.m_height / 2, it->second.m_width,
                     it->second.m_height, mem, 0, 0, SRCCOPY);
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
        else
        {
            //目录、不是图片的文件与缩略图尚未取到的图片：本机的系统类型图标（文件按扩展名取）
            RECT icon = { areaCx - kGridIconSize / 2, areaCy - kGridIconSize / 2, areaCx + kGridIconSize / 2,
                          areaCy + kGridIconSize / 2 };
            shellicon::DrawItemIcon(hdc, entry.m_isDir ? shellicon::kItemFolder : shellicon::kItemFile,
                                    Wide(entry.m_name), icon);
        }
        //名字：图片区下方居中一行，过长时以省略号结尾（搜索结果显示路径的最后一段）
        RECT nameRect = { tile.left + kTileInset, tile.bottom - kTileNameHeight, tile.right - kTileInset, tile.bottom };
        const std::vector<std::string> parts = ShareBrowseModel::SplitPath(entry.m_name);
        const std::wstring name = Wide(parts.empty() ? entry.m_name : parts.back());
        ::SetTextColor(hdc, kTextColor);
        ::DrawTextW(hdc, name.c_str(), -1, &nameRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

std::string CShareBrowserWindow::RowPath(int rowIndex) const
{
    if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_entries.size())
        return std::string();
    const ShareEntry& entry = m_entries[static_cast<size_t>(rowIndex)];
    return m_searchMode ? entry.m_name : ShareBrowseModel::JoinPath(m_currentPath, entry.m_name);
}

void CShareBrowserWindow::OnShareNameEntered(int purpose, const std::wstring& name)
{
    if (purpose == kNamePurposeNewFolder)
    {
        MakeFolder(name);
        return;
    }
    //重命名：按打开名字输入窗口时记下的路径
    if (m_visitor == NULL || m_renamePath.empty())
        return;
    m_visitor->Rename(m_renamePath, ShareWideToUtf8(name), static_cast<int64_t>(::GetTickCount64()));
    m_renamePath.clear();
}

void CShareBrowserWindow::RequestFolderList(const std::string& path)
{
    if (m_visitor != NULL)
        m_visitor->List(path, static_cast<int64_t>(::GetTickCount64()));
}

void CShareBrowserWindow::OnShareFolderPicked(const std::string& dir)
{
    if (m_visitor == NULL || m_movePaths.empty())
        return;
    m_visitor->Move(m_movePaths, dir, static_cast<int64_t>(::GetTickCount64()));
    m_movePaths.clear();
}

void CShareBrowserWindow::OnShareFolderPickClosed()
{
    m_movePaths.clear();
    //选目录窗口用过同一个访问状态机列目录：重新列一次本窗口的当前目录，界面与状态机保持一致
    if (m_visitor != NULL && IsWindow())
        RequestList(m_currentPath);
}

bool CShareBrowserWindow::MakeFolder(const std::wstring& name)
{
    if (m_visitor == NULL)
        return false;
    m_visitor->MakeDirectory(ShareBrowseModel::JoinPath(m_currentPath, ShareWideToUtf8(name)),
                             static_cast<int64_t>(::GetTickCount64()));
    return true;
}

bool CShareBrowserWindow::RenameRow(int rowIndex, const std::wstring& newName)
{
    if (m_visitor == NULL || rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_entries.size())
        return false;
    m_visitor->Rename(RowPath(rowIndex), ShareWideToUtf8(newName), static_cast<int64_t>(::GetTickCount64()));
    return true;
}

bool CShareBrowserWindow::DeleteRow(int rowIndex)
{
    return DeleteRows(std::vector<int>(1, rowIndex));
}

bool CShareBrowserWindow::DeleteRows(const std::vector<int>& rows)
{
    if (m_visitor == NULL)
        return false;
    std::vector<std::string> paths;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i] >= 0 && static_cast<size_t>(rows[i]) < m_entries.size())
            paths.push_back(RowPath(rows[i]));
    }
    if (paths.empty())
        return false;
    m_visitor->Delete(paths, static_cast<int64_t>(::GetTickCount64()));
    return true;
}

bool CShareBrowserWindow::MoveRow(int rowIndex, const std::string& toDir)
{
    return MoveRows(std::vector<int>(1, rowIndex), toDir);
}

bool CShareBrowserWindow::MoveRows(const std::vector<int>& rows, const std::string& toDir)
{
    if (m_visitor == NULL)
        return false;
    std::vector<std::string> paths;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i] >= 0 && static_cast<size_t>(rows[i]) < m_entries.size())
            paths.push_back(RowPath(rows[i]));
    }
    if (paths.empty())
        return false;
    m_visitor->Move(paths, toDir, static_cast<int64_t>(::GetTickCount64()));
    return true;
}

void CShareBrowserWindow::ExecuteMenuItem(int rowIndex, int item)
{
    const bool onRow = rowIndex >= 0 && static_cast<size_t>(rowIndex) < m_entries.size();
    //下载、删除、移动作用于全部选中的条目（rowIndex 在选择里时），其余各项只作用于 rowIndex
    const std::vector<int> targets = ActionRows(rowIndex);
    switch (item)
    {
    //打开目录
    case share_menu_open:
        if (onRow)
            OnRowActivated(rowIndex);
        break;

    //预览图片
    case share_menu_preview:
        if (onRow)
            PreviewRow(rowIndex);
        break;

    //下载：选本机目录后下载
    case share_menu_download:
        if (!targets.empty())
        {
            std::vector<std::wstring> picked;
            if (SharePickItems(m_hWnd, true, false, true, TR(kPickFolderTitle), picked))
                DownloadRows(targets, picked[0]);
        }
        break;

    //重命名：名字输入窗口里预先填好原名
    case share_menu_rename:
        if (onRow)
        {
            m_renamePath = RowPath(rowIndex);
            //输入框里预先填好原名（搜索结果时名字是路径，只取最后一段）
            m_nameDialog.Open(m_hWnd, kNamePurposeRename, TR(kRenameTitle), TR(kRenamePrompt),
                              Wide(ShareBrowseModel::SplitPath(m_renamePath).back()), this);
        }
        break;

    //删除：先确认
    case share_menu_delete:
        if (!targets.empty())
        {
            std::vector<std::wstring> names;
            for (size_t i = 0; i < targets.size(); ++i)
                names.push_back(Wide(m_entries[static_cast<size_t>(targets[i])].m_name));
            const std::wstring text = ShareBrowseModel::DeleteConfirmText(names);
            if (::MessageBoxW(m_hWnd, text.c_str(), TR(kDeleteTitle), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) == IDOK)
                DeleteRows(targets);
        }
        break;

    //移动到…：弹选目标文件夹窗口，从当前目录开始
    case share_menu_move:
        if (!targets.empty() && m_visitor != NULL)
        {
            m_movePaths.clear();
            for (size_t i = 0; i < targets.size(); ++i)
                m_movePaths.push_back(RowPath(targets[i]));
            m_folderPick.Open(m_hWnd, this, this, m_currentPath, RootLabel());
        }
        break;

    //新建文件夹
    case share_menu_new_folder:
        m_nameDialog.Open(m_hWnd, kNamePurposeNewFolder, TR(kNewFolderText), TR(kNewFolderPrompt), TR(kNewFolderDefault), this);
        break;

    //刷新
    case share_menu_refresh:
        RequestList(m_currentPath);
        break;

    //不认识的项：忽略
    default:
        break;
    }
}

CShareNameDialog& CShareBrowserWindow::GetNameDialog()
{
    return m_nameDialog;
}

CShareFolderPickDialog& CShareBrowserWindow::GetFolderPickDialog()
{
    return m_folderPick;
}

std::wstring CShareBrowserWindow::GetStatusText() const
{
    return (m_status != NULL) ? std::wstring(m_status->GetText().GetString()) : std::wstring();
}

bool CShareBrowserWindow::IsNewFolderButtonVisible() const
{
    return m_newFolder != NULL && m_newFolder->IsVisible();
}

const std::string& CShareBrowserWindow::GetCurrentPath() const
{
    return m_currentPath;
}

size_t CShareBrowserWindow::GetEntryCount() const
{
    return m_entries.size();
}

int CShareBrowserWindow::FindRow(const std::wstring& name) const
{
    const std::string utf8 = ShareWideToUtf8(name);
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].m_name == utf8)
            return static_cast<int>(i);
    }
    return -1;
}

bool CShareBrowserWindow::IsBannerVisible() const
{
    return m_bannerBox != NULL && m_bannerBox->IsVisible();
}

std::wstring CShareBrowserWindow::GetBannerText() const
{
    return (m_banner != NULL) ? std::wstring(m_banner->GetText().GetString()) : std::wstring();
}

LRESULT CShareBrowserWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //不拦截消息：基类（DuiHost）随后按新尺寸重新布局
    bHandled = FALSE;
    RememberNormalRect();
    return 0;
}

LRESULT CShareBrowserWindow::OnMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    RememberNormalRect();
    return 0;
}

void CShareBrowserWindow::RememberNormalRect()
{
    if (!IsWindow() || IsIconic() || IsZoomed())
        return;
    RECT rc;
    if (!GetWindowRect(&rc) || rc.right <= rc.left || rc.bottom <= rc.top)
        return;
    m_lastNormalRect = rc;
    m_hasNormalRect = true;
}

bool CShareBrowserWindow::ApplySavedPlacement(HWND owner)
{
    m_hasNormalRect = false;
    winplace::WindowPlacementPrefs saved;
    LoadSharePlacement(m_placementIni.c_str(), kShareBrowserPlacementSection, saved);
    //默认大小取 ResizeClient 之后的整窗大小；最小大小与 SetMinSize 一致，都是物理像素
    RECT current;
    GetWindowRect(&current);
    SIZE defaultSize;
    defaultSize.cx = current.right - current.left;
    defaultSize.cy = current.bottom - current.top;
    SIZE minSize;
    minSize.cx = kMinWindowWidth;
    minSize.cy = kMinWindowHeight;
    const RECT start = ResolveSharePlacementRect(saved, defaultSize, minSize, owner);

    //同时开着别的浏览窗口：相对它错开。它最大化或最小化时窗口矩形不代表平时的位置，不相对它错开
    bool hasPrevious = false;
    POINT previousTopLeft;
    previousTopLeft.x = 0;
    previousTopLeft.y = 0;
    if (m_cascadeFrom != NULL && m_cascadeFrom != m_hWnd && ::IsWindow(m_cascadeFrom)
        && !::IsZoomed(m_cascadeFrom) && !::IsIconic(m_cascadeFrom))
    {
        RECT previous;
        if (::GetWindowRect(m_cascadeFrom, &previous))
        {
            hasPrevious = true;
            previousTopLeft.x = previous.left;
            previousTopLeft.y = previous.top;
        }
    }
    m_cascadeFrom = NULL;
    //起点所在显示器的工作区
    RECT work = start;
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(::MonitorFromRect(&start, MONITOR_DEFAULTTONEAREST), &mi))
        work = mi.rcWork;
    const RECT rc = CascadeSharePlacementRect(start, work, hasPrevious, previousTopLeft, kShareBrowserCascadeStep);
    SetWindowPos(NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    LOG_INFO("share browser: placement saved=%d cascade=%d -> (%ld,%ld %ldx%ld), maximized %d.",
             saved.m_hasSaved ? 1 : 0, hasPrevious ? 1 : 0, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             saved.m_maximized ? 1 : 0);
    return saved.m_hasSaved && saved.m_maximized;
}

void CShareBrowserWindow::SavePlacement()
{
    if (!m_hasNormalRect || m_placementIni.empty())
        return;
    winplace::WindowPlacementPrefs prefs;
    prefs.m_hasSaved = true;
    prefs.m_rect = m_lastNormalRect;
    //最小化时关窗（例如在任务栏上右键关闭）：按最小化之前是否最大化来记
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    const bool restoreToMax = IsIconic() && GetWindowPlacement(&wp) && (wp.flags & WPF_RESTORETOMAXIMIZED) != 0;
    prefs.m_maximized = (IsZoomed() != FALSE) || restoreToMax;
    const bool ok = SaveSharePlacement(m_placementIni.c_str(), kShareBrowserPlacementSection, prefs);
    LOG_INFO("share browser: save placement (%ld,%ld %ldx%ld), maximized %d, ok %d.",
             prefs.m_rect.left, prefs.m_rect.top, prefs.m_rect.right - prefs.m_rect.left,
             prefs.m_rect.bottom - prefs.m_rect.top, prefs.m_maximized ? 1 : 0, ok ? 1 : 0);
}

LRESULT CShareBrowserWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    Close();
    return 0;
}

LRESULT CShareBrowserWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //文件列表上点了右键（连 ctrlId 一起判，UI 约定第 4 条）
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_RCLICK && pNotify->ctrlId == kIdFileList)
    {
        ShowContextMenu();
        return 0;
    }
    //搜索框里按回车：在当前目录之下搜索；按 Esc：回到目录列表
    if (pNotify != NULL && pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ENTER && pNotify->ctrlId == kIdSearchBox)
    {
        if (m_searchBox != NULL)
            StartSearch(std::wstring(m_searchBox->GetText().GetString()));
        return 0;
    }
    if (pNotify != NULL && pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ESCAPE && pNotify->ctrlId == kIdSearchBox)
    {
        if (m_searchMode)
            RequestList(m_currentPath);
        return 0;
    }
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    switch (pNotify->ctrlId)
    {
    //切换列表视图与缩略图视图
    case kIdViewToggle:
        SetGridMode(!m_gridMode);
        return 0;

    //新建文件夹：弹名字输入窗口
    case kIdNewFolder:
        ExecuteMenuItem(-1, share_menu_new_folder);
        return 0;

    //上一级：搜索结果时回到目录列表；共享根时按钮不可用，这里再判一次
    case kIdUp:
        if (m_searchMode)
            RequestList(m_currentPath);
        else if (!m_currentPath.empty())
            RequestList(ShareBrowseModel::ParentPath(m_currentPath));
        return 0;

    //刷新：重新列当前目录（会话已空闲回收或失败时自动重新打开）
    case kIdRefresh:
        RequestList(m_currentPath);
        return 0;

    //下载选中的条目
    case kIdDownload:
        OnDownloadClicked();
        return 0;

    //上传文件、上传文件夹
    case kIdUploadFiles:
        OnUploadClicked(false);
        return 0;
    case kIdUploadFolder:
        OnUploadClicked(true);
        return 0;

    //取消选中的传输
    case kIdCancelXfer:
        OnCancelTransferClicked();
        return 0;

    //表头三列：切换排序
    case kIdHeaderName:
        OnHeaderClicked(share_sort_name);
        return 0;
    case kIdHeaderSize:
        OnHeaderClicked(share_sort_size);
        return 0;
    case kIdHeaderTime:
        OnHeaderClicked(share_sort_mtime);
        return 0;

    //其余控件的点击：交给基类
    default:
        bHandled = FALSE;
        return 0;
    }
}

void CShareBrowserWindow::BuildContent()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBgColor);
    m_root = root.get();

    //横幅：带底色的容器包一个标签，默认隐藏
    std::unique_ptr<DuiVBox> bannerBox(new DuiVBox());
    bannerBox->SetBgColor(kBannerBgColor);
    bannerBox->SetPadding(kBannerPadding, 0, kBannerPadding, 0);
    std::unique_ptr<DuiLabel> banner(new DuiLabel());
    banner->SetTextColor(kBannerTextColor);
    banner->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_banner = banner.get();
    bannerBox->AddChild(std::unique_ptr<DuiControl>(banner.release()), Hint().Weight(1));
    bannerBox->SetVisible(false);
    m_bannerBox = bannerBox.get();
    root->AddChild(std::unique_ptr<DuiControl>(bannerBox.release()), Hint().Fixed(kBannerHeight));

    //导航行：上一级 | 路径 | 搜索框 | 刷新
    std::unique_ptr<DuiHBox> navBar(new DuiHBox());
    navBar->SetPadding(kSidePadding, 0, kSidePadding, 0);
    navBar->SetGap(kPathGap);
    std::unique_ptr<DuiButton> up(new DuiButton());
    up->SetText(TR(kUpText));
    up->SetVariant(DuiButton::Variant::Ghost);
    up->SetCtrlId(kIdUp);
    m_upButton = up.get();
    navBar->AddChild(std::unique_ptr<DuiControl>(up.release()), Hint().Fixed(kToolButtonWidth));
    std::unique_ptr<DuiLabel> path(new DuiLabel());
    path->SetTextColor(kTextColor);
    path->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
    m_pathLabel = path.get();
    navBar->AddChild(std::unique_ptr<DuiControl>(path.release()), Hint().Weight(1));
    std::unique_ptr<DuiEdit> searchBox(new DuiEdit());
    searchBox->SetCtrlId(kIdSearchBox);
    searchBox->SetPlaceholder(TR(kSearchPlaceholder));
    searchBox->SetBgColor(kBgColor);
    searchBox->SetMaxLength(static_cast<int>(kShareSearchKeywordMaxChars));
    m_searchBox = searchBox.get();
    navBar->AddChild(std::unique_ptr<DuiControl>(searchBox.release()),
                     Hint().Fixed(kSearchBoxWidth).Margin(0, kSearchBoxVMargin, 0, kSearchBoxVMargin));
    std::unique_ptr<DuiButton> viewToggle(new DuiButton());
    viewToggle->SetText(TR(kViewGridText));
    viewToggle->SetVariant(DuiButton::Variant::Ghost);
    viewToggle->SetCtrlId(kIdViewToggle);
    m_viewToggle = viewToggle.get();
    navBar->AddChild(std::unique_ptr<DuiControl>(viewToggle.release()), Hint().Fixed(kToolButtonWidth));
    std::unique_ptr<DuiButton> refresh(new DuiButton());
    refresh->SetText(TR(kRefreshText));
    refresh->SetVariant(DuiButton::Variant::Ghost);
    refresh->SetCtrlId(kIdRefresh);
    m_refreshButton = refresh.get();
    navBar->AddChild(std::unique_ptr<DuiControl>(refresh.release()), Hint().Fixed(kToolButtonWidth));
    root->AddChild(std::unique_ptr<DuiControl>(navBar.release()), Hint().Fixed(kToolbarHeight));

    //操作行：下载 | 上传文件 | 上传文件夹 | 新建文件夹（后三个按权限显示）
    std::unique_ptr<DuiHBox> toolbar(new DuiHBox());
    toolbar->SetPadding(kSidePadding, 0, kSidePadding, 0);
    toolbar->SetGap(kPathGap);
    std::unique_ptr<DuiButton> download(new DuiButton());
    download->SetText(TR(kDownloadText));
    download->SetVariant(DuiButton::Variant::Ghost);
    download->SetCtrlId(kIdDownload);
    m_downloadButton = download.get();
    toolbar->AddChild(std::unique_ptr<DuiControl>(download.release()), Hint().Fixed(kToolButtonWidth));
    std::unique_ptr<DuiButton> uploadFiles(new DuiButton());
    uploadFiles->SetText(TR(kUploadFilesText));
    uploadFiles->SetVariant(DuiButton::Variant::Ghost);
    uploadFiles->SetCtrlId(kIdUploadFiles);
    uploadFiles->SetVisible(false);
    m_uploadFiles = uploadFiles.get();
    toolbar->AddChild(std::unique_ptr<DuiControl>(uploadFiles.release()), Hint().Fixed(kWideButtonWidth));
    std::unique_ptr<DuiButton> uploadFolder(new DuiButton());
    uploadFolder->SetText(TR(kUploadFolderText));
    uploadFolder->SetVariant(DuiButton::Variant::Ghost);
    uploadFolder->SetCtrlId(kIdUploadFolder);
    uploadFolder->SetVisible(false);
    m_uploadFolder = uploadFolder.get();
    toolbar->AddChild(std::unique_ptr<DuiControl>(uploadFolder.release()), Hint().Fixed(kWideButtonWidth));
    std::unique_ptr<DuiButton> newFolder(new DuiButton());
    newFolder->SetText(TR(kNewFolderText));
    newFolder->SetVariant(DuiButton::Variant::Ghost);
    newFolder->SetCtrlId(kIdNewFolder);
    newFolder->SetVisible(false);
    m_newFolder = newFolder.get();
    toolbar->AddChild(std::unique_ptr<DuiControl>(newFolder.release()), Hint().Fixed(kWideButtonWidth));
    //其余空间留白，按钮靠左
    toolbar->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    m_toolbar = toolbar.get();
    root->AddChild(std::unique_ptr<DuiControl>(toolbar.release()), Hint().Fixed(kActionBarHeight));

    //表头：名称 | 大小 | 修改时间 | 滚动条占位
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    header->SetPadding(kSidePadding, 0, kSidePadding, 0);
    std::unique_ptr<DuiButton> name(new DuiButton());
    name->SetVariant(DuiButton::Variant::Ghost);
    name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    name->SetCtrlId(kIdHeaderName);
    m_headerName = name.get();
    //各列标题按数据行的单元格内边距缩进：名称与图标左缘对齐，大小右对齐到数字的右缘，时间与时间文字的左缘对齐。
    //布局容器把外边距加在固定宽度之外，所以固定宽度要先减去外边距，格子的总宽才与数据行的列宽相同
    header->AddChild(std::unique_ptr<DuiControl>(name.release()), Hint().Weight(1).Margin(kCellPadding, 0, 0, 0));
    std::unique_ptr<DuiButton> size(new DuiButton());
    size->SetVariant(DuiButton::Variant::Ghost);
    size->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    size->SetCtrlId(kIdHeaderSize);
    m_headerSize = size.get();
    header->AddChild(std::unique_ptr<DuiControl>(size.release()),
                     Hint().Fixed(kSizeColumnWidth - kCellPadding).Margin(0, 0, kCellPadding, 0));
    std::unique_ptr<DuiButton> time(new DuiButton());
    time->SetVariant(DuiButton::Variant::Ghost);
    time->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    time->SetCtrlId(kIdHeaderTime);
    m_headerTime = time.get();
    header->AddChild(std::unique_ptr<DuiControl>(time.release()),
                     Hint().Fixed(kTimeColumnWidth - kCellPadding).Margin(kCellPadding, 0, 0, 0));
    header->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Fixed(kScrollBarWidth));
    m_header = header.get();
    root->AddChild(std::unique_ptr<DuiControl>(header.release()), Hint().Fixed(kHeaderHeight));

    //文件列表
    std::unique_ptr<CShareFileList> list(new CShareFileList());
    list->SetRowHeight(kRowHeight);
    list->SetPaintRowCallback(&CShareBrowserWindow::PaintRowThunk, this);
    //单击、双击、按键与拖动都经监听者交给本窗口，选择由本窗口的选择集维护
    list->SetListener(this);
    list->SetCtrlId(kIdFileList);
    m_list = list.get();
    root->AddChild(std::unique_ptr<DuiControl>(list.release()), Hint().Weight(1).Margin(kSidePadding, 0, kSidePadding, 0));

    //传输列表区：标题行（「传输列表」与「取消」）加列表；有过传输才显示
    std::unique_ptr<DuiVBox> panel(new DuiVBox());
    std::unique_ptr<DuiHBox> panelHeader(new DuiHBox());
    std::unique_ptr<DuiLabel> panelTitle(new DuiLabel());
    panelTitle->SetText(TR(kTransfersTitle));
    panelTitle->SetTextColor(kTextColor);
    panelTitle->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    panelHeader->AddChild(std::unique_ptr<DuiControl>(panelTitle.release()), Hint().Weight(1));
    std::unique_ptr<DuiButton> cancel(new DuiButton());
    cancel->SetText(TR(kCancelText));
    cancel->SetVariant(DuiButton::Variant::Ghost);
    cancel->SetCtrlId(kIdCancelXfer);
    panelHeader->AddChild(std::unique_ptr<DuiControl>(cancel.release()), Hint().Fixed(kToolButtonWidth));
    panel->AddChild(std::unique_ptr<DuiControl>(panelHeader.release()), Hint().Fixed(kTransferHeaderHeight));
    std::unique_ptr<DuiVirtualList> xferList(new DuiVirtualList());
    xferList->SetRowHeight(kTransferRowHeight);
    xferList->SetPaintRowCallback(&CShareBrowserWindow::PaintTransferRowThunk, this);
    m_transferList = xferList.get();
    panel->AddChild(std::unique_ptr<DuiControl>(xferList.release()), Hint().Weight(1));
    panel->SetVisible(false);
    m_transferPanel = panel.get();
    root->AddChild(std::unique_ptr<DuiControl>(panel.release()),
                   Hint().Fixed(kTransferPanelHeight).Margin(kSidePadding, 0, kSidePadding, 0));

    //状态栏
    std::unique_ptr<DuiLabel> status(new DuiLabel());
    status->SetTextColor(kWeakTextColor);
    status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    m_status = status.get();
    root->AddChild(std::unique_ptr<DuiControl>(status.release()), Hint().Fixed(kStatusHeight).Margin(kSidePadding, 0, kSidePadding, 0));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    ApplySort();
}

void CShareBrowserWindow::RequestList(const std::string& path)
{
    if (m_visitor == NULL)
        return;
    //列目录即回到目录列表：放弃正在进行的搜索
    if (m_searchMode)
    {
        m_searchMode = false;
        m_searchReqId = 0;
        m_visitor->CancelSearch();
        if (m_searchBox != NULL)
            m_searchBox->SetTextNoNotify(L"");
    }
    m_requestedPath = path;
    SetStatus(TR(kLoadingText));
    m_visitor->List(path, static_cast<int64_t>(::GetTickCount64()));
}

void CShareBrowserWindow::OnHeaderClicked(int column)
{
    if (column == m_sortColumn)
    {
        m_sortAscending = !m_sortAscending;
    }
    else
    {
        m_sortColumn = column;
        //名称默认升序；大小与修改时间默认降序（大的、新的在前）
        m_sortAscending = (column == share_sort_name);
    }
    ApplySort();
}

void CShareBrowserWindow::ApplySort()
{
    //排序会改变条目的下标：按路径保持选择
    const std::vector<std::string> selected = SelectedPaths();
    ShareBrowseModel::SortEntries(m_entries, m_sortColumn, m_sortAscending);
    RestoreSelection(selected);

    const wchar_t* mark = m_sortAscending ? kAscendingMark : kDescendingMark;
    if (m_headerName != NULL)
        m_headerName->SetText((std::wstring(TR(kHeaderNameText)) + (m_sortColumn == share_sort_name ? mark : L"")).c_str());
    if (m_headerSize != NULL)
        m_headerSize->SetText((std::wstring(TR(kHeaderSizeText)) + (m_sortColumn == share_sort_size ? mark : L"")).c_str());
    if (m_headerTime != NULL)
        m_headerTime->SetText((std::wstring(TR(kHeaderTimeText)) + (m_sortColumn == share_sort_mtime ? mark : L"")).c_str());

    RefreshListRows();
}

std::wstring CShareBrowserWindow::RootLabel() const
{
    if (m_visitor != NULL && !m_visitor->GetShareName().empty())
        return Wide(m_visitor->GetShareName());
    if (m_visitor != NULL && !m_visitor->GetRootName().empty())
        return Wide(m_visitor->GetRootName());
    return TR(kWindowTitle);
}

void CShareBrowserWindow::UpdatePathBar()
{
    if (m_pathLabel == NULL)
        return;
    std::wstring text = RootLabel();
    const std::vector<std::string> parts = ShareBrowseModel::SplitPath(m_currentPath);
    for (size_t i = 0; i < parts.size(); ++i)
        text += kPathSeparator + Wide(parts[i]);
    if (m_searchMode)
    {
        wchar_t buf[kSearchTextBufChars] = { 0 };
        _snwprintf_s(buf, kSearchTextBufChars, _TRUNCATE, TR(kSearchPathFormat), text.c_str(), m_searchKeyword.c_str());
        text = buf;
    }
    m_pathLabel->SetText(text.c_str());
    //搜索结果时「上一级」用来回到目录列表，总是可用
    if (m_upButton != NULL)
        m_upButton->SetEnabled(m_searchMode || !m_currentPath.empty());
}

void CShareBrowserWindow::SetBanner(const std::wstring& text)
{
    if (m_banner == NULL || m_bannerBox == NULL || m_root == NULL)
        return;
    const bool show = !text.empty();
    m_banner->SetText(text.c_str());
    if (m_bannerBox->IsVisible() == show)
        return;
    m_bannerBox->SetVisible(show);
    //SetVisible 不会重新排版：对横幅的父容器（客户区根容器）按当前矩形强制重排（UI 约定第 5 条）
    m_root->ForceLayout(m_root->GetRect());
    m_root->Invalidate();
}

void CShareBrowserWindow::SetStatus(const std::wstring& text)
{
    if (m_status != NULL)
        m_status->SetText(text.c_str());
}

void CShareBrowserWindow::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                        bool hover)
{
    (void)selected;
    CShareBrowserWindow* self = static_cast<CShareBrowserWindow*>(user);
    if (self != NULL)
        self->PaintRow(hdc, rowIndex, rowRect, hover);
}

void CShareBrowserWindow::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover)
{
    if (m_gridMode && m_list != NULL && rowIndex >= 0)
    {
        PaintGridRow(hdc, rowIndex, rowRect);
        return;
    }
    if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_entries.size() || m_list == NULL)
        return;
    const ShareEntry& entry = m_entries[static_cast<size_t>(rowIndex)];
    const bool selected = m_selection.Contains(rowIndex);

    //选中与悬停的底色由画行回调自己画（DuiVirtualList 只告诉状态，不画底色）；右键菜单作用于选中的条目，必须看得出来
    if (selected || hover)
    {
        HBRUSH bg = ::CreateSolidBrush(selected ? kSelectedBg : kHoverBg);
        ::FillRect(hdc, &rowRect, bg);
        ::DeleteObject(bg);
    }

    //三列的左右边界：时间列与大小列固定宽度靠右，名称列占其余。右侧始终按留出滚动条计算（按列表的矩形，
    //不按行矩形：行矩形在出现滚动条时才让出它的宽度），与表头的列对齐
    const int timeLeft = m_list->GetRect().right - kScrollBarWidth - kTimeColumnWidth;
    const int sizeLeft = timeLeft - kSizeColumnWidth;

    //类型图标：本机的系统图标，目录为文件夹图标，文件按扩展名取（搜索结果的名称是相对路径，取最后一段的扩展名）
    const int iconTop = rowRect.top + (rowRect.bottom - rowRect.top - kIconSize) / 2;
    RECT icon = { rowRect.left + kCellPadding, iconTop, rowRect.left + kCellPadding + kIconSize, iconTop + kIconSize };
    shellicon::DrawItemIcon(hdc, entry.m_isDir ? shellicon::kItemFolder : shellicon::kItemFile, Wide(entry.m_name), icon);

    HFONT oldFont = static_cast<HFONT>(::SelectObject(hdc, m_list->GetDefaultFont()));
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);

    RECT nameRect = { icon.right + kIconTextGap, rowRect.top, sizeLeft - kCellPadding, rowRect.bottom };
    ::SetTextColor(hdc, kTextColor);
    const std::wstring name = Wide(entry.m_name);
    ::DrawTextW(hdc, name.c_str(), -1, &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SetTextColor(hdc, kWeakTextColor);
    RECT sizeRect = { sizeLeft, rowRect.top, timeLeft - kCellPadding, rowRect.bottom };
    const std::wstring sizeText = ShareBrowseModel::FormatSize(entry.m_size, entry.m_isDir);
    ::DrawTextW(hdc, sizeText.c_str(), -1, &sizeRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT timeRect = { timeLeft + kCellPadding, rowRect.top, rowRect.right, rowRect.bottom };
    const std::wstring timeText = ShareBrowseModel::FormatTime(entry.m_mtime);
    ::DrawTextW(hdc, timeText.c_str(), -1, &timeRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

int CShareBrowserWindow::RowAtCursor() const
{
    POINT pt;
    ::GetCursorPos(&pt);
    ::ScreenToClient(m_hWnd, &pt);
    return EntryAt(pt);
}

int CShareBrowserWindow::EntryAt(POINT pt) const
{
    if (m_list == NULL || m_list->GetRowHeight() <= 0)
        return -1;
    const RECT rc = m_list->GetRect();
    if (!::PtInRect(&rc, pt))
        return -1;
    //行矩形从列表顶端起、按滚动位置上移
    const int row = (pt.y - rc.top + m_list->GetScrollPos()) / m_list->GetRowHeight();
    if (m_gridMode)
    {
        //缩略图视图：再按横坐标找格子；落在格子下方的纵向间距里也算不在格子上
        if ((pt.y - rc.top + m_list->GetScrollPos()) - row * m_list->GetRowHeight() >= kTileHeight)
            return -1;
        ShareGridMetrics metrics;
        metrics.m_tileWidth = kTileWidth;
        metrics.m_tileHeight = kTileHeight;
        metrics.m_gap = kTileGap;
        metrics.m_padding = kGridPadding;
        return ShareBrowseModel::GridIndexAt(row, pt.x, rc.left, m_gridColumns, m_entries.size(), metrics);
    }
    return (row >= 0 && row < static_cast<int>(m_entries.size())) ? row : -1;
}

void CShareBrowserWindow::ShowContextMenu()
{
    if (m_visitor == NULL || m_list == NULL)
        return;
    const int row = RowAtCursor();
    //右键点在未选中的条目上：只选中它；点在已选中的条目上：菜单作用于全部选中的条目
    if (row >= 0 && !m_selection.Contains(row))
        SelectEntry(row);
    const bool onRow = row >= 0;
    const bool rowIsDir = onRow && m_entries[static_cast<size_t>(row)].m_isDir;
    const bool rowIsImage = onRow && ShareThumbnail::IsImageName(m_entries[static_cast<size_t>(row)].m_name);
    const int items = ShareBrowseModel::MenuItemsForSelection(
        ShareBrowseModel::ContextMenuItems(m_perms, m_visitor->GetKind(), onRow, rowIsDir, rowIsImage),
        onRow ? m_selection.Count() : 0);

    DuiMenu menu;
    if ((items & share_menu_open) != 0)
        menu.AppendItem(kMenuOpen, TR(kMenuOpenText));
    if ((items & share_menu_preview) != 0)
        menu.AppendItem(kMenuPreview, TR(kMenuPreviewText));
    if ((items & share_menu_download) != 0)
        menu.AppendItem(kMenuDownload, TR(kMenuDownloadText));
    if ((items & (share_menu_rename | share_menu_delete | share_menu_move)) != 0)
        menu.AppendSeparator();
    if ((items & share_menu_rename) != 0)
        menu.AppendItem(kMenuRename, TR(kMenuRenameText));
    if ((items & share_menu_delete) != 0)
        menu.AppendItem(kMenuDelete, TR(kMenuDeleteText));
    if ((items & share_menu_move) != 0)
        menu.AppendItem(kMenuMove, TR(kMenuMoveText));
    if (menu.GetCount() > 0)
        menu.AppendSeparator();
    if ((items & share_menu_new_folder) != 0)
        menu.AppendItem(kMenuNewFolder, TR(kMenuNewFolderText));
    menu.AppendItem(kMenuRefresh, TR(kMenuRefreshText));

    //落点由 DuiMenu 自己限制在工作区内（UI 约定第 3 条），这里照常传光标位置
    POINT pt;
    ::GetCursorPos(&pt);
    const UINT cmd = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    switch (cmd)
    {
    //打开目录
    case kMenuOpen:
        ExecuteMenuItem(row, share_menu_open);
        break;
    //预览图片
    case kMenuPreview:
        ExecuteMenuItem(row, share_menu_preview);
        break;
    //下载
    case kMenuDownload:
        ExecuteMenuItem(row, share_menu_download);
        break;
    //重命名
    case kMenuRename:
        ExecuteMenuItem(row, share_menu_rename);
        break;
    //删除
    case kMenuDelete:
        ExecuteMenuItem(row, share_menu_delete);
        break;
    //移动到…
    case kMenuMove:
        ExecuteMenuItem(row, share_menu_move);
        break;
    //新建文件夹
    case kMenuNewFolder:
        ExecuteMenuItem(row, share_menu_new_folder);
        break;
    //刷新
    case kMenuRefresh:
        ExecuteMenuItem(row, share_menu_refresh);
        break;
    //菜单被取消（点了别处或按 Esc）
    default:
        break;
    }
}

void CShareBrowserWindow::OnRowActivated(int rowIndex)
{
    if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_entries.size())
        return;
    const ShareEntry& entry = m_entries[static_cast<size_t>(rowIndex)];
    //目录：进入（搜索结果里的目录同样进入，并回到目录列表）；图片：预览
    if (entry.m_isDir)
        RequestList(RowPath(rowIndex));
    else if (ShareThumbnail::IsImageName(entry.m_name))
        PreviewRow(rowIndex);
}

bool CShareBrowserWindow::DownloadRow(int rowIndex, const std::wstring& targetDir)
{
    return DownloadRows(std::vector<int>(1, rowIndex), targetDir);
}

bool CShareBrowserWindow::DownloadRows(const std::vector<int>& rows, const std::wstring& targetDir)
{
    if (m_service == NULL || m_visitor == NULL)
        return false;
    std::vector<ShareTransferEntry> items;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i] < 0 || static_cast<size_t>(rows[i]) >= m_entries.size())
            continue;
        const ShareEntry& entry = m_entries[static_cast<size_t>(rows[i])];
        ShareTransferEntry item;
        //单文件共享的条目是空串（共享的那个文件本身）
        item.m_path = (m_visitor->GetKind() == share_kind_file) ? std::string() : RowPath(rows[i]);
        item.m_isDir = entry.m_isDir;
        item.m_size = entry.m_size;
        items.push_back(item);
    }
    if (items.empty())
        return false;
    return StartDownloadItems(items, targetDir);
}

bool CShareBrowserWindow::StartDownloadItems(const std::vector<ShareTransferEntry>& items, const std::wstring& targetDir)
{
    if (m_service == NULL || m_visitor == NULL || items.empty())
        return false;
    //访问会话已空闲回收或失败时不再在本窗口排队（第 4 期起）：业务总控把传输排上并重新打开，就绪后发出；
    //本窗口照旧顺带刷新当前目录，状态栏在重新打开之后随列目录结果更新
    const bool reopen = (m_visitor->GetState() != share_visitor_ready);
    //一次传输只能包含同一目录下的条目：按所在目录分组
    std::vector<std::string> paths;
    for (size_t i = 0; i < items.size(); ++i)
        paths.push_back(items[i].m_path);
    const std::vector<std::vector<size_t> > groups = ShareBrowseModel::GroupByParent(paths);
    bool started = false;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        std::vector<ShareTransferEntry> part;
        for (size_t k = 0; k < groups[g].size(); ++k)
            part.push_back(items[groups[g][k]]);
        ShareTransferOptions options;
        options.m_label = TransferDisplayName(part);
        const uint32_t id = m_service->StartDownload(m_shareId, part, targetDir, this, options);
        if (id == 0)
        {
            SetBanner(ShareBrowseModel::ErrorText(error_code_share_relay_broken));
            continue;
        }
        AddTransferRow(id, share_transfer_download, options.m_label);
        started = true;
    }
    if (reopen && started)
        RequestList(m_currentPath);
    return started;
}

std::wstring CShareBrowserWindow::TransferDisplayName(const std::vector<ShareTransferEntry>& items) const
{
    if (items.empty())
        return std::wstring();
    //单文件共享的条目路径为空串：显示共享根名（就是那个文件的名字）
    std::wstring name;
    if (items[0].m_path.empty())
    {
        name = (m_visitor != NULL) ? Wide(m_visitor->GetRootName()) : std::wstring();
    }
    else
    {
        const std::vector<std::string> parts = ShareBrowseModel::SplitPath(items[0].m_path);
        name = Wide(parts.empty() ? items[0].m_path : parts.back());
    }
    if (items.size() == 1)
        return name;
    wchar_t buf[MAX_PATH] = { 0 };
    _snwprintf_s(buf, MAX_PATH, _TRUNCATE, TR(kXferMoreFormat), name.c_str(), static_cast<unsigned>(items.size()));
    return buf;
}

bool CShareBrowserWindow::UploadItems(const std::vector<std::wstring>& localItems)
{
    if (m_service == NULL || m_visitor == NULL || localItems.empty())
        return false;
    //访问会话未就绪时由业务总控排队并重新打开（第 4 期起），本窗口顺带刷新当前目录
    const bool reopen = (m_visitor->GetState() != share_visitor_ready);
    int errCode = 0;
    ShareTransferOptions options;
    options.m_label = UploadDisplayName(localItems);
    const uint32_t id = m_service->StartUpload(m_shareId, localItems, m_currentPath, this, errCode, options);
    LOG_INFO("share browser upload started, shareid: %lld, items: %u, local id: %u.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(localItems.size()), id);
    if (id == 0)
    {
        m_errorText = ShareBrowseModel::ErrorText(errCode);
        SetBanner(m_errorText);
        return false;
    }
    AddTransferRow(id, share_transfer_upload, options.m_label);
    if (reopen)
        RequestList(m_currentPath);
    return true;
}

std::wstring CShareBrowserWindow::UploadDisplayName(const std::vector<std::wstring>& localItems) const
{
    if (localItems.empty())
        return std::wstring();
    const std::wstring& first = localItems[0];
    const size_t slash = first.find_last_of(L'\\');
    std::wstring name = (slash == std::wstring::npos) ? first : first.substr(slash + 1);
    if (localItems.size() > 1)
    {
        wchar_t buf[MAX_PATH] = { 0 };
        _snwprintf_s(buf, MAX_PATH, _TRUNCATE, TR(kXferMoreFormat), name.c_str(), static_cast<unsigned>(localItems.size()));
        name = buf;
    }
    return name;
}

void CShareBrowserWindow::AdoptBackgroundTransfers()
{
    if (m_service == NULL)
        return;
    const std::vector<ShareTransferSnapshot> running = m_service->AdoptVisitorTransfers(m_shareId, this);
    for (size_t i = 0; i < running.size(); ++i)
    {
        AddTransferRow(running[i].m_localId, running[i].m_direction, running[i].m_label);
        TransferRow& row = m_transfers.back();
        row.m_bytesDone = running[i].m_bytesDone;
        row.m_bytesTotal = running[i].m_bytesTotal;
    }
    if (!running.empty() && m_transferList != NULL)
        m_transferList->Invalidate();
}

bool CShareBrowserWindow::RetryTransfer(const ShareVisitorTransferResult& result)
{
    if (m_service == NULL || m_visitor == NULL)
        return false;
    LOG_INFO("share browser retries a transfer, shareid: %lld, direction: %d, old local id: %u.",
             static_cast<long long>(m_shareId), result.m_direction, result.m_localId);
    ShareTransferOptions options;
    options.m_label = result.m_label;
    if (result.m_direction == share_transfer_download)
    {
        if (result.m_downloadItems.empty())
            return false;
        //原来就是同一目录下的一组条目：原样再请求一次，续传记录还在，从断点接着传
        const uint32_t id = m_service->StartDownload(m_shareId, result.m_downloadItems, result.m_targetDir, this, options);
        if (id == 0)
        {
            SetBanner(ShareBrowseModel::ErrorText(error_code_share_relay_broken));
            return false;
        }
        AddTransferRow(id, share_transfer_download, options.m_label);
        return true;
    }
    if (result.m_uploadItems.empty())
        return false;
    int errCode = 0;
    const uint32_t id = m_service->StartUpload(m_shareId, result.m_uploadItems, result.m_toDir, this, errCode, options);
    if (id == 0)
    {
        m_errorText = ShareBrowseModel::ErrorText(errCode);
        SetBanner(m_errorText);
        return false;
    }
    AddTransferRow(id, share_transfer_upload, options.m_label);
    return true;
}

void CShareBrowserWindow::BrowseTo(const std::string& dir)
{
    if (m_visitor == NULL)
        return;
    //列目录即回到目录列表（正在显示搜索结果时一并放弃搜索，见 RequestList）
    LOG_INFO("share browser browses to a dir from a notice, shareid: %lld.", static_cast<long long>(m_shareId));
    RequestList(dir);
}

size_t CShareBrowserWindow::GetTransferRowCount() const
{
    return m_transfers.size();
}

int CShareBrowserWindow::GetTransferRowState(size_t index) const
{
    return (index < m_transfers.size()) ? m_transfers[index].m_state : -1;
}

void CShareBrowserWindow::OnTransferProgress(uint32_t localId, int64_t bytesDone, int64_t bytesTotal, int64_t filesDone,
                                             int64_t filesTotal)
{
    (void)filesDone;
    (void)filesTotal;
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        if (m_transfers[i].m_localId != localId)
            continue;
        m_transfers[i].m_bytesDone = bytesDone;
        m_transfers[i].m_bytesTotal = bytesTotal;
        if (m_transferList != NULL)
            m_transferList->Invalidate();
        return;
    }
}

void CShareBrowserWindow::OnTransferFinished(uint32_t localId, int code)
{
    //拖出的下载：拖出任务的结束已由业务总控经结束回调完成（第 4 期起），这里只去掉计数
    m_dragByTransfer.erase(localId);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        if (m_transfers[i].m_localId != localId)
            continue;
        m_transfers[i].m_state = (code == 0) ? kXferDoneState : kXferFailed;
        m_transfers[i].m_code = code;
        if (code == 0)
            m_transfers[i].m_bytesDone = m_transfers[i].m_bytesTotal;
        if (m_transferList != NULL)
            m_transferList->Invalidate();
        LOG_INFO("share browser transfer finished, shareid: %lld, local id: %u, code: %d.", static_cast<long long>(m_shareId),
                 localId, code);
        //上传成功：刷新当前目录，看到新传上去的条目
        if (code == 0 && m_transfers[i].m_direction == share_transfer_upload)
            RequestList(m_currentPath);
        return;
    }
}

void CShareBrowserWindow::OnDownloadClicked()
{
    std::vector<int> rows = m_selection.Sorted();
    //单文件共享：列表里只有那个文件，不选也下载它
    if (m_visitor != NULL && m_visitor->GetKind() == share_kind_file && !m_entries.empty())
        rows.assign(1, 0);
    if (rows.empty())
    {
        SetStatus(TR(kNoSelectionText));
        return;
    }
    std::vector<std::wstring> picked;
    if (!SharePickItems(m_hWnd, true, false, true, TR(kPickFolderTitle), picked))
        return;
    DownloadRows(rows, picked[0]);
}

void CShareBrowserWindow::OnUploadClicked(bool folders)
{
    std::vector<std::wstring> picked;
    if (!SharePickItems(m_hWnd, folders, !folders, false, folders ? TR(kPickUploadDirTitle) : TR(kPickUploadTitle), picked))
        return;
    UploadItems(picked);
}

void CShareBrowserWindow::OnCancelTransferClicked()
{
    if (m_transferList == NULL || m_service == NULL)
        return;
    const int row = m_transferList->GetCurSel();
    if (row < 0 || static_cast<size_t>(row) >= m_transfers.size() || m_transfers[static_cast<size_t>(row)].m_state != kXferRunning)
        return;
    m_service->CancelTransfer(m_transfers[static_cast<size_t>(row)].m_localId);
}

void CShareBrowserWindow::UpdateUploadButtons()
{
    if (m_uploadFiles == NULL || m_uploadFolder == NULL || m_newFolder == NULL || m_toolbar == NULL)
        return;
    const int kind = (m_visitor != NULL) ? m_visitor->GetKind() : 0;
    const bool canUpload = (m_perms & share_perm_upload) != 0 && kind != share_kind_file && kind != 0;
    const bool canMkdir = kind != 0
                          && (ShareBrowseModel::ContextMenuItems(m_perms, kind, false, false, false) & share_menu_new_folder) != 0;
    if (m_uploadFiles->IsVisible() == canUpload && m_newFolder->IsVisible() == canMkdir)
        return;
    m_uploadFiles->SetVisible(canUpload);
    m_uploadFolder->SetVisible(canUpload);
    m_newFolder->SetVisible(canMkdir);
    //SetVisible 不会重新排版：对按钮所在的工具栏强制重排（UI 约定第 5 条）
    m_toolbar->ForceLayout(m_toolbar->GetRect());
    m_toolbar->Invalidate();
}

void CShareBrowserWindow::AddTransferRow(uint32_t localId, int direction, const std::wstring& name)
{
    TransferRow row;
    row.m_localId = localId;
    row.m_direction = direction;
    row.m_name = name;
    m_transfers.push_back(row);
    if (m_transferList != NULL)
    {
        m_transferList->SetRowCount(static_cast<int>(m_transfers.size()));
        m_transferList->EnsureVisible(static_cast<int>(m_transfers.size()) - 1);
    }
    if (m_transferPanel != NULL && !m_transferPanel->IsVisible())
    {
        m_transferPanel->SetVisible(true);
        //传输列表区显示之后对客户区根容器强制重排（UI 约定第 5 条）
        m_root->ForceLayout(m_root->GetRect());
        m_root->Invalidate();
    }
    else if (m_transferList != NULL)
    {
        m_transferList->ForceLayout(m_transferList->GetRect());
        m_transferList->Invalidate();
    }
}

void CShareBrowserWindow::PaintTransferRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                                bool hover)
{
    (void)selected;
    (void)hover;
    CShareBrowserWindow* self = static_cast<CShareBrowserWindow*>(user);
    if (self != NULL)
        self->PaintTransferRow(hdc, rowIndex, rowRect);
}

void CShareBrowserWindow::PaintTransferRow(HDC hdc, int rowIndex, const RECT& rowRect)
{
    if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_transfers.size() || m_transferList == NULL)
        return;
    const TransferRow& row = m_transfers[static_cast<size_t>(rowIndex)];

    HFONT oldFont = static_cast<HFONT>(::SelectObject(hdc, m_transferList->GetDefaultFont()));
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);

    //第一行：方向与名称（左），状态（右）
    RECT text = { rowRect.left + kCellPadding, rowRect.top, rowRect.right - kCellPadding, rowRect.top + kTransferTextHeight };
    const std::wstring title = std::wstring(row.m_direction == share_transfer_upload ? TR(kDirUpload) : TR(kDirDownload))
                               + L"  " + row.m_name;
    ::SetTextColor(hdc, kTextColor);
    ::DrawTextW(hdc, title.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    std::wstring status;
    COLORREF statusColor = kWeakTextColor;
    if (row.m_state == kXferDoneState)
    {
        status = TR(kXferDone);
        statusColor = kDoneColor;
    }
    else if (row.m_state == kXferFailed)
    {
        status = (row.m_code == error_code_share_transfer_canceled) ? std::wstring(TR(kXferCanceled))
                                                                    : std::wstring(TR(kXferFailPrefix)) + ShareBrowseModel::ErrorText(row.m_code);
        statusColor = (row.m_code == error_code_share_transfer_canceled) ? kWeakTextColor : kFailColor;
    }
    else if (row.m_bytesTotal <= 0)
    {
        //同一共享上前面的传输还没结束时排队（服务端同一访问会话同时只接受一笔传输）
        const bool queued = m_service != NULL && m_service->IsTransferQueued(row.m_localId);
        status = queued ? TR(kXferQueued) : TR(kXferConnecting);
    }
    else
    {
        wchar_t buf[kStatusBufChars] = { 0 };
        const int percent = static_cast<int>(row.m_bytesDone * kPercentScale / row.m_bytesTotal);
        _snwprintf_s(buf, kStatusBufChars, _TRUNCATE, L"%d%%  %s / %s", percent,
                     ShareBrowseModel::FormatSize(row.m_bytesDone, false).c_str(),
                     ShareBrowseModel::FormatSize(row.m_bytesTotal, false).c_str());
        status = buf;
    }
    ::SetTextColor(hdc, statusColor);
    ::DrawTextW(hdc, status.c_str(), -1, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    //「P2P」徽章：正在传、且数据走的是直连链路时，画在状态文字左边（方案决定 4：中转不画）
    const bool transferring = (row.m_state != kXferDoneState && row.m_state != kXferFailed && row.m_bytesTotal > 0);
    if (transferring && m_service != NULL && balloonmain::ShouldShowP2PBadge(m_service->TransferLinkPath(row.m_localId)))
    {
        HFONT listFont = m_transferList->GetDefaultFont();
        SIZE statusSize = { 0, 0 };
        ::GetTextExtentPoint32W(hdc, status.c_str(), static_cast<int>(status.size()), &statusSize);
        const SIZE badgeSize = balloonmain::MeasureP2PBadge(hdc, listFont);
        const int badgeRight = text.right - statusSize.cx - kCellPadding;
        const int badgeTop = text.top + ((text.bottom - text.top) - badgeSize.cy) / 2;
        const RECT badgeRc = { badgeRight - badgeSize.cx, badgeTop, badgeRight, badgeTop + badgeSize.cy };
        if (badgeRc.left > text.left)
            balloonmain::DrawP2PBadge(hdc, badgeRc, listFont);
    }

    //第二行：进度条
    RECT track = { rowRect.left + kCellPadding, rowRect.bottom - kProgressBarBottom - kProgressBarHeight,
                   rowRect.right - kCellPadding, rowRect.bottom - kProgressBarBottom };
    HBRUSH trackBrush = ::CreateSolidBrush(kTrackColor);
    ::FillRect(hdc, &track, trackBrush);
    ::DeleteObject(trackBrush);
    const int64_t done = (row.m_state == kXferDoneState) ? 1 : row.m_bytesDone;
    const int64_t total = (row.m_state == kXferDoneState) ? 1 : row.m_bytesTotal;
    if (total > 0 && done > 0)
    {
        RECT fill = track;
        fill.right = track.left + static_cast<LONG>((track.right - track.left) * done / total);
        HBRUSH barBrush = ::CreateSolidBrush(row.m_state == kXferFailed ? kTrackColor : kBarColor);
        ::FillRect(hdc, &fill, barBrush);
        ::DeleteObject(barBrush);
    }

    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

void CShareBrowserWindow::OnFileListMouseDown(POINT pt, UINT mkFlags)
{
    const int index = EntryAt(pt);
    const bool ctrl = (mkFlags & MK_CONTROL) != 0;
    const bool shift = (mkFlags & MK_SHIFT) != 0;
    m_pressIndex = index;
    m_deferredSelect = -1;
    if (index < 0)
    {
        //点在空白处：不按 Ctrl / Shift 时清空选择
        if (!ctrl && !shift)
            m_selection.Clear();
    }
    else if (!ctrl && !shift && m_selection.Contains(index) && m_selection.Count() > 1)
    {
        //按在已选中的多项之一上：可能要整组拖出，单选推迟到松开时
        m_deferredSelect = index;
    }
    else
    {
        m_selection.Click(index, ctrl, shift);
    }
    if (m_list != NULL)
        m_list->Invalidate();
    //按在选中的条目上：先把选中目录的整棵目录树要来，拖出时要用
    if (index >= 0 && m_selection.Contains(index))
        PrefetchDragTrees();
}

void CShareBrowserWindow::OnFileListMouseUp(POINT pt, UINT mkFlags)
{
    (void)pt;
    (void)mkFlags;
    //没有拖动：推迟的单选在松开时生效
    if (m_deferredSelect >= 0)
    {
        SelectEntry(m_deferredSelect);
        m_deferredSelect = -1;
    }
}

void CShareBrowserWindow::OnFileListDragStart(POINT pressPt)
{
    (void)pressPt;
    m_deferredSelect = -1;
    //只有按在选中的条目上才拖出；按在空白处拖动不做框选
    if (m_pressIndex < 0 || !m_selection.Contains(m_pressIndex))
        return;
    StartDragOut();
}

void CShareBrowserWindow::OnFileListDoubleClick(POINT pt)
{
    const int index = EntryAt(pt);
    if (index < 0)
        return;
    SelectEntry(index);
    OnRowActivated(index);
}

bool CShareBrowserWindow::OnFileListKeyDown(UINT vk)
{
    const int count = static_cast<int>(m_entries.size());
    if (count == 0 || m_list == NULL)
        return false;
    const bool ctrl = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & kKeyDownMask) != 0;
    const int current = SelectedEntry();
    //上下移动一次跨过的条目数：列表视图一行一个，缩略图视图一行若干格
    const int step = (m_gridMode && m_gridColumns > 0) ? m_gridColumns : 1;
    const int rowHeight = m_list->GetRowHeight();
    const RECT rc = m_list->GetRect();
    const int pageRows = (rowHeight > 0 && rc.bottom - rc.top > rowHeight) ? (rc.bottom - rc.top) / rowHeight : 1;
    int next = current;
    switch (vk)
    {
    //Ctrl + A：全选
    case 'A':
        if (!ctrl)
            return false;
        SelectEntry(0);
        m_selection.Click(count - 1, false, true);
        m_list->Invalidate();
        return true;

    //Enter：打开选中的目录或预览选中的图片（只选中一项时）
    case VK_RETURN:
        if (current >= 0 && m_selection.Count() == 1)
            OnRowActivated(current);
        return true;

    //Delete：删除选中的条目（有删除权限时，先确认）
    case VK_DELETE:
        if (current >= 0 && m_visitor != NULL
            && (ShareBrowseModel::ContextMenuItems(m_perms, m_visitor->GetKind(), true, false, false) & share_menu_delete) != 0)
            ExecuteMenuItem(current, share_menu_delete);
        return true;

    //上、下：移到上一行、下一行（缩略图视图跨过一整行的格子）
    case VK_UP:
        next = (current < 0) ? 0 : current - step;
        break;
    case VK_DOWN:
        next = (current < 0) ? 0 : current + step;
        break;

    //左、右：只在缩略图视图里移到前一格、后一格
    case VK_LEFT:
        if (!m_gridMode)
            return false;
        next = (current < 0) ? 0 : current - 1;
        break;
    case VK_RIGHT:
        if (!m_gridMode)
            return false;
        next = (current < 0) ? 0 : current + 1;
        break;

    //Page Up、Page Down：翻一屏
    case VK_PRIOR:
        next = (current < 0) ? 0 : current - pageRows * step;
        break;
    case VK_NEXT:
        next = (current < 0) ? 0 : current + pageRows * step;
        break;

    //Home、End：第一项、最后一项
    case VK_HOME:
        next = 0;
        break;
    case VK_END:
        next = count - 1;
        break;

    //其余按键交给虚拟列表的默认处理
    default:
        return false;
    }
    if (next < 0)
        next = 0;
    if (next > count - 1)
        next = count - 1;
    //按着 Shift：从起点连选到新位置；否则只选新位置
    m_selection.Click(next, false, shift);
    m_list->EnsureVisible((m_gridMode && m_gridColumns > 0) ? next / m_gridColumns : next);
    m_list->Invalidate();
    return true;
}

void CShareBrowserWindow::PrefetchDragTrees()
{
    if (m_visitor == NULL || m_visitor->GetKind() == share_kind_file)
        return;
    const std::vector<int> rows = m_selection.Sorted();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!m_entries[static_cast<size_t>(rows[i])].m_isDir)
            continue;
        const std::string path = RowPath(rows[i]);
        //已有、已失败、正在要、已排队的都不再要
        if (m_dragTrees.find(path) != m_dragTrees.end() || m_dragTreeErrors.find(path) != m_dragTreeErrors.end()
            || path == m_treePath)
            continue;
        bool queued = false;
        for (size_t k = 0; k < m_treeQueue.size() && !queued; ++k)
            queued = (m_treeQueue[k] == path);
        if (!queued)
            m_treeQueue.push_back(path);
    }
    if (m_treeReqId == 0)
        RequestNextDragTree();
}

void CShareBrowserWindow::RequestNextDragTree()
{
    m_treePath.clear();
    m_treeReqId = 0;
    if (m_visitor == NULL || m_treeQueue.empty())
        return;
    m_treePath = m_treeQueue.front();
    m_treeQueue.erase(m_treeQueue.begin());
    m_treeReqId = m_visitor->ListTree(m_treePath, 0, static_cast<int64_t>(::GetTickCount64()));
}

void CShareBrowserWindow::OnVisitorTreeResult(int64_t reqId, const std::string& path, const std::vector<ShareEntry>& entries,
                                              bool truncated, int code)
{
    if (reqId != m_treeReqId || path != m_treePath)
        return;
    if (code != 0)
        m_dragTreeErrors[path] = code;
    else if (truncated)
        m_dragTreeErrors[path] = kDragTreeTooMany;
    else
        m_dragTrees[path] = entries;
    LOG_INFO("share browser drag tree, shareid: %lld, entries: %u, truncated: %d, code: %d.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(entries.size()), truncated ? 1 : 0, code);
    RequestNextDragTree();
}

IDataObject* CShareBrowserWindow::CreateDragOutData(bool interactive, int& result)
{
    result = share_drag_build_empty;
    if (m_visitor == NULL || m_service == NULL || !IsWindow() || m_selection.Count() == 0)
        return NULL;
    const bool singleFile = (m_visitor->GetKind() == share_kind_file);

    //选中的目录还没有目录树：要来，等待期间处理消息（下载事件、目录树结果照常处理）
    PrefetchDragTrees();
    if (m_treeReqId != 0)
    {
        const std::wstring oldStatus = GetStatusText();
        SetStatus(TR(kDragWaitText));
        const ULONGLONG start = ::GetTickCount64();
        while (m_treeReqId != 0 && ::GetTickCount64() - start < kDragTreeWaitMs)
        {
            //用户已经松开鼠标：放弃这次拖出
            if (interactive && (::GetKeyState(VK_LBUTTON) & kKeyDownMask) == 0)
                break;
            MSG msg;
            while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            ::Sleep(kDragTreeWaitSliceMs);
        }
        //等待期间窗口可能被关闭
        if (m_visitor == NULL || !IsWindow())
            return NULL;
        SetStatus(oldStatus);
    }

    //各顶层条目与目录树
    std::vector<ShareDragSource> sources;
    const std::vector<int> rows = m_selection.Sorted();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        ShareDragSource src;
        //单文件共享：条目路径是空串，落地名字用文件名
        src.m_sharePath = singleFile ? m_entries[static_cast<size_t>(rows[i])].m_name : RowPath(rows[i]);
        src.m_entry = m_entries[static_cast<size_t>(rows[i])];
        if (src.m_entry.m_isDir)
        {
            std::map<std::string, int>::const_iterator err = m_dragTreeErrors.find(src.m_sharePath);
            if (err != m_dragTreeErrors.end())
            {
                result = (err->second == kDragTreeTooMany) ? static_cast<int>(share_drag_build_too_many)
                                                           : static_cast<int>(share_drag_build_missing_tree);
                SetStatus(err->second == kDragTreeTooMany ? ShareDragModel::RefusalText(result)
                                                           : ShareBrowseModel::ErrorText(err->second));
                return NULL;
            }
        }
        sources.push_back(src);
    }
    std::vector<ShareDragItem> items;
    result = ShareDragModel::Build(sources, m_dragTrees, items);
    if (result != share_drag_build_ok)
    {
        SetStatus(ShareDragModel::RefusalText(result));
        LOG_INFO("share browser drag-out refused, shareid: %lld, result: %d.", static_cast<long long>(m_shareId), result);
        return NULL;
    }
    //单文件共享：下载时条目路径要用空串
    if (singleFile)
    {
        for (size_t i = 0; i < items.size(); ++i)
            items[i].m_sharePath.clear();
    }
    const std::wstring tempDir = m_service->NewDragTempDir();
    if (tempDir.empty())
    {
        SetStatus(TR(kDragTempFailText));
        return NULL;
    }
    std::shared_ptr<CShareDragJob> job(new CShareDragJob(tempDir, items));
    LOG_INFO("share browser drag-out prepared, shareid: %lld, items: %u.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(items.size()));
    return CreateShareDragDataObject(job, this);
}

void CShareBrowserWindow::StartDragOut()
{
    int result = 0;
    IDataObject* data = CreateDragOutData(true, result);
    if (data == NULL)
        return;
    IDropSource* source = CreateShareDropSource();
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr = ::DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
    source->Release();
    data->Release();
    LOG_INFO("share browser drag-out ended, shareid: %lld, hr: 0x%08lx, effect: %lu.", static_cast<long long>(m_shareId),
             static_cast<unsigned long>(hr), static_cast<unsigned long>(effect));
}

bool CShareBrowserWindow::OnDragDownloadStart(const std::shared_ptr<CShareDragJob>& job)
{
    if (m_service == NULL || m_visitor == NULL || !job)
        return false;
    const int mk = ::SHCreateDirectoryExW(NULL, job->GetTempDir().c_str(), NULL);
    if (mk != ERROR_SUCCESS && mk != ERROR_ALREADY_EXISTS && mk != ERROR_FILE_EXISTS)
    {
        LOG_ERROR(L"share browser: cannot create drag temp dir %s, error %d.", job->GetTempDir().c_str(), mk);
        return false;
    }
    SetStatus(TR(kDragStartedText));
    //访问会话已空闲回收时由业务总控排队并重新打开，就绪后再下载（流的读取一直等着，第 4 期起）；本窗口顺带刷新当前目录
    const bool reopen = (m_visitor->GetState() != share_visitor_ready);
    const bool started = StartDragDownloads(job);
    if (reopen && started)
        RequestList(m_currentPath);
    return started;
}

bool CShareBrowserWindow::StartDragDownloads(const std::shared_ptr<CShareDragJob>& job)
{
    if (m_service == NULL || m_visitor == NULL)
        return false;
    //顶层条目：落地名字里没有反斜杠的那些；目录的字节数为其下全部文件之和（进度条用）
    const std::vector<ShareDragItem>& items = job->GetItems();
    std::vector<ShareTransferEntry> tops;
    std::vector<std::wstring> topNames;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_dropName.find(L'\\') != std::wstring::npos)
            continue;
        ShareTransferEntry entry;
        entry.m_path = items[i].m_sharePath;
        entry.m_isDir = items[i].m_isDir;
        entry.m_size = items[i].m_size;
        tops.push_back(entry);
        topNames.push_back(items[i].m_dropName + L"\\");
    }
    for (size_t i = 0; i < items.size(); ++i)
    {
        for (size_t t = 0; t < tops.size(); ++t)
        {
            if (tops[t].m_isDir && !items[i].m_isDir && items[i].m_dropName.compare(0, topNames[t].size(), topNames[t]) == 0)
                tops[t].m_size += items[i].m_size;
        }
    }
    //一次传输只能包含同一目录下的条目（搜索结果里选中的可能来自不同目录）：按所在目录分组
    std::vector<std::string> paths;
    for (size_t i = 0; i < tops.size(); ++i)
        paths.push_back(tops[i].m_path);
    const std::vector<std::vector<size_t> > groups = ShareBrowseModel::GroupByParent(paths);
    if (groups.empty())
        return false;
    //每一组一笔传输，共用一个下载进度作为结束回调：全部结束（含没能发起的）后才标记拖出任务结束
    std::shared_ptr<DragDownload> dl(new DragDownload(job, groups.size()));
    std::vector<uint32_t> ids;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        std::vector<ShareTransferEntry> part;
        for (size_t k = 0; k < groups[g].size(); ++k)
            part.push_back(tops[groups[g][k]]);
        ShareTransferOptions options;
        options.m_label = TransferDisplayName(part);
        options.m_dragOut = true;
        options.m_completion = dl;
        const uint32_t id = m_service->StartDownload(m_shareId, part, job->GetTempDir(), this, options);
        if (id == 0)
        {
            //没能发起：这一组按失败计入，其余组照常进行
            dl->OnTransferDone(error_code_share_relay_broken);
            continue;
        }
        ids.push_back(id);
        AddTransferRow(id, share_transfer_download, options.m_label);
    }
    if (ids.empty())
        return false;
    for (size_t i = 0; i < ids.size(); ++i)
        m_dragByTransfer[ids[i]] = dl;
    LOG_INFO("share browser drag-out download started, shareid: %lld, transfers: %u, items: %u.",
             static_cast<long long>(m_shareId), static_cast<unsigned>(ids.size()), static_cast<unsigned>(items.size()));
    return true;
}

CShareBrowserWindow::DragDownload::DragDownload(const std::shared_ptr<CShareDragJob>& job, size_t transfers)
    : m_job(job)
    , m_remaining(transfers)
    , m_code(0)
{
}

void CShareBrowserWindow::DragDownload::OnTransferDone(int code)
{
    if (code != 0 && m_code == 0)
        m_code = code;
    if (m_remaining > 0)
        --m_remaining;
    if (m_remaining != 0 || !m_job)
        return;
    //同一拖出任务的传输全部结束：标记任务结束，流的读取随即继续或失败（MarkFinished 只认第一次）
    m_job->MarkFinished(m_code);
    LOG_INFO("share drag-out download finished, code: %d.", m_code);
}

size_t CShareBrowserWindow::GetDragTransferCount() const
{
    return m_dragByTransfer.size();
}

bool CShareBrowserWindow::CanUploadHere() const
{
    if (m_visitor == NULL || m_searchMode)
        return false;
    const int kind = m_visitor->GetKind();
    return (m_perms & share_perm_upload) != 0 && kind != share_kind_file && kind != 0;
}

bool CShareBrowserWindow::UploadDroppedItems(const std::vector<std::wstring>& localItems)
{
    if (localItems.empty() || !CanUploadHere())
        return false;
    //一次上传只能包含同一目录下的本机条目：按所在目录分组（访问会话未就绪时各组由业务总控排队，第 4 期起）
    const std::vector<std::vector<std::wstring> > groups = ShareBrowseModel::GroupLocalByParent(localItems);
    bool started = false;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        if (UploadItems(groups[g]))
            started = true;
    }
    return started;
}

IDropTarget* CShareBrowserWindow::GetUploadDropTarget()
{
    return m_dropHelper ? m_dropHelper->GetDropTarget() : NULL;
}

bool CShareBrowserWindow::OnUploadDragEnter(const POINT& pt, bool hasFiles)
{
    (void)pt;
    //只收本机的文件与文件夹（本窗口自己拖出的虚拟文件不带文件列表，不会进到这里）
    if (!hasFiles || !CanUploadHere())
        return false;
    if (!m_dropHintShown)
    {
        m_statusBeforeDrop = GetStatusText();
        m_dropHintShown = true;
    }
    SetStatus(TR(kDropHintText));
    return true;
}

void CShareBrowserWindow::OnUploadDragLeave()
{
    if (!m_dropHintShown)
        return;
    m_dropHintShown = false;
    SetStatus(m_statusBeforeDrop);
    m_statusBeforeDrop.clear();
}

void CShareBrowserWindow::OnUploadFilesDropped(const std::vector<CString>& paths)
{
    std::vector<std::wstring> items;
    for (size_t i = 0; i < paths.size(); ++i)
        items.push_back(std::wstring(paths[i].GetString()));
    LOG_INFO("share browser files dropped for upload, shareid: %lld, items: %u.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(items.size()));
    UploadDroppedItems(items);
}

} // namespace fileshare
