/**
 *  「清理本地缓存」的磁盘操作层，CacheCleanExecutor.h
 *
 *  只做两件事：统计一个目录占了多少字节、把一个目录整棵删掉。要删哪几个目录由纯逻辑的
 *  CacheCleanPlan 算出，本层不做任何判断性决策 —— 但<u>删除前一定会再过一遍</u>
 *  CacheCleanPlan 的边界检查，因为删除不可恢复，多核一遍不亏。
 *
 *  与 CacheCleanPlan 分开的理由：那一支是纯逻辑、进了 FlamingoUnit 测试工程；本层碰
 *  磁盘，不进测试工程，靠实测验收。
 *
 *  线程：只在主线程调用。扫描大目录可能耗时几秒，调用方负责在扫描期间把界面标成
 *  "正在统计…"；本层不起线程、不投递消息。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <string>

namespace balloonsettings {

/**
 *  递归统计一个目录下所有文件的字节数之和。
 *
 *  目录不存在、或枚举中途出错时按已经数到的量返回，不报错 —— 这个数字只用于给用户
 *  一个"值不值得清"的量级参考，少算一点无妨，为它中断整个流程反而不合算。
 *
 *    dir：目录路径，结尾带不带反斜杠都行；为空时返回 0。
 *  @return 字节数之和；目录不存在时返回 0。
 */
unsigned long long ScanDirSizeBytes(const std::wstring& dir);

/**
 *  把一个目录整棵删掉（含其下所有文件与子目录），随后删除该目录本身。
 *
 *  <u>删除前强制过 IsSafeToDeleteCacheDir</u>：目录必须严格位于 <dataRoot>Users\
 *  之下、且路径里没有上跳段，否则直接拒绝并返回 false，一个文件都不动。
 *
 *  遇到被占用（典型是正在写入的下载文件）或只读的文件时跳过该文件继续删其余的，
 *  最后返回 false 告诉调用方"没删干净"。不重试、不强制解锁 —— 缓存下次还能重建，
 *  为清理去和正在进行的传输抢文件不值当。
 *
 *    dir：     待删目录。
 *    dataRoot：本地数据根目录（g_szDataRoot），用于边界检查。
 *  @return 全部删除成功返回 true；被边界检查拒绝、或有文件没删掉时返回 false。
 */
bool DeleteDirTree(const std::wstring& dir, const std::wstring& dataRoot);

} // namespace balloonsettings
