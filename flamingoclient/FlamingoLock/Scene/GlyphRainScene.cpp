/**
 *  锁屏动画「汉字数字雨」：大红底上落下金色的字符雨，落下的字取自八荣八耻原文；每隔一阵，屏幕中部有一束束金色亮光
 *  落下，定格成一整句：上一行金色的「荣」半句，下一行米白色的「耻」半句，每个字垫一块带金边的深红底衬，停留几秒后淡出。
 *  动作与尺寸取自样稿 tmp/lockscreen/lock_preview.html（场景五），尺寸为样稿 CSS 像素乘以缩放比例；配色在
 *  2026-10-07 由样稿的黑底绿字改为大红喜庆风格（docs/锁屏方案.md 第 12 节），与样稿不再一致。
 *
 *  字符雨靠「不清屏、每帧盖一层半透明底色」留下拖尾，而窗口渲染目标每帧的内容不保留，所以先画在一张与窗口同样
 *  大小的离屏位图上，再整张贴到窗口。离屏位图用纯色大红底；四角压暗是贴到窗口之后另盖的一层径向渐变（中心透明、
 *  四角半透明黑），每帧只画一次、不累积。不能把渐变直接当底色每帧叠加：8 位预乘精度下，半透明的渐变色按底色每隔
 *  若干级才变一档，拖尾残留停下来的亮度随之分档，满屏会出现一圈圈椭圆环（2026-10-07 实测，第 12 节）。
 *  定格句子的时间安排在 SceneModels.cpp（RainCharLandAt 等）。Direct2D 1.0 没有模糊，字的发光用径向渐变光晕近似
 *  （docs/锁屏方案.md 第 7.1 节）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "../Render/LockScene.h"
#include "Scenes.h"
#include "SceneUtil.h"

#include <vector>

#include "../../Source/Common/I18n/I18n.h"

namespace flamingolock {

namespace {

// ---- 颜色（2026-10-07 由黑底绿字改为大红喜庆风格，docs/锁屏方案.md 第 12 节）----
const uint32_t kBackground = 0xC8161D;     // 离屏位图的大红底色
const uint32_t kVignetteColor = 0x000000;  // 四角压暗用的黑色（整屏纯红刺眼，四周压暗）
const float kVignetteEdgeAlpha = 0.45f;    // 压暗层在四角的不透明度：大红 0xC8161D 压暗后约为暗红 0x6E0C10
const float kVignetteRadiusFactor = 1.41421356f; // 压暗层半径与画面半宽、半高之比（√2），四角恰好到达最暗
const uint32_t kTrailColor = 0xFFC94A;     // 头部走过之后留下的金字
const uint32_t kHeadColor = 0xFFF6DC;      // 头部的暖白色字
const uint32_t kHeadGlow = 0xFFD978;       // 头部的淡金色光晕
const float kHeadGlowAlpha = 0.55f;
const uint32_t kStreakTop = 0xFFE7A0;      // 定格亮光的上端（透明）
const uint32_t kStreakHead = 0xFFF6DC;     // 定格亮光的头部
const float kStreakHeadAlpha = 0.95f;
const uint32_t kFlashColor = 0xFFFFFF;     // 字刚落定时的白光
const uint32_t kHonorColor = 0xFFD54A;     // 「荣」半句的金色
const uint32_t kShameColor = 0xFFF1E0;     // 「耻」半句的米白色（红色在红底上看不清）
const uint32_t kHonorGlow = 0xFFC83C;      // 「荣」半句的光晕
const uint32_t kShameGlow = 0xFFA64D;      // 「耻」半句的暖橙色光晕
const float kHonorGlowAlpha = 0.9f;
const float kShameGlowAlpha = 0.85f;
const uint32_t kBackingColor = 0x5C060A;   // 字背后底衬的深红色，像一张斗方红纸
const float kBackingAlpha = 0.85f;         // 底衬的不透明度（挡住字符雨，让整句读得清）
const uint32_t kBackingBorder = 0xF0C45A;  // 底衬的金色细边
const float kBackingBorderAlpha = 0.75f;
const float kBackingBorderWidth = 1.5f;    // 金边线宽（逻辑像素，乘以缩放比例）

// ---- 比例与尺寸（样稿 CSS 像素，乘以缩放比例）----
const float kColumnsPerWidth = 64.0f;      // 字号取画面宽度的 1/64
const float kGlyphFontMin = 16.0f;         // 字号范围
const float kGlyphFontMax = 32.0f;
const float kDropInitMin = -0.6f;          // 一开始各列头部所在的行：[-0.6 × 行数, 行数)
const float kDropRespawnMin = -12.0f;      // 重新开始的一列从画面上方第几行落下：[-12, 0)
const float kDropSpeedMin = 7.0f;          // 下落速度范围（行 / 秒）
const float kDropSpeedMax = 17.0f;
const float kDropLenMin = 8.0f;            // 一列的长度范围（行），头部走出画面这么多行后重新开始
const float kDropLenMax = 26.0f;
const float kDropBrightMin = 0.55f;        // 拖尾金字的亮度（不透明度）范围
const float kDropBrightMax = 1.0f;
const float kSwapGlyphChance = 0.3f;       // 头部走过之后，留下的字有三成换成别的字
const float kTrailFadePerFrame = 0.07f;    // 每帧（按 60 帧 / 秒折算）盖上的底色不透明度
const float kFramesPerSecond = 60.0f;
const float kHeadGlowRadius = 0.75f;       // 头部光晕半径（以字号计）
const float kPhraseScale = 2.3f;           // 定格字号与字符雨字号之比
const float kPhraseGap = 1.08f;            // 定格字距与字号之比
const float kShameScale = 0.82f;           // 「耻」半句字号与「荣」半句之比
const float kShameRowY = 1.45f;            // 「耻」半句在「荣」半句下方多远（以定格字号计）
const float kPhraseYMin = 0.34f;           // 定格句子的纵向位置占画面高度的范围
const float kPhraseYMax = 0.5f;
const float kStreakFall = 0.45f;           // 亮光从字上方多远处开始落下（占画面高度）
const float kStreakLength = 0.25f;         // 亮光的长度（占画面高度）
const float kStreakWidth = 3.0f;           // 亮光的线宽
const float kFlashDur = 0.45f;             // 落定后由白光变成金色或红色的时长（秒）
const float kBackingW = 0.5f;              // 底衬半宽、半高与圆角（以字号计）；宽度小于字距，一字一方、彼此留缝，
                                           // 相邻两块的金边不会重叠成粗线
const float kBackingH = 0.6f;
const float kBackingRadius = 0.12f;
const float kGlowBase = 0.75f;             // 字光晕半径：落定后为字号的 0.75 倍，刚落定时再大 0.5 倍
const float kGlowFlash = 0.5f;
const float kGlowAlphaScale = 0.45f;       // 字光晕整体不透明度
const int kCenterChar = 3;                 // 七个字里居中的那个（第 4 个）的序号

// 一列下落的字
struct Drop
{
    float   m_y;        // 头部所在的行（可以是小数，负数表示还在画面上方）
    float   m_speed;    // 下落速度（行 / 秒）
    float   m_len;      // 长度（行）
    int     m_row;      // 上一次画到的行
    wchar_t m_head;     // 头部的字
    float   m_bright;   // 拖尾金字的亮度 0~1
};

// 一句正在定格的八荣八耻
struct Phrase
{
    float m_start;      // 开始时刻（场景时钟，秒）
    int   m_line;       // 八荣八耻的句序
    float m_cy;         // 「荣」半句的中线纵坐标
};

/**
 *  汉字数字雨场景。
 */
