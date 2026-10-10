/**
 *  HTTP 短连接请求的纯解析函数，HttpRequestParse.h。
 *
 *  HttpSession 原先在 GET 分支对查询串直接 substr(2) 取 "p=" 之后的部分，查询串不足两个字符时抛出 std::out_of_range，
 *  异常从 HTTP I/O 线程逸出后整个 chatserver 退出。解析抽到这里，不合规的请求返回 false，由调用方断开连接；
 *  不碰网络，单元测试直接调用（tests/test_bot.cpp 的 BOT-S11）。
 *
 *  典型用法：
 *      std::string url, param;
 *      if (!httpparse::ParseGetTarget(target, url, param))
 *          conn->forceClose();
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <string>

namespace httpparse {

/**
 *  拆开 GET 请求的目标：参数约定写成 ?p=<值>（既有的 register.do 等接口）。
 *  @param target 请求行里的目标，如 "/register.do?p=xxx"
 *  @param url    [出参] 问号之前的路径
 *  @param param  [出参] p= 之后的值
 *  @return 有问号且查询串以 "p=" 开头时返回 true；其余（没有问号、查询串不足两个字符、不是 p=）返回 false
 */
bool ParseGetTarget(const std::string& target, std::string& url, std::string& param);

} // namespace httpparse
