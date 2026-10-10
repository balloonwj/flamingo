/**
 *  品牌文字读取单元测试（BR1～BR11、SL1～SL3、DC1～DC2），BrandTextTests.cpp
 *
 *  覆盖 App/BrandText 对 flamingo.ini 中 [ui] apptitle、[ui] slogan、[ui] copyrighttext 与
 *  [ui] defaultcompany 的读取：
 *    · 版权文字（关于对话框页脚）：缺省值、空白值、UTF-8 中文（BR9～BR11）；
 *    · 产品名称：缺省值、ASCII、UTF-8 中文、ANSI 中文、空白值、文件不存在；
 *    · 程序用 WritePrivateProfileStringW 回写其它配置项之后，UTF-8 那一行的原始字节
 *      保持不变 —— 「apptitle 可以用 UTF-8 写中文」这一做法成立的前提；
 *    · ini 解析细节：节名键名不区分大小写、值首尾空白与双引号、BOM、注释行、别的节同名键；
 *    · 标语：没有这一项时用内置标语，配置为空时不显示，UTF-8 中文；
 *    · 注册窗的默认公司（DC1～DC2）：没有这一项、值为空白、文件不存在时取 FlamingoIM.org，
 *      ASCII / UTF-8 中文 / ANSI 中文都能读出，首尾空白去掉。注册模型与组合框取用这个值的
 *      用例 DC3 在 RegisterTests.cpp。
 *  每条用例都用独立的临时配置文件，文件名带进程 id，两个测试进程同时跑也不会共用。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <stdio.h>
#include <string>
#include "../App/BrandText.h"

namespace {

// 简体中文 ANSI 代码页（GBK）。BR4 只在这个代码页下才有意义。
const UINT kCodePageGbk = 936;

// 测试用的中文产品名与标语
const wchar_t* const kCjkTitle  = L"火烈鸟办公";
const wchar_t* const kCjkSlogan = L"高效协作，从这里开始";
// 测试用的中文公司全称
const wchar_t* const kCjkCompany = L"火烈鸟科技有限公司";

//生成本进程专用的临时配置文件路径，并删掉上一次残留的同名文件。
//  tag：区分同一进程内不同用例的文件名片段
std::wstring FreshIniPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_brand_%s_%lu.ini", dir, tag, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

//把宽字符串按指定代码页编码成字节串
std::string Encode(const std::wstring& text, UINT codePage)
{
    if (text.empty())
        return std::string();

    int len = ::WideCharToMultiByte(codePage, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string out(len, '\0');
    ::WideCharToMultiByte(codePage, 0, text.c_str(), (int)text.size(), &out[0], len, NULL, NULL);
    return out;
}

//把字节串原样写成文件（不做任何编码转换）
void WriteBytes(const std::wstring& path, const std::string& bytes)
{
    FILE* fp = NULL;
    REQUIRE(_wfopen_s(&fp, path.c_str(), L"wb") == 0);
    fwrite(bytes.data(), 1, bytes.size(), fp);
    fclose(fp);
}

//把文件原样读成字节串
std::string ReadBytes(const std::wstring& path)
{
    std::string out;
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0)
        return out;

    char buf[512];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        out.append(buf, n);
    }
    fclose(fp);
    return out;
}

} // namespace

TEST_CASE("BR1 没有 apptitle 这一项时产品名称取缺省值 FlamingoIM", "[brand]")
{
    CHECK(std::wstring(brandtext::kDefaultAppTitle) == L"FlamingoIM");

    const std::wstring path = FreshIniPath(L"br1");
    WriteBytes(path, "[ui]\r\ncopyrighttext=abc\r\n");
    CHECK(brandtext::ReadAppTitle(path.c_str()) == L"FlamingoIM");
    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR2 ASCII 产品名称原样返回", "[brand]")
{
    const std::wstring path = FreshIniPath(L"br2");
    WriteBytes(path, "[ui]\r\napptitle=Acme Chat\r\n");
    CHECK(brandtext::ReadAppTitle(path.c_str()) == L"Acme Chat");
    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR3 用 UTF-8 保存的中文产品名称能正确解码", "[brand]")
{
    const std::wstring path = FreshIniPath(L"br3");
    WriteBytes(path, "[ui]\r\napptitle=" + Encode(kCjkTitle, CP_UTF8) + "\r\n");
    CHECK(brandtext::ReadAppTitle(path.c_str()) == kCjkTitle);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR4 用 ANSI（GBK）保存的中文产品名称能正确解码", "[brand]")
{
    if (::GetACP() != kCodePageGbk)
    {
        SKIP("system ANSI code page is not 936, GBK text cannot be represented");
    }

    const std::wstring path = FreshIniPath(L"br4");
    WriteBytes(path, "[ui]\r\napptitle=" + Encode(kCjkTitle, CP_ACP) + "\r\n");
    CHECK(brandtext::ReadAppTitle(path.c_str()) == kCjkTitle);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR5 产品名称为空或全是空白时取缺省值", "[brand]")
{
    const std::wstring path = FreshIniPath(L"br5");

    WriteBytes(path, "[ui]\r\napptitle=\r\n");
    CHECK(brandtext::ReadAppTitle(path.c_str()) == L"FlamingoIM");

    WriteBytes(path, "[ui]\r\napptitle=   \t \r\n");
    CHECK(brandtext::ReadAppTitle(path.c_str()) == L"FlamingoIM");

    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR6 程序回写其它配置项后，UTF-8 写的产品名称原始字节不变、仍能正确读出", "[brand]")
{
    //客户端运行中会用 WritePrivateProfileStringW 改写同一个 flamingo.ini 的其它项
    //（[client] deviceid、[app] clearexpirelog 等）。这一行若被按 ANSI 重新编码，
    //UTF-8 写的中文就再也读不回来了
    const std::wstring path = FreshIniPath(L"br6");
    const std::string titleLine = "apptitle=" + Encode(kCjkTitle, CP_UTF8);
    WriteBytes(path, "[ui]\r\n" + titleLine + "\r\n[client]\r\ndeviceid=OLD\r\n");

    REQUIRE(::WritePrivateProfileStringW(L"client", L"deviceid", L"PC-NEW-1234", path.c_str()));
    REQUIRE(::WritePrivateProfileStringW(L"app", L"clearexpirelog", L"1", path.c_str()));

    const std::string after = ReadBytes(path);
    CHECK(after.find(titleLine + "\r\n") != std::string::npos);
    CHECK(after.find("deviceid=PC-NEW-1234") != std::string::npos);
    CHECK(brandtext::ReadAppTitle(path.c_str()) == kCjkTitle);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR7 解析细节：大小写、首尾空白、双引号、BOM、注释行、别的节同名键", "[brand]")
{
    std::string value;

    //节名与键名不区分大小写，值去掉首尾空白
    CHECK(brandtext::FindIniValueBytes("[UI]\r\n  AppTitle =  Acme  \r\n", "ui", "apptitle", value));
    CHECK(value == "Acme");

    //整体被双引号包围时去掉引号
    CHECK(brandtext::FindIniValueBytes("[ui]\napptitle=\"Acme Chat\"\n", "ui", "apptitle", value));
    CHECK(value == "Acme Chat");

    //文件开头的 UTF-8 BOM 不影响第一行的节名
    CHECK(brandtext::FindIniValueBytes("\xEF\xBB\xBF[ui]\r\napptitle=Acme\r\n", "ui", "apptitle", value));
    CHECK(value == "Acme");

    //分号开头的注释行被忽略；别的节里的同名键不会被读到
    value = "untouched";
    CHECK_FALSE(brandtext::FindIniValueBytes("[ui]\r\n;apptitle=Commented\r\n[server]\r\napptitle=Wrong\r\n",
                                             "ui", "apptitle", value));
    CHECK(value == "untouched");

    //同一个键出现多次时取第一次出现的值
    CHECK(brandtext::FindIniValueBytes("[ui]\r\napptitle=First\r\napptitle=Second\r\n", "ui", "apptitle", value));
    CHECK(value == "First");

    //键存在但值为空时也算找到，值为空串
    CHECK(brandtext::FindIniValueBytes("[ui]\r\nslogan=\r\n", "ui", "slogan", value));
    CHECK(value.empty());
}

TEST_CASE("BR8 配置文件不存在时产品名称取缺省值、标语取内置标语", "[brand]")
{
    const std::wstring path = FreshIniPath(L"br8");
    CHECK(brandtext::ReadAppTitle(path.c_str()) == L"FlamingoIM");
    CHECK(brandtext::ReadSlogan(path.c_str()) == brandtext::kDefaultSlogan);
}

TEST_CASE("BR9 没有 copyrighttext 这一项时版权文字取缺省值", "[brand]")
{
    CHECK(std::wstring(brandtext::kDefaultCopyright) == L"© 2024-2026 FlamingoIM Team · All Rights Reserved");

    const std::wstring path = FreshIniPath(L"br9");
    WriteBytes(path, "[ui]\r\napptitle=Acme\r\n");
    CHECK(brandtext::ReadCopyright(path.c_str()) == brandtext::kDefaultCopyright);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR10 版权文字为空或全是空白时取缺省值", "[brand]")
{
    const std::wstring path = FreshIniPath(L"br10");
    WriteBytes(path, "[ui]\r\ncopyrighttext=  \r\n");
    CHECK(brandtext::ReadCopyright(path.c_str()) == brandtext::kDefaultCopyright);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("BR11 用 UTF-8 保存的中文版权文字能正确解码", "[brand]")
{
    const std::wstring copyright = L"© 2026 火烈鸟科技 版权所有";
    const std::wstring path = FreshIniPath(L"br11");
    WriteBytes(path, "[ui]\r\ncopyrighttext=" + Encode(copyright, CP_UTF8) + "\r\n");
    CHECK(brandtext::ReadCopyright(path.c_str()) == copyright);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("SL1 没有 slogan 这一项时取内置标语", "[brand]")
{
    CHECK(std::wstring(brandtext::kDefaultSlogan) == L"开源实用的企业即时通讯软件");

    const std::wstring path = FreshIniPath(L"sl1");
    WriteBytes(path, "[ui]\r\napptitle=Acme\r\n");
    CHECK(brandtext::ReadSlogan(path.c_str()) == brandtext::kDefaultSlogan);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("SL2 slogan 配置为空时返回空串，表示不显示标语", "[brand]")
{
    const std::wstring path = FreshIniPath(L"sl2");
    WriteBytes(path, "[ui]\r\nslogan=\r\n");
    CHECK(brandtext::ReadSlogan(path.c_str()).empty());
    ::DeleteFileW(path.c_str());
}

TEST_CASE("SL3 用 UTF-8 保存的中文标语能正确解码", "[brand]")
{
    const std::wstring path = FreshIniPath(L"sl3");
    WriteBytes(path, "[ui]\r\nslogan=" + Encode(kCjkSlogan, CP_UTF8) + "\r\n");
    CHECK(brandtext::ReadSlogan(path.c_str()) == kCjkSlogan);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("DC1 没有 defaultcompany 这一项、值为空白或文件不存在时，默认公司取 FlamingoIM.org", "[brand]")
{
    //与服务端 chatserver.conf 的 defaultenterprise 缺省值（DEFAULT_ENTERPRISE_NAME）相同
    CHECK(std::wstring(brandtext::kDefaultCompany) == L"FlamingoIM.org");

    //文件不存在
    const std::wstring path = FreshIniPath(L"dc1");
    CHECK(brandtext::ReadDefaultCompany(path.c_str()) == L"FlamingoIM.org");

    //没有这一项
    WriteBytes(path, "[ui]\r\napptitle=Acme\r\n");
    CHECK(brandtext::ReadDefaultCompany(path.c_str()) == L"FlamingoIM.org");

    //值为空、全是空白
    WriteBytes(path, "[ui]\r\ndefaultcompany=\r\n");
    CHECK(brandtext::ReadDefaultCompany(path.c_str()) == L"FlamingoIM.org");
    WriteBytes(path, "[ui]\r\ndefaultcompany= \t \r\n");
    CHECK(brandtext::ReadDefaultCompany(path.c_str()) == L"FlamingoIM.org");

    ::DeleteFileW(path.c_str());
}

TEST_CASE("DC2 默认公司为 ASCII、UTF-8 中文或 ANSI（GBK）中文时都能正确读出，首尾空白去掉", "[brand]")
{
    const std::wstring path = FreshIniPath(L"dc2");

    //首尾空白去掉，中间的空格保留
    WriteBytes(path, "[ui]\r\ndefaultcompany=  Acme  Corp \t\r\n");
    CHECK(brandtext::ReadDefaultCompany(path.c_str()) == L"Acme  Corp");

    WriteBytes(path, "[ui]\r\ndefaultcompany=" + Encode(kCjkCompany, CP_UTF8) + "\r\n");
    CHECK(brandtext::ReadDefaultCompany(path.c_str()) == kCjkCompany);

    //ANSI（GBK）只在简体中文代码页下才能表示这几个字
    if (::GetACP() == kCodePageGbk)
    {
        WriteBytes(path, "[ui]\r\ndefaultcompany=" + Encode(kCjkCompany, CP_ACP) + "\r\n");
        CHECK(brandtext::ReadDefaultCompany(path.c_str()) == kCjkCompany);
    }

    ::DeleteFileW(path.c_str());
}