class CGlyphRainScene : public ILockScene
{
public:
    CGlyphRainScene()
        : m_w(0.0f)
        , m_h(0.0f)
        , m_scale(1.0f)
        , m_fs(0.0f)
        , m_cols(0)
        , m_rows(0)
        , m_clock(0.0f)
        , m_lastDt(1.0f / kFramesPerSecond)
        , m_nextPhrase(0)
    {
    }

    void Init(const SceneEnv& env, float width, float height) override
    {
        m_w = width;
        m_h = height;
        m_scale = env.m_scale;
        m_fs = ClampF(floorf(width / kColumnsPerWidth + 0.5f), kGlyphFontMin * m_scale, kGlyphFontMax * m_scale);
        m_cols = static_cast<int>(ceilf(width / m_fs));
        m_rows = static_cast<int>(ceilf(height / m_fs)) + 1;
        m_drops.resize(m_cols);
        for (int c = 0; c < m_cols; ++c)
        {
            NewDrop(m_drops[c], true);
        }
        m_phrases.clear();
        m_clock = 0.0f;
        m_nextPhrase = 0;
        const std::wstring uiFont = i18n::UiFontName();
        m_glyphFormat = MakeTextFormat(env.m_dwrite, uiFont, m_fs, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const float p = m_fs * kPhraseScale;
        m_honorFormat = MakeTextFormat(env.m_dwrite, uiFont, p, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
                                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_shameFormat = MakeTextFormat(env.m_dwrite, uiFont, p * kShameScale, DWRITE_FONT_WEIGHT_BOLD,
                                       DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DiscardDeviceResources();
    }

    void DiscardDeviceResources() override
    {
        m_solid.Release();
        m_vignette.Release();
        m_headGlow.Release();
        m_honorGlow.Release();
        m_shameGlow.Release();
        m_streak.Release();
        m_offscreen.Release();
    }

    void Update(float dt, uint64_t /*wallMs*/) override
    {
        m_clock += dt;
        m_lastDt = dt;
        for (size_t i = 0; i < m_drops.size(); ++i)
        {
            Drop& d = m_drops[i];
            d.m_y += d.m_speed * dt;
            if (d.m_y > m_rows + d.m_len)
            {
                NewDrop(d, false);
            }
        }
        //按时间表定格下一句
        if (m_clock >= RainPhraseStartAt(m_nextPhrase))
        {
            Phrase p;
            p.m_start = m_clock;
            p.m_line = RainPhraseLine(m_nextPhrase);
            p.m_cy = m_h * RandRangeF(kPhraseYMin, kPhraseYMax);
            m_phrases.push_back(p);
            ++m_nextPhrase;
        }
        const float total = RainPhraseDuration();
        for (size_t i = 0; i < m_phrases.size();)
        {
            if (m_clock - m_phrases[i].m_start >= total)
            {
                m_phrases.erase(m_phrases.begin() + i);
                continue;
            }
            ++i;
        }
    }

    void Draw(ID2D1RenderTarget* rt) override
    {
        if (!EnsureResources(rt))
        {
            return;
        }
        m_offscreen->BeginDraw();
        //按时间折算的淡化量：帧率不同，拖尾长度也大致相同
        const float fade = 1.0f - powf(1.0f - kTrailFadePerFrame, m_lastDt * kFramesPerSecond);
        m_offscreen->FillRectangle(D2D1::RectF(0.0f, 0.0f, m_w, m_h), Solid(kBackground, fade));
        DrawDrops(m_offscreen);
        for (size_t i = 0; i < m_phrases.size(); ++i)
        {
            DrawPhrase(m_offscreen, m_phrases[i]);
        }
        const HRESULT hr = m_offscreen->EndDraw();
        if (FAILED(hr))
        {
            //离屏位图失效（如显卡重置）：释放后下一帧重建
            DiscardDeviceResources();
            return;
        }
        CComPtr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(m_offscreen->GetBitmap(&bitmap)))
        {
            rt->DrawBitmap(bitmap, D2D1::RectF(0.0f, 0.0f, m_w, m_h));
        }
        if (m_vignette != NULL)
        {
            rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, m_w, m_h), m_vignette);
        }
    }

