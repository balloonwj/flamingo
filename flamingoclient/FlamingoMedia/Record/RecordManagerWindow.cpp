/**
 *  录制管理窗口，见 RecordManagerWindow.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "RecordManagerWindow.h"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string.h>
#include "CloudRecordManagerModel.h"   // 云端页的状态、副文字、可用操作与用量
#include "../../Source/net/Protocol.h"   // cloudrecord_status：没有录到内容的行画缺失样式
#include "RecordManagerLayout.h"
#include "RecordManagerList.h"
#include "RecordManagerModel.h"
#include "RecordPolicy.h"   // RecordFileNameOf
#include "../Caption/MediaCaptionBridge.h"   // 语音字幕：可用状态、取字幕模块（字幕状态、编辑器、改名与删除草稿）
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Source/Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：「下载…」保存对话框的过滤串
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTab.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"

namespace screenrecord {

namespace {

//窗口标题与各提示框的标题
const wchar_t* const kWindowTitle = TR_NOOP(L"录制管理");
//行内文字相对列表左边的起点（像素）：与 DuiListBox 绘制时的左边距（6）加图标、图标与文字的间距（8）一致，
//原地编辑的输入框盖在文字上
const int kRowTextLeftPad = 6;
const int kRowIconTextGap = 8;
//行图标的颜色：文件还在时为蓝灰，文件已不存在时为浅灰
const BYTE kIconAlphaOpaque = 255;
const COLORREF kIconPresentColor = RGB(70, 110, 170);
const COLORREF kIconMissingColor = RGB(184, 184, 190);
//行图标的线宽（像素）
const float kIconStrokePx = 1.5f;
//32 位位图每像素的字节数、位数
const int kBytesPerPixel = 4;
const int kBitsPerPixel = 32;
//本机页右键菜单「上传到云端」的菜单项 id：取在 RecordRowAction 的各位之外
const UINT kLocalMenuUploadCloud = 0x100;
//云端页用量文字的颜色：平时为灰，没有开通、配额已满时为橙
const COLORREF kUsageTextColor = RGB(110, 110, 116);
const COLORREF kUsageWarnColor = RGB(217, 130, 43);
//「下载…」默认文件名里要替换掉的字符（Windows 文件名不允许的字符）与替换成的字符
const wchar_t* const kBadFileNameChars = L"\\/:*?\"<>|";
const wchar_t kFileNameReplacement = L'_';

std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0)
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

//当前时间（Unix 毫秒）
int64_t nowUnixMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    return RecordFileTimeToUnixMs(ft);
}

//云端录像显示用的标题：服务端的标题为空时写「云端录像」
std::wstring cloudTitleOf(const CloudRecordInfo& info)
{
    const std::wstring title = utf8ToWide(info.m_title);
    return title.empty() ? std::wstring(TR(L"云端录像")) : title;
}

//录像文件是否还在（是普通文件而不是目录）
bool recordFileExists(const std::wstring& path)
{
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/**
 *  画一个行图标：录像为方框表示画面、中间一个实心播放三角；录音（第 24 期）为话筒（圆角话筒头、托架弧线、立杆与底座）。
 *  文件已不存在时整体浅灰并加一道斜线。坐标按 kRecordMgrIconPx（20）像素的格子写成。
 *  返回 32 位预乘 alpha 位图，调用方负责 DeleteObject；失败时返回 NULL。
 *  @param missing 文件是否已不存在
 *  @param audio   是否是只录声音的录音
 */
HBITMAP makeRowIcon(bool missing, bool audio)
{
    const int side = kRecordMgrIconPx;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = side;
    bi.bmiHeader.biHeight = -side;   //自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
        return bmp;
    memset(bits, 0, side * side * kBytesPerPixel);
    {
        //GDI+ 直接在 DIB 的像素上按预乘 alpha 绘制
        Gdiplus::Bitmap canvas(side, side, side * kBytesPerPixel, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const COLORREF c = missing ? kIconMissingColor : kIconPresentColor;
        const Gdiplus::Color color(kIconAlphaOpaque, GetRValue(c), GetGValue(c), GetBValue(c));
        Gdiplus::Pen pen(color, kIconStrokePx);
        Gdiplus::SolidBrush fill(color);
        if (audio)
        {
            //话筒头：竖向的实心圆角矩形，用上下两个半圆加中间矩形拼成
            g.FillEllipse(&fill, 7.0f, 2.0f, 6.0f, 6.0f);           //上半圆
            g.FillRectangle(&fill, 7.0f, 5.0f, 6.0f, 4.0f);          //中段
            g.FillEllipse(&fill, 7.0f, 6.0f, 6.0f, 6.0f);           //下半圆
            g.DrawArc(&pen, 4.5f, 4.5f, 11.0f, 10.0f, 0.0f, 180.0f); //托架弧线（下半圆弧）
            g.DrawLine(&pen, 10.0f, 14.5f, 10.0f, 17.0f);            //立杆
            g.DrawLine(&pen, 6.5f, 17.5f, 13.5f, 17.5f);             //底座
        }
        else
        {
            g.DrawRectangle(&pen, 2.5f, 4.5f, 15.0f, 11.0f);          //画面方框
            const Gdiplus::PointF play[3] = {
                Gdiplus::PointF(8.5f, 7.5f),                            //三角左上
                Gdiplus::PointF(8.5f, 12.5f),                           //三角左下
                Gdiplus::PointF(13.0f, 10.0f)                           //三角右尖
            };
            g.FillPolygon(&fill, play, 3);
        }
        if (missing)
            g.DrawLine(&pen, 2.5f, 17.5f, 17.5f, 2.5f);             //表示文件缺失的斜线
    }
    return bmp;
}

//重命名不合法时给用户看的原因
std::wstring renameErrorText(RecordRenameResult r)
{
    switch (r)
    {
    //去掉空白后什么都不剩
    case record_rename_empty:
        return TR(L"文件名不能为空。");
    //含不允许的字符，或是 CON、NUL 之类的系统保留名
    case record_rename_invalid:
        return TR(L"文件名里有不允许使用的字符，或者是系统保留的名称，请换一个名字。");
    //同一目录里已有同名文件
    case record_rename_exists:
        return TR(L"同一文件夹里已经有同名的文件，请换一个名字。");
    //新路径放不进录像记录
    case record_rename_too_long:
        return TR(L"文件名太长，请改短一些。");
    //其余结果不是错误，不会走到这里
    default:
        return std::wstring();
    }
}

} // namespace

CRecordManagerWindow::Row::Row()
    : m_id(0)
    , m_durationMs(0)
    , m_sizeBytes(0)
    , m_endTimeMs(0)
    , m_exists(false)
    , m_captionStatus(0)
{
}

CRecordManagerWindow::CRecordManagerWindow()
    : m_host(NULL)
    , m_editId(0)
    , m_list(NULL)
    , m_empty(NULL)
    , m_count(NULL)
    , m_play(NULL)
    , m_openFolder(NULL)
    , m_send(NULL)
    , m_rename(NULL)
    , m_delete(NULL)
    , m_caption(NULL)
    , m_captionAvailable(false)
    , m_iconPresent(NULL)
    , m_iconMissing(NULL)
    , m_iconAudio(NULL)
    , m_iconAudioMissing(NULL)
    , m_cloud(NULL)
    , m_page(kRecordMgrPageLocal)
    , m_cloudRequested(false)
    , m_cloudEditId(0)
    , m_cloudReloadPosted(false)
    , m_tabs(NULL)
    , m_localPage(NULL)
    , m_cloudPage(NULL)
    , m_cloudList(NULL)
    , m_cloudEmpty(NULL)
    , m_cloudUsage(NULL)
    , m_cloudUsageBar(NULL)
    , m_cloudPlay(NULL)
    , m_cloudDownload(NULL)
    , m_cloudSend(NULL)
    , m_cloudRename(NULL)
    , m_cloudDelete(NULL)
    , m_cloudMore(NULL)
{
}

CRecordManagerWindow::~CRecordManagerWindow()
{
    CloseWindow();
    //列表借用这几张图标位图，窗口（连同列表）销毁之后才释放
    if (m_iconPresent != NULL)
        ::DeleteObject(m_iconPresent);
    if (m_iconMissing != NULL)
        ::DeleteObject(m_iconMissing);
    if (m_iconAudio != NULL)
        ::DeleteObject(m_iconAudio);
    if (m_iconAudioMissing != NULL)
        ::DeleteObject(m_iconAudioMissing);
}

void CRecordManagerWindow::Open(HWND owner, flamingomedia::IMediaHost* host)
{
    if (host == NULL)
        return;
    m_host = host;
    if (IsWindow())
    {
        LOG_INFO("record manager: already open, reloaded and brought to front.");
        reload();
        if (m_page == kRecordMgrPageCloud && m_cloud != NULL)
            m_cloud->RequestList(false);
        bringToFront();
        return;
    }
    ensureWindow(owner);
    if (!IsWindow())
        return;
    //语音字幕：打开录制管理时就在后台加载模型，点「编辑字幕」时多半已经加载好（docs/语音字幕方案.md 5.3）
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (captionModule != NULL)
        captionModule->Preload();
    reload();
    //云端页随云端录像中心的列表与进度刷新；回到上次所在的页签
    if (m_cloud != NULL)
        m_cloud->SetObserver(this);
    showPage(m_page);
    ShowWindow(SW_SHOW);
    bringToFront();
    LOG_INFO("record manager: opened, %u records, page %d.", static_cast<unsigned>(m_rows.size()), m_page);
}

void CRecordManagerWindow::SetCloudCenter(CCloudRecordCenter* center)
{
    if (m_cloud != NULL && m_cloud != center)
        m_cloud->SetObserver(NULL);
    m_cloud = center;
    if (m_cloud != NULL && IsWindow())
        m_cloud->SetObserver(this);
    if (IsWindow())
        reloadCloud();
}

void CRecordManagerWindow::ReloadIfOpen()
{
    if (!IsWindow() || m_editId != 0)
        return;
    reload();
}

void CRecordManagerWindow::CloseWindow()
{
    //关闭窗口不影响合成与上传：只解除观察，云端录像中心照常工作，完成时在右下角提示
    if (m_cloud != NULL)
        m_cloud->SetObserver(NULL);
    m_cloudRequested = false;
    m_cloudEditId = 0;
    m_cloudReloadPosted = false;
    m_cloudRows.clear();
    m_tabs = NULL;
    m_localPage = NULL;
    m_cloudPage = NULL;
    m_cloudList = NULL;
    m_cloudEmpty = NULL;
    m_cloudUsage = NULL;
    m_cloudUsageBar = NULL;
    m_cloudPlay = NULL;
    m_cloudDownload = NULL;
    m_cloudSend = NULL;
    m_cloudRename = NULL;
    m_cloudDelete = NULL;
    m_cloudMore = NULL;
    m_editId = 0;
    m_list = NULL;
    m_empty = NULL;
    m_count = NULL;
    m_play = NULL;
    m_openFolder = NULL;
    m_send = NULL;
    m_rename = NULL;
    m_delete = NULL;
    m_caption = NULL;
    m_rows.clear();
    if (IsWindow())
        DestroyWindow();
}

bool CRecordManagerWindow::IsOpen() const
{
    return ::IsWindow(m_hWnd) != FALSE;
}

