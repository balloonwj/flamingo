/**
 *  音视频通话中转自环自测（调试入口），CallRelaySelfTest.h
 *
 *  进程内开两条 relay 连接（主叫端 / 被叫端，同一 sessionid、相反 role）连真实 imgserver
 *  中转端口，互发测试帧验证「连接 → 握手 → 双向透传」整条中转通路是否打通。仅供
 *  --callrelayselftest 调试入口手动验证：依赖 imgserver 在跑、且 imgserver.conf 的
 *  relaytokenkey 与传入密钥一致；不进自动化测试。
 *
 *  令牌签名复刻服务端 utils/RelayToken（HMAC-MD5(key, "sid|role|uid|peeruid|expire")
 *  + 小写十六进制），故本端可在不联 chatserver 的情况下自签出 imgserver 认可的令牌，
 *  单进程闭环验证中转。结构与远程桌面的 RdRelaySelfTest 一致，差异在于：角色用通话的
 *  主叫(3) / 被叫(4)、握手 / 数据走音视频的 1855 / 1856 cmd、载荷用 CallRelayTestFrame
 *  的可校验测试帧（接收端逐字节比对发现透传损坏）。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <string>

namespace call {

/**
 *  运行一次通话中转自环自测。阻塞执行，完成后返回结果摘要文本（多行，供 MessageBox /
 *  日志展示）。
 *  @param ip       imgserver 中转地址（一般 127.0.0.1）
 *  @param port     imgserver 中转端口（一般 20003，与远程桌面共用）
 *  @param tokenKey relay 令牌 HMAC 密钥（须与 imgserver.conf relaytokenkey 一致）
 *  @param serverPubKey 服务端公钥（net::IDENTITY_PUBLICKEY_BYTES 字节），由调用方取自主连接或宿主；
 *                 为 NULL 表示服务端未配置公钥、中转全程明文。只在本次调用期间使用
 *  @param outOk    [出参] 整体是否成功（双向透传均收到且测试帧逐字节一致）
 *  @return 结果摘要文本
 */
std::string RunCallRelaySelfTest(const std::string& ip, int port,
                                 const std::string& tokenKey, const unsigned char* serverPubKey, bool& outOk);

/**
 *  按 imgserver 的规则自签一枚中转令牌（复刻服务端 RelayToken::sign：「sid|role|uid|peeruid|expire」加上它的
 *  HMAC-MD5 小写十六进制）。供本自测与实网用例（Source/Tests/CallReportLiveTests.cpp）在不联 chatserver 时连中转。
 *  @param sessionId 会话 id（两端相同）
 *  @param role      角色，取 net::call_role_caller / call_role_callee
 *  @param uid       本端 userid
 *  @param peerUid   对端 userid
 *  @param expireMs  过期时刻（Unix 毫秒）
 *  @param key       HMAC 密钥，须与 imgserver.conf 的 relaytokenkey 一致
 *  @return 令牌
 */
std::string SignCallRelayToken(const std::string& sessionId, int role, long long uid, long long peerUid,
                               long long expireMs, const std::string& key);

} // namespace call
