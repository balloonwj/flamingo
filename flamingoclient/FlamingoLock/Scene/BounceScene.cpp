/**
 *  锁屏动画「弹跳标志与烟花」：致敬老式 DVD 待机画面。Flamingo 标志在黑底上斜向弹跳，每碰一次边换一种颜色并
 *  沿碰撞方向压扁一下；恰好撞进角落时从角落喷出火花，并从屏幕底部接连放几枚烟花，屏幕中间弹出「正中角落！」。
 *  样式取自样稿 tmp/lockscreen/lock_preview.html（场景四），尺寸为样稿 CSS 像素乘以缩放比例。
 *  运动与撞角判定在 SceneModels.cpp（StepBounce），这里只负责火花、烟花与绘制。Direct2D 1.0 没有加色混合与
 *  模糊，火花按普通透明度叠加，标志与文字的发光用径向渐变光晕近似（docs/锁屏方案.md 第 7.1 节）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "../Render/LockScene.h"
#include "Scenes.h"
#include "SceneUtil.h"

#include <vector>

#include "../../Source/Common/I18n/I18n.h"

namespace flamingolock {

namespace {

// ---- 颜色 ----
const int kColorCount = 7;                // 标志与火花轮换的颜色个数
const uint32_t kColors[kColorCount] =
{
    0xFF5C8A, 0xFFD93D, 0x6BCB77, 0x4D96FF, 0xC77DFF, 0xFF8C42, 0x2EE6D6,
};
const uint32_t kBackground = 0x000000;    // 黑底
const uint32_t kRocketColor = 0xFFE6B4;   // 上升中的烟花尾迹
const float kRocketAlpha = 0.9f;
const uint32_t kMessageColor = 0xFFFFFF;  // 「正中角落！」
const uint32_t kCountColor = 0xFFFFFF;    // 右下角的累计次数
const float kCountAlpha = 0.45f;

// ---- 标志（样稿 CSS 像素，乘以缩放比例）----
const float kLogoWidthRatio = 0.045f;     // 标志字号占画面宽度的比例
const float kLogoFontMin = 34.0f;         // 标志字号范围
const float kLogoFontMax = 92.0f;
const float kLogoHeightRatio = 1.3f;      // 标志高度与字号之比
const float kGlyphBoxRatio = 0.8f;        // 火烈鸟小图形的边长与标志高度之比
const float kGlyphGapRatio = 0.25f;       // 小图形与文字的间距与字号之比
const float kLogoTextY = 0.47f;           // 文字中线在标志高度上的位置
const float kGlyphUnit = 100.0f;          // 小图形的设计尺寸（单位方框边长）
const float kSpeedRatio = 0.11f;          // 弹跳速度占画面长边的比例（每秒）
const float kSpeedYRatio = 0.78f;         // 竖直速度与水平速度之比
const float kSquashDecay = 6.0f;          // 压扁量每秒衰减多少
const float kSquashAmount = 0.14f;        // 压扁的最大比例
const float kSquashBulge = 0.5f;          // 另一个方向鼓出的量与压扁量之比
const float kLogoGlowX = 0.65f;           // 标志背后光晕的横向半径与标志宽度之比
const float kLogoGlowY = 0.9f;            // 纵向半径与标志高度之比
const float kLogoGlowAlpha = 0.32f;

// ---- 小图形各部分（设计尺寸 100 的方框里）----
const float kBodyCx = 46.0f;              // 身体椭圆的中心与半径
const float kBodyCy = 58.0f;
const float kBodyRx = 26.0f;
const float kBodyRy = 14.0f;
const float kBodyTilt = -0.15f;           // 身体椭圆的倾斜（弧度）
const float kNeckWidth = 7.0f;            // 脖子线宽
const float kHeadCx = 68.0f;              // 头的圆心与半径
const float kHeadCy = 10.0f;
const float kHeadR = 7.0f;
const float kBeakWidth = 4.0f;            // 喙的线宽
const float kLegWidth = 3.5f;             // 腿的线宽

// ---- 火花与烟花（样稿 CSS 像素，乘以缩放比例）----
const float kGravity = 170.0f;            // 火花受到的重力（像素 / 秒²）
const float kRocketAccelRatio = 1.4f;     // 烟花上升时受到的加速度与火花重力之比
const int kRocketsPerCorner = 6;          // 每次正中角落放几枚烟花
const float kRocketFirstDelay = 0.25f;    // 第一枚烟花在撞角后多久发射（秒）
const float kRocketInterval = 0.32f;      // 相邻两枚烟花的发射间隔（秒）
const float kFuseMin = 0.9f;              // 引信时长范围（秒）
const float kFuseMax = 1.2f;
const float kBurstYMin = 0.18f;           // 炸开高度占画面高度的范围
const float kBurstYMax = 0.5f;
const float kRocketXMin = 0.15f;          // 发射横坐标占画面宽度的范围
const float kRocketXMax = 0.85f;
const float kRocketDrift = 40.0f;         // 烟花横向漂移速度的上限（像素 / 秒）
const float kRocketStartBelow = 10.0f;    // 烟花从画面底边以下多少像素升起
const int kBurstSparks = 150;             // 一枚烟花炸出的火花数
const float kBurstSpeedMin = 380.0f;      // 炸开速度上限的范围（像素 / 秒）
const float kBurstSpeedMax = 520.0f;
const float kBurstSpeedLow = 0.25f;       // 每粒火花的速度在 [上限 × 0.25, 上限] 之间
const float kSparkLifeMin = 1.1f;         // 烟花火花的寿命范围（秒）
const float kSparkLifeMax = 2.0f;
const int kCornerSparks = 120;            // 撞角时角落里喷出的火花数
const float kCornerSpread = 0.7f;         // 喷射方向偏离「朝向画面中心」的最大角度（弧度）
const float kCornerSpeedMin = 200.0f;     // 喷射速度范围（像素 / 秒）
const float kCornerSpeedMax = 780.0f;
const float kCornerLifeMin = 1.0f;        // 喷射火花的寿命范围（秒）
const float kCornerLifeMax = 1.8f;
const float kSparkDrag = 1.4f;            // 火花的空气阻力（1 / 秒）
const float kSparkTail = 0.035f;          // 火花尾迹的长度（按速度折算的秒数）
const float kRocketTail = 0.05f;          // 烟花尾迹的长度（按速度折算的秒数）
const float kSparkWidth = 2.2f;           // 火花线宽
const float kRocketWidth = 2.4f;          // 烟花线宽
const float kFlickerFreq = 40.0f;         // 火花闪烁频率
const float kFlickerBase = 0.65f;         // 闪烁的最暗与幅度
const float kFlickerRange = 0.35f;

// ---- 「正中角落！」----
const float kMsgPop = 0.35f;              // 弹出动画时长（秒）
const float kMsgFadeStart = 1.9f;         // 开始淡出的时刻（秒）
const float kMsgFadeDur = 0.7f;           // 淡出时长（秒）
const float kMsgDur = 2.6f;               // 总时长（秒）
const float kMsgHeightRatio = 0.09f;      // 字号占画面高度的比例
const float kMsgFontMin = 40.0f;          // 字号范围
const float kMsgFontMax = 110.0f;
const float kMsgY = 0.42f;                // 中心在画面高度上的位置
const float kMsgStartScale = 0.4f;        // 弹出时的起始缩放
const float kMsgGlowX = 3.2f;             // 文字背后光晕的半径（以字号计）
const float kMsgGlowY = 0.9f;
const float kMsgGlowAlpha = 0.45f;
const float kCountFont = 14.0f;           // 右下角累计次数的字号
const float kCountRight = 24.0f;          // 距右边与下边的距离
const float kCountBottom = 22.0f;

// 标志文字（产品名，任何界面语言下都一样）
const wchar_t* const kLogoText = L"Flamingo";
// 标志字体候选：系统有 Segoe UI 就用它
const wchar_t* const kLogoFonts[] = { L"Segoe UI", L"Arial" };
const int kLogoFontCount = 2;

// 一粒火花
struct Spark
{
    float    m_x;       // 横坐标
    float    m_y;       // 纵坐标
    float    m_vx;      // 横向速度
    float    m_vy;      // 纵向速度
    float    m_life;    // 寿命（秒）
    float    m_t;       // 已存在的秒数
    uint32_t m_color;   // 颜色 0xRRGGBB
};

// 一枚上升中的烟花
struct Rocket
{
    float m_x;      // 横坐标
    float m_y;      // 纵坐标
    float m_vx;     // 横向速度
    float m_vy;     // 纵向速度
    float m_fuse;   // 引信时长（秒）
    float m_t;      // 已飞行的秒数
};

/**
 *  弹跳标志与烟花场景。
 */
