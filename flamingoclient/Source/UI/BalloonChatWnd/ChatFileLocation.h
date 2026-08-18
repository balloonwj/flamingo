/**
 *  ChatFileLocation.h
 *
 *  文件气泡「本地文件在哪、还在不在」的决策（纯逻辑核心，不碰磁盘、不依赖 UI）。
 *
 *  一条文件消息在本地可能有两处落点：一是登记表里记下的路径（发送方是当初选中的
 *  源文件，接收方是下载落地的位置），二是按内容 MD5 推导出的下载缓存路径
 *  （<主目录>Users\<userId>\ChatFile\<md5>\<文件名>）。两处都可能失效——用户会把
 *  文件移走、改名、删掉，也会清缓存。气泡要据此决定是显示「打开 / 打开所在文件夹 /
 *  另存为」，还是退回「下载」。
 *
 *  把"选哪条路径"抽成纯函数、把"文件在不在"作为入参交给调用方去查，就能脱离磁盘
 *  单元测试这套优先级与回退（见 flamingoclient/Source/Tests/ChatFileLocationTests.cpp，
 *  Suite CFL），不必在测试里真造文件。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <string>

namespace balloonchatwnd {

/**
 *  决定文件气泡该用哪个本地路径。
 *
 *  优先用登记路径：它是用户实际持有该文件的位置（发送方的源文件往往就在其工作目录，
 *  比缓存副本更贴近用户预期，"打开所在文件夹"也才有意义）。登记路径失效时回退到
 *  缓存路径——下载过的文件即便登记丢了（旧版本数据、库被删），靠 MD5 仍能找回来。
 *  两处都没有才判定为"本地没有"，由调用方渲染成待下载态。
 *
 *  @param registeredPath   登记表里记录的本地路径；空串表示没有登记
 *  @param registeredExists 上述路径对应的文件当前是否存在（由调用方查磁盘得出；
 *                          registeredPath 为空时本参数被忽略）
 *  @param cachePath        按内容 MD5 推导的下载缓存路径；空串表示无法推导
 *                          （缺 MD5 / 缺文件名 / 未登录）
 *  @param cacheExists      上述缓存文件当前是否存在（同上，由调用方查磁盘得出）
 *  @return 应当使用的本地路径；返回空串表示本地没有可用文件、需要下载
 */
std::wstring ResolveLocalFilePath(const std::wstring& registeredPath, bool registeredExists,
                                  const std::wstring& cachePath, bool cacheExists);

} // namespace balloonchatwnd
