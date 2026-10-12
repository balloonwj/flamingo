/**
 *  系统根证书包探测的单元测试（Suite CAB），test_cabundle.cpp
 *
 *  覆盖 utils/CaBundleLocator 的三个纯函数（docs/服务端依赖库源码编译方案.md 第 11.3 节与第 12 节）：
 *    CAB1  候选里有多个存在时，返回排在最前面的那个；
 *    CAB2  候选都不存在（或候选为空）时返回空串；
 *    CAB3  SSL_CERT_FILE 或 SSL_CERT_DIR 任一非空时不探测，两者都为空或未设置时才探测；
 *    CAB4  默认候选列表包含 Debian 与 RHEL 的路径，且 Debian 的排在 RHEL 之前。
 *
 *  判断文件是否存在的函数用假实现（只认 g_fakeExisting 里登记的路径），不访问真实文件系统，
 *  所以用例在 Windows 与 Linux 上结果相同。
 *
 *  balloonwj@qq.com   2026-10-10
 */
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "../utils/CaBundleLocator.h"

namespace {

//假文件系统里「存在」的路径；每个用例开头自己设定
std::set<std::string> g_fakeExisting;

//假的文件存在判断：只认 g_fakeExisting 里登记的路径
bool FakeExists(const std::string& path)
{
    return g_fakeExisting.count(path) > 0;
}

//用例里使用的虚构候选路径
const char* const kPathA = "/fake/a.pem";
const char* const kPathB = "/fake/b.pem";
const char* const kPathC = "/fake/c.pem";

//Debian 与 RHEL 的证书包路径，CAB4 检查它们在默认列表里的位置
const char* const kDebianBundle = "/etc/ssl/certs/ca-certificates.crt";
const char* const kRhelBundle = "/etc/pki/tls/certs/ca-bundle.crt";

//三个虚构候选，按 A、B、C 的顺序
std::vector<std::string> MakeCandidates()
{
    std::vector<std::string> v;
    v.push_back(kPathA);
    v.push_back(kPathB);
    v.push_back(kPathC);
    return v;
}

} // anonymous namespace

TEST_CASE("CAB1 多个候选存在时返回排在最前面的那个")
{
    g_fakeExisting.clear();
    g_fakeExisting.insert(kPathB);
    g_fakeExisting.insert(kPathC);
    CHECK(cabundle::FindFirstExisting(MakeCandidates(), FakeExists) == kPathB);

    //排在最前的也存在时就取它
    g_fakeExisting.insert(kPathA);
    CHECK(cabundle::FindFirstExisting(MakeCandidates(), FakeExists) == kPathA);
}

TEST_CASE("CAB2 候选都不存在或候选为空时返回空串")
{
    g_fakeExisting.clear();
    CHECK(cabundle::FindFirstExisting(MakeCandidates(), FakeExists).empty());

    g_fakeExisting.insert(kPathA);
    CHECK(cabundle::FindFirstExisting(std::vector<std::string>(), FakeExists).empty());
}

TEST_CASE("CAB3 设置了 SSL_CERT_FILE 或 SSL_CERT_DIR 时不探测")
{
    //两者都未设置，或都是空串：探测
    CHECK(cabundle::ShouldProbe(NULL, NULL));
    CHECK(cabundle::ShouldProbe("", ""));
    CHECK(cabundle::ShouldProbe("", NULL));

    //任一非空：不探测
    CHECK_FALSE(cabundle::ShouldProbe("/etc/my-ca.pem", NULL));
    CHECK_FALSE(cabundle::ShouldProbe(NULL, "/etc/my-certs"));
    CHECK_FALSE(cabundle::ShouldProbe("/etc/my-ca.pem", "/etc/my-certs"));
    CHECK_FALSE(cabundle::ShouldProbe("", "/etc/my-certs"));
}

TEST_CASE("CAB4 默认候选包含 Debian 与 RHEL 的证书包，且 Debian 的排在前面")
{
    const std::vector<std::string> candidates = cabundle::DefaultCandidates();
    std::vector<std::string>::const_iterator debian = std::find(candidates.begin(), candidates.end(), kDebianBundle);
    std::vector<std::string>::const_iterator rhel = std::find(candidates.begin(), candidates.end(), kRhelBundle);
    REQUIRE(debian != candidates.end());
    REQUIRE(rhel != candidates.end());
    CHECK(debian < rhel);

    //每一项都应是绝对路径（doctest 的断言里不能写 &&，所以分成两条）
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        REQUIRE_FALSE(candidates[i].empty());
        CHECK_MESSAGE(candidates[i][0] == '/', candidates[i]);
    }
}
