/**
 *  录制条，说明见 RecordBar.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "RecordBar.h"
#include "../Share/ShareMouseEffectMenu.h"   // AppendMouseEffectMenu（「鼠标效果」菜单，第 16 期）

#include <memory>
#include <string>
#include "RecordMessages.h"
#include "RecordPolicy.h"
#include "../Share/ShareSources.h"
#include "../Share/ShareBarButtonArt.h"   // 图标按钮与闪烁红点（2026-10-07 与共享控制条统一样式）
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"   // 只录声音的电平条（第 24 期）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：开关两种文字取较宽的定宽度

namespace screenrecord {

namespace {

//录制条尺寸：标题栏高（只显示「录制」两个字）、内容横条高。整条宽按有哪些按钮累加（见 barWidth）
const int kBarTitleH = 18;
const int kBarContentH = 44;
//红点所占的宽
const int kDotW = 14;
//总有的图标按钮个数：暂停、停止、隐藏、收起 / 固定（后两个 2026-10-07 起）
const int kAlwaysButtons = 4;
//小把手标题栏上红点图标的位图边长（DuiFrameWindow 按 16×16 画标题栏图标）与红点的直径（像素）
const int kHandleDotBmpPx = 16;
const int kHandleDotPx = 10;
//状态文字（「录制中 mm:ss」）的最小宽度与左右留白合计（像素）：按最长的写法量出的宽度不小于它
const int kStatusMinW = 110;
const int kStatusPadPx = 8;
//量状态文字宽度时代入的时长（带小时的最长写法）
const wchar_t* const kStatusSampleDuration = L"00:00:00";
//只录声音的电平条（第 24 期）：高度、与上方开关的间距（像素），底色与填充色（绿色，与深色底区分）
const int kLevelH = 4;
const int kLevelGap = 2;
const COLORREF kLevelBgColor = RGB(70, 74, 82);
const COLORREF kLevelFillColor = RGB(76, 175, 80);
//电平条的取值范围
const int kLevelMax = 100;
//图标按钮在内容横条里上下留白（像素）；只录声音时按钮下面还有电平条，留白相应减少
const int kBtnVMargin = (kBarContentH - screenshare::kShareBarBtnPx) / 2;
const int kLevelColumnVMargin = (kBarContentH - screenshare::kShareBarBtnPx - kLevelGap - kLevelH) / 2;
//横条左右内边距、控件间距、距工作区顶部的距离
const int kRowPad = 12;
const int kRowGap = screenshare::kShareBarBtnGap;
const int kTopMargin = 8;
//录制红点闪烁的定时器 id（取一个不常见的值，避开 balloonui 宿主窗口内部可能使用的小编号定时器）
const UINT_PTR kBlinkTimerId = 0x5243;
//底色与文字色（与共享控制条一致）
const COLORREF kBarBgColor = RGB(44, 48, 56);
const COLORREF kBarTextColor = RGB(255, 255, 255);
//纯色背景图的边长
const int kSolidBitmapSize = 8;
//32 位 DIB 的位深与每像素字节数
const WORD kBitsPerPixel32 = 32;
const int kBytesPerPixel = 4;

//纯色位图
HBITMAP makeBitmap(COLORREF c, int side)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = side;
    bi.bmiHeader.biHeight = -side;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* p = static_cast<BYTE*>(bits);
    for (int i = 0; i < side * side; ++i)
    {
        p[i * kBytesPerPixel + 0] = GetBValue(c);
        p[i * kBytesPerPixel + 1] = GetGValue(c);
        p[i * kBytesPerPixel + 2] = GetRValue(c);
        p[i * kBytesPerPixel + 3] = 255;
    }
    return bmp;
}

/**
 *  底色上画一个实心圆点的位图（小把手标题栏的红点图标）。标题栏按 SRCCOPY 画图标、不做透明混合，所以直接画在条的底色上。
 *  @param dot      圆点颜色
 *  @param bg       底色
 *  @param side     位图边长（像素）
 *  @param diameter 圆点直径（像素）
 *  @return 位图（调用方持有）；失败返回 NULL
 */