void CRecordManagerWindow::ensureWindow(HWND owner)
{
    //有属主的窗口默认不进任务栏；可以最小化，所以要有任务栏按钮，否则最小化了找不回来
    if (Create(owner, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW) == NULL)
    {
        LOG_ERROR("record manager: create window failed.");
        return;
    }
    SetTitle(TR(kWindowTitle));
    SetTitleBarHeight(kRecordMgrTitleBarH);
    SetButtons(true, true, true);
    SetResizable(true);
    //DuiFrameWindow 的 ResizeClient / SetMinSize 设的都是整窗尺寸（开发约定文档「UI 约定」第 7 条），常量已含标题栏
    const int minW = RecordManagerMinWidth();
    const int width = (kRecordMgrDefaultW > minW) ? kRecordMgrDefaultW : minW;
    SetMinSize(minW, kRecordMgrMinH);
    ResizeClient(width, kRecordMgrDefaultH);
    HBITMAP titleIcon = m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor);
    if (titleIcon != NULL)
        SetIcon(titleIcon);
    m_programIcons.ApplyTaskIcons(m_hWnd);
    if (m_iconPresent == NULL)
        m_iconPresent = makeRowIcon(false, false);
    if (m_iconMissing == NULL)
        m_iconMissing = makeRowIcon(true, false);
    if (m_iconAudio == NULL)
        m_iconAudio = makeRowIcon(false, true);
    if (m_iconAudioMissing == NULL)
        m_iconAudioMissing = makeRowIcon(true, true);

    RecordManagerBody body = BuildRecordManagerBody();
    m_list = body.m_list;
    m_empty = body.m_empty;
    m_count = body.m_count;
    m_play = body.m_play;
    m_openFolder = body.m_openFolder;
    m_send = body.m_send;
    m_rename = body.m_rename;
    m_delete = body.m_delete;
    m_caption = body.m_caption;
    m_tabs = body.m_tabs;
    m_localPage = body.m_localPage;
    m_cloudPage = body.m_cloudPage;
    m_cloudList = body.m_cloudList;
    m_cloudEmpty = body.m_cloudEmpty;
    m_cloudUsage = body.m_cloudUsage;
    m_cloudUsageBar = body.m_cloudUsageBar;
    m_cloudPlay = body.m_cloudPlay;
    m_cloudDownload = body.m_cloudDownload;
    m_cloudSend = body.m_cloudSend;
    m_cloudRename = body.m_cloudRename;
    m_cloudDelete = body.m_cloudDelete;
    m_cloudMore = body.m_cloudMore;
    SetClientContent(std::move(body.m_root));

    //居中在属主所在的显示器上
    HMONITOR mon = ::MonitorFromWindow(owner != NULL ? owner : m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(mon, &mi))
    {
        const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - width) / 2;
        const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - kRecordMgrDefaultH) / 2;
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void CRecordManagerWindow::reload()
{
    if (m_list == NULL || m_host == NULL)
        return;
    //记下原来选中的记录，重建后尽量选回它
    const Row* sel = selectedRow();
    const int64_t selectedId = (sel != NULL) ? sel->m_id : 0;

    const uint32_t total = m_host->ListRecordings(NULL, 0);
    std::vector<flamingomedia::MediaRecordingInfo> infos(total);
    for (size_t i = 0; i < infos.size(); ++i)
    {
        memset(&infos[i], 0, sizeof(flamingomedia::MediaRecordingInfo));
        infos[i].m_cbSize = sizeof(flamingomedia::MediaRecordingInfo);
    }
    uint32_t filled = 0;
    if (total > 0)
    {
        filled = m_host->ListRecordings(&infos[0], total);
        //两次读取之间又写入了记录时以较小的数为准，多出的那几条下次刷新再显示
        if (filled > total)
            filled = total;
    }

    //语音字幕：可用时逐行查字幕状态（读草稿里的导出记录），副文字末尾写出来
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    m_captionAvailable = (captionModule != NULL);

    m_rows.clear();
    m_list->DeleteAllItems();
    int selectIndex = -1;
    for (uint32_t i = 0; i < filled; ++i)
    {
        Row row;
        row.m_id = infos[i].m_id;
        row.m_path = infos[i].m_path;
        row.m_durationMs = infos[i].m_durationMs;
        row.m_sizeBytes = infos[i].m_sizeBytes;
        row.m_endTimeMs = infos[i].m_endTimeMs;
        row.m_exists = recordFileExists(row.m_path);
        row.m_captionStatus = (captionModule != NULL && row.m_exists) ? captionModule->GetFileStatus(row.m_path.c_str())
                                                                      : 0;
        SYSTEMTIME local;
        memset(&local, 0, sizeof(local));
        RecordUtcMsToLocal(row.m_endTimeMs, local);
        const int index = m_list->AddItem(RecordFileNameOf(row.m_path).c_str());
        std::wstring subText = RecordRowSubText(row.m_exists, local, row.m_durationMs,
                                                static_cast<uint64_t>(row.m_sizeBytes));
        const std::wstring captionText = RecordCaptionStatusText(row.m_captionStatus);
        if (!captionText.empty())
            subText += L" \u00B7 " + captionText;
        m_list->SetItemSubText(index, subText.c_str());
        //录音（.m4a，第 24 期）画话筒，录像画画面
        if (IsAudioRecordingPath(row.m_path))
            m_list->SetItemIcon(index, row.m_exists ? m_iconAudio : m_iconAudioMissing);
        else
            m_list->SetItemIcon(index, row.m_exists ? m_iconPresent : m_iconMissing);
        if (row.m_id == selectedId)
            selectIndex = index;
        m_rows.push_back(row);
    }
    if (selectIndex >= 0)
        m_list->SetCurSel(selectIndex, false);

    if (m_count != NULL)
        m_count->SetText(RecordManagerCountText(static_cast<int>(m_rows.size())).c_str());
    //没有录像时用提示代替列表；SetVisible 不会重排，要对父容器强制排一次（开发约定文档「UI 约定」第 5 条）
    const bool empty = m_rows.empty();
    if (m_empty != NULL && m_list->IsVisible() == empty)
    {
        m_list->SetVisible(!empty);
        m_empty->SetVisible(empty);
        balloonwjui::DuiControl* parent = m_list->GetParent();
        if (parent != NULL)
            parent->ForceLayout(parent->GetRect());
    }
    applyCaptionButton();
    updateButtons();
    Invalidate();
}

void CRecordManagerWindow::applyCaptionButton()
{
    if (m_caption == NULL || m_caption->IsVisible() == m_captionAvailable)
        return;
    m_caption->SetVisible(m_captionAvailable);
    //SetVisible 不会重排，要对按钮所在的工具栏强制排一次（开发约定文档「UI 约定」第 5 条）
    balloonwjui::DuiControl* bar = m_caption->GetParent();
    if (bar != NULL)
        bar->ForceLayout(bar->GetRect());
}

void CRecordManagerWindow::openCaptionEditor()
{
    const Row* row = selectedRow();
    if (row == NULL)
        return;
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    const bool opened = captionModule != NULL && captionModule->OpenEditor(m_hWnd, row->m_path.c_str());
    LOG_INFO("record manager: edit captions of record id=%lld, opened=%d.", static_cast<long long>(row->m_id),
             opened ? 1 : 0);
    if (!opened)
        ::MessageBoxW(m_hWnd, TR(L"无法打开字幕编辑器。"), TR(kWindowTitle), MB_OK | MB_ICONWARNING);
}

void CRecordManagerWindow::renameCaptionFiles(const std::wstring& oldPath, const std::wstring& newPath)
{
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (captionModule != NULL)
    {
        captionModule->OnVideoRenamed(oldPath.c_str(), newPath.c_str());
        return;
    }
    //字幕不可用（管理员关闭了字幕或字幕 DLL 加载失败）：按命名规则把草稿一起改名，以后重新可用时还能接着编辑
    const std::wstring oldDraft = RecordCaptionDraftPath(oldPath);
    if (!recordFileExists(oldDraft))
        return;
    const bool ok = ::MoveFileW(oldDraft.c_str(), RecordCaptionDraftPath(newPath).c_str()) != FALSE;
    LOG_INFO("record manager: caption draft renamed locally, ok=%d.", ok ? 1 : 0);
}

void CRecordManagerWindow::deleteCaptionFiles(const std::wstring& videoPath)
{
    caption::ICaptionModule* captionModule = flamingomedia::CMediaCaptionBridge::Instance().Acquire();
    if (captionModule != NULL)
    {
        captionModule->OnVideoDeleted(videoPath.c_str());
        return;
    }
    const std::wstring draft = RecordCaptionDraftPath(videoPath);
    if (!recordFileExists(draft))
        return;
    const bool ok = ::DeleteFileW(draft.c_str()) != FALSE;
    LOG_INFO("record manager: caption draft deleted locally, ok=%d.", ok ? 1 : 0);
}

const CRecordManagerWindow::Row* CRecordManagerWindow::selectedRow() const
{
    if (m_list == NULL)
        return NULL;
    const int index = m_list->GetCurSel();
    if (index < 0 || index >= static_cast<int>(m_rows.size()))
        return NULL;
    return &m_rows[index];
}

void CRecordManagerWindow::updateButtons()
{
    const Row* row = selectedRow();
    const int actions = (row != NULL) ? RecordRowActions(row->m_exists, m_captionAvailable) : 0;
    if (m_caption != NULL)
        m_caption->SetEnabled((actions & record_action_caption) != 0);
    if (m_play != NULL)
        m_play->SetEnabled((actions & record_action_play) != 0);
    if (m_openFolder != NULL)
        m_openFolder->SetEnabled((actions & record_action_open_folder) != 0);
    if (m_send != NULL)
        m_send->SetEnabled((actions & record_action_send) != 0);
    if (m_rename != NULL)
        m_rename->SetEnabled((actions & record_action_rename) != 0);
    if (m_delete != NULL)
    {
        //文件已不存在时这个按钮只从列表里移除记录
        const bool removeOnly = (actions & record_action_remove) != 0;
        m_delete->SetText(removeOnly ? TR(L"从列表移除") : TR(L"删除"));
        m_delete->SetEnabled((actions & (record_action_delete | record_action_remove)) != 0);
    }
}

