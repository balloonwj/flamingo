/**
 *  内嵌 XML 布局的文字翻译：在交给 balloonui 的 DuiXmlBuilder 之前，把 XML 里的界面文字属性换成当前语言的译文。
 *
 *  只替换 text、placeholder、title、tooltip 四个属性的值，其余字节原样保留；其它属性里的中文属于用户数据
 *  （如通讯录头像占位的 name="江"），不翻译。替换在字符串层面完成，因此 DuiXmlBuilder::FromString 与
 *  FromFrameXml 两种入口都能用。译文写回前把 & < > " ' 转义成实体，与 DuiXmlBuilder 解码属性值时认的
 *  五种实体一致。0b 步原型与真实解析器的比对结果见 docs/多语言方案.md 第 5.3.1 节。
 *
 *  典型用法：
 *      std::string xml = i18n::TranslateXml(kLoginBodyXml);
 *      std::unique_ptr<DuiControl> root = DuiXmlBuilder::FromString(xml.c_str(), factory);
 *
 *  本文件与 I18nXml.cpp 不依赖 balloonui，也不包含 stdafx.h；在 Flamingo.vcxproj 里须标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

namespace i18n {

/**
 *  翻译一个属性值的函数类型。
 *  @param utf8 已还原实体的属性值（UTF-8）
 *  @return 译文（UTF-8）；没有译文时原样返回
 */
typedef std::string (*XmlTextTranslateFn)(const std::string& utf8);

/**
 *  用给定的翻译函数替换 XML 里四个文字属性的值。
 *  @param xmlUtf8  XML 文本（UTF-8）
 *  @param fn       翻译函数，不能为 NULL
 *  @param replaced [出参] 实际被替换的属性个数，可为 NULL
 *  @return 替换后的 XML；属性值为空、或翻译函数原样返回时，该属性的原始字节不变
 */
std::string TranslateXmlAttrs(const std::string& xmlUtf8, XmlTextTranslateFn fn, int* replaced);

/**
 *  按当前语言翻译 XML 里的文字属性（翻译函数为 i18n::Translate）。当前语言是简体中文时直接返回原文，不做扫描。
 *  @param xmlUtf8 XML 文本（UTF-8），为 NULL 时视为空串
 *  @return 翻译后的 XML
 */
std::string TranslateXml(const char* xmlUtf8);

} // namespace i18n
