/**
 *  远程桌面帧编码器（被控端）实现，ScreenFrameEncoder.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "ScreenFrameEncoder.h"
#include "RdEngineDef.h"     // rd_frame_bitmapinfo / first / next、rd_algo_diff
#include <stdint.h>
#include <string.h>

namespace remotedesktop {

//小端追加一个 32 位整数到字节流。
static void appendU32(std::vector<BYTE>& v, uint32_t x)
{
    v.push_back((BYTE)(x & 0xFF));
    v.push_back((BYTE)((x >> 8) & 0xFF));
    v.push_back((BYTE)((x >> 16) & 0xFF));
    v.push_back((BYTE)((x >> 24) & 0xFF));
}

//BGRA(8:8:8) → RGB555(5:5:5)：取每通道高 5 位拼成 16 位（与 16 位 BI_RGB DIB 一致）。
static WORD bgraToRgb555(BYTE b, BYTE g, BYTE r)
{
    return (WORD)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

CScreenFrameEncoder::CScreenFrameEncoder()
    : m_width(0)
    , m_height(0)
    , m_hasBaseline(false)
{
}

int CScreenFrameEncoder::dibStride() const
{
    //16 位 DIB 行字节，4 字节对齐：((w*16+31) & ~31) >> 3
    return (((m_width * 16) + 31) & ~31) >> 3;
}

int CScreenFrameEncoder::dibSizeImage() const
{
    return dibStride() * m_height;
}

void CScreenFrameEncoder::buildBitmapInfoHeader(BITMAPINFOHEADER& hdr) const
{
    memset(&hdr, 0, sizeof(hdr));
    hdr.biSize        = sizeof(BITMAPINFOHEADER);
    hdr.biWidth       = m_width;
    hdr.biHeight      = m_height;     //正值 = bottom-up，与旧引擎 CScreenSpy 一致
    hdr.biPlanes      = 1;
    hdr.biBitCount    = 16;
    hdr.biCompression = BI_RGB;       //16 位 BI_RGB = RGB555
    hdr.biSizeImage   = (DWORD)dibSizeImage();
}

void CScreenFrameEncoder::convertToDib(const FrameView& bgra, std::vector<BYTE>& dst) const
{
    int stride = dibStride();
    dst.assign((size_t)stride * m_height, 0);

    for (int y = 0; y < m_height; ++y)
    {
        //BGRA top-down 第 y 行 → DIB bottom-up 第 (h-1-y) 行（垂直翻转，匹配正 biHeight）
        const BYTE* src = bgra.m_data + (size_t)y * bgra.m_stride;
        BYTE* row = &dst[(size_t)(m_height - 1 - y) * stride];
        for (int x = 0; x < m_width; ++x)
        {
            BYTE b = src[x * 4 + 0];
            BYTE g = src[x * 4 + 1];
            BYTE r = src[x * 4 + 2];
            WORD c = bgraToRgb555(b, g, r);
            row[x * 2 + 0] = (BYTE)(c & 0xFF);
            row[x * 2 + 1] = (BYTE)((c >> 8) & 0xFF);
        }
    }
}

void CScreenFrameEncoder::Feed(const FrameView& bgra)
{
    if (bgra.m_data == NULL || bgra.m_width <= 0 || bgra.m_height <= 0)
        return;

    //尺寸变化（含首次喂入）：更新尺寸并令下一次输出视为首屏（基准失效）
    if (bgra.m_width != m_width || bgra.m_height != m_height)
    {
        m_width = bgra.m_width;
        m_height = bgra.m_height;
        m_hasBaseline = false;
    }

    convertToDib(bgra, m_current);
}

bool CScreenFrameEncoder::HasFrame() const
{
    return !m_current.empty();
}

bool CScreenFrameEncoder::NeedsKeyframe() const
{
    //Feed 在尺寸变化（含首次）时把基准置为失效，GetFirstFrame 把当前帧设为基准
    return HasFrame() && !m_hasBaseline;
}

int CScreenFrameEncoder::Width() const
{
    return m_width;
}

int CScreenFrameEncoder::Height() const
{
    return m_height;
}

void CScreenFrameEncoder::GetBitmapInfo(std::vector<BYTE>& out) const
{
    out.clear();
    out.push_back((BYTE)rd_frame_bitmapinfo);
    if (m_width <= 0 || m_height <= 0)
        return;

    BITMAPINFOHEADER hdr;
    buildBitmapInfoHeader(hdr);
    const BYTE* p = (const BYTE*)&hdr;
    out.insert(out.end(), p, p + sizeof(hdr));
}

void CScreenFrameEncoder::GetFirstFrame(std::vector<BYTE>& out)
{
    out.clear();
    out.push_back((BYTE)rd_frame_first);
    out.insert(out.end(), m_current.begin(), m_current.end());

    //首屏发出后，对端画面即当前帧：把它设为后续 DIFF 的基准
    m_baseline = m_current;
    m_hasBaseline = true;
}

void CScreenFrameEncoder::GetNextFrame(std::vector<BYTE>& out, POINT cursorPos, BYTE cursorType)
{
    out.clear();
    out.push_back((BYTE)rd_frame_next);
    out.push_back((BYTE)rd_algo_diff);

    //光标位置（POINT）+ 类型，原样写入（与 RdScreenDecoder 解析顺序一致）
    const BYTE* pp = (const BYTE*)&cursorPos;
    out.insert(out.end(), pp, pp + sizeof(POINT));
    out.push_back(cursorType);

    //无基准（未发首屏）或尺寸不匹配：只产出帧头，不含差异块；并把当前帧设为基准
    if (!m_hasBaseline || m_baseline.size() != m_current.size())
    {
        m_baseline = m_current;
        m_hasBaseline = true;
        return;
    }

    //纯内存 DIFF：扫描连续不同的字节段，每段产出 [偏移 + 长度 + 当前帧数据]
    size_t n = m_current.size();
    size_t i = 0;
    while (i < n)
    {
        if (m_current[i] == m_baseline[i])
        {
            ++i;
            continue;
        }
        size_t start = i;
        while (i < n && m_current[i] != m_baseline[i])
            ++i;
        appendU32(out, (uint32_t)start);
        appendU32(out, (uint32_t)(i - start));
        out.insert(out.end(), m_current.begin() + start, m_current.begin() + i);
    }

    //基准推进为当前帧
    m_baseline = m_current;
}

} // namespace remotedesktop
