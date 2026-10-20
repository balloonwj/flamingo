/**
 *  云端录像的离线合成，见 CloudRecordComposer.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordComposer.h"

#include <string.h>
#include <chrono>      // steady_clock：各阶段的累计耗时（2026-10-07）
#include <objbase.h>
#include <mfapi.h>
#include <memory>
#include <set>
#include "CloudRecordCompose.h"
#include "CloudRecordReader.h"
#include "RecordFrameComposer.h"
#include "RecordMp4Writer.h"
#include "RecordPolicy.h"
#include "../Call/AudioPcmUtils.h"
#include "../Call/MfH264Codec.h"
#include "../Call/OpusAudioCodec.h"
#include "../Call/VideoFrameConvert.h"
#include "../Share/ShareAnnotation.h"
#include "../Share/ShareAnnotationRender.h"
#include "../Share/ShareFraming.h"
#include "../Share/ShareImageExport.h"    // RenderBoardImage：云端录像里的白板（2026-10-07）
#include "../Share/ShareScreenDecoders.h"
#include "../Share/ShareScreens.h"
#include "../Share/ShareFrameDrawer.h"    // BgraDownscaler：2K、4K 画面缩小进画布（2026-10-15）
#include "../Share/ShareWhiteboard.h"     // wb_bg_grid：白板没有收到视野时的默认背景
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"

namespace screenrecord {

namespace {

//100 纳秒为单位的时间（MF 的时间戳）每毫秒的个数
const int64_t kHnsPerMs = 10000;
//每秒的毫秒数
const int64_t kMsPerSecond = 1000;
//每毫秒的样点数（48 kHz）
const int64_t kSamplesPerMs = kCloudAudioRate / kMsPerSecond;
//声音比到达时刻晚写多少毫秒：按发送端时间戳排位的包可能比到达时刻稍早落位，留出余量再取走
const int64_t kAudioWriteLagMs = 500;
//Opus 解码器初始化要的码率参数（只解码，取一个常用值即可）
const int kOpusDecodeBitrate = 32000;
//多画面之间的间隙（像素），与观看窗一致
const int kTileGapPx = 4;
//名字标签：离画面左上角的边距、文字两侧与上下的留白（像素），底色与文字色与观看窗一致
const int kLabelMargin = 6;
const int kLabelPadX = 8;
const int kLabelPadY = 3;
const COLORREF kLabelBg = RGB(30, 32, 38);
const COLORREF kLabelText = RGB(240, 240, 240);
//名字标签的字号（像素）
const int kLabelFontPx = 22;
//摄像头解码器的初始尺寸（真实尺寸以码流为准）
const int kCameraInitW = 640;
const int kCameraInitH = 360;
//每轨道一路声音的编号：座位 × 8 + 轨道（与 ShareAudioSession::StreamIdOf 一致）
const int kStreamsPerSeat = 8;
//进度上限
const int kProgressMax = 100;
//32 位 BGRA 像素里 alpha 字节全为 1 的掩码（画布补不透明度用）
const uint32_t kOpaqueAlphaMask = 0xFF000000u;

//一块白板最近画好的图（2026-10-07，方案第 14.1 节 ④-2）：标注版本号、尺寸、背景都没变时复用，避免每帧重画
struct BoardImage
{
    std::vector<uint8_t> m_bgra;        //自上而下的 BGRA，不透明
    int                  m_width;       //宽（像素）
    int                  m_height;      //高（像素）
    int                  m_background;  //背景，取值见 WhiteboardBackground
    uint64_t             m_version;     //画这张图时标注模型的版本号

    BoardImage()
        : m_width(0)
        , m_height(0)
        , m_background(-1)
        , m_version(0)
    {
    }
};

//摄像头解出的最新一帧
struct CameraFrame
{
    std::vector<uint8_t> m_bgra;    //自上而下的 BGRA
    int                  m_width;   //宽
    int                  m_height;  //高

    CameraFrame()
        : m_width(0)
        , m_height(0)
    {
    }
};

//摄像头解码输出：NV12 转成 BGRA 存进 CameraFrame
class CameraSink : public call::MfH264DecoderSink
{
public:
    explicit CameraSink(CameraFrame& frame)
        : m_frame(frame)
        , m_cropW(0)
        , m_cropH(0)
    {
    }

    //发送端给的真实尺寸（解码器输出的高会补齐到 16 的倍数）
    void SetCrop(int w, int h)
    {
        m_cropW = w;
        m_cropH = h;
    }

    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override
    {
        (void)ptsHns;
        std::vector<uint8_t> full(static_cast<size_t>(width) * height * 4);
        call::Nv12ToBgra(nv12, width, height, stride, &full[0], width * 4);
        const int w = (m_cropW > 0 && m_cropW <= width) ? m_cropW : width;
        const int h = (m_cropH > 0 && m_cropH <= height) ? m_cropH : height;
        m_frame.m_width = w;
        m_frame.m_height = h;
        m_frame.m_bgra.resize(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            memcpy(&m_frame.m_bgra[static_cast<size_t>(y) * w * 4], &full[static_cast<size_t>(y) * width * 4],
                   static_cast<size_t>(w) * 4);
    }

private:
    CameraFrame& m_frame;   //写入的目标（不拥有）
    int          m_cropW;   //真实宽
    int          m_cropH;   //真实高
};

//一路摄像头：解码器与最新一帧
struct CameraStream
{
    call::MfH264Decoder m_decoder;   //解码器
    CameraFrame         m_frame;     //最新一帧
    CameraSink          m_sink;      //解码输出
    int64_t             m_lastMs;    //最近一帧的输出毫秒

    CameraStream()
        : m_sink(m_frame)
        , m_lastMs(-1)
    {
    }
};

//画布：一块 DIB 内存设备上下文
class Canvas
{
public:
    Canvas()
        : m_dc(NULL)
        , m_bitmap(NULL)
        , m_old(NULL)
        , m_bits(NULL)
        , m_font(NULL)
        , m_width(0)
        , m_height(0)
    {
    }

    ~Canvas()
    {
        if (m_dc != NULL)
        {
            ::SelectObject(m_dc, m_old);
            ::DeleteDC(m_dc);
        }
        if (m_bitmap != NULL)
            ::DeleteObject(m_bitmap);
        if (m_font != NULL)
            ::DeleteObject(m_font);
    }

    bool Init(int width, int height, const std::wstring& fontName)
    {
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        m_dc = ::CreateCompatibleDC(NULL);
        void* bits = NULL;
        m_bitmap = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (m_dc == NULL || m_bitmap == NULL)
            return false;
        m_bits = static_cast<uint8_t*>(bits);
        m_old = ::SelectObject(m_dc, m_bitmap);
        m_width = width;
        m_height = height;
        m_font = ::CreateFontW(-kLabelFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, fontName.c_str());
        ::SetStretchBltMode(m_dc, HALFTONE);
        return true;
    }

    HDC Dc() const
    {
        return m_dc;
    }

    uint8_t* Bits() const
    {
        return m_bits;
    }

    int Stride() const
    {
        return m_width * 4;
    }

    void Clear()
    {
        memset(m_bits, 0, static_cast<size_t>(m_width) * m_height * 4);
    }

    //按比例缩放画进 cell，四周留黑；返回画面实际所占的矩形
    RECT DrawFit(const uint8_t* bgra, int w, int h, const RECT& cell)
    {
        const RECT dst = CloudFitRect(w, h, cell);
        if (w <= 0 || h <= 0 || dst.right <= dst.left || dst.bottom <= dst.top)
            return dst;
        //原尺寸（最常见：一个人共享 1920×1080 的整屏）：直接逐行复制，不经 GDI 缩放（2026-10-08：HALFTONE 模式
        //的 StretchDIBits 在 1:1 时也按缩放逐像素处理，每帧要二十多毫秒）。先把之前画名字标签等还没执行的 GDI 操作做完
        if (CloudFitIsOneToOne(w, h, dst))
        {
            ::GdiFlush();
            CloudCopyFrameRows(m_bits, m_width, m_height, bgra, w, h, dst.left, dst.top);
            return dst;
        }
        //缩小（2K、4K 画面，或多人共享时缩进小格子）：改用 BgraDownscaler 直接写进画布（2026-10-15，方案 A7）。HALFTONE 的
        //StretchDIBits 在 2560×1440、3840×2160 缩到 1080p 时每帧约 32、49 毫秒，4K 录像合成得比实时还慢（CCB2 实测）
        if (CloudFitUsesFastScale(w, h, dst))
        {
            ::GdiFlush();
            if (CloudScaleFrameInto(m_scaler, m_bits, m_width, m_height, bgra, w, h, dst))
                return dst;
        }
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        ::StretchDIBits(m_dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top, 0, 0, w, h, bgra, &bi,
                        DIB_RGB_COLORS, SRCCOPY);
        return dst;
    }

    //在 cell 左上角画名字标签
    void DrawLabel(const std::wstring& text, const RECT& cell)
    {
        if (text.empty())
            return;
        HGDIOBJ oldFont = ::SelectObject(m_dc, m_font != NULL ? m_font : ::GetStockObject(DEFAULT_GUI_FONT));
        SIZE size = { 0, 0 };
        ::GetTextExtentPoint32W(m_dc, text.c_str(), static_cast<int>(text.size()), &size);
        RECT bg;
        bg.left = cell.left + kLabelMargin;
        bg.top = cell.top + kLabelMargin;
        bg.right = bg.left + size.cx + kLabelPadX * 2;
        bg.bottom = bg.top + size.cy + kLabelPadY * 2;
        HBRUSH brush = ::CreateSolidBrush(kLabelBg);
        ::FillRect(m_dc, &bg, brush);
        ::DeleteObject(brush);
        ::SetBkMode(m_dc, TRANSPARENT);
        ::SetTextColor(m_dc, kLabelText);
        ::TextOutW(m_dc, bg.left + kLabelPadX, bg.top + kLabelPadY, text.c_str(), static_cast<int>(text.size()));
        ::SelectObject(m_dc, oldFont);
    }

    //GDI 画完之后把 alpha 通道置为不透明（编码器按 RGB32 读，不受影响，这里保证读回一致）。
    //按 32 位整体置位，编译器可以向量化
    void Opaque()
    {
        const size_t n = static_cast<size_t>(m_width) * m_height;
        uint32_t* px = reinterpret_cast<uint32_t*>(m_bits);
        for (size_t i = 0; i < n; ++i)
            px[i] |= kOpaqueAlphaMask;
    }

private:
    HDC      m_dc;       //内存设备上下文
    HBITMAP  m_bitmap;   //DIB
    HGDIOBJ  m_old;      //选入之前的位图
    uint8_t* m_bits;     //像素
    HFONT    m_font;     //名字标签的字体
    int      m_width;    //宽
    int      m_height;   //高
    screenshare::BgraDownscaler m_scaler;   //2K、4K 画面缩小进画布时用的快速缩放（2026-10-15），列映射表与中间缓冲按尺寸复用
};

} // namespace

CloudComposeStageTimes::CloudComposeStageTimes()
    : m_decodeMs(0.0)
    , m_drawMs(0.0)
    , m_composeMs(0.0)
    , m_videoWriteMs(0.0)
    , m_audioMs(0.0)
    , m_videoFrames(0)
    , m_reusedFrames(0)
    , m_directFrames(0)
{
}

namespace {

//从 since 到现在的毫秒数（各阶段计时用）
double msSince(const std::chrono::steady_clock::time_point& since)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

CloudComposeStageTimes CCloudRecordComposeTask::StageTimes() const
{
    return m_stageTimes;
}

CCloudRecordComposeTask::CCloudRecordComposeTask(int64_t recordId, const std::wstring& frecPath,
                                                 const std::wstring& mp4Path, HWND notifyWnd, UINT msgNames,
                                                 UINT msgDone, const std::wstring& labelFont)
    : m_recordId(recordId)
    , m_frecPath(frecPath)
    , m_mp4Path(mp4Path)
    , m_notifyWnd(notifyWnd)
    , m_msgNames(msgNames)
    , m_msgDone(msgDone)
    , m_labelFont(labelFont)
    , m_cancel(false)
    , m_progress(0)
    , m_namesReady(false)
{
}

CCloudRecordComposeTask::~CCloudRecordComposeTask()
{
    Cancel();
    Join();
}

void CCloudRecordComposeTask::Start()
{
    m_thread = std::thread(&CCloudRecordComposeTask::threadProc, this);
}

void CCloudRecordComposeTask::Cancel()
{
    m_cancel = true;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cv.notify_all();
}

void CCloudRecordComposeTask::Join()
{
    if (m_thread.joinable())
        m_thread.join();
}

int64_t CCloudRecordComposeTask::RecordId() const
{
    return m_recordId;
}

const std::vector<int64_t>& CCloudRecordComposeTask::UserIds() const
{
    return m_userIds;
}

void CCloudRecordComposeTask::SetNames(const std::map<int64_t, std::wstring>& names)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_names = names;
    m_namesReady = true;
    m_cv.notify_all();
}

int CCloudRecordComposeTask::Progress() const
{
    return m_progress.load();
}

bool CCloudRecordComposeTask::waitNames()
{
    if (!::PostMessageW(m_notifyWnd, m_msgNames, 0, reinterpret_cast<LPARAM>(this)))
        return true;   //回报窗口已不在：没有名字也照样合成
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_namesReady && !m_cancel.load())
        m_cv.wait(lock);
    return !m_cancel.load();
}

void CCloudRecordComposeTask::threadProc()
{
    const HRESULT hrCom = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const HRESULT hrMf = ::MFStartup(MF_VERSION, MFSTARTUP_FULL);
    int result = kCloudComposeMfFail;
    if (SUCCEEDED(hrMf))
    {
        result = run();
        ::MFShutdown();
    }
    if (SUCCEEDED(hrCom))
        ::CoUninitialize();
    if (result != kCloudComposeOk)
        ::DeleteFileW(m_mp4Path.c_str());
    LOG_INFO("cloud compose: record %lld finished, result %d.", static_cast<long long>(m_recordId), result);
    ::PostMessageW(m_notifyWnd, m_msgDone, reinterpret_cast<WPARAM>(this), static_cast<LPARAM>(result));
}

int CCloudRecordComposeTask::run()
{
    //第一遍：只收集座位与 userid
    std::map<int, int64_t> seatUser;
    {
        CFrecReader reader;
        if (!reader.Open(m_frecPath))
            return kCloudComposeBadFile;
        FrecRecord rec;
        std::set<int64_t> users;
        while (reader.Next(rec))
        {
            if (m_cancel.load())
                return kCloudComposeCancelled;
            int seat = 0;
            int64_t userId = 0;
            if (rec.m_type == kFrecRecordSeat && DecodeFrecSeat(rec.m_payload, seat, userId) && userId > 0)
                users.insert(userId);
        }
        m_userIds.assign(users.begin(), users.end());
    }
    if (!waitNames())
        return kCloudComposeCancelled;

    //第二遍：解码、排时间轴、画面与声音写进 MP4
    CFrecReader reader;
    if (!reader.Open(m_frecPath))
        return kCloudComposeBadFile;
    const int width = kCloudComposeWidth;
    const int height = kCloudComposeHeight;
    RecordMp4Writer writer;
    if (!writer.Open(m_mp4Path, width, height, kCloudComposeFps, CloudComposeVideoBitrate()))
        return kCloudComposeWriteFail;
    Canvas canvas;
    RecordFrameComposer composer;
    if (!canvas.Init(width, height, m_labelFont) || !composer.Init(width, height))
        return kCloudComposeWriteFail;

    screenshare::ShareFrameReassembler reassembler;
    screenshare::ShareScreenDecoders screens;
    screenshare::ShareAnnotationModel annotations;
    std::map<int, std::unique_ptr<CameraStream> > cameras;
    std::map<int, std::unique_ptr<call::OpusAudioCodec> > opus;
    CloudComposeTimeline timeline;
    CloudAudioPlacer placer;
    CloudAudioMixBuffer mix;
    std::map<int, int64_t> lastScreenMs;
    std::set<int> boards;                         //是白板的画面座位（2026-10-07）
    std::map<int, BoardImage> boardImages;        //各块白板最近画好的图，笔迹与尺寸没变时复用
    std::map<int, int64_t> lastSpeakMs;
    int pipSeat = -1;
    //画布签名（2026-10-08）：画面、标注与座位都没变时这一帧沿用上一帧的画布，不再重画
    CloudCanvasKey lastCanvasKey;
    bool haveCanvas = false;
    uint64_t seatVersion = 0;
    const int64_t frameMs = kMsPerSecond / kCloudComposeFps;
    int64_t nextVideoMs = 0;
    int64_t lastOutMs = 0;
    std::vector<uint8_t> screenPixels;
    std::vector<int16_t> pcmOut;
    std::vector<int16_t> stereo(static_cast<size_t>(kCloudAudioFrameSamples) * 2);

    FrecRecord rec;
    bool writeOk = true;
    const uint64_t fileBytes = reader.FileBytes() > 0 ? reader.FileBytes() : 1;
    for (;;)
    {
        const bool more = reader.Next(rec);
        if (m_cancel.load())
            return kCloudComposeCancelled;
        int64_t outMs = lastOutMs;
        if (more)
        {
            if (rec.m_type == kFrecRecordPause)
            {
                timeline.OnPause(rec.m_relMs);
                continue;
            }
            if (rec.m_type == kFrecRecordResume)
            {
                timeline.OnResume(rec.m_relMs);
                continue;
            }
            outMs = timeline.OutMs(rec.m_relMs);
        }

        //把输出推进到这一条记录的时刻（读完时推进到最后一条记录处）：先写到点的画面帧，再写已经排好的声音
        //画一帧：所有还在共享的画面并列，标注画在各自画面上，名字标在左上角，说话的人的摄像头作画中画
        while (writeOk && nextVideoMs <= outMs)
        {
            const std::chrono::steady_clock::time_point drawStart = std::chrono::steady_clock::now();
            annotations.ExpireTransient(nextVideoMs);
            const std::vector<int> visible = CloudVisibleScreenSeats(lastScreenMs, nextVideoMs);
            //画布签名：各块画面的帧序号、标注版本、座位变化计数；都没变、又没有正在淡出的激光笔时沿用上一帧的画布
            CloudCanvasKey canvasKey;
            canvasKey.m_seats = visible;
            for (size_t i = 0; i < visible.size(); ++i)
            {
                uint64_t serial = 0;
                if (boards.count(visible[i]) == 0)
                    screens.LatestSerial(visible[i], serial);
                canvasKey.m_serials.push_back(serial);
            }
            canvasKey.m_annotVersion = annotations.Version();
            canvasKey.m_seatVersion = seatVersion;
            const bool reuseCanvas = CloudCanvasReusable(haveCanvas, CloudHasTimedStrokes(annotations.Strokes()),
                                                         lastCanvasKey, canvasKey);
            if (reuseCanvas)
                ++m_stageTimes.m_reusedFrames;
            if (!reuseCanvas)
            {
                canvas.Clear();
                RECT area = { 0, 0, width, height };
                const std::vector<RECT> tiles =
                    screenshare::ComputeScreenTiles(area, static_cast<int>(visible.size()), -1, kTileGapPx);
                for (size_t i = 0; i < visible.size() && i < tiles.size(); ++i)
                {
                    //白板（2026-10-07，方案第 14.1 节 ④-2）：没有视频帧，把整个白板世界画成图放进这一块；
                    //笔迹没变时复用上一次的图
                    if (boards.count(visible[i]) != 0)
                    {
                        BoardImage& img = boardImages[visible[i]];
                        int bw = 0;
                        int bh = 0;
                        CloudBoardImageSize(tiles[i].right - tiles[i].left, tiles[i].bottom - tiles[i].top, bw, bh);
                        int background = screenshare::wb_bg_grid;
                        screenshare::AnnotationBoardView view;
                        if (annotations.BoardView(visible[i], view))
                            background = view.m_background;
                        if (bw > 0 && bh > 0 &&
                            (img.m_version != annotations.Version() || img.m_width != bw || img.m_height != bh ||
                             img.m_background != background))
                        {
                            screenshare::RenderBoardImage(
                                bw, bh, background, screenshare::AnnotStrokesOnCanvas(annotations.Strokes(), visible[i]),
                                img.m_bgra);
                            img.m_version = annotations.Version();
                            img.m_width = bw;
                            img.m_height = bh;
                            img.m_background = background;
                        }
                        if (!img.m_bgra.empty())
                            canvas.DrawFit(&img.m_bgra[0], img.m_width, img.m_height, tiles[i]);
                    }
                    else
                    {
                        int w = 0;
                        int h = 0;
                        uint64_t serial = 0;
                        if (!screens.GetLatestFrame(visible[i], screenPixels, w, h, serial))
                            continue;
                        const RECT content = canvas.DrawFit(&screenPixels[0], w, h, tiles[i]);
                        const std::vector<screenshare::AnnotationStroke> strokes =
                            screenshare::AnnotStrokesOnCanvas(annotations.Strokes(), visible[i]);
                        if (!strokes.empty())
                            screenshare::DrawAnnotations(canvas.Dc(), content, strokes, NULL, false, nextVideoMs);
                    }
                    //只有一块画面时不标名字（与观看窗一致：单画面时标题栏已经写了发起人）
                    if (visible.size() > 1)
                    {
                        std::map<int, int64_t>::const_iterator su = seatUser.find(visible[i]);
                        if (su != seatUser.end())
                        {
                            std::map<int64_t, std::wstring>::const_iterator nm = m_names.find(su->second);
                            if (nm != m_names.end())
                                canvas.DrawLabel(nm->second, tiles[i]);
                        }
                    }
                }
                ::GdiFlush();
                canvas.Opaque();
            }
            lastCanvasKey = canvasKey;
            haveCanvas = true;
            m_stageTimes.m_drawMs += msSince(drawStart);
            //画中画：说话中、摄像头开着的人
            std::set<int> speaking;
            for (std::map<int, int64_t>::const_iterator it = lastSpeakMs.begin(); it != lastSpeakMs.end(); ++it)
            {
                if (nextVideoMs - it->second <= kCloudSpeakHoldMs)
                    speaking.insert(it->first);
            }
            std::set<int> cameraOn;
            for (std::map<int, std::unique_ptr<CameraStream> >::const_iterator it = cameras.begin(); it != cameras.end();
                 ++it)
            {
                if (it->second->m_lastMs >= 0 && nextVideoMs - it->second->m_lastMs <= kCloudCameraStaleMs &&
                    it->second->m_frame.m_width > 0)
                    cameraOn.insert(it->first);
            }
            pipSeat = ChooseCloudPipSeat(speaking, cameraOn, pipSeat);
            const CameraFrame* pip = (pipSeat >= 0) ? &cameras[pipSeat]->m_frame : NULL;
            //没有画中画（最常见）：画布就是要输出的画面，直接交给编码器（2026-10-08：合成器会把画布涂黑后原样
            //再贴一遍，每帧二十多毫秒）；有画中画时才经合成器叠上摄像头
            const uint8_t* outPixels = canvas.Bits();
            int outStride = canvas.Stride();
            if (pip != NULL)
            {
                const std::chrono::steady_clock::time_point composeStart = std::chrono::steady_clock::now();
                composer.Compose(canvas.Bits(), width, height, canvas.Stride(), NULL, &pip->m_bgra[0], pip->m_width,
                                 pip->m_height);
                m_stageTimes.m_composeMs += msSince(composeStart);
                outPixels = composer.Pixels();
                outStride = composer.Stride();
            }
            else
            {
                ++m_stageTimes.m_directFrames;
            }
            const std::chrono::steady_clock::time_point writeStart = std::chrono::steady_clock::now();
            writeOk = writer.WriteVideo(outPixels, outStride, nextVideoMs * kHnsPerMs, frameMs * kHnsPerMs);
            m_stageTimes.m_videoWriteMs += msSince(writeStart);
            ++m_stageTimes.m_videoFrames;
            nextVideoMs += frameMs;
        }
        const int64_t audioUntil = more ? (outMs - kAudioWriteLagMs) : outMs;
        const std::chrono::steady_clock::time_point audioWriteStart = std::chrono::steady_clock::now();
        while (writeOk && mix.Position() + kCloudAudioFrameSamples <= audioUntil * kSamplesPerMs)
        {
            const int64_t pts = mix.Position() / kSamplesPerMs;
            mix.Take(static_cast<size_t>(kCloudAudioFrameSamples), pcmOut);
            call::MonoToStereoI16(&pcmOut[0], &stereo[0], static_cast<size_t>(kCloudAudioFrameSamples));
            writeOk = writer.WriteAudio(&stereo[0], kCloudAudioFrameSamples, pts * kHnsPerMs);
        }
        m_stageTimes.m_audioMs += msSince(audioWriteStart);
        if (!writeOk)
        {
            LOG_ERROR("cloud compose: record %lld write failed at %lld ms.", static_cast<long long>(m_recordId),
                      static_cast<long long>(outMs));
            return kCloudComposeWriteFail;
        }
        if (!more)
            break;
        lastOutMs = outMs;
        m_progress = static_cast<int>(reader.BytesConsumed() * kProgressMax / fileBytes);

        if (rec.m_type == kFrecRecordSeat)
        {
            int seat = 0;
            int64_t userId = 0;
            if (DecodeFrecSeat(rec.m_payload, seat, userId))
            {
                seatUser[seat] = userId;
                //名字标签据此变化：画布签名带上座位的变化计数
                ++seatVersion;
            }
            continue;
        }
        if (rec.m_type != kFrecRecordFrame)
            continue;
        screenshare::ShareMediaFrame frame;
        if (!reassembler.push(rec.m_payload.data(), rec.m_payload.size(), frame))
            continue;
        switch (frame.m_track)
        {
        //屏幕画面：按座位解码，记下最近一帧的时刻
        case screenshare_track_screen:
        {
            const std::chrono::steady_clock::time_point decodeStart = std::chrono::steady_clock::now();
            screens.OnScreenFrame(frame);
            m_stageTimes.m_decodeMs += msSince(decodeStart);
            lastScreenMs[frame.m_senderSeat] = outMs;
            break;
        }

        //摄像头：每个座位一个解码器
        case screenshare_track_camera:
        {
            int w = 0;
            int h = 0;
            const char* h264 = NULL;
            size_t len = 0;
            if (!screenshare::ShareFraming::parseVideoPayload(frame.m_data, w, h, h264, len))
                break;
            std::unique_ptr<CameraStream>& cam = cameras[frame.m_senderSeat];
            if (!cam)
                cam.reset(new CameraStream());
            if (!cam->m_decoder.IsReady())
                cam->m_decoder.Init(kCameraInitW, kCameraInitH);
            cam->m_sink.SetCrop(w, h);
            cam->m_decoder.DecodePacket(reinterpret_cast<const uint8_t*>(h264), len, outMs * kHnsPerMs, &cam->m_sink);
            cam->m_lastMs = outMs;
            break;
        }

        //麦克风与电脑声音：每路一个 Opus 解码器，按发送端时间戳叠加；麦克风另做说话检测
        case screenshare_track_mic:
        case screenshare_track_system_audio:
        {
            const int streamId = frame.m_senderSeat * kStreamsPerSeat + frame.m_track;
            std::unique_ptr<call::OpusAudioCodec>& dec = opus[streamId];
            if (!dec)
            {
                dec.reset(new call::OpusAudioCodec());
                dec->Init(kCloudAudioRate, kCloudAudioFrameSamples, kOpusDecodeBitrate);
            }
            const std::chrono::steady_clock::time_point audioStart = std::chrono::steady_clock::now();
            std::vector<int16_t> pcm;
            if (!dec->Decode(reinterpret_cast<const unsigned char*>(frame.m_data.data()), frame.m_data.size(), pcm) ||
                pcm.empty())
                break;
            mix.Add(placer.Place(streamId, frame.m_timestamp, outMs), &pcm[0], pcm.size());
            m_stageTimes.m_audioMs += msSince(audioStart);
            if (frame.m_track == screenshare_track_mic && CloudPcmRms(&pcm[0], pcm.size()) >= kCloudSpeakRms)
                lastSpeakMs[frame.m_senderSeat] = outMs;
            break;
        }

        //标注：交给标注模型，按输出时刻处理激光笔淡出
        case screenshare_track_annotation:
        {
            screenshare::AnnotationMessage m;
            if (screenshare::ShareAnnotationCodec::decode(frame.m_data, m))
            {
                annotations.Apply(frame.m_senderSeat, m, outMs);
                //白板视野：白板没有视频帧，据此判断这块白板还在（2026-10-07）
                if (m.m_op == screenshare::annot_op_board)
                    CloudNoteBoardView(m.m_canvas, outMs, lastScreenMs, boards);
            }
            break;
        }

        //控制帧：共享内容的状态（暂停、最小化）。录像里沿用最后一帧画面，不另外标注
        case screenshare_track_control:
        {
            int state = 0;
            if (screenshare::ShareFraming::parseSourceState(frame, state))
                screens.SetSourceState(frame.m_senderSeat, state);
            break;
        }

        //不认识的轨道：忽略
        default:
            break;
        }
    }
    if (!writer.Close())
        return kCloudComposeWriteFail;
    m_progress = kProgressMax;
    LOG_INFO("cloud compose: record %lld composed, %lld ms, screens %u, cameras %u, audio streams %u.",
             static_cast<long long>(m_recordId), static_cast<long long>(lastOutMs),
             static_cast<unsigned>(lastScreenMs.size()), static_cast<unsigned>(cameras.size()),
             static_cast<unsigned>(opus.size()));
    LOG_INFO("cloud compose: record %lld stage times, frames %lld (reused canvas %lld, direct %lld), decode %.0f ms, "
             "draw %.0f ms, compose %.0f ms, video write %.0f ms, audio %.0f ms.", static_cast<long long>(m_recordId),
             static_cast<long long>(m_stageTimes.m_videoFrames), static_cast<long long>(m_stageTimes.m_reusedFrames),
             static_cast<long long>(m_stageTimes.m_directFrames), m_stageTimes.m_decodeMs, m_stageTimes.m_drawMs,
             m_stageTimes.m_composeMs, m_stageTimes.m_videoWriteMs, m_stageTimes.m_audioMs);
    return kCloudComposeOk;
}

} // namespace screenrecord
