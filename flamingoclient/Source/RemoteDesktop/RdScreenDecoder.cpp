/**
 *  远程桌面解码绘制（控制端）实现，RdScreenDecoder.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RdScreenDecoder.h"
#include "RdEngineDef.h"
#include <string.h>

namespace remotedesktop {

RdScreenDecoder::RdScreenDecoder()
    : m_width(0)
    , m_height(0)
    , m_bitCount(0)
    , m_biSizeImage(0)
    , m_cursorType(0)
    , m_hasBmi(false)
    , m_hasFirst(false)
{
    m_cursorPos.x = 0;
    m_cursorPos.y = 0;
}

bool RdScreenDecoder::apply(const BYTE* data, size_t len)
{
    if (data == NULL || len < 1)
        return false;

    BYTE type = data[0];
    switch (type)
    {
    case rd_frame_bitmapinfo:
        applyBitmapInfo(data + 1, len - 1);
        return false;
    case rd_frame_first:
        applyFirst(data + 1, len - 1);
        return true;
    case rd_frame_next:
        applyNext(data + 1, len - 1);
        return true;
    default:
        return false;
    }
}

void RdScreenDecoder::applyBitmapInfo(const BYTE* p, size_t n)
{
    if (n < sizeof(BITMAPINFOHEADER))
        return;

    m_bmi.assign(p, p + n);
    const BITMAPINFOHEADER* h = (const BITMAPINFOHEADER*)m_bmi.data();
    m_width    = h->biWidth;
    //DIB 高度可能为负（自顶向下），取绝对值作像素高
    m_height   = h->biHeight >= 0 ? h->biHeight : -h->biHeight;
    m_bitCount = h->biBitCount;

    //每行字节数按 4 字节对齐；biSizeImage 为 0 时据此重算
    long stride = (((long)m_width * m_bitCount + 31) & ~31) >> 3;
    m_biSizeImage = h->biSizeImage != 0 ? (long)h->biSizeImage : stride * m_height;

    m_screen.assign((size_t)m_biSizeImage, 0);
    m_hasBmi = true;
    m_hasFirst = false;
}

void RdScreenDecoder::applyFirst(const BYTE* p, size_t n)
{
    if (!m_hasBmi)
        return;

    size_t copy = (size_t)m_biSizeImage < n ? (size_t)m_biSizeImage : n;
    if (copy > 0)
        memcpy(&m_screen[0], p, copy);
    m_hasFirst = true;
}

void RdScreenDecoder::applyNext(const BYTE* p, size_t n)
{
    //首屏尚未建立时无从增量；头部不足也忽略
    if (!m_hasFirst)
        return;
    if (n < (size_t)rd_next_header_len)
        return;

    unsigned char algo = p[0];
    memcpy(&m_cursorPos, p + 1, sizeof(POINT));
    m_cursorType = p[1 + sizeof(POINT)];

    const BYTE* blk = p + rd_next_header_len;
    size_t remain = n - rd_next_header_len;

    if (algo == rd_algo_diff)
    {
        //差异块序列：[偏移 4][长度 4][数据 长度]，直接贴回整屏缓冲
        while (remain >= 8)
        {
            uint32_t off  = *(const uint32_t*)blk;
            uint32_t blen = *(const uint32_t*)(blk + 4);
            blk += 8;
            remain -= 8;
            if (blen > remain)
                break;
            if ((size_t)off + (size_t)blen <= m_screen.size())
                memcpy(&m_screen[off], blk, blen);
            blk += blen;
            remain -= blen;
        }
    }
    //注：一期抓屏端固定差异算法，故此处只实现 DIFF；
    //隔行扫描（rd_algo_scan）的逆贴留待需要时再补。
}

void RdScreenDecoder::drawTo(HDC hdc, const RECT& dest) const
{
    if (!m_hasFirst || m_bmi.empty() || m_screen.empty())
        return;

    const BITMAPINFO* bmi = (const BITMAPINFO*)m_bmi.data();
    int dw = dest.right - dest.left;
    int dh = dest.bottom - dest.top;
    StretchDIBits(hdc,
                  dest.left, dest.top, dw, dh,
                  0, 0, m_width, m_height,
                  m_screen.data(), bmi, DIB_RGB_COLORS, SRCCOPY);
}

bool RdScreenDecoder::ready() const
{
    return m_hasBmi && m_hasFirst;
}

int RdScreenDecoder::width() const
{
    return m_width;
}

int RdScreenDecoder::height() const
{
    return m_height;
}

POINT RdScreenDecoder::cursorPos() const
{
    return m_cursorPos;
}

BYTE RdScreenDecoder::cursorType() const
{
    return m_cursorType;
}

const std::vector<BYTE>& RdScreenDecoder::screen() const
{
    return m_screen;
}

} // namespace remotedesktop
