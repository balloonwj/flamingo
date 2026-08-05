/**
 *  文件气泡本地落点决策实现，ChatFileLocation.cpp
 *  设计与约定见 ChatFileLocation.h。
 *  balloonwj@qq.com   2026-08-03
 */
#include "ChatFileLocation.h"

namespace balloonchatwnd {

std::wstring ResolveLocalFilePath(const std::wstring& registeredPath, bool registeredExists,
                                  const std::wstring& cachePath, bool cacheExists)
{
    //登记路径优先：那是用户实际持有该文件的位置
    if (!registeredPath.empty() && registeredExists)
    {
        return registeredPath;
    }

    //登记失效（文件被移走 / 删除）或压根没登记时，回退到按 MD5 推导的缓存副本
    if (!cachePath.empty() && cacheExists)
    {
        return cachePath;
    }

    //两处都没有：本地确实没这个文件，调用方据此渲染成待下载态
    return std::wstring();
}

} // namespace balloonchatwnd
