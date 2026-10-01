/**
 *  「消息记录」列表行实现，HistoryRow.cpp
 *  balloonwj@qq.com   2026-08-04
 */
#include "stdafx.h"
#include "HistoryRow.h"

#include <gdiplus.h>

#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 图钉的「取消 Pin」悬停提示
#include "../BalloonChatWnd/ChatIcons.h"       // kChatIconPin / DrawChatIcon
#include "../BalloonChatWnd/ChatFileVisual.h"  // FileTypeColor（与文件气泡共用配色）
#include "../BalloonChatWnd/ChatImageCache.h"  // 缩略图的后台解码与缓存
#include "../BalloonChatWnd/ImagePlaceholder.h"  // 破图 / 需要下载 / 下载中的占位绘制（与聊天气泡共用）

namespace balloonhistory {

//往 path 里加一个圆角矩形。GDI+ 没有现成的圆角矩形，四个角各一段 90° 弧拼起来。
static void AddRoundRectPath(Gdiplus::GraphicsPath& path, const RECT& rc, int radius)
{
    const Gdiplus::REAL x = (Gdiplus::REAL)rc.left;
    const Gdiplus::REAL y = (Gdiplus::REAL)rc.top;
    const Gdiplus::REAL w = (Gdiplus::REAL)(rc.right - rc.left);
    const Gdiplus::REAL h = (Gdiplus::REAL)(rc.bottom - rc.top);
    //半径夹到短边的一半，设过大也不会画烂。
    Gdiplus::REAL r = (Gdiplus::REAL)radius;
    const Gdiplus::REAL half = ((w < h) ? w : h) / 2.0f;
    if (r > half)
    {
        r = half;
    }
    if (r <= 0.0f)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }

    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

//图钉默认色：暖琥珀，与聊天气泡上那枚同色，两处一眼能对上是同一件事。
static const COLORREF kDefaultPinColor = RGB(0xE6, 0x8A, 0x2E);

//图钉描边宽，以 ChatIcons 的 viewBox 24 为基准。
static const Gdiplus::REAL kPinStrokeWidth = 1.6f;

//鼠标停在图钉上时的提示文字：点击图钉就是取消 Pin（与聊天气泡上那枚的提示一致）。
static const wchar_t* const kPinMarkTooltip = L"取消 Pin";

HistoryPinMark::HistoryPinMark()
    : m_color(kDefaultPinColor)
{
    //鼠标进入本控件时 DuiHost 会为已登记的控件自动弹出提示，不必自己处理悬停。
    balloonwjui::DuiToolTipMgr::Inst().Register(this, kPinMarkTooltip);
}

bool HistoryPinMark::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

bool HistoryPinMark::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    return true;
}

void HistoryPinMark::SetIconColor(COLORREF color)
{
    m_color = color;
    Invalidate();
}

void HistoryPinMark::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT rc = GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    //按短边取正方形并居中，免得控件被拉扁时图钉跟着变形。
    const int side = (w < h) ? w : h;
    const Gdiplus::REAL x = (Gdiplus::REAL)(rc.left + (w - side) / 2);
    const Gdiplus::REAL y = (Gdiplus::REAL)(rc.top + (h - side) / 2);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    balloonchatwnd::DrawChatIcon(
        g, balloonchatwnd::kChatIconPin, x, y, (Gdiplus::REAL)side,
        Gdiplus::Color(255, GetRValue(m_color), GetGValue(m_color), GetBValue(m_color)),
        kPinStrokeWidth);
}

//默认悬停底色：极淡的暖灰，与列表白底拉开一点点层次即可。太重会让鼠标划过时
//整屏跟着闪，比不加还糟。
static const COLORREF kDefaultHoverBg = RGB(0xF6, 0xF4, 0xF0);
//定位高亮的底色：淡黄，与搜索命中的高亮同一色系，一眼看出是"翻到的那一条"
static const COLORREF kLocatedBg = RGB(0xFF, 0xF4, 0xCC);

// ---- HistoryRowThumb ------------------------------------------------------

//缩略图 / 色块的圆角半径。
static const int kThumbCorner = 6;
//未命中缓存时的占位块底色，与聊天气泡的破图占位同一个颜色。
static const COLORREF kThumbPlaceholderBg = balloonchatwnd::kImagePlaceholderBg;
//类型色块里那几个字的字号（磅）。扩展名一般 2~4 个字符，小一号才塞得下。
static const int kThumbExtPointSize = 8;

//占位图标所在方框的边长（像素），即设计稿里缩略图的大小。预览图比它大时，图标按这个
//大小画在预览图中央，不随之放大（见 HistoryRowThumb::OnPaint）。
static const float kThumbGlyphDesignSide = 36.0f;

