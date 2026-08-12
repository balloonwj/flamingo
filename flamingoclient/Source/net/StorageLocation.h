/**
 *  本地数据根目录的口径，StorageLocation.h
 *
 *  客户端所有按账号隔离的本地数据都挂在 <数据根目录>Users\<登录账号>\ 下（配置、
 *  聊天记录、公告、文件落点登记表、文件 / 图片 / 头像缓存，见 net/UserCachePath.h）。
 *  这个"数据根目录"以前固定等于程序主目录 g_szHomePath，现在允许用户在系统设置里
 *  改到别的盘 —— 本文件负责把"配置里读到的值"归一成实际可用的根目录。
 *
 *  <u>为什么这一项存在全局的 flamingo.ini 而不是账号配置里</u>：账号配置
 *  <账号>.cfg 本身就位于数据根目录之下，一旦根目录变了，客户端下次启动会去新位置
 *  找它、找不到就用默认值 —— 那样这一项永远不可能生效。故它只能存在数据根目录
 *  <u>之外</u>的全局配置里。
 *
 *  <u>改动后须重启才生效</u>：登录流程在很早的阶段就用这个根目录打开各个库与缓存
 *  目录，运行期切换会让已打开的句柄与新路径不一致。设置页因此只写配置并提示重启。
 *
 *  纯逻辑：不读配置文件、不碰磁盘、不读全局状态（两个入参都由调用方给），因此可以
 *  完全用单元测试覆盖（见 flamingoclient/Source/Tests/StorageLocationTests.cpp，Suite STL）。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <string>

namespace net {

/**
 *  把配置里读到的自定义数据目录归一成实际使用的数据根目录。
 *
 *  归一规则（任何一条不满足都回落到 defaultRoot，绝不返回一个可疑路径）：
 *    · 配置为空 —— 用户没改过，用默认；
 *    · 不是绝对路径（形如 "Data\\Flamingo" 或 "\\Users"）—— 相对路径会随进程的当前
 *      工作目录漂移，数据可能写到任何地方，一律拒绝；
 *    · 含有 ".." —— 配置文件是纯文本、可被手工改坏，含上跳的路径不接受；
 *    · 只给了盘符（"D:"）—— 补成 "D:\\"，否则后续拼接会得到 "D:Users\\..."
 *      这种相对于该盘当前目录的路径。
 *  归一后保证<u>以反斜杠结尾</u>，与 g_szHomePath 的既有约定一致，调用方可直接接
 *  "Users\\..." 往下拼。
 *
 *    configuredPath：flamingo.ini 中 [storage] datapath 的原始取值，可为空。
 *    defaultRoot：   默认根目录，即程序主目录 g_szHomePath；本身应以反斜杠结尾，
 *                    没带的话本函数会补上。
 *
 *  @return 实际数据根目录，以反斜杠结尾；defaultRoot 也为空时返回空串。
 */
std::wstring ResolveDataRoot(const std::wstring& configuredPath, const std::wstring& defaultRoot);

} // namespace net
