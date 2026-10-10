/**
 *  一对一通话的网络质量评级（纯逻辑）的实现，CallNetQuality.cpp
 *  评级门限、防抖、两个方向取较差、降级提示与界面文字的映射，说明见 CallNetQuality.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "CallNetQuality.h"

#include "../../Source/Common/I18n/I18n.h"   //TR / TRF：界面文字的多语言查找

namespace call {

namespace {

//信号格的颜色
const COLORREF kSignalGreen  = RGB(0x2F, 0xB4, 0x5A);   //良好、一般：绿色（与接听按钮同色）
const COLORREF kSignalOrange = RGB(0xF5, 0x9E, 0x0B);   //较差：橙色
const COLORREF kSignalRed    = RGB(0xE5, 0x48, 0x4D);   //很差：红色（与挂断按钮同色）
const COLORREF kSignalDim    = RGB(0xC3, 0xC9, 0xD1);   //未点亮的格子，以及断流、未评级时的全部格子：浅灰

//千分比换算成百分数时的进制：1% = 10‰
const uint32_t kPermillePerPercent = 10;

//p2p_link_path 的取值（FlamingoP2PApi.h），本文件不包含那个头文件，只用这两个数
const int kLinkPathLan     = 1;   //局域网直连
const int kLinkPathPunched = 2;   //打洞直连

//一项指标落在哪一档：小于良好门限为 4、小于一般门限为 3、小于较差门限为 2，其余为 1
int GradeForValue(uint32_t value, uint32_t goodLimit, uint32_t fairLimit, uint32_t poorLimit)
{
    if (value < goodLimit)
        return call_grade_good;
    if (value < fairLimit)
        return call_grade_fair;
    if (value < poorLimit)
        return call_grade_poor;
    return call_grade_very_poor;
}

//两个档位中较差的一个
int WorseGrade(int a, int b)
{
    return (a < b) ? a : b;
}

//「丢包 x%，抖动 y ms，卡顿 z%，缓冲 w ms」四项接收统计的文字，前缀为「我收对方」或「对方收我」
std::wstring RecvLine(const wchar_t* pattern, uint32_t lossPermille, uint32_t jitterMs, uint32_t stallPermille,
                      uint32_t bufferMs)
{
    return ::i18n::FormatText(pattern, FormatPermilleAsPercent(lossPermille), jitterMs,
                              FormatPermilleAsPercent(stallPermille), bufferMs);
}

} // namespace

CallNetMetrics::CallNetMetrics()
    : m_silent(false)
    , m_hasRtt(false)
    , m_rttMs(0)
    , m_audioPackets(0)
    , m_lossPermille(0)
    , m_jitterMs(0)
    , m_stallPermille(0)
    , m_bufferMs(0)
{
}

int GradeForMetrics(const CallNetMetrics& m)
{
    if (m.m_silent)
        return call_grade_lost;

    int grade = call_grade_good;
    if (m.m_hasRtt)
        grade = WorseGrade(grade, GradeForValue(m.m_rttMs, kGradeGoodRttMs, kGradeFairRttMs, kGradePoorRttMs));

    //对方没在发音频（关了麦克风）时，播放端整秒补静音、丢包与抖动也没有样本，这三项不能说明线路好坏
    if (m.m_audioPackets > 0)
    {
        grade = WorseGrade(grade, GradeForValue(m.m_lossPermille, kGradeGoodLossPermille, kGradeFairLossPermille,
                                                kGradePoorLossPermille));
        grade = WorseGrade(grade, GradeForValue(m.m_jitterMs, kGradeGoodJitterMs, kGradeFairJitterMs,
                                                kGradePoorJitterMs));
        grade = WorseGrade(grade, GradeForValue(m.m_stallPermille, kGradeGoodStallPermille, kGradeFairStallPermille,
                                                kGradePoorStallPermille));
        grade = WorseGrade(grade, GradeForValue(m.m_bufferMs, kGradeGoodBufferMs, kGradeFairBufferMs,
                                                kGradePoorBufferMs));
    }
    return grade;
}

CallGradeSmoother::CallGradeSmoother()
{
    Reset();
}

void CallGradeSmoother::Reset()
{
    m_current = kCallGradeUnknown;
    m_pendingDir = 0;
    m_pendingCount = 0;
    m_pendingTarget = kCallGradeUnknown;
}

int CallGradeSmoother::Feed(int raw)
{
    if (raw == kCallGradeUnknown)
        return m_current;

    //第一次输入、断流、从断流恢复：都立即采用，不走连续计数
    if (m_current == kCallGradeUnknown || raw == call_grade_lost || m_current == call_grade_lost)
    {
        m_current = raw;
        m_pendingDir = 0;
        m_pendingCount = 0;
        return m_current;
    }

    if (raw == m_current)
    {
        m_pendingDir = 0;
        m_pendingCount = 0;
        return m_current;
    }

    const int dir = (raw < m_current) ? -1 : 1;
    if (dir != m_pendingDir)
    {
        //方向变了（或刚开始累计）：从这一个窗口重新计数
        m_pendingDir = dir;
        m_pendingCount = 1;
        m_pendingTarget = raw;
    }
    else
    {
        ++m_pendingCount;
        //降档取这几个窗口里最好的一档、升档取最差的一档，两种情况都是取更靠近当前档位的那个
        if (dir < 0)
            m_pendingTarget = (raw > m_pendingTarget) ? raw : m_pendingTarget;
        else
            m_pendingTarget = (raw < m_pendingTarget) ? raw : m_pendingTarget;
    }

    const int needed = (dir < 0) ? kGradeDowngradeWindows : kGradeUpgradeWindows;
    if (m_pendingCount >= needed)
    {
        m_current = m_pendingTarget;
        m_pendingDir = 0;
        m_pendingCount = 0;
    }
    return m_current;
}

int CombineGrades(int mine, int peer)
{
    if (mine == kCallGradeUnknown)
        return peer;
    if (peer == kCallGradeUnknown)
        return mine;
    return WorseGrade(mine, peer);
}

CallDegradeNotifier::CallDegradeNotifier()
    : m_armed(true)
{
}

void CallDegradeNotifier::Reset()
{
    m_armed = true;
}

int CallDegradeNotifier::Feed(int mine, int peer)
{
    const int shown = CombineGrades(mine, peer);
    if (shown == kCallGradeUnknown || shown > kDegradeHintGrade)
    {
        m_armed = true;
        return call_hint_none;
    }
    if (!m_armed)
        return call_hint_none;

    m_armed = false;
    //本端收得差时按本端措辞（两个方向都差也是如此）；否则差的只能是对方收我这个方向
    if (mine != kCallGradeUnknown && mine <= kDegradeHintGrade)
        return call_hint_mine;
    return call_hint_peer;
}

std::wstring DegradeHintText(int hint)
{
    switch (hint)
    {
    //本端收对方收得差
    case call_hint_mine:
        return TR(L"当前网络不佳，通话可能卡顿");

    //对方收本端收得差
    case call_hint_peer:
        return TR(L"对方网络不佳");

    //不提示
    default:
        return std::wstring();
    }
}

CallSignalLook::CallSignalLook()
    : m_bars(0)
    , m_litColor(kSignalDim)
    , m_dimColor(kSignalDim)
{
}

CallSignalLook SignalLookForGrade(int grade)
{
    CallSignalLook look;
    switch (grade)
    {
    //良好：4 格绿
    case call_grade_good:
        look.m_bars = kSignalBarCount;
        look.m_litColor = kSignalGreen;
        break;

    //一般：3 格绿
    case call_grade_fair:
        look.m_bars = kSignalBarCount - 1;
        look.m_litColor = kSignalGreen;
        break;

    //较差：2 格橙
    case call_grade_poor:
        look.m_bars = call_grade_poor;
        look.m_litColor = kSignalOrange;
        break;

    //很差：1 格红
    case call_grade_very_poor:
        look.m_bars = call_grade_very_poor;
        look.m_litColor = kSignalRed;
        break;

    //断流与未评级：一格都不点亮，四格全灰
    default:
        break;
    }
    return look;
}

std::wstring GradeName(int grade)
{
    switch (grade)
    {
    //良好
    case call_grade_good:
        return TRC(L"网络质量", L"良好");

    //一般
    case call_grade_fair:
        return TRC(L"网络质量", L"一般");

    //较差
    case call_grade_poor:
        return TRC(L"网络质量", L"较差");

    //很差
    case call_grade_very_poor:
        return TRC(L"网络质量", L"很差");

    //断流：连续 3 秒没收到对方的任何消息
    case call_grade_lost:
        return TRC(L"网络质量", L"已断开");

    //未评级：刚接通、第一个窗口还没结算
    default:
        return TRC(L"网络质量", L"正在检测");
    }
}

std::wstring LinkPathName(int linkPath)
{
    if (linkPath == kLinkPathLan)
        return TR(L"局域网直连");
    if (linkPath == kLinkPathPunched)
        return TR(L"打洞直连");
    return TR(L"服务器中转");
}

std::wstring FormatPermilleAsPercent(uint32_t permille)
{
    const uint32_t whole = permille / kPermillePerPercent;
    const uint32_t tenth = permille % kPermillePerPercent;
    wchar_t buf[32] = { 0 };
    if (tenth == 0)
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u%%", whole);
    else
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u.%u%%", whole, tenth);
    return std::wstring(buf);
}

CallNetView::CallNetView()
    : m_grade(kCallGradeUnknown)
    , m_linkPath(0)
    , m_hasRtt(false)
    , m_rttMs(0)
    , m_hasMine(false)
    , m_mineLossPermille(0)
    , m_mineJitterMs(0)
    , m_mineStallPermille(0)
    , m_mineBufferMs(0)
    , m_hasPeer(false)
    , m_peerLossPermille(0)
    , m_peerJitterMs(0)
    , m_peerStallPermille(0)
    , m_peerBufferMs(0)
{
}

std::vector<std::wstring> NetDetailLines(const CallNetView& v)
{
    std::vector<std::wstring> lines;
    lines.push_back(TRF(L"网络状况：{0}", GradeName(v.m_grade)));
    lines.push_back(TRF(L"线路：{0}", LinkPathName(v.m_linkPath)));
    if (v.m_hasRtt)
        lines.push_back(TRF(L"往返时延：{0} ms", v.m_rttMs));
    else
        lines.push_back(TR(L"往返时延：正在检测"));

    if (v.m_hasMine)
        lines.push_back(RecvLine(TR(L"我收对方：丢包 {0}，抖动 {1} ms，卡顿 {2}，缓冲 {3} ms"), v.m_mineLossPermille,
                                 v.m_mineJitterMs, v.m_mineStallPermille, v.m_mineBufferMs));
    else
        lines.push_back(TR(L"我收对方：暂无数据"));

    if (v.m_hasPeer)
        lines.push_back(RecvLine(TR(L"对方收我：丢包 {0}，抖动 {1} ms，卡顿 {2}，缓冲 {3} ms"), v.m_peerLossPermille,
                                 v.m_peerJitterMs, v.m_peerStallPermille, v.m_peerBufferMs));
    else
        lines.push_back(TR(L"对方收我：暂无数据"));
    return lines;
}

CallNetSnapshot::CallNetSnapshot()
    : m_hasRtt(false)
    , m_rttMs(0)
    , m_peerReports(0)
    , m_myGrade(kCallGradeUnknown)
    , m_hasMine(false)
    , m_mineLossPermille(0)
    , m_mineJitterMs(0)
    , m_mineStallPermille(0)
    , m_mineBufferMs(0)
{
}

int PeerGradeOf(const CallNetSnapshot& s)
{
    if (s.m_peerReports == 0)
        return kCallGradeUnknown;
    return s.m_peerReport.m_grade;
}

CallNetView MakeNetView(const CallNetSnapshot& s, int linkPath)
{
    CallNetView v;
    v.m_grade = CombineGrades(s.m_myGrade, PeerGradeOf(s));
    v.m_linkPath = linkPath;
    v.m_hasRtt = s.m_hasRtt;
    v.m_rttMs = s.m_rttMs;
    v.m_hasMine = s.m_hasMine;
    v.m_mineLossPermille = s.m_mineLossPermille;
    v.m_mineJitterMs = s.m_mineJitterMs;
    v.m_mineStallPermille = s.m_mineStallPermille;
    v.m_mineBufferMs = s.m_mineBufferMs;
    v.m_hasPeer = (s.m_peerReports > 0);
    v.m_peerLossPermille = s.m_peerReport.m_lossPermille;
    v.m_peerJitterMs = s.m_peerReport.m_jitterMs;
    v.m_peerStallPermille = s.m_peerReport.m_stallPermille;
    v.m_peerBufferMs = s.m_peerReport.m_bufferMs;
    return v;
}

} // namespace call
