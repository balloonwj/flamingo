/**
 *  ConfigTextCodec.h
 *
 *  把任意 Unicode 文字编码成纯 ASCII，存进用户配置的 ini 文件，读出时再还原。
 *
 *  为什么需要：用户配置文件（Users\<账号>\<账号>.ini）是 ANSI 编码，WritePrivateProfileString
 *  会把宽字符串按系统代码页转换后再写盘，代码页里没有的字符（表情、部分生僻字）在转换时丢失，
 *  读回来就不是原来的文字了。先转成 UTF-8、再把每个字节写成两位十六进制，写进去的只有 ASCII，
 *  任何代码页下读写都不会变。目前用于自动回复的两段文字（见 CUserConfig 的 [AutoReply] 段）。
 *
 *  典型用法：
 *      iniFile.WriteString(_T("AutoReply"), _T("AwayText"),
 *                          cfgtext::EncodeConfigText(text).c_str(), path);
 *      std::wstring text;
 *      if (!cfgtext::DecodeConfigText(stored, text))
 *          text.clear();   //配置被手工改坏：按没有设置处理
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

namespace cfgtext {

/**
 *  把文字编码成配置里存的形式：UTF-8 字节逐个写成两位小写十六进制。
 *    text：原文，可含任意 Unicode 字符（含表情、换行）。
 *  @return 编码结果，只含 0-9 与 a-f；原文为空时返回空串。本函数无副作用。
 */
std::wstring EncodeConfigText(const std::wstring& text);

/**
 *  把 EncodeConfigText 的结果还原成原文。
 *    encoded：配置里读出的编码串；空串还原为空串。
 *    out：    [出参] 还原出的原文；返回 false 时清空。
 *  @return 成功返回 true；长度为奇数、含十六进制以外的字符、或字节不是合法的 UTF-8 时返回 false。
 */
bool DecodeConfigText(const std::wstring& encoded, std::wstring& out);

} // namespace cfgtext
