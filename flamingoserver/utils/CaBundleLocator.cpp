/**
 *  系统根证书包的探测，CaBundleLocator.cpp。设计与用法见 CaBundleLocator.h。
 *  balloonwj@qq.com   2026-10-10
 */
#include "CaBundleLocator.h"

#include <sys/types.h>
#include <sys/stat.h>

namespace cabundle {

namespace {

//各发行版存放根证书包的位置，按探测顺序排列。列表参照 Go 语言标准库 crypto/x509 在 Linux 上的同类列表；
//Debian 系排在最前，是因为它在服务器上最常见，而 RHEL 系的两个路径在 Debian 系上不存在，不会误命中
const char* const kCandidates[] =
{
    "/etc/ssl/certs/ca-certificates.crt",                 //Debian、Ubuntu、Gentoo
    "/etc/pki/tls/certs/ca-bundle.crt",                   //RHEL、CentOS、Fedora、Rocky、Alma
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",  //RHEL 7 及以上由 update-ca-trust 生成的原始位置
    "/etc/ssl/ca-bundle.pem",                             //openSUSE
    "/etc/pki/tls/cacert.pem",                            //OpenELEC
    "/etc/ssl/cert.pem"                                   //Alpine
};

} // anonymous namespace

std::vector<std::string> DefaultCandidates()
{
    return std::vector<std::string>(kCandidates, kCandidates + sizeof(kCandidates) / sizeof(kCandidates[0]));
}

bool ShouldProbe(const char* envCertFile, const char* envCertDir)
{
    const bool hasFile = envCertFile != NULL && envCertFile[0] != '\0';
    const bool hasDir = envCertDir != NULL && envCertDir[0] != '\0';
    return !hasFile && !hasDir;
}

std::string FindFirstExisting(const std::vector<std::string>& candidates, FileExistsFunc exists)
{
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (exists(candidates[i]))
            return candidates[i];
    }
    return std::string();
}

bool FileExists(const std::string& path)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0)
        return false;
    return (st.st_mode & S_IFMT) == S_IFREG;
}

} // namespace cabundle
