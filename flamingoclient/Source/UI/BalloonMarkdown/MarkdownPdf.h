/**
 *  MarkdownPdf.h
 *
 *  Markdown 文档导出为 PDF（docs/Markdown消息方案.md 第 11.4 节）：
 *    - 优先用系统自带的「Microsoft Print to PDF」打印机静默输出矢量 PDF：在 StartDoc 的 DOCINFO.lpszOutput 里给出文件名
 *      就不弹任何对话框（据实证：tmp/md/pdfspike/spike.ps1，2026-10-06），文字可选中、可搜索，字体由驱动按子集嵌入；
 *      按 A4、打印机 DPI 排版，由 DLL 的 m_paginate 只在行与行之间分页，逐页裁剪绘制；
 *    - 这台打印机不存在时（Win7、被管理员删除）改为每页画成一张 150 DPI 的图片，封装为 PDF（文字不可选中），图片数据用
 *      zlib 压缩。
 *
 *  典型用法：
 *      std::wstring error;
 *      const bool ok = balloonmd::IsPdfPrinterAvailable()
 *                      ? balloonmd::ExportMarkdownPdfViaPrinter(path, source, &error)
 *                      : balloonmd::ExportMarkdownPdfAsImages(path, source, &error);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace balloonmd {

//系统自带的 PDF 打印机的名字（Windows 10 起默认安装）
extern const wchar_t* const kPdfPrinterName;

//PDF 页面：A4，单位为点（1/72 英寸）
const int kPdfPageWidthPt = 595;
const int kPdfPageHeightPt = 842;

//页边距（毫米）
const int kPdfMarginMm = 15;

//图片式 PDF 每页图片的分辨率（DPI）：清晰度与文件大小的折中
const int kPdfImageDpi = 150;

/**
 *  图片式 PDF 的一页：RGB 像素，自上而下逐行排列，每行 m_width * 3 字节，没有行尾填充。
 */
struct PdfPageImage
{
    int                        m_width;    //宽（像素）
    int                        m_height;   //高（像素）
    std::vector<unsigned char> m_rgb;      //像素数据，大小为 m_width * m_height * 3

    PdfPageImage()
        : m_width(0), m_height(0)
    {
    }
};

/**
 *  系统 PDF 打印机是否可用：能为它创建设备上下文即视为可用。
 */
bool IsPdfPrinterAvailable();

/**
 *  用系统 PDF 打印机把文档静默输出为矢量 PDF。
 *  @param path       目标文件完整路径
 *  @param sourceUtf8 源文本（UTF-8）
 *  @param error      [出参，可为 NULL] 失败时写入给用户看的说明
 *  @return 成功返回 true
 */
bool ExportMarkdownPdfViaPrinter(const std::wstring& path, const std::string& sourceUtf8, std::wstring* error);

/**
 *  把文档逐页画成图片后封装为 PDF（打印机不可用时的退路，文字不可选中）。
 *  @param path       目标文件完整路径
 *  @param sourceUtf8 源文本（UTF-8）
 *  @param error      [出参，可为 NULL] 失败时写入给用户看的说明
 *  @return 成功返回 true
 */
bool ExportMarkdownPdfAsImages(const std::wstring& path, const std::string& sourceUtf8, std::wstring* error);

/**
 *  把若干页图片写成一个 PDF 文件的内容：每页一张图铺满 A4 页面，图片数据用 FlateDecode（zlib）压缩。纯函数，单元测试用。
 *  @param pages 各页图片，至少一页
 *  @param out   [出参] PDF 文件内容
 *  @return 成功返回 true；pages 为空、某页尺寸与数据不符或压缩失败时返回 false
 */
bool BuildImagePdf(const std::vector<PdfPageImage>& pages, std::string& out);

} // namespace balloonmd
