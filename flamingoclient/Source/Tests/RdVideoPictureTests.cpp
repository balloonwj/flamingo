/**
 *  远程桌面控制端的画面切换与 H.264 画面（标签 [rdvideo]；RDV8 为隐藏用例，标签 [.rdvideodec]），RdVideoPictureTests.cpp
 *
 *  背景：docs/远程桌面H264与共享画质提升方案.md 4.7、6.2（2026-10-15）。控制端的画面有旧格式（RdScreenDecoder）与 H.264
 *  （RdVideoPicture，自带解码线程）两种，观看窗与画布只认 IRdPicture，按最近就绪的一种显示。
 *    - RDV7 观看窗：旧格式首屏到了用旧格式画面；H.264 解出画面后换到 H.264，源尺寸按帧头里的源尺寸（编码尺寸小于源尺寸）；
 *           鼠标坐标按源尺寸换算；5 号指针帧更新 H.264 画面的指针；被控端退回旧格式（再来一组旧格式首屏）时换回；
 *           收旧格式期间的 6 号能力帧触发一次带能力位的请求、一秒内不再触发，收 H.264 期间不触发；
 *           H.264 需要关键帧时的请求每秒最多一次；
 *    - RDV8 （隐藏，需要本机的 H.264 编解码器）ShareVideoEncoder 按远程桌面参数编一段合成画面，组成 4 号帧交给 RdVideoPicture，
 *           解码线程解出的画面尺寸与源一致、内容与原图大致相同；回执时刻按「发送时刻 + 停留时长」取。
 *  框架 Catch2 v3。RDV7 会建一个观看窗（与 [rdctl] 的用例相同），请在隐藏桌面上运行。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objbase.h>
#include <mfapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../../FlamingoMedia/RemoteDesktop/RdViewerWindow.h"
#include "../../FlamingoMedia/RemoteDesktop/RdVideoPicture.h"
#include "../../FlamingoMedia/RemoteDesktop/RdVideoFrames.h"
#include "../../FlamingoMedia/RemoteDesktop/RdEngineDef.h"
#include "../../FlamingoMedia/Share/ShareVideoEncoder.h"
#include "../../FlamingoMedia/Share/ShareVideoPolicy.h"

#pragma comment(lib, "mfplat.lib")

using namespace remotedesktop;

namespace {

//用例里的会话 id
const char* const kRdpSession = "rdp-session";
//旧格式画面的尺寸与位深（像素）
const int kRdpLegacyW = 64;
const int kRdpLegacyH = 36;
const int kRdpLegacyBits = 16;
//H.264 画面：源 4K，编码尺寸远小于源（只为让两者不同）
const int kRdpSrcW = 3840;
const int kRdpSrcH = 2160;
const int kRdpEncW = 96;
const int kRdpEncH = 54;
//每像素字节数（BGRA）
const int kRdpBpp = 4;

//旧格式的 BITMAPINFO 帧
std::vector<unsigned char> rdpBitmapInfo(int w, int h)
{
    BITMAPINFOHEADER hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.biSize = sizeof(BITMAPINFOHEADER);
    hdr.biWidth = w;
    hdr.biHeight = h;
    hdr.biPlanes = 1;
    hdr.biBitCount = static_cast<WORD>(kRdpLegacyBits);
    hdr.biCompression = BI_RGB;
    //DIB 行按 4 字节对齐
    const long stride = ((static_cast<long>(w) * kRdpLegacyBits + 31) & ~31) >> 3;
    hdr.biSizeImage = static_cast<DWORD>(stride * h);
    std::vector<unsigned char> out;
    out.push_back(static_cast<unsigned char>(rd_frame_bitmapinfo));
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&hdr);
    out.insert(out.end(), p, p + sizeof(hdr));
    return out;
}

//旧格式的首屏帧（整屏填同一个值）
std::vector<unsigned char> rdpFirst(int w, int h, unsigned char fill)
{
    const long stride = ((static_cast<long>(w) * kRdpLegacyBits + 31) & ~31) >> 3;
    std::vector<unsigned char> out;
    out.push_back(static_cast<unsigned char>(rd_frame_first));
    out.insert(out.end(), static_cast<size_t>(stride * h), fill);
    return out;
}

//像接收线程那样把一帧投给观看窗（堆上拷贝，由窗口释放）；用 SendMessage 同步处理
void rdpSendFrame(CRdViewerWindow& wnd, const std::vector<unsigned char>& frame)
{
    std::vector<unsigned char>* heap = new std::vector<unsigned char>(frame);
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_FRAME, 0, reinterpret_cast<LPARAM>(heap));
}

//发一组旧格式关键帧（BITMAPINFO + 首屏）
void rdpSendLegacyKeyframe(CRdViewerWindow& wnd)
{
    const unsigned char kFill = 0x33;
    rdpSendFrame(wnd, rdpBitmapInfo(kRdpLegacyW, kRdpLegacyH));
    rdpSendFrame(wnd, rdpFirst(kRdpLegacyW, kRdpLegacyH, kFill));
}

} // namespace

TEST_CASE("RDV7 观看窗在旧格式与 H.264 画面间切换，按源尺寸换算鼠标，能力帧与关键帧请求限频", "[rdvideo]")
{
    CRdViewerWindow wnd;
    REQUIRE(wnd.OpenForTest(kRdpSession));
    wnd.SetDecodeCapsForTest(true, screenshare::kRdVideoMaxLongSide);
    CHECK(wnd.ActivePictureForTest() == rd_picture_none);

    //旧格式：只有 BITMAPINFO 还没有画面，首屏到了才换到旧格式画面
    rdpSendFrame(wnd, rdpBitmapInfo(kRdpLegacyW, kRdpLegacyH));
    CHECK(wnd.ActivePictureForTest() == rd_picture_none);
    rdpSendFrame(wnd, rdpFirst(kRdpLegacyW, kRdpLegacyH, 0));
    CHECK(wnd.ActivePictureForTest() == rd_picture_legacy);
    SIZE size = wnd.PictureSourceSizeForTest();
    CHECK(size.cx == kRdpLegacyW);
    CHECK(size.cy == kRdpLegacyH);

    //收旧格式期间被控端通告能编 H.264：重发一次带能力位的请求，一秒之内再来不重发
    int requests = wnd.KeyRequestsForTest();
    std::vector<unsigned char> caps;
    RdVideoFrames::BuildCaps(kRdCapH264, caps);
    rdpSendFrame(wnd, caps);
    CHECK(wnd.KeyRequestsForTest() == requests + 1);
    rdpSendFrame(wnd, caps);
    CHECK(wnd.KeyRequestsForTest() == requests + 1);
    //能力帧不是画面：仍是旧格式画面
    CHECK(wnd.ActivePictureForTest() == rd_picture_legacy);

    //H.264 解出了画面：换到 H.264，源尺寸取帧头里的源尺寸，与编码尺寸无关
    const std::vector<uint8_t> bgra(static_cast<size_t>(kRdpEncW) * kRdpEncH * kRdpBpp, 0x80);
    wnd.VideoPictureForTest().SetFrameForTest(kRdpSrcW, kRdpSrcH, kRdpEncW, kRdpEncH, bgra);
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_PICTURE, 0, 0);
    CHECK(wnd.ActivePictureForTest() == rd_picture_video);
    size = wnd.PictureSourceSizeForTest();
    CHECK(size.cx == kRdpSrcW);
    CHECK(size.cy == kRdpSrcH);

    //鼠标按源尺寸换算：画面左上角是 (0, 0)，正中是源的正中（误差不超过一个窗口像素对应的源像素）
    const RECT pr = wnd.PictureRectForTest();
    const int prW = pr.right - pr.left;
    const int prH = pr.bottom - pr.top;
    REQUIRE(prW > 0);
    REQUIRE(prH > 0);
    //画面按 16:9 放进画布：宽高比与源一致（误差一个像素）
    CHECK(abs(prW * kRdpSrcH - prH * kRdpSrcW) <= kRdpSrcW);
    int rx = -1;
    int ry = -1;
    REQUIRE(wnd.MapMouseMoveForTest(pr.left, pr.top, rx, ry));
    CHECK(rx == 0);
    CHECK(ry == 0);
    REQUIRE(wnd.MapMouseMoveForTest(pr.left + prW / 2, pr.top + prH / 2, rx, ry));
    const int tolerance = kRdpSrcW / prW + 1;
    CHECK(abs(rx - kRdpSrcW / 2) <= tolerance);
    CHECK(abs(ry - kRdpSrcH / 2) <= tolerance);

    //5 号指针帧：更新 H.264 画面的指针
    std::vector<unsigned char> cursor;
    RdVideoFrames::BuildCursor(100, 200, 3, cursor);
    rdpSendFrame(wnd, cursor);
    const POINT at = wnd.VideoPictureForTest().cursorPos();
    CHECK(at.x == 100);
    CHECK(at.y == 200);
    CHECK(wnd.VideoPictureForTest().cursorType() == 3);

    //收 H.264 期间的能力帧（每个关键帧之前都有）：不请求
    wnd.SetDecodeCapsForTest(true, screenshare::kRdVideoMaxLongSide);
    requests = wnd.KeyRequestsForTest();
    rdpSendFrame(wnd, caps);
    CHECK(wnd.KeyRequestsForTest() == requests);

    //H.264 需要关键帧：请求一次，一秒之内再来不请求
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_KEYREQ, 0, 0);
    CHECK(wnd.KeyRequestsForTest() == requests + 1);
    ::SendMessageW(wnd.m_hWnd, CRdViewerWindow::WM_RD_VIEWER_KEYREQ, 0, 0);
    CHECK(wnd.KeyRequestsForTest() == requests + 1);

    //被控端编码器中途失败、退回旧格式：再来一组旧格式关键帧，换回旧格式画面
    rdpSendLegacyKeyframe(wnd);
    CHECK(wnd.ActivePictureForTest() == rd_picture_legacy);
    size = wnd.PictureSourceSizeForTest();
    CHECK(size.cx == kRdpLegacyW);
    CHECK(size.cy == kRdpLegacyH);

    wnd.CloseViewer();
}

namespace {

//RDV8 的源尺寸、帧数、帧间隔（微秒）
const int kRdv8W = 1280;
const int kRdv8H = 720;
const int kRdv8Frames = 30;
const uint64_t kRdv8FrameUs = 33333;
//等解码线程解出画面的上限与轮询间隔（毫秒）
const ULONGLONG kRdv8WaitMs = 5000;
const DWORD kRdv8PollMs = 10;
//推帧间隔（毫秒）：按 30 fps 推，与真实的接收节奏一致。一次推完 30 包会超过收帧队列的上限（15 帧），
//第 16 包时队列清空并开始等关键帧（RDV6 验证的设计行为），之后的增量帧全被丢弃
const DWORD kRdv8PushGapMs = 33;
//解出的画面与原图的绿色通道平均绝对差上限（8 位）
const double kRdv8MaxMeanDiff = 16.0;
//合成画面的格子边长（像素）
const int kRdv8Cell = 80;

//收集编码输出
class Rdv8Sink : public screenshare::ShareVideoEncoderSink
{
public:
    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs) override
    {
        (void)timestampUs;
        m_packets.push_back(std::vector<uint8_t>(data, data + len));
        m_keys.push_back(keyframe);
    }

    std::vector<std::vector<uint8_t> > m_packets;   //每一包 H.264
    std::vector<bool>                  m_keys;      //对应的关键帧标志
};

//合成画面：横向渐变底加棋盘格，静止不动（解出的任何一帧都应与它一致）
std::vector<uint8_t> rdv8Picture()
{
    const int kMaxChannel = 255;
    const unsigned char kCellBright = 220;
    const unsigned char kCellDark = 40;
    std::vector<uint8_t> bgra(static_cast<size_t>(kRdv8W) * kRdv8H * kRdpBpp);
    for (int y = 0; y < kRdv8H; ++y)
    {
        for (int x = 0; x < kRdv8W; ++x)
        {
            uint8_t* p = &bgra[(static_cast<size_t>(y) * kRdv8W + x) * kRdpBpp];
            const bool bright = (((x / kRdv8Cell) + (y / kRdv8Cell)) % 2) == 0;
            p[0] = static_cast<uint8_t>(x * kMaxChannel / kRdv8W);
            p[1] = bright ? kCellBright : kCellDark;
            p[2] = static_cast<uint8_t>(y * kMaxChannel / kRdv8H);
            p[3] = static_cast<uint8_t>(kMaxChannel);
        }
    }
    return bgra;
}

} // namespace

TEST_CASE("RDV8 真实编码的 H.264 经 RdVideoPicture 解出，尺寸与内容大致一致", "[.rdvideodec]")
{
    ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ::MFStartup(MF_VERSION);
    {
        //按远程桌面参数编码（长边上限 2560，1280×720 编原尺寸）
        screenshare::ShareVideoEncoder encoder;
        encoder.SetParams(screenshare::RdScreenVideoParams());
        Rdv8Sink sink;
        const std::vector<uint8_t> picture = rdv8Picture();
        for (int i = 0; i < kRdv8Frames; ++i)
        {
            screenshare::ShareCaptureFrame frame;
            frame.m_data = &picture[0];
            frame.m_stride = kRdv8W * kRdpBpp;
            frame.m_width = kRdv8W;
            frame.m_height = kRdv8H;
            REQUIRE(encoder.Encode(frame, i == 0, static_cast<uint64_t>(i) * kRdv8FrameUs, &sink));
        }
        REQUIRE_FALSE(sink.m_packets.empty());
        const bool firstIsKey = sink.m_keys[0];
        CHECK(firstIsKey);
        CHECK(encoder.EncodeWidth() == kRdv8W);
        CHECK(encoder.EncodeHeight() == kRdv8H);

        //组成 4 号帧交给 RdVideoPicture（像接收线程那样）
        RdVideoPicture pic;
        pic.Start(NULL, 0, 0);
        for (size_t i = 0; i < sink.m_packets.size(); ++i)
        {
            RdVideoFrameHeader h;
            h.m_srcW = kRdv8W;
            h.m_srcH = kRdv8H;
            h.m_encW = encoder.EncodeWidth();
            h.m_encH = encoder.EncodeHeight();
            h.m_keyframe = sink.m_keys[i];
            h.m_sendMs = static_cast<uint32_t>(i);
            std::vector<unsigned char> frame;
            RdVideoFrames::BuildVideo(h, &sink.m_packets[i][0], sink.m_packets[i].size(), frame);
            REQUIRE(pic.PushFrame(frame.data(), frame.size()));
            ::Sleep(kRdv8PushGapMs);
        }
        //等解码线程解出画面
        const ULONGLONG start = ::GetTickCount64();
        while (pic.DecodedCount() == 0 && ::GetTickCount64() - start < kRdv8WaitMs)
            ::Sleep(kRdv8PollMs);
        CAPTURE(sink.m_packets.size(), pic.DecodedCount(), pic.WaitingForKeyframe());
        REQUIRE(pic.ready());
        CHECK(pic.width() == kRdv8W);
        CHECK(pic.height() == kRdv8H);

        //内容：绿色通道（棋盘格）与原图的平均差很小
        std::vector<uint8_t> decoded;
        int w = 0;
        int h = 0;
        REQUIRE(pic.GetFrameForTest(decoded, w, h));
        REQUIRE(w == kRdv8W);
        REQUIRE(h == kRdv8H);
        double sum = 0;
        for (size_t i = 0; i < decoded.size(); i += kRdpBpp)
            sum += abs(static_cast<int>(decoded[i + 1]) - static_cast<int>(picture[i + 1]));
        const double meanDiff = sum / (static_cast<double>(w) * h);
        CAPTURE(meanDiff);
        CHECK(meanDiff < kRdv8MaxMeanDiff);

        //回执：收过视频帧，回送的时刻不早于最后一帧的发送时刻
        uint32_t echo = 0;
        REQUIRE(pic.AckEcho(static_cast<int64_t>(::GetTickCount64()), echo));
        CHECK(echo >= static_cast<uint32_t>(sink.m_packets.size() - 1));
        pic.Stop();
    }
    ::MFShutdown();
    ::CoUninitialize();
}
