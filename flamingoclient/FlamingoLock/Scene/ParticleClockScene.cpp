/**
 *  锁屏动画「粒子时钟」：当前时间由几千个发光粒子拼成；换分钟时粒子炸散，再聚成新的数字；鼠标划过时粒子被推开，
 *  移开后归位。下方一条秒进度线，再下面是日期。样式取自样稿 tmp/lockscreen/lock_preview.html（场景三），
 *  尺寸为样稿 CSS 像素乘以缩放比例。
 *
 *  目标点的取法：用 GDI 把「时:分」画到一张灰度蒙版上，交给 SampleMaskPoints（SceneModels.cpp）取样，使点数接近
 *  且不超过粒子上限。粒子靠「不清屏、每帧盖一层半透明底色」留下拖尾，所以画在离屏位图上再整张贴到窗口；
 *  秒进度线与日期直接画在窗口上。Direct2D 1.0 没有加色混合，粒子按普通透明度叠加（docs/锁屏方案.md 第 7.1 节）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "../Render/LockScene.h"
#include "Scenes.h"
#include "SceneUtil.h"

#include <stdio.h>
#include <vector>

#include "../../Source/Common/I18n/I18n.h"

namespace flamingolock {

namespace {

// ---- 颜色 ----
const uint32_t kBackground = 0x04060F;     // 深蓝黑底
const float kTrailFadePerFrame = 0.34f;    // 每帧（按 60 帧 / 秒折算）盖上的底色不透明度
const float kFramesPerSecond = 60.0f;
const int kHueBuckets = 16;                // 粒子按横坐标分成几组颜色，从左到右由青到紫
const float kHueLeft = 186.0f;             // 最左一组的色相（度）
const float kHueSpan = 110.0f;             // 从左到右色相变化的总量（度）
const float kHueSat = 0.9f;                // 饱和度
const float kHueLight = 0.62f;             // 亮度
const float kParticleAlpha = 0.9f;         // 粒子不透明度
const uint32_t kTrackColor = 0xFFFFFF;     // 秒进度线的底线
const float kTrackAlpha = 0.12f;
const uint32_t kDotColor = 0xFFFFFF;       // 秒进度线头部的圆点
const uint32_t kDotGlow = 0xA0DCFF;        // 圆点的光晕
const float kDotGlowAlpha = 0.9f;
const uint32_t kDateColor = 0xDCE6FF;      // 日期文字
const float kDateAlpha = 0.72f;

// ---- 运动（样稿 CSS 像素，乘以缩放比例）----
const float kSpring = 24.0f;               // 把粒子拉回目标点的弹性系数（1 / 秒²）
const float kDamp = 7.5f;                  // 阻尼系数（1 / 秒）
const float kScatterSec = 0.8f;            // 换分钟时炸散阶段的时长（秒）
const float kScatterDrag = 2.2f;           // 炸散阶段的空气阻力（1 / 秒）
const float kRepelRadius = 120.0f;         // 鼠标推开粒子的半径（像素）
const float kRepelForce = 6000.0f;         // 鼠标推力强度（像素 / 秒²）
const float kRepelMinDist2 = 0.01f;        // 鼠标与粒子重合时不推（避免除以零）
const float kExplodeJitter = 30.0f;        // 炸散方向上加的随机偏移（像素）
const float kExplodeSpeedMin = 300.0f;     // 炸散速度范围（像素 / 秒）
const float kExplodeSpeedMax = 1000.0f;
const float kTargetJitter = 0.8f;          // 目标点上加的随机偏移（像素），聚拢后不至于排得太死板
const float kNoPointer = -9999.0f;         // 鼠标不在画面上时的坐标

// ---- 取样与排版 ----
const int kMaxParticles = 4500;            // 粒子数上限
const int kFineStep = 3;                   // 数字形面积时的细取样间隔（像素）
const float kDigitWidthRatio = 0.25f;      // 数字字号占画面宽度的比例
const float kDigitHeightRatio = 0.42f;     // 数字字号占画面高度的比例
const float kMaskHeightRatio = 1.3f;       // 蒙版高度与字号之比
const float kDigitCenterY = 0.43f;         // 数字中线在画面高度上的位置
const uint8_t kMaskThreshold = 128;        // 蒙版灰度超过这个值才算字形内
const float kSizeMin = 1.6f;               // 每粒子尺寸系数的范围
const float kSizeMax = 2.6f;
const float kSizePerStep = 0.3f;           // 粒子边长 = 尺寸系数 × 取样间隔 × 0.3，聚拢后彼此之间留一点缝
const float kLineGapRatio = 0.07f;         // 秒进度线在数字下方多远（占画面高度）
const float kLineWidth = 2.0f;             // 秒进度线线宽
const float kDotR = 4.0f;                  // 圆点半径
const float kDotGlowR = 12.0f;             // 圆点光晕半径
const float kDateHeightRatio = 0.026f;     // 日期字号占画面高度的比例
const float kDateFontMin = 16.0f;          // 日期字号范围
const float kDateFontMax = 28.0f;
const float kDateY = 1.9f;                 // 日期中线在进度线下方多远（以日期字号计）
const float kSecondsPerMinute = 60.0f;
const float kMsPerSecond = 1000.0f;

// 数字字体候选：系统有 Segoe UI 就用它
const wchar_t* const kDigitFonts[] = { L"Segoe UI", L"Arial" };
const int kDigitFontCount = 2;

// 一个粒子
struct Particle
{
    float m_x;    // 横坐标
    float m_y;    // 纵坐标
    float m_vx;   // 横向速度
    float m_vy;   // 纵向速度
    float m_tx;   // 目标点横坐标
    float m_ty;   // 目标点纵坐标
    float m_s;    // 尺寸系数
};

/**
 *  用 GDI 把一行文字白字黑底画到灰度蒙版上（每像素 1 字节，0 为字形外）。
 *  @param text   文字
 *  @param family 字体名
 *  @param fontPx 字号（像素）
 *  @param width  蒙版宽
 *  @param height 蒙版高
 *  @param mask   接收蒙版，长度 width * height
 *  @return 成功返回 true
 */
