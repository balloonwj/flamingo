/**
 *  企业员工登录账号的生成规则，AccountName.h
 *
 *  2026-10-06 起（docs/登录账号与手机号登录方案.md）新账号改为「前缀 + 8 位随机数字」：前缀取企业在
 *  管理后台设置的账号前缀（NormalizeAccountPrefix 规整），没设时用 EMP；随机数由
 *  UserManager::allocRandomAccount 抽号并查重，本文件的 MakeRandomAccount 只负责拼接。按用户ID拼账号的
 *  MakeEmployeeAccount 保留给存量账号的说明与单测，不再用于建号。手机号格式规则 IsValidPhoneNumber
 *  也放在这里（注册、管理后台建员工、按手机号登录共用）。
 *
 *  登录账号（t_user.f_username）不再由用户自己起名，一律由服务端
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

//随机账号数字部分的位数与取值范围：8 位、首位不为 0（docs/登录账号与手机号登录方案.md 第 3.3 节）
#define RANDOM_ACCOUNT_DIGITS       8
#define RANDOM_ACCOUNT_NUMBER_MIN   10000000
#define RANDOM_ACCOUNT_NUMBER_MAX   99999999

//企业账号前缀的长度上限（字符数），与 t_enterprise.f_account_prefix 的 varchar(16) 留有余量
#define ACCOUNT_PREFIX_MAX_LENGTH   10

/**
 *  拼出一个随机账号：前缀 + 8 位数字。随机数由调用方给出（UserManager::allocRandomAccount 抽号并查重），
 *  本函数只负责拼接，便于单元测试。
 *  @param prefix 企业账号前缀（已规整）；为空时用默认的 EMPLOYEE_ACCOUNT_PREFIX
 *  @param number 8 位数字部分，取值 [RANDOM_ACCOUNT_NUMBER_MIN, RANDOM_ACCOUNT_NUMBER_MAX]；越界时返回空串
 *  @return 形如 "EMP48203917"、"HW.48203917" 的账号
 */
std::string MakeRandomAccount(const std::string& prefix, int32_t number);

/**
 *  校验并规整企业账号前缀：去掉首尾空白后，空串表示清除（合法）；否则须为 1～ACCOUNT_PREFIX_MAX_LENGTH 个
 *  字符、只含英文字母与点号「.」、至少一个字母，规整为大写。
 *  @param raw 管理员填写的原文
 *  @param out [出参] 合法时为规整后的前缀（可能为空串，表示清除）
 *  @return 合法返回 true
 */
bool NormalizeAccountPrefix(const std::string& raw, std::string& out);

//手机号的位数（中国大陆手机号）
#define PHONE_NUMBER_LENGTH   11

/**
 *  手机号格式是否合法：恰好 11 位、以 1 开头、全部是数字（docs/登录账号与手机号登录方案.md 第 3.4 节）。
 *  自助注册、管理后台新建与编辑员工、按手机号登录共用这一条规则。
 *  @param phone 手机号，不做去空白处理：带空格、横线的一律不合格
 *  @return 合法返回 true
 */
bool IsValidPhoneNumber(const std::string& phone);

#endif // __ACCOUNT_NAME_H__
