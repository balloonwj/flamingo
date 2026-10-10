/**
 *  FlamingoSecretChat.dll 内部的日志出口：把格式化好的一行日志（已含 "[secret] " 前缀）经宿主（ISecretChatHost::Log）
 *  写进 exe 的日志文件。做法与 FlamingoP2P 的 P2PLog 相同：DLL 里照常使用 LOG_INFO 等宏，由 HostLogBridge.cpp 在
 *  DLL 内实现 CIULog::Log，格式化后调用本文件的 SecretLogWriteLine。
 *
 *  密聊的日志只记会话号、状态与错误码，从不记聊天内容与密钥。
 *
 *  可在任意线程调用。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include "../SecretChatApi.h"

namespace secretchat {

//单行日志格式化后的最大字节数（含结尾 '\0'），与其它插件 DLL 一致
const int kSecretLogLineBytes = 2048;

//每行日志开头固定加的前缀
const char* const kSecretLogPrefix = "[secret] ";

/**
 *  登记或解除宿主。模块 Init 时登记、Shutdown 时传 NULL 解除；解除返回之后不会再有线程经旧宿主写日志。
 *  @param host 宿主接口（不转移所有权）；NULL 表示解除
 */
void SecretLogSetHost(ISecretChatHost* host);

/**
 *  把一行已经格式化好（已含前缀）的日志交给宿主写出。
 *  @return 已交给宿主返回 true；未登记宿主或 text 为 NULL 返回 false
 */
bool SecretLogWriteLine(int level, const char* text);

} // namespace secretchat