bool RenderTextMask(const std::wstring& text, const std::wstring& family, int fontPx, int width, int height,
                    std::vector<uint8_t>& mask)
{
    mask.assign(static_cast<size_t>(width) * height, 0);
    if (width <= 0 || height <= 0)
    {
        return false;
    }
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;   //负数表示自上而下存放
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL || dib == NULL || bits == NULL)
    {
        if (dib != NULL)
        {
            ::DeleteObject(dib);
        }
        if (mem != NULL)
        {
            ::DeleteDC(mem);
        }
        return false;
    }
    HGDIOBJ oldBmp = ::SelectObject(mem, dib);
    memset(bits, 0, static_cast<size_t>(width) * height * 4);
    HFONT font = ::CreateFontW(-fontPx, 0, 0, 0, FW_EXTRABOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family.c_str());
    HGDIOBJ oldFont = ::SelectObject(mem, font);
    ::SetBkMode(mem, TRANSPARENT);
    ::SetTextColor(mem, RGB(255, 255, 255));
    RECT rc = { 0, 0, width, height };
    ::DrawTextW(mem, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::GdiFlush();
    //白字：任取一个颜色分量即为灰度（每像素按 B、G、R、保留 四个字节存放，取 G）
    const uint8_t* px = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < mask.size(); ++i)
    {
        mask[i] = (px[i * 4 + 1] > kMaskThreshold) ? 1 : 0;
    }
    ::SelectObject(mem, oldFont);
    ::SelectObject(mem, oldBmp);
    if (font != NULL)
    {
        ::DeleteObject(font);
    }
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    return true;
}

/**
 *  粒子时钟场景。
 */
