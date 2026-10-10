/**
 *  flamingoAdmin —— 导出文件的存盘规格实现，说明见 TextFileSave.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Util/TextFileSave.h"

namespace flamingoadmin { namespace filesave {

namespace {

// UTF-8 BOM
const char kUtf8Bom[] = "\xEF\xBB\xBF";
const size_t kUtf8BomLen = 3;

// 「所有文件」那一项的说明与通配
const TCHAR kAllFilesName[]    = _T("所有文件 (*.*)");
const TCHAR kAllFilesPattern[] = _T("*.*");

// Windows 文件名里不允许出现的字符
const TCHAR kBadFileNameChars[] = _T("\\/:*?\"<>|");

// 小于这个值的字符是控制字符，同样不能出现在文件名里
const TCHAR kFirstPrintableChar = 0x20;

// 两种规格共同的部分
SaveFileSpec MakeSpec(LPCTSTR defaultName, LPCTSTR what, LPCTSTR filterName, LPCTSTR pattern, LPCTSTR ext)
{
    SaveFileSpec spec;
    spec.m_defaultName   = defaultName;
    spec.m_filterName    = filterName;
    spec.m_filterPattern = pattern;
    spec.m_defExt        = ext;
    spec.m_successToast  = CString(what) + _T("已导出");
    spec.m_addUtf8Bom    = true;
    return spec;
}

// 把一段以 \0 结尾的文字连同结尾的 \0 追加到过滤串
void AppendWithNul(std::vector<TCHAR>& out, const CString& text)
{
    for (int i = 0; i < text.GetLength(); ++i)
        out.push_back(text[i]);
    out.push_back(_T('\0'));
}

} // namespace

SaveFileSpec CsvFileSpec(LPCTSTR defaultName, LPCTSTR what)
{
    return MakeSpec(defaultName, what, _T("CSV 文件 (*.csv)"), _T("*.csv"), _T("csv"));
}

SaveFileSpec TextFileSpec(LPCTSTR defaultName, LPCTSTR what)
{
    return MakeSpec(defaultName, what, _T("文本文件 (*.txt)"), _T("*.txt"), _T("txt"));
}

std::string BuildFileBytes(const SaveFileSpec& spec, const std::string& content)
{
    const bool hasBom = content.size() >= kUtf8BomLen && content.compare(0, kUtf8BomLen, kUtf8Bom) == 0;
    if (!spec.m_addUtf8Bom || hasBom)
        return content;
    return std::string(kUtf8Bom, kUtf8BomLen) + content;
}

void BuildFilterBuffer(const SaveFileSpec& spec, std::vector<TCHAR>& out)
{
    out.clear();
    AppendWithNul(out, spec.m_filterName);
    AppendWithNul(out, spec.m_filterPattern);
    AppendWithNul(out, kAllFilesName);
    AppendWithNul(out, kAllFilesPattern);
    //过滤串以一个空串收尾，即末尾再多一个 \0
    out.push_back(_T('\0'));
}

CString SensitiveExportFileName(const CString& enterpriseName)
{
    CString name = enterpriseName;
    name.Trim();
    for (int i = 0; i < name.GetLength(); ++i)
    {
        const TCHAR ch = name[i];
        if (ch < kFirstPrintableChar || _tcschr(kBadFileNameChars, ch) != nullptr)
            name.SetAt(i, _T('_'));
    }
    if (name.IsEmpty())
        return CString(_T("敏感词词库.txt"));
    return _T("敏感词词库-") + name + _T(".txt");
}

}} // namespace flamingoadmin::filesave
