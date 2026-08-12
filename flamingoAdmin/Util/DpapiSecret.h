/**
 *  flamingoAdmin —— 本地口令保护（Windows DPAPI）。
 *
 *  "记住密码"要把管理员口令留在 exe 同目录的 flamingoAdmin.ini 里。ini 是明文
 *  文本文件，直接写口令等于放在桌面上；本文件用 Windows 自带的 DPAPI
 *  （CryptProtectData / CryptUnprotectData）把它加密后再存，密文用 Base64 编成
 *  可写进 ini 的一行 ASCII。
 *
 *  DPAPI 的密钥由当前 Windows 用户账户派生，因此：
 *   · 换一台机器、或同机换一个 Windows 用户，密文解不开 —— 此时按"没记过密码"
 *     处理，让用户重新输入即可，不是错误；
 *   · 它挡的是"别人拿到这个 ini 文件"，挡不住"别人已经登进了你的 Windows 账户"。
 *     后者需要的是操作系统层面的账户隔离，不在本文件的职责内。
 *
 *  典型用法：
 *      CString cipher;
 *      if (ProtectSecret(_T("mypassword"), cipher))
 *          ...把 cipher 写进 ini...
 *
 *      CString plain;
 *      if (UnprotectSecret(cipher, plain))
 *          ...用 plain 回填密码框...
 *      else
 *          ...当作没记过密码，让用户手输...
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

namespace flamingoadmin {

/**
 *  用 DPAPI 加密一段口令，输出可直接写进 ini 的 Base64 文本。
 *  @param plain     待保护的明文口令；为空串时输出空串并返回 true
 *                   （"没有密码要存"是正常情形，不算失败）。
 *  @param outCipher [出参] Base64 编码的密文；仅在返回 true 时有效。
 *  @return 加密并编码成功返回 true；DPAPI 调用失败返回 false（此时调用方
 *          应当放弃保存密码，而不是退而求其次存明文）。
 */
bool ProtectSecret(const CString& plain, CString& outCipher);

/**
 *  还原 ProtectSecret 产出的密文。
 *  @param cipher   Base64 编码的密文；为空串时输出空串并返回 true。
 *  @param outPlain [出参] 还原出的明文口令；仅在返回 true 时有效。
 *  @return 成功返回 true。以下情形返回 false，调用方一律按"没记过密码"处理：
 *          Base64 非法、密文被截断或篡改、换了机器 / Windows 用户导致解不开。
 */
bool UnprotectSecret(const CString& cipher, CString& outPlain);

} // namespace flamingoadmin
