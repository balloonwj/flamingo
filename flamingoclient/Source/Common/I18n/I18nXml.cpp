/**
 *  内嵌 XML 布局文字翻译的实现，见 I18nXml.h。扫描逻辑与 0b 步验证过的原型（tmp/i18n_phase0/xmltr_proto.h）相同。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "I18nXml.h"

#include <windows.h>
#include <ctype.h>
#include <string.h>

#include "I18n.h"

namespace i18n {

namespace {

//需要翻译的属性名
const char* const kTextAttrs[] = { "text", "placeholder", "title", "tooltip" };

//XML 注释的起止标记
const char kCommentOpen[] = "<!--";
const char kCommentClose[] = "-->";

//DuiXmlBuilder 认得的五种实体及其对应字符（见 DuiXmlBuilder.cpp 的 DecodeAttrEntities）
const char* const kEntities[] = { "&amp;", "&lt;", "&gt;", "&quot;", "&apos;" };
const char kEntityChars[] = { '&', '<', '>', '"', '\'' };
const size_t kEntityCount = sizeof(kEntityChars) / sizeof(kEntityChars[0]);

bool IsTextAttr(const std::string& name)
{
    for (size_t i = 0; i < sizeof(kTextAttrs) / sizeof(kTextAttrs[0]); ++i)
    {
        if (name == kTextAttrs[i])
            return true;
    }
    return false;
}

//属性名允许的字符（与 DuiXmlBuilder 一致：字母、数字、下划线、连字符、冒号、点）
bool IsNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'
           || c == ':' || c == '.';
}

bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

//把属性值里的实体还原成字符
std::string DecodeEntities(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size())
    {
        bool matched = false;
        if (in[i] == '&')
        {
            for (size_t k = 0; k < kEntityCount; ++k)
            {
                size_t n = strlen(kEntities[k]);
                if (in.compare(i, n, kEntities[k]) == 0)
                {
                    out.push_back(kEntityChars[k]);
                    i += n;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched)
        {
            out.push_back(in[i]);
            ++i;
        }
    }
    return out;
}

//把译文里的特殊字符转成实体，可以放进单引号或双引号包围的属性值
std::string EscapeAttr(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        bool escaped = false;
        for (size_t k = 0; k < kEntityCount; ++k)
        {
            if (in[i] == kEntityChars[k])
            {
                out += kEntities[k];
                escaped = true;
                break;
            }
        }
        if (!escaped)
            out.push_back(in[i]);
    }
    return out;
}

//UTF-8 与 UTF-16 互转；转换失败时返回空串
std::wstring Utf8ToWide(const std::string& in)
{
    if (in.empty())
        return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring out((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const wchar_t* in)
{
    if (in == NULL || in[0] == L'\0')
        return std::string();
    int n = ::WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, NULL, NULL);
    if (n <= 1)
        return std::string();
    std::string out((size_t)(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, in, -1, &out[0], n, NULL, NULL);
    return out;
}

//以当前语言翻译一个属性值；没有译文时原样返回
std::string TranslateByCurrentLanguage(const std::string& utf8)
{
    std::wstring source = Utf8ToWide(utf8);
    if (source.empty())
        return utf8;
    const wchar_t* text = Translate(source.c_str());
    //Translate 没有译文时返回传入的指针本身
    if (text == source.c_str())
        return utf8;
    return WideToUtf8(text);
}

} // namespace

std::string TranslateXmlAttrs(const std::string& xml, XmlTextTranslateFn fn, int* replaced)
{
    int count = 0;
    std::string out;
    out.reserve(xml.size() + xml.size() / 4);
    const size_t n = xml.size();
    size_t i = 0;
    while (i < n)
    {
        //注释原样保留
        if (xml.compare(i, sizeof(kCommentOpen) - 1, kCommentOpen) == 0)
        {
            size_t end = xml.find(kCommentClose, i + sizeof(kCommentOpen) - 1);
            end = (end == std::string::npos) ? n : end + sizeof(kCommentClose) - 1;
            out.append(xml, i, end - i);
            i = end;
            continue;
        }
        //标签之外的内容原样复制到下一个 '<'
        if (xml[i] != '<')
        {
            size_t next = xml.find('<', i);
            if (next == std::string::npos)
                next = n;
            out.append(xml, i, next - i);
            i = next;
            continue;
        }

        //进入标签：复制 '<' 与标签名
        size_t nameEnd = i + 1;
        while (nameEnd < n && xml[nameEnd] != '>' && xml[nameEnd] != '/' && !IsSpace(xml[nameEnd]))
            ++nameEnd;
        out.append(xml, i, nameEnd - i);
        i = nameEnd;

        //逐个属性，直到 '>'
        while (i < n && xml[i] != '>')
        {
            if (!IsNameChar(xml[i]))
            {
                out.push_back(xml[i]);
                ++i;
                continue;
            }
            size_t attrBegin = i;
            while (i < n && IsNameChar(xml[i]))
                ++i;
            std::string name = xml.substr(attrBegin, i - attrBegin);
            out.append(name);
            while (i < n && IsSpace(xml[i]))
                out.push_back(xml[i++]);
            if (i >= n || xml[i] != '=')
                continue;
            out.push_back(xml[i++]);
            while (i < n && IsSpace(xml[i]))
                out.push_back(xml[i++]);
            if (i >= n || (xml[i] != '"' && xml[i] != '\''))
                continue;

            char quote = xml[i];
            size_t valueBegin = i + 1;
            size_t valueEnd = xml.find(quote, valueBegin);
            if (valueEnd == std::string::npos)
            {
                //引号没有闭合：余下内容原样保留，交给解析器报错
                out.append(xml, i, n - i);
                i = n;
                break;
            }
            std::string raw = xml.substr(valueBegin, valueEnd - valueBegin);
            out.push_back(quote);
            bool written = false;
            if (IsTextAttr(name) && !raw.empty())
            {
                std::string decoded = DecodeEntities(raw);
                std::string translated = fn(decoded);
                if (translated != decoded)
                {
                    out += EscapeAttr(translated);
                    ++count;
                    written = true;
                }
            }
            if (!written)
                out += raw;
            out.push_back(quote);
            i = valueEnd + 1;
        }
        if (i < n)
            out.push_back(xml[i++]);
    }
    if (replaced != NULL)
        *replaced = count;
    return out;
}

std::string TranslateXml(const char* xmlUtf8)
{
    if (xmlUtf8 == NULL)
        return std::string();
    if (IsSourceLanguage())
        return std::string(xmlUtf8);
    return TranslateXmlAttrs(xmlUtf8, TranslateByCurrentLanguage, NULL);
}

} // namespace i18n
