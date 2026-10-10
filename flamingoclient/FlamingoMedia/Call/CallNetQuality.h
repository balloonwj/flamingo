/**
 *  一对一通话的网络质量评级（纯逻辑），CallNetQuality.h
 *
 *  docs/音视频通话-网络状况标识方案.md 第 3 期：把 CallNetStats 统计出的指标换算成档位，再换算成界面上的信号格、
 *  悬停详情与降级提示。分四部分：
 *    - GradeForMetrics：按第 5 节的门限表给一个 1 秒窗口评档（4 良好、3 一般、2 较差、1 很差、0 断流）；
 *    - CallGradeSmoother：防抖。降档要连续 2 个窗口都更低，升档要连续 3 个窗口都更高，断流立即生效、恢复收包立即解除；
 *    - CombineGrades / CallDegradeNotifier：两个方向（我收对方、对方收我）取较差的一档；降到「较差」及以下时提示一次，
 *      按差在哪个方向分别措辞，回到「一般」及以上后才会再次提示；
 *    - SignalLookForGrade / NetDetailLines：档位到信号格数与颜色、悬停详情文字的映射。
 *  不碰 socket、设备与窗口，单元测试见 Source/Tests/CallNetQualityTests.cpp（标签 [callnet]）。
 *
 *  门限是常见 VoIP 质量经验值，未经本项目实测，第 3 期需用弱网工具校准（方案第 8.3 节）。
 *
 *  典型用法（统计线程每秒一次）：
 *      CallNetMetrics m = ...;                               //这一秒的接收统计与往返时延
 *      int mine = smoother.Feed(GradeForMetrics(m));         //本端「我收对方」的档位，填进发给对方的报告
 *      int shown = CombineGrades(mine, peerReport.m_grade);  //界面显示的档位
 *      int hint = notifier.Feed(mine, peerReport.m_grade);   //是否要浮出降级提示
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include <windows.h>

#include "CallNetStats.h"   //kCallGradeUnknown

namespace call {

//网络质量档位；「未评级」另用 kCallGradeUnknown（0xFF）表示
enum CallNetGrade
{
    call_grade_lost      = 0,   //断流：连续 kCallSilentMs 没有收到对方的任何消息
    call_grade_very_poor = 1,   //很差：任一指标超出「较差」的门限
    call_grade_poor      = 2,   //较差：各项指标都在「较差」的门限之内，但有一项超出「一般」的门限
    call_grade_fair      = 3,   //一般：各项指标都在「一般」的门限之内，但有一项超出「良好」的门限
    call_grade_good      = 4,   //良好：各项指标都在「良好」的门限之内
};

//评级门限（方案第 5 节表格）：指标「小于」该值才算落在这一档内
const uint32_t kGradeGoodRttMs          = 200;   //良好：往返时延上限（毫秒）
const uint32_t kGradeFairRttMs          = 400;   //一般：往返时延上限（毫秒）
const uint32_t kGradePoorRttMs          = 800;   //较差：往返时延上限（毫秒）
const uint32_t kGradeGoodLossPermille   = 20;    //良好：丢包率上限（千分比，即 2%）
const uint32_t kGradeFairLossPermille   = 50;    //一般：丢包率上限（千分比，即 5%）
const uint32_t kGradePoorLossPermille   = 150;   //较差：丢包率上限（千分比，即 15%）
const uint32_t kGradeGoodJitterMs       = 30;    //良好：抖动上限（毫秒）
const uint32_t kGradeFairJitterMs       = 60;    //一般：抖动上限（毫秒）
const uint32_t kGradePoorJitterMs       = 120;   //较差：抖动上限（毫秒）
const uint32_t kGradeGoodStallPermille  = 20;    //良好：卡顿率上限（千分比，即 2%）
const uint32_t kGradeFairStallPermille  = 50;    //一般：卡顿率上限（千分比，即 5%）
const uint32_t kGradePoorStallPermille  = 150;   //较差：卡顿率上限（千分比，即 15%）
//播放缓冲延迟的门限（毫秒，2026-10-06 方案 B 第 B4 步）：初值按方案第 9.5 节取，第 B5 步用弱网校准实测后定
const uint32_t kGradeGoodBufferMs       = 120;   //良好：播放缓冲延迟上限
const uint32_t kGradeFairBufferMs       = 250;   //一般：播放缓冲延迟上限
const uint32_t kGradePoorBufferMs       = 500;   //较差：播放缓冲延迟上限

//防抖：降档需要连续多少个窗口都落在更低档
const int kGradeDowngradeWindows = 2;
//防抖：升档需要连续多少个窗口都落在更高档
const int kGradeUpgradeWindows = 3;

//一个 1 秒窗口里参与评级的指标（都是「对方 → 本端」这个方向，往返时延除外）
struct CallNetMetrics
{
    bool     m_silent;          //是否已断流（CallRecvStats::IsSilent）；为 true 时直接评为断流
    bool     m_hasRtt;          //是否已有往返时延样本；没有时往返时延不参与评级
    uint32_t m_rttMs;           //往返时延（毫秒）
    uint32_t m_audioPackets;    //窗口内应收的音频包数（实收 + 丢失）。为 0 表示对方没在发音频（关了麦克风），
                                //此时丢包、抖动、卡顿都不参与评级：播放端取不到采样只是因为对方没说话
    uint32_t m_lossPermille;    //丢包率（千分比）
    uint32_t m_jitterMs;        //抖动（毫秒）
    uint32_t m_stallPermille;   //播放端补静音的比例（千分比）
    uint32_t m_bufferMs;        //播放缓冲延迟（毫秒，B4）：正常播放时抖动缓冲中已收到、还没播放的平均时长

    CallNetMetrics();
};

/**
 *  按方案第 5 节的门限给一个窗口评档：从「良好」往下判，所有参与评级的指标都在某档门限之内即取该档。
 *  @param m 窗口指标
 *  @return 档位，取 CallNetGrade（不会返回 kCallGradeUnknown）
 */
