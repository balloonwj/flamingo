/**
 *  系统根证书包的探测，CaBundleLocator.h
 *
 *  服务端在 Linux 上默认链接随仓库源码编出的 OpenSSL 静态库（docs/服务端依赖库源码编译方案.md 第 11 节）。
 *  这样的 OpenSSL 只会到编译时设定的 OPENSSLDIR 里找根证书，而那个目录是有意设成不存在的（避免读取系统里
 *  别的版本的 openssl.cnf）；各发行版又把根证书包放在不同的位置。不处理的话，HttpsClient 的证书校验会全部失败。
 *  本模块按常见发行版的位置依次探测，返回第一个存在的证书包，由 HttpsClient 交给 SSL_CTX_load_verify_locations。
 *
 *  本模块不依赖 OpenSSL；判断文件是否存在的函数由调用方传入，便于单元测试（见 tests/test_cabundle.cpp）。
 *
 *  典型用法（见 utils/HttpsClient.cpp）：
 *      if (cabundle::ShouldProbe(::getenv("SSL_CERT_FILE"), ::getenv("SSL_CERT_DIR")))
 *          path = cabundle::FindFirstExisting(cabundle::DefaultCandidates(), cabundle::FileExists);
 *
 *  balloonwj@qq.com   2026-10-10
 */
#pragma once

#include <string>
#include <vector>

namespace cabundle {

//判断某个路径是否为存在的普通文件（或指向普通文件的符号链接）的函数
typedef bool (*FileExistsFunc)(const std::string& path);

/**
 *  默认的候选证书包路径，按探测顺序排列：Debian / Ubuntu、RHEL / CentOS / Fedora / Rocky、
 *  RHEL 7 及以上的原始位置、openSUSE、OpenELEC、Alpine。
 *  @return 候选路径列表（绝对路径），每次调用返回同样的内容
 *  副作用：无
 */
std::vector<std::string> DefaultCandidates();

/**
 *  判断是否需要探测。OpenSSL 的 SSL_CTX_set_default_verify_paths 会优先使用环境变量 SSL_CERT_FILE、
 *  SSL_CERT_DIR；部署方设置了其中任何一个，说明已经指定了根证书的位置，不再探测。
 *  @param envCertFile 环境变量 SSL_CERT_FILE 的值；未设置时传 NULL
 *  @param envCertDir  环境变量 SSL_CERT_DIR 的值；未设置时传 NULL
 *  @return 两者都为 NULL 或空串时返回 true（需要探测），否则返回 false
 *  副作用：无
 */
bool ShouldProbe(const char* envCertFile, const char* envCertDir);

/**
 *  按顺序返回第一个存在的候选路径。
 *  @param candidates 候选路径，按优先级从高到低排列
 *  @param exists     判断文件是否存在的函数，不能为 NULL；生产代码传 FileExists，单元测试传假实现
 *  @return 第一个 exists 返回 true 的路径；都不存在（或候选为空）时返回空串
 *  副作用：只有 exists 本身的副作用
 */
std::string FindFirstExisting(const std::vector<std::string>& candidates, FileExistsFunc exists);

/**
 *  用 stat 判断路径是否为存在的普通文件（符号链接按其指向的目标判断）。
 *  @param path 文件路径
 *  @return 是存在的普通文件时返回 true；不存在、是目录或无权访问时返回 false
 *  副作用：无（只读取文件系统的元数据）
 */
bool FileExists(const std::string& path);

} // namespace cabundle
