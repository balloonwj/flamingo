/**
 *  内嵌 XML 布局的文字翻译（Common/I18n/I18nXml）的单元测试，用例 I9。
 *
 *  用例（2026-10-03 与用户确认，见 docs/多语言方案.md 第 11 节 1b）：
 *    - I9 TranslateXml 只替换 text / placeholder / title / tooltip 四个属性，其余字节不变；译文里的 & " < > '
 *         正确转义。用 balloonui 真实的 DuiXmlBuilder::Parse 解析替换前后的 XML，逐节点比对结构，
 *         沿用 0b 步原型的验证方法（docs/多语言方案.md 第 5.3.1 节）。
 *
 *  框架 Catch2 v3，标签 [i18n]。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <string>

#include "../Common/I18n/I18n.h"
#include "../Common/I18n/I18nXml.h"
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"

using balloonwjui::DuiXmlBuilder;

namespace {

//四个界面文字属性
const char* const kTextAttrs[] = { "text", "placeholder", "title", "tooltip" };

bool IsTextAttr(const std::string& name)
{
    for (size_t i = 0; i < sizeof(kTextAttrs) / sizeof(kTextAttrs[0]); ++i)
    {
        if (name == kTextAttrs[i])
            return true;
    }
    return false;
}

//判断 UTF-8 文字里是否有汉字（U+4E00 ~ U+9FFF 的首字节为 0xE4 ~ 0xE9）
bool HasCjk(const std::string& utf8)
{
    for (size_t i = 0; i < utf8.size(); ++i)
    {
        unsigned char c = (unsigned char)utf8[i];
        if (c >= 0xE4 && c <= 0xE9)
            return true;
    }
    return false;
}

//伪翻译：含汉字的值前后加方括号
std::string BracketTranslate(const std::string& utf8)
{
    if (!HasCjk(utf8))
        return utf8;
    return "[" + utf8 + "]";
}

//带全部五个特殊字符的译文
const std::string kTrickyText = "Cu&t \"x\" <y> 'z' &amp;";

std::string TrickyTranslate(const std::string& utf8)
{
    if (!HasCjk(utf8))
        return utf8;
    return kTrickyText;
}

/**
 *  逐节点比对：标签、属性集合、层级都相同；文字属性的值等于 expectFn 作用于原值的结果，其它属性不变。
 *  @return 不一致之处的个数；changed 累计取值发生变化的属性个数
 */
int CompareNodes(const DuiXmlBuilder::Node& a, const DuiXmlBuilder::Node& b, i18n::XmlTextTranslateFn expectFn,
                 int& changed)
{
    int problems = 0;
    if (a.tag != b.tag || a.attrs.size() != b.attrs.size() || a.children.size() != b.children.size())
        return 1;
    for (std::map<std::string, std::string>::const_iterator it = a.attrs.begin(); it != a.attrs.end(); ++it)
    {
        std::map<std::string, std::string>::const_iterator jt = b.attrs.find(it->first);
        if (jt == b.attrs.end())
        {
            ++problems;
            continue;
        }
        std::string expect = IsTextAttr(it->first) ? expectFn(it->second) : it->second;
        if (jt->second != expect)
            ++problems;
        if (jt->second != it->second)
            ++changed;
    }
    for (size_t i = 0; i < a.children.size(); ++i)
        problems += CompareNodes(a.children[i], b.children[i], expectFn, changed);
    return problems;
}

//一段覆盖各种写法的布局：四个文字属性、单双引号、自定义标签、头像姓名、注释、空值与不含汉字的值
const char* const kSampleXml = u8R"xml(
<vbox padding="1,0,1,1" gap="0">
  <!-- <label text="注释里的文字"/> -->
  <auth-banner id="140" fixedHeight="240"/>
  <hbox fixedHeight="22" gap="12">
    <auth-check id="110" text="记住密码" weight="1"/>
    <auth-link id='132' text='忘记密码？' link-color="255,122,69" weight="1"/>
  </hbox>
  <edit id="200" placeholder="搜索联系人、群、消息" tooltip="输入关键词" fixedHeight="36"/>
  <avatar id="1000" name="江" shape="round-rect"/>
  <label id="151" fixedHeight="16" text="" text-color="107,107,107"/>
  <label id="152" text="1:1" title="窗口标题"/>
</vbox>
)xml";

//kSampleXml 中值含汉字的文字属性个数：记住密码、忘记密码？、搜索…、输入关键词、窗口标题
const int kSampleTextAttrsWithCjk = 5;

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

TEST_CASE("I9 TranslateXml 只替换四个文字属性并正确转义", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    DuiXmlBuilder::Node before;
    REQUIRE(DuiXmlBuilder::Parse(kSampleXml, before));

    SECTION("替换后结构不变，只有文字属性变了")
    {
        int replaced = 0;
        std::string out = i18n::TranslateXmlAttrs(kSampleXml, BracketTranslate, &replaced);
        CHECK(replaced == kSampleTextAttrsWithCjk);

        DuiXmlBuilder::Node after;
        REQUIRE(DuiXmlBuilder::Parse(out.c_str(), after));
        int changed = 0;
        CHECK(CompareNodes(before, after, BracketTranslate, changed) == 0);
        CHECK(changed == kSampleTextAttrsWithCjk);

        //注释、头像姓名、空值、不含汉字的值保持原样（按原始字节查找）
        CHECK(out.find(u8"<!-- <label text=\"注释里的文字\"/> -->") != std::string::npos);
        CHECK(out.find(u8"name=\"江\"") != std::string::npos);
        CHECK(out.find("text=\"\" text-color") != std::string::npos);
        CHECK(out.find("text=\"1:1\"") != std::string::npos);
        //单引号写法仍用单引号
        CHECK(out.find(u8"text='[忘记密码？]'") != std::string::npos);
    }

    SECTION("译文里的特殊字符转义后解析出原样的译文")
    {
        int replaced = 0;
        std::string out = i18n::TranslateXmlAttrs(kSampleXml, TrickyTranslate, &replaced);
        CHECK(replaced == kSampleTextAttrsWithCjk);
        DuiXmlBuilder::Node after;
        REQUIRE(DuiXmlBuilder::Parse(out.c_str(), after));
        int changed = 0;
        CHECK(CompareNodes(before, after, TrickyTranslate, changed) == 0);
    }

    SECTION("原文里已有的实体先还原再交给翻译函数")
    {
        const char* xml = u8"<vbox><edit id=\"3\" placeholder=\"输入&quot;关键词&quot;\"/></vbox>";
        int replaced = 0;
        std::string out = i18n::TranslateXmlAttrs(xml, BracketTranslate, &replaced);
        CHECK(replaced == 1);
        DuiXmlBuilder::Node node;
        REQUIRE(DuiXmlBuilder::Parse(out.c_str(), node));
        REQUIRE(node.children.size() == 1);
        CHECK(node.children[0].attrs["placeholder"] == u8"[输入\"关键词\"]");
    }

    SECTION("当前语言是简体中文时 TranslateXml 原样返回")
    {
        i18n::ResetForTest();
        CHECK(i18n::TranslateXml(kSampleXml) == std::string(kSampleXml));
        CHECK(i18n::TranslateXml(NULL).empty());
    }
}