HBITMAP makeDotBitmap(COLORREF dot, COLORREF bg, int side, int diameter)
{
    HBITMAP bmp = makeBitmap(bg, side);
    if (bmp == NULL)
        return NULL;
    HDC dc = ::CreateCompatibleDC(NULL);
    if (dc == NULL)
        return bmp;
    HGDIOBJ oldBmp = ::SelectObject(dc, bmp);
    HBRUSH brush = ::CreateSolidBrush(dot);
    HPEN pen = ::CreatePen(PS_SOLID, 1, dot);
    HGDIOBJ oldBrush = ::SelectObject(dc, brush);
    HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const int offset = (side - diameter) / 2;
    ::Ellipse(dc, offset, offset, offset + diameter, offset + diameter);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldBmp);
    ::DeleteObject(pen);
    ::DeleteObject(brush);
    ::DeleteDC(dc);
    ::GdiFlush();
    return bmp;
}

} // namespace

CRecordBar::CRecordBar()
    : m_notifyWnd(NULL)
    , m_label(NULL)
    , m_pauseBtn(NULL)
    , m_elapsedMs(0)
    , m_paused(false)
    , m_bgBitmap(NULL)
    , m_mouseBtn(NULL)
    , m_penBtn(NULL)
    , m_penAvailable(false)
    , m_penActive(false)
    , m_audioAvailable(false)
    , m_micOn(false)
    , m_systemOn(false)
    , m_micBtn(NULL)
    , m_systemBtn(NULL)
    , m_cameraAvailable(false)
    , m_cameraOn(false)
    , m_cameraBtn(NULL)
    , m_captionAvailable(false)
    , m_captionOn(false)
    , m_captionBtn(NULL)
    , m_audioOnly(false)
    , m_micLevel(NULL)
    , m_systemLevel(NULL)
    , m_micPercent(0)
    , m_systemPercent(0)
    , m_dot(NULL)
    , m_dotState(screenshare::share_record_dot_hidden)
    , m_stopBtn(NULL)
    , m_toggleBtn(NULL)
    , m_hideBtn(NULL)
{
    memset(&m_expandedRect, 0, sizeof(m_expandedRect));
    memset(&m_monitorRect, 0, sizeof(m_monitorRect));
    for (int i = 0; i <= screenshare::share_record_dot_paused; ++i)
        m_handleDots[i] = NULL;
}

CRecordBar::~CRecordBar()
{
    Hide();
    //对象析构时不能再推迟：Hide 在菜单弹出期间只隐藏了窗口，这里直接销毁
    if (IsWindow())
        DestroyWindow();
    if (m_bgBitmap != NULL)
        ::DeleteObject(m_bgBitmap);
    //小把手的红点图标：窗口已销毁，标题栏不再引用它们
    for (int i = 0; i <= screenshare::share_record_dot_paused; ++i)
    {
        if (m_handleDots[i] != NULL)
            ::DeleteObject(m_handleDots[i]);
        m_handleDots[i] = NULL;
    }
}

