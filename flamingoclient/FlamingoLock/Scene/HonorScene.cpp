/**
 *  锁屏动画「八荣八耻」：深红底、金色书法字。八句逐句出现，先「荣」半句逐字浮现，再「耻」半句；
 *  八句播完显示全文总览并盖上「知荣明耻」印章，然后循环。样式取自样稿 tmp/lockscreen/lock_preview.html
 *  （场景一），尺寸为样稿 CSS 像素乘以缩放比例。Direct2D 1.0 没有模糊特效，字的「由模糊到清晰」改为由大到小、
 *  由淡到实；发光改为字背后的径向渐变光晕（docs/锁屏方案.md 第 7.1 节）。
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
const uint32_t kBgCenter = 0xA01C1C;     // 背景径向渐变：中心
const uint32_t kBgMiddle = 0x6E0D0D;     // 中段
const uint32_t kBgEdge = 0x2E0404;       // 边缘
const float kBgMiddleStop = 0.55f;       // 中段所在位置
const uint32_t kGold = 0xF0C45A;         // 边框与装饰的金色
const uint32_t kGoldLight = 0xFFF3C4;    // 金字渐变：上
const uint32_t kGoldMid = 0xF7C548;      // 中
const uint32_t kGoldDark = 0xB8780E;     // 下
const float kGoldMidStop = 0.45f;        // 金字渐变中段位置
const uint32_t kTitleGold = 0xF8D682;    // 标题的淡金色
const uint32_t kIvory = 0xF3E4D4;        // 「耻」半句的象牙白
const uint32_t kShameRed = 0xE3A1A1;     // 「耻」字的淡红
const uint32_t kSealRed = 0xC8161D;      // 印章红
const uint32_t kSealInk = 0xFFF1E6;      // 印章上的字与内框
const uint32_t kBadgeRed = 0xC21F1F;     // 「其一」签的底色
const uint32_t kBadgeText = 0xFFF4DC;    // 「其一」签的字
const uint32_t kDustColor = 0xFFD678;    // 金粉
const uint32_t kGlowColor = 0xFFAA46;    // 字背后的暖光
const uint32_t kShadow = 0x000000;       // 字的投影

// ---- 比例与尺寸（样稿 CSS 像素，乘以缩放比例）----
const float kMarginRatio = 0.045f;       // 边框到画面边缘的距离占短边的比例
const float kCornerRatio = 0.035f;       // 角饰长度占短边的比例
const float kFrameInner = 7.0f;          // 内框比外框缩进的距离
const float kFrameOuterWidth = 1.6f;     // 外框线宽
const float kFrameInnerWidth = 0.7f;     // 内框线宽
const float kCornerWidth = 2.0f;         // 角饰线宽
const float kCornerNear = 14.0f;         // 角饰内折线离角的距离
const float kCornerFar = 22.0f;          // 角饰外折线离角的距离
const float kFrameAlpha = 0.55f;         // 边框不透明度
const float kCornerAlpha = 0.75f;        // 角饰不透明度
const float kDustDensity = 7000.0f;      // 每多少平方像素一粒金粉
const float kDustMinR = 0.6f;            // 金粉半径范围
const float kDustMaxR = 2.2f;
const float kDustMinV = 8.0f;            // 金粉上升速度范围（像素 / 秒）
const float kDustMaxV = 28.0f;
const float kDustSway = 8.0f;            // 金粉左右飘动的幅度（像素 / 秒）
const float kDustSwayFreq = 0.8f;        // 飘动频率
const float kDustTwinkleFreq = 1.4f;     // 闪烁频率
const float kDustMinAlpha = 0.25f;       // 闪烁最暗与幅度
const float kDustAlphaRange = 0.55f;
const float kTitleRatio = 0.032f;        // 标题字号占画面高度的比例
const float kTitleMin = 18.0f;           // 标题字号范围
const float kTitleMax = 34.0f;
const float kTitleSpacing = 0.5f;        // 标题字距占字号的比例
const float kTitleY = 2.0f;              // 标题中心距边框的距离（以标题字号计）
const float kSubtitleY = 3.4f;           // 副标题中心距边框的距离（以标题字号计）
const float kSubtitleScale = 0.62f;      // 副标题字号与标题之比
const float kSubtitleAlpha = 0.6f;
const float kLineWidthRatio = 0.085f;    // 主句字号占画面宽度的比例
const float kLineHeightRatio = 0.14f;    // 主句字号占画面高度的比例
const float kLineMax = 150.0f;           // 主句字号上限
const float kShameScale = 0.72f;         // 「耻」半句字号与「荣」半句之比
const float kCharGap = 0.08f;            // 字距占字号的比例
const float kRevealScale = 0.35f;        // 字浮现时的放大量（1.35 → 1）
const float kHonorRowY = -0.2f;          // 「荣」半句中心相对画面中心的纵向偏移（以主句字号计）
const float kShameRowY = 0.95f;          // 「耻」半句中心的纵向偏移
const float kBadgeY = -1.45f;            // 「其一」签中心的纵向偏移
const float kBadgeScale = 0.3f;          // 「其一」签字号与主句之比
const float kBadgeW = 2.4f;              // 签的宽、高（以签字号计）
const float kBadgeH = 1.25f;
const float kDotsY = 1.85f;              // 进度点的纵向偏移
const float kDotStep = 22.0f;            // 进度点间距
const float kDotR = 3.5f;                // 进度点半径
const float kDotCurrentR = 5.0f;         // 当前句的进度点半径
const float kDotAlpha = 0.9f;
const float kGlowRadius = 6.0f;          // 句子背后暖光的半径（以主句字号计）
const float kGlowAlpha = 0.16f;
const float kCharGlowAlpha = 0.35f;      // 「荣」半句每个字背后光晕的不透明度
const float kRongGlowAlpha = 0.6f;       // 「荣」字光晕的基础不透明度
const float kRongGlowPulse = 0.25f;      // 「荣」字光晕的呼吸幅度
const float kRongGlowFreq = 3.0f;        // 呼吸频率
const float kShadowOffset = 0.03f;       // 投影偏移（以字号计）
const float kShadowAlpha = 0.35f;
const float kSummaryHeightRatio = 0.05f; // 总览字号占画面高度的比例
const float kSummaryWidthRatio = 0.028f; // 总览字号占画面宽度的比例
const float kSummaryRowH = 1.6f;         // 总览行高（以总览字号计）
const float kSummaryCenterY = 0.56f;     // 总览表格中心在画面高度上的位置
const float kSummaryColGap = 1.2f;       // 两列间距（以总览字号计）
const float kSummaryRowRise = 14.0f;     // 每行出现时从下往上滑动的距离
const float kSummaryRowFade = 0.6f;      // 每行出现的时长（秒）
const float kSealSize = 2.3f;            // 印章边长（以总览字号计）
const float kSealRotation = -0.1f;       // 印章旋转（弧度）
const float kSealStartScale = 2.4f;      // 落印时的起始放大
const float kSealDur = 0.45f;            // 落印时长（秒）
const float kSealAlpha = 0.92f;
const float kSealInner = 0.42f;          // 印章内框半宽（以边长计）
const float kSealBorder = 0.04f;         // 内框线宽（以边长计）
const float kSealCharSize = 0.36f;       // 印章字号（以边长计）
const float kSealCharOffset = 0.2f;      // 印章四字离中心的偏移（以边长计）
const float kSealRadius = 0.08f;         // 印章圆角（以边长计）
const float kSealFadeIn = 1.6f;          // 印章不透明度比缩放先到位：落印进度乘以这个值再截断到 1
const float kSealInkAlpha = 0.9f;        // 印章内框的不透明度
const float kBgRadiusRatio = 0.75f;      // 背景径向渐变的半径占画面长边的比例
const float kBgCenterY = 0.45f;          // 背景渐变中心在画面高度上的位置
const float kGoldTop = 0.55f;            // 金字竖直渐变的起点在字中心以上多远（以字号计）
const float kGoldBottom = 0.45f;         // 终点在字中心以下多远（以字号计）
const float kCharGlowRatio = 0.75f;      // 普通金字背后光晕的半径（以字号计）；「荣」字为整个字号
const float kCornerReach = 1.6f;         // 角饰内折线的长度（以角饰长度计）
const float kBadgeRadius = 5.0f;         // 「其一」签的圆角
const float kBadgeBorderAlpha = 0.85f;   // 签边框的不透明度
const float kBadgeBorder = 1.5f;         // 签边框线宽
const float kDotPastAlpha = 0.6f;        // 已播过的句子的进度点不透明度（当前句为 1）
const float kDotFutureAlpha = 0.5f;      // 还没播的句子的进度点（空心）不透明度
const float kDotFutureWidth = 1.2f;      // 空心进度点的线宽
const float kSummaryDotR = 0.07f;        // 总览每行中间小圆点的半径（以总览字号计）
const float kSummaryDotAlpha = 0.7f;     // 小圆点的不透明度
const float kSealRowY = 6.5f;            // 印章中心落在总览第几行（自 0 起，取两行之间）
const float kSealRight = 0.75f;          // 印章中心在「耻」半句右端以右多远（以印章边长计）
const float kSummaryHalfRow = 0.5f;      // 总览第一行中心在表格顶边以下半行

// 印章与标题文字（宣传文案的一部分，任何界面语言下都显示中文）
// i18n-ignore-begin: 「社会主义荣辱观」「八荣八耻」「知荣明耻」与「其一~其八」属于八荣八耻的宣传文案，不翻译
const wchar_t* const kTitle = L"社会主义荣辱观";
const wchar_t* const kSubtitle = L"—  八 荣 八 耻  —";
const wchar_t* const kSealChars[4] = { L"知", L"荣", L"明", L"耻" };
const wchar_t* const kCnNumbers[kHonorLineCount] = { L"一", L"二", L"三", L"四", L"五", L"六", L"七", L"八" };
const wchar_t* const kBadgePrefix = L"其";
const wchar_t kRongChar = L'荣';
const wchar_t kChiChar = L'耻';
// i18n-ignore-end

// 书法字体候选：系统装了华文行楷就用它，否则退回楷体
const wchar_t* const kBrushFonts[] = { L"STXingkai", L"华文行楷", L"STKaiti", L"华文楷体", L"KaiTi", L"楷体" }; //i18n-ignore: 字体名
const int kBrushFontCount = 6;

// 一粒上升的金粉
struct Dust
{
    float m_x;      // 横坐标
    float m_y;      // 纵坐标
    float m_r;      // 半径
    float m_v;      // 上升速度
    float m_sway;   // 飘动相位
    float m_tw;     // 闪烁相位
};

/**
 *  八荣八耻场景。
 */
