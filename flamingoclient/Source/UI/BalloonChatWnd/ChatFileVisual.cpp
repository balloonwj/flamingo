/**
 *  文件消息视觉共用件的实现，ChatFileVisual.cpp
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"
#include "ChatFileVisual.h"

namespace balloonchatwnd {

namespace {

// 文件类型色（docs/聊天卡片美化方案.md 3.7 节，2026-10-14 起由四种扩展名扩到八组）
const COLORREF kFileColorPdf = RGB(0xE5, 0x48, 0x4D);       // PDF
const COLORREF kFileColorWord = RGB(0x2F, 0x6F, 0xED);      // Word 文档
const COLORREF kFileColorSheet = RGB(0x1F, 0x9D, 0x5A);     // 表格
const COLORREF kFileColorSlide = RGB(0xF0, 0x66, 0x2B);     // 演示文稿
const COLORREF kFileColorDesign = RGB(0xA2, 0x59, 0xFF);    // 设计稿
const COLORREF kFileColorImage = RGB(0x0E, 0xA5, 0xB7);     // 图片
const COLORREF kFileColorArchive = RGB(0x8C, 0x6D, 0x4F);   // 压缩包
const COLORREF kFileColorOther = RGB(0x8A, 0x94, 0xA6);     // 其他（含无扩展名）

// 一组扩展名与它们的颜色
struct FileColorGroup
{
    const wchar_t* const* m_exts;   // 扩展名表（小写，不含点），以 NULL 结尾
    COLORREF              m_color;  // 这一组的颜色
};

const wchar_t* const kExtsPdf[] = { L"pdf", NULL };
const wchar_t* const kExtsWord[] = { L"doc", L"docx", NULL };
const wchar_t* const kExtsSheet[] = { L"xls", L"xlsx", L"csv", NULL };
const wchar_t* const kExtsSlide[] = { L"ppt", L"pptx", NULL };
const wchar_t* const kExtsDesign[] = { L"fig", L"sketch", L"psd", L"ai", L"xd", NULL };
const wchar_t* const kExtsImage[] = { L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp", L"svg", NULL };
const wchar_t* const kExtsArchive[] = { L"zip", L"rar", L"7z", L"tar", L"gz", NULL };

const FileColorGroup kFileColorGroups[] = {
    { kExtsPdf, kFileColorPdf },
    { kExtsWord, kFileColorWord },
    { kExtsSheet, kFileColorSheet },
    { kExtsSlide, kFileColorSlide },
    { kExtsDesign, kFileColorDesign },
    { kExtsImage, kFileColorImage },
    { kExtsArchive, kFileColorArchive },
};

} // namespace

COLORREF FileTypeColor(const CString& type)
{
    CString ext(type);
    ext.MakeLower();
    for (size_t g = 0; g < sizeof(kFileColorGroups) / sizeof(kFileColorGroups[0]); ++g)
    {
        for (const wchar_t* const* p = kFileColorGroups[g].m_exts; *p != NULL; ++p)
        {
            if (ext == *p)
                return kFileColorGroups[g].m_color;
        }
    }
    return kFileColorOther;
}

} // namespace balloonchatwnd