    bool ShowsCornerClock() const override
    {
        return false;
    }

private:
    wchar_t RandGlyph() const
    {
        const std::wstring& set = RainGlyphSet();
        return set.empty() ? L' ' : set[RandIndex(static_cast<int>(set.size()))];
    }

    void NewDrop(Drop& d, bool initial)
    {
        d.m_y = initial ? RandRangeF(kDropInitMin * m_rows, static_cast<float>(m_rows)) : RandRangeF(kDropRespawnMin, 0.0f);
        d.m_speed = RandRangeF(kDropSpeedMin, kDropSpeedMax);
        d.m_len = RandRangeF(kDropLenMin, kDropLenMax);
        d.m_row = static_cast<int>(floorf(d.m_y));
        d.m_head = RandGlyph();
        d.m_bright = RandRangeF(kDropBrightMin, kDropBrightMax);
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
        m_headGlow = MakeRadialBrush(m_offscreen, ColorRgb(kHeadGlow, 1.0f), ColorRgb(kHeadGlow, 0.0f));
        m_honorGlow = MakeRadialBrush(m_offscreen, ColorRgb(kHonorGlow, kHonorGlowAlpha), ColorRgb(kHonorGlow, 0.0f));
        m_shameGlow = MakeRadialBrush(m_offscreen, ColorRgb(kShameGlow, kShameGlowAlpha), ColorRgb(kShameGlow, 0.0f));
        m_streak = MakeLinearBrush(m_offscreen, ColorRgb(kStreakTop, 0.0f), ColorRgb(kStreakHead, kStreakHeadAlpha));
        //四角压暗层：画在窗口渲染目标上（不是离屏位图），以画面中心为圆心，半径之外按最暗处延伸（渐变默认 CLAMP）。
        //创建失败时不压暗，画面仍完整
        m_vignette = MakeRadialBrush(rt, ColorRgb(kVignetteColor, 0.0f), ColorRgb(kVignetteColor, kVignetteEdgeAlpha));
        if (m_vignette != NULL)
        {
            m_vignette->SetCenter(D2D1::Point2F(m_w / 2.0f, m_h / 2.0f));
            m_vignette->SetRadiusX(m_w / 2.0f * kVignetteRadiusFactor);
            m_vignette->SetRadiusY(m_h / 2.0f * kVignetteRadiusFactor);
        }
        //新的离屏位图内容未定义，先涂成底色
        m_offscreen->BeginDraw();
        m_offscreen->Clear(ColorRgb(kBackground));
        m_offscreen->EndDraw();
        return true;
    }