class CParticleClockScene : public ILockScene
{
public:
    CParticleClockScene()
        : m_w(0.0f)
        , m_h(0.0f)
        , m_scale(1.0f)
        , m_scatterT(-1.0f)
        , m_mouseX(kNoPointer)
        , m_mouseY(kNoPointer)
        , m_wallMs(0)
        , m_sampleStep(4)
        , m_lastDt(1.0f / kFramesPerSecond)
        , m_dateFont(0.0f)
    {
        m_box = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void Init(const SceneEnv& env, float width, float height) override
    {
        m_w = width;
        m_h = height;
        m_scale = env.m_scale;
        m_digitFamily = PickFontFamily(env.m_dwrite, kDigitFonts, kDigitFontCount);
        m_dateFont = ClampF(height * kDateHeightRatio, kDateFontMin * m_scale, kDateFontMax * m_scale);
        m_dateFormat = MakeTextFormat(env.m_dwrite, i18n::UiFontName(), m_dateFont, DWRITE_FONT_WEIGHT_NORMAL,
                                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_parts.clear();
        m_shown.clear();
        m_pending.clear();
        m_scatterT = -1.0f;
        m_wallMs = NowWallMs();
        Assign(TimeText(m_wallMs));
        DiscardDeviceResources();
    }

    void DiscardDeviceResources() override
    {
        m_solid.Release();
        for (int i = 0; i < kHueBuckets; ++i)
        {
            m_hue[i].Release();
        }
        m_progress.Release();
        m_dotGlow.Release();
        m_lineBrush.Release();
        m_offscreen.Release();
    }

    void Update(float dt, uint64_t wallMs) override
    {
        m_lastDt = dt;
        m_wallMs = (wallMs != 0) ? wallMs : NowWallMs();
        const std::wstring text = TimeText(m_wallMs);
        if (text != m_shown && m_scatterT < 0.0f)
        {
            m_pending = text;
            m_scatterT = 0.0f;
            Explode();
        }
        if (m_scatterT >= 0.0f)
        {
            m_scatterT += dt;
            if (m_scatterT >= kScatterSec)
            {
                m_scatterT = -1.0f;
                Assign(m_pending);
            }
        }
        const bool scatter = (m_scatterT >= 0.0f);
        const float drag = expf(-kScatterDrag * dt);
        const float repelR = kRepelRadius * m_scale;
        const float repelF = kRepelForce * m_scale;
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            Particle& p = m_parts[i];
            float ax = 0.0f;
            float ay = 0.0f;
            if (!scatter)
            {
                ax = (p.m_tx - p.m_x) * kSpring - p.m_vx * kDamp;
                ay = (p.m_ty - p.m_y) * kSpring - p.m_vy * kDamp;
            }
            const float dx = p.m_x - m_mouseX;
            const float dy = p.m_y - m_mouseY;
            const float d2 = dx * dx + dy * dy;
            if (d2 < repelR * repelR && d2 > kRepelMinDist2)
            {
                const float d = sqrtf(d2);
                const float f = (1.0f - d / repelR) * repelF;
                ax += dx / d * f;
                ay += dy / d * f;
            }
            p.m_vx += ax * dt;
            p.m_vy += ay * dt;
            if (scatter)
            {
                p.m_vx *= drag;
                p.m_vy *= drag;
            }
            p.m_x += p.m_vx * dt;
            p.m_y += p.m_vy * dt;
        }
    }

    void Draw(ID2D1RenderTarget* rt) override
    {
        if (!EnsureResources(rt))
        {
            return;
        }
        m_offscreen->BeginDraw();
        const float fade = 1.0f - powf(1.0f - kTrailFadePerFrame, m_lastDt * kFramesPerSecond);
        m_solid->SetColor(ColorRgb(kBackground, fade));
        m_offscreen->FillRectangle(D2D1::RectF(0.0f, 0.0f, m_w, m_h), m_solid);
        const float sizeK = m_sampleStep * kSizePerStep;
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            const Particle& p = m_parts[i];
            int k = static_cast<int>(p.m_x / m_w * kHueBuckets);
            k = (k < 0) ? 0 : ((k >= kHueBuckets) ? kHueBuckets - 1 : k);
            const float sz = p.m_s * sizeK;
            m_offscreen->FillRectangle(D2D1::RectF(p.m_x - sz / 2.0f, p.m_y - sz / 2.0f, p.m_x + sz / 2.0f,
                                                   p.m_y + sz / 2.0f),
                                       m_hue[k]);
        }
        if (FAILED(m_offscreen->EndDraw()))
        {
            DiscardDeviceResources();
            return;
        }
        CComPtr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(m_offscreen->GetBitmap(&bitmap)))
        {
            rt->DrawBitmap(bitmap, D2D1::RectF(0.0f, 0.0f, m_w, m_h));
        }
        DrawProgressAndDate(rt);
    }

    void OnPointer(float x, float y) override
    {
        if (x < 0.0f || y < 0.0f)
        {
            m_mouseX = kNoPointer;
            m_mouseY = kNoPointer;
            return;
        }
        m_mouseX = x;
        m_mouseY = y;
    }

    bool ShowsCornerClock() const override
    {
        //场景本身就是时钟
        return false;
    }