void CRecordManagerWindow::doAction(int action)
{
    const Row* row = selectedRow();
    if (row == NULL || (RecordRowActions(row->m_exists, m_captionAvailable) & action) == 0)
        return;
    switch (action)
    {
    //用系统默认的播放器打开
    case record_action_play:
        ::ShellExecuteW(m_hWnd, L"open", row->m_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        LOG_INFO("record manager: play record id=%lld.", static_cast<long long>(row->m_id));
        break;
    //打开所在文件夹并选中文件
    case record_action_open_folder:
    {
        const std::wstring param = L"/select,\"" + row->m_path + L"\"";
        ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
        break;
    }
    //经宿主的选会话窗发送（与从主面板发起的录制完成后「发送到会话…」同一流程）
    case record_action_send:
        if (m_host != NULL)
            m_host->PickConversationAndSendFile(row->m_path.c_str());
        LOG_INFO("record manager: send record id=%lld.", static_cast<long long>(row->m_id));
        break;
    //在列表里原地编辑文件名
    case record_action_rename:
        beginRename(std::wstring());
        break;
    //删除文件（移到回收站）与记录，或只删记录
    case record_action_delete:
    case record_action_remove:
        deleteSelected();
        break;
    //打开字幕编辑器（语音字幕）
    case record_action_caption:
        openCaptionEditor();
        break;
    //没有其他操作
    default:
        break;
    }
}

void CRecordManagerWindow::beginRename(const std::wstring& prefill)
{
    const Row* row = selectedRow();
    if (row == NULL || m_list == NULL || !row->m_exists)
        return;
    const int index = m_list->GetCurSel();
    //先把这一行滚进可见范围，再按当前滚动位置算输入框的位置
    m_list->EnsureVisible(index);
    RECT rc;
    const int textLeft = kRowTextLeftPad + kRecordMgrIconPx + kRowIconTextGap;
    if (!RecordInlineEditRect(m_list->GetRect(), index, m_list->GetItemHeight(), m_list->GetScrollPos(), textLeft,
                              balloonwjui::DuiScrollBar::kOverlayBandPx, rc))
    {
        LOG_WARNING("record manager: rename row %d not fully visible, edit not started.", index);
        return;
    }
    const std::wstring text = prefill.empty() ? RecordRenameEditText(row->m_path) : prefill;
    if (m_list->BeginEdit(rc, text, kRecordMgrIdRenameEdit) == NULL)
        return;
    m_editId = row->m_id;
}

void CRecordManagerWindow::commitRename()
{
    if (m_editId == 0 || m_list == NULL)
        return;
    const int64_t id = m_editId;
    const std::wstring input = m_list->EditText();
    m_editId = 0;
    m_list->EndEdit();

    //编辑期间列表可能已刷新，按 id 找回这一行
    int index = -1;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].m_id == id)
            index = static_cast<int>(i);
    }
    if (index < 0 || m_host == NULL)
        return;
    const std::wstring oldPath = m_rows[index].m_path;
    std::wstring newPath;
    const RecordRenameResult r = ValidateRecordRename(oldPath, input, &recordFileExists, newPath);
    if (r == record_rename_unchanged)
        return;
    if (r != record_rename_ok)
    {
        //不合法：说明原因，再回到编辑，保留用户输入的文字以便修改
        ::MessageBoxW(m_hWnd, renameErrorText(r).c_str(), TR(kWindowTitle), MB_OK | MB_ICONWARNING);
        m_list->SetCurSel(index, false);
        beginRename(input);
        return;
    }
    if (!::MoveFileW(oldPath.c_str(), newPath.c_str()))
    {
        LOG_ERROR("record manager: rename file failed, id=%lld, error=%lu.", static_cast<long long>(id),
                  ::GetLastError());
        ::MessageBoxW(m_hWnd, TR(L"重命名失败，文件可能正被其他程序使用。"), TR(kWindowTitle), MB_OK | MB_ICONWARNING);
        return;
    }
    if (!m_host->UpdateRecordingPath(id, newPath.c_str()))
    {
        //记录改不了：把文件名改回去，免得记录指向一个已经不存在的路径
        LOG_ERROR("record manager: update record path failed, id=%lld, file name restored.", static_cast<long long>(id));
        ::MoveFileW(newPath.c_str(), oldPath.c_str());
        ::MessageBoxW(m_hWnd, TR(L"重命名失败，文件可能正被其他程序使用。"), TR(kWindowTitle), MB_OK | MB_ICONWARNING);
        return;
    }
    LOG_INFO("record manager: renamed record id=%lld.", static_cast<long long>(id));
    //字幕草稿跟着改名，否则改名后的录像就找不到原来的字幕了
    renameCaptionFiles(oldPath, newPath);
    reload();
}

void CRecordManagerWindow::cancelRename()
{
    if (m_editId == 0 || m_list == NULL)
        return;
    m_editId = 0;
    m_list->EndEdit();
}

