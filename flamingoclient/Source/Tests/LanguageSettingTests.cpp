/**
 *  界面语言选择（Common/I18n/LanguageSetting）的单元测试，用例 LS1 ~ LS6（docs/多语言方案.md 第 11 节 1d）。
 *
 *    - LS1 选项顺序：跟随系统、简体中文、语言包里的语言；语言包里万一含简体中文也不重复
 *    - LS2 配置值找选项：空串与空白为跟随系统，不区分大小写、忽略首尾空白，找不到返回 -1
 *    - LS3 显示名称：简体中文、语言包里的语言用各自的写法，未知代码原样返回
 *    - LS4 读写 flamingo.ini 的 [ui] language：写入后读回一致；写空串删除这一项；其它配置项（含 UTF-8 的
 *          apptitle）原样保留
 *    - LS5 用真实的 FlamingoLang.dll 列出语言包里的四种语言；路径不存在时返回 false
 *    - LS6 是否需要重启：按「新配置下次启动生效的语言」与「当前生效的语言」比较
 *
 *  框架 Catch2 v3，标签 [i18n]。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>

#include "../Common/I18n/I18n.h"
#include "../Common/I18n/LanguageSetting.h"

using i18n::LanguageChoice;

namespace {

//系统界面语言 LANGID：简体中文（中国）
const LANGID kLangZhCn = 0x0804;
//英文（美国）
const LANGID kLangEnUs = 0x0409;

//读写 ini 时的缓冲区字符数
const DWORD kIniBufChars = 256;

LanguageChoice MakeChoice(const wchar_t* code, const wchar_t* name)
{
    LanguageChoice c;
    c.m_code = code;
    c.m_name = name;
    return c;
}

std::wstring GetTestExeDirectory()
{
    wchar_t path[MAX_PATH] = { 0 };
    DWORD len = ::GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return std::wstring();
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? std::wstring() : dir.substr(0, pos + 1);
}

std::string ReadFileBytes(const std::wstring& path)
{
    std::string out;
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (fp == NULL)
        return out;
    char buf[1024];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        out.append(buf, n);
    fclose(fp);
    return out;
}

/**
 *  用例结束时把多语言模块恢复到未初始化状态（含中途 REQUIRE 失败退出的情况）：同一测试进程里的其它用例
 *  断言的是中文原文，不能沿用本组用例选定的语言或注入的字体查询函数。
 */
struct ScopedI18nReset
{
    ~ScopedI18nReset()
    {
        i18n::ResetForTest();
        i18n::SetFontProbeForTest(NULL);
    }
};

} // namespace

TEST_CASE("LS1 语言选项的顺序", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    i18n::ResetForTest();
    std::vector<LanguageChoice> pack;
    pack.push_back(MakeChoice(L"zh-TW", L"繁體中文"));
    pack.push_back(MakeChoice(L"zh-CN", L"重复的简体中文"));
    pack.push_back(MakeChoice(L"en-US", L"English"));

    std::vector<LanguageChoice> choices = i18n::BuildLanguageChoices(pack);
    REQUIRE(choices.size() == 4);
    CHECK(choices[0].m_code.empty());
    CHECK(choices[0].m_name == L"跟随系统");
    CHECK(choices[1].m_code == L"zh-CN");
    CHECK(choices[1].m_name == L"简体中文");
    CHECK(choices[2].m_code == L"zh-TW");
    CHECK(choices[2].m_name == L"繁體中文");
    CHECK(choices[3].m_code == L"en-US");
    CHECK(choices[3].m_name == L"English");

    //语言包为空时只有前两项
    CHECK(i18n::BuildLanguageChoices(std::vector<LanguageChoice>()).size() == 2);
}

TEST_CASE("LS2 配置值找对应的选项", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    std::vector<LanguageChoice> pack;
    pack.push_back(MakeChoice(L"zh-TW", L"繁體中文"));
    pack.push_back(MakeChoice(L"en-US", L"English"));
    std::vector<LanguageChoice> choices = i18n::BuildLanguageChoices(pack);

    CHECK(i18n::FindLanguageChoice(choices, L"") == 0);
    CHECK(i18n::FindLanguageChoice(choices, L"   ") == 0);
    CHECK(i18n::FindLanguageChoice(choices, L"zh-CN") == 1);
    CHECK(i18n::FindLanguageChoice(choices, L"ZH-cn") == 1);
    CHECK(i18n::FindLanguageChoice(choices, L"en-US") == 3);
    CHECK(i18n::FindLanguageChoice(choices, L" en-us ") == 3);
    CHECK(i18n::FindLanguageChoice(choices, FLAMINGO_LANG_PSEUDO_CODE) == -1);
    CHECK(i18n::FindLanguageChoice(choices, L"fr-FR") == -1);
}

