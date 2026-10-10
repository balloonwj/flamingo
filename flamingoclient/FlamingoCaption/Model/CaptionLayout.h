/**
 *  字幕的折行（docs/语音字幕方案.md 第 7、8 节），CaptionLayout.h
 *
 *  把一条字幕的文字按给定的最大宽度折成若干行：中日韩文字之间可以随处断开；英文与数字只在空格处断开，
 *  一个单词比整行还宽时才从中间断开；行首不留空格；文字里的换行符强制换行。宽度由调用方传入的测量函数给出
 *  （画字的代码用同一种字体测量），所以本模块不依赖任何绘图库，可以单测。
 *
 *  典型用法：
 *      std::vector<std::wstring> lines = caption::WrapCaptionText(text, maxWidth, &MeasureThunk, &measurer);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

namespace caption {

/**
 *  测量一段文字的宽度。
 *  @param ctx  调用方的上下文
 *  @param text 要测量的文字（一行，不含换行）
 *  @param len  文字的字符数
 *  @return 宽度（与 maxWidth 同一单位，通常为像素）
 */
typedef float (*CaptionMeasureFn)(void* ctx, const wchar_t* text, int len);

/**
 *  折行。
 *  @param text     文字，可含换行符
 *  @param maxWidth 每行的最大宽度
 *  @param measure  测量函数
 *  @param ctx      交给测量函数的上下文
 *  @return 各行文字（不含换行符）；文字为空时返回空列表
 */
std::vector<std::wstring> WrapCaptionText(const std::wstring& text, float maxWidth, CaptionMeasureFn measure, void* ctx);

} // namespace caption