int GradeForMetrics(const CallNetMetrics& m);

/**
 *  档位防抖：避免一两秒的波动让图标来回跳。
 *    - 第一次输入直接采用；
 *    - 输入断流时立即变为断流；处于断流时输入任何非断流档位立即采用（恢复收包立即解除）；
 *    - 连续 kGradeDowngradeWindows 个窗口都低于当前档位才降档，降到这几个窗口里最好的那一档；
 *    - 连续 kGradeUpgradeWindows 个窗口都高于当前档位才升档，升到这几个窗口里最差的那一档；
 *    - 中途出现与当前档位相同、或方向相反的窗口，连续计数从头开始。
 *  不加锁，由调用方串行化。
 */
class CallGradeSmoother
{
public:
    CallGradeSmoother();

    //从头开始（会话开始时调用）：回到「未评级」
    void Reset();

    /**
     *  输入一个窗口的档位，返回防抖后的档位。
     *  @param raw GradeForMetrics 的结果；传入 kCallGradeUnknown 时忽略，返回当前档位
     *  @return 防抖后的档位
     */
    int Feed(int raw);

    //当前档位；还没有输入过时为 kCallGradeUnknown
    int Current() const { return m_current; }

private:
    int m_current;        //当前档位；未评级时为 kCallGradeUnknown
    int m_pendingDir;     //正在累计的变化方向：-1 降档、+1 升档、0 没有在累计
    int m_pendingCount;   //该方向已连续的窗口数
    int m_pendingTarget;  //累计成立后要采用的档位：降档取这几个窗口里最好的，升档取最差的
};

/**
 *  两个方向取较差的一档。还没收到对方报告、或对方报告里是「未评级」时只看本端。
 *  @param mine 本端「我收对方」的档位（可为 kCallGradeUnknown）
 *  @param peer 对方报告里「对方收我」的档位（可为 kCallGradeUnknown）
 *  @return 较差的一档；两边都未评级时返回 kCallGradeUnknown
 */
int CombineGrades(int mine, int peer);

//降级提示的种类
enum CallDegradeHint
{
    call_hint_none = 0,   //不提示
    call_hint_mine = 1,   //本端收得差：「当前网络不佳，通话可能卡顿」
    call_hint_peer = 2,   //对方收得差、本端收得不差：「对方网络不佳」
};

//显示档位降到它及以下时提示（「较差」）
const int kDegradeHintGrade = call_grade_poor;

/**
 *  降级提示的触发：显示档位（两个方向较差的一档）从「一般」及以上（或未评级）降到「较差」及以下时提示一次；
 *  之后一直差就不再提示，回到「一般」及以上后才会再次提示。两个方向都差时按本端收得差措辞。
 *  不加锁，由调用方串行化。
 */
class CallDegradeNotifier
{
public:
    CallDegradeNotifier();

    //从头开始（会话开始时调用）
    void Reset();

    /**
     *  输入两个方向防抖后的档位，返回这一次要浮出的提示。
     *  @param mine 本端「我收对方」的档位（可为 kCallGradeUnknown）
     *  @param peer 对方报告里「对方收我」的档位（可为 kCallGradeUnknown）
     *  @return 取 CallDegradeHint
     */
    int Feed(int mine, int peer);

private:
    bool m_armed;   //是否允许提示：初始与显示档位回到「一般」及以上时为 true，提示一次后为 false
};

//提示文字（已经过 TR 翻译）；call_hint_none 返回空串
std::wstring DegradeHintText(int hint);

//信号图标的外观
struct CallSignalLook
{
    int      m_bars;       //点亮的格数，0~4；断流与未评级为 0（四格都画成灰色）
    COLORREF m_litColor;   //点亮的格子的颜色
    COLORREF m_dimColor;   //未点亮的格子的颜色

    CallSignalLook();
};

