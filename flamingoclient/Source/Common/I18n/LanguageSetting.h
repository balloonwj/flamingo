/**
 *  界面语言的选择：列出语言包里有哪些语言、组装语言选择菜单 / 下拉框的选项、读写 flamingo.ini 的 [ui] language，
 *  以及判断改了语言之后是否需要重启。设置窗「通用」页的「语言」下拉框与登录窗底部的语言链接共用本模块
 *  （docs/多语言方案.md 第 7 节）。
 *
 *  选项的顺序固定为：跟随系统、简体中文、语言包里的语言（按语言包登记的顺序）。语言名称用各语言自己的写法
 *  （English、日本語 ……），不随界面语言翻译，用户在任何界面下都认得出自己的语言；只有「跟随系统」一项翻译。
 *
 *  典型用法：
 *      std::vector<i18n::LanguageChoice> pack;
 *      if (i18n::ListPackLanguages(dllPath, pack))          //语言包不存在时不提供语言选择
 *      {
 *          std::vector<i18n::LanguageChoice> choices = i18n::BuildLanguageChoices(pack);
 *          int current = i18n::FindLanguageChoice(choices, i18n::ReadConfiguredLanguage(iniPath));
 *          ...
 *      }
 *
 *  本文件与 LanguageSetting.cpp 只依赖 <windows.h>、标准库与 I18n.h，不包含 stdafx.h；
 *  在 Flamingo.vcxproj 里须标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace i18n {

/**
 *  一个语言选项。
 */
struct LanguageChoice
{
    std::wstring m_code;    //语言代码，写进 [ui] language 的值；空串表示跟随系统
    std::wstring m_name;    //显示名称：语言自己的写法；「跟随系统」一项为当前界面语言的译文
};

/**
 *  列出语言包里的语言（不含简体中文与伪语言）。语言包尚未加载时按需加载，加载后不卸载。
 *  @param dllPath 语言包的完整路径
 *  @param out     [出参] 语言列表，顺序与语言包登记的顺序一致
 *  @return 语言包存在且有效时返回 true；不存在、无法加载或函数表不完整时返回 false，此时不应提供语言选择
 */
bool ListPackLanguages(const std::wstring& dllPath, std::vector<LanguageChoice>& out);

/**
 *  组装语言选项：跟随系统、简体中文、pack 里的语言。
 *  @param pack ListPackLanguages 的结果
 *  @return 选项列表，第 0 项为跟随系统，第 1 项为简体中文
 */
std::vector<LanguageChoice> BuildLanguageChoices(const std::vector<LanguageChoice>& pack);

/**
 *  找配置值对应的选项。
 *  @param choices    BuildLanguageChoices 的结果
 *  @param configured [ui] language 的值；空串（或全是空白）表示跟随系统
 *  @return 选项下标；配置值不在选项里（如手工写的伪语言代码或语言包没有的语言）时返回 -1
 */
int FindLanguageChoice(const std::vector<LanguageChoice>& choices, const std::wstring& configured);

/**
 *  取某种语言的显示名称，用于登录窗语言链接上的文字。
 *  @param pack ListPackLanguages 的结果
 *  @param code 语言代码
 *  @return 简体中文为「简体中文」，语言包里的语言为它自己的写法，其余原样返回 code
 */
std::wstring GetLanguageDisplayName(const std::vector<LanguageChoice>& pack, const std::wstring& code);

/**
 *  读 flamingo.ini 的 [ui] language。
 *  @param iniPath flamingo.ini 的完整路径
 *  @return 配置值，已去掉首尾空白；没有这一项时为空串
 */
std::wstring ReadConfiguredLanguage(const std::wstring& iniPath);

/**
 *  写 flamingo.ini 的 [ui] language。
 *  @param iniPath flamingo.ini 的完整路径
 *  @param code    语言代码；空串表示跟随系统，此时删除这一项
 *  @return 写入成功返回 true
 */
bool WriteConfiguredLanguage(const std::wstring& iniPath, const std::wstring& code);

/**
 *  判断把配置改成 newConfigured 之后，下次启动生效的语言是否与当前生效的语言不同（即是否需要重启）。
 *  @param newConfigured 新的 [ui] language 值，空串表示跟随系统
 *  @param systemUiLang  系统界面语言
 *  @return 需要重启才能看到变化时返回 true
 */
bool LanguageChangeNeedsRestart(const std::wstring& newConfigured, LANGID systemUiLang);

} // namespace i18n