void CRecordManagerWindow::deleteSelected()
{
    const Row* row = selectedRow();
    if (row == NULL || m_host == NULL)
        return;
    const int64_t id = row->m_id;
    if (row->m_exists)
    {
        //{0} 为文件名
        const std::wstring tip = TRF(L"确定删除「{0}」吗？\n\n文件会移到回收站。", RecordFileNameOf(row->m_path));
        if (::MessageBoxW(m_hWnd, tip.c_str(), TR(L"删除录像"), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
            return;
        //SHFileOperation 要求源路径以两个 L'\0' 结尾：std::wstring 的 c_str 自带一个，这里再补一个
        std::wstring from = row->m_path;
        from.push_back(L'\0');
        SHFILEOPSTRUCTW op;
        memset(&op, 0, sizeof(op));
        op.hwnd = m_hWnd;
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        const int rc = ::SHFileOperationW(&op);
        //确认之后、删除之前文件已被别处删掉：照样删记录
        if ((rc != 0 || op.fAnyOperationsAborted) && recordFileExists(row->m_path))
        {
            LOG_ERROR("record manager: move record file to recycle bin failed, id=%lld, rc=%d.",
                      static_cast<long long>(id), rc);
            ::MessageBoxW(m_hWnd, TR(L"删除失败，文件可能正被其他程序使用。"), TR(L"删除录像"), MB_OK | MB_ICONWARNING);
            return;
        }
    }
    //先记下路径：RemoveRecording 之后 reload 会重建 m_rows，row 指针随之失效
    const std::wstring videoPath = row->m_path;
    const bool ok = m_host->RemoveRecording(id);
    LOG_INFO("record manager: removed record id=%lld, ok=%d.", static_cast<long long>(id), ok ? 1 : 0);
    //录像没了，字幕草稿也不再有用（导出的带字幕录像是独立文件，不删）
    deleteCaptionFiles(videoPath);
    reload();
}

void CRecordManagerWindow::showContextMenu()
{
    const Row* row = selectedRow();
    if (row == NULL)
        return;
    const int actions = RecordRowActions(row->m_exists, m_captionAvailable);
    //菜单项的 id 直接取操作的位值（都不为 0，0 是 TrackPopup 的「没有选中」）
    balloonwjui::DuiMenu menu;
    if ((actions & record_action_play) != 0)
        menu.AppendItem(record_action_play, TR(L"播放"));
    if ((actions & record_action_open_folder) != 0)
        menu.AppendItem(record_action_open_folder, TR(L"打开所在文件夹"));
    if ((actions & record_action_send) != 0)
        menu.AppendItem(record_action_send, TR(L"发送到会话…"));
    if ((actions & record_action_rename) != 0)
    {
        menu.AppendSeparator();
        menu.AppendItem(record_action_rename, TR(L"重命名"));
    }
    if ((actions & record_action_delete) != 0)
        menu.AppendItem(record_action_delete, TR(L"删除"));
    if ((actions & record_action_remove) != 0)
        menu.AppendItem(record_action_remove, TR(L"从列表移除"));
    if ((actions & record_action_caption) != 0)
        menu.AppendItem(record_action_caption, TR(L"编辑字幕"));
    //上传到云端（第 27 期 27d）：文件还在、企业开通了云端录制时才有
    if (row->m_exists && m_cloud != NULL && m_host != NULL && m_host->IsCloudRecordEnabled())
    {
        menu.AppendSeparator();
        menu.AppendItem(kLocalMenuUploadCloud, TR(L"上传到云端"));
    }
    POINT pt;
    ::GetCursorPos(&pt);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    if (chosen == kLocalMenuUploadCloud)
        uploadLocalSelected();
    else if (chosen != 0)
        doAction(static_cast<int>(chosen));
}

void CRecordManagerWindow::bringToFront()
{
    if (!IsWindow())
        return;
    //最小化了先恢复，再提到最前并设为前台窗口
    if (IsIconic())
        ShowWindow(SW_RESTORE);
    ::BringWindowToTop(m_hWnd);
    ::SetForegroundWindow(m_hWnd);
}

LRESULT CRecordManagerWindow::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //各自定义通知码都连同 ctrlId 一起判（开发约定文档「UI 约定」第 4 条）
    //「本机 / 云端」页签切换，extra 为新页签的下标
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) && n->ctrlId == kRecordMgrIdTabs)
    {
        showPage(static_cast<int>(n->extra));
        return 0;
    }
    //云端页的工具栏按钮与「加载更多」
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        if (n->ctrlId == kRecordMgrIdCloudPlay)
        {
            doCloudAction(cloud_action_play);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdCloudDownload)
        {
            doCloudAction(cloud_action_download);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdCloudSend)
        {
            doCloudAction(cloud_action_send);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdCloudRename)
        {
            doCloudAction(cloud_action_rename);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdCloudDelete)
        {
            doCloudAction(cloud_action_delete);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdCloudMore)
        {
            if (m_cloud != NULL)
                m_cloud->RequestList(true);
            return 0;
        }
    }
    //云端列表：选中行变了刷新按钮，双击播放，右键与原地编辑被打断都延后处理
    if (n->ctrlId == kRecordMgrIdCloudList)
    {
        if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED))
        {
            updateCloudButtons();
            return 0;
        }
        if (n->code == static_cast<UINT>(balloonwjui::DUIN_DBLCLK))
        {
            doCloudAction(cloud_action_play);
            return 0;
        }
        if (n->code == static_cast<UINT>(CRecordManagerList::kNotifyContextMenu))
        {
            PostMessage(kMsgRecordManagerDeferred, deferred_cloud_context_menu, n->extra);
            return 0;
        }
        if (n->code == static_cast<UINT>(CRecordManagerList::kNotifyEditInterrupted))
        {
            PostMessage(kMsgRecordManagerDeferred, deferred_cloud_commit_edit, 0);
            return 0;
        }
    }
    //云端页原地重命名的输入框：回车或失去焦点时提交，Esc 放弃
    if (n->ctrlId == kRecordMgrIdCloudRenameEdit)
    {
        if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ENTER) ||
            n->code == static_cast<UINT>(balloonwjui::DUIN_KILLFOCUS))
        {
            PostMessage(kMsgRecordManagerDeferred, deferred_cloud_commit_edit, 0);
            return 0;
        }
        if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE))
        {
            PostMessage(kMsgRecordManagerDeferred, deferred_cloud_cancel_edit, 0);
            return 0;
        }
    }
    //本机页工具栏按钮
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        if (n->ctrlId == kRecordMgrIdPlay)
        {
            doAction(record_action_play);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdOpenFolder)
        {
            doAction(record_action_open_folder);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdSend)
        {
            doAction(record_action_send);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdRename)
        {
            doAction(record_action_rename);
            return 0;
        }
        if (n->ctrlId == kRecordMgrIdDelete)
        {
            const Row* row = selectedRow();
            if (row != NULL)
                doAction(row->m_exists ? record_action_delete : record_action_remove);
            return 0;
        }
    }
    //选中行变了：刷新按钮
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) && n->ctrlId == kRecordMgrIdList)
    {
        updateButtons();
        return 0;
    }
    //双击一行：播放
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_DBLCLK) && n->ctrlId == kRecordMgrIdList)
    {
        doAction(record_action_play);
        return 0;
    }
    //右键：等鼠标事件处理完再弹菜单
    if (n->code == static_cast<UINT>(CRecordManagerList::kNotifyContextMenu) && n->ctrlId == kRecordMgrIdList)
    {
        PostMessage(kMsgRecordManagerDeferred, deferred_context_menu, n->extra);
        return 0;
    }
    //原地编辑中列表被单击或滚动：提交编辑（延后到通知处理完，这时还在列表的鼠标或绘制处理之中）
    if (n->code == static_cast<UINT>(CRecordManagerList::kNotifyEditInterrupted) && n->ctrlId == kRecordMgrIdList)
    {
        PostMessage(kMsgRecordManagerDeferred, deferred_commit_edit, 0);
        return 0;
    }
    //原地编辑的输入框：回车或失去焦点时提交，Esc 放弃，都延后到输入框的通知处理完再做
    if (n->ctrlId == kRecordMgrIdRenameEdit)
    {
        if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ENTER) ||
            n->code == static_cast<UINT>(balloonwjui::DUIN_KILLFOCUS))
        {
            PostMessage(kMsgRecordManagerDeferred, deferred_commit_edit, 0);
            return 0;
        }
        if (n->code == static_cast<UINT>(balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE))
        {
            PostMessage(kMsgRecordManagerDeferred, deferred_cancel_edit, 0);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CRecordManagerWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    LOG_INFO("record manager: closed.");
    CloseWindow();
    return 0;
}

