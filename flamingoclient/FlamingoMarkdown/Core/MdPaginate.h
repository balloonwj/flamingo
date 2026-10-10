/**
 *  按页切分已排版的 Markdown 文档（docs/Markdown消息方案.md 第 11.4 节「导出 .pdf」）：给出每一页在文档里的起始纵坐标，
 *  宿主逐页把文档画到打印机 DC 上（把文档向上平移到该页的起点，并裁剪到页面范围）。
 *
 *  切分只发生在「不落在任何一行中间」的位置：行与行之间的空隙，或某一行的顶边恰好没有被别的行（如同一表格行里更高的
 *  单元格）跨过。一页放不下的单行（极少见，如超高的表格行）从页面高度处硬切。
 *
 *  典型用法（先按页面宽度排版）：
 *      std::vector<int> starts;
 *      flamingomd::PaginateMdLayout(doc->m_layout, pageHeight, starts);   //starts[0] 恒为 0
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <vector>

#include "MdLayout.h"

namespace flamingomd {

/**
 *  切分页面。
 *  @param layout     排版结果
 *  @param pageHeight 一页可用的高度（与排版同一单位），须大于 0
 *  @param starts     [出参] 各页的起始纵坐标，升序；第一页为 0；文档为空时只有一个 0
 */
void PaginateMdLayout(const MdLayoutResult& layout, int pageHeight, std::vector<int>& starts);

} // namespace flamingomd
