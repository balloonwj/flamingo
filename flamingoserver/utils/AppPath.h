/**
 *  可执行文件路径工具，AppPath.h
 *
 *  用来把配置文件里写的相对路径，解析成「相对可执行文件所在目录」的路径。
 *
 *  动机：进程的工作目录是启动方式决定的（双击、从别的目录敲命令、被服务管理器拉起，
 *  拿到的工作目录各不相同），而 SQLite 的库文件必须始终落在同一个地方，否则换个方式
 *  启动就等于换了一个空库。把相对路径钉在可执行文件旁边即可消除这种不确定性。
 *
 *  典型用法（见 chatserversrc/main.cpp）：
 *      std::string dbPath = apppath::ResolveAgainstDirectory(
 *                               apppath::GetExecutableDirectory(), config.getConfigName("dbpath"));
 *
 *  本模块为纯逻辑 + 一次系统调用，不依赖网络与数据库，便于单元测试
 *  （见 tests/test_apppath.cpp）。
 *
 *  balloonwj@qq.com   2026-08-10
 */
#pragma once

#include <string>

namespace apppath {

/**
 *  取当前可执行文件所在的目录。
 *
 *  @return 目录的绝对路径，**不含**结尾的路径分隔符；取不到时返回空串，
 *          此时调用方应当回落到原有的「相对工作目录」行为，而不是让服务起不来。
 *          Windows 下经 GetModuleFileName 取得，Linux 下读 /proc/self/exe。
 *  副作用：无（只读取当前进程自身的信息）。
 */
std::string GetExecutableDirectory();

/**
 *  判断一个路径是否已经是绝对路径。
 *
 *  @param path 待判断的路径，可以为空串。
 *  @return true 表示是绝对路径。判定涵盖四种写法：POSIX 的 "/dir/file"、
 *          Windows 盘符的 "X:\dir\file"、UNC 的 "\\\\server\\share\\file"，
 *          以及 Windows 上「当前盘根目录」的 "\dir\file"。空串返回 false。
 *  副作用：无（不访问文件系统）。
 */
bool IsAbsolutePath(const std::string& path);

/**
 *  把一个路径解析成基于指定目录的路径。
 *
 *  纯函数，不访问文件系统，结果只取决于两个入参，因此可以直接单元测试。
 *  不做符号链接展开与 ".." 归并 —— 这些交给操作系统在真正打开文件时处理。
 *
 *  @param baseDir 基准目录，通常传 GetExecutableDirectory() 的返回值。
 *                 传空串表示拿不到基准，此时原样返回 path（回落到旧行为）。
 *  @param path    待解析的路径。已是绝对路径时原样返回；空串时原样返回空串；
 *                 开头的 "./" 或 ".\" 会被去掉，免得拼出夹着无意义片段的路径。
 *  @return 解析后的路径。拼接用的分隔符跟随 baseDir 里已有的那一种（baseDir 里
 *          没有分隔符时才用平台默认的），避免拼出 "/opt/bin\data" 这类混搭路径。
 *  副作用：无。
 */
std::string ResolveAgainstDirectory(const std::string& baseDir, const std::string& path);

} // namespace apppath