void CRecordBar::BuildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kBarBgColor);
    root->SetPadding(0);
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(kRowPad, 0, kRowPad, 0);
    row->SetGap(kRowGap);
    //左侧状态区（2026-10-07 起与共享控制条同一样式）：闪烁的红点（暂停时变灰、不闪）+「录制中 mm:ss」
    std::unique_ptr<balloonwjui::DuiLabel> dot(new balloonwjui::DuiLabel());
    dot->SetText(_T("●"));
    dot->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    m_dot = dot.get();
    //新建的红点还没上色：清掉记下的状态，随后的 refresh 按当前时刻上色
    m_dotState = screenshare::share_record_dot_hidden;
    row->AddChild(std::move(dot), balloonwjui::DuiLayout::Hint().Fixed(kDotW));
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetTextColor(kBarTextColor);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_label = label.get();
    row->AddChild(std::move(label), balloonwjui::DuiLayout::Hint().Weight(1));
    //右侧一排图标按钮，原来的按钮文字改作悬停提示（ShareBarButtonArt）
    m_pauseBtn = addIconButton(row.get(), kIdPause);
    //鼠标效果（第 16 期）：点开与共享控制条相同的开关与颜色菜单；只录声音（第 24 期）时没有画面，不提供
    m_mouseBtn = m_audioOnly ? NULL : addIconButton(row.get(), kIdMouse);
    //画笔（第 21 期）：在正在录的内容上直接画，录进视频；只有独立录屏有
    m_penBtn = m_penAvailable ? addIconButton(row.get(), kIdPen) : NULL;
    //麦克风、电脑声音开关（2026-10-05）：录制中随时打开或关闭；只有独立录屏有
    m_micBtn = NULL;
    m_systemBtn = NULL;
    m_micLevel = NULL;
    m_systemLevel = NULL;
    if (m_audioAvailable)
    {
        addAudioToggle(row.get(), kIdMic, kIdMicLevel, screenshare::kShareBarBtnPx, m_micBtn, m_micLevel);
        addAudioToggle(row.get(), kIdSystem, kIdSystemLevel, screenshare::kShareBarBtnPx, m_systemBtn, m_systemLevel);
    }
    //摄像头开关（2026-10-05）：录制中随时打开或关闭，打开后屏幕上出现可拖动的摄像头小窗；只有独立录屏有
    m_cameraBtn = m_cameraAvailable ? addIconButton(row.get(), kIdCamera) : NULL;
    //字幕开关（语音字幕 14.12）：录制中关闭后不显示、也不生成字幕；只有勾了「生成字幕」的独立录屏有
    m_captionBtn = m_captionAvailable ? addIconButton(row.get(), kIdCaption) : NULL;
    //停止：红底方块
    m_stopBtn = addIconButton(row.get(), kIdStop);
    //隐藏、收起 / 固定（2026-10-07）：放在最右端，与共享控制条一致
    m_hideBtn = addIconButton(row.get(), kIdHide);
    m_toggleBtn = addIconButton(row.get(), kIdToggle);
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
    refresh();
}

balloonwjui::DuiButton* CRecordBar::addIconButton(balloonwjui::DuiHBox* row, UINT id)
{
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetCtrlId(id);
    balloonwjui::DuiButton* raw = btn.get();
    row->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint().Fixed(screenshare::kShareBarBtnPx)
                                      .Margin(0, kBtnVMargin, 0, kBtnVMargin));
    return raw;
}

int CRecordBar::DotState() const
{
    return m_dotState;
}

void CRecordBar::TickBlink(int64_t nowMs)
{
    const int state = screenshare::ShareRecordDotStateAt(true, m_paused, nowMs);
    if (state == m_dotState && m_dot != NULL)
        return;
    m_dotState = state;
    if (m_dot != NULL)
        m_dot->SetTextColor(screenshare::ShareRecordDotColor(state, kBarBgColor));
    //收起成小把手时，把手标题栏上的红点图标跟着闪
    if (m_collapse.State() == screenshare::top_bar_collapsed)
        applyHandleLook();
}

int CRecordBar::CollapseState() const
{
    return m_collapse.State();
}

void CRecordBar::TickCollapseAt(int64_t nowMs, POINT cursor)
{
    if (!IsWindow())
        return;
    //窗口矩形在收起时就是小把手、显示整条时就是整条；隐藏时窗口不可见，不算在上面
    RECT rc = { 0, 0, 0, 0 };
    GetWindowRect(&rc);
    const bool inside = IsWindowVisible() && ::PtInRect(&rc, cursor) != FALSE;
    const RECT edge = screenshare::ComputeHiddenEdgeZone(m_expandedRect, m_monitorRect);
    const bool overEdge = ::PtInRect(&edge, cursor) != FALSE;
    if (m_collapse.OnTick(nowMs, inside, inside, overEdge))
        applyCollapse();
}