    ID2D1SolidColorBrush* Solid(uint32_t rgb, float alpha)
    {
        m_solid->SetColor(ColorRgb(rgb, alpha));
        return m_solid;
    }

    ID2D1SolidColorBrush* Solid(const D2D1_COLOR_F& color)
    {
        m_solid->SetColor(color);
        return m_solid;
    }

    // 在 (cx, cy) 为中心、边长 size 的格子里画一个字
    void DrawGlyphAt(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt, wchar_t ch, float cx, float cy, float size,
                     ID2D1Brush* brush)
    {
        //文字格式水平、竖直都居中，矩形取字号的两倍见方，字不会被裁掉
        const std::wstring s(1, ch);
        DrawTextIn(rt, fmt, s, D2D1::RectF(cx - size, cy - size, cx + size, cy + size), brush);
    }

    // 只画头部这一帧新走到的行：上一个头部的位置改画成金字，新位置画暖白色的头部
    void DrawDrops(ID2D1RenderTarget* rt)
    {
        const float fs = m_fs;
        for (int c = 0; c < m_cols; ++c)
        {
            Drop& d = m_drops[c];
            const int cur = static_cast<int>(floorf(d.m_y));
            if (cur == d.m_row)
            {
                continue;
            }
            const float x = c * fs + fs / 2.0f;
            for (int r = d.m_row + 1; r <= cur; ++r)
            {
                if (r - 1 >= 0)
                {
                    const float py = (r - 1) * fs;
                    rt->FillRectangle(D2D1::RectF(c * fs, py, c * fs + fs, py + fs), Solid(kBackground, 1.0f));
                    const wchar_t ch = (RandRangeF(0.0f, 1.0f) < kSwapGlyphChance) ? RandGlyph() : d.m_head;
                    DrawGlyphAt(rt, m_glyphFormat, ch, x, py + fs / 2.0f, fs, Solid(kTrailColor, d.m_bright));
                }
                d.m_head = RandGlyph();
                const float hy = r * fs + fs / 2.0f;
                DrawGlowAt(rt, m_headGlow, x, hy, fs * kHeadGlowRadius, fs * kHeadGlowRadius, kHeadGlowAlpha);
                DrawGlyphAt(rt, m_glyphFormat, d.m_head, x, hy, fs, Solid(kHeadColor, 1.0f));
            }
            d.m_row = cur;
        }
    }

