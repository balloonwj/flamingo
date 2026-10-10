/**
 *  flamingoAdmin —— 导出文件的存盘规格（纯逻辑，便于单元测试）。
 *
 *  各处「导出」最后都要弹「另存为」对话框、把一段文字写进用户选的文件、再给一条提示。不同导出之间
 *  只差几项参数：预填的文件名、文件类型、默认扩展名、成功提示、要不要补 UTF-8 BOM。这些参数收进
 *  SaveFileSpec，由 AdminFrame::SaveTextToFile 统一弹框与写盘。成功提示由调用方按导出的内容给出，
 *  不再固定写「企业列表已导出」。
 *  单元测试见 Tests/SensitiveImportExportTests.cpp 的 ST 系列。
 *
 *  典型用法：
 *      const filesave::SaveFileSpec spec = filesave::CsvFileSpec(_T("审计日志.csv"), _T("审计日志"));
 *      SaveTextToFile(csvUtf8, spec);            //AdminFrame 内，提示「审计日志已导出」
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include <vector>

namespace flamingoadmin { namespace filesave {

// 一次导出的存盘规格
struct SaveFileSpec
{
    CString m_defaultName;    // 「另存为」对话框里预填的文件名，如「审计日志.csv」
    CString m_filterName;     // 文件类型说明，如「CSV 文件 (*.csv)」
    CString m_filterPattern;  // 文件类型通配，如「*.csv」
    CString m_defExt;         // 用户没写扩展名时补上的扩展名，不带点，如「csv」
    CString m_successToast;   // 写盘成功后的提示，如「审计日志已导出」
    bool    m_addUtf8Bom;     // 写盘前是否在内容前补 UTF-8 BOM（内容本身已带 BOM 时不再重复补）

    SaveFileSpec()
        : m_addUtf8Bom(false)
    {
    }
};

// CSV 导出的规格：补 UTF-8 BOM（Excel 等工具据此识别中文），成功提示为「<what>已导出」。
//   defaultName：预填的文件名，如「企业列表.csv」。what：导出的是什么，如「企业列表」。
SaveFileSpec CsvFileSpec(LPCTSTR defaultName, LPCTSTR what);

// 文本（.txt）导出的规格：补 UTF-8 BOM，成功提示为「<what>已导出」。
//   defaultName：预填的文件名。what：导出的是什么，如「敏感词词库」。
SaveFileSpec TextFileSpec(LPCTSTR defaultName, LPCTSTR what);

// 按规格得到要写进文件的字节：需要补 BOM 且内容不以 UTF-8 BOM 开头时，在前面补上 EF BB BF。
//   content：要写的内容（UTF-8 字节），可以为空。
std::string BuildFileBytes(const SaveFileSpec& spec, const std::string& content);

// 生成 OPENFILENAME::lpstrFilter 需要的过滤串：「说明\0通配\0所有文件 (*.*)\0*.*\0\0」。
//   out：[出参] 过滤串，调用前会被清空；以两个 \0 结尾，可直接取 &out[0] 传给对话框。
void BuildFilterBuffer(const SaveFileSpec& spec, std::vector<TCHAR>& out);

// 敏感词词库导出时预填的文件名：「敏感词词库-<企业名>.txt」；企业名为空时为「敏感词词库.txt」。
// 企业名里 Windows 文件名不允许的字符（\ / : * ? " < > |）与控制字符替换为下划线，首尾空白去掉。
//   enterpriseName：企业名称，可以为空（企业管理员的会话里没有企业名）。
CString SensitiveExportFileName(const CString& enterpriseName);

}} // namespace flamingoadmin::filesave
