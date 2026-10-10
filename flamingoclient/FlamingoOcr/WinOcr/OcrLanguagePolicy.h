/**
 *  默认识别语言的选择（纯函数，配单元测试 OCRS1）。
 *
 *  界面语言（flamingo.ini 的 [ui] language，如 zh-CN）与识别语言（Windows.Media.Ocr 的 BCP-47 代码，如 zh-Hans-CN）
 *  写法不同：简体中文的识别语言带文字代码 Hans，繁体带 Hant。本模块把界面语言换成识别语言的代码前缀，
 *  再到可用列表里按前缀找。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <string>
#include <vector>

namespace flamingoocr {

/**
 *  把界面语言换成识别语言的代码前缀：
 *    zh-CN、zh-SG、zh-Hans… → zh-Hans；zh-TW、zh-HK、zh-MO、zh-Hant… → zh-Hant；只有 zh → zh-Hans；
 *    其余取主语言代码（en-US → en，ja-JP → ja）。
 *  @param uiLanguage 界面语言代码，可为 NULL 或空串
 *  @return 识别语言的代码前缀；uiLanguage 为 NULL 或空串时返回空串
 */
std::wstring OcrLanguagePrefixForUi(const wchar_t* uiLanguage);

/**
 *  在识别语言列表里按前缀找第一个匹配项：代码与前缀相同，或以「前缀-」开头（不区分大小写）。
 *  @param tags   可用的识别语言代码
 *  @param prefix 代码前缀，为空串时找不到
 *  @return 匹配项的下标；找不到时返回 -1
 */
int FindOcrLanguageByPrefix(const std::vector<std::wstring>& tags, const std::wstring& prefix);

/**
 *  判断两个语言代码是否相同（不区分大小写）。
 */
bool SameLanguageTag(const std::wstring& a, const std::wstring& b);

} // namespace flamingoocr