private:
    // 当前本地时间的「时:分」
    static std::wstring TimeText(uint64_t wallMs)
    {
        const SYSTEMTIME st = LocalTimeFromWallMs(wallMs);
        wchar_t buf[16];
        swprintf_s(buf, L"%02u:%02u", static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute));
        return buf;
    }

    // 把文字画成蒙版并取样，结果为画面坐标的目标点；同时更新数字区域与取样间隔
    void SampleTargets(const std::wstring& text, std::vector<SamplePoint>& pts)
    {
        const float fs = (m_w * kDigitWidthRatio < m_h * kDigitHeightRatio) ? m_w * kDigitWidthRatio
                                                                            : m_h * kDigitHeightRatio;
        const int maskW = static_cast<int>(ceilf(m_w));
        const int maskH = static_cast<int>(ceilf(fs * kMaskHeightRatio));
        std::vector<uint8_t> mask;
        pts.clear();
        if (!RenderTextMask(text, m_digitFamily, static_cast<int>(fs), maskW, maskH, mask))
        {
            return;
        }
        SampleMaskPoints(mask, maskW, maskH, kMaxParticles, kFineStep, pts, m_sampleStep);
        const float top = m_h * kDigitCenterY - maskH / 2.0f;
        float minX = m_w;
        float maxX = 0.0f;
        float minY = m_h;
        float maxY = 0.0f;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            pts[i].m_y += top;
            minX = (pts[i].m_x < minX) ? pts[i].m_x : minX;
            maxX = (pts[i].m_x > maxX) ? pts[i].m_x : maxX;
            minY = (pts[i].m_y < minY) ? pts[i].m_y : minY;
            maxY = (pts[i].m_y > maxY) ? pts[i].m_y : maxY;
        }
        if (!pts.empty())
        {
            m_box = D2D1::RectF(minX, minY, maxX, maxY);
        }
    }

    // 给每个粒子分配一个目标点（随机打乱，聚拢时粒子从四面八方飞来）
    void Assign(const std::wstring& text)
    {
        std::vector<SamplePoint> pts;
        SampleTargets(text, pts);
        m_shown = text;
        if (pts.empty())
        {
            return;
        }
        for (size_t i = pts.size() - 1; i > 0; --i)
        {
            const size_t j = static_cast<size_t>(RandIndex(static_cast<int>(i + 1)));
            const SamplePoint t = pts[i];
            pts[i] = pts[j];
            pts[j] = t;
        }
        while (m_parts.size() < pts.size())
        {
            Particle p;
            p.m_x = RandRangeF(0.0f, m_w);
            p.m_y = RandRangeF(0.0f, m_h);
            p.m_vx = 0.0f;
            p.m_vy = 0.0f;
            p.m_tx = p.m_x;
            p.m_ty = p.m_y;
            p.m_s = RandRangeF(kSizeMin, kSizeMax);
            m_parts.push_back(p);
        }
        const float jitter = kTargetJitter * m_scale;
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            //粒子比目标点多时，多出来的随机叠到已有的点上
            const SamplePoint& t = (i < pts.size()) ? pts[i] : pts[RandIndex(static_cast<int>(pts.size()))];
            m_parts[i].m_tx = t.m_x + RandRangeF(-jitter, jitter);
            m_parts[i].m_ty = t.m_y + RandRangeF(-jitter, jitter);
        }
    }

    // 换分钟：所有粒子从数字中心向外炸散
    void Explode()
    {
        const float cx = (m_box.left + m_box.right) / 2.0f;
        const float cy = (m_box.top + m_box.bottom) / 2.0f;
        const float jitter = kExplodeJitter * m_scale;
        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            Particle& p = m_parts[i];
            const float dx = p.m_x - cx + RandRangeF(-jitter, jitter);
            const float dy = p.m_y - cy + RandRangeF(-jitter, jitter);
            float d = sqrtf(dx * dx + dy * dy);
            if (d <= 0.0f)
            {
                d = 1.0f;
            }
            const float sp = RandRangeF(kExplodeSpeedMin, kExplodeSpeedMax) * m_scale;
            p.m_vx = dx / d * sp;
            p.m_vy = dy / d * sp;
        }
    }

    bool EnsureResources(ID2D1RenderTarget* rt)
    {
        if (m_offscreen != NULL)
        {
            return true;
        }
        if (FAILED(rt->CreateCompatibleRenderTarget(D2D1::SizeF(m_w, m_h), &m_offscreen)))
        {
            return false;
        }
        if (FAILED(m_offscreen->CreateSolidColorBrush(ColorRgb(kBackground), &m_solid)))
        {
            m_offscreen.Release();
            return false;
        }
        for (int i = 0; i < kHueBuckets; ++i)
        {
            const float hue = kHueLeft + static_cast<float>(i) / (kHueBuckets - 1) * kHueSpan;
            m_offscreen->CreateSolidColorBrush(HslColor(hue, kHueSat, kHueLight, kParticleAlpha), &m_hue[i]);
            if (m_hue[i] == NULL)
            {
                m_offscreen.Release();
                return false;
            }
        }
        //秒进度线与圆点直接画在窗口上，画刷从窗口渲染目标创建
        m_progress = MakeLinearBrush(rt, HslColor(kHueLeft, kHueSat, kHueLight, kParticleAlpha),
                                     HslColor(kHueLeft + kHueSpan, kHueSat, kHueLight, kParticleAlpha));
        m_dotGlow = MakeRadialBrush(rt, ColorRgb(kDotGlow, kDotGlowAlpha), ColorRgb(kDotGlow, 0.0f));
        m_lineBrush.Release();
        rt->CreateSolidColorBrush(ColorRgb(kTrackColor, kTrackAlpha), &m_lineBrush);
        m_offscreen->BeginDraw();
        m_offscreen->Clear(ColorRgb(kBackground));
        m_offscreen->EndDraw();
        return m_lineBrush != NULL;
    }

    // 秒进度线与日期
    void DrawProgressAndDate(ID2D1RenderTarget* rt)
    {
        const SYSTEMTIME st = LocalTimeFromWallMs(m_wallMs);
        const float sec = st.wSecond + st.wMilliseconds / kMsPerSecond;
        const float lineY = m_box.bottom + m_h * kLineGapRatio;
        const float x0 = m_box.left;
        const float x1 = m_box.right;
        const float lw = kLineWidth * m_scale;
        m_lineBrush->SetColor(ColorRgb(kTrackColor, kTrackAlpha));
        rt->DrawLine(D2D1::Point2F(x0, lineY), D2D1::Point2F(x1, lineY), m_lineBrush, lw);
        const float xe = LerpF(x0, x1, sec / kSecondsPerMinute);
        if (m_progress != NULL)
        {
            m_progress->SetStartPoint(D2D1::Point2F(x0, lineY));
            m_progress->SetEndPoint(D2D1::Point2F(x1, lineY));
            rt->DrawLine(D2D1::Point2F(x0, lineY), D2D1::Point2F(xe, lineY), m_progress, lw);
        }
        DrawGlowAt(rt, m_dotGlow, xe, lineY, kDotGlowR * m_scale, kDotGlowR * m_scale, 1.0f);
        m_lineBrush->SetColor(ColorRgb(kDotColor, 1.0f));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(xe, lineY), kDotR * m_scale, kDotR * m_scale), m_lineBrush);
        std::wstring timeText;
        std::wstring dateText;
        FormatClockTexts(timeText, dateText, m_wallMs);
        const float dy = lineY + m_dateFont * kDateY;
        m_lineBrush->SetColor(ColorRgb(kDateColor, kDateAlpha));
        DrawTextIn(rt, m_dateFormat, dateText, D2D1::RectF(0.0f, dy - m_dateFont, m_w, dy + m_dateFont), m_lineBrush);
    }

