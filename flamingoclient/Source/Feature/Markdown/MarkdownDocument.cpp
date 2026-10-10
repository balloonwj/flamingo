/**
 *  Markdown 文档包装的实现，说明见 MarkdownDocument.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MarkdownDocument.h"

#include <string.h>
#include <vector>

#include "MarkdownModuleHost.h"
#include "../../Common/Log/IULog.h"

namespace markdown {

namespace {

//测试替换：是否启用、替换用的函数表。只在单元测试里改，界面线程读取
bool g_overrideEnabled = false;
const flamingomd::MdApi* g_overrideApi = NULL;

//填好解析选项
void FillParseOptions(const std::wstring& imagePrefix, flamingomd::MdParseOptions& options)
{
    memset(&options, 0, sizeof(options));
    options.m_cbSize = sizeof(options);
    options.m_imageLinkPrefix = imagePrefix.empty() ? NULL : imagePrefix.c_str();
    options.m_flags = 0;
}

} // namespace

const flamingomd::MdApi* ActiveMarkdownApi()
{
    if (g_overrideEnabled)
        return g_overrideApi;
    return CMarkdownModuleHost::GetInstance().GetApi();
}

bool IsMarkdownAvailable()
{
    return ActiveMarkdownApi() != NULL;
}

void SetMarkdownApiOverrideForTest(bool enabled, const flamingomd::MdApi* api)
{
    g_overrideEnabled = enabled;
    g_overrideApi = enabled ? api : NULL;
}

std::string MarkdownToPlainTextUtf8(const std::string& utf8Source, const std::wstring& imagePrefix)
{
    const flamingomd::MdApi* api = ActiveMarkdownApi();
    if (api == NULL || utf8Source.size() > flamingomd::kMdMaxSourceBytes)
        return utf8Source;

    flamingomd::MdParseOptions options;
    FillParseOptions(imagePrefix, options);
    uint32_t needed = 0;
    const uint32_t bytes = static_cast<uint32_t>(utf8Source.size());
    int rc = api->m_toPlainText(utf8Source.data(), bytes, &options, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small && rc != flamingomd::md_ok)
        return utf8Source;
    std::vector<char> buf(static_cast<size_t>(needed) + 1, 0);
    rc = api->m_toPlainText(utf8Source.data(), bytes, &options, &buf[0], needed + 1, &needed);
    if (rc != flamingomd::md_ok)
        return utf8Source;
    return std::string(&buf[0], needed);
}

CMarkdownDocument::CMarkdownDocument()
    : m_api(NULL)
    , m_doc(NULL)
    , m_laidOut(false)
{
}

CMarkdownDocument::~CMarkdownDocument()
{
    Reset();
}

bool CMarkdownDocument::Parse(const std::string& utf8Source, const std::wstring& imagePrefix)
{
    Reset();
    const flamingomd::MdApi* api = ActiveMarkdownApi();
    if (api == NULL || utf8Source.size() > flamingomd::kMdMaxSourceBytes)
        return false;

    flamingomd::MdParseOptions options;
    FillParseOptions(imagePrefix, options);
    flamingomd::MdDocument* doc = NULL;
    const int rc = api->m_parse(utf8Source.data(), static_cast<uint32_t>(utf8Source.size()), &options, &doc);
    if (rc != flamingomd::md_ok || doc == NULL)
    {
        LOG_WARNING("markdown parse failed with code %d (%u bytes), shown as plain text.", rc,
                    static_cast<unsigned>(utf8Source.size()));
        return false;
    }
    m_api = api;
    m_doc = doc;
    return true;
}

void CMarkdownDocument::Reset()
{
    if (m_doc != NULL && m_api != NULL)
        m_api->m_freeDoc(m_doc);
    m_doc = NULL;
    m_api = NULL;
    m_laidOut = false;
}

bool CMarkdownDocument::IsParsed() const
{
    return m_doc != NULL;
}

bool CMarkdownDocument::Layout(HDC hdc, const flamingomd::MdStyle& style, int maxWidth, SIZE& outSize)
{
    outSize.cx = 0;
    outSize.cy = 0;
    if (m_doc == NULL || hdc == NULL || maxWidth <= 0)
        return false;
    int w = 0;
    int h = 0;
    const int rc = m_api->m_layout(m_doc, hdc, &style, maxWidth, &w, &h);
    if (rc != flamingomd::md_ok)
    {
        LOG_WARNING("markdown layout failed with code %d.", rc);
        m_laidOut = false;
        return false;
    }
    outSize.cx = w;
    outSize.cy = h;
    m_laidOut = true;
    return true;
}

bool CMarkdownDocument::Paint(HDC hdc, int x, int y, const flamingomd::MdPaintState* state)
{
    if (m_doc == NULL || !m_laidOut || hdc == NULL)
        return false;
    return m_api->m_paint(m_doc, hdc, x, y, state) == flamingomd::md_ok;
}

bool CMarkdownDocument::HitTest(int x, int y, flamingomd::MdHitResult& out) const
{
    memset(&out, 0, sizeof(out));
    out.m_cbSize = sizeof(out);
    out.m_linkIndex = -1;
    out.m_codeBlockIndex = -1;
    if (m_doc == NULL || !m_laidOut)
        return false;
    return m_api->m_hitTest(m_doc, x, y, &out) == flamingomd::md_ok;
}

std::wstring CMarkdownDocument::GetText(int begin, int end) const
{
    if (m_doc == NULL)
        return std::wstring();
    uint32_t needed = 0;
    int rc = m_api->m_getText(m_doc, begin, end, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small)
        return std::wstring();
    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1, 0);
    rc = m_api->m_getText(m_doc, begin, end, &buf[0], needed + 1, &needed);
    return (rc == flamingomd::md_ok) ? std::wstring(&buf[0], needed) : std::wstring();
}

int CMarkdownDocument::GetTextLength() const
{
    if (m_doc == NULL)
        return 0;
    flamingomd::MdDocInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    if (m_api->m_getDocInfo(m_doc, &info) != flamingomd::md_ok)
        return 0;
    return info.m_textLength;
}

std::wstring CMarkdownDocument::GetLinkUrl(int index) const
{
    if (m_doc == NULL)
        return std::wstring();
    uint32_t needed = 0;
    int rc = m_api->m_getLinkUrl(m_doc, index, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small)
        return std::wstring();
    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1, 0);
    rc = m_api->m_getLinkUrl(m_doc, index, &buf[0], needed + 1, &needed);
    return (rc == flamingomd::md_ok) ? std::wstring(&buf[0], needed) : std::wstring();
}

std::wstring CMarkdownDocument::GetCodeBlockText(int index) const
{
    if (m_doc == NULL)
        return std::wstring();
    uint32_t needed = 0;
    int rc = m_api->m_getCodeBlockText(m_doc, index, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small)
        return std::wstring();
    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1, 0);
    rc = m_api->m_getCodeBlockText(m_doc, index, &buf[0], needed + 1, &needed);
    return (rc == flamingomd::md_ok) ? std::wstring(&buf[0], needed) : std::wstring();
}

namespace {

//函数表是否带第二版接口（卡片文字、HTML 导出、按页切分）
bool HasV2(const flamingomd::MdApi* api)
{
    return api != NULL && api->m_cbSize >= flamingomd::kMdApiV2Size;
}

//取卡片的一部分（先查长度再取）
bool GetCardPart(const flamingomd::MdApi* api, const flamingomd::MdDocument* doc, int part, std::wstring& out)
{
    out.clear();
    uint32_t needed = 0;
    int rc = api->m_getCardText(doc, part, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small)
        return false;
    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1, 0);
    rc = api->m_getCardText(doc, part, &buf[0], needed + 1, &needed);
    if (rc != flamingomd::md_ok)
        return false;
    out.assign(&buf[0], needed);
    return true;
}

} // namespace

bool CMarkdownDocument::GetCardText(std::wstring& title, std::wstring& summary) const
{
    title.clear();
    summary.clear();
    if (m_doc == NULL || !HasV2(m_api))
        return false;
    return GetCardPart(m_api, m_doc, flamingomd::md_card_title, title)
           && GetCardPart(m_api, m_doc, flamingomd::md_card_summary, summary);
}

bool CMarkdownDocument::ExportHtml(const std::wstring& title, std::string& html) const
{
    html.clear();
    if (m_doc == NULL || !HasV2(m_api))
        return false;
    flamingomd::MdHtmlOptions options;
    memset(&options, 0, sizeof(options));
    options.m_cbSize = sizeof(options);
    options.m_title = title.c_str();
    uint32_t needed = 0;
    int rc = m_api->m_exportHtml(m_doc, &options, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small)
        return false;
    std::vector<char> buf(static_cast<size_t>(needed) + 1, 0);
    rc = m_api->m_exportHtml(m_doc, &options, &buf[0], needed + 1, &needed);
    if (rc != flamingomd::md_ok)
        return false;
    html.assign(&buf[0], needed);
    return true;
}

bool CMarkdownDocument::ExportHtmlRange(int begin, int end, std::string& html) const
{
    html.clear();
    if (m_doc == NULL || m_api == NULL || m_api->m_cbSize < flamingomd::kMdApiV4Size)
        return false;
    //先查长度再取；选区为空时内容长度为 0
    uint32_t needed = 0;
    int rc = m_api->m_exportHtmlRange(m_doc, NULL, begin, end, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small)
        return false;
    std::vector<char> buf(static_cast<size_t>(needed) + 1, 0);
    rc = m_api->m_exportHtmlRange(m_doc, NULL, begin, end, &buf[0], needed + 1, &needed);
    if (rc != flamingomd::md_ok)
        return false;
    html.assign(&buf[0], needed);
    return true;
}

bool CMarkdownDocument::ExportDocx(std::string& bytes) const
{
    bytes.clear();
    if (m_doc == NULL || m_api == NULL || m_api->m_cbSize < flamingomd::kMdApiV3Size)
        return false;
    uint32_t needed = 0;
    int rc = m_api->m_exportDocx(m_doc, NULL, 0, &needed);
    if (rc != flamingomd::md_buffer_too_small || needed == 0)
        return false;
    std::vector<unsigned char> buf(needed, 0);
    rc = m_api->m_exportDocx(m_doc, &buf[0], needed, &needed);
    if (rc != flamingomd::md_ok)
        return false;
    bytes.assign(reinterpret_cast<const char*>(&buf[0]), needed);
    return true;
}

bool CMarkdownDocument::CanExportDocx()
{
    const flamingomd::MdApi* api = ActiveMarkdownApi();
    return api != NULL && api->m_cbSize >= flamingomd::kMdApiV3Size;
}

bool CMarkdownDocument::Paginate(int pageHeight, std::vector<int>& starts) const
{
    starts.clear();
    if (m_doc == NULL || !HasV2(m_api) || pageHeight <= 0)
        return false;
    uint32_t count = 0;
    int rc = m_api->m_paginate(m_doc, pageHeight, NULL, 0, &count);
    if (rc != flamingomd::md_buffer_too_small || count == 0)
        return false;
    std::vector<int32_t> buf(count, 0);
    rc = m_api->m_paginate(m_doc, pageHeight, &buf[0], count, &count);
    if (rc != flamingomd::md_ok)
        return false;
    starts.assign(buf.begin(), buf.begin() + count);
    return true;
}

} // namespace markdown
