/**
 *  一对一通话网络质量评级的单元测试（标签 [callnet]，docs/音视频通话-网络状况标识方案.md 第 8.1 节），CallNetQualityTests.cpp
 *  覆盖 FlamingoMedia/Call/CallNetQuality.h：
 *    - NQ1 评级：门限表每一档的边界值（如往返时延 199 / 200 ms）分别落在正确的档；往返时延未知时只看其余三项；
 *           断流直接评为 0；窗口内没有应收的音频包（对方关了麦克风）时丢包、抖动、卡顿不参与评级；
 *    - NQ2 防抖：第一次直接采用；一个窗口变差不降档、连续两个降档（降到两个窗口里较好的一档）；升档要连续三个；
 *           中途被打断时从头计数；断流立即生效，恢复收包立即解除；
 *    - NQ3 两个方向取较差：我收 4、对方收 2 时显示 2，提示「对方网络不佳」；同一次降级只提示一次，回到一般及以上后
 *           再降才再提示；两个方向都差时按本端措辞；
 *    - NQ5 界面映射：档位到信号格数与颜色；千分比写成百分数；悬停详情各行文字，数据未就绪时写「正在检测」「暂无数据」；
 *           由快照组装悬停数据；
 *    - NQ6 信号图标位图（CallSignalArt.h，2026-10-06）：竖条自左向右不重叠、逐渐升高、都在位图之内；设计尺寸与 125% 尺寸下
 *           前 N 根是点亮色、其余是暗色，竖条以外完全透明；缓存同一档位同一尺寸只画一次。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Call/CallNetQuality.h"
#include "../../FlamingoMedia/Call/CallSignalArt.h"

using namespace call;

namespace {

//NQ1：一组各项都良好的指标（往返时延 50 ms、丢包 0、抖动 5 ms、卡顿 0，窗口内应收 50 个音频包）
const uint32_t kNqGoodRttMs = 50;
const uint32_t kNqGoodJitterMs = 5;
const uint32_t kNqAudioPackets = 50;
//NQ1：对方关了麦克风时播放端整秒补静音，卡顿率为 1000‰
const uint32_t kNqFullStallPermille = 1000;
//NQ1：很大的往返时延（毫秒），超出一切门限
const uint32_t kNqHugeRttMs = 5000;

//NQ5：悬停详情的样例数据
const int      kNqLinkPunched = 2;     //打洞直连（p2p_link_path_punched）
const int      kNqLinkLan = 1;         //局域网直连（p2p_link_path_lan）
const int      kNqLinkRelay = 3;       //中转（p2p_link_path_relay）
const int      kNqLinkNone = 0;        //通话自己的中转（未经 P2P）
const uint32_t kNqRttMs = 34;
const uint32_t kNqMineLoss = 5;        //0.5%
const uint32_t kNqMineJitter = 12;
const uint32_t kNqPeerLoss = 125;      //12.5%
const uint32_t kNqPeerJitter = 8;
const uint32_t kNqPeerStall = 20;      //2%
const uint32_t kNqMineBuffer = 60;     //我收对方的播放缓冲延迟（毫秒，B4）
const uint32_t kNqPeerBuffer = 140;    //对方收我的播放缓冲延迟（毫秒，B4）
//NQ5：悬停详情的行数（网络状况、线路、往返时延、我收对方、对方收我）
const size_t   kNqDetailLines = 5;

//NQ1：返回一组各项都良好的指标，用例在其上只改一项
CallNetMetrics GoodMetrics()
{
    CallNetMetrics m;
    m.m_hasRtt = true;
    m.m_rttMs = kNqGoodRttMs;
    m.m_audioPackets = kNqAudioPackets;
    m.m_jitterMs = kNqGoodJitterMs;
    return m;
}

//NQ1：只改往返时延后的评档
int GradeWithRtt(uint32_t rttMs)
{
    CallNetMetrics m = GoodMetrics();
    m.m_rttMs = rttMs;
    return GradeForMetrics(m);
}

//NQ1：只改丢包率后的评档
int GradeWithLoss(uint32_t permille)
{
    CallNetMetrics m = GoodMetrics();
    m.m_lossPermille = permille;
    return GradeForMetrics(m);
}

//NQ1：只改抖动后的评档
int GradeWithJitter(uint32_t jitterMs)
{
    CallNetMetrics m = GoodMetrics();
    m.m_jitterMs = jitterMs;
    return GradeForMetrics(m);
}

//NQ1：只改卡顿率后的评档
int GradeWithStall(uint32_t permille)
{
    CallNetMetrics m = GoodMetrics();
    m.m_stallPermille = permille;
    return GradeForMetrics(m);
}

//NQ1：只改播放缓冲延迟后的评档（2026-10-06 B4）
int GradeWithBuffer(uint32_t ms)
{
    CallNetMetrics m = GoodMetrics();
    m.m_bufferMs = ms;
    return GradeForMetrics(m);
}

} // namespace

TEST_CASE("NQ1 评级：门限表每一档的边界值落在正确的档", "[callnet]")
{
    SECTION("全部良好时为 4")
    {
        CHECK(GradeForMetrics(GoodMetrics()) == call_grade_good);
    }

    SECTION("往返时延的边界")
    {
        CHECK(GradeWithRtt(kGradeGoodRttMs - 1) == call_grade_good);
        CHECK(GradeWithRtt(kGradeGoodRttMs) == call_grade_fair);
        CHECK(GradeWithRtt(kGradeFairRttMs - 1) == call_grade_fair);
        CHECK(GradeWithRtt(kGradeFairRttMs) == call_grade_poor);
        CHECK(GradeWithRtt(kGradePoorRttMs - 1) == call_grade_poor);
        CHECK(GradeWithRtt(kGradePoorRttMs) == call_grade_very_poor);
    }

    SECTION("丢包率的边界")
    {
        CHECK(GradeWithLoss(kGradeGoodLossPermille - 1) == call_grade_good);
        CHECK(GradeWithLoss(kGradeGoodLossPermille) == call_grade_fair);
        CHECK(GradeWithLoss(kGradeFairLossPermille - 1) == call_grade_fair);
        CHECK(GradeWithLoss(kGradeFairLossPermille) == call_grade_poor);
        CHECK(GradeWithLoss(kGradePoorLossPermille - 1) == call_grade_poor);
        CHECK(GradeWithLoss(kGradePoorLossPermille) == call_grade_very_poor);
    }

    SECTION("抖动的边界")
    {
        CHECK(GradeWithJitter(kGradeGoodJitterMs - 1) == call_grade_good);
        CHECK(GradeWithJitter(kGradeGoodJitterMs) == call_grade_fair);
        CHECK(GradeWithJitter(kGradeFairJitterMs - 1) == call_grade_fair);
        CHECK(GradeWithJitter(kGradeFairJitterMs) == call_grade_poor);
        CHECK(GradeWithJitter(kGradePoorJitterMs - 1) == call_grade_poor);
        CHECK(GradeWithJitter(kGradePoorJitterMs) == call_grade_very_poor);
    }

    SECTION("卡顿率的边界")
    {
        CHECK(GradeWithStall(kGradeGoodStallPermille - 1) == call_grade_good);
        CHECK(GradeWithStall(kGradeGoodStallPermille) == call_grade_fair);
        CHECK(GradeWithStall(kGradeFairStallPermille - 1) == call_grade_fair);
        CHECK(GradeWithStall(kGradeFairStallPermille) == call_grade_poor);
        CHECK(GradeWithStall(kGradePoorStallPermille - 1) == call_grade_poor);
        CHECK(GradeWithStall(kGradePoorStallPermille) == call_grade_very_poor);
    }

    SECTION("播放缓冲延迟的边界（B4）")
    {
        CHECK(GradeWithBuffer(kGradeGoodBufferMs - 1) == call_grade_good);
        CHECK(GradeWithBuffer(kGradeGoodBufferMs) == call_grade_fair);
        CHECK(GradeWithBuffer(kGradeFairBufferMs - 1) == call_grade_fair);
        CHECK(GradeWithBuffer(kGradeFairBufferMs) == call_grade_poor);
        CHECK(GradeWithBuffer(kGradePoorBufferMs - 1) == call_grade_poor);
        CHECK(GradeWithBuffer(kGradePoorBufferMs) == call_grade_very_poor);
        //没有应收的音频包时缓冲延迟同样不参与评级
        CallNetMetrics m = GoodMetrics();
        m.m_audioPackets = 0;
        m.m_bufferMs = kGradePoorBufferMs;
        CHECK(GradeForMetrics(m) == call_grade_good);
    }

    SECTION("多项同时变差时取最差的那一项")
    {
        CallNetMetrics m = GoodMetrics();
        m.m_rttMs = kGradeGoodRttMs;               //一般
        m.m_jitterMs = kGradePoorJitterMs;         //很差
        CHECK(GradeForMetrics(m) == call_grade_very_poor);
    }

    SECTION("往返时延未知时只看其余三项")
    {
        CallNetMetrics m = GoodMetrics();
        m.m_hasRtt = false;
        m.m_rttMs = kNqHugeRttMs;
        CHECK(GradeForMetrics(m) == call_grade_good);
        m.m_lossPermille = kGradeFairLossPermille;
        CHECK(GradeForMetrics(m) == call_grade_poor);
    }

    SECTION("断流直接评为 0，不看其它指标")
    {
        CallNetMetrics m = GoodMetrics();
        m.m_silent = true;
        CHECK(GradeForMetrics(m) == call_grade_lost);
    }

    SECTION("没有应收的音频包（对方关了麦克风）时丢包、抖动、卡顿不参与评级，往返时延照常参与")
    {
        CallNetMetrics m = GoodMetrics();
        m.m_audioPackets = 0;
        m.m_stallPermille = kNqFullStallPermille;
        m.m_jitterMs = kGradePoorJitterMs;
        CHECK(GradeForMetrics(m) == call_grade_good);
        m.m_rttMs = kGradeFairRttMs;
        CHECK(GradeForMetrics(m) == call_grade_poor);
    }
}

TEST_CASE("NQ2 防抖：降档要连续两个窗口、升档要连续三个，断流立即生效", "[callnet]")
{
    CallGradeSmoother s;
    REQUIRE(s.Current() == kCallGradeUnknown);

    SECTION("第一次输入直接采用；未评级的输入被忽略")
    {
        CHECK(s.Feed(call_grade_poor) == call_grade_poor);
        CHECK(s.Feed(kCallGradeUnknown) == call_grade_poor);
    }

    SECTION("一个窗口变差不降档，连续两个才降")
    {
        s.Feed(call_grade_good);
        CHECK(s.Feed(call_grade_poor) == call_grade_good);
        CHECK(s.Feed(call_grade_poor) == call_grade_poor);
    }

    SECTION("连续两个更差的窗口档位不同时，降到其中较好的一档")
    {
        s.Feed(call_grade_good);
        s.Feed(call_grade_very_poor);
        CHECK(s.Feed(call_grade_fair) == call_grade_fair);
    }

    SECTION("变差的窗口之间夹了一个正常窗口，计数从头开始")
    {
        s.Feed(call_grade_good);
        s.Feed(call_grade_poor);
        s.Feed(call_grade_good);
        CHECK(s.Feed(call_grade_poor) == call_grade_good);
        CHECK(s.Feed(call_grade_poor) == call_grade_poor);
    }

    SECTION("升档要连续三个窗口，升到其中最差的一档")
    {
        s.Feed(call_grade_poor);
        CHECK(s.Feed(call_grade_good) == call_grade_poor);
        CHECK(s.Feed(call_grade_fair) == call_grade_poor);
        CHECK(s.Feed(call_grade_good) == call_grade_fair);
    }

    SECTION("升档途中出现一个更差的窗口，升档计数作废")
    {
        s.Feed(call_grade_poor);
        s.Feed(call_grade_good);
        s.Feed(call_grade_good);
        CHECK(s.Feed(call_grade_very_poor) == call_grade_poor);
        CHECK(s.Feed(call_grade_good) == call_grade_poor);
        CHECK(s.Feed(call_grade_good) == call_grade_poor);
        CHECK(s.Feed(call_grade_good) == call_grade_good);
    }

    SECTION("断流立即生效，恢复收包立即解除")
    {
        s.Feed(call_grade_good);
        CHECK(s.Feed(call_grade_lost) == call_grade_lost);
        CHECK(s.Feed(call_grade_fair) == call_grade_fair);
    }

    SECTION("Reset 回到未评级")
    {
        s.Feed(call_grade_good);
        s.Reset();
        CHECK(s.Current() == kCallGradeUnknown);
        CHECK(s.Feed(call_grade_very_poor) == call_grade_very_poor);
    }
}

TEST_CASE("NQ3 两个方向取较差，同一次降级只提示一次", "[callnet]")
{
    SECTION("CombineGrades")
    {
        CHECK(CombineGrades(call_grade_good, call_grade_poor) == call_grade_poor);
        CHECK(CombineGrades(call_grade_very_poor, call_grade_fair) == call_grade_very_poor);
        CHECK(CombineGrades(call_grade_fair, kCallGradeUnknown) == call_grade_fair);
        CHECK(CombineGrades(kCallGradeUnknown, call_grade_poor) == call_grade_poor);
        CHECK(CombineGrades(kCallGradeUnknown, kCallGradeUnknown) == kCallGradeUnknown);
        CHECK(CombineGrades(call_grade_good, call_grade_lost) == call_grade_lost);
    }

    CallDegradeNotifier n;

    SECTION("我收 4、对方收 2：提示对方网络不佳，之后一直差不再提示")
    {
        CHECK(n.Feed(call_grade_good, call_grade_poor) == call_hint_peer);
        CHECK(DegradeHintText(call_hint_peer) == L"对方网络不佳");
        CHECK(n.Feed(call_grade_good, call_grade_poor) == call_hint_none);
        CHECK(n.Feed(call_grade_good, call_grade_very_poor) == call_hint_none);
    }

    SECTION("回到一般及以上之后再降，再提示一次")
    {
        n.Feed(call_grade_good, call_grade_poor);
        CHECK(n.Feed(call_grade_good, call_grade_fair) == call_hint_none);
        CHECK(n.Feed(call_grade_poor, call_grade_good) == call_hint_mine);
        CHECK(DegradeHintText(call_hint_mine) == L"当前网络不佳，通话可能卡顿");
    }

    SECTION("两个方向都差时按本端措辞")
    {
        CHECK(n.Feed(call_grade_poor, call_grade_very_poor) == call_hint_mine);
    }

    SECTION("断流也算降到较差及以下")
    {
        CHECK(n.Feed(call_grade_lost, kCallGradeUnknown) == call_hint_mine);
    }

    SECTION("未评级、一般及以上都不提示")
    {
        CHECK(n.Feed(kCallGradeUnknown, kCallGradeUnknown) == call_hint_none);
        CHECK(n.Feed(call_grade_fair, call_grade_fair) == call_hint_none);
        CHECK(DegradeHintText(call_hint_none).empty());
    }
}

TEST_CASE("NQ5 界面映射：信号格、百分数与悬停详情", "[callnet]")
{
    SECTION("档位到信号格数与颜色")
    {
        const CallSignalLook good = SignalLookForGrade(call_grade_good);
        const CallSignalLook fair = SignalLookForGrade(call_grade_fair);
        const CallSignalLook poor = SignalLookForGrade(call_grade_poor);
        const CallSignalLook bad = SignalLookForGrade(call_grade_very_poor);
        const CallSignalLook lost = SignalLookForGrade(call_grade_lost);
        const CallSignalLook unknown = SignalLookForGrade(kCallGradeUnknown);
        CHECK(good.m_bars == kSignalBarCount);
        CHECK(fair.m_bars == kSignalBarCount - 1);
        CHECK(poor.m_bars == 2);
        CHECK(bad.m_bars == 1);
        CHECK(lost.m_bars == 0);
        CHECK(unknown.m_bars == 0);
        //良好与一般同为绿色；较差、很差各用一种更醒目的颜色，三者互不相同
        CHECK(good.m_litColor == fair.m_litColor);
        CHECK(poor.m_litColor != good.m_litColor);
        CHECK(bad.m_litColor != good.m_litColor);
        CHECK(bad.m_litColor != poor.m_litColor);
        //点亮的格子与未点亮的格子颜色不同
        CHECK(good.m_dimColor != good.m_litColor);
    }

    SECTION("千分比写成百分数")
    {
        CHECK(FormatPermilleAsPercent(0) == L"0%");
        CHECK(FormatPermilleAsPercent(5) == L"0.5%");
        CHECK(FormatPermilleAsPercent(20) == L"2%");
        CHECK(FormatPermilleAsPercent(125) == L"12.5%");
        CHECK(FormatPermilleAsPercent(1000) == L"100%");
    }

    SECTION("档位与线路的名称")
    {
        CHECK(GradeName(call_grade_good) == L"良好");
        CHECK(GradeName(call_grade_fair) == L"一般");
        CHECK(GradeName(call_grade_poor) == L"较差");
        CHECK(GradeName(call_grade_very_poor) == L"很差");
        CHECK(GradeName(call_grade_lost) == L"已断开");
        CHECK(GradeName(kCallGradeUnknown) == L"正在检测");
        CHECK(LinkPathName(kNqLinkLan) == L"局域网直连");
        CHECK(LinkPathName(kNqLinkPunched) == L"打洞直连");
        CHECK(LinkPathName(kNqLinkRelay) == L"服务器中转");
        CHECK(LinkPathName(kNqLinkNone) == L"服务器中转");
    }

    SECTION("数据齐全时的悬停详情")
    {
        CallNetView v;
        v.m_grade = call_grade_poor;
        v.m_linkPath = kNqLinkPunched;
        v.m_hasRtt = true;
        v.m_rttMs = kNqRttMs;
        v.m_hasMine = true;
        v.m_mineLossPermille = kNqMineLoss;
        v.m_mineJitterMs = kNqMineJitter;
        v.m_hasPeer = true;
        v.m_peerLossPermille = kNqPeerLoss;
        v.m_peerJitterMs = kNqPeerJitter;
        v.m_peerStallPermille = kNqPeerStall;
        v.m_mineBufferMs = kNqMineBuffer;
        v.m_peerBufferMs = kNqPeerBuffer;
        const std::vector<std::wstring> lines = NetDetailLines(v);
        REQUIRE(lines.size() == kNqDetailLines);
        CHECK(lines[0] == L"网络状况：较差");
        CHECK(lines[1] == L"线路：打洞直连");
        CHECK(lines[2] == L"往返时延：34 ms");
        CHECK(lines[3] == L"我收对方：丢包 0.5%，抖动 12 ms，卡顿 0%，缓冲 60 ms");
        CHECK(lines[4] == L"对方收我：丢包 12.5%，抖动 8 ms，卡顿 2%，缓冲 140 ms");
    }

    SECTION("由快照组装悬停数据：显示档位取两个方向较差的一档，对方收我取最近一份报告")
    {
        CallNetSnapshot s;
        CHECK(PeerGradeOf(s) == kCallGradeUnknown);
        CallNetView empty = MakeNetView(s, kNqLinkLan);
        CHECK(empty.m_grade == kCallGradeUnknown);
        CHECK(empty.m_linkPath == kNqLinkLan);
        CHECK_FALSE(empty.m_hasMine);
        CHECK_FALSE(empty.m_hasPeer);

        s.m_hasRtt = true;
        s.m_rttMs = kNqRttMs;
        s.m_myGrade = call_grade_good;
        s.m_hasMine = true;
        s.m_mineLossPermille = kNqMineLoss;
        s.m_mineJitterMs = kNqMineJitter;
        s.m_peerReports = 1;
        s.m_peerReport.m_grade = call_grade_poor;
        s.m_peerReport.m_lossPermille = kNqPeerLoss;
        s.m_peerReport.m_jitterMs = kNqPeerJitter;
        s.m_peerReport.m_stallPermille = kNqPeerStall;
        s.m_mineBufferMs = kNqMineBuffer;
        s.m_peerReport.m_bufferMs = static_cast<uint16_t>(kNqPeerBuffer);
        CHECK(PeerGradeOf(s) == call_grade_poor);
        const CallNetView v = MakeNetView(s, kNqLinkPunched);
        CHECK(v.m_grade == call_grade_poor);
        CHECK(v.m_hasRtt);
        CHECK(v.m_rttMs == kNqRttMs);
        CHECK(v.m_hasMine);
        CHECK(v.m_mineLossPermille == kNqMineLoss);
        CHECK(v.m_mineJitterMs == kNqMineJitter);
        CHECK(v.m_hasPeer);
        CHECK(v.m_peerLossPermille == kNqPeerLoss);
        CHECK(v.m_peerJitterMs == kNqPeerJitter);
        CHECK(v.m_peerStallPermille == kNqPeerStall);
        CHECK(v.m_mineBufferMs == kNqMineBuffer);
        CHECK(v.m_peerBufferMs == kNqPeerBuffer);

        //对方报告里尚未评级：只看本端
        s.m_peerReport.m_grade = kCallGradeUnknown;
        CHECK(MakeNetView(s, kNqLinkPunched).m_grade == call_grade_good);
    }

    SECTION("刚接通、数据未就绪时的悬停详情")
    {
        CallNetView v;
        const std::vector<std::wstring> lines = NetDetailLines(v);
        REQUIRE(lines.size() == kNqDetailLines);
        CHECK(lines[0] == L"网络状况：正在检测");
        CHECK(lines[1] == L"线路：服务器中转");
        CHECK(lines[2] == L"往返时延：正在检测");
        CHECK(lines[3] == L"我收对方：暂无数据");
        CHECK(lines[4] == L"对方收我：暂无数据");
    }
}

namespace {

//NQ6：125% DPI 下信号图标的像素尺寸（20×16 的 1.25 倍）
const int kNqScaledW = 25;
const int kNqScaledH = 20;
//NQ6：32 位位图每像素的字节数，以及蓝、绿、红、透明度四个分量在像素里的偏移（BGRA 顺序）
const int kNqBytesPerPixel = 4;
const int kNqBlue = 0;
const int kNqGreen = 1;
const int kNqRed = 2;
const int kNqAlpha = 3;

//NQ6：读位图里某像素的 BGRA 四个分量，按 RGB 宏的顺序拼成 COLORREF，透明度单独返回
bool ReadSignalPixel(HBITMAP bmp, int x, int y, COLORREF& color, BYTE& alpha)
{
    DIBSECTION ds;
    memset(&ds, 0, sizeof(ds));
    if (::GetObject(bmp, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBits == NULL)
        return false;
    const BYTE* row = static_cast<const BYTE*>(ds.dsBm.bmBits) + static_cast<size_t>(y) * ds.dsBm.bmWidthBytes;
    const BYTE* px = row + static_cast<size_t>(x) * kNqBytesPerPixel;
    color = RGB(px[kNqRed], px[kNqGreen], px[kNqBlue]);
    alpha = px[kNqAlpha];
    return true;
}

//NQ6：检查一张位图：前 bars 根竖条中心是点亮色，其余是暗色，左上角透明
void CheckSignalBitmap(int grade, int widthPx, int heightPx)
{
    const CallSignalLook look = SignalLookForGrade(grade);
    HBITMAP bmp = MakeCallSignalBitmap(look, widthPx, heightPx);
    REQUIRE(bmp != NULL);
    for (int i = 0; i < kSignalBarCount; ++i)
    {
        const RECT rc = CallSignalBarRect(i, widthPx, heightPx);
        COLORREF color = 0;
        BYTE alpha = 0;
        REQUIRE(ReadSignalPixel(bmp, (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2, color, alpha));
        INFO("grade " << grade << " bar " << i << " size " << widthPx << "x" << heightPx);
        CHECK(alpha == 255);
        CHECK(color == ((i < look.m_bars) ? look.m_litColor : look.m_dimColor));
    }
    COLORREF corner = 0;
    BYTE cornerAlpha = 1;
    REQUIRE(ReadSignalPixel(bmp, 0, 0, corner, cornerAlpha));
    CHECK(cornerAlpha == 0);
    ::DeleteObject(bmp);
}

} // namespace

TEST_CASE("NQ6 信号图标位图", "[callnet]")
{
    SECTION("竖条几何：自左向右不重叠、逐渐升高、都在位图之内")
    {
        const int sizes[][2] = { { kCallSignalIconW, kCallSignalIconH }, { kNqScaledW, kNqScaledH } };
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        {
            const int w = sizes[s][0];
            const int h = sizes[s][1];
            RECT prev = { 0, 0, 0, 0 };
            for (int i = 0; i < kSignalBarCount; ++i)
            {
                const RECT rc = CallSignalBarRect(i, w, h);
                INFO("size " << w << "x" << h << " bar " << i);
                CHECK(rc.left >= 0);
                CHECK(rc.right <= w);
                CHECK(rc.top >= 0);
                CHECK(rc.bottom <= h);
                CHECK(rc.right > rc.left);
                CHECK(rc.bottom > rc.top);
                if (i > 0)
                {
                    CHECK(rc.left >= prev.right);
                    CHECK(rc.top < prev.top);
                    CHECK(rc.bottom == prev.bottom);
                }
                prev = rc;
            }
        }
        const RECT bad = CallSignalBarRect(kSignalBarCount, kCallSignalIconW, kCallSignalIconH);
        CHECK(bad.right == 0);
        CHECK(MakeCallSignalBitmap(SignalLookForGrade(call_grade_good), 0, kCallSignalIconH) == NULL);
    }

    SECTION("各档位在设计尺寸与 125% 尺寸下的颜色")
    {
        const int grades[] = { call_grade_good, call_grade_fair, call_grade_poor, call_grade_very_poor, call_grade_lost,
                               kCallGradeUnknown };
        for (size_t g = 0; g < sizeof(grades) / sizeof(grades[0]); ++g)
        {
            CheckSignalBitmap(grades[g], kCallSignalIconW, kCallSignalIconH);
            CheckSignalBitmap(grades[g], kNqScaledW, kNqScaledH);
        }
    }

    SECTION("缓存：同一档位同一尺寸只画一次，尺寸不同另画一张")
    {
        CallSignalArtCache cache;
        HBITMAP a = cache.Get(call_grade_good, kCallSignalIconW, kCallSignalIconH);
        REQUIRE(a != NULL);
        CHECK(cache.Get(call_grade_good, kCallSignalIconW, kCallSignalIconH) == a);
        CHECK(cache.Count() == 1);
        HBITMAP b = cache.Get(call_grade_good, kNqScaledW, kNqScaledH);
        CHECK(b != NULL);
        CHECK(b != a);
        CHECK(cache.Get(call_grade_poor, kCallSignalIconW, kCallSignalIconH) != a);
        CHECK(cache.Count() == 3);
        CHECK(cache.Get(call_grade_good, 0, 0) == NULL);
    }
}
