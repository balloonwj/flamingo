/**
 *  ImageCanvas.cpp
 *
 *  ImageCanvas 的实现。绘制层用 GDI+：深色底 + 高质量双三次插值缩放绘制图片，
 *  旋转用 Graphics 的平移 + 旋转变换实现（绕图片中心旋转 0/90/180/270）。交互层
 *  把鼠标坐标换算成画布局部坐标后，调用 ImageViewerGeometry 纯函数算出新的缩放比
 *  与落点，再 Invalidate 重绘。
 *
 *  本 .cpp 用 Flamingo 的 stdafx.h（仅在 Flamingo.vcxproj 编译，不与测试工程共用）。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include <atldlgs.h>   // CFileDialog（另存为）。主工程的 stdafx.h 已含，单元测试工程的没有，这里显式引入

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ImageCanvas.h"
#include "ImageViewerToolbar.h"
#include "ImageViewerGeometry.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"   // 复制成功的「已复制」轻提示
#include "../BalloonChatWnd/ChatClipboardIo.h"   // WriteChatContentToClipboard（与聊天气泡右键「复制」同一套剪贴板格式）
#include "../../Common/Log/IULog.h"              // LOG_INFO / LOG_ERROR
#include "../../Common/Util/EncodeUtil.h"        // UnicodeToUtf8（日志里记路径）

namespace balloonimageviewer {

namespace {

// 画布深色背景色（接近纯黑的深灰，贴合看图器的暗色基调）。
const COLORREF kCanvasBgColor = RGB(0x1E, 0x1E, 0x1E);

// 保证进程内 GDI+ 已初始化（幂等）。ChatBubble 等处依赖进程已 init GDI+，
// 但看图窗可能经调试入口先于聊天窗出现，这里自带一道兜底，逻辑与
// DuiHost::LoadBgImageFromFile 的 lazy-init 一致：只 Startup、不 Shutdown
//（进程退出由 OS 收，避免与其它 GDI+ 用户抢生命周期）。
void EnsureGdiplusStarted()
{
    static ULONG_PTR s_token = 0;
    if (s_token == 0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&s_token, &input, NULL);
    }
}

// 把 double 四舍五入到最近的 int（半数远离零）。
int RoundToInt(double v)
{
    if (v >= 0.0)
    {
        return (int)(v + 0.5);
    }
    return (int)(v - 0.5);
}

} // anonymous namespace

ImageCanvas::ImageCanvas()
    : m_image(NULL)
    , m_imgW(0)
    , m_imgH(0)
    , m_rotation(0)
    , m_scale(1.0)
    , m_fitOnLayout(true)
    , m_dragging(false)
    , m_pressed(false)
    , m_pToolbar(NULL)
    , m_pToast(NULL)
{
    m_origin.x = 0;
    m_origin.y = 0;
    m_dragLastPt.x = 0;
    m_dragLastPt.y = 0;
    m_pressPt.x = 0;
    m_pressPt.y = 0;

    // 底部悬浮工具栏作为本画布子控件，初始隐藏（无图时不显示）。
    std::unique_ptr<ImageViewerToolbar> toolbar(new ImageViewerToolbar(this));
    m_pToolbar = toolbar.get();
    m_pToolbar->SetVisible(false);
    AddChild(std::unique_ptr<balloonwjui::DuiControl>(toolbar.release()));

    // 顶部轻提示（复制成功后显示「已复制」）。放在工具栏之后加入：基类按子控件顺序
    // 绘制，后加入的画在上层。它自己按父控件（本画布）的矩形算出「顶部居中」的落点，
    // 不需要本画布摆放；平时不可见，也不参与命中。
    std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
    m_pToast = toast.get();
    AddChild(std::unique_ptr<balloonwjui::DuiControl>(toast.release()));
}

ImageCanvas::~ImageCanvas()
{
    if (m_image != NULL)
    {
        delete m_image;
        m_image = NULL;
    }
}

bool ImageCanvas::LoadImageFromFile(LPCTSTR path)
{
    EnsureGdiplusStarted();

    // 先释放旧图。
    if (m_image != NULL)
    {
        delete m_image;
        m_image = NULL;
    }
    m_imgW = 0;
    m_imgH = 0;
    m_rotation = 0;
    m_sourcePath.Empty();
    if (m_pToolbar != NULL)
    {
        m_pToolbar->SetVisible(false);   // 无图先隐藏工具栏
    }

    if (path == NULL || path[0] == _T('\0'))
    {
        Invalidate();
        return false;
    }

    // 解码图片（与 ChatBubble 一致：不做色彩管理 useEmbeddedColorManagement=FALSE）。
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile((LPCWSTR)path, FALSE);
    if (bmp == NULL || bmp->GetLastStatus() != Gdiplus::Ok)
    {
        if (bmp != NULL)
        {
            delete bmp;
        }
        Invalidate();
        return false;
    }

    m_image = bmp;
    m_imgW = (int)bmp->GetWidth();
    m_imgH = (int)bmp->GetHeight();
    m_sourcePath = path;
    if (m_pToolbar != NULL)
    {
        m_pToolbar->SetVisible(true);    // 有图显示工具栏
    }

    // 打开即"适应窗口"居中。视口可能尚未布局（尺寸为 0），此时按 1.0 占位，
    // 等首个 Layout 到来再真正适应。
    m_fitOnLayout = true;
    SIZE vp = ViewportSize();
    if (vp.cx > 0 && vp.cy > 0)
    {
        m_scale = ComputeFitScale(DisplaySize(), vp, false);
    }
    else
    {
        m_scale = 1.0;
    }
    ClampOrigin();
    Invalidate();
    return true;
}

bool ImageCanvas::HasImage() const
{
    return m_image != NULL;
}

// ---- 看图动作 --------------------------------------------------------------

void ImageCanvas::ZoomIn()
{
    if (m_image == NULL)
    {
        return;
    }
    SIZE vp = ViewportSize();
    POINT center = { vp.cx / 2, vp.cy / 2 };
    double next = SteppedZoom(m_scale, 1, kZoomStepFactor, kMinZoomScale, kMaxZoomScale);
    ApplyZoomAtAnchor(next, center);
}

void ImageCanvas::ZoomOut()
{
    if (m_image == NULL)
    {
        return;
    }
    SIZE vp = ViewportSize();
    POINT center = { vp.cx / 2, vp.cy / 2 };
    double next = SteppedZoom(m_scale, -1, kZoomStepFactor, kMinZoomScale, kMaxZoomScale);
    ApplyZoomAtAnchor(next, center);
}

void ImageCanvas::ZoomToFit()
{
    if (m_image == NULL)
    {
        return;
    }
    SIZE vp = ViewportSize();
    if (vp.cx > 0 && vp.cy > 0)
    {
        m_scale = ComputeFitScale(DisplaySize(), vp, false);
    }
    m_fitOnLayout = true;
    ClampOrigin();   // 适应比 ≤ 1，ScaledBox ≤ 视口，落点自动居中
    Invalidate();
}

void ImageCanvas::ZoomToActual()
{
    if (m_image == NULL)
    {
        return;
    }
    SIZE vp = ViewportSize();
    POINT center = { vp.cx / 2, vp.cy / 2 };
    ApplyZoomAtAnchor(1.0, center);
}

void ImageCanvas::RotateClockwise()
{
    ApplyRotation(90);    // 顺时针 90°
}

void ImageCanvas::RotateCounterClockwise()
{
    ApplyRotation(-90);   // 逆时针 90°
}

void ImageCanvas::ApplyRotation(int deltaDegreesCW)
{
    if (m_image == NULL)
    {
        return;
    }
    // 累加旋转增量并归一到 [0,360)（deltaDegreesCW 可为负，逆时针）。
    m_rotation = ((m_rotation + deltaDegreesCW) % 360 + 360) % 360;

    // 旋转后按新朝向重新适应窗口（多数看图器的惯常行为）。
    SIZE vp = ViewportSize();
    if (vp.cx > 0 && vp.cy > 0)
    {
        m_scale = ComputeFitScale(DisplaySize(), vp, false);
    }
    m_fitOnLayout = true;
    ClampOrigin();
    Invalidate();
}

CString ImageCanvas::GetZoomReadout() const
{
    return FormatZoomReadout(m_scale);
}

// ---- 绘制 ------------------------------------------------------------------

void ImageCanvas::OnPaint(HDC hdc, const RECT& rcDirty)
{
    const RECT rc = GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);

    // 深色背景铺满整个画布。
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255,
        GetRValue(kCanvasBgColor), GetGValue(kCanvasBgColor), GetBValue(kCanvasBgColor)));
    g.FillRectangle(&bgBrush, rc.left, rc.top, w, h);

    if (m_image != NULL)
    {
        // 缩放后图片在屏矩形（画布局部坐标 + 控件偏移 = 宿主坐标）。
        const SIZE box = ScaledBoxSize();
        const double centerX = rc.left + m_origin.x + box.cx / 2.0;
        const double centerY = rc.top + m_origin.y + box.cy / 2.0;

        // 未旋转时缩放后的宽高（旋转由 Graphics 变换完成，故这里用原始宽高 × 缩放）。
        const double uw = m_imgW * m_scale;
        const double uh = m_imgH * m_scale;

        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        // 裁剪到画布矩形，避免越界（理论上落点已夹取，这里是双保险）。
        g.SetClip(Gdiplus::Rect(rc.left, rc.top, w, h));

        // 绕图片中心旋转后绘制：平移到中心 → 旋转 → 以中心为原点画缩放后的图。
        g.TranslateTransform((Gdiplus::REAL)centerX, (Gdiplus::REAL)centerY);
        g.RotateTransform((Gdiplus::REAL)m_rotation);
        g.DrawImage(m_image, Gdiplus::RectF((Gdiplus::REAL)(-uw / 2.0),
                                            (Gdiplus::REAL)(-uh / 2.0),
                                            (Gdiplus::REAL)uw,
                                            (Gdiplus::REAL)uh));
        g.ResetTransform();
        g.ResetClip();
    }

    // 绘制子控件（底部悬浮工具栏，无图时其自身不可见会被跳过）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

// ---- 鼠标交互 --------------------------------------------------------------

bool ImageCanvas::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // 记下按下点并捕获鼠标：之后按拖动的情况决定平移图片还是移动窗口（见 ResolveCanvasDrag）。
    // 底部工具栏是本画布的子控件，按在工具栏上的点击由工具栏自己消费，不会走到这里。
    m_pressed = true;
    m_pressPt = pt;
    Capture();
    // 图片放大到超出窗口：从按下起就平移，与改动前一致
    if (ResolveCanvasDrag(false, IsPannable(), pt, pt, SIZE()) == kCanvasDragPanImage)
    {
        m_dragging = true;
        m_dragLastPt = pt;
    }
    return true;
}

bool ImageCanvas::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    // 图片不可平移（能完整显示、或还没有图片）：移出系统拖动阈值后改为移动整个窗口
    if (m_pressed && !m_dragging)
    {
        SIZE threshold;
        threshold.cx = ::GetSystemMetrics(SM_CXDRAG);
        threshold.cy = ::GetSystemMetrics(SM_CYDRAG);
        if (ResolveCanvasDrag(false, IsPannable(), m_pressPt, pt, threshold) == kCanvasDragMoveWindow)
        {
            StartWindowMove(pt);
        }
        return true;
    }
    if (!m_dragging)
    {
        return false;
    }
    // 平移增量（宿主坐标差 = 画布局部坐标差，平移不改坐标系）。
    m_origin.x += pt.x - m_dragLastPt.x;
    m_origin.y += pt.y - m_dragLastPt.y;
    m_dragLastPt = pt;
    ClampOrigin();
    Invalidate();
    return true;
}

bool ImageCanvas::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    const bool wasPressed = m_pressed;
    m_pressed = false;
    if (!m_dragging)
    {
        //按下后没移出阈值就松开（单击）：释放捕获即可
        if (wasPressed)
        {
            ReleaseCapture();
            return true;
        }
        return false;
    }
    m_dragging = false;
    ReleaseCapture();
    return true;
}

void ImageCanvas::StartWindowMove(POINT ptHost)
{
    m_pressed = false;
    ReleaseCapture();
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    //系统的窗口移动要求当前没有窗口占着鼠标捕获，否则移动循环收不到鼠标消息
    ::ReleaseCapture();
    //系统的移动从当前光标位置起算，按下后到移出拖动阈值之间的那一小段位移不在其内。先把
    //窗口挪过这段距离，按下处就仍在光标下方，窗口跟手、不会慢半拍
    RECT rcWnd = { 0, 0, 0, 0 };
    if (::GetWindowRect(hwnd, &rcWnd))
    {
        ::SetWindowPos(hwnd, NULL,
                       rcWnd.left + (ptHost.x - m_pressPt.x), rcWnd.top + (ptHost.y - m_pressPt.y),
                       0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    //窗口挪过之后，当前光标在客户区里的位置正好回到按下点
    POINT screenPt = m_pressPt;
    ::ClientToScreen(hwnd, &screenPt);
    LOG_INFO("image viewer: drag on canvas starts window move, at (%ld,%ld).",
             screenPt.x, screenPt.y);
    //与拖动标题栏走同一条系统路径；移动循环在松开左键后返回，那次松开不会再送到画布
    ::SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(screenPt.x, screenPt.y));
}

bool ImageCanvas::OnLButtonDblClk(POINT pt, UINT /*mkFlags*/)
{
    if (m_image == NULL)
    {
        return false;
    }
    // 双击在"适应窗口"与"100%"之间切换：当前为适应态则放到 100%（对着光标），
    // 否则回到适应窗口。
    if (m_fitOnLayout)
    {
        ApplyZoomAtAnchor(1.0, ToLocal(pt));
    }
    else
    {
        ZoomToFit();
    }
    return true;
}