//信号图标的总格数
const int kSignalBarCount = 4;

/**
 *  档位到信号图标外观的映射：良好 4 格绿、一般 3 格绿、较差 2 格橙、很差 1 格红、断流与未评级 0 格（全灰）。
 *  @param grade 档位（可为 kCallGradeUnknown）
 */
CallSignalLook SignalLookForGrade(int grade);

//档位的名称（已经过 TR 翻译）：良好 / 一般 / 较差 / 很差 / 已断开 / 正在检测
std::wstring GradeName(int grade);

/**
 *  线路类型的名称（已经过 TR 翻译）。
 *  @param linkPath 取 FlamingoP2PApi.h 的 p2p_link_path：1 局域网直连、2 打洞直连；其余（3 或 0，走 imgserver 中转）为「服务器中转」
 */
std::wstring LinkPathName(int linkPath);

/**
 *  把千分比写成百分数文字：5 → "0.5%"、20 → "2%"、125 → "12.5%"。不翻译（数字与百分号各语言相同）。
 */
std::wstring FormatPermilleAsPercent(uint32_t permille);

//悬停详情需要的全部数据
struct CallNetView
{
    int      m_grade;              //显示档位（CombineGrades 的结果，可为 kCallGradeUnknown）
    int      m_linkPath;           //线路类型，取值同 LinkPathName
    bool     m_hasRtt;             //是否已有往返时延
    uint32_t m_rttMs;              //往返时延（毫秒）
    bool     m_hasMine;            //「我收对方」是否已有数据（接通后的第一个窗口结算之前没有）
    uint32_t m_mineLossPermille;   //我收对方：丢包率（千分比）
    uint32_t m_mineJitterMs;       //我收对方：抖动（毫秒）
    uint32_t m_mineStallPermille;  //我收对方：卡顿率（千分比）
    uint32_t m_mineBufferMs;       //我收对方：播放缓冲延迟（毫秒，B4）
    bool     m_hasPeer;            //「对方收我」是否已有数据（收到过对方的报告）
    uint32_t m_peerLossPermille;   //对方收我：丢包率（千分比）
    uint32_t m_peerJitterMs;       //对方收我：抖动（毫秒）
    uint32_t m_peerStallPermille;  //对方收我：卡顿率（千分比）
    uint32_t m_peerBufferMs;       //对方收我：对方的播放缓冲延迟（毫秒，B4）

    CallNetView();
};

/**
 *  悬停详情的各行文字（已经过 TR 翻译），依次为：网络状况、线路、往返时延、我收对方、对方收我。
 *  调用方按显示方式自行拼接（单行提示用分隔符连起来，多行面板逐行放）。
 */
std::vector<std::wstring> NetDetailLines(const CallNetView& v);

/**
 *  通话网络状况的快照（第 2 期起放在 CallMediaSession.h，第 3 期移到本文件并补上本端档位与最近一个接收窗口，
 *  使通话窗取数据时不必引入会话的头文件）。由 CallMediaSession::GetNetSnapshot 填写，通话窗每秒取一次。
 */
struct CallNetSnapshot
{
    bool              m_hasRtt;             //是否已有往返时延样本
    uint32_t          m_rttMs;              //最近一次的往返时延（毫秒）；m_hasRtt 为 false 时为 0
    uint64_t          m_peerReports;        //累计收到的对方质量报告份数
    CallQualityReport m_peerReport;         //最近一份对方报告（对方收本端的情况）；没收到过时为默认值
    int               m_myGrade;            //本端「我收对方」防抖后的档位（第 3 期）；还没结算过接收窗口时为 kCallGradeUnknown
    bool              m_hasMine;            //是否已结算过至少一个接收窗口（第 3 期）
    uint32_t          m_mineLossPermille;   //最近一个接收窗口的丢包率（千分比）
    uint32_t          m_mineJitterMs;       //最近一个接收窗口结束时的抖动（毫秒）
    uint32_t          m_mineStallPermille;  //最近一个接收窗口的卡顿率（千分比）；窗口内没有应收的音频包时为 0
                                            //（对方关了麦克风，播放端补静音不算卡顿）
    uint32_t          m_mineBufferMs;       //最近一个接收窗口的播放缓冲延迟（毫秒，B4）；窗口内没有正常播放时为 0

    CallNetSnapshot();
};

/**
 *  快照里对方报告的档位。
 *  @return 还没收到过对方报告时为 kCallGradeUnknown；否则为报告里的档位（对方尚未评级时同样是 kCallGradeUnknown）
 */
int PeerGradeOf(const CallNetSnapshot& s);

/**
 *  由快照与线路类型组装悬停详情所需的数据：显示档位取两个方向较差的一档，「对方收我」取最近一份对方报告。
 *  @param s        快照
 *  @param linkPath 线路类型，取值同 LinkPathName
 */
CallNetView MakeNetView(const CallNetSnapshot& s, int linkPath);

} // namespace call