class CBounceScene : public ILockScene
{
public:
    CBounceScene()
        : m_w(0.0f)
        , m_h(0.0f)
        , m_scale(1.0f)
        , m_logoFont(0.0f)
        , m_textWidth(0.0f)
        , m_squash(0.0f)
        , m_squashX(true)
        , m_msgT(-1.0f)
        , m_msgFont(0.0f)
        , m_dwrite(NULL)
    {
        ResetBounceModel(m_model);
    }

    void Init(const SceneEnv& env, float width, float height) override
    {
        m_w = width;
        m_h = height;
        m_scale = env.m_scale;
        m_dwrite = env.m_dwrite;
        m_logoFont = ClampF(width * kLogoWidthRatio, kLogoFontMin * m_scale, kLogoFontMax * m_scale);
        const std::wstring logoFamily = PickFontFamily(env.m_dwrite, kLogoFonts, kLogoFontCount);
        m_logoFormat.Release();
        env.m_dwrite->CreateTextFormat(logoFamily.c_str(), NULL, DWRITE_FONT_WEIGHT_EXTRA_BOLD,
                                       DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL, m_logoFont, L"",
                                       &m_logoFormat);
        if (m_logoFormat != NULL)
        {
            m_logoFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_logoFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_logoFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        m_textWidth = MeasureTextWidth(env.m_dwrite, m_logoFormat, kLogoText);
        m_msgFont = ClampF(height * kMsgHeightRatio, kMsgFontMin * m_scale, kMsgFontMax * m_scale);
        const std::wstring uiFont = i18n::UiFontName();
        m_msgFormat = MakeTextFormat(env.m_dwrite, uiFont, m_msgFont, DWRITE_FONT_WEIGHT_EXTRA_BOLD,
                                     DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_countFormat = MakeTextFormat(env.m_dwrite, uiFont, kCountFont * m_scale, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        BuildGlyph(env.m_d2d);
        m_round = MakeRoundStroke(env.m_d2d);

        //运动状态：时刻与计数清零后再摆位置与速度
        ResetBounceModel(m_model);
        m_model.m_h = m_logoFont * kLogoHeightRatio;
        m_model.m_w = m_model.m_h * kGlyphBoxRatio + m_logoFont * kGlyphGapRatio + m_textWidth;
        m_model.m_x = RandRangeF(0.0f, (width > m_model.m_w) ? width - m_model.m_w : 0.0f);
        m_model.m_y = RandRangeF(0.0f, (height > m_model.m_h) ? height - m_model.m_h : 0.0f);
        const float speed = ((width > height) ? width : height) * kSpeedRatio;
        m_model.m_vx = speed * ((rand() % 2 == 0) ? -1.0f : 1.0f);
        m_model.m_vy = speed * kSpeedYRatio * ((rand() % 2 == 0) ? -1.0f : 1.0f);
        m_squash = 0.0f;
        m_msgT = -1.0f;
        m_sparks.clear();
        m_rockets.clear();
        m_launchQueue.clear();
        DiscardDeviceResources();
    }

    void DiscardDeviceResources() override
    {
        m_solid.Release();
        for (int i = 0; i < kColorCount; ++i)
        {
            m_glow[i].Release();
        }
    }

    void Update(float dt, uint64_t /*wallMs*/) override
    {
        const BounceStep step = StepBounce(m_model, dt, m_w, m_h, kColorCount);
        if (step.m_hitX || step.m_hitY)
        {
            m_squash = 1.0f;
            m_squashX = step.m_hitX;
        }
        if (step.m_corner)
        {
            Celebrate(step.m_cornerX, step.m_cornerY);
        }
        m_squash = (m_squash - dt * kSquashDecay > 0.0f) ? m_squash - dt * kSquashDecay : 0.0f;

        //到点的烟花从底部升起：先定炸开的高度与引信时长，再反推初速度，保证在画面内炸开
        const float accel = kGravity * kRocketAccelRatio * m_scale;
        while (!m_launchQueue.empty() && m_launchQueue.front() <= m_model.m_clock)
        {
            m_launchQueue.erase(m_launchQueue.begin());
            Rocket r;
            r.m_fuse = RandRangeF(kFuseMin, kFuseMax);
            r.m_x = RandRangeF(m_w * kRocketXMin, m_w * kRocketXMax);
            r.m_y = m_h + kRocketStartBelow * m_scale;
            r.m_vx = RandRangeF(-kRocketDrift, kRocketDrift) * m_scale;
            r.m_vy = RocketLaunchVelocity(r.m_y, m_h * RandRangeF(kBurstYMin, kBurstYMax), r.m_fuse, accel);
            r.m_t = 0.0f;
            m_rockets.push_back(r);
        }
        for (size_t i = 0; i < m_rockets.size();)
        {
            Rocket& r = m_rockets[i];
            r.m_t += dt;
            r.m_vy += accel * dt;
            r.m_x += r.m_vx * dt;
            r.m_y += r.m_vy * dt;
            if (r.m_t >= r.m_fuse)
            {
                Burst(r.m_x, r.m_y, kBurstSparks, RandRangeF(kBurstSpeedMin, kBurstSpeedMax) * m_scale);
                m_rockets.erase(m_rockets.begin() + i);
                continue;
            }
            ++i;
        }
        const float drag = expf(-kSparkDrag * dt);
        const float gravity = kGravity * m_scale;
        std::vector<Spark> alive;
        alive.reserve(m_sparks.size());
        for (size_t i = 0; i < m_sparks.size(); ++i)
        {
            Spark s = m_sparks[i];
            s.m_t += dt;
            if (s.m_t >= s.m_life)
            {
                continue;
            }
            s.m_vx *= drag;
            s.m_vy = s.m_vy * drag + gravity * dt;
            s.m_x += s.m_vx * dt;
            s.m_y += s.m_vy * dt;
            alive.push_back(s);
        }
        m_sparks.swap(alive);
        if (m_msgT >= 0.0f)
        {
            m_msgT += dt;
            if (m_msgT > kMsgDur)
            {
                m_msgT = -1.0f;
            }
        }
    }

    void Draw(ID2D1RenderTarget* rt) override
    {
        if (!EnsureResources(rt))
        {
            return;
        }
        rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, m_w, m_h), Solid(kBackground, 1.0f));
        DrawSparks(rt);
        DrawLogo(rt);
        DrawMessage(rt);
    }

    bool ShowsCornerClock() const override
    {
        //满屏只有标志在弹才像老式待机画面，不显示左下角时间
        return false;
    }

private:
    // 预先建好小图形的几何（设计尺寸 100 的方框里，绘制时按标志大小缩放）
    void BuildGlyph(ID2D1Factory* d2d)
    {
        CPathBuilder pb;
        m_neck.Release();
        if (pb.Begin(d2d))
        {
            pb.MoveTo(64.0f, 50.0f, false);
            pb.CubicTo(84.0f, 36.0f, 50.0f, 22.0f, 66.0f, 10.0f);
            m_neck = pb.Finish();
        }
        m_beak.Release();
        if (pb.Begin(d2d))
        {
            pb.MoveTo(72.0f, 8.0f, false);
            pb.QuadTo(84.0f, 10.0f, 82.0f, 22.0f);
            m_beak = pb.Finish();
        }
        m_legs.Release();
        if (pb.Begin(d2d))
        {
            pb.MoveTo(44.0f, 70.0f, false);
            pb.LineTo(46.0f, 98.0f);
            pb.MoveTo(44.0f, 70.0f, false);
            pb.LineTo(34.0f, 84.0f);
            pb.LineTo(42.0f, 90.0f);
            m_legs = pb.Finish();
        }
    }

    bool EnsureResources(ID2D1RenderTarget* rt)
    {
        if (m_solid != NULL)
        {
            return true;
        }
        if (FAILED(rt->CreateSolidColorBrush(ColorRgb(kBackground), &m_solid)))
        {
            return false;
        }
        for (int i = 0; i < kColorCount; ++i)
        {
            m_glow[i] = MakeRadialBrush(rt, ColorRgb(kColors[i], 1.0f), ColorRgb(kColors[i], 0.0f));
        }
        return true;
    }

    ID2D1SolidColorBrush* Solid(uint32_t rgb, float alpha)
    {
        m_solid->SetColor(ColorRgb(rgb, alpha));
        return m_solid;
    }

    // 从 (x, y) 向四周炸出 count 粒火花，速度上限 speedMax（物理像素 / 秒），颜色随机
    void Burst(float x, float y, int count, float speedMax)
    {
        for (int i = 0; i < count; ++i)
        {
            const float a = RandRangeF(0.0f, kSceneTwoPi);
            const float sp = RandRangeF(speedMax * kBurstSpeedLow, speedMax);
            Spark s;
            s.m_x = x;
            s.m_y = y;
            s.m_vx = cosf(a) * sp;
            s.m_vy = sinf(a) * sp;
            s.m_life = RandRangeF(kSparkLifeMin, kSparkLifeMax);
            s.m_t = 0.0f;
            s.m_color = kColors[RandIndex(kColorCount)];
            m_sparks.push_back(s);
        }
    }

    // 正中角落：角落里朝画面里面喷一股火花，排好几枚烟花，弹出提示
    void Celebrate(float cx, float cy)
    {
        m_msgT = 0.0f;
        const float base = atan2f(m_h / 2.0f - cy, m_w / 2.0f - cx);
        for (int i = 0; i < kCornerSparks; ++i)
        {
            const float a = base + RandRangeF(-kCornerSpread, kCornerSpread);
            const float sp = RandRangeF(kCornerSpeedMin, kCornerSpeedMax) * m_scale;
            Spark s;
            s.m_x = cx;
            s.m_y = cy;
            s.m_vx = cosf(a) * sp;
            s.m_vy = sinf(a) * sp;
            s.m_life = RandRangeF(kCornerLifeMin, kCornerLifeMax);
            s.m_t = 0.0f;
            s.m_color = kColors[i % kColorCount];
            m_sparks.push_back(s);
        }
        for (int i = 0; i < kRocketsPerCorner; ++i)
        {
            m_launchQueue.push_back(m_model.m_clock + kRocketFirstDelay + kRocketInterval * i);
        }
    }

    void DrawSparks(ID2D1RenderTarget* rt)
    {
        for (size_t i = 0; i < m_rockets.size(); ++i)
        {
            const Rocket& r = m_rockets[i];
            rt->DrawLine(D2D1::Point2F(r.m_x, r.m_y),
                         D2D1::Point2F(r.m_x - r.m_vx * kRocketTail, r.m_y - r.m_vy * kRocketTail),
                         Solid(kRocketColor, kRocketAlpha), kRocketWidth * m_scale, m_round);
        }
        for (size_t i = 0; i < m_sparks.size(); ++i)
        {
            const Spark& s = m_sparks[i];
            const float life = 1.0f - s.m_t / s.m_life;
            const float flicker = kFlickerBase + kFlickerRange * sinf(s.m_t * kFlickerFreq + s.m_x);
            rt->DrawLine(D2D1::Point2F(s.m_x, s.m_y),
                         D2D1::Point2F(s.m_x - s.m_vx * kSparkTail, s.m_y - s.m_vy * kSparkTail),
                         Solid(s.m_color, Clamp01F(life * flicker)), kSparkWidth * m_scale, m_round);
        }
    }

    // 标志：碰边后沿碰撞方向压扁一点再弹回
    void DrawLogo(ID2D1RenderTarget* rt)
    {
        const uint32_t color = kColors[m_model.m_colorIndex % kColorCount];
        const float w = m_model.m_w;
        const float h = m_model.m_h;
        const float cx = m_model.m_x + w / 2.0f;
        const float cy = m_model.m_y + h / 2.0f;
        DrawGlowAt(rt, m_glow[m_model.m_colorIndex % kColorCount], cx, cy, w * kLogoGlowX, h * kLogoGlowY,
                   kLogoGlowAlpha);

        const float sq = m_squash * kSquashAmount;
        const float sx = m_squashX ? 1.0f - sq : 1.0f + sq * kSquashBulge;
        const float sy = m_squashX ? 1.0f + sq * kSquashBulge : 1.0f - sq;
        D2D1_MATRIX_3X2_F old;
        rt->GetTransform(&old);
        const D2D1_MATRIX_3X2_F logo = D2D1::Matrix3x2F::Translation(-w / 2.0f, -h / 2.0f) *
                                       D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(cx, cy) * old;
        //小图形：设计尺寸 100 的方框缩放到标志高度的八成
        const float glyphScale = h * kGlyphBoxRatio / kGlyphUnit;
        const D2D1_MATRIX_3X2_F glyph = D2D1::Matrix3x2F::Scale(glyphScale, glyphScale) * logo;
        ID2D1SolidColorBrush* brush = Solid(color, 1.0f);
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(kBodyTilt * kSceneRadToDeg, D2D1::Point2F(kBodyCx, kBodyCy)) * glyph);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kBodyCx, kBodyCy), kBodyRx, kBodyRy), brush);
        rt->SetTransform(glyph);
        if (m_neck != NULL)
        {
            rt->DrawGeometry(m_neck, brush, kNeckWidth, m_round);
        }
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kHeadCx, kHeadCy), kHeadR, kHeadR), brush);
        if (m_beak != NULL)
        {
            rt->DrawGeometry(m_beak, brush, kBeakWidth, m_round);
        }
        if (m_legs != NULL)
        {
            rt->DrawGeometry(m_legs, brush, kLegWidth, m_round);
        }
        //文字
        rt->SetTransform(logo);
        const float textX = h * kGlyphBoxRatio + m_logoFont * kGlyphGapRatio;
        const float textY = h * kLogoTextY;
        DrawTextIn(rt, m_logoFormat, kLogoText,
                   D2D1::RectF(textX, textY - m_logoFont, textX + m_textWidth + m_logoFont, textY + m_logoFont), brush);
        rt->SetTransform(old);
    }

    // 「正中角落！」与右下角的累计次数
    void DrawMessage(ID2D1RenderTarget* rt)
    {
        if (m_msgT >= 0.0f)
        {
            const float p = Clamp01F(m_msgT / kMsgPop);
            const float a = 1.0f - Clamp01F((m_msgT - kMsgFadeStart) / kMsgFadeDur);
            const float sc = LerpF(kMsgStartScale, 1.0f, EaseOutBackF(p));
            const float cx = m_w / 2.0f;
            const float cy = m_h * kMsgY;
            D2D1_MATRIX_3X2_F old;
            rt->GetTransform(&old);
            rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(cx, cy)) * old);
            DrawGlowAt(rt, m_glow[m_model.m_colorIndex % kColorCount], cx, cy, m_msgFont * kMsgGlowX,
                       m_msgFont * kMsgGlowY, kMsgGlowAlpha * a);
            DrawTextIn(rt, m_msgFormat, TR(L"正中角落！"),
                       D2D1::RectF(0.0f, cy - m_msgFont, m_w, cy + m_msgFont), Solid(kMessageColor, a));
            rt->SetTransform(old);
        }
        if (m_model.m_corners > 0)
        {
            const std::wstring text = TRF(L"已正中角落 {0} 次", m_model.m_corners);
            DrawTextIn(rt, m_countFormat, text,
                       D2D1::RectF(0.0f, 0.0f, m_w - kCountRight * m_scale, m_h - kCountBottom * m_scale),
                       Solid(kCountColor, kCountAlpha));
        }
    }

