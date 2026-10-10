/**
 *  从 Markdown 文档模型生成 Word 文档（.docx，docs/Markdown消息方案.md 第 11.6 节）：.docx 是 ZIP 包里的几份 XML，本模块
 *  从模型生成这些 XML，并用不压缩（stored）的方式打成 ZIP，不引入新库。
 *
 *  包内的部件：
 *    [Content_Types].xml、_rels/.rels、word/document.xml、word/styles.xml、word/numbering.xml、word/_rels/document.xml.rels。
 *  对应关系：
 *    标题 → 内置「Heading1 ~ 6」样式（导航窗格、目录可用）；加粗 / 斜体 / 删除线 → 字符格式；行内代码 → Consolas + 浅灰底纹；
 *    链接 → 外部超链接（只放行 http、https、mailto）；有序 / 无序列表 → numbering.xml 的列表编号（嵌套用级别，有序列表各自
 *    从起始编号开始）；任务列表 → 行首 ☐ / ☑；引用 → 「Quote」样式（左缩进加左边框）；代码块 → 每行一个「CodeBlock」段落
 *    （等宽字体、灰色底纹、保留缩进与制表符）；表格 → Word 表格（表头行底色、列对齐、重复表头）；分隔线 → 段落下边框；
 *    图片 → 链接文字（与卡片、预览一致，不下载图片）；强制换行 → <w:br/>。
 *
 *  典型用法：
 *      const std::string docx = flamingomd::BuildMdDocx(doc->m_model);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "MdModel.h"

namespace flamingomd {

/**
 *  ZIP 包里的一个文件。
 */
struct MdZipEntry
{
    std::string m_name;   //包内路径，用 / 分隔（如 word/document.xml）
    std::string m_data;   //文件内容
};

/**
 *  生成 .docx 文件内容。
 *  @param model 文档模型
 *  @return .docx 文件的全部字节
 */
std::string BuildMdDocx(const MdModel& model);

/**
 *  生成 word/document.xml 与其中用到的超链接地址（按关系编号 rId3、rId4…… 的顺序）。单元测试单独检查 XML 内容用。
 *  @param model         文档模型
 *  @param hyperlinks    [出参] 超链接地址，第 i 个对应关系编号 rId(3 + i)
 *  @param orderedStarts [出参] 每个有序列表的起始编号，第 i 个对应编号定义 numId(2 + i)
 *  @return document.xml 的内容（UTF-8）
 */
std::string BuildMdDocxDocumentXml(const MdModel& model, std::vector<std::wstring>& hyperlinks,
                                   std::vector<unsigned>& orderedStarts);

/**
 *  把若干文件打成不压缩（stored）的 ZIP 包。
 *  @param entries 文件，按顺序写入
 *  @return ZIP 文件的全部字节
 */
std::string BuildMdStoredZip(const std::vector<MdZipEntry>& entries);

/**
 *  计算 CRC-32（ZIP 使用的 IEEE 802.3 多项式）。
 *  @param data  数据
 *  @param bytes 字节数
 *  @return CRC-32 值
 */
uint32_t MdCrc32(const void* data, size_t bytes);

} // namespace flamingomd