//以下比例都相对上面那个方框的边长（设计稿里是 36 像素），见
//snapshots/history_image_download_placeholder_design.png。
//破图图标的宽度：36 像素下为 24。
static const float kThumbBrokenGlyphRatio = 24.0f / 36.0f;
//"需要下载 / 下载中"里照片图标的宽度：36 像素下为 22，给右下角的标记让出位置。
static const float kThumbPhotoGlyphRatio = 22.0f / 36.0f;
//照片图标的中心点（横纵相同）：36 像素下为 15.5，略偏左上。
static const float kThumbPhotoCenterRatio = 15.5f / 36.0f;
//右下角下载标记 / 加载圆环的圆心（横纵相同）：36 像素下为 26.5。
static const float kThumbBadgeCenterRatio = 26.5f / 36.0f;
//右下角下载标记 / 加载圆环的直径：36 像素下为 13。
static const float kThumbBadgeDiameterRatio = 13.0f / 36.0f;

//铺一块圆角底色。
static void FillThumbBg(Gdiplus::Graphics& g, const RECT& rc, COLORREF color)
{
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                                             GetBValue(color)));
    Gdiplus::GraphicsPath path;
    AddRoundRectPath(path, rc, kThumbCorner);
    g.FillPath(&brush, &path);
}

HistoryRowThumb::HistoryRowThumb()
    : m_kind(kNone)
    , m_imageState(balloonchatwnd::kHistoryThumbImage)
    , m_clickable(false)
    , m_hot(false)
{
}

void HistoryRowThumb::SetImage(const std::wstring& path, int state, bool clickable)
{
    m_kind       = kImage;
    m_path       = path;
    m_imageState = state;
    m_clickable  = clickable;
    if (!clickable)
    {
        m_hot = false;   //不可点击就不显示悬停态
    }
    Invalidate();
}

int HistoryRowThumb::GetImageState() const
{
    return (m_kind == kImage) ? m_imageState : (int)balloonchatwnd::kHistoryThumbImage;
}

bool HistoryRowThumb::IsClickable() const
{
    return m_kind == kImage && m_clickable;
}

void HistoryRowThumb::SetFile(const CString& ext)
{
    m_kind      = kFile;
    m_ext       = ext;
    m_clickable = false;
    m_hot       = false;
    Invalidate();
}

bool HistoryRowThumb::OnMouseEnter()
{
    if (!IsClickable())
    {
        return balloonwjui::DuiControl::OnMouseEnter();
    }
    m_hot = true;
    Invalidate();
    return true;
}

bool HistoryRowThumb::OnMouseLeave()
{
    if (!IsClickable())
    {
        return balloonwjui::DuiControl::OnMouseLeave();
    }
    m_hot = false;
    Invalidate();
    return true;
}

