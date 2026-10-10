/**
 *  MarkdownFindReplace.h
 *
 *  Markdown 编辑器查找与替换的纯逻辑（docs/Markdown消息方案.md 第 13 节），不依赖窗口，FlamingoUnit 直接测试：
 *    - 查找下一个 / 上一个：从选区处开始，到全文末尾（或开头）后回绕；可选区分大小写；
 *    - 全部替换：从头到尾替换不重叠的匹配，结果是一个替换区间，编辑器一次 ReplaceSel 写回、一步撤销；
 *    - 替换当前：选区正好是匹配时替换它并找下一个，否则只找下一个。
 *  不区分大小写时按 Unicode 小写比较（CharLowerBuffW，逐个 UTF-16 码元映射，下标与原文一致）。
 *
 *  典型用法：
 *      int start = 0;
 *      int end = 0;
 *      if (balloonmd::FindMarkdownText(text, query, selEnd, true, false, start, end, NULL))
 *          editor->SetSel(start, end);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>

#include "../BalloonChatWnd/MarkdownEditCommands.h"   // MarkdownEditResult

namespace balloonmd {

//编辑器里与查找有关的按键
enum MarkdownFindKey
{
    md_find_key_none         = 0,   //不是查找按键
    md_find_key_open_find    = 1,   //Ctrl+F：显示查找行
    md_find_key_open_replace = 2,   //Ctrl+H：显示查找行与替换行
    md_find_key_next         = 3,   //F3：查找下一个
    md_find_key_prev         = 4,   //Shift+F3：查找上一个
};

/**
 *  按键与修饰键对应的查找操作。多按或少按修饰键都不算（Ctrl+Shift+F、Alt+F3 等返回 md_find_key_none）。
 *  @param vk    虚拟键码（WM_KEYDOWN 的 wParam）
 *  @param ctrl  是否按着 Ctrl
 *  @param shift 是否按着 Shift
 *  @param alt   是否按着 Alt
 *  @return 取值见 enum MarkdownFindKey
 */
int MarkdownFindKeyFor(unsigned vk, bool ctrl, bool shift, bool alt);

/**
 *  查找下一个或上一个匹配。
 *  @param text      全文（编辑框里的文字，段落之间为 \r）
 *  @param query     查找词；为空时不查
 *  @param from      起点（字符下标，越界时截断）：向后找时从这里开始（含），通常取选区终点；向前找时找终点不超过
 *                   这里的匹配，通常取选区起点
 *  @param forward   true 向后找（F3、「下一个」），false 向前找（Shift+F3、「上一个」）
 *  @param matchCase 是否区分大小写
 *  @param outStart  [出参] 匹配的起点；没找到时不修改
 *  @param outEnd    [出参] 匹配的终点（不含）；没找到时不修改
 *  @param wrapped   [出参，可为 NULL] 是否越过了全文末尾（向前找时为开头）回绕后才找到
 *  @return 找到返回 true；查找词为空或全文里没有时返回 false
 */
bool FindMarkdownText(const std::wstring& text, const std::wstring& query, int from, bool forward, bool matchCase,
                      int& outStart, int& outEnd, bool* wrapped);

/**
 *  统计全文里不重叠的匹配个数（查找条上显示「共 N 处」）。
 *  @param text      全文
 *  @param query     查找词；为空时返回 0
 *  @param matchCase 是否区分大小写
 *  @return 匹配个数
 */
int CountMarkdownMatches(const std::wstring& text, const std::wstring& query, bool matchCase);

/**
 *  全部替换：从头到尾依次把不重叠的匹配换成替换词。替换进去的文字不再参与查找，替换词里含有查找词也不会重复替换。
 *  @param text        全文
 *  @param query       查找词；为空时不替换
 *  @param replacement 替换词，可为空（即删除匹配）
 *  @param matchCase   是否区分大小写
 *  @param out         [出参] 结果：把 [m_replaceStart, m_replaceEnd) 换成 m_replacement 即得到新全文（m_newText），
 *                     光标停在最后一处替换之后；没有匹配时替换区间为空、全文不变
 *  @return 替换的次数
 */
int ReplaceAllMarkdownText(const std::wstring& text, const std::wstring& query, const std::wstring& replacement,
                           bool matchCase, balloonchatwnd::MarkdownEditResult& out);

/**
 *  替换当前（查找条的「替换」按钮）：选区正好是一个匹配时把它换成替换词，再从替换词之后向后找下一个并选中；
 *  选区不是匹配时不替换，只从选区起点向后找下一个并选中。
 *  @param text        全文
 *  @param selStart    选区起点（与 selEnd 顺序不限，越界时截断）
 *  @param selEnd      选区终点
 *  @param query       查找词；为空时不做任何事
 *  @param replacement 替换词，可为空
 *  @param matchCase   是否区分大小写
 *  @param out         [出参] 结果：替换区间（没有替换时为空）与之后的选区；找不到下一个时光标停在替换词之后（没有替换时
 *                     选区不变）
 *  @param foundNext   [出参，可为 NULL] 之后是否选中了一个匹配
 *  @return 做了替换返回 true
 */
bool ReplaceCurrentMarkdownText(const std::wstring& text, int selStart, int selEnd, const std::wstring& query,
                                const std::wstring& replacement, bool matchCase,
                                balloonchatwnd::MarkdownEditResult& out, bool* foundNext);

} // namespace balloonmd