LRESULT CRecordManagerWindow::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //原地编辑时按键都归输入框（Delete 是删字符、F2 无意义）
    if (m_editId != 0 || m_cloudEditId != 0)
    {
        bHandled = FALSE;
        return 0;
    }
    //云端页：F2 重命名、Delete 删除
    if (m_page == kRecordMgrPageCloud)
    {
        if (wParam == VK_F2)
        {
            doCloudAction(cloud_action_rename);
            return 0;
        }
        if (wParam == VK_DELETE)
        {
            doCloudAction(cloud_action_delete);
            return 0;
        }
        bHandled = FALSE;
        return 0;
    }
    if (wParam == VK_F2)
    {
        doAction(record_action_rename);
        return 0;
    }
    if (wParam == VK_DELETE)
    {
        const Row* row = selectedRow();
        if (row != NULL)
            doAction(row->m_exists ? record_action_delete : record_action_remove);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CRecordManagerWindow::OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //回到本窗口时重新读取：用户可能刚在资源管理器里删除或改名了录像文件
    if (LOWORD(wParam) != WA_INACTIVE)
        ReloadIfOpen();
    bHandled = FALSE;
    return 0;
}

LRESULT CRecordManagerWindow::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    //改变大小后输入框与行的位置对不上：提交编辑
    if (m_editId != 0)
        PostMessage(kMsgRecordManagerDeferred, deferred_commit_edit, 0);
    if (m_cloudEditId != 0)
        PostMessage(kMsgRecordManagerDeferred, deferred_cloud_commit_edit, 0);
    //排版交给基类
    bHandled = FALSE;
    return 0;
}

LRESULT CRecordManagerWindow::OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    switch (static_cast<int>(wParam))
    {
    //右键按下之后弹出菜单
    case deferred_context_menu:
        showContextMenu();
        break;
    //提交原地重命名
    case deferred_commit_edit:
        commitRename();
        break;
    //放弃原地重命名
    case deferred_cancel_edit:
        cancelRename();
        break;
    //云端页：右键按下之后弹出菜单
    case deferred_cloud_context_menu:
        showCloudContextMenu();
        break;
    //云端页：提交原地重命名
    case deferred_cloud_commit_edit:
        commitCloudRename();
        break;
    //云端页：放弃原地重命名
    case deferred_cloud_cancel_edit:
        cancelCloudRename();
        break;
    //云端页：列表变了，重建（正在原地重命名时等编辑结束再重建）
    case deferred_cloud_reload:
        m_cloudReloadPosted = false;
        if (m_cloudEditId == 0)
            reloadCloud();
        break;
    //未知动作：忽略
    default:
        break;
    }
    return 0;
}

//======== 云端页（第 27 期 27d） ========

void CRecordManagerWindow::OnCloudRecordsChanged()
{
    //回调可能发生在别的窗口消息的处理中途（如本窗口自己发起的删除）：延后到下一条消息再重建，多次变化合并成一次
    if (!IsWindow() || m_cloudReloadPosted)
        return;
    m_cloudReloadPosted = true;
    PostMessage(kMsgRecordManagerDeferred, deferred_cloud_reload, 0);
}

void CRecordManagerWindow::OnCloudProgressChanged()
{
    if (IsWindow() && m_page == kRecordMgrPageCloud && m_cloudEditId == 0)
        refreshCloudTrailing();
}

void CRecordManagerWindow::showPage(int page)
{
    if (page != kRecordMgrPageLocal && page != kRecordMgrPageCloud)
        return;
    //换页时正在进行的原地重命名照常提交
    if (m_editId != 0)
        commitRename();
    if (m_cloudEditId != 0)
        commitCloudRename();
    m_page = page;
    if (m_tabs != NULL && m_tabs->GetCurSel() != page)
        m_tabs->SetCurSel(page, false);
    if (m_localPage == NULL || m_cloudPage == NULL)
        return;
    m_localPage->SetVisible(page == kRecordMgrPageLocal);
    m_cloudPage->SetVisible(page == kRecordMgrPageCloud);
    //SetVisible 不会重排；两个页面都是根容器的直接子控件，对根容器强制排一次（开发约定文档「UI 约定」第 5 条）
    balloonwjui::DuiControl* root = m_localPage->GetParent();
    if (root != NULL)
        root->ForceLayout(root->GetRect());
    if (page == kRecordMgrPageCloud)
    {
        //本次打开后第一次看云端页：取最新的一页（登录时补做合成取过一次，这里刷新到最新）
        if (!m_cloudRequested && m_cloud != NULL)
            m_cloudRequested = m_cloud->RequestList(false);
        reloadCloud();
    }
    Invalidate();
    LOG_INFO("record manager: page %d shown.", page);
}

int CRecordManagerWindow::cloudStateOf(const CloudRecordInfo& info) const
{
    const CloudRowLocal local = (m_cloud != NULL) ? m_cloud->LocalState(info.m_recordId) : CloudRowLocal();
    return CloudRowStateOf(info.m_status, local);
}

void CRecordManagerWindow::applyCloudTrailing(int index, const CloudRecordInfo& info, int64_t nowMs)
{
    if (m_cloudList == NULL)
        return;
    const CloudRowLocal local = (m_cloud != NULL) ? m_cloud->LocalState(info.m_recordId) : CloudRowLocal();
    const int state = CloudRowStateOf(info.m_status, local);
    const int daysLeft = CloudDaysLeft(info.m_expireTime, nowMs);
    const int progress = CloudRowShowsProgress(state, local.m_progress) ? local.m_progress : -1;
    m_cloudList->SetTrailing(index, CloudRowStateText(state, local.m_progress, daysLeft),
                             CloudRowStateColor(state, daysLeft), progress);
}

