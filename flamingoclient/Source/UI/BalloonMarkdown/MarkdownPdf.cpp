/**
 *  MarkdownPdf.cpp
 *
 *  Markdown 文档导出为 PDF 的实现，说明见 MarkdownPdf.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "MarkdownPdf.h"

#include <stdio.h>
#include <algorithm>

#include "MarkdownExport.h"                              // WriteWholeFile
#include "../BalloonChatWnd/ChatMarkdown.h"              // MakePreviewMarkdownStyle / MarkdownTextStyleFromFont
#include "../../Feature/Markdown/MarkdownDocument.h"
#include "../../third-party/zlib1.2.11/zlib.h"
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"

namespace balloonmd {

const wchar_t* const kPdfPrinterName = L"Microsoft Print to PDF";

namespace {

//屏幕的基准 DPI：排版样式里的字号与缩放都以它为 100%
const int kBaseDpi = 96;
//一英寸的点数与十分之一毫米数（换算页面尺寸与页边距）
const int kPointsPerInch = 72;
const int kTenthMmPerInch = 254;
const int kTenthMmPerMm = 10;
//百分比的基数
const int kPercent = 100;

//图片的底色与每个像素的字节数
const COLORREF kPageBack = RGB(0xFF, 0xFF, 0xFF);
const int kBgraBytes = 4;
const int kRgbBytes = 3;
const int kBitsPerByte = 8;

//打印任务结束后等待 PDF 文件出现的时长：每次间隔与最多次数（共约 10 秒）
const DWORD kWaitStepMs = 100;
const int kWaitSteps = 100;

/**
 *  按页面尺寸排版好、切好页的文档。
 */
struct PagePlan
{
    markdown::CMarkdownDocument m_doc;      //文档
    int                         m_margin;   //页边距（设备像素）
    int                         m_contentW; //内容区宽（设备像素）
    int                         m_contentH; //内容区高（设备像素）
    std::vector<int>            m_starts;   //各页在文档里的起始纵坐标

    PagePlan()
        : m_margin(0), m_contentW(0), m_contentH(0)
    {
    }
};

/**
 *  按 dpi 与页面尺寸排版文档并切页。
 *  @param hdc        排版用的设备上下文（打印机 DC 或内存 DC）
 *  @param sourceUtf8 源文本
 *  @param dpi        设备分辨率
 *  @param pageW      页面宽（设备像素）
 *  @param pageH      页面高（设备像素）
 *  @param plan       [出参] 排版与切页结果
 *  @return 成功返回 true
 */
bool PreparePages(HDC hdc, const std::string& sourceUtf8, int dpi, int pageW, int pageH, PagePlan& plan)
{
    if (!plan.m_doc.Parse(sourceUtf8, balloonchatwnd::MarkdownImageLinkPrefix()))
        return false;
    plan.m_margin = ::MulDiv(kPdfMarginMm * kTenthMmPerMm, dpi, kTenthMmPerInch);
    plan.m_contentW = pageW - plan.m_margin * 2;
    plan.m_contentH = pageH - plan.m_margin * 2;
    if (plan.m_contentW <= 0 || plan.m_contentH <= 0)
        return false;
    int fontPx = 0;
    COLORREF color = 0;
    //与预览同一套字号与颜色，按设备分辨率放大
    balloonchatwnd::MarkdownTextStyleFromFont(net::TextFont(), fontPx, color);
    flamingomd::MdStyle style = balloonchatwnd::MakePreviewMarkdownStyle(::MulDiv(fontPx, dpi, kBaseDpi), color);
    style.m_scalePercent = ::MulDiv(kPercent, dpi, kBaseDpi);
    SIZE size = { 0, 0 };
    if (!plan.m_doc.Layout(hdc, style, plan.m_contentW, size))
        return false;
    return plan.m_doc.Paginate(plan.m_contentH, plan.m_starts) && !plan.m_starts.empty();
}

//把第 index 页画到 hdc 上：文档向上平移到该页的起点，裁剪到内容区
void PaintPage(HDC hdc, PagePlan& plan, size_t index)
{
    const int start = plan.m_starts[index];
    const int saved = ::SaveDC(hdc);
    RECT content = { plan.m_margin, plan.m_margin, plan.m_margin + plan.m_contentW, plan.m_margin + plan.m_contentH };
    //下一页的起点之后的内容不画在这一页（切在行与行之间，裁到下一页起点即可）
    if (index + 1 < plan.m_starts.size())
        content.bottom = (std::min)(content.bottom, (LONG)(plan.m_margin + plan.m_starts[index + 1] - start));
    ::IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    flamingomd::MdPaintState state;
    memset(&state, 0, sizeof(state));
    state.m_cbSize = sizeof(state);
    state.m_selBegin = -1;
    state.m_selEnd = -1;
    state.m_hoverLink = -1;
    state.m_hoverCodeBlock = -1;
    state.m_hoverCopyButton = -1;
    state.m_clip = content;
    plan.m_doc.Paint(hdc, plan.m_margin, plan.m_margin - start, &state);
    ::RestoreDC(hdc, saved);
}