bool ImageCanvas::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    if (m_image == NULL)
    {
        return false;
    }
    const int dir = (zDelta > 0) ? 1 : -1;
    double next = SteppedZoom(m_scale, dir, kZoomStepFactor, kMinZoomScale, kMaxZoomScale);
    ApplyZoomAtAnchor(next, ToLocal(pt));
    return true;
}

bool ImageCanvas::OnSetCursor(POINT /*pt*/)
{
    // 可平移时给出"四向移动"光标提示；否则用默认箭头（返回 false 让基类处理）。
    if (IsPannable())
    {
        ::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
        return true;
    }
    return false;
}

void ImageCanvas::Layout(const RECT& rcAvail)
{
    // 调基类设置内部矩形（base 默认不重排子控件），再把工具栏摆到底部中央。
    balloonwjui::DuiControl::Layout(rcAvail);
    PositionToolbar();

    if (m_image == NULL)
    {
        return;
    }
    // 窗口尺寸变化：适应态跟随重新适应；非适应态保持缩放比、只把落点夹回。
    if (m_fitOnLayout)
    {
        SIZE vp = ViewportSize();
        if (vp.cx > 0 && vp.cy > 0)
        {
            m_scale = ComputeFitScale(DisplaySize(), vp, false);
        }
    }
    ClampOrigin();
}

