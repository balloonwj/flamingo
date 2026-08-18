/**
 *  汉字 → 完整拼音（全拼）匹配，PinyinFull.h
 *
 *  与同目录的 PinyinInitials 是两件事：那边取的是<u>首字母</u>（"苏沐辰" → "smc"，
 *  与原文 1:1 等长，便于把命中区间映射回原文做高亮）；本模块给的是<u>完整音节</u>
 *  （"苏沐辰" → su / mu / chen），用于"输入 sumuchen 或 sumu 也能搜到苏沐辰"。
 *
 *  数据来自 Unicode 官方 Unihan 数据库的 kMandarin 字段，经 tools/gen_pinyin_table.py
 *  生成成静态查表 PinyinTable.inc（覆盖 CJK 基本区约两万字，多音字取最常用读音，
 *  去声调、ü 记作 v）。要更新数据重跑该脚本即可，不要手工改 .inc。
 *
 *  纯逻辑，不依赖 UI，便于单元测试，见
 *  flamingoclient/Source/Tests/PinyinFullTests.cpp。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <string>
#include <vector>

namespace pinyin {

/**
 *  取单个字符的完整拼音（小写、无声调、ü 记作 v）。
 *  @param ch 待查字符（UTF-16）
 *  @return 该汉字的拼音音节；不是汉字、或表里没有它的读音时返回空串
 */
std::string FullPinyinOf(wchar_t ch);

/**
 *  把一段文本拆成"音节序列"，供全拼匹配使用。
 *
 *  逐字符处理：汉字取其拼音音节；ASCII 字母 / 数字取其自身小写（视作长度 1 的
 *  音节，使"张三abc"这种混排也能被 "zhangsanabc" 搜到）；其余字符（空格、标点、
 *  表外生僻字）产出<u>空音节</u>占位 —— 占位既不参与匹配，也会打断跨越它的连续
 *  匹配，避免"张三 李四"被 "sanli" 命中这种越界联想。
 *
 *  @param text 源文本（UTF-16）
 *  @return 与 text 等长的音节数组，第 i 项对应第 i 个字符
 */
std::vector<std::string> SyllablesOf(const std::wstring& text);

/**
 *  取文本的拼音首字母串（每个字取其音节的第一个字母）。
 *
 *  与同目录 PinyinInitials 的 Initials() 是同一件事的两套实现，差别在覆盖面与用途：
 *    · Initials()  —— 用 GB2312 <u>一级</u>汉字（区位码 ≤ 5589）的分段边界表推首字母，
 *                     产出与原文 1:1 等长的串，便于把命中区间映射回原文做高亮，
 *                     主面板搜索结果的黄底高亮依赖这个特性；代价是二级字取不到
 *                     首字母（如"沐"的区位码 6769 就在覆盖之外）。
 *    · 本函数      —— 走 Unihan 全表（约两万字），二级字、生僻字都有；同样保持 1:1
 *                     等长，但设计用途是<u>匹配</u>而非高亮。
 *  选人对话框的搜索框用本函数，为的是"苏沐辰"这类含二级字的名字也能用 smc 搜到。
 *
 *  @param text 源文本（UTF-16）
 *  @return 与 text 等长的首字母串（小写）：汉字取其拼音首字母，ASCII 字母 / 数字取
 *          自身小写，其余字符（标点、空格、表外字）取占位符 '\x01' —— 该字符不会
 *          出现在正常查询里，从而既占住位置又不会被误命中
 */
std::string InitialsOf(const std::wstring& text);

/**
 *  判断查询词是否命中某段文本的全拼。
 *
 *  匹配语义：查询词必须<u>从某个音节的开头</u>起、沿音节顺序连续匹配下去，
 *  允许在最后一个音节中途结束（前缀）。以"苏沐辰"（su / mu / chen）为例：
 *    · "sumuchen" / "sumu" / "su" / "muchen" / "much" —— 命中；
 *    · "umuch" —— 不命中（从音节中间起头，属于误联想）。
 *
 *  @param text        被搜的文本（UTF-16，如联系人显示名）
 *  @param queryLower  查询词，须为<u>已转小写</u>的 ASCII 串；含非 ASCII 字符时
 *                     本函数直接返回 false（那种查询该走原文子串匹配）
 *  @return 命中返回 true
 */
bool MatchesFullPinyin(const std::wstring& text, const std::string& queryLower);

} // namespace pinyin
