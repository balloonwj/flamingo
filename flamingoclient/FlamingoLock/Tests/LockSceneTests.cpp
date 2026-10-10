/**
 *  锁屏动画场景的测试（标签 [lockdll]），覆盖 docs/锁屏方案.md 第 7 节（2026-10-06 按用户要求自行确定）：
 *    - AH1~AH3 八荣八耻时间线（HonorPositionAt）
 *    - AB1~AB4 弹跳标志：碰边反向换色、正中角落的判定、重新初始化后不漏判、烟花在画面内炸开
 *    - AP1~AP2 粒子时钟的蒙版取样（SampleMaskPoints）
 *    - AR1~AR2 汉字数字雨的定格时间安排
 *    - AF1~AF2 火烈鸟两段腿骨的关节求解（SolveTwoBoneKnee）
 *    - AS1     离屏渲染冒烟：五种动画各在 Direct2D 的 WIC 位图上连续画 3 秒，不报错且画面不是单色。
 *              设了环境变量 FLAMINGO_LOCK_SHOT_DIR 时，把每种动画最后一帧存成 PNG 供人工查看。
 *    - AS2     设置窗口「预览」的退出提示（2026-10-07）：叠加层只在画面上方居中的胶囊里改变画面，胶囊之外不变；
 *              设了 FLAMINGO_LOCK_SHOT_DIR 时存一张「粒子时钟 + 提示」的 lock_preview_hint.png。
 *    - AS3     汉字数字雨的大红配色（2026-10-07，第 12 节）：连续画 3 秒后，整幅画面的平均红色分量大于绿、蓝
 *              分量的 2 倍，防止配色被无意中改回黑底绿字。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <atlbase.h>
#include <wincodec.h>
#include <math.h>
#include <cstdlib>   //std::abs
#include <set>
#include <string>
#include <vector>

#include "../Scene/Scenes.h"
#include "../Render/LockScene.h"
#include "../Render/LockOverlayRenderer.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace flamingolock;

namespace {

const float kEps = 0.001f;              //浮点比较容差
const float kStepSec = 1.0f / 60.0f;    //模拟时每一步的秒数
const int kShotWidth = 960;             //离屏渲染的画面尺寸
const int kShotHeight = 540;
const float kShotSeconds = 3.0f;        //离屏渲染模拟的秒数
const float kShotFps = 30.0f;           //离屏渲染的帧率
const float kMsPerSecond = 1000.0f;     //秒换算为毫秒
const uint64_t kMsPerMinute = 60000;    //一分钟的毫秒数
const uint64_t kStartSecondMs = 10000;  //模拟从一分钟的第 10 秒开始
const wchar_t* const kShotDirEnv = L"FLAMINGO_LOCK_SHOT_DIR";   //设了它就把每种动画的画面存成 PNG

//画面尺寸（物理像素）
const float kScreenW = 1920.0f;
const float kScreenH = 1080.0f;

//距离
float Dist(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx;
    const float dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

//摆一个标志：位置、速度、尺寸
BounceModel MakeBounce(float x, float y, float vx, float vy)
{
    BounceModel m;
    ResetBounceModel(m);
    m.m_x = x;
    m.m_y = y;
    m.m_vx = vx;
    m.m_vy = vy;
    m.m_w = 200.0f;
    m.m_h = 60.0f;
    return m;
}

//按 dt 一步步推进，直到正中角落或超时；返回是否撞到角落
bool RunUntilCorner(BounceModel& m, float seconds, BounceStep* last)
{
    const int steps = static_cast<int>(seconds / kStepSec);
    for (int i = 0; i < steps; ++i)
    {
        const BounceStep s = StepBounce(m, kStepSec, kScreenW, kScreenH, 7);
        if (s.m_corner)
        {
            if (last != NULL)
            {
                *last = s;
            }
            return true;
        }
    }
    return false;
}

} // namespace

// =====================================================================
// AH —— 八荣八耻时间线
// =====================================================================

TEST_CASE("AH1 逐句阶段各时刻对应的句序", "[lockdll]")
{
    const float line = HonorLineDuration();
    REQUIRE(line > 0.0f);
    HonorPosition p = HonorPositionAt(0.0f);
    CHECK(p.m_phase == kHonorLine);
    CHECK(p.m_line == 0);
    for (int i = 0; i < kHonorLineCount; ++i)
    {
        p = HonorPositionAt(line * i + line / 2.0f);
        CHECK(p.m_phase == kHonorLine);
        CHECK(p.m_line == i);
        CHECK(fabsf(p.m_phaseT - line / 2.0f) < kEps);
    }
    //八句的原文各不相同，「荣」半句以「为荣」结尾、「耻」半句以「为耻」结尾
    std::set<std::wstring> seen;
    for (int i = 0; i < kHonorLineCount; ++i)
    {
        const std::wstring honor(HonorLineText(i, true));
        const std::wstring shame(HonorLineText(i, false));
        CHECK(honor.size() == static_cast<size_t>(kHonorHalfChars));
        CHECK(shame.size() == static_cast<size_t>(kHonorHalfChars));
        CHECK(honor.substr(honor.size() - 2) == L"为荣");   //i18n-ignore: 测试数据
        CHECK(shame.substr(shame.size() - 2) == L"为耻");   //i18n-ignore: 测试数据
        seen.insert(honor);
    }
    CHECK(seen.size() == static_cast<size_t>(kHonorLineCount));
}

TEST_CASE("AH2 八句之后进入总览", "[lockdll]")
{
    const float linesTotal = HonorLineDuration() * kHonorLineCount;
    const HonorPosition p = HonorPositionAt(linesTotal + 1.0f);
    CHECK(p.m_phase == kHonorSummary);
    CHECK(p.m_line == -1);
    CHECK(fabsf(p.m_phaseT - 1.0f) < kEps);
    CHECK(HonorCycleDuration() > linesTotal + HonorSummaryDuration() - kEps);
}

TEST_CASE("AH3 总览结束后从第一句循环", "[lockdll]")
{
    const float cycle = HonorCycleDuration();
    const HonorPosition p = HonorPositionAt(cycle + 0.5f);
    CHECK(p.m_phase == kHonorLine);
    CHECK(p.m_line == 0);
    CHECK(fabsf(p.m_phaseT - 0.5f) < 0.01f);
    //负数按 0 处理
    CHECK(HonorPositionAt(-3.0f).m_line == 0);
}

// =====================================================================
// AB —— 弹跳标志
// =====================================================================

TEST_CASE("AB1 碰边反向并换色", "[lockdll]")
{
    BounceModel m = MakeBounce(kScreenW - 200.0f - 1.0f, 500.0f, 300.0f, 100.0f);
    const BounceStep s = StepBounce(m, kStepSec, kScreenW, kScreenH, 7);
    CHECK(s.m_hitX);
    CHECK_FALSE(s.m_hitY);
    CHECK(m.m_vx < 0.0f);
    CHECK(m.m_x + m.m_w <= kScreenW + kEps);
    CHECK(m.m_colorIndex == 1);
    //没碰边时不换色
    const BounceStep s2 = StepBounce(m, kStepSec, kScreenW, kScreenH, 7);
    CHECK_FALSE(s2.m_hitX);
    CHECK(m.m_colorIndex == 1);
}

TEST_CASE("AB2 两个方向碰边相差不超过 0.12 秒算正中角落，超过不算", "[lockdll]")
{
    //瞄准右下角：0.5 秒后同时碰右边与下边
    BounceModel m = MakeBounce(500.0f, 500.0f, 400.0f, 300.0f);
    AimBounceAtCorner(m, kScreenW, kScreenH, 0.5f);
    BounceStep last;
    REQUIRE(RunUntilCorner(m, 1.0f, &last));
    CHECK(m.m_corners == 1);
    CHECK(last.m_cornerX == kScreenW);
    CHECK(last.m_cornerY == kScreenH);

    //横向比竖向早 0.3 秒碰边：不算
    BounceModel n = MakeBounce(0.0f, 0.0f, 400.0f, 300.0f);
    n.m_x = kScreenW - n.m_w - 400.0f * 0.2f;   //0.2 秒后碰右边
    n.m_y = kScreenH - n.m_h - 300.0f * 0.5f;   //0.5 秒后碰下边
    CHECK_FALSE(RunUntilCorner(n, 0.7f, NULL));
    CHECK(n.m_corners == 0);
    CHECK(kBounceCornerWindow == Catch::Approx(0.12f));
}

TEST_CASE("AB3 场景重新初始化（时钟归零）后不会漏掉撞角", "[lockdll]")
{
    BounceModel m = MakeBounce(500.0f, 500.0f, 400.0f, 300.0f);
    AimBounceAtCorner(m, kScreenW, kScreenH, 0.3f);
    REQUIRE(RunUntilCorner(m, 1.0f, NULL));
    //模拟跑了很久之后重新初始化：只复位时钟而不复位「上次撞角时刻」的话，冷却判断会把新的撞角挡掉
    m.m_clock = 1000.0f;
    m.m_lastCorner = 1000.0f;
    ResetBounceModel(m);
    CHECK(m.m_clock == 0.0f);
    CHECK(m.m_corners == 0);
    m.m_vx = 400.0f;
    m.m_vy = 300.0f;
    AimBounceAtCorner(m, kScreenW, kScreenH, 0.2f);
    CHECK(RunUntilCorner(m, 1.0f, NULL));
    CHECK(m.m_corners == 1);
}

TEST_CASE("AB4 烟花在引信燃尽时恰好升到目标高度（在画面内炸开）", "[lockdll]")
{
    const float accel = 170.0f * 1.4f;
    const float y0 = kScreenH + 10.0f;
    const float targets[3] = { kScreenH * 0.18f, kScreenH * 0.3f, kScreenH * 0.5f };
    const float fuses[2] = { 0.9f, 1.2f };
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            const float v = RocketLaunchVelocity(y0, targets[i], fuses[j], accel);
            CHECK(v < 0.0f);
            //按与场景相同的步进积分到引信燃尽
            float y = y0;
            float vy = v;
            float t = 0.0f;
            const float dt = 0.001f;
            while (t < fuses[j])
            {
                vy += accel * dt;
                y += vy * dt;
                t += dt;
            }
            INFO("target = " << targets[i] << ", fuse = " << fuses[j] << ", y = " << y);
            CHECK(fabsf(y - targets[i]) < kScreenH * 0.02f);
            CHECK(y > 0.0f);
            CHECK(y < kScreenH);
        }
    }
    CHECK(RocketLaunchVelocity(y0, 100.0f, 0.0f, accel) == 0.0f);
}

// =====================================================================
// AP —— 粒子时钟的取样
// =====================================================================

TEST_CASE("AP1 取样点数接近且不超过上限", "[lockdll]")
{
    const int w = 400;
    const int h = 200;
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    for (int y = 50; y < 150; ++y)
    {
        for (int x = 100; x < 300; ++x)
            mask[static_cast<size_t>(y) * w + x] = 1;
    }
    std::vector<SamplePoint> pts;
    int step = 0;
    const int maxPoints = 1000;
    SampleMaskPoints(mask, w, h, maxPoints, 3, pts, step);
    CHECK(static_cast<int>(pts.size()) <= maxPoints);
    CHECK(static_cast<int>(pts.size()) >= maxPoints / 2);
    CHECK(step >= 3);
    //上限很大时退回细取样间隔
    SampleMaskPoints(mask, w, h, 1000000, 3, pts, step);
    CHECK(step == 3);
    //空蒙版、尺寸不对时没有点
    std::vector<uint8_t> empty(static_cast<size_t>(w) * h, 0);
    SampleMaskPoints(empty, w, h, maxPoints, 3, pts, step);
    CHECK(pts.empty());
    SampleMaskPoints(mask, w, h + 1, maxPoints, 3, pts, step);
    CHECK(pts.empty());
}

TEST_CASE("AP2 取样点都在字形区域内", "[lockdll]")
{
    const int w = 300;
    const int h = 120;
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    //两个分开的方块，模拟「1」「1」两个笔画
    for (int y = 10; y < 110; ++y)
    {
        for (int x = 20; x < 60; ++x)
            mask[static_cast<size_t>(y) * w + x] = 1;
        for (int x = 200; x < 240; ++x)
            mask[static_cast<size_t>(y) * w + x] = 1;
    }
    std::vector<SamplePoint> pts;
    int step = 0;
    SampleMaskPoints(mask, w, h, 500, 3, pts, step);
    REQUIRE_FALSE(pts.empty());
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const int x = static_cast<int>(pts[i].m_x);
        const int y = static_cast<int>(pts[i].m_y);
        CHECK(mask[static_cast<size_t>(y) * w + x] != 0);
    }
}

// =====================================================================
// AR —— 汉字数字雨
// =====================================================================

TEST_CASE("AR1 每 8 秒定格一句，八句轮流", "[lockdll]")
{
    CHECK(RainPhraseStartAt(0) == Catch::Approx(kRainFirstPhrase));
    for (int k = 0; k < 20; ++k)
    {
        CHECK(RainPhraseStartAt(k + 1) - RainPhraseStartAt(k) == Catch::Approx(kRainPhraseEvery));
        CHECK(RainPhraseLine(k) == k % kHonorLineCount);
    }
    CHECK(RainPhraseLine(kHonorLineCount) == 0);
    //一句的全部时长短于定格间隔，同一时刻最多两句并存
    CHECK(RainPhraseDuration() < kRainPhraseEvery * 2.0f);
}

TEST_CASE("AR2 一句内各字的落定时刻递增，「耻」半句比「荣」半句晚", "[lockdll]")
{
    for (int i = 1; i < kHonorHalfChars * 2; ++i)
        CHECK(RainCharLandAt(i) > RainCharLandAt(i - 1));
    const float gapInside = RainCharLandAt(1) - RainCharLandAt(0);
    const float gapHalves = RainCharLandAt(kHonorHalfChars) - RainCharLandAt(kHonorHalfChars - 1);
    CHECK(gapHalves > gapInside + kEps);
    CHECK(RainCharLandAt(kHonorHalfChars * 2 - 1) < RainPhraseDuration());
    //取字的字表去重，且包含「荣」「耻」
    const std::wstring& set = RainGlyphSet();
    std::set<wchar_t> uniq(set.begin(), set.end());
    CHECK(uniq.size() == set.size());
    CHECK(set.find(L'荣') != std::wstring::npos);   //i18n-ignore: 测试数据
    CHECK(set.find(L'耻') != std::wstring::npos);   //i18n-ignore: 测试数据
}

// =====================================================================
// AF —— 火烈鸟腿骨
// =====================================================================

TEST_CASE("AF1 两段骨骼求得的关节与髋、脚的距离等于骨长", "[lockdll]")
{
    const float l1 = 84.0f;
    const float l2 = 88.0f;
    const float feet[4][2] = { { 0.0f, 0.0f }, { 30.0f, -10.0f }, { -22.0f, -116.0f }, { -30.0f, -22.0f } };
    for (int i = 0; i < 4; ++i)
    {
        float kx = 0.0f;
        float ky = 0.0f;
        SolveTwoBoneKnee(0.0f, -168.0f, feet[i][0], feet[i][1], l1, l2, kx, ky);
        INFO("foot " << i);
        CHECK(fabsf(Dist(0.0f, -168.0f, kx, ky) - l1) < 0.05f);
        CHECK(fabsf(Dist(kx, ky, feet[i][0], feet[i][1]) - l2) < 0.05f);
    }
    //够不着时按最远可达距离计算，大腿长度仍不变
    float kx = 0.0f;
    float ky = 0.0f;
    SolveTwoBoneKnee(0.0f, -168.0f, 0.0f, 200.0f, l1, l2, kx, ky);
    CHECK(fabsf(Dist(0.0f, -168.0f, kx, ky) - l1) < 0.05f);
}

TEST_CASE("AF2 关节凸向身后（局部坐标 x 为负的一侧）", "[lockdll]")
{
    float kx = 0.0f;
    float ky = 0.0f;
    //髋在脚正上方：关节在竖线左侧
    SolveTwoBoneKnee(0.0f, -168.0f, 0.0f, 0.0f, 84.0f, 88.0f, kx, ky);
    CHECK(kx < 0.0f);
    //脚在前方（x 为正）：关节仍在髋脚连线朝 x 负方向的一侧
    SolveTwoBoneKnee(0.0f, -168.0f, 30.0f, 0.0f, 84.0f, 88.0f, kx, ky);
    const float cross = (30.0f - 0.0f) * (ky + 168.0f) - (0.0f + 168.0f) * (kx - 0.0f);
    CHECK(cross > 0.0f);
}

// =====================================================================
// AS1 —— 离屏渲染冒烟
// =====================================================================

namespace {

//把 WIC 位图存成 PNG
bool SavePng(IWICImagingFactory* wic, IWICBitmap* bitmap, const std::wstring& path)
{
    CComPtr<IWICStream> stream;
    CComPtr<IWICBitmapEncoder> encoder;
    CComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder)) ||
        FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, NULL)) || FAILED(frame->Initialize(NULL)))
    {
        return false;
    }
    return SUCCEEDED(frame->WriteSource(bitmap, NULL)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

//动画的英文名，用于 PNG 文件名
const wchar_t* const kAnimFileNames[kAnimCount] = { L"honor", L"flamingo", L"particle", L"bounce", L"rain" };

} // namespace

TEST_CASE("AS1 五种动画各在 WIC 位图上连续画 3 秒，不报错且画面不是单色", "[lockdll][lockrender]")
{
    const HRESULT coHr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    {
        CComPtr<ID2D1Factory> d2d;
        REQUIRE(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d)));
        CComPtr<IDWriteFactory> dwrite;
        REQUIRE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                              reinterpret_cast<IUnknown**>(&dwrite))));
        CComPtr<IWICImagingFactory> wic;
        REQUIRE(SUCCEEDED(wic.CoCreateInstance(CLSID_WICImagingFactory)));
        wchar_t shotDir[MAX_PATH] = { 0 };
        const bool saveShots = ::GetEnvironmentVariableW(kShotDirEnv, shotDir, MAX_PATH) > 0;

        for (int a = 0; a < kAnimCount; ++a)
        {
            INFO("animation = " << a);
            CComPtr<IWICBitmap> bitmap;
            REQUIRE(SUCCEEDED(wic->CreateBitmap(kShotWidth, kShotHeight, GUID_WICPixelFormat32bppPBGRA,
                                                WICBitmapCacheOnLoad, &bitmap)));
            CComPtr<ID2D1RenderTarget> rt;
            const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96.0f, 96.0f);
            REQUIRE(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap, props, &rt)));

            ILockScene* scene = CreateLockScene(static_cast<LockAnimation>(a));
            REQUIRE(scene != NULL);
            SceneEnv env;
            env.m_d2d = d2d;
            env.m_dwrite = dwrite;
            env.m_scale = 1.0f;
            scene->Init(env, static_cast<float>(kShotWidth), static_cast<float>(kShotHeight));
            //画面中央放一个鼠标位置，覆盖粒子时钟的推开逻辑
            scene->OnPointer(kShotWidth / 2.0f, kShotHeight / 2.0f);
            //起始时刻固定在当前这一分钟的第 10 秒：3 秒的模拟不会跨过整分钟（跨过时粒子时钟会炸散重聚，画面是一团）
            const uint64_t startMs = NowWallMs() / kMsPerMinute * kMsPerMinute + kStartSecondMs;
            const int frames = static_cast<int>(kShotSeconds * kShotFps);
            bool drawOk = true;
            for (int f = 0; f < frames; ++f)
            {
                const float dt = 1.0f / kShotFps;
                scene->Update(dt, startMs + static_cast<uint64_t>(f * dt * kMsPerSecond));
                rt->BeginDraw();
                rt->SetTransform(D2D1::Matrix3x2F::Identity());
                scene->Draw(rt);
                drawOk = drawOk && SUCCEEDED(rt->EndDraw());
            }
            delete scene;
            CHECK(drawOk);

            //画面不是单色：数一数不同于左上角像素的像素
            WICRect all = { 0, 0, kShotWidth, kShotHeight };
            CComPtr<IWICBitmapLock> lock;
            REQUIRE(SUCCEEDED(bitmap->Lock(&all, WICBitmapLockRead, &lock)));
            UINT size = 0;
            BYTE* data = NULL;
            UINT stride = 0;
            REQUIRE(SUCCEEDED(lock->GetDataPointer(&size, &data)));
            REQUIRE(SUCCEEDED(lock->GetStride(&stride)));
            const uint32_t first = *reinterpret_cast<const uint32_t*>(data);
            int differ = 0;
            for (int y = 0; y < kShotHeight; ++y)
            {
                const uint32_t* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
                for (int x = 0; x < kShotWidth; ++x)
                {
                    if (row[x] != first)
                        ++differ;
                }
            }
            lock.Release();
            CHECK(differ > kShotWidth * kShotHeight / 100);
            if (saveShots)
            {
                const std::wstring path = std::wstring(shotDir) + L"\\lock_" + kAnimFileNames[a] + L".png";
                CHECK(SavePng(wic, bitmap, path));
            }
        }
    }
    if (SUCCEEDED(coHr))
    {
        ::CoUninitialize();
    }
}

// =====================================================================
// AS2 —— 预览提示叠加层的离屏渲染（2026-10-07）
// =====================================================================

namespace {

//在 WIC 位图上画一帧：先铺深色底（或画场景），再按 frame 画叠加层
bool DrawOverlayShot(ID2D1Factory* d2d, IWICBitmap* bitmap, CLockOverlayRenderer& overlay, ILockScene* scene,
                     const OverlayFrame& frame)
{
    CComPtr<ID2D1RenderTarget> rt;
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bitmap, props, &rt)))
        return false;
    overlay.DiscardDeviceResources();
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (scene != NULL)
        scene->Draw(rt);
    else
        rt->Clear(ColorRgb(0x05060F));
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    overlay.Draw(rt, frame);
    const bool ok = SUCCEEDED(rt->EndDraw());
    overlay.DiscardDeviceResources();
    return ok;
}

//读出位图的全部像素（32 位 PBGRA，逐行紧排）
bool ReadPixels(IWICBitmap* bitmap, std::vector<uint32_t>& out)
{
    WICRect all = { 0, 0, kShotWidth, kShotHeight };
    CComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&all, WICBitmapLockRead, &lock)))
        return false;
    UINT size = 0;
    BYTE* data = NULL;
    UINT stride = 0;
    if (FAILED(lock->GetDataPointer(&size, &data)) || FAILED(lock->GetStride(&stride)))
        return false;
    out.resize(static_cast<size_t>(kShotWidth) * kShotHeight);
    for (int y = 0; y < kShotHeight; ++y)
    {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < kShotWidth; ++x)
            out[static_cast<size_t>(y) * kShotWidth + x] = row[x];
    }
    return true;
}

//预览时叠加层的一帧：只有动画阶段、不画时间，带一行提示
OverlayFrame PreviewFrame(const std::wstring& hint)
{
    OverlayFrame f;
    f.m_phase = kUiIdle;
    f.m_barHere = false;
    f.m_maskChars = 0;
    f.m_dt = kStepSec;
    f.m_nowMs = 0;
    f.m_mouse.x = -1;
    f.m_mouse.y = -1;
    f.m_showClock = false;
    f.m_wallMs = 0;
    f.m_hint = hint;
    return f;
}

} // namespace

TEST_CASE("AS2 预览提示画在画面上方的胶囊里，胶囊之外的画面不受影响", "[lockdll][lockrender]")
{
    const HRESULT coHr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    {
        CComPtr<ID2D1Factory> d2d;
        REQUIRE(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d)));
        CComPtr<IDWriteFactory> dwrite;
        REQUIRE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                              reinterpret_cast<IUnknown**>(&dwrite))));
        CComPtr<IWICImagingFactory> wic;
        REQUIRE(SUCCEEDED(wic.CoCreateInstance(CLSID_WICImagingFactory)));

        CLockOverlayRenderer overlay;
        overlay.Init(dwrite, 1.0f, kShotWidth, kShotHeight);
        const std::wstring hint = L"Press Esc or click to exit the preview";

        //同一深色底上画两帧：不带提示与带提示
        CComPtr<IWICBitmap> plain;
        CComPtr<IWICBitmap> hinted;
        REQUIRE(SUCCEEDED(wic->CreateBitmap(kShotWidth, kShotHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &plain)));
        REQUIRE(SUCCEEDED(wic->CreateBitmap(kShotWidth, kShotHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &hinted)));
        CHECK(DrawOverlayShot(d2d, plain, overlay, NULL, PreviewFrame(std::wstring())));
        CHECK(DrawOverlayShot(d2d, hinted, overlay, NULL, PreviewFrame(hint)));
        std::vector<uint32_t> a;
        std::vector<uint32_t> b;
        REQUIRE(ReadPixels(plain, a));
        REQUIRE(ReadPixels(hinted, b));

        //提示胶囊按文字宽度定位：胶囊里有足够多的像素变了，胶囊外一个像素都没变
        int changedInside = 0;
        int changedOutside = 0;
        int insideTotal = 0;
        RECT bounds = { kShotWidth, kShotHeight, 0, 0 };
        for (int y = 0; y < kShotHeight; ++y)
        {
            for (int x = 0; x < kShotWidth; ++x)
            {
                const size_t i = static_cast<size_t>(y) * kShotWidth + x;
                if (a[i] == b[i])
                    continue;
                bounds.left = (x < bounds.left) ? x : bounds.left;
                bounds.top = (y < bounds.top) ? y : bounds.top;
                bounds.right = (x + 1 > bounds.right) ? x + 1 : bounds.right;
                bounds.bottom = (y + 1 > bounds.bottom) ? y + 1 : bounds.bottom;
            }
        }
        //变化的区域：水平居中，位于画面上方 6% 高度附近，高度约 40 像素
        REQUIRE(bounds.right > bounds.left);
        CHECK(std::abs((bounds.left + bounds.right) / 2 - kShotWidth / 2) <= 2);
        CHECK(std::abs(bounds.top - kShotHeight * 6 / 100) <= 2);
        CHECK(bounds.bottom - bounds.top <= 42);
        CHECK(bounds.bottom - bounds.top >= 38);
        CHECK(bounds.bottom < kShotHeight / 2);
        for (int y = bounds.top; y < bounds.bottom; ++y)
        {
            for (int x = bounds.left; x < bounds.right; ++x)
            {
                const size_t i = static_cast<size_t>(y) * kShotWidth + x;
                ++insideTotal;
                if (a[i] != b[i])
                    ++changedInside;
            }
        }
        for (int y = 0; y < kShotHeight; ++y)
        {
            for (int x = 0; x < kShotWidth; ++x)
            {
                const bool inside = x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
                if (!inside && a[static_cast<size_t>(y) * kShotWidth + x] != b[static_cast<size_t>(y) * kShotWidth + x])
                    ++changedOutside;
            }
        }
        CHECK(changedOutside == 0);
        //胶囊底色铺满大部分区域（圆角之外的四角不变）
        CHECK(changedInside > insideTotal / 2);

        //设了 FLAMINGO_LOCK_SHOT_DIR 时，把「粒子时钟 + 中文提示」的一帧存成 PNG 供人工查看
        wchar_t shotDir[MAX_PATH] = { 0 };
        if (::GetEnvironmentVariableW(kShotDirEnv, shotDir, MAX_PATH) > 0)
        {
            ILockScene* scene = CreateLockScene(kAnimParticle);
            REQUIRE(scene != NULL);
            SceneEnv env;
            env.m_d2d = d2d;
            env.m_dwrite = dwrite;
            env.m_scale = 1.0f;
            scene->Init(env, static_cast<float>(kShotWidth), static_cast<float>(kShotHeight));
            const uint64_t startMs = NowWallMs() / kMsPerMinute * kMsPerMinute + kStartSecondMs;
            const int frames = static_cast<int>(kShotSeconds * kShotFps);
            for (int f = 0; f < frames; ++f)
            {
                const float dt = 1.0f / kShotFps;
                scene->Update(dt, startMs + static_cast<uint64_t>(f * dt * kMsPerSecond));
            }
            CComPtr<IWICBitmap> shot;
            REQUIRE(SUCCEEDED(wic->CreateBitmap(kShotWidth, kShotHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &shot)));
            //提示文字即设置窗口预览时显示的「按 Esc 或单击退出预览」（写成转义，测试文件不进多语言提取）
            OverlayFrame f = PreviewFrame(L"\x6309 Esc \x6216\x5355\x51FB\x9000\x51FA\x9884\x89C8");
            //与锁屏窗口的规则相同：场景自己显示时间（如粒子时钟）时不再画左下角的时间
            f.m_showClock = scene->ShowsCornerClock();
            f.m_wallMs = startMs;
            CHECK(DrawOverlayShot(d2d, shot, overlay, scene, f));
            delete scene;
            CHECK(SavePng(wic, shot, std::wstring(shotDir) + L"\\lock_preview_hint.png"));
        }
    }
    if (SUCCEEDED(coHr))
    {
        ::CoUninitialize();
    }
}

// =====================================================================
// AS3 —— 汉字数字雨的大红配色（2026-10-07，docs/锁屏方案.md 第 12 节）
// =====================================================================

namespace {

const double kRainRedDominance = 2.0;   //整幅画面的平均红色分量至少是绿色、蓝色分量的几倍
const uint32_t kChannelMask = 0xFF;      //32 位像素里取出一个 8 位分量的掩码
const int kGreenShift = 8;               //绿色分量在 32 位 PBGRA 像素里的位移
const int kRedShift = 16;                //红色分量的位移（蓝色在最低字节，位移为 0）

} // namespace

TEST_CASE("AS3 汉字数字雨以红色为主色：平均红色分量大于绿、蓝分量的 2 倍", "[lockdll][lockrender]")
{
    const HRESULT coHr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    {
        CComPtr<ID2D1Factory> d2d;
        REQUIRE(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d)));
        CComPtr<IDWriteFactory> dwrite;
        REQUIRE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                              reinterpret_cast<IUnknown**>(&dwrite))));
        CComPtr<IWICImagingFactory> wic;
        REQUIRE(SUCCEEDED(wic.CoCreateInstance(CLSID_WICImagingFactory)));

        CComPtr<IWICBitmap> bitmap;
        REQUIRE(SUCCEEDED(wic->CreateBitmap(kShotWidth, kShotHeight, GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapCacheOnLoad, &bitmap)));
        CComPtr<ID2D1RenderTarget> rt;
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        REQUIRE(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap, props, &rt)));

        //与 AS1 相同：连续画 3 秒，期间第一句八荣八耻已开始定格，画面里既有字符雨也有定格的字
        ILockScene* scene = CreateLockScene(kAnimRain);
        REQUIRE(scene != NULL);
        SceneEnv env;
        env.m_d2d = d2d;
        env.m_dwrite = dwrite;
        env.m_scale = 1.0f;
        scene->Init(env, static_cast<float>(kShotWidth), static_cast<float>(kShotHeight));
        const uint64_t startMs = NowWallMs() / kMsPerMinute * kMsPerMinute + kStartSecondMs;
        const int frames = static_cast<int>(kShotSeconds * kShotFps);
        bool drawOk = true;
        for (int f = 0; f < frames; ++f)
        {
            const float dt = 1.0f / kShotFps;
            scene->Update(dt, startMs + static_cast<uint64_t>(f * dt * kMsPerSecond));
            rt->BeginDraw();
            rt->SetTransform(D2D1::Matrix3x2F::Identity());
            scene->Draw(rt);
            drawOk = drawOk && SUCCEEDED(rt->EndDraw());
        }
        delete scene;
        CHECK(drawOk);

        //逐像素累加三个分量（32 位 PBGRA：最低字节为蓝，其次为绿、红）
        std::vector<uint32_t> pixels;
        REQUIRE(ReadPixels(bitmap, pixels));
        double sumR = 0.0;
        double sumG = 0.0;
        double sumB = 0.0;
        for (size_t i = 0; i < pixels.size(); ++i)
        {
            const uint32_t p = pixels[i];
            sumB += static_cast<double>(p & kChannelMask);
            sumG += static_cast<double>((p >> kGreenShift) & kChannelMask);
            sumR += static_cast<double>((p >> kRedShift) & kChannelMask);
        }
        const double count = static_cast<double>(pixels.size());
        const double avgR = sumR / count;
        const double avgG = sumG / count;
        const double avgB = sumB / count;
        INFO("average R = " << avgR << ", G = " << avgG << ", B = " << avgB);
        CHECK(avgR > avgG * kRainRedDominance);
        CHECK(avgR > avgB * kRainRedDominance);
    }
    if (SUCCEEDED(coHr))
    {
        ::CoUninitialize();
    }
}