// ---- 私有辅助 --------------------------------------------------------------

SIZE ImageCanvas::ViewportSize() const
{
    const RECT rc = GetRect();
    SIZE s;
    s.cx = rc.right - rc.left;
    s.cy = rc.bottom - rc.top;
    return s;
}

SIZE ImageCanvas::DisplaySize() const
{
    SIZE px;
    px.cx = m_imgW;
    px.cy = m_imgH;
    return RotatedImageSize(px, m_rotation);
}

SIZE ImageCanvas::ScaledBoxSize() const
{
    SIZE disp = DisplaySize();
    SIZE s;
    s.cx = RoundToInt(disp.cx * m_scale);
    s.cy = RoundToInt(disp.cy * m_scale);
    return s;
}

POINT ImageCanvas::ToLocal(POINT ptHost) const
{
    const RECT rc = GetRect();
    POINT p;
    p.x = ptHost.x - rc.left;
    p.y = ptHost.y - rc.top;
    return p;
}

void ImageCanvas::ClampOrigin()
{
    m_origin = ClampImageOrigin(m_origin, ScaledBoxSize(), ViewportSize());
}

void ImageCanvas::ApplyZoomAtAnchor(double newScale, POINT anchorLocal)
{
    POINT newOrigin = OriginForZoomAtAnchor(m_origin, m_scale, newScale, anchorLocal);
    m_scale = newScale;
    m_origin = newOrigin;
    // 手动缩放即脱离"适应窗口"态（窗口尺寸变化不再强制重新适应）。
    m_fitOnLayout = false;
    ClampOrigin();
    Invalidate();
}

