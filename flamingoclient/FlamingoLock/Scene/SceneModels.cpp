/**
 *  SceneModels.cpp —— 五种锁屏动画里可单测的纯逻辑，声明与说明见 Scenes.h。
 *  不依赖 Direct2D 与窗口，同时编进单元测试工程。
 *  balloonwj@qq.com   2026-10-06
 */
#include "Scenes.h"

#include <math.h>
#include <set>

namespace flamingolock {

namespace {

// i18n-ignore-begin: 八荣八耻原文是宣传文案本身，任何界面语言下都显示中文（docs/锁屏方案.md 第 4.11 节）
//八荣八耻原文：每句分「荣」「耻」两半
const wchar_t* const kHonorTexts[kHonorLineCount][2] =
{
    { L"以热爱祖国为荣", L"以危害祖国为耻" },
    { L"以服务人民为荣", L"以背离人民为耻" },
    { L"以崇尚科学为荣", L"以愚昧无知为耻" },
    { L"以辛勤劳动为荣", L"以好逸恶劳为耻" },
    { L"以团结互助为荣", L"以损人利己为耻" },
    { L"以诚实守信为荣", L"以见利忘义为耻" },
    { L"以遵纪守法为荣", L"以违法乱纪为耻" },
    { L"以艰苦奋斗为荣", L"以骄奢淫逸为耻" },
};
// i18n-ignore-end

const float kBounceNever = -1000.0f;   // 「从未碰过边 / 撞过角」的时刻，远早于任何真实时刻
const int kRainCharsPerPhrase = 14;    // 一句定格的字数（荣、耻两个半句）

} // namespace

// ---------------------------------------------------------------------------
// 八荣八耻
// ---------------------------------------------------------------------------

const wchar_t* HonorLineText(int index, bool honor)
{
    const int i = (index >= 0 && index < kHonorLineCount) ? index : 0;
    return kHonorTexts[i][honor ? 0 : 1];
}

float HonorLineDuration()
{
    return kHonorCharStep * kHonorHalfChars + kHonorHalfGap + kHonorCharStep * kHonorHalfChars + kHonorCharDur +
           kHonorHold + kHonorFadeOut;
}

float HonorSummaryDuration()
{
    return kHonorRowStep * kHonorLineCount + kHonorSealDelay + kHonorSummaryHold + kHonorSummaryFade;
}

float HonorCycleDuration()
{
    return HonorLineDuration() * kHonorLineCount + HonorSummaryDuration();
}

HonorPosition HonorPositionAt(float t)
{
    HonorPosition pos;
    const float cycle = HonorCycleDuration();
    float local = (t > 0.0f) ? fmodf(t, cycle) : 0.0f;
    const float linesTotal = HonorLineDuration() * kHonorLineCount;
    if (local < linesTotal)
    {
        int line = static_cast<int>(local / HonorLineDuration());
        if (line >= kHonorLineCount)
        {
            line = kHonorLineCount - 1;
        }
        pos.m_phase = kHonorLine;
        pos.m_line = line;
        pos.m_phaseT = local - HonorLineDuration() * line;
        return pos;
    }
    pos.m_phase = kHonorSummary;
    pos.m_line = -1;
    pos.m_phaseT = local - linesTotal;
    return pos;
}

// ---------------------------------------------------------------------------
// 弹跳标志
// ---------------------------------------------------------------------------

void ResetBounceModel(BounceModel& m)
{
    m.m_colorIndex = 0;
    m.m_clock = 0.0f;
    m.m_lastHitX = kBounceNever;
    m.m_lastHitY = kBounceNever;
    m.m_lastCorner = kBounceNever;
    m.m_corners = 0;
}

BounceStep StepBounce(BounceModel& m, float dt, float width, float height, int colorCount)
{
    BounceStep step;
    step.m_hitX = false;
    step.m_hitY = false;
    step.m_corner = false;
    step.m_cornerX = 0.0f;
    step.m_cornerY = 0.0f;

    m.m_clock += dt;
    m.m_x += m.m_vx * dt;
    m.m_y += m.m_vy * dt;
    if (m.m_x <= 0.0f)
    {
        m.m_x = 0.0f;
        m.m_vx = fabsf(m.m_vx);
        step.m_hitX = true;
    }
    else if (m.m_x + m.m_w >= width)
    {
        m.m_x = width - m.m_w;
        m.m_vx = -fabsf(m.m_vx);
        step.m_hitX = true;
    }
    if (m.m_y <= 0.0f)
    {
        m.m_y = 0.0f;
        m.m_vy = fabsf(m.m_vy);
        step.m_hitY = true;
    }
    else if (m.m_y + m.m_h >= height)
    {
        m.m_y = height - m.m_h;
        m.m_vy = -fabsf(m.m_vy);
        step.m_hitY = true;
    }
    if (!step.m_hitX && !step.m_hitY)
    {
        return step;
    }
    if (colorCount > 0)
    {
        m.m_colorIndex = (m.m_colorIndex + 1) % colorCount;
    }
    if (step.m_hitX)
    {
        m.m_lastHitX = m.m_clock;
    }
    if (step.m_hitY)
    {
        m.m_lastHitY = m.m_clock;
    }
    if (fabsf(m.m_lastHitX - m.m_lastHitY) <= kBounceCornerWindow &&
        m.m_clock - m.m_lastCorner > kBounceCornerCooldown)
    {
        m.m_lastCorner = m.m_clock;
        ++m.m_corners;
        step.m_corner = true;
        //碰边后速度已反向：向右走说明刚碰了左边，向下走说明刚碰了上边
        step.m_cornerX = (m.m_vx > 0.0f) ? 0.0f : width;
        step.m_cornerY = (m.m_vy > 0.0f) ? 0.0f : height;
    }
    return step;
}

void AimBounceAtCorner(BounceModel& m, float width, float height, float t)
{
    const float ax = fabsf(m.m_vx);
    const float ay = fabsf(m.m_vy);
    if (ax <= 0.0f || ay <= 0.0f)
    {
        return;
    }
    float tt = t;
    const float limitX = (width - m.m_w) / ax;
    const float limitY = (height - m.m_h) / ay;
    if (limitX < tt)
    {
        tt = limitX;
    }
    if (limitY < tt)
    {
        tt = limitY;
    }
    if (tt < 0.0f)
    {
        tt = 0.0f;
    }
    m.m_x = (m.m_vx < 0.0f) ? ax * tt : width - m.m_w - ax * tt;
    m.m_y = (m.m_vy < 0.0f) ? ay * tt : height - m.m_h - ay * tt;
}

float RocketLaunchVelocity(float y0, float targetY, float fuse, float accel)
{
    if (fuse <= 0.0f)
    {
        return 0.0f;
    }
    return (targetY - y0 - 0.5f * accel * fuse * fuse) / fuse;
}

// ---------------------------------------------------------------------------
// 粒子时钟
// ---------------------------------------------------------------------------

void SampleMaskPoints(const std::vector<uint8_t>& mask, int width, int height, int maxPoints, int fineStep,
                      std::vector<SamplePoint>& points, int& stepOut)
{
    points.clear();
    stepOut = 1;
    if (width <= 0 || height <= 0 || maxPoints <= 0 || mask.size() < static_cast<size_t>(width) * height)
    {
        return;
    }
    const int fine = (fineStep < 1) ? 1 : fineStep;
    long fineCount = 0;
    for (int y = 0; y < height; y += fine)
    {
        for (int x = 0; x < width; x += fine)
        {
            if (mask[static_cast<size_t>(y) * width + x] != 0)
            {
                ++fineCount;
            }
        }
    }
    //每个细取样点代表 fine×fine 的面积；面积除以点数上限再开方，就是让点数接近上限的取样间隔
    int step = static_cast<int>(sqrtf(static_cast<float>(fineCount) * fine * fine / maxPoints));
    if (step < fine)
    {
        step = fine;
    }
    for (;;)
    {
        points.clear();
        for (int y = 0; y < height; y += step)
        {
            for (int x = 0; x < width; x += step)
            {
                if (mask[static_cast<size_t>(y) * width + x] != 0)
                {
                    SamplePoint p;
                    p.m_x = static_cast<float>(x);
                    p.m_y = static_cast<float>(y);
                    points.push_back(p);
                }
            }
        }
        if (static_cast<int>(points.size()) <= maxPoints)
        {
            break;
        }
        ++step;
    }
    stepOut = step;
}

// ---------------------------------------------------------------------------
// 汉字数字雨
// ---------------------------------------------------------------------------

float RainCharLandAt(int i)
{
    return kRainStreakSec + kRainCharStep * i + ((i >= kHonorHalfChars) ? kRainHalfGap : 0.0f);
}

float RainPhraseDuration()
{
    return kRainStreakSec + kRainCharStep * kRainCharsPerPhrase + kRainHalfGap + kRainHold + kRainFade;
}

float RainPhraseStartAt(int k)
{
    return kRainFirstPhrase + kRainPhraseEvery * k;
}

int RainPhraseLine(int k)
{
    return (k >= 0) ? (k % kHonorLineCount) : 0;
}

const std::wstring& RainGlyphSet()
{
    static std::wstring s_set;
    if (s_set.empty())
    {
        std::set<wchar_t> seen;
        for (int i = 0; i < kHonorLineCount; ++i)
        {
            for (int half = 0; half < 2; ++half)
            {
                for (const wchar_t* p = kHonorTexts[i][half]; *p != L'\0'; ++p)
                {
                    if (seen.insert(*p).second)
                    {
                        s_set.push_back(*p);
                    }
                }
            }
        }
    }
    return s_set;
}

// ---------------------------------------------------------------------------
// 火烈鸟
// ---------------------------------------------------------------------------

void SolveTwoBoneKnee(float hipX, float hipY, float footX, float footY, float len1, float len2,
                      float& kneeX, float& kneeY)
{
    const float kMinReach = 0.01f;   // 距离与可达边界留的余量，避免开方出现负数
    const float dx = footX - hipX;
    const float dy = footY - hipY;
    float d = sqrtf(dx * dx + dy * dy);
    const float lo = fabsf(len1 - len2) + kMinReach;
    const float hi = len1 + len2 - kMinReach;
    const float dir = (d > 0.0f) ? d : 1.0f;   // 方向向量的长度；髋脚重合时随便取一个方向
    if (d < lo)
    {
        d = lo;
    }
    if (d > hi)
    {
        d = hi;
    }
    const float a = (len1 * len1 - len2 * len2 + d * d) / (2.0f * d);
    const float h2 = len1 * len1 - a * a;
    const float h = (h2 > 0.0f) ? sqrtf(h2) : 0.0f;
    const float ux = (d > 0.0f && dir > 0.0f) ? dx / dir : 0.0f;
    const float uy = (d > 0.0f && dir > 0.0f) ? dy / dir : 1.0f;
    const float px = hipX + a * ux;
    const float py = hipY + a * uy;
    //垂直方向取朝身后（x 为负）的一侧
    float nx = -uy;
    float ny = ux;
    if (nx > 0.0f)
    {
        nx = -nx;
        ny = -ny;
    }
    kneeX = px + h * nx;
    kneeY = py + h * ny;
}

} // namespace flamingolock
