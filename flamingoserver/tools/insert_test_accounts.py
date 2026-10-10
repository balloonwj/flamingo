#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
插入一组 v2 测试账号到 t_user，并把它们挂到「云启智能」的「测试组」部门。

用途：本地手工测试 flamingoclient（v2 登录链路）时需要一批口令已知的账号；
      生产库里的存量账号 f_password 还是 v1 的 MD5(明文)，不能直接配
      v2 客户端的 SHA-256(明文) 校验。本脚本直接按 v2 口令存储格式
      （f_password = MD5(SHA256(明文) + f_salt)）批量造测试号，同时把
      它们登记进「云启智能」(enterprise_id=1) → 运营中心 → 测试组
      (dept_id=1610612745)，让账号有真实的企业 / 部门归属，便于组织架构
      / 部门消息等功能联调。

口令存储格式与服务端 BussinessLogic::verifyPassword 一致：
    sha = SHA256(明文密码 UTF-8 字节).hexdigest().lower()
    pwd = MD5( (sha + f_salt) UTF-8 字节 ).hexdigest().lower()
    存入 t_user.f_password = pwd, t_user.f_salt = 上面用到的 salt
客户端 cmd_login 上行：account=登录名、password=SHA256(明文).hex().lower()
服务端用 f_salt 重算 MD5(received + salt) 与 f_password 比对成功即登录。

幂等：脚本两步都走 INSERT ... ON DUPLICATE KEY UPDATE，重跑只刷新内容
不会产生重复行。需要彻底清除测试账号改用：
    DELETE FROM t_department_member WHERE f_user_id BETWEEN 99990 AND 99994;
    DELETE FROM t_user              WHERE f_user_id BETWEEN 99990 AND 99994;

