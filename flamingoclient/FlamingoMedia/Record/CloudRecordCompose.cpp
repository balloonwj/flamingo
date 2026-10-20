/**
 *  云端录像离线合成的纯逻辑部分，见 CloudRecordCompose.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordCompose.h"
#include "RecordPolicy.h"   //RecordVideoBitrate：合成码率按录制画质换算
#include "../Share/ShareAnnotation.h"   // AnnotationStroke、annot_tool_laser：按时间变化的笔迹（2026-10-08）
#include "../Share/ShareAspectFit.h"    // FitRectKeepAspect：CloudFitRect 转调的等比放置
#include "../Share/ShareFrameDrawer.h"  // ShareUsesFastDownscale / BgraDownscaler：何时走快速缩放、怎样缩，与观看窗共用（2026-10-15）

#include <math.h>
#include <string.h>   // memcpy：CloudCopyFrameRows
#include <algorithm>

namespace screenrecord {

namespace {

//每秒的毫秒数，也是每毫秒的微秒数
const int64_t kMsPerSecond = 1000;
//每毫秒的样点数（48 kHz）
const int64_t kSamplesPerMs = kCloudAudioRate / kMsPerSecond;
//微秒到毫秒
const int64_t kUsPerMs = kMsPerSecond;
//16 位 PCM 的上下限
const int32_t kPcmMax = 32767;
const int32_t kPcmMin = -32768;

} // namespace

CloudComposeTimeline::CloudComposeTimeline()
    : m_pausedTotal(0)
    , m_pauseStart(-1)
    , m_lastOut(0)
{
}

void CloudComposeTimeline::OnPause(int64_t relMs)
{
    if (m_pauseStart < 0)
        m_pauseStart = relMs;
}

void CloudComposeTimeline::OnResume(int64_t relMs)
{
    if (m_pauseStart < 0)
        return;
    if (relMs > m_pauseStart)
        m_pausedTotal += relMs - m_pauseStart;
    m_pauseStart = -1;
}

bool CloudComposeTimeline::Paused() const
{
    return m_pauseStart >= 0;
}

int64_t CloudComposeTimeline::OutMs(int64_t relMs)
{
    const int64_t effective = (m_pauseStart >= 0 && relMs > m_pauseStart) ? m_pauseStart : relMs;
    int64_t out = effective - m_pausedTotal;
    if (out < m_lastOut)
        out = m_lastOut;
    m_lastOut = out;
    return out;
}

CloudAudioPlacer::Anchor::Anchor()
    : m_anchorTsUs(0)
    , m_anchorMs(0)
{
}

int64_t CloudAudioPlacer::Place(int streamId, uint64_t senderTsUs, int64_t arrivalMs)
{
    std::map<int, Anchor>::iterator it = m_anchors.find(streamId);
    bool reanchor = (it == m_anchors.end());
    int64_t placedMs = arrivalMs;
    if (!reanchor)
    {
        //时间戳倒退（发送端重新开始采集）或与到达时刻偏离太大：重新定锚
        if (senderTsUs < it->second.m_anchorTsUs)
        {
            reanchor = true;
        }
        else
        {
            placedMs = it->second.m_anchorMs + static_cast<int64_t>((senderTsUs - it->second.m_anchorTsUs) / kUsPerMs);
            const int64_t drift = placedMs - arrivalMs;
            if (drift > kCloudReanchorMs || drift < -kCloudReanchorMs)
                reanchor = true;
        }
    }
    if (reanchor)
    {
        Anchor a;
        a.m_anchorTsUs = senderTsUs;
        a.m_anchorMs = arrivalMs;
        m_anchors[streamId] = a;
        placedMs = arrivalMs;
    }
    return placedMs > 0 ? placedMs * kSamplesPerMs : 0;
}

CloudAudioMixBuffer::CloudAudioMixBuffer()
    : m_base(0)
{
}

void CloudAudioMixBuffer::Add(int64_t startSample, const int16_t* pcm, size_t count)
{
    if (pcm == NULL || count == 0)
        return;
    size_t skip = 0;
    if (startSample < m_base)
    {
        const int64_t late = m_base - startSample;
        if (late >= static_cast<int64_t>(count))
            return;
        skip = static_cast<size_t>(late);
        startSample = m_base;
    }
    const size_t offset = static_cast<size_t>(startSample - m_base);
    const size_t need = offset + (count - skip);
    if (m_acc.size() < need)
        m_acc.resize(need, 0);
    for (size_t i = skip; i < count; ++i)
        m_acc[offset + (i - skip)] += pcm[i];
}

void CloudAudioMixBuffer::Take(size_t count, std::vector<int16_t>& out)
{
    out.assign(count, 0);
    //写成 (std::min)：RecordPolicy.h 带进的 <windows.h> 定义了 min 宏
    const size_t avail = (std::min)(count, m_acc.size());
    for (size_t i = 0; i < avail; ++i)
    {
        int32_t v = m_acc[i];
        if (v > kPcmMax)
            v = kPcmMax;
        if (v < kPcmMin)
            v = kPcmMin;
        out[i] = static_cast<int16_t>(v);
    }
    m_acc.erase(m_acc.begin(), m_acc.begin() + static_cast<std::ptrdiff_t>(avail));
    m_base += static_cast<int64_t>(count);
}

int64_t CloudAudioMixBuffer::Position() const
{
    return m_base;
}

int ChooseCloudPipSeat(const std::set<int>& speakingSeats, const std::set<int>& cameraSeats, int prevSeat)
{
    for (std::set<int>::const_iterator it = speakingSeats.begin(); it != speakingSeats.end(); ++it)
    {
        if (cameraSeats.count(*it) > 0)
            return *it;
    }
    if (prevSeat >= 0 && cameraSeats.count(prevSeat) > 0)
        return prevSeat;
    return -1;
}

std::vector<int> CloudVisibleScreenSeats(const std::map<int, int64_t>& lastFrameMs, int64_t nowMs)
{
    std::vector<int> seats;
    //std::map 按座位升序，发起人（0）自然排在最前
    for (std::map<int, int64_t>::const_iterator it = lastFrameMs.begin(); it != lastFrameMs.end(); ++it)
    {
        if (nowMs - it->second <= kCloudScreenStaleMs)
            seats.push_back(it->first);
    }
    return seats;
}

void CloudNoteBoardView(int boardSeat, int64_t outMs, std::map<int, int64_t>& lastFrameMs, std::set<int>& boards)
{
    lastFrameMs[boardSeat] = outMs;
    boards.insert(boardSeat);
}

void CloudBoardImageSize(int tileW, int tileH, int& w, int& h)
{
    w = 0;
    h = 0;
    if (tileW <= 0 || tileH <= 0)
        return;
    //先按宽放，高放不下再按高放
    w = tileW;
    h = static_cast<int>(static_cast<int64_t>(tileW) * kCloudBoardAspectH / kCloudBoardAspectW);
    if (h > tileH)
    {
        h = tileH;
        w = static_cast<int>(static_cast<int64_t>(tileH) * kCloudBoardAspectW / kCloudBoardAspectH);
    }
}

int CloudPcmRms(const int16_t* pcm, size_t count)
{
    if (pcm == NULL || count == 0)
        return 0;
    double sum = 0;
    for (size_t i = 0; i < count; ++i)
        sum += static_cast<double>(pcm[i]) * pcm[i];
    return static_cast<int>(sqrt(sum / static_cast<double>(count)));
}

uint32_t CloudComposeVideoBitrate()
{
    return RecordVideoBitrate(record_quality_standard, kCloudComposeWidth, kCloudComposeHeight);
}

RECT CloudFitRect(int srcW, int srcH, const RECT& cell)
{
    //画面比格子扁：宽撑满、上下留黑；否则高撑满、左右留黑。与远程桌面、屏幕共享观看窗共用一份（2026-10-14）
    return screenshare::FitRectKeepAspect(cell, srcW, srcH);
}

bool CloudFitIsOneToOne(int srcW, int srcH, const RECT& dst)
{
    return srcW > 0 && srcH > 0 && dst.right - dst.left == srcW && dst.bottom - dst.top == srcH;
}

void CloudCopyFrameRows(uint8_t* dst, int dstW, int dstH, const uint8_t* src, int srcW, int srcH, int left, int top)
{
    if (dst == NULL || src == NULL || dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0)
        return;
    const int bpp = 4;
    //只复制落在画布里的那部分
    const int x0 = (left < 0) ? -left : 0;
    const int x1 = (left + srcW > dstW) ? dstW - left : srcW;
    if (x1 <= x0)
        return;
    for (int y = 0; y < srcH; ++y)
    {
        const int dy = top + y;
        if (dy < 0 || dy >= dstH)
            continue;
        memcpy(dst + (static_cast<size_t>(dy) * dstW + left + x0) * bpp,
               src + (static_cast<size_t>(y) * srcW + x0) * bpp, static_cast<size_t>(x1 - x0) * bpp);
    }
}

bool CloudFitUsesFastScale(int srcW, int srcH, const RECT& dst)
{
    //与观看窗的绘制器共用一条规则：缩小且两个方向都不小于一半
    return screenshare::ShareUsesFastDownscale(srcW, srcH, dst.right - dst.left, dst.bottom - dst.top);
}

bool CloudScaleFrameInto(screenshare::BgraDownscaler& scaler, uint8_t* canvas, int canvasW, int canvasH, const uint8_t* src,
                         int srcW, int srcH, const RECT& dst)
{
    const int bpp = 4;
    const int dstW = dst.right - dst.left;
    const int dstH = dst.bottom - dst.top;
    if (canvas == NULL || src == NULL || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        return false;
    //只处理完全落在画布内的矩形；越界的交给 GDI 裁剪
    if (dst.left < 0 || dst.top < 0 || dst.right > canvasW || dst.bottom > canvasH)
        return false;
    uint8_t* origin = canvas + (static_cast<size_t>(dst.top) * canvasW + dst.left) * bpp;
    scaler.Scale(src, srcW * bpp, srcW, srcH, origin, canvasW * bpp, dstW, dstH);
    return true;
}

CloudCanvasKey::CloudCanvasKey()
    : m_annotVersion(0)
    , m_seatVersion(0)
{
}

bool CloudCanvasKey::operator==(const CloudCanvasKey& rhs) const
{
    return m_seats == rhs.m_seats && m_serials == rhs.m_serials && m_annotVersion == rhs.m_annotVersion &&
           m_seatVersion == rhs.m_seatVersion;
}

bool CloudCanvasKey::operator!=(const CloudCanvasKey& rhs) const
{
    return !(*this == rhs);
}

bool CloudHasTimedStrokes(const std::vector<screenshare::AnnotationStroke>& strokes)
{
    for (size_t i = 0; i < strokes.size(); ++i)
    {
        if (strokes[i].m_tool == screenshare::annot_tool_laser)
            return true;
    }
    return false;
}

bool CloudCanvasReusable(bool haveCanvas, bool timed, const CloudCanvasKey& last, const CloudCanvasKey& now)
{
    return haveCanvas && !timed && last == now;
}

} // namespace screenrecord
