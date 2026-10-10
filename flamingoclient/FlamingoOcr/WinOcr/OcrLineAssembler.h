/**
 *  拼行整理（纯函数，不依赖 WinRT，配单元测试 OCRL1 ~ OCRL6）。
 *
 *  Windows.Media.Ocr 给出的 OcrLine::Text 在每个汉字之间都插了空格，数字有时也被拆开（「1 0 月」）。
 *  本模块按词框之间的间距重新拼出一行文字：
 *    - 同一行的词按横坐标排序；
 *    - 两个汉字（含全角标点、假名、谚文）之间不加空格；
 *    - 汉字与非汉字之间，间距超过行高的 kMixedSpaceRatio 倍才加一个空格；
 *    - 非汉字之间，间距超过行高的 kLatinSpaceRatio 倍才加一个空格（于是被拆开的数字、单词会合并）。
 *  行高取本行含汉字的词的高度中位数，没有含汉字的词时取全部词。两个比例是第 0 期在九张样本图上
 *  网格搜索得出的（docs/图片文字识别方案.md 第 4.3 节）。
 *
 *  同时提供第一遍识别后的「行高中位数」，供 OcrScalePolicy 决定放大倍数。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <string>
#include <vector>

#include "../FlamingoOcrApi.h"

namespace flamingoocr {

//汉字与非汉字之间：间距超过行高的这一比例才加空格
const float kMixedSpaceRatio = 0.25f;

//非汉字之间：间距超过行高的这一比例才加空格
const float kLatinSpaceRatio = 0.35f;

/**
 *  引擎给出的一个词。
 */
struct OcrWordBox
{
    std::wstring m_text;   //词的文字
    OcrRect      m_rect;   //词的外接矩形

    OcrWordBox();
};

/**
 *  判断一个字符是否按「汉字」对待：中日韩统一表意文字及扩展 A、兼容表意文字、部首、中日韩符号与标点、
 *  假名、注音、谚文、全角与半角形式、竖排标点；UTF-16 代理项（扩展 B 及以后的汉字）也按汉字对待。
 *  @param ch 字符
 *  @return 按汉字对待时返回 true
 */
bool IsCjkChar(wchar_t ch);

/**
 *  一行的行高：本行含汉字的词的高度中位数；没有含汉字的词时取全部词的高度中位数；没有词时为 0。
 *  个数为偶数时取中间两个的平均值。
 *  @param words 本行的词，顺序不限
 *  @return 行高（像素）
 */
float LineHeightOf(const std::vector<OcrWordBox>& words);

/**
 *  一次识别结果的行高中位数：先算出每一行（至少有一个词）的行高，再取中位数。
 *  @param lines 各行的词
 *  @return 行高中位数（像素）；没有任何行时为 0
 */
float MedianLineHeight(const std::vector<std::vector<OcrWordBox>>& lines);

/**
 *  按词框间距重新拼出一行文字。
 *  @param words 本行的词，顺序不限（内部按横坐标排序）；文字为空的词被跳过
 *  @return 拼好的一行文字
 */
std::wstring AssembleLineText(const std::vector<OcrWordBox>& words);

/**
 *  按横坐标排好序的词（稳定排序，横坐标相同时保持原顺序）。
 *  @param words 本行的词
 *  @return 排序后的副本
 */
std::vector<OcrWordBox> SortWordsByX(const std::vector<OcrWordBox>& words);

/**
 *  所有词框的并集。
 *  @param words 本行的词
 *  @return 外接矩形；没有词时各字段为 0
 */
OcrRect UnionRect(const std::vector<OcrWordBox>& words);

} // namespace flamingoocr