class CHonorScene : public ILockScene
{
public:
    CHonorScene()
        : m_w(0.0f)
        , m_h(0.0f)
        , m_scale(1.0f)
        , m_clock(0.0f)
        , m_dwrite(NULL)
    {
    }

    void Init(const SceneEnv& env, float width, float height) override
    {
        m_w = width;
        m_h = height;
        m_scale = env.m_scale;
        m_dwrite = env.m_dwrite;
        m_clock = 0.0f;
        m_brushFont = PickFontFamily(env.m_dwrite, kBrushFonts, kBrushFontCount);
        m_uiFont = i18n::UiFontName();
        m_dust.clear();
        const int n = static_cast<int>(width * height / kDustDensity);
        for (int i = 0; i < n; ++i)
        {
            Dust d;
            d.m_x = RandRangeF(0.0f, width);
            d.m_y = RandRangeF(0.0f, height);
            d.m_r = RandRangeF(kDustMinR, kDustMaxR) * m_scale;
            d.m_v = RandRangeF(kDustMinV, kDustMaxV) * m_scale;
            d.m_sway = RandRangeF(0.0f, kSceneTwoPi);
            d.m_tw = RandRangeF(0.0f, kSceneTwoPi);
            m_dust.push_back(d);
        }
        //各处字号
        float lineSize = width * kLineWidthRatio;
        if (height * kLineHeightRatio < lineSize)
        {
            lineSize = height * kLineHeightRatio;
        }
        if (kLineMax * m_scale < lineSize)
        {
            lineSize = kLineMax * m_scale;
        }
        m_lineSize = lineSize;
        float title = height * kTitleRatio;
        title = (title < kTitleMin * m_scale) ? kTitleMin * m_scale : ((title > kTitleMax * m_scale) ? kTitleMax * m_scale : title);
        m_titleSize = title;
        float summary = height * kSummaryHeightRatio;
        if (width * kSummaryWidthRatio < summary)
        {
            summary = width * kSummaryWidthRatio;
        }
        m_summarySize = summary;
        m_honorFormat = MakeCenterFormat(m_brushFont, m_lineSize);
        m_shameFormat = MakeCenterFormat(m_brushFont, m_lineSize * kShameScale);
        m_badgeFormat = MakeCenterFormat(m_brushFont, m_lineSize * kBadgeScale);
        m_titleFormat = MakeCenterFormat(m_uiFont, m_titleSize);
        m_subtitleFormat = MakeCenterFormat(m_uiFont, m_titleSize * kSubtitleScale);
        m_summaryFormat = MakeCenterFormat(m_brushFont, m_summarySize);
        m_sealFormat = MakeCenterFormat(m_brushFont, m_summarySize * kSealSize * kSealCharSize);
        DiscardDeviceResources();
    }

