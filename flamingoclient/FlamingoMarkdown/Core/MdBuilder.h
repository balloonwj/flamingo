/**
 *  用 md4c 解析 Markdown 源文本，生成文档模型（MdModel），并生成渲染后文字。
 *
 *  启用的 md4c 扩展：表格、删除线、任务列表、宽松自动链接（网址、www.、电子邮件），以及「软换行按硬换行处理」
 *  （聊天里单个换行就换行，见 docs/Markdown消息方案.md 第 3.4 节）。关闭原始 HTML（按文字原样显示）。
 *
 *  链接只保留 http、https、mailto 三种地址（www. 开头的补 http://，裸电子邮件补 mailto:），其余地址的链接
 *  文字按普通文字显示。图片语法转成链接，说明文字前加 MdBuildOptions::m_imageLinkPrefix；说明为空时显示地址。
 *
 *  典型用法：
 *
 *      flamingomd::MdModel model;
 *      flamingomd::MdBuildOptions options;
 *      int rc = flamingomd::BuildMdModel(utf8, bytes, options, model);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stddef.h>
#include <string>

#include "MdModel.h"

namespace flamingomd {

//块的嵌套层数上限：超过的层不再单独成块，其中的文字并入外层（防止恶意构造的深层嵌套拖慢排版）
const int kMdMaxBlockDepth = 24;

//块总数上限：超过后其余内容不再显示，文档标记为已截断
const size_t kMdMaxBlockCount = 20000;

//表格列数上限：超出的列不显示，文档标记为已截断
const int kMdMaxTableColumns = 32;

//代码块里 Tab 展开所用的制表位宽度（列数）
const int kMdTabWidth = 4;

/**
 *  解析选项（DLL 内部使用，由导出函数从 MdParseOptions 转换而来）。
 */
struct MdBuildOptions
{
    std::wstring m_imageLinkPrefix;   //图片语法显示成链接时加在说明前面的文字；为空表示不加
};

/**
 *  解析源文本，生成文档模型（含渲染后文字）。
 *  @param utf8    源文本（UTF-8），bytes 为 0 时可为 NULL
 *  @param bytes   源文本字节数
 *  @param options 解析选项
 *  @param model   [出参] 生成的模型；调用前应为空
 *  @return md_ok；内存不足返回 md_out_of_memory；解析器失败返回 md_internal_error（取值见 MdResultCode）
 */
int BuildMdModel(const char* utf8, size_t bytes, const MdBuildOptions& options, MdModel& model);

/**
 *  本模块交给 md4c 的扩展标志（MD_FLAG_* 的组合），单元测试核对用。
 */
unsigned GetMdParserFlags();

/**
 *  规范化链接地址：只接受 http、https、mailto；www. 开头的补 http://，不带协议的电子邮件补 mailto:。
 *  @param href 源文本里的地址（已解码实体）
 *  @return 规范化后的地址；不接受的地址返回空串
 */
std::wstring NormalizeLinkUrl(const std::wstring& href);

} // namespace flamingomd