balloonwj@qq.com   2026-05-25
"""

import hashlib
import sys
import pymysql

# ---- 数据库连接（与 flamingoserver/etc/chatserver.conf 保持一致）----
DB_HOST = "101.37.25.166"
DB_PORT = 3306
DB_USER = "root"
DB_PASSWORD = "123456"
DB_NAME = "flamingo"

# ---- 测试账号归属（云启智能 / 测试组）----
# 查询自当前线上库（已实证）：
#   t_enterprise: id=1   云启智能科技有限公司
#   t_department: id=1610612745  测试组（路径 总部 → 运营中心 → 测试组）
TEST_ENTERPRISE_ID = 1
TEST_DEPT_ID = 1610612745
TEST_DEPT_POSITION = "测试工程师"

# ---- 待插入的测试账号清单 ----
# 每个元组：(f_user_id, f_username, 明文密码, f_salt, 昵称, 个性签名, f_employee_no)
# 选择 99990~99994 区间避开生产库现有 userid（生产库 userid 在 7000 以下）。
# 同一明文密码 + 不同 salt + 不同 user_id，便于覆盖"多账号下拉切换"测试。
# f_employee_no 取 TEST9999x 形态，避免与企业内现有员工工号冲突
# （t_user 唯一键 uk_ent_employee_no = (f_enterprise_id, f_employee_no)）。
TEST_ACCOUNTS = [
    (99990, "test99990", "test1234",
     "test99990_salt0000000000", "测试账号A", "v2 测试账号 - 勿删", "TEST99990"),
    (99991, "test99991", "test1234",
     "test99991_salt0000000000", "测试账号B", "v2 测试账号 - 勿删", "TEST99991"),
    (99992, "test99992", "test1234",
     "test99992_salt0000000000", "测试账号C", "v2 测试账号 - 勿删", "TEST99992"),
    (99993, "test99993", "test1234",
     "test99993_salt0000000000", "测试账号D", "v2 测试账号 - 勿删", "TEST99993"),
    (99994, "test99994", "test1234",
     "test99994_salt0000000000", "测试账号E", "v2 测试账号 - 勿删", "TEST99994"),
]


def derive_v2_password(plaintext: str, salt: str) -> str:
    """按 v2 口令存储格式算出可直接落库的 f_password。

    参数：
        plaintext  明文密码（任意可打印字符串）
        salt       与本账号绑定的盐值，长度需 <= 32（t_user.f_salt 上限）
    返回：
        小写十六进制 MD5 摘要（32 字节字符串）
    """
    sha_hex = hashlib.sha256(plaintext.encode("utf-8")).hexdigest().lower()
    return hashlib.md5((sha_hex + salt).encode("utf-8")).hexdigest().lower()


def upsert_account(cursor, user_id: int, username: str, plaintext: str,
                   salt: str, nickname: str, signature: str,
                   employee_no: str) -> None:
    """把一行测试账号插入或就地更新到 t_user。

    任何主键 / 唯一键冲突走 ON DUPLICATE KEY UPDATE 路径，把口令哈希、
    salt、昵称、签名、企业 / 工号一并刷新——保证重复执行后 5 行测试账号
    的口令始终是本脚本里写死的明文密码，企业归属也始终为云启智能。
    """
    password_hash = derive_v2_password(plaintext, salt)
    sql = (
        "INSERT INTO t_user "
        "(f_user_id, f_username, f_nickname, f_password, f_salt, "
        " f_signature, f_register_time, f_enterprise_id, f_role, "
        " f_employee_no, f_join_date, f_status, f_usertype) "
        "VALUES (%s, %s, %s, %s, %s, %s, NOW(), %s, 0, %s, NOW(), 0, 0) "
        "ON DUPLICATE KEY UPDATE "
        "  f_password = VALUES(f_password), "
        "  f_salt = VALUES(f_salt), "
        "  f_nickname = VALUES(f_nickname), "
        "  f_signature = VALUES(f_signature), "
        "  f_enterprise_id = VALUES(f_enterprise_id), "
        "  f_employee_no = VALUES(f_employee_no), "
        "  f_role = 0, "
        "  f_status = 0"
    )
    cursor.execute(sql, (user_id, username, nickname, password_hash,
                         salt, signature, TEST_ENTERPRISE_ID, employee_no))


def upsert_dept_member(cursor, user_id: int) -> None:
    """把测试账号挂到「云启智能 → 测试组」部门下作为正式成员。

    唯一键 uniq_dept_user(f_dept_id, f_user_id) 保证同 (部门, 用户) 只一行；
    重复执行刷新 f_is_primary / f_position / f_status，账号始终是测试组的
    在职主部门成员。
    """
    sql = (
        "INSERT INTO t_department_member "
        "(f_dept_id, f_enterprise_id, f_user_id, f_position, "
        " f_is_primary, f_member_order, f_status, f_join_time) "
        "VALUES (%s, %s, %s, %s, 1, 0, 0, NOW()) "
        "ON DUPLICATE KEY UPDATE "
        "  f_enterprise_id = VALUES(f_enterprise_id), "
        "  f_position = VALUES(f_position), "
        "  f_is_primary = 1, "
        "  f_status = 0"
    )
    cursor.execute(sql, (TEST_DEPT_ID, TEST_ENTERPRISE_ID, user_id,
                         TEST_DEPT_POSITION))


def main() -> int:
    """连接 MySQL，把 TEST_ACCOUNTS 全部 upsert 进 t_user。返回进程退出码。"""
    print(f"connecting mysql {DB_USER}@{DB_HOST}:{DB_PORT}/{DB_NAME} ...")
    try:
        conn = pymysql.connect(
            host=DB_HOST,
            port=DB_PORT,
            user=DB_USER,
            password=DB_PASSWORD,
            database=DB_NAME,
            charset="utf8mb4",
            autocommit=False,
        )
    except pymysql.MySQLError as e:
        print(f"[FATAL] connect failed: {e}", file=sys.stderr)
        return 1

    try:
        with conn.cursor() as cur:
            print(f"upserting {len(TEST_ACCOUNTS)} test accounts into t_user ...")
            for row in TEST_ACCOUNTS:
                upsert_account(cur, *row)
                user_id, username, plaintext, _, nickname, _, emp_no = row
                print(f"  user  userid={user_id:<6} username={username:<12} "
                      f"plain={plaintext!r:<12} empno={emp_no:<10} nick={nickname}")
            print(f"linking {len(TEST_ACCOUNTS)} test accounts to "
                  f"dept_id={TEST_DEPT_ID} (云启智能/测试组) ...")
            for row in TEST_ACCOUNTS:
                user_id = row[0]
                upsert_dept_member(cur, user_id)
                print(f"  dept  userid={user_id:<6} -> dept {TEST_DEPT_ID} "
                      f"position={TEST_DEPT_POSITION}")
        conn.commit()
        print("commit ok.")
    except pymysql.MySQLError as e:
        conn.rollback()
        print(f"[FATAL] upsert failed (rolled back): {e}", file=sys.stderr)
        return 2
    finally:
        conn.close()

    print("\n=== 测试账号清单（明文密码统一为 test1234；归属: 云启智能/测试组）===")
    for user_id, username, plaintext, _, nickname, _, emp_no in TEST_ACCOUNTS:
        print(f"  userid={user_id:<6} username={username:<12} "
              f"password={plaintext:<10} empno={emp_no:<10} nick={nickname}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