    void DrawPhrase(ID2D1RenderTarget* rt, const Phrase& p)
    {
        const float t = m_clock - p.m_start;
        const float size0 = m_fs * kPhraseScale;
        const float gap = size0 * kPhraseGap;
        const float fade = 1.0f - Clamp01F((t - (RainPhraseDuration() - kRainFade)) / kRainFade);
        const std::wstring honor(HonorLineText(p.m_line, true));
        const std::wstring shame(HonorLineText(p.m_line, false));
        for (int i = 0; i < kHonorHalfChars * 2; ++i)
        {
            const bool isHonor = (i < kHonorHalfChars);
            const int k = isHonor ? i : i - kHonorHalfChars;
            const float size = isHonor ? size0 : size0 * kShameScale;
            const float x = m_w / 2.0f + (k - kCenterChar) * (isHonor ? gap : gap * kShameScale);
            const float y = isHonor ? p.m_cy : p.m_cy + size0 * kShameRowY;
            const float land = RainCharLandAt(i);
            if (t < land - kRainStreakSec)
            {
                continue;
            }
            if (t < land)
            {
                //落下的亮光：一道由透明到亮的竖线，头部到达字的位置时字出现
                const float q = (t - (land - kRainStreakSec)) / kRainStreakSec;
                const float headY = LerpF(y - m_h * kStreakFall, y, EaseOutCubicF(q));
                const float topY = headY - m_h * kStreakLength;
                if (m_streak != NULL)
                {
                    m_streak->SetStartPoint(D2D1::Point2F(x, topY));
                    m_streak->SetEndPoint(D2D1::Point2F(x, headY));
                    rt->DrawLine(D2D1::Point2F(x, topY), D2D1::Point2F(x, headY), m_streak, kStreakWidth * m_scale);
                }
                continue;
            }
            //字的底衬：深红色，加一圈金色细边，像一张斗方红纸
            const D2D1_RECT_F backing = D2D1::RectF(x - size * kBackingW, y - size * kBackingH, x + size * kBackingW,
                                                   y + size * kBackingH);
            const D2D1_ROUNDED_RECT backingRound = D2D1::RoundedRect(backing, size * kBackingRadius,
                                                                     size * kBackingRadius);
            rt->FillRoundedRectangle(backingRound, Solid(kBackingColor, kBackingAlpha * fade));
            rt->DrawRoundedRectangle(backingRound, Solid(kBackingBorder, kBackingBorderAlpha * fade),
                                     kBackingBorderWidth * m_scale);
            //刚落定时先是白光，随后变成金色或红色；光晕随之收小
            const float flash = Clamp01F((t - land) / kFlashDur);
            const D2D1_COLOR_F col = MixColor(ColorRgb(kFlashColor, fade), ColorRgb(isHonor ? kHonorColor : kShameColor, fade),
                                              flash);
            const float glowR = size * (kGlowBase + kGlowFlash * (1.0f - flash));
            DrawGlowAt(rt, isHonor ? m_honorGlow : m_shameGlow, x, y, glowR, glowR, kGlowAlphaScale * fade);
            DrawGlyphAt(rt, isHonor ? m_honorFormat : m_shameFormat, isHonor ? honor[k] : shame[k], x, y, size,
                        Solid(col));
        }
    }

private:
    float                             m_w;            // 画面宽
    float                             m_h;            // 画面高
    float                             m_scale;        // 缩放比例
    float                             m_fs;           // 字符雨字号（即列宽、行高）
    int                               m_cols;         // 列数
    int                               m_rows;         // 行数（多算一行，保证铺满）
    float                             m_clock;        // 场景时钟（秒）
    float                             m_lastDt;       // 上一帧的时长（秒），用来折算拖尾淡化量
    int                               m_nextPhrase;   // 下一次定格是第几次（RainPhraseStartAt 的参数）
    std::vector<Drop>                 m_drops;        // 每列一个
    std::vector<Phrase>               m_phrases;      // 正在定格的句子
    CComPtr<IDWriteTextFormat>        m_glyphFormat;  // 字符雨
    CComPtr<IDWriteTextFormat>        m_honorFormat;  // 定格的「荣」半句
    CComPtr<IDWriteTextFormat>        m_shameFormat;  // 定格的「耻」半句
    CComPtr<ID2D1BitmapRenderTarget>  m_offscreen;    // 保留上一帧内容的离屏位图，字符雨的拖尾画在这里
    CComPtr<ID2D1SolidColorBrush>     m_solid;        // 复用的纯色画刷（离屏位图上创建）
    CComPtr<ID2D1RadialGradientBrush> m_vignette;     // 四角压暗层（窗口渲染目标上创建，与离屏位图同时创建、释放）
    CComPtr<ID2D1RadialGradientBrush> m_headGlow;     // 头部光晕
    CComPtr<ID2D1RadialGradientBrush> m_honorGlow;    // 「荣」半句光晕
    CComPtr<ID2D1RadialGradientBrush> m_shameGlow;    // 「耻」半句光晕
    CComPtr<ID2D1LinearGradientBrush> m_streak;       // 定格前落下的亮光
};

} // namespace

ILockScene* CreateGlyphRainScene()
{
    return new CGlyphRainScene();
}

} // namespace flamingolock