void CRecordManagerWindow::reloadCloud()
{
    if (m_cloudList == NULL || m_cloudEmpty == NULL)
        return;
    const CloudRecordInfo* sel = selectedCloud();
    const int64_t selectedId = (sel != NULL) ? sel->m_recordId : 0;
    m_cloudList->DeleteAllItems();
    m_cloudList->ClearTrailing();
    m_cloudRows.clear();

    std::wstring emptyText;
    bool loaded = false;
    bool hasMore = false;
    if (m_cloud == NULL)
    {
        emptyText = TR(L"云端录制不可用");
    }
    else
    {
        loaded = m_cloud->ListLoaded();
        hasMore = loaded && m_cloud->ListHasMore();
        const int64_t nowMs = nowUnixMs();
        const std::vector<CloudRecordInfo>& records = m_cloud->Records();
        int selectIndex = -1;
        for (size_t i = 0; i < records.size(); ++i)
        {
            const CloudRecordInfo& info = records[i];
            const int index = m_cloudList->AddItem(cloudTitleOf(info).c_str());
            SYSTEMTIME local;
            memset(&local, 0, sizeof(local));
            RecordUtcMsToLocal(info.m_startTime, local);
            m_cloudList->SetItemSubText(index, CloudRowSubText(local, info.m_source, info.m_audioOnly, info.m_durationMs,
                                                               info.m_bytes).c_str());
            //没有录到内容的画缺失的样式，录音画话筒，其余画画面
            const bool failed = (info.m_status == cloudrecord_status_failed);
            if (info.m_audioOnly)
                m_cloudList->SetItemIcon(index, failed ? m_iconAudioMissing : m_iconAudio);
            else
                m_cloudList->SetItemIcon(index, failed ? m_iconMissing : m_iconPresent);
            applyCloudTrailing(index, info, nowMs);
            m_cloudRows.push_back(info.m_recordId);
            if (info.m_recordId == selectedId)
                selectIndex = index;
        }
        if (selectIndex >= 0)
            m_cloudList->SetCurSel(selectIndex, false);
        if (!loaded)
            emptyText = TR(L"正在读取…");
        else if (!m_cloud->CloudEnabled())
            emptyText = TR(L"管理员没有开通云端录制");
        else
            emptyText = TR(L"还没有云端录像");
        //用量：读到列表之后才知道
        if (m_cloudUsage != NULL)
        {
            if (loaded)
            {
                m_cloudUsage->SetText(CloudUsageText(m_cloud->CloudEnabled(), m_cloud->UsedBytes(), m_cloud->QuotaBytes(),
                                                     m_cloud->RetainDays()).c_str());
                m_cloudUsage->SetTextColor(CloudUsageWarn(m_cloud->CloudEnabled(), m_cloud->UsedBytes(),
                                                          m_cloud->QuotaBytes()) ? kUsageWarnColor : kUsageTextColor);
            }
            else
            {
                m_cloudUsage->SetText(L"");
            }
        }
        if (m_cloudUsageBar != NULL)
        {
            const bool showBar = loaded && m_cloud->CloudEnabled();
            m_cloudUsageBar->SetPos(showBar ? CloudUsagePermille(m_cloud->UsedBytes(), m_cloud->QuotaBytes()) : 0, false);
        }
    }
    m_cloudEmpty->SetText(emptyText.c_str());

    //列表与提示二选一，「加载更多」只在还有更早的录像时出现；显隐变了时对云端页强制排一次（UI 约定第 5 条）
    const bool empty = m_cloudRows.empty();
    bool relayout = false;
    if (m_cloudList->IsVisible() == empty)
    {
        m_cloudList->SetVisible(!empty);
        m_cloudEmpty->SetVisible(empty);
        relayout = true;
    }
    if (m_cloudMore != NULL && m_cloudMore->IsVisible() != hasMore)
    {
        m_cloudMore->SetVisible(hasMore);
        relayout = true;
    }
    if (relayout && m_cloudPage != NULL)
        m_cloudPage->ForceLayout(m_cloudPage->GetRect());
    updateCloudButtons();
    Invalidate();
}

void CRecordManagerWindow::refreshCloudTrailing()
{
    if (m_cloudList == NULL || m_cloud == NULL)
        return;
    const int64_t nowMs = nowUnixMs();
    const std::vector<CloudRecordInfo>& records = m_cloud->Records();
    for (size_t row = 0; row < m_cloudRows.size(); ++row)
    {
        for (size_t i = 0; i < records.size(); ++i)
        {
            if (records[i].m_recordId == m_cloudRows[row])
            {
                applyCloudTrailing(static_cast<int>(row), records[i], nowMs);
                break;
            }
        }
    }
    //状态变了，可做的操作也可能跟着变
    updateCloudButtons();
}

const CloudRecordInfo* CRecordManagerWindow::selectedCloud() const
{
    if (m_cloudList == NULL || m_cloud == NULL)
        return NULL;
    const int index = m_cloudList->GetCurSel();
    if (index < 0 || index >= static_cast<int>(m_cloudRows.size()))
        return NULL;
    const std::vector<CloudRecordInfo>& records = m_cloud->Records();
    for (size_t i = 0; i < records.size(); ++i)
    {
        if (records[i].m_recordId == m_cloudRows[index])
            return &records[i];
    }
    return NULL;
}

void CRecordManagerWindow::updateCloudButtons()
{
    const CloudRecordInfo* info = selectedCloud();
    const int actions = (info != NULL) ? CloudRowActions(cloudStateOf(*info)) : 0;
    if (m_cloudPlay != NULL)
        m_cloudPlay->SetEnabled((actions & cloud_action_play) != 0);
    if (m_cloudDownload != NULL)
        m_cloudDownload->SetEnabled((actions & cloud_action_download) != 0);
    if (m_cloudSend != NULL)
        m_cloudSend->SetEnabled((actions & cloud_action_send) != 0);
    if (m_cloudRename != NULL)
        m_cloudRename->SetEnabled((actions & cloud_action_rename) != 0);
    if (m_cloudDelete != NULL)
        m_cloudDelete->SetEnabled((actions & cloud_action_delete) != 0);
}

void CRecordManagerWindow::doCloudAction(int action)
{
    const CloudRecordInfo* info = selectedCloud();
    if (info == NULL || m_cloud == NULL || (CloudRowActions(cloudStateOf(*info)) & action) == 0)
        return;
    const int64_t id = info->m_recordId;
    const std::wstring title = cloudTitleOf(*info);
    switch (action)
    {
    //下载到本机缓存后用系统播放器打开
    case cloud_action_play:
        m_cloud->OpenRecord(id, title);
        break;
    //保存到用户选的位置
    case cloud_action_download:
        downloadCloudSelected();
        break;
    //经宿主的会话选择窗发到会话
    case cloud_action_send:
        m_cloud->SendRecord(id, title);
        break;
    //在列表里原地编辑标题
    case cloud_action_rename:
        beginCloudRename(std::wstring());
        break;
    //确认后删除
    case cloud_action_delete:
        deleteCloudSelected();
        break;
    //重新合成本机合成失败的共享录制
    case cloud_action_recompose:
        m_cloud->Recompose(id);
        break;
    //暂停本机的合成或上传（2026-10-07，方案第 14.4 节）
    case cloud_action_pause:
        m_cloud->Pause(id);
        break;
    //继续暂停了的合成或上传
    case cloud_action_resume:
        m_cloud->Resume(id);
        break;
    //没有其他操作
    default:
        break;
    }
    LOG_INFO("record manager: cloud action 0x%x on record %lld.", action, static_cast<long long>(id));
}

void CRecordManagerWindow::beginCloudRename(const std::wstring& prefill)
{
    const CloudRecordInfo* info = selectedCloud();
    if (info == NULL || m_cloudList == NULL)
        return;
    const int index = m_cloudList->GetCurSel();
    m_cloudList->EnsureVisible(index);
    RECT rc;
    const int textLeft = kRowTextLeftPad + kRecordMgrIconPx + kRowIconTextGap;
    //输入框右边让出状态列与滚动条的命中带，不盖住状态
    const int rightInset = balloonwjui::DuiScrollBar::kOverlayBandPx + RecordManagerTrailingWidth();
    if (!RecordInlineEditRect(m_cloudList->GetRect(), index, m_cloudList->GetItemHeight(), m_cloudList->GetScrollPos(),
                              textLeft, rightInset, rc))
    {
        LOG_WARNING("record manager: cloud rename row %d not fully visible, edit not started.", index);
        return;
    }
    const std::wstring text = prefill.empty() ? utf8ToWide(info->m_title) : prefill;
    if (m_cloudList->BeginEdit(rc, text, kRecordMgrIdCloudRenameEdit) == NULL)
        return;
    m_cloudEditId = info->m_recordId;
}

