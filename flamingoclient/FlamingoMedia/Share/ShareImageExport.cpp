/**
 *  标注另存为图片，见 ShareImageExport.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareImageExport.h"

#include <objidl.h>        // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include "ShareAnnotationRender.h"   // RenderAnnotationLayer
#include "ShareWhiteboard.h"         // 背景种类、颜色与线距参照
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//每像素字节数（BGRA）
const int kBytesPerPixel = 4;
//不透明
const int kOpaque = 255;
//背景线的最小间距（像素）：图片很小时也不至于密成一片
const int kMinLineSpacingPx = 4;
//四舍五入加的半
const double kRoundHalf = 0.5;
//默认文件名里时间部分的最大字符数
const size_t kTimePartChars = 32;

//GDI+ 在本文件惰性初始化一次（进程内多次 GdiplusStartup 是安全的，进程退出时由系统回收）
void ensureGdiplus()
{
    static ULONG_PTR token = 0;
    if (token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
}

//取 PNG 编码器的 CLSID
bool pngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<uint8_t> buffer(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buffer[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//把预乘 alpha 的图层叠到不透明的底图上（大小相同），结果仍不透明
void blendLayer(const std::vector<uint8_t>& layer, std::vector<uint8_t>& base)
{
    const size_t n = (std::min)(layer.size(), base.size());
    for (size_t i = 0; i + kBytesPerPixel <= n; i += kBytesPerPixel)
    {
        const int a = layer[i + 3];
        if (a == 0)
            continue;
        for (int c = 0; c < 3; ++c)
            base[i + c] = static_cast<uint8_t>(layer[i + c] + base[i + c] * (kOpaque - a) / kOpaque);
        base[i + 3] = static_cast<uint8_t>(kOpaque);
    }
}

//在底图上填一块矩形（不透明）
void fillRect(std::vector<uint8_t>& img, int width, int x0, int y0, int x1, int y1, COLORREF color)
{
    for (int y = y0; y < y1; ++y)
    {
        uint8_t* row = &img[static_cast<size_t>(y) * width * kBytesPerPixel];
        for (int x = x0; x < x1; ++x)
        {
            uint8_t* px = row + static_cast<size_t>(x) * kBytesPerPixel;
            px[0] = GetBValue(color);
            px[1] = GetGValue(color);
            px[2] = GetRValue(color);
            px[3] = static_cast<uint8_t>(kOpaque);
        }
    }
}

//导出时不画激光笔：它只是临时的指示，过一会儿就淡出消失
std::vector<AnnotationStroke> withoutLaser(const std::vector<AnnotationStroke>& strokes)
{
    std::vector<AnnotationStroke> out;
    for (size_t i = 0; i < strokes.size(); ++i)
    {
        if (strokes[i].m_tool != annot_tool_laser)
            out.push_back(strokes[i]);
    }
    return out;
}

//把笔迹画成图层叠到底图上
void drawStrokes(std::vector<uint8_t>& base, int width, int height, const std::vector<AnnotationStroke>& strokes)
{
    const std::vector<AnnotationStroke> kept = withoutLaser(strokes);
    if (kept.empty())
        return;
    std::vector<uint8_t> layer;
    RenderAnnotationLayer(width, height, kept, NULL, 0, true, layer);
    blendLayer(layer, base);
}

} // namespace

void RenderBoardImage(int width, int height, int background, const std::vector<AnnotationStroke>& strokes,
                      std::vector<uint8_t>& bgra)
{
    bgra.clear();
    if (width <= 0 || height <= 0)
        return;
    //白底
    bgra.assign(static_cast<size_t>(width) * height * kBytesPerPixel, static_cast<uint8_t>(kOpaque));
    //背景线：间距按世界缩放，与屏幕上看到的一致（见 ShareWhiteboardView::BackgroundLines）
    if (background == wb_bg_grid || background == wb_bg_lines)
    {
        const int basePx = (background == wb_bg_grid) ? kWhiteboardGridPx : kWhiteboardLinePx;
        const int spacing = (std::max)(kMinLineSpacingPx,
                                       static_cast<int>(floor(static_cast<double>(basePx) * width /
                                                              kWhiteboardSharedRefWorldW + kRoundHalf)));
        const COLORREF color = (background == wb_bg_grid) ? kWhiteboardGridColor : kWhiteboardLineColor;
        for (int y = spacing; y < height; y += spacing)
            fillRect(bgra, width, 0, y, width, y + 1, color);
        if (background == wb_bg_grid)
        {
            for (int x = spacing; x < width; x += spacing)
                fillRect(bgra, width, x, 0, x + 1, height, color);
        }
    }
    drawStrokes(bgra, width, height, strokes);
}

void RenderScreenImage(const std::vector<uint8_t>& frame, int width, int height,
                       const std::vector<AnnotationStroke>& strokes, std::vector<uint8_t>& out)
{
    out.clear();
    const size_t bytes = static_cast<size_t>(width) * height * kBytesPerPixel;
    if (width <= 0 || height <= 0 || frame.size() != bytes)
        return;
    out = frame;
    //采到的画面不一定带有效的透明通道：一律按不透明处理
    for (size_t i = 3; i < out.size(); i += kBytesPerPixel)
        out[i] = static_cast<uint8_t>(kOpaque);
    drawStrokes(out, width, height, strokes);
}

bool SaveBgraAsPng(const std::wstring& path, const std::vector<uint8_t>& bgra, int width, int height)
{
    if (path.empty() || width <= 0 || height <= 0 ||
        bgra.size() != static_cast<size_t>(width) * height * kBytesPerPixel)
        return false;
    ensureGdiplus();
    CLSID clsid;
    if (!pngEncoder(clsid))
        return false;
    //按 32 位 RGB 交给 GDI+：透明通道不参与，存出来是不透明的
    Gdiplus::Bitmap bmp(width, height, width * kBytesPerPixel, PixelFormat32bppRGB,
                        const_cast<BYTE*>(&bgra[0]));
    if (bmp.GetLastStatus() != Gdiplus::Ok)
        return false;
    return bmp.Save(path.c_str(), &clsid, NULL) == Gdiplus::Ok;
}

std::wstring MakeAnnotExportFileName(bool whiteboard, const SYSTEMTIME& t)
{
    wchar_t timePart[kTimePartChars] = { 0 };
    swprintf_s(timePart, kTimePartChars, L"_%04u%02u%02u_%02u%02u%02u.png", static_cast<unsigned>(t.wYear),
               static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay), static_cast<unsigned>(t.wHour),
               static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond));
    return std::wstring(whiteboard ? TR(L"白板") : TR(L"屏幕标注")) + timePart;
}

} // namespace screenshare
