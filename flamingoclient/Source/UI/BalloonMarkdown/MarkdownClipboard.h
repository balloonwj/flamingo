/**
 *  Markdown 预览的复制（docs/Markdown消息方案.md 第 12 节）：复制时剪贴板里同时放纯文字（CF_UNICODETEXT）与 HTML
 *  （Windows 约定的「HTML Format」，即 CF_HTML）。粘贴到 Word、Outlook、WPS 等支持富文本的程序时保留标题、加粗、
 *  列表、表格与代码颜色；粘贴到记事本等只认纯文字的程序时取纯文字那一份。
 *
 *  HTML 由 FlamingoMarkdown.dll 第四版接口 m_exportHtmlRange 生成（正文前后带片段标记），本模块负责：
 *    - 按 CF_HTML 的约定在前面加上头部（Version、StartHTML、EndHTML、StartFragment、EndFragment，偏移按字节计）；
 *    - 组装两种情况要放进剪贴板的内容：预览控件复制选区（Ctrl+C、右键「复制」），预览窗口「复制全文（带格式）」；
 *    - 一次打开剪贴板写入两种格式（分两次写的话，后一次的 EmptyClipboard 会清掉前一次写的内容）。
 *  DLL 是旧版（没有第四版接口）时只写纯文字，与以前相同。
 *
 *  前两项是纯函数，FlamingoUnit 直接断言（Tests/MarkdownEditorTests.cpp 的 ED23、ED24，标签 [mdeditor]）；写剪贴板
 *  会覆盖用户剪贴板里的内容，不进单元测试（与 ChatClipboardIo 的做法一致），由实机核对验证。
 *
 *  典型用法：
 *      balloonmd::MarkdownClipboardData data;
 *      if (balloonmd::BuildMarkdownSelectionClipboard(doc, selBegin, selEnd, data))
 *          balloonmd::PutMarkdownClipboard(hwnd, data);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <string>

namespace markdown {
class CMarkdownDocument;
} // namespace markdown

namespace balloonmd {

//剪贴板 HTML 格式的注册名（Windows 约定，注册后即 CF_HTML）
extern const wchar_t* const kCfHtmlFormatName;

/**
 *  复制时要放进剪贴板的内容。
 */
struct MarkdownClipboardData
{
    std::wstring m_text;     //纯文字，换行为 \r\n，写成 CF_UNICODETEXT
    std::string  m_cfHtml;   //CF_HTML 的全部字节（头部加 HTML，UTF-8，不含结尾的 0）；为空时只写纯文字
};

/**
 *  把 DLL 按选区导出的 HTML（正文前后带 <!--StartFragment--> 与 <!--EndFragment-->）包装成 CF_HTML：去掉开头的
 *  文档类型声明，使 StartHTML 指向 <html>；StartFragment 指向开始标记之后，EndFragment 指向结束标记；EndHTML 为
 *  全部内容的长度。四个偏移都是十位十进制数字、按 UTF-8 字节计。
 *  @param html DLL 输出的 HTML（UTF-8）
 *  @return CF_HTML 的全部字节；html 里缺少片段标记时返回空串
 */
std::string WrapCfHtml(const std::string& html);

/**
 *  预览控件复制选区时要放进剪贴板的内容：纯文字取渲染后文字里的这一段（与以前相同）；DLL 有第四版接口时，再按
 *  这一段生成 HTML。
 *  @param doc   已解析的文档
 *  @param begin 选区起点（渲染后文字的下标），与 end 顺序不限
 *  @param end   选区终点（不含）
 *  @param out   [出参] 剪贴板内容；DLL 是旧版时 m_cfHtml 为空
 *  @return 选区里有文字时返回 true；选区为空、文档未解析时返回 false
 */
bool BuildMarkdownSelectionClipboard(const markdown::CMarkdownDocument& doc, int begin, int end,
                                     MarkdownClipboardData& out);

/**
 *  预览窗口「复制全文（带格式）」要放进剪贴板的内容：纯文字与「复制为纯文字」相同（去掉标记），HTML 为全文。
 *  @param sourceUtf8 源文本（UTF-8）
 *  @param out        [出参] 剪贴板内容；DLL 是旧版或解析失败时 m_cfHtml 为空
 *  @return 有文字可复制时返回 true
 */
bool BuildMarkdownWholeClipboard(const std::string& sourceUtf8, MarkdownClipboardData& out);

/**
 *  把内容写进剪贴板：打开一次，清空后写纯文字，m_cfHtml 不为空时再写 HTML 格式。
 *  @param owner 剪贴板的属主窗口，可为 NULL
 *  @param data  要写入的内容
 *  @return 纯文字写入成功返回 true（HTML 写入失败只记日志）；文字为空、剪贴板被别的程序占用时返回 false
 *  副作用：清空剪贴板原有的全部内容
 */
bool PutMarkdownClipboard(HWND owner, const MarkdownClipboardData& data);

/**
 *  预览窗口「复制全文（带格式）」：BuildMarkdownWholeClipboard 之后 PutMarkdownClipboard。
 *  @param owner      剪贴板的属主窗口
 *  @param sourceUtf8 源文本（UTF-8）
 *  @return 写入成功返回 true
 */
bool CopyMarkdownRichTextToClipboard(HWND owner, const std::string& sourceUtf8);

} // namespace balloonmd