    void DiscardDeviceResources() override
    {
        m_solid.Release();
        m_background.Release();
        m_glow.Release();
        m_gold.Release();
        m_charGlow.Release();
    }

    void Update(float dt, uint64_t /*wallMs*/) override
    {
        m_clock += dt;
        for (size_t i = 0; i < m_dust.size(); ++i)
        {
            Dust& d = m_dust[i];
            d.m_y -= d.m_v * dt;
            d.m_x += sinf(m_clock * kDustSwayFreq + d.m_sway) * kDustSway * m_scale * dt;
            if (d.m_y < -d.m_r * 2.0f)
            {
                d.m_y = m_h + d.m_r * 2.0f;
                d.m_x = RandRangeF(0.0f, m_w);
            }
        }
    }

    void Draw(ID2D1RenderTarget* rt) override
    {
        if (!EnsureResources(rt))
        {
            return;
        }
        rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, m_w, m_h), m_background);
        DrawFrame(rt);
        DrawDust(rt);
        DrawTitle(rt);
        const HonorPosition pos = HonorPositionAt(m_clock);
        if (pos.m_phase == kHonorLine)
        {
            DrawLine(rt, pos);
        }
        else
        {
            DrawSummary(rt, pos.m_phaseT);
        }
    }

    bool ShowsCornerClock() const override
    {
        return true;
    }