private:
    float                             m_w;                     // 画面宽
    float                             m_h;                     // 画面高
    float                             m_scale;                 // 缩放比例
    float                             m_logoFont;              // 标志字号
    float                             m_textWidth;             // 「Flamingo」的宽度
    BounceModel                       m_model;                 // 标志的运动状态与撞角计数
    float                             m_squash;                // 压扁量 0~1，碰边时置 1 后衰减
    bool                              m_squashX;               // 本次压扁是否由碰左右边引起
    float                             m_msgT;                  // 「正中角落！」已显示的秒数；-1 表示不显示
    float                             m_msgFont;               // 「正中角落！」的字号
    std::vector<Spark>                m_sparks;                // 火花
    std::vector<Rocket>               m_rockets;               // 上升中的烟花
    std::vector<float>                m_launchQueue;           // 待发射烟花的发射时刻（场景时钟，升序）
    IDWriteFactory*                   m_dwrite;                // DirectWrite 工厂（借用）
    CComPtr<IDWriteTextFormat>        m_logoFormat;            // 标志文字
    CComPtr<IDWriteTextFormat>        m_msgFormat;             // 「正中角落！」
    CComPtr<IDWriteTextFormat>        m_countFormat;           // 右下角累计次数
    CComPtr<ID2D1PathGeometry>        m_neck;                  // 小图形的脖子
    CComPtr<ID2D1PathGeometry>        m_beak;                  // 小图形的喙
    CComPtr<ID2D1PathGeometry>        m_legs;                  // 小图形的腿
    CComPtr<ID2D1StrokeStyle>         m_round;                 // 圆头描边
    CComPtr<ID2D1SolidColorBrush>     m_solid;                 // 复用的纯色画刷
    CComPtr<ID2D1RadialGradientBrush> m_glow[kColorCount];     // 每种颜色一支光晕画刷
};

} // namespace

ILockScene* CreateBounceScene()
{
    return new CBounceScene();
}

} // namespace flamingolock
