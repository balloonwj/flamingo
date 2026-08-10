/**
 *  flamingoAdmin —— TCP 连通性探测。
 *
 *  网络设置页的「测试连接」按钮用它来判断填写的服务器地址是否可达。
 *  ProbeTcp 是阻塞调用（最长 timeoutMs 毫秒），必须放在【工作线程】里
 *  执行，不可在 UI 线程直接调用，否则会卡住界面。
 *
 *  本头文件不暴露任何 winsock 类型，调用方无需引入 winsock 头。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

namespace flamingoadmin {

// TCP 连通性探测结果。
enum class ProbeResult
{
    Ok,        // 连接成功：在超时内完成 TCP 握手
    Refused,   // 连接被拒绝 / 创建套接字失败 / 地址错误
    Timeout,   // 连接超时：超时内服务器无响应
    DnsFail,   // 主机名解析失败
};

// 同步探测 host:port 的 TCP 连通性。
//   host：服务器主机名或 IP。
//   port：服务器端口（1 - 65535）。
//   timeoutMs：连接超时（毫秒）。
//   返回：探测结果。
// 注意：阻塞调用，请在工作线程里使用。Winsock 在首次调用时惰性初始化。
ProbeResult ProbeTcp(const CString& host, int port, int timeoutMs);

} // namespace flamingoadmin