TEST_CASE("LS3 语言的显示名称", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    std::vector<LanguageChoice> pack;
    pack.push_back(MakeChoice(L"ja-JP", L"日本語"));
    CHECK(i18n::GetLanguageDisplayName(pack, L"zh-CN") == L"简体中文");
    CHECK(i18n::GetLanguageDisplayName(pack, L"ja-jp") == L"日本語");
    CHECK(i18n::GetLanguageDisplayName(pack, FLAMINGO_LANG_PSEUDO_CODE) == FLAMINGO_LANG_PSEUDO_CODE);
}

TEST_CASE("LS4 读写 flamingo.ini 的 [ui] language", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    std::wstring ini = GetTestExeDirectory() + L"i18n_language_setting_test.ini";
    //apptitle 用 UTF-8 写的中文：程序回写别的项时不得改动这一行的原始字节（与 BrandText 的约定一致）
    const std::string apptitleLine = u8"apptitle=火烈鸟";
    std::string content = "[server]\r\nserver=1.2.3.4\r\n[ui]\r\n" + apptitleLine + "\r\nslogan=x\r\n";
    FILE* fp = _wfopen(ini.c_str(), L"wb");
    REQUIRE(fp != NULL);
    fwrite(content.data(), 1, content.size(), fp);
    fclose(fp);

    CHECK(i18n::ReadConfiguredLanguage(ini).empty());

    REQUIRE(i18n::WriteConfiguredLanguage(ini, L"en-US"));
    CHECK(i18n::ReadConfiguredLanguage(ini) == L"en-US");

    REQUIRE(i18n::WriteConfiguredLanguage(ini, L" ja-JP "));
    CHECK(i18n::ReadConfiguredLanguage(ini) == L"ja-JP");

    //写空串（跟随系统）：删除这一项，而不是写一个空值
    REQUIRE(i18n::WriteConfiguredLanguage(ini, L""));
    CHECK(i18n::ReadConfiguredLanguage(ini).empty());
    wchar_t buf[kIniBufChars] = { 0 };
    ::GetPrivateProfileStringW(L"ui", L"language", L"<absent>", buf, kIniBufChars, ini.c_str());
    CHECK(std::wstring(buf) == L"<absent>");

    //其它配置项原样保留
    ::GetPrivateProfileStringW(L"server", L"server", L"", buf, kIniBufChars, ini.c_str());
    CHECK(std::wstring(buf) == L"1.2.3.4");
    CHECK(ReadFileBytes(ini).find(apptitleLine) != std::string::npos);

    ::DeleteFileW(ini.c_str());
}

TEST_CASE("LS5 列出真实语言包里的语言", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    std::vector<LanguageChoice> pack;
    std::wstring dllPath = GetTestExeDirectory() + FLAMINGO_LANG_DLL_NAME;
    REQUIRE(::GetFileAttributesW(dllPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    REQUIRE(i18n::ListPackLanguages(dllPath, pack));
    REQUIRE(pack.size() == 4);
    CHECK(pack[0].m_code == L"zh-TW");
    CHECK(pack[0].m_name == L"繁體中文");
    CHECK(pack[1].m_code == L"en-US");
    CHECK(pack[1].m_name == L"English");
    CHECK(pack[2].m_code == L"ja-JP");
    CHECK(pack[2].m_name == L"日本語");
    CHECK(pack[3].m_code == L"ko-KR");
    CHECK(pack[3].m_name == L"한국어");

    std::vector<LanguageChoice> none;
    CHECK_FALSE(i18n::ListPackLanguages(GetTestExeDirectory() + L"no_such_dir\\FlamingoLang.dll", none));
    CHECK(none.empty());
}

TEST_CASE("LS6 改了语言之后是否需要重启", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    SECTION("当前为简体中文")
    {
        i18n::ResetForTest();
        CHECK_FALSE(i18n::LanguageChangeNeedsRestart(L"", kLangZhCn));
        CHECK_FALSE(i18n::LanguageChangeNeedsRestart(L"zh-CN", kLangEnUs));
        CHECK(i18n::LanguageChangeNeedsRestart(L"en-US", kLangZhCn));
        CHECK(i18n::LanguageChangeNeedsRestart(L"", kLangEnUs));
    }

    SECTION("当前为英文")
    {
        i18n::ResetForTest();
        std::wstring dllPath = GetTestExeDirectory() + FLAMINGO_LANG_DLL_NAME;
        REQUIRE(i18n::Init(L"en-US", dllPath, kLangZhCn).m_status == i18n::init_ok);
        CHECK_FALSE(i18n::LanguageChangeNeedsRestart(L"en-US", kLangZhCn));
        CHECK_FALSE(i18n::LanguageChangeNeedsRestart(L"EN-us", kLangZhCn));
        CHECK_FALSE(i18n::LanguageChangeNeedsRestart(L"", kLangEnUs));
        CHECK(i18n::LanguageChangeNeedsRestart(L"", kLangZhCn));
        CHECK(i18n::LanguageChangeNeedsRestart(L"zh-CN", kLangEnUs));
        i18n::ResetForTest();
    }
}
