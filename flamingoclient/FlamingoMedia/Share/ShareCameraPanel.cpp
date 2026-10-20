/**
 *  屏幕共享房间的摄像头小窗面板，见 ShareCameraPanel.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareCameraPanel.h"

#include "ShareVideoPolicy.h"
#include "ShareAspectFit.h"   // FitRectKeepAspect：摄像头画面等比缩进小窗
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//小窗与面板边缘、小窗之间的间隔（像素）
const int kTilePad = 8;
//小窗宽高比 16:9
const int kAspectW = 16;
const int kAspectH = 9;
//小窗最小高度：再小就不画了
const int kMinTileH = 20;
//底部「另有 N 人」那一行的高度
const int kOverflowLineH = 24;
//名字条的高度与透明度
const int kNameBarH = 20;
const BYTE kNameBarAlpha = 150;
//颜色：面板底色、小窗无画面时的底色、文字
const COLORREF kPanelBg = RGB(24, 24, 28);
const COLORREF kTileBg = RGB(48, 48, 54);
const COLORREF kNameBarBg = RGB(0, 0, 0);
const COLORREF kTextColor = RGB(235, 235, 235);
const COLORREF kOverflowText = RGB(170, 170, 176);
//32 位 DIB 的位深
const WORD kBitsPerPixel32 = 32;
//名字条文字的左内边距
const int kNamePadX = 6;
//发起人摄像头浮动窗口：宽、标题栏高、距工作区边缘的边距（像素）
const int kCameraWindowW = 260;
const int kCameraWindowTitleH = 28;
const int kCameraWindowEdge = 12;

} // namespace

void ComputeCameraTileRects(const RECT& area, int count, int bottomReserve, std::vector<RECT>& out)
{
    out.clear();
    if (count <= 0)
        return;
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top - (bottomReserve > 0 ? bottomReserve : 0);
    int tileW = areaW - 2 * kTilePad;
    int tileH = tileW * kAspectH / kAspectW;
    //竖向放不下：按高度均分，宽度随之缩小
    const int needH = count * tileH + (count + 1) * kTilePad;
    if (needH > areaH)
    {
        tileH = (areaH - (count + 1) * kTilePad) / count;
        tileW = tileH * kAspectW / kAspectH;
    }
    if (tileW <= 0 || tileH < kMinTileH)
        return;
    const int left = area.left + (areaW - tileW) / 2;
    int top = area.top + kTilePad;
    for (int i = 0; i < count; ++i)
    {
        RECT rc = { left, top, left + tileW, top + tileH };
        out.push_back(rc);
        top += tileH + kTilePad;
    }
}

int BuildCameraTiles(bool selfOn, int selfSeat, const std::set<int>& otherSeats, const std::map<int, std::wstring>& names,
                     int maxTiles, std::vector<ShareCameraTile>& tiles)
{
    tiles.clear();
    int overflow = 0;
    if (selfOn && maxTiles > 0)
    {
        ShareCameraTile self;
        self.m_seat = selfSeat;
        self.m_name = TR(L"我");
        self.m_self = true;
        tiles.push_back(self);
    }
    //std::set 本身按座位号升序
    for (std::set<int>::const_iterator it = otherSeats.begin(); it != otherSeats.end(); ++it)
    {
        if (static_cast<int>(tiles.size()) >= maxTiles)
        {
            ++overflow;
            continue;
        }
        ShareCameraTile tile;
        tile.m_seat = *it;
        std::map<int, std::wstring>::const_iterator name = names.find(*it);
        tile.m_name = (name != names.end() && !name->second.empty()) ? name->second : std::wstring(TR(L"成员"));
        tiles.push_back(tile);
    }
    return overflow;
}

RECT ComputeCameraWindowRect(const RECT& work, int tileCount, int overflowCount)
{
    //小窗宽用满窗口宽（左右各留 kTilePad），高按 16:9；面板顶部先留一道间隔，每个小窗下面再留一道
    const int tileH = (kCameraWindowW - 2 * kTilePad) * kAspectH / kAspectW;
    int h = kCameraWindowTitleH + kTilePad + (tileCount > 0 ? tileCount : 0) * (tileH + kTilePad) +
            (overflowCount > 0 ? kOverflowLineH : 0);
    const int maxH = (work.bottom - work.top) - 2 * kCameraWindowEdge;
    if (h > maxH)
        h = maxH;
    RECT rc;
    rc.right = work.right - kCameraWindowEdge;
    rc.left = rc.right - kCameraWindowW;
    rc.top = work.top + kCameraWindowEdge + (maxH - h) / 2;
    rc.bottom = rc.top + h;
    return rc;
}

RECT ComputeCameraHandleRect(const RECT& expanded)
{
    RECT rc;
    rc.right = expanded.right;
    rc.left = rc.right - kCameraHandleW;
    rc.top = expanded.top;
    rc.bottom = rc.top + kCameraHandleTitleH + kCameraHandleContentH;
    return rc;
}

CShareCameraPanel::TileFrame::TileFrame()
    : m_width(0)
    , m_height(0)
    , m_serial(0)
{
}

CShareCameraPanel::CShareCameraPanel()
    : m_source(NULL)
    , m_overflowCount(0)
{
}

void CShareCameraPanel::SetSource(IShareCameraFrames* source)
{
    m_source = source;
}

void CShareCameraPanel::SetTiles(const std::vector<ShareCameraTile>& tiles, int overflowCount)
{
    m_tiles = tiles;
    if (m_tiles.size() > static_cast<size_t>(kShareMaxCameraTiles))
        m_tiles.resize(static_cast<size_t>(kShareMaxCameraTiles));
    m_frames.assign(m_tiles.size(), TileFrame());
    m_overflowCount = overflowCount > 0 ? overflowCount : 0;
    Refresh();
    Invalidate();
}

bool CShareCameraPanel::HasContent() const
{
    return !m_tiles.empty() || m_overflowCount > 0;
}

void CShareCameraPanel::Refresh()
{
    if (m_source == NULL)
        return;
    bool changed = false;
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        TileFrame& f = m_frames[i];
        uint64_t serial = 0;
        std::vector<uint8_t> bgra;
        int w = 0;
        int h = 0;
        if (m_source->GetCameraFrame(m_tiles[i].m_seat, m_tiles[i].m_self, bgra, w, h, serial) && serial != f.m_serial)
        {
            f.m_bgra.swap(bgra);
            f.m_width = w;
            f.m_height = h;
            f.m_serial = serial;
            changed = true;
        }
    }
    if (changed)
        Invalidate();
}

void CShareCameraPanel::OnPaint(HDC hdc, const RECT& rcDirty)
{
    (void)rcDirty;
    const RECT rc = m_rcItem;
    HBRUSH bg = ::CreateSolidBrush(kPanelBg);
    ::FillRect(hdc, &rc, bg);
    ::DeleteObject(bg);

    std::vector<RECT> rects;
    ComputeCameraTileRects(rc, static_cast<int>(m_tiles.size()), m_overflowCount > 0 ? kOverflowLineH : 0, rects);
    HGDIOBJ oldFont = ::SelectObject(hdc, ::GetStockObject(DEFAULT_GUI_FONT));
    ::SetBkMode(hdc, TRANSPARENT);
    HBRUSH tileBg = ::CreateSolidBrush(kTileBg);
    for (size_t i = 0; i < rects.size() && i < m_tiles.size(); ++i)
    {
        const RECT& t = rects[i];
        ::FillRect(hdc, &t, tileBg);
        const TileFrame& f = m_frames[i];
        if (!f.m_bgra.empty() && f.m_width > 0 && f.m_height > 0)
        {
            //等比缩进小窗，留黑边（与观看窗共用一份等比放置，2026-10-14）
            const RECT fit = FitRectKeepAspect(t, f.m_width, f.m_height);
            BITMAPINFO bi;
            memset(&bi, 0, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = f.m_width;
            bi.bmiHeader.biHeight = -f.m_height;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = kBitsPerPixel32;
            bi.bmiHeader.biCompression = BI_RGB;
            ::SetStretchBltMode(hdc, HALFTONE);
            ::SetBrushOrgEx(hdc, 0, 0, NULL);
            ::StretchDIBits(hdc, fit.left, fit.top, fit.right - fit.left, fit.bottom - fit.top, 0, 0, f.m_width,
                            f.m_height, &f.m_bgra[0], &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        //名字条：小窗底部一条半透明黑底
        RECT bar = { t.left, t.bottom - kNameBarH, t.right, t.bottom };
        HDC memDc = ::CreateCompatibleDC(hdc);
        HBITMAP solid = ::CreateCompatibleBitmap(hdc, 1, 1);
        HGDIOBJ oldBmp = ::SelectObject(memDc, solid);
        ::SetPixel(memDc, 0, 0, kNameBarBg);
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, kNameBarAlpha, 0 };
        ::AlphaBlend(hdc, bar.left, bar.top, bar.right - bar.left, kNameBarH, memDc, 0, 0, 1, 1, blend);
        ::SelectObject(memDc, oldBmp);
        ::DeleteObject(solid);
        ::DeleteDC(memDc);
        RECT text = bar;
        text.left += kNamePadX;
        ::SetTextColor(hdc, kTextColor);
        ::DrawTextW(hdc, m_tiles[i].m_name.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    ::DeleteObject(tileBg);

    if (m_overflowCount > 0)
    {
        wchar_t line[64] = { 0 };
        swprintf_s(line, TR(L"另有 %d 人开着摄像头"), m_overflowCount);
        RECT text = { rc.left, rc.bottom - kOverflowLineH, rc.right, rc.bottom };
        ::SetTextColor(hdc, kOverflowText);
        ::DrawTextW(hdc, line, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    ::SelectObject(hdc, oldFont);
}

} // namespace screenshare
