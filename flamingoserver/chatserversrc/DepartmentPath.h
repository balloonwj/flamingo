/**
 *  企业部门物化路径算法，DepartmentPath.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  部门树采用"邻接表 + 物化路径"存储（见 table.sql 的 t_department）。
 *  本文件提供物化路径的纯计算函数：拼接、层级推导、祖先判定、成环检测、子树改写。
 *  全部为纯函数，仅依赖 STL，不触碰数据库与网络，便于单元测试。
 */
#ifndef __DEPARTMENT_PATH_H__
#define __DEPARTMENT_PATH_H__

#include <stdint.h>
#include <string>

namespace dept {

//物化路径以 '/' 开头和结尾，中间是自根到本节点的部门ID链，例如 "/1001/1005/"。
//约定：根部门的父路径为空串 ""，父层级为 0。

/**
 *  由父部门的物化路径和本部门ID，拼出本部门的物化路径。
 *  @param parentPath 父部门的物化路径；本部门为根部门时传空串 ""。
 *  @param deptId     本部门的业务ID，取值为正整数。
 *  @return 本部门的物化路径，形如 "/1001/1005/"，始终以 '/' 开头和结尾。
 */
std::string MakeChildPath(const std::string& parentPath, int64_t deptId);

/**
 *  由父部门层级推导本部门层级。
 *  @param parentLevel 父部门层级；本部门为根部门时传 0。
 *  @return 本部门层级，等于 parentLevel + 1；根部门返回 1。
 */
int MakeChildLevel(int parentLevel);

/**
 *  判断 ancestorPath 对应部门是否为 nodePath 对应部门的祖先或其自身。
 *  以 '/' 为边界做前缀匹配，避免 "/1001/" 误命中 "/10011/"。
 *  @param ancestorPath 疑似祖先部门的物化路径。
 *  @param nodePath     待判定部门的物化路径。
 *  @return true 表示 nodePath 在 ancestorPath 的子树内（含 ancestorPath 自身）。
 */
bool IsAncestorOrSelf(const std::string& ancestorPath, const std::string& nodePath);

/**
 *  判断把某部门移动到新父部门之下是否会使部门树成环。
 *  当新父部门正是被移动部门自身、或被移动部门的子孙时，移动会成环，必须拒绝。
 *  @param movingDeptPath 被移动部门当前的物化路径。
 *  @param newParentPath  目标父部门的物化路径。
 *  @return true 表示会成环，应拒绝该次移动。
 */
bool WouldFormCycle(const std::string& movingDeptPath, const std::string& newParentPath);

/**
 *  子树移动时，把被移动子树内某个节点的物化路径从旧前缀改写到新前缀。
 *  @param nodePath  子树内某节点（含被移动子树根节点本身）当前的物化路径。
 *  @param oldPrefix 被移动子树根节点移动前的物化路径。
 *  @param newPrefix 被移动子树根节点移动后的物化路径。
 *  @return 该节点改写后的物化路径；若 nodePath 不以 oldPrefix 开头则原样返回。
 */
std::string RebasePath(const std::string& nodePath,
                       const std::string& oldPrefix,
                       const std::string& newPrefix);

} // namespace dept

#endif // __DEPARTMENT_PATH_H__