//等待文件出现且大小大于 0（打印服务在后台写文件）
bool WaitForFile(const std::wstring& path)
{
    for (int i = 0; i < kWaitSteps; ++i)
    {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)
            && (data.nFileSizeLow > 0 || data.nFileSizeHigh > 0))
            return true;
        ::Sleep(kWaitStepMs);
    }
    return false;
}

//往 PDF 里追加一段文字
void Append(std::string& out, const char* text)
{
    out += text;
}

//往 PDF 里追加格式化文字
void AppendF(std::string& out, const char* format, unsigned a, unsigned b = 0, unsigned c = 0)
{
    char buf[256] = { 0 };
    _snprintf_s(buf, _TRUNCATE, format, a, b, c);
    out += buf;
}

} // namespace

bool IsPdfPrinterAvailable()
{
    HDC dc = ::CreateDCW(L"WINSPOOL", kPdfPrinterName, NULL, NULL);
    if (dc == NULL)
        return false;
    ::DeleteDC(dc);
    return true;
}

bool ExportMarkdownPdfViaPrinter(const std::wstring& path, const std::string& sourceUtf8, std::wstring* error)
{
    HDC dc = ::CreateDCW(L"WINSPOOL", kPdfPrinterName, NULL, NULL);
    if (dc == NULL)
    {
        if (error != NULL)
            *error = TR(L"找不到系统的 PDF 打印机。");
        return false;
    }
    const int dpi = ::GetDeviceCaps(dc, LOGPIXELSY);
    const int pageW = ::GetDeviceCaps(dc, HORZRES);
    const int pageH = ::GetDeviceCaps(dc, VERTRES);
    PagePlan plan;
    if (dpi <= 0 || !PreparePages(dc, sourceUtf8, dpi, pageW, pageH, plan))
    {
        ::DeleteDC(dc);
        if (error != NULL)
            *error = TR(L"文档排版失败，无法导出 PDF。");
        return false;
    }
    //先删掉同名旧文件，结束后据文件是否出现判断打印服务是否写完
    ::DeleteFileW(path.c_str());
    DOCINFOW di;
    memset(&di, 0, sizeof(di));
    di.cbSize = sizeof(di);
    di.lpszDocName = L"Markdown";
    di.lpszOutput = path.c_str();
    bool ok = (::StartDocW(dc, &di) > 0);
    for (size_t i = 0; ok && i < plan.m_starts.size(); ++i)
    {
        ok = (::StartPage(dc) > 0);
        if (ok)
        {
            PaintPage(dc, plan, i);
            ok = (::EndPage(dc) > 0);
        }
    }
    if (ok)
        ok = (::EndDoc(dc) > 0);
    else
        ::AbortDoc(dc);
    ::DeleteDC(dc);
    if (ok)
        ok = WaitForFile(path);
    if (!ok && error != NULL)
        *error = TR(L"通过系统 PDF 打印机导出失败。");
    LOG_INFO("markdown pdf via printer: dpi=%d, pages=%u, ok=%d.", dpi, (unsigned)plan.m_starts.size(), ok ? 1 : 0);
    return ok;
}

bool ExportMarkdownPdfAsImages(const std::wstring& path, const std::string& sourceUtf8, std::wstring* error)
{
    const int dpi = kPdfImageDpi;
    const int pageW = ::MulDiv(kPdfPageWidthPt, dpi, kPointsPerInch);
    const int pageH = ::MulDiv(kPdfPageHeightPt, dpi, kPointsPerInch);
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL)
        return false;
    PagePlan plan;
    if (!PreparePages(mem, sourceUtf8, dpi, pageW, pageH, plan))
    {
        ::DeleteDC(mem);
        if (error != NULL)
            *error = TR(L"文档排版失败，无法导出 PDF。");
        return false;
    }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = pageW;
    bi.bmiHeader.biHeight = -pageH;   //自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBgraBytes * kBitsPerByte;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(mem);
        return false;
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);
    std::vector<PdfPageImage> pages;
    pages.reserve(plan.m_starts.size());
    for (size_t i = 0; i < plan.m_starts.size(); ++i)
    {
        RECT all = { 0, 0, pageW, pageH };
        HBRUSH back = ::CreateSolidBrush(kPageBack);
        ::FillRect(mem, &all, back);
        ::DeleteObject(back);
        PaintPage(mem, plan, i);
        ::GdiFlush();
        //BGRA → RGB
        PdfPageImage page;
        page.m_width = pageW;
        page.m_height = pageH;
        page.m_rgb.resize(static_cast<size_t>(pageW) * pageH * kRgbBytes);
        const unsigned char* src = static_cast<const unsigned char*>(bits);
        unsigned char* dst = &page.m_rgb[0];
        const size_t count = static_cast<size_t>(pageW) * pageH;
        for (size_t p = 0; p < count; ++p)
        {
            dst[p * kRgbBytes + 0] = src[p * kBgraBytes + 2];
            dst[p * kRgbBytes + 1] = src[p * kBgraBytes + 1];
            dst[p * kRgbBytes + 2] = src[p * kBgraBytes + 0];
        }
        pages.push_back(page);
    }
    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    std::string pdf;
    const bool ok = BuildImagePdf(pages, pdf) && WriteWholeFile(path, pdf);
    if (!ok && error != NULL)
        *error = TR(L"导出 PDF 失败，请检查文件是否被占用或目录是否可写。");
    LOG_INFO("markdown pdf as images: pages=%u, bytes=%u, ok=%d.", (unsigned)pages.size(), (unsigned)pdf.size(), ok ? 1 : 0);
    return ok;
}

