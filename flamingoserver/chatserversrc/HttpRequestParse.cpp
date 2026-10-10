/**
 *  HTTP 短连接请求的纯解析函数实现，HttpRequestParse.cpp。约定见 HttpRequestParse.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "HttpRequestParse.h"

namespace httpparse {

namespace {
//查询串里参数名的前缀
const char* const kParamPrefix = "p=";
//前缀长度
const size_t kParamPrefixLen = 2;
}

bool ParseGetTarget(const std::string& target, std::string& url, std::string& param)
{
    url.clear();
    param.clear();
    const size_t q = target.find('?');
    if (q == std::string::npos || q == 0)
        return false;
    const std::string query = target.substr(q + 1);
    if (query.size() < kParamPrefixLen || query.compare(0, kParamPrefixLen, kParamPrefix) != 0)
        return false;
    url = target.substr(0, q);
    param = query.substr(kParamPrefixLen);
    return true;
}

} // namespace httpparse