private:
    float                             m_w;                 // 画面宽
    float                             m_h;                 // 画面高
    float                             m_scale;             // 缩放比例
    std::vector<Particle>             m_parts;             // 粒子
    std::wstring                      m_shown;             // 当前拼出的「时:分」
    std::wstring                      m_pending;           // 炸散结束后要拼的文字
    float                             m_scatterT;          // 炸散阶段已过的秒数；-1 表示不在炸散
    float                             m_mouseX;            // 鼠标横坐标；不在画面上时为 kNoPointer
    float                             m_mouseY;            // 鼠标纵坐标
    uint64_t                          m_wallMs;            // 最近一帧的时刻（自 1970 年起的毫秒，UTC）
    int                               m_sampleStep;        // 最近一次取样的间隔（像素），粒子尺寸随它变化
    float                             m_lastDt;            // 上一帧的时长（秒），用来折算拖尾淡化量
    float                             m_dateFont;          // 日期字号
    D2D1_RECT_F                       m_box;               // 数字所占的区域
    std::wstring                      m_digitFamily;       // 数字字体
    CComPtr<IDWriteTextFormat>        m_dateFormat;        // 日期
    CComPtr<ID2D1BitmapRenderTarget>  m_offscreen;         // 保留上一帧内容的离屏位图，粒子与拖尾画在这里
    CComPtr<ID2D1SolidColorBrush>     m_solid;             // 离屏位图上盖底色用的画刷
    CComPtr<ID2D1SolidColorBrush>     m_hue[kHueBuckets];  // 每组颜色一支粒子画刷（离屏位图上创建）
    CComPtr<ID2D1SolidColorBrush>     m_lineBrush;         // 窗口上画进度线、圆点、日期的纯色画刷
    CComPtr<ID2D1LinearGradientBrush> m_progress;          // 秒进度线由青到紫的渐变
    CComPtr<ID2D1RadialGradientBrush> m_dotGlow;           // 圆点的光晕
};

} // namespace

ILockScene* CreateParticleClockScene()
{
    return new CParticleClockScene();
}

} // namespace flamingolock