bool BuildImagePdf(const std::vector<PdfPageImage>& pages, std::string& out)
{
    out.clear();
    if (pages.empty())
        return false;
    //对象编号：1 目录、2 页面树，第 i 页依次占 3 + 3i（页面）、4 + 3i（内容流）、5 + 3i（图片）
    const unsigned pageCount = static_cast<unsigned>(pages.size());
    const unsigned objectCount = 2 + pageCount * 3;
    std::vector<size_t> offsets(objectCount + 1, 0);

    Append(out, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
    offsets[1] = out.size();
    Append(out, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    offsets[2] = out.size();
    Append(out, "2 0 obj\n<< /Type /Pages /Kids [");
    for (unsigned i = 0; i < pageCount; ++i)
        AppendF(out, "%u 0 R ", 3 + i * 3);
    AppendF(out, "] /Count %u >>\nendobj\n", pageCount);

    for (unsigned i = 0; i < pageCount; ++i)
    {
        const PdfPageImage& page = pages[i];
        const size_t rawBytes = static_cast<size_t>(page.m_width) * page.m_height * kRgbBytes;
        if (page.m_width <= 0 || page.m_height <= 0 || page.m_rgb.size() != rawBytes)
            return false;
        const unsigned pageObj = 3 + i * 3;
        //页面：A4，内容流把图片铺满整页
        offsets[pageObj] = out.size();
        AppendF(out, "%u 0 obj\n<< /Type /Page /Parent 2 0 R ", pageObj);
        AppendF(out, "/MediaBox [0 0 %u %u] ", kPdfPageWidthPt, kPdfPageHeightPt);
        AppendF(out, "/Resources << /XObject << /Im0 %u 0 R >> >> /Contents %u 0 R >>\nendobj\n", pageObj + 2, pageObj + 1);
        char content[128] = { 0 };
        _snprintf_s(content, _TRUNCATE, "q %d 0 0 %d 0 0 cm /Im0 Do Q", kPdfPageWidthPt, kPdfPageHeightPt);
        const unsigned contentLen = static_cast<unsigned>(strlen(content));
        offsets[pageObj + 1] = out.size();
        AppendF(out, "%u 0 obj\n<< /Length %u >>\nstream\n", pageObj + 1, contentLen);
        Append(out, content);
        Append(out, "\nendstream\nendobj\n");
        //图片：RGB 8 位，FlateDecode 压缩
        uLongf packedBytes = compressBound(static_cast<uLong>(rawBytes));
        std::vector<unsigned char> packed(packedBytes, 0);
        if (compress2(&packed[0], &packedBytes, &page.m_rgb[0], static_cast<uLong>(rawBytes), Z_DEFAULT_COMPRESSION) != Z_OK)
            return false;
        offsets[pageObj + 2] = out.size();
        AppendF(out, "%u 0 obj\n<< /Type /XObject /Subtype /Image /Width %u /Height %u ", pageObj + 2,
                static_cast<unsigned>(page.m_width), static_cast<unsigned>(page.m_height));
        AppendF(out, "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length %u >>\nstream\n",
                static_cast<unsigned>(packedBytes));
        out.append(reinterpret_cast<const char*>(&packed[0]), packedBytes);
        Append(out, "\nendstream\nendobj\n");
    }

    //交叉引用表：每项固定 20 字节
    const size_t xref = out.size();
    AppendF(out, "xref\n0 %u\n", objectCount + 1);
    Append(out, "0000000000 65535 f \n");
    for (unsigned k = 1; k <= objectCount; ++k)
        AppendF(out, "%010u 00000 n \n", static_cast<unsigned>(offsets[k]));
    AppendF(out, "trailer\n<< /Size %u /Root 1 0 R >>\nstartxref\n%u\n%%%%EOF\n", objectCount + 1,
            static_cast<unsigned>(xref));
    return true;
}

} // namespace balloonmd
