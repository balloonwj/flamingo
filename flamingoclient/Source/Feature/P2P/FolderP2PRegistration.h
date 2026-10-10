/**
 *  单聊文件夹传输向 P2P 服务登记 / 注销的入口（P2P 方案第 5 期），FolderP2PRegistration.h
 *
 *  FolderP2PTransport.h 带进 <winsock2.h>，使用预编译头的编译单元（如 AppShell.cpp）不能包含它；本头文件只前置声明
 *  P2P 服务接口，供它们调用。实现在 FolderP2PTransport.cpp。
 *
 *  典型用法（CAppShell）：
 *      if (CP2PHost::GetInstance().Load(...))
 *          balloonp2p::RegisterFolderP2P(CP2PHost::GetInstance().GetService());
 *      ...
 *      balloonp2p::UnregisterFolderP2P();
 *      CP2PHost::GetInstance().Shutdown();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

namespace p2p {
class IP2PService;
}

namespace balloonp2p {

/**
 *  把单聊文件夹传输登记为用途 folder 的业务方。P2P 模块加载成功之后、第一次登录之前调用。
 *  诊断开关：环境变量 FLAMINGO_FOLDER_P2P 为 0 时不登记，文件夹传输照旧全部走中转；其余情况都登记（默认打开）。
 *  @param service P2P 服务（借用）
 *  @return 登记成功返回 true；开关关闭或登记失败返回 false
 */
bool RegisterFolderP2P(p2p::IP2PService* service);

//注销（P2P 模块关闭之前调用）；未登记时无效果
void UnregisterFolderP2P();

/**
 *  把单聊单文件直传登记为用途 file 的业务方（2026-10-04 起）。与 RegisterFolderP2P 同时机调用：2301 报到了 file，
 *  服务端才会判定能直传。诊断开关：环境变量 FLAMINGO_DIRECT_FILE_P2P 为 0 时不登记，单聊发文件一律上传服务器。
 *  @param service P2P 服务（借用）
 *  @return 登记成功返回 true；开关关闭或登记失败返回 false
 */
bool RegisterDirectFileP2P(p2p::IP2PService* service);

//注销单文件直传的业务方（P2P 模块关闭之前调用）；未登记时无效果
void UnregisterDirectFileP2P();

/**
 *  本进程是否已登记单文件直传的业务方（聊天窗据此决定发文件时要不要先试在线直传）。可在任意线程调用。
 *  @return 已登记返回 true；P2P 模块没加载、诊断开关关掉、或已注销时返回 false
 */
bool IsDirectFileP2PRegistered();

} // namespace balloonp2p
