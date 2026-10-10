/**
 *  MarkdownExport.cpp
 *
 *  Markdown 文档导出与文件读写的实现，说明见 MarkdownExport.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "MarkdownExport.h"

#include <commdlg.h>
#include <gdiplus.h>
#include <vector>

#include "MarkdownEditorModel.h"
#include "MarkdownPdf.h"
#include "../BalloonChatWnd/ChatMarkdown.h"              // MakePreviewMarkdownStyle / MarkdownTextStyleFromFont / MarkdownImageLinkPrefix
#include "../../Feature/Markdown/MarkdownDocument.h"     // CMarkdownDocument / MarkdownToPlainTextUtf8 / IsMarkdownAvailable
#include "../../Common/Util/FileDialogFilter.h"
#include "../../Common/I18n/I18n.h"
#include "../../Common/Log/IULog.h"

namespace balloonmd {

namespace {

//另存为对话框里文件名缓冲区的长度（字符）
const DWORD kPathBufferChars = 1024;

//32 位位图每个像素的字节数与位数
const int kBytesPerPixel = 4;
const WORD kBitsPerPixel = 32;

//图片的底色
const COLORREF kImageBack = RGB(0xFF, 0xFF, 0xFF);

//UTF-8 转 UTF-16
std::wstring WideFromUtf8(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], len);
    return out;
}

//把 \n 换成 \r\n（已经是 \r\n 的不重复转换）
std::wstring ToCrLf(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
            out += L'\r';
        out += text[i];
    }
    return out;
}

//把一段文字写进剪贴板（CF_UNICODETEXT）
bool PutUnicodeToClipboard(HWND owner, const std::wstring& text)
{
    if (!::OpenClipboard(owner))
        return false;
    ::EmptyClipboard();
    bool ok = false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem != NULL)
    {
        void* p = ::GlobalLock(mem);
        if (p != NULL)
        {
            memcpy(p, text.c_str(), bytes);
            ::GlobalUnlock(mem);
            ok = (::SetClipboardData(CF_UNICODETEXT, mem) != NULL);
        }
        if (!ok)
            ::GlobalFree(mem);
    }
    ::CloseClipboard();
    return ok;
}

//解析源文本并取卡片标题（导出 HTML 时写进 <title>）；取不到时用粗取的标题
std::wstring DocumentTitle(const std::string& sourceUtf8)
{
    markdown::CMarkdownDocument doc;
    std::wstring title;
    std::wstring summary;
    if (doc.Parse(sourceUtf8, balloonchatwnd::MarkdownImageLinkPrefix()) && doc.GetCardText(title, summary)
        && !title.empty())
        return title;
    return MarkdownTitleFromSource(sourceUtf8);
}

//导出 HTML：DLL 从模型生成
bool ExportHtmlFile(const std::wstring& path, const std::string& sourceUtf8)
{
    markdown::CMarkdownDocument doc;
    if (!doc.Parse(sourceUtf8, balloonchatwnd::MarkdownImageLinkPrefix()))
        return false;
    std::string html;
    if (!doc.ExportHtml(DocumentTitle(sourceUtf8), html))
        return false;
    return WriteWholeFile(path, html);
}

//查找 GDI+ 的 PNG 编码器（本文件也编进单元测试，不依赖 Common/Util/Utils）；找不到时返回 false
bool FindPngEncoder(CLSID& clsid)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<unsigned char> buf(bytes, 0);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (_wcsicmp(codecs[i].MimeType, L"image/png") == 0)
        {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//导出 PNG：按图片宽度画成位图后用 GDI+ 编码
bool ExportPngFile(const std::wstring& path, const std::string& sourceUtf8, std::wstring* error)
{
    SIZE size = { 0, 0 };
    bool tooTall = false;
    HBITMAP bmp = RenderMarkdownBitmap(sourceUtf8, kMarkdownImageContentWidth, size, &tooTall);
    if (bmp == NULL)
    {
        if (tooTall && error != NULL)
            *error = TR(L"文档太长，无法生成一张图片，请改为导出 PDF。");
        return false;
    }
    bool ok = false;
    {
        Gdiplus::Bitmap image(bmp, NULL);
        CLSID png;
        ok = FindPngEncoder(png) && (image.GetLastStatus() == Gdiplus::Ok)
             && (image.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
    }
    ::DeleteObject(bmp);
    LOG_INFO("markdown export: png %dx%d, ok=%d.", (int)size.cx, (int)size.cy, ok ? 1 : 0);
    return ok;
}

//把源文本里的 \n 换回编辑框口径再编码，与「保存」写出的 .md 完全相同
std::string MarkdownFileBytes(const std::string& sourceUtf8)
{
    return EncodeMarkdownForSave(MarkdownUtf8ToEditorText(sourceUtf8));
}

//格式对应的扩展名（不含点）
const wchar_t* ExtensionOf(int format)
{
    switch (format)
    {
    //Markdown 源文件
    case md_export_md:
        return L"md";
    //网页
    case md_export_html:
        return L"html";
    //图片
    case md_export_png:
        return L"png";
    //PDF
    case md_export_pdf:
        return L"pdf";
    //Word
    case md_export_docx:
        return L"docx";
    //不认识的格式
    default:
        return L"";
    }
}

//文件名里不能出现的字符换成下划线
std::wstring SanitizeFileName(const std::wstring& name)
{
    std::wstring out = name;
    const std::wstring bad = L"\\/:*?\"<>|";
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (bad.find(out[i]) != std::wstring::npos || out[i] < L' ')
            out[i] = L'_';
    }
    return out;
}

} // namespace

bool ReadWholeFile(const std::wstring& path, size_t maxBytes, std::string& bytes, bool* tooLarge)
{
    if (tooLarge != NULL)
        *tooLarge = false;
    bytes.clear();
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        LOG_WARNING("markdown file: open for read failed, err=%u.", ::GetLastError());
        return false;
    }
    LARGE_INTEGER size;
    size.QuadPart = 0;
    if (!::GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > maxBytes)
    {
        if (tooLarge != NULL && size.QuadPart > 0)
            *tooLarge = true;
        ::CloseHandle(file);
        LOG_WARNING("markdown file: too large or size unknown, bytes=%lld.", (long long)size.QuadPart);
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = true;
    if (!bytes.empty())
        ok = (::ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, NULL) != FALSE) && read == bytes.size();
    ::CloseHandle(file);
    if (!ok)
    {
        LOG_WARNING("markdown file: read failed, err=%u.", ::GetLastError());
        bytes.clear();
    }
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const std::string& bytes)
{
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        LOG_WARNING("markdown file: open for write failed, err=%u.", ::GetLastError());
        return false;
    }
    DWORD written = 0;
    bool ok = true;
    if (!bytes.empty())
        ok = (::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, NULL) != FALSE)
             && written == bytes.size();
    ::CloseHandle(file);
    if (!ok)
        LOG_WARNING("markdown file: write failed, err=%u.", ::GetLastError());
    return ok;
}

bool IsMarkdownExportAvailable(int format)
{
    switch (format)
    {
    //Markdown 源文件：不需要 DLL
    case md_export_md:
        return true;
    //网页、图片与 PDF：需要 DLL（网页与 PDF 还需要第二版接口，旧 DLL 时导出会失败并提示）
    case md_export_html:
    case md_export_png:
    case md_export_pdf:
        return markdown::IsMarkdownAvailable();
    //Word：需要 DLL 第三版接口
    case md_export_docx:
        return markdown::IsMarkdownAvailable() && markdown::CMarkdownDocument::CanExportDocx();
    //不认识的格式
    default:
        return false;
    }
}

HBITMAP RenderMarkdownBitmap(const std::string& sourceUtf8, int contentWidth, SIZE& outSize, bool* tooTall)
{
    outSize.cx = 0;
    outSize.cy = 0;
    if (tooTall != NULL)
        *tooTall = false;
    if (contentWidth <= 0)
        return NULL;
    markdown::CMarkdownDocument doc;
    if (!doc.Parse(sourceUtf8, balloonchatwnd::MarkdownImageLinkPrefix()))
        return NULL;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL)
        return NULL;
    int fontPx = 0;
    COLORREF color = 0;
    //与预览窗口、编辑器预览同一套默认字号与颜色
    balloonchatwnd::MarkdownTextStyleFromFont(net::TextFont(), fontPx, color);
    const flamingomd::MdStyle style = balloonchatwnd::MakePreviewMarkdownStyle(fontPx, color);
    SIZE docSize = { 0, 0 };
    if (!doc.Layout(mem, style, contentWidth, docSize))
    {
        ::DeleteDC(mem);
        return NULL;
    }
    const int width = contentWidth + kMarkdownImagePad * 2;
    const int height = docSize.cy + kMarkdownImagePad * 2;
    if (height > kMarkdownImageMaxHeight)
    {
        if (tooTall != NULL)
            *tooTall = true;
        ::DeleteDC(mem);
        LOG_WARNING("markdown image: document too tall, height=%d.", height);
        return NULL;
    }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;   //自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
    {
        ::DeleteDC(mem);
        return NULL;
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);
    RECT all = { 0, 0, width, height };
    HBRUSH back = ::CreateSolidBrush(kImageBack);
    ::FillRect(mem, &all, back);
    ::DeleteObject(back);
    flamingomd::MdPaintState state;
    memset(&state, 0, sizeof(state));
    state.m_cbSize = sizeof(state);
    state.m_selBegin = -1;
    state.m_selEnd = -1;
    state.m_hoverLink = -1;
    state.m_hoverCodeBlock = -1;
    state.m_hoverCopyButton = -1;
    state.m_clip = all;
    doc.Paint(mem, kMarkdownImagePad, kMarkdownImagePad, &state);
    ::GdiFlush();
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    outSize.cx = width;
    outSize.cy = height;
    return bmp;
}

bool CopyMarkdownSourceToClipboard(HWND owner, const std::string& sourceUtf8)
{
    return PutUnicodeToClipboard(owner, ToCrLf(WideFromUtf8(sourceUtf8)));
}

bool CopyMarkdownPlainTextToClipboard(HWND owner, const std::string& sourceUtf8)
{
    const std::string plain = markdown::MarkdownToPlainTextUtf8(sourceUtf8, balloonchatwnd::MarkdownImageLinkPrefix());
    return PutUnicodeToClipboard(owner, ToCrLf(WideFromUtf8(plain)));
}

bool CopyMarkdownImageToClipboard(HWND owner, const std::string& sourceUtf8, std::wstring* error)
{
    SIZE size = { 0, 0 };
    bool tooTall = false;
    HBITMAP bmp = RenderMarkdownBitmap(sourceUtf8, kMarkdownImageContentWidth, size, &tooTall);
    if (bmp == NULL)
    {
        if (error != NULL)
            *error = tooTall ? std::wstring(TR(L"文档太长，无法生成一张图片。"))
                             : std::wstring(TR(L"生成图片失败。"));
        return false;
    }
    //CF_DIB：信息头加自下而上的像素行
    DIBSECTION ds;
    memset(&ds, 0, sizeof(ds));
    ::GetObject(bmp, sizeof(ds), &ds);
    const size_t stride = static_cast<size_t>(size.cx) * kBytesPerPixel;
    const size_t pixelBytes = stride * static_cast<size_t>(size.cy);
    bool ok = false;
    HGLOBAL global = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixelBytes);
    if (global != NULL && ds.dsBm.bmBits != NULL)
    {
        unsigned char* p = static_cast<unsigned char*>(::GlobalLock(global));
        if (p != NULL)
        {
            BITMAPINFOHEADER header;
            memset(&header, 0, sizeof(header));
            header.biSize = sizeof(header);
            header.biWidth = size.cx;
            header.biHeight = size.cy;   //自下而上
            header.biPlanes = 1;
            header.biBitCount = kBitsPerPixel;
            header.biCompression = BI_RGB;
            header.biSizeImage = static_cast<DWORD>(pixelBytes);
            memcpy(p, &header, sizeof(header));
            const unsigned char* src = static_cast<const unsigned char*>(ds.dsBm.bmBits);
            unsigned char* dst = p + sizeof(header);
            for (int row = 0; row < size.cy; ++row)
                memcpy(dst + stride * row, src + stride * (size.cy - 1 - row), stride);
            ::GlobalUnlock(global);
            if (::OpenClipboard(owner))
            {
                ::EmptyClipboard();
                ok = (::SetClipboardData(CF_DIB, global) != NULL);
                ::CloseClipboard();
            }
        }
    }
    if (!ok && global != NULL)
        ::GlobalFree(global);
    ::DeleteObject(bmp);
    if (!ok && error != NULL)
        *error = TR(L"写入剪贴板失败，请稍后重试。");
    LOG_INFO("markdown copy as image: %dx%d, ok=%d.", (int)size.cx, (int)size.cy, ok ? 1 : 0);
    return ok;
}

std::wstring MarkdownExportMenuText(int format)
{
    switch (format)
    {
    //Markdown 源文件
    case md_export_md:
        return TR(L"Markdown 源文件（.md）");
    //网页
    case md_export_html:
        return TR(L"网页（.html）");
    //图片
    case md_export_png:
        return TR(L"图片（.png）");
    //PDF
    case md_export_pdf:
        return TR(L"PDF（.pdf）");
    //Word
    case md_export_docx:
        return TR(L"Word 文档（.docx）");
    //不认识的格式
    default:
        return std::wstring();
    }
}

bool ExportMarkdownToFile(const std::wstring& path, const std::string& sourceUtf8, int format, std::wstring* error)
{
    bool ok = false;
    switch (format)
    {
    //Markdown 源文件：与「保存」写出的内容相同
    case md_export_md:
        ok = WriteWholeFile(path, MarkdownFileBytes(sourceUtf8));
        break;
    //网页：DLL 从文档模型生成，样式内嵌
    case md_export_html:
        ok = ExportHtmlFile(path, sourceUtf8);
        break;
    //图片：画成一张长图后编码为 PNG
    case md_export_png:
        ok = ExportPngFile(path, sourceUtf8, error);
        break;
    //Word：DLL 从文档模型生成 .docx
    case md_export_docx:
    {
        markdown::CMarkdownDocument doc;
        std::string docx;
        ok = doc.Parse(sourceUtf8, balloonchatwnd::MarkdownImageLinkPrefix()) && doc.ExportDocx(docx)
             && WriteWholeFile(path, docx);
        break;
    }
    //PDF：优先用系统 PDF 打印机输出矢量 PDF，打印机不存在时改为图片式 PDF
    case md_export_pdf:
        ok = IsPdfPrinterAvailable() ? ExportMarkdownPdfViaPrinter(path, sourceUtf8, error)
                                     : ExportMarkdownPdfAsImages(path, sourceUtf8, error);
        break;
    //其余格式在后续步骤实现
    default:
        ok = false;
        break;
    }
    if (!ok && error != NULL && error->empty())
        *error = TR(L"导出失败，请检查文件是否被占用或目录是否可写。");
    LOG_INFO("markdown export: format=%d, ok=%d, bytes=%u.", format, ok ? 1 : 0, (unsigned)sourceUtf8.size());
    return ok;
}

bool ExportMarkdownWithDialog(HWND owner, const std::string& sourceUtf8, const std::wstring& suggestedName, int format,
                              std::wstring* error)
{
    if (error != NULL)
        error->clear();
    const std::wstring ext = ExtensionOf(format);
    if (ext.empty())
        return false;
    //没有系统 PDF 打印机时只能导出图片式 PDF：先说明文字将不可选中，用户同意再继续
    if (format == md_export_pdf && !IsPdfPrinterAvailable())
    {
        const wchar_t* const tip = TR(L"系统里没有「Microsoft Print to PDF」打印机，将把每一页导出为图片，PDF 里的文字不能选中和搜索。要继续吗？");
        const int choice = ::MessageBoxW(owner, tip, TR(L"导出 PDF"), MB_OKCANCEL | MB_ICONINFORMATION);
        if (choice != IDOK)
            return false;
    }
    std::wstring name = SanitizeFileName(suggestedName.empty() ? std::wstring(TR(L"未命名")) : suggestedName);
    name += L".";
    name += ext;

    std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
    wcsncpy_s(&buffer[0], buffer.size(), name.c_str(), _TRUNCATE);
    const std::wstring pattern = L"*." + ext;
    //过滤串中间夹着 \0，说明文字各自翻译后再拼（UI 约定第 8 条）；返回值须活到对话框关闭
    const std::wstring filter = filedlg::MakeFilter(MarkdownExportMenuText(format).c_str(), pattern.c_str(),
                                                    TR(L"所有文件"), L"*.*");
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = &buffer[0];
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrDefExt = ext.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!::GetSaveFileNameW(&ofn))
    {
        LOG_INFO("markdown export: save dialog cancelled, format=%d.", format);
        return false;
    }
    return ExportMarkdownToFile(std::wstring(&buffer[0]), sourceUtf8, format, error);
}

} // namespace balloonmd
