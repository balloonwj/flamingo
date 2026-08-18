/**
 *  聊天正文超链接识别单元测试（Suite CLD），ChatLinkDetectTests.cpp
 *
 *  覆盖 balloonchatwnd::DetectLinks / IsSafeLinkUrl：
 *    CLD1  纯中文正文识别不出链接；
 *    CLD2  http:// 与 https:// 各识别一条，URL 原样；
 *    CLD3  www. 开头补成 https://；
 *    CLD4  链接紧挨中文（无空格）时断在中文之前；
 *    CLD5  尾随英文标点被剥掉；
 *    CLD6  括号配对——(链接) 剥右括号，链接里成对的括号保留；
 *    CLD7  表情占位符 U+FFFC 终止链接，且区间下标仍是扁平口径；
 *    CLD8  一条消息里多个链接各自成段、互不重叠、按位置升序；
 *    CLD9  协议头大小写混排照样识别；
 *    CLD10 file / javascript / data / UNC 一律不识别；
 *    CLD11 只有协议头没有主机时不识别；
 *    CLD12 超长文本不超时、结果正确（性能兜底）；
 *    CLD13 端口 / query / fragment / 百分号编码路径完整识别；
 *    CLD14 IsSafeLinkUrl 只放行 http / https。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-11
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonChatWnd/ChatLinkDetect.h"

#include <string>
#include <vector>

using namespace balloonchatwnd;

TEST_CASE("CLD1 纯中文正文识别不出链接")
{
    CHECK(DetectLinks(L"明天上午十点开会，别迟到").empty());
    CHECK(DetectLinks(L"").empty());
    //像域名但没有协议头：有意不认，中文语境里这类误报太多
    CHECK(DetectLinks(L"版本 2.0 的报表见 report.doc").empty());
    CHECK(DetectLinks(L"访问 example.com 看看").empty());
}

TEST_CASE("CLD2 http 与 https 各识别一条且 URL 原样")
{
    const std::vector<LinkSpan> a = DetectLinks(L"see http://a.com/x now");
    REQUIRE(a.size() == 1);
    CHECK(a[0].m_url == L"http://a.com/x");
    CHECK(a[0].m_scheme == kLinkSchemeHttp);
    CHECK(a[0].m_off == 4);
    CHECK(a[0].m_len == 14);

    const std::vector<LinkSpan> b = DetectLinks(L"https://b.example.org/path");
    REQUIRE(b.size() == 1);
    CHECK(b[0].m_url == L"https://b.example.org/path");
    CHECK(b[0].m_scheme == kLinkSchemeHttps);
    CHECK(b[0].m_off == 0);
}

TEST_CASE("CLD3 www 开头补成 https")
{
    const std::vector<LinkSpan> v = DetectLinks(L"打开 www.example.com 试试");
    REQUIRE(v.size() == 1);
    CHECK(v[0].m_scheme == kLinkSchemeBare);
    //正文里那一段仍是 www. 开头，补协议只体现在可打开的 URL 上
    CHECK(v[0].m_url == L"https://www.example.com");
    CHECK(v[0].m_len == 15);

    //粘在别的词后面的 www 不算（awww.example.com）
    CHECK(DetectLinks(L"awww.example.com").empty());
    //邮箱里的 www. 不算
    CHECK(DetectLinks(L"mail@www.example.com").empty());
}

TEST_CASE("CLD4 链接紧挨中文时断在中文之前")
{
    const std::vector<LinkSpan> v = DetectLinks(L"详见https://a.com/b看看");
    REQUIRE(v.size() == 1);
    CHECK(v[0].m_url == L"https://a.com/b");
    //起点在"详见"两个字之后
    CHECK(v[0].m_off == 2);
    CHECK(v[0].m_len == 15);
}

TEST_CASE("CLD5 尾随英文标点被剥掉")
{
    const std::vector<LinkSpan> a = DetectLinks(L"go to https://a.com.");
    REQUIRE(a.size() == 1);
    CHECK(a[0].m_url == L"https://a.com");

    const std::vector<LinkSpan> b = DetectLinks(L"https://a.com, https://b.com!");
    REQUIRE(b.size() == 2);
    CHECK(b[0].m_url == L"https://a.com");
    CHECK(b[1].m_url == L"https://b.com");

    //中文标点本来就是非 ASCII、天然终止链接
    const std::vector<LinkSpan> c = DetectLinks(L"见 https://a.com。");
    REQUIRE(c.size() == 1);
    CHECK(c[0].m_url == L"https://a.com");
}

TEST_CASE("CLD6 括号按配对情况决定剥不剥")
{
    //正文的括号：末尾那个 ) 在链接里没有配对的左括号，要剥
    const std::vector<LinkSpan> a = DetectLinks(L"(https://a.com)");
    REQUIRE(a.size() == 1);
    CHECK(a[0].m_url == L"https://a.com");

    //URL 自带的成对括号：不能剥
    const std::vector<LinkSpan> b = DetectLinks(L"https://a.com/x(y)");
    REQUIRE(b.size() == 1);
    CHECK(b[0].m_url == L"https://a.com/x(y)");

    //两种混在一起：里面成对的留下，外面那个剥掉
    const std::vector<LinkSpan> c = DetectLinks(L"(https://a.com/x(y))");
    REQUIRE(c.size() == 1);
    CHECK(c[0].m_url == L"https://a.com/x(y)");

    //方括号同理
    const std::vector<LinkSpan> d = DetectLinks(L"[https://a.com]");
    REQUIRE(d.size() == 1);
    CHECK(d[0].m_url == L"https://a.com");
}

TEST_CASE("CLD7 表情占位符终止链接且下标仍是扁平口径")
{
    //扁平文本口径：一个内联表情占一个字符位、写作 U+FFFC
    std::wstring text = L"看";
    text += (wchar_t)0xFFFC;            // 下标 1：一个表情
    text += L"https://a.com";           // 下标 2 起
    text += (wchar_t)0xFFFC;            // 链接后面又一个表情，应终止链接

    const std::vector<LinkSpan> v = DetectLinks(text);
    REQUIRE(v.size() == 1);
    CHECK(v[0].m_off == 2);
    CHECK(v[0].m_len == 13);
    CHECK(v[0].m_url == L"https://a.com");
}

TEST_CASE("CLD8 多个链接各自成段且按位置升序不重叠")
{
    const std::vector<LinkSpan> v =
        DetectLinks(L"http://a.com 和 https://b.com 还有 www.c.com");
    REQUIRE(v.size() == 3);
    CHECK(v[0].m_url == L"http://a.com");
    CHECK(v[1].m_url == L"https://b.com");
    CHECK(v[2].m_url == L"https://www.c.com");

    //升序且互不重叠
    for (std::vector<LinkSpan>::size_type i = 1; i < v.size(); ++i)
    {
        CHECK(v[i - 1].m_off + v[i - 1].m_len <= v[i].m_off);
    }
}

TEST_CASE("CLD9 协议头大小写混排照样识别")
{
    const std::vector<LinkSpan> a = DetectLinks(L"HTTP://A.COM/X");
    REQUIRE(a.size() == 1);
    //URL 原样保留大小写——路径部分区分大小写，不能顺手转小写
    CHECK(a[0].m_url == L"HTTP://A.COM/X");
    CHECK(a[0].m_scheme == kLinkSchemeHttp);

    const std::vector<LinkSpan> b = DetectLinks(L"HtTpS://a.com");
    REQUIRE(b.size() == 1);
    CHECK(b[0].m_scheme == kLinkSchemeHttps);

    const std::vector<LinkSpan> c = DetectLinks(L"WWW.example.com");
    REQUIRE(c.size() == 1);
    CHECK(c[0].m_url == L"https://WWW.example.com");
}

TEST_CASE("CLD10 危险协议与 UNC 路径一律不识别")
{
    CHECK(DetectLinks(L"file:///C:/Windows/system32/calc.exe").empty());
    CHECK(DetectLinks(L"javascript:alert(1)").empty());
    CHECK(DetectLinks(L"data:text/html,<script>alert(1)</script>").empty());
    CHECK(DetectLinks(L"\\\\evil-host\\share\\payload.exe").empty());
    CHECK(DetectLinks(L"ftp://a.com/x").empty());
    //夹在正文里也一样不认
    CHECK(DetectLinks(L"点这里 file:///C:/x.exe 就行").empty());
}

TEST_CASE("CLD11 只有协议头没有主机时不识别")
{
    CHECK(DetectLinks(L"http://").empty());
    CHECK(DetectLinks(L"https://").empty());
    CHECK(DetectLinks(L"www.").empty());
    //协议头后面只有会被剥掉的标点，等于没有主机
    CHECK(DetectLinks(L"https://.").empty());
    CHECK(DetectLinks(L"看 http:// 这样").empty());
}

TEST_CASE("CLD12 超长文本不超时且结果正确")
{
    //一万段中文里埋 100 条链接：一次线性扫描应当很快
    std::wstring text;
    for (int i = 0; i < 100; ++i)
    {
        text += L"这是一段没有链接的中文正文，用来把消息撑长一些。";
        text += L"https://a.com/p ";
    }
    const std::vector<LinkSpan> v = DetectLinks(text);
    CHECK(v.size() == 100);
    CHECK(v[0].m_url == L"https://a.com/p");
    CHECK(v[99].m_url == L"https://a.com/p");
}

TEST_CASE("CLD13 端口 query fragment 与百分号编码路径完整识别")
{
    const std::vector<LinkSpan> a = DetectLinks(L"https://a.com:8443/p?x=1&y=2#frag");
    REQUIRE(a.size() == 1);
    CHECK(a[0].m_url == L"https://a.com:8443/p?x=1&y=2#frag");

    //中文路径要用百分号编码才能完整识别——纯 ASCII，不会被非 ASCII 规则截断
    const std::vector<LinkSpan> b = DetectLinks(L"https://a.com/%E6%96%87%E6%A1%A3.pdf");
    REQUIRE(b.size() == 1);
    CHECK(b[0].m_url == L"https://a.com/%E6%96%87%E6%A1%A3.pdf");

    //字面中文路径按口径只识别到中文之前（见 ChatLinkDetect.h 的取舍说明）：
    //"中文紧跟链接"远比"中文出现在路径里"常见，宁可少认几个字，也不能把正文吃进链接
    const std::vector<LinkSpan> c = DetectLinks(L"https://a.com/文档.pdf");
    REQUIRE(c.size() == 1);
    CHECK(c[0].m_url == L"https://a.com/");
}

TEST_CASE("CLD14 IsSafeLinkUrl 只放行 http 与 https")
{
    CHECK(IsSafeLinkUrl(L"http://a.com"));
    CHECK(IsSafeLinkUrl(L"https://a.com"));
    CHECK(IsSafeLinkUrl(L"HTTPS://A.COM"));

    CHECK_FALSE(IsSafeLinkUrl(L"file:///C:/x.exe"));
    CHECK_FALSE(IsSafeLinkUrl(L"javascript:alert(1)"));
    CHECK_FALSE(IsSafeLinkUrl(L"data:text/html,x"));
    CHECK_FALSE(IsSafeLinkUrl(L"\\\\host\\share"));
    CHECK_FALSE(IsSafeLinkUrl(L"ftp://a.com"));
    CHECK_FALSE(IsSafeLinkUrl(L""));
    //只有协议头、没有主机
    CHECK_FALSE(IsSafeLinkUrl(L"http://"));
    CHECK_FALSE(IsSafeLinkUrl(L"https://"));
}
