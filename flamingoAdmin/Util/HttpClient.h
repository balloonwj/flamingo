/**
 *  flamingoAdmin —— 极简 HTTP 客户端（POST JSON）。
 *
 *  admin 控制台的全部接口都是「POST /admin/<模块>/<动作>.do + JSON 请求体」的
 *  短连接调用，本文件只提供这一种能力：同步地向服务器 POST 一段 JSON、取回
 *  应答体。底层用 WinHTTP，因而能同时支持明文 http 与 TLS https（由 useTls 决定）。
 *
 *  HttpPostJson 是【阻塞】调用，必须放在工作线程执行，不可在 UI 线程直接调用，
 *  否则会卡住界面。请求体与应答体一律按 UTF-8 字节处理。
 *
 *  典型用法（工作线程内）：
 *      HttpResult r = HttpPostJson(_T("127.0.0.1"), 12345, false,
 *                                  _T("/admin/login.do"), jsonUtf8, 5000);
 *      if (r.m_transportOk && r.m_statusCode == 200) { 解析 r.m_body ... }
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <atlstr.h>
#include <string>

namespace flamingoadmin {

// 一次 HTTP 请求的结果。
// m_transportOk 区分「网络层是否成功完成一次往返」与「业务是否成功」：
//   · m_transportOk == false：连接 / 收发失败，m_error 是给用户看的中文原因；
//   · m_transportOk == true ：往返完成，m_statusCode / m_body 有效，业务成败
//                             由调用方解析 m_body 里的 JSON 自行判断。
struct HttpResult
{
    bool        m_transportOk;   // 是否成功完成一次 HTTP 往返
    int         m_statusCode;    // HTTP 状态码（仅 m_transportOk 为 true 时有效）
    std::string m_body;          // 应答体 UTF-8 字节（仅 m_transportOk 为 true 时有效）
    CString     m_error;         // 传输失败原因（仅 m_transportOk 为 false 时有效）

    HttpResult()
        : m_transportOk(false), m_statusCode(0)
    {
    }
};

// 同步 POST 一段 JSON 到 host:port 的指定路径。
//   host：服务器主机名 / IP。
//   port：服务器端口（取值 1 - 65535）。
//   useTls：true 走 https（WINHTTP_FLAG_SECURE），false 走明文 http。
//   path：请求路径，须以 '/' 开头，例如 "/admin/login.do"。
//   jsonUtf8：请求体，UTF-8 编码的 JSON 字节串（不会被本函数修改）。
//   timeoutMs：解析 / 连接 / 发送 / 接收 各阶段的超时（毫秒）。
//   返回：见 HttpResult。
// 注意：阻塞调用，请在工作线程中使用。所有 WinHTTP 句柄在函数内自建自释，
//       不会泄漏；首次调用时 WinHTTP 自行完成初始化。
HttpResult HttpPostJson(const CString& host, int port, bool useTls,
                        const CString& path, const std::string& jsonUtf8,
                        int timeoutMs);

} // namespace flamingoadmin