void CRecordManagerWindow::commitCloudRename()
{
    if (m_cloudEditId == 0 || m_cloudList == NULL)
        return;
    const int64_t id = m_cloudEditId;
    const std::wstring input = m_cloudList->EditText();
    m_cloudEditId = 0;
    m_cloudList->EndEdit();
    const CloudRecordInfo* info = NULL;
    if (m_cloud != NULL)
    {
        const std::vector<CloudRecordInfo>& records = m_cloud->Records();
        for (size_t i = 0; i < records.size(); ++i)
        {
            if (records[i].m_recordId == id)
                info = &records[i];
        }
    }
    if (info == NULL)
    {
        reloadCloud();
        return;
    }
    std::wstring title;
    const CloudTitleResult r = NormalizeCloudTitle(utf8ToWide(info->m_title), input, title);
    if (r == cloud_title_unchanged)
    {
        reloadCloud();
        return;
    }
    if (r != cloud_title_ok)
    {
        //不合法：说明原因，再回到编辑，保留用户输入的文字以便修改
        const std::wstring tip = (r == cloud_title_empty) ? std::wstring(TR(L"标题不能为空。"))
                                                          : TRF(L"标题最多 {0} 个字。", static_cast<int>(kCloudTitleMaxChars));
        ::MessageBoxW(m_hWnd, tip.c_str(), TR(kWindowTitle), MB_OK | MB_ICONWARNING);
        for (size_t row = 0; row < m_cloudRows.size(); ++row)
        {
            if (m_cloudRows[row] == id)
                m_cloudList->SetCurSel(static_cast<int>(row), false);
        }
        beginCloudRename(input);
        return;
    }
    m_cloud->Rename(id, title);
    reloadCloud();
}

void CRecordManagerWindow::cancelCloudRename()
{
    if (m_cloudEditId == 0 || m_cloudList == NULL)
        return;
    m_cloudEditId = 0;
    m_cloudList->EndEdit();
    reloadCloud();
}

void CRecordManagerWindow::deleteCloudSelected()
{
    const CloudRecordInfo* info = selectedCloud();
    if (info == NULL || m_cloud == NULL)
        return;
    const int64_t id = info->m_recordId;
    //{0} 为标题
    const std::wstring tip = TRF(L"确定删除云端录像「{0}」吗？\n\n删除后不能恢复，发到会话里的卡片也将无法再观看。",
                                 cloudTitleOf(*info));
    if (::MessageBoxW(m_hWnd, tip.c_str(), TR(L"删除云端录像"), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        return;
    m_cloud->Delete(id);
}

void CRecordManagerWindow::downloadCloudSelected()
{
    const CloudRecordInfo* info = selectedCloud();
    if (info == NULL || m_cloud == NULL)
        return;
    const int64_t id = info->m_recordId;
    //默认文件名：标题里 Windows 不允许的字符换成下划线，扩展名按是否只有声音
    std::wstring name = cloudTitleOf(*info);
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (wcschr(kBadFileNameChars, name[i]) != NULL || name[i] < L' ')
            name[i] = kFileNameReplacement;
    }
    const wchar_t* ext = info->m_audioOnly ? L"m4a" : L"mp4";
    name += L".";
    name += ext;
    std::vector<wchar_t> path(MAX_PATH, L'\0');
    wcsncpy_s(&path[0], path.size(), name.c_str(), _TRUNCATE);
    //过滤串中间夹着 \0，说明文字各自翻译后再拼（开发约定文档「UI 约定」第 8 条）；返回值须活到对话框关闭
    const std::wstring filter = info->m_audioOnly ? filedlg::MakeFilter(TR(L"音频文件"), L"*.m4a")
                                                  : filedlg::MakeFilter(TR(L"视频文件"), L"*.mp4");
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = &path[0];
    ofn.nMaxFile = static_cast<DWORD>(path.size());
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&ofn))
        return;
    m_cloud->Download(id, std::wstring(&path[0]));
}

void CRecordManagerWindow::showCloudContextMenu()
{
    const CloudRecordInfo* info = selectedCloud();
    if (info == NULL)
        return;
    const int actions = CloudRowActions(cloudStateOf(*info));
    //菜单项的 id 直接取操作的位值（都不为 0，0 是 TrackPopup 的「没有选中」）
    balloonwjui::DuiMenu menu;
    if ((actions & cloud_action_play) != 0)
        menu.AppendItem(cloud_action_play, TR(L"播放"));
    if ((actions & cloud_action_download) != 0)
        menu.AppendItem(cloud_action_download, TR(L"下载…"));
    if ((actions & cloud_action_send) != 0)
        menu.AppendItem(cloud_action_send, TR(L"发送到会话…"));
    //暂停 / 继续（2026-10-07）：合成中、上传中可以暂停，暂停了的可以继续
    if ((actions & cloud_action_pause) != 0)
        menu.AppendItem(cloud_action_pause, TR(L"暂停"));
    if ((actions & cloud_action_resume) != 0)
        menu.AppendItem(cloud_action_resume, TR(L"继续"));
    if ((actions & cloud_action_rename) != 0)
    {
        menu.AppendSeparator();
        menu.AppendItem(cloud_action_rename, TR(L"重命名"));
    }
    if ((actions & cloud_action_delete) != 0)
        menu.AppendItem(cloud_action_delete, TR(L"删除"));
    if ((actions & cloud_action_recompose) != 0)
    {
        menu.AppendSeparator();
        menu.AppendItem(cloud_action_recompose, TR(L"重新合成"));
    }
    POINT pt;
    ::GetCursorPos(&pt);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    if (chosen != 0)
        doCloudAction(static_cast<int>(chosen));
}

void CRecordManagerWindow::uploadLocalSelected()
{
    const Row* row = selectedRow();
    if (row == NULL || !row->m_exists || m_cloud == NULL)
        return;
    const std::wstring path = row->m_path;
    const bool audioOnly = IsAudioRecordingPath(path);
    const int64_t startTime = row->m_endTimeMs - row->m_durationMs;
    const bool ok = m_cloud->UploadLocalRecording(path, audioOnly, row->m_durationMs, startTime, std::wstring());
    LOG_INFO("record manager: upload local record id=%lld to cloud, ok=%d.", static_cast<long long>(row->m_id), ok ? 1 : 0);
    if (!ok)
    {
        ::MessageBoxW(m_hWnd, TR(L"现在不能上传到云端：可能是管理员没有开通云端录制，或者连接已断开。"), TR(kWindowTitle),
                      MB_OK | MB_ICONINFORMATION);
        return;
    }
    //切到云端页看上传进度
    showPage(kRecordMgrPageCloud);
}

} // namespace screenrecord
