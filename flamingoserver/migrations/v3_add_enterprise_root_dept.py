#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
为存量企业建立"公司根部门"，并把原有部门整体下沉一级。

背景：组织架构树原先没有代表"公司本身"的真实部门行，管理端是在客户端合成一个
      假的根节点顶替。结果是员工无法直接挂在公司下——往公司根挂人时传给服务端的
      部门ID是 0，服务端据此跳过入部门这一步，员工建出来却不属于任何部门，在管理
      端组织架构和 IM 客户端通讯录里都看不见。

本脚本做四件事（按企业逐个处理，每个企业一个事务）：
  1) 若 t_enterprise 还没有 f_root_dept_id 列则补上（chatserver 启动时 checkTable
     也会自动补，这里兜底是为了允许在服务尚未升级时先跑迁移）。
  2) 为每个尚无根部门的企业插入一行公司根部门：父部门为 0、层级为 1、名字取企业全称。
  3) 把该企业原有的全部部门下沉一级：层级加一、物化路径最前面插入新根的ID；原先
     的顶级部门改为指向新根。停用状态的部门同样处理，否则日后重新启用会路径错乱。
  4) 把该企业下"有企业归属却不属于任何在职部门"的孤儿员工补挂到公司根部门。

幂等性：已经有 f_root_dept_id（非 0）的企业会被跳过，重复执行不会产生第二个根、
        也不会重复下沉路径。注意物化路径的改写本身不幂等（见
        tests/test_department_path.cpp 的 A18），跳过判断是唯一的防重复手段，
        不要绕过它手工执行其中的 UPDATE。

执行前提：必须在 chatserver 停机时执行，或执行完毕后重启 chatserver。原因是
          DepartmentManager 的组织架构缓存与部门ID分配基数 m_baseDeptId 都在进程
          启动时从库里载入，运行期间改库会让缓存与ID分配基数与实际数据脱节。

用法：
    python3 v3_add_enterprise_root_dept.py