private:
    CComPtr<IDWriteTextFormat> MakeCenterFormat(const std::wstring& family, float size)
    {
        return MakeTextFormat(m_dwrite, family, size, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                              DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 创建与渲染目标绑定的画刷
    bool EnsureResources(ID2D1RenderTarget* rt)
    {
        if (m_solid != NULL)
        {
            return true;
        }
        if (FAILED(rt->CreateSolidColorBrush(ColorRgb(kGold), &m_solid)))
        {
            return false;
        }
        D2D1_GRADIENT_STOP bg[3] =
        {
            { 0.0f, ColorRgb(kBgCenter) }, { kBgMiddleStop, ColorRgb(kBgMiddle) }, { 1.0f, ColorRgb(kBgEdge) },
        };
        CComPtr<ID2D1GradientStopCollection> bgStops;
        rt->CreateGradientStopCollection(bg, 3, &bgStops);
        const float radius = ((m_w > m_h) ? m_w : m_h) * kBgRadiusRatio;
        if (bgStops != NULL)
        {
            rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
                                              D2D1::Point2F(m_w / 2.0f, m_h * kBgCenterY), D2D1::Point2F(0.0f, 0.0f),
                                              radius, radius),
                                          bgStops, &m_background);
        }
        D2D1_GRADIENT_STOP glow[2] = { { 0.0f, ColorRgb(kGlowColor, 1.0f) }, { 1.0f, ColorRgb(kGlowColor, 0.0f) } };
        CComPtr<ID2D1GradientStopCollection> glowStops;
        rt->CreateGradientStopCollection(glow, 2, &glowStops);
        if (glowStops != NULL)
        {
            rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                                                              D2D1::Point2F(0.0f, 0.0f), 1.0f, 1.0f),
                                          glowStops, &m_glow);
            rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                                                              D2D1::Point2F(0.0f, 0.0f), 1.0f, 1.0f),
                                          glowStops, &m_charGlow);
        }
        D2D1_GRADIENT_STOP gold[3] =
        {
            { 0.0f, ColorRgb(kGoldLight) }, { kGoldMidStop, ColorRgb(kGoldMid) }, { 1.0f, ColorRgb(kGoldDark) },
        };
        CComPtr<ID2D1GradientStopCollection> goldStops;
        rt->CreateGradientStopCollection(gold, 3, &goldStops);
        if (goldStops != NULL)
        {
            rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                                                              D2D1::Point2F(0.0f, 1.0f)),
                                          goldStops, &m_gold);
        }
        return m_background != NULL && m_glow != NULL && m_gold != NULL && m_charGlow != NULL;
    }

    ID2D1SolidColorBrush* Solid(uint32_t rgb, float alpha)
    {
        m_solid->SetColor(ColorRgb(rgb, alpha));
        return m_solid;
    }

    // 金色双线边框与四角回纹
    void DrawFrame(ID2D1RenderTarget* rt)
    {
        const float s = m_scale;
        const float shortSide = (m_w < m_h) ? m_w : m_h;
        const float m = shortSide * kMarginRatio;
        rt->DrawRectangle(D2D1::RectF(m, m, m_w - m, m_h - m), Solid(kGold, kFrameAlpha), kFrameOuterWidth * s);
        const float inner = kFrameInner * s;
        rt->DrawRectangle(D2D1::RectF(m + inner, m + inner, m_w - m - inner, m_h - m - inner),
                          Solid(kGold, kFrameAlpha), kFrameInnerWidth * s);
        const float k = shortSide * kCornerRatio;
        const float nearD = kCornerNear * s;
        const float farD = kCornerFar * s;
        const float corners[4][4] =
        {
            { m, m, 1.0f, 1.0f }, { m_w - m, m, -1.0f, 1.0f }, { m, m_h - m, 1.0f, -1.0f }, { m_w - m, m_h - m, -1.0f, -1.0f },
        };
        ID2D1Brush* b = Solid(kGold, kCornerAlpha);
        const float lw = kCornerWidth * s;
        for (int i = 0; i < 4; ++i)
        {
            const float x = corners[i][0];
            const float y = corners[i][1];
            const float sx = corners[i][2];
            const float sy = corners[i][3];
            rt->DrawLine(D2D1::Point2F(x + sx * k * kCornerReach, y + sy * nearD), D2D1::Point2F(x + sx * nearD, y + sy * nearD), b, lw);
            rt->DrawLine(D2D1::Point2F(x + sx * nearD, y + sy * nearD), D2D1::Point2F(x + sx * nearD, y + sy * k * kCornerReach), b, lw);
            rt->DrawLine(D2D1::Point2F(x + sx * farD, y + sy * k), D2D1::Point2F(x + sx * farD, y + sy * farD), b, lw);
            rt->DrawLine(D2D1::Point2F(x + sx * farD, y + sy * farD), D2D1::Point2F(x + sx * k, y + sy * farD), b, lw);
        }
    }

    void DrawDust(ID2D1RenderTarget* rt)
    {
        for (size_t i = 0; i < m_dust.size(); ++i)
        {
            const Dust& d = m_dust[i];
            const float a = kDustMinAlpha + kDustAlphaRange * fabsf(sinf(d.m_tw + m_clock * kDustTwinkleFreq));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(d.m_x, d.m_y), d.m_r, d.m_r), Solid(kDustColor, a));
        }
    }

    // 逐字画一行等距的字（标题用），以 (cx, cy) 为中心
    void DrawSpaced(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt, const wchar_t* text, float cx, float cy,
                    float size, float spacing, ID2D1Brush* brush)
    {
        const std::wstring s(text);
        const float step = size + spacing;
        float x = cx - step * (static_cast<float>(s.size()) - 1.0f) / 2.0f;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const std::wstring ch(1, s[i]);
            DrawTextIn(rt, fmt, ch, D2D1::RectF(x - size, cy - size, x + size, cy + size), brush);
            x += step;
        }
    }

    void DrawTitle(ID2D1RenderTarget* rt)
    {
        const float shortSide = (m_w < m_h) ? m_w : m_h;
        const float m = shortSide * kMarginRatio;
        DrawSpaced(rt, m_titleFormat, kTitle, m_w / 2.0f, m + m_titleSize * kTitleY, m_titleSize,
                   m_titleSize * kTitleSpacing, Solid(kTitleGold, kDotAlpha));
        const float subSize = m_titleSize * kSubtitleScale;
        DrawTextIn(rt, m_subtitleFormat, kSubtitle,
                   D2D1::RectF(0.0f, m + m_titleSize * kSubtitleY - subSize, m_w, m + m_titleSize * kSubtitleY + subSize),
                   Solid(kTitleGold, kSubtitleAlpha));
    }

    // 以 (cx, cy) 为中心、按半径 r 画一团径向渐变的光晕
    void DrawGlow(ID2D1RenderTarget* rt, ID2D1RadialGradientBrush* brush, float cx, float cy, float r, float alpha)
    {
        brush->SetCenter(D2D1::Point2F(cx, cy));
        brush->SetRadiusX(r);
        brush->SetRadiusY(r);
        brush->SetOpacity(alpha);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush);
    }

    // 逐字浮现地画一行。t0：这一行第一个字开始浮现的时刻；honor：「荣」半句（金字）还是「耻」半句
    void DrawRevealLine(ID2D1RenderTarget* rt, const wchar_t* text, float cx, float cy, float size, float phaseT,
                        float t0, float alpha, bool honor)
    {
        IDWriteTextFormat* fmt = honor ? m_honorFormat.p : m_shameFormat.p;
        const std::wstring s(text);
        const float gap = size * kCharGap;
        const float total = static_cast<float>(s.size()) * size + (static_cast<float>(s.size()) - 1.0f) * gap;
        float x = cx - total / 2.0f + size / 2.0f;
        D2D1_MATRIX_3X2_F old;
        rt->GetTransform(&old);
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float p = Clamp01F((phaseT - t0 - static_cast<float>(i) * kHonorCharStep) / kHonorCharDur);
            if (p > 0.0f)
            {
                const float e = EaseOutCubicF(p);
                const float a = e * alpha;
                const float sc = 1.0f + kRevealScale * (1.0f - e);
                const std::wstring ch(1, s[i]);
                if (honor)
                {
                    float glowA = kCharGlowAlpha;
                    float glowR = size * kCharGlowRatio;
                    if (s[i] == kRongChar)
                    {
                        glowA = kRongGlowAlpha + kRongGlowPulse * sinf(m_clock * kRongGlowFreq);
                        glowR = size;
                    }
                    DrawGlow(rt, m_charGlow, x, cy, glowR, glowA * a);
                }
                rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(x, cy)) * old);
                const D2D1_RECT_F box = D2D1::RectF(x - size, cy - size, x + size, cy + size);
                const float off = size * kShadowOffset;
                DrawTextIn(rt, fmt, ch, D2D1::RectF(box.left + off, box.top + off, box.right + off, box.bottom + off),
                           Solid(kShadow, kShadowAlpha * a));
                if (honor)
                {
                    m_gold->SetStartPoint(D2D1::Point2F(x, cy - size * kGoldTop));
                    m_gold->SetEndPoint(D2D1::Point2F(x, cy + size * kGoldBottom));
                    m_gold->SetOpacity(a);
                    DrawTextIn(rt, fmt, ch, box, m_gold);
                }
                else
                {
                    DrawTextIn(rt, fmt, ch, box, Solid(s[i] == kChiChar ? kShameRed : kIvory, a));
                }
                rt->SetTransform(old);
            }
            x += size + gap;
        }
    }

    void DrawLine(ID2D1RenderTarget* rt, const HonorPosition& pos)
    {
        const float size = m_lineSize;
        const float lineDur = HonorLineDuration();
        const float alpha = 1.0f - Clamp01F((pos.m_phaseT - (lineDur - kHonorFadeOut)) / kHonorFadeOut);
        const float cx = m_w / 2.0f;
        const float cy = m_h / 2.0f;
        DrawGlow(rt, m_glow, cx, cy, size * kGlowRadius, kGlowAlpha);

        //「其一」签
        const float badgeA = alpha * Clamp01F(pos.m_phaseT / kHonorCharDur);
        if (badgeA > 0.0f)
        {
            const float bs = size * kBadgeScale;
            const float by = cy + size * kBadgeY;
            const D2D1_RECT_F br = D2D1::RectF(cx - bs * kBadgeW / 2.0f, by - bs * kBadgeH / 2.0f,
                                              cx + bs * kBadgeW / 2.0f, by + bs * kBadgeH / 2.0f);
            const float radius = kBadgeRadius * m_scale;
            rt->FillRoundedRectangle(D2D1::RoundedRect(br, radius, radius), Solid(kBadgeRed, badgeA));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(br, radius, radius), Solid(kTitleGold, kBadgeBorderAlpha * badgeA),
                                     kBadgeBorder * m_scale);
            std::wstring badge(kBadgePrefix);
            badge += kCnNumbers[pos.m_line];
            DrawTextIn(rt, m_badgeFormat, badge, br, Solid(kBadgeText, badgeA));
        }
        DrawRevealLine(rt, HonorLineText(pos.m_line, true), cx, cy + size * kHonorRowY, size, pos.m_phaseT, 0.0f,
                       alpha, true);
        const float t1 = kHonorCharStep * kHonorHalfChars + kHonorHalfGap;
        DrawRevealLine(rt, HonorLineText(pos.m_line, false), cx, cy + size * kShameRowY, size * kShameScale,
                       pos.m_phaseT, t1, alpha, false);

        //进度点
        const float dotsY = cy + size * kDotsY;
        const float step = kDotStep * m_scale;
        for (int i = 0; i < kHonorLineCount; ++i)
        {
            const float x = cx + (static_cast<float>(i) - (kHonorLineCount - 1) / 2.0f) * step;
            if (i <= pos.m_line)
            {
                const float r = ((i == pos.m_line) ? kDotCurrentR : kDotR) * m_scale;
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, dotsY), r, r),
                                Solid(kDustColor, (i == pos.m_line ? 1.0f : kDotPastAlpha) * alpha * kDotAlpha));
            }
            else
            {
                const float r = kDotR * m_scale;
                rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, dotsY), r, r),
                                Solid(kDustColor, kDotFutureAlpha * alpha * kDotAlpha), kDotFutureWidth * m_scale);
            }
        }
    }

    // 「知荣明耻」印章：2×2 的白字红章，按传统从右往左读（右列「知荣」、左列「明耻」）
    void DrawSeal(ID2D1RenderTarget* rt, float x, float y, float size, float t, float alpha)
    {
        if (t <= 0.0f)
        {
            return;
        }
        const float p = Clamp01F(t / kSealDur);
        const float sc = kSealStartScale + (1.0f - kSealStartScale) * EaseOutBackF(p);
        const float a = Clamp01F(p * kSealFadeIn) * kSealAlpha * alpha;
        D2D1_MATRIX_3X2_F old;
        rt->GetTransform(&old);
        rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) * D2D1::Matrix3x2F::Rotation(kSealRotation * kSceneRadToDeg) *
                         D2D1::Matrix3x2F::Translation(x, y) * old);
        const float half = size / 2.0f;
        const float radius = size * kSealRadius;
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(-half, -half, half, half), radius, radius),
                                 Solid(kSealRed, a));
        const float inner = size * kSealInner;
        rt->DrawRectangle(D2D1::RectF(-inner, -inner, inner, inner), Solid(kSealInk, kSealInkAlpha * a), size * kSealBorder);
        const float off = size * kSealCharOffset;
        const float cs = size * kSealCharSize;
        const float pos[4][2] = { { off, -off }, { off, off }, { -off, -off }, { -off, off } };
        for (int i = 0; i < 4; ++i)
        {
            DrawTextIn(rt, m_sealFormat, kSealChars[i],
                       D2D1::RectF(pos[i][0] - cs, pos[i][1] - cs, pos[i][0] + cs, pos[i][1] + cs), Solid(kSealInk, a));
        }
        rt->SetTransform(old);
    }

    void DrawSummary(ID2D1RenderTarget* rt, float phaseT)
    {
        const float fs = m_summarySize;
        const float rowH = fs * kSummaryRowH;
        const float top = m_h * kSummaryCenterY - rowH * kHonorLineCount / 2.0f + rowH * kSummaryHalfRow;
        const float fadeAll = 1.0f - Clamp01F((phaseT - (HonorSummaryDuration() - kHonorSummaryFade)) / kHonorSummaryFade);
        const float colGap = fs * kSummaryColGap;
        for (int i = 0; i < kHonorLineCount; ++i)
        {
            const float p = Clamp01F((phaseT - i * kHonorRowStep) / kSummaryRowFade);
            if (p <= 0.0f)
            {
                continue;
            }
            const float e = EaseOutCubicF(p);
            const float y = top + i * rowH + (1.0f - e) * kSummaryRowRise * m_scale;
            const float a = e * fadeAll;
            //左列「荣」半句右对齐到中线左侧，右列「耻」半句左对齐到中线右侧：逐字排，字距与字号相同
            const std::wstring honor(HonorLineText(i, true));
            const std::wstring shame(HonorLineText(i, false));
            for (size_t c = 0; c < honor.size(); ++c)
            {
                const float x = m_w / 2.0f - colGap / 2.0f - fs * (honor.size() - c) + fs / 2.0f;
                m_gold->SetStartPoint(D2D1::Point2F(x, y - fs * kGoldTop));
                m_gold->SetEndPoint(D2D1::Point2F(x, y + fs * kGoldBottom));
                m_gold->SetOpacity(a);
                DrawTextIn(rt, m_summaryFormat, std::wstring(1, honor[c]), D2D1::RectF(x - fs, y - fs, x + fs, y + fs), m_gold);
            }
            for (size_t c = 0; c < shame.size(); ++c)
            {
                const float x = m_w / 2.0f + colGap / 2.0f + fs * c + fs / 2.0f;
                DrawTextIn(rt, m_summaryFormat, std::wstring(1, shame[c]), D2D1::RectF(x - fs, y - fs, x + fs, y + fs),
                           Solid(kIvory, a));
            }
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(m_w / 2.0f, y), fs * kSummaryDotR, fs * kSummaryDotR),
                            Solid(kTitleGold, kSummaryDotAlpha * a));
        }
        const float sealSize = fs * kSealSize;
        DrawSeal(rt, m_w / 2.0f + colGap / 2.0f + fs * kHonorHalfChars + sealSize * kSealRight, top + rowH * kSealRowY,
                 sealSize, phaseT - (kHonorRowStep * kHonorLineCount + kHonorSealDelay), fadeAll);
    }

