/**
 *  录制画面的合成，见 RecordFrameComposer.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "RecordFrameComposer.h"

#include <string.h>

#include "RecordPolicy.h"
#include "../Share/ShareAnnotationRender.h"

namespace screenrecord {

namespace {

//32 位 DIB 的位深与每像素字节数
const WORD kBitsPerPixel32 = 32;
const int kBgraBytes = 4;
//画中画外圈白边的宽度（像素）
const int kPipBorderPx = 2;

void fillBitmapInfo(BITMAPINFO& bi, int w, int h)
{
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
}

} // namespace

RecordFrameComposer::RecordFrameComposer()
    : m_dc(NULL)
    , m_bitmap(NULL)
    , m_oldBmp(NULL)
    , m_bits(NULL)
    , m_width(0)
    , m_height(0)
{
    memset(&m_content, 0, sizeof(m_content));
}

RecordFrameComposer::~RecordFrameComposer()
{
    release();
}

void RecordFrameComposer::release()
{
    if (m_dc != NULL)
    {
        if (m_oldBmp != NULL)
            ::SelectObject(m_dc, m_oldBmp);
        ::DeleteDC(m_dc);
    }
    if (m_bitmap != NULL)
        ::DeleteObject(m_bitmap);
    m_dc = NULL;
    m_bitmap = NULL;
    m_oldBmp = NULL;
    m_bits = NULL;
    m_width = 0;
    m_height = 0;
}

bool RecordFrameComposer::Init(int width, int height)
{
    release();
    if (width <= 0 || height <= 0)
        return false;
    BITMAPINFO bi;
    fillBitmapInfo(bi, width, height);
    void* bits = NULL;
    m_bitmap = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (m_bitmap == NULL)
        return false;
    m_dc = ::CreateCompatibleDC(NULL);
    if (m_dc == NULL)
    {
        release();
        return false;
    }
    m_oldBmp = ::SelectObject(m_dc, m_bitmap);
    m_bits = static_cast<uint8_t*>(bits);
    m_width = width;
    m_height = height;
    memset(m_bits, 0, static_cast<size_t>(width) * height * kBgraBytes);
    return true;
}

void RecordFrameComposer::Compose(const uint8_t* src, int srcW, int srcH, int srcStride,
                                  const std::vector<screenshare::AnnotationStroke>* strokes, const uint8_t* pip,
                                  int pipW, int pipH)
{
    if (m_dc == NULL)
        return;
    const RECT all = { 0, 0, m_width, m_height };
    ::FillRect(m_dc, &all, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
    m_content = all;
    if (src != NULL && srcW > 0 && srcH > 0 && srcStride >= srcW * kBgraBytes)
    {
        //等比缩放进画布，四周黑边
        int drawW = m_width;
        int drawH = static_cast<int>(static_cast<int64_t>(srcH) * m_width / srcW);
        if (drawH > m_height)
        {
            drawH = m_height;
            drawW = static_cast<int>(static_cast<int64_t>(srcW) * m_height / srcH);
        }
        m_content.left = (m_width - drawW) / 2;
        m_content.top = (m_height - drawH) / 2;
        m_content.right = m_content.left + drawW;
        m_content.bottom = m_content.top + drawH;
        //2026-10-15（docs/远程桌面H264与共享画质提升方案.md A7）：原尺寸逐行复制、缩小用 BgraDownscaler 直接写进画布，
        //只有放大才走 HALFTONE。HALFTONE 的 StretchDIBits 在 2560×1440 原尺寸时每帧约 15 毫秒、缩到 1080p 约 30 毫秒（A0 实测）。
        //直接写像素之前先让上面的 FillRect 落到画布上
        ::GdiFlush();
        uint8_t* origin = m_bits + (static_cast<size_t>(m_content.top) * m_width + m_content.left) * kBgraBytes;
        if (drawW == srcW && drawH == srcH)
        {
            const size_t rowBytes = static_cast<size_t>(srcW) * kBgraBytes;
            for (int y = 0; y < srcH; ++y)
                memcpy(origin + static_cast<size_t>(y) * m_width * kBgraBytes, src + static_cast<size_t>(y) * srcStride,
                       rowBytes);
        }
        else if (screenshare::ShareUsesFastDownscale(srcW, srcH, drawW, drawH))
        {
            m_scaler.Scale(src, srcStride, srcW, srcH, origin, m_width * kBgraBytes, drawW, drawH);
        }
        else
        {
            //放大：行跨度不是宽 × 4 时，按行跨度当作更宽的图、只取左边 srcW 列
            BITMAPINFO bi;
            fillBitmapInfo(bi, srcStride / kBgraBytes, srcH);
            ::SetStretchBltMode(m_dc, HALFTONE);
            ::SetBrushOrgEx(m_dc, 0, 0, NULL);
            ::StretchDIBits(m_dc, m_content.left, m_content.top, drawW, drawH, 0, 0, srcW, srcH, src, &bi, DIB_RGB_COLORS,
                            SRCCOPY);
        }
    }
    //标注：画在画面来源所占的矩形里
    if (strokes != NULL && !strokes->empty())
        screenshare::DrawAnnotations(m_dc, m_content, *strokes, NULL, false);
    //画中画：默认右下角；录制中屏幕上有摄像头小窗时按它的位置（2026-10-05），外圈白边
    if (pip != NULL && pipW > 0 && pipH > 0)
    {
        const RECT rc = PlacePipRect(m_content, m_pipPlacement, m_width, m_height);
        const RECT frame = { rc.left - kPipBorderPx, rc.top - kPipBorderPx, rc.right + kPipBorderPx,
                             rc.bottom + kPipBorderPx };
        ::FillRect(m_dc, &frame, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
        ::FillRect(m_dc, &rc, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
        //摄像头画面按自身宽高比缩进画中画框（4:3 的摄像头左右留黑边），不拉伸变形
        const int boxW = rc.right - rc.left;
        const int boxH = rc.bottom - rc.top;
        int dw = boxW;
        int dh = static_cast<int>(static_cast<int64_t>(pipH) * boxW / pipW);
        if (dh > boxH)
        {
            dh = boxH;
            dw = static_cast<int>(static_cast<int64_t>(pipW) * boxH / pipH);
        }
        BITMAPINFO bi;
        fillBitmapInfo(bi, pipW, pipH);
        ::SetStretchBltMode(m_dc, HALFTONE);
        ::SetBrushOrgEx(m_dc, 0, 0, NULL);
        ::StretchDIBits(m_dc, rc.left + (boxW - dw) / 2, rc.top + (boxH - dh) / 2, dw, dh, 0, 0, pipW, pipH, pip, &bi,
                        DIB_RGB_COLORS, SRCCOPY);
    }
    ::GdiFlush();
}

void RecordFrameComposer::SetPipPlacement(const RecordPipPlacement& placement)
{
    m_pipPlacement = placement;
}

const uint8_t* RecordFrameComposer::Pixels() const
{
    return m_bits;
}

int RecordFrameComposer::Stride() const
{
    return m_width * kBgraBytes;
}

int RecordFrameComposer::Width() const
{
    return m_width;
}

int RecordFrameComposer::Height() const
{
    return m_height;
}

const RECT& RecordFrameComposer::ContentRect() const
{
    return m_content;
}

} // namespace screenrecord
