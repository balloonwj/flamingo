/**
 *  屏幕共享的「选择要共享的内容」窗口，SharePickerDialog.cpp
 *  说明见 SharePickerDialog.h；第 12 期加的密码与刷新规则见 SharePickerPolicy.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "SharePickerDialog.h"

#include <memory>
#include "ShareMessages.h"
#include "ShareRoomPolicy.h"   // 共享标题的默认值与校验（第 15 期）
#include "../Caption/RecordCaptionLiveRules.h"   // RecordCaptionHasSource：「生成字幕」的提示（语音字幕）
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏程序图标，第 12 期）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTab.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：勾选项按字宽算宽度（2026-10-05）

namespace screenshare {

namespace {

//窗口尺寸（整窗，含标题栏；DuiFrameWindow 的 ResizeClient 设的就是整窗）与标题栏高
const int kDialogW = 740;
const int kDialogH = 560;
const int kTitleBarH = 32;
//内容区内边距与行间距
const int kPadding = 16;
const int kGap = 10;
//标签条、提示行、按钮行的高
const int kTabH = 32;
const int kHintH = 20;
const int kButtonRowH = 32;
//按钮宽
const int kCancelW = 80;
const int kOkW = 104;
//「同时开启麦克风」「同时共享电脑声音」两个勾选项的宽（带圆角边框，再窄文字会被截掉；麦克风那项 140 时文字贴边，
//第 13 期放宽到 144）
const int kMicCheckW = 144;
const int kSysAudioCheckW = 160;
//「设置密码」勾选项与 6 位数字密码框的宽（第 12 期）。第 13 期起发起共享时底部为两行：第一行 144 + 160 + 92 + 140
//加间距共 566，第二行 90 + 70 + 80 + 104 加间距共 384，都放得进内容区宽 740 - 2 * 16 = 708
const int kPasswordCheckW = 90;
const int kPasswordEditW = 70;
//「强制模式」「禁止观看者录制」两个勾选项的宽（第 13 期）
const int kForcedCheckW = 92;
const int kRecordBlockCheckW = 140;
//页签行右侧「刷新」按钮的宽（第 12 期）
const int kRefreshW = 64;
//标题栏左侧程序图标的边长（像素）与标题栏底色：图标按这个底色合成，须与 DuiFrameTitleBar 的填充色一致
const int kTitleIconSize = 16;
const COLORREF kTitleBarBgColor = RGB(245, 246, 250);
//网格列数，格子里缩略图的尺寸、名称行高与格子外边距
const int kGridCols = 3;
const int kThumbW = 208;
const int kThumbH = 117;
const int kNameH = 20;
const int kCellMargin = 6;
const int kCellGap = 4;
//一格占的宽度（缩略图加左右外边距），窗口变宽时按它算列数（2026-10-05）
const int kGridCellPitch = kThumbW + 2 * kCellMargin;
//一行格子的高度（缩略图、间距、名称行、上下外边距）
const int kGridRowH = kThumbH + kCellGap + kNameH + 2 * kCellMargin;
//勾选项除文字以外占的宽度（方框、方框与文字的间距、两侧留白）：文字宽度加这个数才放得下（125% 缩放下字变宽，
//2026-10-05 联调发现「摄像头画中画」被截字）
const int kCheckTextExtra = 28;
//最多列出的窗口数（每个都要取缩略图，太多会让窗口打开得很慢）
const size_t kMaxWindows = 30;
//控件 id：标签条、确定、取消；格子按钮按页从 kIdTileBase 起编
const UINT kIdTabs = 10;
const UINT kIdOk = 11;
const UINT kIdCancel = 12;
//勾选项与录制模式的「框选区域」按钮
const UINT kIdMicCheck = 13;
const UINT kIdSysAudioCheck = 14;
const UINT kIdCameraCheck = 15;
const UINT kIdRegion = 16;
//第 12 期：「刷新」按钮、「设置密码」勾选项、密码框
const UINT kIdRefresh = 17;
const UINT kIdPasswordCheck = 18;
const UINT kIdPasswordEdit = 19;
//第 13 期：「强制模式」「禁止观看者录制」勾选项
const UINT kIdForcedCheck = 20;
const UINT kIdRecordBlockCheck = 21;
//第 15 期：共享标题框、描述框
const UINT kIdTitleEdit = 22;
const UINT kIdDescEdit = 23;
//第 21 期：录制模式的「白板」按钮
const UINT kIdWhiteboard = 24;
//语音字幕：录制模式的「生成字幕」勾选项与它后面的提示文字（26 已作废，原「为电脑声音生成字幕」）
const UINT kIdCaptionCheck = 25;
const UINT kIdCaptionHint = 28;
//第 27 期：录制模式的「保存到云端」勾选项，最小宽度（中文文字宽加勾选框，checkWidth 按实际文字再放宽）
const UINT kIdCloudSave = 29;
const int kRecCloudCheckW = 100;
//企业没有开通云端录制时「保存到云端」的悬停说明。balloonui 的提示框只画单行，文字须写得紧凑
const TCHAR* const kCloudSaveOffTip = TR_NOOP(L"管理员没有开通云端录制");
//标题与描述一行（第 15 期）：「标题」「描述」两个文字的宽，两个输入框按 2 : 3 分剩下的宽度
const int kInfoLabelW = 36;
const int kTitleEditWeight = 2;
const int kDescEditWeight = 3;
//「强制模式」勾选项的悬停提示，说明勾选后对观看者的效果（第 13 期）。balloonui 的提示框只画单行，文字须写得紧凑
const TCHAR* const kForcedTip = TR_NOOP(L"观看者加入后只能全屏观看、不能中途退出，切到别的程序会被拉回，直到你结束共享");
//录制模式的勾选项与「框选区域」按钮的宽
const int kRecMicCheckW = 110;
const int kRecSysAudioCheckW = 124;
const int kRecCameraCheckW = 124;
//语音字幕「生成字幕」勾选项的最小宽度（中文文字宽加勾选框，checkWidth 按实际文字再放宽）
const int kRecCaptionCheckW = 96;
//发起共享时「为我的发言生成字幕」勾选项的最小宽度（2026-10-07，同上按实际文字再放宽）
const int kShareCaptionCheckW = 150;
//「没有录制声音，不会生成字幕」提示文字的颜色（橙色，与「强制模式」等提醒一致的醒目色）
const COLORREF kCaptionHintColor = RGB(230, 120, 20);
const int kRegionW = 96;
//录制模式的「白板」按钮的宽（第 21 期）
const int kWhiteboardW = 72;
//录制模式的「只录声音」按钮的设计宽度与控件 id（第 24 期）；译文较长时按文字加宽
const int kAudioOnlyW = 96;
const UINT kIdAudioOnly = 27;
const int kIdTileBase = 100;
//缩略图底色、悬停框色、选中框色（品牌蓝）与框粗细
const COLORREF kTileBg = RGB(236, 238, 242);
const COLORREF kHoverFrame = RGB(160, 174, 196);
const COLORREF kSelectedFrame = RGB(45, 108, 223);
const int kHoverFrameW = 1;
const int kSelectedFrameW = 3;
//占位文字颜色（已最小化的窗口取不到缩略图）
const COLORREF kPlaceholderText = RGB(120, 124, 132);
//顶部提示文字的颜色（橙红，提醒发起人重新选择）
const COLORREF kHintTextColor = RGB(200, 80, 40);
//32 位 DIB 的位深
const WORD kBitsPerPixel32 = 32;

HBITMAP createDib(int w, int h)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    return ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
}

//合成一张格子背景：底色 + 居中的缩略图（没有缩略图时写占位文字）+ 可选的边框
HBITMAP composeTile(HBITMAP thumb, const wchar_t* placeholder, COLORREF frameColor, int frameWidth)
{
    HBITMAP tile = createDib(kThumbW, kThumbH);
    if (tile == NULL)
        return NULL;
    HDC dc = ::CreateCompatibleDC(NULL);
    HGDIOBJ old = ::SelectObject(dc, tile);
    RECT rc = { 0, 0, kThumbW, kThumbH };
    HBRUSH bg = ::CreateSolidBrush(kTileBg);
    ::FillRect(dc, &rc, bg);
    ::DeleteObject(bg);

    BITMAP bm;
    if (thumb != NULL && ::GetObject(thumb, sizeof(bm), &bm) == sizeof(bm))
    {
        HDC src = ::CreateCompatibleDC(NULL);
        HGDIOBJ oldSrc = ::SelectObject(src, thumb);
        const int x = (kThumbW - bm.bmWidth) / 2;
        const int y = (kThumbH - bm.bmHeight) / 2;
        ::BitBlt(dc, x, y, bm.bmWidth, bm.bmHeight, src, 0, 0, SRCCOPY);
        ::SelectObject(src, oldSrc);
        ::DeleteDC(src);
    }
    else if (placeholder != NULL)
    {
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, kPlaceholderText);
        HGDIOBJ oldFont = ::SelectObject(dc, ::GetStockObject(DEFAULT_GUI_FONT));
        ::DrawTextW(dc, placeholder, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::SelectObject(dc, oldFont);
    }

    if (frameWidth > 0)
    {
        HBRUSH frame = ::CreateSolidBrush(frameColor);
        for (int i = 0; i < frameWidth; ++i)
        {
            RECT f = { i, i, kThumbW - i, kThumbH - i };
            ::FrameRect(dc, &f, frame);
        }
        ::DeleteObject(frame);
    }
    ::SelectObject(dc, old);
    ::DeleteDC(dc);
    //按钮按预乘透明度绘制背景位图，而上面的 GDI 绘制不写透明通道（保持 0），不置为不透明的话整个格子画出来是透明的
    MakeDibOpaque(tile);
    return tile;
}

} // namespace

namespace {

//勾选项的宽度：按文字在默认字体下的实际宽度加方框占的宽度，不小于设计宽度（2026-10-05）
int checkWidth(const wchar_t* text, int designWidth)
{
    const int need = i18n::MeasureUiTextWidth(text, i18n::DefaultFontPixelHeight(), false) + kCheckTextExtra;
    return need > designWidth ? need : designWidth;
}

} // namespace

CSharePickerDialog::Item::Item()
    : m_normal(NULL)
    , m_hover(NULL)
    , m_selected(NULL)
    , m_button(NULL)
{
}

CSharePickerDialog::CSharePickerDialog()
    : m_notifyWnd(NULL)
    , m_switchMode(false)
    , m_monitorCount(0)
    , m_selected(-1)
    , m_page(0)
    , m_okButton(NULL)
    , m_micCheck(NULL)
    , m_sysAudioCheck(NULL)
    , m_mode(share_picker_start)
    , m_cameraCheck(NULL)
    , m_captionCheck(NULL)
    , m_captionHint(NULL)
    , m_audioOnlyBtn(NULL)
    , m_offerCaption(false)
    , m_captionDefault(false)
    , m_cloudCheck(NULL)
    , m_cloudAvailable(false)
    , m_passwordCheck(NULL)
    , m_passwordEdit(NULL)
    , m_forcedCheck(NULL)
    , m_recordBlockCheck(NULL)
    , m_titleEdit(NULL)
    , m_descEdit(NULL)
    , m_tabs(NULL)
    , m_titleIcon(NULL)
    , m_owner(NULL)
    , m_gridCols(kGridCols)
{
    m_pages[0] = NULL;
    m_pages[1] = NULL;
    m_grids[0] = NULL;
    m_grids[1] = NULL;
}

CSharePickerDialog::~CSharePickerDialog()
{
    CloseDialog();
    if (m_titleIcon != NULL)
    {
        ::DeleteObject(m_titleIcon);
        m_titleIcon = NULL;
    }
}

int SharePickerDialogHeight(int mode)
{
    //发起共享时底部多一行选项（第 13 期）、多一行标题与描述（第 15 期，通话中发起也有），录制时勾选项单独一行
    //（2026-10-05），每多一行窗口加高一行
    int dialogH = kDialogH;
    if (SharePickerOffersForced(mode) || mode == share_picker_record)
        dialogH += kButtonRowH + kGap;
    if (SharePickerOffersInfo(mode))
        dialogH += kButtonRowH + kGap;
    return dialogH;
}

int SharePickerDialogWidth()
{
    return kDialogW;
}

void CSharePickerDialog::EnsureWindow(HWND owner)
{
    const bool created = !IsWindow();
    if (created)
    {
        //有属主的窗口默认不进任务栏；可以最小化之后（2026-10-05）要有任务栏按钮，否则最小化了找不回来
        if (Create(owner, CWindow::rcDefault, TR(L"选择要共享的内容"), WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW) == NULL)
        {
            LOG_ERROR("screenshare: create picker dialog failed.");
            return;
        }
        SetTitleBarHeight(kTitleBarH);
        //可最小化、最大化、拖动改大小（2026-10-05 用户要求），最小为设计尺寸
        SetButtons(true, true, true);
        SetResizable(true);
        m_taskIcons.ApplyTaskIcons(m_hWnd);
        //标题栏左侧的程序图标（第 12 期）。位图只建一次，跨「关闭再打开」复用；本窗口有属主，不在任务栏上出现，
        //所以不另设任务栏图标
        if (m_titleIcon == NULL)
            m_titleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBgColor);
        if (m_titleIcon != NULL)
            SetIcon(m_titleIcon);
    }
    //标题、最小尺寸随用途变化（2026-10-07 修正：窗口已经开着、换了用途时也要重新设置，原先只在新建时设置一次）
    SetTitle(m_mode == share_picker_record ? TR(L"选择要录制的内容") : TR(L"选择要共享的内容"));
    const int dialogH = SharePickerDialogHeight(m_mode);
    SetMinSize(kDialogW, dialogH);
    if (!created)
    {
        //已经开着：用户可能改过大小，只在低于新的最小尺寸时撑大
        RECT rc;
        GetWindowRect(&rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        if (w < kDialogW || h < dialogH)
            ResizeClient(w > kDialogW ? w : kDialogW, h > dialogH ? h : dialogH);
        return;
    }
    ResizeClient(kDialogW, dialogH);

    //居中在属主所在的显示器上
    HMONITOR mon = ::MonitorFromWindow(owner != NULL ? owner : m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(mon, &mi))
    {
        const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - kDialogW) / 2;
        const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - dialogH) / 2;
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void CSharePickerDialog::freeItems()
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_normal != NULL)
            ::DeleteObject(m_items[i].m_normal);
        if (m_items[i].m_hover != NULL)
            ::DeleteObject(m_items[i].m_hover);
        if (m_items[i].m_selected != NULL)
            ::DeleteObject(m_items[i].m_selected);
    }
    m_items.clear();
    m_monitorCount = 0;
}

void CSharePickerDialog::loadItems()
{
    freeItems();
    std::vector<ShareMonitor> monitors;
    EnumerateShareMonitors(monitors);
    for (size_t i = 0; i < monitors.size(); ++i)
    {
        Item item;
        item.m_source.m_kind = share_source_monitor;
        item.m_source.m_adapter = monitors[i].m_adapter;
        item.m_source.m_output = monitors[i].m_output;
        item.m_source.m_monitorRect = monitors[i].m_rect;
        CString name;
        name.Format(monitors[i].m_primary ? TR(L"屏幕 %u（主屏幕）") : TR(L"屏幕 %u"), static_cast<unsigned>(i + 1));
        item.m_name = name.GetString();
        HBITMAP thumb = CaptureMonitorThumbnail(monitors[i].m_rect, kThumbW, kThumbH);
        item.m_normal = composeTile(thumb, NULL, 0, 0);
        item.m_hover = composeTile(thumb, NULL, kHoverFrame, kHoverFrameW);
        item.m_selected = composeTile(thumb, NULL, kSelectedFrame, kSelectedFrameW);
        if (thumb != NULL)
            ::DeleteObject(thumb);
        m_items.push_back(item);
    }
    m_monitorCount = m_items.size();

    std::vector<ShareWindow> windows;
    EnumerateShareWindows(windows);
    for (size_t i = 0; i < windows.size() && i < kMaxWindows; ++i)
    {
        Item item;
        item.m_source.m_kind = share_source_window;
        item.m_source.m_hwnd = windows[i].m_hwnd;
        item.m_name = windows[i].m_title;
        HBITMAP thumb = CaptureWindowThumbnail(windows[i].m_hwnd, kThumbW, kThumbH);
        const wchar_t* placeholder = windows[i].m_minimized ? TR(L"已最小化") : TR(L"无法预览");
        item.m_normal = composeTile(thumb, placeholder, 0, 0);
        item.m_hover = composeTile(thumb, placeholder, kHoverFrame, kHoverFrameW);
        item.m_selected = composeTile(thumb, placeholder, kSelectedFrame, kSelectedFrameW);
        if (thumb != NULL)
            ::DeleteObject(thumb);
        m_items.push_back(item);
    }
    LOG_INFO("screenshare picker: %u monitor(s), %u window(s).", static_cast<unsigned>(m_monitorCount),
             static_cast<unsigned>(m_items.size() - m_monitorCount));
}

std::unique_ptr<balloonwjui::DuiScrollView> CSharePickerDialog::buildPage(size_t first, size_t count, int ctrlIdBase,
                                                                         balloonwjui::DuiGrid** gridOut)
{
    const int cols = m_gridCols > 0 ? m_gridCols : kGridCols;
    const int rows = static_cast<int>((count + cols - 1) / cols);
    std::unique_ptr<balloonwjui::DuiGrid> grid(new balloonwjui::DuiGrid());
    grid->SetGrid(rows > 0 ? rows : 1, cols);
    if (gridOut != NULL)
        *gridOut = grid.get();
    for (size_t i = 0; i < count; ++i)
    {
        Item& item = m_items[first + i];
        std::unique_ptr<balloonwjui::DuiVBox> cell(new balloonwjui::DuiVBox());
        cell->SetGap(kCellGap);
        std::unique_ptr<balloonwjui::DuiButton> tile(new balloonwjui::DuiButton());
        tile->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        tile->SetBgBitmap(item.m_normal, item.m_hover, item.m_hover, item.m_normal);
        tile->SetBgInsets(0, 0, 0, 0);
        tile->SetCtrlId(static_cast<UINT>(ctrlIdBase + static_cast<int>(i)));
        item.m_button = tile.get();
        cell->AddChild(std::move(tile), balloonwjui::DuiLayout::Hint().Fixed(kThumbH));
        std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
        name->SetText(item.m_name.c_str());
        name->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        cell->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Fixed(kNameH));
        grid->AddChild(std::move(cell), balloonwjui::DuiLayout::Hint().Margin(kCellMargin, kCellMargin, kCellMargin,
                                                                              kCellMargin));
    }
    std::unique_ptr<balloonwjui::DuiScrollView> view(new balloonwjui::DuiScrollView());
    // 滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
    view->SetContentHeight(rows * kGridRowH);
    view->SetContent(std::move(grid));
    return view;
}

void CSharePickerDialog::BuildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);

    //页签行：左边「屏幕 / 窗口」页签，右边「刷新」（第 12 期：选择窗打开之后才打开的窗口，点刷新后列出来）
    std::unique_ptr<balloonwjui::DuiHBox> tabRow(new balloonwjui::DuiHBox());
    tabRow->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiTab> tabs(new balloonwjui::DuiTab());
    tabs->AddTab(TR(L"屏幕"));
    tabs->AddTab(TR(L"窗口"));
    tabs->SetCtrlId(kIdTabs);
    m_tabs = tabs.get();
    tabRow->AddChild(std::move(tabs), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> refreshButton(new balloonwjui::DuiButton());
    refreshButton->SetText(TR(L"刷新"));
    refreshButton->SetVariant(balloonwjui::DuiButton::Variant::Default);
    refreshButton->SetCtrlId(kIdRefresh);
    tabRow->AddChild(std::move(refreshButton), balloonwjui::DuiLayout::Hint().Fixed(kRefreshW));
    root->AddChild(std::move(tabRow), balloonwjui::DuiLayout::Hint().Fixed(kTabH));

    if (!m_hint.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
        hint->SetText(m_hint.c_str());
        hint->SetTextColor(kHintTextColor);
        root->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(kHintH));
    }

    std::unique_ptr<balloonwjui::DuiScrollView> screens = buildPage(0, m_monitorCount, kIdTileBase, &m_grids[0]);
    std::unique_ptr<balloonwjui::DuiScrollView> windows =
        buildPage(m_monitorCount, m_items.size() - m_monitorCount, kIdTileBase + static_cast<int>(m_monitorCount),
                  &m_grids[1]);
    m_pages[0] = screens.get();
    m_pages[1] = windows.get();
    root->AddChild(std::move(screens), balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(std::move(windows), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->SetGap(kGap);
    //发起共享时选项放不进一行（第 13 期）：麦克风、电脑声音、强制模式、禁止观看者录制单独一行，密码留在按钮那一行
    std::unique_ptr<balloonwjui::DuiHBox> options;
    //录制的三个勾选项也单独一行（2026-10-05：与四个按钮放一行时超出窗口宽度）
    if (SharePickerOffersForced(m_mode) || m_mode == share_picker_record)
    {
        options.reset(new balloonwjui::DuiHBox());
        options->SetGap(kGap);
    }
    balloonwjui::DuiHBox* optionRow = options ? options.get() : buttons.get();
    //发起时可以同时打开麦克风、共享电脑声音（docs/屏幕共享与录制方案.md 5.2 第 1 步，默认都不勾）；切换内容时没有这两项
    m_micCheck = NULL;
    m_sysAudioCheck = NULL;
    m_cameraCheck = NULL;
    m_captionCheck = NULL;
    m_captionHint = NULL;
    m_audioOnlyBtn = NULL;
    m_cloudCheck = NULL;
    m_passwordCheck = NULL;
    m_passwordEdit = NULL;
    m_forcedCheck = NULL;
    m_recordBlockCheck = NULL;
    //共享标题与描述（第 15 期）：只有发起共享（含通话中发起）时提供；标题默认「本人显示名的屏幕共享」
    m_titleEdit = NULL;
    m_descEdit = NULL;
    std::unique_ptr<balloonwjui::DuiHBox> infoRow;
    if (SharePickerOffersInfo(m_mode))
    {
        infoRow.reset(new balloonwjui::DuiHBox());
        infoRow->SetGap(kGap);
        std::unique_ptr<balloonwjui::DuiLabel> titleLabel(new balloonwjui::DuiLabel());
        titleLabel->SetText(TR(L"标题"));
        titleLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        infoRow->AddChild(std::move(titleLabel), balloonwjui::DuiLayout::Hint().Fixed(kInfoLabelW));
        std::unique_ptr<balloonwjui::DuiEdit> titleEdit(new balloonwjui::DuiEdit());
        titleEdit->SetCtrlId(kIdTitleEdit);
        titleEdit->SetMaxLength(static_cast<int>(kShareTitleMaxChars));
        titleEdit->SetVerticalCenter(true);
        titleEdit->SetShowBorder(true);
        titleEdit->SetPlaceholder(TR(L"共享标题（必填）"));
        titleEdit->SetTextNoNotify(MakeDefaultShareTitle(m_selfName).c_str());
        m_titleEdit = titleEdit.get();
        infoRow->AddChild(std::move(titleEdit), balloonwjui::DuiLayout::Hint().Weight(kTitleEditWeight));
        std::unique_ptr<balloonwjui::DuiLabel> descLabel(new balloonwjui::DuiLabel());
        descLabel->SetText(TR(L"描述"));
        descLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        infoRow->AddChild(std::move(descLabel), balloonwjui::DuiLayout::Hint().Fixed(kInfoLabelW));
        std::unique_ptr<balloonwjui::DuiEdit> descEdit(new balloonwjui::DuiEdit());
        descEdit->SetCtrlId(kIdDescEdit);
        descEdit->SetMaxLength(static_cast<int>(kShareDescMaxChars));
        descEdit->SetVerticalCenter(true);
        descEdit->SetShowBorder(true);
        descEdit->SetPlaceholder(TR(L"描述（选填）"));
        m_descEdit = descEdit.get();
        infoRow->AddChild(std::move(descEdit), balloonwjui::DuiLayout::Hint().Weight(kDescEditWeight));
    }
    if (m_mode == share_picker_record)
    {
        //录制：麦克风、电脑声音、摄像头画中画（docs/屏幕共享与录制方案.md 6.2），另有「框选区域」
        std::unique_ptr<balloonwjui::DuiButton> mic(new balloonwjui::DuiButton());
        mic->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        mic->SetText(TR(L"录制麦克风"));
        mic->SetCtrlId(kIdMicCheck);
        m_micCheck = mic.get();
        optionRow->AddChild(std::move(mic), balloonwjui::DuiLayout::Hint().Fixed(checkWidth(TR(L"录制麦克风"),
                                                                                             kRecMicCheckW)));
        std::unique_ptr<balloonwjui::DuiButton> sysAudio(new balloonwjui::DuiButton());
        sysAudio->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        sysAudio->SetText(TR(L"录制电脑声音"));
        sysAudio->SetCtrlId(kIdSysAudioCheck);
        sysAudio->SetCheck(true, false);
        m_sysAudioCheck = sysAudio.get();
        optionRow->AddChild(std::move(sysAudio), balloonwjui::DuiLayout::Hint().Fixed(
                                                     checkWidth(TR(L"录制电脑声音"), kRecSysAudioCheckW)));
        std::unique_ptr<balloonwjui::DuiButton> camera(new balloonwjui::DuiButton());
        camera->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        camera->SetText(TR(L"摄像头画中画"));
        camera->SetCtrlId(kIdCameraCheck);
        m_cameraCheck = camera.get();
        optionRow->AddChild(std::move(camera), balloonwjui::DuiLayout::Hint().Fixed(
                                                   checkWidth(TR(L"摄像头画中画"), kRecCameraCheckW)));
        //语音字幕（docs/语音字幕方案.md 6.1）：字幕功能可用时才提供，默认值按账号记住上次的选择（D3）
        if (m_offerCaption)
        {
            std::unique_ptr<balloonwjui::DuiButton> caption(new balloonwjui::DuiButton());
            caption->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
            caption->SetText(TR(L"生成字幕"));
            caption->SetCtrlId(kIdCaptionCheck);
            caption->SetCheck(m_captionDefault, false);
            m_captionCheck = caption.get();
            optionRow->AddChild(std::move(caption), balloonwjui::DuiLayout::Hint().Fixed(
                                                        checkWidth(TR(L"生成字幕"), kRecCaptionCheckW)));
            //「生成字幕」同时识别麦克风与电脑声音（2026-10-06 起）；两路声音都没勾时，后面这行文字提示不会生成字幕。
            //文字常驻、没有提示时为空，免得显隐变化后要重排（开发约定文档 UI 约定第 5 条）
            std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
            hint->SetCtrlId(kIdCaptionHint);
            hint->SetTextColor(kCaptionHintColor);
            hint->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            m_captionHint = hint.get();
            optionRow->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Weight(1));
        }
    }
    else if (!m_switchMode)
    {
        //通话中发起：双方的声音经由通话传输，房间里不另开麦克风
        if (m_mode != share_picker_start_in_call)
        {
            std::unique_ptr<balloonwjui::DuiButton> mic(new balloonwjui::DuiButton());
            mic->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
            mic->SetText(TR(L"同时开启麦克风"));
            mic->SetCtrlId(kIdMicCheck);
            m_micCheck = mic.get();
            optionRow->AddChild(std::move(mic), balloonwjui::DuiLayout::Hint().Fixed(
                                                    checkWidth(TR(L"同时开启麦克风"), kMicCheckW)));
        }
        std::unique_ptr<balloonwjui::DuiButton> sysAudio(new balloonwjui::DuiButton());
        sysAudio->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        sysAudio->SetText(TR(L"同时共享电脑声音"));
        sysAudio->SetCtrlId(kIdSysAudioCheck);
        m_sysAudioCheck = sysAudio.get();
        optionRow->AddChild(std::move(sysAudio), balloonwjui::DuiLayout::Hint().Fixed(
                                                     checkWidth(TR(L"同时共享电脑声音"), kSysAudioCheckW)));
        //强制模式与禁止观看者录制（第 13 期）：只有发起共享时提供，默认都不勾
        if (SharePickerOffersForced(m_mode))
        {
            std::unique_ptr<balloonwjui::DuiButton> forced(new balloonwjui::DuiButton());
            forced->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
            forced->SetText(TR(L"强制模式"));
            forced->SetCtrlId(kIdForcedCheck);
            //悬停时说明强制模式的效果；控件析构时 DuiControl 的析构函数会自动取消注册，重建界面与关窗都不用另外处理
            balloonwjui::DuiToolTipMgr::Inst().Register(forced.get(), TR(kForcedTip));
            m_forcedCheck = forced.get();
            optionRow->AddChild(std::move(forced), balloonwjui::DuiLayout::Hint().Fixed(
                                                       checkWidth(TR(L"强制模式"), kForcedCheckW)));
        }
        if (SharePickerOffersRecordBlock(m_mode))
        {
            std::unique_ptr<balloonwjui::DuiButton> recordBlock(new balloonwjui::DuiButton());
            recordBlock->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
            recordBlock->SetText(TR(L"禁止观看者录制"));
            recordBlock->SetCtrlId(kIdRecordBlockCheck);
            m_recordBlockCheck = recordBlock.get();
            optionRow->AddChild(std::move(recordBlock), balloonwjui::DuiLayout::Hint().Fixed(
                                                            checkWidth(TR(L"禁止观看者录制"), kRecordBlockCheckW)));
        }
        //加入密码（第 12 期）：只有发起共享时提供，通话中发起不提供（通话对方本来就免密码）。勾选时自动生成，可以改
        if (SharePickerOffersPassword(m_mode))
        {
            std::unique_ptr<balloonwjui::DuiButton> pwdCheck(new balloonwjui::DuiButton());
            pwdCheck->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
            pwdCheck->SetText(TR(L"设置密码"));
            pwdCheck->SetCtrlId(kIdPasswordCheck);
            m_passwordCheck = pwdCheck.get();
            buttons->AddChild(std::move(pwdCheck), balloonwjui::DuiLayout::Hint().Fixed(kPasswordCheckW));
            std::unique_ptr<balloonwjui::DuiEdit> pwdEdit(new balloonwjui::DuiEdit());
            pwdEdit->SetCtrlId(kIdPasswordEdit);
            pwdEdit->SetMaxLength(static_cast<int>(kSharePasswordLength));
            pwdEdit->SetVerticalCenter(true);
            pwdEdit->SetShowBorder(true);
            pwdEdit->SetPlaceholder(TR(L"6 位数字"));
            pwdEdit->SetEnabled(false);
            m_passwordEdit = pwdEdit.get();
            buttons->AddChild(std::move(pwdEdit), balloonwjui::DuiLayout::Hint().Fixed(kPasswordEditW));
        }
        //为我的发言生成字幕（语音字幕，2026-10-07）：字幕功能可用时提供，默认值为上次的选择（共享字幕的偏好）；
        //放在按钮一行左侧（发起共享时在密码之后，通话中发起时在电脑声音之后），这一行还有空位，窗口不用加高
        if (m_offerCaption && SharePickerOffersCaption(m_mode))
        {
            std::unique_ptr<balloonwjui::DuiButton> caption(new balloonwjui::DuiButton());
            caption->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
            caption->SetText(TR(L"为我的发言生成字幕"));
            caption->SetCtrlId(kIdCaptionCheck);
            caption->SetCheck(m_captionDefault, false);
            m_captionCheck = caption.get();
            buttons->AddChild(std::move(caption), balloonwjui::DuiLayout::Hint().Fixed(
                                                      checkWidth(TR(L"为我的发言生成字幕"), kShareCaptionCheckW)));
        }
    }
    //保存到云端（第 27 期，docs/屏幕共享与录制方案.md 12.6.4）：放在按钮一行左端（录制模式下这里是空的）。录完后上传，
    //本机文件照常保留；默认不勾。企业没有开通云端录制时不可勾，悬停说明原因
    if (m_mode == share_picker_record)
    {
        std::unique_ptr<balloonwjui::DuiButton> cloud(new balloonwjui::DuiButton());
        cloud->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        cloud->SetText(TR(L"保存到云端"));
        cloud->SetCtrlId(kIdCloudSave);
        if (!m_cloudAvailable)
        {
            cloud->SetEnabled(false);
            //控件析构时 DuiControl 的析构函数会自动取消注册，重建界面与关窗都不用另外处理
            balloonwjui::DuiToolTipMgr::Inst().Register(cloud.get(), TR(kCloudSaveOffTip));
        }
        m_cloudCheck = cloud.get();
        buttons->AddChild(std::move(cloud), balloonwjui::DuiLayout::Hint().Fixed(
                                                checkWidth(TR(L"保存到云端"), kRecCloudCheckW)));
    }
    buttons->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                      balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
    cancel->SetText(TR(L"取消"));
    cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
    cancel->SetCtrlId(kIdCancel);
    buttons->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kCancelW));
    if (m_mode == share_picker_record)
    {
        std::unique_ptr<balloonwjui::DuiButton> region(new balloonwjui::DuiButton());
        region->SetText(TR(L"框选区域"));
        region->SetVariant(balloonwjui::DuiButton::Variant::Default);
        region->SetCtrlId(kIdRegion);
        buttons->AddChild(std::move(region), balloonwjui::DuiLayout::Hint().Fixed(kRegionW));
        //白板（第 21 期）：打开一块白板并录下它
        std::unique_ptr<balloonwjui::DuiButton> board(new balloonwjui::DuiButton());
        board->SetText(TR(L"白板"));
        board->SetVariant(balloonwjui::DuiButton::Variant::Default);
        board->SetCtrlId(kIdWhiteboard);
        buttons->AddChild(std::move(board), balloonwjui::DuiLayout::Hint().Fixed(kWhiteboardW));
        //只录声音（第 24 期）：不选画面，按「录制麦克风」「录制电脑声音」两个勾选项开始；两项都没勾时不可用
        std::unique_ptr<balloonwjui::DuiButton> audioOnly(new balloonwjui::DuiButton());
        audioOnly->SetText(TR(L"只录声音"));
        audioOnly->SetVariant(balloonwjui::DuiButton::Variant::Default);
        audioOnly->SetCtrlId(kIdAudioOnly);
        m_audioOnlyBtn = audioOnly.get();
        buttons->AddChild(std::move(audioOnly), balloonwjui::DuiLayout::Hint().Fixed(
                                                    checkWidth(TR(L"只录声音"), kAudioOnlyW)));
    }
    std::unique_ptr<balloonwjui::DuiButton> ok(new balloonwjui::DuiButton());
    const int confirmKind = SharePickerConfirmKindFor(m_mode);
    if (confirmKind == share_confirm_record)
        ok->SetText(TR(L"开始录制"));
    else if (confirmKind == share_confirm_switch)
        ok->SetText(TR(L"切换"));
    else
        ok->SetText(TR(L"开始共享"));
    ok->SetCtrlId(kIdOk);
    ok->SetEnabled(false);
    m_okButton = ok.get();
    buttons->AddChild(std::move(ok), balloonwjui::DuiLayout::Hint().Fixed(kOkW));
    //标题与描述一行在选项与按钮之上（第 15 期）
    if (infoRow)
        root->AddChild(std::move(infoRow), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    if (options)
    {
        options->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                          balloonwjui::DuiLayout::Hint().Weight(1));
        root->AddChild(std::move(options), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    }
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));

    SetClientContent(std::move(root));
    //排版之后再选中上次的页签：DuiTab 在没有排版（宽度为 0）时选中第二个页签，会按「放不下」把页签整体滚出可见区，
    //排版后也不复位，页签文字就画不出来了
    m_tabs->SetCurSel(m_page, false);
    showPage(m_page);
    updateAudioOnlyEnabled();
    updateCaptionHint();
}

void CSharePickerDialog::updateCaptionHint()
{
    if (m_captionHint == NULL)
        return;
    const bool caption = (m_captionCheck != NULL && m_captionCheck->IsChecked());
    const bool mic = (m_micCheck != NULL && m_micCheck->IsChecked());
    const bool system = (m_sysAudioCheck != NULL && m_sysAudioCheck->IsChecked());
    //勾了「生成字幕」却一路声音都没录：录像里没有可识别的声音
    const bool warn = caption && !flamingomedia::RecordCaptionHasSource(mic, system);
    m_captionHint->SetText(warn ? TR(L"没有录制声音，不会生成字幕") : L"");
    m_captionHint->Invalidate();
}

void CSharePickerDialog::updateAudioOnlyEnabled()
{
    if (m_audioOnlyBtn == NULL)
        return;
    const bool mic = (m_micCheck != NULL && m_micCheck->IsChecked());
    const bool system = (m_sysAudioCheck != NULL && m_sysAudioCheck->IsChecked());
    m_audioOnlyBtn->SetEnabled(SharePickerCanRecordAudioOnly(mic, system));
    m_audioOnlyBtn->Invalidate();
}

void CSharePickerDialog::showPage(int page)
{
    m_page = page;
    if (m_pages[0] == NULL || m_pages[1] == NULL)
        return;
    m_pages[0]->SetVisible(page == 0);
    m_pages[1]->SetVisible(page == 1);
    //SetVisible 不会重新排版，要对父容器强制排一次（开发约定文档「UI 约定」第 5 条）
    balloonwjui::DuiControl* parent = m_pages[0]->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    Invalidate();
}

void CSharePickerDialog::select(int index)
{
    if (index < 0 || index >= static_cast<int>(m_items.size()))
        return;
    if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()) && m_items[m_selected].m_button != NULL)
    {
        Item& prev = m_items[m_selected];
        prev.m_button->SetBgBitmap(prev.m_normal, prev.m_hover, prev.m_hover, prev.m_normal);
    }
    m_selected = index;
    Item& cur = m_items[index];
    if (cur.m_button != NULL)
        cur.m_button->SetBgBitmap(cur.m_selected, cur.m_selected, cur.m_selected, cur.m_selected);
    updateOkEnabled();
}

void CSharePickerDialog::updateOkEnabled()
{
    if (m_okButton == NULL)
        return;
    const bool passwordOn = (m_passwordCheck != NULL && m_passwordCheck->IsChecked());
    std::wstring password;
    if (m_passwordEdit != NULL)
        password = m_passwordEdit->GetText().GetString();
    //共享标题与描述（第 15 期）：标题不能为空、两者都不能超长
    bool infoOk = true;
    if (m_titleEdit != NULL && m_descEdit != NULL)
    {
        std::wstring title = m_titleEdit->GetText().GetString();
        std::wstring desc = m_descEdit->GetText().GetString();
        infoOk = NormalizeShareInfo(title, desc);
    }
    m_okButton->SetEnabled(infoOk && SharePickerCanSubmit(m_selected >= 0, passwordOn, password));
}

void CSharePickerDialog::onPasswordToggled(bool on)
{
    if (m_passwordEdit == NULL)
        return;
    if (on)
    {
        //勾选时生成一个新密码，发起人可以改
        m_passwordEdit->SetEnabled(true);
        m_passwordEdit->SetTextNoNotify(GenerateSharePassword().c_str());
    }
    else
    {
        m_passwordEdit->SetTextNoNotify(_T(""));
        m_passwordEdit->SetEnabled(false);
    }
    updateOkEnabled();
}

void CSharePickerDialog::onPasswordEdited()
{
    if (m_passwordEdit == NULL)
        return;
    const std::wstring text = m_passwordEdit->GetText().GetString();
    const std::wstring filtered = FilterSharePasswordInput(text);
    if (filtered != text)
    {
        //输入了非数字或超出 6 位：改回过滤后的文字，光标放到末尾以便接着输入
        m_passwordEdit->SetTextNoNotify(filtered.c_str());
        const long end = static_cast<long>(filtered.size());
        m_passwordEdit->SetSel(end, end);
    }
    updateOkEnabled();
}

void CSharePickerDialog::refresh()
{
    if (!IsWindow())
        return;
    //记下刷新前选中的内容、各勾选项与密码，重建界面后恢复
    ShareSourceKey previous;
    bool hadSelection = false;
    if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()))
    {
        const ShareSourceSpec& src = m_items[m_selected].m_source;
        previous.m_monitor = (src.m_kind == share_source_monitor);
        previous.m_adapter = src.m_adapter;
        previous.m_output = src.m_output;
        previous.m_hwnd = previous.m_monitor ? NULL : src.m_hwnd;
        hadSelection = true;
    }
    const bool micOn = (m_micCheck != NULL && m_micCheck->IsChecked());
    const bool sysAudioOn = (m_sysAudioCheck != NULL && m_sysAudioCheck->IsChecked());
    const bool cameraOn = (m_cameraCheck != NULL && m_cameraCheck->IsChecked());
    const bool captionOn = (m_captionCheck != NULL && m_captionCheck->IsChecked());
    const bool passwordOn = (m_passwordCheck != NULL && m_passwordCheck->IsChecked());
    std::wstring password;
    if (m_passwordEdit != NULL)
        password = m_passwordEdit->GetText().GetString();
    const bool forcedOn = (m_forcedCheck != NULL && m_forcedCheck->IsChecked());
    const bool recordBlockOn = (m_recordBlockCheck != NULL && m_recordBlockCheck->IsChecked());
    //已经输入的标题与描述（第 15 期）
    const bool hadInfo = (m_titleEdit != NULL && m_descEdit != NULL);
    std::wstring titleText;
    std::wstring descText;
    if (hadInfo)
    {
        titleText = m_titleEdit->GetText().GetString();
        descText = m_descEdit->GetText().GetString();
    }

    //重新列出并重建界面；m_page 不变，BuildUi 会停在原来的页签上
    m_selected = -1;
    loadItems();
    BuildUi();

    if (m_micCheck != NULL)
        m_micCheck->SetCheck(micOn, false);
    if (m_sysAudioCheck != NULL)
        m_sysAudioCheck->SetCheck(sysAudioOn, false);
    if (m_cameraCheck != NULL)
        m_cameraCheck->SetCheck(cameraOn, false);
    if (m_captionCheck != NULL)
        m_captionCheck->SetCheck(captionOn, false);
    if (m_forcedCheck != NULL)
        m_forcedCheck->SetCheck(forcedOn, false);
    if (m_recordBlockCheck != NULL)
        m_recordBlockCheck->SetCheck(recordBlockOn, false);
    //勾选项按刷新前的状态恢复之后，再判断「只录声音」能不能点（第 24 期），并刷新「生成字幕」的提示
    updateAudioOnlyEnabled();
    updateCaptionHint();
    if (hadInfo && m_titleEdit != NULL && m_descEdit != NULL)
    {
        m_titleEdit->SetTextNoNotify(titleText.c_str());
        m_descEdit->SetTextNoNotify(descText.c_str());
    }
    if (m_passwordCheck != NULL && m_passwordEdit != NULL && passwordOn)
    {
        m_passwordCheck->SetCheck(true, false);
        m_passwordEdit->SetEnabled(true);
        m_passwordEdit->SetTextNoNotify(password.c_str());
    }
    if (hadSelection)
    {
        std::vector<ShareSourceKey> keys;
        keys.reserve(m_items.size());
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            ShareSourceKey key;
            key.m_monitor = (m_items[i].m_source.m_kind == share_source_monitor);
            key.m_adapter = m_items[i].m_source.m_adapter;
            key.m_output = m_items[i].m_source.m_output;
            key.m_hwnd = key.m_monitor ? NULL : m_items[i].m_source.m_hwnd;
            keys.push_back(key);
        }
        const int index = FindShareSourceAfterRefresh(previous, keys);
        if (index >= 0)
            select(index);
    }
    updateOkEnabled();
    LOG_INFO("screenshare picker: refreshed, previous selection %s.",
             !hadSelection ? "none" : (m_selected >= 0 ? "kept" : "gone"));
}

void CSharePickerDialog::Open(HWND owner, HWND notifyWnd, int mode, const std::wstring& hint,
                              const std::wstring& selfName)
{
    //已经开着、用途与提示都没变：只恢复并激活，已选的内容与已填的选项保留（2026-10-05）
    if (IsWindow() && mode == m_mode && hint == m_hint)
    {
        m_notifyWnd = notifyWnd;
        LOG_INFO("screenshare picker: already open (mode %d), brought to front.", mode);
        BringToFront();
        return;
    }
    m_notifyWnd = notifyWnd;
    m_selfName = selfName;
    m_mode = mode;
    m_switchMode = SharePickerIsSwitchLayout(mode);
    m_owner = owner;
    m_hint = hint;
    m_selected = -1;
    EnsureWindow(owner);
    if (!IsWindow())
        return;
    loadItems();
    BuildUi();
    ShowWindow(SW_SHOW);
    BringToFront();
}

void CSharePickerDialog::BringToFront()
{
    if (!IsWindow())
        return;
    //最小化了先恢复，再提到最前并设为前台窗口
    if (IsIconic())
        ShowWindow(SW_RESTORE);
    ::BringWindowToTop(m_hWnd);
    ::SetForegroundWindow(m_hWnd);
}

int CSharePickerDialog::GridColumns() const
{
    return m_gridCols;
}

void CSharePickerDialog::regridPages(int clientWidth)
{
    int cols = (clientWidth - 2 * kPadding) / kGridCellPitch;
    if (cols < 1)
        cols = 1;
    if (cols == m_gridCols)
        return;
    m_gridCols = cols;
    //两页各自按自己的格子数算行数；网格按行优先重新摆放已有的格子，不必重建
    const size_t counts[2] = { m_monitorCount, m_items.size() - m_monitorCount };
    for (int p = 0; p < 2; ++p)
    {
        if (m_grids[p] == NULL || m_pages[p] == NULL)
            continue;
        int rows = static_cast<int>((counts[p] + cols - 1) / cols);
        if (rows < 1)
            rows = 1;
        m_grids[p]->SetGrid(rows, cols);
        m_pages[p]->SetContentHeight(rows * kGridRowH);
    }
}

LRESULT CSharePickerDialog::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    //最小化时宽度为 0，不重排
    if (wParam != SIZE_MINIMIZED)
        regridPages(static_cast<int>(LOWORD(lParam)));
    //排版交给基类
    bHandled = FALSE;
    return 0;
}

void CSharePickerDialog::SetCaptionOptions(bool offer, bool captionOn)
{
    m_offerCaption = offer;
    m_captionDefault = captionOn;
}

void CSharePickerDialog::SetCloudSaveOption(bool available)
{
    m_cloudAvailable = available;
}

void CSharePickerDialog::CloseDialog()
{
    m_okButton = NULL;
    m_micCheck = NULL;
    m_sysAudioCheck = NULL;
    m_cameraCheck = NULL;
    m_captionCheck = NULL;
    m_captionHint = NULL;
    m_audioOnlyBtn = NULL;
    m_cloudCheck = NULL;
    m_passwordCheck = NULL;
    m_passwordEdit = NULL;
    m_forcedCheck = NULL;
    m_recordBlockCheck = NULL;
    m_titleEdit = NULL;
    m_descEdit = NULL;
    m_tabs = NULL;
    m_pages[0] = NULL;
    m_pages[1] = NULL;
    m_grids[0] = NULL;
    m_grids[1] = NULL;
    if (IsWindow())
        DestroyWindow();
    freeItems();
}

void CSharePickerDialog::submit(bool region, bool whiteboard, bool audioOnly)
{
    SharePickerResult* result = new SharePickerResult();
    if (audioOnly)
    {
        //只录声音（第 24 期）：不用选内容；按钮在两个声音勾选项都没勾时不可用，这里再判断一次，防止其它途径提交
        const bool mic = (m_micCheck != NULL && m_micCheck->IsChecked());
        const bool system = (m_sysAudioCheck != NULL && m_sysAudioCheck->IsChecked());
        if (!SharePickerCanRecordAudioOnly(mic, system))
        {
            delete result;
            return;
        }
        result->m_audioOnly = true;
    }
    else if (whiteboard)
    {
        //白板（第 21 期）：不用选内容，录制控制器打开一块白板并录下它
        result->m_whiteboard = true;
    }
    else if (region)
    {
        //框选区域：在选中的显示器上框选；没选显示器时用属主（主面板）所在的显示器
        int monitor = -1;
        if (m_selected >= 0 && m_selected < static_cast<int>(m_monitorCount))
            monitor = m_selected;
        if (monitor < 0 && m_monitorCount > 0)
        {
            monitor = 0;
            const HMONITOR target = ::MonitorFromWindow(m_owner != NULL ? m_owner : m_hWnd, MONITOR_DEFAULTTOPRIMARY);
            for (size_t i = 0; i < m_monitorCount; ++i)
            {
                if (::MonitorFromRect(&m_items[i].m_source.m_monitorRect, MONITOR_DEFAULTTONULL) == target)
                    monitor = static_cast<int>(i);
            }
        }
        if (monitor < 0)
        {
            delete result;
            return;
        }
        result->m_source = m_items[monitor].m_source;
        result->m_region = true;
    }
    else
    {
        if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
        {
            delete result;
            return;
        }
        result->m_source = m_items[m_selected].m_source;
    }
    HWND notify = m_notifyWnd;
    result->m_mic = (m_micCheck != NULL && m_micCheck->IsChecked());
    result->m_systemAudio = (m_sysAudioCheck != NULL && m_sysAudioCheck->IsChecked());
    result->m_camera = (m_cameraCheck != NULL && m_cameraCheck->IsChecked());
    result->m_captionOffered = (m_captionCheck != NULL);
    result->m_caption = (m_captionCheck != NULL && m_captionCheck->IsChecked());
    result->m_saveToCloud = (m_cloudCheck != NULL && m_cloudCheck->IsEnabled() && m_cloudCheck->IsChecked());
    if (m_passwordCheck != NULL && m_passwordCheck->IsChecked() && m_passwordEdit != NULL)
    {
        const std::wstring password = m_passwordEdit->GetText().GetString();
        //按钮在密码不合法时不可用，这里再判断一次，防止回车等其它途径提交
        if (!IsValidSharePassword(password))
        {
            delete result;
            return;
        }
        result->m_password = SharePasswordToUtf8(password);
    }
    result->m_forced = (m_forcedCheck != NULL && m_forcedCheck->IsChecked());
    result->m_recordBlocked = (m_recordBlockCheck != NULL && m_recordBlockCheck->IsChecked());
    //共享标题与描述（第 15 期）：按钮在不合格时不可用，这里再规整、判断一次，防止回车等其它途径提交
    if (m_titleEdit != NULL && m_descEdit != NULL)
    {
        std::wstring title = m_titleEdit->GetText().GetString();
        std::wstring desc = m_descEdit->GetText().GetString();
        if (!NormalizeShareInfo(title, desc))
        {
            delete result;
            return;
        }
        result->m_title = ShareTextToUtf8(title);
        result->m_desc = ShareTextToUtf8(desc);
    }
    LOG_INFO("screenshare picker: submit, kind %d, password %d, forced %d, record blocked %d, title %s, desc bytes %u.",
             result->m_source.m_kind, result->m_password.empty() ? 0 : 1, result->m_forced ? 1 : 0,
             result->m_recordBlocked ? 1 : 0, result->m_title.c_str(), static_cast<unsigned>(result->m_desc.size()));
    CloseDialog();
    if (notify == NULL || !::IsWindow(notify) || !::PostMessage(notify, kMsgSharePickerResult, 1,
                                                                 reinterpret_cast<LPARAM>(result)))
        delete result;
}

void CSharePickerDialog::cancel()
{
    HWND notify = m_notifyWnd;
    CloseDialog();
    if (notify != NULL && ::IsWindow(notify))
        ::PostMessage(notify, kMsgSharePickerResult, 0, 0);
}

LRESULT CSharePickerDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //切换标签页
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) && n->ctrlId == kIdTabs)
    {
        showPage(static_cast<int>(n->extra));
        return 0;
    }
    //勾选 / 取消「设置密码」（勾选框切换后先发 DUIN_VALUECHANGED，extra 为新的勾选状态）
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) && n->ctrlId == kIdPasswordCheck)
    {
        onPasswordToggled(n->extra != 0);
        return 0;
    }
    //录制模式里勾选或取消「录制麦克风」「录制电脑声音」：两项都没勾时「只录声音」不可用（第 24 期）。
    //不返回：勾选框本身的后续处理照常进行
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) &&
        (n->ctrlId == kIdMicCheck || n->ctrlId == kIdSysAudioCheck))
    {
        updateAudioOnlyEnabled();
    }
    //录制模式里勾选或取消「录制麦克风」「录制电脑声音」「生成字幕」：刷新「不会生成字幕」的提示（语音字幕）。
    //同样不返回：勾选框本身的后续处理照常进行
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) &&
        (n->ctrlId == kIdMicCheck || n->ctrlId == kIdSysAudioCheck || n->ctrlId == kIdCaptionCheck))
    {
        updateCaptionHint();
    }
    //密码框文字变了：本通知在文本引擎处理按键的中途发出，过滤留到通知处理完再做
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) && n->ctrlId == kIdPasswordEdit)
    {
        PostMessage(kMsgSharePickerDeferred, deferred_filter_password, 0);
        return 0;
    }
    //标题或描述框文字变了（第 15 期）：同样留到通知处理完再刷新按钮
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED) &&
        (n->ctrlId == kIdTitleEdit || n->ctrlId == kIdDescEdit))
    {
        PostMessage(kMsgSharePickerDeferred, deferred_info_changed, 0);
        return 0;
    }
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        if (n->ctrlId == kIdOk)
        {
            submit();
            return 0;
        }
        //「刷新」：重建界面会释放这个按钮，留到点击处理完再做
        if (n->ctrlId == kIdRefresh)
        {
            PostMessage(kMsgSharePickerDeferred, deferred_refresh, 0);
            return 0;
        }
        if (n->ctrlId == kIdCancel)
        {
            cancel();
            return 0;
        }
        //录制模式的「框选区域」
        if (n->ctrlId == kIdRegion)
        {
            submit(true);
            return 0;
        }
        //录制模式的「白板」（第 21 期）
        if (n->ctrlId == kIdWhiteboard)
        {
            submit(false, true);
            return 0;
        }
        //录制模式的「只录声音」（第 24 期）
        if (n->ctrlId == kIdAudioOnly)
        {
            submit(false, false, true);
            return 0;
        }
        const int tile = static_cast<int>(n->ctrlId) - kIdTileBase;
        if (tile >= 0 && tile < static_cast<int>(m_items.size()))
        {
            select(tile);
            return 0;
        }
    }
    //双击缩略图：等于选中后点确定
    if (n->code == static_cast<UINT>(balloonwjui::DUIN_DBLCLK))
    {
        const int tile = static_cast<int>(n->ctrlId) - kIdTileBase;
        if (tile >= 0 && tile < static_cast<int>(m_items.size()))
        {
            select(tile);
            submit();
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CSharePickerDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    cancel();
    return 0;
}

LRESULT CSharePickerDialog::OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    switch (wParam)
    {
    //点了「刷新」
    case deferred_refresh:
        refresh();
        break;

    //密码框的文字变了
    case deferred_filter_password:
        onPasswordEdited();
        break;

    //标题或描述框的文字变了（第 15 期）
    case deferred_info_changed:
        updateOkEnabled();
        break;

    //未知取值：不处理
    default:
        break;
    }
    return 0;
}

LRESULT CSharePickerDialog::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //ESC 等于取消
    if (wParam == VK_ESCAPE)
    {
        cancel();
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenshare
