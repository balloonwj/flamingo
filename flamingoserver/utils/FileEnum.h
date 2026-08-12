/**
 *  本地文件枚举与元信息读取，FileEnum.h
 *
 *  把"列目录 / 取大小与修改时间 / 算文件内容MD5"这三件平台相关的小事收在一处，
 *  供 fileserver 的升级包索引（UpdatePackageIndex）与 chatserver 的升级包扫描
 *  （UpdatePackageScanner）共用——两者都要遍历同一个升级包目录，各写一份 Windows
 *  与 POSIX 的分支迟早会失配。本模块属于 utils，三个可执行程序都已链接它。
 *
 *  本模块只做只读操作，不创建、不删除、不修改任何文件。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace fileenum {

/**
 *  列出目录下<u>一层</u>的普通文件名（不含子目录，不含 "." 与 ".."）。
 *
 *    dir：  目标目录，可带或不带结尾分隔符；空串直接返回空列表
 *    out：  [出参] 文件名列表，不含路径前缀；<u>进函数先清空</u>，并按字典序排序，
 *           使同一目录在不同平台、不同文件系统上得到一致且可复现的顺序
 *
 *  目录不存在或不可读<u>不算错误</u>：只是得到空列表。调用方若需区分"空目录"与
 *  "目录不存在"，请自行 stat 该目录。
 */
void ListFileNames(const std::string& dir, std::vector<std::string>& out);

/**
 *  取文件的字节数与最后修改时间。
 *
 *    path：      文件完整路径
 *    outSize：   [出参] 文件字节数
 *    outMtime：  [出参] 最后修改时间，Unix 秒
 *
 *  @return path 是一个可读的<u>普通文件</u>时返回 true 并回填两个出参；
 *          不存在、是目录、或不可读时返回 false，出参不可用。
 *
 *  Windows 上走 `_stat64`：标准 `struct stat` 的 st_size 在 MSVC 下是 32 位的，
 *  超过 2GiB 的升级包会被截断成错误值，进而让"文件有没有变过"的判定失效。
 */
bool StatFile64(const std::string& path, int64_t& outSize, int64_t& outMtime);

/**
 *  计算文件内容的 MD5。
 *
 *    path： 文件完整路径
 *
 *  @return 小写十六进制的 32 位 MD5；文件打不开时返回空串。
 *
 *  按流式读取，不把整个文件读进内存——升级包可能有几百 MB。
 */
std::string ComputeFileMd5(const std::string& path);

/**
 *  给目录路径补上结尾分隔符，便于与文件名直接拼接。
 *
 *    dir： 目录路径；空串原样返回
 *
 *  @return 以 '/' 或 '\\' 结尾的路径。已有结尾分隔符时原样返回。
 */
std::string WithTrailingSeparator(const std::string& dir);

} // namespace fileenum
