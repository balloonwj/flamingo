/**
 *  MarkdownExport.h
 *
 *  Markdown 文档的导出与文件读写（docs/Markdown消息方案.md 第 11.4 节），编辑器窗口与预览窗口的「导出 ▾」共用：
 *    - .md：原样写出源文本（UTF-8 不带 BOM，换行写成 CRLF）；
 *    - .html：DLL 从文档模型生成单个文件、样式内嵌；
 *    - .png：按预览宽度排版，画成一张长图；
 *    - .pdf：优先用系统的「Microsoft Print to PDF」打印机静默输出矢量 PDF，打印机不存在时改为每页一张图片的 PDF；
 *    - .docx：第 6 期。
 *  导出前弹出另存为对话框；某种格式不可用（如旧 DLL 缺少对应接口）时菜单里不出现，由 IsMarkdownExportAvailable 判断。
 *
 *  典型用法（导出菜单选中某一项后）：
 *      std::wstring error;
 *      if (!balloonmd::ExportMarkdownWithDialog(m_hWnd, source, title, balloonmd::md_export_html, &error) && !error.empty())
 *          ::MessageBox(m_hWnd, error.c_str(), caption, MB_OK | MB_ICONWARNING);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <string>

namespace balloonmd {

//导出格式（同时作为导出菜单项的编号）
enum MarkdownExportFormat
{
    md_export_md    = 1,   //Markdown 源文件 .md
    md_export_html  = 2,   //网页 .html
    md_export_png   = 3,   //图片 .png
    md_export_pdf   = 4,   //PDF .pdf
    md_export_docx  = 5,   //Word .docx
    md_export_count = 5,   //格式个数（最大的取值）
};

//打开 .md 文件时允许的最大字节数：超过时提示文件太大，不打开
const size_t kMarkdownOpenMaxBytes = 4 * 1024 * 1024;

//导出图片与「复制为图片」时文档的排版宽度（像素，不含四周留白）：与预览区接近，长图在手机上看也合适
const int kMarkdownImageContentWidth = 760;

//图片四周的留白（像素）
const int kMarkdownImagePad = 24;

//图片的最大高度（像素）：超过时提示改用 PDF（GDI+ 能编码的上限是 65535，留出余量）
const int kMarkdownImageMaxHeight = 32000;

/**
 *  把文档按 contentWidth 排版后画成一张 32 位位图：白底，四周留 kMarkdownImagePad 像素。
 *  @param sourceUtf8   源文本（UTF-8）
 *  @param contentWidth 文档的排版宽度（像素，不含留白）
 *  @param outSize      [出参] 位图尺寸
 *  @param tooTall      [出参，可为 NULL] 因高度超过 kMarkdownImageMaxHeight 而失败时置为 true
 *  @return 位图（DIB section，调用方 DeleteObject）；DLL 不可用、解析失败或太高时返回 NULL
 */
HBITMAP RenderMarkdownBitmap(const std::string& sourceUtf8, int contentWidth, SIZE& outSize, bool* tooTall);

/**
 *  把源文本写进剪贴板（纯文字，换行为 \r\n）。
 *  @param owner      剪贴板的属主窗口
 *  @param sourceUtf8 源文本（UTF-8）
 *  @return 写入成功返回 true
 */
bool CopyMarkdownSourceToClipboard(HWND owner, const std::string& sourceUtf8);

/**
 *  把去掉标记的纯文字写进剪贴板（块之间换行、单元格之间制表符，换行为 \r\n）。
 *  @param owner      剪贴板的属主窗口
 *  @param sourceUtf8 源文本（UTF-8）
 *  @return 写入成功返回 true
 */
bool CopyMarkdownPlainTextToClipboard(HWND owner, const std::string& sourceUtf8);

/**
 *  把整篇文档画成图片（排版宽度 kMarkdownImageContentWidth）写进剪贴板（CF_DIB），可以直接粘到聊天或文档里。
 *  @param owner      剪贴板的属主窗口
 *  @param sourceUtf8 源文本（UTF-8）
 *  @param error      [出参，可为 NULL] 失败时写入给用户看的说明
 *  @return 写入成功返回 true
 */
bool CopyMarkdownImageToClipboard(HWND owner, const std::string& sourceUtf8, std::wstring* error);

/**
 *  读出整个文件。
 *  @param path     文件完整路径
 *  @param maxBytes 允许的最大字节数；文件更大时返回 false
 *  @param bytes    [出参] 文件内容
 *  @param tooLarge [出参，可为 NULL] 因文件超过 maxBytes 而失败时置为 true
 *  @return 读取成功返回 true
 */
bool ReadWholeFile(const std::wstring& path, size_t maxBytes, std::string& bytes, bool* tooLarge);

/**
 *  把内容写入文件（覆盖已有文件）。
 *  @param path  文件完整路径
 *  @param bytes 要写入的内容
 *  @return 全部写入返回 true
 */
bool WriteWholeFile(const std::wstring& path, const std::string& bytes);

/**
 *  某种导出格式当前是否可用。
 *  @param format 取值见 enum MarkdownExportFormat
 *  @return 可用返回 true
 */
bool IsMarkdownExportAvailable(int format);

/**
 *  导出格式在菜单里显示的文字（已翻译），如「网页（.html）」。
 *  @param format 取值见 enum MarkdownExportFormat
 *  @return 菜单文字；format 不认识时为空串
 */
std::wstring MarkdownExportMenuText(int format);

/**
 *  弹出另存为对话框，把文档导出为 format 格式。
 *  @param owner         对话框的所有者窗口
 *  @param sourceUtf8    源文本（UTF-8，换行为 \n）
 *  @param suggestedName 建议的文件名（不含扩展名）；为空时用「未命名」
 *  @param format        取值见 enum MarkdownExportFormat
 *  @param error         [出参，可为 NULL] 失败时写入给用户看的说明；用户取消对话框时保持为空
 *  @return 导出成功返回 true；用户取消或失败返回 false
 */
bool ExportMarkdownWithDialog(HWND owner, const std::string& sourceUtf8, const std::wstring& suggestedName, int format,
                              std::wstring* error);

/**
 *  不弹对话框，把文档导出到指定路径（单元测试与「另存为」之后共用）。
 *  @param path       目标文件完整路径（扩展名由调用方给出）
 *  @param sourceUtf8 源文本（UTF-8，换行为 \n）
 *  @param format     取值见 enum MarkdownExportFormat
 *  @param error      [出参，可为 NULL] 失败时写入给用户看的说明
 *  @return 导出成功返回 true
 */
bool ExportMarkdownToFile(const std::wstring& path, const std::string& sourceUtf8, int format, std::wstring* error);

} // namespace balloonmd
