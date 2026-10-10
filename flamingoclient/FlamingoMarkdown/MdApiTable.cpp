/**
 *  FlamingoMarkdown.dll 导出函数表的实现，说明见 MdApiTable.h。
 *
 *  每个导出函数的结构相同：先检查参数，再在 try 块里调用内部模块；std::bad_alloc 转成 md_out_of_memory，
 *  其它异常转成 md_internal_error 并写一条错误日志。异常不越过模块边界。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdApiTable.h"

#include <windows.h>
#include <new>
#include <string>
#include <vector>

#include "Core/MdAlert.h"
#include "Core/MdBuilder.h"
#include "Core/MdDocument.h"
#include "Core/MdDocx.h"
#include "Core/MdHostLog.h"
#include "Core/MdHtml.h"
#include "Core/MdLayout.h"
#include "Core/MdOutline.h"
#include "Core/MdPaginate.h"
#include "Core/MdPaint.h"
#include "Core/MdSelection.h"
#include "Core/MdText.h"

namespace flamingomd {

namespace {

//DLL 版本，写日志与排查问题用
const wchar_t* const kMdVersion = L"1.5.0 (md4c 0.6.0)";

//排版耗时超过它（毫秒）时写一条信息日志，便于发现超长消息拖慢界面
const DWORD kSlowLayoutMs = 50;

//初始化状态：只有第一次 m_init 生效
SRWLOCK g_initLock = SRWLOCK_INIT;
bool g_initDone = false;
int g_initResult = md_internal_error;

//把解析选项转成内部选项
void ToBuildOptions(const MdParseOptions* options, MdBuildOptions& out)
{
    out = MdBuildOptions();
    if (options == NULL || options->m_cbSize < kMdParseOptionsMinSize)
        return;
    if (options->m_imageLinkPrefix != NULL)
        out.m_imageLinkPrefix = options->m_imageLinkPrefix;
}

//把宽字符串写进调用方的缓冲区；缓冲区为 NULL 或不够大时返回 md_buffer_too_small
int CopyWideOut(const std::wstring& text, wchar_t* buf, uint32_t bufChars, uint32_t* outChars)
{
    *outChars = static_cast<uint32_t>(text.size());
    if (buf == NULL || bufChars < text.size() + 1)
        return md_buffer_too_small;
    if (!text.empty())
        memcpy(buf, text.c_str(), text.size() * sizeof(wchar_t));
    buf[text.size()] = 0;
    return md_ok;
}

//把窄字符串写进调用方的缓冲区；缓冲区为 NULL 或不够大时返回 md_buffer_too_small
int CopyNarrowOut(const std::string& text, char* buf, uint32_t bufBytes, uint32_t* outBytes)
{
    *outBytes = static_cast<uint32_t>(text.size());
    if (buf == NULL || bufBytes < text.size() + 1)
        return md_buffer_too_small;
    if (!text.empty())
        memcpy(buf, text.c_str(), text.size());
    buf[text.size()] = 0;
    return md_ok;
}

int __cdecl ApiInit(const MdHostInfo* host)
{
    if (host == NULL || host->m_cbSize < kMdHostInfoMinSize)
        return md_bad_argument;

    ::AcquireSRWLockExclusive(&g_initLock);
    if (g_initDone)
    {
        const int previous = g_initResult;
        ::ReleaseSRWLockExclusive(&g_initLock);
        return previous;
    }
    SetMdLogSink(host->m_log);
    int result = md_ok;
    try
    {
        if (!EnsureMdGdiplus())
            MdLogWarning("init: GDI+ unavailable, shapes will be skipped.");
        MdLogInfo("init: FlamingoMarkdown %ls ready.", kMdVersion);
    }
    catch (...)
    {
        result = md_internal_error;
    }
    g_initDone = true;
    g_initResult = result;
    ::ReleaseSRWLockExclusive(&g_initLock);
    return result;
}

const wchar_t* __cdecl ApiGetVersion()
{
    return kMdVersion;
}

int __cdecl ApiParse(const char* utf8, uint32_t bytes, const MdParseOptions* options, MdDocument** outDoc)
{
    if (outDoc == NULL)
        return md_bad_argument;
    *outDoc = NULL;
    if ((utf8 == NULL && bytes != 0) || bytes > kMdMaxSourceBytes)
        return md_bad_argument;

    MdDocument* doc = NULL;
    try
    {
        doc = new MdDocument();
        MdBuildOptions buildOptions;
        ToBuildOptions(options, buildOptions);
        const int rc = BuildMdModel(utf8, bytes, buildOptions, doc->m_model);
        if (rc != md_ok)
        {
            doc->m_magic = 0;
            delete doc;
            return rc;
        }
        *outDoc = doc;
        return md_ok;
    }
    catch (const std::bad_alloc&)
    {
        if (doc != NULL)
        {
            doc->m_magic = 0;
            delete doc;
        }
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("parse: unexpected exception.");
        if (doc != NULL)
        {
            doc->m_magic = 0;
            delete doc;
        }
        return md_internal_error;
    }
}

void __cdecl ApiFreeDoc(MdDocument* doc)
{
    if (doc == NULL)
        return;
    if (!IsValidMdDocument(doc))
    {
        MdLogWarning("free: pointer is not a document created by this module, ignored.");
        return;
    }
    doc->m_magic = 0;
    try
    {
        delete doc;
    }
    catch (...)
    {
        MdLogError("free: unexpected exception.");
    }
}

int __cdecl ApiGetDocInfo(const MdDocument* doc, MdDocInfo* out)
{
    if (!IsValidMdDocument(doc) || out == NULL || out->m_cbSize < kMdDocInfoMinSize)
        return md_bad_argument;
    const uint32_t size = out->m_cbSize;
    out->m_textLength = static_cast<int32_t>(doc->m_model.m_flatText.size());
    if (size >= offsetof(MdDocInfo, m_linkCount) + sizeof(int32_t))
        out->m_linkCount = static_cast<int32_t>(doc->m_model.m_links.size());
    if (size >= offsetof(MdDocInfo, m_codeBlockCount) + sizeof(int32_t))
        out->m_codeBlockCount = static_cast<int32_t>(doc->m_model.m_codeBlocks.size());
    if (size >= offsetof(MdDocInfo, m_truncated) + sizeof(int32_t))
        out->m_truncated = doc->m_model.m_truncated ? 1 : 0;
    return md_ok;
}

int __cdecl ApiLayout(MdDocument* doc, HDC hdc, const MdStyle* style, int maxWidth, int* outWidth, int* outHeight)
{
    if (!IsValidMdDocument(doc) || hdc == NULL || style == NULL || style->m_cbSize < kMdStyleMinSize
        || style->m_fontName == NULL || style->m_fontName[0] == 0 || maxWidth <= 0
        || outWidth == NULL || outHeight == NULL)
        return md_bad_argument;
    try
    {
        MdResolvedStyle resolved;
        ResolveMdStyle(*style, resolved);
        if (!doc->m_laidOut || doc->m_maxWidth != maxWidth || !(doc->m_style == resolved))
        {
            const DWORD start = ::GetTickCount();
            LayoutMdModel(doc->m_model, hdc, resolved, maxWidth, doc->m_layout);
            const DWORD elapsed = ::GetTickCount() - start;
            if (elapsed > kSlowLayoutMs)
                MdLogInfo("layout: %u chars, %u lines, width %d, took %u ms.",
                          static_cast<unsigned>(doc->m_model.m_flatText.size()),
                          static_cast<unsigned>(doc->m_layout.m_lines.size()), maxWidth,
                          static_cast<unsigned>(elapsed));
            doc->m_style = resolved;
            doc->m_maxWidth = maxWidth;
            doc->m_laidOut = true;
        }
        *outWidth = doc->m_layout.m_width;
        *outHeight = doc->m_layout.m_height;
        return md_ok;
    }
    catch (const std::bad_alloc&)
    {
        doc->m_laidOut = false;
        return md_out_of_memory;
    }
    catch (...)
    {
        doc->m_laidOut = false;
        MdLogError("layout: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiPaint(MdDocument* doc, HDC hdc, int x, int y, const MdPaintState* state)
{
    if (!IsValidMdDocument(doc) || hdc == NULL)
        return md_bad_argument;
    if (!doc->m_laidOut)
        return md_not_laid_out;
    try
    {
        MdPaintParams params;
        if (state != NULL && state->m_cbSize >= kMdPaintStateMinSize)
        {
            int a = state->m_selBegin;
            int b = state->m_selEnd;
            if (a >= 0 && b >= 0 && a != b)
            {
                params.m_selBegin = (a < b) ? a : b;
                params.m_selEnd = (a < b) ? b : a;
            }
            params.m_hoverLink = state->m_hoverLink;
            params.m_hoverCodeBlock = state->m_hoverCodeBlock;
            params.m_hoverCopyButton = state->m_hoverCopyButton;
            const RECT& c = state->m_clip;
            params.m_hasClip = !(c.left == 0 && c.top == 0 && c.right == 0 && c.bottom == 0);
            params.m_clip = c;
        }
        PaintMdLayout(doc->m_layout, doc->m_style, hdc, x, y, params);
        return md_ok;
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("paint: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiHitTest(const MdDocument* doc, int x, int y, MdHitResult* out)
{
    if (!IsValidMdDocument(doc) || out == NULL || out->m_cbSize < kMdHitResultMinSize)
        return md_bad_argument;
    if (!doc->m_laidOut)
        return md_not_laid_out;
    try
    {
        MdHitInfo hit;
        HitTestMdLayout(doc->m_layout, x, y, hit);
        const uint32_t size = out->m_cbSize;
        out->m_kind = hit.m_kind;
        out->m_charPos = hit.m_charPos;
        if (size >= offsetof(MdHitResult, m_linkIndex) + sizeof(int32_t))
            out->m_linkIndex = hit.m_linkIndex;
        if (size >= offsetof(MdHitResult, m_codeBlockIndex) + sizeof(int32_t))
            out->m_codeBlockIndex = hit.m_codeBlockIndex;
        return md_ok;
    }
    catch (...)
    {
        MdLogError("hittest: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiGetText(const MdDocument* doc, int32_t begin, int32_t end, wchar_t* buf, uint32_t bufChars,
                       uint32_t* outChars)
{
    if (!IsValidMdDocument(doc) || outChars == NULL)
        return md_bad_argument;
    try
    {
        return CopyWideOut(GetMdTextRange(doc->m_model, begin, end), buf, bufChars, outChars);
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("gettext: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiGetLinkUrl(const MdDocument* doc, int32_t index, wchar_t* buf, uint32_t bufChars, uint32_t* outChars)
{
    if (!IsValidMdDocument(doc) || outChars == NULL)
        return md_bad_argument;
    *outChars = 0;
    if (index < 0 || static_cast<size_t>(index) >= doc->m_model.m_links.size())
        return md_out_of_range;
    try
    {
        return CopyWideOut(doc->m_model.m_links[index].m_url, buf, bufChars, outChars);
    }
    catch (...)
    {
        return md_out_of_memory;
    }
}

int __cdecl ApiGetCodeBlockText(const MdDocument* doc, int32_t index, wchar_t* buf, uint32_t bufChars,
                                uint32_t* outChars)
{
    if (!IsValidMdDocument(doc) || outChars == NULL)
        return md_bad_argument;
    *outChars = 0;
    if (index < 0 || static_cast<size_t>(index) >= doc->m_model.m_codeBlocks.size())
        return md_out_of_range;
    try
    {
        const MdBlock& block = doc->m_model.m_blocks[doc->m_model.m_codeBlocks[index]];
        return CopyWideOut(ToCrLf(block.m_codeRaw), buf, bufChars, outChars);
    }
    catch (...)
    {
        return md_out_of_memory;
    }
}

int __cdecl ApiToPlainText(const char* utf8, uint32_t bytes, const MdParseOptions* options, char* buf,
                           uint32_t bufBytes, uint32_t* outBytes)
{
    if (outBytes == NULL)
        return md_bad_argument;
    *outBytes = 0;
    if ((utf8 == NULL && bytes != 0) || bytes > kMdMaxSourceBytes)
        return md_bad_argument;
    try
    {
        MdBuildOptions buildOptions;
        ToBuildOptions(options, buildOptions);
        MdModel model;
        const int rc = BuildMdModel(utf8, bytes, buildOptions, model);
        if (rc != md_ok)
            return rc;
        return CopyNarrowOut(WideToUtf8(model.m_flatText), buf, bufBytes, outBytes);
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("plaintext: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiGetCardText(const MdDocument* doc, int32_t part, wchar_t* buf, uint32_t bufChars, uint32_t* outChars)
{
    if (!IsValidMdDocument(doc) || outChars == NULL)
        return md_bad_argument;
    *outChars = 0;
    if (part != md_card_title && part != md_card_summary)
        return md_bad_argument;
    try
    {
        std::wstring title;
        std::wstring summary;
        BuildMdCardText(doc->m_model, title, summary);
        return CopyWideOut(part == md_card_title ? title : summary, buf, bufChars, outChars);
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("cardtext: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiExportHtml(const MdDocument* doc, const MdHtmlOptions* options, char* buf, uint32_t bufBytes,
                          uint32_t* outBytes)
{
    if (!IsValidMdDocument(doc) || outBytes == NULL)
        return md_bad_argument;
    *outBytes = 0;
    try
    {
        std::wstring title;
        if (options != NULL && options->m_cbSize >= kMdHtmlOptionsMinSize && options->m_title != NULL)
            title = options->m_title;
        return CopyNarrowOut(BuildMdHtml(doc->m_model, title), buf, bufBytes, outBytes);
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("exporthtml: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiPaginate(const MdDocument* doc, int32_t pageHeight, int32_t* starts, uint32_t capacity,
                        uint32_t* outCount)
{
    if (!IsValidMdDocument(doc) || outCount == NULL || pageHeight <= 0)
        return md_bad_argument;
    *outCount = 0;
    if (!doc->m_laidOut)
        return md_not_laid_out;
    try
    {
        std::vector<int> pages;
        PaginateMdLayout(doc->m_layout, pageHeight, pages);
        *outCount = static_cast<uint32_t>(pages.size());
        if (starts == NULL || capacity < pages.size())
            return md_buffer_too_small;
        for (size_t i = 0; i < pages.size(); ++i)
            starts[i] = pages[i];
        return md_ok;
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("paginate: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiExportDocx(const MdDocument* doc, unsigned char* buf, uint32_t bufBytes, uint32_t* outBytes)
{
    if (!IsValidMdDocument(doc) || outBytes == NULL)
        return md_bad_argument;
    *outBytes = 0;
    try
    {
        const std::string docx = BuildMdDocx(doc->m_model);
        *outBytes = static_cast<uint32_t>(docx.size());
        if (buf == NULL || bufBytes < docx.size())
            return md_buffer_too_small;
        if (!docx.empty())
            memcpy(buf, docx.data(), docx.size());
        return md_ok;
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("exportdocx: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiExportHtmlRange(const MdDocument* doc, const MdHtmlOptions* options, int32_t begin, int32_t end,
                               char* buf, uint32_t bufBytes, uint32_t* outBytes)
{
    if (!IsValidMdDocument(doc) || outBytes == NULL)
        return md_bad_argument;
    *outBytes = 0;
    try
    {
        std::wstring title;
        if (options != NULL && options->m_cbSize >= kMdHtmlOptionsMinSize && options->m_title != NULL)
            title = options->m_title;
        return CopyNarrowOut(BuildMdHtmlRange(doc->m_model, title, begin, end), buf, bufBytes, outBytes);
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("exporthtmlrange: unexpected exception.");
        return md_internal_error;
    }
}

int __cdecl ApiSetAlertTitles(const wchar_t* const* titles, uint32_t count)
{
    try
    {
        SetMdAlertTitles(titles, count);
        MdLogInfo("setalerttitles: %u title(s) set.", (titles != NULL) ? count : 0u);
        return md_ok;
    }
    catch (const std::bad_alloc&)
    {
        return md_out_of_memory;
    }
    catch (...)
    {
        MdLogError("setalerttitles: unexpected exception.");
        return md_internal_error;
    }
}

//函数表：字段顺序与 FlamingoMarkdownApi.h 的 MdApi 一致
const MdApi g_api =
{
    sizeof(MdApi),
    &ApiInit,
    &ApiGetVersion,
    &ApiParse,
    &ApiFreeDoc,
    &ApiGetDocInfo,
    &ApiLayout,
    &ApiPaint,
    &ApiHitTest,
    &ApiGetText,
    &ApiGetLinkUrl,
    &ApiGetCodeBlockText,
    &ApiToPlainText,
    &ApiGetCardText,
    &ApiExportHtml,
    &ApiPaginate,
    &ApiExportDocx,
    &ApiExportHtmlRange,
    &ApiSetAlertTitles,
};

} // namespace

const MdApi* GetMdApiTable()
{
    return &g_api;
}

} // namespace flamingomd
