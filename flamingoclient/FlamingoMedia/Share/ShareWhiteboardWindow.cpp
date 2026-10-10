/**
 *  白板窗口，见 ShareWhiteboardWindow.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareWhiteboardWindow.h"

#include <math.h>   // pow：滚轮缩放

#include "ShareMessages.h"   // kMsgShareBoardCommand（第 23 期房间模式）

#include <memory>
#include "ShareAnnotationRender.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//窗口：标题栏高、最小尺寸、占工作区的比例（千分比）
const int kTitleBarH = 36;
const int kMinW = 720;
const int kMinH = 480;
const int kWorkAreaPermille = 700;
const int kPermille = 1000;
//上方一行：高、内边距、按钮宽、间距
const int kTopRowH = 40;
const int kRowPad = 8;
const int kBgBtnW = 64;
const int kToolbarBtnW = 80;
const int kRowGap = 6;
//画布外的底色、画布底色、网格线与横线的颜色、画布边线与右下角拖动把手的颜色
const COLORREF kOutsideColor = RGB(226, 229, 234);
const COLORREF kCanvasColor = RGB(255, 255, 255);
const COLORREF kGridColor = RGB(228, 233, 241);
const COLORREF kLineColor = RGB(204, 216, 232);
const COLORREF kEdgeColor = RGB(190, 196, 206);
//右下角拖动把手的边长（像素）
const int kGripSize = 10;
//滚轮一格滚动的距离（像素）与一格的增量（WHEEL_DELTA）
const int kWheelStepPx = 48;
//提示文字的颜色
const COLORREF kHintColor = RGB(120, 128, 140);

//画一条水平或竖直的细线
void drawLine(HDC hdc, int x0, int y0, int x1, int y1, COLORREF color)
{
    HPEN pen = ::CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old = ::SelectObject(hdc, pen);
    ::MoveToEx(hdc, x0, y0, NULL);
    ::LineTo(hdc, x1, y1);
    ::SelectObject(hdc, old);
    ::DeleteObject(pen);
}

//用纯色填充矩形
void fillRect(HDC hdc, const RECT& rc, COLORREF color)
{
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(hdc, &rc, brush);
    ::DeleteObject(brush);
}

} // namespace

//==================== CShareWhiteboardCanvas ====================

CShareWhiteboardCanvas::CShareWhiteboardCanvas()
    : m_viewReady(false)
    , m_session(NULL)
    , m_input(NULL)
    , m_background(wb_bg_grid)
    , m_pressed(false)
    , m_dragEdge(wb_edge_none)
    , m_shared(false)
    , m_canvasSeat(kAnnotDefaultCanvas)
    , m_notifyWnd(NULL)
{
}

void CShareWhiteboardCanvas::SetShared(int canvas, HWND notifyWnd)
{
    m_shared = true;
    m_canvasSeat = canvas;
    m_notifyWnd = notifyWnd;
    m_viewReady = false;
    Invalidate();
}

void CShareWhiteboardCanvas::notifyViewChanged()
{
    if (m_shared && m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareBoardCommand, share_board_cmd_view, 0);
}

void CShareWhiteboardCanvas::SetAnnotation(const ShareAnnotationSession* session, IShareAnnotationInput* input)
{
    m_session = session;
    m_input = input;
    Invalidate();
}

void CShareWhiteboardCanvas::SetBackground(int background)
{
    m_background = background;
    Invalidate();
}

int CShareWhiteboardCanvas::Background() const
{
    return m_background;
}

const ShareWhiteboardView& CShareWhiteboardCanvas::View() const
{
    return m_view;
}

void CShareWhiteboardCanvas::syncViewSize()
{
    const RECT& rc = GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return;
    //第一次有尺寸时按它定下世界（之后窗口变大变小只改可见区域；房间模式按比例重算世界）
    if (!m_viewReady)
    {
        if (m_shared)
            m_view.InitShared(w, h);
        else
            m_view.Init(w, h);
        m_viewReady = true;
        notifyViewChanged();
        return;
    }
    if (w != m_view.ViewWidth() || h != m_view.ViewHeight())
    {
        m_view.SetViewSize(w, h);
        notifyViewChanged();
    }
}

POINT CShareWhiteboardCanvas::toView(POINT pt) const
{
    const RECT& rc = GetRect();
    POINT v = { pt.x - rc.left, pt.y - rc.top };
    return v;
}

bool CShareWhiteboardCanvas::inCanvas(POINT view) const
{
    const RECT canvas = m_view.CanvasRect();
    return ::PtInRect(&canvas, view) != FALSE;
}

void CShareWhiteboardCanvas::deliver(int phase, POINT pt)
{
    if (m_input == NULL || !m_viewReady)
        return;
    const POINT v = toView(pt);
    POINT screen = pt;
    if (GetHost() != NULL)
        ::ClientToScreen(GetHost()->m_hWnd, &screen);
    //橡皮的容差：kAnnotEraserTolerancePx 个像素按世界宽度换成归一化单位
    const int64_t tolerance = static_cast<int64_t>(kAnnotEraserTolerancePx) * kAnnotCoordMax / m_view.WorldWidth();
    //房间模式（第 23 期）：先告诉接收方画在这块白板上
    if (m_shared)
        m_input->OnAnnotCanvas(m_canvasSeat);
    m_input->OnAnnotPointer(phase, m_view.ViewToAnnot(v.x, v.y), tolerance < 1 ? 1 : static_cast<int>(tolerance),
                            m_view.Aspect(), screen);
}

void CShareWhiteboardCanvas::OnPaint(HDC hdc, const RECT& rcDirty)
{
    (void)rcDirty;
    syncViewSize();
    const RECT& rc = GetRect();
    fillRect(hdc, rc, kOutsideColor);
    if (!m_viewReady)
        return;
    //画布：底色、背景线、右边与下边的边线、右下角拖动把手
    RECT canvas = m_view.CanvasRect();
    ::OffsetRect(&canvas, rc.left, rc.top);
    RECT visible;
    ::IntersectRect(&visible, &canvas, &rc);
    fillRect(hdc, visible, kCanvasColor);
    std::vector<int> xs;
    std::vector<int> ys;
    m_view.BackgroundLines(m_background, xs, ys);
    const COLORREF lineColor = (m_background == wb_bg_lines) ? kLineColor : kGridColor;
    for (size_t i = 0; i < xs.size(); ++i)
        drawLine(hdc, rc.left + xs[i], visible.top, rc.left + xs[i], visible.bottom, lineColor);
    for (size_t i = 0; i < ys.size(); ++i)
        drawLine(hdc, visible.left, rc.top + ys[i], visible.right, rc.top + ys[i], lineColor);
    drawLine(hdc, canvas.right - 1, visible.top, canvas.right - 1, visible.bottom, kEdgeColor);
    drawLine(hdc, visible.left, canvas.bottom - 1, visible.right, canvas.bottom - 1, kEdgeColor);
    //右下角拖动把手：房间模式的画布就是整个世界，不能拖
    if (!m_shared)
    {
        RECT grip = { canvas.right - kGripSize, canvas.bottom - kGripSize, canvas.right, canvas.bottom };
        fillRect(hdc, grip, kEdgeColor);
    }
    //笔迹与选择提示：按世界换算，只画看得见的部分
    if (m_session == NULL)
        return;
    RECT content = m_view.ContentRect();
    ::OffsetRect(&content, rc.left, rc.top);
    //房间模式（第 23 期）：只画这块白板的笔迹，连同别人的画笔位置
    if (m_shared)
    {
        DrawSessionCanvasInView(hdc, rc, content, *m_session, m_canvasSeat, static_cast<int64_t>(::GetTickCount64()));
        return;
    }
    AnnotationStroke pending;
    const bool hasPending = m_session->PendingShape(pending);
    const AnnotSelectionVisual selection = m_session->SelectionVisual();
    DrawAnnotationsInView(hdc, rc, content, m_session->Model().Strokes(), hasPending ? &pending : NULL,
                          static_cast<int64_t>(::GetTickCount64()), &selection);
}

bool CShareWhiteboardCanvas::OnLButtonDown(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (!m_viewReady)
        return false;
    const POINT v = toView(pt);
    //画布的边：开始拖动扩展
    const int edge = m_view.HitEdge(v.x, v.y);
    if (edge != wb_edge_none)
    {
        m_dragEdge = edge;
        Capture();
        return true;
    }
    if (!inCanvas(v))
        return false;
    m_pressed = true;
    Capture();
    deliver(annot_pointer_down, pt);
    Invalidate();
    return true;
}

bool CShareWhiteboardCanvas::OnMouseMove(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (!m_viewReady)
        return false;
    //拖画布的边：画布跟着扩展或收回
    if (m_dragEdge != wb_edge_none)
    {
        const POINT v = toView(pt);
        m_view.DragEdge(m_dragEdge, v.x, v.y);
        Invalidate();
        return true;
    }
    if (m_pressed)
    {
        deliver(annot_pointer_move, pt);
        Invalidate();
        return true;
    }
    //没按着：折线、曲线的最后一点跟着走
    if (inCanvas(toView(pt)))
    {
        deliver(annot_pointer_hover, pt);
        Invalidate();
        return true;
    }
    return false;
}

bool CShareWhiteboardCanvas::OnLButtonUp(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (m_dragEdge != wb_edge_none)
    {
        m_dragEdge = wb_edge_none;
        ReleaseCapture();
        LOG_INFO("whiteboard: canvas extended to %dx%d.", m_view.CanvasWidth(), m_view.CanvasHeight());
        return true;
    }
    if (!m_pressed)
        return false;
    m_pressed = false;
    ReleaseCapture();
    deliver(annot_pointer_up, pt);
    Invalidate();
    return true;
}

bool CShareWhiteboardCanvas::OnLButtonDblClk(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (!m_viewReady || !inCanvas(toView(pt)))
        return false;
    deliver(annot_pointer_finish, pt);
    Invalidate();
    return true;
}

bool CShareWhiteboardCanvas::OnRButtonDown(POINT pt, UINT mkFlags)
{
    return OnLButtonDblClk(pt, mkFlags);
}

bool CShareWhiteboardCanvas::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    if (!m_viewReady)
        return false;
    //房间模式（第 23 期）：按住 Ctrl 以光标处为中心缩放
    if (m_shared && (mkFlags & MK_CONTROL) != 0)
    {
        const POINT v = toView(pt);
        const double factor = pow(kWhiteboardZoomStep, static_cast<double>(zDelta) / WHEEL_DELTA);
        if (m_view.SetZoom(m_view.Zoom() * factor, v.x, v.y))
        {
            Invalidate();
            notifyViewChanged();
        }
        return true;
    }
    const int step = -zDelta * kWheelStepPx / WHEEL_DELTA;
    const bool horizontal = (mkFlags & MK_SHIFT) != 0;
    if (m_view.ScrollBy(horizontal ? step : 0, horizontal ? 0 : step))
    {
        Invalidate();
        notifyViewChanged();
    }
    return true;
}

bool CShareWhiteboardCanvas::OnSetCursor(POINT pt)
{
    if (!m_viewReady)
        return false;
    const POINT v = toView(pt);
    //画布的边：左右、上下、斜向的调整光标
    const int edge = (m_dragEdge != wb_edge_none) ? m_dragEdge : m_view.HitEdge(v.x, v.y);
    if (edge != wb_edge_none)
    {
        LPCTSTR id = IDC_SIZENWSE;
        if (edge == wb_edge_right)
            id = IDC_SIZEWE;
        else if (edge == wb_edge_bottom)
            id = IDC_SIZENS;
        ::SetCursor(::LoadCursor(NULL, id));
        return true;
    }
    if (!inCanvas(v))
        return false;
    //画布上：当前工具的光标；取不到时用十字
    HCURSOR cursor = (m_input != NULL) ? m_input->AnnotCursor() : NULL;
    ::SetCursor(cursor != NULL ? cursor : ::LoadCursor(NULL, IDC_CROSS));
    return true;
}

//==================== CShareWhiteboardWindow ====================

CShareWhiteboardWindow::CShareWhiteboardWindow()
    : m_canvas(NULL)
    , m_shared(false)
    , m_notifyWnd(NULL)
    , m_sharedInput(NULL)
{
    for (int i = 0; i < kWhiteboardBackgroundCount; ++i)
        m_bgButtons[i] = NULL;
}

CShareWhiteboardWindow::~CShareWhiteboardWindow()
{
    Close();
}

bool CShareWhiteboardWindow::Open(HWND anchor)
{
    if (IsWindow())
    {
        ::SetForegroundWindow(m_hWnd);
        return true;
    }
    m_shared = false;
    if (!createFrame(anchor))
        return false;
    //本机标注：线宽按世界与可见区域的比例缩小
    if (!m_annot.Start(this, kWhiteboardWidthScale))
    {
        DestroyWindow();
        return false;
    }
    m_canvas->SetAnnotation(&m_annot.Session(), &m_annot);
    m_annot.ShowBar(windowRect());
    ::SetForegroundWindow(m_hWnd);
    return true;
}

bool CShareWhiteboardWindow::OpenShared(HWND anchor, const ShareAnnotationSession* session, IShareAnnotationInput* input,
                                        int canvas, HWND notifyWnd)
{
    if (IsWindow())
    {
        ::SetForegroundWindow(m_hWnd);
        return true;
    }
    m_shared = true;
    m_notifyWnd = notifyWnd;
    m_sharedInput = input;
    if (!createFrame(anchor))
        return false;
    m_canvas->SetShared(canvas, notifyWnd);
    m_canvas->SetAnnotation(session, input);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("whiteboard: opened as room whiteboard, canvas %d.", canvas);
    return true;
}

bool CShareWhiteboardWindow::IsOpen() const
{
    return IsWindow() != FALSE;
}

int CShareWhiteboardWindow::Background() const
{
    return (m_canvas != NULL) ? m_canvas->Background() : wb_bg_grid;
}

bool CShareWhiteboardWindow::VisibleRegion(int& left, int& top, int& width) const
{
    if (m_canvas == NULL || !IsWindow())
        return false;
    const ShareWhiteboardView& v = m_canvas->View();
    if (!v.IsShared())
        return false;
    v.VisibleRegion(left, top, width);
    return true;
}

void CShareWhiteboardWindow::Redraw()
{
    if (m_canvas != NULL)
        m_canvas->Invalidate();
}

RECT CShareWhiteboardWindow::WindowRect() const
{
    return windowRect();
}

void CShareWhiteboardWindow::postBoardCommand(int command, LPARAM value)
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareBoardCommand, static_cast<WPARAM>(command), value);
}

bool CShareWhiteboardWindow::createFrame(HWND anchor)
{
    if (Create(NULL, CWindow::rcDefault, TR(L"白板"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("whiteboard: create window failed.");
        return false;
    }
    SetTitle(TR(L"白板"));
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    //没有所有者、会出现在任务栏：任务栏与 Alt-Tab 也用程序图标
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetTitleBarHeight(kTitleBarH);
    SetButtons(true, true, true);
    SetResizable(true);
    SetMinSize(kMinW, kMinH);
    //在 anchor 所在显示器的工作区居中，约占七成
    const HWND monitorOf = (anchor != NULL && ::IsWindow(anchor)) ? anchor : NULL;
    HMONITOR mon = ::MonitorFromWindow(monitorOf, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT work;
    if (::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    int w = (work.right - work.left) * kWorkAreaPermille / kPermille;
    int h = (work.bottom - work.top) * kWorkAreaPermille / kPermille;
    w = w < kMinW ? kMinW : w;
    h = h < kMinH ? kMinH : h;
    BuildUi();
    ::SetWindowPos(m_hWnd, NULL, work.left + ((work.right - work.left) - w) / 2,
                   work.top + ((work.bottom - work.top) - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(SW_SHOW);
    UpdateWindow();
    LOG_INFO("whiteboard: opened, %dx%d.", w, h);
    return true;
}

void CShareWhiteboardWindow::Close()
{
    m_annot.Stop();
    m_canvas = NULL;
    m_shared = false;
    m_notifyWnd = NULL;
    m_sharedInput = NULL;
    for (int i = 0; i < kWhiteboardBackgroundCount; ++i)
        m_bgButtons[i] = NULL;
    if (IsWindow())
    {
        DestroyWindow();
        LOG_INFO("whiteboard: closed.");
    }
}

void CShareWhiteboardWindow::BuildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(0);
    //上方一行：背景三个按钮、「工具条」按钮、操作提示
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(kRowPad, kRowPad / 2, kRowPad, kRowPad / 2);
    row->SetGap(kRowGap);
    const wchar_t* const names[kWhiteboardBackgroundCount] = { TR_NOOP(L"纯白"), TR_NOOP(L"网格"), TR_NOOP(L"横线") };
    const UINT ids[kWhiteboardBackgroundCount] = { kIdPlain, kIdGrid, kIdLines };
    for (int i = 0; i < kWhiteboardBackgroundCount; ++i)
    {
        std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
        b->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        b->SetText(TR(names[i]));
        b->SetCtrlId(ids[i]);
        m_bgButtons[i] = b.get();
        row->AddChild(std::move(b), balloonwjui::DuiLayout::Hint().Fixed(kBgBtnW));
    }
    std::unique_ptr<balloonwjui::DuiButton> toolbar(new balloonwjui::DuiButton());
    toolbar->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    toolbar->SetText(TR(L"工具条"));
    toolbar->SetCtrlId(kIdToolbar);
    row->AddChild(std::move(toolbar), balloonwjui::DuiLayout::Hint().Fixed(kToolbarBtnW).Margin(kRowGap * 2, 0, 0, 0));
    std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
    //房间模式的画布就是整个世界，不能拖边，改为可以缩放
    hint->SetText(m_shared ? TR(L"滚轮上下滚动，按住 Shift 左右滚动，按住 Ctrl 缩放")
                           : TR(L"拖动画布的右边或下边可以扩大画布；滚轮上下滚动，按住 Shift 左右滚动"));
    hint->SetTextColor(kHintColor);
    hint->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    row->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Weight(1).Margin(kRowGap * 2, 0, 0, 0));
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kTopRowH));
    //白板画布
    std::unique_ptr<CShareWhiteboardCanvas> canvas(new CShareWhiteboardCanvas());
    m_canvas = canvas.get();
    root->AddChild(std::move(canvas), balloonwjui::DuiLayout::Hint().Weight(1));
    SetClientContent(std::move(root));
    refreshBackgroundButtons();
}

void CShareWhiteboardWindow::refreshBackgroundButtons()
{
    const int current = (m_canvas != NULL) ? m_canvas->Background() : wb_bg_grid;
    for (int i = 0; i < kWhiteboardBackgroundCount; ++i)
    {
        if (m_bgButtons[i] == NULL)
            continue;
        m_bgButtons[i]->SetVariant(i == current ? balloonwjui::DuiButton::Variant::Primary
                                                : balloonwjui::DuiButton::Variant::Default);
        m_bgButtons[i]->Invalidate();
    }
}

RECT CShareWhiteboardWindow::windowRect() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (IsWindow())
        ::GetWindowRect(m_hWnd, &rc);
    return rc;
}

void CShareWhiteboardWindow::OnLocalAnnotationsChanged()
{
    if (m_canvas != NULL)
        m_canvas->Invalidate();
}

void CShareWhiteboardWindow::OnLocalAnnotatorExit()
{
    m_annot.HideBar();
}

LRESULT CShareWhiteboardWindow::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK) || m_canvas == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    //背景：纯白
    case kIdPlain:
        m_canvas->SetBackground(wb_bg_plain);
        refreshBackgroundButtons();
        postBoardCommand(share_board_cmd_background, wb_bg_plain);
        return 0;
    //背景：网格
    case kIdGrid:
        m_canvas->SetBackground(wb_bg_grid);
        refreshBackgroundButtons();
        postBoardCommand(share_board_cmd_background, wb_bg_grid);
        return 0;
    //背景：横线
    case kIdLines:
        m_canvas->SetBackground(wb_bg_lines);
        refreshBackgroundButtons();
        postBoardCommand(share_board_cmd_background, wb_bg_lines);
        return 0;
    //重新显示标注工具条：房间模式由控制器显示它的工具条
    case kIdToolbar:
        if (m_shared)
            postBoardCommand(share_board_cmd_toolbar, 0);
        else
            m_annot.ShowBar(windowRect());
        return 0;
    //其余控件（标题栏等）交给基类
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareWhiteboardWindow::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //Delete 删除选中的对象、Esc 取消选择；没有选中对象时交给基类。房间模式交给控制器
    const bool handled = m_shared ? (m_sharedInput != NULL && m_sharedInput->OnAnnotKey(static_cast<UINT>(wParam)))
                                  : m_annot.OnAnnotKey(static_cast<UINT>(wParam));
    if (handled)
        return 0;
    bHandled = FALSE;
    return 0;
}

LRESULT CShareWhiteboardWindow::OnMoveOrSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    //标注工具条跟着窗口走；房间模式的工具条归控制器管
    if (m_shared)
        postBoardCommand(share_board_cmd_moved, 0);
    else
        m_annot.MoveBar(windowRect());
    bHandled = FALSE;
    return 0;
}

LRESULT CShareWhiteboardWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //房间模式：交给控制器（有内容时先提示保存，再关闭这块白板），窗口暂不销毁
    if (m_shared)
    {
        postBoardCommand(share_board_cmd_close, 0);
        return 0;
    }
    //关窗：录屏控制器的定时器发现被录的窗口没了，会停止并保存
    Close();
    return 0;
}

} // namespace screenshare
