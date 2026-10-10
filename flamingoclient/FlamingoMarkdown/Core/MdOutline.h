/**
 *  从 Markdown 文档模型里取卡片要显示的标题与摘要（docs/Markdown消息方案.md 第 11.3 节）：聊天窗里 Markdown 消息
 *  显示为「标题 + 几行摘要」的卡片，这两段文字由本模块从模型生成，宿主经 m_getCardText 取得。
 *
 *  规则：
 *    - 标题取文档里第一个标题块的文字；没有标题块时取第一个有文字的块（段落、单元格、代码块）的第一行；
 *    - 摘要是其余各块的文字依次拼接：换行、制表符换成空格，连续空白压成一个，去掉首尾空白，最长 kMdCardSummaryMaxChars；
 *      标题取自某块第一行时，该块剩下的行也算进摘要。
 *
 *  典型用法：
 *      std::wstring title, summary;
 *      flamingomd::BuildMdCardText(doc->m_model, title, summary);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "MdModel.h"

namespace flamingomd {

//摘要的最大长度（UTF-16 个数）：卡片最多显示三行，宿主再按宽度截断，这里只防止超长文档拼出很长的字符串
const size_t kMdCardSummaryMaxChars = 400;

//标题的最大长度（UTF-16 个数）：卡片只显示一行，宿主再按宽度截断
const size_t kMdCardTitleMaxChars = 200;

/**
 *  生成卡片的标题与摘要。
 *  @param model   文档模型
 *  @param title   [出参] 标题；文档没有任何文字时为空串
 *  @param summary [出参] 摘要；没有其余文字时为空串
 */
void BuildMdCardText(const MdModel& model, std::wstring& title, std::wstring& summary);

} // namespace flamingomd