bool ImageCanvas::IsPannable() const
{
    if (m_image == NULL)
    {
        return false;
    }
    SIZE box = ScaledBoxSize();
    SIZE vp = ViewportSize();
    return (box.cx > vp.cx) || (box.cy > vp.cy);
}

void ImageCanvas::PositionToolbar()
{
    if (m_pToolbar == NULL)
    {
        return;
    }
    // 工具栏离画布底边的间距。
    const int kBottomMargin = 24;

    SIZE bar = m_pToolbar->GetBarSize();
    const RECT rc = GetRect();
    const int vpw = rc.right - rc.left;

    int x = rc.left + (vpw - bar.cx) / 2;       // 水平居中
    int y = rc.bottom - bar.cy - kBottomMargin; // 贴底（留边距）
    RECT br;
    br.left = x;
    br.top = y;
    br.right = x + bar.cx;
    br.bottom = y + bar.cy;
    m_pToolbar->SetRect(br);
}

std::vector<balloonchatwnd::ChatClipSegment> BuildImageViewerCopySegments(const CString& sourcePath,
                                                                          bool hasImage)
{
    if (!hasImage || sourcePath.IsEmpty())
    {
        return std::vector<balloonchatwnd::ChatClipSegment>();
    }
    // 源文件必须还在：剪贴板里记的是路径，粘进输入框后发送时才去读文件，文件没了
    // 发送就会失败。目录不算。
    const DWORD attrs = ::GetFileAttributesW((LPCWSTR)sourcePath);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return std::vector<balloonchatwnd::ChatClipSegment>();
    }
    // 与聊天气泡右键「复制」一张图片时构造的片段完全相同。
    return balloonchatwnd::BuildClipSegmentsForImage(sourcePath);
}

