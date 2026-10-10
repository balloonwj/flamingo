/**
 *  屏幕录制的参数与规则，见 RecordPolicy.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "RecordPolicy.h"

#include <math.h>    // log10：录制条电平按分贝换算（第 24 期）
#include <stdio.h>
#include <string.h>
#include <wchar.h>   // _wcsicmp：按扩展名判断录音文件（第 24 期）
#include "../../Source/Common/I18n/I18n.h"   // TRF：界面文字的多语言查找

namespace screenrecord {

namespace {

//1080p 的像素数：码率按它换算
const int64_t kPixels1080p = 1920LL * 1080;
//画中画的宽高比 16:9
const int kPipAspectW = 16;
const int kPipAspectH = 9;
//时长换算
const int64_t kMsPerSecond = 1000;
const int64_t kSecondsPerMinute = 60;
const int64_t kSecondsPerHour = 3600;
//文件名缓冲区的容量（字符）
const int kNameChars = 96;

int evenDown(int v)
{
    return v & ~1;
}

} // namespace

void ComputeRecordSize(int srcW, int srcH, int& outW, int& outH)
{
    ComputeRecordSize(srcW, srcH, record_quality_standard, outW, outH);
}

int RecordMaxLongSide(int quality)
{
    return (quality == record_quality_high) ? kRecordHighMaxLongSide : kRecordMaxLongSide;
}

void ComputeRecordSize(int srcW, int srcH, int quality, int& outW, int& outH)
{
    if (srcW <= 0 || srcH <= 0)
    {
        outW = kRecordMinSide;
        outH = kRecordMinSide;
        return;
    }
    const int64_t maxLong = RecordMaxLongSide(quality);
    int64_t w = srcW;
    int64_t h = srcH;
    const int64_t longSide = w > h ? w : h;
    if (longSide > maxLong)
    {
        //等比缩小，长边正好是上限
        if (w >= h)
        {
            h = h * maxLong / w;
            w = maxLong;
        }
        else
        {
            w = w * maxLong / h;
            h = maxLong;
        }
    }
    outW = evenDown(static_cast<int>(w));
    outH = evenDown(static_cast<int>(h));
    if (outW < kRecordMinSide)
        outW = kRecordMinSide;
    if (outH < kRecordMinSide)
        outH = kRecordMinSide;
}

int ClampRecordFps(int fps)
{
    if (fps < 1)
        return 1;
    if (fps > kRecordMaxFps)
        return kRecordMaxFps;
    return fps;
}

uint32_t RecordVideoBitrate(int quality, int width, int height)
{
    const uint32_t base = (quality == record_quality_high) ? kRecordHighBitrate1080p : kRecordStandardBitrate1080p;
    const int64_t pixels = static_cast<int64_t>(width) * height;
    const int64_t rate = static_cast<int64_t>(base) * pixels / kPixels1080p;
    if (rate < static_cast<int64_t>(kRecordMinBitrate))
        return kRecordMinBitrate;
    return static_cast<uint32_t>(rate);
}

std::wstring MakeRecordFileName(const SYSTEMTIME& t, int seq, bool audioOnly)
{
    //只录声音（第 24 期）用 .m4a
    const wchar_t* ext = audioOnly ? kRecordAudioExt : L".mp4";
    wchar_t buf[kNameChars] = { 0 };
    if (seq > 0)
    {
        _snwprintf_s(buf, kNameChars, _TRUNCATE, L"FlamingoRecord_%04u%02u%02u_%02u%02u%02u_%d%s", t.wYear, t.wMonth,
                     t.wDay, t.wHour, t.wMinute, t.wSecond, seq, ext);
    }
    else
    {
        _snwprintf_s(buf, kNameChars, _TRUNCATE, L"FlamingoRecord_%04u%02u%02u_%02u%02u%02u%s", t.wYear, t.wMonth, t.wDay,
                     t.wHour, t.wMinute, t.wSecond, ext);
    }
    return std::wstring(buf);
}

bool IsAudioRecordingPath(const std::wstring& path)
{
    const size_t extLen = wcslen(kRecordAudioExt);
    //只有扩展名、没有文件名的不算
    if (path.size() <= extLen)
        return false;
    return _wcsicmp(path.c_str() + path.size() - extLen, kRecordAudioExt) == 0;
}

int RecordLevelPercent(int peak)
{
    //16 位采样的满幅值
    const double kFullScale = 32767.0;
    //电平的上限（百分比）
    const double kPercentMax = 100.0;
    //振幅比换算成分贝的系数：dB = 20·log10(振幅比)
    const double kAmplitudeDbFactor = 20.0;
    //四舍五入时加的半个单位
    const double kRoundHalf = 0.5;
    const int magnitude = peak < 0 ? -peak : peak;
    if (magnitude <= 0)
        return 0;
    const double db = kAmplitudeDbFactor * log10(static_cast<double>(magnitude) / kFullScale);
    const double floorDb = static_cast<double>(kRecordLevelFloorDb);
    if (db <= floorDb)
        return 0;
    const double percent = (db - floorDb) / -floorDb * kPercentMax;
    if (percent >= kPercentMax)
        return static_cast<int>(kPercentMax);
    return static_cast<int>(percent + kRoundHalf);
}

std::wstring MakeDefaultRecordFolder(const std::wstring& videosDir)
{
    if (videosDir.empty())
        return std::wstring();
    std::wstring dir = videosDir;
    //末尾已有分隔符时不再多加一个
    const wchar_t last = dir[dir.size() - 1];
    if (last != L'\\' && last != L'/')
        dir += L'\\';
    return dir + kRecordDefaultSubDir;
}

RECT ComputePipRect(int frameW, int frameH)
{
    const int margin = frameW / kRecordPipMarginDivisor;
    const int w = evenDown(frameW / kRecordPipWidthDivisor);
    const int h = evenDown(w * kPipAspectH / kPipAspectW);
    RECT rc;
    rc.right = frameW - margin;
    rc.bottom = frameH - margin;
    rc.left = rc.right - w;
    rc.top = rc.bottom - h;
    if (rc.left < 0)
        rc.left = 0;
    if (rc.top < 0)
        rc.top = 0;
    return rc;
}

RecordPipPlacement::RecordPipPlacement()
    : m_custom(false)
    , m_x(0)
    , m_y(0)
    , m_w(0)
    , m_h(0)
{
}

RecordPipPlacement MapPipWindowToContent(const RECT& camScreen, const RECT& recorded)
{
    RecordPipPlacement p;
    const double rw = static_cast<double>(recorded.right - recorded.left);
    const double rh = static_cast<double>(recorded.bottom - recorded.top);
    const double cw = static_cast<double>(camScreen.right - camScreen.left);
    const double ch = static_cast<double>(camScreen.bottom - camScreen.top);
    if (rw <= 0 || rh <= 0 || cw <= 0 || ch <= 0)
        return p;
    p.m_custom = true;
    //大小按比例，比被录内容还大时缩到同宽或同高
    p.m_w = cw / rw > 1.0 ? 1.0 : cw / rw;
    p.m_h = ch / rh > 1.0 ? 1.0 : ch / rh;
    //位置按比例，有一部分在被录内容以外时贴到最近的边上
    double x = (camScreen.left - recorded.left) / rw;
    double y = (camScreen.top - recorded.top) / rh;
    if (x > 1.0 - p.m_w)
        x = 1.0 - p.m_w;
    if (y > 1.0 - p.m_h)
        y = 1.0 - p.m_h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    p.m_x = x;
    p.m_y = y;
    return p;
}

RECT DefaultPipWindowRect(const RECT& recorded)
{
    RECT rc = { 0, 0, 0, 0 };
    const int rw = recorded.right - recorded.left;
    const int rh = recorded.bottom - recorded.top;
    if (rw <= 0 || rh <= 0)
        return rc;
    //与 ComputePipRect 在录像里的摆法一致：右下角留边距，宽为五分之一，16:9
    const int margin = rw / kRecordPipMarginDivisor;
    const int w = rw / kRecordPipWidthDivisor;
    const int h = w * kPipAspectH / kPipAspectW;
    rc.right = recorded.right - margin;
    rc.bottom = recorded.bottom - margin;
    rc.left = rc.right - w;
    rc.top = rc.bottom - h;
    if (rc.top < recorded.top)
        rc.top = recorded.top;
    return rc;
}

RECT PlacePipRect(const RECT& content, const RecordPipPlacement& placement, int frameW, int frameH)
{
    const int cw = content.right - content.left;
    const int ch = content.bottom - content.top;
    if (!placement.m_custom || cw <= 0 || ch <= 0)
        return ComputePipRect(frameW, frameH);
    RECT rc;
    rc.left = content.left + static_cast<LONG>(placement.m_x * cw);
    rc.top = content.top + static_cast<LONG>(placement.m_y * ch);
    rc.right = rc.left + static_cast<LONG>(placement.m_w * cw);
    rc.bottom = rc.top + static_cast<LONG>(placement.m_h * ch);
    return rc;
}

RECT NormalizeSelection(POINT a, POINT b, const RECT& bounds)
{
    RECT rc;
    rc.left = a.x < b.x ? a.x : b.x;
    rc.right = a.x < b.x ? b.x : a.x;
    rc.top = a.y < b.y ? a.y : b.y;
    rc.bottom = a.y < b.y ? b.y : a.y;
    RECT out;
    if (!::IntersectRect(&out, &rc, &bounds))
    {
        RECT empty = { 0, 0, 0, 0 };
        return empty;
    }
    return out;
}

bool IsSelectionUsable(const RECT& rc)
{
    return (rc.right - rc.left) >= kRecordMinSide && (rc.bottom - rc.top) >= kRecordMinSide;
}

bool RecordCanStart(uint64_t freeBytes)
{
    return freeBytes >= kRecordMinFreeToStart;
}

bool RecordCanContinue(uint64_t freeBytes)
{
    return freeBytes >= kRecordMinFreeToContinue;
}

std::wstring FormatRecordDuration(int64_t ms)
{
    if (ms < 0)
        ms = 0;
    const int64_t total = ms / kMsPerSecond;
    const int64_t h = total / kSecondsPerHour;
    const int64_t m = (total % kSecondsPerHour) / kSecondsPerMinute;
    const int64_t s = total % kSecondsPerMinute;
    wchar_t buf[kNameChars] = { 0 };
    if (h > 0)
        _snwprintf_s(buf, kNameChars, _TRUNCATE, L"%lld:%02lld:%02lld", h, m, s);
    else
        _snwprintf_s(buf, kNameChars, _TRUNCATE, L"%02lld:%02lld", m, s);
    return std::wstring(buf);
}

std::wstring RecordFileNameOf(const std::wstring& path)
{
    const std::wstring::size_type pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return path;
    return path.substr(pos + 1);
}

std::wstring FormatRecordDoneDetail(int64_t durationMs, uint64_t bytes)
{
    //1 MB 的字节数
    const double kBytesPerMb = 1024.0 * 1024.0;
    wchar_t size[kNameChars] = { 0 };
    _snwprintf_s(size, kNameChars, _TRUNCATE, L"%.1f MB", static_cast<double>(bytes) / kBytesPerMb);
    //{0} 为时长，{1} 为文件大小
    return TRF(L"时长 {0} · {1}", FormatRecordDuration(durationMs), size);
}

} // namespace screenrecord