bool HistoryRowThumb::OnSetCursor(POINT /*pt*/)
{
    if (!IsClickable())
    {
        return false;
    }
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

bool HistoryRowThumb::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    const RECT rc = GetRect();
    if (!IsClickable() || !::PtInRect(&rc, pt))
    {
        return false;   //不可点击：什么都不发，免得窗口把它当成一次下载请求
    }
    NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

bool HistoryRowThumb::OnLButtonDblClk(POINT pt, UINT mkFlags)
{
    //转交给所在的行：行会以自己的 ctrlId 冒 DUIN_DBLCLK，窗口据此认出是哪一行。
    //若直接交给基类，冒出去的是缩略图的 ctrlId，窗口认不出，双击就没有反应。
    //
    //要沿父链往上找行，不能只看直接父控件：图片预览与文件卡片都放在昵称下方的内容列
    //里（文件色块外面还多一层卡片），直接父控件是那一列或那张卡片，不是行。
    for (balloonwjui::DuiControl* node = GetParent(); node != NULL; node = node->GetParent())
    {
        HistoryRow* row = dynamic_cast<HistoryRow*>(node);
        if (row != NULL)
        {
            return row->OnLButtonDblClk(pt, mkFlags);
        }
    }
    return balloonwjui::DuiControl::OnLButtonDblClk(pt, mkFlags);
}

void HistoryRowThumb::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (m_kind == kNone)
    {
        return;
    }

    const RECT rc = GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    if (m_kind == kFile)
    {
        //类型色块：圆角矩形铺底 + 居中写扩展名。配色与聊天窗的文件气泡共用
        //（见 ChatFileVisual.h），同一个 .xlsx 在两处必须是同一个绿。
        const COLORREF bg = balloonchatwnd::FileTypeColor(m_ext);
        Gdiplus::SolidBrush brush(
            Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
        Gdiplus::GraphicsPath path;
        AddRoundRectPath(path, rc, kThumbCorner);
        g.FillPath(&brush, &path);

        if (!m_ext.IsEmpty())
        {
            //文字用 GDI 画：与列表其余文字同一套字体渲染，不必再引一套 GDI+ 字体。
            const int oldBk = ::SetBkMode(hdc, TRANSPARENT);
            const COLORREF oldClr = ::SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
            HFONT font = ::CreateFont(-::MulDiv(kThumbExtPointSize, 96, 72), 0, 0, 0,
                                      FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, _T("微软雅黑"));
            HFONT oldFont = (HFONT)::SelectObject(hdc, font);
            RECT textRc = rc;
            ::DrawText(hdc, m_ext, -1, &textRc,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
            ::SelectObject(hdc, oldFont);
            ::DeleteObject(font);
            ::SetTextColor(hdc, oldClr);
            ::SetBkMode(hdc, oldBk);
        }
        return;
    }

    //图片占位里的图标先在一个设计稿大小（36 像素）的方框里按比例排好，再把方框居中放进
    //控件。预览图显示在昵称下方、可达 200×150，图标若随之放大会显得笨重，故方框不随
    //控件放大；控件比方框还小时（很扁的小图）方框按短边缩小。
    const float shortSide   = (float)((w < h) ? w : h);
    const float side        = (shortSide < kThumbGlyphDesignSide) ? shortSide : kThumbGlyphDesignSide;
    const float boxLeft     = (float)rc.left + ((float)w - side) / 2.0f;
    const float boxTop      = (float)rc.top + ((float)h - side) / 2.0f;
    const float photoCx     = boxLeft + side * kThumbPhotoCenterRatio;
    const float photoCy     = boxTop + side * kThumbPhotoCenterRatio;
    const float badgeCx     = boxLeft + side * kThumbBadgeCenterRatio;
    const float badgeCy     = boxTop + side * kThumbBadgeCenterRatio;
    const float badgeD      = side * kThumbBadgeDiameterRatio;
    const float centerX     = (float)rc.left + (float)w / 2.0f;
    const float centerY     = (float)rc.top + (float)h / 2.0f;
    const float brokenSize  = side * kThumbBrokenGlyphRatio;
    //可点击且鼠标悬停时底色深一档，提示"这里能点"。
    const COLORREF bg = (m_clickable && m_hot) ? balloonchatwnd::kImagePlaceholderHoverBg
                                               : kThumbPlaceholderBg;

    switch (m_imageState)
    {
    //需要下载：灰色照片图标 + 右下角蓝色下载标记
    case balloonchatwnd::kHistoryThumbNeedDownload:
        FillThumbBg(g, rc, bg);
        balloonchatwnd::DrawPhotoGlyph(g, photoCx, photoCy, side * kThumbPhotoGlyphRatio,
                                       balloonchatwnd::kImagePlaceholderGlyphStroke);
        balloonchatwnd::DrawDownloadBadge(g, badgeCx, badgeCy, badgeD,
                                          m_hot ? balloonchatwnd::kImagePlaceholderBadgeBlueHover
                                                : balloonchatwnd::kImagePlaceholderBadgeBlue);
        return;

    //下载中：照片图标变淡，右下角换成加载圆环；此时不可点击
    case balloonchatwnd::kHistoryThumbDownloading:
        FillThumbBg(g, rc, kThumbPlaceholderBg);
        balloonchatwnd::DrawPhotoGlyph(g, photoCx, photoCy, side * kThumbPhotoGlyphRatio,
                                       balloonchatwnd::kImagePlaceholderGlyphFaded);
        balloonchatwnd::DrawDownloadingBadge(g, badgeCx, badgeCy, badgeD);
        return;

    //破图：下载失败（仍可单击重试时悬停底色加深）、文件损坏或缺少下载信息
    case balloonchatwnd::kHistoryThumbBroken:
        FillThumbBg(g, rc, bg);
        balloonchatwnd::DrawBrokenImageGlyph(g, centerX, centerY, brokenSize,
                                             balloonchatwnd::kImagePlaceholderGlyphStroke);
        return;

    //显示真实缩略图：交给下面的缓存查询
    default:
        break;
    }

    //图片：命中缓存就画缩放好的图，未命中画占位块并请求后台解码。
    //缓存键含展示尺寸，故这里传的宽高要与实际绘制尺寸一致。
    if (!m_path.empty())
    {
        Gdiplus::Bitmap* bmp = balloonchatwnd::ChatImageCache::Inst().Get(m_path, w, h);
        if (bmp != NULL)
        {
            Gdiplus::GraphicsPath clipPath;
            AddRoundRectPath(clipPath, rc, kThumbCorner);
            g.SetClip(&clipPath);
            g.DrawImage(bmp, rc.left, rc.top, w, h);
            g.ResetClip();
            return;
        }
    }

    FillThumbBg(g, rc, kThumbPlaceholderBg);
    if (m_path.empty())
    {
        return;   //调试入口没有登录态、或消息缺少 MD5：算不出缓存路径，只铺底色
    }

    //本地原图解码失败（文件损坏）：画破图。窗口判定状态时并不解码，所以这一情形只能
    //在绘制时按缓存的失败记录来认。
    if (balloonchatwnd::ChatImageCache::Inst().IsFailed(m_path, w, h))
    {
        balloonchatwnd::DrawBrokenImageGlyph(g, centerX, centerY, brokenSize,
                                             balloonchatwnd::kImagePlaceholderGlyphStroke);
        return;
    }

    //请求后台解码。已缓存 / 进行中 / 已记下失败的都会被内部去重，重复调用无害。
    balloonchatwnd::ChatImageCache::Inst().Request(m_path, w, h);
}

// ---- HistoryRow -----------------------------------------------------------

HistoryRow::HistoryRow()
    : m_hoverBg(kDefaultHoverBg)
    , m_hovered(false)
    , m_located(false)
{
}

void HistoryRow::AddHoverOnlyControl(balloonwjui::DuiControl* ctrl)
{
    if (ctrl == NULL)
    {
        return;
    }
    m_hoverOnly.push_back(ctrl);
    //登记即隐藏：鼠标还没进来，这枚按钮就不该出现。
    ctrl->SetVisible(false);
}

void HistoryRow::SetHoverBgColor(COLORREF color)
{
    m_hoverBg = color;
    Invalidate();
}

void HistoryRow::SetHoverControlsVisible(bool visible)
{
    for (size_t i = 0; i < m_hoverOnly.size(); ++i)
    {
        if (m_hoverOnly[i] == NULL)
        {
            continue;
        }
        m_hoverOnly[i]->SetVisible(visible);

        //改完可见性还要把它<u>所在的那个占位容器</u>重新布局一次，否则按钮由隐藏转为
        //可见后拿不到矩形、画不出来（只是"逻辑上可见"）。
        //
        //为什么不能指望本行 ForceLayout 一路级联下去：DuiLayout 给子控件定位走的是
        //DuiControl::SetRect，而它在矩形<u>没变</u>时会提前 return、连 Layout 都不调。
        //占位容器一直可见、宽度也固定，它的矩形恰恰是不变的 —— 于是级联就在这里断了。
        //ForceLayout 不做这个相等短路，正好用来打通这一层。
        balloonwjui::DuiControl* slot = m_hoverOnly[i]->GetParent();
        if (slot != NULL)
        {
            slot->ForceLayout(slot->GetRect());
        }
    }
}

void HistoryRow::SetHovered(bool hovered)
{
    if (m_hovered == hovered)
    {
        return;
    }
    m_hovered = hovered;
    SetHoverControlsVisible(hovered);
    //切完可见性必须重排一次：DuiLayout 布局时跳过不可见子控件，不重排的话按钮
    //由隐藏转为可见后拿不到矩形，画不出来（只是"逻辑上可见"）。
    ForceLayout(GetRect());
    Invalidate();
}

void HistoryRow::SetLocated(bool located)
{
    if (m_located == located)
    {
        return;
    }
    m_located = located;
    Invalidate();
}

void HistoryRow::OnPaint(HDC hdc, const RECT& rcDirty)
{
    //定位高亮优先于悬停底色：鼠标正好停在翻到的那一行上时，仍要看得出是它
    if (m_located || m_hovered)
    {
        const RECT rc = GetRect();
        HBRUSH brush = ::CreateSolidBrush(m_located ? kLocatedBg : m_hoverBg);
        ::FillRect(hdc, &rc, brush);
        ::DeleteObject(brush);
    }
    //底色铺完再交基类画子控件；反过来的话底色会把刚画好的文字盖掉。
    balloonwjui::DuiHBox::OnPaint(hdc, rcDirty);
}

bool HistoryRow::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    //双击整行：图片行打开看图窗，其余类型定位到聊天窗（由宿主窗按消息类型决定）。
    //本行不认识"会话是哪个""要跳到哪条消息"，故只把"第几行被双击了"冒给宿主窗
    //（DuiNotify 带的是本行的 ctrlId），由它去做。
    //宿主窗须调过 DuiHost::EnableDoubleClick(true)，否则这个虚函数根本不会被调到。
    NotifyParent(balloonwjui::DUIN_DBLCLK);
    return true;
}

} // namespace balloonhistory
