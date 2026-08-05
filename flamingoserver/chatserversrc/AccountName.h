/**
 *  企业员工登录账号的生成规则，AccountName.h
 *
 *  登录账号（t_user.f_username）不再由用户自己起名，一律由服务端按用户ID
 *  自动生成，全局唯一。两条建号路径都必须用本文件的函数，不要各写一份：
 *    · admin 控制台建员工   —— EmployeeManager::createEmployee
 *    · PC 客户端自助注册     —— UserManager::addUser（经 RegisterService）
 *  历史上这两条路径各自 snprintf 拼 "EMP%lld"，一旦有人改了格式另一边不会
 *  跟着变，故收敛到这里。
 *
 *  纯函数、不依赖数据库与任何单例，便于单元测试（见 tests/test_accountname.cpp）。
 *
 *  典型用法：
 *      int32_t     userId  = Singleton<UserManager>::Instance().allocUserId();
 *      std::string account = MakeEmployeeAccount(userId);   // "EMP1024"
 *
 *  balloonwj@qq.com   2026-08-04
 */
#ifndef __ACCOUNT_NAME_H__
#define __ACCOUNT_NAME_H__

#include <stdint.h>
#include <string>

//自动生成的员工登录账号的前缀。取 employee 的缩写，与 admin 控制台历史上
//生成的账号保持一致，存量账号无需迁移。
#define EMPLOYEE_ACCOUNT_PREFIX   "EMP"

/**
 *  按用户ID生成登录账号。
 *  @param userId 服务器分配的用户ID，须为正数；调用方保证其全局唯一
 *                （统一由 UserManager::allocUserId 分配）。
 *  @return 形如 "EMP1024" 的账号串。userId 非正数时返回空串——那说明调用方
 *          的ID分配出了问题，宁可让建号失败也不要造出一个会与别人相撞的账号。
 *
 *  账号的唯一性完全由 userId 的唯一性保证：ID 唯一则账号唯一，库上还有
 *  t_user.uk_f_username 兜底。
 */
std::string MakeEmployeeAccount(int64_t userId);

#endif // __ACCOUNT_NAME_H__
