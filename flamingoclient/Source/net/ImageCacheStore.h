/**
 *  本地图片缓存的落地写入，ImageCacheStore.h
 *
 *  聊天图片的本地缓存按登录账号隔离、按内容 MD5 命名：
 *  <数据根>Users\<账号>\ChatImage\<md5>（路径由 ImageTransfer::CacheFilePathForMd5 拼）。
 *  读取侧（聊天窗、消息记录窗）只判断文件是否存在、大小是否与消息一致，就认定它是
 *  完整文件，因此写入必须保证目标名下**不会出现写了一半的文件**：先写临时文件，写完
 *  再改名为目标名。
 *
 *  典型用法：发送图片成功后，把已经读入内存的图片字节写进缓存，使本端的消息记录窗
 *  与重新打开的聊天窗都能直接显示这张图，而不必再从服务器下载一遍：
 *      net::StoreImageToCache(cachePath, bytes);
 *
 *  只依赖 Windows 文件接口与 STL，不读任何全局状态，与 FlamingoUnit 测试工程共用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <string>

namespace net {

// 写入时使用的临时文件后缀：先写 <目标路径>.storing，写完再改名为目标路径。
// 与下载侧使用的 ".downloading" 区分开，排查残留文件时能看出是哪条路径留下的。
const wchar_t kImageCacheStoringSuffix[] = L".storing";

/**
 *  把一段图片字节写到本地图片缓存里的指定路径。
 *
 *  - 目标文件已存在且大小与 bytes 相同：视为同一内容（缓存按内容 MD5 命名），不重写，
 *    直接返回成功。
 *  - 目标文件不存在，或大小不符（例如早先写了一半的残留）：先写临时文件，写完用
 *    MoveFileExW 改名覆盖目标；任何一步失败都会删除临时文件。
 *  - 不创建目录：目标所在目录须由调用方事先建好。
 *
 *  @param cachePath 目标文件的完整路径；为空时直接返回失败
 *  @param bytes     图片的完整内容；为空时直接返回失败，不创建任何文件
 *  @return true 表示返回时目标文件已存在且大小与 bytes 一致（本次写入或原本就有）；
 *          false 表示参数无效或写入失败，此时目标路径下不会多出临时文件
 */
bool StoreImageToCache(const std::wstring& cachePath, const std::string& bytes);

} // namespace net