std::vector<balloonchatwnd::ChatClipSegment> ImageCanvas::BuildCopySegments() const
{
    return BuildImageViewerCopySegments(m_sourcePath, HasImage());
}

void ImageCanvas::CopyImage()
{
    balloonwjui::DuiHost* host = GetHost();
    HWND owner = (host != NULL) ? host->m_hWnd : NULL;

    const std::vector<balloonchatwnd::ChatClipSegment> segs = BuildCopySegments();
    if (segs.empty())
    {
        // 无图时按钮本不可见；走到这里多半是源文件在打开看图窗之后被移走或删除了。
        LOG_ERROR("image viewer: copy image skipped, no image or source file missing.");
        ::MessageBox(owner, _T("复制图片失败，图片文件可能已被移动或删除。"),
                     _T("图片查看"), MB_ICONWARNING | MB_OK);
        return;
    }

    // 一次写入 Flamingo 聊天内容格式（粘进聊天输入框还原成可发送的图片）、位图（粘到画图、
    // Word 等程序）与纯文字「[图片]」，与聊天气泡右键「复制」同一个函数。
    if (!balloonchatwnd::WriteChatContentToClipboard(owner, segs))
    {
        LOG_ERROR("image viewer: copy image failed, clipboard unavailable.");
        ::MessageBox(owner, _T("复制图片失败，剪贴板可能正被其他程序占用，请重试。"),
                     _T("图片查看"), MB_ICONWARNING | MB_OK);
        return;
    }

    // 窄字符日志按 UTF-8 原样落盘，路径里可能有中文，先转成 UTF-8 再记。
    LOG_INFO("image viewer: image copied to clipboard, path=%s.",
             EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)m_sourcePath)).c_str());
    if (m_pToast != NULL)
    {
        m_pToast->Show(_T("已复制"));
    }
}

void ImageCanvas::SaveImageAs()
{
    if (m_image == NULL || m_sourcePath.IsEmpty())
    {
        return;
    }

    balloonwjui::DuiHost* host = GetHost();
    HWND owner = (host != NULL) ? host->m_hWnd : NULL;

    // 默认文件名取源文件名，默认扩展名取源扩展名。
    int slash = m_sourcePath.ReverseFind(_T('\\'));
    CString defName = (slash >= 0) ? m_sourcePath.Mid(slash + 1) : m_sourcePath;
    int dot = m_sourcePath.ReverseFind(_T('.'));
    CString defExt = (dot >= 0) ? m_sourcePath.Mid(dot + 1) : CString(_T("png"));

    CFileDialog dlg(FALSE, defExt, defName,
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_EXPLORER,
                    _T("图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0所有文件\0*.*\0\0"),
                    owner);
    if (dlg.DoModal() == IDOK)
    {
        // 按原文件字节整体拷贝：无损、保持原格式。
        if (!::CopyFile(m_sourcePath, dlg.m_szFileName, FALSE))
        {
            ::MessageBox(owner, _T("另存为失败，请重试。"),
                         _T("图片查看"), MB_ICONWARNING | MB_OK);
        }
    }
}

} // namespace balloonimageviewer
