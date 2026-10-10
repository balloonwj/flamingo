/**
 *  远程桌面中转自环自测（调试入口），RdRelaySelfTest.h
 *
 *  进程内开两条 relay 连接（控制端 / 被控端，同一 sessionid、相反 role）连真实
 *  imgserver 中转端口，互发字节验证「连接 → 握手 → 双向透传」整条中转通路是否
 *  打通。仅供 --rdrelayselftest 调试入口手动验证：依赖 imgserver 在跑、且
 *  imgserver.conf 的 relaytokenkey 与传入密钥一致；不进自动化测试。
 *
 *  令牌签名复刻服务端 utils/RelayToken（HMAC-MD5(key, "sid|role|uid|peeruid|expire")
 *  + 小写十六进制），故本端可在不联 chatserver 的情况下自签出 imgserver 认可的
 *  令牌，单进程闭环验证中转。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>

namespace remotedesktop {

/**
 *  运行一次中转自环自测。阻塞执行，完成后返回结果摘要文本（多行，供 MessageBox /
 *  日志展示）。
 *  @param ip       imgserver 中转地址（一般 127.0.0.1）
 *  @param port     imgserver 中转端口（一般 20003）
 *  @param tokenKey relay 令牌 HMAC 密钥（须与 imgserver.conf relaytokenkey 一致）
 *  @param serverPubKey 服务端公钥（net::IDENTITY_PUBLICKEY_BYTES 字节），由调用方取自主连接或宿主；
 *                 为 NULL 表示服务端未配置公钥、中转全程明文。只在本次调用期间使用
 *  @param outOk    [出参] 整体是否成功（双向透传均收到且内容比对一致）
 *  @return 结果摘要文本
 */
std::string RunRdRelaySelfTest(const std::string& ip, int port,
                               const std::string& tokenKey, const unsigned char* serverPubKey, bool& outOk);

} // namespace remotedesktop
