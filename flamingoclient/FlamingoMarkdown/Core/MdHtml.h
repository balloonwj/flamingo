/**
 *  从 Markdown 文档模型生成单个 HTML 文件（docs/Markdown消息方案.md 第 11.4 节「导出 .html」）：UTF-8 编码、样式内嵌，
 *  浏览器直接打开即可。不用 md4c 自带的 HTML 输出模块，而是从同一个模型生成，保证图片转链接、HTML 标签按文字显示
 *  等处理与消息卡片、预览一致。
 *
 *  对应关系：段落 <p>、标题 <h1>~<h6>、引用 <blockquote>、列表 <ul>/<ol start>（任务项带禁用的复选框）、
 *  代码块 <pre><code class="language-xxx">、分隔线 <hr>、表格 <table>（表头行进 <thead>，单元格带 text-align）；
 *  行内的加粗 <strong>、斜体 <em>、删除线 <del>、行内代码 <code>、链接 <a href>（只有 http、https、mailto），
 *  强制换行 <br>。文字一律转义。
 *
 *  另有按选区导出（BuildMdHtmlRange，docs/Markdown消息方案.md 第 12 节）：复制到剪贴板时只导出渲染后文字里被选中的
 *  一段，头部与样式表与整篇导出相同，正文前后带 CF_HTML 的片段标记，宿主据此算出剪贴板头部里的偏移。
 *
 *  典型用法：
 *      const std::string html = flamingomd::BuildMdHtml(doc->m_model, L"发布说明");
 *      const std::string clip = flamingomd::BuildMdHtmlRange(doc->m_model, L"", selBegin, selEnd);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "MdModel.h"

namespace flamingomd {

/**
 *  生成 HTML 文件内容。
 *  @param model 文档模型
 *  @param title 文档标题，写进 <title>；为空时写「Markdown」
 *  @return UTF-8 编码的完整 HTML 文件（不带 BOM）
 */
std::string BuildMdHtml(const MdModel& model, const std::wstring& title);

//按选区导出时正文前后的片段标记（CF_HTML 约定的注释写法），宿主按它们在输出里的位置写剪贴板头部的偏移
const char* const kMdHtmlFragmentStart = "<!--StartFragment-->";
const char* const kMdHtmlFragmentEnd = "<!--EndFragment-->";

/**
 *  把渲染后文字里 [begin, end) 这一段导出为 HTML（复制到剪贴板用）。头部与样式表同 BuildMdHtml；正文只含与这一段
 *  相交的块：段落、标题、单元格里只留下选中的文字（行内样式照留），列表只含相交的项（有序列表的起始编号随之调整），
 *  表格只含相交的行（这些行里不在选区内的单元格留空，列数不变），代码块只含选中的部分；整个 <div class="md"> 夹在
 *  kMdHtmlFragmentStart 与 kMdHtmlFragmentEnd 之间。
 *  @param model 文档模型
 *  @param title 文档标题，写进 <title>；为空时写「Markdown」
 *  @param begin 选区起点（渲染后文字的下标），与 end 顺序不限，越界截断
 *  @param end   选区终点（不含）
 *  @return UTF-8 编码的完整 HTML（不带 BOM）；截断后起点等于终点时返回空串
 */
std::string BuildMdHtmlRange(const MdModel& model, const std::wstring& title, int begin, int end);

/**
 *  HTML 文字转义：& < > " ' 转成实体。
 *  @param text UTF-16 文字
 *  @return 转义后的 UTF-8 文字
 */
std::string EscapeMdHtml(const std::wstring& text);

} // namespace flamingomd
