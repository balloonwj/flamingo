/**
 *  LockOverlayRenderer.cpp —— 见同名头文件说明。配色与尺寸取自样稿的 CSS（逻辑像素，乘以缩放比例）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "LockOverlayRenderer.h"

#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / UiFontName

namespace flamingolock {

namespace {

// ---- 图标 ----
const int kIconNone = -1;      // 不画图标
const int kIconLock = 0;       // 锁（解锁按钮）
const int kIconRestart = 1;    // 圆形箭头（重启按钮）
const int kIconPower = 2;      // 电源（关机按钮）
const int kIconArrow = 3;      // 向右箭头（密码框右边的确认按钮）
const float kIconViewBox = 24.0f;    // 图标路径的设计尺寸（与样稿的 SVG viewBox 一致）
const float kIconSize = 20.0f;       // 按钮里图标的边长（逻辑像素）
const float kIconStroke = 2.0f;      // 图标线宽（设计尺寸单位）
const float kIconTextGap = 9.0f;     // 图标与文字的间距（逻辑像素）

// ---- 颜色 ----
const uint32_t kGlassFill = 0x12121A;        // 按钮、输入框的半透明深色底
const float kGlassFillAlpha = 0.58f;
const float kGlassHoverAlpha = 0.72f;        // 悬停时深色底更实一些
const uint32_t kWhite = 0xFFFFFF;
const float kGlassBorderAlpha = 0.26f;       // 白色细边框的不透明度
const uint32_t kBrand = 0xF56A3C;            // 品牌橙：解锁按钮、箭头按钮、输入框获焦边框
const uint32_t kBrandHover = 0xFF7D52;       // 品牌橙悬停
const uint32_t kDanger = 0xE5484D;           // 确认框里的「关机 / 重启」
const uint32_t kMessageColor = 0xFF8F8F;     // 密码框下方提示的浅红色
const uint32_t kBlack = 0x000000;
const float kConfirmDimAlpha = 0.45f;        // 确认框打开时压暗整屏的遮罩
const uint32_t kPanelFill = 0x1C1C24;        // 确认框底色
const float kPanelFillAlpha = 0.9f;
const float kPlaceholderAlpha = 0.55f;       // 占位文字的不透明度
const float kConfirmTextAlpha = 0.75f;       // 确认框说明文字的不透明度
const float kClockShadowAlpha = 0.55f;       // 左下角时间投影的不透明度
const float kClockShadowOffset = 2.0f;       // 投影偏移（逻辑像素）
const float kMessageShadowAlpha = kClockShadowAlpha;    // 密码框下方提示的投影，与左下角时间相同
const float kMessageShadowOffset = kClockShadowOffset;

// ---- 字号（逻辑像素）----
const float kButtonFontSize = 16.0f;
const float kInputFontSize = 16.0f;
const float kMessageFontSize = 13.0f;
const float kTitleFontSize = 18.0f;
const float kTextFontSize = 14.0f;
const float kClockMinSize = 48.0f;           // 左下角时间字号下限
const float kClockMaxSize = 108.0f;          // 上限
const float kClockWidthRatio = 0.07f;        // 期望字号占画面宽度的比例
const float kDateMinSize = 16.0f;            // 日期字号下限
const float kDateMaxSize = 24.0f;            // 上限
const float kDateWidthRatio = 0.016f;        // 期望字号占画面宽度的比例
const float kClockLeftRatio = 0.065f;        // 时间左边距占画面宽度的比例
const float kClockBottomRatio = 0.11f;       // 时间块底边距占画面高度的比例
const float kDateGap = 10.0f;                // 时间与日期的间距（逻辑像素）

// ---- 动画 ----
const float kFadeSeconds = 0.35f;            // 按钮栏淡入淡出时长
const uint64_t kShakeMs = 420;               // 输错时抖动的时长
const uint64_t kCaretBlinkMs = 530;          // 光标闪烁半周期
const float kInputPadX = 20.0f;              // 输入框内文字左边距（逻辑像素）
const float kCaretWidth = 1.5f;              // 光标宽度（逻辑像素）
const float kCaretGap = 3.0f;                // 光标与最后一个圆点的间距（逻辑像素）
const float kBorderWidth = 1.0f;             // 边框线宽（逻辑像素）
const float kPanelRadius = 14.0f;            // 确认框圆角半径（逻辑像素）
const float kCancelHoverAlpha = 0.12f;       // 确认框「取消」悬停时的白色底不透明度
const float kOkHoverAlpha = 0.85f;           // 确认框「关机 / 重启」悬停时略微变淡
const float kDateAlpha = 0.88f;              // 左下角日期文字的不透明度
const float kDateLineFactor = 1.4f;          // 日期行高与字号之比
const float kCaretHeightFactor = 1.2f;       // 光标高度与字号之比

// ---- 预览提示（2026-10-07）----
const float kHintFontSize = 15.0f;           // 提示文字字号（逻辑像素）
const float kHintTextAlpha = 0.92f;          // 提示文字的不透明度
const float kHintFillAlpha = 0.62f;          // 提示胶囊深色底的不透明度（比按钮略实，浅色画面上也看得清）

// 抖动关键帧：时间比例与水平偏移（逻辑像素），与样稿的 @keyframes shake 一致
const float kShakeKeyTimes[] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
const float kShakeKeyOffsets[] = { 0.0f, -10.0f, 9.0f, -6.0f, 4.0f, 0.0f };
const int kShakeKeyCount = 6;

// 按钮栏三个按钮的文字与图标，下标与 LockOverlayLayout 的按钮顺序一致；到使用处再 TR
const wchar_t* const kButtonTexts[kOverlayButtonCount] = { TR_NOOP(L"解锁"), TR_NOOP(L"重启"), TR_NOOP(L"关机") };
const int kButtonIcons[kOverlayButtonCount] = { kIconLock, kIconRestart, kIconPower };

float ClampF(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

bool PointIn(const RECT& rc, POINT pt)
{
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

// 抖动的水平偏移（逻辑像素）：按关键帧分段线性插值
float ShakeOffset(float t)
{
    for (int i = 0; i + 1 < kShakeKeyCount; ++i)
    {
        if (t <= kShakeKeyTimes[i + 1])
        {
            const float span = kShakeKeyTimes[i + 1] - kShakeKeyTimes[i];
            const float k = (span > 0.0f) ? (t - kShakeKeyTimes[i]) / span : 0.0f;
            return kShakeKeyOffsets[i] + (kShakeKeyOffsets[i + 1] - kShakeKeyOffsets[i]) * k;
        }
    }
    return 0.0f;
}

} // namespace

CLockOverlayRenderer::CLockOverlayRenderer()
    : m_dwrite(NULL)
    , m_scale(1.0f)
    , m_width(0)
    , m_height(0)
    , m_barAlpha(0.0f)
    , m_lastVisiblePhase(kUiButtons)
    , m_shakeStartMs(0)
{
    for (int i = 0; i < kOverlayButtonCount; ++i)
    {
        m_contentWidths[i] = 0;
    }
    ::ZeroMemory(&m_layout, sizeof(m_layout));
    ::ZeroMemory(&m_hintRect, sizeof(m_hintRect));
}

void CLockOverlayRenderer::Init(IDWriteFactory* dwrite, float scale, int width, int height)
{
    m_dwrite = dwrite;
    m_scale = (scale > 0.0f) ? scale : 1.0f;
    m_width = width;
    m_height = height;
    const std::wstring ui = i18n::UiFontName();
    const float s = m_scale;
    m_buttonFormat = MakeTextFormat(dwrite, ui, kButtonFontSize * s, DWRITE_FONT_WEIGHT_NORMAL,
                                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_inputFormat = MakeTextFormat(dwrite, ui, kInputFontSize * s, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_messageFormat = MakeTextFormat(dwrite, ui, kMessageFontSize * s, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_titleFormat = MakeTextFormat(dwrite, ui, kTitleFontSize * s, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormat = MakeTextFormat(dwrite, ui, kTextFontSize * s, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (m_textFormat != NULL)
    {
        //确认框说明可能较长（译文），允许折成两行
        m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    const float clockSize = ClampF(static_cast<float>(width) * kClockWidthRatio, kClockMinSize * s, kClockMaxSize * s);
    const float dateSize = ClampF(static_cast<float>(width) * kDateWidthRatio, kDateMinSize * s, kDateMaxSize * s);
    m_clockFormat = MakeTextFormat(dwrite, ui, clockSize, DWRITE_FONT_WEIGHT_LIGHT,
                                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    m_dateFormat = MakeTextFormat(dwrite, ui, dateSize, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    m_hintFormat = MakeTextFormat(dwrite, ui, kHintFontSize * s, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    //尺寸或字号变了，提示胶囊要按新的字号重新测量
    m_hintText.clear();

    for (int i = 0; i < kOverlayButtonCount; ++i)
    {
        const float textW = MeasureTextWidth(dwrite, m_buttonFormat, TR(kButtonTexts[i]));
        m_contentWidths[i] = static_cast<int>((kIconSize + kIconTextGap) * s + textW + 1.0f);
    }
    m_layout = ComputeOverlayLayout(width, height, m_scale, m_contentWidths);
}

const LockOverlayLayout& CLockOverlayRenderer::Layout() const
{
    return m_layout;
}

void CLockOverlayRenderer::StartShake(uint64_t nowMs)
{
    m_shakeStartMs = (nowMs == 0) ? 1 : nowMs;
}

void CLockOverlayRenderer::DiscardDeviceResources()
{
    m_brush.Release();
}

ID2D1SolidColorBrush* CLockOverlayRenderer::Brush(ID2D1RenderTarget* rt, uint32_t rgb, float alpha)
{
    if (m_brush == NULL)
    {
        rt->CreateSolidColorBrush(ColorRgb(rgb, alpha), &m_brush);
    }
    if (m_brush != NULL)
    {
        m_brush->SetColor(ColorRgb(rgb, alpha));
    }
    return m_brush;
}

void CLockOverlayRenderer::Draw(ID2D1RenderTarget* rt, const OverlayFrame& frame)
{
    if (rt == NULL)
    {
        return;
    }
    if (frame.m_showClock)
    {
        DrawClock(rt, frame.m_wallMs);
    }
    if (!frame.m_hint.empty())
    {
        DrawHint(rt, frame.m_hint);
    }

    //按钮栏的淡入淡出：在本显示器上可见时趋向 1，否则趋向 0；淡出过程中仍按最后可见时的阶段画
    const bool visibleHere = frame.m_barHere && frame.m_phase != kUiIdle;
    if (visibleHere)
    {
        m_lastVisiblePhase = frame.m_phase;
    }
    const float step = (kFadeSeconds > 0.0f) ? frame.m_dt / kFadeSeconds : 1.0f;
    m_barAlpha = ClampF(m_barAlpha + (visibleHere ? step : -step), 0.0f, 1.0f);
    if (m_barAlpha <= 0.0f)
    {
        return;
    }
    const LockUiPhase phase = visibleHere ? frame.m_phase : m_lastVisiblePhase;
    DrawBar(rt, frame, phase, m_barAlpha);
    if (phase == kUiConfirmShutdown || phase == kUiConfirmRestart)
    {
        DrawConfirm(rt, frame, phase, m_barAlpha);
    }
}

void CLockOverlayRenderer::DrawClock(ID2D1RenderTarget* rt, uint64_t wallMs)
{
    if (m_clockFormat == NULL || m_dateFormat == NULL)
    {
        return;
    }
    std::wstring timeText;
    std::wstring dateText;
    FormatClockTexts(timeText, dateText, wallMs);
    const float left = static_cast<float>(m_width) * kClockLeftRatio;
    const float bottom = static_cast<float>(m_height) * (1.0f - kClockBottomRatio);
    const float dateSize = m_dateFormat->GetFontSize();
    const float dateTop = bottom - dateSize * kDateLineFactor;
    const float timeBottom = dateTop - kDateGap * m_scale;
    const float off = kClockShadowOffset * m_scale;
    const float right = static_cast<float>(m_width);
    //先画投影，再画白字
    DrawTextIn(rt, m_clockFormat, timeText, D2D1::RectF(left + off, 0.0f, right, timeBottom + off),
               Brush(rt, kBlack, kClockShadowAlpha));
    DrawTextIn(rt, m_dateFormat, dateText, D2D1::RectF(left + off, dateTop + off, right, bottom + off),
               Brush(rt, kBlack, kClockShadowAlpha));
    DrawTextIn(rt, m_clockFormat, timeText, D2D1::RectF(left, 0.0f, right, timeBottom), Brush(rt, kWhite, 1.0f));
    DrawTextIn(rt, m_dateFormat, dateText, D2D1::RectF(left, dateTop, right, bottom), Brush(rt, kWhite, kDateAlpha));
}

void CLockOverlayRenderer::DrawHint(ID2D1RenderTarget* rt, const std::wstring& hint)
{
    if (m_hintFormat == NULL)
    {
        return;
    }
    if (hint != m_hintText)
    {
        //文字变了才重新测量：每帧都建文字排版对象没有必要
        m_hintText = hint;
        const float textW = MeasureTextWidth(m_dwrite, m_hintFormat, hint);
        m_hintRect = ComputePreviewHintRect(m_width, m_height, m_scale, static_cast<int>(textW + 1.0f));
    }
    const D2D1_RECT_F r = ToRectF(m_hintRect);
    const float radius = (r.bottom - r.top) / 2.0f;
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), Brush(rt, kGlassFill, kHintFillAlpha));
    const float bw = kBorderWidth * m_scale;
    rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + bw / 2.0f, r.top + bw / 2.0f,
                                                           r.right - bw / 2.0f, r.bottom - bw / 2.0f),
                                               radius, radius),
                             Brush(rt, kWhite, kGlassBorderAlpha), bw);
    DrawTextIn(rt, m_hintFormat, hint, r, Brush(rt, kWhite, kHintTextAlpha));
}

void CLockOverlayRenderer::DrawButton(ID2D1RenderTarget* rt, const RECT& rc, const std::wstring& text, int icon,
                                      uint32_t fill, float fillAlpha, uint32_t border, float borderAlpha, float alpha)
{
    const D2D1_RECT_F r = ToRectF(rc);
    const float radius = (r.bottom - r.top) / 2.0f;
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
    rt->FillRoundedRectangle(rr, Brush(rt, fill, fillAlpha * alpha));
    const float bw = kBorderWidth * m_scale;
    const D2D1_ROUNDED_RECT inner = D2D1::RoundedRect(
        D2D1::RectF(r.left + bw / 2.0f, r.top + bw / 2.0f, r.right - bw / 2.0f, r.bottom - bw / 2.0f), radius, radius);
    rt->DrawRoundedRectangle(inner, Brush(rt, border, borderAlpha * alpha), bw);

    const float s = m_scale;
    if (text.empty())
    {
        //只有图标的圆形按钮（密码框右边的箭头）
        const float size = kIconSize * s;
        DrawIcon(rt, icon, (r.left + r.right - size) / 2.0f, (r.top + r.bottom - size) / 2.0f, size, alpha);
        return;
    }
    const float textW = MeasureTextWidth(m_dwrite, m_buttonFormat, text);
    const float contentW = (icon == kIconNone) ? textW : kIconSize * s + kIconTextGap * s + textW;
    float x = (r.left + r.right - contentW) / 2.0f;
    if (icon != kIconNone)
    {
        const float size = kIconSize * s;
        DrawIcon(rt, icon, x, (r.top + r.bottom - size) / 2.0f, size, alpha);
        x += size + kIconTextGap * s;
    }
    DrawTextIn(rt, m_buttonFormat, text, D2D1::RectF(x, r.top, r.right, r.bottom), Brush(rt, kWhite, alpha));
}

void CLockOverlayRenderer::DrawIcon(ID2D1RenderTarget* rt, int icon, float x, float y, float size, float alpha)
{
    if (icon == kIconNone)
    {
        return;
    }
    CComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);
    CComPtr<ID2D1PathGeometry> path;
    if (factory == NULL || FAILED(factory->CreatePathGeometry(&path)) || path == NULL)
    {
        return;
    }
    CComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink)) || sink == NULL)
    {
        return;
    }
    switch (icon)
    {
    //锁：U 形锁梁（锁身单独画圆角矩形）
    case kIconLock:
        sink->BeginFigure(D2D1::Point2F(8.0f, 10.0f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(8.0f, 7.0f));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(16.0f, 7.0f), D2D1::SizeF(4.0f, 4.0f), 0.0f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(16.0f, 10.0f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        break;

    //重启：大半个圆弧加右上角的折线箭头
    case kIconRestart:
        sink->BeginFigure(D2D1::Point2F(20.0f, 12.0f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(17.66f, 6.34f), D2D1::SizeF(8.0f, 8.0f), 0.0f,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(D2D1::Point2F(20.0f, 4.0f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(20.0f, 9.0f));
        sink->AddLine(D2D1::Point2F(15.0f, 9.0f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        break;

    //电源：顶部竖线加缺口朝上的圆弧
    case kIconPower:
        sink->BeginFigure(D2D1::Point2F(12.0f, 3.0f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(12.0f, 12.0f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(D2D1::Point2F(6.3f, 6.3f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(17.7f, 6.3f), D2D1::SizeF(8.0f, 8.0f), 0.0f,
                                      D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        break;

    //向右的箭头
    default:
        sink->BeginFigure(D2D1::Point2F(5.0f, 12.0f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(19.0f, 12.0f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->BeginFigure(D2D1::Point2F(13.0f, 6.0f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(19.0f, 12.0f));
        sink->AddLine(D2D1::Point2F(13.0f, 18.0f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        break;
    }
    sink->Close();

    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    const float k = size / kIconViewBox;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * old);
    CComPtr<ID2D1StrokeStyle> round;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    factory->CreateStrokeStyle(props, NULL, 0, &round);
    ID2D1Brush* brush = Brush(rt, kWhite, alpha);
    rt->DrawGeometry(path, brush, kIconStroke, round);
    if (icon == kIconLock)
    {
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(5.0f, 10.0f, 19.0f, 20.0f), 2.0f, 2.0f),
                                 brush, kIconStroke, round);
    }
    rt->SetTransform(old);
}

void CLockOverlayRenderer::DrawBar(ID2D1RenderTarget* rt, const OverlayFrame& frame, LockUiPhase phase, float alpha)
{
    const RECT* buttons[kOverlayButtonCount] =
    {
        &m_layout.m_unlockButton, &m_layout.m_restartButton, &m_layout.m_shutdownButton,
    };
    for (int i = 0; i < kOverlayButtonCount; ++i)
    {
        const bool hover = PointIn(*buttons[i], frame.m_mouse);
        if (i == 0)
        {
            DrawButton(rt, *buttons[i], TR(kButtonTexts[i]), kButtonIcons[i], hover ? kBrandHover : kBrand, 1.0f,
                       kBrand, 1.0f, alpha);
        }
        else
        {
            DrawButton(rt, *buttons[i], TR(kButtonTexts[i]), kButtonIcons[i], kGlassFill,
                       hover ? kGlassHoverAlpha : kGlassFillAlpha, kWhite, kGlassBorderAlpha, alpha);
        }
    }
    if (phase != kUiPassword)
    {
        return;
    }

    //密码框：输错时整行左右抖动
    float dx = 0.0f;
    if (m_shakeStartMs != 0)
    {
        const uint64_t elapsed = (frame.m_nowMs > m_shakeStartMs) ? frame.m_nowMs - m_shakeStartMs : 0;
        if (elapsed >= kShakeMs)
        {
            m_shakeStartMs = 0;
        }
        else
        {
            dx = ShakeOffset(static_cast<float>(elapsed) / static_cast<float>(kShakeMs)) * m_scale;
        }
    }
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(dx, 0.0f) * old);

    const D2D1_RECT_F box = ToRectF(m_layout.m_passwordBox);
    const float radius = (box.bottom - box.top) / 2.0f;
    rt->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), Brush(rt, kGlassFill, kGlassFillAlpha * alpha));
    const float bw = kBorderWidth * m_scale;
    rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(box.left + bw / 2.0f, box.top + bw / 2.0f,
                                                           box.right - bw / 2.0f, box.bottom - bw / 2.0f),
                                               radius, radius),
                             Brush(rt, kBrand, alpha), bw);
    const float textLeft = box.left + kInputPadX * m_scale;
    const D2D1_RECT_F textRect = D2D1::RectF(textLeft, box.top, box.right - radius / 2.0f, box.bottom);
    float caretX = textLeft;
    if (frame.m_maskChars > 0)
    {
        const std::wstring dots(static_cast<size_t>(frame.m_maskChars), static_cast<wchar_t>(0x25CF));
        DrawTextIn(rt, m_inputFormat, dots, textRect, Brush(rt, kWhite, alpha));
        caretX = textLeft + MeasureTextWidth(m_dwrite, m_inputFormat, dots) + kCaretGap * m_scale;
    }
    else
    {
        DrawTextIn(rt, m_inputFormat, TR(L"请输入本次锁屏密码"), textRect, Brush(rt, kWhite, kPlaceholderAlpha * alpha));
    }
    if ((frame.m_nowMs / kCaretBlinkMs) % 2 == 0)
    {
        const float h = m_inputFormat != NULL ? m_inputFormat->GetFontSize() * kCaretHeightFactor : (box.bottom - box.top) / 2.0f;
        const float cy = (box.top + box.bottom) / 2.0f;
        rt->FillRectangle(D2D1::RectF(caretX, cy - h / 2.0f, caretX + kCaretWidth * m_scale, cy + h / 2.0f),
                          Brush(rt, kWhite, alpha));
    }
    const bool goHover = PointIn(m_layout.m_passwordGo, frame.m_mouse);
    DrawButton(rt, m_layout.m_passwordGo, std::wstring(), kIconArrow, goHover ? kBrandHover : kBrand, 1.0f,
               kBrand, 1.0f, alpha);
    rt->SetTransform(old);

    if (!frame.m_message.empty())
    {
        //提示直接画在动画上、下面没有底，先画一层投影再画浅红字，否则在大红底的汉字数字雨上看不清
        //（docs/锁屏方案.md 第 12 节）
        const D2D1_RECT_F msgRect = ToRectF(m_layout.m_passwordMessage);
        const float off = kMessageShadowOffset * m_scale;
        DrawTextIn(rt, m_messageFormat, frame.m_message,
                   D2D1::RectF(msgRect.left + off, msgRect.top + off, msgRect.right + off, msgRect.bottom + off),
                   Brush(rt, kBlack, kMessageShadowAlpha * alpha));
        DrawTextIn(rt, m_messageFormat, frame.m_message, msgRect, Brush(rt, kMessageColor, alpha));
    }
}

void CLockOverlayRenderer::DrawConfirm(ID2D1RenderTarget* rt, const OverlayFrame& frame, LockUiPhase phase, float alpha)
{
    rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)),
                      Brush(rt, kBlack, kConfirmDimAlpha * alpha));
    const D2D1_RECT_F panel = ToRectF(m_layout.m_confirmPanel);
    const float radius = kPanelRadius * m_scale;
    rt->FillRoundedRectangle(D2D1::RoundedRect(panel, radius, radius), Brush(rt, kPanelFill, kPanelFillAlpha * alpha));
    rt->DrawRoundedRectangle(D2D1::RoundedRect(panel, radius, radius), Brush(rt, kWhite, kGlassBorderAlpha * alpha),
                             kBorderWidth * m_scale);
    const bool shutdown = (phase == kUiConfirmShutdown);
    DrawTextIn(rt, m_titleFormat, shutdown ? TR(L"确定要关机吗？") : TR(L"确定要重启吗？"),
               ToRectF(m_layout.m_confirmTitle), Brush(rt, kWhite, alpha));
    DrawTextIn(rt, m_textFormat, TR(L"其他程序里未保存的内容可能会丢失。"), ToRectF(m_layout.m_confirmText),
               Brush(rt, kWhite, kConfirmTextAlpha * alpha));
    const bool cancelHover = PointIn(m_layout.m_confirmCancel, frame.m_mouse);
    DrawButton(rt, m_layout.m_confirmCancel, TR(L"取消"), kIconNone, kWhite, cancelHover ? kCancelHoverAlpha : 0.0f,
               kWhite, kGlassBorderAlpha, alpha);
    const bool okHover = PointIn(m_layout.m_confirmOk, frame.m_mouse);
    DrawButton(rt, m_layout.m_confirmOk, shutdown ? TR(L"关机") : TR(L"重启"), kIconNone, kDanger,
               okHover ? kOkHoverAlpha : 1.0f, kDanger, 1.0f, alpha);
}

} // namespace flamingolock