private:
    float                               m_w;              // 画面宽
    float                               m_h;              // 画面高
    float                               m_scale;          // 缩放比例
    float                               m_clock;          // 场景时钟（秒）
    float                               m_lineSize;       // 逐句阶段「荣」半句的字号
    float                               m_titleSize;      // 标题字号
    float                               m_summarySize;    // 总览字号
    IDWriteFactory*                     m_dwrite;         // DirectWrite 工厂（借用）
    std::wstring                        m_brushFont;      // 书法字体
    std::wstring                        m_uiFont;         // 界面字体（标题用）
    std::vector<Dust>                   m_dust;           // 金粉
    CComPtr<IDWriteTextFormat>          m_honorFormat;    // 「荣」半句
    CComPtr<IDWriteTextFormat>          m_shameFormat;    // 「耻」半句
    CComPtr<IDWriteTextFormat>          m_badgeFormat;    // 「其一」签
    CComPtr<IDWriteTextFormat>          m_titleFormat;    // 标题
    CComPtr<IDWriteTextFormat>          m_subtitleFormat; // 副标题
    CComPtr<IDWriteTextFormat>          m_summaryFormat;  // 总览
    CComPtr<IDWriteTextFormat>          m_sealFormat;     // 印章字
    CComPtr<ID2D1SolidColorBrush>       m_solid;          // 复用的纯色画刷
    CComPtr<ID2D1RadialGradientBrush>   m_background;     // 深红径向渐变底
    CComPtr<ID2D1RadialGradientBrush>   m_glow;           // 句子背后的暖光
    CComPtr<ID2D1RadialGradientBrush>   m_charGlow;       // 每个金字背后的光晕
    CComPtr<ID2D1LinearGradientBrush>   m_gold;           // 金字的竖直渐变
};

} // namespace

ILockScene* CreateHonorScene()
{
    return new CHonorScene();
}

} // namespace flamingolock
