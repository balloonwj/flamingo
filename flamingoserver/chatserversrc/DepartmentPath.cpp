/**
 *  企业部门物化路径算法实现，DepartmentPath.cpp
 *  balloonwj@qq.com   2026.05.21
 */
#include "DepartmentPath.h"

namespace dept {

//物化路径的分隔符，路径形如 "/1001/1005/"。
static const char PATH_SEPARATOR = '/';

std::string MakeChildPath(const std::string& parentPath, int64_t deptId)
{
    //根部门 parentPath 为空串，其路径从单个分隔符起算；
    //非根部门的父路径已以 '/' 结尾，直接在其后追加 "本部门ID/"。
    std::string base = parentPath.empty() ? std::string(1, PATH_SEPARATOR) : parentPath;
    return base + std::to_string(deptId) + PATH_SEPARATOR;
}

int MakeChildLevel(int parentLevel)
{
    //根部门 parentLevel 传 0，故根部门层级为 1；其余逐级 +1。
    return parentLevel + 1;
}

bool IsAncestorOrSelf(const std::string& ancestorPath, const std::string& nodePath)
{
    //ancestorPath 以 '/' 结尾，nodePath 以其为前缀即说明位于其子树内。
    //因前缀以 '/' 收尾，"/1001/" 不会成为 "/10011/" 的前缀，天然避免粘连误判。
    //空的 ancestorPath 不视为任何节点的祖先。
    if (ancestorPath.empty() || nodePath.size() < ancestorPath.size())
    {
        return false;
    }

    return nodePath.compare(0, ancestorPath.size(), ancestorPath) == 0;
}

bool WouldFormCycle(const std::string& movingDeptPath, const std::string& newParentPath)
{
    //目标父部门若是被移动部门自身或其子孙，移动后将形成环。
    return IsAncestorOrSelf(movingDeptPath, newParentPath);
}

std::string RebasePath(const std::string& nodePath,
                       const std::string& oldPrefix,
                       const std::string& newPrefix)
{
    //仅当 nodePath 确实位于旧子树内（以 oldPrefix 开头）才改写前缀，
    //否则原样返回，以防误改子树之外不相关的节点。
    if (oldPrefix.empty() || nodePath.compare(0, oldPrefix.size(), oldPrefix) != 0)
    {
        return nodePath;
    }

    return newPrefix + nodePath.substr(oldPrefix.size());
}

} // namespace dept