balloonwj@qq.com   2026-07-31
"""

import os
import sys
import pymysql

# MySQL 默认端口。chatserver.conf 不含端口配置项，服务端也是按默认端口连的。
DEFAULT_DB_PORT = 3306


def load_db_config():
    """从 flamingoserver/etc/chatserver.conf 读出数据库连接参数。

    直接读服务端自己的配置文件，而不是在脚本里再硬编码一份：仓库里已有的运维
    脚本抄的是某个历史地址，早已连不上了。配置文件是简单的 key=value 格式
    （对应服务端的 base/ConfigFileReader），缺文件或缺项时回退到本机默认值。
    @return 五元组 (host, port, user, password, dbname)。
    """
    defaults = {
        "dbserver": "127.0.0.1",
        "dbuser": "root",
        "dbpassword": "123456",
        "dbname": "flamingo",
    }

    conf_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             os.pardir, "etc", "chatserver.conf")
    try:
        with open(conf_path, "r", encoding="utf-8", errors="replace") as conf:
            for line in conf:
                line = line.strip()
                # 跳过空行与以 # 开头的注释行。
                if not line or line.startswith("#"):
                    continue
                if "=" not in line:
                    continue
                key, _, value = line.partition("=")
                key = key.strip()
                if key in defaults:
                    defaults[key] = value.strip()
    except OSError as exc:
        print("读取 %s 失败（%s），改用默认的本机连接参数。" % (conf_path, exc),
              file=sys.stderr)

    return (defaults["dbserver"], DEFAULT_DB_PORT, defaults["dbuser"],
            defaults["dbpassword"], defaults["dbname"])


DB_HOST, DB_PORT, DB_USER, DB_PASSWORD, DB_NAME = load_db_config()

# 部门业务ID的起始基数，必须与 chatserversrc/DepartmentManager.h 的
# DEPARTMENT_ID_BASE 保持一致，否则本脚本分配的ID会与服务端运行期分配的ID冲突。
DEPARTMENT_ID_BASE = 0x60000000

# 部门状态与成员状态取值，与 table.sql 中各表的 f_status 注释一致。
DEPT_STATUS_NORMAL = 0      # 部门正常在用
MEMBER_STATUS_ACTIVE = 0    # 成员在职

# 公司根部门的固定属性：它是整棵组织架构树的根。
ROOT_PARENT_DEPT_ID = 0     # 无父部门
ROOT_DEPT_LEVEL = 1         # 层级为最外层
ROOT_DEPT_ORDER = 0         # 同级排序权重（根部门没有同级，取 0）

# 普通用户的 f_usertype 取值；服务号用户（1）不属于组织架构，不补挂到部门。
USER_TYPE_NORMAL = 0

# ---- 存量企业的处理方式 ----
# 部分企业在引入"公司根部门"这个概念之前，就已经用一个顶级部门（形如"XX总部"）
# 事实上充当了公司节点。对这些企业若还在上面新建一层，组织树会凭空多出一层几乎
# 同义的节点，所以改为直接"认领"那个已有部门作为公司根，并把它改名为企业全称。
#
# 这个判断是人工的：脚本没法可靠地区分"XX总部"这种公司节点和"技术部"这种真部门。
# 因此采取白名单制——列在下面的企业走认领，其余企业一律新建一层公司根（更安全，
# 绝不会改动或吞掉一个真实存在的业务部门）。新增企业时按需往里加 ID。
#
# 当前名单依据 tools/check_root_dept.py 的迁移前现状调查（2026-07-31）：
#   企业 1 明远科技有限公司 → 顶级部门"明远科技总部"，是公司节点，认领
#   企业 2 青鸟物流集团     → 顶级部门"青鸟物流总部"，是公司节点，认领
#   企业 3 禾川生物医药     → 顶级部门"禾川生物总部"，是公司节点，认领
#   企业 4 test             → 顶级部门"技术部"，是真部门，不认领，新建一层
CLAIM_EXISTING_ROOT_ENTERPRISE_IDS = {1, 2, 3}


def column_exists(cur, table_name, column_name):
    """判断某表是否已有某列。用于兼容尚未升级过的库。"""
    cur.execute(
        "SELECT COUNT(*) FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = %s AND TABLE_NAME = %s AND COLUMN_NAME = %s",
        (DB_NAME, table_name, column_name))
    return cur.fetchone()[0] > 0


def ensure_root_dept_column(cur):
    """确保 t_enterprise 有 f_root_dept_id 列，没有就补上。"""
    if column_exists(cur, "t_enterprise", "f_root_dept_id"):
        print("t_enterprise.f_root_dept_id 已存在，跳过加列。")
        return

    cur.execute(
        "ALTER TABLE t_enterprise ADD COLUMN f_root_dept_id bigint(20) NOT NULL DEFAULT 0 "
        "COMMENT '公司根部门ID，关联 t_department.f_dept_id；0表示尚未建立'")
    print("已为 t_enterprise 增加 f_root_dept_id 列。")


def alloc_next_dept_id(cur):
    """分配下一个部门业务ID。

    取全局最大 f_dept_id 与 DEPARTMENT_ID_BASE 中的较大者加一，与服务端
    DepartmentManager 的 ++m_baseDeptId 口径一致，避免两边分配出同一个ID。
    """
    cur.execute("SELECT IFNULL(MAX(f_dept_id), 0) FROM t_department")
    max_in_db = cur.fetchone()[0]
    return max(max_in_db, DEPARTMENT_ID_BASE) + 1


def query_top_level_depts(cur, enterprise_id):
    """取某企业当前所有"父部门为 0 且在用"的部门。

    @return [(f_dept_id, f_dept_name), ...]，按排序权重与 ID 排列。
    """
    cur.execute(
        "SELECT f_dept_id, f_dept_name FROM t_department "
        "WHERE f_enterprise_id = %s AND f_parent_dept_id = %s AND f_status = %s "
        "ORDER BY f_dept_order, f_dept_id",
        (enterprise_id, ROOT_PARENT_DEPT_ID, DEPT_STATUS_NORMAL))
    return cur.fetchall()


def claim_existing_root(cur, enterprise_id, enterprise_name):
    """把该企业已有的那个唯一顶级部门认领为公司根部门，并改名为企业全称。

    适用于历史上已经用一个"XX总部"式的顶级部门充当公司节点的企业：它本来就是
    父部门为 0、层级为 1、物化路径为 /自身ID/ 的形态，正是公司根部门要求的样子，
    因此无需新建行，也无需改写任何路径或层级，只要改名并回写企业记录即可。

    @return 认领到的根部门ID；该企业顶级部门不是恰好一个时返回 0（不做猜测）。
    """
    top_depts = query_top_level_depts(cur, enterprise_id)
    if len(top_depts) != 1:
        return 0

    root_dept_id, old_name = top_depts[0]
    cur.execute(
        "UPDATE t_department SET f_dept_name = %s WHERE f_dept_id = %s",
        (enterprise_name, root_dept_id))
    print("    认领已有顶级部门 %d 作为公司根部门，名称由「%s」改为「%s」。"
          % (root_dept_id, old_name, enterprise_name))
    return root_dept_id


def create_new_root(cur, enterprise_id, enterprise_name):
    """在该企业现有部门之上新建一层公司根部门，并把原有部门整体下沉一级。

    适用于顶级部门本身就是真实业务部门的企业：这类部门不能被改名顶替，只能在
    它们上面加一层公司节点。

    @return 二元组 (新根部门ID, 被下沉的部门数)。
    """
    root_dept_id = alloc_next_dept_id(cur)
    root_dept_path = "/%d/" % root_dept_id

    # 1) 插入公司根部门。描述与负责人留空，由管理员日后自行维护。
    cur.execute(
        "INSERT INTO t_department(f_dept_id, f_enterprise_id, f_dept_name, f_parent_dept_id, "
        "f_dept_path, f_dept_level, f_dept_order, f_leader_user_id, f_dept_desc, f_status, f_create_time) "
        "VALUES(%s, %s, %s, %s, %s, %s, %s, 0, '', %s, NOW())",
        (root_dept_id, enterprise_id, enterprise_name, ROOT_PARENT_DEPT_ID, root_dept_path,
         ROOT_DEPT_LEVEL, ROOT_DEPT_ORDER, DEPT_STATUS_NORMAL))

    # 2) 原有部门整体下沉一级：层级加一，物化路径最前面插入新根的ID。
    #    路径原先形如 /1005/2001/，改写后为 /<新根ID>/1005/2001/，等价于
    #    dept::RebasePath(旧路径, "/", "/<新根ID>/")。停用的部门一并处理。
    cur.execute(
        "UPDATE t_department "
        "SET f_dept_level = f_dept_level + 1, "
        "    f_dept_path = CONCAT(%s, f_dept_path) "
        "WHERE f_enterprise_id = %s AND f_dept_id <> %s",
        ("/%d" % root_dept_id, enterprise_id, root_dept_id))
    sunk_count = cur.rowcount

    # 3) 原先的顶级部门改为挂在新根之下。必须排除新根自身，它的父部门就该是 0。
    cur.execute(
        "UPDATE t_department SET f_parent_dept_id = %s "
        "WHERE f_enterprise_id = %s AND f_dept_id <> %s AND f_parent_dept_id = %s",
        (root_dept_id, enterprise_id, root_dept_id, ROOT_PARENT_DEPT_ID))

    print("    新建公司根部门 %d（%s），原有 %d 个部门整体下沉一级。"
          % (root_dept_id, enterprise_name, sunk_count))
    return root_dept_id, sunk_count


def attach_orphan_users(cur, enterprise_id, root_dept_id, has_usertype_column):
    """把该企业下"有企业归属却不属于任何在职部门"的员工补挂到公司根部门。

    这些账号正是历史上"新增员工时部门ID为 0"留下的：登录能用，但在管理端组织
    架构和 IM 客户端通讯录里都看不见。
    @return 被补挂的员工数。
    """
    usertype_filter = ""
    if has_usertype_column:
        usertype_filter = " AND u.f_usertype = %d" % USER_TYPE_NORMAL

    cur.execute(
        "SELECT u.f_user_id FROM t_user u "
        "LEFT JOIN t_department_member m "
        "       ON m.f_user_id = u.f_user_id AND m.f_status = %s "
        "WHERE u.f_enterprise_id = %s AND m.f_user_id IS NULL" + usertype_filter,
        (MEMBER_STATUS_ACTIVE, enterprise_id))
    orphan_ids = [row[0] for row in cur.fetchall()]

    for user_id in orphan_ids:
        # ON DUPLICATE 覆盖的是"该用户曾被移出公司根部门"的历史行：既然他现在没有
        # 任何在职部门，重新置为在职并挂回公司根部门就是本次修复想要的结果。
        cur.execute(
            "INSERT INTO t_department_member(f_dept_id, f_enterprise_id, f_user_id, f_position, "
            "f_is_primary, f_member_order, f_status, f_join_time) "
            "VALUES(%s, %s, %s, '', 1, 0, %s, NOW()) "
            "ON DUPLICATE KEY UPDATE f_status = %s, f_is_primary = 1",
            (root_dept_id, enterprise_id, user_id, MEMBER_STATUS_ACTIVE, MEMBER_STATUS_ACTIVE))

    return len(orphan_ids)


def migrate_one_enterprise(cur, enterprise_id, enterprise_name, has_usertype_column):
    """迁移单个企业：确立其公司根部门，并把孤儿员工补挂到该部门下。

    确立方式有两种，取决于该企业是否在 CLAIM_EXISTING_ROOT_ENTERPRISE_IDS 名单里：
    认领已有的唯一顶级部门，或者在现有部门之上新建一层。名单外的企业一律新建，
    这样绝不会改动一个真实存在的业务部门。
    @return 三元组 (根部门ID, 被下沉的部门数, 被补挂的孤儿员工数)；确立失败时
            根部门ID 为 0，调用方据此判定该企业未完成迁移。
    """
    root_dept_id = 0
    sunk_count = 0

    if enterprise_id in CLAIM_EXISTING_ROOT_ENTERPRISE_IDS:
        root_dept_id = claim_existing_root(cur, enterprise_id, enterprise_name)
        if root_dept_id == 0:
            # 顶级部门不是恰好一个，说明实际数据与名单的前提不符。此时宁可跳过
            # 也不猜：认领错了会把一个真实业务部门改名顶替成公司节点。
            top_depts = query_top_level_depts(cur, enterprise_id)
            print("    跳过：本企业在认领名单里，但顶级在用部门有 %d 个 %s，"
                  "与「恰好一个」的前提不符，请人工确认后再处理。"
                  % (len(top_depts), [name for _, name in top_depts]))
            return 0, 0, 0
    else:
        root_dept_id, sunk_count = create_new_root(cur, enterprise_id, enterprise_name)

    # 回写企业记录，后续判定"某节点是否公司节点"一律以此字段为准。
    cur.execute(
        "UPDATE t_enterprise SET f_root_dept_id = %s WHERE f_enterprise_id = %s",
        (root_dept_id, enterprise_id))

    orphan_count = attach_orphan_users(cur, enterprise_id, root_dept_id, has_usertype_column)
    return root_dept_id, sunk_count, orphan_count


def main():
    conn = pymysql.connect(host=DB_HOST, port=DB_PORT, user=DB_USER,
                           password=DB_PASSWORD, database=DB_NAME,
                           charset="utf8mb4", autocommit=False)
    try:
        cur = conn.cursor()

        ensure_root_dept_column(cur)
        conn.commit()

        has_usertype_column = column_exists(cur, "t_user", "f_usertype")
        if not has_usertype_column:
            print("t_user 无 f_usertype 列，补挂孤儿员工时不区分服务号用户。")

        # 只处理尚未建立根部门的企业，这是本脚本幂等的唯一依据。
        cur.execute(
            "SELECT f_enterprise_id, f_name FROM t_enterprise "
            "WHERE f_root_dept_id = 0 ORDER BY f_enterprise_id")
        pending = cur.fetchall()
        if not pending:
            print("所有企业都已建立公司根部门，无需迁移。")
            return 0

        print("待迁移企业 %d 家。" % len(pending))
        skipped = 0
        for enterprise_id, enterprise_name in pending:
            print("  企业 %d（%s）：" % (enterprise_id, enterprise_name))
            try:
                root_dept_id, sunk_count, orphan_count = migrate_one_enterprise(
                    cur, enterprise_id, enterprise_name, has_usertype_column)
                if root_dept_id == 0:
                    # 未能确立根部门，本企业什么都没改，回滚掉以免留下半截状态。
                    conn.rollback()
                    skipped += 1
                    continue
                conn.commit()
                print("    完成：根部门 %d，下沉部门 %d 个，补挂孤儿员工 %d 人。"
                      % (root_dept_id, sunk_count, orphan_count))
            except Exception as exc:
                # 单个企业失败就回滚这一家，其余企业继续，避免一家的脏数据挡住全部迁移。
                conn.rollback()
                skipped += 1
                print("    失败并已回滚：%s" % exc, file=sys.stderr)

        if skipped:
            print("\n有 %d 家企业未完成迁移，需人工处理后重跑本脚本。" % skipped)
        print("迁移结束。请重启 chatserver，并执行 tools/check_root_dept.py 校验结果。")
        return 1 if skipped else 0
    finally:
        conn.close()


if __name__ == "__main__":
    sys.exit(main())
