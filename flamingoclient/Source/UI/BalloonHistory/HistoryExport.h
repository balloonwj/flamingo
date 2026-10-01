/**
 *  HistoryExport.h
 *
 *  消息记录窗口「导出」的纯逻辑：把消息整理成网页（.html），以及导出时用到的文件名处理。
 *  窗口负责查库、解析发送人名字、找本地图片并复制到 <网页名>_files 文件夹，再把整理好的
 *  行交给这里生成网页文本；文本导出（.txt）沿用窗口里原有的写法。
 *
 *  纯逻辑，不碰磁盘、不碰窗口，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程共用（见
 *  Tests/HistoryExportTests.cpp）。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      std::vector<HistoryExportRow> rows = ...;               //时间正序（旧 → 新）
 *      std::wstring html = BuildHistoryHtml(L"消息记录 · 苏沐辰", rows);
 *      //按 UTF-8 写入 .html 文件
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include <stddef.h>
#include <set>
#include <string>
#include <vector>

namespace balloonhistory {

//导出网页里一条消息的呈现方式
enum HistoryExportKind
{
    kHistoryExportText  = 0,   //文字，以及其它只能用一句文字表示的消息（语音、通话记录等写成「[语音]」这类占位）
    kHistoryExportImage = 1,   //图片：本地有原图时嵌入图片，没有时写「[图片未下载]」
    kHistoryExportFile  = 2    //文件：写「[文件] 文件名」
};

//导出网页里的一条消息。所有文字都是未转义的原文，由 BuildHistoryHtml 负责转义
struct HistoryExportRow
{
    std::wstring m_time;           //时间文字，如「2026-09-30 20:07」
    std::wstring m_sender;         //发送人名字
    int          m_kind;           //呈现方式，取值见 HistoryExportKind
    std::wstring m_text;           //文字内容（文字消息）、文件名（文件消息）或占位文字；图片消息不用
    std::wstring m_imageRelPath;   //图片相对于网页文件的路径（如「消息记录_苏沐辰_files/1.png」，用 / 分隔）；
                                   //空串表示本地没有这张图

    HistoryExportRow()
        : m_kind(kHistoryExportText)
    {
    }
};

//把文字转成可以放进网页正文的形式：& < > " ' 换成字符实体，换行换成 <br>。
std::wstring HtmlEscape(const std::wstring& text);

/**
 *  生成导出网页的完整文本（UTF-8 编码由调用方写文件时转换）。
 *    title：网页标题，出现在 <title> 与页首大标题里。
 *    rows：消息，按调用方给的顺序输出（一般是时间正序）。
 *  @return 完整的 HTML 文本，自带样式，不依赖外部文件（图片除外）
 */
std::wstring BuildHistoryHtml(const std::wstring& title, const std::vector<HistoryExportRow>& rows);

//把文件名里 Windows 不允许的字符（\ / : * ? " < > | 与控制字符）换成下划线，并去掉首尾的点与空白；
//处理后为空时返回「image」。
std::wstring SanitizeExportFileName(const std::wstring& name);

/**
 *  在同一个文件夹里取一个不重名的文件名：没用过就原样返回，重名时在扩展名前加「 (2)」「 (3)」……
 *  比较不区分大小写（Windows 文件系统不区分）。
 *    name：想用的文件名（应已经过 SanitizeExportFileName）。
 *    used：[入出参] 这个文件夹里已经用掉的文件名（小写）；返回的名字会加进去。
 *  @return 不重名的文件名
 */
std::wstring UniqueExportFileName(const std::wstring& name, std::set<std::wstring>& used);

//由网页文件名推出存放图片的文件夹名：去掉扩展名后加「_files」，与浏览器"另存为网页"的习惯一致。
//  htmlFileName：网页文件名（不含目录），如「消息记录_苏沐辰.html」。
std::wstring ExportFilesDirName(const std::wstring& htmlFileName);

/**
 *  按图片文件开头的几个字节判断格式，返回对应的扩展名（含点，如「.png」）。本地图片缓存按内容
 *  MD5 命名、没有扩展名，复制到导出文件夹时据此补上，浏览器才能可靠地显示。
 *    head：文件开头的字节；size：字节数。
 *  @return 「.png」「.jpg」「.gif」「.bmp」「.webp」之一；认不出时返回「.img」
 */
std::wstring ImageExtensionFromHeader(const unsigned char* head, size_t size);

//导出之前要不要先问用户"还有更早的消息没加载"。
//  hasUnloadedOlder：服务端是否还有更早的消息没拉到本地。
bool ShouldPromptUnloadedBeforeExport(bool hasUnloadedOlder);

} // namespace balloonhistory