void CRecordBar::applyCollapse()
{
    if (!IsWindow())
        return;
    const int state = m_collapse.State();
    if (state == screenshare::top_bar_hidden)
    {
        //隐藏：什么都不留，定时器照常检查鼠标是否到了屏幕边缘
        ShowWindow(SW_HIDE);
    }
    else if (screenshare::TopBarShowsHandle(state))
    {
        //收起：缩成整条顶边正中的小把手，只剩标题栏（时长与红点图标）
        applyHandleLook();
        const RECT h = screenshare::ComputeTopBarHandleRect(m_expandedRect);
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, h.left, h.top, h.right - h.left, h.bottom - h.top,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    else
    {
        //整条：标题栏恢复为「录制」、不带图标
        SetIcon(NULL);
        SetTitle(TR(L"录制"));
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_expandedRect.left, m_expandedRect.top,
                       m_expandedRect.right - m_expandedRect.left, m_expandedRect.bottom - m_expandedRect.top,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    //「收起 / 固定」：临时展开与从隐藏临时显示时画「固定」
    applyArt(m_toggleBtn, screenshare::share_bar_collapse,
             state == screenshare::top_bar_peek || state == screenshare::top_bar_hidden_peek);
    //一直展开时不需要检查鼠标
    if (state == screenshare::top_bar_pinned)
        KillTimer(screenshare::kTopBarTimerId);
    else
        SetTimer(screenshare::kTopBarTimerId, screenshare::kTopBarTickMs, NULL);
}

void CRecordBar::applyHandleLook()
{
    if (!IsWindow())
        return;
    const std::wstring title = std::wstring(screenshare::kTopBarHandlePrefix) + FormatRecordDuration(m_elapsedMs);
    if (std::wstring(GetTitle().GetString()) != title)
        SetTitle(title.c_str());
    //红点的状态：刚建好还没闪过时按当前时刻算
    int dot = m_dotState;
    if (dot == screenshare::share_record_dot_hidden)
        dot = screenshare::ShareRecordDotStateAt(true, m_paused, static_cast<int64_t>(::GetTickCount64()));
    HBITMAP icon = handleDotBitmap(dot);
    if (icon != GetIcon())
        SetIcon(icon);
}

HBITMAP CRecordBar::handleDotBitmap(int dotState)
{
    if (dotState < 0 || dotState > screenshare::share_record_dot_paused)
        return NULL;
    if (m_handleDots[dotState] == NULL)
    {
        m_handleDots[dotState] = makeDotBitmap(screenshare::ShareRecordDotColor(dotState, kBarBgColor), kBarBgColor,
                                               kHandleDotBmpPx, kHandleDotPx);
    }
    return m_handleDots[dotState];
}

LRESULT CRecordBar::OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //收起 / 隐藏状态下检查鼠标位置（2026-10-07）
    if (wParam == screenshare::kTopBarTimerId)
    {
        POINT pt = { 0, 0 };
        ::GetCursorPos(&pt);
        TickCollapseAt(static_cast<int64_t>(::GetTickCount64()), pt);
        return 0;
    }
    if (wParam != kBlinkTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    TickBlink(static_cast<int64_t>(::GetTickCount64()));
    return 0;
}

void CRecordBar::addAudioToggle(balloonwjui::DuiHBox* row, UINT id, UINT levelId, int width,
                                balloonwjui::DuiButton*& btnOut, balloonwjui::DuiProgressBar*& levelOut)
{
    levelOut = NULL;
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetCtrlId(id);
    btnOut = btn.get();
    if (!m_audioOnly)
    {
        row->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint().Fixed(width).Margin(0, kBtnVMargin, 0, kBtnVMargin));
        return;
    }
    //只录声音（第 24 期）：开关在上，下面一条细电平条，两者竖排占同一列
    std::unique_ptr<balloonwjui::DuiVBox> column(new balloonwjui::DuiVBox());
    column->SetPadding(0);
    column->SetGap(kLevelGap);
    column->AddChild(std::move(btn), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiProgressBar> level(new balloonwjui::DuiProgressBar());
    level->SetCtrlId(levelId);
    level->SetRange(0, kLevelMax);
    level->SetPos(0, false);
    level->SetShowPercent(false);
    level->SetBgColor(kLevelBgColor);
    level->SetBorderColor(kLevelBgColor);
    level->SetFillColor(kLevelFillColor);
    levelOut = level.get();
    column->AddChild(std::move(level), balloonwjui::DuiLayout::Hint().Fixed(kLevelH));
    row->AddChild(std::move(column), balloonwjui::DuiLayout::Hint().Fixed(width).Margin(0, kLevelColumnVMargin, 0,
                                                                                         kLevelColumnVMargin));
}

void CRecordBar::SetAudioOnly(bool on)
{
    m_audioOnly = on;
}

bool CRecordBar::IsAudioOnly() const
{
    return m_audioOnly;
}

void CRecordBar::SetLevels(int micPercent, int systemPercent)
{
    if (micPercent != m_micPercent && m_micLevel != NULL)
        m_micLevel->SetPos(micPercent, false);
    if (systemPercent != m_systemPercent && m_systemLevel != NULL)
        m_systemLevel->SetPos(systemPercent, false);
    m_micPercent = micPercent;
    m_systemPercent = systemPercent;
}

std::wstring CRecordBar::StatusText() const
{
    if (m_label == NULL)
        return std::wstring();
    return std::wstring(m_label->GetText().GetString());
}

int CRecordBar::barWidth() const
{
    //图标按钮个数：暂停、停止总有；只录声音时没有「鼠标效果」；其余按是否提供
    int buttons = kAlwaysButtons;
    if (!m_audioOnly)
        ++buttons;
    if (m_penAvailable)
        ++buttons;
    if (m_audioAvailable)
        buttons += 2;
    if (m_cameraAvailable)
        ++buttons;
    if (m_captionAvailable)
        ++buttons;
    return 2 * kRowPad + kDotW + kRowGap + statusWidth() + buttons * (screenshare::kShareBarBtnPx + kRowGap);
}

int CRecordBar::statusWidth() const
{
    //三种写法里最长的一种（带小时的时长），非简体中文界面下按译文加宽
    const std::wstring sample(kStatusSampleDuration);
    const int px = i18n::DefaultFontPixelHeight();
    int w = i18n::MeasureUiTextWidth(TRF(L"录制中 {0}", sample).c_str(), px, false);
    const int paused = i18n::MeasureUiTextWidth(TRF(L"已暂停 {0}", sample).c_str(), px, false);
    const int audio = i18n::MeasureUiTextWidth(TRF(L"录音中 {0}", sample).c_str(), px, false);
    w = paused > w ? paused : w;
    w = audio > w ? audio : w;
    w += kStatusPadPx;
    return w > kStatusMinW ? w : kStatusMinW;
}

void CRecordBar::SetAudioToggles(bool available, bool micOn, bool systemOn)
{
    m_audioAvailable = available;
    m_micOn = micOn;
    m_systemOn = systemOn;
    refresh();
}

void CRecordBar::SetCameraToggle(bool available, bool on)
{
    m_cameraAvailable = available;
    m_cameraOn = on;
    refresh();
}

void CRecordBar::SetCaptionToggle(bool available, bool on)
{
    m_captionAvailable = available;
    m_captionOn = on;
    refresh();
}

void CRecordBar::SetPenAvailable(bool available)
{
    m_penAvailable = available;
}

void CRecordBar::SetPenActive(bool active)
{
    m_penActive = active;
    //画笔模式开着时按钮为蓝底
    if (m_penBtn != NULL)
        m_art.Apply(m_penBtn, screenshare::ShareBarLookFor(screenshare::share_bar_annotate, active),
                    screenshare::ShareBarTooltipFor(screenshare::share_bar_annotate, active, true));
}

void CRecordBar::BringToTop()
{
    if (IsWindow())
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CRecordBar::SetMouseEffects(const screenshare::ShareMouseEffectSettings& settings)
{
    m_mouseSettings = settings;
}

void CRecordBar::showMouseMenu()
{
    if (m_mouseBtn == NULL || m_notifyWnd == NULL || !::IsWindow(m_notifyWnd))
        return;
    balloonwjui::DuiMenu menu;
    std::vector<std::unique_ptr<balloonwjui::DuiMenu> > subMenus;
    std::vector<std::unique_ptr<CImageEx> > icons;   //颜色项的图标，菜单关闭之后才释放
    screenshare::AppendMouseEffectMenu(menu, screenshare::BuildMouseEffectMenu(m_mouseSettings, kMenuMouseBase), subMenus, icons);
    //允许截图（2026-10-08）：本机的录制条与画笔工具条能否被截图，只对这一次录制有效
    menu.AppendSeparator();
    menu.AppendChecked(kMenuAllowCapture, TR(L"允许截图"), screenshare::IsBarCaptureAllowed());
    //菜单弹在按钮下方
    const RECT& rc = m_mouseBtn->GetRect();
    POINT pt = { rc.left, rc.bottom };
    ClientToScreen(&pt);
    //记下正在弹出的菜单与子菜单：期间录制条被隐藏或点了条上的按钮时据此关掉它们
    m_popup.BeginMenu(&menu, subMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    m_popup.End();
    //菜单期间录制条被隐藏过（录制已结束）或通知窗口没了：丢弃这次选择
    if (!screenshare::ShareBarShouldDeliverChoice(chosen, m_popup.WasInterrupted(),
                                                  m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return;
    if (chosen == kMenuAllowCapture)
        ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdAllowCapture, screenshare::IsBarCaptureAllowed() ? 0 : 1);
    else if (chosen >= kMenuMouseBase && chosen < kMenuMouseBase + kMenuMouseSpan)
        ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdMouseEffect, static_cast<LPARAM>(chosen));
}

bool CRecordBar::IsPopupActive() const
{
    return m_popup.IsActive();
}

LRESULT CRecordBar::OnShowMouseMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //已经有菜单开着、窗口已隐藏（等待销毁或录制已结束）：不再弹
    if (m_popup.IsActive() || m_popup.IsPendingDestroy() || !IsWindowVisible())
        return 0;
    showMouseMenu();
    //菜单期间控制器隐藏了录制条：现在才销毁窗口（本条消息是投递来的，返回后 ATL 即清空 m_hWnd）
    if (m_popup.TakePendingDestroy())
    {
        LOG_INFO("record: record bar destroyed after its menu closed.");
        DestroyWindow();
    }
    return 0;
}

void CRecordBar::refresh()
{
    if (m_label != NULL)
    {
        //{0} 为已录制的时长；只录声音（第 24 期）时写「录音中」
        const std::wstring duration = FormatRecordDuration(m_elapsedMs);
        std::wstring text;
        if (m_paused)
            text = TRF(L"已暂停 {0}", duration);
        else if (m_audioOnly)
            text = TRF(L"录音中 {0}", duration);
        else
            text = TRF(L"录制中 {0}", duration);
        m_label->SetText(text.c_str());
    }
    //图标按钮：外观与悬停提示随状态变化（开关打开时为蓝底，关着时图标带斜杠）
    applyArt(m_pauseBtn, screenshare::share_bar_pause, m_paused);
    applyArt(m_mouseBtn, screenshare::share_bar_mouse, false);
    applyArt(m_penBtn, screenshare::share_bar_annotate, m_penActive);
    applyArt(m_micBtn, screenshare::share_bar_mic, m_micOn);
    applyArt(m_systemBtn, screenshare::share_bar_system_audio, m_systemOn);
    applyArt(m_cameraBtn, screenshare::share_bar_camera, m_cameraOn);
    applyArt(m_captionBtn, screenshare::share_bar_caption, m_captionOn);
    applyArt(m_stopBtn, screenshare::share_bar_stop_record, false);
    applyArt(m_hideBtn, screenshare::share_bar_hide, false);
    applyArt(m_toggleBtn, screenshare::share_bar_collapse,
             m_collapse.State() == screenshare::top_bar_peek || m_collapse.State() == screenshare::top_bar_hidden_peek);
    //红点：暂停时立即变灰、继续时立即恢复闪烁
    TickBlink(static_cast<int64_t>(::GetTickCount64()));
    //收起成小把手时，把手标题的时长随之刷新
    if (m_collapse.State() == screenshare::top_bar_collapsed)
        applyHandleLook();
    if (IsWindow())
        Invalidate();
}

void CRecordBar::applyArt(balloonwjui::DuiButton* button, int action, bool on)
{
    if (button == NULL)
        return;
    m_art.Apply(button, screenshare::ShareBarLookFor(action, on), screenshare::ShareBarTooltipFor(action, on, true));
}

void CRecordBar::Show(const RECT& anchor, HWND notifyWnd, int topOffset)
{
    m_notifyWnd = notifyWnd;
    //菜单弹出期间被隐藏、等待销毁的窗口：直接复用并取消推迟的销毁，控件指针已在 Hide 里清空，重建界面
    if (IsWindow() && m_popup.TakePendingDestroy())
    {
        LOG_INFO("record: record bar reused while its menu is closing.");
        BuildUi();
    }
    if (!IsWindow())
    {
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        if (Create(NULL, CWindow::rcDefault, TR(L"录制"), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("record: create record bar failed.");
            return;
        }
        SetTitle(TR(L"录制"));
        SetTitleBarHeight(kBarTitleH);
        SetButtons(false, false, false);
        SetResizable(false);
        SetBorderPx(0);
        if (m_bgBitmap == NULL)
            m_bgBitmap = makeBitmap(kBarBgColor, kSolidBitmapSize);
        if (m_bgBitmap != NULL)
        {
            RECT insets = { 0, 0, 0, 0 };
            SetBgImage(m_bgBitmap, insets);
            SetTitleBarTransparent(true);
            SetTitleTextColor(kBarTextColor);
        }
        //录制条默认不能被录进去；「允许截图」打开时（2026-10-08）允许
        screenshare::ApplyBarCaptureState(m_hWnd);
        //每次开始录制都从一直展开开始
        m_collapse.Reset();
        BuildUi();
    }
    HMONITOR mon = ::MonitorFromRect(&anchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
    {
        work = mi.rcWork;
        m_monitorRect = mi.rcMonitor;
    }
    else
    {
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        m_monitorRect = work;
    }
    const int barW = barWidth();
    const int x = work.left + ((work.right - work.left) - barW) / 2;
    m_expandedRect.left = x;
    m_expandedRect.top = work.top + kTopMargin + topOffset;
    m_expandedRect.right = x + barW;
    m_expandedRect.bottom = m_expandedRect.top + kBarTitleH + kBarContentH;
    //按当前的收起 / 隐藏状态摆放：重复调用（重新摆位）时不把用户收起或隐藏的录制条重新展开
    applyCollapse();
    //红点闪烁：按半个闪烁周期检查一次亮暗，相位由 ShareRecordDotStateAt 按时刻算出
    SetTimer(kBlinkTimerId, screenshare::kShareRecordBlinkMs / 2, NULL);
    TickBlink(static_cast<int64_t>(::GetTickCount64()));
}

void CRecordBar::Hide()
{
    //控件树随窗口一起销毁，指向其中控件的指针全部清空；漏清的指针在下一次录制时会被当作有效控件使用
    //（2026-10-05 连续录制两次崩溃：m_penBtn 没有清空，第二次开始录制时 SetPenActive 访问了已释放的按钮）
    m_label = NULL;
    m_dot = NULL;
    m_dotState = screenshare::share_record_dot_hidden;
    m_pauseBtn = NULL;
    m_mouseBtn = NULL;
    m_penBtn = NULL;
    m_micBtn = NULL;
    m_systemBtn = NULL;
    m_cameraBtn = NULL;
    m_captionBtn = NULL;
    m_stopBtn = NULL;
    m_hideBtn = NULL;
    m_toggleBtn = NULL;
    m_micLevel = NULL;
    m_systemLevel = NULL;
    m_micPercent = 0;
    m_systemPercent = 0;
    m_notifyWnd = NULL;
    //下一次录制从一直展开开始
    m_collapse.Reset();
    if (!IsWindow())
        return;
    KillTimer(kBlinkTimerId);
    KillTimer(screenshare::kTopBarTimerId);
    //标题栏不再引用小把手的红点图标
    SetIcon(NULL);
    //「鼠标效果」菜单正在弹出：菜单的嵌套消息循环还在本窗口的消息处理里，此时销毁窗口，ATL 要等那条消息返回才清 m_hWnd，
    //菜单也不会随之关闭。改为先关菜单、只隐藏窗口，等 TrackPopup 返回后由 OnShowMouseMenu 销毁
    if (screenshare::ShareBarShouldDeferDestroy(m_popup.IsActive()))
    {
        m_popup.Close();
        m_popup.SetPendingDestroy();
        ShowWindow(SW_HIDE);
        LOG_INFO("record: record bar hidden while its menu is open, destroy deferred.");
        return;
    }
    DestroyWindow();
}

void CRecordBar::SetState(int64_t elapsedMs, bool paused)
{
    //只在秒数或暂停状态变化时刷新文字，避免每次定时器都重画
    if (elapsedMs / 1000 == m_elapsedMs / 1000 && paused == m_paused)
        return;
    m_elapsedMs = elapsedMs;
    m_paused = paused;
    refresh();
}

LRESULT CRecordBar::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //菜单开着时点了录制条上的按钮：录制条不抢焦点，点它不会让菜单失去焦点，这里先关掉菜单；点的就是「鼠标效果」时
    //只关菜单，不再重新弹出
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && m_popup.IsActive())
    {
        m_popup.Close();
        if (n->ctrlId == static_cast<UINT>(kIdMouse))
            return 0;
    }
    //收起 / 固定、隐藏（2026-10-07）：只改录制条自己的摆放，不通知控制器
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdToggle))
    {
        if (m_collapse.OnToggleClicked())
            applyCollapse();
        return 0;
    }
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == static_cast<UINT>(kIdHide))
    {
        if (m_collapse.OnHideClicked())
            applyCollapse();
        return 0;
    }
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && m_notifyWnd != NULL &&
        ::IsWindow(m_notifyWnd))
    {
        //暂停 / 继续
        if (n->ctrlId == static_cast<UINT>(kIdPause))
        {
            ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, m_paused ? kCmdResume : kCmdPause, 0);
            return 0;
        }
        //麦克风开关（2026-10-05）：请求切换到相反的状态，控制器启停采集后再同步回来
        if (n->ctrlId == static_cast<UINT>(kIdMic))
        {
            ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdMic, m_micOn ? 0 : 1);
            return 0;
        }
        //摄像头开关（2026-10-05）
        if (n->ctrlId == static_cast<UINT>(kIdCamera))
        {
            ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdCamera, m_cameraOn ? 0 : 1);
            return 0;
        }
        //字幕开关（语音字幕 14.12）：请求切换到相反的状态
        if (n->ctrlId == static_cast<UINT>(kIdCaption))
        {
            ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdCaption, m_captionOn ? 0 : 1);
            return 0;
        }
        //电脑声音开关（2026-10-05）
        if (n->ctrlId == static_cast<UINT>(kIdSystem))
        {
            ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdSystemAudio, m_systemOn ? 0 : 1);
            return 0;
        }
        //画笔（第 21 期）：进入或退出画笔模式
        if (n->ctrlId == static_cast<UINT>(kIdPen))
        {
            ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdPen, 0);
            return 0;
        }
        //鼠标效果（第 16 期）：投给自己，等这次鼠标事件处理完再弹菜单
        if (n->ctrlId == static_cast<UINT>(kIdMouse))
        {
            PostMessage(kMsgShowMouseMenu, 0, 0);
            return 0;
        }
        //停止
        if (n->ctrlId == static_cast<UINT>(kIdStop))
        {
            ::PostMessage(m_notifyWnd, kMsgRecordBarCommand, kCmdStop, 0);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenrecord
